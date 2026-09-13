#pragma once
#include "assets/model_document.h"
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
}
