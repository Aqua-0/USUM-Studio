#pragma once
#include "field/archive_sources.h"
#include "scene/spatial.h"
#include "field/pedestrian_routes.h"
#include <optional>
namespace studio {
enum class OverworldKind {
    Pickup,
    StaticObject,
    Character,
    Trainer,
    Entrance,
    Interaction,
    StoryTrigger
};
const char *overworld_kind_name(OverworldKind kind);
struct OverworldEntry {
    OverworldKind kind;
    unsigned zone = 0, row = 0, event = 0, model = 0, script = 0, condition = 0;
    SpatialPoint position{};
    std::string restriction;
};
struct PlacementVolume {
    unsigned type = 0;
    std::array<float, 10> values{};
    bool operator==(const PlacementVolume &) const = default;
};
SpatialScene preview_placement_volumes(const std::vector<PlacementVolume> &volumes,
                                       SpatialPoint origin);
struct PlacementVolumeGroup {
    std::string name;
    std::vector<PlacementVolume> shapes;
};
struct TrainerPatrolAction {
    float progress = 0, facing = 0, frame = 0;
    unsigned motion = 0, repeats = 1;
    bool operator==(const TrainerPatrolAction &) const = default;
};
struct TrainerPatrol {
    unsigned movement = 4, motion = 0;
    float frame = 0;
    PedestrianPath path;
    std::vector<TrainerPatrolAction> actions;
    std::vector<std::array<unsigned, 4>> signals;
    bool operator==(const TrainerPatrol &) const = default;
};
struct OverworldOperation {
    enum class Action { Add, Remove, Update };
    Action action = Action::Add;
    unsigned editor_id = 0;
    unsigned entry = 0, event = 0, model = 0, flag = 0, item = 0, quantity = 1,
             destination_zone = 0, destination_event = 0;
    float turn = 0;
    int script = -1, battle_encounter = -1;
    std::string dialogue;
    std::optional<TrainerPatrol> patrol;
    SpatialPoint position{};
    std::map<unsigned, std::vector<PlacementVolume>> shapes;
    bool operator==(const OverworldOperation &) const = default;
};
struct WorkingPlacement {
    std::uint64_t id = 0;
    OverworldEntry entry;
};
class OverworldDocument {
  public:
    OverworldDocument(unsigned area, Bytes placements, Bytes characters, Bytes objects);
    const std::vector<OverworldEntry> &entries() const {
        return entries_;
    }
    const std::vector<OverworldOperation> &operations() const {
        return operations_;
    }
    OverworldOperation draft(unsigned entry, OverworldOperation::Action action) const;
    std::vector<PlacementVolumeGroup> shape_groups(unsigned entry) const;
    TrainerPatrol trainer_patrol(unsigned entry) const;
    void prepare_dialogue(const std::filesystem::path &dump, OverworldOperation &operation) const;
    void apply(const OverworldOperation &operation);
    const std::vector<WorkingPlacement> &working_entries() const;
    bool interaction_target_available(std::uint64_t id) const;
    bool same_structure(const OverworldDocument &other) const;
    OverworldOperation working_draft(std::uint64_t id, OverworldOperation::Action action) const;
    void apply_working(std::uint64_t id, const OverworldOperation &operation);
    std::map<std::size_t, Bytes> preview_members(const std::filesystem::path &dump) const;
    void undo();
    void redo();
    bool can_undo() const {
        return cursor_ > 0;
    }
    bool can_redo() const {
        return cursor_ + 1 < history_.size();
    }
    bool dirty() const {
        return operations_ != saved_;
    }
    bool changed() const {
        return !operations_.empty();
    }
    void mark_saved() {
        saved_ = operations_;
    }
    Bytes placements() const;
    std::map<std::size_t, Bytes> compile(const std::filesystem::path &dump) const;
    void export_to(const std::filesystem::path &dump, const std::filesystem::path &output) const;
    std::string serialize() const;
    void restore(const std::string &text);

  private:
    struct DialogueChanges {
        std::map<std::size_t, Bytes> scripts, messages;
    };
    DialogueChanges compile_dialogues(const std::filesystem::path &dump) const;
    unsigned area_;
    unsigned next_editor_id_ = 1;
    Bytes original_, characters_, objects_;
    std::string hash_;
    std::vector<OverworldEntry> entries_;
    std::vector<OverworldOperation> operations_, saved_;
    mutable bool working_entries_valid_ = false;
    mutable std::vector<OverworldOperation> working_entries_operations_;
    mutable std::vector<WorkingPlacement> working_entries_cache_;
    std::vector<std::vector<OverworldOperation>> history_{{}};
    std::size_t cursor_ = 0;
};
}
