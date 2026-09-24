#pragma once
#include "field/interaction_document.h"
#include "field/interaction_source.h"
#include <future>
#include "native/project_binding.h"
#include "scene/environment.h"
namespace studio {
class InteractionInspector {
  public:
    void controls(const Environment *scene, const std::filesystem::path &dump, int area,
                  int selection, bool show_launcher = true);

  private:
    ProjectBinding project_;
    std::unique_ptr<InteractionDocument> document_;
    std::vector<InteractionStep> steps_;
    std::vector<InteractionStateAccess> state_actions_;
    std::filesystem::path project_root_;
    unsigned entry_ = 0, script_ = 0, event_ = 0;
    int selected_step_ = -1;
    bool instructions_ = false, scroll_to_step_ = false;
    void refresh_steps();
    void draw_steps();
    void draw_state_actions();
    bool state_view_ = true, state_filter_ = false, focus_calls_ = false;
    int state_kind_ = 0, state_access_ = 0, state_id_ = 0;
    bool open_ = false, technical_ = false;
    std::string identity_, error_;
    InteractionInspection inspection_;
    InteractionSource source_;
    bool shared_edit_ = false;
    std::future<std::vector<InteractionUse>> uses_loading_;
    std::vector<InteractionUse> uses_;
    std::string uses_identity_, uses_error_;
    bool uses_scanned_ = false;
};
}
