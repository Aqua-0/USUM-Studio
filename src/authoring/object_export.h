#pragma once
#include "authoring/composition_document.h"
namespace studio {
Bytes compile_composition_objects(View placements, const CompositionDocument &document,
                                  const std::map<std::size_t, std::uint16_t> &models = {});
std::map<std::size_t, Bytes> compile_composition_resources(const std::filesystem::path &dump,
                                                           const CompositionDocument &document);
void export_composition_objects(const std::filesystem::path &dump,
                                const CompositionDocument &document,
                                const std::filesystem::path &output);
}
