#pragma once
#include "field/archive_sources.h"
#include "scene/spatial.h"
namespace studio {
struct EncounterShape {
    unsigned type = 1;
    SpatialPoint position{}, size{200, 200, 200};
    std::array<float, 4> rotation{0, 0, 0, 1};
};
struct EncounterRegion {
    int source = -1;
    unsigned zone = 0, table = 0;
    std::array<unsigned, 4> attributes{};
    Bytes shapes;
    bool operator==(const EncounterRegion &) const = default;
};
struct EncounterSlot {
    unsigned species = 0, form = 0, weight = 0;
};
struct EncounterPeriod {
    unsigned minimum = 1, maximum = 1;
    std::array<EncounterSlot, 10> slots;
};
class EncounterDocument {
  public:
    EncounterDocument(unsigned area, Bytes placements, Bytes tables);
    const std::vector<EncounterRegion> &regions() const {
        return state_.regions;
    }
    unsigned zone_count() const {
        return unsigned(zones_.files.size());
    }
    unsigned table_count() const {
        return unsigned(state_.tables.size());
    }
    bool has_table(unsigned table) const;
    unsigned table_users(unsigned table) const;
    EncounterPeriod period(unsigned table, bool night) const;
    unsigned shape_count(unsigned region) const;
    EncounterShape shape(unsigned region, unsigned index) const;
    void set_shape(unsigned region, unsigned index, const EncounterShape &shape);
    void preview_shape(unsigned region, unsigned index, const EncounterShape &shape);
    void commit_preview();
    void cancel_preview();
    void set_region(unsigned index, const EncounterRegion &region);
    unsigned add_region(unsigned zone, unsigned table, const EncounterShape &shape);
    unsigned duplicate(unsigned region);
    void erase(unsigned region);
    unsigned make_table_unique(unsigned region);
    void set_period(unsigned table, bool night, const EncounterPeriod &values,
                    unsigned species_count);
    void undo();
    void redo();
    bool can_undo() const {
        return cursor_ > 0;
    }
    bool can_redo() const {
        return cursor_ + 1 < history_.size();
    }
    bool dirty() const {
        return state_ != saved_;
    }
    bool changed() const {
        return state_ != original_;
    }
    void mark_saved() {
        saved_ = state_;
    }
    Bytes placements() const;
    Bytes tables() const;
    std::string serialize() const;
    void restore(const std::string &patch);
    void export_to(const std::filesystem::path &dump, const ArchiveSources &,
                   const std::filesystem::path &output) const;

  private:
    struct Table {
        unsigned source = 0;
        Bytes bytes;
        bool operator==(const Table &) const = default;
    };
    struct State {
        std::vector<EncounterRegion> regions;
        std::vector<Table> tables;
        bool operator==(const State &) const = default;
    };
    void validate(const State &) const;
    void commit(State);
    unsigned area_;
    Bytes placement_source_, table_source_;
    Container placements_, zones_, tables_;
    std::string hash_;
    State original_, state_, saved_;
    std::vector<State> history_;
    std::size_t cursor_ = 0;
};
void decode_encounter_regions(SpatialScene &, View placements,
                              const std::map<unsigned, int> &zone_ids);
}
