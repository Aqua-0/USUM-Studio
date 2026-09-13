#pragma once
#include "native/project_binding.h"
#include "images/image_document.h"
#include "native/folder_picker.h"
#include <bgfx/bgfx.h>
#include <SDL3/SDL.h>
#include <atomic>
#include <future>
#include <memory>
namespace studio {
class ImageEditor {
  public:
    explicit ImageEditor(SDL_Window *window) : window_(window) {
    }
    ~ImageEditor();
    void draw(const std::filesystem::path &dump);
    void update();
    void request_leave(std::function<void()> action);

  private:
    ProjectBinding project_;
    void bind_project();
    enum class Action { Import, ExportPng, Save, Load, Export };
    void import_file(const std::filesystem::path &file);
    void choose(Action action);
    void scan(const std::filesystem::path &dump);
    void select(std::size_t index);
    void refresh();
    void clear_textures();
    void save(const std::filesystem::path &file);
    void filter();
    SDL_Window *window_;
    std::unique_ptr<ImageDocument> document_;
    std::future<ImageCatalog> scan_;
    std::future<std::string> export_;
    std::future<Bytes> encoding_;
    std::atomic<unsigned> progress_ = 0, total_ = 1;
    std::atomic<bool> cancel_ = false;
    std::map<std::size_t, bgfx::TextureHandle> thumbnails_;
    bgfx::TextureHandle preview_ = BGFX_INVALID_HANDLE;
    std::shared_ptr<FolderSelection> dialog_ = std::make_shared<FolderSelection>();
    Action action_ = Action::Import;
    std::function<void()> leave_;
    std::filesystem::path saved_, imported_, pending_import_;
    std::string error_, notice_, category_ = "All";
    std::vector<std::string> categories_;
    std::vector<std::size_t> filtered_;
    char search_[160]{};
    std::size_t selected_ = std::size_t(-1);
    int language_ = -1, channel_ = 0;
    float zoom_ = 4;
    bool original_ = false, grid_ = false, checker_ = true;
    float background_[3] = {.18f, .20f, .23f};
};
}
