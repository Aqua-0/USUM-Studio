#pragma once
#include "assets/material_document.h"
namespace studio {
using AssetPackage = std::map<std::string, Bytes>;
bool is_asset_package(View bytes);
Bytes encode_asset_package(const AssetPackage &package);
AssetPackage decode_asset_package(View bytes);
Bytes compile_static_package(const AssetPackage &package);
void add_package_model(AssetPackage &package, const MaterialDocument &document, std::size_t index);
AssetPackage model_asset_package(const MaterialDocument &document);
void import_model_asset_package(MaterialDocument &document, const AssetPackage &package);
}
