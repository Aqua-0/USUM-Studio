#include "field/pickup_document.h"
#include "field/area.h"
#include "field/placement_document.h"
#include "field/overworld_document.h"
#include "field/map_catalog.h"
#include "formats/compression.h"
#include "core/digest.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
namespace studio {
namespace {
void coordinate(float v) {
    require(std::isfinite(v) && std::abs(v) < 1e7f,
            "Pickup position must be finite and within the supported world range");
}
SpatialPoint point(View b, std::size_t p) {
    return {f32(b, p), f32(b, p + 4), f32(b, p + 8)};
}
}
std::vector<std::string> load_pickup_item_names(const std::filesystem::path &dump) {
    Archive text(dump / TargetProfile::location_text_archive);
    return decode_location_text(text.decoded(TargetProfile::item_names_member));
}
PickupDocument::PickupDocument(unsigned area, Bytes original)
    : area_(area), original_(std::move(original)), current_(original_), saved_(original_),
      hash_(sha256(original_)), history_{current_} {
    auto ed = Container::parse(original_, "ED");
    require(ed.files.size() > TargetProfile::pickup_placement_pack,
            "Field area has no pickup category");
    auto &pack = ed.files[TargetProfile::pickup_placement_pack];
    if (pack.empty())
        return;
    auto zones = Container::parse(pack);
    auto base = u32(original_, 4 + TargetProfile::pickup_placement_pack * 4);
    std::map<std::size_t, unsigned> owners;
    for (unsigned z = 0; z < zones.files.size(); ++z) {
        auto &b = zones.files[z];
        if (b.empty())
            continue;
        auto count = u32(b, 0);
        require(count <= 4096, "Excessive pickup records");
        auto end = 4 + std::size_t(count) * 64;
        slice(b, 0, end);
        auto zone_base = std::size_t(base) + u32(pack, 4 + z * 4);
        for (unsigned row = 0; row < count; ++row) {
            auto local = 4 + std::size_t(row) * 64, p = zone_base + local;
            require(u32(b, local) == TargetProfile::pickup_record_type,
                    "Unexpected pickup record type");
            PickupRecord r{z,
                           row,
                           u32(b, local + 44),
                           u32(b, local + 36),
                           u32(b, local + 40),
                           u16(b, local + 54),
                           p,
                           {},
                           {}};
            if (r.appearance > 9)
                r.restriction =
                    "This appearance uses a specialized pickup script; editing is not supported.";
            auto collision = u32(b, local + 60);
            if (collision) {
                require(collision >= end, "Pickup trigger overlaps placement records");
                auto n = u32(b, collision);
                require(n <= 64, "Excessive pickup trigger shapes");
                slice(b, collision + 4, n * 4);
                auto off = std::size_t(collision) + 4 + n * 4;
                constexpr unsigned sizes[] = {20, 40, 28, 36};
                for (unsigned k = 0; k < n; ++k) {
                    auto type = u32(b, collision + 4 + k * 4);
                    require(type < 4, "Unknown pickup trigger shape");
                    slice(b, off, sizes[type]);
                    if (type != 0)
                        r.restriction = "This pickup's trigger shape is not supported for editing.";
                    else {
                        r.centers.push_back(zone_base + off);
                        for (unsigned c = 0; c < 5; ++c)
                            coordinate(f32(b, off + c * 4));
                        require(f32(b, off + 12) >= 0 && f32(b, off + 16) >= 0,
                                "Invalid pickup cylinder dimensions");
                    }
                    for (unsigned c = 0; c < sizes[type]; ++c) {
                        auto [it, inserted] =
                            owners.emplace(zone_base + off + c, unsigned(records_.size()));
                        if (!inserted) {
                            r.restriction = "Shared pickup triggers are not editable.";
                            if (it->second < records_.size())
                                records_[it->second].restriction = r.restriction;
                        }
                    }
                    off += sizes[type];
                }
            }
            records_.push_back(std::move(r));
        }
    }
    if (ed.files.size() > TargetProfile::static_pack &&
        !ed.files[TargetProfile::static_pack].empty()) {
        PlacementDocument models(area_, original_);
        for (auto &r : records_) {
            std::vector<unsigned> matches;
            for (unsigned i = 0; i < models.entries().size(); ++i) {
                auto &e = models.entries()[i];
                if (e.zone == r.local_zone && e.source.event == r.event)
                    matches.push_back(i);
            }
            if (matches.size() == 1) {
                auto i = matches.front();
                auto &e = models.entries()[i];
                bool same =
                    point(original_, e.offset + 4) == point(original_, r.offset + 4) &&
                    std::equal(original_.begin() + e.offset + 32, original_.begin() + e.offset + 44,
                               original_.begin() + r.offset + 32);
                if (!same || !e.restriction.empty())
                    r.movement_restriction = "The linked object has different placement data; "
                                             "movement needs reconciliation.";
                else {
                    r.visual = int(i);
                    r.visual_flag = e.offset + 36;
                    r.centers.push_back(e.offset + 4);
                    if (e.source.collision) {
                        auto base = e.offset - 4 - e.row * 56;
                        auto start = base + e.source.collision;
                        auto count = u32(original_, start);
                        auto p = start + 4 + count * 4;
                        constexpr unsigned sizes[] = {20, 40, 28, 36};
                        for (unsigned k = 0; k < count; ++k) {
                            auto type = u32(original_, start + 4 + k * 4);
                            require(type < 4, "Unknown linked pickup collision");
                            slice(original_, p, sizes[type]);
                            r.centers.push_back(p);
                            if (type == 2 || type == 3)
                                r.centers.push_back(p + 12);
                            if (type == 3)
                                r.centers.push_back(p + 24);
                            p += sizes[type];
                        }
                    }
                }
            } else if (!matches.empty() || r.appearance == 0 || r.appearance == 1 ||
                       r.appearance == 3)
                r.movement_restriction = "A unique linked pickup object could not be resolved; "
                                         "reward editing is available.";
        }
    }
    for (auto &r : records_)
        if (r.visual < 0 && (r.appearance == 0 || r.appearance == 1 || r.appearance == 3) &&
            r.movement_restriction.empty())
            r.movement_restriction =
                "A unique linked pickup object could not be resolved; reward editing is available.";
    auto allow = [&](std::size_t p, unsigned n) {
        for (unsigned i = 0; i < n; ++i)
            allowed_.push_back(p + i);
    };
    for (auto &r : records_)
        if (r.restriction.empty()) {
            allow(r.offset + 4, 12);
            allow(r.offset + 48, 6);
            if (r.movement_restriction.empty()) {
                allow(r.offset + 36, 4);
                if (r.visual_flag)
                    allow(r.visual_flag, 4);
            }
            for (auto c : r.centers)
                allow(c, 12);
        }
    std::sort(allowed_.begin(), allowed_.end());
    validate(current_);
}
void PickupDocument::validate(View bytes) const {
    require(bytes.size() == original_.size(), "Pickup editing must preserve resource size");
    for (auto &r : records_) {
        auto flag = u32(bytes, r.offset + 36);
        if (flag != r.condition) {
            require(r.movement_restriction.empty(),
                    "Collection flag editing needs a verified pickup object link");
            require(flag > 0 && flag < TargetProfile::saved_event_flag_count,
                    "Collection flag must be from 1 to 4927");
            for (auto &other : records_)
                if (other.offset != r.offset)
                    require(flag != u32(bytes, other.offset + 36),
                            "Collection flag is already used by another pickup in this area");
        }
        if (r.visual_flag)
            require(u32(bytes, r.visual_flag) == flag,
                    "Pickup and linked object collection flags disagree");
        for (unsigned k = 0; k < 3; ++k)
            coordinate(f32(bytes, r.offset + 4 + k * 4));
        for (auto c : r.centers)
            for (unsigned k = 0; k < 3; ++k) {
                coordinate(f32(bytes, c + k * 4));
                auto delta =
                    f32(bytes, r.offset + 4 + k * 4) - f32(original_, r.offset + 4 + k * 4);
                require(f32(bytes, c + k * 4) == f32(original_, c + k * 4) + delta,
                        "Pickup and linked trigger/object positions disagree");
            }
        if (point(bytes, r.offset + 4) != point(original_, r.offset + 4))
            require(r.movement_restriction.empty(), r.movement_restriction);
        auto item = u32(bytes, r.offset + 48);
        auto quantity = u16(bytes, r.offset + 52);
        if (item != u32(original_, r.offset + 48) || quantity != u16(original_, r.offset + 52)) {
            require(item > 0, "Choose an item other than None");
            require(quantity > 0, "Pickup quantity must be positive");
        }
    }
}
PickupValues PickupDocument::values(unsigned index) const {
    auto p = records_.at(index).offset;
    return {point(current_, p + 4), u32(current_, p + 48), u16(current_, p + 52),
            u32(current_, p + 36)};
}
void PickupDocument::set(unsigned index, const PickupValues &v) {
    auto &r = records_.at(index);
    require(r.restriction.empty(), r.restriction);
    require(v.item > 0 && v.quantity > 0 && v.quantity <= 65535,
            "Choose a nonzero item and a quantity from 1 to 65535");
    auto before = values(index);
    if (v.position != before.position)
        require(r.movement_restriction.empty(), r.movement_restriction);
    auto next = current_;
    put32(next, r.offset + 36, v.flag);
    if (r.visual_flag)
        put32(next, r.visual_flag, v.flag);
    for (unsigned k = 0; k < 3; ++k) {
        put_float(next, r.offset + 4 + k * 4, v.position[k]);
        auto delta = v.position[k] - f32(original_, r.offset + 4 + k * 4);
        for (auto c : r.centers)
            put_float(next, c + k * 4, f32(original_, c + k * 4) + delta);
    }
    put32(next, r.offset + 48, v.item);
    put16(next, r.offset + 52, std::uint16_t(v.quantity));
    validate(next);
    current_ = std::move(next);
}
void PickupDocument::validate_flags(const std::filesystem::path &dump,
                                    const ArchiveSources &sources) const {
    std::set<unsigned> flags;
    for (auto &r : records_)
        if (auto flag = u32(current_, r.offset + 36); flag != r.condition)
            require(flags.insert(flag).second,
                    "Edited pickups must have independent collection flags");
    if (flags.empty())
        return;
    Archive field(sources.resolve(dump, GameProfile::field_archive(dump)));
    for (unsigned area = 0; area < field.size() / TargetProfile::area_stride; ++area) {
        OverworldDocument check(area, field.decoded(area * TargetProfile::area_stride), {}, {});
        for (auto &e : check.entries())
            require(!flags.contains(e.condition), "Collection flag " + std::to_string(e.condition) +
                                                      " is already used by a placement in area " +
                                                      std::to_string(area));
    }
}
void PickupDocument::validate_items(const std::vector<std::string> &names) const {
    for (auto &r : records_) {
        auto id = u32(current_, r.offset + 48);
        if (id != u32(original_, r.offset + 48) ||
            u16(current_, r.offset + 52) != u16(original_, r.offset + 52))
            require(id > 0 && id < names.size() && !names[id].empty(),
                    "Pickup item is not present in the dump's item catalog");
    }
}
void PickupDocument::commit() {
    if (current_ == history_[cursor_])
        return;
    history_.resize(++cursor_);
    history_.push_back(current_);
}
void PickupDocument::cancel() {
    current_ = history_[cursor_];
}
void PickupDocument::undo() {
    cancel();
    if (can_undo())
        current_ = history_[--cursor_];
}
void PickupDocument::redo() {
    cancel();
    if (can_redo())
        current_ = history_[++cursor_];
}
std::string PickupDocument::serialize() const {
    std::ostringstream s;
    s << "USUMSTUDIO_PICKUPS 1\narea " << area_ << "\nsource " << hash_ << '\n';
    for (auto p : allowed_)
        if (current_[p] != original_[p])
            s << "byte " << p << ' ' << unsigned(current_[p]) << '\n';
    s << "end\n";
    return s.str();
}
void PickupDocument::restore(const std::string &patch) {
    std::istringstream s(patch);
    std::string word, hash;
    unsigned version, area;
    require(bool(s >> word >> version) && word == "USUMSTUDIO_PICKUPS" && version == 1,
            "Unsupported pickup document");
    require(bool(s >> word >> area) && word == "area" && area == area_,
            "Pickup document belongs to another area");
    require(bool(s >> word >> hash) && word == "source" && hash == hash_,
            "Pickup source changed; reopen the matching source");
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
                "Invalid pickup document change");
        next[p] = std::uint8_t(value);
    }
    s >> std::ws;
    require(end && s.eof(), "Incomplete pickup document");
    validate(next);
    current_ = std::move(next);
    commit();
    mark_saved();
}
void PickupDocument::export_to(const std::filesystem::path &dump, const ArchiveSources &sources,
                               const std::filesystem::path &folder) const {
    auto source = sources.resolve(dump, GameProfile::field_archive(dump)),
         target = folder / GameProfile::field_archive(dump);
    require(std::filesystem::absolute(source).lexically_normal() !=
                std::filesystem::absolute(target).lexically_normal(),
            "Choose a separate staging output");
    Archive archive(source);
    auto member = area_ * TargetProfile::area_stride + TargetProfile::placement_slot;
    require(sha256(archive.decoded(member)) == hash_, "Pickup source changed after loading");
    validate_items(load_pickup_item_names(dump));
    validate_flags(dump, sources);
    auto raw = archive.raw(member);
    auto encoded =
        changed() ? (!raw.empty() && raw[0] == 0x11 ? compress(current_) : current_) : raw;
    require(decompress(encoded) == current_, "Pickup compression readback failed");
    std::filesystem::create_directories(target.parent_path());
    archive.export_to(target, {{member, std::move(encoded)}});
}
}
