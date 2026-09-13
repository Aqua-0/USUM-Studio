#include "native/tutorial_controller.h"
#include "native/tutorial_widgets.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <string_view>
namespace studio {
namespace {
TutorialController *active = nullptr;
std::string lower(std::string value) {
    for (char &c : value)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
}
TutorialController::TutorialController(Preferences &preferences) : preferences_(preferences) {
    try {
        state_.restore(preferences_.get("tutorial_progress"));
    } catch (const std::exception &e) {
        error_ = e.what();
    }
    paused_ = preferences_.get("tutorial_paused") == "1";
    active = this;
    TutorialWidgets::observer = [](const char *module, const char *label, bool used) {
        if (active)
            active->observe(module, label, used);
    };
}
TutorialController::~TutorialController() {
    if (active == this) {
        active = nullptr;
        TutorialWidgets::observer = nullptr;
    }
}
void TutorialController::save() {
    preferences_.set("tutorial_progress", state_.serialize());
    preferences_.set("tutorial_paused", paused_ ? "1" : "0");
    for (const auto &[workspace, topics] : encountered_) {
        std::ostringstream out;
        for (const auto &id : topics)
            out << std::quoted(id) << ' ';
        preferences_.set("tutorial_workspace_" + std::to_string(workspace), out.str());
    }
    if (!preferences_.save())
        error_ = preferences_.error;
    else
        error_.clear();
}
void TutorialController::begin_frame(int workspace, bool suspended) {
    targets_.clear();
    visible_.clear();
    suspended_ = suspended;
    if (workspace_ != workspace) {
        workspace_ = workspace;
        history_.clear();
        if (!encountered_.contains(workspace)) {
            std::istringstream in(
                preferences_.get("tutorial_workspace_" + std::to_string(workspace)));
            auto &topics = encountered_[workspace];
            std::string id;
            while (in >> std::quoted(id))
                topics.insert(id);
        }
        if (state_.unseen(tutorial_workspace(workspace).id))
            state_.current = tutorial_workspace(workspace).id;
    }
    const auto &intro = tutorial_workspace(workspace_);
    visible_.push_back(intro.id);
}
void TutorialController::observe(const char *module, const char *label, bool used) {
    if (suspended_ || !ImGui::IsItemVisible())
        return;
    auto *topic = tutorial_topic(module, label);
    if (!topic)
        return;
    Target target{ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                  (GImGui->LastItemData.ItemFlags & ImGuiItemFlags_Disabled) != 0};
    auto origin = std::string_view(module), caption = std::string_view(label);
    bool navigation =
        origin == "main" && (caption == "Maps" || caption == "Models" || caption == "Studio" ||
                             caption == "Authoring" || caption == "Collision" ||
                             caption == "Cameras" || caption == "Images" || caption == "Audio");
    bool relevant = !(workspace_ == -1 && origin != "project_workspace") && !navigation;
    if (relevant && !targets_.contains(topic->id)) {
        targets_[topic->id] = target;
        visible_.push_back(topic->id);
        encountered_[workspace_].insert(topic->id);
        if (used && state_.enabled && state_.welcomed && !paused_ && state_.unseen(topic->id)) {
            state_.current = topic->id;
            save();
        }
    }
    if ((state_.enabled || inspect_) && state_.welcomed &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal |
                             ImGuiHoveredFlags_NoSharedDelay)) {
        auto last_item = GImGui->LastItemData;
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
        ImGui::TextUnformatted(topic->title.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted(topic->body.c_str());
        if (target.disabled)
            ImGui::TextDisabled(
                "Currently unavailable; check the selection and panel requirements.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
        GImGui->LastItemData = last_item;
    }
}
void TutorialController::menu() {
    if (!ImGui::BeginMenu("Help"))
        return;
    if (ImGui::MenuItem("Tutorials enabled", nullptr, &state_.enabled)) {
        if (state_.enabled)
            paused_ = false;
        save();
    }
    if (ImGui::MenuItem(paused_ ? "Resume walkthrough" : "Pause walkthrough", nullptr, false,
                        state_.enabled)) {
        paused_ = !paused_;
        save();
    }
    if (ImGui::MenuItem("Replay this workspace", nullptr, false, state_.enabled)) {
        const auto &intro = tutorial_workspace(workspace_);
        state_.completed.erase(intro.id);
        for (auto &id : encountered_[workspace_])
            state_.completed.erase(id);
        state_.current = intro.id;
        paused_ = false;
        save();
    }
    if (ImGui::MenuItem("Restart all tutorials"))
        restart_requested_ = true;
    ImGui::Separator();
    ImGui::MenuItem("Control reference...", nullptr, &reference_);
    ImGui::MenuItem("Explain controls on hover", nullptr, &inspect_);
    ImGui::EndMenu();
}
void TutorialController::draw_reference() {
    if (!reference_)
        return;
    ImGui::SetNextWindowSize(ImVec2(720, 480), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Control reference", &reference_)) {
        ImGui::InputTextWithHint("##find-help", "Find a feature or button", search_,
                                 sizeof(search_));
        ImGui::BeginChild("Topics", ImVec2(ImGui::GetContentRegionAvail().x * .42f, 0),
                          ImGuiChildFlags_Borders);
        auto query = lower(search_);
        for (const auto &topic : tutorial_topics()) {
            if (!query.empty() &&
                lower(topic.title + " " + topic.body).find(query) == std::string::npos)
                continue;
            ImGui::PushID(topic.id.c_str());
            if (ImGui::Selectable(topic.title.c_str(), reference_id_ == topic.id))
                reference_id_ = topic.id;
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("Explanation", ImVec2(0, 0));
        ImGui::PushTextWrapPos();
        if (auto *topic = tutorial_topic_by_id(reference_id_)) {
            ImGui::TextUnformatted(topic->title.c_str());
            ImGui::Separator();
            ImGui::TextUnformatted(topic->body.c_str());
            if (targets_.contains(topic->id) && ImGui::Button("Show this control")) {
                state_.enabled = true;
                state_.welcomed = true;
                state_.current = topic->id;
                paused_ = false;
                reference_ = false;
                save();
            }
        } else
            ImGui::TextUnformatted("Choose a topic. These explanations are available offline, even "
                                   "with tutorials turned off.");
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
    }
    ImGui::End();
}
void TutorialController::draw() {
    if (suspended_)
        return;
    if (restart_requested_) {
        ImGui::OpenPopup("Restart tutorials?");
        restart_requested_ = false;
    }
    if (ImGui::BeginPopupModal("Restart tutorials?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Clear tutorial progress? Your project edits are unaffected.");
        if (ImGui::Button("Restart tutorials")) {
            state_.restart();
            paused_ = false;
            history_.clear();
            save();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    // A project progress or error dialog takes priority over instruction.
    if (auto *modal = ImGui::GetTopMostPopupModal();
        modal && std::string(modal->Name) != "Welcome to USUMStudio")
        return;
    if (state_.enabled && !state_.welcomed) {
        ImGui::OpenPopup("Welcome to USUMStudio");
        auto available = ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSize(ImVec2(std::min(520.f, available.x - 24), 0), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(180, 0),
            ImVec2(std::max(180.f, available.x - 24), std::max(120.f, available.y - 24)));
        if (ImGui::BeginPopupModal("Welcome to USUMStudio", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushTextWrapPos();
            ImGui::TextUnformatted("Learn the editor as you use it");
            ImGui::Separator();
            ImGui::TextUnformatted("Start with a project, load a map, then explore the workspace "
                                   "tabs. A walkthrough card explains the controls you encounter "
                                   "for the first time and highlights each one.");
            ImGui::Spacing();
            ImGui::TextUnformatted(
                "Use Next control to continue, or pause whenever you want. Hover over buttons for "
                "quick help. Lessons never press buttons or change project data for you.");
            bool off = false;
            if (ImGui::Checkbox("Turn tutorials off", &off) && off) {
                state_.enabled = false;
                state_.welcomed = true;
                save();
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextDisabled("You can turn them back on from Help.");
            if (ImGui::Button("Start walkthrough")) {
                state_.welcomed = true;
                paused_ = false;
                save();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Later")) {
                state_.welcomed = true;
                paused_ = true;
                save();
                ImGui::CloseCurrentPopup();
            }
            if (!error_.empty())
                ImGui::TextWrapped("%s", error_.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndPopup();
        }
        return;
    }
    draw_reference();
    if (!state_.enabled || paused_)
        return;
    auto chosen = state_.choose(visible_);
    if (chosen.empty())
        return;
    if (state_.current != chosen) {
        state_.current = chosen;
        save();
    }
    auto *topic = tutorial_topic_by_id(chosen);
    if (!topic)
        return;
    auto target = targets_.find(chosen);
    auto *viewport = ImGui::GetMainViewport();
    float width = std::min(ImGui::GetFontSize() * 26, viewport->WorkSize.x - 24);
    bool left = target != targets_.end() &&
                target->second.low.x > viewport->WorkPos.x + viewport->WorkSize.x * .5f;
    ImGui::SetNextWindowPos(
        ImVec2(left ? viewport->WorkPos.x + 12 : viewport->WorkPos.x + viewport->WorkSize.x - 12,
               viewport->WorkPos.y + viewport->WorkSize.y - 12),
        ImGuiCond_Always, ImVec2(left ? 0.f : 1.f, 1));
    ImGui::SetNextWindowSize(ImVec2(width, 0));
    ImGui::SetNextWindowSizeConstraints(ImVec2(180, 0),
                                        ImVec2(std::max(180.f, width), viewport->WorkSize.y - 24));
    if (ImGui::Begin("Walkthrough", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(topic->title.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted(topic->body.c_str());
        if (target != targets_.end()) {
            auto &rect = target->second;
            ImGui::GetForegroundDrawList()->AddRect(rect.low, rect.high,
                                                    IM_COL32(255, 206, 90, 255), 3, 0, 2);
            if (rect.disabled)
                ImGui::TextWrapped("This control is currently unavailable. Check the required "
                                   "selection or finish the operation in progress.");
        }
        ImGui::Spacing();
        auto previous = std::find_if(history_.rbegin(), history_.rend(), [&](const auto &id) {
            return std::find(visible_.begin(), visible_.end(), id) != visible_.end();
        });
        ImGui::BeginDisabled(previous == history_.rend());
        if (ImGui::Button("Back")) {
            state_.current = *previous;
            history_.erase(previous.base() - 1, history_.end());
            state_.completed.erase(state_.current);
            save();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Next control")) {
            history_.push_back(chosen);
            state_.finish(chosen);
            save();
        }
        if (ImGui::GetContentRegionAvail().x > ImGui::CalcTextSize("Skip visible controls").x + 30)
            ImGui::SameLine();
        if (ImGui::Button("Skip visible controls")) {
            for (auto &id : visible_)
                state_.finish(id);
            save();
        }
        if (ImGui::Button("Pause")) {
            paused_ = true;
            save();
        }
        ImGui::SameLine();
        bool off = false;
        if (ImGui::Checkbox("Turn tutorials off", &off) && off) {
            state_.enabled = false;
            save();
        }
        auto remaining = std::count_if(visible_.begin(), visible_.end(), [&](auto &id) {
            return state_.unseen(id);
        });
        ImGui::TextDisabled("%d new topics here. More appear as you explore.", int(remaining));
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}
}
