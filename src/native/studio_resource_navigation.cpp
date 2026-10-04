#include "native/model_workspace.h"
namespace studio {
void ModelWorkspace::navigate_resource(const StudioResourceRequest &request) {
    if (!document_ || !editor_->document())
        return;
    using Action = StudioResourceRequest::Action;
    auto &row = request.resource;
    auto &scene = *document_->scene;
    if (request.action == Action::Save) {
        editor_->save_edits();
        return;
    }
    if (request.action != Action::Select && !editor_->editing_available())
        return;
    const bool edit = request.action == Action::Edit;
    switch (row.kind) {
    case StudioResourceKind::Mesh:
        if (row.index < 0 || std::size_t(row.index) >= scene.draws.size())
            return;
        selection_.select_mesh(row.index, int(scene.draws[row.index].material), false);
        if (edit)
            inspector_page_ = "Meshes";
        break;
    case StudioResourceKind::Material:
        if (row.index < 0 || std::size_t(row.index) >= scene.materials.size())
            return;
        selection_.material = row.index;
        selection_.draw = -1;
        selection_.meshes.clear();
        for (unsigned i = 0; i < scene.draws.size(); ++i)
            if (scene.draws[i].material == std::size_t(row.index)) {
                selection_.meshes.insert(int(i));
                if (selection_.draw < 0)
                    selection_.draw = int(i);
            }
        if (edit) {
            editor_->focus_material();
            ImGui::SetWindowFocus("Studio materials");
        }
        break;
    case StudioResourceKind::Texture:
        editor_->focus_texture(row.name, selection_);
        if (edit)
            ImGui::SetWindowFocus("Studio materials");
        break;
    case StudioResourceKind::Bone:
        if (row.skeleton != 0 || scene.skeletons.empty() || row.index < 0 ||
            std::size_t(row.index) >= scene.skeletons[0].joints.size())
            return;
        bone_ = row.index;
        show_bones_ = true;
        if (edit)
            inspector_page_ = "Skeleton";
        break;
    case StudioResourceKind::Motion:
        if (row.index < 0 || std::size_t(row.index) >= document_->motions.size())
            return;
        if (!document_->motions[row.index].error.empty())
            return;
        if (document_->motion != row.index)
            renderer_.playback.seconds = 0;
        playing_ = false;
        document_->select_motion(row.index, repeat_);
        editor_->document()->model.select_motion(row.index, repeat_);
        renderer_.refresh_materials();
        motion_group_ = int(document_->motions[row.index].group);
        if (edit || request.action == Action::Pose || request.action == Action::MaterialMotion ||
            request.action == Action::VisibilityMotion) {
            inspector_page_ = "Motions";
            auto &motion = document_->motions[row.index];
            motion_kind_ = request.action == Action::Pose               ? 1
                           : request.action == Action::MaterialMotion   ? 0
                           : request.action == Action::VisibilityMotion ? 2
                           : !motion.skeletal.tracks.empty()            ? 1
                           : !motion.material.tracks.empty()            ? 0
                                                                        : 2;
            if (motion_kind_ == 1)
                show_bones_ = true;
        }
        break;
    case StudioResourceKind::Other:
        if (edit)
            inspector_page_ = row.key.starts_with("refresh") ? "Refresh" : "Source";
        break;
    }
    if (request.action == Action::Uvs)
        inspector_page_ = "UVs";
    if (request.action == Action::Frame && selection_.draw >= 0)
        fit(selection_.draw);
    if (request.action == Action::Isolate && selection_.draw >= 0)
        for (unsigned i = 0; i < scene.draws.size(); ++i)
            renderer_.set_draw_visible(i, selection_.mesh_selected(int(i)));
    if (edit && row.kind != StudioResourceKind::Material && row.kind != StudioResourceKind::Texture)
        ImGui::SetWindowFocus("Studio inspector");
}
}
