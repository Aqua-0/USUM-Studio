#include "native/undo_shortcuts.h"
#include "native/existing_map_editor.h"
#include "native/imgui_renderer.h"
#include "native/viewport_navigation.h"
#include <bx/math.h>
#include <algorithm>
#include <cmath>
namespace studio {
namespace {
bool same_source(const SceneDraw &draw, const ModelDocument &model) {
    if (!draw.source)
        return false;
    const auto &resource = model.resources.at(model.material_resources.at(0));
    const auto &source = model.sources.at(resource.source);
    return draw.source->archive == source.archive && draw.source->member == source.member &&
           draw.source->path == resource.path;
}
}
ExistingMapEditor::ExistingMapEditor(const std::filesystem::path &shaders, SDL_Window *window)
    : renderer_(shaders), window_(window) {
    renderer_.retain_frame_during_upload = true;
    renderer_.lighting.enabled = true;
    renderer_.characters_enabled = false;
    renderer_.sky_enabled = false;
    renderer_.particles_enabled = false;
    renderer_.playback.enabled = false;
}
ExistingMapEditor::~ExistingMapEditor() {
    if (loading_.valid())
        loading_.wait();
}
void ExistingMapEditor::open(const std::filesystem::path &dump, unsigned area) {
    require(project_store() != nullptr, "Open a project to edit an existing map");
    finish(false);
    require(save_editor_project(), "Save existing edits before loading another map");
    binding_.unbind();
    document_.reset();
    geometry_.reset();
    base_.reset();
    view_.reset();
    choices_.clear();
    compose_ = {};
    edited_models_.clear();
    dump_ = dump;
    area_ = area;
    project_root_ = project_store()->root;
    loading_ = std::async(std::launch::async, [dump, area] {
        return load_environment(dump, area);
    });
}
void ExistingMapEditor::select(unsigned draw) {
    finish(false);
    require(save_editor_project(), "Save the current resource before selecting another");
    auto model = isolate_map_model(*base_, int(draw), dump_, int(area_));
    require(std::all_of(model.scene->draws.begin(), model.scene->draws.end(),
                        [](auto &d) {
                            return d.skeleton < 0;
                        }),
            "Choose unskinned terrain. Edit animated objects in Studio.");
    auto next = std::make_unique<MaterialDocument>(std::move(model));
    auto key = "material/" + next->identity();
    for (auto *binding : ProjectBinding::all())
        require(
            binding == &binding_ || binding->key() != key,
            "This resource is open in Studio. Close that model before editing it in Authoring.");
    if (auto it = project_store()->edits.find(key); it != project_store()->edits.end())
        next->restore(text(read_file(project_store()->document(key))));
    auto geometry = next->geometry();
    require(!geometry.meshes.empty(), "This resource has no editable meshes");

    binding_.unbind();
    document_ = std::move(next);
    geometry_ = std::move(geometry);
    editable_.clear();
    material_ = 0;
    binding_.bind(
        "material", key, document_->model.name, project_model_parameters(document_->model),
        [this] {
            return document_ && document_->dirty();
        },
        [this] {
            require(!dragging_, "Finish the terrain brush stroke before saving");
            document_->commit();
            return project_text(document_->serialize());
        },
        [this] {
            document_->mark_saved();
        });
    binding_.ready([this] {
        require(!dragging_, "Finish the terrain brush stroke before saving");
    });
    binding_.autosave_when([this] {
        return !dragging_;
    });
    refresh();
    status_ = "Choose the mesh parts to edit. Other geometry is protected.";
}
void ExistingMapEditor::refresh() {
    if (!document_)
        return;
    geometry_ = document_->geometry();
    std::erase_if(editable_, [&](auto mesh) {
        return mesh >= geometry_->meshes.size();
    });
    view_ = std::make_shared<Environment>(*base_);
    edited_models_[document_->identity()] = document_->model;
    std::erase_if(view_->draws, [&](const auto &draw) {
        return std::any_of(edited_models_.begin(), edited_models_.end(), [&](const auto &entry) {
            return same_source(draw, entry.second);
        });
    });
    for (const auto &[identity, model] : edited_models_) {
        const auto &scene = *model.scene;
        auto material_offset = view_->materials.size();
        auto table_offset = view_->lighting_tables.size();
        for (auto material : scene.materials) {
            for (auto &table : material.reflection_tables)
                if (table >= 0)
                    table += int(table_offset);
            view_->materials.push_back(std::move(material));
        }
        view_->lighting_tables.insert(view_->lighting_tables.end(), scene.lighting_tables.begin(),
                                      scene.lighting_tables.end());
        for (auto &[name, texture] : scene.textures)
            view_->textures[name] = texture;
        if (identity == document_->identity())
            first_draw_ = view_->draws.size();
        for (auto draw : scene.draws) {
            draw.material += unsigned(material_offset);
            draw.placement = -1;
            view_->draws.push_back(std::move(draw));
        }
    }
    view_->material_animations.clear();
    view_->visibility_animations.clear();
    renderer_.set_scene(compose_ ? std::make_shared<Environment>(compose_(*view_)) : view_);
}
void ExistingMapEditor::finish(bool cancel) {
    if (!dragging_)
        return;
    dragging_ = false;
    try {
        if (!cancel && changed_) {
            if (tool_ == 4) {
                auto result =
                    paint_existing_faces(*document_, painted_, std::size_t(material_),
                                         tile_mapping_ ? tile_size_ : 0, rotation_ * .01745329252f);
                auto previous = editable_;
                editable_.clear();
                for (std::size_t i = 0; i < result.sources.size(); ++i)
                    if (!result.sources[i].empty() &&
                        std::all_of(result.sources[i].begin(), result.sources[i].end(),
                                    [&](auto source) {
                                        return previous.contains(source);
                                    }))
                        editable_.insert(i);
                status_ = "Surface stroke applied.";
            } else {
                if (recalculate_)
                    for (auto m : editable_)
                        rebuild_mesh_normals(stroke_->meshes.at(m));
                document_->edit_geometry(*stroke_);
                status_ = "Sculpt stroke applied. Collision has not changed.";
            }
        }
    } catch (const std::exception &e) {
        error_ = e.what();
    }
    stroke_.reset();
    painted_.clear();
    changed_ = false;
    try {
        refresh();
    } catch (const std::exception &e) {
        error_ = e.what();
    }
}
bool ExistingMapEditor::draw(std::uint32_t, const std::filesystem::path &dump, unsigned area) {
    bool back = false;
    bool open_library = false;
    auto attempt = [&](auto action) {
        try {
            action();
            error_.clear();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    };
    ImGui::Begin("Composition");
    ImGui::SeparatorText("Map resources");
    try {
        if (!project_store() ||
            (!project_root_.empty() && (project_root_ != project_store()->root || dump_ != dump))) {
            if (loading_.valid()) {
                loading_.wait();
                loading_ = {};
            }
            binding_.unbind();
            document_.reset();
            base_.reset();
            view_.reset();
            geometry_.reset();
            stroke_.reset();
            editable_.clear();
            choices_.clear();
            edited_models_.clear();
            dragging_ = false;
            project_root_.clear();
        }
        if (loading_.valid() &&
            loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            base_ = std::make_shared<Environment>(loading_.get());
            view_ = base_;
            camera_.fit(base_->low, base_->high);
            if (auto start = base_->start_position())
                camera_.focus_start(*start);
            renderer_.set_scene(base_);
            for (unsigned i = 0; i < base_->draws.size(); ++i) {
                const auto &d = base_->draws[i];
                if (!d.source || d.character || d.player >= 0 || d.placement >= 0 ||
                    d.skeleton >= 0)
                    continue;
                if (std::none_of(choices_.begin(), choices_.end(), [&](auto index) {
                        auto &s = base_->draws[index].source;
                        return s->archive == d.source->archive && s->member == d.source->member &&
                               s->path == d.source->path;
                    }))
                    choices_.push_back(i);
            }
            status_ = "Select a terrain resource, then enable only its ground meshes.";
        }
        ImGui::BeginDisabled(loading_.valid() || dragging_ || !project_store());
        if (ImGui::Button("Load selected map"))
            attempt([&] {
                open(dump, area);
            });
        ImGui::EndDisabled();
        if (loading_.valid())
            ImGui::TextUnformatted("Loading map...");
        if (base_) {
            ImGui::Text("Field area %u", area_);
            ImGui::SeparatorText("Edit terrain");
            ImGui::TextWrapped("Open a terrain resource, then choose its editable mesh parts in Asset Library.");
            ImGui::BeginDisabled(dragging_ || loading_.valid());
            ImGui::BeginChild("Existing terrain resources", {0, 190}, ImGuiChildFlags_Borders);
            for (auto index : choices_) {
                const auto &draw = base_->draws[index];
                ImGui::PushID(int(index));
                ImGui::TextWrapped("%s", draw.name.c_str());
                const bool current = document_ && same_source(draw, document_->model);
                if (ImGui::Button(current ? "Continue editing terrain" : "Edit this terrain", {-1, 0}))
                    attempt([&] {
                        if (!current) select(index);
                        open_library = true;
                    });
                ImGui::Spacing();
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndDisabled();
        }
        else if (!loading_.valid())
            ImGui::TextWrapped("Load the selected map to choose terrain to edit.");
        ImGui::End();
        if (open_library) ImGui::SetNextWindowFocus();
        ImGui::Begin("Asset Library");
        if (document_) {
            ImGui::TextWrapped("%s", document_->model.name.c_str());
            ImGui::TextWrapped("Check the mesh parts you want to brush. Unchecked parts are protected.");
            ImGui::BeginDisabled(dragging_);
            ImGui::SeparatorText("Editable mesh parts");
            if (ImGui::Button("Enable all"))
                for (std::size_t i = 0; i < geometry_->meshes.size(); ++i)
                    editable_.insert(i);
            ImGui::SameLine();
            if (ImGui::Button("Protect all"))
                editable_.clear();
            ImGui::BeginChild("Editable terrain parts", {0, 0});
            for (std::size_t i = 0; i < geometry_->meshes.size(); ++i) {
                bool enabled = editable_.contains(i);
                ImGui::PushID(int(i));
                const auto &draw = document_->model.scene->draws.at(i);
                auto label =
                    draw.mesh + " / " + document_->model.scene->materials.at(draw.material).name;
                if (ImGui::Checkbox(label.c_str(), &enabled)) {
                    if (enabled)
                        editable_.insert(i);
                    else
                        editable_.erase(i);
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndDisabled();
        } else {
            ImGui::TextWrapped("Choose Edit this terrain in Composition to begin.");
            if (ImGui::Button("Go to Composition")) ImGui::SetWindowFocus("Composition");
        }
    } catch (const std::exception &e) {
        error_ = e.what();
    }
    ImGui::End();
    ImGui::Begin("Authoring tools");
    document_toolbar();
    ImGui::BeginDisabled(!document_ || dragging_);
    ImGui::SeparatorText(tool_ == 4 ? "Surface brush" : "Sculpt brush");
    ImGui::DragFloat("Radius", &radius_, 1, 1, 100000, "%.1f", ImGuiSliderFlags_AlwaysClamp);
    if (tool_ < 4) {
        ImGui::DragFloat("Strength", &strength_, 1, 1, 10000, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        if (tool_ == 3)
            ImGui::DragFloat("Target height", &level_, 1);
        ImGui::Checkbox("Recalculate normals", &recalculate_);
    } else if (document_) {
        const auto &materials = document_->model.scene->materials;
        material_ = std::clamp(material_, 0, int(materials.size()) - 1);
        if (ImGui::BeginCombo("Surface material", materials[material_].name.c_str())) {
            for (unsigned i = 0; i < materials.size(); ++i)
                if (ImGui::Selectable(materials[i].name.c_str(), material_ == int(i)))
                    material_ = int(i);
            ImGui::EndCombo();
        }
        auto texture = renderer_.texture(materials[material_].texture);
        if (bgfx::isValid(texture))
            ImGui::Image(ImTextureID(ImGuiRenderer::image_id(texture)), {128, 128});
        ImGui::Checkbox("Planar tile UVs", &tile_mapping_);
        if (tile_mapping_) {
            ImGui::DragFloat("Tile size", &tile_size_, 1, 1, 100000, "%.1f",
                             ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Rotation", &rotation_, -180, 180, "%.0f degrees");
        }
        ImGui::TextWrapped("Paint existing materials onto whole faces. Planar mapping repeats the "
                           "surface across X/Z; it is intended for ground.");
    }
    ImGui::EndDisabled();
    if (ImGui::CollapsingHeader("Controls"))
        ImGui::TextWrapped("Left drag: brush | Esc: cancel\nMiddle drag: orbit | Shift+middle: pan "
                           "| RMB+WASD: fly");
    ImGui::TextWrapped("Save and stage to update Maps. Collision is edited separately.");
    ImGui::End();
    ImGui::Begin("Authoring status");
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    else
        ImGui::TextWrapped("%s", status_.c_str());
    ImGui::End();
    viewport();
    return back;
}
void ExistingMapEditor::document_toolbar() {
    const auto action = [&](const char *label, bool enabled, auto callback) {
        ImGui::BeginDisabled(!enabled || dragging_);
        if (studio::UndoShortcuts::button("existing_map_editor", label, {0, 0})) {
            try {
                callback();
                error_.clear();
            } catch (const std::exception &e) {
                error_ = e.what();
            }
        }
        ImGui::EndDisabled();
    };
    action("Undo", document_ && document_->can_undo(), [&] {
        document_->undo();
        editable_.clear();
        refresh();
    });
    ImGui::SameLine();
    action("Redo", document_ && document_->can_redo(), [&] {
        document_->redo();
        editable_.clear();
        refresh();
    });
    ImGui::SameLine();
    action("Save Project", bool(document_), [&] {
        require(save_editor_project(), "Save failed");
    });
    ImGui::Separator();
}
void ExistingMapEditor::viewport() {
    ImGui::Begin("Authoring viewport");
    ImGui::BeginDisabled(dragging_);
    const char *tools[] = {"Raise", "Lower", "Smooth", "Flatten", "Paint"};
    for (int i = tool_ == 4 ? 4 : 0; i < (tool_ == 4 ? 5 : 4); ++i) {
        if (i && i != 4)
            ImGui::SameLine();
        if (tool_ == i)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        bool clicked = ImGui::Button(tools[i], {80, 30});
        if (tool_ == i)
            ImGui::PopStyleColor();
        if (clicked)
            tool_ = i;
    }
    ImGui::EndDisabled();
    if (!view_) {
        ImGui::TextUnformatted("Load the selected map to begin.");
        ImGui::End();
        return;
    }
    if (ImGui::Button("Frame map"))
        camera_.fit(base_->low, base_->high);
    ImGui::SameLine();
    if (document_ && ImGui::Button("Frame resource"))
        camera_.fit(document_->model.scene->low, document_->model.scene->high);
    auto available = ImGui::GetContentRegionAvail();
    auto resolution =
        ImGuiRenderer::viewport_resolution(std::max(1.f, available.x), std::max(1.f, available.y));
    ImVec2 size{resolution.display_width, resolution.display_height};
    float view[16], projection[16];
    auto eye = camera_.eye();
    bx::mtxLookAt(view, {eye[0], eye[1], eye[2]},
                  {camera_.target[0], camera_.target[1], camera_.target[2]}, {0, 1, 0},
                  bx::Handedness::Right);
    bx::mtxProj(projection, 45, float(resolution.width) / resolution.height, camera_.near_clip(),
                camera_.far_clip(), bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
    std::set<int> selected;
    for (auto m : editable_)
        selected.insert(int(first_draw_ + m));
    renderer_.select_draws(selected);
    renderer_.upload_step();
    auto texture = renderer_.render(resolution.width, resolution.height, view, projection, true,
                                    false, 0, false);
    bool flip = bgfx::getCaps()->originBottomLeft;
    ImGui::Image(ImTextureID(ImGuiRenderer::image_id(texture, false, ImGuiRenderer::preview_3ds)),
                 size, {0, flip ? 1.f : 0.f}, {1, flip ? 0.f : 1.f});
    auto origin = ImGui::GetItemRectMin();
    if (!renderer_.ready()) ImGui::GetWindowDrawList()->AddText({origin.x+12,origin.y+12}, IM_COL32(255,255,255,255), "Updating preview...");
    bool hovered = ImGui::IsItemHovered();
    auto &io = ImGui::GetIO();
    auto project = [&](std::array<float, 3> p, ImVec2 &screen) {
        float a[4]{}, b[4]{};
        for (unsigned i = 0; i < 4; ++i)
            a[i] = view[i] * p[0] + view[i + 4] * p[1] + view[i + 8] * p[2] + view[i + 12];
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned j = 0; j < 4; ++j)
                b[i] += projection[i + j * 4] * a[j];
        if (b[3] <= 0)
            return false;
        screen = {origin.x + (b[0] / b[3] + 1) * size.x * .5f,
                  origin.y + (1 - b[1] / b[3]) * size.y * .5f};
        return true;
    };
    try {
        if (dragging_ && (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                          !(SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS)))
            finish(true);
        auto direction = camera_.forward();
        auto right = camera_.right(), up = camera_.up();
        float x = ((io.MousePos.x - origin.x) / size.x * 2 - 1) * .41421356f * size.x / size.y;
        float y = (1 - (io.MousePos.y - origin.y) / size.y * 2) * .41421356f;
        for (unsigned i = 0; i < 3; ++i)
            direction[i] += right[i] * x + up[i] * y;
        auto hit =
            hovered && geometry_ && renderer_.ready() && !viewport_navigating()
                ? pick_existing_mesh(stroke_ ? *stroke_ : *geometry_, editable_, eye, direction)
                : std::nullopt;
        if (hit) {
            auto *draw = ImGui::GetWindowDrawList();
            draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
            ImVec2 previous{};
            bool connected = false;
            for (unsigned i = 0; i <= 48; ++i) {
                auto p = hit->point;
                float angle = i * 6.2831853f / 48;
                p[0] += std::cos(angle) * radius_;
                p[2] += std::sin(angle) * radius_;
                ImVec2 screen;
                if (project(p, screen)) {
                    if (connected)
                        draw->AddLine(previous, screen, IM_COL32(100, 240, 185, 255), 2);
                    previous = screen;
                    connected = true;
                } else
                    connected = false;
            }
            draw->PopClipRect();
        }
        if (hit && !dragging_ && !io.WantTextInput &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            stroke_ = *geometry_;
            painted_.clear();
            changed_ = false;
            dragging_ = true;
            error_.clear();
        }
        if (dragging_ && hit && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (tool_ == 4) {
                auto faces = existing_brush_faces(*stroke_, editable_, *hit, radius_);
                for (auto &[m, selected_faces] : faces)
                    painted_[m].insert(selected_faces.begin(), selected_faces.end());
                changed_ = !painted_.empty();
            } else {
                auto mode = ExistingBrush(tool_);
                if (io.KeyAlt && tool_ < 2)
                    mode = tool_ == 0 ? ExistingBrush::Lower : ExistingBrush::Raise;
                float amount = std::min(io.DeltaTime, .05f) * strength_ * (tool_ >= 2 ? .02f : 1.f);
                changed_ |= sculpt_existing_mesh(*stroke_, editable_, hit->point, radius_, amount,
                                                 mode, level_);
                for (auto m : editable_) {
                    auto vertices = document_->model.scene->draws.at(m).vertices;
                    for (unsigned v = 0; v < vertices.size(); ++v) {
                        auto p = stroke_->meshes.at(m).vertices.at(v).position;
                        vertices[v].x = p[0];
                        vertices[v].y = p[1];
                        vertices[v].z = p[2];
                    }
                    renderer_.preview_vertices(first_draw_ + m, vertices);
                }
            }
        }
        if (dragging_ && tool_ == 4) {
            auto *draw = ImGui::GetWindowDrawList();
            draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
            for (auto &[m, faces] : painted_)
                for (auto f : faces) {
                    ImVec2 p[3];
                    bool valid = true;
                    for (unsigned i = 0; i < 3; ++i)
                        valid &= project(stroke_->meshes[m]
                                             .vertices[stroke_->meshes[m].indices[f * 3 + i]]
                                             .position,
                                         p[i]);
                    if (valid)
                        draw->AddTriangleFilled(p[0], p[1], p[2], IM_COL32(240, 190, 75, 95));
                }
            draw->PopClipRect();
        }
        if (dragging_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            finish(false);
        if (!dragging_)
            viewport_navigation(camera_, window_, hovered);
    } catch (const std::exception &e) {
        error_ = e.what();
        finish(true);
    }
    ImGui::End();
}
}
