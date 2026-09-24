#pragma once
#include "assets/model_document.h"
#include "field/authored_interaction.h"
#include "field/trainer_catalog.h"
#include "native/map_cursor.h"
#include <future>
namespace studio {
class ConversationSceneTools {
  public:
    ~ConversationSceneTools() {
        stop_motion();
    }
    void context(std::shared_ptr<Environment> scene, EnvironmentRenderer &renderer,
                 MapCursor &cursor, std::filesystem::path source, unsigned area, unsigned zone,
                 unsigned event, bool scenery = false);
    void update(bool active);
    void reset();
    void initialize(ConversationStep &step) const;
    bool actor(ConversationStep &step);
    bool motion(ConversationStep &step);
    bool destination(ConversationStep &step);
    bool reward(ConversationStep &step);
    bool encounter(ConversationStep &step);
    bool trainer(ConversationStep &step);
    std::string text_token();
    void stop_motion();

  private:
    int region(int actor) const;
    std::string actor_name(int actor) const;
    void load_motion(int actor);
    std::shared_ptr<Environment> scene_, preview_scene_;
    EnvironmentRenderer *renderer_ = nullptr;
    MapCursor *cursor_ = nullptr;
    std::filesystem::path source_;
    unsigned area_ = 0, zone_ = 0, event_ = 0, step_ = 0;
    bool scenery_ = false;
    int loaded_actor_ = -3, preview_slot_ = -1;
    std::future<std::vector<AssetMotion>> loading_;
    std::vector<AssetMotion> motions_;
    std::map<unsigned, SceneSkeleton> originals_;
    std::vector<std::string> items_, encounters_, species_;
    std::vector<TrainerBattleEntry> trainers_;
    bool trainers_loaded_ = false;
    char trainer_filter_[100]{}, word_filter_[100]{};
    int word_kind_ = 0, word_value_ = 1;
    std::string trainer_error_;
    bool items_loaded_ = false, encounters_loaded_ = false, drawn_ = false, discard_ = false;
    char actor_filter_[100]{}, item_filter_[100]{}, encounter_filter_[100]{};
    std::string motion_error_, item_error_, encounter_error_;
};
}
