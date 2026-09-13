#include "native/tutorial_widgets.h"
#include "native/pickup_editor.h"
#include "field/area.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
namespace studio {
namespace {
std::string lowercase(std::string s) {
    for (auto &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}
void PickupEditor::set_scene(std::shared_ptr<Environment> scene, unsigned area,
                             const std::filesystem::path &dump) {
    scene_ = std::move(scene);
    area_ = area;
    dump_ = dump;
    document_.reset();
    visual_matrices_.clear();
    visual_offsets_.clear();
    original_spatial_ = scene_->spatial;
    names_.clear();
    message_.clear();
    try {
        document_ = std::make_unique<PickupDocument>(area, scene_->placement_source);
        names_ = load_pickup_item_names(dump);
        project_.bind(
            "pickups", "pickups/" + std::to_string(area),
            "Item pickups in area " + std::to_string(area), std::to_string(area),
            [this] {
                return document_ && document_->dirty();
            },
            [this] {
                document_->commit();
                document_->validate_items(names_);
                return project_text(document_->serialize());
            },
            [this] {
                document_->mark_saved();
            });
        if (auto file = project_.document(); !file.empty()) {
            document_->restore(text(read_file(file)));
            project_.restored();
        }
        synchronize();
    } catch (const std::exception &e) {
        document_.reset();
        message_ = e.what();
    }
}
void PickupEditor::synchronize() {
    std::map<unsigned, int> ids;
    for (auto &r : scene_->spatial.regions)
        if (r.kind == SpatialKind::Pickup && r.overworld && r.zone >= 0)
            ids[r.overworld->local_zone] = r.zone;
    SpatialScene next;
    decode_overworld_regions(next, document_->compile(), ids);
    for (auto &r : scene_->spatial.regions)
        if (r.kind == SpatialKind::Pickup && r.overworld) {
            auto it = std::find_if(next.regions.begin(), next.regions.end(), [&](auto &n) {
                return n.kind == r.kind && n.overworld &&
                       n.overworld->local_zone == r.overworld->local_zone &&
                       n.overworld->row == r.overworld->row;
            });
            if (it != next.regions.end()) {
                r = *it;
                auto item = r.overworld->item;
                if (item < names_.size())
                    r.name += " / " + names_[item];
            }
        }
    renderer_.spatial.invalidate_geometry();
}
void PickupEditor::synchronize_visuals() {
    if (!document_ || !scene_)
        return;
    bool changed = false;
    for (unsigned i = 0; i < document_->records().size(); ++i) {
        auto &r = document_->records()[i];
        if (r.visual < 0 || std::size_t(r.visual) >= scene_->placement_transforms.size())
            continue;
        auto next = scene_->placement_transforms[r.visual];
        auto delta = document_->values(i).position;
        for (unsigned k = 0; k < 3; ++k)
            delta[k] -= f32(scene_->placement_source, r.offset + 4 + k * 4);
        auto previous = visual_matrices_.find(r.visual);
        bool ours = previous != visual_matrices_.end() && next == previous->second;
        for (unsigned k = 0; k < 3; ++k)
            next[k * 4 + 3] += delta[k] - (ours ? visual_offsets_[r.visual][k] : 0);
        if (next != scene_->placement_transforms[r.visual]) {
            changed = true;
            scene_->placement_transforms[r.visual] = next;
            for (unsigned j = 0; j < scene_->spatial.regions.size(); ++j) {
                auto &region = scene_->spatial.regions[j];
                if (region.placement != r.visual)
                    continue;
                region.vertices = original_spatial_.regions[j].vertices;
                for (auto &vertex : region.vertices) {
                    auto v = vertex.position;
                    for (unsigned k = 0; k < 3; ++k)
                        vertex.position[k] = next[k * 4] * v[0] + next[k * 4 + 1] * v[1] +
                                             next[k * 4 + 2] * v[2] + next[k * 4 + 3];
                }
            }
        }
        visual_matrices_[r.visual] = next;
        visual_offsets_[r.visual] = delta;
    }
    if (changed) {
        renderer_.spatial.invalidate_geometry();
        renderer_.invalidate_selection_readback();
    }
}
void PickupEditor::draw(bool loading) {
    synchronize_visuals();
    int index = -1;
    auto selected = renderer_.spatial.selected;
    if (document_ && scene_ && selected >= 0 &&
        std::size_t(selected) < scene_->spatial.regions.size()) {
        auto &r = scene_->spatial.regions[selected];
        if (r.kind == SpatialKind::Pickup && r.overworld)
            for (unsigned i = 0; i < document_->records().size(); ++i) {
                auto &entry = document_->records()[i];
                if (entry.local_zone == r.overworld->local_zone && entry.row == r.overworld->row) {
                    index = int(i);
                    break;
                }
            }
    }
    ImGui::Begin("Map editing");
    ImGui::BeginDisabled(index < 0 || loading);
    if (studio::TutorialWidgets::Button("pickup_editor", "Edit selected pickup", ImVec2(-1, 0)))
        open_ = true;
    ImGui::EndDisabled();
    if (!document_ && !message_.empty())
        ImGui::TextWrapped("Pickup editing unavailable: %s", message_.c_str());
    ImGui::End();
    if (!open_)
        return;
    ImGui::SetNextWindowSize({530, 550}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Pickup editing", &open_)) {
        ImGui::TextWrapped("Edit an existing pickup. Change its reward, position or collection "
                           "flag. Appearance stays unchanged.");
        if (!project_store())
            ImGui::TextWrapped("Open an editor project to save and stage pickup edits.");
        if (index < 0)
            ImGui::TextWrapped("Select an item pickup in Maps > Spatial. Use the Interactions "
                               "preset to show pickups.");
        else {
            auto &r = document_->records()[index];
            auto v = document_->values(unsigned(index));
            ImGui::Text("Pickup %u / zone slot %u", r.event, r.local_zone);
            ImGui::TextWrapped(
                "Collection flag/work: %u | expected work value: %u | appearance: %u", v.flag,
                r.expected, r.appearance);
            ImGui::TextWrapped(
                "Changing the flag makes this pickup follow the new flag in your save: it may "
                "reappear or disappear. Save state is not evaluated here.");
            if (!r.restriction.empty())
                ImGui::TextWrapped("%s", r.restriction.c_str());
            ImGui::BeginDisabled(!project_store() || loading || !r.restriction.empty());
            auto apply = [&](auto action) {
                try {
                    action();
                    message_.clear();
                } catch (const std::exception &e) {
                    message_ = e.what();
                }
            };
            ImGui::BeginDisabled(!r.movement_restriction.empty());
            if (ImGui::DragFloat3("Position", v.position.data(), 1))
                apply([&] {
                    document_->set(unsigned(index), v);
                    synchronize();
                });
            if (ImGui::IsItemDeactivatedAfterEdit())
                document_->commit();
            ImGui::EndDisabled();
            if (!r.movement_restriction.empty())
                ImGui::TextWrapped("%s", r.movement_restriction.c_str());
            ImGui::TextWrapped("Moving the pickup moves its interaction trigger and linked object. "
                               "Ground collision is unchanged.");
            ImGui::SeparatorText("Collection state");
            ImGui::BeginDisabled(!r.movement_restriction.empty());
            int flag = int(v.flag);
            if (ImGui::InputInt("Collection flag", &flag, 0, 0,
                                ImGuiInputTextFlags_EnterReturnsTrue))
                apply([&] {
                    require(flag > 0 && flag < TargetProfile::saved_event_flag_count,
                            "Collection flag must be from 1 to 4927");
                    v.flag = unsigned(flag);
                    document_->set(unsigned(index), v);
                    document_->commit();
                    synchronize();
                });
            ImGui::EndDisabled();
            ImGui::TextWrapped("Enter a reserved flag and press Enter. Staging checks known "
                               "placement uses; script uses are not fully indexed.");
            ImGui::SeparatorText("Reward");
            ImGui::InputTextWithHint("##item-filter", "Search item name or ID", search_,
                                     sizeof(search_));
            auto query = lowercase(search_);
            std::string label = v.item < names_.size() ? names_[v.item] : "Unknown item";
            label += " (" + std::to_string(v.item) + ")";
            if (ImGui::BeginCombo("Item", label.c_str())) {
                for (unsigned i = 1; i < names_.size(); ++i)
                    if (!names_[i].empty() &&
                        (query.empty() || lowercase(names_[i]).find(query) != std::string::npos ||
                         std::to_string(i) == query)) {
                        ImGui::PushID(int(i));
                        auto name = names_[i] + " (" + std::to_string(i) + ")";
                        if (ImGui::Selectable(name.c_str(), v.item == i))
                            apply([&] {
                                v.item = i;
                                document_->set(unsigned(index), v);
                                document_->commit();
                                synchronize();
                            });
                        ImGui::PopID();
                    }
                ImGui::EndCombo();
            }
            int quantity = int(v.quantity);
            if (ImGui::InputInt("Quantity", &quantity)) {
                apply([&] {
                    require(quantity > 0 && quantity <= 65535, "Quantity must be from 1 to 65535");
                    v.quantity = unsigned(quantity);
                    document_->set(unsigned(index), v);
                    synchronize();
                });
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
                document_->commit();
            ImGui::TextWrapped("Items come from the dump's catalog. The game's pickup routine "
                               "still controls inventory capacity and special item handling.");
            ImGui::Separator();
            ImGui::BeginDisabled(!document_->can_undo());
            if (studio::TutorialWidgets::Button("pickup_editor", "Undo"))
                apply([&] {
                    document_->undo();
                    synchronize();
                });
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!document_->can_redo());
            if (studio::TutorialWidgets::Button("pickup_editor", "Redo"))
                apply([&] {
                    document_->redo();
                    synchronize();
                });
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("pickup_editor", "Save Project")) {
                try {
                    save_editor_project();
                    message_ = "Saved. Stage Project stores changed members; Build game export "
                               "creates installable game files.";
                } catch (const std::exception &e) {
                    message_ = e.what();
                }
            }
            ImGui::TextUnformatted(document_->dirty()     ? "Unsaved pickup edits"
                                   : document_->changed() ? "Pickup edits saved in project"
                                                          : "No pickup edits");
            ImGui::EndDisabled();
        }
        if (!message_.empty())
            ImGui::TextWrapped("%s", message_.c_str());
    }
    ImGui::End();
}
}
