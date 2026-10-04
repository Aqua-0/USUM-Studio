#include "scene/object_relationships.h"
#include <algorithm>
#include <sstream>
#include <map>
namespace studio {
int object_region(const Environment &scene, int index) {
    if (index < 0 || std::size_t(index) >= scene.draws.size()) return -1;
    const auto &draw = scene.draws[index];
    if (draw.preview_only) return -1;
    unsigned category = 0, zone = 0, row = 0;
    bool character = draw.scope.starts_with("character/");
    if (character) {
        auto scope = draw.scope;
        std::replace(scope.begin(), scope.end(), '/', ' ');
        std::istringstream input(scope);
        std::string prefix;
        if (!(input >> prefix >> category >> zone >> row)) return -1;
    }
    for (std::size_t i = 0; i < scene.spatial.regions.size(); ++i) {
        const auto &region = scene.spatial.regions[i];
        if (character && region.overworld && region.overworld->category == category &&
            region.overworld->local_zone == zone && region.overworld->row == row) return int(i);
        if (!character && draw.placement >= 0 && region.kind == SpatialKind::Placement &&
            region.placement == draw.placement) return int(i);
    }
    return -1;
}
int object_draw(const Environment &scene, int region) {
    if (region < 0 || std::size_t(region) >= scene.spatial.regions.size()) return -1;
    for (std::size_t i = 0; i < scene.draws.size(); ++i)
        if (object_region(scene, int(i)) == region) return int(i);
    return -1;
}
std::vector<ObjectLink> object_relationships(const Environment &scene, int draw, int region) {
    std::vector<int> draw_regions(scene.draws.size(), -1), region_draws(scene.spatial.regions.size(), -1);
    for (std::size_t i = 0; i < scene.draws.size(); ++i) {
        int target = draw_regions[i] = object_region(scene, int(i));
        if (target >= 0 && region_draws[target] < 0) region_draws[target] = int(i);
    }
    if (region < 0 || std::size_t(region) >= scene.spatial.regions.size()) region = -1;
    if (draw < 0 || std::size_t(draw) >= scene.draws.size()) draw = region >= 0 ? region_draws[region] : -1;
    if (region < 0 && draw >= 0) region = draw_regions[draw];
    const auto *selected = draw >= 0 ? &scene.draws[draw] : nullptr;
    const auto *spatial = region >= 0 ? &scene.spatial.regions[region] : nullptr;
    const auto *reference = spatial && spatial->overworld ? spatial->overworld.get() : nullptr;
    std::map<std::string, ObjectLink> links;
    auto add = [&](int target_draw, int target_region) -> ObjectLink & {
        const auto *mesh = target_draw >= 0 ? &scene.draws[target_draw] : nullptr;
        auto key = target_region >= 0 ? "region/" + std::to_string(target_region)
                 : mesh->placement >= 0 ? "placement/" + std::to_string(mesh->placement)
                 : mesh->scope.empty() ? "draw/" + std::to_string(target_draw) : "scope/" + mesh->scope;
        auto [it, inserted] = links.try_emplace(key);
        if (inserted) {
            auto &link = it->second;
            link.draw = target_draw;
            link.region = target_region;
            link.label = target_region >= 0 ? scene.spatial.regions[target_region].name
                                            : mesh->name.substr(0, mesh->name.find(" / "));
        }
        return it->second;
    };
    if (selected && selected->source)
        for (std::size_t i = 0; i < scene.draws.size(); ++i) {
            const auto &other = scene.draws[i];
            if (int(i) == draw || other.preview_only || !other.source) continue;
            int target = draw_regions[i];
            if ((region >= 0 && target == region) ||
                (selected->placement >= 0 && selected->placement == other.placement) ||
                (!selected->scope.empty() && selected->scope == other.scope &&
                 selected->placement == other.placement)) continue;
            const auto &a = *selected->source, &b = *other.source;
            if (a.archive.lexically_normal() == b.archive.lexically_normal() &&
                a.member == b.member && a.path == b.path) add(int(i), target).model = true;
        }
    if (reference)
        for (std::size_t i = 0; i < scene.spatial.regions.size(); ++i) {
            const auto &other = scene.spatial.regions[i];
            if (int(i) == region || !other.overworld) continue;
            const auto &ref = *other.overworld;
            bool script = reference->script && reference->script == ref.script &&
                          reference->local_zone == ref.local_zone && reference->category == ref.category;
            bool condition = reference->condition && reference->condition == ref.condition &&
                             reference->value == ref.value && reference->category == ref.category;
            bool destination = spatial->kind == SpatialKind::Entrance && other.kind == SpatialKind::Entrance &&
                reference->destination_zone >= 0 && reference->destination_zone == other.zone &&
                reference->destination_event == ref.event;
            bool incoming = other.kind == SpatialKind::Entrance && spatial->kind == SpatialKind::Entrance &&
                ref.destination_zone >= 0 && ref.destination_zone == spatial->zone && ref.destination_event == reference->event;
            if (script || condition || destination || incoming) {
                auto &link = add(region_draws[i], int(i));
                link.script = script; link.condition = condition;
                link.destination = destination; link.incoming = incoming;
            }
        }
    std::vector<ObjectLink> result;
    for (auto &[key, link] : links) result.push_back(std::move(link));
    return result;
}
}
