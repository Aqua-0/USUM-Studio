#include "field/overworld_document.h"
#include "field/pickup_document.h"
#include "field/warp_document.h"
#include "core/digest.h"
#include "formats/compression.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
namespace studio {
namespace {
struct Layout {
    unsigned category, size, type, model, script, alias;
    std::vector<unsigned> pointers, shapes;
};
const Layout layouts[] = {{10, 64, 10, 0, 0, 0, {60}, {60}},
                          {4, 56, 4, 48, 0, 50, {52}, {52}},
                          {1, 120, 1, 52, 56, 104, {112, 116}, {112, 116}},
                          {7, 84, 7, 48, 52, 56, {60, 64, 68, 72, 76, 80}, {64, 68}},
                          {2, 88, 3, 0, 0, 0, {84}, {84}},
                          {3, 60, 2, 0, 48, 0, {56}, {56}},
                          {0, 60, 0, 0, 44, 0, {56}, {56}}};
const Layout &layout(OverworldKind kind) {
    return layouts[unsigned(kind)];
}
SpatialPoint position(View b, unsigned p = 4) {
    return {f32(b, p), f32(b, p + 4), f32(b, p + 8)};
}
Bytes record(View original, const OverworldEntry &e) {
    auto ed = Container::parse(original, "ED");
    auto zones = Container::parse(ed.files.at(layout(e.kind).category));
    auto r = slice(zones.files.at(e.zone), 4 + e.row * layout(e.kind).size, layout(e.kind).size);
    return Bytes(r.begin(), r.end());
}
Bytes shape(View zone, unsigned offset, SpatialPoint delta) {
    auto count = u32(zone, offset);
    require(count <= 64, "Too many placement shapes");
    auto start = offset + 4 + count * 4, p = start;
    constexpr unsigned sizes[] = {20, 40, 28, 36};
    for (unsigned i = 0; i < count; ++i) {
        auto type = u32(zone, offset + 4 + i * 4);
        require(type < 4, "Unknown placement shape");
        slice(zone, p, sizes[type]);
        p += sizes[type];
    }
    auto view = slice(zone, offset, p - offset);
    Bytes out(view.begin(), view.end());
    p = start - offset;
    for (unsigned i = 0; i < count; ++i) {
        auto type = u32(zone, offset + 4 + i * 4);
        for (unsigned at = 0; at < (type == 3 ? 36u : type == 2 ? 24u : 12u); at += 12)
            for (unsigned k = 0; k < 3; ++k)
                put_float(out, p + at + k * 4, f32(out, p + at + k * 4) + delta[k]);
        p += sizes[type];
    }
    return out;
}
unsigned model_id(View r, OverworldKind kind) {
    auto &l = layout(kind);
    return !l.model ? 0 : kind == OverworldKind::StaticObject ? u16(r, l.model) : u32(r, l.model);
}
bool same_position(View a, View b) {
    return position(a) == position(b);
}
}
const char *overworld_kind_name(OverworldKind kind) {
    static const char *names[] = {"Item pickup", "Static prop",         "NPC",          "Trainer",
                                  "Warp",        "Scenery interaction", "Story trigger"};
    return names[unsigned(kind)];
}
OverworldDocument::OverworldDocument(unsigned area, Bytes placements, Bytes characters,
                                     Bytes objects)
    : area_(area), original_(std::move(placements)), characters_(std::move(characters)),
      objects_(std::move(objects)) {
    hash_ = sha256(original_) + sha256(characters_) + sha256(objects_);
    auto ed = Container::parse(original_, "ED");
    for (unsigned k = 0; k < std::size(layouts); ++k) {
        auto kind = OverworldKind(k);
        auto &l = layouts[k];
        if (l.category >= ed.files.size() || ed.files[l.category].empty())
            continue;
        auto zones = Container::parse(ed.files[l.category]);
        for (unsigned z = 0; z < zones.files.size(); ++z) {
            auto &bytes = zones.files[z];
            if (bytes.empty())
                continue;
            auto count = u32(bytes, 0);
            require(count <= 4096, "Excessive overworld placement count");
            slice(bytes, 4, count * l.size);
            for (unsigned i = 0; i < count; ++i) {
                auto r = slice(bytes, 4 + i * l.size, l.size);
                require(u32(r, 0) == l.type, "Unexpected placement record type");
                OverworldEntry e{kind,
                                 z,
                                 i,
                                 kind == OverworldKind::StoryTrigger ? 0 : u32(r, 44),
                                 model_id(r, kind),
                                 l.script ? u32(r, l.script) : 0,
                                 u32(r, 36),
                                 position(r),
                                 {}};
                for (auto at : l.pointers) {
                    auto p = u32(r, at);
                    require(!p || (p >= 4 + count * l.size && p < bytes.size()),
                            "Placement pointer is outside its data tail");
                }
                if (l.alias &&
                    (kind == OverworldKind::StaticObject ? u16(r, l.alias) : u32(r, l.alias)))
                    e.restriction = "Shared alias placements require editing their owning zone.";
                if (kind == OverworldKind::Trainer) {
                    if (auto p = u32(r, 76))
                        slice(bytes, p, 8 + std::size_t(u16(bytes, p)) * 12);
                    if (auto p = u32(r, 80))
                        slice(bytes, p, 4 + std::size_t(u32(bytes, p)) * 20);
                    if (auto p = u32(r, 72))
                        slice(bytes, p, 4 + std::size_t(u32(bytes, p)) * 16);
                }
                if (kind == OverworldKind::Pickup && u16(r, 54) > 9)
                    e.restriction = "Specialized pickup routine is not supported.";
                entries_.push_back(std::move(e));
            }
        }
    }
}
OverworldOperation OverworldDocument::draft(unsigned i, OverworldOperation::Action action) const {
    auto &e = entries_.at(i);
    auto r = record(original_, e);
    OverworldOperation o;
    o.entry = i;
    o.action = action;
    o.position = e.position;
    o.model = e.model;
    o.event = e.event;
    o.flag = e.condition;
    if (e.kind == OverworldKind::Pickup) {
        o.item = u32(r, 48);
        o.quantity = u16(r, 52);
    }
    if (e.kind == OverworldKind::Entrance) {
        o.destination_zone = u16(r, 54);
        o.destination_event = u32(r, 48);
    }
    if (action == OverworldOperation::Action::Add) {
        o.position[0] += 100;
        if (e.kind == OverworldKind::Pickup)
            o.flag = 0;
        std::set<unsigned> used;
        for (auto &item : entries_)
            if (item.zone == e.zone)
                used.insert(item.event);
        for (auto &pending : operations_)
            if (entries_[pending.entry].zone == e.zone)
                used.insert(pending.event);
        while (used.count(o.event) && o.event < 65535)
            ++o.event;
    }
    return o;
}
void OverworldDocument::apply(const OverworldOperation &o) {
    auto &e = entries_.at(o.entry);
    require(e.restriction.empty(), e.restriction);
    require(unsigned(o.action) <= 2, "Invalid overworld operation");
    for (auto v : o.position)
        require(std::isfinite(v) && std::abs(v) < 1e7f, "Position is outside the supported range");
    if (o.action != OverworldOperation::Action::Add)
        require(o.event == e.event, "Existing event identities are preserved");
    if (o.action == OverworldOperation::Action::Add && e.kind != OverworldKind::StoryTrigger)
        require(o.event > 0 && o.event <= 65535, "Choose a nonzero event ID up to 65535");
    if (e.kind == OverworldKind::Pickup && o.action != OverworldOperation::Action::Remove) {
        require(o.item && o.quantity && o.quantity <= 65535,
                "Choose an item and quantity from 1 to 65535");
        if (o.action == OverworldOperation::Action::Add || o.flag != e.condition)
            require(o.flag > 0 && o.flag < TargetProfile::saved_event_flag_count,
                    "Enter a reserved persistent collection flag");
    } else
        require(o.flag == e.condition, "Placement conditions are preserved");
    if (e.kind == OverworldKind::Entrance)
        require(o.destination_zone <= 65535, "Destination zone is out of range");
    if (o.action == OverworldOperation::Action::Add && e.kind != OverworldKind::StoryTrigger) {
        for (auto &other : entries_)
            require(!(other.zone == e.zone && other.event == o.event),
                    "Event ID is already used in this zone");
        auto ed = Container::parse(original_, "ED");
        if (ed.files.size() > TargetProfile::contact_placement_pack &&
            !ed.files[TargetProfile::contact_placement_pack].empty()) {
            auto zones = Container::parse(ed.files[TargetProfile::contact_placement_pack]);
            if (e.zone < zones.files.size() && !zones.files[e.zone].empty()) {
                auto &z = zones.files[e.zone];
                auto count = u32(z, 0);
                slice(z, 4, std::size_t(count) * 152);
                for (unsigned i = 0; i < count; ++i)
                    require(u32(z, 4 + i * 152 + 44) != o.event,
                            "Event ID is already used by a contact actor in this zone");
            }
        }
        for (auto &pending : operations_)
            if (pending.action == OverworldOperation::Action::Add)
                require(!(entries_[pending.entry].zone == e.zone && pending.event == o.event),
                        "Event ID is already reserved by another pending addition");
    }
    auto previous = operations_;
    operations_.push_back(o);
    try {
        placements();
    } catch (...) {
        operations_ = std::move(previous);
        throw;
    }
    history_.resize(++cursor_);
    history_.push_back(operations_);
}
void OverworldDocument::undo() {
    if (can_undo())
        operations_ = history_[--cursor_];
}
void OverworldDocument::redo() {
    if (can_redo())
        operations_ = history_[++cursor_];
}
Bytes OverworldDocument::placements() const {
    if (operations_.empty())
        return original_;
    auto ed = Container::parse(original_, "ED");
    auto expanded = operations_;
    for (auto &o : operations_) {
        auto &e = entries_.at(o.entry);
        if (e.kind == OverworldKind::StaticObject) {
            for (auto &p : entries_)
                require(
                    !(p.kind == OverworldKind::Pickup && p.zone == e.zone && p.event == e.event),
                    "Edit the linked pickup to change this visible object");
        }
        if (e.kind == OverworldKind::Pickup) {
            auto source = record(original_, e);
            std::vector<unsigned> links;
            for (unsigned i = 0; i < entries_.size(); ++i)
                if (entries_[i].kind == OverworldKind::StaticObject && entries_[i].zone == e.zone &&
                    entries_[i].event == e.event)
                    links.push_back(i);
            require(links.size() <= 1, "Ambiguous pickup object link");
            if (links.empty()) {
                auto appearance = u16(source, 54);
                require(appearance != 0 && appearance != 1 && appearance != 3,
                        "Pickup has no verified visible object");
            } else {
                auto linked = record(original_, entries_[links[0]]);
                require(same_position(source, linked) && u32(source, 36) == u32(linked, 36) &&
                            u32(source, 40) == u32(linked, 40) &&
                            u32(source, 32) == u32(linked, 32),
                        "Pickup object metadata does not match");
                require(entries_[links[0]].restriction.empty(), entries_[links[0]].restriction);
                auto copy = o;
                copy.entry = links[0];
                copy.model = entries_[links[0]].model;
                expanded.push_back(copy);
            }
        }
    }
    for (unsigned k = 0; k < std::size(layouts); ++k) {
        auto kind = OverworldKind(k);
        auto &l = layouts[k];
        if (l.category >= ed.files.size() || ed.files[l.category].empty())
            continue;
        auto zones = Container::parse(ed.files[l.category]);
        bool category_changed = false;
        for (unsigned z = 0; z < zones.files.size(); ++z) {
            std::vector<OverworldOperation> ops;
            for (auto &o : expanded) {
                auto &e = entries_[o.entry];
                if (e.kind == kind && e.zone == z)
                    ops.push_back(o);
            }
            if (ops.empty())
                continue;
            category_changed = true;
            auto original = zones.files[z];
            auto n = u32(original, 0), tail = 4 + n * l.size;
            std::map<unsigned, OverworldOperation> replacements;
            std::vector<OverworldOperation> additions;
            for (auto &o : ops) {
                if (o.action == OverworldOperation::Action::Add)
                    additions.push_back(o);
                else
                    require(replacements.emplace(entries_[o.entry].row, o).second,
                            "This source placement already has a pending edit; undo it before "
                            "changing it again");
            }
            unsigned removed = 0;
            for (auto &[row, o] : replacements)
                removed += o.action == OverworldOperation::Action::Remove;
            unsigned count = n - removed + unsigned(additions.size());
            require(count <= 4096, "Too many placements in this zone");
            std::int64_t shift = (std::int64_t(count) - n) * l.size;
            Bytes body;
            auto tail_view = slice(original, tail, original.size() - tail);
            Bytes extras(tail_view.begin(), tail_view.end());
            std::set<unsigned> events;
            auto emit = [&](unsigned row, const OverworldOperation *edit) {
                auto rview = slice(original, 4 + row * l.size, l.size);
                Bytes r(rview.begin(), rview.end());
                for (auto at : l.pointers)
                    if (auto old = u32(r, at))
                        put32(r, at, narrow(std::size_t(std::int64_t(old) + shift)));
                if (edit) {
                    auto &o = *edit;
                    auto before = position(r);
                    SpatialPoint delta;
                    for (unsigned c = 0; c < 3; ++c) {
                        delta[c] = o.position[c] - before[c];
                        put_float(r, 4 + c * 4, o.position[c]);
                    }
                    if (kind != OverworldKind::StoryTrigger)
                        put32(r, 44, o.event);
                    if (l.model) {
                        if (kind == OverworldKind::StaticObject) {
                            require(o.model <= 65535, "Static model ID is out of range");
                            put16(r, l.model, std::uint16_t(o.model));
                        } else
                            put32(r, l.model, o.model);
                    }
                    if (kind == OverworldKind::Pickup) {
                        put32(r, 36, o.flag);
                        put32(r, 48, o.item);
                        put16(r, 52, std::uint16_t(o.quantity));
                    }
                    if (kind == OverworldKind::StaticObject && o.flag != u32(r, 36))
                        put32(r, 36, o.flag);
                    if (kind == OverworldKind::Entrance) {
                        put32(r, 48, o.destination_event);
                        put16(r, 54, std::uint16_t(o.destination_zone));
                        for (unsigned c = 0; c < 3; ++c)
                            put_float(r, 60 + c * 4, f32(r, 60 + c * 4) + delta[c]);
                    }
                    if (o.action == OverworldOperation::Action::Add || delta != SpatialPoint{})
                        for (auto at : l.shapes)
                            if (auto old = u32(rview, at)) {
                                auto b = shape(original, old, delta);
                                put32(r, at, 4 + count * l.size + narrow(extras.size()));
                                append(extras, b);
                            }
                    if (kind == OverworldKind::Trainer &&
                        (o.action == OverworldOperation::Action::Add || delta != SpatialPoint{}))
                        if (auto old = u32(rview, 76)) {
                            auto count_points = u16(original, old);
                            auto view = slice(original, old, 8 + std::size_t(count_points) * 12);
                            Bytes path(view.begin(), view.end());
                            for (unsigned i = 0; i < count_points; ++i)
                                for (unsigned c = 0; c < 3; ++c)
                                    put_float(path, 8 + i * 12 + c * 4,
                                              f32(path, 8 + i * 12 + c * 4) + delta[c]);
                            put32(r, 76, 4 + count * l.size + narrow(extras.size()));
                            append(extras, path);
                        }
                }
                if (kind != OverworldKind::StoryTrigger)
                    require(events.insert(u32(r, 44)).second,
                            "Duplicate event ID within the placement category and zone");
                append(body, r);
            };
            for (unsigned row = 0; row < n; ++row) {
                auto found = replacements.find(row);
                if (found != replacements.end() &&
                    found->second.action == OverworldOperation::Action::Remove)
                    continue;
                emit(row, found == replacements.end() ? nullptr : &found->second);
            }
            for (auto &o : additions)
                emit(entries_[o.entry].row, &o);
            Bytes rebuilt(4);
            put32(rebuilt, 0, count);
            append(rebuilt, body);
            append(rebuilt, extras);
            zones.files[z] = std::move(rebuilt);
        }
        if (category_changed)
            ed.files[l.category] = zones.write(4);
    }
    return ed.write(4);
}
std::map<std::size_t, Bytes> OverworldDocument::compile(const std::filesystem::path &dump) const {
    auto placement = placements();
    std::map<std::size_t, Bytes> result;
    if (placement == original_)
        return result;
    auto base = area_ * TargetProfile::area_stride;
    result[base + TargetProfile::placement_slot] = placement;
    auto ac = Container::parse(characters_, "AC");
    std::set<unsigned> character_ids;
    for (auto &bytes : ac.files) {
        auto cp = Container::parse(bytes, "CP");
        character_ids.insert(unsigned(std::stoul(text(cp.files.at(0)))));
    }
    auto as = Container::parse(objects_, "AS");
    std::set<unsigned> object_ids;
    for (auto &b : as.files) {
        auto sm = Container::parse(b, "SM");
        object_ids.insert(u16(sm.files.at(0), 0));
    }
    std::unique_ptr<Archive> shared;
    std::set<unsigned> new_flags;
    std::vector<WarpRecord> removed_warps;
    WarpDocument warps(area_, original_);
    for (auto &o : operations_) {
        auto &e = entries_[o.entry];
        if (o.action == OverworldOperation::Action::Remove) {
            if (e.kind == OverworldKind::Entrance)
                for (auto &w : warps.records())
                    if (w.local_zone == e.zone && w.row == e.row)
                        removed_warps.push_back(w);
            continue;
        }
        if (e.kind == OverworldKind::Character || e.kind == OverworldKind::Trainer) {
            require(o.model > 1, "Player character assemblies are not ordinary NPC resources");
            if (character_ids.insert(o.model).second) {
                if (!shared)
                    shared = std::make_unique<Archive>(dump / TargetProfile::character_archive);
                require(o.model < shared->size(), "Character model is outside the shared archive");
                auto bytes = shared->decoded(o.model);
                Container::parse(bytes, "CM");
                Container cp;
                cp.tag = {'C', 'P'};
                auto id = std::to_string(o.model);
                cp.files = {Bytes(id.begin(), id.end()), std::move(bytes)};
                ac.files.push_back(cp.write(128));
            }
        }
        if (e.kind == OverworldKind::StaticObject)
            require(object_ids.contains(o.model),
                    "Choose a static resource already present in this area");
        if (e.kind == OverworldKind::Pickup) {
            auto names = load_pickup_item_names(dump);
            require(o.item < names.size() && !names[o.item].empty(),
                    "Item is absent from the dump catalog");
            if (o.action == OverworldOperation::Action::Add || o.flag != e.condition)
                require(new_flags.insert(o.flag).second,
                        "Added or edited pickups must have independent collection flags");
        }
    }
    if (!new_flags.empty() || !removed_warps.empty()) {
        Archive field(dump / GameProfile::field_archive(dump));
        for (unsigned area = 0; area < field.size() / TargetProfile::area_stride; ++area) {
            auto bytes = field.decoded(area * TargetProfile::area_stride);
            if (!new_flags.empty()) {
                OverworldDocument check(area, bytes, characters_, objects_);
                for (auto &e : check.entries())
                    require(!new_flags.contains(e.condition),
                            "The selected collection flag is already used by a placement in area " +
                                std::to_string(area));
            }
            if (!removed_warps.empty()) {
                WarpDocument check(area, area == area_ ? placement : bytes);
                for (unsigned i = 0; i < check.records().size(); ++i) {
                    auto v = check.values(i);
                    for (auto &removed : removed_warps)
                        require(!(v.destination_zone == removed.zone &&
                                  v.destination_event == removed.event),
                                "Warp is still targeted from area " + std::to_string(area) +
                                    "; redirect or delete its incoming entrance first");
                }
            }
        }
    }
    auto destinations = load_warp_destinations(dump);
    std::erase_if(destinations, [&](auto &d) {
        return d.area == area_;
    });
    WarpDocument updated(area_, placement);
    for (auto &w : updated.records())
        destinations.push_back({area_, w.zone, w.event, {}});
    for (auto &o : operations_)
        if (entries_[o.entry].kind == OverworldKind::Entrance &&
            o.action != OverworldOperation::Action::Remove)
            require(std::count_if(destinations.begin(), destinations.end(),
                                  [&](auto &d) {
                                      return d.zone == o.destination_zone &&
                                             d.event == o.destination_event;
                                  }) == 1,
                    "Choose a unique existing warp destination");
    auto ed = Container::parse(placement, "ED");
    constexpr unsigned limits[] = {16, 24, 32, 16, 24, 28, 32};
    for (unsigned kind = 0; kind < std::size(layouts); ++kind) {
        auto &l = layouts[kind];
        bool changed_category = std::any_of(operations_.begin(), operations_.end(), [&](auto &o) {
            return unsigned(entries_[o.entry].kind) == kind ||
                   (kind == unsigned(OverworldKind::StaticObject) &&
                    entries_[o.entry].kind == OverworldKind::Pickup);
        });
        if (!changed_category)
            continue;
        auto zones = Container::parse(ed.files.at(l.category));
        std::set<unsigned> aliases;
        if (l.alias)
            for (auto &z : zones.files)
                if (!z.empty())
                    for (unsigned i = 0; i < u32(z, 0); ++i) {
                        auto p = 4 + i * l.size;
                        if (kind == unsigned(OverworldKind::StaticObject) ? u16(z, p + l.alias)
                                                                          : u32(z, p + l.alias))
                            aliases.insert(u32(z, p + 44));
                    }
        std::vector<Bytes> contact_zones;
        if (kind == unsigned(OverworldKind::Character) &&
            ed.files.size() > TargetProfile::contact_placement_pack &&
            !ed.files[TargetProfile::contact_placement_pack].empty()) {
            contact_zones = Container::parse(ed.files[TargetProfile::contact_placement_pack]).files;
            for (auto &z : contact_zones)
                if (!z.empty()) {
                    auto count = u32(z, 0);
                    slice(z, 4, std::size_t(count) * 152);
                    for (unsigned i = 0; i < count; ++i)
                        if (u32(z, 4 + i * 152 + 104))
                            aliases.insert(u32(z, 4 + i * 152 + 44));
                }
        }
        for (unsigned zone = 0; zone < zones.files.size(); ++zone) {
            auto &z = zones.files[zone];
            unsigned count = unsigned(aliases.size());
            if (zone < contact_zones.size() && !contact_zones[zone].empty()) {
                auto &contacts = contact_zones[zone];
                for (unsigned i = 0; i < u32(contacts, 0); ++i)
                    if (!u32(contacts, 4 + i * 152 + 104))
                        ++count;
            }
            if (!z.empty())
                for (unsigned i = 0; i < u32(z, 0); ++i) {
                    auto p = 4 + i * l.size;
                    bool alias = l.alias && (kind == unsigned(OverworldKind::StaticObject)
                                                 ? u16(z, p + l.alias)
                                                 : u32(z, p + l.alias));
                    if (!alias)
                        ++count;
                }
            require(count <= limits[kind], std::string(overworld_kind_name(OverworldKind(kind))) +
                                               " usage in zone slot " + std::to_string(zone) +
                                               " is " + std::to_string(count) + " / " +
                                               std::to_string(limits[kind]) +
                                               " slots; remove placements or choose another zone");
        }
    }
    auto chars = ac.write(128);
    if (chars != characters_)
        result[base + TargetProfile::character_resource_slot] = std::move(chars);
    return result;
}
void OverworldDocument::export_to(const std::filesystem::path &dump,
                                  const std::filesystem::path &output) const {
    Archive field(dump / GameProfile::field_archive(dump));
    auto base = area_ * TargetProfile::area_stride;
    require(field.decoded(base) == original_ &&
                field.decoded(base + TargetProfile::character_resource_slot) == characters_ &&
                field.decoded(base + TargetProfile::static_resource_slot) == objects_,
            "Overworld source changed; reopen against the matching baseline");
    auto changes = compile(dump);
    for (auto &[member, bytes] : changes) {
        auto raw = field.raw(member);
        auto encoded = !raw.empty() && raw.front() == 0x11 ? compress(bytes) : bytes;
        require(decompress(encoded) == bytes, "Overworld compression readback failed");
        bytes = std::move(encoded);
    }
    if (!changes.empty()) {
        auto target = output / GameProfile::field_archive(dump);
        require(std::filesystem::absolute(target).lexically_normal() !=
                    std::filesystem::absolute(dump / GameProfile::field_archive(dump))
                        .lexically_normal(),
                "Export must not replace the source");
        std::filesystem::create_directories(target.parent_path());
        field.export_to(target, changes);
    }
}
std::string OverworldDocument::serialize() const {
    std::ostringstream s;
    s << "USUMSTUDIO_OVERWORLD 1\narea " << area_ << "\nsource " << hash_ << '\n'
      << std::setprecision(9);
    for (auto &o : operations_)
        s << "operation " << unsigned(o.action) << ' ' << o.entry << ' ' << o.event << ' '
          << o.model << ' ' << o.flag << ' ' << o.item << ' ' << o.quantity << ' '
          << o.destination_zone << ' ' << o.destination_event << ' ' << o.position[0] << ' '
          << o.position[1] << ' ' << o.position[2] << '\n';
    s << "end\n";
    return s.str();
}
void OverworldDocument::restore(const std::string &text) {
    std::istringstream s(text);
    std::string tag, hash;
    unsigned version, area;
    require(bool(s >> tag >> version) && tag == "USUMSTUDIO_OVERWORLD" && version == 1,
            "Unsupported overworld document");
    require(bool(s >> tag >> area) && tag == "area" && area == area_, "Wrong overworld area");
    require(bool(s >> tag >> hash) && tag == "source" && hash == hash_,
            "Overworld source fingerprint differs");
    OverworldDocument next(area_, original_, characters_, objects_);
    bool end = false;
    while (s >> tag) {
        if (tag == "end") {
            end = true;
            break;
        }
        OverworldOperation o;
        unsigned action;
        require(tag == "operation" &&
                    bool(s >> action >> o.entry >> o.event >> o.model >> o.flag >> o.item >>
                         o.quantity >> o.destination_zone >> o.destination_event >> o.position[0] >>
                         o.position[1] >> o.position[2]) &&
                    action <= 2,
                "Invalid overworld operation");
        o.action = OverworldOperation::Action(action);
        next.apply(o);
        require(next.operations_.size() <= 4096, "Too many overworld operations");
    }
    s >> std::ws;
    require(end && s.eof(), "Incomplete overworld document");
    operations_ = std::move(next.operations_);
    history_ = {operations_};
    cursor_ = 0;
    mark_saved();
}
}
