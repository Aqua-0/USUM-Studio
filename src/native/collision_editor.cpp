#include "native/tutorial_widgets.h"
#include "native/collision_editor.h"
#include "authoring/ground_export.h"
#include "native/theme.h"
#include "native/imgui_renderer.h"
#include "field/collision_surfaces.h"
#include <SDL3/SDL.h>
#include <bx/math.h>
#include <algorithm>
#include <cmath>
#include <sstream>
namespace studio {
namespace {
std::vector<CollisionState> live_faces(const CollisionDocument &document) {
    std::vector<CollisionState> result;
    for (unsigned i = 0; i < document.size(); ++i)
        if (document.live(i))
            result.push_back(document.state(i));
    return result;
}
const char *layer_names[] = {"Ground", "Wall", "Surf boundary", "Ride barrier", "Mudsdale barrier"};
void next_control(float width) {
    if (ImGui::GetItemRectMax().x + width + ImGui::GetStyle().ItemSpacing.x <
        ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x -
            ImGui::GetStyle().ScrollbarSize)
        ImGui::SameLine();
}
}
void CollisionEditor::set_scene(std::shared_ptr<Environment> scene, unsigned area,
                                const std::filesystem::path &dump) {
    cancel_drag();
    scene_ = std::move(scene);
    document_ = std::make_unique<CollisionDocument>(scene_, area, dump);
    area_ = area;
    if (!authored_changed_)
        bind_project();
    vertices_mode_ = false;
    path_.clear();
    obj_imported_ = false;
    selection_.clear();
    vertices_.clear();
    revision_ = document_->revision();
    message_.clear();
    camera_.fit(scene_->low, scene_->high);
    for (unsigned i = 0; i < document_->size(); ++i)
        if (document_->live(i) && document_->state(i).kind == SpatialKind::Ground) {
            selection_.insert(i);
            break;
        }
}
void CollisionEditor::set_authored_scene(std::shared_ptr<Environment> scene, unsigned area,
                                         const std::filesystem::path &dump,
                                         std::function<void(std::vector<CollisionState>)> changed,
                                         std::function<bool()> dirty) {
    authored_changed_ = std::move(changed);
    authored_dirty_ = std::move(dirty);
    set_scene(std::move(scene), area, dump);
    authored_revision_ = document_->revision();
    select_all();
    frame_selection();
    message_ = "Custom collision is saved with the composition. Terrain changes preserve it.";
}
void CollisionEditor::prepare_save() {
    require(!operation_pending() && (!document_ || !document_->previewing()),
            "Finish the collision operation before saving");
    synchronize();
}
CollisionDocument &CollisionEditor::exchange_document() {
    if (!authored_changed_)
        return *document_;
    auto faces = live_faces(*document_);
    if (!authored_exchange_ || live_faces(*authored_exchange_) != faces)
        authored_exchange_ = std::make_unique<CollisionDocument>(
            authored_collision_scene(*scene_, faces), area_, std::filesystem::path{});
    return *authored_exchange_;
}
void CollisionEditor::activate(bool active) {
    if (active_ && !active)
        cancel_drag();
    active_ = active;
    if (active)
        renderer_.player.active = false;
}
void CollisionEditor::synchronize() {
    if (document_ && revision_ != document_->revision()) {
        revision_ = document_->revision();
        std::erase_if(selection_, [&](auto id) {
            return !document_->live(id);
        });
        std::erase_if(vertices_, [&](auto vertex) {
            return !document_->live(vertex.face);
        });
        renderer_.spatial.invalidate_geometry();
        renderer_.spatial.selected = -1;
        renderer_.invalidate_selection_readback();
    }
    if (authored_changed_ && document_ && !document_->previewing() &&
        authored_revision_ != document_->revision()) {
        std::vector<CollisionState> faces;
        for (unsigned i = 0; i < document_->size(); ++i)
            if (document_->live(i))
                faces.push_back(document_->state(i));
        authored_changed_(std::move(faces));
        authored_revision_ = document_->revision();
    }
}
void CollisionEditor::cancel_drag() {
    if (document_)
        document_->cancel_preview();
    drag_axis_ = -1;
    selecting_ = false;
    drag_vertices_.clear();
    synchronize();
}
void CollisionEditor::request_leave(std::function<void()> action) {
    if (project_store()) {
        try {
            save_editor_project();
            action();
        } catch (const std::exception &e) {
            message_ = e.what();
        }
        return;
    }
    cancel_drag();
    if (export_.valid() || dialog_kind_) {
        request_focus();
        message_ = "Wait for the collision file operation to finish.";
        return;
    }
    if (document_ && document_->dirty()) {
        request_focus();
        leave_ = true;
        leave_action_ = std::move(action);
    } else
        action();
}
void CollisionEditor::open_patch(const std::filesystem::path &path) {
    require(bool(document_), "Load the originating map before opening a collision patch");
    require(std::filesystem::file_size(path) <= 64 * 1024 * 1024, "Collision patch is too large");
    document_->restore(text(read_file(path)));
    if (!authored_changed_)
        project_.imported();
    path_ = path;
    obj_imported_ = false;
    select_changed();
    synchronize();
    message_ = "Collision patch opened.";
}
void CollisionEditor::dialog(int kind) {
    if (kind == 1 && project_store())
        save_editor_project();
    dialog_kind_ = kind;
    {
        std::lock_guard lock(dialog_->mutex);
        dialog_->pending = true;
        dialog_->ready = false;
        dialog_->error.clear();
        dialog_->path.clear();
    }
    if (kind == 3) {
        choose_folder(window_, dialog_, nullptr);
        return;
    }
    auto *owner = new std::shared_ptr<FolderSelection>(dialog_);
    static const SDL_DialogFileFilter patch_filters[] = {{"Collision patch", "usum-collision"}};
    static const SDL_DialogFileFilter obj_filters[] = {{"Collision mesh", "obj"}};
    auto *filters = kind >= 4 ? obj_filters : patch_filters;
    auto callback = [](void *data, const char *const *files, int) {
        std::unique_ptr<std::shared_ptr<FolderSelection>> owner(
            static_cast<std::shared_ptr<FolderSelection> *>(data));
        auto &result = **owner;
        std::lock_guard lock(result.mutex);
        if (!files)
            result.error = SDL_GetError();
        else if (files[0])
            result.path = files[0];
        result.pending = false;
        result.ready = true;
    };
    if (kind == 1 || kind == 4)
        SDL_ShowSaveFileDialog(callback, owner, window_, filters, 1,
                               kind == 4 || path_.empty() ? nullptr : path_.string().c_str());
    else
        SDL_ShowOpenFileDialog(callback, owner, window_, filters, 1, nullptr, false);
}
void CollisionEditor::save() {
    if (save_editor_project()) {
        message_ = "Project saved. Stage Project builds the overlay.";
        return;
    }
    if (path_.empty()) {
        dialog(1);
        return;
    }
    document_->save(path_);
    message_ = "Collision patch saved.";
    if (save_leave_) {
        save_leave_ = false;
        auto action = std::move(leave_action_);
        if (action)
            action();
    }
}
void CollisionEditor::update() {
    if (export_.valid() && export_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            export_.get();
            message_ = "Collision override exported. In-game validation is still needed.";
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    std::string path, error;
    int kind = 0;
    {
        std::lock_guard lock(dialog_->mutex);
        if (dialog_->ready) {
            dialog_->ready = false;
            path = dialog_->path;
            error = dialog_->error;
            kind = dialog_kind_;
            dialog_kind_ = 0;
        }
    }
    if (kind)
        try {
            require(error.empty(), error);
            if (path.empty()) {
                save_leave_ = false;
                return;
            }
            if (kind == 1) {
                path_ = std::filesystem::u8path(path);
                if (path_.extension() != ".usum-collision")
                    path_ += ".usum-collision";
                save();
            } else if (kind == 2) {
                auto file = std::filesystem::u8path(path);
                require(std::filesystem::file_size(file) <= 64 * 1024 * 1024,
                        "Collision patch is too large");
                open_patch(file);
            } else if (kind == 4) {
                auto file = std::filesystem::u8path(path);
                if (file.extension() != ".obj")
                    file += ".obj";
                exchange_document().export_obj(file, export_reference_);
                message_ = "Collision bundle exported. Use the Blender collision helper to retain "
                           "triangle IDs.";
            } else if (kind == 5) {
                auto &exchange = exchange_document();
                auto result = exchange.import_obj(std::filesystem::u8path(path));
                if (authored_changed_ && live_faces(exchange) != live_faces(*document_)) {
                    std::map<unsigned, CollisionState> removed;
                    for (unsigned i = 0; i < document_->size(); ++i)
                        if (document_->live(i)) {
                            auto state = document_->state(i);
                            state.deleted = true;
                            removed[i] = state;
                        }
                    std::vector<CollisionAddition> added;
                    for (unsigned i = 0; i < exchange.size(); ++i)
                        if (exchange.live(i))
                            added.push_back({exchange.member(i), exchange.state(i)});
                    document_->replace_faces(removed, added);
                }
                if (!obj_imported_)
                    path_.clear();
                obj_imported_ = true;
                select_changed();
                message_ = "OBJ import: " + std::to_string(result.modified) + " modified, " +
                           std::to_string(result.added) + " added, " +
                           std::to_string(result.removed) + " removed. Undo reverses the import.";
            } else {
                auto snapshot = *document_;
                export_ = std::async(std::launch::async, [snapshot, path] {
                    snapshot.export_to(std::filesystem::u8path(path));
                });
                message_ = "Exporting terrain collision...";
            }
        } catch (const std::exception &e) {
            message_ = e.what();
            save_leave_ = false;
        }
    synchronize();
}
std::vector<CollisionVertex> CollisionEditor::selection_vertices() const {
    std::map<CollisionVertexGroup, CollisionVertex> unique;
    if (!document_)
        return {};
    if (vertices_mode_) {
        for (auto v : vertices_)
            unique.emplace(document_->vertex_group(v), v);
    } else
        for (auto id : selection_)
            for (unsigned v = 0; v < 3; ++v)
                unique.emplace(document_->vertex_group({id, v}), CollisionVertex{id, v});
    std::vector<CollisionVertex> result;
    for (auto &[key, value] : unique)
        result.push_back(value);
    return result;
}
void CollisionEditor::select_mode(bool vertices) {
    if (vertices_mode_ == vertices || !document_)
        return;
    cancel_drag();
    if (vertices) {
        auto selected = selection_vertices();
        vertices_ = {selected.begin(), selected.end()};
    } else {
        std::set<CollisionVertexGroup> groups;
        for (auto v : vertices_)
            groups.insert(document_->vertex_group(v));
        selection_.clear();
        for (unsigned i = 0; i < document_->size(); ++i)
            if (document_->live(i) && visible_[unsigned(document_->state(i).kind)])
                for (unsigned v = 0; v < 3; ++v)
                    if (groups.contains(document_->vertex_group({i, v}))) {
                        selection_.insert(i);
                        break;
                    }
    }
    vertices_mode_ = vertices;
}
void CollisionEditor::select_all() {
    if (!document_)
        return;
    selection_.clear();
    for (unsigned i = 0; i < document_->size(); ++i)
        if (document_->live(i) && visible_[unsigned(document_->state(i).kind)])
            selection_.insert(i);
    if (vertices_mode_) {
        vertices_mode_ = false;
        auto refs = selection_vertices();
        vertices_ = {refs.begin(), refs.end()};
        vertices_mode_ = true;
    }
}
void CollisionEditor::select_layer(unsigned kind) {
    if (!document_)
        return;
    vertices_mode_ = false;
    selection_.clear();
    visible_[kind] = true;
    for (unsigned i = 0; i < document_->size(); ++i)
        if (document_->live(i) && unsigned(document_->state(i).kind) == kind)
            selection_.insert(i);
}
void CollisionEditor::select_changed() {
    selection_.clear();
    vertices_.clear();
    vertices_mode_ = false;
    for (unsigned i = 0; i < document_->size(); ++i)
        if (document_->live(i) && document_->is_changed(i)) {
            selection_.insert(i);
            visible_[unsigned(document_->state(i).kind)] = true;
        }
    frame_selection();
}
void CollisionEditor::select_connected() {
    if (!document_)
        return;
    std::set<CollisionVertexGroup> connected;
    for (auto v : selection_vertices())
        connected.insert(document_->vertex_group(v));
    std::set<unsigned> faces;
    bool changed = true;
    while (changed) {
        changed = false;
        for (unsigned i = 0; i < document_->size(); ++i) {
            if (!document_->live(i) || faces.contains(i) ||
                !visible_[unsigned(document_->state(i).kind)])
                continue;
            bool hit = false;
            for (unsigned v = 0; v < 3; ++v)
                hit |= connected.contains(document_->vertex_group({i, v}));
            if (hit) {
                faces.insert(i);
                for (unsigned v = 0; v < 3; ++v)
                    connected.insert(document_->vertex_group({i, v}));
                changed = true;
            }
        }
    }
    selection_ = std::move(faces);
    if (vertices_mode_) {
        vertices_mode_ = false;
        auto refs = selection_vertices();
        vertices_ = {refs.begin(), refs.end()};
        vertices_mode_ = true;
    }
}
void CollisionEditor::frame_selection(bool all) {
    if (!scene_)
        return;
    auto low = scene_->low, high = scene_->high;
    if (!all) {
        auto refs = selection_vertices();
        if (refs.empty())
            return;
        low = high = document_->state(refs[0].face).vertices[refs[0].corner];
        for (auto ref : refs)
            for (unsigned k = 0; k < 3; ++k) {
                auto x = document_->state(ref.face).vertices[ref.corner][k];
                low[k] = std::min(low[k], x);
                high[k] = std::max(high[k], x);
            }
    } else
        for (unsigned i = 0; i < document_->size(); ++i)
            if (document_->live(i))
                for (auto p : document_->state(i).vertices)
                    for (unsigned k = 0; k < 3; ++k) {
                        low[k] = std::min(low[k], p[k]);
                        high[k] = std::max(high[k], p[k]);
                    }
    camera_.fit(low, high);
}
void CollisionEditor::height_edit(bool absolute, float amount) {
    require(std::isfinite(amount), "Enter a finite height");
    std::map<CollisionVertex, SpatialPoint> edits;
    if (vertices_mode_) {
        for (auto v : selection_vertices()) {
            auto p = document_->state(v.face).vertices[v.corner];
            p[1] = absolute ? amount : p[1] + amount;
            edits[v] = p;
        }
    } else
        for (auto id : selection_)
            for (unsigned c = 0; c < 3; ++c) {
                auto p = document_->state(id).vertices[c];
                p[1] = absolute ? amount : p[1] + amount;
                edits[{id, c}] = p;
            }
    document_->edit_vertices(edits, vertices_mode_ || shared_);
    synchronize();
}
void CollisionEditor::layers(bool locked) {
    ImGui::Begin("Collision layers");
    if (!document_) {
        ImGui::TextWrapped("Load an existing map to edit its collision.");
        ImGui::End();
        return;
    }
    if (authored_changed_)
        ImGui::TextUnformatted("Authored ground collision");
    ImGui::Text("Field area %u", area_);
    ImGui::Text("%zu collision triangles", document_->active_size());
    ImGui::Separator();
    ImGui::BeginDisabled(locked);
    for (unsigned kind = 0; kind < 5; ++kind) {
        ImGui::PushID(int(kind));
        if (studio::TutorialWidgets::Checkbox("collision_editor", "##visible", &visible_[kind])) {
            for (auto it = selection_.begin(); it != selection_.end();)
                if (!visible_[unsigned(document_->state(*it).kind)])
                    it = selection_.erase(it);
                else
                    ++it;
            for (auto it = vertices_.begin(); it != vertices_.end();)
                if (!visible_[unsigned(document_->state(it->face).kind)])
                    it = vertices_.erase(it);
                else
                    ++it;
        }
        ImGui::SameLine();
        unsigned count = 0;
        for (unsigned i = 0; i < document_->size(); ++i)
            count += document_->live(i) && unsigned(document_->state(i).kind) == kind;
        if (ImGui::Selectable(
                (std::string(layer_names[kind]) + " (" + std::to_string(count) + ")").c_str()))
            select_layer(kind);
        ImGui::PopID();
    }
    ImGui::Separator();
    studio::TutorialWidgets::Checkbox("collision_editor", "Show map scenery", &context_);
    studio::TutorialWidgets::Checkbox("collision_editor", "See through scenery", &xray_);
    studio::TutorialWidgets::Checkbox("collision_editor", "Filled collision", &filled_);
    studio::TutorialWidgets::Checkbox("collision_editor", "Triangle edges", &edges_);
    studio::TutorialWidgets::Checkbox("collision_editor", "Color by attribute", &attribute_colors_);
    if (attribute_colors_ && ImGui::TreeNodeEx("Surface colors", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::map<std::uint32_t, std::pair<unsigned, bool>> counts;
        for (unsigned id = 0; id < document_->size(); ++id) {
            auto &state = document_->state(id);
            if (!state.deleted && visible_[unsigned(state.kind)]) {
                auto &count = counts[state.attribute];
                ++count.first;
                count.second |= state.kind == SpatialKind::Ground;
            }
        }
        for (auto [attribute, details] : counts) {
            auto count = details.first;
            auto color = collision_surface_color(attribute);
            ImGui::PushID(std::to_string(attribute).c_str());
            ImGui::ColorButton("##swatch", {color[0], color[1], color[2], 1},
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                               {14, 14});
            ImGui::SameLine();
            auto label =
                std::to_string(attribute) + " " +
                (details.second ? collision_surface_name(attribute) : "Collision attribute") +
                " (" + std::to_string(count) + ")";
            if (ImGui::Selectable(label.c_str())) {
                vertices_mode_ = false;
                vertices_.clear();
                if (!ImGui::GetIO().KeyShift)
                    selection_.clear();
                for (unsigned id = 0; id < document_->size(); ++id) {
                    auto &state = document_->state(id);
                    if (!state.deleted && visible_[unsigned(state.kind)] &&
                        state.attribute == attribute)
                        selection_.insert(id);
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", label.c_str());
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    ImGui::Spacing();
    if (studio::TutorialWidgets::Button("collision_editor", "Select all"))
        select_all();
    if (studio::TutorialWidgets::Button("collision_editor", "Select connected"))
        select_connected();
    if (studio::TutorialWidgets::Button("collision_editor", "Clear selection")) {
        selection_.clear();
        vertices_.clear();
    }
    ImGui::Separator();
    try {
        if (primary_button(project_store() ? "Save Project" : "Save patch"))
            save();
        if (!authored_changed_)
            next_control(85);
        if (!project_store() && studio::TutorialWidgets::Button("collision_editor", "Save as..."))
            dialog(1);
        if (!authored_changed_ &&
            studio::TutorialWidgets::Button("collision_editor", "Open patch..."))
            request_leave([this] {
                dialog(2);
            });
        if (!authored_changed_) {
            ImGui::BeginDisabled(!document_->changed());
            if (studio::TutorialWidgets::Button("collision_editor", "Export override..."))
                dialog(3);
            ImGui::EndDisabled();
        } else
            ImGui::TextWrapped(
                "Save Project keeps custom collision with the composition. Stage Project combines "
                "it with the terrain; Build game export creates game files.");
    } catch (const std::exception &e) {
        message_ = e.what();
    }
    if (ImGui::TreeNodeEx("Blender exchange", ImGuiTreeNodeFlags_DefaultOpen)) {
        studio::TutorialWidgets::Checkbox("collision_editor", "Include map reference geometry",
                                          &export_reference_);
        if (studio::TutorialWidgets::Button("collision_editor", "Export collision OBJ..."))
            dialog(4);
        if (studio::TutorialWidgets::Button("collision_editor", "Import edited OBJ..."))
            dialog(5);
        ImGui::TextWrapped("Use the bundled Blender helper for triangle IDs, added/deleted faces "
                           "and material attributes. Export includes a baseline patch.");
        ImGui::TreePop();
    }
    ImGui::TextWrapped("%s", (authored_dirty_ ? authored_dirty_() : document_->dirty())
                                 ? "Unsaved collision changes"
                             : authored_changed_    ? "Custom collision saved with composition"
                             : document_->changed() ? "Collision patch saved"
                                                    : "No collision changes");
    ImGui::EndDisabled();
    ImGui::End();
}
void CollisionEditor::toolbar(bool locked) {
    ImGui::BeginDisabled(locked);
    if (studio::TutorialWidgets::RadioButton("collision_editor", "Triangles", !vertices_mode_))
        select_mode(false);
    next_control(75);
    if (studio::TutorialWidgets::RadioButton("collision_editor", "Vertices", vertices_mode_))
        select_mode(true);
    next_control(60);
    if (ImGui::Selectable("Click", tool_ == Tool::Select, 0, {45, ImGui::GetFrameHeight()}))
        tool_ = Tool::Select;
    next_control(45);
    if (ImGui::Selectable("Box", tool_ == Tool::Box, 0, {35, ImGui::GetFrameHeight()}))
        tool_ = Tool::Box;
    next_control(80);
    auto tool_button = [&](const char *label, Tool mode) {
        bool selected = tool_ == mode;
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.20f, .40f, .34f, 1));
        auto at = ImGui::GetCursorScreenPos();
        bool pressed = studio::TutorialWidgets::Button("collision_editor", label);
        auto *draw = ImGui::GetWindowDrawList();
        auto color = ImGui::GetColorU32(ImGuiCol_Text);
        float cy = at.y + ImGui::GetFrameHeight() * .5f, cx = at.x + 12;
        if (mode == Tool::Move) {
            draw->AddLine({cx - 5, cy}, {cx + 5, cy}, color);
            draw->AddLine({cx, cy - 5}, {cx, cy + 5}, color);
            draw->AddTriangleFilled({cx, cy - 7}, {cx - 3, cy - 3}, {cx + 3, cy - 3}, color);
            draw->AddTriangleFilled({cx + 7, cy}, {cx + 3, cy - 3}, {cx + 3, cy + 3}, color);
        } else {
            draw->AddCircle({cx, cy}, 6, color, 20);
            draw->AddLine({cx - 6, cy}, {cx + 6, cy}, color);
            draw->AddLine({cx, cy - 6}, {cx, cy + 6}, color);
        }
        if (selected)
            ImGui::PopStyleColor();
        if (pressed)
            tool_ = mode;
    };
    tool_button("    Move", Tool::Move);
    next_control(95);
    tool_button("    Rotate", Tool::Rotate);
    next_control(75);
    studio::TutorialWidgets::Checkbox("collision_editor", "Snap", &snap_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("5 units / 15 degrees; Ctrl while dragging also snaps");
    next_control(115);
    if (studio::TutorialWidgets::Button("collision_editor", "Frame selection"))
        frame_selection();
    next_control(75);
    if (studio::TutorialWidgets::Button("collision_editor", "Fit map"))
        frame_selection(true);
    ImGui::EndDisabled();
}
void CollisionEditor::inspector(bool locked) {
    ImGui::Begin("Collision properties");
    if (!document_) {
        ImGui::TextWrapped("Load a map in Maps, then return to Collision.");
        ImGui::End();
        return;
    }
    auto refs = selection_vertices();
    ImGui::Text("%zu %s selected", vertices_mode_ ? refs.size() : selection_.size(),
                vertices_mode_ ? (refs.size() == 1 ? "vertex" : "vertices")
                               : (selection_.size() == 1 ? "triangle" : "triangles"));
    ImGui::BeginDisabled(locked);
    ImGui::BeginDisabled(!document_->can_undo());
    if (studio::TutorialWidgets::Button("collision_editor", "Undo"))
        document_->undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!document_->can_redo());
    if (studio::TutorialWidgets::Button("collision_editor", "Redo"))
        document_->redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("collision_editor", "Reset all"))
        document_->reset();
    ImGui::Separator();
    ImGui::BeginDisabled(refs.empty());
    ImGui::TextUnformatted("Transform");
    ImGui::TextWrapped("World axes; pivot at the selection center.");
    if (!refs.empty()) {
        SpatialPoint center{};
        for (auto v : refs)
            for (unsigned k = 0; k < 3; ++k)
                center[k] += document_->state(v.face).vertices[v.corner][k] / float(refs.size());
        ImGui::Text("X %.2f  Y %.2f  Z %.2f", center[0], center[1], center[2]);
    }
    ImGui::TextUnformatted("Target height (Y)");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##height", &height_, 0, 0, "%.3f");
    if (studio::TutorialWidgets::Button("collision_editor", "Apply height"))
        try {
            height_edit(true, height_);
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    if (studio::TutorialWidgets::TreeNode("collision_editor", "Nudge height")) {
        ImGui::InputFloat("Units", &step_);
        ImGui::BeginDisabled(!std::isfinite(step_) || step_ <= 0);
        if (studio::TutorialWidgets::Button("collision_editor", "Lower"))
            try {
                height_edit(false, -step_);
            } catch (const std::exception &e) {
                message_ = e.what();
            }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("collision_editor", "Raise"))
            try {
                height_edit(false, step_);
            } catch (const std::exception &e) {
                message_ = e.what();
            }
        ImGui::EndDisabled();
        ImGui::TreePop();
    }
    ImGui::BeginDisabled(vertices_mode_);
    studio::TutorialWidgets::Checkbox("collision_editor", "Keep shared vertices together",
                                      &shared_);
    ImGui::EndDisabled();
    ImGui::TextWrapped(
        vertices_mode_ ? "Vertex edits move attached triangles in the same collision resource."
        : shared_ ? "Shared corners follow the selection. Turn this off to separate selected faces."
                  : "Selected faces move independently of their neighbors.");
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::BeginDisabled(vertices_mode_ || selection_.empty());
    std::optional<SpatialKind> kind;
    std::optional<std::uint32_t> attribute;
    bool first = true, ground = true;
    for (auto id : selection_) {
        auto &state = document_->state(id);
        ground &= state.kind == SpatialKind::Ground;
        if (first) {
            kind = state.kind;
            attribute = state.attribute;
            first = false;
        } else {
            if (kind && *kind != state.kind)
                kind.reset();
            if (attribute && *attribute != state.attribute)
                attribute.reset();
        }
    }
    ImGui::TextUnformatted("Collision type");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##kind", kind ? layer_names[unsigned(*kind)] : "Mixed / none")) {
        for (unsigned i = 0; i < 5; ++i)
            if (ImGui::Selectable(layer_names[i], kind && unsigned(*kind) == i))
                try {
                    document_->properties(selection_, SpatialKind(i), {});
                    visible_[i] = true;
                } catch (const std::exception &e) {
                    message_ = e.what();
                }
        ImGui::EndCombo();
    }
    ImGui::TextWrapped("Type changes move triangles between ground and wall resources on export.");
    ImGui::BeginDisabled(!ground);
    ImGui::TextUnformatted("Ground surface");
    auto label = attribute ? collision_surface_name(*attribute) : "Mixed / none";
    if (attribute) {
        auto color = collision_surface_color(*attribute);
        ImGui::ColorButton("##surface-color", {color[0], color[1], color[2], 1},
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                           {14, 14});
        ImGui::SameLine();
        ImGui::TextUnformatted(label.c_str());
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##surface", label.c_str())) {
        for (unsigned i = 0; i < collision_surfaces.size(); ++i)
            if (ImGui::Selectable(
                    (std::string(collision_surfaces[i]) + " (" + std::to_string(i) + ")").c_str(),
                    attribute && *attribute == i))
                try {
                    document_->properties(selection_, {}, i);
                } catch (const std::exception &e) {
                    message_ = e.what();
                }
        ImGui::EndCombo();
    }
    if (attribute)
        ImGui::Text("Attribute %u (0x%08X)", *attribute, *attribute);
    if (attribute && *attribute == 38)
        ImGui::TextWrapped("This Ultra Moon entry has a provisional name.");
    ImGui::TextWrapped(
        "Footsteps are selected by the game using the ground surface and character animation.");
    if (studio::TutorialWidgets::TreeNode("collision_editor", "Raw surface attribute")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputScalar("##raw", ImGuiDataType_U32, &raw_attribute_);
        if (studio::TutorialWidgets::Button("collision_editor", "Apply attribute"))
            try {
                document_->properties(selection_, {}, raw_attribute_);
            } catch (const std::exception &e) {
                message_ = e.what();
            }
        ImGui::TextWrapped("Unknown values are preserved. They have no verified surface meaning.");
        ImGui::TreePop();
    }
    if (attribute && (*attribute == 7 || *attribute == 8 || *attribute == 21))
        ImGui::TextWrapped(
            "Water attributes alone do not enable Surf; entry also uses Surf boundaries.");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    modal();
    ImGui::End();
    synchronize();
}
void CollisionEditor::modal() {
    if (leave_) {
        ImGui::OpenPopup("Unsaved collision edits");
        leave_ = false;
    }
    if (ImGui::BeginPopupModal("Unsaved collision edits", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save collision edits before continuing?");
        if (studio::TutorialWidgets::Button("collision_editor", "Save")) {
            ImGui::CloseCurrentPopup();
            save_leave_ = true;
            try {
                save();
            } catch (const std::exception &e) {
                message_ = e.what();
                save_leave_ = false;
            }
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("collision_editor", "Discard")) {
            ImGui::CloseCurrentPopup();
            auto action = std::move(leave_action_);
            if (action)
                action();
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("collision_editor", "Cancel")) {
            ImGui::CloseCurrentPopup();
            leave_action_ = {};
        }
        ImGui::EndPopup();
    }
}
std::string CollisionEditor::report() const {
    std::ostringstream out;
    out << "Collision area: " << area_
        << "\nCollision triangles: " << (document_ ? document_->active_size() : 0)
        << "\nSelected triangles: " << selection_.size()
        << "\nSelected vertices: " << selection_vertices().size()
        << "\nCollision dirty: " << (document_ && document_->dirty()) << '\n';
    return out.str();
}
bool CollisionEditor::draw_workspace(bool loading) {
    loading_ = loading;
    bool go_maps = false;
    bool locked = busy() || drag_axis_ >= 0;
    layers(locked);
    inspector(locked);
    ImGui::Begin("Collision viewport");
    if (!document_) {
        ImGui::TextWrapped("Load an existing map to start editing collision.");
        if (primary_button("Choose map in Maps"))
            go_maps = true;
        ImGui::End();
    } else {
        toolbar(locked);
        ImGui::Separator();
        auto size = ImGui::GetContentRegionAvail();
        size.x = std::max(size.x, 1.f);
        size.y = std::max(size.y, 1.f);
        unsigned width = unsigned(
                     std::clamp(size.x * ImGui::GetIO().DisplayFramebufferScale.x, 1.f, 4096.f)),
                 height = unsigned(
                     std::clamp(size.y * ImGui::GetIO().DisplayFramebufferScale.y, 1.f, 4096.f));
        auto eye = camera_.eye();
        float view[16], projection[16];
        bx::mtxLookAt(view, {eye[0], eye[1], eye[2]},
                      {camera_.target[0], camera_.target[1], camera_.target[2]}, {0, 1, 0},
                      bx::Handedness::Right);
        bx::mtxProj(projection, 45, float(width) / height, camera_.near_clip(), camera_.far_clip(),
                    bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
        auto old_selected = renderer_.spatial.selected;
        auto old_opacity = renderer_.spatial.opacity;
        auto enabled = renderer_.spatial.enabled;
        auto old_filled = renderer_.spatial.filled, old_edges = renderer_.spatial.edges,
             old_xray = renderer_.spatial.xray, old_only = renderer_.spatial.selected_only,
             old_attribute_colors = renderer_.spatial.attribute_colors,
             old_geometry = renderer_.geometry_enabled, old_particles = renderer_.particles_enabled;
        renderer_.spatial.enabled.fill(false);
        for (unsigned i = 0; i < 5; ++i)
            renderer_.spatial.enabled[i] = visible_[i];
        renderer_.spatial.filled = filled_;
        renderer_.spatial.edges = edges_;
        renderer_.spatial.xray = xray_;
        renderer_.spatial.selected_only = false;
        renderer_.spatial.selected = -1;
        renderer_.spatial.attribute_colors = attribute_colors_;
        if (attribute_colors_)
            renderer_.spatial.opacity = .32f;
        renderer_.geometry_enabled = context_;
        renderer_.particles_enabled = false;
        renderer_.select_draw(-1);
        auto texture = renderer_.render(width, height, view, projection, true, false, 0, false);
        renderer_.spatial.selected = old_selected;
        renderer_.spatial.opacity = old_opacity;
        renderer_.spatial.enabled = enabled;
        renderer_.spatial.filled = old_filled;
        renderer_.spatial.edges = old_edges;
        renderer_.spatial.xray = old_xray;
        renderer_.spatial.selected_only = old_only;
        renderer_.spatial.attribute_colors = old_attribute_colors;
        renderer_.geometry_enabled = old_geometry;
        renderer_.particles_enabled = old_particles;
        bool flip = bgfx::getCaps()->originBottomLeft;
        ImGui::Image(ImTextureID(ImGuiRenderer::image_id(texture, false)), size,
                     {0, flip ? 1.f : 0.f}, {1, flip ? 0.f : 1.f});
        bool hovered = ImGui::IsItemHovered();
        auto origin = ImGui::GetItemRectMin();
        bool captured = viewport(view, projection, origin, size, hovered);
        auto &io = ImGui::GetIO();
        if (!captured && hovered && !io.WantTextInput) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
                camera_.rotate(io.MouseDelta.x, io.MouseDelta.y, false);
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
                camera_.pan(io.MouseDelta.x, io.MouseDelta.y);
            camera_.wheel(io.MouseWheel, false);
        }
        ImGui::End();
    }
    ImGui::Begin("Collision status");
    if (loading)
        ImGui::TextUnformatted("Loading map; collision editing is paused.");
    if (!message_.empty())
        ImGui::TextWrapped("%s", message_.c_str());
    ImGui::TextWrapped(
        "Click selects; Shift adds/removes. 1/2: triangles/vertices. W/E: move/rotate. B: box. "
        "Ctrl snaps; Escape cancels. Middle drag orbits, right drag pans, wheel zooms.");
    ImGui::TextWrapped("%s", authored_changed_
                                 ? "Editing only authored collision. Original map scenery is "
                                   "reference. Terrain and custom collision move independently; "
                                   "regenerate from Authoring to follow terrain again."
                                 : "Collision edits do not move the visual terrain. Export writes "
                                   "a separate archive; shared resources affect every area using "
                                   "them. In-game validation is still required.");
    if (document_ && studio::TutorialWidgets::Button(
                         "collision_editor",
                         authored_changed_ ? "Return to Authoring" : "Choose another map in Maps"))
        go_maps = true;
    ImGui::End();
    return go_maps;
}
void CollisionEditor::bind_project() {
    if (!document_)
        return;
    project_.bind(
        "collision", "collision/" + std::to_string(area_), "Collision " + std::to_string(area_),
        std::to_string(area_),
        [this] {
            return document_ && document_->dirty();
        },
        [this] {
            require(!busy() && !document_->previewing(),
                    "Finish the collision operation before saving");
            return project_text(document_->serialize());
        },
        [this] {
            document_->mark_saved();
        });
    project_.ready([this] {
        if (document_) {
            require(!busy() && !document_->previewing(),
                    "Finish the collision operation before saving");
        }
    });
    if (auto file = project_.document(); !file.empty()) {
        open_patch(file);
        project_.restored();
    }
}
}
