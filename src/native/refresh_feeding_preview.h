#pragma once
#include "assets/material_document.h"
#include "assets/refresh_feeding_preview.h"
#include "native/renderer.h"
namespace studio {
class RefreshFeedingPreview {
  public:
    explicit RefreshFeedingPreview(std::filesystem::path shaders) : shaders_(std::move(shaders)) {
    }
    bool active = false, camera_view = false;
    void reset();
    void controls(const ModelDocument &document, MaterialDocument *edit);
    void viewport(const ModelDocument &document, const EnvironmentRenderer &original,
                  std::uint64_t revision);

  private:
    void camera_controls(const RefreshFeedingData &data, MaterialDocument *edit, unsigned slot);
    int camera_kind_ = 0;
    std::filesystem::path shaders_;
    std::unique_ptr<EnvironmentRenderer> renderer_;
    std::unique_ptr<ModelDocument> model_;
    std::shared_ptr<const Environment> source_;
    RefreshFeedingSession session_;
    RefreshFeedingArea area_;
    bool show_zone_ = true, overrides_ = false, held_ = false, paused_ = false;
    int initial_fullness_ = 0;
    float food_x_ = 280, food_y_ = 65;
    std::uint64_t revision_ = 0;
    std::string error_;
};
}
