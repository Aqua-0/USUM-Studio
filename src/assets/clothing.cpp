#include "assets/clothing.h"
#include "scene/clothing_colors.h"
#include "core/digest.h"
#include "formats/compression.h"
#include <algorithm>
#include <cmath>
namespace studio {
namespace {
void checkpoint(std::atomic_bool *cancel) {
    require(!cancel || !cancel->load(), "Clothing load cancelled");
}
Bytes item_record(const Archive &archive, unsigned item, unsigned part, bool hat, bool short_legs) {
    auto footer = archive.decoded(archive.size() - 1);
    auto count = u32(footer, 0);
    auto row = item * ClothingProfile::variants(part) + (part == 8 ? unsigned(short_legs)
                                                         : (part == 0 || part == 1) ? unsigned(!hat)
                                                                                    : 0);
    require(row < count, "Clothing item is unavailable in this archive");
    auto view = slice(footer, 4 + std::size_t(row) * 8, 8);
    return Bytes(view.begin(), view.end());
}
std::array<float, 3> color(const Container &colors, unsigned type, unsigned index) {
    auto &data = colors.files.at(type);
    require(index < u32(data, 0), "Clothing color is unavailable");
    auto value = slice(data, 4 + std::size_t(index) * (type == 6 ? 8 : 4), 3);
    return {value[0] / 255.f, value[1] / 255.f, value[2] / 255.f};
}
Matrix locator(View bytes) {
    slice(bytes, 0, 36);
    auto result = pose_identity();
    for (unsigned axis = 0; axis < 3; ++axis) {
        float a = f32(bytes, 12 + axis * 4);
        require(std::isfinite(a), "Invalid clothing attachment rotation");
        auto rotation = pose_identity();
        unsigned x = (axis + 1) % 3, y = (axis + 2) % 3;
        rotation[x * 4 + x] = rotation[y * 4 + y] = std::cos(a);
        rotation[x * 4 + y] = -std::sin(a);
        rotation[y * 4 + x] = std::sin(a);
        result = pose_multiply(rotation, result);
    }
    for (unsigned c = 0; c < 3; ++c) {
        auto scale = f32(bytes, c * 4);
        require(std::isfinite(scale), "Invalid clothing attachment scale");
        for (unsigned r = 0; r < 3; ++r)
            result[r * 4 + c] *= scale;
        auto position = f32(bytes, 24 + c * 4);
        require(std::isfinite(position), "Invalid clothing attachment position");
        result[c * 4 + 3] = position;
    }
    return result;
}
}
ClothingColors decode_clothing_palette(View bytes) {
    auto pack = Container::parse(bytes);
    require(pack.files.size() >= 7, "Clothing palette needs seven sections");
    ClothingColors result;
    for (unsigned type = 0; type < 7; ++type) {
        auto &data = pack.files[type];
        auto count = u32(data, 0);
        require(count <= 65536 && 4ull + count * (type == 6 ? 8ull : 4ull) <= data.size(),
                "Invalid clothing palette section");
        for (unsigned i = 0; i < count; ++i)
            result[type].push_back(color(pack, type, i));
    }
    return result;
}
Bytes replace_clothing_color(View bytes, unsigned section, unsigned index,
                             const std::array<float, 3> &rgb) {
    auto colors = decode_clothing_palette(bytes);
    require(section < 7 && index < colors[section].size(), "Unknown clothing palette entry");
    Bytes result(bytes.begin(), bytes.end());
    auto offset = u32(bytes, 4 + section * 4) + 4 + index * (section == 6 ? 8 : 4);
    for (unsigned channel = 0; channel < 3; ++channel) {
        require(std::isfinite(rgb[channel]) && rgb[channel] >= 0 && rgb[channel] <= 1,
                "Palette RGB must be between zero and one");
        result[offset + channel] = std::uint8_t(std::lround(rgb[channel] * 255));
    }
    return result;
}
void ClothingPalette::load(const std::filesystem::path &path) {
    auto bytes = Archive(path).decoded(0);
    decode_clothing_palette(bytes);
    source = std::filesystem::absolute(path);
    original = current = saved = std::move(bytes);
    history = {current};
    cursor = 0;
}
void ClothingPalette::commit() {
    if (history.empty())
        history = {original};
    if (current == history[cursor])
        return;
    history.resize(cursor + 1);
    history.push_back(current);
    ++cursor;
    if (history.size() > 257) {
        history.erase(history.begin());
        --cursor;
    }
}
void ClothingPalette::undo() {
    commit();
    if (cursor)
        current = history[--cursor];
}
void ClothingPalette::redo() {
    if (cursor + 1 < history.size())
        current = history[++cursor];
}
void ClothingPalette::reset() {
    current = original;
    commit();
}
void ClothingPalette::export_archive(const std::filesystem::path &output) const {
    decode_clothing_palette(current);
    require(current.size() == original.size(), "Palette size changed");
    auto baseline = decode_clothing_palette(original);
    auto edited = decode_clothing_palette(current);
    auto expected = original;
    for (unsigned section = 0; section < 7; ++section) {
        require(baseline[section].size() == edited[section].size(), "Palette entry count changed");
        for (unsigned i = 0; i < edited[section].size(); ++i)
            if (baseline[section][i] != edited[section][i])
                expected = replace_clothing_color(expected, section, i, edited[section][i]);
    }
    require(expected == current, "Palette changes unsupported bytes");
    require(std::filesystem::weakly_canonical(source) != std::filesystem::weakly_canonical(output),
            "Choose a separate output archive");
    Archive archive(source);
    require(archive.decoded(0) == original, "Palette source changed; reload before exporting");
    auto raw = archive.raw(0);
    auto bytes = current == original ? raw : current;
    if (current != original && !raw.empty() && (raw[0] == 0x10 || raw[0] == 0x11))
        bytes = compress(bytes);
    archive.export_to(output, {{0, std::move(bytes)}});
}
std::filesystem::path clothing_archive(const std::filesystem::path &dump,
                                       const ClothingSelection &selection, unsigned part) {
    require(selection.profile < 4 && part < ClothingProfile::parts,
            "Unknown clothing profile or part");
    if (!selection.archives[part].empty())
        return selection.archives[part];
    auto path = ClothingProfile::archives[selection.profile][part];
    return *path ? dump / path : std::filesystem::path{};
}
ClothingCatalog load_clothing_catalog(const std::filesystem::path &dump,
                                      const ClothingSelection &selection,
                                      std::atomic_bool *cancel) {
    require(selection.profile < 4, "Unknown clothing profile");
    ClothingCatalog result;
    result.colors = decode_clothing_palette(
        selection.palette.empty() ? Archive(selection.color_archive.empty()
                                                ? dump / ClothingProfile::colors[selection.profile]
                                                : selection.color_archive)
                                        .decoded(0)
                                  : selection.palette);
    for (unsigned part = 0; part < ClothingProfile::parts; ++part) {
        checkpoint(cancel);
        auto path = clothing_archive(dump, selection, part);
        if (path.empty())
            continue;
        try {
            Archive archive(path);
            auto footer = archive.decoded(archive.size() - 1);
            auto count = u32(footer, 0);
            require(count <= 65536 && 4ull + count * 8ull <= footer.size() &&
                        count % ClothingProfile::variants(part) == 0,
                    "Invalid clothing item table");
            std::map<unsigned, std::string> names;
            for (unsigned item = 0; item < count / ClothingProfile::variants(part); ++item) {
                checkpoint(cancel);
                auto row =
                    slice(footer, 4 + std::size_t(item) * ClothingProfile::variants(part) * 8, 8);
                auto member = u16(row, 0);
                if (!names.contains(member)) {
                    std::string name;
                    try {
                        auto pack = Container::parse(archive.decoded(member), "CM");
                        auto model = ModelPack::parse(pack.files.at(0));
                        auto found = std::find_if(model.resources.begin(), model.resources.end(),
                                                  [](auto &r) {
                                                      return r.category == 0;
                                                  });
                        if (found != model.resources.end())
                            name = found->name;
                    } catch (const std::exception &) {
                    }
                    names[member] = std::move(name);
                }
                if (!names[member].empty())
                    result.parts[part].push_back(
                        {item, names[member] + " / item " + std::to_string(item)});
            }
        } catch (const std::exception &e) {
            checkpoint(cancel);
            result.diagnostics.push_back(std::string(ClothingProfile::names[part]) + ": " +
                                         e.what());
        }
    }
    return result;
}
ModelDocument load_clothing(const std::filesystem::path &dump, const ClothingSelection &selection,
                            int isolated_part, std::atomic_bool *cancel) {
    require(selection.profile < 4 && isolated_part >= -1 &&
                isolated_part < int(ClothingProfile::parts),
            "Unknown clothing preview");
    checkpoint(cancel);
    auto body_path = dump / (selection.profile < 2 ? TargetProfile::character_archive
                                                   : TargetProfile::battle_trainers_archive);
    auto base = load_library_model(dump, body_path, ModelCategory::FieldCharacters,
                                   {selection.profile % 2, "Outfit body"}, cancel, "outfit/body/");
    require(!base.scene->skeletons.empty(), "Outfit body skeleton is missing");
    ModelDocument doc;
    doc.kind = ModelAssetKind::ArchiveModel;
    doc.dump = dump;
    doc.clothing = std::make_shared<ClothingSelection>(selection);
    doc.clothing_part = isolated_part;
    doc.name = std::string(selection.profile % 2 ? "Female" : "Male") +
               (selection.profile < 2 ? " overworld" : " battle") +
               (isolated_part < 0 ? " outfit"
                                  : " / " + std::string(ClothingProfile::names[isolated_part]));
    doc.looping_effects = false;
    doc.texture_prefix = "outfit/";
    doc.sources = base.sources;
    doc.resources = base.resources;
    doc.motions = std::move(base.motions);
    doc.scene = std::make_shared<Environment>();
    auto &scene = *doc.scene;
    scene.skeletons.push_back(base.scene->skeletons[0]);
    for (auto &motion : doc.motions) {
        motion.skeleton_motions = {std::make_shared<SkeletalMotion>(motion.skeletal)};
        motion.material = {};
        motion.visibility = {};
    }
    auto color_path = selection.color_archive.empty()
                          ? dump / ClothingProfile::colors[selection.profile]
                          : selection.color_archive;
    Archive color_archive(color_path);
    auto color_bytes = color_archive.decoded(0);
    auto preview_colors = selection.palette.empty() ? color_bytes : selection.palette;
    decode_clothing_palette(preview_colors);
    auto colors = Container::parse(preview_colors);
    doc.sources.push_back({std::filesystem::absolute(color_path), 0, "Outfit colors",
                           sha256(color_bytes), color_bytes});
    auto selected_record = [&](unsigned part, bool hat = true, bool short_legs = false) {
        auto path = clothing_archive(dump, selection, part);
        require(!path.empty() && selection.items[part] >= 0, "Select a clothing item first");
        return item_record(Archive(path), unsigned(selection.items[part]), part, hat, short_legs);
    };
    bool special = selection.items[10] >= 0, hat = selection.items[7] >= 0 && !special,
         short_legs = selection.items[6] >= 0 && (selected_record(6)[6] & 1);
    if (hat && selection.items[1] >= 0 && (selected_record(1)[6] & 16)) {
        hat = false;
        scene.diagnostics.push_back(
            "Selected hair does not support a hat; the hat is hidden in the assembled preview.");
    }
    Bytes hair_locator;
    if (selection.items[1] >= 0) {
        auto record = selected_record(1, hat);
        auto hair = Container::parse(
            Archive(clothing_archive(dump, selection, 1)).decoded(u16(record, 0)), "CM");
        if (hair.files.size() > 2)
            hair_locator = hair.files[2];
    }
    bool sandals = false;
    for (unsigned part = 0; part < ClothingProfile::parts; ++part) {
        checkpoint(cancel);
        if (selection.items[part] < 0 || (isolated_part >= 0 && unsigned(isolated_part) != part))
            continue;
        if (isolated_part < 0 &&
            ((special && part != 0 && part != 5 && part != 10) || (part == 7 && !hat)))
            continue;
        auto path = clothing_archive(dump, selection, part);
        auto record = selected_record(part, hat, short_legs);
        auto prefix = "outfit/" + std::to_string(part) + "/";
        auto model =
            load_library_model(dump, path, ModelCategory::FieldCharacters,
                               {u16(record, 0), ClothingProfile::names[part]}, cancel, prefix);
        auto container = Container::parse(model.sources.at(0).original, "CM");
        if ((part == 2 || part == 3) && model.scene->skeletons.empty()) {
            SceneSkeleton rig;
            rig.placement = rig.inverse_placement = pose_identity();
            Joint joint;
            joint.name = "Accessory";
            joint.scale = {1, 1, 1};
            joint.bind = joint.inverse_bind = pose_identity();
            rig.joints = {joint};
            rig.tracks = {-1};
            model.scene->skeletons.push_back(rig);
            for (auto &draw : model.scene->draws) {
                draw.skeleton = 0;
                draw.palette = {0};
                for (auto &vertex : draw.vertices) {
                    vertex.joints = {};
                    vertex.weights = {1, 0, 0, 0};
                }
            }
        }

        std::array<std::array<float, 3>, 6> tints{
            color(colors, 0, selection.skin),
            std::int8_t(record[4]) < 0 ? std::array<float, 3>{1, 1, 1}
                                       : color(colors, record[4], record[5]),
            color(colors, 2, selection.hair),
            color(colors, 3, selection.eyes),
            color(colors, 4, selection.hair),
            selection.lip ? color(colors, 5, selection.lip) : color(colors, 0, selection.skin)};
        apply_clothing_colors(model.scene->textures, container, tints, prefix);
        std::size_t source_start = doc.sources.size(), resource_start = doc.resources.size(),
                    material_start = scene.materials.size(), rig_start = scene.skeletons.size(),
                    table_start = scene.lighting_tables.size();
        doc.sources.insert(doc.sources.end(), model.sources.begin(), model.sources.end());
        for (auto link : model.resources) {
            link.source += source_start;
            doc.resources.push_back(std::move(link));
        }
        for (auto &[name, index] : model.texture_resources)
            doc.texture_resources[name] = index + resource_start;
        Archive archive(path);
        auto footer = archive.decoded(archive.size() - 1);
        doc.sources.push_back({std::filesystem::absolute(path), archive.size() - 1,
                               std::string(ClothingProfile::names[part]) + " item table",
                               sha256(footer), std::move(footer)});
        for (auto rig : model.scene->skeletons) {
            std::string attach;
            if (!rig.joints.empty() && rig.joints[0].name.ends_with("_attach"))
                attach = rig.joints[0].name.substr(0, rig.joints[0].name.size() - 7);
            if (part == 2)
                attach = "AccEye";
            if (part == 3)
                attach = "Head";
            if (!attach.empty()) {
                auto &joints = scene.skeletons[0].joints;
                auto joint = std::find_if(joints.begin(), joints.end(), [&](auto &j) {
                    return j.name == attach;
                });
                require(joint != joints.end(), "Clothing attachment joint is missing: " + attach);
                rig.parent_skeleton = 0;
                rig.parent_joint = int(joint - joints.begin());
                if (part == 3) {
                    if (record[6] & 8) {
                        rig.attachment_transform = joint->inverse_bind;
                        rig.attachment_transform[3] = rig.attachment_transform[7] =
                            rig.attachment_transform[11] = 0;
                    } else if (hair_locator.size() >= 36)
                        rig.attachment_transform = locator(hair_locator);
                }
            }
            for (auto &motion : doc.motions) {
                auto local = std::find_if(model.motions.begin(), model.motions.end(), [&](auto &m) {
                    return m.slot == motion.slot && m.error.empty();
                });
                motion.skeleton_motions.push_back(
                    attach.empty() ? motion.skeleton_motions[0]
                    : local == model.motions.end()
                        ? nullptr
                        : std::make_shared<SkeletalMotion>(local->skeletal));
            }
            scene.skeletons.push_back(std::move(rig));
        }
        for (auto material : model.scene->materials) {
            for (auto &table : material.reflection_tables)
                if (table >= 0)
                    table += int(table_start);
            scene.materials.push_back(std::move(material));
        }
        for (auto index : model.material_resources)
            doc.material_resources.push_back(index + resource_start);
        for (unsigned i = 0; i < model.scene->draws.size(); ++i) {
            auto draw = model.scene->draws[i];
            if (part == 1 && hat && draw.mesh.find("nohat") != std::string::npos)
                continue;
            draw.material += material_start;
            if (draw.skeleton >= 0)
                draw.skeleton += int(rig_start);
            scene.draws.push_back(std::move(draw));
            doc.draw_resources.push_back(model.draw_resources[i] + resource_start);
        }
        scene.textures.insert(model.scene->textures.begin(), model.scene->textures.end());
        scene.lighting_tables.insert(scene.lighting_tables.end(),
                                     model.scene->lighting_tables.begin(),
                                     model.scene->lighting_tables.end());
        for (auto &motion : doc.motions) {
            auto local = std::find_if(model.motions.begin(), model.motions.end(), [&](auto &m) {
                return m.slot == motion.slot && m.error.empty();
            });
            if (local == model.motions.end())
                continue;
            if (!local->material.tracks.empty()) {
                MaterialAnimation animation{local->name, prefix, local->material, false, {}};
                for (unsigned t = 0; t < animation.motion.tracks.size(); ++t)
                    for (unsigned m = 0; m < model.scene->materials.size(); ++m)
                        if (model.scene->materials[m].name == animation.motion.tracks[t].material)
                            animation.bindings.push_back({t, m + material_start});
                motion.material_channels.push_back(std::move(animation));
            }
            if (!local->visibility.tracks.empty())
                motion.visibility_channels.push_back({prefix, local->visibility, false});
        }
        if (part == 9)
            sandals = (record[6] & 4) != 0;
    }
    if (sandals) {
        auto legs = std::find_if(scene.textures.begin(), scene.textures.end(), [](auto &t) {
            return t.first.starts_with("outfit/8/") && t.first.find("_legs_") != std::string::npos;
        });
        if (legs != scene.textures.end())
            for (auto &material : scene.materials)
                if (material.resource_scope == "outfit/9/") {
                    for (auto &name : material.texture_inputs)
                        if (name.find("_legs_") != std::string::npos)
                            name = legs->first;
                    material.texture = material.texture_inputs[0];
                }
    }
    require(!scene.draws.empty(), "Select a clothing item to preview");
    scene.source_bytes = 0;
    for (auto &source : doc.sources)
        scene.source_bytes += source.original.size();
    scene.diagnostics.push_back(
        "Outfit preview only. Item colors, hat/hair and leg variants are resolved from clothing "
        "tables. Palette edits can be exported from the Clothing browser. Geometry and item-table "
        "editing are not available.");
    if (special && isolated_part < 0)
        scene.diagnostics.push_back(
            "Special outfit replaces ordinary clothing; face and bracelet are retained.");
    doc.select_motion(-1);
    return doc;
}
}
