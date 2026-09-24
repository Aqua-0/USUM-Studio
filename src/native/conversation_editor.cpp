#include "native/tutorial_widgets.h"
#include "native/conversation_editor.h"
#include "native/undo_shortcuts.h"
#include "field/dialogue_text.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <iomanip>
#include <functional>
#include <sstream>
namespace studio {
namespace {
bool input(const char *label, std::string &value, bool multiline = false, ImVec2 size = {0, 0},
           bool readonly = false) {
    auto callback = [](ImGuiInputTextCallbackData *data) {
        auto *s = static_cast<std::string *>(data->UserData);
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
            s->resize(std::size_t(data->BufTextLen));
            data->Buf = s->data();
        }
        return 0;
    };
    auto flags = ImGuiInputTextFlags_CallbackResize | (readonly ? ImGuiInputTextFlags_ReadOnly : 0);
    if (multiline)
        return ImGui::InputTextMultiline(label, value.data(), value.capacity() + 1, size,
                                         flags | ImGuiInputTextFlags_AllowTabInput, callback,
                                         &value);
    return ImGui::InputText(label, value.data(), value.capacity() + 1, flags, callback, &value);
}
const char *name(ConversationAction action) {
    static const char *names[] = {"Begin conversation",
                                  "Show message",
                                  "Ask Yes / No",
                                  "Play sound",
                                  "Wait",
                                  "Repeat",
                                  "End conversation",
                                  "Face player",
                                  "Rotate character",
                                  "Play motion",
                                  "Move character (raw)",
                                  "Wait for action",
                                  "Wait for motion",
                                  "Set flag",
                                  "Set work value",
                                  "If flag",
                                  "If work value",
                                  "Move to destination",
                                  "Give item",
                                  "Start encounter",
                                  "If battle result",
                                  "Start trainer battle",
                                  "Ask multiple choice",
                                  "Option"};
    return names[int(action)];
}
std::string target_name(const ConversationActor &target) {
    if (target.kind == InteractionTargetKind::Trainer)
        return "Trainer event " + std::to_string(target.event);
    if (target.kind == InteractionTargetKind::PositionTrigger)
        return "Position trigger row " + std::to_string(target.event);
    return std::string(target.kind == InteractionTargetKind::Scenery ? "Scenery event " : "NPC ") +
           std::to_string(target.event);
}
const char *branch_name(ConversationAction action, bool other) {
    if (action == ConversationAction::Menu)
        return other ? "Cancel" : "Options";
    if (action == ConversationAction::Choice)
        return other ? "No / Cancel" : "Yes";
    if (action == ConversationAction::GiveItem)
        return other ? "Not given" : "Given";
    if (action == ConversationAction::TrainerBattle)
        return other ? "Defeated / before recovery" : "Won";
    if (action == ConversationAction::Encounter)
        return other ? "Defeated / before recovery" : "Returned";
    return other ? "False" : "True";
}
struct Location {
    std::vector<ConversationStep> *siblings = nullptr;
    std::size_t index = 0;
};
Location locate(std::vector<ConversationStep> &nodes, unsigned id) {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].id == id)
            return {&nodes, i};
        auto found = locate(nodes[i].children, id);
        if (found.siblings)
            return found;
        found = locate(nodes[i].otherwise, id);
        if (found.siblings)
            return found;
    }
    return {};
}
unsigned next_id(const std::vector<ConversationStep> &nodes) {
    unsigned next = 1;
    for (const auto &n : nodes)
        next = std::max({next, n.id + 1, next_id(n.children), next_id(n.otherwise)});
    return next;
}
void flow(const std::vector<ConversationStep> &nodes, unsigned &selected,
          const ConversationDraft &draft) {
    for (const auto &n : nodes) {
        ImGui::PushID(int(n.id));
        std::string title = name(n.action);
        if (!n.message.empty()) {
            auto m = std::find_if(draft.messages.begin(), draft.messages.end(), [&](const auto &v) {
                return v.symbol == n.message;
            });
            if (m != draft.messages.end())
                title += "  —  " + m->text.substr(0, 55);
        }
        if (n.action == ConversationAction::SetFlag || n.action == ConversationAction::IfFlag)
            title += " " + std::to_string(n.state_id) + (n.value ? " on" : " off");
        if (n.action == ConversationAction::SetWork || n.action == ConversationAction::IfWork)
            title +=
                " " + std::to_string(n.state_id) + " " +
                (n.action == ConversationAction::SetWork ? "="
                                                         : conversation_comparison(n.comparison)) +
                " " + std::to_string(n.value);
        if (ImGui::Selectable(title.c_str(), selected == n.id))
            selected = n.id;
        if (conversation_branch(n.action) || n.action == ConversationAction::Repeat ||
            n.action == ConversationAction::MenuOption) {
            ImGui::Indent();
            if (n.action != ConversationAction::MenuOption)
                ImGui::TextDisabled("%s", n.action == ConversationAction::Repeat
                                              ? "Repeated steps"
                                              : branch_name(n.action, false));
            flow(n.children, selected, draft);
            if (conversation_branch(n.action)) {
                ImGui::TextDisabled("%s", branch_name(n.action, true));
                flow(n.otherwise, selected, draft);
            }
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
}
}
void ConversationEditor::select_actor(ConversationActor actor) {
    auto candidate = workspace_->interactions().contains(actor)
                         ? nullptr
                         : std::make_unique<AuthoredInteraction>(workspace_->preview(actor));
    actor_ = actor;
    candidate_ = std::move(candidate);
    actor_snapshot_ = std::make_unique<ConversationWorkspace>(*workspace_);
    selected_ = 2;
    preview_open_ = false;
    diagnostics_.clear();
    output_.clear();
    sound_picker_.reset();
    scene_tools_.reset();
}
void ConversationEditor::load(unsigned area, ConversationActor actor) {
    sound_picker_.reset();
    scene_tools_.reset();
    auto *store = project_store();
    require(store, "Open a project to author interactions");
    if (workspace_ && area_ == area) {
        if (actor != actor_)
            select_actor(actor);
        return;
    }
    require(save_editor_project(), "Save the current project before changing areas");
    binding_.unbind();
    workspace_.reset();
    candidate_.reset();
    actor_snapshot_.reset();
    area_ = area;
    actor_ = actor;
    auto key = area == ConversationWorkspace::trainer_workspace
                   ? std::string("trainer-interactions")
                   : "authored-conversations/" + std::to_string(area);
    auto found = store->edits.find(key);
    std::string parameters;
    std::filesystem::path source = store->source;
    if (found != store->edits.end()) {
        parameters = found->second.parameters;
        std::istringstream args(parameters);
        unsigned saved_area;
        std::string baseline;
        require(bool(args >> saved_area >> std::quoted(baseline)) && saved_area == area,
                "Invalid saved conversation area");
        source = store->source_for(baseline);
    } else {
        std::ostringstream out;
        out << area << ' ' << std::quoted(store->base);
        parameters = out.str();
    }
    workspace_ = std::make_unique<ConversationWorkspace>(source, area);
    binding_.bind(
        "authored-conversations", key,
        area == ConversationWorkspace::trainer_workspace
            ? "Authored trainer interactions"
            : "Authored interactions: area " + std::to_string(area),
        parameters,
        [this] {
            return workspace_ && workspace_->dirty();
        },
        [this] {
            return project_text(workspace_->serialize());
        },
        [this] {
            workspace_->mark_saved();
        });
    binding_.ready([this] {
        require(!compilation_.valid(),
                "Wait for conversation compilation to finish before saving or staging");
    });
    binding_.autosave_when([this] {
        return !compilation_.valid();
    });
    if (auto document = binding_.document(); !document.empty())
        workspace_->restore(text(read_file(document)));
    select_actor(actor);
    open_ = true;
}
void ConversationEditor::compiler_settings(Preferences &preferences) {
    if (compiler_.empty())
        compiler_ = preferences.get("pawn.compiler");
    if (sdk_.empty())
        sdk_ = preferences.get("pawn.sdk");
    auto base = std::filesystem::path(SDL_GetBasePath());
    if (sdk_.empty())
        sdk_ = (base / "resources/pawn").string();
    if (compiler_.empty()) {
#ifdef _WIN32
        compiler_ = (base / "resources/pawn/gf-pawncc.exe").string();
#else
        compiler_ = (base / "resources/pawn/gf-pawncc").string();
#endif
    }
}
void ConversationEditor::compile_all(Preferences &preferences) {
    if (!project_store() || compilation_.valid())
        return;
    compiler_settings(preferences);
    compile_status_open_ = true;
    error_.clear();
    try {
        discard_compilation_ = false;
        require(save_editor_project(), "Save the project before compiling conversations");
        auto snapshot = workspace_ ? std::make_unique<ConversationWorkspace>(*workspace_) : nullptr;
        std::vector<std::pair<ProjectEdit, ConversationWorkspace>> other_areas;
        for (const auto &[key, edit] : project_store()->edits) {
            if (edit.kind != "authored-conversations" || (workspace_ && key == binding_.key()))
                continue;
            unsigned area;
            std::string baseline;
            std::istringstream args(edit.parameters);
            require(bool(args >> area >> std::quoted(baseline)), "Invalid conversation source");
            ConversationWorkspace saved(project_store()->source_for(baseline), area);
            saved.restore(text(read_file(project_store()->document(key))));
            if (!saved.compiled())
                other_areas.emplace_back(edit, std::move(saved));
        }
        PawnCompilerSettings settings{std::filesystem::path(compiler_), std::filesystem::path(sdk_),
                                      project_store()->root / "scratch"};
        compilation_ = std::async(std::launch::async, [snapshot = std::move(snapshot),
                                                       other_areas = std::move(other_areas),
                                                       settings]() mutable {
            CompileOutcome outcome;
            if (snapshot)
                for (const auto &[actor, document] : snapshot->interactions()) {
                    outcome.actor = actor;
                    try {
                        auto compiled = snapshot->compile(actor, settings);
                        outcome.diagnostics = std::move(compiled.diagnostics);
                        outcome.output = std::move(compiled.output);
                        if (!compiled.succeeded()) {
                            outcome.error = "Compilation failed for " + target_name(actor);
                            return outcome;
                        }
                    } catch (const std::exception &e) {
                        outcome.error = e.what();
                        return outcome;
                    }
                }
            for (auto &[edit, saved] : other_areas) {
                try {
                    saved.compile_all(settings);
                    outcome.other_areas.emplace_back(edit, saved.serialize());
                } catch (const std::exception &e) {
                    outcome.error = edit.label + ": " + e.what();
                    return outcome;
                }
            }
            outcome.workspace = std::move(snapshot);
            outcome.succeeded = true;
            return outcome;
        });
        output_ = "Compiling...";
    } catch (const std::exception &e) {
        error_ = e.what();
    }
}
void ConversationEditor::global_controls(Preferences &preferences) {
    auto *store = project_store();
    auto root = store ? store->root : std::filesystem::path{};
    auto source = store ? store->source : std::filesystem::path{};
    if (root != project_root_ || source != session_source_) {
        if (compilation_.valid())
            discard_compilation_ = true;
        diagnostics_.clear();
        output_.clear();
        error_.clear();
        sound_picker_.reset();
        scene_tools_.reset();
        binding_.unbind();
        workspace_.reset();
        candidate_.reset();
        actor_snapshot_.reset();
        open_ = false;
        preview_open_ = false;
        project_root_ = root;
        session_source_ = source;
    }
    if (compilation_.valid() &&
        compilation_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto result = compilation_.get();
            if (!discard_compilation_) {
                diagnostics_ = std::move(result.diagnostics);
                diagnostic_actor_ = result.actor;
                output_ = std::move(result.output);
                error_ = std::move(result.error);
                if (error_.empty() && result.succeeded) {
                    auto *store = project_store();
                    for (const auto &[edit, record] : result.other_areas) {
                        auto found = store->edits.find(edit.key);
                        require(found != store->edits.end() && found->second == edit,
                                "Conversations changed during compilation; compile again");
                    }
                    for (const auto &[edit, record] : result.other_areas)
                        store->capture(edit, project_text(record));
                    if (result.workspace)
                        workspace_ = std::move(result.workspace);
                    output_ =
                        "All conversations compiled and link-checked. Save and Stage Project to "
                        "apply them.";
                }
            }
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    }

    compilation_guard_.ready([this] {
        require(!compilation_.valid(), "Wait for conversation compilation to finish");
    });
    compilation_guard_.autosave_when([this] {
        return !compilation_.valid();
    });
    const auto &io = ImGui::GetIO();
    if (store && io.KeyCtrl && io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_C, false))
        compile_all(preferences);
    if (compile_status_open_) {
        ImGui::SetNextWindowSize({480, 140}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Conversation compilation", &compile_status_open_,
                         ImGuiWindowFlags_AlwaysAutoResize)) {
            if (compilation_.valid()) {
                ImGui::TextUnformatted("Compiling project conversations...");
                ImGui::ProgressBar(-float(ImGui::GetTime()), {420, 0});
            } else {
                ImGui::TextWrapped("%s", error_.empty() ? output_.c_str() : error_.c_str());
                if (!error_.empty() && workspace_ && ImGui::Button("Open conversation editor"))
                    open_ = true;
                if (ImGui::Button("Close"))
                    compile_status_open_ = false;
            }
        }
        ImGui::End();
    }
}
void ConversationEditor::menu(Preferences &preferences) {
    if (TutorialWidgets::MenuItem("conversation_editor", "Compile conversations", "Ctrl+Shift+C",
                                  false, project_store() && !compilation_.valid()))
        compile_all(preferences);
}
void ConversationEditor::controls(std::shared_ptr<Environment> scene, int area, int selection,
                                  bool launcher, Preferences &preferences,
                                  EnvironmentRenderer &renderer, MapCursor &cursor) {
    auto *store = project_store();
    auto source = store ? store->source : std::filesystem::path{};
    auto region = scene && selection >= 0 && std::size_t(selection) < scene->spatial.regions.size()
                      ? &scene->spatial.regions[selection]
                      : nullptr;
    if (launcher && region && region->overworld &&
        (region->overworld->category == TargetProfile::trainer_placement_pack ||
         region->overworld->category == TargetProfile::character_placement_pack ||
         region->overworld->category == TargetProfile::interaction_placement_pack ||
         region->overworld->category == TargetProfile::position_event_pack)) {
        ImGui::Begin("Map inspector");
        ImGui::BeginDisabled(!store || compilation_.valid() ||
                             (!region->overworld->script &&
                              region->overworld->category != TargetProfile::position_event_pack));
        if (ImGui::Button("Author interaction...", ImVec2(-1, 0))) {
            try {
                const bool trainer =
                    region->overworld->category == TargetProfile::trainer_placement_pack;
                load(trainer ? ConversationWorkspace::trainer_workspace : unsigned(area),
                     {trainer ? unsigned(region->zone) : region->overworld->local_zone,
                      region->overworld->category == TargetProfile::position_event_pack
                          ? region->overworld->row
                          : region->overworld->event,
                      trainer ? InteractionTargetKind::Trainer
                      : region->overworld->category == TargetProfile::position_event_pack
                          ? InteractionTargetKind::PositionTrigger
                      : region->overworld->category == TargetProfile::interaction_placement_pack
                          ? InteractionTargetKind::Scenery
                          : InteractionTargetKind::Npc});
                open_ = true;
                error_.clear();
            } catch (const std::exception &e) {
                error_ = e.what();
                open_ = true;
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (!store)
                ImGui::SetTooltip("Open a project to author an interaction.");
            else if (compilation_.valid())
                ImGui::SetTooltip("Wait for conversation compilation to finish.");
            else if (!region->overworld->script)
                ImGui::SetTooltip("Select a placement with an interaction script.");
        }
        ImGui::End();
    }
    auto context_area = area_;
    auto context_zone = actor_.zone;
    if (workspace_ && actor_.kind == InteractionTargetKind::Trainer &&
        (candidate_ || workspace_->interactions().contains(actor_))) {
        const auto &target =
            candidate_ ? candidate_->target() : workspace_->interactions().at(actor_).target();
        context_area = target.area;
        context_zone = target.local_zone;
    }
    scene_tools_.context(area >= 0 && unsigned(area) == context_area ? scene : nullptr, renderer,
                         cursor, source, context_area, context_zone, actor_.event,
                         actor_.kind == InteractionTargetKind::Scenery ||
                             actor_.kind == InteractionTargetKind::PositionTrigger);
    if (open_)
        draw(preferences);
    if (preview_open_)
        preview();
}
void ConversationEditor::messages() {
    auto &document = workspace_->open(actor_);
    auto draft = document.draft();
    bool changed = false;
    if (ImGui::CollapsingHeader("Languages and messages")) {
        ImGui::TextWrapped("Select languages to export. Blank translations use English. "
                           "Unchecked languages are not exported; their text is retained here.");
        if (ImGui::BeginTable("Languages", 2)) {
            for (unsigned language = 0; language < GameProfile::dialogue_languages.size(); ++language) {
                ImGui::TableNextColumn();
                bool enabled = draft.languages.contains(language);
                ImGui::BeginDisabled(language == GameProfile::dialogue_fallback_language ||
                                     !workspace_->language_available(language));
                if (ImGui::Checkbox(GameProfile::dialogue_languages[language].name, &enabled)) {
                    if (enabled) draft.languages.insert(language);
                    else draft.languages.erase(language);
                    changed = true;
                }
                ImGui::EndDisabled();
            }
            ImGui::EndTable();
        }
        ImGui::TextWrapped("Conversations sharing a message table use the combined language selection. "
                           "Other conversations in that table use their English fallback.");
    }
    if (!draft.languages.contains(message_language_))
        message_language_ = GameProfile::dialogue_fallback_language;
    if (ImGui::BeginCombo("Text language", GameProfile::dialogue_languages[message_language_].name)) {
        for (auto language : draft.languages)
            if (ImGui::Selectable(GameProfile::dialogue_languages[language].name,
                                  message_language_ == language))
                message_language_ = language;
        ImGui::EndCombo();
    }
    if (ImGui::TreeNode("All messages")) {
        ImGui::BeginChild("Message translations", ImVec2(0, 230), true);
        for (auto &message : draft.messages) {
            ImGui::PushID(message.symbol.c_str());
            ImGui::TextUnformatted(message.symbol.c_str());
            auto &value = message_language_ == GameProfile::dialogue_fallback_language
                              ? message.text : message.translations[message_language_];
            changed |= input("##text", value, true, ImVec2(-1, 65));
            if (message_language_ != GameProfile::dialogue_fallback_language) {
                ImGui::TextWrapped("English: %s", message.text.c_str());
                if (value.empty()) ImGui::TextDisabled("Using English fallback");
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::TreePop();
    }
    if (changed) {
        try {
            document.update(std::move(draft));
            error_.clear();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    }
}
void ConversationEditor::visual() {
    auto &document = workspace_->open(actor_);
    auto draft = document.draft();
    bool changed = false;
    if (draft.custom)
        ImGui::TextWrapped("Custom Pawn is active for compilation. Visual steps are retained "
                           "separately; restore generated Pawn to build these steps.");
    if (ImGui::BeginTable("Conversation layout", 2, ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Flow", ImGuiTableColumnFlags_WidthStretch, 0.52f);
        ImGui::TableSetupColumn("Step", ImGuiTableColumnFlags_WidthStretch, 0.48f);
        ImGui::TableNextColumn();
        ImGui::BeginChild("Flow", ImVec2(0, 420));
        flow(draft.steps, selected_, draft);
        ImGui::EndChild();
        ImGui::TableNextColumn();
        ImGui::BeginChild("Step details", ImVec2(0, 420));
        auto found = locate(draft.steps, selected_);
        if (found.siblings) {
            auto &s = found.siblings->at(found.index);
            ImGui::SeparatorText(name(s.action));
            auto message = [&](const char *label, const std::string &symbol, bool rich = false) {
                auto m =
                    std::find_if(draft.messages.begin(), draft.messages.end(), [&](const auto &v) {
                        return v.symbol == symbol;
                    });
                if (m != draft.messages.end()) {
                    ImGui::TextUnformatted(label);
                    auto &value = message_language_ == GameProfile::dialogue_fallback_language
                                      ? m->text : m->translations[message_language_];
                    changed |= input(("##" + symbol).c_str(), value, true, ImVec2(-1, 75));
                    if (message_language_ != GameProfile::dialogue_fallback_language) {
                        ImGui::TextWrapped("English: %s", m->text.c_str());
                        if (value.empty()) ImGui::TextDisabled("Using English fallback");
                    }
                    ImGui::TextDisabled("%s", symbol.c_str());
                    if (rich) {
                        ImGui::PushID(symbol.c_str());
                        changed |= ImGui::Checkbox("Text tokens", &m->formatted);
                        ImGui::SameLine();
                        if (ImGui::Button("Append token..."))
                            ImGui::OpenPopup("Text token");
                        if (ImGui::BeginPopup("Text token")) {
                            auto token = scene_tools_.text_token();
                            if (!token.empty()) {
                                value += token;
                                m->formatted = true;
                                changed = true;
                            }
                            ImGui::EndPopup();
                        }
                        if (m->formatted) {
                            ImGui::TextWrapped(
                                "Names use the game's language. Use {{ and }} for literal braces.");
                            try {
                                auto text = encode_dialogue_translation(m->text, m->text_for(message_language_), true);
                                ImGui::TextWrapped("Preview: %s", text.preview.c_str());
                                ImGui::TextDisabled(
                                    "Names shown as placeholders; paging is not simulated.");
                            } catch (const std::exception &e) {
                                ImGui::TextWrapped("%s", e.what());
                            }
                        }
                        ImGui::PopID();
                    }
                }
            };
            if (s.action == ConversationAction::Message || s.action == ConversationAction::Choice ||
                s.action == ConversationAction::Menu)
                message("Dialogue", s.message, true);
            if (s.action == ConversationAction::MenuOption) {
                message("Option label", s.message);
                ImGui::TextWrapped("Add steps here to run when this option is chosen. Moving this "
                                   "option moves its entire branch.");
            }
            if (s.action == ConversationAction::Menu) {
                ImGui::TextWrapped("Select an option on the left to edit its label and steps. Add "
                                   "step with the menu selected adds to Cancel.");
                if (ImGui::Button("Add option")) {
                    ConversationStep option;
                    option.id = next_id(draft.steps);
                    option.action = ConversationAction::MenuOption;
                    option.message = "MSG_OPTION_" + std::to_string(option.id);
                    while (std::any_of(draft.messages.begin(), draft.messages.end(),
                                       [&](const auto &m) {
                                           return m.symbol == option.message;
                                       }))
                        option.message += "_";
                    draft.messages.push_back({option.message, "New option"});
                    s.children.push_back(option);
                    selected_ = option.id;
                    changed = true;
                }
            }
            if (s.action == ConversationAction::Choice) {
                message("Yes label", s.yes_message);
                message("No label", s.no_message);
            }
            if (s.action == ConversationAction::Wait) {
                int frames = int(s.frames);
                if (ImGui::InputInt("Frames", &frames)) {
                    s.frames = unsigned(std::max(1, frames));
                    changed = true;
                }
            }
            if (s.action == ConversationAction::Repeat) {
                int count = int(s.repeat);
                if (ImGui::InputInt("Times", &count)) {
                    s.repeat = unsigned(std::max(1, count));
                    changed = true;
                }
            }
            if (s.action == ConversationAction::Sound) {
                int sound = int(s.sound);
                if (ImGui::InputInt("Sound ID", &sound)) {
                    s.sound = unsigned(std::max(0, sound));
                    changed = true;
                }
                changed |= ImGui::SliderFloat("Volume", &s.volume, 0, 1);
                changed |= sound_picker_.draw(session_source_, s.sound);
            }
            if (s.action >= ConversationAction::FacePlayer &&
                s.action <= ConversationAction::WaitMotion) {
                changed |= scene_tools_.actor(s);
                if (s.action == ConversationAction::Rotate)
                    changed |= ImGui::InputFloat("Angle (degrees)", &s.angle);
                if (s.action == ConversationAction::PlayMotion)
                    changed |= scene_tools_.motion(s);
                if (s.action == ConversationAction::Move) {
                    ImGui::TextWrapped(
                        "Legacy raw movement command. Use Move to destination for coordinates.");
                    for (int i = 0; i < 2; ++i)
                        changed |= ImGui::InputInt(("Argument " + std::to_string(i + 2)).c_str(),
                                                   &s.arguments[i]);
                }
                if (s.action <= ConversationAction::Move) {
                    changed |= ImGui::Checkbox("Wait for completion", &s.wait_for_completion);
                    if (s.action == ConversationAction::PlayMotion)
                        ImGui::TextWrapped(
                            "A looping motion may never complete. Leave waiting off for loops.");
                }
                if (s.action == ConversationAction::WaitMotion)
                    ImGui::TextWrapped(
                        "Waits until the motion stops. A looping motion may never complete.");
            }
            if (s.action == ConversationAction::MoveTo) {
                changed |= scene_tools_.actor(s);
                changed |= scene_tools_.destination(s);
                changed |= ImGui::Checkbox("Wait for completion", &s.wait_for_completion);
                if (s.wait_for_completion)
                    ImGui::TextWrapped(
                        "Waits until the actor finishes. A blocked actor can keep the interaction "
                        "waiting. Use clear intermediate destinations to route around obstacles.");
            }
            if (s.action == ConversationAction::GiveItem)
                changed |= scene_tools_.reward(s);
            if (s.action == ConversationAction::Encounter)
                changed |= scene_tools_.encounter(s);
            if (s.action == ConversationAction::TrainerBattle)
                changed |= scene_tools_.trainer(s);
            if (s.action == ConversationAction::IfBattleResult) {
                int result = s.value - 1;
                if (ImGui::Combo("Result", &result, "Result 1\0Result 2\0Result 3\0Result 4\0")) {
                    s.value = result + 1;
                    changed = true;
                }
                ImGui::TextWrapped("Tests the last battle in this interaction. Trainer victory is "
                                   "result 1. Defeat is handled by the battle step. Before a "
                                   "battle, the condition is false.");
            }
            if (s.action >= ConversationAction::SetFlag && s.action <= ConversationAction::IfWork) {
                bool flag = s.action == ConversationAction::SetFlag ||
                            s.action == ConversationAction::IfFlag;
                changed |= ImGui::InputInt(flag ? "Flag ID" : "Work ID", &s.state_id);
                if (flag) {
                    bool enabled = s.value != 0;
                    if (ImGui::Checkbox("On", &enabled)) {
                        s.value = enabled;
                        changed = true;
                    }
                } else {
                    if (s.action == ConversationAction::IfWork) {
                        int comparison = int(s.comparison);
                        if (ImGui::Combo(
                                "Compare", &comparison,
                                "Equal\0Not equal\0Less than\0At most\0Greater than\0At least\0")) {
                            s.comparison = ConversationComparison(comparison);
                            changed = true;
                        }
                    }
                    changed |= ImGui::InputInt("Value", &s.value);
                }
                ImGui::TextWrapped(
                    "Uses game state IDs. No unused ID or story-stage name is inferred.");
            }
            bool boundary =
                s.action == ConversationAction::Begin || s.action == ConversationAction::End;
            ImGui::BeginDisabled(boundary);
            if (TutorialWidgets::Button("conversation_editor", "Move up") && found.index > 0 &&
                found.siblings->at(found.index - 1).action != ConversationAction::Begin) {
                std::swap(found.siblings->at(found.index), found.siblings->at(found.index - 1));
                changed = true;
            }
            ImGui::SameLine();
            if (TutorialWidgets::Button("conversation_editor", "Move down") &&
                found.index + 1 < found.siblings->size() &&
                found.siblings->at(found.index + 1).action != ConversationAction::End) {
                std::swap(found.siblings->at(found.index), found.siblings->at(found.index + 1));
                changed = true;
            }
            ImGui::BeginDisabled(s.action == ConversationAction::MenuOption &&
                                 found.siblings->size() <= 2);
            if (TutorialWidgets::Button("conversation_editor", "Delete step")) {
                found.siblings->erase(found.siblings->begin() + std::ptrdiff_t(found.index));
                selected_ = draft.steps.front().id;
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
    auto insertion = locate(draft.steps, selected_);
    if (insertion.siblings && conversation_branch(insertion.siblings->at(insertion.index).action) &&
        insertion.siblings->at(insertion.index).action != ConversationAction::Menu) {
        auto action = insertion.siblings->at(insertion.index).action;
        ImGui::TextUnformatted("Insert into:");
        ImGui::SameLine();
        if (ImGui::RadioButton(branch_name(action, false), !insert_otherwise_))
            insert_otherwise_ = false;
        ImGui::SameLine();
        if (ImGui::RadioButton(branch_name(action, true), insert_otherwise_))
            insert_otherwise_ = true;
    }
    bool defeat_insertion = false;
    if (insertion.siblings) {
        const auto &at = insertion.siblings->at(insertion.index);
        defeat_insertion = (at.action == ConversationAction::Encounter ||
                            at.action == ConversationAction::TrainerBattle) &&
                           insert_otherwise_;
        std::function<void(const std::vector<ConversationStep> &)> scan = [&](const auto &steps) {
            for (const auto &step : steps) {
                if ((step.action == ConversationAction::Encounter ||
                     step.action == ConversationAction::TrainerBattle) &&
                    &step.otherwise == insertion.siblings)
                    defeat_insertion = true;
                scan(step.children);
                scan(step.otherwise);
            }
        };
        scan(draft.steps);
    }
    if (defeat_insertion)
        ImGui::TextWrapped("Defeat: add flag/work updates before the game takes over recovery.");
    if (TutorialWidgets::Button("conversation_editor", "Add step..."))
        ImGui::OpenPopup("Add conversation step");
    if (ImGui::BeginPopup("Add conversation step")) {
        const std::vector<ConversationAction> groups[] = {
            {ConversationAction::Message, ConversationAction::Choice, ConversationAction::Menu,
             ConversationAction::Sound, ConversationAction::Wait, ConversationAction::Repeat},
            {ConversationAction::FacePlayer, ConversationAction::Rotate,
             ConversationAction::PlayMotion, ConversationAction::MoveTo,
             ConversationAction::WaitAction, ConversationAction::WaitMotion,
             ConversationAction::Move},
            {ConversationAction::SetFlag, ConversationAction::SetWork, ConversationAction::IfFlag,
             ConversationAction::IfWork},
            {ConversationAction::GiveItem, ConversationAction::Encounter,
             ConversationAction::TrainerBattle, ConversationAction::IfBattleResult}};
        const char *titles[] = {"Dialogue and timing", "Characters", "Game state",
                                "Items and battles"};
        for (unsigned group = 0; group < std::size(groups); ++group) {
            if (!ImGui::BeginMenu(titles[group], !defeat_insertion || group == 2))
                continue;
            for (auto action : groups[group]) {
                bool allowed = !defeat_insertion || action == ConversationAction::SetFlag ||
                               action == ConversationAction::SetWork;
                if (ImGui::MenuItem(name(action), nullptr, false, allowed)) {
                    ConversationStep s;
                    s.id = next_id(draft.steps);
                    s.action = action;
                    if (actor_.kind != InteractionTargetKind::Npc)
                        s.actor = -1;
                    scene_tools_.initialize(s);
                    if (action == ConversationAction::MoveTo)
                        s.wait_for_completion = true;
                    if (action == ConversationAction::IfBattleResult)
                        s.value = 1;
                    auto add_message = [&](const std::string &suffix, const char *text) {
                        auto symbol = "MSG_STEP_" + std::to_string(s.id) + suffix;
                        while (std::any_of(draft.messages.begin(), draft.messages.end(),
                                           [&](const auto &m) {
                                               return m.symbol == symbol;
                                           }))
                            symbol += "_";
                        draft.messages.push_back({symbol, text});
                        return symbol;
                    };
                    if (action == ConversationAction::Message ||
                        action == ConversationAction::Choice || action == ConversationAction::Menu)
                        s.message = add_message("", "Alola!");
                    if (action == ConversationAction::Choice) {
                        s.yes_message = add_message("_YES", "Yes");
                        s.no_message = add_message("_NO", "No");
                    }
                    if (action == ConversationAction::Menu) {
                        for (unsigned i = 0; i < 2; ++i) {
                            ConversationStep option;
                            option.id = s.id + i + 1;
                            option.action = ConversationAction::MenuOption;
                            option.message = add_message("_OPTION_" + std::to_string(i + 1),
                                                         i ? "Option 2" : "Option 1");
                            s.children.push_back(option);
                        }
                    }
                    auto location = locate(draft.steps, selected_);
                    if (location.siblings) {
                        auto &at = location.siblings->at(location.index);
                        if (at.action == ConversationAction::Menu)
                            at.otherwise.push_back(s);
                        else if (conversation_branch(at.action))
                            (insert_otherwise_ ? at.otherwise : at.children).push_back(s);
                        else if (at.action == ConversationAction::Repeat ||
                                 at.action == ConversationAction::MenuOption)
                            at.children.push_back(s);
                        else
                            location.siblings->insert(
                                location.siblings->begin() +
                                    std::ptrdiff_t(location.index +
                                                   (at.action == ConversationAction::End ? 0 : 1)),
                                s);
                    } else
                        draft.steps.insert(draft.steps.end() - 1, s);
                    selected_ = s.id;
                    changed = true;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::TextDisabled("Select a branch to insert inside it, or a step to insert after it.");
        ImGui::EndPopup();
    }
    if (changed) {
        try {
            document.update(std::move(draft));
            error_.clear();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    }
}
void ConversationEditor::draw(Preferences &preferences) {
    ImGui::SetNextWindowSize(ImVec2(1000, 760), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Interaction authoring", &open_)) {
        ImGui::End();
        return;
    }
    if (!workspace_ || (!candidate_ && !workspace_->interactions().contains(actor_))) {
        ImGui::TextWrapped("%s", error_.c_str());
        ImGui::End();
        return;
    }
    bool busy = compilation_.valid();
    ImGui::BeginDisabled(busy);
    if (actor_.kind == InteractionTargetKind::Trainer)
        ImGui::Text("Trainer talk / zone %u / event %u", actor_.zone, actor_.event);
    else
        ImGui::Text("Area %u / zone slot %u / %s", area_, actor_.zone, target_name(actor_).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(workspace_->dirty() ? "Unsaved changes" : "Saved");
    if (ImGui::BeginCombo("Conversation", target_name(actor_).c_str())) {
        for (const auto &[actor, doc] : workspace_->interactions())
            if (ImGui::Selectable(
                    ("Zone " + std::to_string(actor.zone) + " / " + target_name(actor)).c_str(),
                    actor == actor_)) {
                select_actor(actor);
            }
        ImGui::EndCombo();
    }
    if (candidate_) {
        ImGui::TextWrapped("No override is being authored for this interaction. The template below "
                           "is a replacement, not a reconstruction of the original script.");
        if (TutorialWidgets::Button("conversation_editor", "Create override")) {
            workspace_->open(actor_);
            candidate_.reset();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close"))
            open_ = false;
        if (candidate_) {
            ImGui::SeparatorText("Replacement template");
            flow(candidate_->draft().steps, selected_, candidate_->draft());
            ImGui::EndDisabled();
            ImGui::End();
            return;
        }
    }
    if (TutorialWidgets::Button("conversation_editor", "Discard draft"))
        ImGui::OpenPopup("Discard interaction draft?");
    ImGui::SameLine();
    if (TutorialWidgets::Button("conversation_editor", "Remove override"))
        ImGui::OpenPopup("Remove interaction override?");
    bool changed_override = false;
    for (bool remove : {false, true}) {
        if (ImGui::BeginPopupModal(remove ? "Remove interaction override?"
                                          : "Discard interaction draft?",
                                   nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped(
                remove ? "Remove this interaction's override? Save and stage to restore its "
                         "baseline behavior. Other interactions stay authored."
                       : "Restore this interaction to when you opened it, including any changes "
                         "saved since then? Other interactions stay authored.");
            if (ImGui::Button(remove ? "Remove" : "Discard")) {
                if (remove)
                    workspace_->remove(actor_);
                else
                    workspace_->restore_actor(actor_, *actor_snapshot_);
                select_actor(actor_);
                changed_override = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    if (changed_override) {
        ImGui::EndDisabled();
        ImGui::End();
        return;
    }
    auto &document = workspace_->open(actor_);
    if (actor_.kind == InteractionTargetKind::Trainer)
        ImGui::TextWrapped(
            "Edits this trainer's talk interaction. Trainer identity, patrol and the game's "
            "sight-triggered challenge stay intact. Paired challenges keep the original "
            "interaction. The template checks defeat before offering a battle.");
    else if (document.target().script >= TargetProfile::first_shared_script)
        ImGui::TextWrapped(
            "Replaces this placement's interaction. The shared script stays unchanged; its steps "
            "are not copied.");
    if (actor_.kind == InteractionTargetKind::PositionTrigger)
        ImGui::TextWrapped("Runs on region activation. Use a flag for one-time events. "
                           "Choose a target for character steps.");
    if (actor_.kind == InteractionTargetKind::Scenery)
        ImGui::TextDisabled("Scenery: sign dialogue and player look-at.");
    if (UndoShortcuts::button("conversation_editor", "Undo"))
        document.undo();
    ImGui::SameLine();
    if (UndoShortcuts::button("conversation_editor", "Redo"))
        document.redo();
    ImGui::SameLine();
    if (ImGui::Button("Save Project")) {
        try {
            save_editor_project();
            error_.clear();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    }
    ImGui::SameLine();
    if (TutorialWidgets::Button("conversation_editor", "Compiler settings"))
        settings_ = !settings_;
    compiler_settings(preferences);
    if (settings_) {
        input("Compiler executable", compiler_);
        input("Support include folder", sdk_);
        if (TutorialWidgets::Button("conversation_editor", "Save compiler settings")) {
            preferences.set("pawn.compiler", compiler_);
            preferences.set("pawn.sdk", sdk_);
            preferences.save();
        }
        ImGui::TextWrapped(
            "Use a compatible version-10 gf-pawncc and the bundled support includes. "
            "The includes declare the supported game functions and their arguments.");
    }
    if (TutorialWidgets::Button("conversation_editor", "Compile all conversations"))
        compile_all(preferences);
    ImGui::SameLine();
    ImGui::TextDisabled(workspace_->compiled() ? "Compiled" : "Compile before staging");
    ImGui::SameLine();
    if (TutorialWidgets::Button("conversation_editor", "Preview visual flow")) {
        preview_battle_result_ = -1;
        preview_flags_.clear();
        preview_work_.clear();
        preview_queue_ = document.draft().steps;
        preview_choice_.reset();
        preview_text_.clear();
        preview_open_ = true;
    }
    ImGui::BeginDisabled(compilation_.valid());
    messages();
    if (TutorialWidgets::RadioButton("conversation_editor", "Visual editor", !code_))
        code_ = false;
    ImGui::SameLine();
    if (TutorialWidgets::RadioButton("conversation_editor", "Pawn source", code_))
        code_ = true;
    if (!code_)
        visual();
    else {
        ConversationPawn generated;
        try {
            generated = workspace_->generate(actor_);
        } catch (const std::exception &e) {
            error_ = e.what();
        }
        auto draft = document.draft();
        auto source = draft.custom ? draft.pawn : generated.source;
        ImGui::TextDisabled(draft.custom
                                ? "Custom Pawn / visual preview remains separate"
                                : "Generated Pawn / typing creates a custom source version");
        if (input("##Pawn", source, true, ImVec2(-1, 340))) {
            draft.pawn = source;
            draft.custom = true;
            try {
                document.update(std::move(draft));
            } catch (const std::exception &e) {
                error_ = e.what();
            }
        }
        if (document.draft().custom &&
            TutorialWidgets::Button("conversation_editor", "Restore generated source..."))
            ImGui::OpenPopup("Restore generated Pawn?");
        if (ImGui::BeginPopupModal("Restore generated Pawn?", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(
                "Switch compilation back to the visual steps? Custom text is retained for Undo.");
            if (ImGui::Button("Restore")) {
                auto next = document.draft();
                next.custom = false;
                document.update(next);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::CollapsingHeader("Message definitions"))
            input("##definitions", generated.definitions, true, ImVec2(-1, 130), true);
        if (ImGui::CollapsingHeader("Support library / native declarations")) {
            try {
                auto support = text(read_file(std::filesystem::path(sdk_) / "usum.inc"));
                input("##support", support, true, ImVec2(-1, 190), true);
            } catch (const std::exception &e) {
                ImGui::TextWrapped("%s", e.what());
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (busy)
        ImGui::TextUnformatted("Compiling in the background...");
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    if (!diagnostics_.empty()) {
        for (std::size_t i = 0; i < diagnostics_.size(); ++i) {
            const auto diagnostic = diagnostics_[i];
            ImGui::PushID(int(i));
            auto label = diagnostic.file + ":" + std::to_string(diagnostic.line) + " - " +
                         diagnostic.message;
            if (ImGui::Selectable(label.c_str())) {
                if (actor_ != diagnostic_actor_)
                    select_actor(diagnostic_actor_);
                if (diagnostic.step) {
                    selected_ = *diagnostic.step;
                    code_ = false;
                } else
                    code_ = true;
            }
            ImGui::PopID();
        }
    }
    if (!output_.empty() && ImGui::CollapsingHeader("Compiler output"))
        ImGui::TextWrapped("%s", output_.c_str());
    ImGui::TextDisabled("Compile all, Save Project, Stage Project, then reload staged assets. Game "
                        "export uses Build Project.");
    ImGui::End();
}
void ConversationEditor::preview() {
    ImGui::SetNextWindowSize(ImVec2(540, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Conversation preview", &preview_open_)) {
        ImGui::TextDisabled("Visual flow only / no game engine or custom Pawn execution");
        auto message = [&](const std::string &symbol) {
            for (const auto &m : workspace_->open(actor_).draft().messages)
                if (m.symbol == symbol)
                    try {
                        return encode_dialogue_translation(m.text, m.text_for(message_language_), m.formatted).preview;
                    } catch (const std::exception &e) {
                        return std::string("Invalid text: ") + e.what();
                    }
            return symbol;
        };
        if (preview_text_.empty() && !preview_choice_) {
            unsigned budget = 0;
            while (!preview_queue_.empty() && ++budget < 500) {
                auto s = preview_queue_.front();
                preview_queue_.erase(preview_queue_.begin());
                if (s.action == ConversationAction::GiveItem ||
                    (s.action == ConversationAction::Encounter ||
                     s.action == ConversationAction::TrainerBattle)) {
                    preview_text_ =
                        std::string(name(s.action)) + ". Choose a simulated game result.";
                    preview_choice_ = s;
                    break;
                }
                if (s.action == ConversationAction::IfBattleResult) {
                    const auto &branch =
                        preview_battle_result_ == s.value ? s.children : s.otherwise;
                    preview_queue_.insert(preview_queue_.begin(), branch.begin(), branch.end());
                    continue;
                }
                if (s.action == ConversationAction::MoveTo) {
                    preview_text_ = "Move to world position " + std::to_string(s.destination[0]) +
                                    ", " + std::to_string(s.destination[1]) + ", " +
                                    std::to_string(s.destination[2]) +
                                    ". Movement is not simulated.";
                    break;
                }
                if (s.action == ConversationAction::Message) {
                    preview_text_ = message(s.message);
                    break;
                }
                if (s.action == ConversationAction::SetFlag ||
                    s.action == ConversationAction::SetWork) {
                    auto &state =
                        s.action == ConversationAction::SetFlag ? preview_flags_ : preview_work_;
                    state[s.state_id] = s.value;
                    preview_text_ = std::string("Preview only: ") + name(s.action) + " " +
                                    std::to_string(s.state_id) + " = " + std::to_string(s.value);
                    break;
                }
                if (s.action == ConversationAction::IfFlag ||
                    s.action == ConversationAction::IfWork) {
                    auto &state =
                        s.action == ConversationAction::IfFlag ? preview_flags_ : preview_work_;
                    if (state.contains(s.state_id)) {
                        bool result = conversation_compare(state.at(s.state_id),
                                                           s.action == ConversationAction::IfFlag
                                                               ? ConversationComparison::Equal
                                                               : s.comparison,
                                                           s.value);
                        const auto &branch = result ? s.children : s.otherwise;
                        preview_queue_.insert(preview_queue_.begin(), branch.begin(), branch.end());
                        preview_text_ =
                            std::string("Preview condition: ") + (result ? "True" : "False");
                    } else {
                        preview_text_ = std::string(name(s.action)) + " " +
                                        std::to_string(s.state_id) + " " +
                                        (s.action == ConversationAction::IfFlag
                                             ? "=="
                                             : conversation_comparison(s.comparison)) +
                                        " " + std::to_string(s.value) +
                                        ". Game state is not loaded; choose a simulated result.";
                        preview_choice_ = s;
                    }
                    break;
                }
                if (s.action >= ConversationAction::FacePlayer &&
                    s.action <= ConversationAction::WaitMotion) {
                    preview_text_ = std::string(name(s.action)) + " / " +
                                    (s.actor == -2   ? "this NPC"
                                     : s.actor == -1 ? "player"
                                                     : "event " + std::to_string(s.actor)) +
                                    ". Character actions are not simulated.";
                    break;
                }
                if (s.action == ConversationAction::Choice ||
                    s.action == ConversationAction::Menu) {
                    preview_text_ = message(s.message);
                    preview_choice_ = s;
                    break;
                }
                if (s.action == ConversationAction::Wait) {
                    preview_text_ = "Wait " + std::to_string(s.frames) + " frames";
                    break;
                }
                if (s.action == ConversationAction::Sound) {
                    preview_text_ = "Play sound " + std::to_string(s.sound);
                    break;
                }
                if (s.action == ConversationAction::Repeat) {
                    if (s.repeat > 100 ||
                        s.children.size() * s.repeat + preview_queue_.size() > 4096) {
                        preview_text_ = "Preview limit reached. Reduce the repeat count.";
                        preview_queue_.clear();
                        break;
                    }
                    for (unsigned i = 0; i < s.repeat; ++i)
                        preview_queue_.insert(preview_queue_.begin(), s.children.begin(),
                                              s.children.end());
                }
            }
            if (preview_text_.empty())
                preview_text_ = "Conversation complete.";
        }
        ImGui::TextWrapped("%s", preview_text_.c_str());
        if (preview_choice_ && preview_choice_->action == ConversationAction::Menu) {
            auto choice = *preview_choice_;
            const std::vector<ConversationStep> *branch = nullptr;
            for (const auto &option : choice.children) {
                ImGui::PushID(int(option.id));
                if (ImGui::Button(message(option.message).c_str()))
                    branch = &option.children;
                ImGui::PopID();
            }
            if (ImGui::Button("Cancel (B)"))
                branch = &choice.otherwise;
            if (branch) {
                preview_queue_.insert(preview_queue_.begin(), branch->begin(), branch->end());
                preview_choice_.reset();
                preview_text_.clear();
            }
        } else if (preview_choice_) {
            auto choice = *preview_choice_;
            bool yes = ImGui::Button(choice.action == ConversationAction::Choice
                                         ? message(choice.yes_message).c_str()
                                         : branch_name(choice.action, false));
            ImGui::SameLine();
            bool no = ImGui::Button(choice.action == ConversationAction::Choice
                                        ? message(choice.no_message).c_str()
                                        : branch_name(choice.action, true));
            if (choice.action == ConversationAction::Choice) {
                ImGui::SameLine();
                no |= ImGui::Button("Cancel");
            }
            if (choice.action == ConversationAction::TrainerBattle) {
                if (yes)
                    preview_battle_result_ = 1;
                if (no) {
                    preview_battle_result_ = 0;
                    preview_queue_.clear();
                }
            }
            if (choice.action == ConversationAction::Encounter) {
                static int result = 0;
                ImGui::Combo("Returned result", &result,
                             "Result 1\0Result 2\0Result 3\0Result 4\0");
                if (yes)
                    preview_battle_result_ = result + 1;
                if (no) {
                    preview_battle_result_ = 0;
                    preview_queue_.clear();
                }
            }
            if (yes || no) {
                const auto &steps = yes ? choice.children : choice.otherwise;
                preview_queue_.insert(preview_queue_.begin(), steps.begin(), steps.end());
                preview_choice_.reset();
                preview_text_.clear();
            }
        } else if (!preview_queue_.empty() && ImGui::Button("Continue"))
            preview_text_.clear();
    }
    ImGui::End();
}
}
