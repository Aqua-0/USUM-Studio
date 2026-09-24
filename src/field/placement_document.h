#pragma once
#include "field/archive_sources.h"
#include "field/area.h"
#include "formats/skinning.h"
namespace studio {
struct PlacementState {
    std::array<float, 3> position{};
    float turn = 0;
    bool operator==(const PlacementState &) const = default;
};
struct PlacementEntry {
    std::size_t zone = 0, row = 0, offset = 0;
    Placement source;
    std::string restriction;
    bool character = false, trainer = false;
    std::size_t patrol = 0;
    unsigned character_model = 0;
    std::vector<std::size_t> shapes;
};
class PlacementDocument {
  public:
    PlacementDocument() = default;
    PlacementDocument(unsigned area, Bytes source);
    unsigned area() const {
        return area_;
    }
    const Bytes &source() const {
        return source_;
    }
    const std::vector<PlacementEntry> &entries() const {
        return entries_;
    }
    const PlacementState &state(std::size_t i) const {
        return states_.at(i);
    }
    Matrix transform(std::size_t i) const;
    void preview(std::size_t i, PlacementState state);
    void commit();
    void cancel();
    void undo();
    void redo();
    void reset(std::size_t i);
    bool can_undo() const {
        return cursor_ > 0;
    }
    bool can_redo() const {
        return cursor_ + 1 < history_.size();
    }
    bool dirty() const {
        return states_ != saved_;
    }
    std::size_t changed_count() const;
    std::uint64_t revision() const {
        return revision_;
    }
    std::string serialize() const;
    void restore(const std::string &text);
    void mark_saved() {
        saved_ = states_;
    }
    Bytes compile() const;

  private:
    unsigned area_ = 0;
    Bytes source_;
    std::string digest_;
    std::vector<PlacementEntry> entries_;
    std::vector<PlacementState> states_, saved_, initial_;
    std::vector<std::vector<PlacementState>> history_;
    std::size_t cursor_ = 0;
    std::uint64_t revision_ = 0;
};
void export_placements(const std::filesystem::path &dump, const std::filesystem::path &folder,
                       const PlacementDocument &document, const ArchiveSources &archives = {});
}
