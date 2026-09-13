#include "native/folder_picker.h"
#include <SDL3/SDL.h>
namespace studio {
void choose_folder(SDL_Window *window, const std::shared_ptr<FolderSelection> &selection,
                   const char *initial) {
    {
        std::lock_guard lock(selection->mutex);
        selection->pending = true;
        selection->ready = false;
        selection->path.clear();
        selection->error.clear();
    }
    auto *owner = new std::shared_ptr<FolderSelection>(selection);
    SDL_ShowOpenFolderDialog(
        [](void *userdata, const char *const *files, int) {
            std::unique_ptr<std::shared_ptr<FolderSelection>> owner(
                static_cast<std::shared_ptr<FolderSelection> *>(userdata));
            auto &result = **owner;
            std::lock_guard lock(result.mutex);
            if (!files)
                result.error = SDL_GetError();
            else if (files[0])
                result.path = files[0];
            result.pending = false;
            result.ready = true;
        },
        owner, window, initial && *initial ? initial : nullptr, false);
}
void choose_patch(SDL_Window *window, const std::shared_ptr<FolderSelection> &selection,
                  const char *initial, bool save) {
    {
        std::lock_guard lock(selection->mutex);
        selection->pending = true;
        selection->ready = false;
        selection->path.clear();
        selection->error.clear();
    }
    auto *owner = new std::shared_ptr<FolderSelection>(selection);
    static const SDL_DialogFileFilter filters[] = {{"Placement patch", "usum-map"}};
    auto callback = [](void *userdata, const char *const *files, int) {
        std::unique_ptr<std::shared_ptr<FolderSelection>> owner(
            static_cast<std::shared_ptr<FolderSelection> *>(userdata));
        auto &result = **owner;
        std::lock_guard lock(result.mutex);
        if (!files)
            result.error = SDL_GetError();
        else if (files[0])
            result.path = files[0];
        result.pending = false;
        result.ready = true;
    };
    if (save)
        SDL_ShowSaveFileDialog(callback, owner, window, filters, 1,
                               initial && *initial ? initial : nullptr);
    else
        SDL_ShowOpenFileDialog(callback, owner, window, filters, 1,
                               initial && *initial ? initial : nullptr, false);
}
void choose_material_document(SDL_Window *window, const std::shared_ptr<FolderSelection> &selection,
                              const char *initial, bool save) {
    {
        std::lock_guard lock(selection->mutex);
        selection->pending = true;
        selection->ready = false;
        selection->path.clear();
        selection->error.clear();
    }
    auto *owner = new std::shared_ptr<FolderSelection>(selection);
    static const SDL_DialogFileFilter filters[] = {{"Studio material document", "usum-material"}};
    auto callback = [](void *userdata, const char *const *files, int) {
        std::unique_ptr<std::shared_ptr<FolderSelection>> owner(
            static_cast<std::shared_ptr<FolderSelection> *>(userdata));
        auto &result = **owner;
        std::lock_guard lock(result.mutex);
        if (!files)
            result.error = SDL_GetError();
        else if (files[0])
            result.path = files[0];
        result.pending = false;
        result.ready = true;
    };
    if (save)
        SDL_ShowSaveFileDialog(callback, owner, window, filters, 1,
                               initial && *initial ? initial : nullptr);
    else
        SDL_ShowOpenFileDialog(callback, owner, window, filters, 1,
                               initial && *initial ? initial : nullptr, false);
}

void choose_png(SDL_Window *window, const std::shared_ptr<FolderSelection> &selection) {
    {
        std::lock_guard lock(selection->mutex);
        selection->pending = true;
        selection->ready = false;
        selection->path.clear();
        selection->error.clear();
    }
    auto *owner = new std::shared_ptr<FolderSelection>(selection);
    static const SDL_DialogFileFilter filter[] = {{"PNG texture", "png"}};
    SDL_ShowOpenFileDialog(
        [](void *userdata, const char *const *files, int) {
            std::unique_ptr<std::shared_ptr<FolderSelection>> owner(
                static_cast<std::shared_ptr<FolderSelection> *>(userdata));
            auto &result = **owner;
            std::lock_guard lock(result.mutex);
            if (!files)
                result.error = SDL_GetError();
            else if (files[0])
                result.path = files[0];
            result.pending = false;
            result.ready = true;
        },
        owner, window, filter, 1, nullptr, false);
}
void choose_archive(SDL_Window *window, const std::shared_ptr<FolderSelection> &selection,
                    const char *initial) {
    {
        std::lock_guard lock(selection->mutex);
        selection->pending = true;
        selection->ready = false;
        selection->path.clear();
        selection->error.clear();
    }
    auto *owner = new std::shared_ptr<FolderSelection>(selection);
    static const SDL_DialogFileFilter filter[] = {
        {"Game archives (including extensionless files)", "*"}};
    SDL_ShowOpenFileDialog(
        [](void *userdata, const char *const *files, int) {
            std::unique_ptr<std::shared_ptr<FolderSelection>> owner(
                static_cast<std::shared_ptr<FolderSelection> *>(userdata));
            auto &result = **owner;
            std::lock_guard lock(result.mutex);
            if (!files)
                result.error = SDL_GetError();
            else if (files[0])
                result.path = files[0];
            result.pending = false;
            result.ready = true;
        },
        owner, window, filter, 1, initial && *initial ? initial : nullptr, false);
}

}
