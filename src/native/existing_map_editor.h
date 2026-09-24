#pragma once
#include "authoring/existing_map_brush.h"
#include "native/project_binding.h"
#include "native/renderer.h"
#include "native/camera.h"
#include <SDL3/SDL.h>
#include <future>
namespace studio {
class ExistingMapEditor {
  public:
    ExistingMapEditor(const std::filesystem::path &shaders, SDL_Window *window);
    ~ExistingMapEditor();
    bool draw(std::uint32_t frame, const std::filesystem::path &dump, unsigned area);

    bool busy() const {
        return dragging_ || loading_.valid();
    }
    void surface_tools(bool enabled) {
        if (!dragging_)
            tool_ = enabled ? 4 : (tool_ == 4 ? 0 : tool_);
    }
    std::shared_ptr<Environment> scene() const {
        return view_;
    }
    unsigned area() const {
        return area_;
    }
    ViewportCamera &camera() {
        return camera_;
    }
    void context(std::function<Environment(const Environment &)> compose) {
        compose_ = std::move(compose);
        if (view_)
            renderer_.set_scene(std::make_shared<Environment>(compose_(*view_)));
    }
    void open(const std::filesystem::path &dump, unsigned area);

  private:
    void select(unsigned draw);
    void refresh();
    void finish(bool cancel);
    void viewport();
    void document_toolbar();
    std::function<Environment(const Environment &)> compose_;
    EnvironmentRenderer renderer_;
    ViewportCamera camera_;
    SDL_Window *window_;
    std::filesystem::path dump_, project_root_;
    unsigned area_ = 0;
    std::future<Environment> loading_;
    std::shared_ptr<Environment> base_, view_;
    std::unique_ptr<MaterialDocument> document_;
    std::map<std::string, ModelDocument> edited_models_;
    ProjectBinding binding_;
    std::vector<unsigned> choices_;
    std::set<std::size_t> editable_;
    std::optional<SkinnedModel> geometry_, stroke_;
    MaterialFaces painted_;
    std::size_t first_draw_ = 0;
    bool dragging_ = false, changed_ = false, tile_mapping_ = false, recalculate_ = false;
    int tool_ = 0, material_ = 0;
    float radius_ = 100, strength_ = 50, level_ = 0, tile_size_ = 100, rotation_ = 0;
    std::string status_, error_;
};
}
