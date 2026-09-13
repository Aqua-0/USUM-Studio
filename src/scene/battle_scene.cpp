#include "scene/battle_scene.h"
#include "scene/model_decoder.h"
#include "assets/battle_profile.h"
#include <cmath>
namespace studio {
std::vector<BattleAsset> load_battle_catalog(const std::filesystem::path &dump, bool trainers) {
    Archive archive(dump / (trainers ? TargetProfile::battle_trainers_archive
                                     : TargetProfile::battle_arenas_archive));
    std::vector<BattleAsset> result;
    for (unsigned i = 0; i < archive.size(); ++i) {
        try {
            auto container = Container::parse(archive.decoded(i), trainers ? "CM" : "BG");
            if (container.files.empty() || container.files[0].size() < 4 ||
                u32(container.files[0], 0) != 0x10000)
                continue;
            auto pack = ModelPack::parse(container.files[0]);
            auto model = std::find_if(pack.resources.begin(), pack.resources.end(), [](auto &r) {
                return r.category == 0;
            });
            if (model != pack.resources.end() && (!trainers || model->name.starts_with("tr")))
                result.push_back({i, model->name});
        } catch (const std::exception &) {
        }
    }
    return result;
}
Environment load_battle_stage(const std::filesystem::path &dump,
                              const std::vector<unsigned> &arenas, int trainer) {
    ModelDecoder decoder;
    decoder.keep_skeleton = true;
    auto add = [&](const char *path, unsigned member, bool character) {
        Archive archive(dump / path);
        auto container = Container::parse(archive.decoded(member), character ? "CM" : "BG");
        auto pack = ModelPack::parse(container.files.at(0));
        auto prefix = std::string(character ? "battle-trainer/" : "battle-arena/") +
                      std::to_string(member) + "/";
        decoder.skeletal_motions[prefix] = {SkeletalMotion{}, false};
        for (auto &r : pack.resources)
            if (r.category == 3 || r.category == 4)
                decoder.shader(r.bytes, prefix);
        for (auto &r : pack.resources)
            if (r.category == 1)
                decoder.texture(r.bytes, prefix);
        if (container.files.size() > 1 && !container.files[1].empty()) {
            auto &b = container.files[1];
            if (!character)
                decoder.motion(b, "Arena loop", prefix, false);
            else if (b.size() >= 8) {
                auto count = u32(b, 0);
                auto slot = BattleProfile::trainer_idle_slot;
                if (slot < count) {
                    auto offset = u32(b, 4 + slot * 4);
                    if (offset && offset + 4 < b.size())
                        decoder.motion(slice(b, offset + 4, b.size() - offset - 4), "Trainer idle",
                                       prefix, false);
                }
            }
        }
        auto start = decoder.out.draws.size();
        for (auto &r : pack.resources)
            if (r.category == 0)
                decoder.placed_model(r.bytes, r.name, prefix);
        if (character) {
            auto transform = pose_identity();
            transform[0] = transform[10] = -1;
            transform[3] = -70;
            transform[11] = 500;
            decoder.out.placement_transforms.push_back(transform);
            for (auto i = start; i < decoder.out.draws.size(); ++i)
                decoder.out.draws[i].placement = 0;
        }
    };
    for (auto member : arenas)
        add(TargetProfile::battle_arenas_archive, member, false);
    if (trainer >= 0)
        add(TargetProfile::battle_trainers_archive, unsigned(trainer), true);
    for (auto &motion : decoder.out.material_animations)
        for (unsigned track = 0; track < motion.motion.tracks.size(); ++track)
            for (unsigned material = 0; material < decoder.out.materials.size(); ++material)
                if (decoder.out.materials[material].name == motion.motion.tracks[track].material &&
                    decoder.out.materials[material].resource_scope == motion.texture_prefix)
                    motion.bindings.push_back({track, material});
    require(!decoder.out.draws.empty(), "No battle arena geometry could be decoded");
    return std::move(decoder.out);
}
BattleModelRange append_battle_pokemon(Environment &stage, const Environment &pokemon,
                                       const Environment *shadow) {
    auto shadow_start = stage.draws.size(), shadow_material = stage.materials.size();
    if (shadow) {
        stage.materials.insert(stage.materials.end(), shadow->materials.begin(),
                               shadow->materials.end());
        for (auto draw : shadow->draws) {
            draw.material += shadow_material;
            draw.projected_shadow = true;
            draw.placement = int(stage.placement_transforms.size());
            if (draw.skeleton >= 0)
                draw.skeleton += int(stage.skeletons.size());
            stage.draws.push_back(std::move(draw));
        }
    }
    BattleModelRange r{stage.draws.size(),
                       stage.materials.size(),
                       stage.skeletons.size(),
                       stage.lighting_tables.size(),
                       stage.material_animations.size(),
                       stage.visibility_animations.size()};
    stage.textures.insert(pokemon.textures.begin(), pokemon.textures.end());
    stage.lighting_tables.insert(stage.lighting_tables.end(), pokemon.lighting_tables.begin(),
                                 pokemon.lighting_tables.end());
    stage.materials.insert(stage.materials.end(), pokemon.materials.begin(),
                           pokemon.materials.end());
    stage.skeletons.insert(stage.skeletons.end(), pokemon.skeletons.begin(),
                           pokemon.skeletons.end());
    stage.draws.insert(stage.draws.end(), pokemon.draws.begin(), pokemon.draws.end());
    auto placement = int(stage.placement_transforms.size());
    stage.placement_transforms.push_back(pose_identity());
    for (auto i = r.draws; i < stage.draws.size(); ++i) {
        auto &draw = stage.draws[i];
        draw.material += r.materials;
        if (draw.skeleton >= 0)
            draw.skeleton += int(r.skeletons);
        draw.placement = placement;
    }
    r.shadow_draws = shadow_start;
    r.shadow_count = shadow ? shadow->draws.size() : 0;
    r.shadow_materials = shadow_material;
    return r;
}
void update_battle_pokemon(Environment &stage, const Environment &pokemon,
                           const BattleModelRange &r, const PokemonSettings &s, bool shiny,
                           bool far) {
    float scale = pokemon_adjusted_scale(s), sign = far ? 1.f : -1.f;
    auto transform = pose_identity();
    transform[0] = transform[10] = scale * sign;
    transform[5] = scale;
    transform[11] = (far ? -1.f : 1.f) * TargetProfile::battle_pokemon_distances.at(s.size);
    stage.placement_transforms.back() = transform;
    if (stage.placement_transforms.size() > 1) {
        auto &trainer = stage.placement_transforms[0];
        trainer[0] = trainer[10] = sign;
        trainer[3] = far ? 70.f : -70.f;
        float clearance = -s.bounds[2] * scale + std::abs(transform[11]) + 60;
        trainer[11] = (far ? -1.f : 1.f) * std::max(500.f, clearance);
    }
    for (unsigned i = 0; i < pokemon.skeletons.size(); ++i) {
        stage.skeletons[r.skeletons + i] = pokemon.skeletons[i];
        if (stage.skeletons[r.skeletons + i].parent_skeleton >= 0)
            stage.skeletons[r.skeletons + i].parent_skeleton += int(r.skeletons);
    }
    for (unsigned i = 0; i < pokemon.materials.size(); ++i) {
        auto &material = stage.materials[r.materials + i];
        material = pokemon.materials[i];
        for (auto &table : material.reflection_tables)
            if (table >= 0)
                table += int(r.tables);
        if (shiny)
            for (auto &color : s.shiny)
                if (color.material == material.name)
                    for (unsigned j = 0; j < 6; ++j)
                        if (material.constant_assignments[j] == color.slot)
                            for (unsigned k = 0; k < 4; ++k)
                                material.combiner.stages[j].constant[k] = std::clamp(
                                    material.combiner.stages[j].constant[k] + color.offset[k], 0.f,
                                    1.f);
    }
    for (auto i = r.shadow_materials; i < r.materials; ++i)
        for (auto &main : pokemon.materials)
            if (main.name == stage.materials[i].name)
                for (unsigned slot = 0; slot < 3; ++slot) {
                    stage.materials[i].inputs[slot].transform = main.inputs[slot].transform;
                    update_texture_transform(stage.materials[i].inputs[slot]);
                }
    stage.material_animations.resize(r.material_motions);
    for (auto animation : pokemon.material_animations) {
        for (auto &binding : animation.bindings)
            binding.material += r.materials;
        auto bindings = animation.bindings;
        for (auto binding : bindings)
            if (animation.motion.tracks.at(binding.track).kind ==
                MaterialTrack::Kind::TextureTransform)
                for (auto i = r.shadow_materials; i < r.materials; ++i)
                    if (stage.materials[i].name == stage.materials[binding.material].name) {
                        binding.material = i;
                        animation.bindings.push_back(binding);
                    }
        stage.material_animations.push_back(std::move(animation));
    }
    stage.visibility_animations.resize(r.visibility);
    stage.visibility_animations.insert(stage.visibility_animations.end(),
                                       pokemon.visibility_animations.begin(),
                                       pokemon.visibility_animations.end());
}
void update_battle_shadow(Environment &stage, const BattleModelRange &range, bool far,
                          float elevation, float azimuth) {
    require(std::isfinite(elevation) && std::isfinite(azimuth) && elevation >= 5 && elevation <= 90,
            "Shadow light elevation must be between 5 and 90 degrees");
    auto vertical = elevation * 3.14159265f / 180, around = azimuth * 3.14159265f / 180;
    auto length = std::cos(vertical) / std::sin(vertical);
    for (auto i = range.shadow_draws; i < range.shadow_draws + range.shadow_count; ++i)
        stage.draws.at(i).shadow_projection = {length * std::sin(around),
                                               (far ? -1.f : 1.f) * length * std::cos(around), .5f};
}

}
