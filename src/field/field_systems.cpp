#include "field/field_systems.h"
#include "field/pedestrian_routes.h"
#include "formats/container.h"
#include <cmath>
#include <algorithm>
#include <set>
#include <sstream>
namespace studio {
namespace {
struct Layout {
    unsigned category, size;
    const char *name;
    SpatialKind kind;
};
constexpr Layout layouts[] = {{1, 120, "NPC", SpatialKind::Actor},
                              {5, 60, "Field action", SpatialKind::FieldAction},
                              {7, 84, "Trainer", SpatialKind::Actor},
                              {8, 76, "Special encounter", SpatialKind::SpecialEncounter},
                              {9, 24, "Pedestrian route", SpatialKind::Pedestrian},
                              {11, 68, "Berry pile", SpatialKind::Berry},
                              {12, 88, "Fishing spot", SpatialKind::Fishing},
                              {13, 120, "Poke Finder spot", SpatialKind::PhotoSpot},
                              {14, 152, "Contact Pokemon", SpatialKind::Contact},
                              {15, 52, "Push-rock puzzle", SpatialKind::RockPuzzle},
                              {16, 16, "Ambient sound", SpatialKind::AmbientSound}};
SpatialPoint point(View b, std::size_t p) {
    SpatialPoint result{f32(b, p), f32(b, p + 4), f32(b, p + 8)};
    for (auto v : result)
        require(std::isfinite(v), "Non-finite field-system coordinate");
    return result;
}
void marker(SpatialRegion &r, SpatialPoint c, float radius = 20) {
    auto base = narrow(r.vertices.size());
    r.vertices.push_back({{c[0] - radius, c[1], c[2] - radius}});
    r.vertices.push_back({{c[0] + radius, c[1], c[2] - radius}});
    r.vertices.push_back({{c[0], c[1], c[2] + radius}});
    r.vertices.push_back({{c[0], c[1] + radius * 2, c[2]}});
    for (unsigned i : {0u, 1u, 3u, 1u, 2u, 3u, 2u, 0u, 3u, 0u, 2u, 1u})
        r.triangles.push_back(base + i);
}
void merge(SpatialRegion &r, const SpatialRegion &s) {
    auto base = narrow(r.vertices.size());
    r.vertices.insert(r.vertices.end(), s.vertices.begin(), s.vertices.end());
    for (auto i : s.triangles)
        r.triangles.push_back(base + i);
    for (auto i : s.lines)
        r.lines.push_back(base + i);
}
struct Reader {
    View b;
    std::size_t end;
    FieldSystemEntry &e;
    void value(const std::string &group, const std::string &name, const std::string &v) {
        e.properties.push_back({group, name, v});
    }
    void number(const std::string &g, const std::string &n, std::size_t p, unsigned bytes = 4) {
        value(g, n,
              std::to_string(bytes == 1   ? slice(b, p, 1)[0]
                             : bytes == 2 ? u16(b, p)
                                          : u32(b, p)));
    }
    void real(const std::string &g, const std::string &n, std::size_t p) {
        auto v = f32(b, p);
        require(std::isfinite(v), "Non-finite field-system setting");
        std::ostringstream s;
        s << v;
        value(g, n, s.str());
    }
    void vector(const std::string &g, const std::string &n, std::size_t p) {
        auto v = point(b, p);
        std::ostringstream s;
        s << v[0] << ", " << v[1] << ", " << v[2];
        value(g, n, s.str());
    }
    unsigned pointer(std::size_t p) {
        auto q = u32(b, p);
        require(!q || (q >= end && q < b.size()), "Field-system payload is outside its data tail");
        return q;
    }
    void shape(std::size_t p, const std::string &name) {
        auto q = pointer(p);
        if (!q) {
            value(name, "Shapes", "None");
            return;
        }
        SpatialScene shapes;
        decode_placement_collision(shapes, slice(b, q, b.size() - q), name, -1);
        value(name, "Shapes", std::to_string(shapes.regions.size()));
        for (auto &r : shapes.regions)
            merge(e.region, r);
        auto n = u32(b, q);
        std::size_t at = q + 4 + n * 4;
        constexpr unsigned sizes[] = {20, 40, 28, 36};
        const char *names[] = {"Cylinder", "Box", "Line wall", "Triangle"};
        for (unsigned i = 0; i < n; ++i) {
            auto type = u32(b, q + 4 + i * 4);
            require(type < 4, "Unsupported field shape");
            auto g = name + " / " + std::to_string(i + 1);
            value(g, "Shape", names[type]);
            vector(g, "Position / point A", at);
            if (type == 0) {
                real(g, "Radius", at + 12);
                real(g, "Height", at + 16);
            }
            if (type == 1) {
                for (unsigned k = 0; k < 4; ++k)
                    real(g, std::string("Quaternion ") + "XYZW"[k], at + 12 + k * 4);
                vector(g, "Extents", at + 28);
            }
            if (type >= 2)
                vector(g, "Point B", at + 12);
            if (type == 2)
                real(g, "Height", at + 24);
            if (type == 3)
                vector(g, "Point C", at + 24);
            at += sizes[type];
        }
    }
    void path(std::size_t p, const std::string &name) {
        auto q = pointer(p);
        if (!q) {
            value(name, "Path", "None");
            return;
        }
        auto n = u16(b, q);
        slice(b, q, 8 + std::size_t(n) * 12);
        number(name, "Points", q, 2);
        number(name, "Curved", q + 2, 2);
        number(name, "Loop", q + 4, 2);
        number(name, "Follow ground", q + 6, 2);
        for (unsigned i = 0; i < n; ++i) {
            auto at = q + 8 + i * 12;
            vector(name, "Point " + std::to_string(i + 1), at);
        }
        append_route_geometry(e.region, read_pedestrian_path(b, q));
        if (n) marker(e.region, point(b, q + 8), 12);
        e.notice = "Curves use game distance sampling. Ground following and actor behavior require preview.";
    }
};
void extrude_sound_outline(SpatialRegion &r, const std::vector<SpatialPoint> &outline,
                           float height) {
    if (outline.empty())
        return;
    auto base = narrow(r.vertices.size()), count = narrow(outline.size());
    SpatialPoint center{};
    for (auto p : outline) {
        r.vertices.push_back({p});
        p[1] += height;
        r.vertices.push_back({p});
    }
    for (auto p : outline)
        for (unsigned k = 0; k < 3; ++k)
            center[k] += p[k] / float(count);
    auto bottom = narrow(r.vertices.size());
    r.vertices.push_back({center});
    center[1] += height;
    r.vertices.push_back({center});
    for (unsigned i = 0; i < count; ++i) {
        auto a = base + i * 2, b = base + ((i + 1) % count) * 2;
        r.triangles.insert(r.triangles.end(),
                           {a, b, b + 1, a, b + 1, a + 1, bottom, b, a, bottom + 1, a + 1, b + 1});
        r.lines.insert(r.lines.end(), {a, b, a + 1, b + 1});
        if (i % std::max(1u, count / 4) == 0)
            r.lines.insert(r.lines.end(), {a, a + 1});
    }
}
void sound_cylinder(SpatialRegion &r, SpatialPoint center, float radius, float height) {
    std::vector<SpatialPoint> outline;
    for (unsigned i = 0; i < 32; ++i) {
        float angle = float(i) * 6.283185307f / 32;
        outline.push_back({center[0] + radius * std::cos(angle), center[1],
                           center[2] + radius * std::sin(angle)});
    }
    extrude_sound_outline(r, outline, height);
}
void sound_strip(SpatialRegion &r, SpatialPoint a, SpatialPoint b, float radius, float height) {
    float dx = b[0] - a[0], dz = b[2] - a[2];
    if (dx * dx + dz * dz < 1e-10f)
        return;
    float heading = std::atan2(dz, dx);
    std::vector<SpatialPoint> outline;
    // Flat segments use the first endpoint's elevation, even when the next point is higher.
    for (unsigned end = 0; end < 2; ++end)
        for (unsigned i = 0; i <= 16; ++i) {
            float angle =
                heading - 1.570796327f + float(end) * 3.141592654f + float(i) * 3.141592654f / 16;
            auto center = end ? a : b;
            outline.push_back(
                {center[0] + radius * std::cos(angle), a[1], center[2] + radius * std::sin(angle)});
        }
    extrude_sound_outline(r, outline, height);
}
void sound_capsule(SpatialRegion &r, SpatialPoint a, SpatialPoint b, float radius) {
    SpatialPoint axis{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float length = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (length > 0)
        for (auto &v : axis)
            v /= length;
    else
        axis = {0, 1, 0};
    SpatialPoint u = std::abs(axis[1]) < .9f ? SpatialPoint{-axis[2], 0, axis[0]}
                                             : SpatialPoint{axis[1], -axis[0], 0};
    float scale = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    for (auto &v : u)
        v /= scale;
    SpatialPoint v{axis[1] * u[2] - axis[2] * u[1], axis[2] * u[0] - axis[0] * u[2],
                   axis[0] * u[1] - axis[1] * u[0]};
    constexpr unsigned sides = 32, hemisphere_steps = 8;
    auto base = narrow(r.vertices.size());
    unsigned rings = 0;
    for (unsigned end = 0; end < 2; ++end)
        for (unsigned j = (end && length == 0) ? 1 : 0; j <= hemisphere_steps; ++j) {
            float angle = (float(j) / hemisphere_steps + float(end) - 1) * 1.570796327f;
            auto center = end ? b : a;
            for (unsigned i = 0; i < sides; ++i) {
                float around = float(i) * 6.283185307f / sides;
                SpatialPoint p{};
                for (unsigned k = 0; k < 3; ++k)
                    p[k] = center[k] + radius * (axis[k] * std::sin(angle) +
                                                 std::cos(angle) * (u[k] * std::cos(around) +
                                                                    v[k] * std::sin(around)));
                r.vertices.push_back({p});
            }
            ++rings;
        }
    for (unsigned ring = 0; ring < rings; ++ring)
        for (unsigned i = 0; i < sides; ++i) {
            auto a0 = base + ring * sides + i, a1 = base + ring * sides + (i + 1) % sides;
            if (ring + 1 < rings) {
                r.triangles.insert(r.triangles.end(),
                                   {a0, a1, a1 + sides, a0, a1 + sides, a0 + sides});
                if (i % 8 == 0)
                    r.lines.insert(r.lines.end(), {a0, a0 + sides});
            }
            if (ring == hemisphere_steps || (length > 0 && ring == hemisphere_steps + 1))
                r.lines.insert(r.lines.end(), {a0, a1});
        }
}

void sound_shape(Reader &s, std::size_t p) {
    auto q = s.pointer(p);
    if (!q)
        return;
    auto type = u32(s.b, q);
    auto &r = s.e.region;
    const char *names[] = {"Cylinder", "Flat strip", "Sphere", "Tube"};
    require(type < 4, "Unsupported ambient sound shape");
    s.value("Sound region", "Shape", names[type]);
    if (type == 0 || type == 2) {
        auto at = q + (type == 0 ? 12 : 8);
        auto center = point(s.b, at);
        s.vector("Sound region", "Center", at);
        s.real("Sound region", "Radius", q + (type == 0 ? 8 : 4));
        if (type == 0)
            s.real("Sound region", "Height", q + 4);
        float radius = f32(s.b, q + (type == 0 ? 8 : 4));
        require(std::isfinite(radius) && radius >= 0, "Invalid sound radius");
        if (type == 0) {
            float height = f32(s.b, q + 4);
            require(std::isfinite(height) && height >= 0, "Invalid sound height");
            sound_cylinder(r, center, radius, height);
        } else
            sound_capsule(r, center, center, radius);
    } else {
        auto count_at = q + (type == 1 ? 12 : 8), n = u32(s.b, count_at);
        require(n <= 65535, "Excessive sound-region points");
        slice(s.b, count_at + 4, std::size_t(n) * 12);
        s.real("Sound region", "Width (distance from centerline)", q + (type == 1 ? 8 : 4));
        if (type == 1)
            s.real("Sound region", "Height", q + 4);
        float radius = f32(s.b, q + (type == 1 ? 8 : 4));
        float height = type == 1 ? f32(s.b, q + 4) : 0;
        require(std::isfinite(radius) && radius >= 0 && std::isfinite(height) && height >= 0,
                "Invalid sound-region dimensions");
        std::vector<SpatialPoint> points;
        for (unsigned i = 0; i < n; ++i) {
            auto at = count_at + 4 + i * 12;
            s.vector("Sound region", "Point " + std::to_string(i + 1), at);
            points.push_back(point(s.b, at));
        }
        for (unsigned i = 1; i < n; ++i) {
            auto a = points[i - 1], b = points[i];
            if (type == 1)
                sound_strip(r, a, b, radius, height);
            else if (a != b)
                sound_capsule(r, a, b, radius);
            if (type == 1)
                b[1] = a[1];
            auto base = narrow(r.vertices.size());
            r.vertices.push_back({a});
            r.vertices.push_back({b});
            r.lines.insert(r.lines.end(), {base, base + 1});
        }
    }
}
}
std::vector<FieldSystemEntry> inspect_field_systems(View source) {
    std::vector<FieldSystemEntry> result;
    auto ed = Container::parse(source, "ED");
    for (auto layout : layouts) {
        if (layout.category >= ed.files.size() || ed.files[layout.category].empty())
            continue;
        auto zones = Container::parse(ed.files[layout.category]);
        for (unsigned zone = 0; zone < zones.files.size(); ++zone) {
            auto &b = zones.files[zone];
            if (b.empty())
                continue;
            auto count = u32(b, 0);
            require(count <= 4096, "Excessive field-system records");
            auto end = 4 + std::size_t(count) * layout.size;
            slice(b, 0, end);
            for (unsigned row = 0; row < count; ++row) {
                auto p = 4 + std::size_t(row) * layout.size;
                FieldSystemEntry e;
                e.category = layout.category;
                e.local_zone = zone;
                e.row = row;
                e.name = std::string(layout.name) + " " + std::to_string(row + 1);
                e.region.kind = layout.kind;
                e.region.name = e.name;
                auto ref = std::make_shared<OverworldReference>();
                ref->category = layout.category;
                ref->local_zone = zone;
                ref->row = row;
                ref->rotation = {0, 0, 0, 1};
                e.region.overworld = ref;
                Reader s{b, end, e};
                try {
                    if (layout.category != 15 && layout.category != 16)
                        require(u32(b, p) == (layout.category == 14 ? 1 : layout.category),
                                "Unexpected field-system record type");
                    if (layout.category != 9 && layout.category != 16) {
                        ref->position = point(b, p + 4);
                        s.vector("Placement", "Position", p + 4);
                        if (layout.category != 15)
                            for (unsigned k = 0; k < 4; ++k) {
                                ref->rotation[k] = f32(b, p + 16 + k * 4);
                                s.real("Placement", std::string("Quaternion ") + "XYZW"[k],
                                       p + 16 + k * 4);
                            }
                    }
                    auto conditions = [&](unsigned flag, unsigned val, unsigned version = 0) {
                        ref->condition = u32(b, p + flag);
                        ref->value = u32(b, p + val);
                        s.number("Conditions", "Flag / work", p + flag);
                        s.number("Conditions", "Expected value", p + val);
                        if (version) {
                            ref->version = u32(b, p + version);
                            s.number("Conditions", "Game version mask", p + version);
                        }
                    };
                    if (layout.category == 1 || layout.category == 5 || layout.category == 7 ||
                        layout.category == 14) {
                        conditions(36, 40, 32);
                        ref->event = u32(b, p + 44);
                        s.number("Related data", "Event ID", p + 44);
                    } else if (layout.category == 8 || layout.category == 12 ||
                               layout.category == 13)
                        conditions(32, 36);
                    if (layout.category == 1 || layout.category == 14) {
                        ref->script = u32(b, p + 56);
                        s.number("Movement", "Movement code", p + 48);
                        s.number("Related data", "Character resource", p + 52);
                        s.number("Related data", "Script ID", p + 56);
                        s.number("Related data", "Shared alias", p + 104);
                        s.number("Idle motions", "Default motion", p + 60);
                        s.number("Idle motions", "Motion count", p + 64);
                        for (unsigned i = 0; i < 3; ++i) {
                            auto g = "Conditional idle " + std::to_string(i + 1);
                            s.number(g, "Motion", p + 68 + i * 12);
                            s.number(g, "Flag / work", p + 72 + i * 12);
                            s.number(g, "Expected value", p + 76 + i * 12);
                        }
                        s.number("Conditions", "Alternate-world mode", p + 108);
                        s.shape(p + 112, "Talk range");
                        s.shape(p + 116, "Body collision");
                        if (layout.category == 14) {
                            s.number("Related data", "Associated NPC event", p + 120);
                            s.number("Related data", "Saved contact state", p + 124);
                            s.number("Movement", "Contact movement code", p + 128);
                            s.number("Movement", "Follow ground", p + 144);
                            s.shape(p + 132, "Movement area");
                            s.shape(p + 148, "Reserved area");
                        }
                    } else if (layout.category == 5) {
                        s.number("Action", "Action type", p + 48);
                        s.shape(p + 56, "Activation range");
                        if (auto q = s.pointer(p + 52))
                            s.number("Action payload", "First payload word (undecoded)", q);
                    } else if (layout.category == 7) {
                        ref->script = u32(b, p + 52);
                        s.number("Related data", "Character resource", p + 48);
                        s.number("Related data", "Script ID", p + 52);
                        s.number("Related data", "Shared alias", p + 56);
                        if (auto q = s.pointer(p + 60)) {
                            s.number("Movement", "Movement type", q);
                            s.number("Movement", "Motion", q + 4);
                            s.real("Movement", "Start frame", q + 8);
                        }
                        s.shape(p + 64, "Talk range");
                        s.shape(p + 68, "Body collision");
                        s.path(p + 76, "Patrol route");
                        for (auto pair : {std::pair{72u, 16u}, std::pair{80u, 20u}})
                            if (auto q = s.pointer(p + pair.first)) {
                                auto n = u32(b, q);
                                require(n <= 4096, "Excessive trainer actions");
                                slice(b, q + 4, std::size_t(n) * pair.second);
                                for (unsigned i = 0; i < n; ++i) {
                                    auto at = q + 4 + i * pair.second;
                                    auto g = std::string(pair.first == 72 ? "Motion signal "
                                                                          : "Patrol action ") +
                                             std::to_string(i + 1);
                                    if (pair.first == 72) {
                                        s.number(g, "Motion", at);
                                        s.number(g, "Signal type", at + 4);
                                        s.value(g, "Value",
                                                std::to_string(std::int32_t(u32(b, at + 8))));
                                        s.real(g, "Frame", at + 12);
                                    } else {
                                        s.real(g, "Route progress", at);
                                        s.number(g, "Motion", at + 4);
                                        s.real(g, "Facing", at + 8);
                                        s.number(g, "Repeat count", at + 12);
                                        s.real(g, "Start frame", at + 16);
                                    }
                                }
                            }
                    } else if (layout.category == 8) {
                        const char *names[] = {"Event ID", "Preset", "Character resource",
                                               "Encounter data"};
                        for (unsigned i = 0; i < 4; ++i)
                            s.number("Related data", names[i], p + 40 + i * 4);
                        ref->event = u32(b, p + 40);
                        s.shape(p + 56, "Encounter range");
                        s.shape(p + 60, "Action range");
                        s.path(p + 64, "Route");
                        s.shape(p + 68, "Second action range");
                        s.path(p + 72, "Second route");
                    } else if (layout.category == 9) {
                        conditions(16, 20);
                        s.real("Spawning", "Cooldown (seconds)", p + 8);
                        s.path(p + 4, "Route");
                        if (auto q = s.pointer(p + 12)) {
                            auto n = u32(b, q);
                            require(n <= 4096, "Excessive pedestrian choices");
                            slice(b, q + 4, std::size_t(n) * 32);
                            const char *names[] = {"Character resource", "Appearance weight",
                                                   "Walk motion",        "Emote",
                                                   "Script ID",          "Game version mask",
                                                   "Weather mask",       "Time mask"};
                            for (unsigned i = 0; i < n; ++i)
                                for (unsigned j = 0; j < 8; ++j)
                                    s.number("Pedestrian choice " + std::to_string(i + 1), names[j],
                                             q + 4 + i * 32 + j * 4);
                        }
                    } else if (layout.category == 11) {
                        ref->event = u32(b, p + 32);
                        s.number("Related data", "Event ID", p + 32);
                        s.number("Related data", "Persistent pile ID", p + 36);
                        s.number("Related data", "Encounter table", p + 40);
                        s.number("Rewards", "Encounter chance (%)", p + 44, 2);
                        s.number("Rewards", "Normal level", p + 46, 1);
                        s.number("Rewards", "Boss level", p + 47, 1);
                        s.number("Rewards", "Abundant pile chance (%)", p + 48, 2);
                        const char *names[] = {"Normal minimum", "Normal maximum",
                                               "Abundant minimum", "Abundant maximum"};
                        for (unsigned i = 0; i < 4; ++i)
                            s.number("Rewards", names[i], p + 50 + i, 1);
                        for (unsigned i = 0; i < 6; ++i)
                            s.number("Berry items", "Item slot " + std::to_string(i + 1),
                                     p + 54 + i * 2, 2);
                        s.number("Berry items", "Rare berry", p + 66, 2);
                    } else if (layout.category == 12) {
                        ref->event = u32(b, p + 40);
                        s.number("Related data", "Event ID", p + 40);
                        s.number("Related data", "Focus ID", p + 44);
                        s.number("Related data", "Static resource", p + 48, 2);
                        s.number("Fishing", "Rare spot chance (%)", p + 50, 2);
                        s.real("Fishing", "Escape radius", p + 76);
                        for (unsigned i = 0; i < 2; ++i) {
                            auto g = i ? "Rare fishing" : "Normal fishing";
                            s.number(g, "Encounter table", p + 52 + i * 12);
                            s.number(g, "Item table", p + 56 + i * 12);
                            s.number(g, "Encounter chance (%)", p + 60 + i * 12);
                        }
                        s.shape(p + 80, "Watching range");
                        s.shape(p + 84, "Fishing range");
                    } else if (layout.category == 13) {
                        s.number("Related data", "Spot ID", p + 40);
                        s.value("Related data", "Subject",
                                std::to_string(std::int32_t(u32(b, p + 44))));
                        s.number("Photo settings", "Base score", p + 48);
                        s.number("Photo settings", "Place type", p + 52, 2);
                        s.number("Photo settings", "Starting focus", p + 54, 2);
                        s.vector("Photo camera", "Position", p + 56);
                        for (unsigned i = 0; i < 4; ++i)
                            s.real("Photo camera", std::string("Quaternion ") + "XYZW"[i],
                                   p + 68 + i * 4);
                        const char *names[] = {"Pitch minimum", "Pitch maximum", "Yaw minimum",
                                               "Yaw maximum"};
                        for (unsigned i = 0; i < 4; ++i)
                            s.real("Photo camera", names[i], p + 84 + i * 4);
                        s.shape(p + 100, "Activation range");
                        const char *graphs[] = {"Patrol A", "Patrol B", "Waiting positions",
                                                "One-way path"};
                        for (unsigned i = 0; i < 4; ++i)
                            s.value("Subject movement", graphs[i],
                                    s.pointer(p + 104 + i * 4) ? "Present; graph decoding pending"
                                                               : "None");
                    } else if (layout.category == 15) {
                        s.number("Related data", "Character resource", p);
                        s.real("Grid", "Facing (degrees)", p + 16);
                        s.number("Grid", "Columns", p + 20);
                        s.number("Grid", "Rows", p + 24);
                        s.number("Grid", "Spacing", p + 28);
                        auto nx = u32(b, p + 20), nz = u32(b, p + 24), spacing = u32(b, p + 28);
                        require(std::uint64_t(nx) * nz <= 1000000, "Excessive puzzle grid");
                        for (unsigned kind = 0; kind < 2; ++kind) {
                            auto n = u32(b, p + 32 + kind * 4), q = s.pointer(p + 40 + kind * 4);
                            require(n <= 4096, "Excessive puzzle pieces");
                            if (!q) {
                                require(!n, "Missing puzzle pieces");
                                continue;
                            }
                            slice(b, q, std::size_t(n) * 12);
                            for (unsigned i = 0; i < n; ++i) {
                                auto at = q + i * 12;
                                auto g =
                                    std::string(kind ? "Hole " : "Rock ") + std::to_string(i + 1);
                                s.number(g, "Column", at);
                                s.number(g, "Row", at + 4);
                                s.number(g, "Persistent ID", at + 8);
                                float a = f32(b, p + 16) * .01745329252f,
                                      x = float(u32(b, at)) * float(spacing),
                                      z = float(u32(b, at + 4)) * float(spacing);
                                marker(e.region,
                                       {ref->position[0] + std::cos(a) * x + std::sin(a) * z,
                                        ref->position[1],
                                        ref->position[2] - std::sin(a) * x + std::cos(a) * z},
                                       kind ? 12.f : 25.f);
                            }
                        }
                        if (auto q = s.pointer(p + 48)) {
                            slice(b, q, std::size_t(nx) * nz * 4);
                            for (unsigned z = 0; z < nz; ++z) {
                                std::ostringstream cells;
                                for (unsigned x = 0; x < nx; ++x) {
                                    if (x)
                                        cells << ", ";
                                    cells << u32(b, q + (z * nx + x) * 4);
                                }
                                s.value("Grid cells", "Row " + std::to_string(z), cells.str());
                            }
                        }
                    } else if (layout.category == 16) {
                        ref->version = u32(b, p);
                        s.number("Conditions", "Game version mask", p);
                        s.number("Playback", "Priority", p + 13, 1);
                        s.number("Playback", "Panning (0 camera, 1 player, 2 none)", p + 14, 2);
                        sound_shape(s, p + 4);
                        auto n = u32(b, end);
                        require(n <= 256, "Excessive sound parameters");
                        slice(b, end + 4, std::size_t(n) * 2056);
                        const char *times[] = {"Morning", "Noon", "Evening", "Night", "Midnight"};
                        std::set<unsigned> seen;
                        for (unsigned i = 0; i < 5; ++i) {
                            unsigned reference = b[p + 8 + i];
                            if (!reference) {
                                s.value("Schedule", times[i], "Silence");
                                continue;
                            }
                            if (reference > n) {
                                s.value("Schedule", times[i],
                                        "Missing sound parameters " + std::to_string(reference));
                                continue;
                            }
                            s.value("Schedule", times[i],
                                    "Parameters " + std::to_string(reference));
                            if (!seen.insert(reference).second)
                                continue;
                            auto q = end + 4 + (reference - 1) * 2056;
                            auto g = "Sound parameters " + std::to_string(reference);
                            s.number(g, "Sound ID", q);
                            s.number(g, "Player form mask", q + 4);
                            const char *curves[] = {"Volume", "Pitch", "Low-pass", "High-pass"};
                            for (unsigned c = 0; c < 4; ++c) {
                                std::ostringstream values;
                                for (unsigned j = 0; j < 128; ++j) {
                                    if (j)
                                        values << ", ";
                                    values << u32(b, q + 8 + (c * 128 + j) * 4);
                                }
                                s.value(g, curves[c], values.str());
                            }
                        }
                        e.notice =
                            "Area-wide sound data. Shared parameters affect multiple "
                            "regions. Volumes show the spatial eligibility boundary, with rounded "
                            "surfaces approximated by triangles. Overlapping segments can "
                            "appear darker. Volume is controlled by the distance curves; "
                            "spatial playback is not simulated.";
                    }
                } catch (const std::exception &error) {
                    e.notice = "Partially decoded: " + std::string(error.what());
                }
                if (e.region.vertices.empty())
                    marker(e.region, ref->position);
                e.region.detail = e.notice;
                result.push_back(std::move(e));
            }
        }
    }
    return result;
}
void decode_field_system_regions(SpatialScene &scene, View source,
                                 const std::map<unsigned, int> &ids) {
    for (auto &e : inspect_field_systems(source)) {
        if (e.category == 1 || e.category == 7) {
            for (auto &existing : scene.regions)
                if (existing.overworld && existing.overworld->category == e.category &&
                    existing.overworld->local_zone == e.local_zone &&
                    existing.overworld->row == e.row) {
                    merge(existing, e.region);
                    existing.detail = "Authored position, interaction ranges and any patrol route. "
                                      "Movement is not simulated.";
                    break;
                }
            continue;
        }
        auto it = ids.find(e.local_zone);
        if (e.category != 16 && it != ids.end())
            e.region.zone = it->second;
        e.region.name +=
            e.category == 16 ? " / area" : " / local zone " + std::to_string(e.local_zone);
        scene.regions.push_back(std::move(e.region));
    }
}
}
