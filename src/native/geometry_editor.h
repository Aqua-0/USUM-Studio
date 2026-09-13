#pragma once
#include "assets/material_document.h"
#include "assets/mesh_geometry.h"
#include "assets/mesh_selection.h"
#include "native/renderer.h"
#include "native/camera.h"
#include <imgui.h>
namespace studio {
class GeometryEditor {
  public:
    ~GeometryEditor();
    void deactivate(ModelDocument &preview, EnvironmentRenderer &renderer);
    void panel(MaterialDocument &document, ModelDocument &preview, EnvironmentRenderer &renderer,
               bool &playing, int &material);
    bool viewport(MaterialDocument &document, ModelDocument &preview, EnvironmentRenderer &renderer,
                  const ViewportCamera &camera, const float *view, const float *projection,
                  ImVec2 origin, ImVec2 size, bool hovered);

  private:
    void material_panel(MaterialDocument &document, ModelDocument &preview,
                        EnvironmentRenderer &renderer, int &material);
    void update_selection();
    MeshInfluences influences(EnvironmentRenderer &renderer) const;
    void test_pose(ModelDocument &preview, EnvironmentRenderer &renderer);
    void clear_test_pose(ModelDocument &preview, EnvironmentRenderer &renderer);
    std::size_t element_count(std::size_t mesh) const;
    std::size_t selected_count() const;
    void select_all(EnvironmentRenderer &renderer);
    void sync(MaterialDocument &document, ModelDocument &preview);
    void apply(MaterialDocument &document, ModelDocument &preview, EnvironmentRenderer &renderer,
               const SkinnedModel &next);
    void display(ModelDocument &preview, EnvironmentRenderer &renderer, const SkinnedModel &model);
    std::optional<SkinnedModel> model_;
    std::shared_ptr<const Environment> cached_scene_;
    std::string identity_, error_;
    std::uint64_t version_ = ~std::uint64_t(0);
    bool proportional_ = false, test_bone_ = false;
    float influence_radius_ = 10, test_angle_ = 25;
    int test_axis_ = 0;
    MeshInfluences affected_;
    std::shared_ptr<Environment> test_scene_;
    std::shared_ptr<const Environment> test_source_;
    MeshSurface surface_;
    bgfx::TextureHandle overlay_ = BGFX_INVALID_HANDLE;
    unsigned overlay_width_ = 0, overlay_height_ = 0;
    std::vector<std::vector<MeshEdge>> edges_;
    std::set<std::size_t> editable_{0};
    MeshVertexSelection elements_;
    int task_ = 0, selection_mode_ = 2, flatten_axis_ = 1, material_ = 0;
    bool material_highlight_ = true;
    bool box_tool_ = false, box_drag_ = false, painting_ = false, brush_mask_ = false, wire_ = true,
         all_triangles_ = false;
    ImVec2 box_start_{}, brush_previous_{};
    float brush_radius_ = 45, brush_strength_ = .5f, brush_target_ = 1;
    std::vector<std::vector<float>> brush_coverage_;
    SkinnedModel brush_base_;
    char bone_search_[96]{};
    MeshVertexSelection selected_;
    int bone_ = 0, mode_ = 0, transfer_bone_ = -1, transfer_for_ = -1;
    float weight_ = 1;
    bool through_ = false, seams_ = true, recompute_ = false, weights_ = true;
    std::array<float, 3> move_{}, rotate_{}, scale_{1, 1, 1};
    int axis_ = -1;
    float start_ = 0, last_angle_ = 0, angle_ = 0, length_ = 1, last_amount_ = 0;
    std::array<float, 3> pivot_{};
    SkinnedModel dragged_;
    bool blocked_ = false;
};
}
