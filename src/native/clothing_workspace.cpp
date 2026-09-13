#include "native/tutorial_widgets.h"
#include "native/model_workspace.h"
#include "native/theme.h"
#include <algorithm>
#include <cctype>
namespace studio {
void ModelWorkspace::clothing_leave_dialog() {
    if (clothing_leave_pending_) {
        ImGui::OpenPopup("Unsaved clothing palettes");
        clothing_leave_pending_ = false;
    }
    if (ImGui::BeginPopupModal("Unsaved clothing palettes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Clothing palette edits have not been exported.");
        if (studio::TutorialWidgets::Button("clothing_workspace", "Return to palettes")) {
            clothing_leave_action_ = {};
            category_ = 7;
            for (unsigned i = 0; i < 4; ++i)
                if (clothing_palettes_[i].dirty()) {
                    clothing_profile_ = int(i);
                    break;
                }
            refresh_clothing();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("clothing_workspace", "Discard and continue")) {
            for (unsigned i = 0; i < 4; ++i) {
                auto &palette = clothing_palettes_[i];
                palette.current = palette.saved;
                palette.commit();
                clothing_profiles_[i].palette = palette.current;
            }
            auto action = std::move(clothing_leave_action_);
            ImGui::CloseCurrentPopup();
            if (action)
                request_leave(std::move(action));
        }
        ImGui::EndPopup();
    }
}
void ModelWorkspace::refresh_clothing() {
    if (job_.valid() || catalog_job_.valid() || library_job_.valid() || clothing_job_.valid())
        return;
    try {
        auto &palette = clothing_palettes_[clothing_profile_];
        auto &selection = clothing_profiles_[clothing_profile_];
        auto source = selection.color_archive.empty()
                          ? dump_ / ClothingProfile::colors[selection.profile]
                          : selection.color_archive;
        if (palette.original.empty() || std::filesystem::weakly_canonical(palette.source) !=
                                            std::filesystem::weakly_canonical(source)) {
            require(!palette.dirty(),
                    "Export or discard palette changes before loading a different dump");
            palette.load(source);
            bind_palette(unsigned(clothing_profile_));
        }
        selection.palette = palette.current;
    } catch (const std::exception &e) {
        error_ = e.what();
        return;
    }
    cancel_ = false;
    clothing_catalog_ = {};
    error_.clear();
    status_ = "Reading clothing items...";
    auto selection = clothing_profiles_[clothing_profile_];
    auto dump = dump_;
    clothing_job_ = std::async(std::launch::async, [this, dump, selection] {
        return load_clothing_catalog(dump, selection, &cancel_);
    });
}
void ModelWorkspace::open_clothing(const ClothingSelection &selection, int isolated_part,
                                   const std::filesystem::path &source) {
    if (job_.valid())
        return;
    auto dump = source.empty() ? dump_ : source;
    settings_editor_.request_leave([this, selection, isolated_part, dump] {
        cancel_ = false;
        error_.clear();
        status_ = "Assembling clothing preview...";
        job_ = std::async(std::launch::async, [this, dump, selection, isolated_part] {
            return load_clothing(dump, selection, isolated_part, &cancel_);
        });
    });
}
void ModelWorkspace::browse_clothing(bool busy) {
    {
        std::lock_guard lock(clothing_dialog_->mutex);
        if (clothing_dialog_->ready) {
            clothing_dialog_->ready = false;
            int slot = clothing_dialog_slot_;
            clothing_dialog_slot_ = -1;
            if (!clothing_dialog_->error.empty())
                error_ = clothing_dialog_->error;
            else if (!clothing_dialog_->path.empty() && slot >= 0) {
                try {
                    auto path = std::filesystem::u8path(clothing_dialog_->path);
                    auto &palette = clothing_palettes_[clothing_profile_];
                    auto &selection = clothing_profiles_[clothing_profile_];
                    if (slot == 12) {
                        ClothingPalette next;
                        next.load(path);
                        palette = std::move(next);
                        selection.color_archive = path;
                        refresh_clothing();
                        busy = true;
                    } else if (slot == 13) {
                        auto output = path / ClothingProfile::colors[selection.profile];
                        require(std::filesystem::weakly_canonical(output) !=
                                    std::filesystem::weakly_canonical(
                                        dump_ / ClothingProfile::colors[selection.profile]),
                                "Choose an output folder outside the original dump");
                        palette.commit();
                        palette.export_archive(output);
                        palette.saved = palette.current;
                        status_ = "Saved palette GARC: " + output.string();
                    } else {
                        selection.archives[slot] = path;
                        refresh_clothing();
                        busy = true;
                    }
                } catch (const std::exception &e) {
                    error_ = e.what();
                }
            }
        }
    }
    bool changed = false;
    ImGui::BeginDisabled(busy || clothing_dialog_slot_ >= 0);
    ImGui::SetNextItemWidth(-1);
    static const char *profiles[] = {"Overworld / male", "Overworld / female", "Battle / male",
                                     "Battle / female"};
    if (ImGui::Combo("##clothing-profile", &clothing_profile_, profiles, 4)) {
        search_[0] = 0;
        refresh_clothing();
        busy = true;
    }
    ImGui::EndDisabled();
    auto &selection = clothing_profiles_[clothing_profile_];
    ImGui::BeginDisabled(busy || clothing_dialog_slot_ >= 0);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##clothing-part", &clothing_slot_, ClothingProfile::names.data(),
                     int(ClothingProfile::parts))) {
        search_[0] = 0;
        changed = !clothing_assembled_ && selection.items[clothing_slot_] >= 0;
    }
    if (studio::TutorialWidgets::Checkbox("clothing_workspace", "Assembled outfit",
                                          &clothing_assembled_))
        changed = clothing_assembled_ || selection.items[clothing_slot_] >= 0;
    ImGui::BeginDisabled(!clothing_assembled_ && selection.items[clothing_slot_] < 0);
    if (primary_button(clothing_assembled_ ? "Preview outfit" : "Preview part"))
        changed = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("clothing_workspace", "Reset outfit")) {
        selection.items = ClothingProfile::defaults[selection.profile % 2];
        selection.skin = 1;
        selection.hair = 0;
        selection.eyes = 4;
        selection.lip = 0;
        changed = true;
    }
    if (studio::TutorialWidgets::CollapsingHeader("clothing_workspace", "Appearance colors")) {
        auto color_picker = [&](const char *label, unsigned type, unsigned &index) {
            auto &colors = clothing_catalog_.colors[type];
            ImGui::SetNextItemWidth(145);
            auto current = std::to_string(index);
            if (ImGui::BeginCombo(label, current.c_str())) {
                for (unsigned i = 0; i < colors.size(); ++i) {
                    ImGui::PushID(int(i));
                    auto c = colors[i];
                    ImGui::ColorButton("##swatch", {c[0], c[1], c[2], 1},
                                       ImGuiColorEditFlags_NoTooltip, {16, 16});
                    ImGui::SameLine();
                    if (ImGui::Selectable(std::to_string(i).c_str(), index == i)) {
                        index = i;
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        };
        color_picker("Skin", 0, selection.skin);
        color_picker("Hair", 2, selection.hair);
        color_picker("Eyes", 3, selection.eyes);
        if (selection.profile % 2) {
            color_picker("Lip", 5, selection.lip);
            ImGui::TextDisabled("Lip 0 uses the skin color.");
        }
    }
    if (studio::TutorialWidgets::CollapsingHeader("clothing_workspace", "Edit color palettes")) {
        auto &palette = clothing_palettes_[clothing_profile_];
        ImGui::TextWrapped("Edit this body's shared colors. Export before closing the app. "
                           "Clothing items choose their colors from these tables.");
        ImGui::TextUnformatted(palette.dirty() ? "Palette has unsaved changes"
                                               : "Palette saved / unchanged");
        ImGui::BeginDisabled(palette.original.empty());
        static const char *sections[] = {"Skin", "Clothing",         "Hair", "Eyes", "Eyebrows",
                                         "Lips", "Extended clothing"};
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##palette-section", &clothing_palette_section_, sections, 7);
        bool palette_changed = false;
        ImGui::BeginDisabled(palette.cursor == 0);
        if (studio::TutorialWidgets::Button("clothing_workspace", "Undo##palette")) {
            palette.undo();
            palette_changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(palette.cursor + 1 >= palette.history.size());
        if (studio::TutorialWidgets::Button("clothing_workspace", "Redo##palette")) {
            palette.redo();
            palette_changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("clothing_workspace", "Reset palette")) {
            palette.reset();
            palette_changed = true;
        }
        auto colors =
            palette.current.empty() ? ClothingColors{} : decode_clothing_palette(palette.current);
        ImGui::BeginChild("Palette entries", {0, 190}, ImGuiChildFlags_Borders);
        for (unsigned i = 0; i < colors[clothing_palette_section_].size(); ++i) {
            ImGui::PushID(int(i));
            auto rgb = colors[clothing_palette_section_][i];
            ImGui::SetNextItemWidth(-45);
            if (ImGui::ColorEdit3(std::to_string(i).c_str(), rgb.data(),
                                  ImGuiColorEditFlags_Uint8)) {
                palette.current = replace_clothing_color(
                    palette.current, unsigned(clothing_palette_section_), i, rgb);
                clothing_catalog_.colors = decode_clothing_palette(palette.current);
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                palette.commit();
                palette_changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (!ImGui::IsAnyItemActive() && selection.palette != palette.current) {
            palette.commit();
            palette_changed = true;
        }
        if (palette_changed) {
            selection.palette = palette.current;
            clothing_catalog_.colors = decode_clothing_palette(palette.current);
            changed = true;
        }
        if (primary_button(project_store() ? "Save palette to project"
                                           : "Export palette GARC...")) {
            if (project_store()) {
                try {
                    save_editor_project();
                    status_ = "Palette saved to project";
                } catch (const std::exception &e) {
                    error_ = e.what();
                }
            } else {
                palette.commit();
                clothing_dialog_slot_ = 13;
                choose_folder(window_, clothing_dialog_, nullptr);
            }
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(palette.dirty());
        if (!project_store() &&
            studio::TutorialWidgets::Button("clothing_workspace", "Open palette GARC...")) {
            clothing_dialog_slot_ = 12;
            choose_archive(window_, clothing_dialog_, palette.source.string().c_str());
        }
        if (!project_store() && !selection.color_archive.empty() &&
            studio::TutorialWidgets::Button("clothing_workspace", "Use dump palette")) {
            try {
                ClothingPalette next;
                next.load(dump_ / ClothingProfile::colors[selection.profile]);
                palette = std::move(next);
                selection.color_archive.clear();
                refresh_clothing();
                busy = true;
            } catch (const std::exception &e) {
                error_ = e.what();
            }
        }
        ImGui::EndDisabled();
        if (palette.dirty()) {
            if (studio::TutorialWidgets::Button("clothing_workspace",
                                                "Discard unsaved palette changes")) {
                palette.current = palette.saved;
                palette.commit();
                selection.palette = palette.current;
                clothing_catalog_.colors = decode_clothing_palette(palette.current);
                changed = true;
            }
            ImGui::TextWrapped("Export or discard changes before replacing the palette source.");
        }
        ImGui::TextWrapped("%s", palette.source.string().c_str());
    }
    if (studio::TutorialWidgets::CollapsingHeader("clothing_workspace", "Clothing source")) {
        auto path = clothing_archive(dump_, selection, unsigned(clothing_slot_));
        ImGui::TextWrapped("%s", path.empty() ? "This body has no archive for this part."
                                              : path.string().c_str());
        if (!project_store() &&
            studio::TutorialWidgets::Button("clothing_workspace", "External GARC...")) {
            clothing_dialog_slot_ = clothing_slot_;
            choose_archive(window_, clothing_dialog_, path.string().c_str());
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("clothing_workspace", "Refresh items")) {
            refresh_clothing();
            busy = true;
        }
        if (!project_store() && !selection.archives[clothing_slot_].empty() &&
            studio::TutorialWidgets::Button("clothing_workspace", "Use dump archive")) {
            selection.archives[clothing_slot_].clear();
            refresh_clothing();
            busy = true;
        }
    }
    if (selection.items[10] >= 0 && clothing_assembled_)
        ImGui::TextWrapped(
            "Special outfit replaces normal clothes. Remove it to show the regular outfit.");
    if (clothing_slot_ != 0 && selection.items[clothing_slot_] >= 0 &&
        studio::TutorialWidgets::Button("clothing_workspace", "Remove selected part")) {
        selection.items[clothing_slot_] = -1;
        changed = clothing_assembled_;
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s", status_.c_str());
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    for (auto &error : clothing_catalog_.diagnostics)
        ImGui::TextWrapped("%s", error.c_str());
    if (job_.valid() || clothing_job_.valid())
        if (studio::TutorialWidgets::Button("clothing_workspace", "Cancel load"))
            cancel_ = true;
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##clothing-search", "Search clothing resource or item", search_,
                             sizeof(search_));
    std::string filter = search_;
    std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    auto &items = clothing_catalog_.parts[clothing_slot_];
    ImGui::TextDisabled("%zu wearable items", items.size());
    ImGui::BeginChild("Clothing items", {0, 0}, ImGuiChildFlags_Borders);
    ImGui::BeginDisabled(busy || clothing_dialog_slot_ >= 0);
    for (auto &item : items) {
        auto label = item.name;
        std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) {
            return char(std::tolower(c));
        });
        if (!filter.empty() && label.find(filter) == std::string::npos)
            continue;
        ImGui::PushID(int(item.item));
        if (ImGui::Selectable(item.name.c_str(),
                              selection.items[clothing_slot_] == int(item.item))) {
            selection.items[clothing_slot_] = int(item.item);
            changed = true;
        }
        ImGui::PopID();
    }
    if (items.empty() && !busy)
        ImGui::TextWrapped("No wearable items available for this part.");
    ImGui::EndDisabled();
    ImGui::EndChild();
    if (changed && !busy)
        open_clothing(selection, clothing_assembled_ ? -1 : clothing_slot_);
}
void ModelWorkspace::bind_palette(unsigned profile) {
    auto &binding = palette_projects_.at(profile);
    binding.bind(
        "palette", "palette/" + std::to_string(profile),
        "Clothing colors / profile " + std::to_string(profile), std::to_string(profile),
        [this, profile] {
            return clothing_palettes_[profile].dirty();
        },
        [this, profile] {
            return clothing_palettes_[profile].current;
        },
        [this, profile] {
            clothing_palettes_[profile].saved = clothing_palettes_[profile].current;
        });
    if (auto file = binding.document(); !file.empty()) {
        auto &palette = clothing_palettes_[profile];
        palette.current = read_file(file);
        decode_clothing_palette(palette.current);
        palette.commit();
        palette.saved = palette.current;
    }
}
}
