#include "native/viewport_navigation.h"
#include "native/warp_ride_workspace.h"
#include "native/imgui_renderer.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <bx/math.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
namespace studio {
namespace {
constexpr double course_length = 10000;
}
WarpRideWorkspace::WarpRideWorkspace(const std::filesystem::path &shaders, SDL_Window *window)
    : window_(window), renderer_(shaders) {
    gamepad_initialized_ = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    ride_.restart(1, settings_);
    renderer_.lighting.enabled = true;
    renderer_.lighting.game = false;
    renderer_.lighting.soft = true;
    renderer_.lighting.camera_relative = true;
    renderer_.lighting.ambient = .75f;
    renderer_.lighting.strength = .25f;
}
WarpRideWorkspace::~WarpRideWorkspace() {
    cancel_ = true;
    if (loading_.valid())
        loading_.wait();
    if (gamepad_)
        SDL_CloseGamepad(gamepad_);
    if (gamepad_initialized_)
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}
void WarpRideWorkspace::activate(bool active) {
    active_ = active;
    if (!active || !(SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS)) {
        running_ = false;
        focused_ = false;
    }
}
void WarpRideWorkspace::load(const std::filesystem::path &dump) {
    requested_ = dump;
    cancel_ = true;
    running_ = false;
    if (loading_.valid())
        return;
    source_ = dump;
    error_.clear();
    renderer_.set_scene({});
    assets_ = {};
    instances_.clear();
    cancel_ = false;
    loading_ = std::async(std::launch::async, [this, dump] {
        return load_ride_assets(dump, &cancel_);
    });
}
void WarpRideWorkspace::restart() {
    ride_.restart(std::uint32_t(seed_), settings_);
    running_ = false;
    free_camera_ = false;
    resource_view_ = false;
    animation_seconds_ = 0;
    for (auto &instance : instances_)
        instance.section = -999;
}
void WarpRideWorkspace::install(RideAssets assets) {
    assets_ = std::move(assets);
    instances_.clear();
    auto &scene = *assets_.scene;
    for (auto asset : {3u, 4u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u}) {
        const auto geometry = assets_.geometry[asset];
        const unsigned count = asset >= 13 || asset == 6 ? 1 : asset == 7 ? 5 : 32;
        for (unsigned n = 0; n < count; ++n) {
            Instance instance{asset, scene.draws.size(), geometry.count,
                              scene.placement_transforms.size()};
            scene.placement_transforms.push_back(pose_identity());
            for (auto i = geometry.first; i < geometry.first + geometry.count; ++i) {
                auto draw = scene.draws[i];
                draw.placement = int(instance.placement);
                if (asset == 6) {
                    auto material = scene.materials[draw.material];
                    material.layer = -10000;
                    material.depth_state &= ~0x1001u;
                    auto original = draw.material;
                    draw.material = scene.materials.size();
                    scene.materials.push_back(material);
                    for (auto &animation : scene.material_animations) {
                        auto bindings = animation.bindings;
                        for (const auto &binding : bindings)
                            if (binding.material == original)
                                animation.bindings.push_back({binding.track, draw.material});
                    }
                }
                scene.draws.push_back(std::move(draw));
            }
            instances_.push_back(instance);
        }
    }
    camera_.fit(assets_.geometry[selected_].low, assets_.geometry[selected_].high);
    renderer_.set_scene(assets_.scene);
    restart();
}
void WarpRideWorkspace::place(const Instance &instance, float x, float y, double distance) {
    const auto frame = ride_.route.frame(distance);
    const auto position = ride_.route.position(distance, x, y, ride_.distance);
    const bool character = instance.asset >= 13;
    const float scale = 1;
    auto origin = std::array<float, 3>{};
    if (character && mount_ > 0)
        origin = assets_.rider_origins[std::size_t((mount_ - 1) * 2 + (rider_ == 2 ? 1 : 0))];
    auto transform = pose_identity();
    for (unsigned r = 0; r < 3; ++r) {
        float facing = character ? -1.f : 1.f;
        transform[r * 4] = frame.right[r] * scale * facing;
        transform[r * 4 + 1] = frame.up[r] * scale;
        transform[r * 4 + 2] = frame.back[r] * scale * facing;
        transform[r * 4 + 3] = position[r];
        for (unsigned c = 0; c < 3; ++c)
            transform[r * 4 + 3] -= transform[r * 4 + c] * origin[c];
    }
    assets_.scene->placement_transforms[instance.placement] = transform;
    for (auto i = instance.first; i < instance.first + instance.count; ++i)
        renderer_.set_draw_visible(i, true);
}
void WarpRideWorkspace::place_course(Instance &instance, std::int64_t section) {
    const double start = double(section) * course_length;
    const auto &geometry = assets_.geometry[instance.asset];
    if (instance.section != section && renderer_.ready()) {
        float scale = 1;
        for (std::size_t draw = 0; draw < geometry.count; ++draw) {
            auto vertices = assets_.scene->draws[geometry.first + draw].vertices;
            for (auto &v : vertices) {
                double at = start + (geometry.high[2] - v.z) /
                                        (geometry.high[2] - geometry.low[2]) * course_length;
                auto position = ride_.route.position(at, v.x * scale, v.y * scale, start);
                auto frame = ride_.route.frame(at);
                auto rotate = [&](float x, float y, float z) {
                    std::array<float, 3> out;
                    for (unsigned i = 0; i < 3; ++i)
                        out[i] = frame.right[i] * x + frame.up[i] * y + frame.back[i] * z;
                    return out;
                };
                auto normal = rotate(v.nx, v.ny, v.nz), tangent = rotate(v.tx, v.ty, v.tz);
                v.x = position[0];
                v.y = position[1];
                v.z = position[2];
                v.nx = normal[0];
                v.ny = normal[1];
                v.nz = normal[2];
                v.tx = tangent[0];
                v.ty = tangent[1];
                v.tz = tangent[2];
            }
            renderer_.preview_vertices(instance.first + draw, vertices);
        }
        instance.section = section;
    }
    auto translation = ride_.route.position(start, 0, 0, ride_.distance);
    auto transform = pose_identity();
    for (unsigned i = 0; i < 3; ++i)
        transform[i * 4 + 3] = translation[i];
    assets_.scene->placement_transforms[instance.placement] = transform;
    for (auto i = instance.first; i < instance.first + instance.count; ++i)
        renderer_.set_draw_visible(i, true);
}
void WarpRideWorkspace::reset_free_camera() {
    auto frame = ride_.route.frame(ride_.distance);
    auto base = ride_.route.position(ride_.distance, ride_.x * .3f, ride_.y * .3f, ride_.distance);
    std::array<float, 3> eye{}, direction{};
    free_view_ = ViewportCamera{};
    for (unsigned i = 0; i < 3; ++i) {
        eye[i] = base[i] + frame.back[i] * 1250 + frame.up[i] * 430;
        free_view_.target[i] = base[i] - frame.back[i] * 1000 + frame.up[i] * 100;
        direction[i] = free_view_.target[i] - eye[i];
    }
    free_view_.distance = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] +
                                    direction[2] * direction[2]);
    free_view_.yaw = std::atan2(-direction[0], -direction[2]);
    free_view_.pitch = std::asin(-direction[1] / free_view_.distance);
    free_view_.speed = 1500;
    free_origin_ = ride_.distance;
}
void WarpRideWorkspace::controls() {
    ImGui::Begin("Warp Ride controls");
    ImGui::TextDisabled("Preview only - settings are not exported");
    ImGui::SeparatorText("Simulation");
    ImGui::InputInt("Seed", &seed_);
    ImGui::BeginDisabled(!settings_.valid());
    if (ImGui::Button("Restart with settings"))
        restart();
    ImGui::EndDisabled();
    ImGui::Checkbox("Collision guides", &guides_);
    if (ImGui::Checkbox("Free camera", &free_camera_) && free_camera_) {
        running_ = false;
        reset_free_camera();
    }
    if (free_camera_) {
        ImGui::SameLine();
        if (ImGui::Button("Reset camera"))
            reset_free_camera();
        ImGui::TextWrapped("Play resumes the ride; steering stays disabled.");
        ImGui::TextWrapped("%s", viewport_navigation_help);
    }
    ImGui::SeparatorText("Rider and view");
    ImGui::SetNextItemWidth(150);
    ImGui::Combo("Mount", &mount_, "Marker only\0Solgaleo\0Lunala\0");
    ImGui::SetNextItemWidth(150);
    ImGui::Combo("Rider", &rider_, "None\0Boy\0Girl\0");
    if (!free_camera_ && ImGui::CollapsingHeader("Ride controls"))
        ImGui::TextWrapped(
            "Click the viewport to steer with WASD, arrow keys or a controller's left "
            "stick. Space pauses. Leaving the workspace pauses the run.");
    ImGui::Separator();
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * .45f);
    if (ImGui::CollapsingHeader("Energy and movement")) {
        ImGui::SliderFloat("Start energy", &settings_.initial_energy, 0, 2);
        ImGui::SliderFloat("Drain frames", &settings_.drain_frames, 300, 3600, "%.0f");
        ImGui::SliderFloat("Pickup gain", &settings_.pickup_energy, 0, .5f);
        ImGui::SliderFloat("Obstacle loss", &settings_.obstacle_loss, 0, 1);
        ImGui::SliderFloat("Min. speed", &settings_.minimum_speed, 10, 120, "%.0f");
        ImGui::SliderFloat("Max. speed", &settings_.maximum_speed, 50, 200, "%.0f");
        ImGui::SliderFloat("Steering", &settings_.steering, 1, 30, "%.1f");
    }
    if (ImGui::CollapsingHeader("Course and generation")) {
        ImGui::Checkbox("Curved preview route", &settings_.curved);
        float start = float(settings_.start_distance / 100);
        if (ImGui::SliderFloat("Start distance", &start, 0, 10000, "%.0f m"))
            settings_.start_distance = start * 100;
        ImGui::Checkbox("Game distance bands", &settings_.distance_rules);
        if (!settings_.distance_rules) {
            ImGui::SliderFloat("Object spacing", &settings_.spacing, 500, 5000, "%.0f");
            int obstacles = int(settings_.obstacle_percent),
                holes = int(settings_.wormhole_percent);
            if (ImGui::SliderInt("Obstacles %", &obstacles, 0, 100))
                settings_.obstacle_percent = unsigned(obstacles);
            if (ImGui::SliderInt("Wormholes %", &holes, 0, 100))
                settings_.wormhole_percent = unsigned(holes);
            ImGui::TextWrapped(
                "Remaining objects are energy pickups. Spacing and placement are simulator rules.");
        } else
            ImGui::TextWrapped("Pickup and obstacle rates change with distance. Wormholes start "
                               "after the opening stretch.");
        ImGui::Checkbox("First / story ride weights", &settings_.story);
        if (settings_.story) {
            int row = int(settings_.story_ride);
            if (ImGui::SliderInt("Story weight row", &row, 0, 4))
                settings_.story_ride = unsigned(row);
        }
        ImGui::TextWrapped("Type and rarity weights use the inspected game tables. This seed is "
                           "local to the simulator.");
    }
    if (ImGui::CollapsingHeader("Collision and attraction")) {
        ImGui::SliderFloat("Pickup radius", &settings_.pickup_radius, 20, 450, "%.0f");
        ImGui::SliderFloat("Obstacle radius", &settings_.obstacle_radius, 20, 450, "%.0f");
        ImGui::SliderFloat("Entry radius", &settings_.wormhole_radius, 20, 450, "%.0f");
        ImGui::SliderFloat("Attraction", &settings_.attraction, 0, .2f);
    }
    ImGui::PopItemWidth();
    if (!settings_.valid())
        ImGui::TextWrapped(
            "Check speed limits and keep obstacle + wormhole percentages at or below 100.");
    if (ImGui::Button("Restore default settings"))
        settings_ = RideTuning{};
    ImGui::TextDisabled("Settings apply on restart.");
    ImGui::End();
}
void WarpRideWorkspace::viewport() {
    ImGui::Begin("Warp Ride preview");
    if (!assets_.scene) {
        ImGui::TextUnformatted(loading_.valid() ? "Loading course resources..."
                                                : "Load course resources to preview the ride.");
        focused_ = false;
        ImGui::End();
        return;
    }
    ImGui::BeginDisabled(resource_view_ || ride_.result.entered || !assets_.scene ||
                         !renderer_.ready());
    if (ImGui::Button(running_ ? "Pause" : "Play"))
        running_ = !running_;
    ImGui::SameLine();
    ImGui::BeginDisabled(running_);
    if (ImGui::Button("Step frame"))
        ride_.tick(0, 0);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * .45f);
    ImGui::SliderFloat("Speed", &playback_speed_, .1f, 1.f, "%.2fx");
    renderer_.upload_step();
    if (resource_view_) {
        ImGui::Checkbox("Animate asset", &animate_);
        ImGui::SameLine();
        if (ImGui::Button("Frame asset"))
            camera_.fit(assets_.geometry[selected_].low, assets_.geometry[selected_].high);
        ImGui::TextWrapped("%s", viewport_navigation_help);
        float duration = 1;
        const auto prefix = "ride/" + std::to_string(selected_) + "/";
        const auto &geometry = assets_.geometry[selected_];
        for (auto i = geometry.first; i < geometry.first + geometry.count; ++i) {
            int skeleton = assets_.scene->draws[i].skeleton;
            if (skeleton >= 0)
                duration = std::max(duration,
                                    assets_.scene->skeletons[std::size_t(skeleton)].motion.frames);
        }
        for (const auto &motion : assets_.scene->material_animations)
            if (motion.texture_prefix == prefix)
                duration = std::max(duration, motion.motion.frames);
        float frame = float(std::fmod(animation_seconds_ * 30, duration));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##asset-frame", &frame, 0, duration, "Frame %.0f")) {
            animation_seconds_ = frame / 30;
            animate_ = false;
        }
    } else {
        ImGui::Text("%.0f m   Energy %.0f%%   %.0f units/frame", ride_.distance / 100,
                    ride_.energy * 100, ride_.speed());
        char energy_label[64];
        std::snprintf(energy_label, sizeof(energy_label), "%.0f%% energy / %.0f%% capacity",
                      ride_.energy * 100, ride_.energy_cap * 100);
        ImGui::ProgressBar(ride_.energy / ride_.energy_cap, ImVec2(-1, 0), energy_label);
    }
    for (std::size_t i = 0; i < assets_.scene->draws.size(); ++i)
        renderer_.set_draw_visible(i, false);
    if (resource_view_) {
        auto &geometry = assets_.geometry[selected_];
        for (auto i = geometry.first; i < geometry.first + geometry.count; ++i)
            renderer_.set_draw_visible(i, true);
    } else {
        unsigned course = 0;
        std::array<unsigned, WarpRideProfile::assets.size()> used{};
        for (auto &instance : instances_) {
            if (instance.asset == 7) {
                place_course(instance, std::int64_t(std::floor(ride_.distance / course_length)) -
                                           1 + course++);
                continue;
            }
            if (instance.asset == 6) {
                place(instance, 0, 0, ride_.distance);
                continue;
            }
            if (instance.asset >= 13) {
                if (mount_ > 0 &&
                    (instance.asset == std::size_t(12 + mount_) ||
                     (rider_ > 0 &&
                      instance.asset == std::size_t(15 + (mount_ - 1) * 2 + rider_ - 1))))
                    place(instance, ride_.x, ride_.y, ride_.distance);
                continue;
            }
            unsigned found = 0;
            for (const auto &object : ride_.objects) {
                if (object.consumed ||
                    object.distance <
                        ride_.distance - (object.kind == RideObjectKind::Wormhole ? 500 : 200))
                    continue;
                auto asset = object.kind == RideObjectKind::Energy     ? 3u
                             : object.kind == RideObjectKind::Obstacle ? 4u
                                                                       : 8u + object.type;
                if (asset != instance.asset || found++ != used[asset])
                    continue;
                place(instance, object.x, object.y, object.distance);
                ++used[asset];
                break;
            }
        }
    }
    const auto available = ImGui::GetContentRegionAvail();
    if (available.x < 2 || available.y < 2) {
        ImGui::End();
        return;
    }
    auto resolution = ImGuiRenderer::viewport_resolution(available.x, available.y);
    ImVec2 size{resolution.display_width, resolution.display_height};
    auto cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos({cursor.x + resolution.offset_x, cursor.y + resolution.offset_y});
    float view[16], projection[16];
    auto frame = ride_.route.frame(ride_.distance);
    auto eye = camera_.eye(), target = camera_.target;
    if (!resource_view_ && !free_camera_) {
        auto base =
            ride_.route.position(ride_.distance, ride_.x * .3f, ride_.y * .3f, ride_.distance);
        for (unsigned i = 0; i < 3; ++i) {
            eye[i] = base[i] + frame.back[i] * 1250 + frame.up[i] * 430;
            target[i] = base[i] - frame.back[i] * 1000 + frame.up[i] * 100;
        }
    }
    if (!resource_view_ && free_camera_) {
        auto previous = ride_.route.frame(free_origin_),
             current = ride_.route.frame(ride_.distance);
        for (unsigned i = 0; i < 3; ++i)
            free_view_.target[i] += float(previous.center[i] - current.center[i]);
        free_origin_ = ride_.distance;
        eye = free_view_.eye();
        target = free_view_.target;
    }
    auto up = resource_view_ || free_camera_ ? std::array<float, 3>{0, 1, 0} : frame.up;
    bx::mtxLookAt(view, {eye[0], eye[1], eye[2]}, {target[0], target[1], target[2]},
                  {up[0], up[1], up[2]}, bx::Handedness::Right);
    bx::mtxProj(projection, resource_view_ ? 55.f : 35.f,
                float(resolution.width) / float(resolution.height),
                resource_view_ ? camera_.near_clip() : 1.f,
                resource_view_ ? camera_.far_clip() : 30000.f, bgfx::getCaps()->homogeneousDepth,
                bx::Handedness::Right);
    renderer_.playback.seconds =
        resource_view_ ? animation_seconds_ : double(ride_.ticks) * WarpRide::tick_seconds;
    auto texture = renderer_.render(resolution.width, resolution.height, view, projection, true,
                                    false, 0, false);
    auto origin = ImGui::GetCursorScreenPos();
    bool flip = bgfx::getCaps()->originBottomLeft;
    ImGui::Image(ImTextureID(ImGuiRenderer::image_id(texture, false, ImGuiRenderer::preview_3ds)),
                 size, {0, flip ? 1.f : 0.f}, {1, flip ? 0.f : 1.f});
    bool hovered = ImGui::IsItemHovered();
    if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                    ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
        ImGui::SetWindowFocus();
    focused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
               (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) &&
               !ImGui::GetIO().WantTextInput;
    auto &io = ImGui::GetIO();
    if (resource_view_)
        viewport_navigation(camera_, window_, hovered);
    else if (free_camera_ && focused_)
        viewport_navigation(free_view_, window_, hovered);
    if (!resource_view_) {
        auto *draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        auto project = [&](const std::array<float, 3> &position, ImVec2 &screen) {
            float camera[4]{}, clip[4]{};
            for (unsigned i = 0; i < 4; ++i)
                camera[i] = view[i] * position[0] + view[i + 4] * position[1] +
                            view[i + 8] * position[2] + view[i + 12];
            for (unsigned i = 0; i < 4; ++i)
                for (unsigned j = 0; j < 4; ++j)
                    clip[i] += projection[j * 4 + i] * camera[j];
            if (clip[3] <= 1)
                return false;
            screen = {origin.x + (clip[0] / clip[3] * .5f + .5f) * size.x,
                      origin.y + (.5f - clip[1] / clip[3] * .5f) * size.y};
            return true;
        };
        ImVec2 player;
        if (!mount_ &&
            project(ride_.route.position(ride_.distance, ride_.x, ride_.y, ride_.distance), player))
            draw->AddTriangleFilled({player.x, player.y - 12}, {player.x - 9, player.y + 8},
                                    {player.x + 9, player.y + 8}, IM_COL32_WHITE);
        auto circle = [&](double at, float x, float y, float radius, ImU32 color) {
            ImVec2 previous{};
            bool valid = false;
            for (unsigned i = 0; i <= 48; ++i) {
                float angle = float(i) * 6.2831853f / 48;
                ImVec2 point{};
                bool visible =
                    project(ride_.route.position(at, x + std::cos(angle) * radius,
                                                 y + std::sin(angle) * radius, ride_.distance),
                            point);
                if (valid && visible)
                    draw->AddLine(previous, point, color, 1.5f);
                previous = point;
                valid = visible;
            }
        };
        if (guides_) {
            circle(ride_.distance, 0, 0, WarpRide::rail_radius, IM_COL32(130, 190, 255, 150));
            for (const auto &o : ride_.objects) {
                if (o.consumed || o.distance < ride_.distance)
                    continue;
                auto radius = o.kind == RideObjectKind::Energy     ? ride_.tuning.pickup_radius
                              : o.kind == RideObjectKind::Obstacle ? ride_.tuning.obstacle_radius
                                                                   : ride_.tuning.wormhole_radius;
                auto color = o.kind == RideObjectKind::Energy     ? IM_COL32(255, 220, 40, 230)
                             : o.kind == RideObjectKind::Obstacle ? IM_COL32(255, 80, 80, 230)
                                                                  : IM_COL32(130, 230, 255, 230);
                circle(o.distance, o.x, o.y, radius, color);
            }
        }
        draw->AddText({origin.x + 10, origin.y + 10}, IM_COL32_WHITE,
                      ride_.result.entered ? "Wormhole entered"
                      : running_ ? (free_camera_ ? "Free camera" : "Steer with WASD / arrows")
                                 : "Paused");
        draw->PopClipRect();
    }
    ImGui::End();
}
void WarpRideWorkspace::draw(const std::filesystem::path &dump) {
    if (dump != requested_)
        load(dump);
    if (loading_.valid() &&
        loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto assets = loading_.get();
            if (source_ == requested_)
                install(std::move(assets));
        } catch (const std::exception &e) {
            if (source_ == requested_)
                error_ = e.what();
        }
        if (source_ != requested_)
            load(requested_);
    }
    ImGui::Begin("Warp Ride assets");
    if (ImGui::RadioButton("Simulator", !resource_view_)) {
        resource_view_ = false;
        running_ = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Assets", resource_view_)) {
        resource_view_ = true;
        running_ = false;
    }
    ImGui::BeginDisabled(loading_.valid());
    if (ImGui::Button("Reload resources"))
        load(dump);
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::SeparatorText("Course assets");
    for (int i = 0; i < int(WarpRideProfile::assets.size()); ++i)
        if (ImGui::Selectable(WarpRideProfile::assets[i].name, resource_view_ && selected_ == i)) {
            selected_ = i;
            resource_view_ = true;
            running_ = false;
            animation_seconds_ = 0;
            if (assets_.scene)
                camera_.fit(assets_.geometry[i].low, assets_.geometry[i].high);
        }
    ImGui::End();
    controls();
    if (gamepad_ && !SDL_GamepadConnected(gamepad_)) {
        SDL_CloseGamepad(gamepad_);
        gamepad_ = nullptr;
    }
    if (gamepad_initialized_ && !gamepad_) {
        int count = 0;
        auto ids = SDL_GetGamepads(&count);
        if (count > 0)
            gamepad_ = SDL_OpenGamepad(ids[0]);
        SDL_free(ids);
    }
    float horizontal = 0, vertical = 0;
    if (focused_ && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        horizontal = float(ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) -
                     float(ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow));
        vertical = float(ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) -
                   float(ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow));
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && !resource_view_ && !ride_.result.entered)
            running_ = !running_;
        if (gamepad_) {
            auto axis = [&](SDL_GamepadAxis a) {
                float value = float(SDL_GetGamepadAxis(gamepad_, a)) / 32767.f;
                return std::abs(value) < .15f
                           ? 0.f
                           : std::copysign((std::abs(value) - .15f) / .85f, value);
            };
            horizontal += axis(SDL_GAMEPAD_AXIS_LEFTX);
            vertical -= axis(SDL_GAMEPAD_AXIS_LEFTY);
        }
    }
    if (free_camera_)
        horizontal = vertical = 0;
    if (active_ && running_ && !resource_view_ && assets_.scene && renderer_.ready())
        ride_.advance(ImGui::GetIO().DeltaTime * playback_speed_, horizontal, vertical);
    if (ride_.result.entered)
        running_ = false;
    if (resource_view_ && animate_)
        animation_seconds_ += std::min(ImGui::GetIO().DeltaTime, .1f);
    viewport();
    ImGui::Begin("Warp Ride controls");
    ImGui::SeparatorText("Run summary");
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    ImGui::Text("Frame %llu | Pickups %u | Obstacle hits %u",
                static_cast<unsigned long long>(ride_.ticks), ride_.pickups, ride_.hits);
    if (ride_.result.entered)
        ImGui::Text("%s wormhole | Rarity %u | %.0f m", WarpRide::type_name(ride_.result.type),
                    ride_.result.rarity, ride_.result.distance / 100);
    if (ImGui::CollapsingHeader("Simulator limitations"))
        ImGui::TextWrapped("Preview-only: curved routes, steering and collision are approximate. "
                           "Particle effects, "
                           "encounter prediction and game export are not included yet.");
    if (assets_.scene && !assets_.scene->diagnostics.empty() &&
        ImGui::TreeNode("Asset diagnostics")) {
        for (const auto &note : assets_.scene->diagnostics)
            ImGui::TextWrapped("%s", note.c_str());
        ImGui::TreePop();
    }
    ImGui::End();
}
}
