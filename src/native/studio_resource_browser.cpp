#include "native/studio_resource_browser.h"
#include "native/imgui_renderer.h"
#include "native/tutorial_widgets.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <utility>
namespace studio {
namespace {
std::string resource_label(const StudioResource &row) {
    auto label = std::string(studio_resource_kind_name(row.kind)) + " / " + row.name;
    if (row.kind == StudioResourceKind::Mesh)
        label += " / part " + std::to_string(row.index + 1);
    return label;
}
std::string lower(std::string s) {
    for (auto &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}
void StudioResourceBrowser::follow(StudioResourceKind kind, int index, int skeleton) {
    StudioResource r;
    r.kind = kind;
    r.index = index;
    r.skeleton = skeleton;
    pending_follow_ = std::move(r);
}
const StudioResource *StudioResourceBrowser::find(const std::string &key) const {
    auto it = std::find_if(rows_.begin(), rows_.end(), [&](auto &r) {
        return r.key == key;
    });
    return it == rows_.end() ? nullptr : &*it;
}
std::optional<StudioResourceRequest>
StudioResourceBrowser::draw(MaterialDocument &doc, const EnvironmentRenderer &renderer,
                            const std::string &status, bool editable) {
    std::optional<StudioResourceRequest> request;
    auto identity = doc.identity();
    if (document_ != &doc || identity_ != identity) {
        document_ = &doc;
        identity_ = identity;
        history_ = {};
        search_[0] = 0;
        rows_.clear();
        changes_list_.clear();
        error_.clear();
        revision_ = texture_revision_ = model_revision_ = changes_revision_ = ~std::uint64_t(0);
    }
    if (model_revision_ != doc.model_revision() || texture_revision_ != doc.texture_revision() ||
        (revision_ != doc.revision() && !ImGui::IsAnyItemActive())) {
        rows_ = studio_resources(doc.model);
        revision_ = doc.revision();
        texture_revision_ = doc.texture_revision();
        model_revision_ = doc.model_revision();
        changes_revision_ = ~std::uint64_t(0);
    }
    if (pending_follow_) {
        for (auto &row : rows_)
            if (row.kind == pending_follow_->kind && row.index == pending_follow_->index &&
                (row.kind != StudioResourceKind::Bone ||
                 row.skeleton == pending_follow_->skeleton)) {
                history_.visit(row.key);
                break;
            }
        pending_follow_.reset();
    }
    if (history_.current().empty() && !rows_.empty())
        history_.visit(rows_.front().key);
    if (!open_)
        return request;
    ImGui::SetNextWindowSize({1000, 680}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Studio resources", &open_)) {
        ImGui::End();
        return request;
    }
    const bool select_tab = std::exchange(focus_tab_, false), requested_changes = changes_;
    ImGui::TextUnformatted(doc.model.name.c_str());
    ImGui::TextWrapped("%s", status.c_str());
    auto navigate = [&](const StudioResource &row, bool visit = true) {
        if (visit)
            history_.visit(row.key);
        request = StudioResourceRequest{row};
    };
    ImGui::BeginDisabled(!history_.can_back());
    if (TutorialWidgets::Button("studio_resources", "Back"))
        if (auto *row = find(history_.back()))
            navigate(*row, false);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!history_.can_forward());
    if (TutorialWidgets::Button("studio_resources", "Forward"))
        if (auto *row = find(history_.forward()))
            navigate(*row, false);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!editable);
    if (TutorialWidgets::Button("studio_resources", "Save edits"))
        request = StudioResourceRequest{{}, StudioResourceRequest::Action::Save};
    ImGui::EndDisabled();
    if (ImGui::BeginTabBar("Resource views")) {
        if (ImGui::BeginTabItem("Browse", nullptr,
                                select_tab && !requested_changes ? ImGuiTabItemFlags_SetSelected
                                                                 : 0)) {
            changes_ = false;
            ImGui::SetNextItemWidth(140);
            ImGui::Combo("##kind", &kind_,
                         "All resources\0Meshes\0Materials\0Textures\0Bones\0Motions\0");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##search", "Search resource names or details", search_,
                                     sizeof(search_));
            auto needle = lower(search_);
            if (ImGui::BeginTable("Resource browser", 2, ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Resources", ImGuiTableColumnFlags_WidthFixed, 380);
                ImGui::TableSetupColumn("Connections", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::BeginChild("Resource list", {0, 0}, ImGuiChildFlags_Borders);
                unsigned matches = 0;
                for (auto &row : rows_) {
                    if (kind_ && int(row.kind) != kind_ - 1)
                        continue;
                    if (!needle.empty() &&
                        lower(row.name + " " + row.detail).find(needle) == std::string::npos)
                        continue;
                    ++matches;
                    ImGui::PushID(row.key.c_str());
                    if (row.kind == StudioResourceKind::Texture) {
                        auto handle = renderer.texture(row.name);
                        if (bgfx::isValid(handle)) {
                            ImGui::Image(ImTextureID(ImGuiRenderer::image_id(handle)), {32, 32});
                            ImGui::SameLine();
                        }
                    }
                    std::string label = resource_label(row);
                    if (ImGui::Selectable(label.c_str(), history_.current() == row.key))
                        navigate(row);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s\n%s", row.name.c_str(), row.detail.c_str());
                    ImGui::PopID();
                }
                if (!matches)
                    ImGui::TextWrapped(
                        "No matching resources. Clear the search or choose another resource type.");
                ImGui::EndChild();
                ImGui::TableNextColumn();
                ImGui::BeginChild("Resource connections", {0, 0}, ImGuiChildFlags_Borders);
                if (auto *row = find(history_.current())) {
                    ImGui::TextWrapped("%s", resource_label(*row).c_str());
                    ImGui::TextWrapped("%s", row->detail.c_str());
                    if (row->kind == StudioResourceKind::Texture) {
                        auto handle = renderer.texture(row->name);
                        auto image = doc.model.scene->textures.find(row->name);
                        if (bgfx::isValid(handle) && image != doc.model.scene->textures.end()) {
                            float scale =
                                std::min(160.f / std::max(1.f, float(image->second.width)),
                                         160.f / std::max(1.f, float(image->second.height)));
                            ImGui::Image(
                                ImTextureID(ImGuiRenderer::image_id(handle)),
                                {image->second.width * scale, image->second.height * scale});
                        }
                    }
                    auto meshes =
                        std::count_if(row->used_by.begin(), row->used_by.end(), [&](auto &key) {
                            auto *r = find(key);
                            return r && r->kind == StudioResourceKind::Mesh;
                        });
                    if (row->kind == StudioResourceKind::Material && meshes > 1)
                        ImGui::TextWrapped(
                            "Shared by %zu mesh parts. Material edits affect all of them.",
                            std::size_t(meshes));
                    auto materials =
                        std::count_if(row->used_by.begin(), row->used_by.end(), [&](auto &key) {
                            auto *r = find(key);
                            return r && r->kind == StudioResourceKind::Material;
                        });
                    if (row->kind == StudioResourceKind::Texture && materials > 1)
                        ImGui::TextWrapped("Shared by %zu materials. Replacing this texture "
                                           "affects every binding.",
                                           std::size_t(materials));
                    bool supported = row->kind != StudioResourceKind::Bone || row->skeleton == 0;
                    ImGui::BeginDisabled(!editable || !supported);
                    if (row->kind == StudioResourceKind::Motion) {
                        auto &m = doc.model.motions.at(row->index);
                        if (!m.skeletal.tracks.empty() &&
                            TutorialWidgets::Button("studio_resources", "Edit bone animation"))
                            request =
                                StudioResourceRequest{*row, StudioResourceRequest::Action::Pose};
                        if (!m.material.tracks.empty() &&
                            TutorialWidgets::Button("studio_resources", "Edit material animation"))
                            request = StudioResourceRequest{
                                *row, StudioResourceRequest::Action::MaterialMotion};
                        if (!m.visibility.tracks.empty() &&
                            TutorialWidgets::Button("studio_resources",
                                                    "Edit visibility animation"))
                            request = StudioResourceRequest{
                                *row, StudioResourceRequest::Action::VisibilityMotion};
                    } else if (TutorialWidgets::Button("studio_resources", "Open editor"))
                        request = StudioResourceRequest{*row, StudioResourceRequest::Action::Edit};
                    if (row->kind == StudioResourceKind::Mesh ||
                        row->kind == StudioResourceKind::Material) {
                        if (TutorialWidgets::Button("studio_resources", "Open UVs"))
                            request =
                                StudioResourceRequest{*row, StudioResourceRequest::Action::Uvs};
                        ImGui::SameLine();
                        if (TutorialWidgets::Button("studio_resources", "Frame selection"))
                            request =
                                StudioResourceRequest{*row, StudioResourceRequest::Action::Frame};
                        ImGui::SameLine();
                        if (TutorialWidgets::Button("studio_resources", "Isolate selection"))
                            request =
                                StudioResourceRequest{*row, StudioResourceRequest::Action::Isolate};
                    }
                    ImGui::EndDisabled();
                    if (!supported)
                        ImGui::TextWrapped(
                            "This bone belongs to an additional skeleton. Its relationships can be "
                            "inspected here; bone editing uses the main skeleton.");
                    auto links = [&](const char *title, const std::vector<std::string> &keys) {
                        ImGui::SeparatorText(title);
                        if (keys.empty())
                            ImGui::TextDisabled("None");
                        ImGui::PushID(title);
                        for (auto &key : keys)
                            if (auto *linked = find(key)) {
                                ImGui::PushID(key.c_str());
                                auto label = resource_label(*linked);
                                if (ImGui::Selectable(label.c_str()))
                                    navigate(*linked);
                                ImGui::PopID();
                            }
                        ImGui::PopID();
                    };
                    links("Uses", row->uses);
                    links("Used by", row->used_by);
                } else
                    ImGui::TextWrapped("This resource no longer exists. Select a resource from the "
                                       "list or use Back.");
                ImGui::EndChild();
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Changes", nullptr,
                                select_tab && requested_changes ? ImGuiTabItemFlags_SetSelected
                                                                : 0)) {
            changes_ = true;
            if (changes_revision_ != doc.revision() && !ImGui::IsAnyItemActive()) {
                try {
                    changes_list_ = doc.resource_changes();
                    error_.clear();
                    changes_revision_ = doc.revision();
                } catch (const std::exception &e) {
                    error_ = e.what();
                }
            }
            ImGui::TextWrapped(
                "Changes are compared with the source opened for this document. Select a resource "
                "to inspect it. Saving keeps these edits without building game files.");
            if (!error_.empty())
                ImGui::TextWrapped("%s", error_.c_str());
            if (changes_list_.empty() && error_.empty())
                ImGui::TextDisabled("No resource changes from the opened source.");
            ImGui::BeginChild("Changed resources", {0, 0}, ImGuiChildFlags_Borders);
            for (auto &change : changes_list_) {
                ImGui::PushID(change.key.c_str());
                auto label = std::string(studio_resource_kind_name(change.kind)) + " / " +
                             change.name + " / " + change.detail;
                if (ImGui::Selectable(label.c_str())) {
                    if (auto *row = find(change.key)) {
                        navigate(*row);
                        request->action = StudioResourceRequest::Action::Edit;
                        changes_ = false;
                        focus_tab_ = true;
                    } else if (change.kind == StudioResourceKind::Other) {
                        StudioResource other;
                        other.key = change.key;
                        other.name = change.name;
                        request = StudioResourceRequest{other, StudioResourceRequest::Action::Edit};
                    }
                }
                if (change.removed && ImGui::IsItemHovered())
                    ImGui::SetTooltip("Removed resources have no editor target. Use Undo to "
                                      "restore the operation.");
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
    return request;
}
}
