#include "native/viewport_navigation.h"
#include "native/tutorial_widgets.h"
#include "native/map_authoring_workspace.h"
#include "native/imgui_renderer.h"
#include <SDL3/SDL.h>
#include <bx/math.h>
#include <cmath>
#include <sstream>

namespace studio {
void MapAuthoringWorkspace::cancel_ground_drag() {
    if (!ground_handle_)
        return;
    ground_handle_ = 0;
    ground_drag_preview_.reset();
    brush_stroke_ = {};
    refresh_scene();
    status_ = "Ground edit canceled.";
}
std::optional<SpatialPoint> MapAuthoringWorkspace::ground_pointer(ImVec2 origin,
                                                                  ImVec2 size) const {
    const auto &io = ImGui::GetIO();
    const float x = ((io.MousePos.x - origin.x) / size.x * 2 - 1) * std::tan(.3926990817f) *
                    size.x / size.y,
                y = (1 - (io.MousePos.y - origin.y) / size.y * 2) * std::tan(.3926990817f);
    auto direction = map_camera_.forward();
    const auto right = map_camera_.right(), up = map_camera_.up();
    for (unsigned axis = 0; axis < 3; ++axis)
        direction[axis] += right[axis] * x + up[axis] * y;
    return pick_authoring_ground(authoring_base_->spatial, map_camera_.eye(), direction,
                                 cutaway_ ? std::min(cut_height_, ground_ceiling_)
                                          : ground_ceiling_);
}
bool MapAuthoringWorkspace::brush_input(ImVec2 origin, ImVec2 size,
                                        const std::function<bool(SpatialPoint, ImVec2 &)> &project,
                                        bool hovered) {
    auto &io = ImGui::GetIO();
    const bool active = ground_handle_ == 10;
    if (active && (!(SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) ||
                   ImGui::IsKeyPressed(ImGuiKey_Escape) || busy())) {
        cancel_ground_drag();
        return true;
    }
    if (inspect_ || !ground_mode_ || stage_ == AuthoringStage::Review || !document_->ground() ||
        busy() || preview_ || ground_transform_tool_ < Sculpt ||
        ground_transform_tool_ > BlendBrush)
        return false;
    brush_hover_ = hovered ? ground_pointer(origin, size) : std::nullopt;
    if (brush_hover_ && !io.KeyShift) {
        auto *draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        for (int ring = 0; ring < 2; ++ring) {
            const float fraction = ring ? brush_hardness_ : 1.f;
            if (ring && (fraction <= 0 || fraction >= 1 || ground_transform_tool_ == PaintBrush))
                continue;
            ImVec2 previous{};
            bool connected = false;
            for (int i = 0; i <= 48; ++i) {
                const float angle = i * 6.2831853f / 48;
                const float radius = brush_radius_ * document_->grid().tile_size * fraction;
                SpatialPoint point{(*brush_hover_)[0] + std::cos(angle) * radius,
                                   authoring_base_->high[1] + 100000,
                                   (*brush_hover_)[2] + std::sin(angle) * radius};
                auto hit = pick_authoring_ground(authoring_base_->spatial, point, {0, -1, 0},
                                                 ground_ceiling_);
                if (!hit) {
                    connected = false;
                    continue;
                }
                ImVec2 screen;
                if (project(*hit, screen)) {
                    if (connected)
                        draw->AddLine(previous, screen,
                                      IM_COL32(155, 235, 205, fraction == 1 ? 240 : 100),
                                      fraction == 1 ? 2.f : 1.f);
                    previous = screen;
                    connected = true;
                } else
                    connected = false;
            }
        }
        draw->PopClipRect();
    }
    if (!active && hovered && brush_hover_ && !io.KeyShift && !io.WantTextInput &&
        (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        brush_stroke_ = {};
        brush_stroke_.radius = brush_radius_ * document_->grid().tile_size;
        brush_stroke_.hardness = brush_hardness_;
        brush_stroke_.value = sculpt_mode_ == 2 ? ground_level_ : ground_step_;
        if (ground_transform_tool_ == Sculpt) {
            brush_stroke_.mode = sculpt_mode_ == 2   ? GroundBrushMode::Level
                                 : sculpt_mode_ == 1 ? GroundBrushMode::Lower
                                                     : GroundBrushMode::Raise;
            if (io.KeyAlt && sculpt_mode_ != 2)
                brush_stroke_.mode = brush_stroke_.mode == GroundBrushMode::Raise
                                         ? GroundBrushMode::Lower
                                         : GroundBrushMode::Raise;
        } else {
            brush_stroke_.mode = ground_transform_tool_ == PaintBrush ? GroundBrushMode::Paint
                                                                      : GroundBrushMode::Blend;
            brush_stroke_.texture =
                ground_texture_ < 0 ? "" : ground_textures_.at(std::size_t(ground_texture_)).key;
            brush_stroke_.value = ground_coverage_;
        }
        if (brush_selection_) {
            require(bool(tile_), "Select a region or turn off Limit to selected region");
            brush_stroke_.selection =
                std::array<AuthoringTile, 2>{*tile_, region_end_.value_or(*tile_)};
        }
        brush_stroke_.points.push_back({(*brush_hover_)[0], (*brush_hover_)[2]});
        status_ = "Brush preview: release to apply; Escape cancels.";
        ground_handle_ = 10;
        ground_drag_preview_ = brush_ground(document_->grid(), *document_->ground(), brush_stroke_);
        refresh_scene();
        error_.clear();
    }
    if (ground_handle_ == 10) {
        if (brush_hover_) {
            const auto last = brush_stroke_.points.back();
            const std::array<float, 2> next{(*brush_hover_)[0], (*brush_hover_)[2]};
            if (std::hypot(next[0] - last[0], next[1] - last[1]) >= brush_stroke_.radius * .08f) {
                auto candidate = brush_stroke_;
                candidate.points.push_back(next);
                auto preview = brush_ground(document_->grid(), *document_->ground(), candidate);
                brush_stroke_ = std::move(candidate);
                ground_drag_preview_ = std::move(preview);
                refresh_scene();
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const bool changed =
                ground_drag_preview_ && *ground_drag_preview_ != *document_->ground();
            document_->apply_ground_brush(brush_stroke_);
            ground_handle_ = 0;
            brush_stroke_ = {};
            ground_drag_preview_.reset();
            refresh_scene();
            ground_ceiling_ = authoring_base_->high[1] + 100000;
            status_ = changed ? "Brush stroke applied. Undo restores the whole stroke."
                              : "Stroke made no change. For height or blends, use a radius that "
                                "reaches ground vertices.";
        }
        return true;
    }
    return false;
}
bool MapAuthoringWorkspace::vertex_handles(
    ImVec2 origin, ImVec2 size, const std::function<bool(SpatialPoint, ImVec2 &)> &project,
    bool hovered) {
    const auto &grid = document_->grid();
    const auto &ground = ground_drag_preview_ ? *ground_drag_preview_ : *document_->ground();
    auto &io = ImGui::GetIO();
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    struct ClipScope {
        ImDrawList *draw;
        ~ClipScope() {
            draw->PopClipRect();
        }
    } clip{draw};
    std::optional<AuthoringTile> nearest;
    float near_distance = 9;
    for (int z = 0; z <= grid.height; ++z)
        for (int x = 0; x <= grid.width; ++x) {
            const auto point = ground_vertex(grid, ground, x, z);
            if (cutaway_ && point[1] > cut_height_)
                continue;
            ImVec2 screen;
            if (!project(point, screen) || screen.x < origin.x || screen.y < origin.y ||
                screen.x > origin.x + size.x || screen.y > origin.y + size.y)
                continue;
            const bool selected = ground_vertex_ && *ground_vertex_ == AuthoringTile{x, z};
            draw->AddCircleFilled(screen, selected ? 5.f : 3.f,
                                  selected ? IM_COL32(255, 255, 255, 255)
                                           : IM_COL32(120, 195, 240, 190));
            const float distance = std::hypot(screen.x - io.MousePos.x, screen.y - io.MousePos.y);
            if (distance < near_distance) {
                near_distance = distance;
                nearest = AuthoringTile{x, z};
            }
        }
    int hot = 0;
    ImVec2 drag_axis{};
    if (ground_vertex_) {
        const auto pivot = ground_vertex(grid, ground, ground_vertex_->x, ground_vertex_->z);
        ImVec2 center;
        if (project(pivot, center)) {
            const auto eye = map_camera_.eye();
            float distance = 0;
            for (unsigned axis = 0; axis < 3; ++axis)
                distance += (eye[axis] - pivot[axis]) * (eye[axis] - pivot[axis]);
            const float radius = std::max(.001f, std::sqrt(distance) * .828427f / size.y * 70);
            for (unsigned axis = 0; axis < 3; ++axis) {
                auto point = pivot;
                point[axis] += radius;
                ImVec2 tip;
                if (!project(point, tip))
                    continue;
                const float dx = tip.x - center.x, dy = tip.y - center.y, n = std::hypot(dx, dy);
                if (n < 12)
                    continue;
                const ImU32 colors[] = {IM_COL32(245, 110, 100, 255), IM_COL32(105, 225, 155, 255),
                                        IM_COL32(100, 165, 255, 255)};
                draw->AddLine(center, tip, colors[axis], 3);
                const float ux = dx / n, uy = dy / n;
                draw->AddTriangleFilled(tip, {tip.x - ux * 13 - uy * 6, tip.y - uy * 13 + ux * 6},
                                        {tip.x - ux * 13 + uy * 6, tip.y - uy * 13 - ux * 6},
                                        colors[axis]);
                draw->AddText({tip.x + 5, tip.y - 8}, colors[axis],
                              axis == 0   ? "X"
                              : axis == 1 ? "Y"
                                          : "Z");
                const float t = std::clamp(
                    ((io.MousePos.x - center.x) * dx + (io.MousePos.y - center.y) * dy) / (n * n),
                    .18f, 1.f);
                if (std::hypot(io.MousePos.x - center.x - t * dx,
                               io.MousePos.y - center.y - t * dy) < 9) {
                    hot = int(axis) + 5;
                    drag_axis = {dx / (n * n) * radius, dy / (n * n) * radius};
                }
            }
        }
    }
    const bool was_active = ground_handle_ != 0;
    if (hovered && !io.KeyShift && !ground_handle_ &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hot) {
            ground_handle_ = hot;
            ground_drag_start_ = io.MousePos;
            ground_drag_axis_ = drag_axis;
            ground_drag_value_ = 0;
        } else if (nearest) {
            ground_vertex_ = nearest;
            vertex_delta_ = {};
            tile_ = AuthoringTile{std::min(nearest->x, grid.width - 1),
                                  std::min(nearest->z, grid.height - 1)};
            region_end_.reset();
            selected_ = 0;
            status_ = "Vertex selected. Drag an axis arrow to move it.";
        }
    }
    if (ground_handle_ >= 5) {
        const auto axis = unsigned(ground_handle_ - 5);
        float value = (io.MousePos.x - ground_drag_start_.x) * ground_drag_axis_.x +
                      (io.MousePos.y - ground_drag_start_.y) * ground_drag_axis_.y;
        if (io.KeyShift && std::isfinite(ground_step_) && ground_step_ > 0)
            value = std::round(value / ground_step_) * ground_step_;
        if (value != ground_drag_value_) {
            SpatialPoint delta{};
            delta[axis] = value;
            try {
                auto preview =
                    move_ground_vertex(grid, *document_->ground(), *ground_vertex_, delta);
                ground_drag_preview_ = std::move(preview);
                ground_drag_value_ = value;
                refresh_scene();
                error_.clear();
            } catch (const std::exception &error) {
                error_ = error.what();
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            SpatialPoint delta{};
            delta[axis] = ground_drag_value_;
            document_->move_vertex(*ground_vertex_, delta);
            ground_handle_ = 0;
            ground_drag_preview_.reset();
            refresh_scene();
            ground_ceiling_ = authoring_base_->high[1] + 100000;
            status_ = "Vertex moved. Undo restores the whole drag.";
        }
    }
    return was_active || ground_handle_ || (hovered && !io.KeyShift && (hot || nearest));
}

bool MapAuthoringWorkspace::terrain_handles(
    ImVec2 origin, ImVec2 size, const std::function<bool(SpatialPoint, ImVec2 &)> &project,
    bool hovered) {
    if (ground_handle_ == 20)
        return false;
    auto &io = ImGui::GetIO();
    const auto &ground = ground_drag_preview_ ? *ground_drag_preview_ : *document_->ground();
    const auto positions = terrain_positions(document_->grid(), ground);
    using Element = std::array<std::uint32_t, 3>;
    const auto faces = terrain_triangles(document_->grid(), ground);
    std::set<Element> elements;
    std::map<std::pair<std::uint32_t, std::uint32_t>, unsigned> edges;
    for (const auto &face : faces) {
        if (terrain_selection_mode_ == 2)
            elements.insert(face.vertices);
        for (unsigned i = 0; i < 3; ++i) {
            auto a = face.vertices[i], b = face.vertices[(i + 1) % 3];
            if (a > b)
                std::swap(a, b);
            ++edges[{a, b}];
            if (terrain_selection_mode_ == 0)
                elements.insert({face.vertices[i], face.vertices[i], face.vertices[i]});
        }
    }
    if (terrain_selection_mode_ == 1)
        for (auto [edge, count] : edges)
            elements.insert({edge.first, edge.second, edge.second});
    std::erase_if(terrain_elements_, [&](const auto &e) {
        return !elements.contains(e);
    });
    const std::set<Element> selected_elements(terrain_elements_.begin(), terrain_elements_.end());
    std::set<std::uint32_t> selected_points;
    for (const auto &e : terrain_elements_)
        for (auto i : e)
            selected_points.insert(i);
    terrain_selection_.assign(selected_points.begin(), selected_points.end());
    if (terrain_box_ && (!(SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) ||
                         ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        terrain_box_ = false;
        terrain_click_.reset();
        return true;
    }
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    std::optional<Element> nearest;
    float distance = 9, nearest_depth = INFINITY;
    const auto eye = map_camera_.eye(), forward = map_camera_.forward();
    const auto cross = [](ImVec2 a, ImVec2 b, ImVec2 c) {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    };
    const auto screens = [&](const Element &e, std::array<ImVec2, 3> &points) {
        for (unsigned i = 0; i < 3; ++i)
            if ((cutaway_ && positions[e[i]][1] > cut_height_) ||
                !project(positions[e[i]], points[i]))
                return false;
        return true;
    };
    for (const auto &e : elements) {
        std::array<ImVec2, 3> p;
        if (!screens(e, p))
            continue;
        const bool selected = selected_elements.contains(e);
        const auto color = selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(120, 195, 240, 190);
        float d = INFINITY, depth = INFINITY;
        if (terrain_selection_mode_ == 0) {
            draw->AddCircleFilled(p[0], selected ? 5.f : 3.f, color);
            d = std::hypot(p[0].x - io.MousePos.x, p[0].y - io.MousePos.y);
        } else if (terrain_selection_mode_ == 1) {
            draw->AddLine(p[0], p[1], color, selected ? 3.f : 1.f);
            const float dx = p[1].x - p[0].x, dy = p[1].y - p[0].y, n = dx * dx + dy * dy;
            const float t =
                n > 0 ? std::clamp(((io.MousePos.x - p[0].x) * dx + (io.MousePos.y - p[0].y) * dy) /
                                       n,
                                   0.f, 1.f)
                      : 0;
            d = std::hypot(io.MousePos.x - p[0].x - t * dx, io.MousePos.y - p[0].y - t * dy);
        } else {
            draw->AddTriangle(p[0], p[1], p[2], IM_COL32(120, 195, 240, 130), 1);
            if (selected) {
                draw->AddTriangleFilled(p[0], p[1], p[2], IM_COL32(105, 215, 230, 95));
                draw->AddTriangle(p[0], p[1], p[2], color, 2);
            }
            const float area = cross(p[0], p[1], p[2]);
            if (std::abs(area) > .01f) {
                const float a = cross(io.MousePos, p[1], p[2]) / area,
                            b = cross(p[0], io.MousePos, p[2]) / area, c = 1 - a - b;
                if (a >= 0 && b >= 0 && c >= 0) {
                    float inverse = 0;
                    const float weights[] = {a, b, c};
                    for (unsigned i = 0; i < 3; ++i) {
                        float z = 0;
                        for (unsigned axis = 0; axis < 3; ++axis)
                            z += (positions[e[i]][axis] - eye[axis]) * forward[axis];
                        if (z > 0)
                            inverse += weights[i] / z;
                    }
                    if (inverse > 0) {
                        d = 0;
                        depth = 1 / inverse;
                    }
                }
            }
        }
        if (d < distance || (d == distance && depth < nearest_depth)) {
            distance = d;
            nearest_depth = depth;
            nearest = e;
        }
    }
    if (nearest && hovered && !terrain_box_ && !ground_handle_ && !io.KeyShift) {
        std::array<ImVec2, 3> p;
        if (screens(*nearest, p)) {
            const auto color = IM_COL32(130, 245, 210, 255);
            if (terrain_selection_mode_ == 0)
                draw->AddCircle(p[0], 7, color, 12, 2);
            else if (terrain_selection_mode_ == 1)
                draw->AddLine(p[0], p[1], color, 3);
            else
                draw->AddTriangle(p[0], p[1], p[2], color, 2);
        }
    }
    if (terrain_box_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 low{std::min(terrain_box_start_.x, io.MousePos.x),
                         std::min(terrain_box_start_.y, io.MousePos.y)},
            high{std::max(terrain_box_start_.x, io.MousePos.x),
                 std::max(terrain_box_start_.y, io.MousePos.y)};
        draw->AddRectFilled(low, high, IM_COL32(100, 190, 230, 30));
        draw->AddRect(low, high, IM_COL32(130, 220, 250, 255));
    }
    int hot = 0;
    ImVec2 drag_axis{};
    if (!terrain_selection_.empty()) {
        SpatialPoint pivot{};
        for (auto index : terrain_selection_)
            for (unsigned a = 0; a < 3; ++a)
                pivot[a] += positions[index][a] / float(terrain_selection_.size());
        ImVec2 center;
        if (project(pivot, center)) {
            const auto eye = map_camera_.eye();
            float d = 0;
            for (unsigned a = 0; a < 3; ++a)
                d += (eye[a] - pivot[a]) * (eye[a] - pivot[a]);
            const float radius = std::max(.001f, std::sqrt(d) * .828427f / size.y * 65);
            if (terrain_influence_ > 0) {
                for (auto selected_point : terrain_selection_)
                    for (unsigned plane = 0; plane < 3; ++plane) {
                        ImVec2 previous{};
                        bool connected = false;
                        for (int i = 0; i <= 48; ++i) {
                            auto point = positions[selected_point];
                            const float angle = i * 6.2831853f / 48,
                                        extent = terrain_influence_ * document_->grid().tile_size;
                            point[plane] += std::cos(angle) * extent;
                            point[(plane + 1) % 3] += std::sin(angle) * extent;
                            ImVec2 screen;
                            if (project(point, screen)) {
                                if (connected)
                                    draw->AddLine(previous, screen, IM_COL32(140, 220, 200, 110),
                                                  1);
                                previous = screen;
                                connected = true;
                            } else
                                connected = false;
                        }
                    }
            }
            for (unsigned axis = 0; axis < 3; ++axis) {
                auto point = pivot;
                point[axis] += radius;
                ImVec2 tip;
                if (!project(point, tip))
                    continue;
                const float dx = tip.x - center.x, dy = tip.y - center.y, n = std::hypot(dx, dy);
                if (n < 12)
                    continue;
                const ImU32 colors[] = {IM_COL32(245, 110, 100, 255), IM_COL32(105, 225, 155, 255),
                                        IM_COL32(100, 165, 255, 255)};
                draw->AddLine(center, tip, colors[axis], 3);
                const float ux = dx / n, uy = dy / n;
                draw->AddTriangleFilled(tip, {tip.x - ux * 13 - uy * 6, tip.y - uy * 13 + ux * 6},
                                        {tip.x - ux * 13 + uy * 6, tip.y - uy * 13 - ux * 6},
                                        colors[axis]);
                draw->AddText({tip.x + 5, tip.y - 8}, colors[axis],
                              axis == 0   ? "X"
                              : axis == 1 ? "Y"
                                          : "Z");
                const float t = std::clamp(
                    ((io.MousePos.x - center.x) * dx + (io.MousePos.y - center.y) * dy) / (n * n),
                    .18f, 1.f);
                if (std::hypot(io.MousePos.x - center.x - t * dx,
                               io.MousePos.y - center.y - t * dy) < 9) {
                    hot = int(axis) + 11;
                    drag_axis = {dx / (n * n) * radius, dy / (n * n) * radius};
                }
            }
        }
    }
    draw->PopClipRect();
    if (hovered && !io.KeyShift && !ground_handle_ &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hot) {
            ground_handle_ = hot;
            ground_drag_start_ = io.MousePos;
            ground_drag_axis_ = drag_axis;
            ground_drag_value_ = 0;
        } else {
            terrain_box_ = true;
            terrain_box_start_ = io.MousePos;
            terrain_box_add_ = io.KeyCtrl;
            terrain_click_ = nearest;
        }
    }
    if (terrain_box_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const bool box =
            io.MouseDragMaxDistanceSqr[0] >= io.MouseDragThreshold * io.MouseDragThreshold;
        std::set<Element> selection;
        if (terrain_box_add_)
            selection.insert(terrain_elements_.begin(), terrain_elements_.end());
        const auto select = [&](const Element &e, bool toggle) {
            if (toggle && selection.contains(e))
                selection.erase(e);
            else
                selection.insert(e);
        };
        if (box) {
            const ImVec2 low{std::min(terrain_box_start_.x, io.MousePos.x),
                             std::min(terrain_box_start_.y, io.MousePos.y)},
                high{std::max(terrain_box_start_.x, io.MousePos.x),
                     std::max(terrain_box_start_.y, io.MousePos.y)};
            for (const auto &e : elements) {
                std::array<ImVec2, 3> p;
                if (screens(e, p) && std::all_of(p.begin(), p.end(), [&](ImVec2 v) {
                        return v.x >= low.x && v.x <= high.x && v.y >= low.y && v.y <= high.y;
                    }))
                    select(e, false);
            }
        } else if (terrain_click_)
            select(*terrain_click_, terrain_box_add_);
        terrain_elements_.assign(selection.begin(), selection.end());
        terrain_box_ = false;
        terrain_click_.reset();
        return true;
    }
    if (terrain_box_)
        return true;
    if (ground_handle_ >= 11 && ground_handle_ <= 13) {
        float value = (io.MousePos.x - ground_drag_start_.x) * ground_drag_axis_.x +
                      (io.MousePos.y - ground_drag_start_.y) * ground_drag_axis_.y;
        if ((terrain_snap_ || io.KeyCtrl) && std::isfinite(ground_step_) && ground_step_ > 0)
            value = std::round(value / ground_step_) * ground_step_;
        SpatialPoint delta{};
        delta[ground_handle_ - 11] = value;
        if (value != ground_drag_value_) {
            ground_drag_preview_ =
                move_terrain_points(document_->grid(), *document_->ground(), terrain_selection_,
                                    delta, terrain_influence_ * document_->grid().tile_size);
            ground_drag_value_ = value;
            refresh_scene();
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (ground_drag_preview_)
                document_->edit_terrain(*ground_drag_preview_);
            ground_handle_ = 0;
            ground_drag_preview_.reset();
            refresh_scene();
        }
        return true;
    }
    return hovered && !io.KeyShift && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}
bool MapAuthoringWorkspace::ground_handles(
    ImVec2 origin, ImVec2 size, const std::function<bool(SpatialPoint, ImVec2 &)> &project,
    bool hovered) {
    auto &io = ImGui::GetIO();
    if (ground_handle_ && (!(SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) ||
                           ImGui::IsKeyPressed(ImGuiKey_Escape) || !ground_mode_ || busy())) {
        cancel_ground_drag();
        return true;
    }
    if (inspect_ || stage_ == AuthoringStage::Review || !ground_mode_ || !document_->ground() ||
        busy() || preview_)
        return false;
    if (ground_transform_tool_ == Mesh ||
        (ground_transform_tool_ == Vertices && !document_->ground()->triangles.empty()))
        return terrain_handles(origin, size, project, hovered);
    if (ground_transform_tool_ == 2)
        return vertex_handles(origin, size, project, hovered);
    if (ground_transform_tool_ >= 3 || !tile_)
        return false;
    const auto &grid = document_->grid();
    const auto last = region_end_.value_or(*tile_);
    const int x0 = std::min(tile_->x, last.x), x1 = std::max(tile_->x, last.x) + 1,
              z0 = std::min(tile_->z, last.z), z1 = std::max(tile_->z, last.z) + 1;
    const auto &surface = ground_drag_preview_ ? *ground_drag_preview_ : *document_->ground();
    SpatialPoint pivot{};
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            auto point = ground_vertex(grid, surface, x, z);
            for (unsigned axis = 0; axis < 3; ++axis)
                pivot[axis] += point[axis];
        }
    for (auto &value : pivot)
        value /= float((x1 - x0 + 1) * (z1 - z0 + 1));
    ImVec2 center;
    if (!project(pivot, center)) {
        if (ground_handle_)
            cancel_ground_drag();
        return false;
    }
    const auto eye = map_camera_.eye();
    float distance = 0;
    for (unsigned i = 0; i < 3; ++i)
        distance += (eye[i] - pivot[i]) * (eye[i] - pivot[i]);
    const float units = std::max(.001f, 2 * std::sqrt(distance) * std::tan(.3926990817f) / size.y),
                radius = units * 62;
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    struct ClipScope {
        ImDrawList *draw;
        ~ClipScope() {
            draw->PopClipRect();
        }
    } clip{draw};
    int hot = 0;
    float nearest = 9;
    ImVec2 tangent{};
    const auto segment = [&](ImVec2 a, ImVec2 b, int handle, ImU32 color) {
        const float dx = b.x - a.x, dy = b.y - a.y, length = dx * dx + dy * dy;
        const float t =
            length > 0
                ? std::clamp(((io.MousePos.x - a.x) * dx + (io.MousePos.y - a.y) * dy) / length,
                             0.f, 1.f)
                : 0;
        const float d = std::hypot(io.MousePos.x - a.x - t * dx, io.MousePos.y - a.y - t * dy);
        if (hovered && (d < nearest || ((handle == 1 || handle == 4) && d < 7))) {
            nearest = d;
            hot = handle;
            const float n = std::max(.001f, std::sqrt(length));
            tangent = {dx / n, dy / n};
        }
        draw->AddLine(a, b, color, ground_handle_ == handle ? 4.f : 2.5f);
    };
    for (int axis = 2; ground_transform_tool_ == 1 && axis <= 3; ++axis) {
        const auto color = axis == 2 ? IM_COL32(235, 100, 100, 240) : IM_COL32(90, 160, 255, 240);
        auto along = pivot, vertical = pivot;
        along[axis == 2 ? 2 : 0] += radius;
        vertical[1] += radius;
        ImVec2 a, b;
        if (!project(along, a) || !project(vertical, b))
            continue;
        a = {a.x - center.x, a.y - center.y};
        b = {b.x - center.x, b.y - center.y};
        const float n = std::max(.001f, std::hypot(b.x, b.y));
        if (std::abs(a.x * b.y - a.y * b.x) / n < 18) {
            const float sign = a.x * b.y - a.y * b.x < 0 ? -1.f : 1.f;
            a = {b.y / n * 18 * sign, -b.x / n * 18 * sign};
        }
        ImVec2 previous{};
        for (int i = 0; i <= 80; ++i) {
            const float angle = i * 6.28318530718f / 80;
            ImVec2 screen{center.x + a.x * std::cos(angle) + b.x * std::sin(angle),
                          center.y + a.y * std::cos(angle) + b.y * std::sin(angle)};
            if (i)
                segment(previous, screen, axis, color);
            previous = screen;
        }
        draw->AddText({center.x + a.x + 5, center.y + a.y - 8}, color, axis == 2 ? "X" : "Z");
    }
    ImVec2 up, down;
    auto point = pivot;
    point[1] += radius * 1.4f;
    bool arrows = project(point, up);
    point[1] -= radius * 2.8f;
    arrows &= project(point, down);
    if (arrows && ground_transform_tool_ == 0) {
        if (std::hypot(up.x - down.x, up.y - down.y) < 35) {
            up = {center.x, center.y - 80};
            down = {center.x, center.y + 80};
        }
        segment(center, up, 1, IM_COL32(105, 225, 155, 255));
        segment(down, center, 4, IM_COL32(105, 225, 155, 255));
        for (auto tip : {up, down}) {
            const float n = std::max(.001f, std::hypot(tip.x - center.x, tip.y - center.y)),
                        dx = (tip.x - center.x) / n, dy = (tip.y - center.y) / n;
            draw->AddTriangleFilled(tip, {tip.x - dx * 15 - dy * 7, tip.y - dy * 15 + dx * 7},
                                    {tip.x - dx * 15 + dy * 7, tip.y - dy * 15 - dx * 7},
                                    IM_COL32(105, 225, 155, 255));
        }
    }
    const bool was_active = ground_handle_ != 0;
    if (!ground_handle_ && hot && !selecting_ground_ && !io.KeyShift) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip(hot == 1 || hot == 4
                              ? "Drag: raise / lower | Scroll while held: neighbor influence | "
                                "Click: height step | Ctrl: snap"
                          : hot == 2 ? "Drag: tilt around X | Ctrl: snap"
                                     : "Drag: tilt around Z | Ctrl: snap");
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ground_handle_ = hot;
            ground_drag_start_ = io.MousePos;
            ground_drag_value_ = 0;
            if (hot == 1 || hot == 4) {
                const float n = std::max(.001f, std::hypot(up.x - down.x, up.y - down.y));
                ground_drag_axis_ = {(up.x - down.x) / n * units, (up.y - down.y) / n * units};
            } else
                ground_drag_axis_ = {tangent.x * .5f, tangent.y * .5f};
        }
    }
    if (ground_handle_) {
        const bool height = ground_handle_ == 1 || ground_handle_ == 4;
        float value = (io.MousePos.x - ground_drag_start_.x) * ground_drag_axis_.x +
                      (io.MousePos.y - ground_drag_start_.y) * ground_drag_axis_.y;
        if (!height)
            value = std::clamp(value, -75.f, 75.f);
        if (io.KeyCtrl) {
            const float step = height ? ground_step_ : ground_tilt_step_;
            if (std::isfinite(step) && step > 0)
                value = std::round(value / step) * step;
        }
        if (!height)
            value = std::clamp(value, -75.f, 75.f);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && height &&
            io.MouseDragMaxDistanceSqr[0] < io.MouseDragThreshold * io.MouseDragThreshold) {
            require(std::isfinite(ground_step_) && ground_step_ > 0,
                    "Height step must be finite and positive");
            value = ground_handle_ == 1 ? ground_step_ : -ground_step_;
        }
        bool influence_changed = false;
        if (height && io.MouseWheel != 0) {
            const float previous = ground_influence_cells_;
            ground_influence_cells_ =
                std::clamp(previous + io.MouseWheel, 0.f, float(std::max(grid.width, grid.height)));
            influence_changed = previous != ground_influence_cells_;
        }
        if (value != ground_drag_value_ || influence_changed) {
            ground_drag_preview_ = height
                                       ? influence_ground(grid, *document_->ground(), *tile_, last,
                                                          value, ground_influence_cells_)
                                       : transform_ground(grid, *document_->ground(), *tile_, last,
                                                          0, ground_handle_ == 2 ? value : 0,
                                                          ground_handle_ == 3 ? value : 0);
            ground_drag_value_ = value;
            refresh_scene();
        }
        if (height) {
            const auto [selection_center, inner] =
                ground_selection_influence(grid, *document_->ground(), *tile_, last);
            const float outer = inner + ground_influence_cells_ * grid.tile_size;
            for (int ring = 0; ring < 2; ++ring) {
                if (ring && ground_influence_cells_ == 0)
                    continue;
                const float extent = ring ? inner : outer;
                ImVec2 previous{};
                bool connected = false;
                for (int i = 0; i <= 96; ++i) {
                    const float angle = i * 6.2831853f / 96;
                    SpatialPoint point{selection_center[0] + std::cos(angle) * extent,
                                       authoring_base_->high[1] + 100000,
                                       selection_center[2] + std::sin(angle) * extent};
                    const auto hit =
                        pick_authoring_ground(authoring_base_->spatial, point, {0, -1, 0});
                    if (hit)
                        point = *hit;
                    else
                        point[1] = pivot[1];
                    ImVec2 screen;
                    if (project(point, screen)) {
                        if (connected)
                            draw->AddLine(previous, screen,
                                          IM_COL32(155, 235, 205, ring ? 100 : 240),
                                          ring ? 1.f : 2.f);
                        previous = screen;
                        connected = true;
                    } else
                        connected = false;
                }
            }
        }
        if (height)
            ImGui::SetTooltip("Height %+.1f | Neighbor influence %.1f cells\nScroll: adjust "
                              "influence | Release: apply | Escape: cancel",
                              value, ground_influence_cells_);
        else
            ImGui::SetTooltip("Slope %+.1f degrees | Release to apply | Escape cancels", value);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (height)
                document_->influence_ground_height(*tile_, last, value, ground_influence_cells_);
            else
                document_->tilt_ground(*tile_, last, ground_handle_ == 2 ? value : 0,
                                       ground_handle_ == 3 ? value : 0);
            ground_handle_ = 0;
            ground_drag_preview_.reset();
            refresh_scene();
            ground_ceiling_ = authoring_base_->high[1] + 100000;
            status_ = "Ground transform applied. Undo restores the whole drag.";
        }
    }
    return was_active || ground_handle_ || (hot && hovered && !io.KeyShift);
}

