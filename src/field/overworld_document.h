#pragma once
#include "field/archive_sources.h"
#include "scene/spatial.h"
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
struct OverworldOperation {
    enum class Action { Add, Remove, Update };
    Action action = Action::Add;
    unsigned entry = 0, event = 0, model = 0, flag = 0, item = 0, quantity = 1,
             destination_zone = 0, destination_event = 0;
    SpatialPoint position{};
    bool operator==(const OverworldOperation &) const = default;
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
    void apply(const OverworldOperation &operation);
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
    unsigned area_;
    Bytes original_, characters_, objects_;
    std::string hash_;
    std::vector<OverworldEntry> entries_;
    std::vector<OverworldOperation> operations_, saved_;
    std::vector<std::vector<OverworldOperation>> history_{{}};
    std::size_t cursor_ = 0;
};
}
