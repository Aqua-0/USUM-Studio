#pragma once
#include "field/archive_sources.h"
#include "scene/spatial.h"
namespace studio {
struct WarpShape {
    unsigned type = 0;
    std::size_t offset = 0;
};
struct WarpRecord {
    unsigned local_zone = 0, row = 0, zone = 0, event = 0;
    std::size_t offset = 0;
    std::vector<WarpShape> shapes;
};
struct WarpValues {
    SpatialPoint position{}, arrival{};
    std::array<float, 4> rotation{};
    unsigned destination_zone = 0, destination_event = 0;
};
struct WarpDestination {
    unsigned area = 0, zone = 0, event = 0;
    std::string label;
};
std::vector<WarpDestination> load_warp_destinations(const std::filesystem::path &dump,
                                                    const ArchiveSources &sources = {});
class WarpDocument {
  public:
    WarpDocument(unsigned area, Bytes original);
    const std::vector<WarpRecord> &records() const {
        return records_;
    }
    WarpValues values(unsigned index) const;
    void set(unsigned index, const WarpValues &values);
    float shape_value(const WarpShape &shape, unsigned component) const;
    void set_shape(unsigned index, unsigned shape, unsigned component, float value);
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
    void validate_destinations(const std::vector<WarpDestination> &destinations) const;
    std::string serialize() const;
    void restore(const std::string &patch);
    void export_to(const std::filesystem::path &dump, const ArchiveSources &sources,
                   const std::filesystem::path &folder) const;

  private:
    void validate(View bytes) const;
    unsigned area_ = 0;
    Bytes original_, current_, saved_;
    std::string hash_;
    std::vector<WarpRecord> records_;
    std::vector<std::size_t> allowed_;
    std::vector<Bytes> history_;
    std::size_t cursor_ = 0;
};
}
