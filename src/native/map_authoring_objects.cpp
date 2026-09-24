#include "native/map_authoring_workspace.h"
#include <algorithm>
#include <cmath>
namespace studio {
void MapAuthoringWorkspace::object_drag_source(std::size_t resource, const std::string &name) {
    if (object_drag_canceled_) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            object_drag_canceled_ = false;
        return;
    }
    if (!ImGui::BeginDragDropSource())
        return;
    if (!object_drag_resource_) {
        set_stage(AuthoringStage::Objects);
        load_resource(resource);
        object_drag_resource_ = resource;
        inspect_ = false;
        renderer_.set_scene(composed_);
    }
    ImGui::SetDragDropPayload("AUTHORING_OBJECT", &resource, sizeof(resource));
    ImGui::TextUnformatted(name.c_str());
    ImGui::TextDisabled(resources_.contains(resource) ? "Drag onto the map; release to place"
                                                      : "Loading preview...");
    ImGui::EndDragDropSource();
}
bool MapAuthoringWorkspace::object_interaction(
    ImVec2 origin, ImVec2 size, const std::function<bool(SpatialPoint, ImVec2 &)> &project,
    bool hovered) {
    if (stage_ != AuthoringStage::Objects || inspect_)
        return false;
    auto &io = ImGui::GetIO();
    const bool over = io.MousePos.x >= origin.x && io.MousePos.y >= origin.y &&
                      io.MousePos.x < origin.x + size.x && io.MousePos.y < origin.y + size.y;
    if (object_drag_resource_) {
        bool delivered = false;
        const auto previous_tile = tile_;
        const bool canceled = ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                              !(SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS);
        auto direction = map_camera_.forward();
        auto right = map_camera_.right(), up = map_camera_.up();
        float x = ((io.MousePos.x - origin.x) / size.x * 2 - 1) * std::tan(.3926990817f) * size.x /
                  size.y;
        float y = (1 - (io.MousePos.y - origin.y) / size.y * 2) * std::tan(.3926990817f);
        for (unsigned axis = 0; axis < 3; ++axis)
            direction[axis] += right[axis] * x + up[axis] * y;
        auto hit = over && !canceled
                       ? pick_map_surface(*authoring_base_, map_camera_.eye(), direction,
                                          cutaway_ ? std::min(cut_height_, ground_ceiling_)
                                                   : ground_ceiling_)
                       : std::nullopt;
        bool valid = bool(hit) && resources_.contains(*object_drag_resource_) && !busy();
        if (valid) {
            resource_ = int(*object_drag_resource_);
            surface_position_ = hit;
            if (!existing_mode_) {
                tile_ = document_->grid().tile_at((*hit)[0], (*hit)[2]);
                valid = bool(tile_);
                if (valid) {
                    auto ground = tile_ground_position(document_->grid(), *tile_,
                                                       authoring_base_->spatial, ground_ceiling_);
                    if (!document_->ground()) {
                        valid = bool(ground);
                        if (ground)
                            hit = ground;
                    }
                }
            }
        }
        if (valid) {
            if (!object_drop_preview_) {
                place_preview();
                object_drop_preview_ = true;
            }
            if (existing_mode_) {
                draft_.position = *hit;
                draft_.position[1] += height_offset_ - resources_.at(*object_drag_resource_).low[1];
                preview_transform();
            } else if (previous_tile != tile_) {
                cancel_preview();
                place_preview();
            }
        } else if (object_drop_preview_) {
            cancel_preview();
            object_drop_preview_ = false;
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const auto *payload = ImGui::AcceptDragDropPayload(
                    "AUTHORING_OBJECT", ImGuiDragDropFlags_AcceptBeforeDelivery))
                delivered = payload->IsDelivery();
            ImGui::EndDragDropTarget();
        }
        if (delivered && valid && !canceled) {
            selected_ = document_->add(*object_drag_resource_, draft_);
            preview_ = false;
            preview_id_ = 0;
            object_drop_preview_ = false;
            object_drag_resource_.reset();
            refresh_scene();
            status_ =
                "Object placed. Drag its arrows to move or the ring to rotate. Undo removes it.";
        } else if (delivered || canceled || !ImGui::GetDragDropPayload()) {
            if(delivered) status_=resources_.contains(*object_drag_resource_)
                ? "No valid placement surface here. Drag the object onto the map again."
                : "Object is still loading. Drag it onto the map again when its preview is ready.";
            if (object_drop_preview_)
                cancel_preview();
            object_drag_canceled_ = canceled && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            object_drop_preview_ = false;
            object_drag_resource_.reset();
        }
        return true;
    }
    if (!selected_ || (preview_ && object_axis_ < 0) || busy())
        return false;
    auto *draw = ImGui::GetWindowDrawList();
    ImVec2 center;
    if (!project(draft_.position, center)) {
        if(object_axis_>=0) {object_axis_=-1;cancel_preview();select_instance(selected_);}
        return false;
    }
    const float length = std::max(1.f, map_camera_.distance * .11f);
    const ImU32 colors[] = {IM_COL32(245, 100, 100, 255), IM_COL32(110, 235, 130, 255),
                            IM_COL32(100, 170, 255, 255)};
    int hot = -1;
    float closest = 9;
    ImVec2 ends[3];
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    for (int axis = 0; axis < 3; ++axis) {
        auto p = draft_.position;
        p[axis] += length;
        if (!project(p, ends[axis]))
            continue;
        float dx = ends[axis].x - center.x, dy = ends[axis].y - center.y, n = dx * dx + dy * dy;
        if (n < 16)
            continue;
        float t = std::clamp(
            ((io.MousePos.x - center.x) * dx + (io.MousePos.y - center.y) * dy) / n, 0.f, 1.f);
        float d = std::hypot(io.MousePos.x - center.x - t * dx, io.MousePos.y - center.y - t * dy);
        if (hovered && d < closest) {
            hot = axis;
            closest = d;
        }
        draw->AddLine(center, ends[axis], colors[axis], object_axis_ == axis ? 4 : 2);
        draw->AddCircleFilled(ends[axis], 5, colors[axis]);
        draw->AddText({ends[axis].x + 6, ends[axis].y}, colors[axis],
                      axis == 0   ? "X"
                      : axis == 1 ? "Y"
                                  : "Z");
    }
    draw->AddCircle(center, 48, IM_COL32(245, 205, 100, 255), 48, object_axis_ == 3 ? 4 : 2);
    if (hovered &&
        std::abs(std::hypot(io.MousePos.x - center.x, io.MousePos.y - center.y) - 48) < 6)
        hot = 3;
    draw->PopClipRect();
    bool consumed = object_axis_ >= 0 || hot >= 0;
    if (object_axis_ < 0 && hot >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        object_axis_ = hot;
        object_drag_start_ = draft_;
        object_drag_mouse_ = io.MousePos;
        object_axis_length_ = length;
        object_rotation_last_ = std::atan2(io.MousePos.y - center.y, io.MousePos.x - center.x);
        object_rotation_delta_ = 0;
        if (hot < 3)
            object_axis_screen_ = {ends[hot].x - center.x, ends[hot].y - center.y};
    }
    if (object_axis_ >= 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            !(SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS)) {
            object_axis_ = -1;
            cancel_preview();
            select_instance(selected_);
            return true;
        }
        draft_ = object_drag_start_;
        auto dx = io.MousePos.x - object_drag_mouse_.x, dy = io.MousePos.y - object_drag_mouse_.y;
        if (object_axis_ < 3) {
            auto a = object_axis_screen_;
            float n = a.x * a.x + a.y * a.y;
            if (n > 1)
                draft_.position[object_axis_] += (dx * a.x + dy * a.y) / n * object_axis_length_;
        } else {
            auto angle = std::atan2(io.MousePos.y - center.y, io.MousePos.x - center.x);
            object_rotation_delta_ += std::remainder(angle - object_rotation_last_, 6.283185307f);
            object_rotation_last_ = angle;
            draft_.turn += object_rotation_delta_ * 57.29577951f;
        }
        if (draft_ != object_drag_start_ || preview_)
            preview_transform();
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (draft_ != object_drag_start_)
                document_->transform(selected_, draft_);
            object_axis_ = -1;
            preview_ = false;
            preview_id_ = 0;
            refresh_scene();
        }
    }
    return consumed;
}
}
