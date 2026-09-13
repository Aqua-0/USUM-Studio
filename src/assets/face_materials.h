#pragma once
#include "formats/model.h"
#include <map>
#include <set>
namespace studio {
using MaterialFaces = std::map<std::size_t, std::set<std::size_t>>;
struct FaceMaterialEdit {
    Bytes bytes;
    std::vector<std::set<std::size_t>> sources;
    MaterialFaces selected;
};
std::vector<std::string> mesh_materials(View model);
FaceMaterialEdit assign_face_material(View model, const MaterialFaces &faces,
                                      const std::string &material);
}
