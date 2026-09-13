#pragma once
#include "native/folder_picker.h"
#include "assets/pokemon_catalog.h"
#include <imgui.h>
namespace studio {
class ArchivePicker {
  public:
    bool poll(std::filesystem::path &selected, const char *role) {
        std::string path;
        {
            std::lock_guard lock(dialog_->mutex);
            if (!dialog_->ready)
                return false;
            dialog_->ready = false;
            error_ = dialog_->error;
            path = dialog_->path;
        }
        if (path.empty())
            return false;
        try {
            auto candidate =
                std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal();
            Archive archive(candidate);
            if (std::string_view(role) == TargetProfile::field_archive)
                require(archive.size() > 0 && archive.size() % TargetProfile::area_stride == 0,
                        "Expected a complete field archive");
            if (std::string_view(role) == TargetProfile::pokemon_archive)
                decode_pokemon_catalog(archive.decoded(0), archive.size(), {});
            selected = std::move(candidate);
            return true;
        } catch (const std::exception &e) {
            error_ = "Archive was not selected: " + std::string(e.what());
            return false;
        }
    }
    bool draw(SDL_Window *window, const char *label, std::filesystem::path &selected,
              const std::filesystem::path &fallback) {
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        bool pending;
        {
            std::lock_guard lock(dialog_->mutex);
            pending = dialog_->pending;
        }
        bool changed = false;
        ImGui::BeginDisabled(pending);
        if (ImGui::Button("Choose GARC...")) {
            error_.clear();
            auto initial = selected.empty() ? fallback : selected;
            choose_archive(window, dialog_, initial.string().c_str());
        }
        if (!selected.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Use dump")) {
                selected.clear();
                error_.clear();
                changed = true;
            }
        }
        ImGui::EndDisabled();
        if (selected.empty())
            ImGui::TextDisabled("Using dump archive");
        else
            ImGui::TextWrapped("External: %s", selected.filename().string().c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", (selected.empty() ? fallback : selected).string().c_str());
        if (pending)
            ImGui::TextDisabled("Choosing archive...");
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::PopID();
        return changed;
    }

  private:
    std::shared_ptr<FolderSelection> dialog_ = std::make_shared<FolderSelection>();
    std::string error_;
};
}
