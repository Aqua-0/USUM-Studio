#pragma once
#include "assets/model_document.h"
#include <set>
namespace studio {
struct MaterialEffectResult {
    std::map<std::size_t, Bytes> members;
    std::vector<std::string> materials;
    std::string summary;
};
MaterialEffectResult borrow_material_effect(const ModelDocument &target,
                                            std::size_t target_material, const ModelDocument &donor,
                                            const std::vector<std::size_t> &donor_materials,
                                            bool keep_original = false);
MaterialEffectResult copy_model_meshes(const ModelDocument &target, const ModelDocument &donor,
                                       const std::set<std::size_t> &draws, unsigned bone);
}
