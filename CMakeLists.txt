cmake_minimum_required(VERSION 3.20)
project(USUMStudio VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_library(studio_tutorials EXCLUDE_FROM_ALL src/tutorials/tutorial_state.cpp src/tutorials/tutorial_catalog.cpp)
target_include_directories(studio_tutorials PUBLIC src)

add_library(studio_core
  src/core/game_profile.cpp
  src/project/project_store.cpp
  src/audio/music.cpp src/audio/audio_document.cpp src/audio/sound_wave.cpp src/audio/cry.cpp src/audio/cry_document.cpp
  src/core/binary.cpp src/core/digest.cpp src/core/resource_source.cpp src/assets/pokemon_catalog.cpp src/formats/compression.cpp src/formats/container.cpp
  src/formats/amx.cpp src/field/interaction.cpp src/formats/texture.cpp src/formats/texture_codec.cpp src/formats/archive.cpp src/formats/model.cpp src/field/placement_document.cpp src/field/placements.cpp src/field/map_catalog.cpp src/formats/skinning.cpp)
add_library(studio_compiler EXCLUDE_FROM_ALL
  src/compiler/terrain.cpp src/compiler/asset.cpp src/compiler/motion.cpp src/compiler/model_build.cpp src/compiler/skinned.cpp src/compiler/box.cpp src/field/area.cpp src/field/character.cpp)
target_link_libraries(studio_compiler PUBLIC studio_core)
target_include_directories(studio_core PUBLIC src)
foreach(library studio_core studio_compiler)
if(MSVC)
  target_compile_definitions(${library} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(${library} PRIVATE /W4 /permissive-)
else()
  target_compile_options(${library} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
endforeach()
add_library(studio_scene src/field/encounter_document.cpp src/assets/pokemon_memory.cpp src/field/overworld_document.cpp src/field/pickup_document.cpp src/assets/pokemon_bundle.cpp src/scene/texture_uv.cpp src/assets/motion_exchange.cpp src/assets/face_materials.cpp src/field/map_creation.cpp src/field/weather_document.cpp src/field/warp_document.cpp src/field/camera_document.cpp src/field/collision_document.cpp src/field/collision_obj.cpp src/assets/refresh_regions.cpp src/assets/refresh_edit.cpp src/scene/refresh_surface.cpp src/assets/material_effect.cpp src/assets/material_document.cpp src/assets/map_model_document.cpp src/assets/model_document.cpp src/assets/pokemon_shadow.cpp src/assets/model_library.cpp src/assets/motion_table.cpp src/assets/clothing.cpp src/scene/clothing_colors.cpp src/assets/pokemon_settings.cpp src/scene/battle_scene.cpp src/scene/battle_sendout.cpp src/scene/environment.cpp src/scene/material.cpp src/scene/normals.cpp src/scene/lighting.cpp src/scene/animation.cpp src/scene/material_animation.cpp src/scene/skeleton.cpp src/scene/particles.cpp src/scene/spatial.cpp src/scene/player_assets.cpp src/scene/player.cpp src/assets/map_material_motion.cpp src/assets/material_motion.cpp src/assets/refresh_feeding_preview.cpp src/assets/refresh_feeding.cpp src/assets/mesh_geometry.cpp src/assets/model_exchange.cpp src/assets/blender_preview.cpp src/assets/model_resources.cpp src/assets/material_resources.cpp src/assets/mesh_selection.cpp src/images/image_format.cpp src/images/image_document.cpp src/assets/joint_motion_transform.cpp src/assets/skeletal_motion.cpp src/assets/skeleton_edit.cpp src/assets/visibility_motion.cpp)
target_link_libraries(studio_scene PUBLIC studio_core)
add_library(studio_map_authoring src/authoring/object_export.cpp src/authoring/ground_export.cpp src/authoring/resource_catalog.cpp src/authoring/tile_grid.cpp src/authoring/composition_document.cpp src/authoring/composition_preview.cpp src/authoring/ground_surface.cpp)
target_link_libraries(studio_map_authoring PUBLIC studio_scene studio_compiler)
include(cmake/NativeViewport.cmake)
