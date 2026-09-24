#include "scene/spatial.h"
#include "field/area.h"
#include "formats/container.h"
#include <cmath>
#include <algorithm>
#include <map>
namespace studio {
namespace {
float scalar(View b, std::size_t p) {
    float v = f32(b, p);
    require(std::isfinite(v) && std::abs(v) < 1e9f, "Invalid spatial value");
    return v;
}
SpatialPoint point(View b, std::size_t p) {
    return {scalar(b, p), scalar(b, p + 4), scalar(b, p + 8)};
}
void triangle(SpatialRegion &r, SpatialPoint a, SpatialPoint b, SpatialPoint c,
              std::array<float, 3> ratio = {}) {
    auto n = narrow(r.vertices.size());
    r.vertices.push_back({a, ratio[0]});
    r.vertices.push_back({b, ratio[1]});
    r.vertices.push_back({c, ratio[2]});
    r.triangles.insert(r.triangles.end(), {n, n + 1, n + 2});
    r.lines.insert(r.lines.end(), {n, n + 1, n + 1, n + 2, n + 2, n});
}
void quad(SpatialRegion &r, SpatialPoint a, SpatialPoint b, SpatialPoint c, SpatialPoint d) {
    auto n = narrow(r.vertices.size());
    for (auto p : {a, b, c, d})
        r.vertices.push_back({p, 0});
    r.triangles.insert(r.triangles.end(), {n, n + 1, n + 2, n, n + 2, n + 3});
    r.lines.insert(r.lines.end(), {n, n + 1, n + 1, n + 2, n + 2, n + 3, n + 3, n});
}
CameraPoint camera_point(View b, std::size_t p) {
    return {point(b, p), point(b, p + 12), scalar(b, p + 24), scalar(b, p + 28),
            u32(b, p + 32) != 0};
}
CameraSupport support(View b, std::size_t p) {
    CameraSupport s;
    s.zone_default = u32(b, p) != 0;
    s.type = u32(b, p + 4);
    s.parameter_default = u32(b, p + 8) != 0;
    s.maximum = scalar(b, p + 12);
    s.leaving = {u16(b, p + 16), u16(b, p + 18), u32(b, p + 20)};
    s.entering = {u16(b, p + 24), u16(b, p + 26), u32(b, p + 28)};
    s.single_axis = u32(b, p + 32) != 0;
    s.axis = scalar(b, p + 36);
    return s;
}
}
const char *spatial_kind_name(SpatialKind kind) {
    static const char *names[] = {
        "Ground surfaces",     "Normal walls",      "Water boundaries", "Ride restrictions",
        "Mudsdale boundaries", "Placement shapes",  "Camera regions",   "Camera scroll stops",
        "Zone boundaries",     "Entrances & warps", "Story triggers",   "Scenery interactions",
        "NPCs & trainers",     "Item pickups",      "Wild encounters",
        "Field actions", "Special encounters", "Pedestrian routes", "Berry piles",
        "Fishing spots", "Poke Finder spots", "Contact Pokemon", "Push-rock puzzles", "Ambient sounds"};
    static_assert(std::size(names) == unsigned(SpatialKind::Count));
    return names[unsigned(kind)];
}
ZoneCamera decode_zone_camera(View b, int zone) {
    slice(b, 0, 84);
    return {zone, b[61], b[62]};
}
std::vector<CameraSetting>
decode_camera_settings(View b, std::vector<std::array<unsigned, 3>> *replacements) {
    if (b.empty() || std::all_of(b.begin(), b.end(), [](auto v) {
            return v == 0;
        }))
        return {};
    require(u32(b, 0) == 1, "Unsupported camera table version");
    auto total = u32(b, 4), replace = u32(b, 24);
    require(total <= 65536 && replace <= 65536, "Excessive camera table");
    std::array<unsigned, 4> counts{}, sizes{96, 136, 68, 68};
    unsigned sum = 0;
    for (unsigned i = 0; i < 4; ++i) {
        counts[i] = u32(b, 8 + i * 4);
        require(counts[i] <= total, "Invalid camera type count");
        sum += counts[i];
    }
    require(sum == total, "Camera type counts do not match total");
    std::size_t p = 28;
    std::vector<CameraSetting> out;
    for (unsigned type = 0; type < 4; ++type)
        for (unsigned i = 0; i < counts[type]; ++i) {
            slice(b, p, sizes[type]);
            CameraSetting s;
            s.type = u32(b, p);
            require(s.type == type, "Camera type does not match table section");
            s.transition = u32(b, p + 4);
            s.priority = std::int32_t(u32(b, p + 8));
            s.work = std::int32_t(u32(b, p + 12));
            s.value = u16(b, p + 16);
            if (type < 2) {
                s.a = camera_point(b, p + 20);
                s.b = type == 1 ? camera_point(b, p + 56) : s.a;
                s.support = support(b, p + (type == 1 ? 96 : 56));
                if (type == 1)
                    s.easing = u32(b, p + 92);
            } else if (type == 2) {
                s.player_target = u32(b, p + 20) != 0;
                s.hold_offset = point(b, p + 24);
                s.target = point(b, p + 36);
                s.position = point(b, p + 48);
                s.hold_fov = scalar(b, p + 60);
                s.bank = scalar(b, p + 64);
            }
            out.push_back(s);
            p += sizes[type];
        }
    slice(b, p, std::size_t(replace) * 12);
    if (replacements)
        for (unsigned i = 0; i < replace; ++i)
            replacements->push_back(
                {u32(b, p + i * 12), u32(b, p + i * 12 + 4), u32(b, p + i * 12 + 8)});
    return out;
}
void decode_collision_mesh(SpatialScene &scene, View b, SpatialKind kind, const std::string &name,
                           std::shared_ptr<const CollisionSource> source) {
    if (b.empty())
        return;
    require(u32(b, 0) == 0x14120500, "Unsupported collision mesh version");
    auto count = u32(b, 4);
    require(count <= 2000000, "Excessive collision triangles");
    slice(b, 8, std::size_t(count) * 72);
    std::map<unsigned, std::size_t> groups;
    for (unsigned i = 0; i < count; ++i) {
        auto p = 8 + std::size_t(i) * 72;
        auto attribute = u32(b, p + 64);
        auto [it, added] = groups.emplace(attribute, scene.regions.size());
        if (added) {
            SpatialRegion r;
            r.collision_source = source;
            r.kind = kind;
            r.attribute = attribute;
            r.name = name + " / attribute " + std::to_string(attribute);
            if (kind == SpatialKind::Camera) {
                r.camera = int(attribute);
                r.name = "Camera region " + std::to_string(attribute);
                r.detail = "Triangle region; vertex values encode the camera blend ratio.";
            }
            scene.regions.push_back(std::move(r));
        }
        auto &r = scene.regions[it->second];
        std::array<float, 3> ratio{};
        if (kind == SpatialKind::Camera) {
            auto bits = u32(b, p + 68);
            for (unsigned k = 0; k < 3; ++k)
                ratio[k] = float((bits >> (k * 8)) & 255) / 255;
        }
        triangle(r, point(b, p), point(b, p + 16), point(b, p + 32), ratio);
        if (source)
            r.collision_faces.push_back(i);
    }
}
void decode_camera_regions(SpatialScene &scene, View b) {
    auto pack = Container::parse(b, "BG");
    require(pack.files.size() == 4, "Unsupported camera resource layout");
    scene.cameras = decode_camera_settings(pack.files[0], &scene.replacements);
    decode_collision_mesh(scene, pack.files[2], SpatialKind::Camera, "Camera");
    auto &circles = pack.files[1];
    if (!circles.empty()) {
        require(u32(circles, 0) == 0, "Unsupported camera circle version");
        auto count = u32(circles, 8);
        require(count <= 65536 && u32(circles, 4) == count, "Invalid camera circle count");
        slice(circles, 12, std::size_t(count) * 24);
        for (unsigned i = 0; i < count; ++i) {
            auto p = 12 + i * 24;
            auto center = point(circles, p);
            float radius = scalar(circles, p + 12), ratio = scalar(circles, p + 20);
            require(radius > 0 && ratio >= 0 && ratio <= 1,
                    "Invalid camera circle radius or ratio");
            SpatialRegion r;
            r.kind = SpatialKind::Camera;
            r.circle_center = center;
            r.circle_radius = radius;
            r.center_ratio = ratio;
            r.attribute = u32(circles, p + 16);
            r.camera = int(r.attribute);
            r.name =
                "Camera circle " + std::to_string(i) + " / setting " + std::to_string(r.attribute);
            r.detail = "Radius " + std::to_string(radius) + "; center ratio " +
                       std::to_string(ratio) + ". Activation uses X/Z distance.";
            // The inner disk holds A; the outer ring blends towards B.
            constexpr unsigned segments = 48;
            for (unsigned j = 0; j < segments; ++j) {
                float a = float(j) * 6.283185307f / segments,
                      b = float(j + 1) * 6.283185307f / segments;
                auto edge = [&](float angle, float scale) {
                    return SpatialPoint{center[0] + std::cos(angle) * radius * scale, center[1],
                                        center[2] + std::sin(angle) * radius * scale};
                };
                auto ca = edge(a, ratio), cb = edge(b, ratio), ea = edge(a, 1), eb = edge(b, 1);
                if (ratio > 0)
                    triangle(r, center, ca, cb);
                if (ratio < 1) {
                    triangle(r, ca, ea, eb, {0, 1, 1});
                    triangle(r, ca, eb, cb, {0, 1, 0});
                }
            }
            scene.regions.push_back(std::move(r));
        }
    }
    auto &stops = pack.files[3];
    if (stops.empty())
        return;
    require(u32(stops, 0) == 0, "Unsupported camera scroll-stop version");
    auto in = u32(stops, 8), out = u32(stops, 12);
    require(in <= 65536 && out <= 65536 && u32(stops, 4) == in + out,
            "Invalid camera scroll-stop count");
    std::size_t p = 16;
    slice(stops, p, std::size_t(in) * 100 + std::size_t(out) * 52);
    for (unsigned type = 0; type < 2; ++type)
        for (unsigned i = 0; i < (type ? out : in); ++i) {
            CameraScrollStop stop;
            stop.outside = type != 0;
            for (unsigned k = 0; k < 4; ++k) {
                stop.hit[k] = point(stops, p + 4 + k * 12);
                if (!type)
                    stop.clamp[k] = point(stops, p + 52 + k * 12);
            }
            scene.scroll_stops.push_back(stop);
            SpatialRegion r;
            r.kind = SpatialKind::ScrollStop;
            r.name = std::string(type ? "Keep outside " : "Clamp inside ") + std::to_string(i);
            r.detail = "Axis " + std::to_string(scalar(stops, p)) +
                       " degrees; outer quad is the activation region.";
            quad(r, point(stops, p + 4), point(stops, p + 16), point(stops, p + 40),
                 point(stops, p + 28));
            if (!type) {
                auto n = narrow(r.vertices.size());
                for (unsigned k = 0; k < 4; ++k)
                    r.vertices.push_back({point(stops, p + 52 + k * 12), 1});
                r.lines.insert(r.lines.end(), {n, n + 1, n + 1, n + 3, n + 3, n + 2, n + 2, n});
                r.detail += " Inner outline is the clamp boundary.";
            }
            scene.regions.push_back(std::move(r));
            p += type ? 52 : 100;
        }
}
void decode_zone_regions(SpatialScene &scene, View b, unsigned area,
                         const std::map<unsigned, int> &zone_ids) {
    auto pack = Container::parse(b, "WD");
    auto &header = pack.files.at(0);
    auto &mesh = pack.files.at(1);
    if (mesh.empty())
        return;
    auto begin = u32(header, 8), end = u32(header, 12);
    require(end >= begin && (end - begin) % 4 == 0, "Invalid world zone bindings");
    slice(header, begin, end - begin);
    auto count = u32(mesh, 0), vertices = u32(mesh, 4), vertex_start = u32(mesh, 8),
         file_end = u32(mesh, 12);
    require(count == (end - begin) / 4 && vertices <= 1000000 && count <= 65536,
            "Invalid zone mesh counts");
    slice(mesh, 16, std::size_t(count) * 4);
    slice(mesh, vertex_start, std::size_t(vertices) * 12);
    require(file_end <= mesh.size(), "Invalid zone mesh end");
    for (unsigned i = 0; i < count; ++i) {
        auto first = u32(mesh, 16 + i * 4), last = i + 1 < count ? u32(mesh, 20 + i * 4) : file_end;
        require(last >= first && (last - first) % 12 == 0, "Invalid zone triangle indices");
        slice(mesh, first, last - first);
        if (u16(header, begin + i * 4 + 2) != area)
            continue;
        unsigned local = u16(header, begin + i * 4);
        auto it = zone_ids.find(local);
        SpatialRegion r;
        r.kind = SpatialKind::Zone;
        r.attribute = local;
        r.zone = it == zone_ids.end() ? -1 : it->second;
        r.name =
            r.zone >= 0 ? "Zone " + std::to_string(r.zone) : "Zone group " + std::to_string(local);
        r.detail = "World zone membership surface; not a movement-blocking wall.";
        for (auto p = first; p < last; p += 12) {
            std::array<SpatialPoint, 3> points;
            for (unsigned k = 0; k < 3; ++k) {
                auto index = u32(mesh, p + k * 4);
                require(index < vertices, "Zone triangle vertex is out of range");
                points[k] = point(mesh, vertex_start + index * 12);
            }
            triangle(r, points[0], points[1], points[2]);
        }
        if (!r.vertices.empty())
            scene.regions.push_back(std::move(r));
    }
}
void decode_placement_collision(SpatialScene &scene, View b, const std::string &name, int zone) {
    auto count = u32(b, 0);
    require(count <= 4096, "Excessive placement shapes");
    slice(b, 4, std::size_t(count) * 4);
    std::size_t p = 4 + count * 4;
    constexpr unsigned sizes[] = {20, 40, 28, 36};
    for (unsigned i = 0; i < count; ++i) {
        auto type = u32(b, 4 + i * 4);
        require(type < 4, "Unsupported placement shape");
        slice(b, p, sizes[type]);
        SpatialRegion r;
        r.kind = SpatialKind::Placement;
        r.zone = zone;
        r.attribute = type;
        const char *names[] = {"cylinder", "box", "line wall", "triangle"};
        r.name = name + " / " + names[type] + " " + std::to_string(i);
        if (type == 0) {
            auto c = point(b, p);
            float radius = scalar(b, p + 12), height = scalar(b, p + 16);
            require(radius >= 0 && height >= 0, "Invalid cylinder dimensions");
            for (unsigned k = 0; k < 32; ++k) {
                float a = k * 6.283185307f / 32, b = (k + 1) * 6.283185307f / 32;
                SpatialPoint x{c[0] + radius * std::cos(a), c[1], c[2] + radius * std::sin(a)},
                    y{c[0] + radius * std::cos(b), c[1], c[2] + radius * std::sin(b)},
                    xt = x, yt = y, top = c;
                xt[1] += height;
                yt[1] += height;
                top[1] += height;
                quad(r, x, y, yt, xt);
                triangle(r, c, y, x);
                triangle(r, top, xt, yt);
            }
        } else if (type == 1) {
            auto c = point(b, p), scale = point(b, p + 28);
            std::array<float, 4> q{};
            float length = 0;
            for (unsigned k = 0; k < 4; ++k) {
                q[k] = scalar(b, p + 12 + k * 4);
                length += q[k] * q[k];
            }
            require(length > 1e-8f, "Invalid collision quaternion");
            for (auto &v : q)
                v /= std::sqrt(length);
            auto [x, y, z, w] = q;
            std::array<SpatialPoint, 8> corners{};
            for (unsigned k = 0; k < 8; ++k) {
                SpatialPoint v{(k & 1) ? scale[0] : -scale[0], (k & 2) ? scale[1] : 0,
                               (k & 4) ? scale[2] : -scale[2]};
                corners[k] = {c[0] + (1 - 2 * (y * y + z * z)) * v[0] + 2 * (x * y - z * w) * v[1] +
                                  2 * (x * z + y * w) * v[2],
                              c[1] + 2 * (x * y + z * w) * v[0] + (1 - 2 * (x * x + z * z)) * v[1] +
                                  2 * (y * z - x * w) * v[2],
                              c[2] + 2 * (x * z - y * w) * v[0] + 2 * (y * z + x * w) * v[1] +
                                  (1 - 2 * (x * x + y * y)) * v[2]};
            }
            for (auto f : std::array<std::array<unsigned, 4>, 6>{{{0, 1, 3, 2},
                                                                  {4, 6, 7, 5},
                                                                  {0, 4, 5, 1},
                                                                  {2, 3, 7, 6},
                                                                  {0, 2, 6, 4},
                                                                  {1, 5, 7, 3}}})
                quad(r, corners[f[0]], corners[f[1]], corners[f[2]], corners[f[3]]);
        } else if (type == 2) {
            auto a = point(b, p), c = point(b, p + 12), at = a, ct = c;
            float height = scalar(b, p + 24);
            at[1] += height;
            ct[1] += height;
            quad(r, a, c, ct, at);
        } else
            triangle(r, point(b, p), point(b, p + 12), point(b, p + 24));
        scene.regions.push_back(std::move(r));
        p += sizes[type];
    }
}
void decode_overworld_regions(SpatialScene &scene, View bytes,
                              const std::map<unsigned, int> &zone_ids) {
    auto placements = Container::parse(bytes, "ED");
    SpatialScene decoded;
    struct Category {
        unsigned slot, size, type;
        SpatialKind kind;
    };
    const Category categories[] = {
        {TargetProfile::position_event_pack, 60, 0, SpatialKind::StoryTrigger},
        {TargetProfile::warp_placement_pack, 88, 3, SpatialKind::Entrance},
        {TargetProfile::interaction_placement_pack, 60, 2, SpatialKind::Interaction},
        {TargetProfile::character_placement_pack, 120, 1, SpatialKind::Actor},
        {TargetProfile::trainer_placement_pack, 84, 7, SpatialKind::Actor},
        {TargetProfile::pickup_placement_pack, 64, TargetProfile::pickup_record_type,
         SpatialKind::Pickup}};
    for (auto category : categories) {
        if (category.slot >= placements.files.size() || placements.files[category.slot].empty())
            continue;
        auto zones = Container::parse(placements.files[category.slot]);
        for (unsigned local = 0; local < zones.files.size(); ++local) {
            auto &b = zones.files[local];
            if (b.empty())
                continue;
            auto count = u32(b, 0);
            require(count <= 4096, "Excessive overworld records");
            auto end = 4 + std::size_t(count) * category.size;
            slice(b, 0, end);
            for (unsigned row = 0; row < count; ++row) {
                auto p = 4 + std::size_t(row) * category.size;
                require(u32(b, p) == category.type, "Unexpected overworld record type");
                auto ref = std::make_shared<OverworldReference>();
                ref->category = category.slot;
                ref->local_zone = local;
                ref->row = row;
                ref->position = point(b, p + 4);
                float length = 0;
                for (unsigned k = 0; k < 4; ++k) {
                    ref->rotation[k] = scalar(b, p + 16 + k * 4);
                    length += ref->rotation[k] * ref->rotation[k];
                }
                require(length > .9f && length < 1.1f, "Invalid overworld orientation");
                ref->version = u32(b, p + 32);
                ref->condition = u32(b, p + 36);
                ref->value = u32(b, p + 40);
                auto id = zone_ids.find(local);
                int zone = id == zone_ids.end() ? -1 : id->second;
                std::string detail;
                unsigned collision = 0;
                if (category.kind == SpatialKind::Entrance) {
                    ref->event = u32(b, p + 44);
                    ref->destination_event = u32(b, p + 48);
                    zone = u16(b, p + 52);
                    ref->destination_zone = u16(b, p + 54);
                    ref->style = u32(b, p + 56);
                    ref->arrival = point(b, p + 60);
                    collision = u32(b, p + 84);
                    detail = "Transition style " + std::to_string(ref->style) + " | entry camera " +
                             std::to_string(u32(b, p + 72)) + " | exit camera " +
                             std::to_string(u32(b, p + 76)) + " | focus " +
                             std::to_string(u32(b, p + 80));
                } else if (category.kind == SpatialKind::Pickup) {
                    ref->event = u32(b, p + 44);
                    ref->item = u32(b, p + 48);
                    ref->quantity = u16(b, p + 52);
                    ref->appearance = u16(b, p + 54);
                    collision = u32(b, p + 60);
                    detail = "Item " + std::to_string(ref->item) + " x " +
                             std::to_string(ref->quantity) + " | appearance " +
                             std::to_string(ref->appearance) + " | collection flag/work " +
                             std::to_string(ref->condition);
                } else if (category.kind == SpatialKind::StoryTrigger) {
                    ref->script = u32(b, p + 44);
                    collision = u32(b, p + 56);
                    detail = "Alternate-world mode " + std::to_string(u32(b, p + 48)) +
                             " | interrupt flags " + std::to_string(u32(b, p + 52));
                } else if (category.kind == SpatialKind::Interaction) {
                    ref->event = u32(b, p + 44);
                    ref->script = u32(b, p + 48);
                    collision = u32(b, p + 56);
                    detail = "Attention reference " + std::to_string(u32(b, p + 52));
                } else {
                    ref->event = u32(b, p + 44);
                    ref->script =
                        u32(b, p + (category.slot == TargetProfile::character_placement_pack ? 56
                                                                                             : 52));
                    detail =
                        std::string(category.slot == TargetProfile::trainer_placement_pack
                                        ? "Trainer"
                                        : "NPC") +
                        " model " +
                        std::to_string(u32(
                            b, p + (category.slot == TargetProfile::trainer_placement_pack ? 48
                                                                                           : 52))) +
                        ". Marker shows authored placement, not movement or interaction range.";
                }
                SpatialRegion region;
                region.kind = category.kind;
                region.zone = zone;
                region.overworld = ref;
                region.detail = detail;
                region.name = std::string(spatial_kind_name(category.kind)) + " / " +
                              (zone >= 0 ? "zone " + std::to_string(zone)
                                         : "zone slot " + std::to_string(local)) +
                              " / " +
                              (category.kind == SpatialKind::StoryTrigger
                                   ? "script " + std::to_string(ref->script)
                                   : "event " + std::to_string(ref->event));
                if (ref->version || ref->condition)
                    region.name += " [conditional]";
                if (collision) {
                    require(collision >= end && collision <= b.size() - 4,
                            "Overworld collision offset is out of bounds");
                    SpatialScene shapes;
                    decode_placement_collision(shapes, slice(b, collision, b.size() - collision),
                                               "", zone);
                    for (auto &shape : shapes.regions) {
                        auto base = narrow(region.vertices.size());
                        region.vertices.insert(region.vertices.end(), shape.vertices.begin(),
                                               shape.vertices.end());
                        for (auto i : shape.triangles)
                            region.triangles.push_back(base + i);
                        for (auto i : shape.lines)
                            region.lines.push_back(base + i);
                    }
                }
                if (region.vertices.empty()) {
                    auto c = ref->position;
                    SpatialPoint top = c;
                    top[1] += 65;
                    for (unsigned i = 0; i < 4; ++i) {
                        float a = i * 1.570796327f, b = (i + 1) * 1.570796327f;
                        triangle(region, {c[0] + 22 * std::cos(a), c[1], c[2] + 22 * std::sin(a)},
                                 {c[0] + 22 * std::cos(b), c[1], c[2] + 22 * std::sin(b)}, top);
                    }
                }
                decoded.regions.push_back(std::move(region));
            }
        }
    }
    scene.regions.insert(scene.regions.end(), std::make_move_iterator(decoded.regions.begin()),
                         std::make_move_iterator(decoded.regions.end()));
}

}
