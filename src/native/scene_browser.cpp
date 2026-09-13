#include "native/tutorial_widgets.h"
#include "native/scene_browser.h"
#include <imgui.h>
#include <cctype>
#include <limits>
#include <map>
namespace studio {
namespace {
std::string lowercase(std::string text) {
    for (auto &c : text)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
bool needs_attention(const Environment &scene, int index) {
    auto &m = scene.materials[scene.draws[index].material];
    if (m.unsupported_mapping || !m.combiner.unsupported.empty())
        return true;
    for (auto &texture : m.texture_inputs)
        if (!texture.empty() && !scene.textures.contains(texture))
            return true;
    return false;
}
void frame_draws(const Environment &scene, const std::vector<int> &draws,
                 const EnvironmentRenderer &renderer, ViewportCamera &camera) {
    auto low = ViewportCamera::Position{INFINITY, INFINITY, INFINITY},
         high = ViewportCamera::Position{-INFINITY, -INFINITY, -INFINITY};
    auto poses = evaluate_scene_poses(scene.skeletons, renderer.player, renderer.playback.seconds,
                                      renderer.lighting.hour,
                                      renderer.playback.enabled && renderer.playback.skeletal);
    bool found = false;
    for (auto index : draws) {
        auto &draw = scene.draws[index];
        if (draw.sky_part >= 0 || draw.weather_mask ||
            (draw.player >= 0 &&
             (!renderer.player.active || draw.player != int(renderer.player.appearance))))
            continue;

        for (auto &v : draw.vertices) {
            std::array<float, 3> point{v.x, v.y, v.z};
            if (draw.skeleton >= 0) {
                point = {};
                for (unsigned j = 0; j < 4; ++j)
                    if (v.weights[j] > 0) {
                        auto &m =
                            poses.at(draw.skeleton).at(draw.palette.at(std::size_t(v.joints[j])));
                        for (unsigned c = 0; c < 3; ++c)
                            point[c] += v.weights[j] * (m[c * 4] * v.x + m[c * 4 + 1] * v.y +
                                                        m[c * 4 + 2] * v.z + m[c * 4 + 3]);
                    }
            }
            if (draw.placement >= 0) {
                auto original = point;
                auto &m = scene.placement_transforms.at(draw.placement);
                for (unsigned k = 0; k < 3; ++k)
                    point[k] = m[k * 4] * original[0] + m[k * 4 + 1] * original[1] +
                               m[k * 4 + 2] * original[2] + m[k * 4 + 3];
            }
            found = true;
            for (unsigned c = 0; c < 3; ++c) {
                low[c] = std::min(low[c], point[c]);
                high[c] = std::max(high[c], point[c]);
            }
        }
    }
    if (found) {
        camera.fit(low, high);
        camera.bounds_low = scene.low;
        camera.bounds_high = scene.high;
    }
}
}
void SceneBrowser::rebuild(const Environment &scene) {
    objects_.clear();
    searches_.clear();
    std::map<std::pair<int, std::string>, std::size_t> groups;
    for (unsigned i = 0; i < scene.draws.size(); ++i) {
        auto &draw = scene.draws[i];
        int category = draw.player >= 0                    ? 5
                       : draw.sky_part >= 0                ? 3
                       : draw.weather_mask                 ? 4
                       : draw.character                    ? 2
                       : draw.scope.starts_with("static/") ? 1
                                                           : 0;
        auto name = draw.name.substr(0, draw.name.find(" / "));
        auto key = std::make_pair(category, draw.placement >= 0
                                                ? "placement/" + std::to_string(draw.placement)
                                                : draw.scope + name);
        auto [it, added] = groups.emplace(key, objects_.size());
        if (added)
            objects_.push_back({name, {}, category, {}});
        auto &object = objects_[it->second];
        object.draws.push_back(int(i));
        auto &mat = scene.materials[draw.material];
        auto search = draw.name + ' ' + mat.name;
        for (auto &texture : mat.texture_inputs)
            search += ' ' + texture;
        searches_.push_back(lowercase(search));
        object.search += ' ' + search;
    }
    for (auto &object : objects_)
        object.search = lowercase(object.search);
}
void SceneBrowser::draw(const Environment *scene, const EnvironmentRenderer &renderer,
                        MaterialSelection &selection, ViewportCamera &camera) {
    if (selection.reveal_scene)
        ImGui::SetNextWindowFocus();
    ImGui::Begin("Scene");
    if (!scene) {
        ImGui::TextWrapped("Load a map to browse its objects and materials.");
        ImGui::End();
        return;
    }
    if (selection.reveal_scene) {
        filter_[0] = 0;
        problems_only_ = false;
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##scene-search", "Search objects, meshes, materials, textures",
                             filter_, sizeof(filter_));
    studio::TutorialWidgets::Checkbox("scene_browser", "Material issues only", &problems_only_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show objects with missing textures or unsupported material settings.");
    const Object *selected = nullptr;
    for (auto &object : objects_)
        if (std::find(object.draws.begin(), object.draws.end(), selection.draw) !=
            object.draws.end()) {
            selected = &object;
            break;
        }
    bool can_frame =
        selected && !renderer.player.active && (selected->category < 3 || selected->category == 5);
    ImGui::BeginDisabled(!can_frame);
    if (studio::TutorialWidgets::Button("scene_browser", "Frame object (F)"))
        frame_draws(*scene, selected->draws, renderer, camera);
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("scene_browser", "Frame mesh"))
        frame_draws(*scene, {selection.draw}, renderer, camera);
    ImGui::EndDisabled();
    if (can_frame && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F))
        frame_draws(*scene, selected->draws, renderer, camera);
    if (selected) {
        ImGui::TextWrapped("%s", selected->name.c_str());
        ImGui::TextDisabled("%zu mesh parts", selected->draws.size());
    } else
        ImGui::TextDisabled("Select a mesh or click the viewport.");
    auto query = lowercase(filter_);
    std::vector<const Object *> matches;
    for (auto &object : objects_)
        if ((query.empty() || object.search.find(query) != std::string::npos) &&
            (!problems_only_ || std::any_of(object.draws.begin(), object.draws.end(), [&](int i) {
                return needs_attention(*scene, i);
            })))
            matches.push_back(&object);
    ImGui::TextDisabled("%zu / %zu objects", matches.size(), objects_.size());
    ImGui::BeginChild("Scene tree", ImVec2(0, 0));
    const char *categories[] = {"Terrain", "Static objects", "Characters",
                                "Sky",     "Weather",        "Player"};
    for (int category = 0; category < 6; ++category) {
        unsigned count = 0;
        for (auto *object : matches)
            count += object->category == category;
        if (!count)
            continue;
        ImGui::PushID(category);
        if (selection.reveal_scene && selected && selected->category == category)
            ImGui::SetNextItemOpen(true);
        bool open = ImGui::TreeNodeEx("category", ImGuiTreeNodeFlags_DefaultOpen, "%s (%u)",
                                      categories[category], count);
        if (open) {
            for (auto *object : matches)
                if (object->category == category) {
                    ImGui::PushID(int(object - objects_.data()));
                    if ((selection.reveal_scene && object == selected) || !query.empty())
                        ImGui::SetNextItemOpen(true);
                    bool expanded = ImGui::TreeNodeEx(
                        "object",
                        ImGuiTreeNodeFlags_SpanAvailWidth |
                            (object == selected ? ImGuiTreeNodeFlags_Selected : 0),
                        "%s", object->name.c_str());
                    if (ImGui::IsItemHovered() &&
                        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && category < 3)
                        frame_draws(*scene, object->draws, renderer, camera);
                    if (expanded) {
                        for (auto index : object->draws) {
                            if (!query.empty() &&
                                lowercase(object->name).find(query) == std::string::npos &&
                                searches_[index].find(query) == std::string::npos)
                                continue;
                            if (problems_only_ && !needs_attention(*scene, index))
                                continue;
                            auto &mesh = scene->draws[index];
                            ImGui::PushID(index);
                            auto label =
                                mesh.mesh +
                                (needs_attention(*scene, index) ? "  [material issue]" : "");
                            if (ImGui::Selectable(label.c_str(), selection.draw == index)) {
                                selection.draw = index;
                                selection.material = int(mesh.material);
                                selection.focus = true;
                                selection.filter[0] = 0;
                            }
                            if (selection.reveal_scene && selection.draw == index)
                                ImGui::SetScrollHereY();
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s\nMaterial: %s", mesh.name.c_str(),
                                                  scene->materials[mesh.material].name.c_str());
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (matches.empty())
        ImGui::TextWrapped("No matching objects. Clear the search or material-issue filter.");
    ImGui::EndChild();
    selection.reveal_scene = false;
    ImGui::End();
}
}
