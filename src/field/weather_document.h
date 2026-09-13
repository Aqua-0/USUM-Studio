#pragma once
#include "core/binary.h"
#include <map>
#include <array>
namespace studio {
using WeatherSchedule = std::array<unsigned, 5>;
unsigned weather_period(float hour);
const char *weather_period_name(unsigned period);
class WeatherDocument {
  public:
    explicit WeatherDocument(Bytes original);
    WeatherSchedule schedule(unsigned zone) const;
    void set(unsigned zone, unsigned period, unsigned kind);
    void reset(unsigned zone);
    void undo();
    void redo();
    bool can_undo() const {
        return cursor_ > 0;
    }
    bool can_redo() const {
        return cursor_ + 1 < history_.size();
    }
    bool changed() const {
        return !current_.empty();
    }
    bool dirty() const {
        return current_ != saved_;
    }
    void mark_saved() {
        saved_ = current_;
    }
    Bytes compile() const;
    std::string serialize() const;
    void restore(const std::string &patch);
    void export_to(const std::filesystem::path &dump, const std::filesystem::path &output) const;

  private:
    using Changes = std::map<std::pair<unsigned, unsigned>, unsigned>;
    std::size_t offset(unsigned zone, unsigned period) const;
    void commit(Changes next);
    Bytes original_;
    std::string hash_;
    Changes current_, saved_;
    std::vector<Changes> history_{{}};
    std::size_t cursor_ = 0;
};
}
