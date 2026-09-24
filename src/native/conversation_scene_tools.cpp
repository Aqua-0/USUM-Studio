#include "native/conversation_scene_tools.h"
#include "native/scene_browser.h"
#include "assets/model_library.h"
#include "field/pickup_document.h"
#include "field/map_catalog.h"
#include "formats/archive.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
namespace studio {
namespace {
bool matches(std::string label, std::string filter) {
    auto lower = [](unsigned char c) {
        return char(std::tolower(c));
    };
    std::transform(label.begin(), label.end(), label.begin(), lower);
    std::transform(filter.begin(), filter.end(), filter.begin(), lower);
    return label.find(filter) != std::string::npos;
}
bool choice(const char *label, unsigned &value, const std::vector<std::string> &names, char *filter,
            bool allow_zero) {
    bool changed = false;
    auto current = value < names.size() ? names[value] : "Unknown ID " + std::to_string(value);
    if (ImGui::BeginCombo(label, current.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##filter", "Search name or ID", filter, 100);
        ImGui::BeginChild("choices", {0, 220});
        for (unsigned i = allow_zero ? 0 : 1; i < names.size(); ++i) {
            if (!matches(names[i], filter))
                continue;
            ImGui::PushID(int(i));
            if (ImGui::Selectable(names[i].c_str(), value == i)) {
                value = i;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndCombo();
    }
    return changed;
}
}
void ConversationSceneTools::stop_motion() {
    if (preview_scene_)
        for (auto &[index, original] : originals_)
            if (index < preview_scene_->skeletons.size()) {
                auto &rig = preview_scene_->skeletons[index];
                rig.motion = std::move(original.motion);
                rig.tracks = std::move(original.tracks);
                rig.overlays = std::move(original.overlays);
                rig.daily = original.daily;
                rig.seconds_offset = original.seconds_offset;
            }
    originals_.clear();
    preview_scene_.reset();
}
void ConversationSceneTools::reset() {
    stop_motion();
    discard_ = loading_.valid();
    scene_.reset();
    loaded_actor_ = -3;
    motions_.clear();
    items_.clear();
    encounters_.clear();
    trainers_.clear();
    species_.clear();
    trainers_loaded_ = false;
    trainer_error_.clear();
    items_loaded_ = encounters_loaded_ = false;
    motion_error_.clear();
    item_error_.clear();
    encounter_error_.clear();
}
void ConversationSceneTools::update(bool active) {
    if (!active || !drawn_)
        stop_motion();
    drawn_ = false;
    if (loading_.valid() &&
        loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto result = loading_.get();
            if (!discard_)
                motions_ = std::move(result);
        } catch (const std::exception &e) {
            if (!discard_)
                motion_error_ = e.what();
        }
        discard_ = false;
    }
}
void ConversationSceneTools::context(std::shared_ptr<Environment> scene,
                                     EnvironmentRenderer &renderer, MapCursor &cursor,
                                     std::filesystem::path source, unsigned area, unsigned zone,
                                     unsigned event, bool scenery) {
    if (scene != scene_ || source != source_ || area != area_ || zone != zone_ || event != event_ ||
        scenery != scenery_)
        reset();
    scene_ = std::move(scene);
    renderer_ = &renderer;
    cursor_ = scene_ ? &cursor : nullptr;
    source_ = std::move(source);
    area_ = area;
    zone_ = zone;
    event_ = event;
    scenery_ = scenery;
}
int ConversationSceneTools::region(int actor) const {
    if (!scene_ || actor == -1)
        return -1;
    unsigned event = actor == -2 ? event_ : unsigned(actor);
    for (unsigned i = 0; i < scene_->spatial.regions.size(); ++i) {
        auto &ref = scene_->spatial.regions[i].overworld;
        if (ref && (ref->category == 1 || ref->category == 7 || ref->category == 14) &&
            ref->local_zone == zone_ && ref->event == event)
            return int(i);
    }
    return -1;
}
std::string ConversationSceneTools::actor_name(int actor) const {
    if (actor == -2)
        return "This NPC";
    if (actor == -1)
        return "Player";
    int index = region(actor);
    return index < 0 ? "Event " + std::to_string(actor) : scene_->spatial.regions[index].name;
}
bool ConversationSceneTools::actor(ConversationStep &step) {
    bool changed = false;
    if (!scene_)
        ImGui::TextWrapped(
            "Load this conversation's map to pick scene characters and preview motions.");
    if (ImGui::BeginCombo("Character", actor_name(step.actor).c_str())) {
        ImGui::InputTextWithHint("##actors", "Search characters", actor_filter_,
                                 sizeof(actor_filter_));
        for (int actor : {-2, -1}) {
            if (scenery_ && actor == -2)
                continue;
            if (ImGui::Selectable(actor_name(actor).c_str(), step.actor == actor)) {
                step.actor = actor;
                changed = true;
            }
        }
        if (scene_)
            for (const auto &entry : scene_->spatial.regions) {
                auto &ref = entry.overworld;
                if (!ref || (ref->category != 1 && ref->category != 7 && ref->category != 14) ||
                    ref->local_zone != zone_)
                    continue;
                auto label = entry.name;
                if (!matches(label, actor_filter_))
                    continue;
                ImGui::PushID(int(ref->event));
                if (ImGui::Selectable(label.c_str(), step.actor == int(ref->event))) {
                    step.actor = int(ref->event);
                    changed = true;
                }
                ImGui::PopID();
            }
        ImGui::EndCombo();
    }
    if (ImGui::TreeNode("Advanced character reference")) {
        changed |= ImGui::InputInt("Event ID (-2 self, -1 player)", &step.actor);
        ImGui::TreePop();
    }
    return changed;
}
void ConversationSceneTools::load_motion(int actor) {
    if (loading_.valid() || actor == loaded_actor_)
        return;
    stop_motion();
    motions_.clear();
    motion_error_.clear();
    loaded_actor_ = actor;
    int selected = region(actor), draw = -1;
    if (scene_ && selected >= 0)
        for (unsigned i = 0; i < scene_->draws.size(); ++i)
            if (SceneBrowser::character_region(*scene_, int(i)) == selected) {
                draw = int(i);
                break;
            }
    if (draw < 0 || !scene_->draws[draw].source) {
        motion_error_ =
            "No model motion library is loaded for this character. You can still enter a slot.";
        return;
    }
    auto link = *scene_->draws[draw].source;
    auto archives = scene_->archive_sources;
    auto source = source_;
    discard_ = false;
    loading_ = std::async(std::launch::async, [link, archives, source] {
        require(link.path.size() >= 2 && link.path[link.path.size() - 2] == 0,
                "Unsupported character model source");
        std::vector<std::size_t> path(link.path.begin(), link.path.end() - 2);
        return load_library_model(source, archives.resolve(source, link.archive),
                                  ModelCategory::FieldCharacters,
                                  {link.member, "Character motions"}, nullptr, "model/", nullptr,
                                  link.path.back(), path)
            .motions;
    });
}
bool ConversationSceneTools::motion(ConversationStep &step) {
    drawn_ = true;
    if (loaded_actor_ != step.actor || step_ != step.id ||
        (!originals_.empty() && preview_slot_ != step.arguments[0]))
        stop_motion();
    step_ = step.id;
    load_motion(step.actor);
    bool changed = false;
    if (loading_.valid())
        ImGui::TextDisabled("Loading character motions...");
    const AssetMotion *selected = nullptr;
    if (loaded_actor_ == step.actor)
        for (const auto &motion : motions_)
            if (motion.group == 0 && int(motion.slot) == step.arguments[0])
                selected = &motion;
    auto label = selected ? selected->name : "Slot " + std::to_string(step.arguments[0]);
    if (ImGui::BeginCombo("Motion", label.c_str())) {
        for (const auto &motion : motions_) {
            if (motion.group != 0)
                continue;
            ImGui::PushID(int(motion.slot));
            auto title = motion.name + (motion.skeletal.looping ? " / Loop" : " / Once");
            ImGui::BeginDisabled(!motion.error.empty());
            if (ImGui::Selectable(title.c_str(), selected == &motion)) {
                stop_motion();
                step.arguments[0] = int(motion.slot);
                if (motion.skeletal.looping)
                    step.wait_for_completion = false;
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (ImGui::InputInt("Motion slot", &step.arguments[0])) {
        stop_motion();
        changed = true;
    }
    if (ImGui::TreeNode("Advanced motion arguments")) {
        changed |= ImGui::InputInt("Argument 3", &step.arguments[1]);
        changed |= ImGui::InputInt("Argument 4", &step.arguments[2]);
        ImGui::TextWrapped("Default cells: 0, -1. Their full behavior is not verified.");
        ImGui::TreePop();
    }
    if (selected && int(selected->slot) != step.arguments[0])
        selected = nullptr;
    if (selected) {
        ImGui::Text("%.0f frames / %s", selected->skeletal.frames,
                    selected->skeletal.looping ? "Looping" : "One shot");
        if (selected->skeletal.looping && step.wait_for_completion) {
            ImGui::TextWrapped("Waiting on this loop can stall the script.");
            if (ImGui::Button("Do not wait")) {
                step.wait_for_completion = false;
                changed = true;
            }
        }
        ImGui::BeginDisabled(selected->skeletal.tracks.empty() || !selected->error.empty());
        if (ImGui::Button("Preview motion on map")) {
            stop_motion();
            int target = region(step.actor);
            for (unsigned i = 0; i < scene_->draws.size(); ++i) {
                auto &draw = scene_->draws[i];
                if (draw.skeleton < 0 || SceneBrowser::character_region(*scene_, int(i)) != target)
                    continue;
                unsigned index = unsigned(draw.skeleton);
                if (originals_.contains(index))
                    continue;
                auto &rig = scene_->skeletons.at(index);
                originals_[index] = rig;
                rig.motion = selected->skeletal;
                rig.overlays.clear();
                rig.daily = false;
                rig.seconds_offset = -renderer_->playback.seconds;
                rig.tracks.clear();
                for (auto &joint : rig.joints) {
                    auto track = std::find_if(rig.motion.tracks.begin(), rig.motion.tracks.end(),
                                              [&](auto &t) {
                                                  return t.name == joint.name;
                                              });
                    rig.tracks.push_back(track == rig.motion.tracks.end()
                                             ? -1
                                             : int(track - rig.motion.tracks.begin()));
                }
            }
            preview_scene_ = scene_;
            preview_slot_ = step.arguments[0];
        }
        ImGui::EndDisabled();
    }
    if (!originals_.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Stop preview"))
            stop_motion();
        if (!renderer_->playback.enabled || !renderer_->playback.skeletal)
            ImGui::TextWrapped("Enable map animation to play the motion preview.");
    }
    if (!motion_error_.empty())
        ImGui::TextWrapped("%s", motion_error_.c_str());
    return changed;
}
void ConversationSceneTools::initialize(ConversationStep &step) const {
    if (step.action == ConversationAction::MoveTo) {
        int index = region(step.actor);
        if (index >= 0)
            step.destination = scene_->spatial.regions[index].overworld->position;
    }
}
bool ConversationSceneTools::destination(ConversationStep &step) {
    bool changed = ImGui::InputFloat3("Destination XYZ", step.destination.data(), "%.3f");
    changed |= ImGui::InputFloat("Facing (degrees)", &step.angle);
    if (cursor_) {
        if (ImGui::Button("Pick with map cursor"))
            cursor_->begin_placement();
        ImGui::SameLine();
        ImGui::BeginDisabled(!cursor_->position());
        if (ImGui::Button("Use cursor position")) {
            step.destination = *cursor_->position();
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped("Click a map surface, then use its cursor position. Coordinates use the "
                           "world origin; this step resets the event base position.");
    }
    if (ImGui::TreeNode("Turning behavior")) {
        changed |=
            ImGui::InputFloat("Turn animation threshold (degrees)", &step.turn_threshold_degrees);
        ImGui::TextWrapped(
            "Turns smaller than this angle use an interpolated turn instead of the turn animation. "
            "Retail example: 20 degrees. This does not control walking speed.");
        ImGui::TreePop();
    }
    return changed;
}
bool ConversationSceneTools::reward(ConversationStep &step) {
    if (!items_loaded_) {
        items_loaded_ = true;
        try {
            items_ = load_pickup_item_names(source_);
            for (unsigned i = 0; i < items_.size(); ++i)
                items_[i] += " / " + std::to_string(i);
        } catch (const std::exception &e) {
            item_error_ = e.what();
        }
    }
    bool changed = choice("Item", step.item, items_, item_filter_, false);
    int id = int(step.item), quantity = int(step.quantity);
    if (ImGui::InputInt("Item ID", &id)) {
        step.item = unsigned(std::max(1, id));
        changed = true;
    }
    if (ImGui::InputInt("Quantity", &quantity)) {
        step.quantity = unsigned(std::clamp(quantity, 1, 999));
        changed = true;
    }
    ImGui::TextWrapped("Checks bag capacity before adding. Put dialogue and completion flags in "
                       "the Given branch; use Not given for a full bag or failed delivery.");
    if (!item_error_.empty())
        ImGui::TextWrapped("%s", item_error_.c_str());
    return changed;
}
std::string ConversationSceneTools::text_token() {
    if (ImGui::Combo(
            "Insert", &word_kind_,
            "Player name\0Pokemon species name\0Item name\0Number\0Next page\0Scroll text\0"))
        word_filter_[0] = 0;
    bool ready = true;
    try {
        if (word_kind_ == 1 || word_kind_ == 2) {
            if (word_kind_ == 1 && species_.empty())
                species_ =
                    decode_location_text(Archive(source_ / TargetProfile::location_text_archive)
                                             .decoded(TargetProfile::pokemon_names_member));
            if (word_kind_ == 2 && !items_loaded_) {
                items_ = load_pickup_item_names(source_);
                for (unsigned i = 0; i < items_.size(); ++i)
                    items_[i] += " / " + std::to_string(i);
                items_loaded_ = true;
            }
            unsigned value = unsigned(std::max(1, word_value_));
            const auto &names = word_kind_ == 1 ? species_ : items_;
            choice(word_kind_ == 1 ? "Species" : "Item", value, names, word_filter_, false);
            word_value_ = int(value);
            ImGui::InputInt("ID", &word_value_);
            ready = word_value_ > 0 && std::size_t(word_value_) < names.size();
        } else if (word_kind_ == 3) {
            ImGui::InputInt("Value", &word_value_);
            word_value_ = std::max(0, word_value_);
        }
    } catch (const std::exception &e) {
        ImGui::TextWrapped("%s", e.what());
        ready = false;
    }
    ImGui::BeginDisabled(!ready);
    bool append = ImGui::Button("Append to text");
    ImGui::EndDisabled();
    if (!append)
        return {};
    ImGui::CloseCurrentPopup();
    const std::string tokens[] = {"{player}",
                                  "{pokemon:" + std::to_string(word_value_) + "}",
                                  "{item:" + std::to_string(word_value_) + "}",
                                  "{number:" + std::to_string(word_value_) + "}",
                                  "{page}",
                                  "{scroll}"};
    return tokens[word_kind_];
}
bool ConversationSceneTools::encounter(ConversationStep &step) {
    if (!encounters_loaded_) {
        encounters_loaded_ = true;
        try {
            auto table = Archive(source_ / TargetProfile::script_events_archive)
                             .decoded(TargetProfile::static_encounters_member);
            require(table.size() % TargetProfile::static_encounter_record_size == 0,
                    "Invalid static encounter table");
            auto names =
                decode_location_text(Archive(source_ / TargetProfile::location_text_archive)
                                         .decoded(TargetProfile::pokemon_names_member));
            for (unsigned row = 0; row < table.size() / TargetProfile::static_encounter_record_size;
                 ++row) {
                auto at = row * TargetProfile::static_encounter_record_size;
                unsigned species = u16(table, at);
                auto name =
                    species < names.size() ? names[species] : "Species " + std::to_string(species);
                encounters_.push_back("Row " + std::to_string(row) + " / " + name);
            }
        } catch (const std::exception &e) {
            encounter_error_ = e.what();
        }
    }
    bool changed = choice("Encounter", step.encounter, encounters_, encounter_filter_, true);
    int row = int(step.encounter);
    if (ImGui::InputInt("Encounter row", &row)) {
        step.encounter = unsigned(std::max(0, row));
        changed = true;
    }
    ImGui::TextWrapped(
        "Uses an existing static encounter. Return restores the field fade and conversation. "
        "Defeat runs flag/work updates, then the game's recovery and ends this interaction.");
    ImGui::TextWrapped("Use If battle result within Returned for finer branches. Result numbers "
                       "are preserved without assuming their names.");
    if (!encounter_error_.empty())
        ImGui::TextWrapped("%s", encounter_error_.c_str());
    return changed;
}
bool ConversationSceneTools::trainer(ConversationStep &step) {
    if (!trainers_loaded_) {
        trainers_loaded_ = true;
        try {
            trainers_ = load_trainer_battles(source_);
            species_ = decode_location_text(Archive(source_ / TargetProfile::location_text_archive)
                                                .decoded(TargetProfile::pokemon_names_member));
        } catch (const std::exception &e) {
            trainer_error_ = e.what();
        }
    }
    auto selected = std::find_if(trainers_.begin(), trainers_.end(), [&](const auto &entry) {
        return entry.id == step.trainer;
    });
    std::string label =
        selected == trainers_.end() ? "Trainer " + std::to_string(step.trainer) : selected->name;
    bool changed = false;
    if (ImGui::BeginCombo("Trainer", label.c_str())) {
        ImGui::InputTextWithHint("##trainer-filter", "Find trainer name or ID", trainer_filter_,
                                 sizeof(trainer_filter_));
        ImGui::BeginChild("trainer-list", {0, 240});
        for (const auto &entry : trainers_) {
            if (!matches(entry.name, trainer_filter_))
                continue;
            ImGui::BeginDisabled(!entry.error.empty());
            if (ImGui::Selectable(entry.name.c_str(), entry.id == step.trainer)) {
                step.trainer = entry.id;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
        }
        ImGui::EndChild();
        ImGui::EndCombo();
    }
    int id = int(step.trainer);
    if (ImGui::InputInt("Trainer ID", &id)) {
        step.trainer = unsigned(std::max(1, id));
        changed = true;
    }
    selected = std::find_if(trainers_.begin(), trainers_.end(), [&](const auto &entry) {
        return entry.id == step.trainer;
    });
    if (selected != trainers_.end()) {
        if (!selected->error.empty())
            ImGui::TextWrapped("%s", selected->error.c_str());
        for (const auto &pokemon : selected->team) {
            auto name = pokemon.species < species_.size()
                            ? species_[pokemon.species]
                            : "Species " + std::to_string(pokemon.species);
            ImGui::BulletText("%s / level %u / form %u", name.c_str(), pokemon.level, pokemon.form);
        }
        if (selected->mode <= 1)
            ImGui::Text("Battle rule: %s", selected->mode ? "Double battle" : "Single battle");
        else
            ImGui::Text("Battle rule from trainer record: %u", selected->mode);
    }
    ImGui::TextWrapped(
        "Uses the project's existing trainer and team. Victory returns to this conversation; "
        "defeat runs recovery. Add a flag explicitly to prevent rematches.");
    if (!trainer_error_.empty())
        ImGui::TextWrapped("%s", trainer_error_.c_str());
    return changed;
}

}
