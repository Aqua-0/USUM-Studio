#include "field/overworld_document.h"
#include "field/placement_document.h"
#include "field/npc_dialogue.h"
#include "field/map_catalog.h"
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
constexpr unsigned volume_values[] = {5, 10, 7, 9};
void validate_volume(const PlacementVolume &v) {
    require(v.type < 4, "Unsupported placement shape type");
    for (auto value : v.values)
        require(std::isfinite(value), "Shape values must be finite");
    if (v.type == 0)
        require(v.values[3] >= 0 && v.values[4] >= 0,
                "Cylinder radius and height cannot be negative");
    if (v.type == 1) {
        float length = 0;
        for (unsigned k = 3; k < 7; ++k)
            length += v.values[k] * v.values[k];
        require(std::isfinite(length) && length > 1e-8f, "Box rotation quaternion cannot be zero");
        require(v.values[7] >= 0 && v.values[8] >= 0 && v.values[9] >= 0,
                "Box dimensions cannot be negative");
    }
    if (v.type == 2)
        require(v.values[6] >= 0, "Wall height cannot be negative");
}
Bytes write_volumes(const std::vector<PlacementVolume> &volumes, SpatialPoint origin) {
    Bytes result;
    append32(result, narrow(volumes.size()));
    for (auto &v : volumes) {
        validate_volume(v);
        append32(result, v.type);
    }
    for (auto &v : volumes) {
        auto at = result.size();
        result.resize(at + volume_values[v.type] * 4);
        for (unsigned k = 0; k < volume_values[v.type]; ++k) {
            bool coordinate = k < (v.type == 3 ? 9u : v.type == 2 ? 6u : 3u);
            put_float(result, at + k * 4, v.values[k] + (coordinate ? origin[k % 3] : 0));
        }
    }
    return result;
}
unsigned model_id(View r, OverworldKind kind) {
    auto &l = layout(kind);
    return !l.model ? 0 : kind == OverworldKind::StaticObject ? u16(r, l.model) : u32(r, l.model);
}
bool same_position(View a, View b) {
    return position(a) == position(b);
}
}
SpatialScene preview_placement_volumes(const std::vector<PlacementVolume> &volumes,
                                       SpatialPoint origin) {
    SpatialScene scene;
    auto bytes = write_volumes(volumes, origin);
    decode_placement_collision(scene, bytes, "Draft placement shape", -1);
    return scene;
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
std::vector<PlacementVolumeGroup> OverworldDocument::shape_groups(unsigned entry) const {
    const auto &e = entries_.at(entry);
    const auto &l = layout(e.kind);
    auto ed = Container::parse(original_, "ED");
    auto zones = Container::parse(ed.files.at(l.category));
    const auto &zone = zones.files.at(e.zone);
    auto r = record(original_, e);
    std::vector<PlacementVolumeGroup> result;
    for (unsigned g = 0; g < l.shapes.size(); ++g) {
        bool character = e.kind == OverworldKind::Character || e.kind == OverworldKind::Trainer;
        PlacementVolumeGroup group{character ? (g == 0 ? "Talk range" : "Body collision")
                                   : e.kind == OverworldKind::StaticObject ? "Body collision"
                                                                           : "Activation region",
                                   {}};
        if (auto offset = u32(r, l.shapes[g])) {
            auto count = u32(zone, offset);
            slice(zone, offset + 4, std::size_t(count) * 4);
            auto at = offset + 4 + count * 4;
            for (unsigned i = 0; i < count; ++i) {
                PlacementVolume v;
                v.type = u32(zone, offset + 4 + i * 4);
                require(v.type < 4, "Unsupported placement shape type");
                for (unsigned k = 0; k < volume_values[v.type]; ++k) {
                    bool coordinate = k < (v.type == 3 ? 9u : v.type == 2 ? 6u : 3u);
                    v.values[k] = f32(zone, at + k * 4) - (coordinate ? e.position[k % 3] : 0);
                }
                validate_volume(v);
                group.shapes.push_back(v);
                at += volume_values[v.type] * 4;
            }
        }
        result.push_back(std::move(group));
    }
    return result;
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
TrainerPatrol OverworldDocument::trainer_patrol(unsigned entry) const {
    const auto &e = entries_.at(entry);
    require(e.kind == OverworldKind::Trainer, "Select a trainer placement");
    auto ed = Container::parse(original_, "ED");
    auto zones = Container::parse(ed.files.at(layout(e.kind).category));
    const auto &b = zones.files.at(e.zone);
    auto at = 4 + e.row * layout(e.kind).size;
    TrainerPatrol out;
    if (auto p = u32(b, at + 60)) {
        out.movement = u32(b, p);
        out.motion = u32(b, p + 4);
        out.frame = f32(b, p + 8);
    }
    if (auto p = u32(b, at + 76)) {
        out.path = read_pedestrian_path(b, p);
        for (auto &point : out.path.points)
            for (unsigned axis = 0; axis < 3; ++axis)
                point[axis] -= e.position[axis];
    }
    if (auto p = u32(b, at + 80))
        for (unsigned i = 0; i < u32(b, p); ++i) {
            auto q = p + 4 + i * 20;
            out.actions.push_back(
                {f32(b, q), f32(b, q + 8), f32(b, q + 16), u32(b, q + 4), u32(b, q + 12)});
        }
    if (auto p = u32(b, at + 72))
        for (unsigned i = 0; i < u32(b, p); ++i) {
            std::array<unsigned, 4> signal;
            for (unsigned n = 0; n < 4; ++n)
                signal[n] = u32(b, p + 4 + i * 16 + n * 4);
            out.signals.push_back(signal);
        }
    return out;
}
void OverworldDocument::prepare_dialogue(const std::filesystem::path &dump,
                                         OverworldOperation &o) const {
    auto &e = entries_.at(o.entry);
    require(e.kind == OverworldKind::Character, "Select an ordinary NPC for dialogue");
    Archive field(dump / GameProfile::field_archive(dump));
    auto scripts = Container::parse(
        field.decoded(area_ * TargetProfile::area_stride + TargetProfile::zone_script_slot), "ZS");
    auto used = zone_script_ids(scripts.files.at(e.zone));
    for (auto &entry : entries_)
        if (entry.zone == e.zone)
            used.insert(entry.script);
    for (auto &pending : operations_)
        if (entries_.at(pending.entry).zone == e.zone && pending.script >= 0)
            used.insert(unsigned(pending.script));
    unsigned id = 1;
    while (used.contains(id) && id < 256)
        ++id;
    require(id < 256, "No free local dialogue script ID");
    std::optional<NpcBattlePrompt> battle;
    if (o.battle_encounter >= 0) {
        auto table = Archive(dump / TargetProfile::script_events_archive)
                         .decoded(TargetProfile::static_encounters_member);
        require(table.size() % TargetProfile::static_encounter_record_size == 0 &&
                    unsigned(o.battle_encounter) <
                        table.size() / TargetProfile::static_encounter_record_size,
                "Static encounter is missing from this dump");
        battle = NpcBattlePrompt{unsigned(o.battle_encounter), 1, 2};
    }
    append_npc_dialogue_script(scripts.files.at(e.zone), id, o.event, {0}, battle);
    o.script = int(id);
}
OverworldDocument::DialogueChanges
OverworldDocument::compile_dialogues(const std::filesystem::path &dump) const {
    DialogueChanges result;
    if (std::none_of(operations_.begin(), operations_.end(), [](const auto &o) {
            return o.action != OverworldOperation::Action::Remove && o.script >= 0;
        }))
        return result;
    Archive field(dump / GameProfile::field_archive(dump));
    auto member = area_ * TargetProfile::area_stride + TargetProfile::zone_script_slot;
    auto scripts = Container::parse(field.decoded(member), "ZS");
    Archive zones(dump / TargetProfile::zone_archive);
    auto zone_table = zones.decoded(0);
    auto zone_ids = load_area_zone_ids(dump, area_);
    Archive messages(dump / TargetProfile::interaction_text_archive);
    bool changed = false;
    for (auto &o : operations_) {
        if (o.action == OverworldOperation::Action::Remove || o.script < 0)
            continue;
        auto &entry = entries_.at(o.entry);
        auto &program = scripts.files.at(entry.zone);
        if (o.dialogue.empty()) {
            require(zone_script_ids(program).contains(unsigned(o.script)),
                    "Assigned script has no handler in this zone");
            continue;
        }
        require(zone_ids.contains(entry.zone), "NPC zone has no global zone registration");
        auto message_member = u16(zone_table, std::size_t(zone_ids.at(entry.zone)) * 84 + 12);
        auto [it, added] = result.messages.try_emplace(message_member);
        if (added)
            it->second = messages.decoded(message_member);
        auto message = u16(it->second, 2);
        it->second = append_dialogue_message(it->second, o.dialogue);
        std::optional<NpcBattlePrompt> battle;
        if (o.battle_encounter >= 0) {
            auto table = Archive(dump / TargetProfile::script_events_archive)
                             .decoded(TargetProfile::static_encounters_member);
            require(table.size() % TargetProfile::static_encounter_record_size == 0 &&
                        unsigned(o.battle_encounter) <
                            table.size() / TargetProfile::static_encounter_record_size,
                    "Static encounter is missing from this dump");
            auto yes = u16(it->second, 2);
            it->second = append_dialogue_message(it->second, "Yes");
            auto no = u16(it->second, 2);
            it->second = append_dialogue_message(it->second, "No");
            battle = NpcBattlePrompt{unsigned(o.battle_encounter), yes, no};
        }
        program =
            append_npc_dialogue_script(program, unsigned(o.script), o.event, {message}, battle);
        changed = true;
    }
    if (changed)
        result.scripts[member] = scripts.write(4);
    return result;
}
void OverworldDocument::apply(const OverworldOperation &o) {
    auto &e = entries_.at(o.entry);
    require(e.restriction.empty(), e.restriction);
    for (auto &[group, volumes] : o.shapes) {
        require(group < layout(e.kind).shapes.size(),
                "Shape group does not belong to this placement");
        for (auto &v : volumes)
            validate_volume(v);
    }
    if (o.patrol) {
        require(e.kind == OverworldKind::Trainer, "Patrol routes belong to trainers");
        const auto &patrol = *o.patrol;
        require(patrol.path.points.size() <= 4096 && patrol.actions.size() <= 4096 &&
                    patrol.signals.size() <= 4096,
                "Too many trainer route records");
        require(patrol.path.points.empty() || patrol.path.points.size() >= 2,
                "A patrol needs at least two points");
        require(patrol.movement != 8 || patrol.path.points.size() >= 2,
                "Patrol movement needs a route");
        require(std::isfinite(patrol.frame), "Invalid trainer motion frame");
        for (const auto &point : patrol.path.points)
            for (auto value : point)
                require(std::isfinite(value) && std::abs(value) < 1e7f, "Invalid route point");
        for (const auto &action : patrol.actions)
            require(std::isfinite(action.progress) && std::isfinite(action.facing) &&
                        std::isfinite(action.frame),
                    "Invalid patrol action");
    }
    require(std::isfinite(o.turn) && std::abs(o.turn) <= 360000, "Invalid turn angle");
    require(o.turn == 0 || e.kind == OverworldKind::Character || e.kind == OverworldKind::Trainer ||
                e.kind == OverworldKind::StaticObject,
            "Rotation is supported for NPCs, trainers and static props");
    require(unsigned(o.action) <= 2, "Invalid overworld operation");
    require(o.script >= -1 && o.script < 256, "Choose a local script ID from 1 to 255");
    require((o.script == -1 && o.dialogue.empty()) || e.kind == OverworldKind::Character,
            "Dialogue assignment is supported for ordinary NPCs");
    require(o.dialogue.empty() || (o.script > 0 && o.dialogue.size() <= 2048),
            "New dialogue needs an allocated script ID and text up to 2048 bytes");
    require(o.battle_encounter >= -1 && o.battle_encounter <= 65535 &&
                (o.battle_encounter == -1 || !o.dialogue.empty()),
            "A static encounter needs a dialogue question and valid encounter ID");
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
    if (o.action == OverworldOperation::Action::Add && o.editor_id)
        require(std::none_of(operations_.begin(), operations_.end(),
                             [&](const auto &p) {
                                 return p.action == OverworldOperation::Action::Add &&
                                        p.editor_id == o.editor_id;
                             }),
                "Duplicate placement editor identity");
    auto previous = operations_;
    auto stored = o;
    if (stored.action == OverworldOperation::Action::Add) {
        if (!stored.editor_id)
            stored.editor_id = next_editor_id_++;
        next_editor_id_ = std::max(next_editor_id_, stored.editor_id + 1);
    }
    operations_.push_back(stored);
    try {
        placements();
    } catch (...) {
        operations_ = std::move(previous);
        throw;
    }
    history_.resize(++cursor_);
    history_.push_back(operations_);
}
namespace {
constexpr std::uint64_t added_placement = std::uint64_t(1) << 32;
}
const std::vector<WorkingPlacement> &OverworldDocument::working_entries() const {
    if (working_entries_valid_ && working_entries_operations_ == operations_)
        return working_entries_cache_;
    std::vector<WorkingPlacement> result;
    std::map<std::pair<OverworldKind, unsigned>, unsigned> rows;
    auto append_entry = [&](std::uint64_t id, unsigned source, const OverworldOperation *edit) {
        auto e = entries_.at(source);
        if (edit && edit->action == OverworldOperation::Action::Remove)
            return;
        e.row = rows[{e.kind, e.zone}]++;
        if (edit) {
            e.position = edit->position;
            e.event = edit->event;
            e.model = edit->model;
            e.condition = edit->flag;
            if (edit->script >= 0)
                e.script = unsigned(edit->script);
        }
        result.push_back({id, std::move(e)});
    };
    for (unsigned i = 0; i < entries_.size(); ++i) {
        auto op = std::find_if(operations_.begin(), operations_.end(), [&](const auto &o) {
            return o.entry == i && o.action != OverworldOperation::Action::Add;
        });
        append_entry(i + 1, i, op == operations_.end() ? nullptr : &*op);
    }
    for (const auto &o : operations_)
        if (o.action == OverworldOperation::Action::Add)
            append_entry(added_placement | o.editor_id, o.entry, &o);
    OverworldDocument projected(area_, placements(), characters_, objects_);
    std::erase_if(result, [&](auto &working) {
        const auto &e = working.entry;
        auto found =
            std::find_if(projected.entries_.begin(), projected.entries_.end(), [&](const auto &p) {
                return p.kind == e.kind && p.zone == e.zone &&
                       (e.kind == OverworldKind::StoryTrigger ? p.row == e.row
                                                              : p.event == e.event);
            });
        if (found == projected.entries_.end())
            return true;
        working.entry = *found;
        return false;
    });
    working_entries_operations_ = operations_;
    working_entries_cache_ = std::move(result);
    working_entries_valid_ = true;
    return working_entries_cache_;
}
bool OverworldDocument::interaction_target_available(std::uint64_t id) const {
    if (!id || id > entries_.size())
        return false;
    const auto &original = entries_[id - 1];
    const auto &working = working_entries();
    const auto found = std::find_if(working.begin(), working.end(), [&](const auto &entry) {
        return entry.id == id;
    });
    if (found == working.end())
        return false;
    const auto &current = found->entry;
    if (current.zone != original.zone || current.event != original.event ||
        current.script != original.script ||
        (current.kind == OverworldKind::StoryTrigger && current.row != original.row))
        return false;
    return std::none_of(operations_.begin(), operations_.end(), [&](const auto &edit) {
        return edit.action == OverworldOperation::Action::Update && edit.entry + 1 == id &&
               !edit.dialogue.empty();
    });
}
bool OverworldDocument::same_structure(const OverworldDocument &other) const {
    if (hash_ != other.hash_)
        return false;
    const auto a = working_entries(), b = other.working_entries();
    if (a.size() != b.size())
        return false;
    for (unsigned i = 0; i < a.size(); ++i) {
        if (a[i].id != b[i].id)
            return false;
        auto left = working_draft(a[i].id, OverworldOperation::Action::Update);
        auto right = other.working_draft(b[i].id, OverworldOperation::Action::Update);
        if (a[i].entry.kind == OverworldKind::Character ||
            a[i].entry.kind == OverworldKind::Trainer ||
            a[i].entry.kind == OverworldKind::StaticObject) {
            left.position = right.position = {};
            left.turn = right.turn = 0;
        }
        if (left != right)
            return false;
    }
    return true;
}
OverworldOperation OverworldDocument::working_draft(std::uint64_t id,
                                                    OverworldOperation::Action action) const {
    auto op = std::find_if(operations_.begin(), operations_.end(), [&](const auto &o) {
        return id & added_placement
                   ? o.action == OverworldOperation::Action::Add && o.editor_id == unsigned(id)
                   : o.action != OverworldOperation::Action::Add && o.entry + 1 == id;
    });
    require(!(id & added_placement) || op != operations_.end(), "Placement no longer exists");
    auto result = op == operations_.end() ? draft(unsigned(id - 1), action) : *op;
    if (action == OverworldOperation::Action::Add) {
        auto fresh = draft(result.entry, action);
        result.event = fresh.event;
        result.position[0] += op == operations_.end() ? 0 : 100;
        result.editor_id = 0;
        if (!result.dialogue.empty())
            result.script = -1;
        if (entries_[result.entry].kind == OverworldKind::Pickup)
            result.flag = 0;
    }
    result.action = action;
    return result;
}
void OverworldDocument::apply_working(std::uint64_t id, const OverworldOperation &operation) {
    if (operation.action == OverworldOperation::Action::Add) {
        apply(operation);
        return;
    }
    const auto previous = operations_;
    const auto history = history_;
    const auto cursor = cursor_;
    try {
        auto o = operation;
        if (id & added_placement) {
            auto found = std::find_if(operations_.begin(), operations_.end(), [&](const auto &p) {
                return p.action == OverworldOperation::Action::Add && p.editor_id == unsigned(id);
            });
            require(found != operations_.end(), "Placement no longer exists");
            require(found->entry == o.entry && found->event == o.event,
                    "Placement identity changed");
            const auto offset = found - operations_.begin();
            operations_.erase(found);
            if (o.action != OverworldOperation::Action::Remove) {
                o.action = OverworldOperation::Action::Add;
                o.editor_id = unsigned(id);
                apply(o);
                auto stored = operations_.back();
                operations_.pop_back();
                operations_.insert(operations_.begin() + offset, std::move(stored));
            }
        } else {
            require(o.entry + 1 == id, "Placement identity changed");
            std::erase_if(operations_, [&](const auto &p) {
                return p.entry == o.entry && p.action != OverworldOperation::Action::Add;
            });
            apply(o);
        }
        history_ = history;
        cursor_ = cursor;
        history_.resize(++cursor_);
        history_.push_back(operations_);
    } catch (...) {
        operations_ = previous;
        history_ = history;
        cursor_ = cursor;
        throw;
    }
}
std::map<std::size_t, Bytes>
OverworldDocument::preview_members(const std::filesystem::path &dump) const {
    auto base = area_ * TargetProfile::area_stride;
    std::map<std::size_t, Bytes> result{{base + TargetProfile::placement_slot, placements()}};
    auto ac = Container::parse(characters_, "AC");
    std::set<unsigned> models;
    for (const auto &bytes : ac.files)
        models.insert(unsigned(std::stoul(text(Container::parse(bytes, "CP").files.at(0)))));
    std::unique_ptr<Archive> shared;
    for (const auto &o : operations_) {
        auto kind = entries_[o.entry].kind;
        if (o.action == OverworldOperation::Action::Remove ||
            (kind != OverworldKind::Character && kind != OverworldKind::Trainer) ||
            !models.insert(o.model).second)
            continue;
        require(o.model > 1, "Choose an ordinary NPC model");
        if (!shared)
            shared = std::make_unique<Archive>(dump / TargetProfile::character_archive);
        auto bytes = shared->decoded(o.model);
        Container::parse(bytes, "CM");
        Container cp;
        cp.tag = {'C', 'P'};
        auto name = std::to_string(o.model);
        cp.files = {Bytes(name.begin(), name.end()), std::move(bytes)};
        ac.files.push_back(cp.write(128));
    }
    result[base + TargetProfile::character_resource_slot] = ac.write(128);
    return result;
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
                copy.shapes.clear();
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
                    if (l.script && o.script >= 0)
                        put32(r, l.script, unsigned(o.script));
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
                    for (unsigned g = 0; g < l.shapes.size(); ++g) {
                        auto at = l.shapes[g];
                        if (auto changed = o.shapes.find(g); changed != o.shapes.end()) {
                            if (changed->second.empty())
                                put32(r, at, 0);
                            else {
                                auto b = write_volumes(changed->second, o.position);
                                put32(r, at, 4 + count * l.size + narrow(extras.size()));
                                append(extras, b);
                            }
                        } else if (o.action == OverworldOperation::Action::Add ||
                                   delta != SpatialPoint{}) {
                            if (auto old = u32(rview, at)) {
                                auto b = shape(original, old, delta);
                                put32(r, at, 4 + count * l.size + narrow(extras.size()));
                                append(extras, b);
                            }
                        }
                    }
                    if (kind == OverworldKind::Trainer && o.patrol) {
                        const auto &patrol = *o.patrol;
                        auto payload = [&](unsigned offset, const Bytes &data) {
                            put32(r, offset,
                                  data.empty() ? 0 : 4 + count * l.size + narrow(extras.size()));
                            append(extras, data);
                        };
                        Bytes movement(12);
                        put32(movement, 0, patrol.movement);
                        put32(movement, 4, patrol.motion);
                        put_float(movement, 8, patrol.frame);
                        payload(60, movement);
                        Bytes path;
                        if (!patrol.path.points.empty()) {
                            path.resize(8 + patrol.path.points.size() * 12);
                            put16(path, 0, std::uint16_t(patrol.path.points.size()));
                            put16(path, 2, patrol.path.curved);
                            put16(path, 4, patrol.path.loop);
                            put16(path, 6, patrol.path.follow_ground);
                            for (unsigned i = 0; i < patrol.path.points.size(); ++i)
                                for (unsigned axis = 0; axis < 3; ++axis)
                                    put_float(path, 8 + i * 12 + axis * 4,
                                              patrol.path.points[i][axis] + o.position[axis]);
                        }
                        payload(76, path);
                        Bytes actions;
                        if (!patrol.actions.empty()) {
                            actions.resize(4 + patrol.actions.size() * 20);
                            put32(actions, 0, narrow(patrol.actions.size()));
                            for (unsigned i = 0; i < patrol.actions.size(); ++i) {
                                auto at = 4 + i * 20;
                                auto &action = patrol.actions[i];
                                put_float(actions, at, action.progress);
                                put32(actions, at + 4, action.motion);
                                put_float(actions, at + 8, action.facing);
                                put32(actions, at + 12, action.repeats);
                                put_float(actions, at + 16, action.frame);
                            }
                        }
                        payload(80, actions);
                        Bytes signals;
                        if (!patrol.signals.empty()) {
                            signals.resize(4 + patrol.signals.size() * 16);
                            put32(signals, 0, narrow(patrol.signals.size()));
                            for (unsigned i = 0; i < patrol.signals.size(); ++i)
                                for (unsigned n = 0; n < 4; ++n)
                                    put32(signals, 4 + i * 16 + n * 4, patrol.signals[i][n]);
                        }
                        payload(72, signals);
                    } else if (kind == OverworldKind::Trainer &&
                               (o.action == OverworldOperation::Action::Add ||
                                delta != SpatialPoint{}))
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
    auto result = ed.write(4);
    if (std::any_of(operations_.begin(), operations_.end(), [](const auto &o) {
            return o.action != OverworldOperation::Action::Remove && o.turn != 0;
        })) {
        PlacementDocument transforms(area_, result);
        for (unsigned i = 0; i < transforms.entries().size(); ++i) {
            const auto &entry = transforms.entries()[i];
            for (const auto &o : operations_) {
                const auto &source = entries_[o.entry];
                if (o.action == OverworldOperation::Action::Remove || !o.turn ||
                    (source.kind != OverworldKind::Character &&
                     source.kind != OverworldKind::Trainer &&
                     source.kind != OverworldKind::StaticObject) ||
                    entry.character != (source.kind == OverworldKind::Character ||
                                        source.kind == OverworldKind::Trainer) ||
                    entry.trainer != (source.kind == OverworldKind::Trainer) ||
                    entry.zone != source.zone || entry.source.event != o.event)
                    continue;
                auto state = transforms.state(i);
                state.turn = o.turn;
                transforms.preview(i, state);
            }
        }
        result = transforms.compile();
    }
    return result;
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
    for (auto &[member, bytes] : compile_dialogues(dump).scripts)
        result[member] = std::move(bytes);
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
    auto messages = compile_dialogues(dump).messages;
    if (!messages.empty()) {
        Archive text_archive(dump / TargetProfile::interaction_text_archive);
        for (auto &[member, data] : messages) {
            auto raw = text_archive.raw(member);
            if (!raw.empty() && raw.front() == 0x11)
                data = compress(data);
        }
        auto target = output / TargetProfile::interaction_text_archive;
        std::filesystem::create_directories(target.parent_path());
        text_archive.export_to(target, messages);
    }
}
std::string OverworldDocument::serialize() const {
    std::ostringstream s;
    s << "USUMSTUDIO_OVERWORLD 6\narea " << area_ << "\nsource " << hash_ << '\n'
      << std::setprecision(9);
    for (auto &o : operations_) {
        s << "operation " << unsigned(o.action) << ' ' << o.entry << ' ' << o.event << ' '
          << o.model << ' ' << o.flag << ' ' << o.item << ' ' << o.quantity << ' '
          << o.destination_zone << ' ' << o.destination_event << ' ' << o.position[0] << ' '
          << o.position[1] << ' ' << o.position[2] << ' ' << o.script << ' '
          << std::quoted(o.dialogue) << ' ' << o.battle_encounter << ' ' << o.shapes.size();
        for (auto &[group, volumes] : o.shapes) {
            s << ' ' << group << ' ' << volumes.size();
            for (auto &v : volumes) {
                s << ' ' << v.type;
                for (float value : v.values)
                    s << ' ' << value;
            }
        }
        s << ' ' << o.editor_id << ' ' << o.turn << ' ' << bool(o.patrol);
        if (o.patrol) {
            const auto &p = *o.patrol;
            s << ' ' << p.movement << ' ' << p.motion << ' ' << p.frame << ' ' << p.path.curved
              << ' ' << p.path.loop << ' ' << p.path.follow_ground << ' ' << p.path.points.size();
            for (const auto &point : p.path.points)
                for (auto value : point)
                    s << ' ' << value;
            s << ' ' << p.actions.size();
            for (const auto &a : p.actions)
                s << ' ' << a.progress << ' ' << a.motion << ' ' << a.facing << ' ' << a.repeats
                  << ' ' << a.frame;
            s << ' ' << p.signals.size();
            for (const auto &signal : p.signals)
                for (auto value : signal)
                    s << ' ' << value;
        }
        s << '\n';
    }
    s << "end\n";
    return s.str();
}
void OverworldDocument::restore(const std::string &text) {
    std::istringstream s(text);
    std::string tag, hash;
    unsigned version, area;
    require(bool(s >> tag >> version) && tag == "USUMSTUDIO_OVERWORLD" &&
                (version >= 1 && version <= 6),
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
        if (version >= 2)
            require(bool(s >> o.script >> std::quoted(o.dialogue)),
                    "Invalid NPC dialogue operation");
        if (version >= 3)
            require(bool(s >> o.battle_encounter), "Invalid NPC battle prompt");
        if (version >= 4) {
            unsigned groups;
            require(bool(s >> groups) && groups <= 2, "Invalid placement shape groups");
            for (unsigned i = 0; i < groups; ++i) {
                unsigned group, count;
                require(bool(s >> group >> count) && count <= text.size() / 20 &&
                            !o.shapes.contains(group),
                        "Invalid shape group");
                auto &volumes = o.shapes[group];
                volumes.resize(count);
                for (auto &v : volumes) {
                    require(bool(s >> v.type), "Missing shape type");
                    for (auto &value : v.values)
                        require(bool(s >> value), "Missing shape value");
                }
            }
        }
        if (version >= 5)
            require(bool(s >> o.editor_id >> o.turn), "Missing placement editor identity");
        if (version >= 6) {
            bool edited;
            require(bool(s >> edited), "Missing trainer patrol state");
            if (edited) {
                auto &p = o.patrol.emplace();
                unsigned count;
                require(bool(s >> p.movement >> p.motion >> p.frame >> p.path.curved >>
                             p.path.loop >> p.path.follow_ground >> count) &&
                            count <= 4096,
                        "Invalid patrol route");
                p.path.points.resize(count);
                for (auto &point : p.path.points)
                    for (auto &value : point)
                        require(bool(s >> value), "Missing route point");
                require(bool(s >> count) && count <= 4096, "Invalid patrol actions");
                p.actions.resize(count);
                for (auto &a : p.actions)
                    require(bool(s >> a.progress >> a.motion >> a.facing >> a.repeats >> a.frame),
                            "Missing patrol action");
                require(bool(s >> count) && count <= 4096, "Invalid motion signals");
                p.signals.resize(count);
                for (auto &signal : p.signals)
                    for (auto &value : signal)
                        require(bool(s >> value), "Missing motion signal");
            }
        }
        o.action = OverworldOperation::Action(action);
        next.apply(o);
        require(next.operations_.size() <= 4096, "Too many overworld operations");
    }
    s >> std::ws;
    require(end && s.eof(), "Incomplete overworld document");
    operations_ = std::move(next.operations_);
    next_editor_id_ = next.next_editor_id_;
    history_ = {operations_};
    cursor_ = 0;
    mark_saved();
}
}
