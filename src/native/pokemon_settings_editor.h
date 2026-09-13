#pragma once
#include "native/project_binding.h"
#include "assets/model_document.h"
#include "scene/battle_scene.h"
#include "scene/battle_sendout.h"
#include "native/renderer.h"
#include "native/camera.h"
#include "native/folder_picker.h"
#include <future>
#include <functional>
namespace studio {
class PokemonSettingsEditor {
  public:
    PokemonSettingsEditor(SDL_Window *window, const std::filesystem::path &shaders)
        : window_(window), shaders_(shaders) {
    }
    void bind(const ModelDocument &model);
    void update();
    void draw();
    void request_leave(std::function<void()> action);
    void viewport(const ModelDocument &model, double seconds);
    bool active = false, shadows = true;
    bool sendout_active() const {
        return bool(sendout_model_) || sendout_job_.valid();
    }
    bool wants_focus() const {
        return leave_;
    }
    bool ready() const {
        return !active || (stage_ && renderer_ && renderer_->ready() && !stage_job_.valid());
    }
    std::string report() const;
    void open(const std::filesystem::path &path);

  private:
    std::string project_parameters_;
    ProjectBinding project_;
    void bind_project();
    std::future<BattleSendOut> sendout_job_;
    std::unique_ptr<ModelDocument> sendout_model_;
    BattleSendOut sendout_;
    float sendout_frame_ = 0;
    bool sendout_playing_ = true;
    void dialog(int kind);
    void save();
    void load_stage();
    SDL_Window *window_;
    std::filesystem::path shaders_, dump_, archive_, path_;
    std::unique_ptr<PokemonSettingsDocument> document_;
    std::string message_;
    std::shared_ptr<FolderSelection> dialog_ = std::make_shared<FolderSelection>();
    int kind_ = 0;
    bool leave_ = false, save_leave_ = false;
    std::function<void()> leave_action_;
    std::future<void> export_;
    std::future<std::pair<std::vector<BattleAsset>, std::vector<BattleAsset>>> catalog_job_;
    std::vector<BattleAsset> arenas_, trainers_;
    std::array<int, 4> arena_{0, -1, -1, -1};
    int trainer_ = 2;
    bool far_ = false, stage_dirty_ = true, catalog_started_ = false;
    std::future<Environment> stage_job_;
    std::shared_ptr<Environment> stage_;
    std::shared_ptr<const Environment> model_source_;
    BattleModelRange range_;
    std::unique_ptr<EnvironmentRenderer> renderer_;
    ViewportCamera camera_;
    int camera_mode_ = 0, other_size_ = 0;
    float camera_fov_ = 30, shadow_elevation_ = 50, shadow_azimuth_ = 40;
};
}
