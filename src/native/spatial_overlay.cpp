#include "native/tutorial_widgets.h"
#include "native/spatial_overlay.h"
#include "field/collision_surfaces.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
namespace studio {
namespace {
std::array<float, 4> tint(SpatialKind kind) {
    static const std::array<float, 4> colors[] = {
        {.3f, 1, .45f, 1},    {1, .35f, .2f, 1},    {.15f, .65f, 1, 1}, {1, .35f, .85f, 1},
        {.85f, .6f, .25f, 1}, {.95f, .85f, .2f, 1}, {.2f, .85f, 1, 1},  {.8f, .45f, 1, 1},
        {.85f, 1, .8f, 1},    {.25f, .9f, 1, 1},    {1, .4f, .85f, 1},  {1, .65f, .2f, 1},
        {.6f, .5f, 1, 1},     {1, .85f, .25f, 1},   {.15f, 1, .65f, 1}};
    static_assert(std::size(colors) == unsigned(SpatialKind::Count));
    return colors[unsigned(kind)];
}
std::string lower(std::string s) {
    for (auto &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
const char *camera_type(unsigned type) {
    static const char *names[] = {"Follow", "Blended follow", "Hold", "Path"};
    return type < 4 ? names[type] : "Unknown";
}
std::pair<SpatialPoint, SpatialPoint> bounds(const SpatialRegion &r) {
    SpatialPoint low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
    for (auto &v : r.vertices)
        for (unsigned i = 0; i < 3; ++i) {
            low[i] = std::min(low[i], v.position[i]);
            high[i] = std::max(high[i], v.position[i]);
        }
    return {low, high};
}
SpatialPoint add(SpatialPoint a, SpatialPoint b) {
    for (unsigned i = 0; i < 3; ++i)
        a[i] += b[i];
    return a;
}
SpatialPoint scale(SpatialPoint a, float b) {
    for (auto &v : a)
        v *= b;
    return a;
}
SpatialPoint cross(SpatialPoint a, SpatialPoint b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
SpatialPoint normalize(SpatialPoint a) {
    float length = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    return length > 1e-6f ? scale(a, 1 / length) : SpatialPoint{0, 0, 1};
}
SpatialPoint rotate(SpatialPoint v, SpatialPoint degrees) {
    for (unsigned axis = 0; axis < 3; ++axis) {
        auto a = (axis + 1) % 3, b = (axis + 2) % 3;
        float r = degrees[axis] * .01745329252f, c = std::cos(r), s = std::sin(r), x = v[a],
              y = v[b];
        v[a] = c * x - s * y;
        v[b] = s * x + c * y;
    }
    return v;
}
}
SpatialOverlay::SpatialOverlay(const std::filesystem::path &shaders) {
    auto shader = [&](const char *name) {
        auto bytes = read_file(shaders / name);
        auto h = bgfx::createShader(bgfx::copy(bytes.data(), narrow(bytes.size())));
        require(bgfx::isValid(h), "Cannot create spatial overlay shader");
        return h;
    };
    program_ = bgfx::createProgram(shader("vs_spatial.bin"), shader("fs_spatial.bin"), true);
    require(bgfx::isValid(program_), "Cannot link spatial overlay shader");
    color_ = bgfx::createUniform("u_overlayColor", bgfx::UniformType::Vec4);
    params_ = bgfx::createUniform("u_overlayParams", bgfx::UniformType::Vec4);
    layout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 1, bgfx::AttribType::Float)
        .end();
}
SpatialOverlay::~SpatialOverlay() {
    clear();
    bgfx::destroy(program_);
    bgfx::destroy(color_);
    bgfx::destroy(params_);
}
void SpatialOverlay::clear() {
    for (auto &b : buffers_) {
        if (bgfx::isValid(b.vertices))
            bgfx::destroy(b.vertices);
        if (bgfx::isValid(b.triangles))
            bgfx::destroy(b.triangles);
        if (bgfx::isValid(b.lines))
            bgfx::destroy(b.lines);
    }
    buffers_.clear();
}
void SpatialOverlay::set_scene(std::shared_ptr<const Environment> scene) {
    clear();
    scene_ = std::move(scene);
    selected = -1;
    zone_ = -1;
    anchor_selection_ = -2;
    guide_vertices_.clear();
    interaction_guides_.clear();
}
void SpatialOverlay::upload() {
    if (!scene_ || !buffers_.empty())
        return;
    for (auto &region : scene_->spatial.regions) {
        Buffers b;
        if (!region.vertices.empty())
            b.vertices = bgfx::createVertexBuffer(
                bgfx::copy(region.vertices.data(),
                           narrow(region.vertices.size() * sizeof(SpatialVertex))),
                layout_);
        if (!region.triangles.empty())
            b.triangles = bgfx::createIndexBuffer(
                bgfx::copy(region.triangles.data(), narrow(region.triangles.size() * 4)),
                BGFX_BUFFER_INDEX32);
        if (!region.lines.empty())
            b.lines = bgfx::createIndexBuffer(
                bgfx::copy(region.lines.data(), narrow(region.lines.size() * 4)),
                BGFX_BUFFER_INDEX32);
        buffers_.push_back(b);
    }
}
void SpatialOverlay::render(bgfx::ViewId view, bool picking, unsigned first_id) {
    if (!scene_ || (picking && !pick_overlays) ||
        std::none_of(enabled.begin(), enabled.end(), [](bool v) {
            return v;
        }))
        return;
    upload();
    auto state =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | (xray ? 0 : BGFX_STATE_DEPTH_TEST_LEQUAL);
    float params[4] = {picking ? 1.f : 0.f, 0, 0, 0};
    for (unsigned i = 0; i < buffers_.size(); ++i) {
        auto &region = scene_->spatial.regions[i];
        if (!enabled[unsigned(region.kind)] || (picking && locked[unsigned(region.kind)]) ||
            (selected_only && int(i) != selected))
            continue;
        auto &b = buffers_[i];
        if (!bgfx::isValid(b.vertices))
            continue;
        auto color = attribute_colors && unsigned(region.kind) < 5
                         ? collision_surface_color(region.attribute)
                         : tint(region.kind);
        if (int(i) == selected)
            color = {1, .85f, .1f, 1};
        if (picking) {
            auto id = first_id + i;
            color = {float(id & 255) / 255, float((id >> 8) & 255) / 255,
                     float((id >> 16) & 255) / 255, 1};
        }
        if ((filled || picking) && bgfx::isValid(b.triangles)) {
            color[3] = picking ? 1.f : opacity;
            bgfx::setUniform(color_, color.data());
            bgfx::setUniform(params_, params);
            bgfx::setVertexBuffer(0, b.vertices);
            bgfx::setIndexBuffer(b.triangles);
            bgfx::setState(state | (picking ? BGFX_STATE_WRITE_Z : BGFX_STATE_BLEND_ALPHA));
            bgfx::submit(view, program_);
        }
        if (!picking && edges && bgfx::isValid(b.lines)) {
            color[3] = int(i) == selected ? 1.f : .8f;
            bgfx::setUniform(color_, color.data());
            bgfx::setUniform(params_, params);
            bgfx::setVertexBuffer(0, b.vertices);
            bgfx::setIndexBuffer(b.lines);
            bgfx::setState(state | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_PT_LINES);
            bgfx::submit(view, program_);
        }
    }
    if (!picking && !interaction_guides_.empty() && selected >= 0 &&
        std::size_t(selected) < scene_->spatial.regions.size() &&
        enabled[unsigned(scene_->spatial.regions[selected].kind)]) {
        auto count = narrow(interaction_guides_.size());
        if (bgfx::getAvailTransientVertexBuffer(count, layout_) >= count) {
            bgfx::TransientVertexBuffer buffer;
            bgfx::allocTransientVertexBuffer(&buffer, count, layout_);
            std::memcpy(buffer.data, interaction_guides_.data(), count * sizeof(SpatialVertex));
            float color[4] = {.3f, 1, 1, 1};
            bgfx::setUniform(color_, color);
            bgfx::setUniform(params_, params);
            bgfx::setVertexBuffer(0, &buffer);
            bgfx::setState(state | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_PT_LINES);
            bgfx::submit(view, program_);
        }
    }
    if (!picking && guides_ && enabled[unsigned(SpatialKind::Camera)] && !guide_vertices_.empty()) {
        auto count = narrow(guide_vertices_.size());
        if (bgfx::getAvailTransientVertexBuffer(count, layout_) >= count) {
            bgfx::TransientVertexBuffer buffer;
            bgfx::allocTransientVertexBuffer(&buffer, count, layout_);
            std::memcpy(buffer.data, guide_vertices_.data(), count * sizeof(SpatialVertex));
            float color[4] = {1, .85f, .15f, 1};
            bgfx::setUniform(color_, color);
            bgfx::setUniform(params_, params);
            bgfx::setVertexBuffer(0, &buffer);
            bgfx::setState(state | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_PT_LINES);
            bgfx::submit(view, program_);
        }
    }
}
void SpatialOverlay::camera_guides(const CameraSetting &setting, const CameraSetting *defaults) {
    guide_vertices_.clear();
    auto line = [&](SpatialPoint a, SpatialPoint b) {
        guide_vertices_.push_back({a, 1});
        guide_vertices_.push_back({b, 1});
    };
    auto frustum = [&](SpatialPoint eye, SpatialPoint target, SpatialPoint right, SpatialPoint up,
                       float fov) {
        if (fov <= 0 || fov >= 179)
            return;
        auto forward = normalize(add(target, scale(eye, -1)));
        float height = std::tan(fov * .00872664626f) * guide_length_;
        auto center = add(eye, scale(forward, guide_length_));
        std::array<SpatialPoint, 4> corners;
        for (unsigned i = 0; i < 4; ++i)
            corners[i] = add(center, add(scale(right, (i & 1 ? 1.f : -1.f) * height * 400 / 240),
                                         scale(up, (i & 2 ? 1.f : -1.f) * height)));
        for (auto c : corners)
            line(eye, c);
        for (auto pair : std::array<std::array<unsigned, 2>, 4>{{{0, 1}, {1, 3}, {3, 2}, {2, 0}}})
            line(corners[pair[0]], corners[pair[1]]);
        line(eye, target);
    };
    if (setting.type < 2) {
        for (unsigned i = 0; i < (setting.type == 1 ? 2u : 1u); ++i) {
            auto p = i ? setting.b : setting.a;
            if (p.zone_default) {
                if (!defaults)
                    return;
                p = defaults->a;
            }
            if (p.distance <= 0)
                continue;
            auto target = add(anchor_, p.offset);
            auto eye = add(target, rotate({0, 0, p.distance}, p.rotation));
            auto forward = normalize(add(target, scale(eye, -1))),
                 right = normalize(cross(forward, {0, 1, 0})),
                 up = normalize(cross(right, forward));
            frustum(eye, target, right, up, p.fov);
        }
    } else if (setting.type == 2) {
        auto target = setting.player_target ? add(anchor_, setting.hold_offset) : setting.target;
        auto forward = normalize(add(target, scale(setting.position, -1))),
             right = normalize(cross(forward, {0, 1, 0})), up = normalize(cross(right, forward));
        float angle = setting.bank * .01745329252f;
        auto rolled_right = add(scale(right, std::cos(angle)), scale(up, std::sin(angle)));
        auto rolled_up = add(scale(up, std::cos(angle)), scale(right, -std::sin(angle)));
        frustum(setting.position, target, rolled_right, rolled_up, setting.hold_fov);
    }
}
bool SpatialOverlay::controls(ViewportCamera &camera, int loaded_zone) {
    if (focus)
        ImGui::SetNextWindowFocus();
    ImGui::Begin("Spatial");
    bool changed = false;
    ImGui::TextUnformatted("Visible map layers");
    if (studio::TutorialWidgets::Button("spatial_overlay", "Show interactions")) {
        enabled.fill(false);
        for (unsigned i = unsigned(SpatialKind::Entrance); i < enabled.size(); ++i)
            enabled[i] = true;
        pick_overlays = true;
        changed = true;
    }
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("spatial_overlay", "Hide all")) {
        enabled.fill(false);
        changed = true;
    }
    ImGui::TextDisabled("Ctrl+click an unlocked region to inspect it.");
    if (studio::TutorialWidgets::CollapsingHeader("spatial_overlay", "Choose layers") &&
        ImGui::BeginTable("Layer visibility", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Selection", ImGuiTableColumnFlags_WidthFixed, 60);
        for (unsigned i = 0; i < enabled.size(); ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            auto color = tint(SpatialKind(i));
            ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(color[0], color[1], color[2], 1));
            changed |= studio::TutorialWidgets::Checkbox(
                "spatial_overlay", spatial_kind_name(SpatialKind(i)), &enabled[i]);
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::PushID(int(i));
            changed |= studio::TutorialWidgets::Checkbox("spatial_overlay", "Lock", &locked[i]);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Keep this layer visible without selecting it in the viewport.");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (studio::TutorialWidgets::CollapsingHeader("spatial_overlay", "Display & selection")) {
        changed |=
            studio::TutorialWidgets::Checkbox("spatial_overlay", "See through geometry", &xray);
        changed |= studio::TutorialWidgets::Checkbox("spatial_overlay", "Filled regions", &filled);
        changed |= studio::TutorialWidgets::Checkbox(
            "spatial_overlay", "Collision attribute colors", &attribute_colors);
        if (filled)
            changed |= ImGui::SliderFloat("Opacity", &opacity, .03f, .6f, "%.2f");
        changed |= studio::TutorialWidgets::Checkbox(
            "spatial_overlay", "Include overlays in Ctrl+click selection", &pick_overlays);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Select visible, unlocked regions or the model under the cursor.");
        studio::TutorialWidgets::Checkbox("spatial_overlay", "Selected region only",
                                          &selected_only);
    }
    if (!scene_) {
        ImGui::TextWrapped(
            "Load a map to inspect entrances, actors, triggers, cameras and collision.");
        ImGui::End();
        return changed;
    }
    auto &data = scene_->spatial;
    if (zone_ < 0 || std::none_of(
                         data.zones.begin(), data.zones.end(),
                         [&](auto &z) {
                             return z.zone == zone_;
                         }))
        zone_ = loaded_zone >= 0 ? loaded_zone : data.zones.empty() ? -1 : data.zones.front().zone;
    const ZoneCamera *zone = nullptr;
    for (auto &z : data.zones)
        if (z.zone == zone_)
            zone = &z;
    bool camera_layer =
        enabled[unsigned(SpatialKind::Camera)] || enabled[unsigned(SpatialKind::ScrollStop)];
    if (camera_layer &&
        ImGui::BeginCombo("Camera zone", zone ? std::to_string(zone_).c_str() : "Unavailable")) {
        for (auto &z : data.zones) {
            std::string label = "Zone " + std::to_string(z.zone);
            for (auto &location : scene_->locations)
                if (location.zone == z.zone) {
                    label += " / " + location.name;
                    break;
                }
            if (ImGui::Selectable(label.c_str(), z.zone == zone_)) {
                zone_ = z.zone;
                zone = &z;
                anchor_selection_ = -2;
            }
        }
        ImGui::EndCombo();
    }
    const CameraSetting *defaults =
        zone && zone->camera < data.defaults.size() ? &data.defaults[zone->camera] : nullptr;
    if (camera_layer && zone)
        ImGui::TextDisabled("Default camera %u | support %u", zone->camera, zone->support);
    if (focus) {
        search_[0] = 0;
        kind_filter_ = 0;
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##spatial-search", "Search regions or attribute IDs", search_,
                             sizeof(search_));
    if (ImGui::BeginCombo("List", kind_filter_ ? spatial_kind_name(SpatialKind(kind_filter_ - 1))
                                               : "All types")) {
        if (ImGui::Selectable("All types", !kind_filter_))
            kind_filter_ = 0;
        for (unsigned i = 0; i < enabled.size(); ++i)
            if (ImGui::Selectable(spatial_kind_name(SpatialKind(i)), kind_filter_ == int(i) + 1))
                kind_filter_ = int(i) + 1;
        ImGui::EndCombo();
    }
    auto query = lower(search_);
    std::vector<int> matches;
    for (unsigned i = 0; i < data.regions.size(); ++i) {
        auto &r = data.regions[i];
        if ((r.kind != SpatialKind::Encounter || !r.vertices.empty()) &&
            enabled[unsigned(r.kind)] &&
            (!kind_filter_ || unsigned(r.kind) == unsigned(kind_filter_ - 1)) &&
            (query.empty() || lower(r.name).find(query) != std::string::npos))
            matches.push_back(int(i));
    }
    std::stable_sort(matches.begin(), matches.end(), [&](int a, int b) {
        auto &x = data.regions[a];
        auto &y = data.regions[b];
        if (x.kind != y.kind)
            return x.kind < y.kind;
        if (x.attribute != y.attribute)
            return x.attribute < y.attribute;
        return x.name < y.name;
    });
    ImGui::TextDisabled("%zu enabled / %zu regions", matches.size(), data.regions.size());
    ImGui::BeginChild("Regions", ImVec2(0, 180), ImGuiChildFlags_Borders);
    ImGuiListClipper clipper;
    clipper.Begin(int(matches.size()));
    if (focus) {
        auto it = std::find(matches.begin(), matches.end(), selected);
        if (it != matches.end())
            clipper.IncludeItemByIndex(int(it - matches.begin()));
    }
    while (clipper.Step())
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            int i = matches[row];
            auto &r = data.regions[i];
            ImGui::PushID(i);
            if (ImGui::Selectable(r.name.c_str(), selected == i)) {
                selected = i;
                if (r.kind == SpatialKind::Zone && r.zone >= 0)
                    zone_ = r.zone;
                enabled[unsigned(r.kind)] = true;
                changed = true;
            }
            if (focus && selected == i)
                ImGui::SetScrollHereY();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", spatial_kind_name(r.kind));
            ImGui::PopID();
        }
    ImGui::EndChild();
    if (matches.empty())
        ImGui::TextWrapped("Enable a layer above, or clear the list filters.");
    focus = false;
    const SpatialRegion *region = selected >= 0 && std::size_t(selected) < data.regions.size()
                                      ? &data.regions[selected]
                                      : nullptr;
    const CameraSetting *setting = defaults;
    if (region) {
        ImGui::TextWrapped("%s", region->name.c_str());
        ImGui::TextDisabled("%s | %zu triangles", spatial_kind_name(region->kind),
                            region->triangles.size() / 3);
        if (!region->detail.empty())
            ImGui::TextWrapped("%s", region->detail.c_str());
        if (region->zone >= 0)
            ImGui::Text(region->kind == SpatialKind::Zone || region->overworld
                            ? "Zone %d"
                            : "Placement zone slot %d",
                        region->zone);
        if (studio::TutorialWidgets::Button("spatial_overlay", "Frame region") &&
            !region->vertices.empty()) {
            auto [low, high] = bounds(*region);
            camera.fit(low, high);
            camera.bounds_low = scene_->low;
            camera.bounds_high = scene_->high;
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("spatial_overlay", "Clear region")) {
            selected = -1;
            region = nullptr;
        }
        if (region && region->kind == SpatialKind::Camera) {
            if (region->attribute == 65535)
                setting = defaults;
            else if (region->attribute < data.cameras.size())
                setting = &data.cameras[region->attribute];
            else {
                setting = nullptr;
                ImGui::TextWrapped("This region has no resolved camera setting.");
            }
        }
    }
    interaction_guides_.clear();
    if (region && region->overworld) {
        auto &r = *region->overworld;
        ImGui::SeparatorText("Interaction details");
        ImGui::TextDisabled("Read-only trigger inspection.");
        if (studio::TutorialWidgets::TreeNode("spatial_overlay", "Source record")) {
            ImGui::Text("Category %u / zone slot %u / row %u", r.category, r.local_zone, r.row);
            ImGui::TreePop();
        }
        ImGui::Text("Position: %.1f, %.1f, %.1f", r.position[0], r.position[1], r.position[2]);
        if (r.script)
            ImGui::Text("Script reference: %u (zone context required)", r.script);
        if (r.version || r.condition)
            ImGui::TextWrapped("Conditional: version %u, flag/work %u, expected work value %u. "
                               "Save state is not evaluated.",
                               r.version, r.condition, r.value);
        else
            ImGui::TextWrapped("Placement conditions: none. Scripts may add checks.");
        auto line = [&](SpatialPoint a, SpatialPoint b) {
            interaction_guides_.push_back({a, 0});
            interaction_guides_.push_back({b, 0});
        };
        auto origin = r.destination_zone >= 0 ? r.arrival : r.position;
        origin[1] += 8;
        auto [x, y, z, w] = r.rotation;
        SpatialPoint direction{2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)};
        auto tip = add(origin, scale(direction, 90));
        line(origin, tip);
        auto side = normalize(cross(direction, {0, 1, 0}));
        line(tip, add(add(tip, scale(direction, -20)), scale(side, 12)));
        line(tip, add(add(tip, scale(direction, -20)), scale(side, -12)));
        if (r.destination_zone >= 0) {
            line(r.position, origin);
            ImGui::Text("Arrival here: %.1f, %.1f, %.1f", r.arrival[0], r.arrival[1], r.arrival[2]);
            ImGui::Text("Destination: zone %d / entrance %u", r.destination_zone,
                        r.destination_event);
            if (studio::TutorialWidgets::Button("spatial_overlay",
                                                "Inspect destination entrance")) {
                destination_zone = r.destination_zone;
                destination_event = r.destination_event;
            }
            if (studio::TutorialWidgets::Button("spatial_overlay", "Frame arrival")) {
                auto low = r.arrival, high = r.arrival;
                for (unsigned k = 0; k < 3; ++k) {
                    low[k] -= 80;
                    high[k] += 80;
                }
                camera.fit(low, high);
            }
            for (auto &other : data.regions)
                if (other.kind == SpatialKind::Entrance && other.overworld &&
                    other.zone == r.destination_zone &&
                    other.overworld->event == r.destination_event)
                    line(r.position, other.overworld->position);
        }
    }
    if (anchor_selection_ != selected) {
        anchor_selection_ = selected;
        if (region && !region->vertices.empty()) {
            auto [low, high] = bounds(*region);
            for (unsigned i = 0; i < 3; ++i)
                anchor_[i] = (low[i] + high[i]) * .5f;
        } else if (auto start = scene_->start_position(zone_))
            anchor_ = *start;
    }
    guide_vertices_.clear();
    if (camera_layer && (!region || !region->overworld) &&
        studio::TutorialWidgets::CollapsingHeader("spatial_overlay", "Camera settings",
                                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        if (setting) {
            ImGui::Text("%s | priority %d", camera_type(setting->type), setting->priority);
            ImGui::Text("Transition: %u frames", setting->transition);
            if (setting->work)
                ImGui::Text("Requires work %d = %u", setting->work, setting->value);
            if (setting->type == 3)
                ImGui::TextWrapped(
                    "Path settings are identified; path playback is not implemented.");
            auto display_point = [&](const char *label, CameraPoint p) {
                ImGui::TextUnformatted(label);
                if (p.zone_default) {
                    ImGui::TextDisabled("Uses zone default");
                    if (defaults)
                        p = defaults->a;
                    else
                        return;
                }
                ImGui::Text("Offset %.1f, %.1f, %.1f", p.offset[0], p.offset[1], p.offset[2]);
                ImGui::Text("Rotation %.1f, %.1f, %.1f deg", p.rotation[0], p.rotation[1],
                            p.rotation[2]);
                ImGui::Text("FOV %.1f deg | distance %.1f", p.fov, p.distance);
            };
            if (setting->type < 2) {
                display_point("Pose A", setting->a);
                if (setting->type == 1) {
                    display_point("Pose B", setting->b);
                    ImGui::Text("Blend easing ID %u", setting->easing);
                }
                auto support = setting->support;
                if (zone && zone->support < data.support_defaults.size()) {
                    auto d = data.support_defaults[zone->support].support;
                    if (support.zone_default)
                        support = d;
                    else if (support.parameter_default) {
                        support.maximum = d.maximum;
                        support.entering = d.entering;
                        support.leaving = d.leaving;
                    }
                }
                static const char *types[] = {"None", "Direction", "Distance",
                                              "Direction + distance"};
                ImGui::Text("Support: %s", support.type < 4 ? types[support.type] : "Unknown");
                ImGui::Text("Maximum %.2f", support.maximum);
                ImGui::Text("Enter: wait %u, move %u frames", support.entering[0],
                            support.entering[1]);
                ImGui::Text("Leave: wait %u, move %u frames", support.leaving[0],
                            support.leaving[1]);
            } else if (setting->type == 2) {
                ImGui::Text("Position %.1f, %.1f, %.1f", setting->position[0], setting->position[1],
                            setting->position[2]);
                ImGui::Text("FOV %.1f | bank %.1f deg", setting->hold_fov, setting->bank);
                ImGui::TextUnformatted(setting->player_target ? "Targets the player plus offset"
                                                              : "Uses a fixed target");
            }
            studio::TutorialWidgets::Checkbox("spatial_overlay", "Camera pose guides", &guides_);
            ImGui::DragFloat3("Target anchor", anchor_.data(), 1);
            if (studio::TutorialWidgets::Button("spatial_overlay", "Use view target"))
                anchor_ = camera.target;
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("spatial_overlay", "Use zone start")) {
                if (auto start = scene_->start_position(zone_))
                    anchor_ = *start;
            }
            ImGui::SliderFloat("Guide length", &guide_length_, 50, 1500, "%.0f");
            if (guides_)
                camera_guides(*setting, defaults);
            ImGui::TextWrapped(
                "Guides show authored endpoint poses at this anchor with the game's 400:240 "
                "aspect. They do not simulate support motion, story conditions or transitions.");
        } else
            ImGui::TextWrapped("No camera defaults are available for this zone.");
        if (!data.replacements.empty() &&
            studio::TutorialWidgets::TreeNode("spatial_overlay",
                                              "Conditional camera replacements")) {
            for (auto rule : data.replacements)
                ImGui::Text("Previous %u + entered %u -> %u", rule[0], rule[1], rule[2]);
            ImGui::TreePop();
        }
    }
    ImGui::End();
    return changed;
}
}
