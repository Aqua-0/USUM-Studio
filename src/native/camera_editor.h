#pragma once
#include "native/project_binding.h"
#include "field/camera_document.h"
#include "native/renderer.h"
#include "native/folder_picker.h"
#include <functional>
#include <future>
namespace studio {
class CameraEditor {
  public:
    CameraEditor(SDL_Window *window, EnvironmentRenderer &renderer)
        : window_(window), renderer_(renderer) {
    }
    void set_scene(std::shared_ptr<Environment> scene, unsigned area,
                   const std::filesystem::path &dump);
    bool draw_workspace(bool loading);
    void open_patch(const std::filesystem::path &path);
    bool take_focus_request() {
        bool value = focus_requested_;
        focus_requested_ = false;
        return value;
    }
    void update();
    void draw(bool loading, ViewportCamera &view);
    void request_leave(std::function<void()> action);

  private:
    unsigned project_area_ = 0;
    ProjectBinding project_;
    void bind_project();
    void synchronize();
    void dialog(int kind);
    void save();
    SDL_Window *window_;
    EnvironmentRenderer &renderer_;
    std::shared_ptr<Environment> scene_;
    std::unique_ptr<CameraDocument> document_;
    std::filesystem::path dump_, path_;
    std::string group_, message_;
    ViewportCamera view_;
    bool overview_ = false, hovered_ = false;
    bool focus_requested_ = false, player_started_ = false;
    bool follow_ = true, leave_ = false, save_leave_ = false;
    int kind_ = 0, category_ = 0;
    std::shared_ptr<FolderSelection> dialog_ = std::make_shared<FolderSelection>();
    std::future<void> export_;
    std::function<void()> leave_action_;
};
}
