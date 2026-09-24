#pragma once
#include "scene/spatial.h"
namespace studio {
struct PedestrianPath {
    bool curved = false, loop = false, follow_ground = false;
    std::vector<SpatialPoint> points;
    bool operator==(const PedestrianPath &) const = default;
};
class PedestrianCurve {
  public:
    explicit PedestrianCurve(const PedestrianPath &path);
    SpatialPoint position(float progress) const;
    SpatialPoint direction(float progress) const;
    float length() const {
        return length_;
    }

  private:
    PedestrianPath path_;
    std::vector<float> lengths_;
    float length_ = 0;
    SpatialPoint segment(unsigned index, float t) const;
};
PedestrianPath read_pedestrian_path(View bytes, std::size_t offset);
void append_route_geometry(SpatialRegion &region, const PedestrianPath &path);
struct PedestrianRoute {
    unsigned zone = 0, row = 0;
    PedestrianPath path;
    float cooldown = 0;
    std::vector<std::array<unsigned, 8>> choices;
    bool operator==(const PedestrianRoute &) const = default;
};
class PedestrianDocument {
  public:
    explicit PedestrianDocument(Bytes source);
    const std::vector<PedestrianRoute> &routes() const {
        return current_;
    }
    void set(unsigned index, PedestrianRoute route);
    void undo();
    void redo();
    bool can_undo() const {
        return cursor_ > 0;
    }
    bool can_redo() const {
        return cursor_ + 1 < history_.size();
    }
    bool dirty() const {
        return saved_ != current_;
    }
    void mark_saved() {
        saved_ = current_;
    }
    std::string serialize() const;
    void restore(const std::string &patch);
    Bytes compile(View source) const;
    Bytes compile() const {
        return compile(source_);
    }
    void export_to(const std::filesystem::path &dump, const std::filesystem::path &output,
                   unsigned area) const;

  private:
    Bytes source_;
    std::string hash_;
    std::vector<PedestrianRoute> original_, current_, saved_;
    std::vector<std::vector<PedestrianRoute>> history_;
    std::size_t cursor_ = 0;
};
}
