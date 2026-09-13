#pragma once
#include "assets/model_document.h"
#include "native/folder_picker.h"
namespace studio {
class PokemonBundleEditor {
  public:
    void draw(const ModelDocument &donor, SDL_Window *window);
    std::unique_ptr<ModelDocument> take_created() {
        return std::move(created_);
    }

  private:
    std::shared_ptr<FolderSelection> dialog_ = std::make_shared<FolderSelection>();
    std::unique_ptr<ModelDocument> created_, prepared_;
    std::string message_;
    std::filesystem::path folder_, export_;
    int mode_ = 0, target_ = 0, action_ = 0;
    std::string owner_;
};
}
