#include "native/collision_editor.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <limits>
namespace studio {
namespace {
SpatialPoint subtract(SpatialPoint a, SpatialPoint b) {
    for (unsigned k = 0; k < 3; ++k)
        a[k] -= b[k];
    return a;
}
float dot(SpatialPoint a, SpatialPoint b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
SpatialPoint cross(SpatialPoint a, SpatialPoint b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float segment_distance(ImVec2 p, ImVec2 a, ImVec2 b) {
    float x = b.x - a.x, y = b.y - a.y,
          t = std::clamp(((p.x - a.x) * x + (p.y - a.y) * y) / std::max(x * x + y * y, .001f), 0.f,
                         1.f);
    return std::hypot(p.x - a.x - t * x, p.y - a.y - t * y);
}
float hit_triangle(SpatialPoint eye, SpatialPoint ray, const CollisionFace &face) {
    auto e = subtract(face[1], face[0]), f = subtract(face[2], face[0]), h = cross(ray, f);
    float determinant = dot(e, h);
    if (std::abs(determinant) < .000001f)
        return -1;
    auto s = subtract(eye, face[0]);
    float u = dot(s, h) / determinant;
    if (u < 0 || u > 1)
        return -1;
    auto q = cross(s, e);
    float v = dot(ray, q) / determinant;
    if (v < 0 || u + v > 1)
        return -1;
    return dot(f, q) / determinant;
}
}
bool CollisionEditor::viewport(const float *view, const float *projection, ImVec2 origin,
                               ImVec2 size, bool hovered) {
    auto &io = ImGui::GetIO();
    auto mouse = io.MousePos;
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    auto project = [&](SpatialPoint p, ImVec2 &screen) {
        float camera[4]{};
        for (unsigned r = 0; r < 4; ++r)
            camera[r] = view[r] * p[0] + view[4 + r] * p[1] + view[8 + r] * p[2] + view[12 + r];
        float clip[4]{};
        for (unsigned r = 0; r < 4; ++r)
            for (unsigned c = 0; c < 4; ++c)
                clip[r] += projection[c * 4 + r] * camera[c];
        if (clip[3] <= .001f)
            return false;
        screen = {origin.x + (clip[0] / clip[3] + 1) * size.x * .5f,
                  origin.y + (1 - clip[1] / clip[3]) * size.y * .5f};
        return std::isfinite(screen.x) && std::isfinite(screen.y);
    };
    auto eye = camera_.eye(), ray = camera_.forward(), right = camera_.right(), up = camera_.up();
    float nx = (2 * (mouse.x - origin.x) / size.x - 1) * .41421356f * size.x / size.y,
          ny = (1 - 2 * (mouse.y - origin.y) / size.y) * .41421356f;
    for (unsigned k = 0; k < 3; ++k)
        ray[k] += right[k] * nx + up[k] * ny;
    float length = std::sqrt(dot(ray, ray));
    for (auto &x : ray)
        x /= length;
    bool focused = (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0;
    if (!focused) {
        cancel_drag();
        mouse_blocked_ = true;
    }
    if (mouse_blocked_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        mouse_blocked_ = false;
    bool input =
        focused && !busy() && !io.WantTextInput &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (input && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (drag_axis_ >= 0 || selecting_) {
            cancel_drag();
            mouse_blocked_ = true;
        } else if (hovered) {
            selection_.clear();
            vertices_.clear();
        }
    }
    if (input && hovered && drag_axis_ < 0 && !selecting_ &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        try {


            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A))
                select_all();
            if (!project_store() && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
                save();
            if (!io.KeyCtrl) {
                if (ImGui::IsKeyPressed(ImGuiKey_1))
                    select_mode(false);
                if (ImGui::IsKeyPressed(ImGuiKey_2))
                    select_mode(true);
                if (ImGui::IsKeyPressed(ImGuiKey_G))
                    tool_ = Tool::Move;
                if (ImGui::IsKeyPressed(ImGuiKey_R))
                    tool_ = Tool::Rotate;
                if (ImGui::IsKeyPressed(ImGuiKey_B))
                    tool_ = Tool::Box;
                if (ImGui::IsKeyPressed(ImGuiKey_V))
                    tool_ = Tool::Select;
                if (ImGui::IsKeyPressed(ImGuiKey_F))
                    frame_selection();
            }
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    auto refs = selection_vertices();
    std::set<CollisionVertexGroup> selected_groups;
    for (auto ref : refs)
        selected_groups.insert(document_->vertex_group(ref));
    std::map<CollisionVertexGroup, CollisionVertex> all_vertices;
    for (unsigned id = 0; id < document_->size(); ++id) {
        auto &state = document_->state(id);
        if (state.deleted || !visible_[unsigned(state.kind)])
            continue;
        ImVec2 p[3];
        bool projected = true;
        for (unsigned c = 0; c < 3; ++c) {
            projected &= project(state.vertices[c], p[c]);
            if (vertices_mode_)
                all_vertices.emplace(document_->vertex_group({id, c}), CollisionVertex{id, c});
        }
        if (!vertices_mode_ && selection_.contains(id) && projected) {
            if (!attribute_colors_)
                draw->AddTriangleFilled(p[0], p[1], p[2], IM_COL32(245, 190, 65, 75));
            draw->AddTriangle(p[0], p[1], p[2], IM_COL32(255, 205, 85, 255), 2);
        }
    }
    if (vertices_mode_)
        for (auto &[group, ref] : all_vertices) {
            ImVec2 p;
            if (project(document_->state(ref.face).vertices[ref.corner], p)) {
                bool selected = selected_groups.contains(group);
                draw->AddCircleFilled(p, selected ? 5.f : 2.5f,
                                      selected ? IM_COL32(255, 205, 85, 255)
                                               : IM_COL32(180, 208, 211, 190));
            }
        }
    SpatialPoint pivot{};
    if (drag_axis_ >= 0)
        pivot = pivot_;
    else if (!refs.empty())
        for (auto ref : refs)
            for (unsigned k = 0; k < 3; ++k)
                pivot[k] += document_->state(ref.face).vertices[ref.corner][k] / float(refs.size());
    float handle = drag_axis_ >= 0
                       ? handle_length_
                       : std::max(dot(subtract(pivot, eye), camera_.forward()) * .16f, 1.f);
    int hot = -1;
    float nearest = 9;
    ImVec2 center;
    bool gizmo =
        !refs.empty() && (tool_ == Tool::Move || tool_ == Tool::Rotate) && project(pivot, center);
    const ImU32 colors[] = {IM_COL32(245, 100, 95, 255), IM_COL32(115, 220, 145, 255),
                            IM_COL32(110, 165, 250, 255)};
    auto axis_parameter = [&](unsigned axis) {
        auto w = subtract(eye, pivot);
        float parallel = ray[axis], denominator = 1 - parallel * parallel;
        if (denominator < .001f)
            return std::numeric_limits<float>::quiet_NaN();
        return (w[axis] - parallel * dot(ray, w)) / denominator;
    };
    auto angle = [&](unsigned axis) {
        if (std::abs(ray[axis]) < .001f)
            return std::numeric_limits<float>::quiet_NaN();
        float t = (pivot[axis] - eye[axis]) / ray[axis];
        if (t <= 0)
            return std::numeric_limits<float>::quiet_NaN();
        unsigned u = (axis + 1) % 3, v = (axis + 2) % 3;
        return std::atan2(eye[v] + ray[v] * t - pivot[v], eye[u] + ray[u] * t - pivot[u]);
    };
    if (gizmo)
        for (unsigned axis = 0; axis < 3; ++axis) {
            auto color = drag_axis_ == int(axis) ? IM_COL32(255, 225, 110, 255) : colors[axis];
            if (tool_ == Tool::Move) {
                auto end = pivot;
                end[axis] += handle;
                ImVec2 p;
                if (!project(end, p))
                    continue;
                float distance = segment_distance(mouse, center, p);
                if (distance < nearest && std::hypot(p.x - center.x, p.y - center.y) > 12 &&
                    std::isfinite(axis_parameter(axis))) {
                    nearest = distance;
                    hot = int(axis);
                }
                draw->AddLine(center, p, color, 3);
                float dx = p.x - center.x, dy = p.y - center.y,
                      d = std::max(std::hypot(dx, dy), .001f);
                dx /= d;
                dy /= d;
                draw->AddTriangleFilled(p, {p.x - dx * 12 - dy * 5, p.y - dy * 12 + dx * 5},
                                        {p.x - dx * 12 + dy * 5, p.y - dy * 12 - dx * 5}, color);
                draw->AddText({p.x + 5, p.y + 3}, color, axis == 0 ? "X" : axis == 1 ? "Y" : "Z");
            } else {
                ImVec2 previous;
                bool previous_ok = false;
                for (unsigned n = 0; n <= 64; ++n) {
                    float t = float(n) * 6.283185307f / 64;
                    auto point = pivot;
                    point[(axis + 1) % 3] += std::cos(t) * handle;
                    point[(axis + 2) % 3] += std::sin(t) * handle;
                    ImVec2 p;
                    bool ok = project(point, p);
                    if (ok && previous_ok) {
                        float distance = segment_distance(mouse, previous, p);
                        if (distance < nearest && std::isfinite(angle(axis))) {
                            nearest = distance;
                            hot = int(axis);
                        }
                        draw->AddLine(previous, p, color, 2);
                    }
                    previous = p;
                    previous_ok = ok;
                }
            }
        }
    if (input && hovered && !mouse_blocked_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (gizmo && hot >= 0) {
            drag_axis_ = hot;
            pivot_ = pivot;
            handle_length_ = handle;
            axis_start_ = axis_parameter(hot);
            last_angle_ = angle(hot);
            drag_angle_ = 0;
            drag_valid_ = true;
            drag_vertices_.clear();
            if (vertices_mode_)
                for (auto ref : refs)
                    drag_vertices_[ref] = document_->state(ref.face).vertices[ref.corner];
            else
                for (auto id : selection_)
                    for (unsigned c = 0; c < 3; ++c)
                        drag_vertices_[{id, c}] = document_->state(id).vertices[c];
            document_->begin_preview();
        } else {
            selecting_ = true;
            mouse_start_ = mouse;
            selection_add_ = io.KeyCtrl;
        }
    }
    if (drag_axis_ >= 0 && input && !mouse_blocked_) {
        try {
            float amount = 0;
            if (tool_ == Tool::Move) {
                amount = axis_parameter(unsigned(drag_axis_)) - axis_start_;
                require(std::isfinite(amount), "View is parallel to the movement axis");
                if (snap_ || io.KeyCtrl)
                    amount = std::round(amount / 5) * 5;
            } else {
                float current = angle(unsigned(drag_axis_));
                require(std::isfinite(current), "View is parallel to the rotation plane");
                drag_angle_ += std::remainder(current - last_angle_, 6.283185307f);
                last_angle_ = current;
                amount = drag_angle_;
                if (snap_ || io.KeyCtrl)
                    amount = std::round(amount / .261799388f) * .261799388f;
            }
            auto edits = drag_vertices_;
            unsigned axis = unsigned(drag_axis_), u = (axis + 1) % 3, v = (axis + 2) % 3;
            for (auto &[ref, p] : edits) {
                if (tool_ == Tool::Move)
                    p[axis] += amount;
                else {
                    float x = p[u] - pivot_[u], y = p[v] - pivot_[v];
                    p[u] = pivot_[u] + std::cos(amount) * x - std::sin(amount) * y;
                    p[v] = pivot_[v] + std::sin(amount) * x + std::cos(amount) * y;
                }
            }
            document_->edit_vertices(edits, vertices_mode_ || shared_);
            drag_valid_ = true;
            synchronize();
        } catch (const std::exception &e) {
            drag_valid_ = false;
            message_ = e.what();
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (drag_valid_) {
                document_->commit_preview();
                message_ = "Transform applied. Ctrl+Z to undo.";
            } else
                document_->cancel_preview();
            drag_axis_ = -1;
            drag_vertices_.clear();
            synchronize();
        }
    }
    if (selecting_ && input) {
        ImVec2 low{std::min(mouse.x, mouse_start_.x), std::min(mouse.y, mouse_start_.y)},
            high{std::max(mouse.x, mouse_start_.x), std::max(mouse.y, mouse_start_.y)};
        bool box = tool_ == Tool::Box &&
                   std::hypot(mouse.x - mouse_start_.x, mouse.y - mouse_start_.y) > 4;
        if (box) {
            draw->AddRectFilled(low, high, IM_COL32(80, 180, 165, 35));
            draw->AddRect(low, high, IM_COL32(100, 215, 190, 255));
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            selecting_ = false;
            auto contains = [&](ImVec2 p) {
                return p.x >= low.x && p.x <= high.x && p.y >= low.y && p.y <= high.y;
            };
            if (!selection_add_) {
                selection_.clear();
                vertices_.clear();
            }
            if (vertices_mode_) {
                std::vector<CollisionVertex> hits;
                float nearest_vertex = 9, depth = std::numeric_limits<float>::max();
                for (auto &[group, ref] : all_vertices) {
                    auto point = document_->state(ref.face).vertices[ref.corner];
                    ImVec2 p;
                    if (!project(point, p))
                        continue;
                    float d = std::hypot(mouse.x - p.x, mouse.y - p.y),
                          z = dot(subtract(point, eye), camera_.forward());
                    if (box) {
                        if (contains(p))
                            hits.push_back(ref);
                    } else if (d < nearest_vertex - .1f ||
                               (std::abs(d - nearest_vertex) <= .1f && z < depth)) {
                        hits = {ref};
                        nearest_vertex = d;
                        depth = z;
                    }
                }
                for (auto ref : hits) {
                    auto group = document_->vertex_group(ref);
                    auto it = std::find_if(vertices_.begin(), vertices_.end(), [&](auto v) {
                        return document_->vertex_group(v) == group;
                    });
                    if (!box && selection_add_ && it != vertices_.end())
                        vertices_.erase(it);
                    else
                        vertices_.insert(ref);
                }
            } else {
                int hit = -1;
                float distance = std::numeric_limits<float>::max();
                for (unsigned id = 0; id < document_->size(); ++id) {
                    auto &state = document_->state(id);
                    if (state.deleted || !visible_[unsigned(state.kind)])
                        continue;
                    if (box) {
                        SpatialPoint midpoint{};
                        for (auto p : state.vertices)
                            for (unsigned k = 0; k < 3; ++k)
                                midpoint[k] += p[k] / 3;
                        ImVec2 p;
                        if (project(midpoint, p) && contains(p))
                            selection_.insert(id);
                    } else {
                        float d = hit_triangle(eye, ray, state.vertices);
                        if (d > 0 && d < distance) {
                            distance = d;
                            hit = int(id);
                        }
                    }
                }
                if (hit >= 0) {
                    if (selection_add_ && selection_.contains(unsigned(hit)))
                        selection_.erase(unsigned(hit));
                    else
                        selection_.insert(unsigned(hit));
                }
            }
        }
    }
    draw->PopClipRect();
    return selecting_ || drag_axis_ >= 0 || mouse_blocked_;
}
}
