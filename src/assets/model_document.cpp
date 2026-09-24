#include "assets/model_document.h"
#include "assets/pokemon_motions.h"
#include "assets/pokemon_shadow.h"
#include "scene/model_decoder.h"
#include "core/digest.h"
#include <sstream>
#include <functional>
namespace studio {
void ModelDocument::select_motion(int index, bool repeat) {
    if (area >= 0) {
        require(index >= -1 && (index < 0 || std::size_t(index) < motions.size()),
                "Map motion index is out of range");
        motion = index;
        return;
    }
    require(index >= -1 && (index < 0 || std::size_t(index) < motions.size()),
            "Motion index is out of range");
    if (index >= 0)
        require(motions[index].error.empty(), motions[index].error);
    motion = index;
    looping_overlay = -1;
    scene->material_animations.clear();
    scene->visibility_animations.clear();
    auto bind_joints = [](const auto &joints, const SkeletalMotion &motion) {
        std::vector<int> tracks;
        for (auto &j : joints) {
            auto it = std::find_if(motion.tracks.begin(), motion.tracks.end(), [&](const auto &t) {
                return t.name == j.name;
            });
            tracks.push_back(it == motion.tracks.end() ? -1 : int(it - motion.tracks.begin()));
        }
        return tracks;
    };
    for (auto &rig : scene->skeletons) {
        rig.overlays.clear();
        auto rig_index = std::size_t(&rig - scene->skeletons.data());
        rig.motion = index < 0 ? SkeletalMotion{}
                     : rig_index < motions[index].skeleton_motions.size()
                         ? (motions[index].skeleton_motions[rig_index]
                                ? *motions[index].skeleton_motions[rig_index]
                                : SkeletalMotion{})
                         : motions[index].skeletal;
        rig.motion.looping = repeat;
        rig.tracks = bind_joints(rig.joints, rig.motion);
    }
    if (index < 0)
        return;
    auto append_channels = [&](const AssetMotion &m, bool loop, bool overlay = false) {
        if (!m.material.tracks.empty()) {
            MaterialAnimation a{m.name, texture_prefix, m.material, false, {}};
            a.motion.looping = loop;
            for (unsigned t = 0; t < a.motion.tracks.size(); ++t)
                for (unsigned i = 0; i < scene->materials.size(); ++i)
                    if (scene->materials[i].name == a.motion.tracks[t].material)
                        a.bindings.push_back({t, i});
            scene->material_animations.push_back(std::move(a));
        }
        if (!m.visibility.tracks.empty()) {
            auto v = m.visibility;
            v.clock.looping = loop;
            scene->visibility_animations.push_back({texture_prefix, std::move(v), false, overlay});
        }
    };
    auto &main = motions[index];
    append_channels(main, repeat);
    for (auto channel : main.material_channels) {
        channel.motion.looping = repeat;
        scene->material_animations.push_back(std::move(channel));
    }
    for (auto channel : main.visibility_channels) {
        channel.motion.clock.looping = repeat;
        scene->visibility_animations.push_back(std::move(channel));
    }
    if (!is_pokemon() || !looping_effects || main.group >= pokemon_main_motion_counts.size() ||
        main.slot >= pokemon_main_motion_counts[main.group] ||
        pokemon.species == TargetProfile::pokemon_fixed_markings_species)
        return;
    auto first = pokemon_main_motion_counts[main.group] + 7;
    auto overlay = std::find_if(motions.begin(), motions.end(), [&](const auto &m) {
        return m.group == main.group && m.slot == first && m.error.empty();
    });
    if (overlay == motions.end() ||
        (overlay->skeletal.tracks.empty() && overlay->material.tracks.empty() &&
         overlay->visibility.tracks.empty()))
        return;
    looping_overlay = int(overlay - motions.begin());
    append_channels(*overlay, true, true);
    if (!overlay->skeletal.tracks.empty())
        for (auto &rig : scene->skeletons) {
            SkeletalLayer layer{overlay->skeletal, bind_joints(rig.joints, overlay->skeletal)};
            layer.motion.looping = true;
            rig.overlays.push_back(std::move(layer));
        }
}
static ModelDocument load_pokemon_impl(const std::filesystem::path &dump, const PokemonEntry &entry,
                                       bool shiny, std::atomic_bool *cancel,
                                       const std::map<std::size_t, Bytes> &overrides,
                                       const ArchiveSources &archives, bool shadow_model) {
    ModelDocument doc;
    doc.shadow_model = shadow_model;
    doc.dump = dump;
    doc.archive_sources = archives;
    doc.pokemon = entry;
    doc.name = entry.label + (shadow_model ? " / Shadow" : "");
    doc.shiny = shiny;
    std::unique_ptr<Archive> archive;
    ModelDecoder decoder;
    decoder.cancel = cancel;
    decoder.keep_skeleton = true;
    decoder.skeletal_motions["model/"] = {SkeletalMotion{}, false};
    auto decoded = [&](std::size_t index) {
        auto it = overrides.find(index);
        if (it != overrides.end()) return it->second;
        if (!archive) archive = std::make_unique<Archive>(archives.resolve(dump, TargetProfile::pokemon_archive));
        return archive->decoded(index);
    };
    auto management = decoded(0);
    doc.sources.push_back(
        {TargetProfile::pokemon_archive, 0, "Catalog", sha256(management), std::move(management)});
    auto member = [&](unsigned index, const std::string &role) {
        decoder.checkpoint();
        auto bytes = decoded(index);
        auto source = doc.sources.size();
        doc.sources.push_back({TargetProfile::pokemon_archive, index, role, sha256(bytes), bytes});
        decoder.out.source_bytes += bytes.size();
        return std::pair{source, Container::parse(bytes, "PC")};
    };
    auto link = [&](std::size_t source, unsigned child, const std::string &role,
                    const std::string &name) {
        doc.resources.push_back({source, {child}, role, name});
        return doc.resources.size() - 1;
    };
    auto [model_source, model] = member(entry.model_member, "Model and shaders");
    require(!model.files.empty(), "Missing Pokemon model");
    doc.has_shadow_model = model.files.size() > 1 && !model.files[1].empty();
    require(!shadow_model || doc.has_shadow_model, "This Pokemon has no shadow model");
    if (shadow_model)
        validate_pokemon_shadow(model.files[0], model.files[1]);
    std::function<void(View, std::vector<std::size_t>)> shaders =
        [&](View bytes, std::vector<std::size_t> path) {
            if (bytes.empty())
                return;
            try {
                auto shader = decode_material_shader(bytes);
                doc.resources.push_back({model_source, path, "Shader", shader.name});
                decoder.shader(bytes, "model/");
                ++decoder.out.shader_resources;
                return;
            } catch (const std::exception &) {
            }
            if (bytes.size() >= 4 &&
                ((bytes[0] == 'P' && bytes[1] == 'S') || (bytes[0] == 'P' && bytes[1] == 'C'))) {
                auto nested = Container::parse(bytes);
                for (unsigned i = 0; i < nested.files.size(); ++i) {
                    auto child = path;
                    child.push_back(i);
                    shaders(nested.files[i], child);
                }
            } else
                doc.resources.push_back({model_source, path, "Auxiliary", "Preserved resource"});
        };
    for (unsigned i = 1; i < model.files.size(); ++i)
        if (i == (shadow_model ? 3u : 2u))
            shaders(model.files[i], {i});
        else
            link(model_source, i, "Auxiliary", i == 1 ? "Shadow model" : "Preserved resource");
    auto [texture_source, textures] = member(entry.texture_member + (shiny ? 2 : 1),
                                             shiny ? "Shiny textures" : "Normal textures");
    for (unsigned i = 0; i < textures.files.size(); ++i)
        if (!textures.files[i].empty()) {
            auto &b = textures.files[i];
            auto name = text(slice(b, 40, 64));
            auto resource = link(texture_source, i, "Texture", name);
            doc.texture_resources["model/" + name] = resource;
            decoder.texture(b, "model/");
        }
    auto model_child = shadow_model ? 1u : 0u;
    auto model_resource = link(model_source, model_child, "Model", doc.name);
    decoder.placed_model(model.files[model_child], doc.name, "model/");
    require(!decoder.out.draws.empty(), "Pokemon model has no supported geometry");
    if (shadow_model) {
        auto shared = SkinnedModel::parse(model.files[0]).joints;
        for (auto &rig : decoder.out.skeletons) {
            rig.joints = shared;
            rig.tracks.assign(shared.size(), -1);
        }
    }
    doc.draw_resources.assign(decoder.out.draws.size(), model_resource);
    doc.material_resources.assign(decoder.out.materials.size(), model_resource);
    for (unsigned group = 0; group < 4; ++group) {
        auto [source, pack] =
            member(entry.motion_member + TargetProfile::pokemon_motion_slots[group],
                   std::string(TargetProfile::pokemon_motion_names[group]) + " motions");
        for (unsigned slot = 0; slot < pack.files.size(); ++slot)
            if (!pack.files[slot].empty()) {
                decoder.checkpoint();
                if (pack.files[slot].size() < 4 || u32(pack.files[slot], 0) != 0x60000) {
                    link(source, slot, "Auxiliary", "Motion-set resource " + std::to_string(slot));
                    continue;
                }
                AssetMotion motion;
                motion.group = group;
                motion.slot = slot;
                motion.name = pokemon_motion_label(group, slot);
                motion.resource = link(source, slot, "Motion", motion.name);
                try {
                    motion.skeletal = decode_skeletal_motion(pack.files[slot]);
                    motion.material = decode_material_motion(pack.files[slot]);
                    motion.visibility = decode_visibility_motion(pack.files[slot]);
                } catch (const std::exception &e) {
                    motion.error = e.what();
                }
                doc.motions.push_back(std::move(motion));
            }
    }
    try {
        auto [source, pack] =
            member(entry.texture_member + TargetProfile::pokemon_refresh_texture_slot,
                   "Refresh interaction regions");
        doc.refresh_regions = std::make_shared<RefreshRegionPack>(
            decode_refresh_regions(doc.sources[source].original));
        link(source, 0, "Refresh material rules", "Interaction surface rules");
        for (auto &mask : doc.refresh_regions->masks)
            doc.refresh_resources.push_back(
                link(source, unsigned(mask.child), "Refresh mask", mask.texture));
    } catch (const std::exception &e) {
        if (cancel && cancel->load())
            throw;
        doc.refresh_error = e.what();
        decoder.note("Refresh regions unavailable: " + doc.refresh_error);
    }
    try {
        Archive parameters(archives.resolve(dump, TargetProfile::refresh_parameters_archive));
        auto options = parameters.decoded(TargetProfile::refresh_options_member);
        auto cameras = parameters.decoded(TargetProfile::refresh_cameras_member);
        doc.refresh_feeding = std::make_shared<RefreshFeedingData>(decode_refresh_feeding(
            options, cameras, entry.species, entry.form, unsigned(entry.female)));
    } catch (const std::exception &e) {
        doc.feeding_error = e.what();
    }
    for (std::size_t i = 0; i < decoder.out.draws.size(); ++i)
        doc.native_meshes.push_back(i);
    std::stable_sort(doc.native_meshes.begin(), doc.native_meshes.end(), [&](auto a, auto b) {
        auto &x = decoder.out.materials[decoder.out.draws[a].material];
        auto &y = decoder.out.materials[decoder.out.draws[b].material];
        return x.layer != y.layer ? x.layer < y.layer : x.priority < y.priority;
    });
    auto native_draws = std::move(decoder.out.draws);
    for (auto i : doc.native_meshes)
        decoder.out.draws.push_back(std::move(native_draws[i]));
    decoder.out.low.fill(std::numeric_limits<float>::max());
    decoder.out.high.fill(std::numeric_limits<float>::lowest());
    for (auto &d : decoder.out.draws)
        for (auto &v : d.vertices) {
            std::array<float, 3> p{v.x, v.y, v.z};
            for (unsigned k = 0; k < 3; ++k) {
                decoder.out.low[k] = std::min(decoder.out.low[k], p[k]);
                decoder.out.high[k] = std::max(decoder.out.high[k], p[k]);
            }
        }
    decoder.note("Preview lighting is adjustable and is not saved into game materials. Native "
                 "vertex programs and some lighting lookup modes remain approximate.");
    doc.scene = std::make_shared<Environment>(std::move(decoder.out));
    auto first = std::find_if(doc.motions.begin(), doc.motions.end(), [](auto &m) {
        return m.error.empty() && !m.skeletal.tracks.empty();
    });
    doc.select_motion(first == doc.motions.end() ? -1 : int(first - doc.motions.begin()));
    return doc;
}
ModelDocument load_pokemon(const std::filesystem::path &dump, const PokemonEntry &entry, bool shiny,
                           std::atomic_bool *cancel, const ArchiveSources &archives,
                           bool shadow_model) {
    return load_pokemon_impl(dump, entry, shiny, cancel, {}, archives, shadow_model);
}
ModelDocument reload_pokemon(const ModelDocument &source,
                             const std::map<std::size_t, Bytes> &members) {
    require(source.is_pokemon(), "Effect bundles currently require a Pokemon model");
    std::map<std::size_t, Bytes> resources;
    for (auto &member : source.sources)
        resources[member.member] = member.original;
    for (auto &[index, bytes] : members)
        resources[index] = bytes;
    auto result = load_pokemon_impl(source.dump, source.pokemon, source.shiny, nullptr, resources,
                                    source.archive_sources, source.shadow_model);
    for (auto &[index, bytes] : members)
        if (std::none_of(result.sources.begin(), result.sources.end(), [&](auto &member) {
                return member.member == index;
            }))
            result.sources.push_back(
                {TargetProfile::pokemon_archive, index, "Effect resources", sha256(bytes), bytes});
    result.independent_asset = source.independent_asset;
    result.refresh_feeding = source.refresh_feeding;
    result.feeding_error = source.feeding_error;
    result.looping_effects = source.looping_effects;
    if (source.motion >= 0) {
        auto &previous = source.motions[source.motion];
        auto it = std::find_if(result.motions.begin(), result.motions.end(), [&](auto &motion) {
            return motion.group == previous.group && motion.slot == previous.slot;
        });
        result.select_motion(it == result.motions.end() ? -1 : int(it - result.motions.begin()));
    } else
        result.select_motion(-1);
    return result;
}
std::string ModelDocument::report() const {
    std::ostringstream out;
    out << "Model: " << name << "\nShiny: " << shiny << "\nMotion slots: " << motions.size()
        << "\nSource members: " << sources.size() << '\n';
    for (auto &s : sources)
        out << s.role << ": " << s.archive.generic_string() << " ("
            << archive_sources.resolve(dump, s.archive).string() << ") member " << s.member
            << " sha256 " << s.hash << '\n';
    return out.str() + scene->report();
}
}
