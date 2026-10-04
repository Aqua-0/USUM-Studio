#pragma once
#include "assets/battle_model_addition.h"
namespace studio {
class BattleModelAdditionEditor {
  public:
    static bool accepts(const ModelDocument &donor);
    bool draw(const ModelDocument &donor);
    std::unique_ptr<ModelDocument> take_created() { return std::move(created_); }
  private:
    std::filesystem::path project_root_;
    std::string message_;
    std::unique_ptr<ModelDocument> created_;
};
}
