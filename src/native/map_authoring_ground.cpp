#include "native/tutorial_widgets.h"
#include "native/map_authoring_workspace.h"
#include "native/imgui_renderer.h"
#include "field/collision_surfaces.h"
#include "authoring/ground_export.h"
#include "authoring/object_export.h"
#include "field/warp_document.h"
#include "field/overworld_document.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace studio {
void MapAuthoringWorkspace::clear_ground_thumbnails() {
    for (auto &[key, texture] : ground_thumbnails_)
        if (bgfx::isValid(texture))
            bgfx::destroy(texture);
    ground_thumbnails_.clear();
}
void MapAuthoringWorkspace::set_stage(AuthoringStage stage) {
    if (busy() || preview_ || ground_handle_ || stage_ == stage)
        return;
    stage_ = stage;
    focus_library_ = stage != AuthoringStage::Review;
    ground_mode_ = stage != AuthoringStage::Objects;
    selected_ = 0;
    selecting_ground_ = false;
    if (stage == AuthoringStage::Terrain) {
        library_mode_ = 0;
        ground_transform_tool_ = Height;
    }
    if (stage == AuthoringStage::Surfaces) {
        library_mode_ = 0;
        ground_transform_tool_ = PaintBrush;
    }
    if (stage == AuthoringStage::Objects)
        library_mode_ = 1;
    if (inspect_)
        refresh_scene();
}
void MapAuthoringWorkspace::stage_toolbar() {
    const char *stages[] = {"1  Terrain", "2  Surfaces", "3  Objects", "4  Review"};
    ImGui::BeginDisabled(busy() || preview_ || ground_handle_);
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < 4; ++i) {
        if (i && ImGui::GetItemRectMax().x + 112 < right)
            ImGui::SameLine();
        const bool active = int(stage_) == i;
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (studio::TutorialWidgets::Button("map_authoring_ground", stages[i], {104, 28}))
            set_stage(AuthoringStage(i));
        if (active)
            ImGui::PopStyleColor();
    }
    const auto action = [&](const char *label, bool enabled, auto callback) {
        if (ImGui::GetItemRectMax().x + 65 < right)
            ImGui::SameLine();
        ImGui::BeginDisabled(!enabled);
        if (studio::TutorialWidgets::Button("map_authoring_ground", label, {55, 28}))
            try {
                callback();
                error_.clear();
            } catch (const std::exception &error) {
                error_ = error.what();
            }
        ImGui::EndDisabled();
    };
    action("Undo", document_->can_undo(), [&] {
        document_->undo();
        selected_ = 0;
        terrain_elements_.clear();
        terrain_selection_.clear();
        terrain_box_ = false;
        grid_edit_ = document_->grid();
        refresh_scene();
        status_ = "Last edit undone.";
    });
    action("Redo", document_->can_redo(), [&] {
        document_->redo();
        selected_ = 0;
        terrain_elements_.clear();
        terrain_selection_.clear();
        terrain_box_ = false;
        grid_edit_ = document_->grid();
        refresh_scene();
        status_ = "Edit restored.";
    });
    action("Save", true, [&] {
        save();
    });
    ImGui::EndDisabled();
    ImGui::Separator();
}
void MapAuthoringWorkspace::open_collision() {
    require(project_store(), "Open an editor project before customizing collision");
    require(document_ && document_->ground(), "Create authored ground first");
    require(!busy() && !preview_ && !ground_handle_ && !asset_edit_,
            "Finish the authoring operation first");
    auto faces = composition_collision(*document_);
    auto context = compose_map_preview(*base_, {{1, 0, {}}}, {{0, *composed_}});
    auto scene = authored_collision_scene(context, faces);
    collision_editor_.reset();
    collision_renderer_ = std::make_unique<EnvironmentRenderer>(shaders_);
    collision_renderer_->set_scene(scene);
    collision_renderer_->characters_enabled = false;
    collision_renderer_->sky_enabled = false;
    collision_renderer_->particles_enabled = false;
    collision_editor_ = std::make_unique<CollisionEditor>(window_, *collision_renderer_);
    collision_editor_->set_authored_scene(
        scene, document_->catalog().area, dump_,
        [this](std::vector<CollisionState> changed) {
            document_->customize_collision(std::move(changed));
        },
        [this] {
            return document_ && document_->dirty();
        });
    document_->customize_collision(std::move(faces));
    collision_request_ = true;
}
void MapAuthoringWorkspace::collision_tick(bool active) {
    if (collision_editor_) {
        collision_editor_->activate(active);
        collision_editor_->update();
    }
}
bool MapAuthoringWorkspace::draw_collision() {
    if (!collision_editor_)
        return true;
    collision_renderer_->upload_step();
    return collision_editor_->draw_workspace(!collision_renderer_->ready());
}
void MapAuthoringWorkspace::review_tools() {
    ImGui::SeparatorText("Export review");
    ImGui::Text("%zu added objects", document_->instances().size());
    if (document_->ground()) {
        std::size_t triangles = 0;
        for (const auto &draw : authoring_base_->draws)
            triangles += draw.indices.size() / 3;
        ImGui::Text("%zu triangles | %zu surfaces", triangles, authoring_base_->draws.size());
        const auto &ground = *document_->ground();
        const auto unpainted =
            std::count(ground.textures.begin(), ground.textures.end(), std::string{});
        if (unpainted)
            ImGui::Text("Paint the remaining %zu cells before export.", std::size_t(unpainted));
    }
    ImGui::SeparatorText("Game objects");
    bool objects = document_->stage_objects();
    if (studio::TutorialWidgets::Checkbox("map_authoring_ground", "Export objects", &objects))
        document_->object_export_settings(objects, document_->object_zone());
    try {
        auto zones = Container::parse(
            Container::parse(base_->placement_source, "ED").files.at(TargetProfile::static_pack),
            "ES");
        ImGui::SetNextItemWidth(-1);
        const auto label = "Zone group " + std::to_string(document_->object_zone());
        if (ImGui::BeginCombo("##object-zone", label.c_str())) {
            for (unsigned zone = 0; zone < zones.files.size(); ++zone) {
                const auto item = "Zone group " + std::to_string(zone) + " (" +
                                  std::to_string(read_placements(zones.files[zone]).size()) +
                                  " existing objects)";
                if (ImGui::Selectable(item.c_str(), zone == document_->object_zone()))
                    document_->object_export_settings(objects, zone);
            }
            ImGui::EndCombo();
        }
        if (document_->object_zone() < zones.files.size()) {
            std::size_t used = 0;
            for (unsigned zone = 0; zone < zones.files.size(); ++zone)
                for (auto &row : read_placements(zones.files[zone]))
                    used += row.alias != 0 || zone == document_->object_zone();
            ImGui::Text("Static actors: %zu / %zu", used + document_->instances().size(),
                        TargetProfile::actor_capacity);
        }
    } catch (const std::exception &e) {
        ImGui::TextWrapped("%s", e.what());
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Object resources and their textures, shaders and motions are included. "
                          "Edit fitted collision with each placement.");
    ImGui::SeparatorText("Ground export");
    bool enabled = document_->stage_ground();
    if (studio::TutorialWidgets::Checkbox("map_authoring_ground", "Export terrain", &enabled))
        document_->ground_export_settings(enabled, document_->ground_attribute());
    int mode = document_->replaces_terrain() ? 1 : 0;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##ground-export-mode", &mode, "Add ground patch\0Replace terrain\0"))
        document_->replace_terrain(mode == 1);
    if (mode == 1)
        ImGui::TextWrapped(
            "Replaces terrain and collision. Existing objects and entrances are kept.");
    ImGui::BeginDisabled(document_->custom_collision().has_value());
    ImGui::TextUnformatted("Ground surface");
    ImGui::SetNextItemWidth(-1);
    auto attribute = document_->ground_attribute();
    if (ImGui::BeginCombo("##ground-surface", collision_surface_name(attribute).c_str())) {
        std::set<std::uint32_t> attributes{attribute};
        for (auto &region : base_->spatial.regions)
            if (region.kind == SpatialKind::Ground)
                attributes.insert(region.attribute);
        for (auto value : attributes) {
            auto label = collision_surface_name(value) + " (" + std::to_string(value) + ")";
            if (ImGui::Selectable(label.c_str(), attribute == value))
                document_->ground_export_settings(enabled, value);
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::SeparatorText("Authored collision");
    ImGui::TextUnformatted(document_->custom_collision() ? "Custom collision"
                                                         : "Automatic collision");
    ImGui::TextWrapped("%s", document_->custom_collision()
                                 ? "Terrain edits preserve your collision."
                                 : "Collision follows the terrain.");
    ImGui::BeginDisabled(!project_store() || !document_->ground());
    if (studio::TutorialWidgets::Button(
            "map_authoring_ground",
            document_->custom_collision() ? "Edit collision" : "Customize collision", {-1, 0}))
        try {
            open_collision();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    ImGui::EndDisabled();
    if (!project_store())
        ImGui::TextWrapped("Open a project to customize collision.");
    if (document_->custom_collision() &&
        studio::TutorialWidgets::Button("map_authoring_ground", "Regenerate from terrain...",
                                        {-1, 0}))
        ImGui::OpenPopup("Replace custom collision?");
    if (ImGui::BeginPopupModal("Replace custom collision?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(
            "Replace custom triangle geometry and attributes with the current terrain?");
        ImGui::TextUnformatted(
            "Collision will follow terrain again. Authoring Undo can restore the custom version.");
        if (studio::TutorialWidgets::Button("map_authoring_ground", "Regenerate")) {
            document_->follow_terrain_collision();
            collision_editor_.reset();
            collision_renderer_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("map_authoring_ground", "Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!document_->replaces_terrain())
        ImGui::TextWrapped("Patch edges must meet the existing floor.");
    if (studio::TutorialWidgets::Button("map_authoring_ground", "Check export", {-1, 0})) {
        error_.clear();
        status_ = "Checking export...";
        std::string overworld_patch, warp_patch;
        auto entrance_dump = dump_;
        if (auto *store = project_store()) {
            entrance_dump = store->source;
            save_editor_project();
            const auto area = std::to_string(document_->catalog().area);
            if (const auto file = store->document("overworld/" + area); !file.empty())
                overworld_patch = text(read_file(file));
            if (const auto file = store->document("warps/" + area); !file.empty())
                warp_patch = text(read_file(file));
        }
        review_job_ = std::async(std::launch::async, [dump = dump_, document = *document_,
                                                      entrance_dump, overworld_patch, warp_patch] {
            require(document.stage_objects() || document.stage_ground(),
                    "Enable ground or objects to check their export.");
            if (document.stage_objects())
                compile_composition_resources(dump, document);
            std::string result = "Export compilation passed. Save and stage the enabled changes.";
            if (document.stage_ground()) {
                auto ground = compile_ground_patch(dump, document, document.ground_attribute());
                if (document.replaces_terrain()) {
                    auto terrain = Container::parse(ground.compiled);
                    SpatialScene collision;
                    decode_collision_mesh(collision,
                                          terrain.files.at(TargetProfile::terrain_ground_slot),
                                          SpatialKind::Ground, "Ground");
                    const auto area = document.catalog().area;
                    Archive field(entrance_dump / GameProfile::field_archive(entrance_dump));
                    const auto base = area * TargetProfile::area_stride;
                    const auto original = field.decoded(base + TargetProfile::placement_slot);
                    auto adjusted = original;
                    if (!overworld_patch.empty()) {
                        OverworldDocument actors(
                            area, original,
                            field.decoded(base + TargetProfile::character_resource_slot),
                            field.decoded(base + TargetProfile::static_resource_slot));
                        actors.restore(overworld_patch);
                        adjusted = actors.placements();
                    }
                    if (!warp_patch.empty()) {
                        WarpDocument edited(area, original);
                        edited.restore(warp_patch);
                        adjusted = merge_project_resource(original, adjusted, edited.compile(),
                                                          "Entrance review");
                    }
                    WarpDocument warps(area, std::move(adjusted));
                    for (unsigned i = 0; i < warps.records().size(); ++i) {
                        const auto &record = warps.records()[i];
                        const auto arrival = warps.values(i).arrival;
                        auto floor = pick_authoring_ground(
                            collision, {arrival[0], 1e7f, arrival[2]}, {0, -1, 0});
                        if (!floor || std::abs((*floor)[1] - arrival[1]) > 5)
                            result += "\nCheck arrival for zone " + std::to_string(record.zone) +
                                      ", entrance " + std::to_string(record.event) +
                                      ": it does not meet the replacement ground. Update it in "
                                      "Maps > Spatial.";
                    }
                }
            }
            return result;
        });
    }
    if (studio::TutorialWidgets::Button("map_authoring_ground", "Save Project", {-1, 0})) {
        try {
            if (!save_editor_project())
                file_dialog(true);
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    }
    ImGui::TextWrapped("Ctrl+Shift+T stages enabled ground and object changes. Reload the staged "
                       "overlay in Maps to inspect it with the surrounding scenery and player. "
                       "Build game export creates the files for your game.");
    if (studio::TutorialWidgets::Button("map_authoring_ground", "Continue shaping", {-1, 0}))
        set_stage(AuthoringStage::Terrain);
    if (studio::TutorialWidgets::Button("map_authoring_ground", "Continue painting", {-1, 0}))
        set_stage(AuthoringStage::Surfaces);
}
void MapAuthoringWorkspace::ground_toolbar() {
    const char *names[] = {"Height: raise or lower selected cells",
                           "Slope: tilt selected cells",
                           "Vertices: move shared points",
                           "Texture size: adjust repetition",
                           "Extend: grow an outer edge",
                           "Region: paint textures and directional blends",
                           "Select: choose a rectangular region",
                           "Sculpt: soft height brush",
                           "Paint: brush a base texture",
                           "Blend: brush second-layer coverage",
                           "Mesh: subdivide, move edges, extrude and simplify"};
    const char *labels[] = {"Height", "Slope",  "Vertices", "Size",  "Extend", "Region",
                            "Select", "Sculpt", "Paint",    "Blend", "Mesh"};
    const std::vector<int> tools =
        stage_ == AuthoringStage::Surfaces
            ? std::vector<int>{PaintBrush, BlendBrush, TextureSize, RegionPaint, Select}
            : std::vector<int>{Select, Height, Slope, Vertices, Sculpt, Extend, Mesh};
    ImGui::BeginDisabled(busy() || ground_handle_);
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (int tool : tools) {
        const float width = ImGui::CalcTextSize(labels[tool]).x + 42;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= right)
            ImGui::SameLine();
        ImGui::PushID(tool);
        const bool selected = ground_transform_tool_ == tool;
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (studio::TutorialWidgets::Button("map_authoring_ground", "##terrain-tool",
                                            {width, 30})) {
            ground_transform_tool_ = tool;
            vertex_delta_ = {};
            if (tool == TextureSize && tile_)
                texture_size_ =
                    document_->ground()->texture_scales.empty()
                        ? std::array<float, 2>{1, 1}
                        : document_->ground()
                              ->texture_scales[std::size_t(tile_->z) * document_->grid().width +
                                               tile_->x];
        }
        if (selected)
            ImGui::PopStyleColor();
        const auto p = ImGui::GetItemRectMin();
        auto *draw = ImGui::GetWindowDrawList();
        const auto color = ImGui::GetColorU32(ImGuiCol_Text);
        if (selected)
            draw->AddRect({p.x + 1, p.y + 1}, {p.x + width - 1, p.y + 29},
                          ImGui::GetColorU32(ImGuiCol_CheckMark), 3, 0, 2);
        const auto line = [&](float x, float y, float u, float v) {
            draw->AddLine({p.x + x, p.y + y}, {p.x + u, p.y + v}, color, 1.8f);
        };
        if (tool == 0) {
            line(17, 4, 17, 24);
            line(12, 9, 17, 4);
            line(22, 9, 17, 4);
            line(12, 19, 17, 24);
            line(22, 19, 17, 24);
        }
        if (tool == 1) {
            for (int i = 0; i < 24; ++i) {
                const float a = i * 6.2831853f / 24, b = (i + 1) * 6.2831853f / 24;
                line(17 + 11 * std::cos(a), 14 + 6 * std::sin(a), 17 + 11 * std::cos(b),
                     14 + 6 * std::sin(b));
            }
            line(17, 4, 17, 24);
        }
        if (tool == 2) {
            line(13, 18, 13, 5);
            line(13, 18, 28, 18);
            line(13, 18, 6, 24);
            draw->AddCircleFilled({p.x + 13, p.y + 18}, 3, color);
            line(10, 8, 13, 5);
            line(25, 15, 28, 18);
        }
        if (tool == 3) {
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 3; ++x)
                    if ((x + y) % 2 == 0)
                        draw->AddRectFilled({p.x + 7 + x * 7, p.y + 4 + y * 7},
                                            {p.x + 14 + x * 7, p.y + 11 + y * 7}, color);
        }
        if (tool == 4) {
            draw->AddRect({p.x + 4, p.y + 8}, {p.x + 14, p.y + 23}, color);
            line(14, 8, 24, 8);
            line(24, 8, 24, 23);
            line(24, 23, 14, 23);
            line(17, 4, 29, 4);
            line(25, 1, 29, 4);
            line(25, 7, 29, 4);
        }
        if (tool == 5) {
            line(11, 19, 23, 5);
            line(15, 21, 27, 7);
            draw->AddTriangleFilled({p.x + 11, p.y + 18}, {p.x + 16, p.y + 22}, {p.x + 6, p.y + 25},
                                    color);
        }

        if (tool == Mesh) {
            line(6, 23, 17, 5);
            line(17, 5, 28, 23);
            line(28, 23, 6, 23);
            line(17, 5, 17, 23);
        }
        if (tool == Select) {
            draw->AddRect({p.x + 7, p.y + 6}, {p.x + 25, p.y + 23}, color, 0, 0, 1.5f);
            line(11, 3, 11, 9);
            line(4, 12, 10, 12);
        }
        if (tool == Sculpt || tool == PaintBrush || tool == BlendBrush) {
            draw->AddCircle({p.x + 16, p.y + 16}, 9, color, 24, 1.5f);
            line(16, 4, 16, 19);
            line(12, 8, 16, 4);
            line(20, 8, 16, 4);
            if (tool == BlendBrush)
                draw->AddCircleFilled({p.x + 16, p.y + 16}, 4, color);
        }
        draw->AddText({p.x + 33, p.y + 7}, color, labels[tool]);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", names[tool]);
        ImGui::PopID();
    }
    ImGui::EndDisabled();
}
void MapAuthoringWorkspace::ground_tools() {
    const auto run = [&](auto action) {
        try {
            action();
            error_.clear();
        } catch (const std::exception &error) {
            error_ = error.what();
        }
    };
    if (ground_handle_ == 20) {
        ImGui::SeparatorText("Simplification preview");
        ImGui::Text("%zu -> %zu triangles", document_->ground()->triangles.size(),
                    ground_drag_preview_->triangles.size());
        if (studio::TutorialWidgets::Button("map_authoring_ground", "Apply simplification",
                                            {-1, 0}))
            run([&] {
                document_->edit_terrain(*ground_drag_preview_);
                ground_handle_ = 0;
                ground_drag_preview_.reset();
                refresh_scene();
            });
        if (studio::TutorialWidgets::Button("map_authoring_ground", "Cancel simplification",
                                            {-1, 0}))
            cancel_ground_drag();
        return;
    }
    ImGui::BeginDisabled(busy() || preview_ || ground_handle_);
    if (!document_->ground() || (stage_ == AuthoringStage::Terrain &&
                                 studio::TutorialWidgets::CollapsingHeader(
                                     "map_authoring_ground", "Replace ground surface"))) {
        ImGui::SeparatorText("Create flat ground");
        ImGui::TextUnformatted("Width / depth (cells)");
        int dimensions[]{new_ground_grid_.width, new_ground_grid_.height};
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputInt2("##ground-dimensions", dimensions)) {
            new_ground_grid_.width = dimensions[0];
            new_ground_grid_.height = dimensions[1];
        }
        ImGui::TextUnformatted("Tile size");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputFloat("##ground-tile-size", &new_ground_grid_.tile_size);
        ImGui::TextUnformatted("Origin X / Z");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputFloat2("##ground-origin", new_ground_grid_.origin.data());
        ImGui::TextUnformatted("Starting height");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputFloat("##ground-start-height", &new_ground_height_);
        if (studio::TutorialWidgets::Button(
                "map_authoring_ground",
                document_->ground() ? "Replace with flat ground" : "Create flat ground", {-1, 0}))
            run([&] {
                document_->create_ground(new_ground_grid_, new_ground_height_);
                terrain_elements_.clear();
                terrain_selection_.clear();
                tile_.reset();
                region_end_.reset();
                selected_ = 0;
                refresh_scene();
                map_camera_.fit(authoring_base_->low, authoring_base_->high);
                ground_ceiling_ = authoring_base_->high[1] + 100000;
                status_ = "Flat ground created. Select cells and choose a texture from the Asset "
                          "Library.";
            });
        ImGui::TextWrapped("Creates a new surface; source map geometry is hidden. Existing added "
                           "objects keep their positions. Replacing ground can be undone.");
    }
    if (document_->ground()) {
        const auto &grid = document_->grid();
        std::size_t vertices = 0, triangles = 0;
        for (const auto &draw : authoring_base_->draws) {
            vertices += draw.vertices.size();
            triangles += draw.indices.size() / 3;
        }
        ImGui::SeparatorText(stage_ == AuthoringStage::Surfaces ? "Surface editing"
                                                                : "Terrain editing");
        ImGui::Text("%d x %d cells | %.1f cell size", grid.width, grid.height, grid.tile_size);
        ImGui::Text("%zu vertices | %zu triangles", vertices, triangles);
        if (studio::TutorialWidgets::Button("map_authoring_ground", "Select all cells")) {
            tile_ = AuthoringTile{0, 0};
            region_end_ = AuthoringTile{grid.width - 1, grid.height - 1};
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("map_authoring_ground", "Clear selection")) {
            tile_.reset();
            region_end_.reset();
            ground_vertex_.reset();
            terrain_elements_.clear();
            terrain_selection_.clear();
        }
        if (tile_) {
            const auto last = region_end_.value_or(*tile_);
            ImGui::Text("Selected: %d cells",
                        (std::abs(last.x - tile_->x) + 1) * (std::abs(last.z - tile_->z) + 1));
        } else
            ImGui::TextDisabled("No cell selection");
        if (ground_transform_tool_ == Mesh ||
            (ground_transform_tool_ == Vertices && !document_->ground()->triangles.empty())) {
            ImGui::SeparatorText("Connected mesh");
            ImGui::TextWrapped(
                "Choose Vertices, Edges or Faces above the view. Click selects; drag a box around "
                "whole components. Ctrl adds to a box or toggles a clicked component. Vertex and "
                "edge selection sees through the mesh.");
            if (studio::TutorialWidgets::CollapsingHeader("map_authoring_ground",
                                                          "Subdivide selected cells")) {
                ImGui::SetNextItemWidth(-1);
                ImGui::Combo("##subdivision-method", &terrain_subdivision_,
                             "Even triangles\0Longest edge\0Face centers\0");
                const char *subdivision_help[] = {
                    "Four similar triangles per face. Regular tiles become four smaller squares; "
                    "borders connect to coarser neighbors.",
                    "Split the longest edge of each selected face. Adds detail gradually and helps "
                    "shorten stretched triangles.",
                    "Add a point inside each face, making three triangles. Keeps existing edges; "
                    "useful for local peaks and depressions."};
                ImGui::TextWrapped("%s", subdivision_help[terrain_subdivision_]);
                ImGui::BeginDisabled(!tile_);
                if (studio::TutorialWidgets::Button("map_authoring_ground",
                                                    "Subdivide selected cells", {-1, 0}))
                    run([&] {
                        document_->edit_terrain(subdivide_terrain(
                            grid, *document_->ground(), *tile_, region_end_.value_or(*tile_),
                            TerrainSubdivision(terrain_subdivision_)));
                        refresh_scene();
                    });
                ImGui::EndDisabled();
            }

            ImGui::Text("Selected: %zu %s | %zu vertices", terrain_elements_.size(),
                        terrain_selection_mode_ == 0   ? "vertices"
                        : terrain_selection_mode_ == 1 ? "edges"
                                                       : "faces",
                        terrain_selection_.size());
            ImGui::TextUnformatted("Move / extrude X, Y, Z");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputFloat3("##mesh-delta", terrain_delta_.data());
            ImGui::SetNextItemWidth(145);
            ImGui::SliderFloat("Influence (cells)", &terrain_influence_, 0, 8, "%.1f");
            studio::TutorialWidgets::Checkbox("map_authoring_ground", "Snap arrow movement",
                                              &terrain_snap_);
            if (terrain_snap_) {
                ImGui::SetNextItemWidth(145);
                ImGui::InputFloat("Step", &ground_step_);
                if (!std::isfinite(ground_step_) || ground_step_ <= 0)
                    ImGui::TextWrapped("Use a positive finite step to enable snapping.");
            }
            ImGui::BeginDisabled(terrain_selection_.empty());
            const bool move =
                studio::TutorialWidgets::Button("map_authoring_ground", "Move selection", {-1, 0});
            ImGui::EndDisabled();
            if (move)
                run([&] {
                    document_->edit_terrain(
                        move_terrain_points(grid, *document_->ground(), terrain_selection_,
                                            terrain_delta_, terrain_influence_ * grid.tile_size));
                    refresh_scene();
                });
            const auto mesh_faces = terrain_triangles(grid, *document_->ground());
            unsigned edge_faces = 0;
            if (terrain_selection_.size() == 2)
                for (const auto &face : mesh_faces)
                    if (std::find(face.vertices.begin(), face.vertices.end(),
                                  terrain_selection_[0]) != face.vertices.end() &&
                        std::find(face.vertices.begin(), face.vertices.end(),
                                  terrain_selection_[1]) != face.vertices.end())
                        ++edge_faces;
            ImGui::BeginDisabled(edge_faces != 1);
            const bool extrude = studio::TutorialWidgets::Button("map_authoring_ground",
                                                                 "Extrude boundary edge", {-1, 0});
            ImGui::EndDisabled();
            if (extrude)
                run([&] {
                    const auto next =
                        extrude_terrain_edge(grid, *document_->ground(), terrain_selection_[0],
                                             terrain_selection_[1], terrain_delta_);
                    const auto count = std::uint32_t(next.heights.size() + next.points.size());
                    document_->edit_terrain(next);
                    terrain_selection_mode_ = 1;
                    terrain_elements_ = {{{count - 2, count - 1, count - 1}}};
                    terrain_selection_ = {count - 2, count - 1};
                    refresh_scene();
                });
            if (edge_faces != 1)
                ImGui::TextDisabled("Extrude: select one outer boundary edge.");
            if (studio::TutorialWidgets::CollapsingHeader("map_authoring_ground",
                                                          "Shape and simplify selected cells")) {
                studio::TutorialWidgets::Checkbox("map_authoring_ground", "Keep region boundary",
                                                  &terrain_lock_boundary_);
                ImGui::SetNextItemWidth(145);
                ImGui::InputFloat("Level", &ground_level_);
                ImGui::SetNextItemWidth(145);
                ImGui::SliderFloat("Smooth amount", &terrain_smooth_, 0, 1);
                ImGui::BeginDisabled(!tile_);
                const bool flatten = studio::TutorialWidgets::Button("map_authoring_ground",
                                                                     "Flatten mesh"),
                           smooth = (ImGui::SameLine(), studio::TutorialWidgets::Button(
                                                            "map_authoring_ground", "Smooth mesh"));
                ImGui::EndDisabled();
                if (flatten || smooth)
                    run([&] {
                        document_->edit_terrain(shape_terrain_selection(
                            grid, *document_->ground(), *tile_, region_end_.value_or(*tile_),
                            smooth, smooth ? terrain_smooth_ : ground_level_,
                            terrain_lock_boundary_));
                        refresh_scene();
                    });
                ImGui::SetNextItemWidth(145);
                ImGui::InputFloat("Simplify tolerance", &terrain_tolerance_);
                ImGui::BeginDisabled(!tile_ || document_->ground()->triangles.empty());
                const bool simplify = studio::TutorialWidgets::Button(
                    "map_authoring_ground", "Preview simplification", {-1, 0});
                ImGui::EndDisabled();
                if (simplify)
                    run([&] {
                        ground_drag_preview_ = simplify_terrain_selection(
                            grid, *document_->ground(), *tile_, region_end_.value_or(*tile_),
                            terrain_tolerance_);
                        ground_handle_ = 20;
                        refresh_scene();
                    });
                ImGui::TextWrapped(
                    "Simplification removes interior detail from nearly planar patches. Region "
                    "boundaries, blended cells and texture seams stay in place.");
            }
        }
        if (ground_transform_tool_ == Select) {
            ImGui::SeparatorText("Select region");
            ImGui::TextWrapped("Drag across the ground to select a rectangle. Ctrl-click extends "
                               "it. Selection is shared by terrain and surface tools.");
            if (studio::TutorialWidgets::Button("map_authoring_ground", "Read current selection") &&
                tile_) {
                const auto last = region_end_.value_or(*tile_);
                selection_start_ = {std::min(tile_->x, last.x), std::min(tile_->z, last.z)};
                selection_size_ = {std::abs(tile_->x - last.x) + 1,
                                   std::abs(tile_->z - last.z) + 1};
            }
            ImGui::TextUnformatted("Start column / row (from 0)");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputInt2("##selection-start", selection_start_.data());
            ImGui::TextUnformatted("Width / depth (cells)");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputInt2("##selection-size", selection_size_.data());
            if (studio::TutorialWidgets::Button("map_authoring_ground", "Select these cells",
                                                {-1, 0}))
                run([&] {
                    require(selection_start_[0] >= 0 && selection_start_[1] >= 0 &&
                                selection_start_[0] < grid.width &&
                                selection_start_[1] < grid.height && selection_size_[0] > 0 &&
                                selection_size_[1] > 0 &&
                                selection_size_[0] <= grid.width - selection_start_[0] &&
                                selection_size_[1] <= grid.height - selection_start_[1],
                            "Selection must fit inside the terrain grid");
                    tile_ = AuthoringTile{selection_start_[0], selection_start_[1]};
                    region_end_ = AuthoringTile{selection_start_[0] + selection_size_[0] - 1,
                                                selection_start_[1] + selection_size_[1] - 1};
                });
        }
        if (ground_transform_tool_ >= Sculpt && ground_transform_tool_ <= BlendBrush) {
            ImGui::SeparatorText(ground_transform_tool_ == Sculpt       ? "Sculpt brush"
                                 : ground_transform_tool_ == PaintBrush ? "Base texture brush"
                                                                        : "Blend brush");
            if (ground_transform_tool_ == Sculpt) {
                ImGui::SetNextItemWidth(-1);
                ImGui::Combo("##sculpt-mode", &sculpt_mode_, "Raise\0Lower\0Level\0");
                ImGui::TextUnformatted(sculpt_mode_ == 2 ? "Target height" : "Height per stroke");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat("##brush-height",
                                  sculpt_mode_ == 2 ? &ground_level_ : &ground_step_, 5, 25);
            } else {
                ImGui::TextWrapped(
                    "Surface: %s",
                    ground_texture_ < 0
                        ? "Unpainted"
                        : ground_textures_.at(std::size_t(ground_texture_)).name.c_str());
                if (studio::TutorialWidgets::Button("map_authoring_ground", "Choose surface..."))
                    ImGui::SetWindowFocus("Asset Library");
            }
            ImGui::TextUnformatted("Radius (cells)");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat(
                "##brush-radius", &brush_radius_, .5f,
                std::max(8.f, float(std::max(document_->grid().width, document_->grid().height))),
                "%.1f");
            if (ground_transform_tool_ != PaintBrush) {
                ImGui::TextUnformatted("Hardness");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##brush-hardness", &brush_hardness_, 0, 1, "%.2f");
            }
            if (ground_transform_tool_ == BlendBrush) {
                ImGui::TextUnformatted("Target coverage");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##brush-coverage", &ground_coverage_, 0, 1, "%.2f");
                ImGui::TextWrapped("Paint a base first. Blends use shared corner weights, so small "
                                   "details depend on cell spacing. A different second texture "
                                   "replaces the old layer in touched cells.");
            }
            studio::TutorialWidgets::Checkbox("map_authoring_ground", "Limit to selected region",
                                              &brush_selection_);
            ImGui::TextWrapped("Drag to brush. Each stroke is one undo step; Escape or focus loss "
                               "cancels it. Shift-drag orbits.");
            if (ground_transform_tool_ == Sculpt)
                ImGui::TextWrapped(
                    "Alt reverses Raise / Lower. A stroke applies its height once, even when you "
                    "pause or cross the same point. Existing objects keep their positions.");
            if (ground_transform_tool_ == PaintBrush)
                ImGui::TextWrapped("Paints whole cells under the circular brush and clears their "
                                   "second layer. Use Blend for soft transitions.");
        }
        if (ground_transform_tool_ == 2 && document_->ground()->triangles.empty()) {
            ImGui::SeparatorText("Move vertex");
            ImGui::TextWrapped("Click a ground point, then drag its X, Y or Z arrow. Shared cells "
                               "follow that point. Shift-drag still orbits.");
            if (ground_vertex_) {
                auto point =
                    ground_vertex(grid, *document_->ground(), ground_vertex_->x, ground_vertex_->z);
                ImGui::Text("Vertex %d, %d", ground_vertex_->x, ground_vertex_->z);
                ImGui::Text("Position: %.1f, %.1f, %.1f", point[0], point[1], point[2]);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat3("##vertex-delta", vertex_delta_.data());
                if (studio::TutorialWidgets::Button("map_authoring_ground", "Move by X / Y / Z",
                                                    {-1, 0}))
                    run([&] {
                        document_->move_vertex(*ground_vertex_, vertex_delta_);
                        vertex_delta_ = {};
                        refresh_scene();
                        ground_ceiling_ = authoring_base_->high[1] + 100000;
                    });
            }
        } else if (ground_transform_tool_ == 3) {
            ImGui::SeparatorText("Texture size");
            ImGui::TextWrapped("Size in tiles for one texture repeat. Larger values make the "
                               "texture appear larger. Both blend layers use the same size.");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputFloat2("##texture-size", texture_size_.data());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Width (U) / depth (V), in tiles per repeat");
            ImGui::BeginDisabled(!tile_);
            if (studio::TutorialWidgets::Button("map_authoring_ground", "Apply texture size",
                                                {-1, 0}))
                run([&] {
                    document_->scale_ground_texture(*tile_, region_end_.value_or(*tile_),
                                                    texture_size_);
                    refresh_scene();
                });
            ImGui::EndDisabled();
        } else if (ground_transform_tool_ == 4) {
            ImGui::SeparatorText("Extend terrain");
            ImGui::TextWrapped(
                "Add a connected strip along an entire outer edge. New cells inherit edge heights, "
                "textures and blend coverage. Existing vertices and objects stay in place.");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputInt("##extension-count", &extension_cells_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Number of new rows or columns");
            const char *edges[] = {"Extend +X", "Extend +Z", "Extend -X", "Extend -Z"};
            for (int edge = 0; edge < 4; ++edge)
                if (studio::TutorialWidgets::Button("map_authoring_ground", edges[edge], {-1, 0}))
                    run([&] {
                        document_->extend_ground(edge, extension_cells_);
                        tile_.reset();
                        region_end_.reset();
                        ground_vertex_.reset();
                        terrain_elements_.clear();
                        terrain_selection_.clear();
                        refresh_scene();
                        map_camera_.fit(authoring_base_->low, authoring_base_->high);
                        status_ = "Terrain extended. Undo restores the previous edge.";
                    });
        }
        if (ground_transform_tool_ == 5) {
            ImGui::SeparatorText("Paint ground");
            ImGui::TextWrapped(
                "Paint with: %s",
                ground_texture_ < 0
                    ? "Unpainted"
                    : ground_textures_.at(std::size_t(ground_texture_)).name.c_str());
            if (studio::TutorialWidgets::Button("map_authoring_ground", "Choose texture..."))
                ImGui::SetWindowFocus("Asset Library");
            ImGui::BeginDisabled(!tile_);
            if (studio::TutorialWidgets::Button("map_authoring_ground", "Paint selected cells",
                                                {-1, 0}))
                run([&] {
                    document_->paint_ground(
                        *tile_, region_end_.value_or(*tile_),
                        ground_texture_ < 0
                            ? ""
                            : ground_textures_.at(std::size_t(ground_texture_)).key);
                    refresh_scene();
                    status_ = "Ground texture painted. Heights were kept.";
                });
            if (studio::TutorialWidgets::CollapsingHeader("map_authoring_ground",
                                                          "Blend a second texture")) {
                ImGui::TextWrapped(
                    "Second layer: %s",
                    ground_blend_texture_ < 0
                        ? "Choose a texture"
                        : ground_textures_.at(std::size_t(ground_blend_texture_)).name.c_str());
                if (studio::TutorialWidgets::Button("map_authoring_ground",
                                                    "Use chosen texture as second layer"))
                    ground_blend_texture_ = ground_texture_;
                ImGui::SliderFloat("Coverage", &ground_coverage_, 0, 1, "%.2f");
                ImGui::Combo(
                    "Transition", &ground_blend_direction_,
                    "Even mix\0Fade toward +X\0Fade toward +Z\0Fade toward -X\0Fade toward -Z\0");
                ImGui::BeginDisabled(ground_blend_texture_ < 0);
                if (studio::TutorialWidgets::Button("map_authoring_ground", "Blend selected cells",
                                                    {-1, 0}))
                    run([&] {
                        document_->blend_ground(
                            *tile_, region_end_.value_or(*tile_),
                            ground_textures_.at(std::size_t(ground_blend_texture_)).key,
                            ground_coverage_, ground_blend_direction_ - 1);
                        refresh_scene();
                        status_ = "Second texture blended across the selection.";
                    });
                ImGui::EndDisabled();
                if (studio::TutorialWidgets::Button("map_authoring_ground", "Remove second layer",
                                                    {-1, 0}))
                    run([&] {
                        document_->blend_ground(*tile_, region_end_.value_or(*tile_), "", 0);
                        refresh_scene();
                    });
                ImGui::TextWrapped("Paint a base first, then blend another texture. Fades span the "
                                   "selected region; neighboring regions keep their own paint.");
            }
            ImGui::EndDisabled();
        }
        if (ground_transform_tool_ == 0 || ground_transform_tool_ == 1) {
            ImGui::BeginDisabled(!tile_);
            if (ground_transform_tool_ == 0) {
                ImGui::SeparatorText("Height");
                ImGui::TextUnformatted("Height step");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat("##ground-height-step", &ground_step_, 5, 25);
                ImGui::TextUnformatted("Neighbor influence (cells)");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##height-influence", &ground_influence_cells_, 0,
                                   float(std::max(grid.width, grid.height)), "%.1f");
                ImGui::TextWrapped("Hold a height arrow and scroll to change influence. Selected "
                                   "points move fully; surrounding points fade toward the outer "
                                   "circle. Zero affects only the selection.");
                const auto shape = [&](float value, bool flatten) {
                    run([&] {
                        require(flatten || (std::isfinite(ground_step_) && ground_step_ > 0),
                                "Height step must be finite and positive");
                        if (flatten)
                            document_->shape_ground(*tile_, region_end_.value_or(*tile_), value,
                                                    true);
                        else
                            document_->influence_ground_height(*tile_, region_end_.value_or(*tile_),
                                                               value, ground_influence_cells_);
                        refresh_scene();
                        ground_ceiling_ = authoring_base_->high[1] + 100000;
                        status_ = "Ground heights changed. Added objects keep their positions.";
                    });
                };
                if (studio::TutorialWidgets::Button("map_authoring_ground", "Raise", {100, 0}))
                    shape(ground_step_, false);
                ImGui::SameLine();
                if (studio::TutorialWidgets::Button("map_authoring_ground", "Lower", {100, 0}))
                    shape(-ground_step_, false);
                ImGui::TextUnformatted("Flatten to height");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat("##ground-flatten-height", &ground_level_);
                if (studio::TutorialWidgets::Button("map_authoring_ground",
                                                    "Flatten selected cells", {-1, 0}))
                    shape(ground_level_, true);
            } else {
                ImGui::SeparatorText("Slope");
                ImGui::TextUnformatted("Slope step (degrees)");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat("##ground-tilt-step", &ground_tilt_step_, 1, 5);
                const auto tilt = [&](float x, float z) {
                    run([&] {
                        require(std::isfinite(ground_tilt_step_) && ground_tilt_step_ > 0,
                                "Slope step must be positive");
                        document_->tilt_ground(*tile_, region_end_.value_or(*tile_), x, z);
                        refresh_scene();
                        ground_ceiling_ = authoring_base_->high[1] + 100000;
                    });
                };
                if (studio::TutorialWidgets::Button("map_authoring_ground", "Tilt X -"))
                    tilt(-ground_tilt_step_, 0);
                ImGui::SameLine();
                if (studio::TutorialWidgets::Button("map_authoring_ground", "Tilt X +"))
                    tilt(ground_tilt_step_, 0);
                if (studio::TutorialWidgets::Button("map_authoring_ground", "Tilt Z -"))
                    tilt(0, -ground_tilt_step_);
                ImGui::SameLine();
                if (studio::TutorialWidgets::Button("map_authoring_ground", "Tilt Z +"))
                    tilt(0, ground_tilt_step_);
            }
            ImGui::TextWrapped("Choose Height or Slope above the viewport. Shift-drag orbits; hold "
                               "Shift after grabbing a handle to snap. Escape cancels the edit.");
            ImGui::EndDisabled();
        }
        if (studio::TutorialWidgets::CollapsingHeader("map_authoring_ground",
                                                      "Mesh and export notes")) {
            ImGui::TextWrapped("Cells start with two triangles; Mesh adds local detail. Shared "
                               "corners keep neighboring cells joined and form slopes around "
                               "raised regions. Material boundaries can add vertices.");
            ImGui::TextWrapped("Ground picking and object snapping use this surface. Review can "
                               "stage supported ground patches with matching game collision.");
        }
    }
    ImGui::EndDisabled();
}
void MapAuthoringWorkspace::ground_browser() {
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##ground-texture-search", "Search surfaces", ground_search_,
                             sizeof(ground_search_));
    if (ImGui::Selectable("Unpainted ground", ground_texture_ < 0) && !busy() && !preview_ &&
        !ground_handle_)
        ground_texture_ = -1;
    const auto lower = [](std::string text) {
        for (auto &c : text)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    const auto filter = lower(ground_search_);
    std::vector<std::size_t> matches;
    for (std::size_t i = 0; i < ground_textures_.size(); ++i)
        if (lower(ground_textures_[i].name).find(filter) != std::string::npos)
            matches.push_back(i);
    ImGui::TextDisabled("%zu surfaces | largest first", matches.size());
    ImGui::BeginChild("Ground texture palette", {0, 0}, ImGuiChildFlags_Borders);
    const int columns = std::max(1, int(ImGui::GetContentRegionAvail().x / 105));
    if (ImGui::BeginTable("Surface cards", columns, ImGuiTableFlags_SizingStretchSame)) {
        for (auto i : matches) {
            ImGui::TableNextColumn();
            const auto &item = ground_textures_[i];
            ImGui::PushID(int(i));
            const auto p = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const bool visible = ImGui::IsRectVisible({width, 112});
            if (ImGui::InvisibleButton("##surface-card", {width, 112}) && !busy() && !preview_ &&
                !ground_handle_) {
                ground_texture_ = int(i);
                if (stage_ != AuthoringStage::Surfaces)
                    set_stage(AuthoringStage::Surfaces);
            }
            const bool hovered = ImGui::IsItemHovered();
            auto *draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(p, {p.x + width, p.y + 108},
                                ImGui::GetColorU32(ground_texture_ == int(i) ? ImGuiCol_ButtonActive
                                                                             : ImGuiCol_FrameBg),
                                4);
            bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
            auto found = ground_thumbnails_.find(item.key);
            if (found != ground_thumbnails_.end())
                texture = found->second;
            else if (visible) {
                const auto &source = base_->textures.at(item.texture);
                Bytes pixels(64 * 64 * 4);
                for (unsigned y = 0; y < 64; ++y)
                    for (unsigned x = 0; x < 64; ++x) {
                        const auto from = (std::size_t(y * source.height / 64) * source.width +
                                           x * source.width / 64) *
                                          4;
                        std::copy_n(source.rgba.data() + from, 4, pixels.data() + (y * 64 + x) * 4);
                    }
                texture = bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::RGBA8, 0,
                                                bgfx::copy(pixels.data(), narrow(pixels.size())));
                if (bgfx::isValid(texture))
                    ground_thumbnails_.emplace(item.key, texture);
            }
            if (bgfx::isValid(texture))
                draw->AddImage(ImTextureID(ImGuiRenderer::image_id(texture, false)),
                               {p.x + (width - 64) * .5f, p.y + 5},
                               {p.x + (width + 64) * .5f, p.y + 69});
            draw->PushClipRect({p.x + 4, p.y + 74}, {p.x + width - 4, p.y + 105}, true);
            draw->AddText(nullptr, 0, {p.x + 5, p.y + 74}, ImGui::GetColorU32(ImGuiCol_Text),
                          item.name.c_str(), nullptr, width - 10);
            draw->PopClipRect();
            if (ground_texture_ == int(i))
                draw->AddRect(p, {p.x + width, p.y + 108}, ImGui::GetColorU32(ImGuiCol_CheckMark),
                              4, 0, 2);
            if (hovered) {
                const auto &image = base_->textures.at(item.texture);
                ImGui::SetTooltip("%s\n%u x %u\nWhole texture. Use Size to adjust repetition; "
                                  "atlas cropping is not available.",
                                  item.name.c_str(), image.width, image.height);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}
}
