#pragma once
#include "scene/environment.h"
#include <compare>
#include <optional>
#include <set>
#include <tuple>
namespace studio {
using CollisionFace = std::array<SpatialPoint, 3>;
struct CollisionVertex {
    unsigned face = 0, corner = 0;
    auto operator<=>(const CollisionVertex &) const = default;
};
struct CollisionState {
    CollisionFace vertices{};
    std::uint32_t attribute = 0;
    SpatialKind kind = SpatialKind::Ground;
    bool deleted = false;
    bool operator==(const CollisionState &) const = default;
};
using CollisionVertexGroup = std::tuple<std::size_t, SpatialKind, SpatialPoint>;
CollisionFace read_collision_face(View bytes, std::size_t face);
Bytes edit_collision_faces(View bytes, const std::map<std::size_t, CollisionFace> &edits);
struct CollisionImportResult {
    std::size_t added = 0, removed = 0, modified = 0;
};
void validate_collision_state(const CollisionState &state);
struct CollisionAddition {
    std::size_t member;
    CollisionState state;
};
class CollisionDocument {
  public:
    explicit CollisionDocument(std::shared_ptr<Environment> scene, unsigned area,
                               std::filesystem::path dump);
    std::size_t size() const {
        return current_.size();
    }
    std::size_t active_size() const;
    bool live(unsigned id) const {
        return id < size() && !state(id).deleted;
    }
    const CollisionState &state(unsigned id) const {
        return current_.at(id);
    }
    unsigned face_id(unsigned region, std::size_t face) const {
        return bindings_.at(region).at(face);
    }
    std::size_t member(unsigned id) const;
    CollisionVertexGroup vertex_group(CollisionVertex vertex) const;
    CollisionFace face(unsigned region, std::size_t face) const {
        return state(face_id(region, face)).vertices;
    }
    void edit(unsigned region, std::size_t face, const CollisionFace &vertices, bool shared);
    void edit_vertices(const std::map<CollisionVertex, SpatialPoint> &vertices, bool shared);
    void properties(const std::set<unsigned> &faces, std::optional<SpatialKind> kind,
                    std::optional<std::uint32_t> attribute);
    void replace_faces(const std::map<unsigned, CollisionState> &faces,
                       const std::vector<CollisionAddition> &additions);
    std::string face_token(unsigned id) const;
    std::string exchange_signature() const;
    void export_obj(const std::filesystem::path &path, bool reference = true) const;
    CollisionImportResult import_obj(const std::filesystem::path &path);
    void begin_preview();
    void commit_preview();
    void cancel_preview();
    bool previewing() const {
        return preview_.has_value();
    }
    bool is_changed(unsigned id) const {
        return edits_.contains(id);
    }
    void mark_saved() {
        saved_ = edits_;
    }
    bool dirty() const {
        return edits_ != saved_;
    }
    bool changed() const {
        return !edits_.empty();
    }
    bool can_undo() const {
        return cursor_ > 0;
    }
    bool can_redo() const {
        return cursor_ + 1 < history_.size();
    }
    void undo();
    void redo();
    void reset();
    std::string serialize() const;
    void restore(const std::string &text);
    void save(const std::filesystem::path &path);
    void export_to(const std::filesystem::path &folder) const;
    Bytes compile_member(std::size_t member, View original) const;
    unsigned revision() const {
        return revision_;
    }

  private:
    struct Source {
        std::shared_ptr<const CollisionSource> link;
        std::string hash;
        unsigned first = 0;
        SpatialKind kind = SpatialKind::Ground;
    };
    struct Origin {
        unsigned source;
        std::size_t face;
        bool added = false;
    };
    using Edits = std::map<unsigned, CollisionState>;
    void accept(Edits edits);
    void synchronize();
    void store(Edits &edits, unsigned id, const CollisionState &state) const;
    const CollisionState &from(const Edits &edits, unsigned id) const;
    std::shared_ptr<Environment> scene_;
    std::filesystem::path dump_;
    unsigned area_, revision_ = 0;
    std::size_t base_regions_ = 0;
    std::vector<Source> sources_;
    std::vector<Origin> origins_;
    std::vector<CollisionState> original_, current_;
    std::map<unsigned, std::vector<unsigned>> bindings_;
    std::map<std::tuple<unsigned, SpatialKind, std::uint32_t>, unsigned> base_groups_;
    std::string obj_baseline_, obj_result_;
    std::set<unsigned> obj_faces_;
    Edits edits_, saved_;
    std::optional<Edits> preview_;
    std::vector<Edits> history_{{}};
    std::size_t cursor_ = 0;
};
}
