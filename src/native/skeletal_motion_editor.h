#pragma once
#include "assets/material_document.h"
#include "native/renderer.h"
#include "native/folder_picker.h"
#include "native/skeletal_track_graph.h"
namespace studio {
class SkeletalMotionEditor {
  public:
    void draw(MaterialDocument &document, ModelDocument &preview, EnvironmentRenderer &renderer,
              bool &playing, bool &repeat, int &bone, bool &show_bones, bool *show_weights = nullptr);

  private:
    SkeletalTrackGraph tracks_;
    std::shared_ptr<FolderSelection> pose_dialog_ = std::make_shared<FolderSelection>();
    std::string pose_export_;
    bool graph_expanded_ = false;
    std::string identity_, error_;
    int channel_ = 6, key_frame_ = 0, editing_bone_ = -1, editing_channel_ = -1;
    float value_ = 0, slope_ = 0;
    int first_ = 0, last_ = 0, offset_ = 0, paste_frame_ = 0;
    float time_scale_ = 1;
    std::vector<AnimationKey> clipboard_;
    std::vector<std::pair<std::string, SkeletalMotion>> poses_;
    char pose_name_[96] = "Pose";
    char search_[96]{};
};
}
