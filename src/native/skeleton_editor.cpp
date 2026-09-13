#include "native/tutorial_widgets.h"
#include "native/skeleton_editor.h"
#include <imgui.h>
#include <algorithm>
namespace studio {
void SkeletonEditor::draw(MaterialDocument &doc, ModelDocument &preview,
                          EnvironmentRenderer &renderer, bool &playing, int &bone) {
    if (preview.area >= 0 || preview.scene->skeletons.empty())
        return;
    auto joints = preview.scene->skeletons[0].joints;
    bone = std::clamp(bone, 0, int(joints.size()) - 1);
    if (identity_ != doc.identity() || revision_ != doc.revision() || selected_ != bone) {
        identity_ = doc.identity();
        revision_ = doc.revision();
        selected_ = bone;
        parent_ = joints[bone].parent;
        translation_ = joints[bone].translation;
        rotation_ = joints[bone].rotation;
        for (auto &value : rotation_)
            value *= 57.295779513f;
        error_.clear();
    }
    ImGui::SeparatorText("Edit skeleton");
    ImGui::TextWrapped("Local bind transforms. Parents must precede children. Mesh positions and "
                       "existing motion keys stay unchanged.");
    ImGui::SetNextItemWidth(-80);
    if (ImGui::BeginCombo("Parent", parent_ < 0 ? "No parent" : joints[parent_].name.c_str())) {
        if (joints[bone].parent < 0 && ImGui::Selectable("No parent", parent_ < 0))
            parent_ = -1;
        for (unsigned i = 0; i < unsigned(bone); ++i) {
            bool descendant = false;
            for (int p = int(i); p >= 0; p = joints[p].parent)
                if (p == bone) {
                    descendant = true;
                    break;
                }
            if (!descendant && ImGui::Selectable(joints[i].name.c_str(), parent_ == int(i)))
                parent_ = int(i);
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(-80);
    ImGui::InputFloat3("Position", translation_.data());
    ImGui::SetNextItemWidth(-80);
    ImGui::InputFloat3("Degrees", rotation_.data());
    auto apply = [&](std::vector<Joint> next, int selection) {
        try {
            std::vector<bool> visible;
            for (unsigned i = 0; i < preview.scene->draws.size(); ++i)
                visible.push_back(renderer.draw_visible(i));
            doc.edit_skeleton(next);
            preview = doc.model;
            preview.select_motion(-1);
            playing = false;
            renderer.set_scene(preview.scene);
            for (unsigned i = 0; i < visible.size(); ++i)
                renderer.set_draw_visible(i, visible[i]);
            bone = selection;
            selected_ = -1;
            error_.clear();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    };
    if (studio::TutorialWidgets::Button("skeleton_editor", "Apply bind transform")) {
        auto next = joints;
        next[bone].parent = parent_;
        next[bone].translation = translation_;
        next[bone].rotation = rotation_;
        for (auto &value : next[bone].rotation)
            value *= .01745329252f;
        apply(std::move(next), bone);
    }
    ImGui::SeparatorText("Add child bone");
    ImGui::TextWrapped("Parent: %s. Starts at the parent's origin with no weights.",
                       joints[bone].name.c_str());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##new-bone", "Unique bone name", name_, sizeof(name_));
    ImGui::BeginDisabled(!name_[0]);
    if (studio::TutorialWidgets::Button("skeleton_editor", "Add child bone")) {
        auto next = joints;
        Joint added;
        added.name = name_;
        added.parent = bone;
        added.scale = {1, 1, 1};
        next.push_back(added);
        apply(std::move(next), int(joints.size()));
        if (error_.empty())
            name_[0] = 0;
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Paint influences in Geometry > Weights, then animate it in Motions > "
                       "Bones. Save edits and Write game files include the skeleton.");
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
}
}
