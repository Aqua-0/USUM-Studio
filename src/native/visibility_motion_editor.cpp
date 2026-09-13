#include "native/tutorial_widgets.h"
#include "native/visibility_motion_editor.h"
#include <imgui.h>
#include <algorithm>
#include <set>
namespace studio {
void VisibilityMotionEditor::draw(MaterialDocument &doc, ModelDocument &preview,
                                  EnvironmentRenderer &renderer, bool &playing, bool &repeat) {
    if (preview.motion < 0) {
        ImGui::TextWrapped("Choose a motion in the viewport.");
        return;
    }
    auto index = std::size_t(preview.motion);
    auto motion = preview.motions.at(index).visibility;
    auto identity = doc.identity() + "/" + std::to_string(index);
    if (identity_ != identity) {
        identity_ = identity;
        start_ = end_ = 0;
        mesh_.clear();
        error_.clear();
    }
    ImGui::TextWrapped("Show or hide named meshes over this motion. Uses Save edits, Undo/Redo and "
                       "Write game files.");
    auto clock = motion.clock;
    clock.looping = repeat;
    int frame = int(animation_frame(clock, renderer.playback.seconds, false, 12));
    auto scrub = [&](int at) {
        playing = false;
        repeat = false;
        preview.select_motion(int(index), false);
        renderer.playback.seconds = at / 30.;
    };
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##visibility-frame", &frame, 0, int(clock.frames), "Frame %d"))
        scrub(frame);
    std::set<std::string> meshes;
    for (auto &draw : preview.scene->draws)
        meshes.insert(draw.mesh);
    if (meshes.empty())
        return;
    if (!meshes.contains(mesh_))
        mesh_ = *meshes.begin();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##visibility-mesh", mesh_.c_str())) {
        for (auto &name : meshes)
            if (ImGui::Selectable(name.c_str(), name == mesh_))
                mesh_ = name;
        ImGui::EndCombo();
    }
    auto found = std::find_if(motion.tracks.begin(), motion.tracks.end(), [&](auto &t) {
        return t.mesh == mesh_;
    });
    VisibilityTrack track;
    if (found == motion.tracks.end()) {
        track.mesh = mesh_;
        track.frames.assign(std::size_t(clock.frames) + 1, true);
    } else
        track = *found;
    ImGui::Text("At frame %d: %s", frame, track.frames.at(frame) ? "Shown" : "Hidden");
    if (found == motion.tracks.end())
        ImGui::TextDisabled("No track: mesh is shown by default.");
    ImGui::BeginChild("Visibility transitions", {0, 120}, ImGuiChildFlags_Borders);
    for (unsigned i = 0; i < track.frames.size(); ++i)
        if (i == 0 || track.frames[i] != track.frames[i - 1]) {
            char label[80];
            std::snprintf(label, sizeof(label), "%u: %s", i, track.frames[i] ? "Show" : "Hide");
            if (ImGui::Selectable(label, start_ == int(i))) {
                start_ = int(i);
                end_ = int(i);
                while (end_ + 1 < int(track.frames.size()) &&
                       track.frames[end_ + 1] == track.frames[i])
                    ++end_;
                scrub(int(i));
            }
        }
    ImGui::EndChild();
    ImGui::SetNextItemWidth(-90);
    ImGui::InputInt("First frame", &start_);
    ImGui::SetNextItemWidth(-90);
    ImGui::InputInt("Last frame", &end_);
    if (studio::TutorialWidgets::Button("visibility_motion_editor", "Use current frame"))
        start_ = end_ = frame;
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("visibility_motion_editor", "Whole motion")) {
        start_ = 0;
        end_ = int(clock.frames);
    }
    auto apply = [&](bool remove) {
        try {
            auto next = motion;
            std::erase_if(next.tracks, [&](auto &t) {
                return t.mesh == mesh_;
            });
            if (!remove) {
                if (found == motion.tracks.end())
                    next.tracks.push_back(track);
                else
                    next.tracks.insert(
                        next.tracks.begin() + std::distance(motion.tracks.begin(), found), track);
            }
            std::vector<bool> visible;
            for (unsigned i = 0; i < preview.scene->draws.size(); ++i)
                visible.push_back(renderer.draw_visible(i));
            doc.edit_visibility_motion(index, next);
            preview = doc.model;
            preview.select_motion(int(index), repeat);
            renderer.set_scene(preview.scene);
            for (unsigned i = 0; i < visible.size(); ++i)
                renderer.set_draw_visible(i, visible[i]);
            error_.clear();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    };
    ImGui::BeginDisabled(start_ < 0 || end_ < start_ || end_ > clock.frames);
    if (studio::TutorialWidgets::Button("visibility_motion_editor", "Show range")) {
        std::fill(track.frames.begin() + start_, track.frames.begin() + end_ + 1, true);
        apply(false);
    }
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("visibility_motion_editor", "Hide range")) {
        std::fill(track.frames.begin() + start_, track.frames.begin() + end_ + 1, false);
        apply(false);
    }
    ImGui::EndDisabled();
    if (found != motion.tracks.end() &&
        studio::TutorialWidgets::Button("visibility_motion_editor", "Remove mesh track"))
        apply(true);
    ImGui::TextWrapped("Ranges include both endpoints. All parts with this mesh name share the "
                       "track. Meshes tab hiding is preview-only.");
    if (preview.looping_overlay >= 0)
        ImGui::TextWrapped(
            "Disable Looping effects to inspect the base motion without its visibility overlay.");
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
}
}
