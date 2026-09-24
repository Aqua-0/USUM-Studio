#pragma once
#include "authoring/composition_document.h"
#include "assets/material_document.h"
#include "assets/asset_package.h"
namespace studio {
ModelDocument open_studio_object(const AssetPackage &package, const std::filesystem::path &dump);
AssetPackage new_studio_object(const ModelDocument &material_template, const ModelExchange &geometry,
                              const std::vector<std::size_t> &materials, const std::string &name);
Bytes export_studio_object(const MaterialDocument &document, const std::string &name);
Bytes export_asset_package(const std::filesystem::path &dump, const MapResourceCatalog &catalog,
                           const ProjectAsset &asset);
ProjectAsset import_asset_package(View bytes);
Bytes compile_project_asset(const std::filesystem::path &dump, const MapResourceCatalog &catalog,
                            const ProjectAsset &asset);
ModelDocument project_asset_studio(const std::filesystem::path &dump,
                                   const MapResourceCatalog &catalog, const ProjectAsset &asset,
                                   std::size_t selected_model = 0);
ProjectAsset save_project_asset_studio(const ProjectAsset &asset, const MaterialDocument &document);
Bytes compile_composition_objects(View placements, const CompositionDocument &document,
                                  const std::map<std::size_t, std::uint16_t> &models = {});
std::map<std::size_t, Bytes> compile_composition_resources(const std::filesystem::path &dump,
                                                           const CompositionDocument &document);
void export_composition_objects(const std::filesystem::path &dump,
                                const CompositionDocument &document,
                                const std::filesystem::path &output);
}
