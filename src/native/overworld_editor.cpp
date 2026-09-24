#include "native/tutorial_widgets.h"
#include "native/overworld_editor.h"
#include "field/pickup_document.h"
#include "field/placement_document.h"
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
void identify_placements(Environment &scene, const OverworldDocument &document) {
    const auto entries = document.working_entries();
    constexpr unsigned categories[]{10, 4, 1, 7, 2, 3, 0};
    for (auto &region : scene.spatial.regions) {
        if (!region.overworld)
            continue;
        const auto &r = *region.overworld;
        auto found = std::find_if(entries.begin(), entries.end(), [&](const auto &entry) {
            return categories[unsigned(entry.entry.kind)] == r.category &&
                   entry.entry.zone == r.local_zone && entry.entry.row == r.row;
        });
        if (found != entries.end()) {
            auto reference = std::make_shared<OverworldReference>(r);
            reference->editor_id = found->id;
            region.overworld = std::move(reference);
        }
    }
}
void number(const char *label, unsigned &value) {
    ImGui::InputScalar(label, ImGuiDataType_U32, &value);
}
}
void OverworldEditor::set_scene(std::shared_ptr<Environment> scene, unsigned area,
                                const std::filesystem::path &dump) {
    cancel_shape_drag();
    shape_group_ = shape_index_ = -1;
    shape_undo_.clear();
    shape_redo_.clear();
    scene_ = std::move(scene);
    area_ = area;
    dump_ = dump;
    document_.reset();
    selected_ = preview_focus_ = 0;
    ++scene_generation_;
    preview_source_.clear();
    preview_requested_.clear();
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
        identify_placements(*scene_, *document_);
        preview_source_ = document_->serialize();
        preview_document_ = std::make_unique<OverworldDocument>(*document_);
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
        binding_.ready([this] {
            require(!editing_, "Apply or cancel the placement draft before saving");
            if (prepare_save)
                prepare_save();
        });
        binding_.autosave_when([this] {
            return !editing_ && !ImGui::IsAnyItemActive();
        });
        if (auto p = binding_.document(); !p.empty()) {
            document_->restore(text(read_file(p)));
            binding_.restored();
        }
        if (auto store = project_store()) {
            const auto key = "placements/" + std::to_string(area);
            if (store->edits.contains(key)) {
                PlacementDocument legacy(area, scene_->placement_source);
                legacy.restore(text(read_file(store->document(key))));
                auto merged = *document_;
                const auto working = merged.working_entries();
                for (unsigned i = 0; i < legacy.entries().size(); ++i) {
                    const auto &source = legacy.entries()[i];
                    const auto &state = legacy.state(i);
                    if (state.position == source.source.position && state.turn == 0)
                        continue;
                    auto found =
                        std::find_if(working.begin(), working.end(), [&](const auto &entry) {
                            return entry.entry.kind == (source.trainer ? OverworldKind::Trainer
                                                        : source.character
                                                            ? OverworldKind::Character
                                                            : OverworldKind::StaticObject) &&
                                   entry.entry.zone == source.zone &&
                                   entry.entry.event == source.source.event;
                        });
                    require(found != working.end(),
                            "A saved placement was deleted; resolve its conflicting edits first");
                    auto edit = merged.working_draft(found->id, OverworldOperation::Action::Update);
                    require((edit.position == source.source.position ||
                             edit.position == state.position) &&
                                (edit.turn == 0 || edit.turn == state.turn),
                            "Saved placement transforms conflict with the placement list edits");
                    edit.position = state.position;
                    edit.turn = state.turn;
                    merged.apply_working(found->id, edit);
                }
                *document_ = std::move(merged);
                store->edits.erase(key);
                binding_.imported();
            }
        }
    } catch (const std::exception &e) {
        document_.reset();
        message_ = e.what();
    }
}
std::shared_ptr<Environment>
OverworldEditor::poll_preview(bool loading, const std::function<void()> &refresh_transforms) {
    if (!document_)
        return {};
    const auto current = document_->serialize();
    if (preview_job_.valid()) {
        if (preview_job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return {};
        try {
            auto result = std::make_shared<Environment>(preview_job_.get());
            if (!loading && preview_generation_ == scene_generation_ &&
                current == preview_requested_) {
                identify_placements(*result, *document_);
                preview_source_ = current;
                preview_document_ = std::make_unique<OverworldDocument>(*document_);
                scene_ = result;
                return result;
            }
        } catch (const std::exception &e) {
            if (preview_generation_ == scene_generation_) {
                message_ = "Map preview: " + std::string(e.what());
                preview_source_ = preview_requested_;
            }
        }
    }
    if (!loading && current != preview_source_ && preview_document_ &&
        document_->same_structure(*preview_document_)) {
        refresh_transforms();
        preview_focus_ = 0;
        preview_source_ = current;
        preview_document_ = std::make_unique<OverworldDocument>(*document_);
        return {};
    }
    if (!loading && current != preview_source_) {
        preview_requested_ = current;
        preview_generation_ = scene_generation_;
        auto document = *document_;
        preview_job_ = std::async(std::launch::async, [document = std::move(document), dump = dump_,
                                                       area = area_,
                                                       archives = scene_->archive_sources] {
            return load_environment(dump, area, nullptr, archives, document.preview_members(dump));
        });
    }
    return {};
}
void OverworldEditor::inspect(const OverworldReference &reference) {
    if (!document_ || !reference.editor_id)
        return;
    open_ = true;
    selected_ = reference.editor_id;
    try {
        begin(OverworldOperation::Action::Update);
    } catch (const std::exception &e) {
        message_ = e.what();
    }
}
void OverworldEditor::begin(OverworldOperation::Action action) {
    require(document_ && selected_ != 0, "Select a placement first");
    require(project_store() != nullptr, "Open an editor project first");
    for (auto &[key, e] : project_store()->edits)
        if ((e.kind == "placements" || e.kind == "pickups" || e.kind == "warps" ||
             e.kind == "encounters") &&
            e.parameters == std::to_string(area_))
            throw std::runtime_error("Stage and reload the existing property edits before changing "
                                     "placement lists in this area.");
    cancel_shape_drag();
    shape_group_ = shape_index_ = -1;
    shape_undo_.clear();
    shape_redo_.clear();
    draft_ = document_->working_draft(selected_, action);
    shape_groups_ = document_->shape_groups(draft_.entry);
    patrol_editing_ = false;
    patrol_axis_ = -1;
    patrol_point_ = 0;
    patrol_undo_.clear();
    patrol_redo_.clear();
    if (document_->entries()[draft_.entry].kind == OverworldKind::Trainer)
        patrol_draft_ = draft_.patrol.value_or(document_->trainer_patrol(draft_.entry));
    dialogue_mode_ = !draft_.dialogue.empty() ? 2 : draft_.script >= 0 ? 1 : 0;
    std::snprintf(dialogue_, sizeof(dialogue_), "%s", draft_.dialogue.c_str());
    editing_ = true;
    flag_reserved_ = false;
    message_.clear();
}
void OverworldEditor::shapes(ViewportCamera &camera) {
    if (!ImGui::CollapsingHeader("Interaction and collision shapes"))
        return;
    ImGui::TextWrapped("Offsets are relative to this placement. Edit in viewport shows a live "
                       "draft outline. Apply changes, then save or stage.");
    if (shape_active()) {
        patrol_editing_ = false;
        ImGui::RadioButton("Move (G)", &shape_mode_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Scale (S)", &shape_mode_, 1);
        const auto g = unsigned(shape_group_);
        const auto &selected_shapes =
            draft_.shapes.contains(g) ? draft_.shapes.at(g) : shape_groups_[g].shapes;
        if (selected_shapes[shape_index_].type == 2)
            ImGui::Combo("Line endpoint", &shape_endpoint_, "Start\0End\0");
        ImGui::TextWrapped("Ctrl: snap | Esc: cancel");
        if (ImGui::Button("Stop viewport editing")) {
            cancel_shape_drag();
            shape_group_ = -1;
        }
    }
    ImGui::BeginDisabled(shape_undo_.empty() || drag_axis_ >= 0);
    if (ImGui::Button("Undo shape"))
        undo_shape(false);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(shape_redo_.empty() || drag_axis_ >= 0);
    if (ImGui::Button("Redo shape"))
        undo_shape(true);
    ImGui::EndDisabled();
    auto before = draft_.shapes;
    for (unsigned g = 0; g < shape_groups_.size(); ++g) {
        ImGui::PushID(int(g));
        const auto &group = shape_groups_[g];
        if (ImGui::TreeNodeEx(group.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            auto values = draft_.shapes.contains(g) ? draft_.shapes.at(g) : group.shapes;
            bool changed = false;
            for (unsigned i = 0; i < values.size(); ++i) {
                ImGui::PushID(int(i));
                auto &v = values[i];
                ImGui::SeparatorText(("Shape " + std::to_string(i + 1)).c_str());
                if (v.type < 3 && ImGui::Button(shape_group_ == int(g) && shape_index_ == int(i)
                                                    ? "Editing in viewport"
                                                    : "Edit in viewport")) {
                    cancel_shape_drag();
                    shape_group_ = int(g);
                    shape_index_ = int(i);
                    renderer_.player.active = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Frame shape")) {
                    try {
                        auto geometry = preview_placement_volumes({v}, draft_.position);
                        SpatialPoint low{}, high{};
                        bool first = true;
                        for (auto &r : geometry.regions)
                            for (auto &vertex : r.vertices) {
                                if (first) {
                                    low = high = vertex.position;
                                    first = false;
                                }
                                for (unsigned k = 0; k < 3; ++k) {
                                    low[k] = std::min(low[k], vertex.position[k]);
                                    high[k] = std::max(high[k], vertex.position[k]);
                                }
                            }
                        if (!first) {
                            renderer_.player.active = false;
                            camera.fit(low, high);
                            camera.distance *= 1.8f;
                            camera.bounds_low = scene_->low;
                            camera.bounds_high = scene_->high;
                        }
                    } catch (const std::exception &e) {
                        message_ = e.what();
                    }
                }
                int type = int(v.type);
                if (v.type == 3)
                    ImGui::TextWrapped("Stored triangle: preserved. Runtime support is not "
                                       "verified; edit other shapes or remove this one.");
                else {
                    if (ImGui::Combo("Shape", &type, "Cylinder\0Box\0Line wall\0")) {
                        std::array<float, 3> origin{v.values[0], v.values[1], v.values[2]};
                        v = {};
                        v.type = unsigned(type);
                        std::copy(origin.begin(), origin.end(), v.values.begin());
                        if (type == 0) {
                            v.values[3] = 30;
                            v.values[4] = 80;
                        }
                        if (type == 1) {
                            v.values[6] = 1;
                            v.values[7] = 30;
                            v.values[8] = 80;
                            v.values[9] = 30;
                        }
                        if (type == 2) {
                            v.values[3] = origin[0] + 60;
                            v.values[4] = origin[1];
                            v.values[5] = origin[2];
                            v.values[6] = 80;
                        }
                        changed = true;
                    }
                    changed |= ImGui::InputFloat3(
                        type == 2 ? "Start offset" : "Center / base offset", v.values.data());
                    if (type == 0) {
                        changed |= ImGui::InputFloat("Radius", &v.values[3]);
                        changed |= ImGui::InputFloat("Height", &v.values[4]);
                    } else if (type == 1) {
                        float dimensions[]{v.values[7] * 2, v.values[8], v.values[9] * 2};
                        if (ImGui::InputFloat3("Width / height / depth", dimensions)) {
                            v.values[7] = dimensions[0] * .5f;
                            v.values[8] = dimensions[1];
                            v.values[9] = dimensions[2] * .5f;
                            changed = true;
                        }
                        if (ImGui::TreeNode("Advanced rotation")) {
                            changed |= ImGui::InputFloat4("Quaternion XYZW", &v.values[3]);
                            ImGui::TreePop();
                        }
                    } else {
                        changed |= ImGui::InputFloat3("End offset", &v.values[3]);
                        changed |= ImGui::InputFloat("Height", &v.values[6]);
                    }
                }
                bool remove = ImGui::Button("Remove shape");
                ImGui::PopID();
                if (remove) {
                    if (shape_group_ == int(g)) {
                        if (shape_index_ == int(i))
                            shape_group_ = -1;
                        else if (shape_index_ > int(i))
                            --shape_index_;
                    }
                    values.erase(values.begin() + i);
                    changed = true;
                    break;
                }
            }
            if (ImGui::Button("Add cylinder")) {
                PlacementVolume v;
                v.values[3] = 30;
                v.values[4] = 80;
                values.push_back(v);
                changed = true;
            }
            ImGui::SameLine();
            bool reset = ImGui::Button("Restore original shapes");
            bool empty = values.empty();
            if (reset)
                draft_.shapes.erase(g);
            else if (changed)
                draft_.shapes[g] = std::move(values);
            if (empty)
                ImGui::TextDisabled("No explicit placement shapes.");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (before != draft_.shapes)
        remember_shapes(std::move(before));
}
void OverworldEditor::draw(ViewportCamera &camera, bool loading,
                           const std::function<void()> &stage_reload, const SpatialPoint *cursor,
                           bool show_launcher) {
    loading_ = loading;
    if (model_job_.valid() &&
        model_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            models_ = model_job_.get();
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    if (show_launcher) {
        ImGui::Begin("Map inspector");
        if (studio::TutorialWidgets::Button("overworld_editor", "NPCs and triggers",
                                            ImVec2(-1, 0))) {
            open_ = true;
            ImGui::SetNextWindowFocus();
            if (document_ && scene_ && renderer_.spatial.selected >= 0 &&
                std::size_t(renderer_.spatial.selected) < scene_->spatial.regions.size()) {
                auto &r = scene_->spatial.regions[renderer_.spatial.selected];
                if (r.overworld && r.overworld->editor_id)
                    selected_ = r.overworld->editor_id;
            }
        }
        ImGui::End();
    }
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
            "Use a placement as a template, then choose its position, model and interaction. "
            "Movement and visibility conditions are inherited.");
        if (!document_) {
            ImGui::TextWrapped("Unavailable: %s", message_.c_str());
            ImGui::End();
            return;
        }
        ImGui::BeginDisabled(loading || !project_store());
        const auto &working = document_->working_entries();
        if (selected_ && !editing_) {
            if (std::none_of(working.begin(), working.end(), [&](const auto &e) {
                    return e.id == selected_;
                }))
                selected_ = 0;
        }
        ImGui::Combo("Type", &filter_,
                     "All\0Item pickups\0Static props\0NPCs\0Trainers\0Warps\0Scenery "
                     "interactions\0Story triggers\0");
        ImGui::InputTextWithHint("##placement-search", "Search type, event, model or zone", search_,
                                 sizeof(search_));
        auto query = lower(search_);
        ImGui::BeginChild("Placement list", {0, 210}, ImGuiChildFlags_Borders);
        for (unsigned i = 0; i < working.size(); ++i) {
            const auto &e = working[i].entry;
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
            if (ImGui::Selectable(label.c_str(), selected_ == working[i].id,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                selected_ = working[i].id;
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
        ImGui::BeginDisabled(selected_ == 0);
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
                                   overworld_kind_name(e.kind), draft_.event, e.zone);
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
                    if (e.kind == OverworldKind::Character) {
                        ImGui::SeparatorText("Interaction");
                        if (ImGui::Combo("Behavior", &dialogue_mode_,
                                         "Keep existing\0Existing local script\0New dialogue\0")) {
                            draft_.script = dialogue_mode_ == 1 ? int(e.script) : -1;
                            draft_.dialogue.clear();
                        }
                        if (dialogue_mode_ == 0)
                            ImGui::Text("Script %u", e.script);
                        else if (dialogue_mode_ == 1) {
                            ImGui::InputInt("Local script ID", &draft_.script);
                            ImGui::TextWrapped("Uses a handler already present in this zone.");
                        } else {
                            ImGui::TextUnformatted("Dialogue");
                            ImGui::InputTextMultiline("##npc-dialogue", dialogue_,
                                                      sizeof(dialogue_), ImVec2(-1, 110));
                            ImGui::TextWrapped("Creates a separate conversation. Keep each line "
                                               "short enough for the game text box.");
                        }
                        if (e.condition)
                            ImGui::TextWrapped(
                                "This template has a visibility condition (%u). "
                                "Choose an unconditional NPC for an always-visible test.",
                                e.condition);
                    } else
                        ImGui::TextWrapped(
                            "Trainer copies retain script %u and its battle behavior.", e.script);
                    ImGui::TextWrapped("Character resources are included automatically. Movement "
                                       "and visibility conditions are kept from the template.");
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
                        "other actors remain unchanged. Use Author interaction on the map marker "
                        "to replace its script after staging.",
                        e.script);
            }
            if (!remove) {
                if (e.kind == OverworldKind::Trainer)
                    patrol_controls(cursor);
                shapes(camera);
            }
            ImGui::BeginDisabled(drag_axis_ >= 0 || patrol_axis_ >= 0 || !e.restriction.empty() ||
                                 ((add || draft_.flag != e.condition) &&
                                  e.kind == OverworldKind::Pickup && !flag_reserved_));
            if (studio::TutorialWidgets::Button("overworld_editor", remove ? "Apply deletion"
                                                                    : add  ? "Apply addition"
                                                                           : "Apply changes"))
                attempt([&] {
                    if (e.kind == OverworldKind::Character && !remove) {
                        if (dialogue_mode_ == 2) {
                            require(dialogue_[0] != 0, "Enter dialogue first");
                            draft_.dialogue = dialogue_;
                            if (draft_.script < 1)
                                document_->prepare_dialogue(dump_, draft_);
                        } else {
                            draft_.dialogue.clear();
                            if (dialogue_mode_ == 0)
                                draft_.script = -1;
                            else
                                require(draft_.script > 0, "Enter a positive local script ID");
                        }
                    }
                    document_->apply_working(selected_, draft_);
                    if (add)
                        selected_ =
                            (std::uint64_t(1) << 32) | document_->operations().back().editor_id;
                    else if (remove)
                        selected_ = 0;
                    preview_focus_ = selected_;
                    editing_ = false;
                });
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("overworld_editor", "Cancel"))
                editing_ = false;
        }
        ImGui::SeparatorText(document_->dirty()     ? "Unsaved changes"
                             : document_->changed() ? "Saved — needs staging"
                                                    : "Staged");
        if (preview_job_.valid())
            ImGui::TextDisabled("Updating map preview...");
        if (ImGui::TreeNode("Change details")) {
            for (auto &o : document_->operations()) {
                auto &e = document_->entries()[o.entry];
                ImGui::BulletText("%s %s / event %u / zone slot %u",
                                  o.action == OverworldOperation::Action::Add      ? "Add"
                                  : o.action == OverworldOperation::Action::Remove ? "Delete"
                                                                                   : "Update",
                                  overworld_kind_name(e.kind), o.event, e.zone);
                if (!o.dialogue.empty())
                    ImGui::TextDisabled("New dialogue / script %d", o.script);
                else if (o.script >= 0)
                    ImGui::TextDisabled("Local script %d", o.script);
            }
            ImGui::TreePop();
        }
        ImGui::BeginDisabled(editing_ || !document_->can_undo());
        if (studio::TutorialWidgets::Button("overworld_editor", "Undo"))
            document_->undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(editing_ || !document_->can_redo());
        if (studio::TutorialWidgets::Button("overworld_editor", "Redo"))
            document_->redo();
        ImGui::EndDisabled();
        if (wide_actions)
            ImGui::SameLine();
        ImGui::BeginDisabled(editing_);
        if (studio::TutorialWidgets::Button("overworld_editor", "Save Project", action_size))
            attempt([&] {
                save_editor_project();
            });
        if (wide_actions)
            ImGui::SameLine();
        ImGui::BeginDisabled(!document_->changed() || editing_);
        if (studio::TutorialWidgets::Button("overworld_editor", "Stage and reload", action_size))
            attempt(stage_reload);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (editing_)
            ImGui::TextDisabled("Draft — Apply changes or Cancel first.");
        ImGui::TextWrapped("Save keeps your edits. Stage updates game resources.");
        ImGui::EndDisabled();
        if (!message_.empty())
            ImGui::TextWrapped("%s", message_.c_str());
    }
    ImGui::End();
    if (!open_ && editing_) {
        cancel_shape_drag();
        editing_ = false;
    }
}
}
