#pragma once
#include <memory>
#include <mutex>
#include <string>
struct SDL_Window;
namespace studio {
struct FolderSelection {
    std::mutex mutex;
    bool pending = false, ready = false;
    std::string path, error;
};
void choose_png(SDL_Window *window, const std::shared_ptr<FolderSelection> &selection);
void choose_archive(SDL_Window *window, const std::shared_ptr<FolderSelection> &selection,
                    const char *initial);
void choose_material_document(SDL_Window *window, const std::shared_ptr<FolderSelection> &selection,
                              const char *initial, bool save);
void choose_patch(SDL_Window *window, const std::shared_ptr<FolderSelection> &selection,
                  const char *initial, bool save);
void choose_folder(SDL_Window *window, const std::shared_ptr<FolderSelection> &selection,
                   const char *initial);
}
