#include "field/warp_document.h"
#include "field/field_systems.h"
#include "field/map_catalog.h"
#include "formats/container.h"
#include "formats/archive.h"
#include "formats/compression.h"
#include "core/digest.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
namespace studio {
namespace {
constexpr unsigned shape_sizes[] = {20, 40, 28, 36};
SpatialPoint point(View b, std::size_t p) {
    return {f32(b, p), f32(b, p + 4), f32(b, p + 8)};
}
void finite(float v) {
    require(std::isfinite(v) && std::abs(v) < 1e7f,
            "Warp coordinate must be finite and within the supported world range");
}
std::vector<unsigned> positions(unsigned type) {
    if (type == 2)
        return {0, 12};
    if (type == 3)
        return {0, 12, 24};
    return {0};
}
}
WarpDocument::WarpDocument(unsigned area, Bytes original)
    : area_(area), source_(original), original_(std::move(original)), current_(original_),
      saved_(original_), hash_(sha256(source_)) {
    reindex();
    history_.push_back({original_, current_, operations_});
}
void WarpDocument::reindex() {
    records_.clear();
    allowed_.clear();
    auto ed = Container::parse(original_, "ED");
    require(TargetProfile::warp_placement_pack < ed.files.size(),
            "Field area has no entrance placement category");
    auto base = u32(original_, 4 + TargetProfile::warp_placement_pack * 4);
    auto zones = Container::parse(ed.files[TargetProfile::warp_placement_pack]);
    auto allow = [&](std::size_t p, unsigned n) {
        for (unsigned k = 0; k < n; ++k)
            allowed_.push_back(p + k);
    };
    std::set<std::size_t> shape_bytes;
    for (unsigned z = 0; z < zones.files.size(); ++z) {
        auto &b = zones.files[z];
        if (b.empty())
            continue;
        auto count = u32(b, 0);
        require(count <= 4096, "Excessive entrance records");
        auto end = 4 + std::size_t(count) * 88;
        slice(b, 0, end);
        auto zone_base =
            std::size_t(base) + u32(ed.files[TargetProfile::warp_placement_pack], 4 + z * 4);
        for (unsigned row = 0; row < count; ++row) {
            auto local = 4 + std::size_t(row) * 88, p = zone_base + local;
            require(u32(b, local) == 3, "Unexpected entrance record type");
            WarpRecord record{z, row, u16(b, local + 52), u32(b, local + 44), p, {}};
            allow(p + 4, 28);
            allow(p + 48, 4);
            allow(p + 54, 2);
            allow(p + 56, 4);
            allow(p + 60, 12);
            auto collision = u32(b, local + 84);
            if (collision) {
                require(collision >= end, "Entrance collision overlaps records");
                auto n = u32(b, collision);
                require(n <= 64, "Excessive entrance trigger shapes");
                slice(b, collision + 4, n * 4);
                auto off = std::size_t(collision) + 4 + n * 4;
                for (unsigned k = 0; k < n; ++k) {
                    auto type = u32(b, collision + 4 + k * 4);
                    require(type < 4, "Unknown entrance trigger shape");
                    slice(b, off, shape_sizes[type]);
                    for (unsigned j = 0; j < shape_sizes[type]; ++j)
                        require(shape_bytes.insert(zone_base + off + j).second,
                                "Shared entrance trigger geometry is not editable");
                    record.shapes.push_back({type, zone_base + off});
                    for (auto at : positions(type))
                        allow(zone_base + off + at, 12);
                    if (type == 0)
                        allow(zone_base + off + 12, 8);
                    if (type == 1)
                        allow(zone_base + off + 28, 12);
                    if (type == 2)
                        allow(zone_base + off + 24, 4);
                    off += shape_sizes[type];
                }
            }
            records_.push_back(std::move(record));
        }
    }
    std::sort(allowed_.begin(), allowed_.end());
    validate(current_);
}
void WarpDocument::validate(View bytes) const {
    require(bytes.size() == original_.size(), "Entrance editing must preserve resource size");
    for (auto &r : records_) {
        auto p = r.offset;
        require(u32(bytes, p + 56) == u32(original_, p + 56) ||
                    u32(bytes, p + 56) < TargetProfile::entrance_transition_count,
                "Unsupported entrance transition type");
        for (unsigned k = 0; k < 3; ++k) {
            finite(f32(bytes, p + 4 + k * 4));
            finite(f32(bytes, p + 60 + k * 4));
        }
        float norm = 0;
        for (unsigned k = 0; k < 4; ++k) {
            auto v = f32(bytes, p + 16 + k * 4);
            finite(v);
            norm += v * v;
        }
        require(std::abs(norm - 1) < .01f, "Entrance facing must be a unit quaternion");
        for (auto s : r.shapes) {
            for (auto at : positions(s.type))
                for (unsigned k = 0; k < 3; ++k)
                    finite(f32(bytes, s.offset + at + k * 4));
            if (s.type == 0 || s.type == 1 || s.type == 2) {
                auto start = s.type == 0 ? 12u : s.type == 1 ? 28u : 24u;
                for (unsigned k = start; k < shape_sizes[s.type]; k += 4) {
                    auto v = f32(bytes, s.offset + k);
                    finite(v);
                    require(v >= 0, "Trigger dimensions cannot be negative");
                    if (v != f32(original_, s.offset + k))
                        require(v > 0, "Edited trigger dimensions must be positive");
                }
            }
            if (s.type == 3 && !std::equal(bytes.begin() + s.offset, bytes.begin() + s.offset + 36,
                                           original_.begin() + s.offset)) {
                auto a = point(bytes, s.offset), b = point(bytes, s.offset + 12),
                     c = point(bytes, s.offset + 24);
                SpatialPoint ab{}, ac{};
                for (unsigned k = 0; k < 3; ++k) {
                    ab[k] = b[k] - a[k];
                    ac[k] = c[k] - a[k];
                }
                auto x = ab[1] * ac[2] - ab[2] * ac[1];
                auto y = ab[2] * ac[0] - ab[0] * ac[2];
                auto z = ab[0] * ac[1] - ab[1] * ac[0];
                require(x * x + y * y + z * z > 1e-8f,
                        "Trigger triangle must have three non-collinear points");
            }
            if (s.type == 2 && (point(bytes, s.offset) != point(original_, s.offset) ||
                                point(bytes, s.offset + 12) != point(original_, s.offset + 12))) {
                auto a = point(bytes, s.offset), b = point(bytes, s.offset + 12);
                require(std::abs(a[0] - b[0]) + std::abs(a[2] - b[2]) > .001f,
                        "Trigger endpoints must differ horizontally");
            }
        }
    }
}
WarpValues WarpDocument::values(unsigned index) const {
    auto p = records_.at(index).offset;
    WarpValues v;
    v.position = point(current_, p + 4);
    v.arrival = point(current_, p + 60);
    for (unsigned k = 0; k < 4; ++k)
        v.rotation[k] = f32(current_, p + 16 + k * 4);
    v.transition_type = u32(current_, p + 56);
    v.destination_event = u32(current_, p + 48);
    v.destination_zone = u16(current_, p + 54);
    return v;
}
void WarpDocument::set(unsigned index, const WarpValues &v) {
    require(v.destination_zone <= 65535, "Destination zone is out of range");
    auto &r = records_.at(index);
    auto next = current_;
    auto before = values(index);
    auto p = r.offset;
    for (unsigned k = 0; k < 3; ++k) {
        put_float(next, p + 4 + k * 4, v.position[k]);
        put_float(next, p + 60 + k * 4, v.arrival[k]);
        auto delta = v.position[k] - before.position[k];
        if (delta != 0)
            for (auto shape : r.shapes)
                for (auto at : positions(shape.type))
                    put_float(next, shape.offset + at + k * 4,
                              f32(current_, shape.offset + at + k * 4) + delta);
    }
    for (unsigned k = 0; k < 4; ++k)
        put_float(next, p + 16 + k * 4, v.rotation[k]);
    put32(next, p + 56, v.transition_type);
    put32(next, p + 48, v.destination_event);
    put16(next, p + 54, std::uint16_t(v.destination_zone));
    validate(next);
    current_ = std::move(next);
}
float WarpDocument::shape_value(const WarpShape &shape, unsigned component) const {
    require(component * 4 < shape_sizes[shape.type], "Trigger component out of range");
    return f32(current_, shape.offset + component * 4);
}
void WarpDocument::set_shape(unsigned index, unsigned shape, unsigned component, float value) {
    auto s = records_.at(index).shapes.at(shape);
    require((s.type == 0 && component < 5) || (s.type == 2 && component < 7) ||
                (s.type == 1 && (component < 3 || (component >= 7 && component < 10))) ||
                (s.type == 3 && component < 9),
            "Unsupported trigger component");
    auto next = current_;
    put_float(next, s.offset + component * 4, value);
    validate(next);
    current_ = std::move(next);
}
std::vector<unsigned> WarpDocument::used_events(unsigned local) const {
    std::vector<unsigned> used;
    for (auto &r : records_)
        if (r.local_zone == local)
            used.push_back(r.event);
    for (auto &entry : inspect_field_systems(current_))
        if (entry.local_zone == local && entry.region.overworld && entry.region.overworld->event)
            used.push_back(entry.region.overworld->event);
    auto ed = Container::parse(current_, "ED");
    for (auto [category, size] : {std::pair{3u, 60u}, std::pair{4u, 56u}, std::pair{10u, 64u}}) {
        if (category >= ed.files.size() || ed.files[category].empty())
            continue;
        auto zones = Container::parse(ed.files[category]);
        if (local >= zones.files.size() || zones.files[local].empty())
            continue;
        auto &bytes = zones.files[local];
        auto n = u32(bytes, 0);
        slice(bytes, 4, std::size_t(n) * size);
        for (unsigned i = 0; i < n; ++i)
            used.push_back(u32(bytes, 4 + i * size + 44));
    }
    return used;
}
unsigned WarpDocument::duplicate(unsigned index) {
    auto used = used_events(records_.at(index).local_zone);
    unsigned event = 1;
    while (event <= 65535 && std::find(used.begin(), used.end(), event) != used.end())
        ++event;
    require(event <= 65535, "This zone has no free entrance event ID");
    return duplicate(index, event);
}
unsigned WarpDocument::duplicate(unsigned index, unsigned event) {
    auto record = records_.at(index);
    auto used = used_events(record.local_zone);
    require(event > 0 && event <= 65535 && std::find(used.begin(), used.end(), event) == used.end(),
            "New entrance event ID is already used in this zone");
    auto expand = [&](const Bytes &bytes) {
        auto ed = Container::parse(bytes, "ED");
        auto zones = Container::parse(ed.files[TargetProfile::warp_placement_pack]);
        auto &old = zones.files.at(record.local_zone);
        auto n = u32(old, 0), tail = 4 + n * 88;
        require(n < 4096, "Too many entrances in this zone");
        Bytes next(4 + (n + 1) * 88);
        put32(next, 0, n + 1);
        append(next, slice(old, tail, old.size() - tail));
        for (unsigned row = 0; row < n; ++row) {
            auto p = 4 + row * 88;
            std::copy_n(old.begin() + p, 88, next.begin() + p);
            if (auto q = u32(old, p + 84))
                put32(next, p + 84, q + 88);
        }
        auto from = 4 + record.row * 88, to = 4 + n * 88;
        std::copy_n(old.begin() + from, 88, next.begin() + to);
        put32(next, to + 44, event);
        auto collision = u32(old, from + 84);
        if (collision) {
            auto count = u32(old, collision);
            auto end = collision + 4 + count * 4;
            for (unsigned k = 0; k < count; ++k) {
                auto type = u32(old, collision + 4 + k * 4);
                require(type < 4, "Unknown entrance trigger shape");
                end += shape_sizes[type];
            }
            put32(next, to + 84, narrow(next.size()));
            append(next, slice(old, collision, end - collision));
        }
        zones.files[record.local_zone] = std::move(next);
        ed.files[TargetProfile::warp_placement_pack] = zones.write();
        return ed.write();
    };
    commit();
    auto baseline = expand(original_), edited = expand(current_);
    original_ = std::move(baseline);
    current_ = std::move(edited);
    operations_.push_back({0, index, event, 0});
    reindex();
    unsigned result = 0;
    for (unsigned i = 0; i < records_.size(); ++i)
        if (records_[i].local_zone == record.local_zone && records_[i].event == event)
            result = i;
    commit();
    return result;
}
void WarpDocument::remove(unsigned index) {
    const auto record = records_.at(index);
    auto rebuild = [&](const Bytes &bytes) {
        auto ed = Container::parse(bytes, "ED");
        auto zones = Container::parse(ed.files[TargetProfile::warp_placement_pack]);
        auto &zone = zones.files.at(record.local_zone);
        const auto count = u32(zone, 0), start = 4 + record.row * 88;
        const auto collision = u32(zone, start + 84);
        std::size_t collision_end = collision;
        if (collision) {
            const auto shapes = u32(zone, collision);
            collision_end += 4 + shapes * 4;
            for (unsigned k = 0; k < shapes; ++k)
                collision_end += shape_sizes[u32(zone, collision + 4 + k * 4)];
            slice(zone, collision, collision_end - collision);
        }
        Bytes next(zone.begin(), zone.begin() + start);
        append(next, slice(zone, start + 88, zone.size() - start - 88));
        if (collision)
            next.erase(next.begin() + collision - 88, next.begin() + collision_end - 88);
        put32(next, 0, count - 1);
        for (unsigned row = 0; row < count - 1; ++row) {
            const auto p = 4 + row * 88 + 84;
            auto pointer = u32(next, p);
            if (!pointer)
                continue;
            require(!collision || pointer < collision || pointer >= collision_end,
                    "Shared entrance trigger geometry cannot be deleted");
            pointer -= 88;
            if (collision && u32(next, p) >= collision_end)
                pointer -= unsigned(collision_end - collision);
            put32(next, p, pointer);
        }
        zone = std::move(next);
        ed.files[TargetProfile::warp_placement_pack] = zones.write();
        return ed.write();
    };
    auto baseline = rebuild(original_), edited = rebuild(current_);
    commit();
    original_ = std::move(baseline);
    current_ = std::move(edited);
    operations_.push_back({2, index, 0, 0});
    reindex();
    commit();
}
void WarpDocument::change_shape_type(unsigned index, unsigned shape, unsigned type) {
    require(type < 4, "Unknown entrance trigger shape");
    auto record = records_.at(index);
    auto old_shape = record.shapes.at(shape);
    if (old_shape.type == type)
        return;
    auto rebuild = [&](const Bytes &bytes) {
        auto ed = Container::parse(bytes, "ED");
        auto zones = Container::parse(ed.files[TargetProfile::warp_placement_pack]);
        auto &zone = zones.files.at(record.local_zone);
        auto collision = u32(zone, 4 + record.row * 88 + 84);
        auto count = u32(zone, collision);
        std::size_t begin = collision + 4 + count * 4;
        for (unsigned k = 0; k < shape; ++k)
            begin += shape_sizes[u32(zone, collision + 4 + k * 4)];
        auto end = begin + shape_sizes[old_shape.type];
        auto anchor = point(zone, begin);
        Bytes replacement(shape_sizes[type]);
        for (unsigned k = 0; k < 3; ++k)
            put_float(replacement, k * 4, anchor[k]);
        if (type == 0) {
            put_float(replacement, 12, 50);
            put_float(replacement, 16, 100);
        } else if (type == 1) {
            put_float(replacement, 24, 1);
            put_float(replacement, 28, 50);
            put_float(replacement, 32, 100);
            put_float(replacement, 36, 50);
        } else {
            for (unsigned k = 0; k < 3; ++k)
                put_float(replacement, 12 + k * 4, anchor[k] + (k == 0 ? 100 : 0));
            if (type == 2)
                put_float(replacement, 24, 100);
            else
                for (unsigned k = 0; k < 3; ++k)
                    put_float(replacement, 24 + k * 4, anchor[k] + (k == 1 ? 100 : 0));
        }
        Bytes next(zone.begin(), zone.begin() + begin);
        append(next, replacement);
        append(next, slice(zone, end, zone.size() - end));
        put32(next, collision + 4 + shape * 4, type);
        const auto delta = std::int64_t(replacement.size()) - std::int64_t(end - begin);
        for (unsigned row = 0; row < u32(zone, 0); ++row) {
            auto p = 4 + row * 88 + 84;
            auto pointer = u32(zone, p);
            if (pointer >= end)
                put32(next, p, std::uint32_t(std::int64_t(pointer) + delta));
        }
        zone = std::move(next);
        ed.files[TargetProfile::warp_placement_pack] = zones.write();
        return ed.write();
    };
    commit();
    auto baseline = rebuild(original_), edited = rebuild(current_);
    original_ = std::move(baseline);
    current_ = std::move(edited);
    operations_.push_back({1, index, shape, type});
    reindex();
    commit();
}
std::vector<EntranceBehavior> load_entrance_behaviors(const std::filesystem::path &dump,
                                                      const ArchiveSources &sources) {
    auto resources = Container::parse(
        Archive(sources.resolve(dump, TargetProfile::resident_archive)).decoded(0), "FR");
    const auto &bytes = resources.files.at(TargetProfile::entrance_behavior_resource);
    const char *names[] = {
        "Normal passage",   "Door",           "Door mat",         "Ladder up",
        "Ladder down",      "Rope ladder up", "Rope ladder down", "Gate",
        "Normal passage C", "Door mat C",     "Outdoor passage",  "Outdoor passage C",
        "Water passage",    "Water passage C"};
    std::vector<EntranceBehavior> result;
    for (unsigned i = 0; i < TargetProfile::entrance_transition_count; ++i) {
        auto mode = u32(bytes, i * 12);
        require(mode <= 2, "Unrecognized entrance activation mode");
        result.push_back({names[i], mode, u32(bytes, i * 12 + 4) != 0, u32(bytes, i * 12 + 8)});
    }
    return result;
}
void WarpDocument::commit() {
    if (current_ == history_[cursor_].current)
        return;
    history_.resize(++cursor_);
    history_.push_back({original_, current_, operations_});
}
void WarpDocument::load_snapshot(const Snapshot &snapshot) {
    original_ = snapshot.original;
    current_ = snapshot.current;
    operations_ = snapshot.operations;
    reindex();
}
void WarpDocument::cancel() {
    load_snapshot(history_[cursor_]);
}
void WarpDocument::undo() {
    cancel();
    if (can_undo())
        load_snapshot(history_[--cursor_]);
}
void WarpDocument::redo() {
    cancel();
    if (can_redo())
        load_snapshot(history_[++cursor_]);
}
void WarpDocument::validate_destinations(const std::vector<WarpDestination> &list) const {
    auto resolved = list;
    std::erase_if(resolved, [&](const auto &d) {
        return d.area == area_;
    });
    for (const auto &r : records_)
        resolved.push_back({area_, r.zone, r.event, {}});
    for (unsigned i = 0; i < records_.size(); ++i) {
        auto v = values(i);
        auto count = std::count_if(resolved.begin(), resolved.end(), [&](auto &d) {
            return d.zone == v.destination_zone && d.event == v.destination_event;
        });
        require(count == 1, "Entrance " + std::to_string(records_[i].event) +
                                " targets missing or ambiguous zone " +
                                std::to_string(v.destination_zone) + " / entrance " +
                                std::to_string(v.destination_event));
    }
}
std::string WarpDocument::serialize() const {
    std::ostringstream s;
    s << "USUMSTUDIO_WARPS " << 4 << "\narea " << area_ << "\nsource " << hash_ << '\n';
    for (auto op : operations_) {
        if (op[0] == 0)
            s << "clone " << op[1] << ' ' << op[2] << '\n';
        else if (op[0] == 2)
            s << "delete " << op[1] << '\n';
        else
            s << "shape " << op[1] << ' ' << op[2] << ' ' << op[3] << '\n';
    }
    for (auto p : allowed_)
        if (current_[p] != original_[p])
            s << "byte " << p << ' ' << unsigned(current_[p]) << '\n';
    s << "end\n";
    return s.str();
}
void WarpDocument::restore(const std::string &patch) {
    std::istringstream s(patch);
    std::string word, hash;
    unsigned version, area;
    require(bool(s >> word >> version) && word == "USUMSTUDIO_WARPS" &&
                (version >= 1 && version <= 4),
            "Unsupported entrance document");
    require(bool(s >> word >> area) && word == "area" && area == area_,
            "Entrance document belongs to another area");
    require(bool(s >> word >> hash) && word == "source" && hash == hash_,
            "Entrance source changed; reopen the matching project source");
    WarpDocument restored(area_, source_);
    auto next = source_;
    std::set<std::size_t> seen;
    bool end = false;
    while (s >> word) {
        if (word == "end") {
            end = true;
            break;
        }
        if (word == "clone") {
            unsigned index, event;
            require(version >= 2 && seen.empty() && bool(s >> index >> event),
                    "Invalid entrance template operation");
            restored.duplicate(index, event);
            next = restored.current_;
            continue;
        }
        if (word == "delete") {
            unsigned index;
            require(version >= 4 && seen.empty() && bool(s >> index),
                    "Invalid entrance deletion operation");
            restored.remove(index);
            next = restored.current_;
            continue;
        }
        if (word == "shape") {
            unsigned index, shape, type;
            require(version >= 3 && seen.empty() && bool(s >> index >> shape >> type),
                    "Invalid entrance shape operation");
            restored.change_shape_type(index, shape, type);
            next = restored.current_;
            continue;
        }
        std::size_t p;
        unsigned value;
        require(word == "byte" && bool(s >> p >> value) && value <= 255 &&
                    std::binary_search(restored.allowed_.begin(), restored.allowed_.end(), p) &&
                    seen.insert(p).second,
                "Invalid entrance document change");
        next[p] = std::uint8_t(value);
    }
    s >> std::ws;
    require(end && s.eof(), "Incomplete entrance document");
    restored.validate(next);
    original_ = std::move(restored.original_);
    operations_ = std::move(restored.operations_);
    current_ = std::move(next);
    reindex();
    history_ = {{original_, current_, operations_}};
    cursor_ = 0;
    mark_saved();
}
std::vector<WarpDestination> load_warp_destinations(const std::filesystem::path &dump,
                                                    const ArchiveSources &sources) {
    Archive archive(sources.resolve(dump, GameProfile::field_archive(dump)));
    require(archive.size() % TargetProfile::area_stride == 0, "Invalid field area archive");
    std::vector<WarpDestination> out;
    auto catalog = load_map_catalog(dump, sources);
    for (unsigned area = 0; area < archive.size() / TargetProfile::area_stride; ++area) {
        WarpDocument doc(area, archive.decoded(area * TargetProfile::area_stride +
                                               TargetProfile::placement_slot));
        for (unsigned i = 0; i < doc.records().size(); ++i) {
            const auto &r = doc.records()[i];
            const auto v = doc.values(i);
            std::string name = "Zone " + std::to_string(r.zone);
            for (auto &location : catalog.locations)
                if (location.zone == int(r.zone)) {
                    name = location.name + " / " + name;
                    break;
                }
            out.push_back({area, r.zone, r.event, name + " / entrance " + std::to_string(r.event),
                           std::pair{v.destination_zone, v.destination_event}});
        }
    }
    return out;
}
void WarpDocument::export_to(const std::filesystem::path &dump, const ArchiveSources &sources,
                             const std::filesystem::path &folder) const {
    auto source = sources.resolve(dump, GameProfile::field_archive(dump)),
         target = folder / GameProfile::field_archive(dump);
    require(std::filesystem::absolute(source).lexically_normal() !=
                std::filesystem::absolute(target).lexically_normal(),
            "Choose a separate staging output");
    Archive archive(source);
    auto member = area_ * TargetProfile::area_stride + TargetProfile::placement_slot;
    require(sha256(archive.decoded(member)) == hash_, "Entrance source changed after loading");
    validate_destinations(load_warp_destinations(dump, sources));
    std::filesystem::create_directories(target.parent_path());
    auto raw = archive.raw(member);
    auto bytes = changed() ? (raw[0] == 0x11 ? compress(current_) : current_) : raw;
    require(decompress(bytes) == current_, "Entrance compression readback failed");
    archive.export_to(target, {{member, std::move(bytes)}});
}
}
