#pragma once
#include "field/archive_sources.h"
#include "scene/spatial.h"
namespace studio {
struct PickupRecord {
    unsigned local_zone = 0, row = 0, event = 0, condition = 0, expected = 0, appearance = 0;
    std::size_t offset = 0;
    std::vector<std::size_t> centers;
    std::string restriction;
    int visual = -1;
    std::string movement_restriction;
    std::size_t visual_flag = 0;
};
struct PickupValues {
    SpatialPoint position{};
    unsigned item = 0, quantity = 1, flag = 0;
};
std::vector<std::string> load_pickup_item_names(const std::filesystem::path &dump);
class PickupDocument {
  public:
    PickupDocument(unsigned area, Bytes original);
    const std::vector<PickupRecord> &records() const {
        return records_;
    }
    PickupValues values(unsigned index) const;
    void set(unsigned index, const PickupValues &values);
    void commit();
    void cancel();
    void undo();
    void redo();
    bool can_undo() const {
        return cursor_ > 0;
    }
    bool can_redo() const {
        return cursor_ + 1 < history_.size();
    }
    bool dirty() const {
        return current_ != saved_;
    }
    bool changed() const {
        return current_ != original_;
    }
    void mark_saved() {
        saved_ = current_;
    }
    Bytes compile() const {
        return current_;
    }
    std::string serialize() const;
    void restore(const std::string &patch);
    void validate_flags(const std::filesystem::path &dump,
                        const ArchiveSources &sources = {}) const;
    void validate_items(const std::vector<std::string> &names) const;
    void export_to(const std::filesystem::path &dump, const ArchiveSources &sources,
                   const std::filesystem::path &folder) const;

  private:
    void validate(View bytes) const;
    unsigned area_;
    Bytes original_, current_, saved_;
    std::string hash_;
    std::vector<PickupRecord> records_;
    std::vector<std::size_t> allowed_;
    std::vector<Bytes> history_;
    std::size_t cursor_ = 0;
};
}
