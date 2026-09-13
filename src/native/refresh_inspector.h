#pragma once
#include "assets/material_document.h"
#include <imgui.h>
#include "native/material_inspector.h"
#include "native/refresh_feeding_preview.h"
namespace studio {
class RefreshInspector {
  public:
    explicit RefreshInspector(const std::filesystem::path &shaders) : feeding(shaders) {
    }
    RefreshFeedingPreview feeding;
    void draw(const ModelDocument &document, EnvironmentRenderer &renderer,
              const MaterialSelection &selection, MaterialDocument *edit = nullptr);
    void sync(const ModelDocument &document, EnvironmentRenderer &renderer);
    bool painting() const {
        return painting_;
    }
    void reset() {
        feeding.reset();
        painting_ = stroke_ = false;
        error_.clear();
        preview_material_ = -2;
        preview_mask_ = 0;
        uv_scene_.reset();
        uv_edges_.clear();
    }
    void paint(MaterialDocument *edit, EnvironmentRenderer &renderer, const float *view,
               const float *projection, ImVec2 origin, ImVec2 size, bool hovered,
               MaterialSelection &selection);

  private:
    bool painting_ = false, stroke_ = false, show_all_categories_ = false, show_uvs_ = false;
    std::shared_ptr<const Environment> uv_scene_;
    std::vector<std::vector<std::array<ImVec2, 2>>> uv_edges_;
    int preview_material_ = -2, preview_mask_ = 0;
    int category_ = 1, radius_ = 3;
    ImVec2 previous_{};
    std::string error_;
};
}
