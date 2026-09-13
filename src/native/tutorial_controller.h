#pragma once
#include "native/preferences.h"
#include "tutorials/tutorial_state.h"
#include <imgui.h>
#include <map>
namespace studio {
class TutorialController {
  public:
    explicit TutorialController(Preferences &preferences);
    ~TutorialController();
    void begin_frame(int workspace, bool suspended);
    void menu();
    void draw();
    void observe(const char *module, const char *label, bool used);
    const TutorialState &state() const {
        return state_;
    }

  private:
    struct Target {
        ImVec2 low, high;
        bool disabled = false;
    };
    Preferences &preferences_;
    TutorialState state_;
    std::map<std::string, Target> targets_;
    std::vector<std::string> visible_, history_;
    std::map<int, std::set<std::string>> encountered_;
    std::string error_, reference_id_;
    int workspace_ = -1;
    bool suspended_ = false, paused_ = false, reference_ = false, inspect_ = false,
         restart_requested_ = false;
    char search_[160]{};
    void save();
    void draw_reference();
};
}
