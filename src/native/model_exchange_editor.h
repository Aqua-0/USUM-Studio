#pragma once
#include "assets/material_document.h"
#include "native/renderer.h"
#include "native/folder_picker.h"
namespace studio {
class ModelExchangeEditor {
  public:
    bool pending() const {
        return action_ != 0;
    }
    void draw(MaterialDocument &document, ModelDocument &preview, EnvironmentRenderer &renderer,
              bool &playing, SDL_Window *window);

  private:
    std::shared_ptr<FolderSelection> dialog_ = std::make_shared<FolderSelection>();
    int action_ = 0, motion_ = -1;
    std::string owner_, message_;
    std::filesystem::path file_;
};
}
