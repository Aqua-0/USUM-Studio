#include "native/tutorial_widgets.h"
#include "native/overworld_editor.h"
#include "field/pickup_document.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
namespace studio {
namespace {
std::string lower(std::string s) {
    for (auto &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
void number(const char *label, unsigned &value) {
    ImGui::InputScalar(label, ImGuiDataType_U32, &value);
}
}
void OverworldEditor::set_scene(std::shared_ptr<Environment> scene, unsigned area,
                                const std::filesystem::path &dump) {
    scene_ = std::move(scene);
    area_ = area;
    dump_ = dump;
    document_.reset();
    selected_ = -1;
    editing_ = false;
    message_.clear();
    props_.clear();
    items_.clear();
    try {
        Archive archive(scene_->archive_sources.resolve(dump, TargetProfile::field_archive));
        auto base = area * TargetProfile::area_stride;
        auto objects = archive.decoded(base + TargetProfile::static_resource_slot);
        document_ = std::make_unique<OverworldDocument>(
            area, scene_->placement_source,
            archive.decoded(base + TargetProfile::character_resource_slot), objects);
        for (auto &b : Container::parse(objects, "AS").files) {
            auto sm = Container::parse(b, "SM");
            props_.push_back(u16(sm.files.at(0), 0));
        }
        items_ = load_pickup_item_names(dump);
        binding_.bind(
            "overworld", "overworld/" + std::to_string(area),
            "Overworld placements in area " + std::to_string(area), std::to_string(area),
            [this] {
                return document_ && document_->dirty();
            },
            [this] {
                return project_text(document_->serialize());
            },
            [this] {
                document_->mark_saved();
            });
        binding_.autosave_when([this] {
            return !editing_;
        });
        if (auto p = binding_.document(); !p.empty()) {
            document_->restore(text(read_file(p)));
            binding_.restored();
        }
    } catch (const std::exception &e) {
        document_.reset();
        message_ = e.what();
    }
}
void OverworldEditor::begin(OverworldOperation::Action action) {
    require(document_ && selected_ >= 0, "Select a placement first");
    require(project_store() != nullptr, "Open an editor project first");
    save_editor_project();
    for (auto &[key, e] : project_store()->edits)
        if ((e.kind == "placements" || e.kind == "pickups" || e.kind == "warps" ||
             e.kind == "encounters") &&
            e.parameters == std::to_string(area_))
            throw std::runtime_error("Stage and reload the existing property edits before changing "
                                     "placement lists in this area.");
    draft_ = document_->draft(unsigned(selected_), action);
    editing_ = true;
    flag_reserved_ = false;
    message_.clear();
}
void OverworldEditor::draw(ViewportCamera &camera, bool loading,
                           const std::function<void()> &stage_reload, const SpatialPoint *cursor) {
    if (model_job_.valid() &&
        model_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            models_ = model_job_.get();
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    ImGui::Begin("Map editing");
    if (studio::TutorialWidgets::Button("overworld_editor", "NPCs and triggers", ImVec2(-1, 0))) {
        open_ = true;
        ImGui::SetNextWindowFocus();
        if (document_ && scene_ && renderer_.spatial.selected >= 0 &&
            std::size_t(renderer_.spatial.selected) < scene_->spatial.regions.size()) {
            auto &r = scene_->spatial.regions[renderer_.spatial.selected];
            if (r.overworld)
                for (unsigned i = 0; i < document_->entries().size(); ++i) {
                    auto &e = document_->entries()[i];
                    const unsigned cats[] = {10, 4, 1, 7, 2, 3, 0};
                    if (cats[unsigned(e.kind)] == r.overworld->category &&
                        e.zone == r.overworld->local_zone && e.row == r.overworld->row)
                        selected_ = int(i);
                }
        }
    }
    if (pending())
        ImGui::TextWrapped(
            "Placement list changes pending. Apply and reload them before using property editors.");
    ImGui::End();
    if (!open_)
        return;
    ImGui::SetNextWindowSize({700, 760}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Overworld placements", &open_)) {
        auto attempt = [&](auto action) {
            try {
                action();
                message_.clear();
            } catch (const std::exception &e) {
                message_ = e.what();
            }
        };
        ImGui::TextWrapped(
            "Add from an existing placement in this zone, edit its position/model, or delete it. "
            "New placements keep the template's behavior and conditions.");
        if (!document_) {
            ImGui::TextWrapped("Unavailable: %s", message_.c_str());
            ImGui::End();
            return;
        }
        ImGui::BeginDisabled(loading || !project_store());
        ImGui::Combo("Type", &filter_,
                     "All\0Item pickups\0Static props\0NPCs\0Trainers\0Warps\0Scenery "
                     "interactions\0Story triggers\0");
        ImGui::InputTextWithHint("##placement-search", "Search type, event, model or zone", search_,
                                 sizeof(search_));
        auto query = lower(search_);
        ImGui::BeginChild("Placement list", {0, 210}, ImGuiChildFlags_Borders);
        for (unsigned i = 0; i < document_->entries().size(); ++i) {
            auto &e = document_->entries()[i];
            if (filter_ && unsigned(filter_ - 1) != unsigned(e.kind))
                continue;
            auto label =
                std::string(overworld_kind_name(e.kind)) + " / zone slot " +
                std::to_string(e.zone) + " / " +
                (e.kind == OverworldKind::StoryTrigger ? "script " + std::to_string(e.script)
                                                       : "event " + std::to_string(e.event));
            if (e.model)
                label += " / model " + std::to_string(e.model);
            if (!query.empty() && lower(label).find(query) == std::string::npos)
                continue;
            ImGui::PushID(int(i));
            if (ImGui::Selectable(label.c_str(), selected_ == int(i),
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                selected_ = int(i);
                editing_ = false;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && scene_) {
                    auto low = e.position, high = e.position;
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        low[axis] -= 80;
                        high[axis] += 80;
                    }
                    const unsigned categories[] = {10, 4, 1, 7, 2, 3, 0};
                    for (std::size_t region = 0; region < scene_->spatial.regions.size();
                         ++region) {
                        auto &r = scene_->spatial.regions[region];
                        if (!r.overworld || r.overworld->category != categories[unsigned(e.kind)] ||
                            r.overworld->local_zone != e.zone || r.overworld->row != e.row)
                            continue;
                        renderer_.spatial.selected = int(region);
                        for (auto &vertex : r.vertices)
                            for (unsigned axis = 0; axis < 3; ++axis) {
                                low[axis] = std::min(low[axis], vertex.position[axis]);
                                high[axis] = std::max(high[axis], vertex.position[axis]);
                            }
                    }
                    renderer_.player.active = false;
                    camera.fit(low, high);
                    camera.bounds_low = scene_->low;
                    camera.bounds_high = scene_->high;
                }
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::TextDisabled("Double-click a placement to frame it in the map.");
        ImGui::BeginDisabled(selected_ < 0);
        const auto &style = ImGui::GetStyle();
        const bool wide_actions =
            ImGui::GetContentRegionAvail().x >=
            ImGui::CalcTextSize("Add from selectedEdit selectedDelete selected").x +
                6 * style.FramePadding.x + 2 * style.ItemSpacing.x;
        const ImVec2 action_size{wide_actions ? 0.f : -1.f, 0};
        if (studio::TutorialWidgets::Button("overworld_editor", "Add from selected", action_size))
            attempt([&] {
                begin(OverworldOperation::Action::Add);
            });
        if (wide_actions)
            ImGui::SameLine();
        if (studio::TutorialWidgets::Button("overworld_editor", "Edit selected", action_size))
            attempt([&] {
                begin(OverworldOperation::Action::Update);
            });
        if (wide_actions)
            ImGui::SameLine();
        if (studio::TutorialWidgets::Button("overworld_editor", "Delete selected", action_size))
            attempt([&] {
                begin(OverworldOperation::Action::Remove);
            });
        ImGui::EndDisabled();
        if (editing_) {
            auto &e = document_->entries()[draft_.entry];
            bool add = draft_.action == OverworldOperation::Action::Add,
                 remove = draft_.action == OverworldOperation::Action::Remove;
            ImGui::SeparatorText(remove ? "Delete placement"
                                 : add  ? "New placement"
                                        : "Edit placement");
            if (!e.restriction.empty())
                ImGui::TextWrapped("%s", e.restriction.c_str());
            if (remove) {
                ImGui::TextWrapped("Delete %s / event %u in zone slot %u. Pickup deletion includes "
                                   "its verified visible object. Shared model resources and "
                                   "scripts remain available to other placements.",
                                   overworld_kind_name(e.kind), e.event, e.zone);
                ImGui::TextWrapped("Scripts that refer to this event are not rewritten. Review "
                                   "those dependencies before removing an actor or interaction.");
                if (e.kind == OverworldKind::Entrance)
                    ImGui::TextWrapped("Staging checks incoming warps and rejects deletion while "
                                       "an entrance still points here.");
            } else {
                if (cursor &&
                    studio::TutorialWidgets::Button("overworld_editor", "Use 3D cursor position"))
                    draft_.position = *cursor;
                ImGui::InputFloat3("Position", draft_.position.data());
                if (add && e.kind != OverworldKind::StoryTrigger)
                    number("Event ID", draft_.event);
                if (e.kind == OverworldKind::Character || e.kind == OverworldKind::Trainer) {
                    number("Character model ID", draft_.model);
                    if (models_.empty()) {
                        ImGui::BeginDisabled(model_job_.valid());
                        if (studio::TutorialWidgets::Button("overworld_editor",
                                                            model_job_.valid()
                                                                ? "Loading character catalog..."
                                                                : "Browse character models")) {
                            auto path = dump_ / TargetProfile::character_archive;
                            model_job_ = std::async(std::launch::async, [path] {
                                return load_model_library(path, ModelCategory::FieldCharacters);
                            });
                        }
                        ImGui::EndDisabled();
                    } else if (ImGui::BeginCombo("Character model",
                                                 std::to_string(draft_.model).c_str())) {
                        for (auto &m : models_)
                            if (m.member > 1) {
                                auto label = m.name + " (" + std::to_string(m.member) + ")";
                                if (ImGui::Selectable(label.c_str(), draft_.model == m.member))
                                    draft_.model = unsigned(m.member);
                            }
                        ImGui::EndCombo();
                    }
                    ImGui::TextWrapped("Missing character resources are included in the area "
                                       "automatically. Existing script %u and movement settings "
                                       "are retained; trainer copies share that battle behavior.",
                                       e.script);
                }
                if (e.kind == OverworldKind::StaticObject) {
                    if (ImGui::BeginCombo("Area static resource",
                                          std::to_string(draft_.model).c_str())) {
                        for (auto id : props_)
                            if (ImGui::Selectable(std::to_string(id).c_str(), draft_.model == id))
                                draft_.model = id;
                        ImGui::EndCombo();
                    }
                    ImGui::TextWrapped("Collision is copied from the template; review whether it "
                                       "fits a different model.");
                }
                if (e.kind == OverworldKind::Pickup) {
                    auto name = draft_.item < items_.size() ? items_[draft_.item]
                                                            : std::to_string(draft_.item);
                    if (ImGui::BeginCombo("Item", name.c_str())) {
                        for (unsigned i = 1; i < items_.size(); ++i)
                            if (!items_[i].empty()) {
                                auto label = items_[i] + " (" + std::to_string(i) + ")";
                                if (ImGui::Selectable(label.c_str(), draft_.item == i))
                                    draft_.item = i;
                            }
                        ImGui::EndCombo();
                    }
                    number("Quantity", draft_.quantity);
                    number("Collection flag", draft_.flag);
                    if (add || draft_.flag != e.condition) {
                        ImGui::TextWrapped(
                            "Use a persistent flag you have reserved for this pickup. Staging "
                            "detects known placement uses; it cannot prove a flag unused by "
                            "scripts or other game systems.");
                        studio::TutorialWidgets::Checkbox(
                            "overworld_editor", "I have reserved this flag for this pickup",
                            &flag_reserved_);
                    }
                    if (!add)
                        ImGui::TextWrapped(
                            "Changing the flag may make this pickup reappear or disappear in an "
                            "existing save. Its linked object uses the same flag.");
                }
                if (e.kind == OverworldKind::Entrance) {
                    number("Destination zone", draft_.destination_zone);
                    number("Destination entrance", draft_.destination_event);
                    ImGui::TextWrapped(
                        "The template supplies transition style, trigger and arrival offset. Add "
                        "the return entrance separately; existing warp tools can adjust arrival "
                        "and facing after reload.");
                }
                if (e.kind == OverworldKind::StoryTrigger || e.kind == OverworldKind::Interaction)
                    ImGui::TextWrapped(
                        "Reuses script %u in the same zone. Script conditions and references to "
                        "other actors remain unchanged. General script authoring is separate.",
                        e.script);
            }
            ImGui::BeginDisabled(!e.restriction.empty() ||
                                 ((add || draft_.flag != e.condition) &&
                                  e.kind == OverworldKind::Pickup && !flag_reserved_));
            if (studio::TutorialWidgets::Button("overworld_editor", remove ? "Queue deletion"
                                                                    : add  ? "Queue addition"
                                                                           : "Queue update"))
                attempt([&] {
                    document_->apply(draft_);
                    editing_ = false;
                });
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("overworld_editor", "Cancel"))
                editing_ = false;
        }
        ImGui::SeparatorText("Pending changes");
        for (auto &o : document_->operations()) {
            auto &e = document_->entries()[o.entry];
            ImGui::BulletText("%s %s / event %u / zone slot %u",
                              o.action == OverworldOperation::Action::Add      ? "Add"
                              : o.action == OverworldOperation::Action::Remove ? "Delete"
                                                                               : "Update",
                              overworld_kind_name(e.kind), o.event, e.zone);
        }
        ImGui::BeginDisabled(!document_->can_undo());
        if (studio::TutorialWidgets::Button("overworld_editor", "Undo"))
            document_->undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!document_->can_redo());
        if (studio::TutorialWidgets::Button("overworld_editor", "Redo"))
            document_->redo();
        ImGui::EndDisabled();
        if (wide_actions)
            ImGui::SameLine();
        if (studio::TutorialWidgets::Button("overworld_editor", "Save Project", action_size))
            attempt([&] {
                save_editor_project();
            });
        if (wide_actions)
            ImGui::SameLine();
        ImGui::BeginDisabled(!document_->changed() || editing_);
        if (studio::TutorialWidgets::Button("overworld_editor", "Apply and reload map",
                                            action_size))
            attempt(stage_reload);
        ImGui::EndDisabled();
        ImGui::TextWrapped("Apply validates and stages these changes, then reloads the map.");
        ImGui::EndDisabled();
        if (!message_.empty())
            ImGui::TextWrapped("%s", message_.c_str());
    }
    ImGui::End();
}
}
