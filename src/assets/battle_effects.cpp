#include "assets/battle_effects.h"
#include "assets/battle_profile.h"
#include "assets/material_document.h"
#include "core/digest.h"
#include "scene/model_decoder.h"
#include <algorithm>
namespace studio {
namespace {
EffectResourceKind kind(View b) {
    if (b.size() < 4)
        return EffectResourceKind::Unknown;
    if (b.size() >= 24 && text(slice(b, 16, 8)) == "shader")
        return EffectResourceKind::Shader;
    switch (u32(b, 0)) {
    case 0x10000:
        return EffectResourceKind::Pack;
    case 0x15122117:
        return EffectResourceKind::Model;
    case 0x15041213:
        return EffectResourceKind::Texture;
    case 0x60000:
        return EffectResourceKind::Motion;
    case 0x44425053:
        return EffectResourceKind::Particle;
    default:
        break;
    }
    if (text(slice(b, 0, 4)) == "GFBE")
        return EffectResourceKind::Environment;
    if (b[0] == 'P' && b[1] == 'C')
        return EffectResourceKind::Pack;
    return EffectResourceKind::Unknown;
}
void visit(View bytes, EffectResource entry, std::vector<EffectResource> &out,
           std::atomic_bool *cancel) {
    require(!cancel || !cancel->load(), "Effect catalog cancelled");
    require(entry.path.size() <= 12, "Effect container nesting exceeds supported depth");
    entry.kind = kind(bytes);
    entry.bytes = bytes.size();
    if (entry.name.empty()) {
        if (entry.kind == EffectResourceKind::Texture && bytes.size() >= 104)
            entry.name = text(slice(bytes, 40, 64));
        if (entry.kind == EffectResourceKind::Shader && bytes.size() >= 96)
            entry.name = text(slice(bytes, 32, 64));
        if (entry.name.empty())
            entry.name = effect_resource_kind(entry.kind);
    }
    auto index = out.size();
    out.push_back(entry);
    if (entry.kind != EffectResourceKind::Pack)
        return;
    try {
        if (u32(bytes, 0) == 0x10000) {
            auto pack = ModelPack::parse(bytes);
            for (const auto &resource : pack.resources)
                if (resource.category == 0) {
                    out[index].name = resource.name;
                    break;
                }
            for (std::size_t i = 0; i < pack.resources.size(); ++i) {
                auto child = entry;
                child.path.push_back(i);
                child.name = pack.resources[i].name;
                visit(pack.resources[i].bytes, child, out, cancel);
            }
        } else {
            auto pack = Container::parse(bytes, "PC");
            for (std::size_t i = 0; i < pack.files.size(); ++i) {
                if (pack.files[i].empty())
                    continue;
                auto child = entry;
                child.path.push_back(i);
                child.name.clear();
                visit(pack.files[i], child, out, cancel);
            }
        }
    } catch (const std::exception &e) {
        require(!cancel || !cancel->load(), "Effect catalog cancelled");
        out[index].error = e.what();
    }
}
}
const char *effect_resource_kind(EffectResourceKind kind) {
    static const char *names[] = {
        "Model pack",           "Model",       "Texture", "Motion", "Particles", "Shader",
        "Camera / environment", "Unrecognized"};
    return names[unsigned(kind)];
}
std::vector<EffectResource> load_effect_catalog(const std::filesystem::path &dump,
                                                std::atomic_bool *cancel) {
    Archive archive(dump / BattleProfile::effects_archive);
    std::vector<EffectResource> out;
    for (std::size_t i = 0; i < archive.size(); ++i)
        for (auto sub : archive.subfiles(i)) {
            require(!cancel || !cancel->load(), "Effect catalog cancelled");
            EffectResource entry;
            entry.member = i;
            entry.subfile = sub;
            try {
                visit(archive.decoded(i, sub), entry, out, cancel);
            } catch (const std::exception &e) {
                require(!cancel || !cancel->load(), "Effect catalog cancelled");
                entry.name = "Unreadable resource";
                entry.error = e.what();
                out.push_back(entry);
            }
        }
    return out;
}
Bytes read_effect_resource(const std::filesystem::path &dump, const EffectResource &resource) {
    return asset_resource(read_archive_subfile(dump / BattleProfile::effects_archive,
                                               resource.member, resource.subfile),
                          resource.path);
}
ModelDocument load_effect_model(const std::filesystem::path &dump, std::size_t member,
                                unsigned subfile, const std::vector<std::size_t> &selected,
                                const std::vector<EffectMotionSource> &motions,
                                std::atomic_bool *cancel,
                                const std::map<std::size_t, Bytes> &replacements) {
    ModelDocument doc;
    doc.kind = ModelAssetKind::ArchiveModel;
    doc.battle_effect = true;
    doc.dump = dump;
    doc.looping_effects = false;
    doc.effect_motions = motions;
    Archive archive(dump / BattleProfile::effects_archive);
    auto read = [&](std::size_t index, unsigned sub) {
        auto found = replacements.find(index);
        return found == replacements.end() ? archive.decoded(index, sub) : found->second;
    };
    auto bytes = read(member, subfile);
    doc.sources.push_back(
        {BattleProfile::effects_archive, member, "Battle effect", sha256(bytes), bytes, subfile});
    std::vector<EffectResource> resources;
    EffectResource root;
    root.member = member;
    root.subfile = subfile;
    visit(bytes, root, resources, cancel);
    ModelDecoder decoder;
    decoder.cancel = cancel;
    decoder.keep_skeleton = true;
    decoder.skeletal_motions[doc.texture_prefix] = {SkeletalMotion{}, false};
    decoder.out.source_bytes = bytes.size();
    for (auto &resource : resources) {
        if (resource.kind == EffectResourceKind::Pack)
            continue;
        auto data = asset_resource(bytes, resource.path);
        auto index = doc.resources.size();
        doc.resources.push_back(
            {0, resource.path, effect_resource_kind(resource.kind), resource.name});
        if (resource.kind == EffectResourceKind::Shader)
            decoder.shader(data, doc.texture_prefix);
        if (resource.kind == EffectResourceKind::Texture) {
            decoder.texture(data, doc.texture_prefix);
            doc.texture_resources[doc.texture_prefix + text(slice(data, 40, 64))] = index;
        }
    }
    bool has_meshes = false;
    for (std::size_t i = 0; i < doc.resources.size(); ++i) {
        auto &link = doc.resources[i];
        if (link.role != "Model" || (!selected.empty() && link.path != selected))
            continue;
        auto data = asset_resource(bytes, link.path);
        auto parsed = Model::parse(data);
        has_meshes |=
            std::any_of(parsed.sections.begin(), parsed.sections.end(), [](const auto &section) {
                return section.kind == "mesh";
            });
        decoder.placed_model(data, link.name, doc.texture_prefix);
        doc.draw_resources.resize(decoder.out.draws.size(), i);
        doc.material_resources.resize(decoder.out.materials.size(), i);
        if (doc.name.empty())
            doc.name = link.name;
    }
    if (decoder.out.draws.empty()) {
        require(has_meshes,
                "This is a transform-only resource. It has no mesh to preview or edit.");
        std::string reason = "This model has no renderable meshes";
        for (const auto &diagnostic : decoder.out.diagnostics)
            reason += "\n" + diagnostic;
        throw std::runtime_error(reason);
    }
    for (const auto &dependency : motions) {
        auto found = std::find_if(doc.sources.begin(), doc.sources.end(), [&](const auto &source) {
            return source.member == dependency.member;
        });
        std::size_t source = std::size_t(found - doc.sources.begin());
        if (found == doc.sources.end()) {
            auto data = read(dependency.member, dependency.subfile);
            doc.sources.push_back({BattleProfile::effects_archive, dependency.member, "Motion",
                                   sha256(data), std::move(data), dependency.subfile});
        } else
            require(found->subfile == dependency.subfile,
                    "A model session can use one subfile per archive entry");
        auto data = asset_resource(doc.sources[source].original, dependency.path);
        require(kind(data) == EffectResourceKind::Motion, "Selected dependency is not a motion");
        bool existing =
            std::any_of(doc.resources.begin(), doc.resources.end(), [&](const auto &link) {
                return link.source == source && link.path == dependency.path;
            });
        if (!existing) {
            auto name = "Motion " + std::to_string(dependency.member) + ":" +
                        std::to_string(dependency.subfile);
            for (auto child : dependency.path)
                name += "/" + std::to_string(child);
            doc.resources.push_back({source, dependency.path, "Motion", name});
        }
    }
    for (std::size_t i = 0; i < doc.resources.size(); ++i) {
        const auto &link = doc.resources[i];
        if (link.role != "Motion")
            continue;
        AssetMotion motion;
        motion.name = link.name;
        motion.resource = i;
        motion.slot = unsigned(doc.motions.size());
        try {
            auto data = asset_resource(doc.sources.at(link.source).original, link.path);
            motion.skeletal = decode_skeletal_motion(data);
            motion.material = decode_material_motion(data);
            motion.visibility = decode_visibility_motion(data);
        } catch (const std::exception &e) {
            motion.error = e.what();
        }
        doc.motions.push_back(std::move(motion));
    }
    decoder.note("Standalone battle-effect preview. Sequence timing, attachment and lighting are "
                 "not applied.");
    doc.scene = std::make_shared<Environment>(std::move(decoder.out));
    doc.select_motion(-1);
    return doc;
}
ModelDocument load_effect_particles(const std::filesystem::path &dump,
                                    const EffectResource &resource) {
    ModelDocument doc;
    doc.kind = ModelAssetKind::ArchiveModel;
    doc.dump = dump;
    doc.name = resource.name;
    doc.scene = std::make_shared<Environment>();
    auto bytes = read_effect_resource(dump, resource);
    auto emitters = decode_particle_emitters(bytes, doc.scene->diagnostics);
    require(!emitters.empty(), "No supported emitters in this particle resource");
    doc.scene->weather_particles.push_back({1, std::move(emitters)});
    doc.scene->diagnostics.push_back("Particle preview only. Child particles, force fields and "
                                     "trails are not reproduced; particle editing is unavailable.");
    return doc;
}
}
