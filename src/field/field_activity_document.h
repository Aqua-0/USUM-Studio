#pragma once
#include "field/field_systems.h"
namespace studio {
struct FieldActivitySetting {
    std::string name;
    unsigned offset, width = 4;
};
const std::vector<FieldActivitySetting> &field_activity_settings(unsigned category);
class FieldActivityDocument {
  public:
    explicit FieldActivityDocument(Bytes source);
    const Bytes &bytes() const {
        return current_;
    }
    Bytes record(unsigned category, unsigned zone, unsigned row) const;
    void update(unsigned category, unsigned zone, unsigned row, const Bytes &record);
    unsigned duplicate(unsigned category, unsigned zone, unsigned row, unsigned event,
                       SpatialPoint position);
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
    void mark_saved() {
        saved_ = current_;
    }
    std::string serialize() const;
    void restore(const std::string &);
    Bytes compile(View source) const;
    void export_to(const std::filesystem::path &, const std::filesystem::path &,
                   unsigned area) const;

  private:
    Bytes original_, current_, saved_;
    std::vector<Bytes> history_;
    std::size_t cursor_ = 0;
    void commit(Bytes);
};
}
