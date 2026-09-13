#include "native/tutorial_widgets.h"
#include "native/uv_editor.h"
#include "native/imgui_renderer.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <cstdio>
namespace studio {
void UvEditor::sync(MaterialDocument &doc, int material, unsigned channel) {
    if (revision_ == doc.model_revision() && material_ == material && channel_ == channel)
        return;
    std::set<std::pair<std::size_t, std::size_t>> selected;
    if (keep_selection_)
        for (auto &p : points_)
            if (p.selected)
                selected.emplace(p.mesh, p.vertex);
    keep_selection_ = false;
    bool changed = material_ != material || channel_ != channel;
    points_.clear();
    triangles_.clear();
    before_.clear();
    dragging_ = boxing_ = false;
    material_ = material;
    channel_ = channel;
    revision_ = doc.model_revision();
    auto exchange = doc.model_exchange();
    for (std::size_t draw = 0; draw < doc.model.scene->draws.size(); ++draw) {
        if (doc.model.scene->draws[draw].material != std::size_t(material))
            continue;
        auto native = doc.model.native_meshes.empty() ? draw : doc.model.native_meshes.at(draw);
        auto &mesh = exchange.meshes.at(native);
        auto format = mesh.formats.at(4 + channel);
        if (format[1] < 2)
            continue;
        float divisor = format[0] == 3   ? 1.f
                        : format[0] == 2 ? 32767.f
                        : format[0] == 1 ? 255.f
                                         : 127.f;
        std::map<std::size_t, std::size_t> ids;
        for (auto vertex : mesh.indices)
            if (!ids.contains(vertex)) {
                auto index = points_.size();
                ids[vertex] = index;
                auto &uv = mesh.vertices[vertex].channels[4 + channel];
                points_.push_back({native,
                                   vertex,
                                   index,
                                   {uv[0] / divisor, uv[1] / divisor},
                                   selected.contains({native, vertex})});
            }
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
            triangles_.push_back({ids.at(mesh.indices[i]), ids.at(mesh.indices[i + 1]),
                                  ids.at(mesh.indices[i + 2])});
    }
    std::vector<std::size_t> parents(points_.size());
    std::iota(parents.begin(), parents.end(), 0);
    auto root = [&](std::size_t i) {
        while (parents[i] != i) {
            parents[i] = parents[parents[i]];
            i = parents[i];
        }
        return i;
    };
    for (auto t : triangles_)
        for (unsigned k = 1; k < 3; ++k)
            parents[root(t[k])] = root(t[0]);
    for (std::size_t i = 0; i < points_.size(); ++i)
        points_[i].island = root(i);
    if (changed)
        fit_ = true;
}
void UvEditor::transform(ImVec2 move, float angle, ImVec2 scale) {
    float c = std::cos(angle), s = std::sin(angle);
    for (std::size_t i = 0; i < points_.size(); ++i)
        if (points_[i].selected) {
            auto p = before_[i];
            float x = (p.x - pivot_.x) * scale.x, y = (p.y - pivot_.y) * scale.y;
            points_[i].uv = {pivot_.x + x * c - y * s + move.x, pivot_.y + x * s + y * c + move.y};
        }
}
void UvEditor::apply(MaterialDocument &doc) {
    MeshUvEdits edits;
    for (auto &p : points_)
        if (p.selected)
            edits[p.mesh][p.vertex] = {p.uv.x, p.uv.y};
    try {
        doc.edit_uvs(channel_, edits);
        error_.clear();
    } catch (const std::exception &e) {
        error_ = e.what();
        for (std::size_t i = 0; i < points_.size() && i < before_.size(); ++i)
            points_[i].uv = before_[i];
    }
    keep_selection_ = true;
    revision_ = ~std::uint64_t(0);
    dragging_ = false;
}
void UvEditor::draw(MaterialDocument &doc, const EnvironmentRenderer &renderer, int material,
                    unsigned unit) {
    auto input = doc.model.scene->materials.at(material).inputs.at(unit);
    if (input.source > 2) {
        ImGui::TextWrapped("Generated texture coordinates cannot be edited as mesh UVs.");
        return;
    }
    try {
        sync(doc, material, input.source);
    } catch (const std::exception &e) {
        revision_ = ~std::uint64_t(0);
        ImGui::TextWrapped("%s", e.what());
        return;
    }
    ImGui::Text("UV %u | %zu vertices", channel_, points_.size());
    ImGui::TextWrapped("Editing original UV tiles. Material transforms remain separate.");
    studio::TutorialWidgets::RadioButton("uv_editor", "Vertices", &selection_, 0);
    ImGui::SameLine();
    studio::TutorialWidgets::RadioButton("uv_editor", "Islands", &selection_, 1);
    studio::TutorialWidgets::RadioButton("uv_editor", "Select", &operation_, 0);
    ImGui::SameLine();
    studio::TutorialWidgets::RadioButton("uv_editor", "Move", &operation_, 1);
    ImGui::SameLine();
    studio::TutorialWidgets::RadioButton("uv_editor", "Rotate", &operation_, 2);
    ImGui::SameLine();
    studio::TutorialWidgets::RadioButton("uv_editor", "Scale", &operation_, 3);
    if (studio::TutorialWidgets::Button("uv_editor", "Select all"))
        for (auto &p : points_)
            p.selected = true;
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("uv_editor", "Clear"))
        for (auto &p : points_)
            p.selected = false;
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("uv_editor", "Fit UVs"))
        fit_ = true;
    auto count = std::count_if(points_.begin(), points_.end(), [](auto &p) {
        return p.selected;
    });
    ImGui::Text("%zu selected", std::size_t(count));
    auto snapshot = [&] {
        before_.clear();
        pivot_ = {};
        for (auto &p : points_) {
            before_.push_back(p.uv);
            if (p.selected) {
                pivot_.x += p.uv.x / float(count);
                pivot_.y += p.uv.y / float(count);
            }
        }
    };
    if (studio::TutorialWidgets::TreeNode("uv_editor", "Numeric transform")) {
        ImGui::SetNextItemWidth(150);
        ImGui::InputFloat2("Move UV", move_);
        ImGui::SetNextItemWidth(150);
        ImGui::InputFloat("Rotate degrees", &angle_);
        ImGui::SetNextItemWidth(150);
        ImGui::InputFloat2("Scale UV", scale_);
        ImGui::BeginDisabled(!count);
        if (studio::TutorialWidgets::Button("uv_editor", "Apply UV transform")) {
            snapshot();
            transform({move_[0], move_[1]}, angle_ * .01745329252f, {scale_[0], scale_[1]});
            apply(doc);
            move_[0] = move_[1] = angle_ = 0;
            scale_[0] = scale_[1] = 1;
        }
        ImGui::EndDisabled();
        ImGui::TreePop();
    }
    ImGui::TextWrapped(
        "Click or drag a box to select; Ctrl adds/removes. In Move/Rotate/Scale, drag the canvas "
        "to transform the selection. Escape cancels. Wheel zooms; middle drag pans.");
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    auto origin = ImGui::GetCursorScreenPos();
    ImVec2 size{std::max(32.f, ImGui::GetContentRegionAvail().x),
                std::max(160.f, ImGui::GetContentRegionAvail().y)};
    ImGui::InvisibleButton("##uv-canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    auto &io = ImGui::GetIO();
    float base = std::min(size.x, size.y);
    if (fit_ && !points_.empty()) {
        float lo_x = INFINITY, lo_y = INFINITY, hi_x = -INFINITY, hi_y = -INFINITY;
        for (auto &p : points_) {
            lo_x = std::min(lo_x, p.uv.x);
            lo_y = std::min(lo_y, p.uv.y);
            hi_x = std::max(hi_x, p.uv.x);
            hi_y = std::max(hi_y, p.uv.y);
        }
        center_ = {(lo_x + hi_x) * .5f, (lo_y + hi_y) * .5f};
        zoom_ = std::clamp(
            .85f *
                std::min(size.x / std::max(.1f, hi_x - lo_x), size.y / std::max(.1f, hi_y - lo_y)) /
                base,
            .05f, 100.f);
        fit_ = false;
    }
    float pixels = base * zoom_;
    auto uv = [&](ImVec2 p) {
        return ImVec2{center_.x + (p.x - origin.x - size.x * .5f) / pixels,
                      center_.y + (p.y - origin.y - size.y * .5f) / pixels};
    };
    if (hovered && !dragging_ && !boxing_) {
        if (io.MouseWheel) {
            auto anchor = uv(io.MousePos);
            zoom_ = std::clamp(zoom_ * std::pow(1.2f, io.MouseWheel), .05f, 100.f);
            pixels = base * zoom_;
            auto after = uv(io.MousePos);
            center_.x += anchor.x - after.x;
            center_.y += anchor.y - after.y;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            center_.x -= io.MouseDelta.x / pixels;
            center_.y -= io.MouseDelta.y / pixels;
        }
    }
    auto screen = [&](ImVec2 p) {
        return ImVec2{origin.x + size.x * .5f + (p.x - center_.x) * pixels,
                      origin.y + size.y * .5f + (p.y - center_.y) * pixels};
    };
    auto mouse = uv(io.MousePos);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        start_ = mouse;
        if (operation_ && count) {
            snapshot();
            dragging_ = true;
        } else {
            std::optional<std::size_t> hit;
            float best = 64;
            for (std::size_t i = 0; i < points_.size(); ++i) {
                auto p = screen(points_[i].uv);
                float d = (p.x - io.MousePos.x) * (p.x - io.MousePos.x) +
                          (p.y - io.MousePos.y) * (p.y - io.MousePos.y);
                if (d < best) {
                    best = d;
                    hit = i;
                }
            }
            if (!hit && selection_ == 1)
                for (auto t : triangles_) {
                    auto a = points_[t[0]].uv, b = points_[t[1]].uv, c = points_[t[2]].uv;
                    auto cross = [](ImVec2 a, ImVec2 b, ImVec2 p) {
                        return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
                    };
                    float x = cross(a, b, mouse), y = cross(b, c, mouse), z = cross(c, a, mouse);
                    if (std::abs(cross(a, b, c)) > 1e-12f &&
                        ((x >= 0 && y >= 0 && z >= 0) || (x <= 0 && y <= 0 && z <= 0))) {
                        hit = t[0];
                        break;
                    }
                }
            bool selected = hit ? !points_[*hit].selected : true;
            if (!io.KeyCtrl)
                for (auto &p : points_)
                    p.selected = false;
            if (hit) {
                auto island = points_[*hit].island;
                for (std::size_t i = 0; i < points_.size(); ++i)
                    if (i == *hit || (selection_ == 1 && points_[i].island == island))
                        points_[i].selected = io.KeyCtrl ? selected : true;
            } else
                boxing_ = true;
        }
    }
    if (dragging_) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            for (std::size_t i = 0; i < points_.size(); ++i)
                points_[i].uv = before_[i];
            dragging_ = false;
        } else {
            if (operation_ == 1)
                transform({mouse.x - start_.x, mouse.y - start_.y}, 0, {1, 1});
            if (operation_ == 2)
                transform({},
                          std::atan2(mouse.y - pivot_.y, mouse.x - pivot_.x) -
                              std::atan2(start_.y - pivot_.y, start_.x - pivot_.x),
                          {1, 1});
            if (operation_ == 3) {
                float initial = std::hypot(start_.x - pivot_.x, start_.y - pivot_.y);
                float scale = initial > 1e-6f
                                  ? std::hypot(mouse.x - pivot_.x, mouse.y - pivot_.y) / initial
                                  : 1;
                transform({}, 0, {scale, scale});
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                apply(doc);
        }
    }
    if (boxing_ && ImGui::IsKeyPressed(ImGuiKey_Escape))
        boxing_ = false;
    if (boxing_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        std::set<std::size_t> hits, islands;
        for (std::size_t i = 0; i < points_.size(); ++i) {
            auto &p = points_[i];
            if (p.uv.x >= std::min(start_.x, mouse.x) && p.uv.x <= std::max(start_.x, mouse.x) &&
                p.uv.y >= std::min(start_.y, mouse.y) && p.uv.y <= std::max(start_.y, mouse.y)) {
                hits.insert(i);
                islands.insert(p.island);
            }
        }
        for (std::size_t i = 0; i < points_.size(); ++i)
            if (hits.contains(i) || (selection_ == 1 && islands.contains(points_[i].island)))
                points_[i].selected = io.KeyCtrl ? !points_[i].selected : true;
        boxing_ = false;
    }
    auto *lines = ImGui::GetWindowDrawList();
    lines->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    lines->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(25, 32, 38, 255));
    auto lo = uv(origin), hi = uv({origin.x + size.x, origin.y + size.y});
    auto texture =
        renderer.texture(doc.model.scene->materials.at(material).texture_inputs.at(unit));
    if (bgfx::isValid(texture))
        for (int y = int(std::floor(lo.y)); y <= int(std::floor(hi.y)); ++y)
            for (int x = int(std::floor(lo.x)); x <= int(std::floor(hi.x)); ++x) {
                if ((input.wrap_u < 2 && x != 0) || (input.wrap_v < 2 && y != 0))
                    continue;
                bool flip_u = input.wrap_u == 3 && x % 2 != 0,
                     flip_v = input.wrap_v == 3 && y % 2 != 0;
                auto a = screen({float(x), float(y)}), b = screen({float(x + 1), float(y + 1)});
                lines->AddImage(ImTextureID(ImGuiRenderer::image_id(texture, false)), a, b,
                                {flip_u ? 1.f : 0.f, flip_v ? 1.f : 0.f},
                                {flip_u ? 0.f : 1.f, flip_v ? 0.f : 1.f},
                                IM_COL32(190, 190, 190, 255));
                lines->AddRect(a, b, IM_COL32(160, 160, 160, 180));
                char label[64];
                std::snprintf(label, sizeof(label), "%d, %d", x, y);
                lines->AddText(a, IM_COL32(255, 255, 255, 255), label);
            }
    for (auto t : triangles_) {
        auto a = screen(points_[t[0]].uv), b = screen(points_[t[1]].uv),
             c = screen(points_[t[2]].uv);
        lines->AddTriangle(a, b, c, IM_COL32(0, 0, 0, 220), 2);
        lines->AddTriangle(a, b, c, IM_COL32(90, 240, 255, 210), 1);
    }
    for (auto &p : points_)
        lines->AddCircleFilled(screen(p.uv), p.selected ? 3.f : 2.f,
                               p.selected ? IM_COL32(255, 180, 40, 255)
                                          : IM_COL32(80, 220, 240, 200));
    if (boxing_)
        lines->AddRect(screen(start_), screen(mouse), IM_COL32(255, 190, 60, 255));
    lines->PopClipRect();
}
}
