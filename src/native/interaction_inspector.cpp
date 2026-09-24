#include "native/undo_shortcuts.h"
#include "native/tutorial_widgets.h"
#include "native/interaction_inspector.h"
#include <imgui.h>
#include "formats/container.h"
#include <algorithm>
#include <cstdio>
namespace studio {
void InteractionInspector::refresh_steps() {
    steps_ = interaction_steps(document_->program(), entry_);
    state_actions_ = interaction_state_accesses(document_->program(), steps_);
    auto trace = trace_interaction(document_->program(), script_, event_, {});
    for (auto &access : state_actions_) {
        auto found =
            std::find_if(trace.actions.begin(), trace.actions.end(), [&](const auto &action) {
                return action.address == access.address;
            });
        if (found == trace.actions.end())
            continue;
        for (unsigned i = 0; i < access.arguments.size() && i < found->arguments.size(); ++i)
            if (!access.arguments[i].literal && found->arguments[i]) {
                access.arguments[i].resolved = true;
                access.arguments[i].value = *found->arguments[i];
                access.arguments[i].source = "Computed on the traced path; other callers may differ";
                if (i == 0) {
                    const std::string unknown = "[unresolved]";
                    for (auto *text : {&access.title, &access.condition})
                        for (auto at = text->find(unknown); at != std::string::npos;
                             at = text->find(unknown))
                            text->replace(at, unknown.size(), std::to_string(*found->arguments[i]));
                }
            }
    }
    inspection_.listing = document_->program().listing();
}
void InteractionInspector::draw_steps() {
    if (!document_) {
        ImGui::TextWrapped("No resolved script is loaded.");
        return;
    }
    ImGui::TextWrapped(
        "Calls and control flow are shown in code-address order, not a simulated timeline. "
        "Follow branch/helper targets below. Helper edits affect every caller in the loaded "
        "program.");
    if (ImGui::Button("Go to interaction entry")) {
        auto found = std::find_if(steps_.begin(), steps_.end(), [&](auto &step) {
            return step.address == entry_;
        });
        if (found != steps_.end()) {
            selected_step_ = int(found - steps_.begin());
            scroll_to_step_ = true;
        }
    }
    ImGui::Checkbox("Show argument pushes and register instructions", &instructions_);
    if (!ImGui::BeginTable("Call editor", 2, ImGuiTableFlags_Resizable))
        return;
    ImGui::TableSetupColumn("Flow", ImGuiTableColumnFlags_WidthStretch, 0.48f);
    ImGui::TableSetupColumn("Arguments", ImGuiTableColumnFlags_WidthStretch, 0.52f);
    ImGui::TableNextColumn();
    ImGui::BeginChild("Steps");
    for (unsigned n = 0; n < steps_.size(); ++n) {
        auto &step = steps_[n];
        auto &op = document_->program().instructions.at(step.address).name;
        bool flow = op == "sysreq.n" || op == "call" || op == "proc" || op == "ret" ||
                    op == "retn" || op == "switch" || op == "casetbl" || op.starts_with("j") ||
                    op.starts_with("halt");
        if (!instructions_ && !flow && selected_step_ != int(n))
            continue;
        ImGui::PushID(int(n));
        char address[20];
        std::snprintf(address, sizeof(address), "%06X  ", step.address);
        auto label = std::string(address) + step.label;
        if (ImGui::Selectable(label.c_str(), selected_step_ == int(n)))
            selected_step_ = int(n);
        if (scroll_to_step_ && selected_step_ == int(n)) {
            ImGui::SetScrollHereY();
            scroll_to_step_ = false;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::TableNextColumn();
    ImGui::BeginChild("Call arguments");
    if (selected_step_ >= 0 && std::size_t(selected_step_) < steps_.size()) {
        auto step = steps_[selected_step_];
        ImGui::TextWrapped("%s", step.label.c_str());
        ImGui::TextDisabled("AMX %06X", step.address);
        for (unsigned n = 0; n < step.arguments.size(); ++n) {
            auto argument = step.arguments[n];
            ImGui::PushID(int(n));
            ImGui::Separator();
            ImGui::TextWrapped("%u. %s", n + 1, argument.label.c_str());
            if (argument.literal) {
                int value = argument.value;
                ImGui::BeginDisabled(!project_store() || (source_.shared && !shared_edit_));
                if (ImGui::InputInt("##value", &value, 0, 0,
                                    ImGuiInputTextFlags_EnterReturnsTrue)) {
                    try {
                        document_->set(*argument.literal, value);
                        refresh_steps();
                        error_.clear();
                    } catch (const std::exception &e) {
                        error_ = e.what();
                    }
                }
                ImGui::EndDisabled();
                ImGui::TextDisabled("Literal at %06X / Enter to apply", *argument.literal);
            } else
                ImGui::TextWrapped("%s (read-only)", argument.source.c_str());
            ImGui::PopID();
        }
        if (!step.targets.empty()) {
            ImGui::SeparatorText("Control flow targets");
            for (unsigned n = 0; n < step.targets.size(); ++n) {
                auto target = step.targets[n];
                ImGui::PushID(int(n));
                char label[48];
                std::snprintf(label, sizeof(label), "Follow AMX %06X", target);
                if (ImGui::Button(label)) {
                    auto found = std::find_if(steps_.begin(), steps_.end(), [&](auto &s) {
                        return s.address == target;
                    });
                    if (found != steps_.end()) {
                        selected_step_ = int(found - steps_.begin());
                        scroll_to_step_ = true;
                    }
                }
                ImGui::PopID();
            }
            ImGui::TextWrapped(
                "Conditional jumps also continue to the following instruction when not taken. "
                "Helpers return to the instruction after the call. Backward jumps can form wait "
                "loops.");
        }
        auto &instruction = document_->program().instructions.at(step.address);
        if (instruction.name == "jzer")
            ImGui::TextWrapped(
                "Jump when PRI is zero; otherwise continue to the next instruction.");
        if (instruction.name == "jnz")
            ImGui::TextWrapped(
                "Jump when PRI is nonzero; otherwise continue to the next instruction.");
        if (instruction.name == "jump.pri")
            ImGui::TextWrapped(
                "Runtime-computed jump: its destination cannot be expanded statically.");
        if (instruction.name == "halt" || instruction.name == "halt.p")
            ImGui::TextWrapped(
                "HALT 12 yields the script; execution can resume at the next instruction.");
        if (instruction.name == "sysreq.n")
            ImGui::TextWrapped(
                "Native return value is placed in PRI. Computed arguments and caller parameters "
                "are preserved; enable register instructions to follow their producers.");
    } else
        ImGui::TextWrapped(
            "Select a call to inspect its arguments, or a branch to follow its target. "
            "Literal edits preserve control flow; inserting/reordering calls is not supported "
            "yet.");
    ImGui::EndChild();
    ImGui::EndTable();
}
void InteractionInspector::draw_state_actions() {
    if (!document_) {
        ImGui::TextWrapped("No resolved script is loaded.");
        return;
    }
    ImGui::TextWrapped("Reads and writes reachable from this interaction, including local helpers "
                       "and both branch paths. Code order is not execution order. Other entry "
                       "points and shared-script calls are not searched.");
    if (std::any_of(steps_.begin(), steps_.end(), [](const auto &step) {
            return step.label == "CallTrainerBattleCore";
        }))
        ImGui::TextWrapped("The game's battle system records trainer defeat. Those flag writes "
                           "are not Pawn calls and are not listed here.");
    ImGui::SetNextItemWidth(180);
    ImGui::Combo("State", &state_kind_, "All state\0Flags\0Work variables\0");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    ImGui::Combo("Access", &state_access_, "All accesses\0Reads\0Writes\0");
    ImGui::Checkbox("Filter by ID", &state_filter_);
    ImGui::SameLine();
    ImGui::BeginDisabled(!state_filter_);
    ImGui::SetNextItemWidth(160);
    ImGui::InputInt("ID", &state_id_, 0, 0);
    ImGui::EndDisabled();
    if (ImGui::TreeNode("About these results")) {
        ImGui::TextWrapped("IDs are raw game identifiers; names and current save values are "
                           "unknown. Only direct literal arguments are editable. Enter applies; "
                           "Save Project and Stage Project write the edits.");
        ImGui::TreePop();
    }
    auto accesses = state_actions_;
    ImGui::BeginChild("State actions");
    unsigned shown = 0, unresolved = 0;
    auto follow = [&](unsigned address) {
        auto found = std::find_if(steps_.begin(), steps_.end(), [&](auto &step) {
            return step.address == address;
        });
        if (found != steps_.end()) {
            selected_step_ = int(found - steps_.begin());
            scroll_to_step_ = true;
            focus_calls_ = true;
        }
    };
    for (const auto &access : accesses) {
        if ((state_kind_ == 1 && access.work) || (state_kind_ == 2 && !access.work) ||
            (state_access_ == 1 && access.write) || (state_access_ == 2 && !access.write))
            continue;
        bool known = !access.arguments.empty() &&
                     (access.arguments[0].literal.has_value() || access.arguments[0].resolved);
        if (!known)
            ++unresolved;
        if (state_filter_ && (!known || access.arguments[0].value != state_id_))
            continue;
        ++shown;
        ImGui::PushID(int(access.address));
        char address[24];
        std::snprintf(address, sizeof(address), "%06X  ", access.address);
        if (ImGui::TreeNodeEx("Access", ImGuiTreeNodeFlags_DefaultOpen, "%s%s", address,
                              access.title.c_str())) {
            if (!access.condition.empty())
                ImGui::TextWrapped("%s", access.condition.c_str());
            if (ImGui::SmallButton("Inspect call"))
                follow(access.address);
            if (access.branch) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Follow jump"))
                    follow(*access.branch);
                ImGui::SameLine();
                if (ImGui::SmallButton("Follow otherwise"))
                    follow(*access.continuation);
            }
            if (known) {
                if (ImGui::SmallButton("Find reads")) {
                    state_filter_ = true;
                    state_id_ = access.arguments[0].value;
                    state_kind_ = access.work ? 2 : 1;
                    state_access_ = 1;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Find writes")) {
                    state_filter_ = true;
                    state_id_ = access.arguments[0].value;
                    state_kind_ = access.work ? 2 : 1;
                    state_access_ = 2;
                }
            }
            for (unsigned n = 0; n < access.arguments.size(); ++n) {
                const auto &argument = access.arguments[n];
                ImGui::PushID(int(n));
                if (argument.literal) {
                    int value = argument.value;
                    ImGui::SetNextItemWidth(180);
                    ImGui::BeginDisabled(!project_store() || (source_.shared && !shared_edit_));
                    if (ImGui::InputInt(argument.label.c_str(), &value, 0, 0,
                                        ImGuiInputTextFlags_EnterReturnsTrue)) {
                        try {
                            document_->set(*argument.literal, value);
                            refresh_steps();
                            error_.clear();
                        } catch (const std::exception &e) {
                            error_ = e.what();
                        }
                    }
                    ImGui::EndDisabled();
                } else if (argument.resolved)
                    ImGui::TextWrapped("%s: %d (%s). Read-only.", argument.label.c_str(),
                                       argument.value, argument.source.c_str());
                else
                    ImGui::TextWrapped("%s: unresolved (%s). Read-only.", argument.label.c_str(),
                                       argument.source.c_str());
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (!shown)
        ImGui::TextWrapped(
            "No matching calls were resolved in this interaction and its local helpers.");
    if (state_filter_ && unresolved)
        ImGui::TextWrapped("%u calls have unresolved IDs and cannot be matched to this filter. "
                           "Clear Filter by ID to inspect them.",
                           unresolved);
    ImGui::EndChild();
}
void InteractionInspector::controls(const Environment *scene, const std::filesystem::path &dump,
                                    int area, int selection, bool show_launcher) {
    if (uses_loading_.valid() &&
        uses_loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto uses = uses_loading_.get();
            if (uses_identity_ == identity_) {
                uses_ = std::move(uses);
                uses_scanned_ = true;
            }
        } catch (const std::exception &e) {
            if (uses_identity_ == identity_)
                uses_error_ = e.what();
        }
    }
    auto root = project_store() ? project_store()->root : std::filesystem::path{};
    if (project_root_ != root) {
        project_.unbind();
        document_.reset();
        steps_.clear();
        open_ = false;
        project_root_ = root;
        identity_.clear();
        uses_identity_.clear();
        uses_.clear();
        uses_scanned_ = false;
        uses_error_.clear();
    }
    const SpatialRegion *region =
        scene && selection >= 0 && std::size_t(selection) < scene->spatial.regions.size()
            ? &scene->spatial.regions[selection]
            : nullptr;
    bool available = region && region->overworld && region->kind != SpatialKind::Entrance &&
                     region->overworld->script;
    if (show_launcher && available) {
        ImGui::Begin("Map inspector");
        ImGui::BeginDisabled(!available);
        if (studio::TutorialWidgets::Button("interaction_inspector", "Inspect interaction",
                                            ImVec2(-1, 0))) {
            open_ = true;
            error_.clear();
            if (document_ && document_->dirty()) {
                try {
                    require(save_editor_project(),
                            "Save the current interaction in a project before switching");
                } catch (const std::exception &e) {
                    error_ = e.what();
                    ImGui::EndDisabled();
                    ImGui::End();
                    return;
                }
            }
            project_.unbind();
            document_.reset();
            steps_.clear();
            selected_step_ = -1;
            inspection_ = {};
            source_ = {};
            shared_edit_ = false;
            uses_identity_.clear();
            uses_.clear();
            uses_scanned_ = false;
            uses_error_.clear();
            auto &reference = *region->overworld;
            identity_ = "Zone " + std::to_string(region->zone) + " / event " +
                        std::to_string(reference.event) + " / script " +
                        std::to_string(reference.script);
            try {
                inspection_ = inspect_interaction(dump, scene->archive_sources, unsigned(area),
                                                  reference.local_zone, region->zone,
                                                  reference.script, reference.event);
                auto source = project_store() ? project_store()->source : dump;
                source_ =
                    resolve_interaction_source(source, {}, unsigned(area), reference.local_zone,
                                               region->zone, reference.script);
                document_ = std::make_unique<InteractionDocument>(source_.program);
                auto parameters = source_.shared ? std::to_string(source_.member)
                                                 : std::to_string(area) + " " +
                                                       std::to_string(reference.local_zone);
                project_.bind(
                    source_.shared ? "shared-interaction" : "interaction",
                    source_.shared ? "shared-interaction/" + std::to_string(source_.member)
                                   : "interaction/" + std::to_string(area) + "/" +
                                         std::to_string(reference.local_zone),
                    source_.shared ? "Shared interaction program " + std::to_string(source_.member)
                                   : "Zone interaction calls: area " + std::to_string(area) +
                                         ", slot " + std::to_string(reference.local_zone),
                    parameters,
                    [this] {
                        return document_ && document_->dirty();
                    },
                    [this] {
                        return project_text(document_->serialize());
                    },
                    [this] {
                        document_->mark_saved();
                    });
                if (auto file = project_.document(); !file.empty())
                    document_->restore(text(read_file(file)));
                script_ = reference.script;
                event_ = reference.event;
                entry_ =
                    trace_interaction(document_->program(), reference.script, reference.event, {})
                        .entry;
                refresh_steps();
                auto start = std::find_if(steps_.begin(), steps_.end(), [&](auto &step) {
                    return step.address == entry_;
                });
                selected_step_ = start == steps_.end() ? -1 : int(start - steps_.begin());
                scroll_to_step_ = true;
            } catch (const std::exception &e) {
                error_ = e.what();
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Select an NPC or trigger with a script.");
        ImGui::End();
    }
    if (!open_)
        return;
    ImGui::SetNextWindowSize(ImVec2(740, 620), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Interaction inspector", &open_)) {
        ImGui::TextUnformatted(identity_.c_str());
        ImGui::TextDisabled("Static inspection / scripts are not executed");
        if (source_.shared) {
            ImGui::TextWrapped(
                "Shared program: edits can affect other placements and other entry points. "
                "For NPCs and scenery, Author interaction replaces only that placement.");
            ImGui::Checkbox("Edit shared program", &shared_edit_);
            if (ImGui::CollapsingHeader("Used by / direct placement references")) {
                ImGui::TextWrapped("Includes all entries in this shared program. Calls made by "
                                   "other scripts are not included.");
                ImGui::BeginDisabled(uses_loading_.valid());
                if (ImGui::Button(uses_scanned_ ? "Rescan placements" : "Find placements")) {
                    auto source = project_store() ? project_store()->source : dump;
                    auto archives = scene ? scene->archive_sources : ArchiveSources{};
                    auto member = source_.member;
                    uses_identity_ = identity_;
                    uses_error_.clear();
                    uses_loading_ = std::async(std::launch::async, [source, archives, member] {
                        return shared_script_uses(source, archives, member);
                    });
                }
                ImGui::EndDisabled();
                if (uses_loading_.valid())
                    ImGui::TextDisabled("Scanning field placements...");
                if (!uses_error_.empty())
                    ImGui::TextWrapped("%s", uses_error_.c_str());
                if (uses_scanned_)
                    ImGui::Text("%zu direct references", uses_.size());
                ImGui::BeginChild("Shared program users", ImVec2(0, 130));
                for (const auto &use : uses_)
                    ImGui::Text("%s / zone %d / area %u, slot %u / event %u, row %u / script %u",
                                use.kind.c_str(), use.zone, use.area, use.local_zone, use.event,
                                use.row, use.script);
                ImGui::EndChild();
            }
        }
        if (document_) {
            ImGui::BeginDisabled(!project_store() || (source_.shared && !shared_edit_));
            if (ImGui::Button("Save Project")) {
                try {
                    save_editor_project();
                } catch (const std::exception &e) {
                    error_ = e.what();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (studio::UndoShortcuts::button("interaction_inspector", "Undo") && document_->undo())
                refresh_steps();
            ImGui::SameLine();
            if (studio::UndoShortcuts::button("interaction_inspector", "Redo") && document_->redo())
                refresh_steps();
            ImGui::SameLine();
            ImGui::TextUnformatted(document_->dirty() ? "Unsaved argument edits"
                                                      : "Saved / unchanged");
        }
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        {
            if (ImGui::BeginTabBar("Interaction views")) {
                if (ImGui::BeginTabItem("Calls & flow", nullptr,
                                        focus_calls_ ? ImGuiTabItemFlags_SetSelected : 0)) {
                    focus_calls_ = false;
                    draw_steps();
                    ImGui::EndTabItem();
                }
                if (studio::TutorialWidgets::BeginTabItem("interaction_inspector", "Actions")) {
                    if (ImGui::RadioButton("Flags & work variables", state_view_))
                        state_view_ = true;
                    ImGui::SameLine();
                    if (ImGui::RadioButton("Dialogue summary", !state_view_))
                        state_view_ = false;
                    if (state_view_)
                        draw_state_actions();
                    else {
                        ImGui::TextWrapped("%s", inspection_.notice.c_str());
                        ImGui::TextWrapped(
                            "Source snapshot: reopen after staging to refresh dialogue summaries. "
                            "Dynamic words and formatting are omitted.");
                        studio::TutorialWidgets::Checkbox("interaction_inspector",
                                                          "Show technical calls", &technical_);
                        ImGui::BeginChild("Action sequence", ImVec2(0, 0));
                        if (inspection_.actions.empty())
                            ImGui::TextWrapped(
                                "No reliable action summary is available. Inspect the "
                                "AMX tab for the decoded instructions.");
                        unsigned visible_index = 0;
                        for (unsigned index = 0; index < inspection_.actions.size(); ++index) {
                            auto &action = inspection_.actions[index];
                            if (!technical_ && !action.recognized &&
                                action.title != "Unresolved control flow" &&
                                action.title != "Unresolved helper" &&
                                action.title != "Runtime loop" &&
                                action.title.rfind("Unknown native", 0) != 0 &&
                                action.title.rfind("Call shared script", 0) != 0 &&
                                action.title != "Change message context")
                                continue;
                            ImGui::PushID(int(index));
                            ImGui::Separator();
                            ImGui::TextDisabled("%u.  AMX %06X", ++visible_index, action.address);
                            if (action.recognized) {
                                ImGui::TextUnformatted(action.title.c_str());
                                ImGui::TextWrapped("%s", action.detail.c_str());
                            } else if (ImGui::TreeNode("detail", "%s", action.title.c_str())) {
                                ImGui::TextWrapped("%s", action.detail.c_str());
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndChild();
                    }
                    ImGui::EndTabItem();
                }
                if (studio::TutorialWidgets::BeginTabItem("interaction_inspector", "AMX")) {
                    ImGui::TextWrapped("%s", inspection_.source.c_str());
                    if (studio::TutorialWidgets::Button("interaction_inspector",
                                                        "Copy disassembly"))
                        ImGui::SetClipboardText(inspection_.listing.c_str());
                    ImGui::BeginChild("Disassembly", ImVec2(0, 0), ImGuiChildFlags_None,
                                      ImGuiWindowFlags_HorizontalScrollbar);
                    ImGui::TextUnformatted(inspection_.listing.c_str());
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
    }
    ImGui::End();
}
}
