#pragma once
#include <utility>
#include "assets/model_document.h"
#include "assets/overworld_character_asset.h"
#include "assets/model_library.h"
namespace studio {
class CharacterRegistrationEditor {
  public:
    static bool accepts(const ModelDocument &donor);
    bool draw(const ModelDocument &donor);
    bool draw_conversion(MaterialDocument &document, ModelDocument &preview);
    bool take_motion_preview() { return std::exchange(motion_preview_, false); }
    std::unique_ptr<ModelDocument> take_created() {
        return std::move(created_);
    }

  private:
    std::string message_, conversion_source_;
    std::map<std::string, std::pair<unsigned, unsigned>> registration_ids_;
    std::filesystem::path donor_source_;
    std::vector<LibraryModel> conversion_donors_;
    std::vector<unsigned> conversion_donor_types_;
    bool conversion_pokemon_ = false;
    int conversion_donor_ = -1, conversion_scale_ = 100;
    OverworldMotionSelection conversion_motions_;
    bool motion_preview_ = false;
    std::filesystem::path project_root_;
    std::optional<unsigned> registered_;
    std::string registered_name_;
    std::unique_ptr<ModelDocument> created_;
};
}
