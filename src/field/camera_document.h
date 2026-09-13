#pragma once
#include "scene/environment.h"
#include "formats/container.h"
namespace studio {
struct CameraField {
    std::string group, label;
    unsigned part = 0;
    std::size_t offset = 0;
    enum Type { Float, Unsigned, Signed, Short, Boolean, Byte } type = Float;
};
class CameraDocument {
  public:
    CameraDocument(unsigned area, Bytes original);
    const std::vector<CameraField> &fields() const {
        return fields_;
    }
    double value(const CameraField &field) const;
    void set(const CameraField &field, double value);
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
    void mark_saved() {
        saved_ = current_;
    }
    bool dirty() const {
        return current_ != saved_;
    }
    bool changed() const {
        return current_ != original_;
    }
    Bytes compile() const;
    void apply(SpatialScene &scene) const;
    std::string serialize() const;
    void restore(const std::string &patch);
    void save(const std::filesystem::path &path);
    void export_to(const std::filesystem::path &dump, const ArchiveSources &sources,
                   const std::filesystem::path &folder) const;

  private:
    void validate(const Container &data) const;
    void describe();
    unsigned area_;
    Bytes baseline_;
    std::string hash_;
    Container pack_;
    std::vector<Bytes> current_, original_, saved_;
    std::vector<std::vector<Bytes>> history_;
    std::size_t cursor_ = 0;
    std::vector<CameraField> fields_;
};
}
