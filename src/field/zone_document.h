#pragma once
#include "core/binary.h"
#include <map>
namespace studio {
struct ZoneProperty {
    const char *key, *label, *group;
    unsigned offset, width;
    int bit;
    std::int64_t minimum, maximum;
};
const std::vector<ZoneProperty> &zone_properties();
class ZoneDocument {
  public:
    explicit ZoneDocument(Bytes source);
    unsigned size() const;
    std::int64_t value(unsigned zone, unsigned property) const;
    std::vector<std::int64_t> source_values(unsigned property) const;
    void set(unsigned zone, unsigned property, std::int64_t value);
    void reset(unsigned zone);
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
        return !current_.empty();
    }
    void mark_saved() {
        saved_ = current_;
    }
    void validate_resources(const std::filesystem::path &dump) const;
    Bytes compile() const;
    std::string serialize() const;
    void restore(const std::string &patch);
    void export_to(const std::filesystem::path &dump, const std::filesystem::path &output) const;

  private:
    using Changes = std::map<std::pair<unsigned, unsigned>, std::int64_t>;
    std::int64_t original(unsigned zone, unsigned property) const;
    void validate(unsigned zone, unsigned property, std::int64_t value) const;
    void commit(Changes next);
    Bytes source_;
    std::string hash_;
    Changes current_, saved_;
    std::vector<Changes> history_{{}};
    std::size_t cursor_ = 0;
};
}
