#pragma once
#include "native/renderer.h"
namespace studio {
struct MaterialSelection {
    int material = -1, draw = -1;
    bool focus = false, reveal_scene = false;
    char filter[128]{};
};
void material_inspector(const Environment *scene, const EnvironmentRenderer &renderer,
                        MaterialSelection &selection, const char *title = "Map materials");
}