void MapAuthoringWorkspace::asset_editor_viewport(std::uint32_t) {
    ImGui::Begin("Authoring viewport");
    ImGui::TextUnformatted("Extract object");
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("map_authoring_viewport", "Frame"))
        asset_camera_.fit(asset_edit_scene_->low, asset_edit_scene_->high);
    studio::TutorialWidgets::RadioButton("map_authoring_viewport", "Face", &asset_selection_mode_,
                                         0);
    ImGui::SameLine();
    studio::TutorialWidgets::RadioButton("map_authoring_viewport", "Box", &asset_selection_mode_,
                                         1);
    ImGui::SameLine();
    studio::TutorialWidgets::RadioButton("map_authoring_viewport", "Connected",
                                         &asset_selection_mode_, 2);
    if (ImGui::Button("Controls"))
        ImGui::OpenPopup("Authoring controls");
    if (ImGui::BeginPopup("Authoring controls")) {
        ImGui::TextWrapped("Click: select | Ctrl: add / toggle | Box: select through");
        ImGui::TextWrapped("%s", viewport_navigation_help);
        ImGui::EndPopup();
    }
    auto size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 1.f);
    size.y = std::max(size.y, 1.f);
    auto &io = ImGui::GetIO();
    auto &camera = asset_camera_;
    auto resolution = ImGuiRenderer::viewport_resolution(size.x, size.y);
    size = {resolution.display_width, resolution.display_height};
    const auto width = resolution.width, height = resolution.height;
    float view[16], projection[16];
    const auto eye = camera.eye();
    bx::mtxLookAt(view, {eye[0], eye[1], eye[2]},
                  {camera.target[0], camera.target[1], camera.target[2]}, {0, 1, 0},
                  bx::Handedness::Right);
    bx::mtxProj(projection, 45, float(width) / height, camera.near_clip(), camera.far_clip(),
                bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
    renderer_.lighting.game = false;
    renderer_.lighting.soft = true;
    renderer_.lighting.camera_relative = true;
    renderer_.lighting.ambient = .75f;
    renderer_.lighting.strength = .25f;
    renderer_.fog_enabled = false;
    renderer_.select_draw(-1);
    auto texture = renderer_.render(width, height, view, projection, true, false, 0, false);
    const bool flip = bgfx::getCaps()->originBottomLeft;
    ImGui::Image(ImTextureID(ImGuiRenderer::image_id(texture, false, ImGuiRenderer::preview_3ds)),
                 size, {0, flip ? 1.f : 0.f}, {1, flip ? 0.f : 1.f});
    const auto origin = ImGui::GetItemRectMin();
    const bool hovered = ImGui::IsItemHovered();
    const auto project = [&](SpatialPoint point, ImVec2 &screen) {
        float a[4]{}, b[4]{};
        for (unsigned i = 0; i < 4; ++i)
            a[i] =
                view[i] * point[0] + view[i + 4] * point[1] + view[i + 8] * point[2] + view[i + 12];
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned j = 0; j < 4; ++j)
                b[i] += projection[j * 4 + i] * a[j];
        if (b[3] <= .001f)
            return false;
        screen = {origin.x + (b[0] / b[3] * .5f + .5f) * size.x,
                  origin.y + (.5f - b[1] / b[3] * .5f) * size.y};
        return true;
    };
    const bool focused = (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0;
    if (!focused || ImGui::IsKeyPressed(ImGuiKey_Escape))
        asset_box_drag_ = false;
    if (hovered && focused && !io.WantTextInput) {
        viewport_navigation(camera, window_, true);
        if (asset_selection_mode_ == 1 && !io.KeyShift &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            asset_box_drag_ = true;
            asset_box_start_ = io.MousePos;
        }
    }
    const bool box = asset_box_drag_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left),
               click =
                   hovered && focused && !io.WantTextInput && !io.KeyShift &&
                   asset_selection_mode_ != 1 && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                   io.MouseDragMaxDistanceSqr[0] < io.MouseDragThreshold * io.MouseDragThreshold;
    if (box && !io.KeyCtrl)
        asset_faces_selected_.clear();
    const auto sub = [](SpatialPoint a, SpatialPoint b) {
        for (unsigned i = 0; i < 3; ++i)
            a[i] -= b[i];
        return a;
    };
    const auto cross = [](SpatialPoint a, SpatialPoint b) {
        return SpatialPoint{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                            a[0] * b[1] - a[1] * b[0]};
    };
    const auto dot = [](SpatialPoint a, SpatialPoint b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    auto ray = camera.forward();
    const auto right = camera.right(), up = camera.up();
    const float rx = ((io.MousePos.x - origin.x) / size.x * 2 - 1) * std::tan(.3926990817f) *
                     float(width) / height,
                ry = (1 - (io.MousePos.y - origin.y) / size.y * 2) * std::tan(.3926990817f);
    for (unsigned a = 0; a < 3; ++a)
        ray[a] += right[a] * rx + up[a] * ry;
    std::optional<std::pair<std::size_t, std::uint32_t>> hit;
    float nearest = INFINITY;
    const auto &source = *asset_edit_geometry_;
    auto *overlay = ImGui::GetWindowDrawList();
    overlay->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    for (std::size_t section = 0; section < asset_draft_.faces.size(); ++section) {
        const auto &draw = source.draws[section];
        for (auto face : asset_draft_.faces[section]) {
            const auto identity = std::pair{section, face};
            if (!click && !box && !asset_faces_selected_.contains(identity))
                continue;
            SpatialPoint points[3];
            ImVec2 screens[3];
            bool visible = true;
            for (unsigned v = 0; v < 3; ++v) {
                const auto &vertex = draw.vertices[draw.indices[std::size_t(face) * 3 + v]];
                points[v] = sub({vertex.x, vertex.y, vertex.z}, asset_draft_.pivot);
                visible &= project(points[v], screens[v]);
            }
            if (click) {
                const auto e1 = sub(points[1], points[0]), e2 = sub(points[2], points[0]),
                           p = cross(ray, e2);
                const float det = dot(e1, p);
                const auto cull = source.materials[draw.material].cull;
                if (std::abs(det) > 1e-8f && (cull != 2 || det > 0) && (cull != 1 || det < 0)) {
                    const auto t = sub(eye, points[0]), q = cross(t, e1);
                    const float u = dot(t, p) / det, v = dot(ray, q) / det, d = dot(e2, q) / det;
                    if (u >= 0 && v >= 0 && u + v <= 1 && d >= camera.near_clip() && d < nearest) {
                        nearest = d;
                        hit = identity;
                    }
                }
            }
            if (box && visible) {
                const ImVec2 center{(screens[0].x + screens[1].x + screens[2].x) / 3,
                                    (screens[0].y + screens[1].y + screens[2].y) / 3};
                if (center.x >= std::min(asset_box_start_.x, io.MousePos.x) &&
                    center.x <= std::max(asset_box_start_.x, io.MousePos.x) &&
                    center.y >= std::min(asset_box_start_.y, io.MousePos.y) &&
                    center.y <= std::max(asset_box_start_.y, io.MousePos.y))
                    asset_faces_selected_.insert(identity);
            }
            if (visible && asset_faces_selected_.contains(identity)) {
                overlay->AddTriangleFilled(screens[0], screens[1], screens[2],
                                           IM_COL32(80, 190, 255, 65));
                overlay->AddTriangle(screens[0], screens[1], screens[2],
                                     IM_COL32(120, 215, 255, 220), 1);
            }
        }
    }
    if (click) {
        if (!io.KeyCtrl)
            asset_faces_selected_.clear();
        if (hit) {
            if (asset_selection_mode_ == 2) {
                const auto section = hit->first;
                const auto &draw = source.draws[section];
                std::vector<std::vector<std::uint32_t>> adjacent(draw.vertices.size());
                for (auto f : asset_draft_.faces[section])
                    for (unsigned v = 0; v < 3; ++v)
                        adjacent[draw.indices[std::size_t(f) * 3 + v]].push_back(f);
                std::set<std::uint32_t> seen{hit->second};
                std::vector<std::uint32_t> pending{hit->second};
                for (std::size_t i = 0; i < pending.size(); ++i) {
                    const auto f = pending[i];
                    asset_faces_selected_.insert({section, f});
                    for (unsigned v = 0; v < 3; ++v)
                        for (auto next : adjacent[draw.indices[std::size_t(f) * 3 + v]])
                            if (seen.insert(next).second)
                                pending.push_back(next);
                }
            } else if (io.KeyCtrl && asset_faces_selected_.contains(*hit))
                asset_faces_selected_.erase(*hit);
            else
                asset_faces_selected_.insert(*hit);
        }
    }
    if (asset_box_drag_) {
        overlay->AddRect(asset_box_start_, io.MousePos, IM_COL32(120, 215, 255, 255), 0, 0, 2);
        if (box)
            asset_box_drag_ = false;
    }
    ImVec2 pivot;
    if (project({0, 0, 0}, pivot)) {
        overlay->AddLine({pivot.x - 6, pivot.y}, {pivot.x + 6, pivot.y},
                         IM_COL32(255, 255, 255, 240), 2);
        overlay->AddLine({pivot.x, pivot.y - 6}, {pivot.x, pivot.y + 6},
                         IM_COL32(255, 255, 255, 240), 2);
        overlay->AddText({pivot.x + 8, pivot.y}, IM_COL32(255, 255, 255, 240), "Pivot");
    }
    overlay->PopClipRect();
    ImGui::End();
}
void MapAuthoringWorkspace::viewport(std::uint32_t frame) {
    ImGui::Begin("Authoring viewport");
    if (object_drag_resource_ && document_ && inspect_) { inspect_=false; renderer_.set_scene(composed_); }
    if (!document_) {
        ImGui::TextWrapped("Start from the selected map or open a composition.");
        ImGui::End();
        return;
    }
    if (auto picked = renderer_.poll_pick(frame); picked && *picked >= 0 && composed_ &&
                                                  std::size_t(*picked) < composed_->draws.size() &&
                                                  !inspect_ && !preview_) {
        const auto placement = composed_->draws[std::size_t(*picked)].placement;
        if (placement >= 0 && std::size_t(placement) >= base_->placement_transforms.size()) {
            const auto i = std::size_t(placement) - base_->placement_transforms.size();
            if (i < shown_.size())
                select_instance(shown_[i].id);
        }
    }
    ImGui::BeginDisabled(busy() || preview_ || ground_handle_);
    if (studio::TutorialWidgets::RadioButton("map_authoring_viewport", "Map", !inspect_) &&
        inspect_) {
        inspect_ = false;
        renderer_.set_scene(composed_);
    }
    ImGui::SameLine();
    if (!ground_mode_ &&
        studio::TutorialWidgets::RadioButton("map_authoring_viewport", "Inspect asset", inspect_) &&
        resource_ >= 0 && resources_.contains(std::size_t(resource_))) {
        inspect_ = true;
        const auto &asset = resources_.at(std::size_t(resource_));
        renderer_.set_scene(std::make_shared<Environment>(asset));
        asset_camera_.fit(asset.low, asset.high);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    auto &camera = inspect_ ? asset_camera_ : map_camera_;
    if (studio::TutorialWidgets::Button("map_authoring_viewport", "Frame")) {
        if (inspect_) {
            const auto &asset = resources_.at(std::size_t(resource_));
            camera.fit(asset.low, asset.high);
        } else if (selected_ || preview_) {
            const auto &asset = resources_.at(std::size_t(resource_));
            auto low = draft_.position, high = low;
            for (unsigned axis = 0; axis < 3; ++axis) {
                low[axis] += asset.low[axis];
                high[axis] += asset.high[axis];
            }
            camera.fit(low, high);
            camera.distance = std::max(camera.distance, document_->grid().tile_size * 4);
        } else {
            camera.fit(authoring_base_->low, authoring_base_->high);
            if (document_->ground() && !document_->ground()->triangles.empty())
                camera.distance *= 1.4f;
        }
    }
    if (!inspect_ && ground_mode_ && stage_ != AuthoringStage::Review && document_->ground()) {
        ground_toolbar();
        if (ground_transform_tool_ == Mesh ||
            (ground_transform_tool_ == Vertices && !document_->ground()->triangles.empty())) {
            ImGui::BeginDisabled(ground_handle_ || terrain_box_);
            const char *modes[] = {"Vertices##mesh-selection", "Edges##mesh-selection",
                                   "Faces##mesh-selection"};
            for (int i = 0; i < 3; ++i) {
                if (i)
                    ImGui::SameLine();
                if (studio::TutorialWidgets::RadioButton("map_authoring_viewport", modes[i],
                                                         terrain_selection_mode_ == i)) {
                    terrain_selection_mode_ = i;
                    terrain_elements_.clear();
                    terrain_selection_.clear();
                }
            }
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("map_authoring_viewport",
                                                "Clear##mesh-selection")) {
                terrain_elements_.clear();
                terrain_selection_.clear();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(terrain_selection_.empty());
            if (studio::TutorialWidgets::Button("map_authoring_viewport", "Frame selection")) {
                const auto points = terrain_positions(document_->grid(), *document_->ground());
                auto low = points.at(terrain_selection_.front()), high = low;
                for (auto index : terrain_selection_)
                    for (unsigned a = 0; a < 3; ++a) {
                        low[a] = std::min(low[a], points.at(index)[a]);
                        high[a] = std::max(high[a], points.at(index)[a]);
                    }
                map_camera_.fit(low, high);
                map_camera_.distance =
                    std::max(map_camera_.distance, document_->grid().tile_size * 2);
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        }
    }
    if (!inspect_) {
        if (!existing_mode_) studio::TutorialWidgets::Checkbox("map_authoring_viewport", "Grid guide", &show_grid_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(document_->ground()
                                  ? "Shows cell edges across the entire authored terrain, "
                                    "including moved vertices."
                                  : "A local tile guide samples source-map ground below the search "
                                    "ceiling and shows through scenery.");
        ImGui::SameLine();
        studio::TutorialWidgets::Checkbox("map_authoring_viewport", "Cutaway", &cutaway_);
        if (cutaway_) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110);
            ImGui::DragFloat("Height", &cut_height_, 5);
        }
    }
    if (ImGui::Button("Controls"))
        ImGui::OpenPopup("Authoring controls");
    if (ImGui::BeginPopup("Authoring controls")) {
        ImGui::TextWrapped(
            ground_mode_ && document_->ground() &&
                    (ground_transform_tool_ == Mesh || (ground_transform_tool_ == Vertices &&
                                                        !document_->ground()->triangles.empty()))
                ? "Click: select | Drag: box select | Ctrl: add/toggle | Arrows: move"
            : ground_mode_ && document_->ground() && ground_transform_tool_ >= Sculpt &&
                    ground_transform_tool_ <= BlendBrush && stage_ != AuthoringStage::Review
                ? "Drag: brush | Alt: reverse height | Escape: cancel"
            : ground_mode_ && document_->ground()
                ? "Drag: select region | Ctrl-click: extend selection"
            : inspect_ ? "Inspect asset"
                       : existing_mode_ ? "Click surface: choose placement | Ctrl+click: select added asset" : "Click ground: select tile | Ctrl+click: select added asset");
        ImGui::TextWrapped("%s", viewport_navigation_help);
        ImGui::EndPopup();
    }
    if (preview_)
        ImGui::TextColored({1, .8f, .25f, 1},
                           "Unapplied placement preview: Apply or Cancel in Placement details");
    auto size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 1.f);
    size.y = std::max(size.y - ImGui::GetTextLineHeightWithSpacing(), 1.f);
    auto &io = ImGui::GetIO();
    auto resolution = ImGuiRenderer::viewport_resolution(size.x, size.y);
    size = {resolution.display_width, resolution.display_height};
    const auto width = resolution.width, height = resolution.height;
    float view[16], projection[16];
    const auto eye = camera.eye();
    bx::mtxLookAt(view, {eye[0], eye[1], eye[2]},
                  {camera.target[0], camera.target[1], camera.target[2]}, {0, 1, 0},
                  bx::Handedness::Right);
    bx::mtxProj(projection, 45, float(width) / float(height), camera.near_clip(), camera.far_clip(),
                bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
    renderer_.lighting.game = !inspect_ && !document_->ground();
    renderer_.lighting.soft = inspect_;
    renderer_.lighting.camera_relative = inspect_;
    renderer_.lighting.ambient = inspect_ ? .75f : .35f;
    renderer_.lighting.strength = inspect_ ? .25f : .8f;
    renderer_.fog_enabled = !inspect_;
    int selected_draw = -1;
    if (!inspect_ && composed_) {
        const auto id = preview_ ? preview_id_ : selected_;
        auto instance = std::find_if(shown_.begin(), shown_.end(), [&](const auto &item) {
            return item.id == id;
        });
        if (instance != shown_.end()) {
            const auto placement =
                base_->placement_transforms.size() + std::size_t(instance - shown_.begin());
            for (std::size_t i = 0; i < composed_->draws.size(); ++i)
                if (composed_->draws[i].placement == int(placement)) {
                    selected_draw = int(i);
                    break;
                }
        }
    }
    renderer_.select_draw(selected_draw);
    if (renderer_.ready())
        renderer_.playback.seconds += std::min(double(io.DeltaTime), .1);
    auto texture = renderer_.render(width, height, view, projection, true, !inspect_ && cutaway_,
                                    cut_height_, false);
    const bool flip = bgfx::getCaps()->originBottomLeft;
    ImGui::Image(ImTextureID(ImGuiRenderer::image_id(texture, false, ImGuiRenderer::preview_3ds)),
                 size, {0, flip ? 1.f : 0.f}, {1, flip ? 0.f : 1.f});
    const auto origin = ImGui::GetItemRectMin();
    if (!renderer_.ready()) ImGui::GetWindowDrawList()->AddText({origin.x+12,origin.y+12}, IM_COL32(255,255,255,255), "Updating preview...");
    const bool hovered = ImGui::IsItemHovered();
    const auto project = [&](SpatialPoint point, ImVec2 &screen) {
        float a[4]{}, b[4]{};
        for (unsigned i = 0; i < 4; ++i)
            a[i] =
                view[i] * point[0] + view[i + 4] * point[1] + view[i + 8] * point[2] + view[i + 12];
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned j = 0; j < 4; ++j)
                b[i] += projection[j * 4 + i] * a[j];
        if (b[3] <= .001f)
            return false;
        screen = {origin.x + (b[0] / b[3] * .5f + .5f) * size.x,
                  origin.y + (.5f - b[1] / b[3] * .5f) * size.y};
        return true;
    };
    try {
        if (!inspect_) {
            auto *overlay = ImGui::GetWindowDrawList();
            overlay->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
            if (existing_mode_ && surface_position_) {
                ImVec2 point;
                if (project(*surface_position_, point)) {
                    overlay->AddCircle(point, 7, IM_COL32(100, 240, 185, 255), 20, 2);
                    overlay->AddLine({point.x-12,point.y}, {point.x+12,point.y}, IM_COL32(100,240,185,255));
                    overlay->AddLine({point.x,point.y-12}, {point.x,point.y+12}, IM_COL32(100,240,185,255));
                }
            }
            for (const auto &instance : shown_)
                if (instance.collision) {
                    const auto corners = object_collision_corners(instance);
                    for (unsigned i = 0; i < 8; ++i)
                        for (unsigned bit : {1u, 2u, 4u})
                            if (!(i & bit)) {
                                ImVec2 a, b;
                                if (project(corners[i], a) && project(corners[i | bit], b))
                                    overlay->AddLine(a, b,
                                                     instance.id == selected_
                                                         ? IM_COL32(255, 195, 75, 255)
                                                         : IM_COL32(110, 220, 175, 200),
                                                     instance.id == selected_ ? 2.5f : 1.5f);
                            }
                }
            overlay->PopClipRect();
        }
        const bool handle_input = object_interaction(origin, size, project, hovered) ||
                                  brush_input(origin, size, project, hovered) ||
                                  ground_handles(origin, size, project, hovered);
        if (hovered && !handle_input && !io.WantTextInput &&
            (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS)) {
            viewport_navigation(camera, window_, true);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                cancel_preview();
                if (selected_)
                    select_instance(selected_);
            }
            if (!inspect_ && stage_ != AuthoringStage::Review && !busy() && !preview_ &&
                !io.KeyShift) {
                const auto ground_hit = [&]() {
                    const float x = ((io.MousePos.x - origin.x) / size.x * 2 - 1) *
                                    std::tan(.3926990817f) * float(width) / float(height),
                                y = (1 - (io.MousePos.y - origin.y) / size.y * 2) *
                                    std::tan(.3926990817f);
                    auto direction = camera.forward();
                    const auto right = camera.right(), up = camera.up();
                    for (unsigned axis = 0; axis < 3; ++axis)
                        direction[axis] += right[axis] * x + up[axis] * y;
                    if (existing_mode_) {
                        surface_position_ = pick_map_surface(*authoring_base_, eye, direction,
                            cutaway_ ? std::min(cut_height_, ground_ceiling_) : ground_ceiling_);
                        return std::optional<AuthoringTile>{};
                    }
                    auto hit = pick_authoring_ground(
                        authoring_base_->spatial, eye, direction,
                        cutaway_ ? std::min(cut_height_, ground_ceiling_) : ground_ceiling_);
                    return !hit ? std::nullopt
                           : document_->ground()
                               ? ground_tile_at(document_->grid(), *document_->ground(), (*hit)[0],
                                                (*hit)[2], (*hit)[1])
                               : document_->grid().tile_at((*hit)[0], (*hit)[2]);
                };
                if (ground_mode_ && document_->ground()) {
                    if (ground_transform_tool_ != Vertices && ground_transform_tool_ < Sculpt &&
                        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        auto hit = ground_hit();
                        selecting_ground_ = bool(hit);
                        if (hit) {
                            if (!io.KeyCtrl || !tile_)
                                tile_ = hit;
                            region_end_ = hit;
                            selected_ = 0;
                            status_ = "Region selected. Choose a terrain tool or switch to "
                                      "Surfaces to paint it.";
                        } else {
                            tile_.reset();
                            region_end_.reset();
                        }
                    }
                    if (selecting_ground_ && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                        if (auto hit = ground_hit())
                            region_end_ = hit;
                } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                           io.MouseDragMaxDistanceSqr[0] <
                               io.MouseDragThreshold * io.MouseDragThreshold) {
                    if (io.KeyCtrl)
                        renderer_.request_pick(resolution.pixel_x(io.MousePos.x - origin.x),
                                               resolution.pixel_y(io.MousePos.y - origin.y), width,
                                               height, view, projection, true, cutaway_,
                                               cut_height_, false);
                    else {
                        if(stage_ == AuthoringStage::Objects)
                            renderer_.request_pick(resolution.pixel_x(io.MousePos.x-origin.x), resolution.pixel_y(io.MousePos.y-origin.y), width, height, view, projection, true, cutaway_,cut_height_,false);
                        tile_ = ground_hit();
                        region_end_.reset();
                        selected_ = 0;
                        status_ = existing_mode_ ? (surface_position_ ? "Surface selected. Preview an asset here, then Place asset." : "No map surface at this point.") : tile_ ? "Tile selected. Preview an asset here, then Place asset."
                                        : "No supported ground tile at this point.";
                    }
                }
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            selecting_ground_ = false;
        if (!inspect_ && stage_ != AuthoringStage::Review && document_->ground() && tile_ &&
            (ground_transform_tool_ < Sculpt || brush_selection_)) {
            auto last = region_end_.value_or(*tile_);
            const auto &grid = document_->grid();
            const auto &ground =
                ground_drag_preview_ ? *ground_drag_preview_ : *document_->ground();
            auto *draw = ImGui::GetWindowDrawList();
            draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
            if (!ground.triangles.empty()) {
                const auto positions = terrain_positions(grid, ground);
                for (const auto &face : ground.triangles) {
                    const int x = int(face.cell) % grid.width, z = int(face.cell) / grid.width;
                    if (x < std::min(tile_->x, last.x) || x > std::max(tile_->x, last.x) ||
                        z < std::min(tile_->z, last.z) || z > std::max(tile_->z, last.z))
                        continue;
                    ImVec2 points[3];
                    bool visible = true;
                    for (unsigned i = 0; i < 3; ++i)
                        visible &= project(positions[face.vertices[i]], points[i]);
                    if (visible) {
                        draw->AddTriangleFilled(points[0], points[1], points[2],
                                                IM_COL32(105, 195, 255, 40));
                        draw->AddTriangle(points[0], points[1], points[2],
                                          IM_COL32(135, 210, 255, 210), 1);
                    }
                }
            } else
                for (int z = std::min(tile_->z, last.z); z <= std::max(tile_->z, last.z); ++z)
                    for (int x = std::min(tile_->x, last.x); x <= std::max(tile_->x, last.x); ++x) {
                        ImVec2 points[4];
                        bool visible = true;
                        int i = 0;
                        for (auto corner : {AuthoringTile{x, z}, AuthoringTile{x, z + 1},
                                            AuthoringTile{x + 1, z + 1}, AuthoringTile{x + 1, z}}) {
                            visible &= project(ground_vertex(grid, ground, corner.x, corner.z),
                                               points[i++]);
                        }
                        if (visible) {
                            draw->AddTriangleFilled(points[0], points[1], points[3],
                                                    IM_COL32(105, 195, 255, 40));
                            draw->AddTriangleFilled(points[3], points[1], points[2],
                                                    IM_COL32(105, 195, 255, 40));
                            draw->AddQuad(points[0], points[1], points[2], points[3],
                                          IM_COL32(135, 210, 255, 210), 1.5f);
                        }
                    }
            draw->PopClipRect();
        }
        if (!existing_mode_ && !inspect_ && show_grid_ &&
            (!document_->ground() || document_->ground()->triangles.empty())) {
            const auto &grid = document_->grid();
            const auto anchor = document_->ground() ? std::optional(AuthoringTile{0, 0})
                                : tile_             ? tile_
                                        : grid.tile_at(camera.target[0], camera.target[2]);
            if (anchor) {
                std::ostringstream key;
                key << grid.origin[0] << ',' << grid.origin[1] << ',' << grid.tile_size << ','
                    << grid.width << ',' << grid.height << ',' << anchor->x << ',' << anchor->z
                    << ',' << ground_ceiling_;
                if (key.str() != grid_key_) {
                    grid_key_ = key.str();
                    grid_lines_.clear();
                    const int x0 = document_->ground() ? 0 : std::max(0, anchor->x - 4),
                              z0 = document_->ground() ? 0 : std::max(0, anchor->z - 4),
                              x1 = document_->ground() ? grid.width
                                                       : std::min(grid.width, anchor->x + 5),
                              z1 = document_->ground() ? grid.height
                                                       : std::min(grid.height, anchor->z + 5);
                    std::map<std::pair<int, int>, std::optional<SpatialPoint>> points;
                    for (int z = z0; z <= z1; ++z)
                        for (int x = x0; x <= x1; ++x) {
                            const float px = grid.origin[0] + float(x) * grid.tile_size,
                                        pz = grid.origin[1] + float(z) * grid.tile_size;
                            AuthoringGrid query{{px - .5f, pz - .5f}, 1, 1, 1};
                            points[{x, z}] =
                                document_->ground()
                                    ? std::optional(ground_vertex(grid,
                                                                  ground_drag_preview_
                                                                      ? *ground_drag_preview_
                                                                      : *document_->ground(),
                                                                  x, z))
                                    : tile_ground_position(query, {0, 0}, authoring_base_->spatial,
                                                           ground_ceiling_);
                        }
                    for (int z = z0; z <= z1; ++z)
                        for (int x = x0; x <= x1; ++x) {
                            auto a = points.at({x, z});
                            if (!a)
                                continue;
                            for (const auto &next : {std::pair{x + 1, z}, std::pair{x, z + 1}}) {
                                auto b = points.find(next);
                                if (b != points.end() && b->second)
                                    grid_lines_.push_back({*a, *b->second});
                            }
                        }
                }
                auto *draw = ImGui::GetWindowDrawList();
                draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
                for (const auto &line : grid_lines_) {
                    ImVec2 a, b;
                    if (project(line.first, a) && project(line.second, b))
                        draw->AddLine(a, b, IM_COL32(115, 210, 220, 120), 1);
                }
                draw->PopClipRect();
            }
        }
        if (!existing_mode_ && !inspect_ && show_grid_ && document_->ground() &&
            !document_->ground()->triangles.empty()) {
            auto *draw = ImGui::GetWindowDrawList();
            draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
            std::set<std::pair<std::uint32_t, std::uint32_t>> edges;
            const auto &region = authoring_base_->spatial.regions.front();
            for (std::size_t i = 0; i < region.triangles.size(); i += 3)
                for (unsigned j = 0; j < 3; ++j) {
                    auto a = region.triangles[i + j], b = region.triangles[i + (j + 1) % 3];
                    if (a > b)
                        std::swap(a, b);
                    edges.insert({a, b});
                }
            for (auto [a, b] : edges) {
                ImVec2 p, q;
                if (project(region.vertices[a].position, p) &&
                    project(region.vertices[b].position, q))
                    draw->AddLine(p, q, IM_COL32(115, 210, 220, 150), 1);
            }
            draw->PopClipRect();
        }
    } catch (const std::exception &error) {
        if(object_axis_>=0 || object_drag_resource_) {
            object_axis_=-1;object_drag_resource_.reset();object_drop_preview_=false;
            object_drag_canceled_=ImGui::IsMouseDown(ImGuiMouseButton_Left);
            cancel_preview();
        }
        if (ground_handle_)
            cancel_ground_drag();
        error_ = error.what();
    }
    if (document_->ground()) {
        std::size_t vertices = 0, triangles = 0;
        for (const auto &draw : authoring_base_->draws) {
            vertices += draw.vertices.size();
            triangles += draw.indices.size() / 3;
        }
        ImGui::TextDisabled("%zu verts | %zu tris | %zu draws | %s", vertices, triangles,
                            authoring_base_->draws.size(),
                            ground_handle_       ? "Preview"
                            : document_->dirty() ? "Unsaved"
                                                 : "Saved");
    }
    ImGui::End();
}
}
