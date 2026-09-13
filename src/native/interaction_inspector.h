#pragma once
#include "field/interaction.h"
#include "scene/environment.h"
namespace studio {
class InteractionInspector {
  public:
    void controls(const Environment *scene, const std::filesystem::path &dump, int area,
                  int selection);

  private:
    bool open_ = false, technical_ = false;
    std::string identity_, error_;
    InteractionInspection inspection_;
};
}
