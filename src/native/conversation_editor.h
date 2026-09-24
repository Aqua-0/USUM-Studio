#pragma once
#include "field/conversation_workspace.h"
#include "native/project_binding.h"
#include "native/script_sound_picker.h"
#include "native/conversation_scene_tools.h"
#include "native/preferences.h"
#include "scene/environment.h"
#include <future>
namespace studio {
class ConversationEditor {
  public:
    void global_controls(Preferences &preferences);
    void menu(Preferences &preferences);
    void clear_scene_preview() {
        scene_tools_.reset();
    }
    void update(bool active) {
        sound_picker_.update(active && open_);
        scene_tools_.update(active && open_);
    }
    void controls(std::shared_ptr<Environment> scene, int area, int selection, bool launcher,
                  Preferences &preferences, EnvironmentRenderer &renderer, MapCursor &cursor);

  private:
    void compile_all(Preferences &preferences);
    void compiler_settings(Preferences &preferences);
    void load(unsigned area, ConversationActor actor);
    void select_actor(ConversationActor actor);
    void draw(Preferences &preferences);
    void visual();
    void messages();
    unsigned message_language_ = GameProfile::dialogue_fallback_language;
    void preview();
    ScriptSoundPicker sound_picker_;
    ConversationSceneTools scene_tools_;
    int preview_battle_result_ = -1;
    ProjectBinding binding_, compilation_guard_;
    bool compile_status_open_ = false;
    std::unique_ptr<ConversationWorkspace> workspace_, actor_snapshot_;
    std::unique_ptr<AuthoredInteraction> candidate_;
    struct CompileOutcome {
        std::unique_ptr<ConversationWorkspace> workspace;
        ConversationActor actor;
        bool succeeded = false;
        std::vector<std::pair<ProjectEdit, std::string>> other_areas;
        std::vector<PawnDiagnostic> diagnostics;
        std::string output, error;
    };
    std::future<CompileOutcome> compilation_;
    std::vector<PawnDiagnostic> diagnostics_;
    ConversationActor diagnostic_actor_;
    std::filesystem::path project_root_, session_source_;
    ConversationActor actor_;
    unsigned area_ = 0, selected_ = 2;
    bool open_ = false, code_ = false, settings_ = false, discard_compilation_ = false;
    std::string error_, output_, compiler_, sdk_;
    std::vector<ConversationStep> preview_queue_;
    std::string preview_text_;
    std::optional<ConversationStep> preview_choice_;
    bool preview_open_ = false, insert_otherwise_ = false;
    std::map<int, int> preview_flags_, preview_work_;
};
}
