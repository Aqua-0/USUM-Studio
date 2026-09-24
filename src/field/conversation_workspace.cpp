#include "field/conversation_workspace.h"
#include "field/interaction_linker.h"
#include "field/npc_dialogue.h"
#include "field/dialogue_text.h"
#include "field/pickup_document.h"
#include "field/trainer_catalog.h"
#include <functional>
#include <algorithm>
#include "field/map_catalog.h"
#include "formats/archive.h"
#include "formats/container.h"
#include "formats/compression.h"
#include "core/digest.h"
#include <iomanip>
#include <set>
#include <sstream>
namespace studio {
namespace {
Bytes bytes(const std::string &s) {
    return Bytes(s.begin(), s.end());
}
std::string hex(View b) {
    constexpr char alphabet[] = "0123456789abcdef";
    std::string out;
    out.reserve(b.size() * 2);
    for (auto c : b) {
        out += alphabet[c >> 4];
        out += alphabet[c & 15];
    }
    return out;
}
Bytes unhex(const std::string &s) {
    require(s.size() % 2 == 0 && s.size() <= 32 * 1024 * 1024,
            "Invalid compiled conversation size");
    auto digit = [](char c) {
        require((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'),
                "Invalid compiled conversation encoding");
        return c <= '9' ? c - '0' : c - 'a' + 10;
    };
    Bytes out;
    out.reserve(s.size() / 2);
    for (std::size_t i = 0; i < s.size(); i += 2)
        out.push_back(std::uint8_t(digit(s[i]) * 16 + digit(s[i + 1])));
    return out;
}
unsigned placement_category(InteractionTargetKind kind) {
    if (kind == InteractionTargetKind::Trainer)
        return TargetProfile::trainer_placement_pack;
    if (kind == InteractionTargetKind::PositionTrigger)
        return TargetProfile::position_event_pack;
    return kind == InteractionTargetKind::Scenery ? TargetProfile::interaction_placement_pack
                                                  : TargetProfile::character_placement_pack;
}
unsigned placement_size(InteractionTargetKind kind) {
    return kind == InteractionTargetKind::Trainer ? 84
           : kind == InteractionTargetKind::Npc   ? 120
                                                  : 60;
}
unsigned placement_script_offset(InteractionTargetKind kind) {
    if (kind == InteractionTargetKind::Trainer)
        return 52;
    if (kind == InteractionTargetKind::PositionTrigger)
        return 44;
    return kind == InteractionTargetKind::Scenery ? 48 : 56;
}
std::map<unsigned, std::pair<unsigned, unsigned>>
actors(View placement, unsigned zone, InteractionTargetKind kind = InteractionTargetKind::Npc) {
    auto ed = Container::parse(placement, "ED");
    auto groups = Container::parse(ed.files.at(placement_category(kind)));
    require(zone < groups.files.size(), "Interaction zone no longer exists");
    const auto &data = groups.files[zone];
    std::map<unsigned, std::pair<unsigned, unsigned>> result;
    if (data.empty())
        return result;
    auto count = u32(data, 0);
    require(count <= 4096, "Invalid interaction placement count");
    auto size = placement_size(kind);
    slice(data, 4, std::size_t(count) * size);
    for (unsigned row = 0; row < count; ++row) {
        unsigned at = 4 + row * size;
        require(u32(data, at) == (kind == InteractionTargetKind::Trainer           ? 7u
                                  : kind == InteractionTargetKind::PositionTrigger ? 0u
                                  : kind == InteractionTargetKind::Scenery         ? 2u
                                                                                   : 1u),
                "Unexpected interaction record type");
        require(
            result
                .emplace(kind == InteractionTargetKind::PositionTrigger ? row : u32(data, at + 44),
                         std::pair{row, u32(data, at + placement_script_offset(kind))})
                .second,
            "Duplicate interaction event ID");
    }
    return result;
}
}
ConversationWorkspace::ConversationWorkspace(std::filesystem::path source, unsigned area)
    : source_(std::move(source)), area_(area) {
    if (area_ == trainer_workspace) {
        trainer_source_ = resolve_interaction_source(source_, {}, 0, 0, 0, 1001);
        require(trainer_source_.shared &&
                    trainer_source_.messages == TargetProfile::interaction_text_archive,
                "Unsupported trainer script message source");
        scripts_ = trainer_source_.program;
        message_sources_[trainer_source_.message_member] =
            Archive(source_ / trainer_source_.messages).decoded(trainer_source_.message_member);
        fingerprint_ =
            sha256(scripts_) + sha256(message_sources_.at(trainer_source_.message_member));
        saved_ = serialize();
        return;
    }
    Archive field(source_ / GameProfile::field_archive(source_));
    placements_ = field.decoded(area_ * TargetProfile::area_stride + TargetProfile::placement_slot);
    scripts_ = field.decoded(area_ * TargetProfile::area_stride + TargetProfile::zone_script_slot);
    auto zones = Archive(source_ / TargetProfile::zone_archive).decoded(0);
    auto ids = load_area_zone_ids(source_, area_);
    for (auto [local, global] : ids)
        message_members_[local] = u16(zones, std::size_t(global) * 84 + 12);
    Archive messages(source_ / TargetProfile::interaction_text_archive);
    for (auto [zone, member] : message_members_)
        if (!message_sources_.contains(member))
            message_sources_[member] = messages.decoded(member);
    fingerprint_ = sha256(placements_) + sha256(scripts_) + sha256(zones);
    saved_ = serialize();
}
AuthoredInteraction ConversationWorkspace::preview(ConversationActor actor) const {
    auto copy = *this;
    return copy.open(actor);
}
void ConversationWorkspace::remove(ConversationActor actor) {
    interactions_.erase(actor);
    binaries_.erase(actor);
}
void ConversationWorkspace::restore_actor(ConversationActor actor,
                                          const ConversationWorkspace &snapshot) {
    require(area_ == snapshot.area_ && fingerprint_ == snapshot.fingerprint_,
            "Conversation baseline changed");
    remove(actor);
    if (auto found = snapshot.interactions_.find(actor); found != snapshot.interactions_.end())
        interactions_.emplace(actor, found->second);
    if (auto found = snapshot.binaries_.find(actor); found != snapshot.binaries_.end())
        binaries_.emplace(actor, found->second);
}
AuthoredInteraction &ConversationWorkspace::open(ConversationActor actor) {
    if (auto found = interactions_.find(actor); found != interactions_.end())
        return found->second;
    if (area_ == trainer_workspace) {
        require(actor.kind == InteractionTargetKind::Trainer, "Select a trainer interaction");
        auto catalog = load_map_catalog(source_);
        auto location = std::find_if(catalog.locations.begin(), catalog.locations.end(),
                                     [&](const auto &entry) {
                                         return entry.zone == int(actor.zone);
                                     });
        require(location != catalog.locations.end(), "Trainer zone is missing");
        unsigned area = unsigned(location->area);
        auto ids = load_area_zone_ids(source_, area);
        auto local = std::find_if(ids.begin(), ids.end(), [&](const auto &entry) {
            return entry.second == int(actor.zone);
        });
        require(local != ids.end(), "Trainer local zone is missing");
        auto placements =
            Archive(source_ / GameProfile::field_archive(source_))
                .decoded(area * TargetProfile::area_stride + TargetProfile::placement_slot);
        auto rows = actors(placements, local->first, actor.kind);
        require(rows.contains(actor.event), "Trainer placement is missing");
        auto ed = Container::parse(placements, "ED");
        auto group = Container::parse(ed.files.at(TargetProfile::trainer_placement_pack));
        require(u32(group.files.at(local->first), 4 + rows.at(actor.event).first * 84 + 56) == 0,
                "Author shared alias trainers in their owning zone");
        auto selector = rows.at(actor.event).second;
        require(selector > 1000 && selector < 3000,
                "This trainer does not use the ordinary trainer dispatcher");
        auto source =
            resolve_interaction_source(source_, {}, area, local->first, int(actor.zone), selector);
        require(source.program == scripts_ &&
                    source.message_member == trainer_source_.message_member,
                "Trainer uses a different shared program");
        return interactions_
            .emplace(actor, AuthoredInteraction(
                                scripts_, {area, local->first, actor.event, selector, actor.kind}))
            .first->second;
    }
    require(actor.kind != InteractionTargetKind::Trainer,
            "Trainer interactions use the project trainer workspace");
    auto rows = actors(placements_, actor.zone, actor.kind);
    require(rows.contains(actor.event),
            "Select an NPC, scenery interaction or position trigger in this area");
    if (actor.kind == InteractionTargetKind::Npc) {
        auto ed = Container::parse(placements_, "ED");
        auto group = Container::parse(ed.files.at(TargetProfile::character_placement_pack));
        require(u32(group.files.at(actor.zone), 4 + rows.at(actor.event).first * 120 + 104) == 0,
                "Shared alias placements must be authored in their owning zone");
    }
    unsigned selector = rows.at(actor.event).second;
    require((selector > 0 || actor.kind == InteractionTargetKind::PositionTrigger) &&
                selector <= 65535,
            "This placement has no valid interaction script");
    require(message_members_.contains(actor.zone), "This zone has no message table mapping");
    auto scripts = Container::parse(scripts_, "ZS");
    return interactions_
        .emplace(actor, AuthoredInteraction(scripts.files.at(actor.zone),
                                            {area_, actor.zone, actor.event, selector, actor.kind}))
        .first->second;
}
bool ConversationWorkspace::language_available(unsigned language) const {
    return language < GameProfile::dialogue_languages.size() &&
           resource_exists(source_ / GameProfile::dialogue_languages[language].archive);
}
const Bytes &ConversationWorkspace::message_source(unsigned member, unsigned language) const {
    if (language == GameProfile::dialogue_fallback_language)
        return message_sources_.at(member);
    require(language_available(language), "Dialogue language archive is unavailable");
    auto key = std::pair{member, language};
    if (!translated_sources_.contains(key))
        translated_sources_[key] =
            Archive(source_ / GameProfile::dialogue_languages[language].archive).decoded(member);
    return translated_sources_.at(key);
}
std::map<unsigned, std::set<unsigned>> ConversationWorkspace::message_languages() const {
    std::map<unsigned, std::set<unsigned>> result;
    for (const auto &[actor, document] : interactions_) {
        auto member = area_ == trainer_workspace ? trainer_source_.message_member
                                                : message_members_.at(actor.zone);
        const auto &languages = document.draft().languages;
        result[member].insert(languages.begin(), languages.end());
    }
    return result;
}
std::map<ConversationActor, ConversationWorkspace::Allocation>
ConversationWorkspace::allocate() const {
    auto scripts = area_ == trainer_workspace ? Container{} : Container::parse(scripts_, "ZS");
    std::map<unsigned, unsigned> next_message;
    for (const auto &[member, languages] : message_languages())
        for (auto language : languages)
            next_message[member] = std::max(next_message[member], unsigned(u16(message_source(member, language), 2)));
    std::map<unsigned, std::set<unsigned>> used;
    std::map<ConversationActor, Allocation> result;
    for (const auto &[actor, document] : interactions_) {
        if (area_ != trainer_workspace && !used.contains(actor.zone)) {
            used[actor.zone] = zone_script_ids(scripts.files.at(actor.zone));
            for (auto kind : {InteractionTargetKind::Npc, InteractionTargetKind::Scenery,
                              InteractionTargetKind::PositionTrigger})
                for (auto [event, row] : actors(placements_, actor.zone, kind))
                    used[actor.zone].insert(row.second);
        }
        Allocation a;
        if (area_ == trainer_workspace) {
            a.script = document.target().script;
            a.member = trainer_source_.message_member;
        } else {
            a.script = 1;
            while (a.script < 256 && used[actor.zone].contains(a.script))
                ++a.script;
            require(a.script < 256, "No free local interaction selector remains in this zone");
            used[actor.zone].insert(a.script);
            a.member = message_members_.at(actor.zone);
        }
        for (const auto &m : document.draft().messages) {
            require(next_message[a.member] < 65535, "The zone message table is full");
            a.messages[m.symbol] = next_message[a.member]++;
        }
        result.emplace(actor, std::move(a));
    }
    return result;
}
ConversationPawn ConversationWorkspace::generate(ConversationActor actor) const {
    return interactions_.at(actor).generate(allocate().at(actor).messages);
}
std::string ConversationWorkspace::stamp(ConversationActor actor) const {
    auto code = generate(actor);
    return sha256(bytes(code.source)) + sha256(bytes(code.definitions));
}
void ConversationWorkspace::validate_references(const ConversationDraft &draft) const {
    std::optional<std::size_t> item_count, encounter_count, species_count;
    for (const auto &message : draft.messages) {
        auto text = encode_dialogue_text(message.text, message.formatted);
        for (auto item : text.items) {
            if (!item_count)
                item_count = load_pickup_item_names(source_).size();
            require(item < *item_count,
                    message.symbol + ": item name is absent from the project text table");
        }
        for (auto species : text.species) {
            if (!species_count)
                species_count =
                    decode_location_text(Archive(source_ / TargetProfile::location_text_archive)
                                             .decoded(TargetProfile::pokemon_names_member))
                        .size();
            require(species < *species_count,
                    message.symbol + ": species name is absent from the project text table");
        }
    }
    if (draft.custom)
        return;
    std::function<void(const std::vector<ConversationStep> &)> visit = [&](const auto &steps) {
        for (const auto &step : steps) {
            if (step.action == ConversationAction::TrainerBattle)
                load_trainer_battle(source_, step.trainer);
            if (step.action == ConversationAction::GiveItem) {
                if (!item_count)
                    item_count = load_pickup_item_names(source_).size();
                require(step.item < *item_count && step.item < 1024,
                        "Step " + std::to_string(step.id) +
                            ": item ID is not supported by this project's item table or inventory");
            }
            if (step.action == ConversationAction::Encounter) {
                if (!encounter_count) {
                    auto table = Archive(source_ / TargetProfile::script_events_archive)
                                     .decoded(TargetProfile::static_encounters_member);
                    require(table.size() % TargetProfile::static_encounter_record_size == 0,
                            "Invalid static encounter table");
                    encounter_count = table.size() / TargetProfile::static_encounter_record_size;
                }
                require(
                    step.encounter < *encounter_count,
                    "Step " + std::to_string(step.id) +
                        ": encounter row is missing from this project's static encounter table");
            }
            visit(step.children);
            visit(step.otherwise);
        }
    };
    visit(draft.steps);
}
PawnCompileResult ConversationWorkspace::compile(ConversationActor actor,
                                                 const PawnCompilerSettings &settings) {
    validate_references(interactions_.at(actor).draft());
    auto result = compile_conversation(generate(actor), settings);
    if (result.succeeded()) {
        if (area_ == trainer_workspace)
            link_authored_interaction(scripts_, result.program,
                                      interactions_.at(actor).target().script,
                                      TrainerInteractionTarget{actor.zone, actor.event});
        else {
            auto script = Container::parse(scripts_, "ZS");
            link_authored_interaction(script.files.at(actor.zone), result.program,
                                      allocate().at(actor).script);
        }
        binaries_[actor] = {stamp(actor), result.program};
    }
    return result;
}
void ConversationWorkspace::compile_all(const PawnCompilerSettings &settings) {
    auto pending = binaries_;
    try {
        for (const auto &[actor, doc] : interactions_) {
            auto result = compile(actor, settings);
            require(result.succeeded(), "Pawn compilation failed for event " +
                                            std::to_string(actor.event) + ":\n" + result.output);
        }
    } catch (...) {
        binaries_ = std::move(pending);
        throw;
    }
}
bool ConversationWorkspace::compiled() const {
    try {
        for (const auto &[actor, doc] : interactions_) {
            auto found = binaries_.find(actor);
            if (found == binaries_.end() || found->second.stamp != stamp(actor))
                return false;
        }
        return true;
    } catch (const std::exception &) {
        return false;
    }
}
bool ConversationWorkspace::dirty() const {
    return serialize() != saved_;
}
void ConversationWorkspace::mark_saved() {
    saved_ = serialize();
}
std::string ConversationWorkspace::serialize() const {
    std::ostringstream out;
    out << "conversation_workspace 2 " << area_ << ' ' << fingerprint_ << ' '
        << interactions_.size() << '\n';
    for (const auto &[actor, doc] : interactions_) {
        auto found = binaries_.find(actor);
        out << actor.zone << ' ' << actor.event << ' ' << int(actor.kind) << ' '
            << std::quoted(doc.serialize()) << ' '
            << std::quoted(found == binaries_.end() ? std::string{} : found->second.stamp) << ' '
            << std::quoted(found == binaries_.end() ? std::string{} : hex(found->second.program))
            << '\n';
    }
    return out.str();
}
void ConversationWorkspace::restore(const std::string &record) {
    require(record.size() <= 64 * 1024 * 1024, "Conversation workspace is too large");
    std::istringstream in(record);
    std::string magic, hash;
    unsigned version, area, count;
    require(bool(in >> magic >> version >> area >> hash >> count) &&
                magic == "conversation_workspace" && (version == 1 || version == 2) &&
                area == area_ && hash == fingerprint_ && count <= 4096,
            "Conversation baseline changed");
    ConversationWorkspace next(source_, area_);
    for (unsigned i = 0; i < count; ++i) {
        ConversationActor actor;
        std::string doc, stamp, program;
        require(bool(in >> actor.zone >> actor.event), "Missing interaction target");
        if (version >= 2) {
            int kind;
            require(bool(in >> kind) && kind >= 0 && kind <= int(InteractionTargetKind::Trainer),
                    "Invalid interaction placement kind");
            actor.kind = InteractionTargetKind(kind);
        }
        require(bool(in >> std::quoted(doc) >> std::quoted(stamp) >> std::quoted(program)),
                "Malformed authored conversation");
        require(!next.interactions_.contains(actor), "Duplicate authored actor");
        next.open(actor).restore(doc);
        if (!program.empty()) {
            auto binary = unhex(program);
            decode_field_amx(binary);
            next.binaries_[actor] = {stamp, std::move(binary)};
        }
    }
    in >> std::ws;
    require(in.eof(), "Trailing conversation data");
    interactions_ = std::move(next.interactions_);
    binaries_ = std::move(next.binaries_);
    mark_saved();
}
ConversationBuild ConversationWorkspace::build(ConversationBuildMode mode) const {
    if (mode == ConversationBuildMode::Current)
        require(compiled(), (area_ == trainer_workspace ? std::string("Trainer interactions")
                                                        : "Area " + std::to_string(area_)) +
                                ": compile all conversations before staging; saved source is newer "
                                "than its compiled output");
    ConversationBuild out;
    if (interactions_.empty())
        return out;
    auto allocations = allocate();
    auto languages = message_languages();
    auto append_messages = [&](const AuthoredInteraction &doc, const Allocation &allocation) {
        for (auto language : languages.at(allocation.member)) {
            auto &tables = language == GameProfile::dialogue_fallback_language
                               ? out.messages : out.translations[language];
            auto [it, added] = tables.try_emplace(allocation.member);
            if (added)
                it->second = message_source(allocation.member, language);
            for (const auto &message : doc.draft().messages) {
                auto id = allocation.messages.at(message.symbol);
                auto localized = encode_dialogue_translation(
                    message.text, doc.draft().languages.contains(language)
                                      ? message.text_for(language) : message.text,
                    message.formatted);
                while (u16(it->second, 2) < id)
                    it->second = append_dialogue_message(it->second, "[Unused]");
                require(u16(it->second, 2) == id, "Localized message allocation changed");
                it->second = append_dialogue_message(it->second, localized);
            }
        }
    };
    if (area_ == trainer_workspace) {
        auto program = scripts_;
        for (const auto &[actor, doc] : interactions_) {
            const auto &allocation = allocations.at(actor);
            append_messages(doc, allocation);
            if (mode == ConversationBuildMode::Current)
                validate_references(doc.draft());
            program =
                link_authored_interaction(program, binaries_.at(actor).program, allocation.script,
                                          TrainerInteractionTarget{actor.zone, actor.event})
                    .program;
        }
        out.shared[trainer_source_.member] = std::move(program);
        return out;
    }
    auto scripts = Container::parse(scripts_, "ZS");
    auto ed = Container::parse(placements_, "ED");
    std::map<unsigned, Container> groups;
    for (const auto &[actor, doc] : interactions_) {
        auto binary = binaries_.find(actor);
        require(binary != binaries_.end() && !binary->second.program.empty() &&
                    !binary->second.stamp.empty(),
                "Retained conversation is missing its compiled output; restore a complete staged "
                "revision");
        if (mode == ConversationBuildMode::Current)
            validate_references(doc.draft());
        const auto &a = allocations.at(actor);
        append_messages(doc, a);
        scripts.files.at(actor.zone) =
            link_authored_interaction(scripts.files.at(actor.zone), binaries_.at(actor).program,
                                      a.script)
                .program;
        auto category = placement_category(actor.kind);
        if (!groups.contains(category))
            groups.emplace(category, Container::parse(ed.files.at(category)));
        auto row = actors(placements_, actor.zone, actor.kind).at(actor.event).first;
        put32(groups.at(category).files.at(actor.zone),
              4 + row * placement_size(actor.kind) + placement_script_offset(actor.kind), a.script);
    }
    for (auto &[category, group] : groups)
        ed.files[category] = group.write(4);
    out.field[area_ * TargetProfile::area_stride + TargetProfile::placement_slot] = ed.write(4);
    out.field[area_ * TargetProfile::area_stride + TargetProfile::zone_script_slot] =
        scripts.write(4);
    return out;
}
void ConversationWorkspace::export_to(const std::filesystem::path &output,
                                      ConversationBuildMode mode) const {
    auto changes = build(mode);
    auto emit = [&](const std::filesystem::path &path, std::map<std::size_t, Bytes> edits) {
        if (edits.empty())
            return;
        Archive original(source_ / path);
        for (auto &[member, data] : edits) {
            auto raw = original.raw(member);
            if (!raw.empty() && raw[0] == 0x11 && !(raw.size() >= 8 && u16(raw, 4) == 0xf1e0))
                data = compress(data);
        }
        std::filesystem::create_directories((output / path).parent_path());
        original.export_to(output / path, edits);
    };
    emit(GameProfile::field_archive(source_), std::move(changes.field));
    emit(TargetProfile::interaction_text_archive, std::move(changes.messages));
    for (auto &[language, messages] : changes.translations)
        emit(GameProfile::dialogue_languages.at(language).archive, std::move(messages));
    emit(TargetProfile::shared_script_archive, std::move(changes.shared));
}
}
