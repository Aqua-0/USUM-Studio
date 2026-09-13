#pragma once
#include "formats/model.h"
namespace studio {
struct BoxSize {
    float x = 100, y = 100, z = 100;
};
struct BoxBuild {
    Bytes model;
    std::string preview_obj;
    std::string material;
};
BoxBuild compile_box(const Model &donor, BoxSize size);
}
