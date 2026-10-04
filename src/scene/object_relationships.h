#pragma once
#include "scene/environment.h"
namespace studio {
struct ObjectLink {
    int draw = -1, region = -1;
    std::string label;
    bool model = false, script = false, condition = false, destination = false, incoming = false;
};
int object_region(const Environment &scene, int draw);
int object_draw(const Environment &scene, int region);
std::vector<ObjectLink> object_relationships(const Environment &scene, int draw, int region);
}
