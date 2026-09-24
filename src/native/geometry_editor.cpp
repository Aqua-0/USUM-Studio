#include "native/viewport_navigation.h"
#include "native/tutorial_widgets.h"
#include "native/geometry_editor.h"
#include "native/imgui_renderer.h"
#include <cctype>
#include <algorithm>
#include <cmath>
#include <limits>
namespace studio {
namespace {
using Point = std::array<float, 3>;
bool can_paint(const SkinMesh &mesh) {
    return mesh.influences > 1 && !mesh.vertices.empty() && mesh.vertices[0].index_offset &&
           mesh.vertices[0].weight_offset && mesh.vertices[0].index_elements >= 2 &&
           mesh.vertices[0].weight_elements >= 2;
}
Point sub(Point a, Point b) {
    for (unsigned k = 0; k < 3; ++k)
        a[k] -= b[k];
    return a;
}
float dot(Point a, Point b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Point cross(Point a, Point b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float distance(ImVec2 p, ImVec2 a, ImVec2 b) {
    float x = b.x - a.x, y = b.y - a.y,
          t = std::clamp(((p.x - a.x) * x + (p.y - a.y) * y) / std::max(x * x + y * y, .001f), 0.f,
                         1.f);
    return std::hypot(p.x - a.x - t * x, p.y - a.y - t * y);
}
}
GeometryEditor::~GeometryEditor() {
    if (bgfx::isValid(overlay_))
        bgfx::destroy(overlay_);
}
std::size_t GeometryEditor::element_count(std::size_t m) const {
    if (!model_)
        return 0;
    auto &mesh = model_->meshes[m];
    return selection_mode_ == 0   ? mesh.vertices.size()
           : selection_mode_ == 1 ? edges_[m].size()
                                  : mesh.indices.size() / 3;
}
std::size_t GeometryEditor::selected_count() const {
    std::size_t count = 0;
    for (auto &[m, vertices] : selected_)
        count += vertices.size();
    return count;
}
void GeometryEditor::update_selection() {
    selected_.clear();
    if (model_)
        for (auto &[m, elements] : elements_)
            if (editable_.contains(m) && !elements.empty())
                selected_[m] = mesh_selection_vertices(model_->meshes[m], unsigned(selection_mode_),
                                                       elements, seams_);
}
void GeometryEditor::select_all(EnvironmentRenderer &renderer) {
    elements_.clear();
    for (auto m : editable_)
        if (renderer.draw_visible(m))
            for (std::size_t i = 0; i < element_count(m); ++i)
                elements_[m].insert(i);
    update_selection();
}
MeshInfluences GeometryEditor::influences(EnvironmentRenderer &renderer) const {
    std::set<std::size_t> enabled;
    for (auto m : editable_)
        if (renderer.draw_visible(m))
            enabled.insert(m);
    return mesh_selection_influences(*model_, selected_, enabled,
                                     proportional_ ? influence_radius_ : 0);
}
void GeometryEditor::clear_test_pose(ModelDocument &preview, EnvironmentRenderer &renderer) {
    if (!test_scene_)
        return;
    std::vector<bool> shown;
    for (unsigned m = 0; m < preview.scene->draws.size(); ++m)
        shown.push_back(renderer.draw_visible(m));
    renderer.set_scene(preview.scene);
    for (unsigned m = 0; m < shown.size(); ++m)
        renderer.set_draw_visible(m, shown[m]);
    test_scene_.reset();
    test_source_.reset();
}
void GeometryEditor::test_pose(ModelDocument &preview, EnvironmentRenderer &renderer) {
    if (!test_bone_ || task_ != 1 || preview.motion >= 0) {
        clear_test_pose(preview, renderer);
        return;
    }
    if (!test_scene_ || test_source_ != preview.scene) {
        clear_test_pose(preview, renderer);
        test_scene_ = std::make_shared<Environment>(*preview.scene);
        test_source_ = preview.scene;
        std::vector<bool> shown;
        for (unsigned m = 0; m < preview.scene->draws.size(); ++m)
            shown.push_back(renderer.draw_visible(m));
        renderer.set_scene(test_scene_);
        for (unsigned m = 0; m < shown.size(); ++m)
            renderer.set_draw_visible(m, shown[m]);
    }
    test_scene_->skeletons[0] = bone_test_skeleton(preview.scene->skeletons[0], unsigned(bone_),
                                                   unsigned(test_axis_), test_angle_);
}
void GeometryEditor::sync(MaterialDocument &doc, ModelDocument &preview) {
    if (identity_ == doc.identity() && version_ == doc.model_revision() &&
        cached_scene_ == doc.model.scene)
        return;
    bool same = identity_ == doc.identity();
    identity_ = doc.identity();
    version_ = doc.model_revision();
    cached_scene_ = doc.model.scene;
    axis_ = -1;
    painting_ = box_drag_ = false;
    auto previous = std::move(model_);
    model_.reset();
    if (!same) {
        bone_ = 0;
        transfer_for_ = -1;
        test_bone_ = false;
        editable_.clear();
        selected_.clear();
        elements_.clear();
    }
    error_.clear();
    try {
        model_ = doc.geometry();
        require(model_->meshes.size() <= preview.scene->draws.size(),
                "Native meshes could not be matched to the preview");
        for (unsigned i = 0; i < model_->meshes.size(); ++i) {
            auto &a = model_->meshes[i];
            auto &b = preview.scene->draws[i];
            require(a.vertices.size() == b.vertices.size() && a.indices == b.indices,
                    "Native mesh order differs from the preview; this model cannot be edited yet");
        }
        if (!same)
            for (unsigned i = 0; i < model_->meshes.size(); ++i)
                editable_.insert(i);
        std::erase_if(editable_, [&](auto m) {
            return m >= model_->meshes.size();
        });
        bone_ = model_->joints.empty() ? -1 : std::clamp(bone_, 0, int(model_->joints.size()) - 1);
        if (model_->joints.empty() && task_ == 1)
            task_ = 0;
        bool topology_changed = previous && previous->meshes.size() != model_->meshes.size();
        if (previous && !topology_changed)
            for (unsigned i = 0; i < model_->meshes.size(); ++i)
                topology_changed |= previous->meshes[i].indices != model_->meshes[i].indices;
        if (topology_changed) {
            elements_.clear();
            selected_.clear();
            editable_.clear();
            for (unsigned i = 0; i < model_->meshes.size(); ++i)
                editable_.insert(i);
        }
        edges_.clear();
        for (auto &mesh : model_->meshes)
            edges_.push_back(mesh_edges(mesh));
        std::erase_if(elements_, [&](auto &entry) {
            return !editable_.contains(entry.first);
        });
        for (auto &[m, elements] : elements_)
            std::erase_if(elements, [&](auto i) {
                return i >= element_count(m);
            });
        update_selection();
    } catch (const std::exception &e) {
        model_.reset();
        error_ = e.what();
    }
}
void GeometryEditor::display(ModelDocument &preview, EnvironmentRenderer &renderer,
                             const SkinnedModel &model) {
    for (std::size_t m = 0; m < model.meshes.size(); ++m) {
        auto &mesh = model.meshes[m];
        auto vertices = preview.scene->draws.at(m).vertices;
        for (unsigned i = 0; i < vertices.size(); ++i) {
            vertices[i].x = mesh.vertices[i].position[0];
            vertices[i].y = mesh.vertices[i].position[1];
            vertices[i].z = mesh.vertices[i].position[2];
            auto &v = mesh.vertices[i];
            auto normalized = [](Point p) {
                float l = std::hypot(p[0], p[1], p[2]);
                if (l > 1e-8f)
                    for (auto &x : p)
                        x /= l;
                return p;
            };
            if (v.normal_offset) {
                auto n = normalized(v.normal);
                vertices[i].nx = n[0];
                vertices[i].ny = n[1];
                vertices[i].nz = n[2];
            }
            if (v.tangent_offset) {
                auto n = normalized(v.tangent);
                vertices[i].tx = n[0];
                vertices[i].ty = n[1];
                vertices[i].tz = n[2];
            }
        }
        renderer.preview_vertices(m, vertices);
    }
}
void GeometryEditor::apply(MaterialDocument &doc, ModelDocument &preview,
                           EnvironmentRenderer &renderer, const SkinnedModel &next) {
    test_scene_.reset();
    test_source_.reset();
    auto selected_motion = preview.motion;
    std::vector<bool> visible;
    for (unsigned i = 0; i < preview.scene->draws.size(); ++i)
        visible.push_back(renderer.draw_visible(i));
    doc.edit_geometry(next);
    preview = doc.model;
    preview.select_motion(selected_motion);
    renderer.set_scene(preview.scene);
    for (unsigned i = 0; i < visible.size(); ++i)
        renderer.set_draw_visible(i, visible[i]);
    version_ = ~std::uint64_t(0);
    sync(doc, preview);
}
void GeometryEditor::material_panel(MaterialDocument &doc, ModelDocument &preview,
                                    EnvironmentRenderer &renderer, int &material) {
    material_ = std::clamp(material, 0, int(preview.scene->materials.size()) - 1);
    ImGui::SeparatorText("Face material");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##face-material", preview.scene->materials[material_].name.c_str())) {
        for (unsigned i = 0; i < preview.scene->materials.size(); ++i)
            if (ImGui::Selectable(preview.scene->materials[i].name.c_str(), material_ == int(i)))
                material_ = int(i);
        ImGui::EndCombo();
    }
    material = material_;
    std::size_t total = 0;
    for (auto &draw : preview.scene->draws)
        if (draw.material == std::size_t(material_))
            total += draw.indices.size() / 3;
    ImGui::Text("%zu faces use this material", total);
    studio::TutorialWidgets::Checkbox("geometry_editor", "Highlight assigned faces",
                                      &material_highlight_);
    if (studio::TutorialWidgets::Button("geometry_editor", "Select assigned faces", {-1, 0})) {
        elements_.clear();
        for (auto m : editable_)
            if (renderer.draw_visible(m) &&
                preview.scene->draws[m].material == std::size_t(material_))
                for (std::size_t face = 0; face < model_->meshes[m].indices.size() / 3; ++face)
                    elements_[m].insert(face);
        update_selection();
    }
    ImGui::BeginDisabled(selected_.empty() || preview.motion >= 0);
    if (studio::TutorialWidgets::Button("geometry_editor", "Assign selected faces", {-1, 0}))
        try {
            std::vector<bool> visible;
            for (unsigned i = 0; i < preview.scene->draws.size(); ++i)
                visible.push_back(renderer.draw_visible(i));
            auto enabled = editable_;
            auto motion = preview.motion;
            auto edit = doc.assign_material_faces(elements_, std::size_t(material_));
            preview = doc.model;
            preview.select_motion(motion);
            test_scene_.reset();
            test_source_.reset();
            renderer.set_scene(preview.scene);
            version_ = ~std::uint64_t(0);
            sync(doc, preview);
            editable_.clear();
            for (unsigned i = 0; i < edit.sources.size(); ++i) {
                bool show = false, enable = false;
                for (auto source : edit.sources[i]) {
                    show |= visible.at(source);
                    enable |= enabled.contains(source);
                }
                renderer.set_draw_visible(i, show);
                if (enable)
                    editable_.insert(i);
            }
            elements_ = std::move(edit.selected);
            update_selection();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    ImGui::EndDisabled();
    ImGui::TextWrapped(
        "Cyan: material coverage. Orange: selected faces. Assign replaces their previous material. "
        "To remove faces from a material, assign them to another.");
}
void GeometryEditor::panel(MaterialDocument &doc, ModelDocument &preview,
                           EnvironmentRenderer &renderer, bool &playing, int &material) {
    sync(doc, preview);
    if (doc.model.area >= 0 && !doc.model.project_asset)
        ImGui::TextWrapped("Edits affect all placements using this model. Collision is edited separately.");
    if (!model_) {
        ImGui::TextWrapped("%s", error_.c_str());
        return;
    }
    std::erase_if(elements_, [&](auto &entry) {
        return !renderer.draw_visible(entry.first);
    });
    update_selection();
    ImGui::BeginDisabled(axis_ >= 0 || painting_ || box_drag_);
    auto mesh_label = editable_.size() == 1 ? model_->meshes[*editable_.begin()].name
                                            : std::to_string(editable_.size()) + " meshes enabled";
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##geometry-mesh", mesh_label.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (studio::TutorialWidgets::Button("geometry_editor", "All visible")) {
            editable_.clear();
            for (unsigned i = 0; i < model_->meshes.size(); ++i)
                if (renderer.draw_visible(i))
                    editable_.insert(i);
            update_selection();
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("geometry_editor", "None")) {
            editable_.clear();
            elements_.clear();
            update_selection();
        }
        ImGui::TextDisabled("Check meshes to edit together");
        for (unsigned i = 0; i < model_->meshes.size(); ++i) {
            bool enabled = editable_.contains(i);
            ImGui::PushID(int(i));
            if (studio::TutorialWidgets::Checkbox("geometry_editor", model_->meshes[i].name.c_str(),
                                                  &enabled)) {
                if (enabled)
                    editable_.insert(i);
                else {
                    editable_.erase(i);
                    elements_.erase(i);
                }
                update_selection();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    studio::TutorialWidgets::RadioButton("geometry_editor", "Geometry", &task_, 0);
    ImGui::SameLine();
    ImGui::BeginDisabled(model_->joints.empty());
    studio::TutorialWidgets::RadioButton("geometry_editor", "Weights", &task_, 1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (studio::TutorialWidgets::RadioButton("geometry_editor", "Materials", &task_, 2)) {
        if (selection_mode_ != 2)
            elements_.clear();
        selection_mode_ = 2;
        editable_.clear();
        for (unsigned i = 0; i < model_->meshes.size(); ++i)
            if (renderer.draw_visible(i))
                editable_.insert(i);
        update_selection();
    }
    if (preview.motion >= 0) {
        ImGui::TextWrapped("Motion preview. Return to bind pose to edit.");
        if (studio::TutorialWidgets::Button("geometry_editor", "Edit in bind pose")) {
            preview.select_motion(-1);
            renderer.refresh_materials();
            renderer.playback.seconds = 0;
            playing = false;
        }
    }
    if (task_ != 1) {
        if (task_ == 0) {
            int old = selection_mode_;
            studio::TutorialWidgets::RadioButton("geometry_editor", "Vertex (1)", &selection_mode_,
                                                 0);
            ImGui::SameLine();
            studio::TutorialWidgets::RadioButton("geometry_editor", "Edge (2)", &selection_mode_,
                                                 1);
            ImGui::SameLine();
            studio::TutorialWidgets::RadioButton("geometry_editor", "Face (3)", &selection_mode_,
                                                 2);
            if (old != selection_mode_) {
                elements_.clear();
                update_selection();
            }
        } else
            selection_mode_ = 2;
        if (task_ == 2)
            material_panel(doc, preview, renderer, material);
        ImGui::Text("%zu %s | %zu vertices", ([&] {
                        std::size_t count = 0;
                        for (auto &[m, e] : elements_)
                            count += e.size();
                        return count;
                    })(),
                    selection_mode_ == 0   ? "vertices selected"
                    : selection_mode_ == 1 ? "edges selected"
                                           : "faces selected",
                    selected_count());
        if (studio::TutorialWidgets::Button("geometry_editor", "Select all")) {
            select_all(renderer);
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("geometry_editor", "Clear selection")) {
            elements_.clear();
            update_selection();
        }
        ImGui::SameLine();
        studio::TutorialWidgets::Checkbox("geometry_editor", "Box (B)", &box_tool_);
        studio::TutorialWidgets::Checkbox("geometry_editor", "X-ray selection", &through_);
        studio::TutorialWidgets::Checkbox("geometry_editor", "Triangle outlines", &wire_);
        studio::TutorialWidgets::Checkbox("geometry_editor", "All visible meshes", &all_triangles_);
        if (studio::TutorialWidgets::Checkbox("geometry_editor", "Keep seams together", &seams_))
            update_selection();
        if (ImGui::CollapsingHeader("Controls"))
            ImGui::TextWrapped("Click: select | Ctrl: add/remove | B: box\nMiddle drag: orbit | "
                               "Shift+middle: pan | RMB+WASD: fly");
        if (studio::TutorialWidgets::Button("geometry_editor", "Isolate enabled"))
            for (unsigned i = 0; i < preview.scene->draws.size(); ++i)
                renderer.set_draw_visible(i, editable_.contains(i));
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("geometry_editor", "Show all"))
            renderer.show_all_draws();
        if (task_ == 0) {
            studio::TutorialWidgets::Checkbox("geometry_editor", "Proportional editing",
                                              &proportional_);
            if (proportional_) {
                ImGui::SetNextItemWidth(120);
                ImGui::DragFloat("Influence radius", &influence_radius_, .5f, 0, 100000, "%.1f");
                ImGui::TextWrapped("Nearby vertices use smooth falloff. Scroll while dragging a "
                                   "handle to grow or shrink the radius.");
            }
            ImGui::SeparatorText("Transform selection");
            studio::TutorialWidgets::RadioButton("geometry_editor", "Move (G)", &mode_, 0);
            ImGui::SameLine();
            studio::TutorialWidgets::RadioButton("geometry_editor", "Rotate (R)", &mode_, 1);
            ImGui::SameLine();
            studio::TutorialWidgets::RadioButton("geometry_editor", "Scale (S)", &mode_, 2);
            ImGui::TextWrapped("G / R / S: tool | Ctrl: snap | Esc: cancel");
            if (studio::TutorialWidgets::TreeNode("geometry_editor", "Numeric transform")) {
                ImGui::TextUnformatted("Move");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat3("##geometry-move", move_.data());
                ImGui::TextUnformatted("Rotate degrees");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat3("##geometry-rotate", rotate_.data());
                ImGui::TextUnformatted("Scale");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat3("##geometry-scale", scale_.data());
                ImGui::BeginDisabled(selected_.empty() || preview.motion >= 0);
                if (studio::TutorialWidgets::Button("geometry_editor", "Apply transform"))
                    try {
                        auto next = *model_;
                        affected_ = influences(renderer);
                        transform_mesh_influences(next, selected_, affected_, move_, rotate_,
                                                  scale_);
                        if (recompute_)
                            for (auto &[m, v] : affected_)
                                rebuild_mesh_normals(next.meshes[m]);
                        apply(doc, preview, renderer, next);
                        move_ = rotate_ = {};
                        scale_ = {1, 1, 1};
                    } catch (const std::exception &e) {
                        error_ = e.what();
                    }
                ImGui::EndDisabled();
                ImGui::TreePop();
            }
            ImGui::SetNextItemWidth(80);
            ImGui::Combo("Flatten axis", &flatten_axis_, "X\0Y\0Z\0");
            ImGui::BeginDisabled(selected_.empty() || preview.motion >= 0);
            if (studio::TutorialWidgets::Button("geometry_editor", "Flatten selection"))
                try {
                    auto next = *model_;
                    affected_ = influences(renderer);
                    flatten_mesh_influences(next, selected_, affected_, unsigned(flatten_axis_));
                    if (recompute_)
                        for (auto &[m, v] : affected_)
                            rebuild_mesh_normals(next.meshes[m]);
                    apply(doc, preview, renderer, next);
                } catch (const std::exception &e) {
                    error_ = e.what();
                }
            ImGui::EndDisabled();
            studio::TutorialWidgets::Checkbox("geometry_editor", "Recalculate normals",
                                              &recompute_);
        }
    } else {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##weight-bone", model_->joints[bone_].name.c_str())) {
            ImGui::InputTextWithHint("##bone-filter", "Find bone", bone_search_,
                                     sizeof(bone_search_));
            auto lower = [](std::string s) {
                for (auto &c : s)
                    c = char(std::tolower(static_cast<unsigned char>(c)));
                return s;
            };
            auto search = lower(bone_search_);
            for (unsigned i = 0; i < model_->joints.size(); ++i)
                if (lower(model_->joints[i].name).find(search) != std::string::npos &&
                    ImGui::Selectable(model_->joints[i].name.c_str(), bone_ == int(i)))
                    bone_ = int(i);
            ImGui::EndCombo();
        }
        if (transfer_for_ != bone_) {
            transfer_for_ = bone_;
            transfer_bone_ = model_->joints[bone_].parent;
            if (transfer_bone_ < 0)
                for (unsigned i = 0; i < model_->joints.size(); ++i)
                    if (int(i) != bone_) {
                        transfer_bone_ = int(i);
                        break;
                    }
        }
        if (studio::TutorialWidgets::Checkbox("geometry_editor", "Test selected bone",
                                              &test_bone_) &&
            test_bone_) {
            preview.select_motion(-1);
            renderer.playback.seconds = 0;
            playing = false;
        }
        if (test_bone_) {
            ImGui::SetNextItemWidth(80);
            ImGui::Combo("Test axis", &test_axis_, "X\0Y\0Z\0");
            ImGui::SliderFloat("Test angle", &test_angle_, -90, 90, "%.0f deg");
            if (studio::TutorialWidgets::Button("geometry_editor", "Reset test"))
                test_angle_ = 0;
            ImGui::TextWrapped(
                "Temporary pose; not saved or exported. Turn off to paint. Includes child bones.");
        }
        ImGui::TextWrapped("Surface colors: blue = 0, green = half, red = full weight.");
        ImGui::SliderFloat("Target weight", &weight_, 0, 1, "%.3f");
        ImGui::SliderFloat("Radius", &brush_radius_, 5, 150, "%.0f px");
        ImGui::SliderFloat("Strength", &brush_strength_, .01f, 1, "%.2f");
        studio::TutorialWidgets::Checkbox("geometry_editor", "Limit brush to selection",
                                          &brush_mask_);
        studio::TutorialWidgets::Checkbox("geometry_editor", "Show heatmap", &weights_);
        ImGui::TextUnformatted("Brush fallback bone");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##weight-transfer", transfer_bone_ >= 0
                                                       ? model_->joints[transfer_bone_].name.c_str()
                                                       : "No other bone available")) {
            for (unsigned i = 0; i < model_->joints.size(); ++i)
                if (int(i) != bone_ &&
                    ImGui::Selectable(model_->joints[i].name.c_str(), transfer_bone_ == int(i)))
                    transfer_bone_ = int(i);
            ImGui::EndCombo();
        }
        ImGui::TextWrapped(
            "Removed weight goes to this bone only when the vertex has no other influence.");
        bool paintable = false;
        for (auto m : editable_)
            paintable |= can_paint(model_->meshes[m]);
        if (test_bone_)
            ImGui::TextDisabled("Painting paused during bone test.");
        else if (paintable)
            ImGui::TextWrapped("Drag on the surface to paint. Ctrl paints toward zero. Strength "
                               "applies per stroke; Escape cancels. Middle drag orbits.");
        else
            ImGui::TextWrapped(
                "Enabled meshes have rigid or shared skin attributes. Use attachment controls "
                "below; per-vertex painting requires a blended mesh.");
        ImGui::TextWrapped(
            "The brush skips rigid/shared meshes. Other weights are normalized. The strongest "
            "remaining influences are kept within this mesh's native limit.");
        if (studio::TutorialWidgets::TreeNode("geometry_editor",
                                              "Selection and whole-mesh attachment")) {
            ImGui::Text("%zu selected vertices", selected_count());
            if (studio::TutorialWidgets::Button("geometry_editor", "Select enabled meshes")) {
                select_all(renderer);
            }
            ImGui::BeginDisabled(selected_.empty());
            bool assign = studio::TutorialWidgets::Button("geometry_editor", "Set selected weight");
            ImGui::SameLine();
            bool attach = studio::TutorialWidgets::Button("geometry_editor", "Attach 100%");
            if (assign || attach)
                try {
                    auto next = *model_;
                    for (auto &[m, vertices] : selected_)
                        set_vertex_weight(next, m, vertices, unsigned(bone_),
                                          attach ? 1.f : weight_);
                    apply(doc, preview, renderer, next);
                } catch (const std::exception &e) {
                    error_ = e.what();
                }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
    }
    ImGui::EndDisabled();
    if (preview.motion >= 0)
        test_bone_ = false;
    test_pose(preview, renderer);
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
}
bool GeometryEditor::viewport(MaterialDocument &doc, ModelDocument &preview,
                              EnvironmentRenderer &renderer, const ViewportCamera &camera,
                              const float *view, const float *projection, ImVec2 origin,
                              ImVec2 size, bool hovered) {
    sync(doc, preview);
    if (!model_ || !renderer.ready() || size.x <= 0 || size.y <= 0)
        return false;
    auto &io = ImGui::GetIO();
    bool input =
        !io.WantTextInput && !io.AppFocusLost &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    auto cancel = [&]() {
        if (axis_ >= 0) {
            display(preview, renderer, *model_);
            axis_ = -1;
        }
        painting_ = box_drag_ = false;
        blocked_ = ImGui::IsMouseDown(0);
    };
    if ((axis_ >= 0 || painting_ || box_drag_) &&
        (!input || preview.motion >= 0 || ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        cancel();
        return true;
    }
    if (blocked_) {
        blocked_ = ImGui::IsMouseDown(0);
        return true;
    }
    for (auto it = elements_.begin(); it != elements_.end();)
        if (!renderer.draw_visible(it->first))
            it = elements_.erase(it);
        else
            ++it;
    update_selection();
    bool editing = preview.motion < 0 && !test_scene_ && !editable_.empty();
    if (input && editing && task_ != 1 && hovered && axis_ < 0 && !box_drag_ && !io.KeyCtrl &&
        !viewport_navigating()) {
        if (ImGui::IsKeyPressed(ImGuiKey_G))
            mode_ = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_R))
            mode_ = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_S))
            mode_ = 2;
        if (ImGui::IsKeyPressed(ImGuiKey_B))
            box_tool_ = !box_tool_;
        int previous = selection_mode_;
        if (task_ == 0 && ImGui::IsKeyPressed(ImGuiKey_1))
            selection_mode_ = 0;
        if (task_ == 0 && ImGui::IsKeyPressed(ImGuiKey_2))
            selection_mode_ = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_3))
            selection_mode_ = 2;
        if (previous != selection_mode_) {
            elements_.clear();
            update_selection();
        }
    }
    bool radius_changed = false;
    if (axis_ >= 0 && proportional_ && io.MouseWheel != 0) {
        float old = influence_radius_;
        influence_radius_ =
            std::clamp(old + io.MouseWheel * std::max(1.f, old * .1f), 0.f, 100000.f);
        radius_changed = old != influence_radius_;
    }
    auto project = [&](Point p, ImVec2 &screen) {
        float a[4]{}, b[4]{};
        for (unsigned r = 0; r < 4; ++r)
            a[r] = view[r] * p[0] + view[4 + r] * p[1] + view[8 + r] * p[2] + view[12 + r];
        for (unsigned r = 0; r < 4; ++r)
            for (unsigned c = 0; c < 4; ++c)
                b[r] += projection[c * 4 + r] * a[c];
        if (b[3] <= .001f)
            return false;
        screen = {origin.x + (b[0] / b[3] + 1) * size.x * .5f,
                  origin.y + (1 - b[1] / b[3]) * size.y * .5f};
        return std::isfinite(screen.x) && std::isfinite(screen.y);
    };
    auto eye = camera.eye(), ray = camera.forward(), right = camera.right(), up = camera.up();
    float nx = (2 * (io.MousePos.x - origin.x) / size.x - 1) * .41421356f * size.x / size.y,
          ny = (1 - 2 * (io.MousePos.y - origin.y) / size.y) * .41421356f;
    for (unsigned k = 0; k < 3; ++k)
        ray[k] += right[k] * nx + up[k] * ny;
    float magnitude = std::sqrt(dot(ray, ray));
    for (auto &x : ray)
        x /= magnitude;
    Point pivot{};
    for (auto &[m, vertices] : selected_)
        for (auto i : vertices)
            for (unsigned k = 0; k < 3; ++k)
                pivot[k] += model_->meshes[m].vertices[i].position[k] / float(selected_count());
    float length = std::max(dot(sub(pivot, eye), camera.forward()) * .13f, .1f);
    if (axis_ >= 0) {
        pivot = pivot_;
        length = length_;
    }
    auto parameter = [&](unsigned axis) {
        auto w = sub(eye, pivot);
        float parallel = ray[axis], denominator = 1 - parallel * parallel;
        return denominator < .001f ? std::numeric_limits<float>::quiet_NaN()
                                   : (w[axis] - parallel * dot(ray, w)) / denominator;
    };
    auto angle = [&](unsigned axis) {
        auto w = sub(eye, pivot);
        if (std::abs(ray[axis]) < .001f)
            return std::numeric_limits<float>::quiet_NaN();
        float t = -w[axis] / ray[axis];
        if (t <= 0)
            return std::numeric_limits<float>::quiet_NaN();
        auto u = (axis + 1) % 3, v = (axis + 2) % 3;
        return std::atan2(w[v] + ray[v] * t, w[u] + ray[u] * t);
    };
    SkinnedModel visible_model;
    visible_model.meshes = model_->meshes;
    if (axis_ >= 0 || painting_)
        visible_model.meshes = dragged_.meshes;
    auto poses =
        test_scene_ ? evaluate_skeleton(test_scene_->skeletons[0], 0, 12, true)
        : preview.motion >= 0 && !preview.scene->skeletons.empty()
            ? evaluate_skeleton(preview.scene->skeletons[0], renderer.playback.seconds, 12, true)
            : std::vector<Matrix>{};
    std::vector<bool> shown;
    std::vector<unsigned> culls;
    for (unsigned m = 0; m < visible_model.meshes.size(); ++m) {
        shown.push_back(renderer.draw_visible(m));
        culls.push_back(preview.scene->materials.at(preview.scene->draws[m].material).cull);
    }
    float resolution = std::min(1.f, 512.f / std::max(size.x, size.y));
    unsigned surface_width = unsigned(std::max(1.f, size.x * resolution)),
             surface_height = unsigned(std::max(1.f, size.y * resolution));
    surface_.build(visible_model, shown, view, projection, surface_width, surface_height, poses,
                   culls);
    auto screen = [&](MeshProjection p) {
        return ImVec2{origin.x + p.x * size.x, origin.y + p.y * size.y};
    };
    float pointer_x = (io.MousePos.x - origin.x) / size.x,
          pointer_y = (io.MousePos.y - origin.y) / size.y;
    auto exact_face = task_ != 1 ? surface_.pick_face(visible_model, pointer_x, pointer_y,
                                                      editable_, culls, through_)
                                 : std::optional<MeshSurfacePixel>{};
    const auto *pointer = task_ == 1   ? surface_.at(pointer_x, pointer_y)
                          : exact_face ? &*exact_face
                                       : nullptr;
    int hovered_face =
        pointer && pointer->mesh >= 0 && editable_.contains(std::size_t(pointer->mesh))
            ? pointer->face
            : -1;
    affected_ = influences(renderer);
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    Bytes overlay_pixels(std::size_t(surface_width) * surface_height * 4, 0);
    bool overlay_visible = false;
    for (unsigned i = 0; i < surface_.pixels.size(); ++i) {
        auto &pixel = surface_.pixels[i];
        if (pixel.mesh < 0 || (!editable_.contains(std::size_t(pixel.mesh)) && task_ != 2))
            continue;
        auto &visible_mesh = visible_model.meshes[pixel.mesh];
        float r = 0, g = 0, b = 0, alpha = 0;
        if (task_ == 1 && weights_) {
            float w = 0;
            for (unsigned corner = 0; corner < 3; ++corner) {
                auto &v = visible_mesh
                              .vertices[visible_mesh.indices[std::size_t(pixel.face) * 3 + corner]];
                for (unsigned k = 0; k < 4; ++k)
                    if (v.joints[k] == bone_)
                        w += v.weights[k] * pixel.weights[corner];
            }
            w = std::clamp(w, 0.f, 1.f);
            r = w < .5f ? 0 : (w - .5f) * 2;
            g = w < .5f ? .2f + w * 1.6f : 1 - (w - .5f) * 1.8f;
            b = w < .5f ? 1 - w * 2 : 0;
            alpha = .8f;
        } else if (task_ != 1) {
            bool selected = selection_mode_ == 2
                                ? elements_[pixel.mesh].contains(std::size_t(pixel.face))
                                : false;
            if (selected) {
                r = 1;
                g = .55f;
                b = .12f;
                alpha = .4f;
            } else if (selection_mode_ == 2 && hovered && hovered_face == pixel.face &&
                       pointer->mesh == pixel.mesh) {
                r = 1;
                g = .85f;
                b = .55f;
                alpha = .2f;
            } else if (task_ == 2 && material_highlight_ &&
                       preview.scene->draws[pixel.mesh].material == std::size_t(material_)) {
                r = .1f;
                g = .85f;
                b = 1;
                alpha = .35f;
            }
        }
        if (alpha == 0)
            continue;
        overlay_visible = true;
        overlay_pixels[i * 4] = std::uint8_t(r * 255);
        overlay_pixels[i * 4 + 1] = std::uint8_t(g * 255);
        overlay_pixels[i * 4 + 2] = std::uint8_t(b * 255);
        overlay_pixels[i * 4 + 3] = std::uint8_t(alpha * 255);
    }
    if (overlay_visible) {
        if (!bgfx::isValid(overlay_) || overlay_width_ != surface_width ||
            overlay_height_ != surface_height) {
            if (bgfx::isValid(overlay_))
                bgfx::destroy(overlay_);
            overlay_ = bgfx::createTexture2D(
                std::uint16_t(surface_width), std::uint16_t(surface_height), false, 1,
                bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            overlay_width_ = surface_width;
            overlay_height_ = surface_height;
        }
        bgfx::updateTexture2D(overlay_, 0, 0, 0, 0, std::uint16_t(surface_width),
                              std::uint16_t(surface_height),
                              bgfx::copy(overlay_pixels.data(), narrow(overlay_pixels.size())));
        draw->AddImage(ImTextureID(ImGuiRenderer::image_id(overlay_, true)), origin,
                       {origin.x + size.x, origin.y + size.y});
    }
    auto line = [&](MeshProjection a, MeshProjection b, ImU32 color, float thickness) {
        if (!a.inverse_w || !b.inverse_w || !std::isfinite(a.x) || !std::isfinite(a.y) ||
            !std::isfinite(a.z) || !std::isfinite(b.x) || !std::isfinite(b.y) ||
            !std::isfinite(b.z))
            return;
        double low = 0, high = 1;
        auto clip = [&](double first, double last, double padding) {
            double delta = last - first;
            if (delta == 0)
                return first >= -padding && first <= 1 + padding;
            double enter = (-padding - first) / delta, leave = (1 + padding - first) / delta;
            if (enter > leave)
                std::swap(enter, leave);
            low = std::max(low, enter);
            high = std::min(high, leave);
            return low <= high;
        };
        if (!clip(a.x, b.x, thickness / size.x) || !clip(a.y, b.y, thickness / size.y))
            return;
        auto interpolate = [&](double t) {
            return MeshProjection{float(double(a.x) + (double(b.x) - a.x) * t),
                                  float(double(a.y) + (double(b.y) - a.y) * t),
                                  float(double(a.z) + (double(b.z) - a.z) * t), 1};
        };
        auto first = interpolate(low), last = interpolate(high);
        auto start = screen(first), end = screen(last);
        if (through_) {
            draw->AddLine(start, end, color, thickness);
            return;
        }
        unsigned count =
            unsigned(std::clamp(std::hypot(end.x - start.x, end.y - start.y) / 4, 1.f, 400.f));
        int visible_start = -1;
        for (unsigned j = 0; j <= count; ++j) {
            float t = (j + .5f) / count;
            MeshProjection p{first.x + (last.x - first.x) * t, first.y + (last.y - first.y) * t,
                             first.z + (last.z - first.z) * t, 1};
            bool visible = j < count && surface_.visible(p);
            if (visible && visible_start < 0)
                visible_start = int(j);
            if (!visible && visible_start >= 0) {
                float from = float(visible_start) / count, to = float(j) / count;
                draw->AddLine(
                    {start.x + (end.x - start.x) * from, start.y + (end.y - start.y) * from},
                    {start.x + (end.x - start.x) * to, start.y + (end.y - start.y) * to}, color,
                    thickness);
                visible_start = -1;
            }
        }
    };
    for (unsigned m = 0; m < model_->meshes.size(); ++m)
        if (renderer.draw_visible(m) && (all_triangles_ || (task_ != 1 && editable_.contains(m)))) {
            auto &projected = surface_.points[m];
            auto &edges = edges_[m];
            auto &elements = elements_[m];
            const std::set<std::size_t> empty;
            auto found = selected_.find(m);
            auto &selected = found == selected_.end() ? empty : found->second;
            for (unsigned i = 0; i < edges.size(); ++i) {
                auto edge = edges[i];
                bool chosen = task_ != 1 && (selection_mode_ == 1 ? elements.contains(i)
                                                                  : selected.contains(edge[0]) &&
                                                                        selected.contains(edge[1]));
                if (all_triangles_ || wire_ || chosen)
                    line(projected[edge[0]], projected[edge[1]],
                         chosen ? IM_COL32(255, 175, 60, 240) : IM_COL32(30, 45, 52, 155),
                         chosen ? 2.f : 1.f);
            }
            if (task_ == 0 && selection_mode_ == 0 && editable_.contains(m))
                for (unsigned i = 0; i < projected.size(); ++i)
                    if (projected[i].inverse_w && (through_ || surface_.visible(projected[i])))
                        draw->AddCircleFilled(screen(projected[i]),
                                              selected.contains(i) ? 3.f : 1.7f,
                                              selected.contains(i) ? IM_COL32(255, 185, 65, 255)
                                                                   : IM_COL32(50, 65, 75, 210));
        }
    if (task_ == 0 && proportional_ && !selected_.empty()) {
        for (auto &[m, vertices] : affected_)
            for (auto [i, w] : vertices)
                if (!selected_.contains(m) || !selected_.at(m).contains(i)) {
                    auto p = surface_.points[m][i];
                    if (through_ || surface_.visible(p))
                        draw->AddCircleFilled(screen(p), 2,
                                              IM_COL32(255, 180, 60, int(50 + w * 205)));
                }
        ImVec2 center, edge;
        auto outer = pivot;
        for (unsigned k = 0; k < 3; ++k)
            outer[k] += right[k] * influence_radius_;
        if (project(pivot, center) && project(outer, edge))
            draw->AddCircle(center, std::hypot(edge.x - center.x, edge.y - center.y),
                            IM_COL32(255, 195, 90, 160), 64);
    }
    int hot = -1;
    float nearest = 8;
    const ImU32 colors[] = {IM_COL32(245, 95, 85, 255), IM_COL32(100, 235, 130, 255),
                            IM_COL32(95, 155, 255, 255)};
    ImVec2 center;
    if (task_ == 0 && !box_tool_ && editing && !selected_.empty() && project(pivot, center))
        for (unsigned axis = 0; axis < 3; ++axis) {
            auto color = axis_ == int(axis) ? IM_COL32(255, 225, 110, 255) : colors[axis];
            if (mode_ != 1) {
                Point p = pivot;
                p[axis] += length;
                ImVec2 end;
                if (!project(p, end))
                    continue;
                float d = distance(io.MousePos, center, end);
                if (d < nearest && std::hypot(end.x - center.x, end.y - center.y) > 12 &&
                    std::isfinite(parameter(axis))) {
                    nearest = d;
                    hot = int(axis);
                }
                draw->AddLine(center, end, color, 3);
                if (mode_ == 2)
                    draw->AddRectFilled({end.x - 5, end.y - 5}, {end.x + 5, end.y + 5}, color);
                else {
                    float dx = end.x - center.x, dy = end.y - center.y,
                          n = std::max(std::hypot(dx, dy), .001f);
                    dx /= n;
                    dy /= n;
                    draw->AddTriangleFilled(
                        end, {end.x - dx * 12 - dy * 5, end.y - dy * 12 + dx * 5},
                        {end.x - dx * 12 + dy * 5, end.y - dy * 12 - dx * 5}, color);
                }
                draw->AddText({end.x + 5, end.y + 3}, color,
                              axis == 0   ? "X"
                              : axis == 1 ? "Y"
                                          : "Z");
            } else {
                ImVec2 previous{};
                bool valid = false;
                for (unsigned i = 0; i <= 64; ++i) {
                    float t = float(i) * 6.283185307f / 64;
                    Point p = pivot;
                    p[(axis + 1) % 3] += std::cos(t) * length;
                    p[(axis + 2) % 3] += std::sin(t) * length;
                    ImVec2 at{};
                    bool ok = project(p, at);
                    if (ok && valid) {
                        float d = distance(io.MousePos, previous, at);
                        if (d < nearest && std::isfinite(angle(axis))) {
                            nearest = d;
                            hot = int(axis);
                        }
                        draw->AddLine(previous, at, color, 2);
                    }
                    previous = at;
                    valid = ok;
                }
            }
        }
    bool captured = axis_ >= 0 || painting_ || box_drag_ ||
                    (input && hovered && editing && !io.KeyShift &&
                     (ImGui::IsMouseDown(0) || ImGui::IsMouseReleased(0)));
    if (task_ == 1 && hovered && editing) {
        draw->AddCircle(io.MousePos, brush_radius_, IM_COL32(250, 245, 220, 230), 64, 1.5f);
        draw->AddCircle(io.MousePos, 3, IM_COL32(250, 245, 220, 230), 12, 1);
    }
    if (input && hovered && editing && !io.KeyShift && axis_ < 0 && !painting_ && !box_drag_ &&
        ImGui::IsMouseClicked(0)) {
        if (task_ == 1) {
            if (hovered_face >= 0 && can_paint(model_->meshes[pointer->mesh])) {
                painting_ = true;
                brush_target_ = io.KeyCtrl ? 0.f : weight_;
                brush_base_ = *model_;
                dragged_ = *model_;
                brush_coverage_.clear();
                for (auto &mesh : model_->meshes)
                    brush_coverage_.emplace_back(mesh.vertices.size(), 0.f);
                brush_previous_ = io.MousePos;
                error_.clear();
            }
        } else if (box_tool_) {
            box_drag_ = true;
            box_start_ = io.MousePos;
        } else if (hot >= 0) {
            axis_ = hot;
            pivot_ = pivot;
            length_ = length;
            start_ = parameter(unsigned(hot));
            last_angle_ = angle(unsigned(hot));
            angle_ = 0;
            last_amount_ = std::numeric_limits<float>::quiet_NaN();
            dragged_ = *model_;
            error_.clear();
        } else {
            int picked = -1, picked_mesh = -1;
            float best = 9;
            if (selection_mode_ == 2) {
                picked = hovered_face;
                if (picked >= 0)
                    picked_mesh = pointer->mesh;
            } else
                for (auto m : editable_)
                    if (renderer.draw_visible(m)) {
                        auto &projected = surface_.points[m];
                        for (unsigned i = 0; i < element_count(m); ++i) {
                            float d = 0;
                            MeshProjection p;
                            if (selection_mode_ == 0) {
                                p = projected[i];
                                auto at = screen(p);
                                d = std::hypot(at.x - io.MousePos.x, at.y - io.MousePos.y);
                            } else {
                                auto edge = edges_[m][i];
                                auto a = projected[edge[0]], b = projected[edge[1]];
                                if (!a.inverse_w || !b.inverse_w)
                                    continue;
                                auto start = screen(a), end = screen(b);
                                float dx = end.x - start.x, dy = end.y - start.y,
                                      t = std::clamp(((io.MousePos.x - start.x) * dx +
                                                      (io.MousePos.y - start.y) * dy) /
                                                         std::max(dx * dx + dy * dy, .001f),
                                                     0.f, 1.f);
                                p = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                                     a.z + (b.z - a.z) * t, 1};
                                d = distance(io.MousePos, start, end);
                            }
                            if (p.inverse_w && d < best && (through_ || surface_.visible(p))) {
                                best = d;
                                picked = int(i);
                                picked_mesh = int(m);
                            }
                        }
                    }
            if (!io.KeyCtrl || picked < 0)
                elements_.clear();
            if (picked >= 0) {
                auto &elements = elements_[picked_mesh];
                if (!elements.erase(std::size_t(picked)))
                    elements.insert(std::size_t(picked));
            }
            update_selection();
        }
    }
    if (box_drag_) {
        ImVec2 low{std::min(box_start_.x, io.MousePos.x), std::min(box_start_.y, io.MousePos.y)},
            high{std::max(box_start_.x, io.MousePos.x), std::max(box_start_.y, io.MousePos.y)};
        draw->AddRectFilled(low, high, IM_COL32(255, 185, 75, 25));
        draw->AddRect(low, high, IM_COL32(255, 200, 100, 230));
        if (ImGui::IsMouseReleased(0) || !ImGui::IsMouseDown(0)) {
            if (!io.KeyCtrl)
                elements_.clear();
            bool picked_any = false;
            for (auto m : editable_)
                if (renderer.draw_visible(m)) {
                    auto &mesh = model_->meshes[m];
                    auto &projected = surface_.points[m];
                    for (unsigned i = 0; i < element_count(m); ++i) {
                        MeshProjection p{};
                        unsigned count = selection_mode_ == 0 ? 1 : selection_mode_ == 1 ? 2 : 3;
                        bool valid = true;
                        for (unsigned k = 0; k < count; ++k) {
                            auto index = selection_mode_ == 0   ? i
                                         : selection_mode_ == 1 ? edges_[m][i][k]
                                                                : mesh.indices[i * 3 + k];
                            auto v = projected[index];
                            valid &= v.inverse_w > 0;
                            p.x += v.x / count;
                            p.y += v.y / count;
                            p.z += v.z / count;
                        }
                        p.inverse_w = valid ? 1.f : 0.f;
                        auto at = screen(p);
                        if (valid && at.x >= low.x && at.x <= high.x && at.y >= low.y &&
                            at.y <= high.y && (through_ || surface_.visible(p))) {
                            elements_[m].insert(i);
                            picked_any = true;
                        }
                    }
                }
            if (!picked_any && std::hypot(high.x - low.x, high.y - low.y) < io.MouseDragThreshold)
                elements_.clear();
            update_selection();
            box_drag_ = false;
            box_tool_ = false;
        }
    }
    if (painting_)
        try {
            for (auto m : editable_)
                if (renderer.draw_visible(m) && can_paint(model_->meshes[m])) {
                    auto &mesh = model_->meshes[m];
                    auto &projected = surface_.points[m];
                    for (unsigned i = 0; i < mesh.vertices.size(); ++i) {
                        if (brush_mask_ && (!selected_.contains(m) || !selected_.at(m).contains(i)))
                            continue;
                        auto p = projected[i];
                        if (!surface_.visible(p))
                            continue;
                        auto at = screen(p);
                        float d = distance(at, brush_previous_, io.MousePos);
                        if (d >= brush_radius_)
                            continue;
                        float falloff = 1 - d / brush_radius_;
                        float strength = brush_strength_ * falloff * falloff * (3 - 2 * falloff);
                        if (strength <= brush_coverage_[m][i])
                            continue;
                        brush_coverage_[m][i] = strength;
                        auto vertex = brush_base_.meshes[m].vertices[i];
                        paint_vertex_weight(vertex, mesh.influences, unsigned(bone_), brush_target_,
                                            strength, transfer_bone_);
                        dragged_.meshes[m].vertices[i] = vertex;
                    }
                }
            brush_previous_ = io.MousePos;
            if (ImGui::IsMouseReleased(0) || !ImGui::IsMouseDown(0)) {
                apply(doc, preview, renderer, dragged_);
                painting_ = false;
            }
        } catch (const std::exception &e) {
            cancel();
            error_ = e.what();
        }
    draw->PopClipRect();
    if (axis_ >= 0)
        try {
            float amount = 0;
            Point translation{}, rotation{}, scale{1, 1, 1};
            if (mode_ == 1) {
                float current = angle(unsigned(axis_));
                angle_ += std::remainder(current - last_angle_, 6.283185307f);
                last_angle_ = current;
                amount = angle_ * 57.29577951f;
                if (io.KeyCtrl)
                    amount = std::round(amount / 15) * 15;
                rotation[axis_] = amount;
            } else {
                amount = parameter(unsigned(axis_)) - start_;
                if (mode_ == 0) {
                    if (io.KeyCtrl)
                        amount = std::round(amount);
                    translation[axis_] = amount;
                } else {
                    amount = std::max(.001f, 1 + amount / length_);
                    if (io.KeyCtrl)
                        amount = std::max(.1f, std::round(amount * 10) / 10);
                    scale[axis_] = amount;
                }
            }
            require(std::isfinite(amount), "View is parallel to this handle; orbit and try again");
            if (amount != last_amount_ || radius_changed) {
                dragged_ = *model_;
                transform_mesh_influences(dragged_, selected_, affected_, translation, rotation,
                                          scale);
                display(preview, renderer, dragged_);
                last_amount_ = amount;
            }
            if (ImGui::IsMouseReleased(0) || !ImGui::IsMouseDown(0)) {
                auto next = dragged_;
                if (recompute_)
                    for (auto &[m, v] : affected_)
                        rebuild_mesh_normals(next.meshes[m]);
                apply(doc, preview, renderer, next);
                axis_ = -1;
            }
        } catch (const std::exception &e) {
            cancel();
            error_ = e.what();
        }
    return captured;
}
void GeometryEditor::deactivate(ModelDocument &preview, EnvironmentRenderer &renderer) {
    test_bone_ = false;
    clear_test_pose(preview, renderer);
    painting_ = box_drag_ = false;
    if (axis_ >= 0) {
        if (model_ && renderer.ready())
            display(preview, renderer, *model_);
        axis_ = -1;
        blocked_ = ImGui::IsMouseDown(0);
    }
}
}
