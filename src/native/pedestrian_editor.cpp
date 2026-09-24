#include "native/undo_shortcuts.h"
#include "native/pedestrian_editor.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include "field/area.h"
#include "formats/container.h"
namespace studio {
void PedestrianEditor::set_scene(std::shared_ptr<Environment> scene, unsigned area,
                                 const std::filesystem::path &dump) {
    if (model_job_.valid()) {
        model_job_.wait();
        try {
            model_job_.get();
        } catch (...) {
        }
    }
    model_.reset();
    walk_ = {};
    walk_frames_ = 0;
    dump_ = dump;
    area_ = area;
    binding_.unbind();
    scene_ = std::move(scene);
    document_.reset();
    route_ = -1;
    axis_ = -1;
    playing_ = false;
    error_.clear();
    if (!scene_)
        return;
    try {
        document_ = std::make_unique<PedestrianDocument>(scene_->placement_source);
        binding_.bind(
            "pedestrians", "pedestrians/" + std::to_string(area),
            "Pedestrian routes in area " + std::to_string(area), std::to_string(area),
            [this] {
                return document_ && document_->dirty();
            },
            [this] {
                return project_text(document_->serialize());
            },
            [this] {
                document_->mark_saved();
            });
        binding_.autosave_when([this] {
            return axis_ < 0;
        });
        if (auto p = binding_.document(); !p.empty()) {
            document_->restore(text(read_file(p)));
            binding_.restored();
        }
        synchronize();
    } catch (const std::exception &e) {
        error_ = e.what();
        document_.reset();
        binding_.unbind();
    }
}
void PedestrianEditor::select(unsigned zone, unsigned row) {
    if (!document_)
        return;
    for (unsigned i = 0; i < document_->routes().size(); ++i)
        if (document_->routes()[i].zone == zone && document_->routes()[i].row == row) {
            if (route_ != int(i)) {
                detach_model();
                route_ = int(i);
                draft_ = document_->routes()[i];
                point_ = choice_ = 0;
                progress_ = 0;
                reverse_ = false;
                axis_ = -1;
                playing_ = false;
            }
            return;
        }
}
void PedestrianEditor::synchronize() {
    if (!document_ || !scene_)
        return;
    for (auto &region : scene_->spatial.regions) {
        if (region.kind != SpatialKind::Pedestrian || !region.overworld)
            continue;
        for (auto &r : document_->routes())
            if (r.zone == region.overworld->local_zone && r.row == region.overworld->row) {
                region.vertices.clear();
                region.lines.clear();
                region.triangles.clear();
                append_route_geometry(region, r.path);
                break;
            }
    }
    renderer_.spatial.invalidate_geometry();
    renderer_.invalidate_selection_readback();
}
void PedestrianEditor::reveal() {
    renderer_.spatial.enabled[unsigned(SpatialKind::Pedestrian)] = true;
    renderer_.spatial.pick_overlays = true;
    for (unsigned i = 0; i < scene_->spatial.regions.size(); ++i) {
        auto &r = scene_->spatial.regions[i];
        if (r.kind == SpatialKind::Pedestrian && r.overworld &&
            r.overworld->local_zone == draft_.zone && r.overworld->row == draft_.row) {
            renderer_.spatial.selected = int(i);
            break;
        }
    }
}
void PedestrianEditor::apply() {
    try {
        document_->set(unsigned(route_), draft_);
        synchronize();
        error_.clear();
    } catch (const std::exception &e) {
        error_ = e.what();
        draft_ = document_->routes()[route_];
        point_ = std::min(point_, int(draft_.path.points.size()) - 1);
    }
}
void PedestrianEditor::update(bool enabled) {
    if (model_job_.valid() &&
        model_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            auto loaded = model_job_.get();
            if (active && enabled && route_ == requested_route_ && choice_ >= 0 &&
                std::size_t(choice_) < draft_.choices.size() &&
                draft_.choices[choice_][0] == requested_character_ &&
                draft_.choices[choice_][2] == requested_motion_) {
                attach_model(std::move(loaded));
                loaded_character_ = requested_character_;
                loaded_motion_ = requested_motion_;
                model_error_.clear();
            }
        } catch (const std::exception &e) {
            model_error_ = e.what();
        }
    if (!active || !enabled)
        detach_model();
    move_model(active && enabled && show_model_);
    if (!active || !enabled || !playing_ || route_ < 0)
        return;
    PedestrianCurve curve(draft_.path);
    if (curve.length() <= .001f)
        return;
    float dt = std::min(ImGui::GetIO().DeltaTime, .1f), distance = dt * speed_;
    if (animation_speed_ && walk_frames_ > 0 && !walk_.keys.empty()) {
        auto displacement = [&](double seconds) {
            double frames = seconds * 30;
            auto cycle = std::floor(frames / walk_frames_);
            return float(cycle) * (walk_.sample(walk_frames_, 0) - walk_.sample(0, 0)) +
                   walk_.sample(float(frames - cycle * walk_frames_), 0) - walk_.sample(0, 0);
        };
        distance = displacement(seconds_ + dt) - displacement(seconds_);
    }
    seconds_ += dt;
    float next = progress_ + distance / curve.length() * (reverse_ ? -1.f : 1.f);
    if (draft_.path.loop)
        next -= std::floor(next);
    else if (next >= 1 || next <= 0) {
        next = std::clamp(next, 0.f, 1.f);
        playing_ = false;
    }
    progress_ = next;
}
SpatialPoint PedestrianEditor::preview_position() const {
    auto p = PedestrianCurve(draft_.path).position(progress_);
    if (!draft_.path.follow_ground || !scene_)
        return p;
    float nearest = INFINITY, height = p[1];
    for (auto &r : scene_->spatial.regions) {
        if (r.kind != SpatialKind::Ground)
            continue;
        for (std::size_t i = 0; i + 2 < r.triangles.size(); i += 3) {
            auto a = r.vertices[r.triangles[i]].position,
                 b = r.vertices[r.triangles[i + 1]].position,
                 c = r.vertices[r.triangles[i + 2]].position;
            float x = b[0] - a[0], z = b[2] - a[2], u = c[0] - a[0], v = c[2] - a[2],
                  det = x * v - z * u;
            if (std::abs(det) < .00001f)
                continue;
            float px = p[0] - a[0], pz = p[2] - a[2], wb = (px * v - pz * u) / det,
                  wc = (x * pz - z * px) / det, wa = 1 - wb - wc;
            if (std::min({wa, wb, wc}) < -.0001f)
                continue;
            float y = wa * a[1] + wb * b[1] + wc * c[1], d = std::abs(y - p[1]);
            if (d < nearest) {
                nearest = d;
                height = y;
            }
        }
    }
    p[1] = height;
    return p;
}
void PedestrianEditor::load_model() {
    if (model_job_.valid() || draft_.choices.empty())
        return;
    auto c = draft_.choices[std::min(std::size_t(choice_), draft_.choices.size() - 1)];
    auto dump = dump_;
    auto archives = scene_->archive_sources;
    auto area = area_;
    model_error_.clear();
    requested_character_ = c[0];
    requested_motion_ = c[2];
    requested_route_ = route_;
    model_job_ = std::async(std::launch::async, [dump, archives, area, c] {
        auto fieldpath = archives.resolve(dump, GameProfile::field_archive(dump));
        Archive field(fieldpath);
        auto resource = area * TargetProfile::area_stride + TargetProfile::character_resource_slot;
        auto ac = Container::parse(field.decoded(resource), "AC");
        std::optional<Bytes> local;
        for (auto &bytes : ac.files) {
            auto cp = Container::parse(bytes, "CP");
            if (std::stoul(text(cp.files.at(0))) == c[0]) {
                local = cp.files.at(1);
                break;
            }
        }
        auto path = local ? fieldpath : archives.resolve(dump, TargetProfile::character_archive);
        auto result = load_library_model(dump, path, ModelCategory::FieldCharacters,
                                         {local ? resource : c[0], "Pedestrian preview"}, nullptr,
                                         "pedestrian-preview/", local ? &*local : nullptr);
        int motion = -1;
        for (unsigned i = 0; i < result.motions.size(); ++i)
            if (result.motions[i].group == 0 && result.motions[i].slot == c[2]) {
                motion = int(i);
                break;
            }
        require(motion >= 0, "This character has no authored walking motion at the selected slot");
        require(result.motions[motion].error.empty(), result.motions[motion].error);
        result.select_motion(motion, true);
        return result;
    });
}
void PedestrianEditor::detach_model() {
    if (!model_ || !scene_)
        return;
    scene_->draws.resize(base_draws_);
    scene_->materials.resize(base_materials_);
    scene_->skeletons.resize(base_skeletons_);

    scene_->lighting_tables.resize(base_tables_);
    for (auto &[name, image] : model_->scene->textures)
        scene_->textures.erase(name);
    model_.reset();
    walk_ = {};
    walk_frames_ = 0;
    renderer_.reload_scene_buffers();
}
void PedestrianEditor::attach_model(ModelDocument model) {
    detach_model();
    auto &source = *model.scene;
    for (auto &[name, image] : source.textures)
        require(!scene_->textures.contains(name), "Preview texture name conflicts with map data");
    base_draws_ = scene_->draws.size();
    base_materials_ = scene_->materials.size();
    base_skeletons_ = scene_->skeletons.size();

    base_tables_ = scene_->lighting_tables.size();

    scene_->lighting_tables.insert(scene_->lighting_tables.end(), source.lighting_tables.begin(),
                                   source.lighting_tables.end());
    for (auto mat : source.materials) {
        for (auto &table : mat.reflection_tables)
            if (table >= 0)
                table += int(base_tables_);
        scene_->materials.push_back(std::move(mat));
    }
    for (auto rig : source.skeletons) {
        if (rig.parent_skeleton >= 0)
            rig.parent_skeleton += int(base_skeletons_);
        scene_->skeletons.push_back(std::move(rig));
    }
    for (auto draw : source.draws) {
        draw.material += base_materials_;
        if (draw.skeleton >= 0)
            draw.skeleton += int(base_skeletons_);
        draw.placement = -1;
        draw.source.reset();
        draw.preview_only = true;
        draw.character = true;
        draw.name = "Pedestrian preview";
        scene_->draws.push_back(std::move(draw));
    }
    for (auto &[name, image] : source.textures)
        scene_->textures.emplace(name, image);
    auto &motion = model.motions.at(model.motion).skeletal;
    for (auto &track : motion.tracks)
        if (track.name == "_") {
            walk_ = track.curves[8];
            walk_frames_ = motion.frames;
            break;
        }
    model_ = std::make_unique<ModelDocument>(std::move(model));
    seconds_ = 0;
    renderer_.reload_scene_buffers();
    move_model(active && show_model_);
}
void PedestrianEditor::move_model(bool visible) {
    if (!model_ || !scene_)
        return;
    for (std::size_t i = base_draws_; i < scene_->draws.size(); ++i)
        renderer_.set_draw_visible(i, visible);
    auto p = preview_position(), dir = PedestrianCurve(draft_.path).direction(progress_);
    float angle = std::atan2(dir[0], dir[2]) + (reverse_ ? 3.14159265f : 0);
    auto m = pose_identity();
    m[0] = m[10] = std::cos(angle);
    m[2] = std::sin(angle);
    m[8] = -m[2];
    m[3] = p[0];
    m[7] = p[1];
    m[11] = p[2];
    for (std::size_t i = base_draws_; i < scene_->draws.size(); ++i)
        renderer_.preview_transforms[i] = m;
    for (std::size_t i = base_skeletons_; i < scene_->skeletons.size(); ++i)
        scene_->skeletons[i].seconds_offset = seconds_ - renderer_.playback.seconds;
}
SpatialRegion PedestrianEditor::route_geometry() const {
    SpatialRegion result;
    if (document_ && route_ >= 0)
        append_route_geometry(result, draft_.path);
    return result;
}
std::string PedestrianEditor::copy_details() const {
    std::ostringstream out;
    if (!document_ || route_ < 0)
        return {};
    out << "Pedestrian route " << draft_.row + 1 << " / local zone " << draft_.zone << '\n';
    out << "Curved: " << draft_.path.curved << " / Loop: " << draft_.path.loop
        << " / Follow ground: " << draft_.path.follow_ground << '\n';
    out << "Cooldown: " << draft_.cooldown << " seconds\n";
    for (unsigned i = 0; i < draft_.path.points.size(); ++i) {
        auto p = draft_.path.points[i];
        out << "Point " << i + 1 << ": " << p[0] << ", " << p[1] << ", " << p[2] << '\n';
    }
    for (auto c : draft_.choices) {
        out << "Character " << c[0] << ": weight " << c[1] << ", motion " << c[2] << ", emote "
            << c[3] << ", script " << c[4] << ", version " << c[5] << ", weather bits " << c[6]
            << ", time bits " << c[7] << '\n';
    }
    return out.str();
}
void PedestrianEditor::draw(ViewportCamera &camera) {
    if (!document_ || route_ < 0) {
        ImGui::TextWrapped("%s",
                           error_.empty() ? "No editable route is available." : error_.c_str());
        return;
    }
    ImGui::BeginDisabled(!project_store());
    if (ImGui::Button("Save Project"))
        try {
            save_editor_project();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    ImGui::SameLine();
    ImGui::BeginDisabled(!document_->can_undo());
    if (studio::UndoShortcuts::button("pedestrian_editor", "Undo")) {
        document_->undo();
        draft_ = document_->routes()[route_];
        point_ = std::min(point_, int(draft_.path.points.size()) - 1);
        synchronize();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!document_->can_redo());
    if (studio::UndoShortcuts::button("pedestrian_editor", "Redo")) {
        document_->redo();
        draft_ = document_->routes()[route_];
        point_ = std::min(point_, int(draft_.path.points.size()) - 1);
        synchronize();
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (document_->dirty())
        ImGui::TextDisabled("Unsaved route edits");
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    if (!ImGui::BeginTabBar("Pedestrian details"))
        return;
    if (ImGui::BeginTabItem("Path")) {
        ImGui::BeginDisabled(!project_store());
        bool changed = ImGui::Checkbox("Curved path", &draft_.path.curved);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Loop", &draft_.path.loop);
        changed |= ImGui::Checkbox("Follow ground in game", &draft_.path.follow_ground);
        if (changed)
            apply();
        ImGui::EndDisabled();
        ImGui::TextDisabled("%.1f world units | %zu control points",
                            PedestrianCurve(draft_.path).length(), draft_.path.points.size());
        if (ImGui::Button("Edit points on map")) {
            show_points_ = true;
            reveal();
        }
        ImGui::Checkbox("Numbered points", &show_points_);
        ImGui::SameLine();
        ImGui::Checkbox("Control-point guide", &show_guide_);
        ImGui::TextWrapped("Click a numbered point in the map, then drag its X, Y or Z arrow. "
                           "Escape cancels a drag.");
        ImGui::BeginChild("Route points", {0, 140}, ImGuiChildFlags_Borders);
        for (unsigned i = 0; i < draft_.path.points.size(); ++i) {
            auto p = draft_.path.points[i];
            char label[120];
            std::snprintf(label, sizeof(label), "%u   %.1f, %.1f, %.1f", i + 1, p[0], p[1], p[2]);
            if (ImGui::Selectable(label, point_ == int(i)))
                point_ = int(i);
        }
        ImGui::EndChild();
        if (point_ >= 0 && std::size_t(point_) < draft_.path.points.size()) {
            ImGui::BeginDisabled(!project_store());
            ImGui::InputFloat3("Point position", draft_.path.points[point_].data(), "%.3f");
            if (ImGui::IsItemDeactivatedAfterEdit())
                apply();
            ImGui::BeginDisabled(draft_.path.points.size() +
                                     (draft_.path.loop && !draft_.path.curved ? 1 : 0) >=
                                 TargetProfile::pedestrian_point_capacity);
            if (ImGui::Button("Insert after")) {
                auto p = draft_.path.points[point_];
                auto next = (point_ + 1) % draft_.path.points.size();
                if (next || draft_.path.loop)
                    for (unsigned k = 0; k < 3; ++k)
                        p[k] = (p[k] + draft_.path.points[next][k]) * .5f;
                else {
                    auto prev = draft_.path.points[point_ - 1];
                    for (unsigned k = 0; k < 3; ++k)
                        p[k] += p[k] - prev[k];
                }
                draft_.path.points.insert(draft_.path.points.begin() + point_ + 1, p);
                ++point_;
                apply();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(draft_.path.points.size() <= 2);
            if (ImGui::Button("Remove point")) {
                draft_.path.points.erase(draft_.path.points.begin() + point_);
                point_ = std::min(point_, int(draft_.path.points.size()) - 1);
                apply();
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        }
        if (draft_.path.follow_ground)
            ImGui::TextWrapped("The displayed path uses authored heights. Ground collision can "
                               "change the character's height in game.");
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Pedestrians")) {
        ImGui::BeginDisabled(!project_store());
        ImGui::InputFloat("Respawn cooldown (seconds)", &draft_.cooldown);
        if (ImGui::IsItemDeactivatedAfterEdit())
            apply();
        ImGui::EndDisabled();
        ImGui::TextDisabled("A negative cooldown disables respawning.");
        if (draft_.choices.empty())
            ImGui::TextWrapped("This route has no pedestrian choices.");
        for (unsigned i = 0; i < draft_.choices.size(); ++i) {
            auto &c = draft_.choices[i];
            auto label = "Character " + std::to_string(c[0]) + " / weight " + std::to_string(c[1]) +
                         "##" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), choice_ == int(i)))
                choice_ = int(i);
        }
        if (choice_ >= 0 && std::size_t(choice_) < draft_.choices.size()) {
            auto &c = draft_.choices[choice_];
            const char *labels[] = {"Character resource",
                                    "Appearance weight",
                                    "Walk motion",
                                    "Emote",
                                    "Interaction script",
                                    "Game version",
                                    "Weather bits (runtime unverified)",
                                    "Time-of-day bits"};
            ImGui::Separator();
            ImGui::BeginDisabled(!project_store());
            for (unsigned k = 0; k < 8; ++k) {
                ImGui::InputScalar(labels[k], ImGuiDataType_U32, &c[k]);
                if (ImGui::IsItemDeactivatedAfterEdit())
                    apply();
            }
            ImGui::EndDisabled();
            ImGui::TextWrapped("Eligibility uses game state. Raw condition fields are retained; "
                               "this preview does not run the spawn scheduler or scripts.");
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Preview")) {
        ImGui::Checkbox("Show route marker", &show_preview_);
        if (ImGui::Button(playing_ ? "Pause" : "Play")) {
            playing_ = !playing_;
            reveal();
        }
        ImGui::SameLine();
        if (ImGui::Button("Restart")) {
            progress_ = reverse_ ? 1.f : 0.f;
            seconds_ = 0;
            reveal();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Reverse", &reverse_);
        ImGui::SameLine();
        if (ImGui::Button("Frame preview")) {
            auto p = preview_position(), low = p, high = p;
            low[0] -= 120;
            low[2] -= 120;
            high[0] += 120;
            high[1] += 180;
            high[2] += 120;
            camera.fit(low, high);
            camera.bounds_low = scene_->low;
            camera.bounds_high = scene_->high;
            reveal();
        }
        if (ImGui::SliderFloat("Route progress", &progress_, 0, 1, "%.3f")) {
            playing_ = false;
            seconds_ = progress_ * PedestrianCurve(draft_.path).length() / std::max(speed_, 1.f);
            move_model(active && show_model_);
        }
        if (!draft_.choices.empty()) {
            auto &c = draft_.choices[std::min(std::size_t(choice_), draft_.choices.size() - 1)];
            ImGui::Text("Selected pedestrian: character %u / motion %u", c[0], c[2]);
            ImGui::BeginDisabled(model_job_.valid());
            if (ImGui::Button(model_job_.valid() ? "Loading character..."
                                                 : "Load selected pedestrian")) {
                load_model();
                reveal();
            }
            ImGui::EndDisabled();
        }
        if (model_) {
            ImGui::Checkbox("Show character on route", &show_model_);
            ImGui::TextDisabled("Loaded character %u / walk motion %u", loaded_character_,
                                loaded_motion_);
        }
        if (!model_error_.empty())
            ImGui::TextWrapped("%s", model_error_.c_str());
        ImGui::BeginDisabled(walk_.keys.empty());
        ImGui::Checkbox("Use walking-animation speed", &animation_speed_);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(animation_speed_ && !walk_.keys.empty());
        ImGui::SliderFloat("Preview speed (units / second)", &speed_, 1, 500, "%.1f");
        ImGui::EndDisabled();
        if (ImGui::CollapsingHeader("Preview limits"))
            ImGui::TextWrapped(
                "Movement preview, not full pedestrian AI: spawning, collision avoidance, endpoint "
                "waits, turn animations and scripts are not simulated. Non-looping preview stops "
                "at "
                "its endpoint. Ground height uses the nearest loaded ground surface.");
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
bool PedestrianEditor::viewport(const float *view, const float *projection, ImVec2 origin,
                                ImVec2 size, bool hovered) {
    if (!active || !scene_ || !document_ || route_ < 0 ||
        !renderer_.spatial.enabled[unsigned(SpatialKind::Pedestrian)]) {
        if (axis_ >= 0) {
            draft_ = drag_start_;
            axis_ = -1;
        }
        point_click_ = false;
        return false;
    }
    auto &io = ImGui::GetIO();
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    auto project = [&](SpatialPoint p, ImVec2 &out) {
        float a[4]{}, b[4]{};
        for (unsigned i = 0; i < 4; ++i)
            a[i] = view[i] * p[0] + view[4 + i] * p[1] + view[8 + i] * p[2] + view[12 + i];
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned j = 0; j < 4; ++j)
                b[i] += projection[j * 4 + i] * a[j];
        if (b[3] <= .001f)
            return false;
        out = {origin.x + (b[0] / b[3] * .5f + .5f) * size.x,
               origin.y + (.5f - b[1] / b[3] * .5f) * size.y};
        return true;
    };
    auto hit = [](ImVec2 p, ImVec2 a, ImVec2 b) {
        float x = b.x - a.x, y = b.y - a.y;
        float t = std::clamp(((p.x - a.x) * x + (p.y - a.y) * y) / std::max(.001f, x * x + y * y),
                             0.f, 1.f);
        return std::hypot(p.x - a.x - x * t, p.y - a.y - y * t);
    };
    bool captured = axis_ >= 0 || point_click_;
    if (point_click_ && !ImGui::IsMouseDown(0))
        point_click_ = false;
    auto &points = draft_.path.points;
    if (show_guide_)
        for (unsigned i = 1; i < points.size(); ++i) {
            ImVec2 a, b;
            if (project(points[i - 1], a) && project(points[i], b))
                draw->AddLine(a, b, IM_COL32(155, 165, 175, 100));
        }
    if (show_points_)
        for (unsigned i = 0; i < points.size(); ++i) {
            ImVec2 p;
            if (!project(points[i], p))
                continue;
            draw->AddCircleFilled(p, point_ == int(i) ? 6.f : 4.f,
                                  point_ == int(i) ? IM_COL32(255, 210, 80, 255)
                                                   : IM_COL32(120, 215, 255, 255));
            char label[16];
            std::snprintf(label, sizeof(label), "%u", i + 1);
            draw->AddText({p.x + 7, p.y - 12}, IM_COL32(255, 255, 255, 255), label);
            if (axis_ < 0 && hovered && ImGui::IsMouseClicked(0) &&
                std::hypot(io.MousePos.x - p.x, io.MousePos.y - p.y) < 9) {
                point_ = int(i);
                captured = true;
                point_click_ = true;
            }
        }
    PedestrianCurve curve(draft_.path);
    for (unsigned i = 1; i <= 5; ++i) {
        auto p = curve.position(float(i) / 6), dir = curve.direction(float(i) / 6);
        ImVec2 a, b, c;
        SpatialPoint l = p, r = p;
        for (unsigned k = 0; k < 3; ++k) {
            l[k] -= dir[k] * 20;
            r[k] -= dir[k] * 20;
        }
        l[0] += dir[2] * 9;
        l[2] -= dir[0] * 9;
        r[0] -= dir[2] * 9;
        r[2] += dir[0] * 9;
        if (project(p, a) && project(l, b) && project(r, c)) {
            draw->AddLine(a, b, IM_COL32(100, 240, 215, 220), 2);
            draw->AddLine(a, c, IM_COL32(100, 240, 215, 220), 2);
        }
    }
    for (unsigned i = 0; i < (draft_.path.loop ? 1u : 2u); ++i) {
        ImVec2 p;
        if (project(curve.position(float(i)), p))
            draw->AddText({p.x + 9, p.y + 8}, IM_COL32(255, 230, 150, 255),
                          draft_.path.loop ? "Start / loop"
                          : i              ? "End"
                                           : "Start");
    }
    if (show_preview_) {
        ImVec2 p, q;
        auto v = preview_position(), d = curve.direction(progress_);
        auto tip = v;
        for (unsigned k = 0; k < 3; ++k)
            tip[k] += d[k] * 45 * (reverse_ ? -1.f : 1.f);
        if (project(v, p)) {
            draw->AddCircle(p, 10, IM_COL32(255, 190, 65, 255), 0, 3);
            if (project(tip, q))
                draw->AddLine(p, q, IM_COL32(255, 190, 65, 255), 3);
        }
    }
    bool editable = project_store() && !renderer_.player.active &&
                    !renderer_.spatial.locked[unsigned(SpatialKind::Pedestrian)];
    if (show_points_ && point_ >= 0 && std::size_t(point_) < points.size() && editable) {
        auto center_world = axis_ >= 0 ? drag_start_.path.points[point_] : points[point_];
        ImVec2 center;
        if (project(center_world, center)) {
            float depth = -(view[2] * center_world[0] + view[6] * center_world[1] +
                            view[10] * center_world[2] + view[14]);
            float length = axis_ >= 0 ? handle_length_ : std::clamp(depth * .1f, 15.f, 2000.f);
            const ImU32 colors[] = {IM_COL32(245, 85, 80, 255), IM_COL32(90, 230, 115, 255),
                                    IM_COL32(80, 160, 255, 255)};
            for (unsigned k = 0; k < 3; ++k) {
                auto tip = center_world;
                tip[k] += length;
                ImVec2 end;
                if (!project(tip, end))
                    continue;
                draw->AddLine(center, end, colors[k], 3);
                draw->AddCircleFilled(end, 4, colors[k]);
                draw->AddText({end.x + 4, end.y}, colors[k], k == 0 ? "X" : k == 1 ? "Y" : "Z");
                if (axis_ < 0 && hovered && ImGui::IsMouseClicked(0) &&
                    hit(io.MousePos, center, end) < 6) {
                    axis_ = int(k);
                    drag_start_ = draft_;
                    start_mouse_ = io.MousePos;
                    handle_length_ = length;
                    captured = true;
                }
                if (axis_ == int(k) && ImGui::IsMouseDown(0)) {
                    float x = end.x - center.x, y = end.y - center.y;
                    float delta = ((io.MousePos.x - start_mouse_.x) * x +
                                   (io.MousePos.y - start_mouse_.y) * y) /
                                  std::max(.001f, x * x + y * y) * length;
                    points[point_][k] = drag_start_.path.points[point_][k] + delta;
                }
            }
        }
    }
    if (axis_ >= 0) {
        captured = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || !editable) {
            draft_ = drag_start_;
            axis_ = -1;
        } else if (ImGui::IsMouseReleased(0)) {
            axis_ = -1;
            apply();
        } else {
            PedestrianCurve moved(draft_.path);
            ImVec2 last, next;
            for (unsigned i = 1; i <= 100; ++i)
                if (project(moved.position(float(i - 1) / 100), last) &&
                    project(moved.position(float(i) / 100), next))
                    draw->AddLine(last, next, IM_COL32(255, 210, 80, 255), 2);
        }
    }
    draw->PopClipRect();
    return captured;
}
}
