#include "field/warp_document.h"
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
    : area_(area), original_(std::move(original)), current_(original_), saved_(original_),
      hash_(sha256(original_)), history_{current_} {
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
            if (s.type == 0 || s.type == 2) {
                auto start = s.type == 0 ? 12u : 24u;
                for (unsigned k = start; k < shape_sizes[s.type]; k += 4) {
                    auto v = f32(bytes, s.offset + k);
                    finite(v);
                    require(v >= 0, "Trigger dimensions cannot be negative");
                    if (v != f32(original_, s.offset + k))
                        require(v > 0, "Edited trigger dimensions must be positive");
                }
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
    require((s.type == 0 && component < 5) || (s.type == 2 && component < 7),
            "Only existing cylinder and line trigger properties are editable");
    auto next = current_;
    put_float(next, s.offset + component * 4, value);
    validate(next);
    current_ = std::move(next);
}
void WarpDocument::commit() {
    if (current_ == history_[cursor_])
        return;
    history_.resize(++cursor_);
    history_.push_back(current_);
}
void WarpDocument::cancel() {
    current_ = history_[cursor_];
}
void WarpDocument::undo() {
    cancel();
    if (can_undo())
        current_ = history_[--cursor_];
}
void WarpDocument::redo() {
    cancel();
    if (can_redo())
        current_ = history_[++cursor_];
}
void WarpDocument::validate_destinations(const std::vector<WarpDestination> &list) const {
    for (unsigned i = 0; i < records_.size(); ++i) {
        auto v = values(i);
        auto count = std::count_if(list.begin(), list.end(), [&](auto &d) {
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
    s << "USUMSTUDIO_WARPS 1\narea " << area_ << "\nsource " << hash_ << '\n';
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
    require(bool(s >> word >> version) && word == "USUMSTUDIO_WARPS" && version == 1,
            "Unsupported entrance document");
    require(bool(s >> word >> area) && word == "area" && area == area_,
            "Entrance document belongs to another area");
    require(bool(s >> word >> hash) && word == "source" && hash == hash_,
            "Entrance source changed; reopen the matching project source");
    auto next = original_;
    std::set<std::size_t> seen;
    bool end = false;
    while (s >> word) {
        if (word == "end") {
            end = true;
            break;
        }
        std::size_t p;
        unsigned value;
        require(word == "byte" && bool(s >> p >> value) && value <= 255 &&
                    std::binary_search(allowed_.begin(), allowed_.end(), p) &&
                    seen.insert(p).second,
                "Invalid entrance document change");
        next[p] = std::uint8_t(value);
    }
    s >> std::ws;
    require(end && s.eof(), "Incomplete entrance document");
    validate(next);
    current_ = std::move(next);
    commit();
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
        for (auto &r : doc.records()) {
            std::string name = "Zone " + std::to_string(r.zone);
            for (auto &location : catalog.locations)
                if (location.zone == int(r.zone)) {
                    name = location.name + " / " + name;
                    break;
                }
            out.push_back({area, r.zone, r.event, name + " / entrance " + std::to_string(r.event)});
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
