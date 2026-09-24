#include "native/undo_shortcuts.h"
#include "native/lighting_table_editor.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace studio {
void LightingTableEditor::draw(MaterialDocument &doc, bool editable) {
    if (!visible_)
        return;
    if (material_ >= doc.model.scene->materials.size()) {
        close();
        return;
    }
    if (revision_ != doc.revision() || model_revision_ != doc.model_revision()) {
        try {
            tables_ = doc.lighting_tables();
            bindings_ = doc.lighting_bindings(material_);
            users_.clear();
            for (std::size_t i = 0; i < doc.model.scene->materials.size(); ++i)
                users_.push_back(doc.lighting_bindings(i));
            auto found = tables_.find(bindings_[channel_].table);
            if (found != tables_.end())
                curve_ = found->second;
            revision_ = doc.revision();
            model_revision_ = doc.model_revision();
            dragging_ = false;

        } catch (const std::exception &e) {
            error_ = e.what();
        }
    }
    ImGui::SetNextWindowSize({680, 660}, ImGuiCond_FirstUseEver);
    if (focus_) {
        ImGui::SetNextWindowFocus();
        focus_ = false;
    }
    bool shown = ImGui::Begin("Material lighting tables", &visible_, ImGuiWindowFlags_NoDocking);
    bool apply_curve = false, apply_bindings = false, undo = false, redo = false;
    if (dragging_ && (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !shown || !visible_ ||
                      !editable || ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        if (!editable || ImGui::IsKeyPressed(ImGuiKey_Escape))
            curve_ = before_;
        else
            apply_curve = true;
        dragging_ = false;
    }
    if (shown && visible_) {
        ImGui::Text("Material: %s", doc.model.scene->materials[material_].name.c_str());
        ImGui::TextWrapped("Reflection channels feed Secondary lighting in the texture combiners.");
        ImGui::BeginDisabled(!editable || dragging_);
        if (ImGui::Combo("Lighting channel", &channel_, "Red\0Green\0Blue\0")) {
            auto found = tables_.find(bindings_[channel_].table);
            if (found != tables_.end())
                curve_ = found->second;
        }
        auto &binding = bindings_[channel_];
        auto label = [&](std::uint32_t hash) {
            if (!hash)
                return std::string("None");
            auto found = tables_.find(hash);
            if (found == tables_.end())
                return std::string("Missing table");
            return "Table " + std::to_string(std::distance(tables_.begin(), found) + 1);
        };
        if (ImGui::BeginCombo("Table", label(binding.table).c_str())) {
            if (ImGui::Selectable("None", !binding.table)) {
                binding.table = 0;
                apply_bindings = true;
            }
            for (auto &[hash, table] : tables_)
                if (ImGui::Selectable(label(hash).c_str(), binding.table == hash)) {
                    binding.table = hash;
                    curve_ = table;
                    apply_bindings = true;
                }
            ImGui::EndCombo();
        }
        const char *inputs[] = {"Normal / half vector",
                                "View / half vector",
                                "Normal / view",
                                "Normal / light",
                                "Spotlight (preview unsupported)",
                                "Tangent / half vector"};
        int input = int(binding.input);
        if (ImGui::Combo("Input angle", &input, inputs, 6)) {
            binding.input = unsigned(input);
            apply_bindings = true;
        }
        apply_bindings |= ImGui::Checkbox("Unsigned input (0 to 1)", &binding.unsigned_range);
        const float scales[] = {.25f, .5f, 1, 2, 4, 8};
        char scale_name[32];
        std::snprintf(scale_name, sizeof(scale_name), "%.2gx", binding.scale);
        if (ImGui::BeginCombo("Scale", scale_name)) {
            for (float scale : scales) {
                std::snprintf(scale_name, sizeof(scale_name), "%.2gx", scale);
                if (ImGui::Selectable(scale_name, binding.scale == scale)) {
                    binding.scale = scale;
                    apply_bindings = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(!doc.can_undo());
        if (studio::UndoShortcuts::button("material_editor", "Undo"))
            undo = true;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!doc.can_redo());
        if (studio::UndoShortcuts::button("material_editor", "Redo"))
            redo = true;
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (tables_.contains(binding.table)) {
            std::string users;
            const char *channels[] = {"R", "G", "B"};
            for (std::size_t i = 0; i < users_.size(); ++i)
                for (unsigned c = 0; c < 3; ++c)
                    if (users_[i][c].table == binding.table) {
                        if (!users.empty())
                            users += ", ";
                        users += doc.model.scene->materials[i].name + " / " + channels[c];
                    }
            ImGui::TextWrapped("Shared curve: %s", users.c_str());
            ImGui::TextDisabled("Curve edits affect every reference above. Binding settings affect "
                                "this channel only.");
            auto raw_index = [&](unsigned displayed) {
                return binding.unsigned_range ? displayed : (displayed + 128) % 256;
            };
            ImGui::BeginDisabled(!editable || dragging_);
            const char *presets[] = {
                "Broad highlight", "Narrow highlight", "Rim", "Linear", "Zero", "One"};
            for (unsigned preset = 0; preset < 6; ++preset) {
                if (preset && preset != 3)
                    ImGui::SameLine();
                if (ImGui::Button(presets[preset])) {
                    for (unsigned i = 0; i < 256; ++i) {
                        float x = binding.unsigned_range ? float(i) / 255 : float(i) / 255 * 2 - 1;
                        float v = std::max(x, 0.f);
                        curve_.values[raw_index(i)] = preset == 0   ? std::pow(v, 4.f)
                                                      : preset == 1 ? std::pow(v, 16.f)
                                                      : preset == 2 ? std::pow(1 - v, 4.f)
                                                      : preset == 3 ? v
                                                      : preset == 4 ? 0.f
                                                                    : 1.f;
                    }
                    apply_curve = true;
                }
            }
            ImGui::EndDisabled();
            auto origin = ImGui::GetCursorScreenPos();
            ImVec2 size{std::max(100.f, ImGui::GetContentRegionAvail().x), 200};
            ImGui::InvisibleButton("##lighting-curve", size);
            bool hovered = ImGui::IsItemHovered();
            auto mouse = ImGui::GetIO().MousePos;
            int index = int(std::clamp((mouse.x - origin.x) / size.x, 0.f, 1.f) * 255);
            float value = std::clamp(1 - (mouse.y - origin.y) / size.y, 0.f, 1.f);
            if (editable && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                dragging_ = true;
                before_ = curve_;
                previous_ = index;
                previous_value_ = value;
            }
            if (dragging_ && editable) {
                int low = std::min(index, previous_), high = std::max(index, previous_);
                for (int i = low; i <= high; ++i) {
                    float t =
                        index == previous_ ? 1.f : float(i - previous_) / float(index - previous_);
                    curve_.values[raw_index(unsigned(i))] =
                        previous_value_ + (value - previous_value_) * t;
                }
                previous_ = index;
                previous_value_ = value;
                sample_ = index;
            }
            auto *draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                                IM_COL32(22, 29, 35, 255));
            for (int i = 0; i <= 4; ++i) {
                float y = origin.y + size.y * i / 4;
                draw->AddLine({origin.x, y}, {origin.x + size.x, y}, IM_COL32(60, 70, 80, 255));
            }
            for (unsigned i = 1; i < 256; ++i)
                draw->AddLine({origin.x + size.x * (i - 1) / 255,
                               origin.y + size.y * (1 - curve_.values[raw_index(i - 1)])},
                              {origin.x + size.x * i / 255,
                               origin.y + size.y * (1 - curve_.values[raw_index(i)])},
                              IM_COL32(100, 230, 220, 255), 2);
            ImGui::TextDisabled(binding.unsigned_range ? "Input 0 to 1 | Output 0 to 1"
                                                       : "Input -1 to 1 | Output 0 to 1");
            ImGui::TextWrapped(
                "Drag to reshape the curve; release to apply. Escape cancels the stroke.");
            ImGui::BeginDisabled(!editable || dragging_);
            ImGui::SliderInt("Sample", &sample_, 0, 255);
            auto &sample = curve_.values[raw_index(unsigned(sample_))];
            ImGui::SliderFloat("Sample value", &sample, 0, 1, "%.4f");
            if (ImGui::IsItemDeactivatedAfterEdit())
                apply_curve = true;
            ImGui::EndDisabled();
        } else
            ImGui::TextWrapped("Choose an existing model table to edit its curve.");
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::TextDisabled(
            "Save Project keeps applied edits; stage/build writes them to the game model.");
    }
    ImGui::End();
    try {
        if (undo) {
            doc.undo();
            return;
        }
        if (redo) {
            doc.redo();
            return;
        }
        if (apply_bindings || apply_curve)
            error_.clear();
        if (apply_bindings)
            doc.edit_lighting_bindings(material_, bindings_);
        if (apply_curve)
            doc.edit_lighting_table(bindings_[channel_].table, curve_);
    } catch (const std::exception &e) {
        error_ = e.what();
        revision_ = ~std::uint64_t(0);
    }
}
}
