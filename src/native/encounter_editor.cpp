#include "native/tutorial_widgets.h"
#include "native/encounter_editor.h"
#include "field/collision_surfaces.h"
#include "field/map_catalog.h"
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
void EncounterEditor::set_scene(std::shared_ptr<Environment> scene, unsigned area,
                                const std::filesystem::path &dump) {
    scene_ = std::move(scene);
    area_ = area;
    document_.reset();
    selected_ = -1;
    drag_axis_ = -1;
    blocked_ = false;
    zone_ids_.clear();
    names_.clear();
    message_.clear();
    new_zone_ = new_table_ = 0;
    try {
        Archive field(scene_->archive_sources.resolve(dump, TargetProfile::field_archive));
        document_ = std::make_unique<EncounterDocument>(
            area, scene_->placement_source,
            field.decoded(area * TargetProfile::area_stride + TargetProfile::encounter_table_slot));
        Archive text_archive(dump / TargetProfile::location_text_archive);
        names_ = decode_location_text(text_archive.decoded(TargetProfile::pokemon_names_member));
        Archive zones(dump / TargetProfile::zone_archive);
        auto data = zones.decoded(0);
        for (auto &location : scene_->locations)
            if (location.zone >= 0)
                zone_ids_[u16(data, std::size_t(location.zone) * 84 + 10)] = location.zone;
        binding_.bind(
            "encounters", "encounters/" + std::to_string(area),
            "Wild encounters in area " + std::to_string(area), std::to_string(area),
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
            require(drag_axis_ < 0, "Finish the encounter drag before saving");
        });
        if (auto p = binding_.document(); !p.empty()) {
            document_->restore(text(read_file(p)));
            binding_.restored();
        }
        for (unsigned t = 0; t < document_->table_count(); ++t)
            if (document_->has_table(t)) {
                new_table_ = t;
                break;
            }
        synchronize();
    } catch (const std::exception &e) {
        document_.reset();
        message_ = e.what();
    }
}
void EncounterEditor::ensure_editable() {
    require(project_store() != nullptr, "Open an editor project to edit encounters");
    save_editor_project();
    for (auto &[key, e] : project_store()->edits)
        if ((e.kind == "overworld" || e.kind == "placements" || e.kind == "pickups" ||
             e.kind == "warps") &&
            e.parameters == std::to_string(area_))
            throw std::runtime_error("Stage and reload existing placement edits before editing "
                                     "encounter regions in this area.");
}
void EncounterEditor::refresh_table() {
    if (selected_ >= 0) {
        auto table = document_->regions()[unsigned(selected_)].table;
        if (document_->has_table(table))
            table_draft_ = document_->period(table, night_);
    }
}
void EncounterEditor::select(int index) {
    selected_ = index;
    shape_editable_ = false;
    shape_index_ = 0;
    if (index < 0 || unsigned(index) >= document_->regions().size()) {
        selected_ = -1;
        return;
    }
    region_draft_ = document_->regions()[index];
    new_zone_ = region_draft_.zone;
    new_table_ = region_draft_.table;
    try {
        shape_draft_ = document_->shape(index, 0);
        shape_editable_ = true;
    } catch (const std::exception &) {
    }
    refresh_table();
    unsigned row = 0;
    for (int i = 0; i < index; ++i)
        row += document_->regions()[i].zone == region_draft_.zone;
    for (unsigned i = 0; i < scene_->spatial.regions.size(); ++i) {
        auto &r = scene_->spatial.regions[i];
        if (r.kind == SpatialKind::Encounter && r.encounter_zone == int(region_draft_.zone) &&
            r.encounter_row == int(row))
            renderer_.spatial.selected = int(i);
    }
}
void EncounterEditor::synchronize() {
    if (!document_)
        return;
    SpatialScene next;
    decode_encounter_regions(next, document_->placements(), zone_ids_);
    std::vector<unsigned> slots;
    for (unsigned i = 0; i < scene_->spatial.regions.size(); ++i)
        if (scene_->spatial.regions[i].kind == SpatialKind::Encounter) {
            slots.push_back(i);
            SpatialRegion empty;
            empty.kind = SpatialKind::Encounter;
            scene_->spatial.regions[i] = std::move(empty);
        }
    for (unsigned i = 0; i < next.regions.size(); ++i) {
        if (i < slots.size())
            scene_->spatial.regions[slots[i]] = std::move(next.regions[i]);
        else
            scene_->spatial.regions.push_back(std::move(next.regions[i]));
    }
    renderer_.spatial.invalidate_geometry();
    renderer_.invalidate_selection_readback();
    renderer_.spatial.selected = -1;
    if (selected_ >= 0 && unsigned(selected_) < document_->regions().size()) {
        region_draft_.shapes = document_->regions()[selected_].shapes;
        auto zone = document_->regions()[selected_].zone;
        unsigned row = 0;
        for (int i = 0; i < selected_; ++i)
            row += document_->regions()[i].zone == zone;
        for (unsigned i = 0; i < scene_->spatial.regions.size(); ++i) {
            auto &r = scene_->spatial.regions[i];
            if (r.kind == SpatialKind::Encounter && r.encounter_zone == int(zone) &&
                r.encounter_row == int(row))
                renderer_.spatial.selected = int(i);
        }
    }
}
void EncounterEditor::draw(bool loading, ViewportCamera &camera,
                           const std::function<void()> &stage_reload, const SpatialPoint *cursor,
                           bool show_launcher) {
    loading_ = loading;
    if (show_launcher) {
        ImGui::Begin("Map inspector");
        if (studio::TutorialWidgets::Button("encounter_editor", "Wild encounters", ImVec2(-1, 0))) {
            open_ = true;
            ImGui::SetNextWindowFocus();
            renderer_.spatial.enabled[unsigned(SpatialKind::Encounter)] = true;
            renderer_.spatial.pick_overlays = true;
        }
        ImGui::End();
    }
    if (!open_)
        return;
    ImGui::SetNextWindowSize({720, 820}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Wild encounters", &open_)) {
        if (!document_) {
            ImGui::TextWrapped("Encounter editing unavailable: %s", message_.c_str());
            ImGui::End();
            return;
        }
        auto attempt = [&](auto action) {
            try {
                action();
                message_.clear();
            } catch (const std::exception &e) {
                message_ = e.what();
            }
        };
        auto index = renderer_.spatial.selected;
        if (index >= 0 && unsigned(index) < scene_->spatial.regions.size()) {
            auto &r = scene_->spatial.regions[index];
            if (r.kind == SpatialKind::Encounter && r.encounter_row >= 0) {
                unsigned row = 0;
                for (unsigned i = 0; i < document_->regions().size(); ++i)
                    if (document_->regions()[i].zone == unsigned(r.encounter_zone)) {
                        if (row++ == unsigned(r.encounter_row)) {
                            if (selected_ != int(i))
                                select(int(i));
                            break;
                        }
                    }
            }
        }
        ImGui::BeginDisabled(drag_axis_ >= 0);
        ImGui::TextWrapped("An encounter needs a region, an allowed ground collision type, and a "
                           "populated local table. Grass textures alone do not enable encounters.");
        ImGui::InputTextWithHint("##encounter-search", "Search zone or table", search_,
                                 sizeof(search_));
        auto query = lower(search_);
        ImGui::BeginChild("Encounter regions", {0, 145}, ImGuiChildFlags_Borders);
        std::map<unsigned, unsigned> rows;
        for (unsigned i = 0; i < document_->regions().size(); ++i) {
            auto &r = document_->regions()[i];
            auto label = "Zone slot " + std::to_string(r.zone) + " / region " +
                         std::to_string(rows[r.zone]++) + " / table " + std::to_string(r.table);
            if (!query.empty() && lower(label).find(query) == std::string::npos)
                continue;
            ImGui::PushID(int(i));
            if (ImGui::Selectable(label.c_str(), selected_ == int(i)))
                select(int(i));
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (studio::TutorialWidgets::Button("encounter_editor", "Show encounter regions")) {
            renderer_.spatial.enabled[unsigned(SpatialKind::Encounter)] = true;
            renderer_.spatial.pick_overlays = true;
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("encounter_editor", "Show ground types")) {
            renderer_.spatial.enabled[unsigned(SpatialKind::Ground)] = true;
            renderer_.spatial.attribute_colors = true;
        }
        ImGui::BeginDisabled(loading || !project_store());
        auto table_combo = [&](const char *label, unsigned &value) {
            bool changed = false;
            if (ImGui::BeginCombo(label, document_->has_table(value) ? std::to_string(value).c_str()
                                                                     : "No populated table")) {
                for (unsigned i = 0; i < document_->table_count(); ++i)
                    if (document_->has_table(i)) {
                        auto text = "Table " + std::to_string(i) + " / " +
                                    std::to_string(document_->table_users(i)) + " regions";
                        if (ImGui::Selectable(text.c_str(), i == value)) {
                            value = i;
                            changed = true;
                        }
                    }
                ImGui::EndCombo();
            }
            return changed;
        };
        if (studio::TutorialWidgets::CollapsingHeader("encounter_editor", "Add encounter region")) {
            if (ImGui::BeginCombo("Local zone", std::to_string(new_zone_).c_str())) {
                for (unsigned z = 0; z < document_->zone_count(); ++z)
                    if (ImGui::Selectable(std::to_string(z).c_str(), z == new_zone_))
                        new_zone_ = z;
                ImGui::EndCombo();
            }
            table_combo("Initial table", new_table_);
            ImGui::BeginDisabled(!document_->has_table(new_table_));
            auto add = [&](unsigned type) {
                ensure_editable();
                EncounterShape shape;
                shape.type = type;
                shape.position = cursor ? *cursor : camera.target;
                auto added = document_->add_region(new_zone_, new_table_, shape);
                synchronize();
                select(int(added));
            };
            if (studio::TutorialWidgets::Button(
                    "encounter_editor", cursor ? "New box at cursor" : "New box at view center"))
                attempt([&] {
                    add(1);
                });
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("encounter_editor",
                                                cursor ? "New cylinder at cursor"
                                                       : "New cylinder at view center"))
                attempt([&] {
                    add(0);
                });
            ImGui::EndDisabled();
            if (!document_->has_table(new_table_))
                ImGui::TextWrapped("Choose a map template with existing encounters.");
        }
        if (selected_ >= 0) {
            if (studio::TutorialWidgets::Button("encounter_editor", "Frame region")) {
                auto selected = renderer_.spatial.selected;
                if (selected >= 0) {
                    auto &r = scene_->spatial.regions[selected];
                    SpatialPoint low{INFINITY, INFINITY, INFINITY},
                        high{-INFINITY, -INFINITY, -INFINITY};
                    for (auto &v : r.vertices)
                        for (unsigned k = 0; k < 3; ++k) {
                            low[k] = std::min(low[k], v.position[k]);
                            high[k] = std::max(high[k], v.position[k]);
                        }
                    if (!r.vertices.empty())
                        camera.fit(low, high);
                }
            }
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("encounter_editor", "Duplicate region"))
                attempt([&] {
                    ensure_editable();
                    auto added = document_->duplicate(unsigned(selected_));
                    synchronize();
                    select(int(added));
                });
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("encounter_editor", "Delete region"))
                attempt([&] {
                    ensure_editable();
                    document_->erase(unsigned(selected_));
                    synchronize();
                    select(-1);
                });
        }
        if (selected_ >= 0 && ImGui::BeginTabBar("Encounter properties")) {
            if (studio::TutorialWidgets::BeginTabItem("encounter_editor", "Region")) {
                if (studio::TutorialWidgets::CollapsingHeader("encounter_editor", "Region shape",
                                                              ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto count = document_->shape_count(unsigned(selected_));
                    if (count > 1 && ImGui::SliderInt("Shape", &shape_index_, 0, int(count) - 1)) {
                        try {
                            shape_draft_ =
                                document_->shape(unsigned(selected_), unsigned(shape_index_));
                            shape_editable_ = true;
                        } catch (...) {
                            shape_editable_ = false;
                        }
                    }
                    if (shape_editable_) {
                        ImGui::TextUnformatted(shape_draft_.type
                                                   ? "Box (position is bottom center)"
                                                   : "Cylinder (position is bottom center)");
                        if (studio::TutorialWidgets::RadioButton("encounter_editor", "Move (G)",
                                                                 mode_ == 0))
                            mode_ = 0;
                        ImGui::SameLine();
                        ImGui::BeginDisabled(!shape_draft_.type);
                        if (studio::TutorialWidgets::RadioButton("encounter_editor", "Rotate (R)",
                                                                 mode_ == 1))
                            mode_ = 1;
                        ImGui::EndDisabled();
                        ImGui::SameLine();
                        if (studio::TutorialWidgets::RadioButton("encounter_editor", "Scale (S)",
                                                                 mode_ == 2))
                            mode_ = 2;
                        ImGui::TextDisabled("Ctrl: snap | Esc: cancel");
                        bool apply = false;
                        if (cursor && studio::TutorialWidgets::Button("encounter_editor",
                                                                      "Move to 3D cursor")) {
                            shape_draft_.position = *cursor;
                            apply = true;
                        }
                        ImGui::InputFloat3("Position", shape_draft_.position.data());
                        apply |= ImGui::IsItemDeactivatedAfterEdit();
                        if (shape_draft_.type) {
                            ImGui::InputFloat3("Width / height / depth", shape_draft_.size.data());
                            apply |= ImGui::IsItemDeactivatedAfterEdit();
                        } else {
                            float radius = shape_draft_.size[0] * .5f;
                            if (ImGui::InputFloat("Radius", &radius))
                                shape_draft_.size[0] = radius * 2;
                            apply |= ImGui::IsItemDeactivatedAfterEdit();
                            ImGui::InputFloat("Height", &shape_draft_.size[1]);
                            apply |= ImGui::IsItemDeactivatedAfterEdit();
                            shape_draft_.size[2] = shape_draft_.size[0];
                        }
                        if (apply)
                            attempt([&] {
                                ensure_editable();
                                document_->set_shape(unsigned(selected_), unsigned(shape_index_),
                                                     shape_draft_);
                                synchronize();
                            });
                    } else
                        ImGui::TextWrapped("This shape is displayed and preserved; editing "
                                           "supports boxes and cylinders.");
                }
                if (studio::TutorialWidgets::CollapsingHeader("encounter_editor",
                                                              "Ground types and table",
                                                              ImGuiTreeNodeFlags_DefaultOpen)) {
                    table_combo("Encounter table", region_draft_.table);
                    ImGui::TextWrapped(
                        "Allowed ground collision types (unknown IDs are retained):");
                    if (ImGui::BeginTable("Allowed ground types", 3)) {
                        for (unsigned attr = 0; attr < 128; ++attr) {
                            auto mask = 1u << (attr % 32);
                            bool on = (region_draft_.attributes[attr / 32] & mask) != 0;
                            if (attr >= collision_surfaces.size() && !on)
                                continue;
                            ImGui::TableNextColumn();
                            auto label = collision_surface_name(attr);
                            ImGui::PushID(int(attr));
                            if (studio::TutorialWidgets::Checkbox("encounter_editor", label.c_str(),
                                                                  &on)) {
                                if (on)
                                    region_draft_.attributes[attr / 32] |= mask;
                                else
                                    region_draft_.attributes[attr / 32] &= ~mask;
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                    if (studio::TutorialWidgets::Button("encounter_editor",
                                                        "Apply ground types and table"))
                        attempt([&] {
                            ensure_editable();
                            auto selected = selected_;
                            document_->set_region(unsigned(selected), region_draft_);
                            synchronize();
                            select(selected);
                        });
                    ImGui::TextWrapped(
                        "Edit the ground itself in the Collision workspace. Region overlap order "
                        "and live game checks can also affect encounters.");
                }
                ImGui::EndTabItem();
            }
            if (studio::TutorialWidgets::BeginTabItem("encounter_editor", "Pokemon")) {
                auto table = document_->regions()[unsigned(selected_)].table;
                if (studio::TutorialWidgets::CollapsingHeader("encounter_editor", "Pokemon table",
                                                              ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Text("Table %u is used by %u regions in this area.", table,
                                document_->table_users(table));
                    if (studio::TutorialWidgets::Button("encounter_editor", "Make table unique"))
                        attempt([&] {
                            ensure_editable();
                            document_->make_table_unique(unsigned(selected_));
                            auto selected = selected_;
                            synchronize();
                            select(selected);
                        });
                    ImGui::SameLine();
                    if (studio::TutorialWidgets::Checkbox("encounter_editor", "Night table",
                                                          &night_))
                        refresh_table();
                    ImGui::TextWrapped(
                        "Apply updates every region sharing this table. Make unique first for an "
                        "independent patch. Form 30/31 are native special codes; ordinary form "
                        "availability is not validated here.");
                    number("Minimum level", table_draft_.minimum);
                    number("Maximum level", table_draft_.maximum);
                    if (ImGui::BeginTable("Pokemon slots", 4,
                                          ImGuiTableFlags_Borders |
                                              ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 35);
                        ImGui::TableSetupColumn("Pokemon");
                        ImGui::TableSetupColumn("Form", ImGuiTableColumnFlags_WidthFixed, 65);
                        ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthFixed, 65);
                        ImGui::TableHeadersRow();
                        for (unsigned i = 0; i < 10; ++i) {
                            auto &slot = table_draft_.slots[i];
                            ImGui::PushID(int(i));
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%u", i + 1);
                            ImGui::TableNextColumn();
                            ImGui::SetNextItemWidth(-1);
                            auto label = slot.species < names_.size() ? names_[slot.species]
                                                                      : "Unknown species";
                            if (ImGui::BeginCombo("##species", label.c_str())) {
                                for (unsigned species = 0; species < names_.size(); ++species)
                                    if (!names_[species].empty() &&
                                        ImGui::Selectable(names_[species].c_str(),
                                                          species == slot.species))
                                        slot.species = species;
                                ImGui::EndCombo();
                            }
                            ImGui::TableNextColumn();
                            ImGui::SetNextItemWidth(-1);
                            number("##form", slot.form);
                            ImGui::TableNextColumn();
                            ImGui::SetNextItemWidth(-1);
                            number("##weight", slot.weight);
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                    unsigned total = 0;
                    for (auto &slot : table_draft_.slots)
                        total += slot.weight;
                    ImGui::Text("Total weight: %u / 100", total);
                    if (studio::TutorialWidgets::Button("encounter_editor", "Apply Pokemon table"))
                        attempt([&] {
                            ensure_editable();
                            document_->set_period(table, night_, table_draft_,
                                                  unsigned(names_.size()));
                            refresh_table();
                        });
                    ImGui::TextWrapped(
                        "Day and night are edited separately. Encounter-rate pattern, SOS entries "
                        "and other native settings are preserved.");
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::Separator();
        ImGui::TextDisabled("Shapes apply immediately. Apply table changes before saving.");
        ImGui::BeginDisabled(!document_->can_undo());
        if (studio::TutorialWidgets::Button("encounter_editor", "Undo")) {
            document_->undo();
            auto selected = selected_;
            synchronize();
            select(selected);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!document_->can_redo());
        if (studio::TutorialWidgets::Button("encounter_editor", "Redo")) {
            document_->redo();
            auto selected = selected_;
            synchronize();
            select(selected);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("encounter_editor", "Save Project"))
            attempt([&] {
                save_editor_project();
            });
        ImGui::SameLine();
        ImGui::BeginDisabled(!document_->changed());
        if (studio::TutorialWidgets::Button("encounter_editor", "Stage and reload"))
            attempt(stage_reload);
        ImGui::EndDisabled();
        ImGui::TextUnformatted(document_->dirty()     ? "Unsaved encounter edits"
                               : document_->changed() ? "Encounter edits saved in project"
                                                      : "No encounter edits");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (!message_.empty())
            ImGui::TextWrapped("%s", message_.c_str());
    }
    ImGui::End();
}
bool EncounterEditor::gizmo(const float *view, const float *projection, ImVec2 origin, ImVec2 size,
                            bool hovered) {
    auto &io = ImGui::GetIO();
    auto cancel = [&] {
        document_->cancel_preview();
        drag_axis_ = -1;
        blocked_ = true;
        shape_draft_ = document_->shape(unsigned(selected_), unsigned(shape_index_));
        synchronize();
    };
    if (!document_)
        return false;
    if (!open_ || loading_ || renderer_.player.active ||
        !renderer_.spatial.enabled[unsigned(SpatialKind::Encounter)] ||
        renderer_.spatial.locked[unsigned(SpatialKind::Encounter)]) {
        if (drag_axis_ >= 0)
            cancel();
        return false;
    }
    if (blocked_) {
        blocked_ = ImGui::IsMouseDown(0);
        return true;
    }
    if (drag_axis_ < 0 &&
        (renderer_.spatial.selected < 0 ||
         scene_->spatial.regions[renderer_.spatial.selected].kind != SpatialKind::Encounter))
        return false;
    if (selected_ < 0 || !shape_editable_ || !project_store() ||
        !renderer_.spatial.enabled[unsigned(SpatialKind::Encounter)])
        return false;
    auto shape = document_->shape(unsigned(selected_), unsigned(shape_index_));
    if (!shape.type && mode_ == 1)
        mode_ = 0;
    auto project = [&](SpatialPoint p, ImVec2 &out) {
        float a[4]{}, b[4]{};
        for (unsigned i = 0; i < 4; ++i)
            a[i] = view[i] * p[0] + view[4 + i] * p[1] + view[8 + i] * p[2] + view[12 + i];
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned j = 0; j < 4; ++j)
                b[i] += projection[j * 4 + i] * a[j];
        if (b[3] <= .001f)
            return false;
        out = {origin.x + (b[0] / b[3] * .5f + .5f) * size.x,
               origin.y + (.5f - b[1] / b[3] * .5f) * size.y};
        return true;
    };
    auto rotate = [&](SpatialPoint p) {
        auto q = shape.rotation;
        SpatialPoint t{2 * (q[1] * p[2] - q[2] * p[1]), 2 * (q[2] * p[0] - q[0] * p[2]),
                       2 * (q[0] * p[1] - q[1] * p[0])};
        return SpatialPoint{p[0] + q[3] * t[0] + q[1] * t[2] - q[2] * t[1],
                            p[1] + q[3] * t[1] + q[2] * t[0] - q[0] * t[2],
                            p[2] + q[3] * t[2] + q[0] * t[1] - q[1] * t[0]};
    };
    auto center_world = shape.position;
    ImVec2 center;
    if (!project(center_world, center)) {
        if (drag_axis_ >= 0)
            cancel();
        return false;
    }
    float depth = -(view[2] * center_world[0] + view[6] * center_world[1] +
                    view[10] * center_world[2] + view[14]);
    float length = drag_axis_ >= 0 ? handle_length_ : std::clamp(depth * .12f, 15.f, 2000.f);
    auto distance = [](ImVec2 p, ImVec2 a, ImVec2 b) {
        float x = b.x - a.x, y = b.y - a.y,
              t = std::clamp(((p.x - a.x) * x + (p.y - a.y) * y) / std::max(x * x + y * y, .001f),
                             0.f, 1.f);
        return std::hypot(p.x - a.x - t * x, p.y - a.y - t * y);
    };
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    const ImU32 colors[]{IM_COL32(255, 95, 85, 255), IM_COL32(110, 240, 130, 255),
                         IM_COL32(100, 160, 255, 255)};
    ImVec2 directions[3]{};
    int hot = -1;
    float nearest = 10;
    if (mode_ != 1) {
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (mode_ == 2 && !shape.type && axis == 2)
                continue;
            SpatialPoint direction{};
            direction[axis] = length;
            if (mode_ == 2 && shape.type)
                direction = rotate(direction);
            auto end = center_world;
            for (unsigned k = 0; k < 3; ++k)
                end[k] += direction[k];
            ImVec2 projected;
            if (!project(end, projected))
                continue;
            directions[axis] = {projected.x - center.x, projected.y - center.y};
            if (std::hypot(directions[axis].x, directions[axis].y) < 12)
                continue;
            auto handle = projected;
            if (mode_ == 2) {
                auto face = center_world;
                float extent = shape.size[axis] * (axis == 1 ? 1.f : .5f);
                SpatialPoint lift{0, axis == 1 ? 0.f : shape.size[1] * .5f, 0};
                if (shape.type)
                    lift = rotate(lift);
                for (unsigned k = 0; k < 3; ++k)
                    face[k] += direction[k] * extent / length + lift[k];
                if (!project(face, handle))
                    continue;
            }
            float d = mode_ == 2 ? std::hypot(io.MousePos.x - handle.x, io.MousePos.y - handle.y)
                                 : distance(io.MousePos, center, handle);
            if (d < nearest) {
                nearest = d;
                hot = int(axis);
            }
            auto color = drag_axis_ == int(axis) ? IM_COL32(255, 225, 110, 255) : colors[axis];
            draw->AddLine(center, handle, color, 2);
            if (mode_ == 2)
                draw->AddRectFilled({handle.x - 6, handle.y - 6}, {handle.x + 6, handle.y + 6},
                                    color);
            else {
                float n = std::hypot(directions[axis].x, directions[axis].y),
                      x = directions[axis].x / n, y = directions[axis].y / n;
                draw->AddTriangleFilled(
                    handle, {handle.x - 12 * x + 5 * y, handle.y - 12 * y - 5 * x},
                    {handle.x - 12 * x - 5 * y, handle.y - 12 * y + 5 * x}, color);
            }
            draw->AddText({handle.x + 8, handle.y - 8}, color,
                          mode_ == 2  ? (axis == 0   ? (shape.type ? "Width" : "Radius")
                                         : axis == 1 ? "Height"
                                                     : "Depth")
                          : axis == 0 ? "X"
                          : axis == 1 ? "Y"
                                      : "Z");
        }
    } else {
        for (unsigned k = 0; k < 64; ++k) {
            float a = k * 6.283185307f / 64, b = (k + 1) * 6.283185307f / 64;
            auto p = center_world, q = p;
            p[0] += std::cos(a) * length;
            p[2] += std::sin(a) * length;
            q[0] += std::cos(b) * length;
            q[2] += std::sin(b) * length;
            ImVec2 x, y;
            if (project(p, x) && project(q, y)) {
                float d = distance(io.MousePos, x, y);
                if (d < nearest) {
                    nearest = d;
                    hot = 3;
                }
                draw->AddLine(x, y, colors[1], 3);
            }
        }
    }
    draw->AddCircleFilled(center, 4, IM_COL32(255, 255, 255, 255));
    draw->PopClipRect();
    if (hovered && drag_axis_ < 0 && !io.WantTextInput && !io.KeyCtrl &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (ImGui::IsKeyPressed(ImGuiKey_G))
            mode_ = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_R) && shape.type)
            mode_ = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_S))
            mode_ = 2;
    }
    auto ring_angle = [&] {
        ImVec2 c, x, z;
        auto p = drag_axis_ >= 0 ? drag_start_.position : shape.position, q = p, r = p;
        q[0] += length;
        r[2] += length;
        if (!project(p, c) || !project(q, x) || !project(r, z))
            return 0.f;
        float ax = x.x - c.x, ay = x.y - c.y, bx = z.x - c.x, by = z.y - c.y,
              dx = io.MousePos.x - c.x, dy = io.MousePos.y - c.y, det = ax * by - ay * bx;
        return std::abs(det) < .01f
                   ? 0.f
                   : std::atan2((ax * dy - ay * dx) / det, (dx * by - dy * bx) / det);
    };
    bool available = hovered && hot >= 0 && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt;
    if (drag_axis_ < 0 && available && ImGui::IsMouseClicked(0)) {
        try {
            ensure_editable();
            drag_start_ = shape;
            mouse_start_ = io.MousePos;
            handle_length_ = length;
            angle_start_ = ring_angle();
            drag_axis_ = hot;
            if (hot < 3)
                axis_screen_ = directions[hot];
        } catch (const std::exception &e) {
            message_ = e.what();
            blocked_ = true;
            return true;
        }
    }
    bool captured = drag_axis_ >= 0 || available;
    if (drag_axis_ >= 0) {
        try {
            auto next = drag_start_;
            if (drag_axis_ < 3) {
                float square = axis_screen_.x * axis_screen_.x + axis_screen_.y * axis_screen_.y;
                float delta = ((io.MousePos.x - mouse_start_.x) * axis_screen_.x +
                               (io.MousePos.y - mouse_start_.y) * axis_screen_.y) *
                              handle_length_ / std::max(square, .01f);
                if (io.KeyCtrl)
                    delta = std::round(delta);
                if (mode_ == 0)
                    next.position[drag_axis_] += delta;
                else {
                    next.size[drag_axis_] = std::max(
                        .1f, next.size[drag_axis_] + delta * (drag_axis_ == 1 ? 1.f : 2.f));
                    if (!next.type)
                        next.size[2] = next.size[0];
                }
            } else {
                float angle = std::remainder(ring_angle() - angle_start_, 6.283185307f);
                if (io.KeyCtrl)
                    angle = std::round(angle / .261799388f) * .261799388f;
                auto q = next.rotation;
                float s = std::sin(angle * .5f), c = std::cos(angle * .5f);
                next.rotation = {c * q[0] + s * q[2], c * q[1] + s * q[3], c * q[2] - s * q[0],
                                 c * q[3] - s * q[1]};
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || io.AppFocusLost) {
                cancel();
                shape_draft_ = document_->shape(unsigned(selected_), unsigned(shape_index_));
            } else {
                document_->preview_shape(unsigned(selected_), unsigned(shape_index_), next);
                shape_draft_ = next;
                synchronize();
                if (!ImGui::IsMouseDown(0)) {
                    document_->commit_preview();
                    drag_axis_ = -1;
                }
            }

        } catch (const std::exception &e) {
            cancel();
            message_ = e.what();
        }
    }
    return captured;
}

}
