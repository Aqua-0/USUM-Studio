#include "field/pedestrian_routes.h"
#include "field/area.h"
#include "core/digest.h"
#include "formats/compression.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
namespace studio {
namespace {
float distance(SpatialPoint a, SpatialPoint b) {
    float sum = 0;
    for (unsigned k = 0; k < 3; ++k)
        sum += (a[k] - b[k]) * (a[k] - b[k]);
    return std::sqrt(sum);
}
void validate(const PedestrianRoute &r) {
    require(
        r.path.points.size() >= 2 &&
            r.path.points.size() + (r.path.loop && !r.path.curved ? 1 : 0) <=
                TargetProfile::pedestrian_point_capacity,
        "A pedestrian route needs 2 to 16 points; straight loops reserve one point for closure.");
    require(std::isfinite(r.cooldown), "Cooldown must be finite");
    for (auto p : r.path.points)
        for (auto v : p)
            require(std::isfinite(v) && std::abs(v) < 1e7f, "Invalid route position");
    require(PedestrianCurve(r.path).length() > .001f, "Route must have a nonzero length");
}
std::vector<PedestrianRoute> read_routes(View source) {
    auto ed = Container::parse(source, "ED");
    std::vector<PedestrianRoute> result;
    if (ed.files.size() <= TargetProfile::pedestrian_placement_pack ||
        ed.files[TargetProfile::pedestrian_placement_pack].empty())
        return result;
    auto zones = Container::parse(ed.files[TargetProfile::pedestrian_placement_pack]);
    for (unsigned z = 0; z < zones.files.size(); ++z) {
        auto &b = zones.files[z];
        if (b.empty())
            continue;
        auto n = u32(b, 0);
        require(n <= 4096, "Too many pedestrian routes");
        auto end = 4 + std::size_t(n) * TargetProfile::pedestrian_record_size;
        slice(b, 0, end);
        for (unsigned i = 0; i < n; ++i) {
            auto p = 4 + i * TargetProfile::pedestrian_record_size;
            require(u32(b, p) == TargetProfile::pedestrian_record_type,
                    "Invalid pedestrian record");
            auto q = u32(b, p + 4);
            require(q >= end, "Pedestrian route overlaps records");
            PedestrianRoute r{z, i, read_pedestrian_path(b, q), i, f32(b, p + 8), {}};
            auto c = u32(b, p + 12);
            if (c) {
                require(c >= end, "Pedestrian choices overlap records");
                auto count = u32(b, c);
                require(count <= 4096, "Too many pedestrian choices");
                slice(b, c + 4, std::size_t(count) * 32);
                for (unsigned j = 0; j < count; ++j) {
                    std::array<unsigned, 8> choice{};
                    for (unsigned k = 0; k < 8; ++k)
                        choice[k] = u32(b, c + 4 + j * 32 + k * 4);
                    r.choices.push_back(choice);
                }
            }
            result.push_back(std::move(r));
        }
    }
    return result;
}
}
PedestrianPath read_pedestrian_path(View b, std::size_t p) {
    auto n = u16(b, p);
    require(n <= 4096, "Too many route points");
    slice(b, p, 8 + std::size_t(n) * 12);
    PedestrianPath r{u16(b, p + 2) != 0, u16(b, p + 4) != 0, u16(b, p + 6) != 0, {}};
    for (unsigned i = 0; i < n; ++i) {
        SpatialPoint v{f32(b, p + 8 + i * 12), f32(b, p + 12 + i * 12), f32(b, p + 16 + i * 12)};
        for (auto x : v)
            require(std::isfinite(x), "Non-finite route point");
        r.points.push_back(v);
    }
    return r;
}
SpatialPoint PedestrianCurve::segment(unsigned i, float t) const {
    auto n = int(path_.points.size());
    auto at = [&](int k) {
        return path_.points[path_.loop ? (k % n + n) % n : std::clamp(k, 0, n - 1)];
    };
    auto a = at(int(i)), b = at(int(i) + 1);
    SpatialPoint out{};
    if (!path_.curved) {
        for (unsigned k = 0; k < 3; ++k)
            out[k] = a[k] + (b[k] - a[k]) * t;
    } else {
        auto prev = at(int(i) - 1), next = at(int(i) + 2);
        for (unsigned k = 0; k < 3; ++k) {
            float m0 = (b[k] - prev[k]) * .5f, m1 = (next[k] - a[k]) * .5f;
            out[k] = (2 * t * t * t - 3 * t * t + 1) * a[k] + (t * t * t - 2 * t * t + t) * m0 +
                     (-2 * t * t * t + 3 * t * t) * b[k] + (t * t * t - t * t) * m1;
        }
    }
    return out;
}
PedestrianCurve::PedestrianCurve(const PedestrianPath &p) : path_(p) {
    if (p.points.size() < 2)
        return;
    unsigned count = unsigned(p.points.size()) - (p.loop ? 0 : 1), steps = p.curved ? 10 : 1;
    for (unsigned i = 0; i < count; ++i) {
        auto previous = segment(i, 0);
        for (unsigned j = 1; j <= steps; ++j) {
            auto next = segment(i, float(j) / steps);
            auto len = distance(previous, next);
            lengths_.push_back(len);
            length_ += len;
            previous = next;
        }
    }
}
SpatialPoint PedestrianCurve::position(float t) const {
    if (path_.points.empty())
        return {};
    if (t <= 0 || length_ <= .000001f)
        return path_.points.front();
    if (t >= 1)
        return path_.loop ? path_.points.front() : path_.points.back();
    float remaining = t * length_;
    unsigned steps = path_.curved ? 10 : 1;
    for (unsigned i = 0; i < lengths_.size(); ++i) {
        auto len = lengths_[i];
        if (remaining <= len && len > .000001f)
            return segment(i / steps, (float(i % steps) + remaining / len) / steps);
        remaining -= len;
    }
    return path_.loop ? path_.points.front() : path_.points.back();
}
SpatialPoint PedestrianCurve::direction(float t) const {
    t = std::clamp(t, 0.f, .999f);
    auto a = position(t), b = position(t + .001f);
    float len = distance(a, b);
    for (unsigned k = 0; k < 3; ++k)
        b[k] = len > .000001f ? (b[k] - a[k]) / len : 0;
    return b;
}
void append_route_geometry(SpatialRegion &r, const PedestrianPath &path) {
    if (path.points.empty())
        return;
    PedestrianCurve curve(path);
    unsigned count = std::max(1u, unsigned(path.points.size()) * (path.curved ? 20u : 1u));
    auto base = narrow(r.vertices.size());
    if (!path.curved) {
        for (auto p : path.points)
            r.vertices.push_back({p});
        for (unsigned i = 1; i < path.points.size(); ++i) {
            r.lines.push_back(base + i - 1);
            r.lines.push_back(base + i);
        }
        if (path.loop && path.points.size() > 1) {
            r.lines.push_back(base + unsigned(path.points.size()) - 1);
            r.lines.push_back(base);
        }
    } else {
        for (unsigned i = 0; i <= count; ++i) {
            r.vertices.push_back({curve.position(float(i) / count)});
            if (i) {
                r.lines.push_back(base + i - 1);
                r.lines.push_back(base + i);
            }
        }
    }
}
PedestrianDocument::PedestrianDocument(Bytes source) : source_(std::move(source)) {
    auto ed = Container::parse(source_, "ED");
    hash_ = ed.files.size() > TargetProfile::pedestrian_placement_pack
                ? sha256(ed.files[TargetProfile::pedestrian_placement_pack])
                : sha256(Bytes{});
    original_ = current_ = saved_ = read_routes(source_);
    history_ = {current_};
}
void PedestrianDocument::set(unsigned i, PedestrianRoute r) {
    require(i < current_.size(), "Missing pedestrian route");
    require(r.zone == current_[i].zone && r.row == current_[i].row &&
                r.source_row == current_[i].source_row,
            "Route identity changed");
    require(r.choices.size() <= 4096, "Too many pedestrian choices");
    validate(r);
    if (r == current_[i])
        return;
    current_[i] = std::move(r);
    history_.resize(cursor_ + 1);
    history_.push_back(current_);
    ++cursor_;
}
void PedestrianDocument::undo() {
    if (can_undo())
        current_ = history_[--cursor_];
}
void PedestrianDocument::redo() {
    if (can_redo())
        current_ = history_[++cursor_];
}
unsigned PedestrianDocument::duplicate(unsigned index, SpatialPoint offset) {
    require(index < current_.size(), "Choose a route to copy");
    auto route = current_[index];
    route.row = 0;
    for (auto &r : current_)
        if (r.zone == route.zone)
            route.row = std::max(route.row, r.row + 1);
    require(route.row < 4096, "Too many pedestrian routes in this zone");
    for (auto &p : route.path.points)
        for (unsigned k = 0; k < 3; ++k)
            p[k] += offset[k];
    validate(route);
    auto next = current_;
    next.push_back(route);
    std::sort(next.begin(), next.end(), [](auto &a, auto &b) {
        return std::pair{a.zone, a.row} < std::pair{b.zone, b.row};
    });
    auto found = std::find(next.begin(), next.end(), route);
    auto result = unsigned(found - next.begin());
    current_ = std::move(next);
    history_.resize(cursor_ + 1);
    history_.push_back(current_);
    ++cursor_;
    return result;
}
std::string PedestrianDocument::serialize() const {
    std::ostringstream s;
    s << std::setprecision(9) << "USUMSTUDIO_PEDESTRIANS 2\nsource " << hash_ << '\n';
    for (unsigned i = 0; i < current_.size(); ++i) {
        auto &r = current_[i];
        s << "route " << i << ' ' << r.zone << ' ' << r.row << ' ' << r.source_row << ' '
          << r.cooldown << ' ' << r.path.curved << ' ' << r.path.loop << ' ' << r.path.follow_ground
          << ' ' << r.path.points.size() << '\n';
        for (auto p : r.path.points)
            s << p[0] << ' ' << p[1] << ' ' << p[2] << '\n';
        s << r.choices.size() << '\n';
        for (auto c : r.choices) {
            for (auto v : c)
                s << v << ' ';
            s << '\n';
        }
    }
    s << "end\n";
    return s.str();
}
void PedestrianDocument::restore(const std::string &patch) {
    std::istringstream s(patch);
    std::string word, hash;
    unsigned version;
    require(bool(s >> word >> version) && word == "USUMSTUDIO_PEDESTRIANS" &&
                (version == 1 || version == 2),
            "Unsupported pedestrian document");
    require(bool(s >> word >> hash) && word == "source" && hash == hash_,
            "Pedestrian source changed; reopen the matching project source");
    auto next = version == 1 ? original_ : std::vector<PedestrianRoute>{};
    std::vector<bool> seen(next.size());
    bool end = false;
    while (s >> word) {
        if (word == "end") {
            end = true;
            break;
        }
        unsigned i, n;
        require(word == "route" && bool(s >> i), "Invalid route index");
        if (version == 2) {
            require(i == next.size() && i < 65536, "Invalid route order");
            PedestrianRoute route;
            require(bool(s >> route.zone >> route.row >> route.source_row),
                    "Missing route identity");
            require(std::any_of(original_.begin(), original_.end(),
                                [&](auto &r) {
                                    return r.zone == route.zone && r.row == route.source_row;
                                }),
                    "Missing route template");
            unsigned expected = 0;
            for (auto &r : next)
                if (r.zone == route.zone)
                    ++expected;
            require(route.row == expected, "Invalid route row");
            next.push_back(route);
        } else {
            require(i < next.size() && !seen[i], "Invalid route index");
            seen[i] = true;
        }
        auto &r = next[i];
        require(
            bool(s >> r.cooldown >> r.path.curved >> r.path.loop >> r.path.follow_ground >> n) &&
                n >= 2 && n <= TargetProfile::pedestrian_point_capacity,
            "Invalid route settings");
        r.path.points.resize(n);
        for (auto &p : r.path.points)
            require(bool(s >> p[0] >> p[1] >> p[2]), "Missing route position");
        require(bool(s >> n) && n <= 4096 && (version == 2 || n == r.choices.size()),
                "Invalid pedestrian choice count");
        r.choices.resize(n);
        for (auto &c : r.choices)
            for (auto &v : c)
                require(bool(s >> v), "Missing pedestrian choice");
        validate(r);
    }
    s >> std::ws;
    require(end && s.eof(), "Incomplete pedestrian document");
    for (auto &r : original_)
        require(std::any_of(next.begin(), next.end(),
                            [&](auto &entry) {
                                return entry.zone == r.zone && entry.row == r.row &&
                                       entry.source_row == r.row;
                            }),
                "Original route identity changed");
    current_ = std::move(next);
    history_ = {current_};
    cursor_ = 0;
    mark_saved();
}
Bytes PedestrianDocument::compile(View source) const {
    if (current_ == original_)
        return Bytes(source.begin(), source.end());
    auto ed = Container::parse(source, "ED");
    require(ed.files.size() > TargetProfile::pedestrian_placement_pack &&
                sha256(ed.files[TargetProfile::pedestrian_placement_pack]) == hash_,
            "Pedestrian source changed during export");
    auto zones = Container::parse(ed.files[TargetProfile::pedestrian_placement_pack]);
    for (unsigned z = 0; z < zones.files.size(); ++z) {
        auto original = zones.files[z];
        if (original.empty())
            continue;
        unsigned count = 0;
        for (auto &r : current_)
            if (r.zone == z)
                ++count;
        unsigned old_count = u32(original, 0), size = TargetProfile::pedestrian_record_size;
        auto tail = 4 + old_count * size;
        require(count >= old_count, "Removing original routes is not supported");
        Bytes b(4 + count * size);
        put32(b, 0, count);
        append(b, slice(original, tail, original.size() - tail));
        for (auto &r : current_)
            if (r.zone == z) {
                validate(r);
                require(r.source_row < old_count, "Missing pedestrian template");
                unsigned p = 4 + r.row * size, old = 4 + r.source_row * size;
                auto record = slice(original, old, size);
                std::copy(record.begin(), record.end(), b.begin() + p);
                put_float(b, p + 8, r.cooldown);
                auto q = b.size();
                b.resize(q + 8 + r.path.points.size() * 12);
                put32(b, p + 4, narrow(q));
                put16(b, q, std::uint16_t(r.path.points.size()));
                put16(b, q + 2, r.path.curved);
                put16(b, q + 4, r.path.loop);
                put16(b, q + 6, r.path.follow_ground);
                for (unsigned j = 0; j < r.path.points.size(); ++j)
                    for (unsigned k = 0; k < 3; ++k)
                        put_float(b, q + 8 + j * 12 + k * 4, r.path.points[j][k]);
                q = b.size();
                b.resize(q + 4 + r.choices.size() * 32);
                put32(b, p + 12, narrow(q));
                put32(b, q, narrow(r.choices.size()));
                for (unsigned j = 0; j < r.choices.size(); ++j)
                    for (unsigned k = 0; k < 8; ++k)
                        put32(b, q + 4 + j * 32 + k * 4, r.choices[j][k]);
            }
        zones.files[z] = std::move(b);
    }
    ed.files[TargetProfile::pedestrian_placement_pack] = zones.write();
    auto result = ed.write();
    auto expected = current_;
    for (auto &r : expected)
        r.source_row = r.row;
    require(read_routes(result) == expected, "Pedestrian writeback verification failed");
    return result;
}
void PedestrianDocument::export_to(const std::filesystem::path &dump,
                                   const std::filesystem::path &output, unsigned area) const {
    auto relative = GameProfile::field_archive(dump);
    auto target = output / relative;
    require(std::filesystem::absolute(target).lexically_normal() !=
                std::filesystem::absolute(dump / relative).lexically_normal(),
            "Choose a separate staging output");
    Archive archive(std::filesystem::exists(target) ? target : dump / relative);
    auto member = area * TargetProfile::area_stride + TargetProfile::placement_slot;
    auto raw = archive.raw(member);
    auto compiled = compile(archive.decoded(member));
    auto encoded = !raw.empty() && raw[0] == 0x11 ? compress(compiled) : compiled;
    require(decompress(encoded) == compiled, "Pedestrian compression readback failed");
    std::filesystem::create_directories(target.parent_path());
    archive.export_to(target, {{member, std::move(encoded)}}, std::filesystem::exists(target));
}
}
