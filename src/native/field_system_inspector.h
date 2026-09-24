#pragma once
#include "field/field_systems.h"
#include "native/ambient_sound_preview.h"
#include "native/renderer.h"
#include "native/pedestrian_editor.h"
namespace studio {
class FieldSystemInspector {
  public:
    explicit FieldSystemInspector(EnvironmentRenderer &renderer)
        : pedestrians(renderer), renderer_(renderer) {
    }
    void set_scene(std::shared_ptr<Environment> scene, const std::filesystem::path &dump,
                   unsigned area);
    void update(bool active) {
        audio_.update(active && open_);
        pedestrians.active = open_ && selected_ >= 0 && std::size_t(selected_) < entries_.size() &&
                             entries_[selected_].category == 9;
        pedestrians.update(active);
    }
    void draw(ViewportCamera &camera, bool launcher);

    PedestrianEditor pedestrians;

  private:
    AmbientSoundPreview audio_;
    std::filesystem::path dump_;
    int audio_entry_ = -1;
    EnvironmentRenderer &renderer_;
    std::shared_ptr<Environment> scene_;
    std::vector<FieldSystemEntry> entries_;
    std::string error_;
    bool open_ = false;
    int selected_ = -1, category_ = -1;
    char search_[128]{};
};
}
