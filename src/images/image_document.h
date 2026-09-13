#pragma once
#include "images/image_format.h"
#include "formats/archive.h"
#include <functional>
namespace studio {
struct ImageSource {
    std::string archive, category, name, package_hash;
    unsigned member = 0, subfile = 0;
    std::size_t offset = 0, size = 0;
    NativeImageInfo info;
    std::string key() const;
};
struct ImageCatalog {
    std::filesystem::path dump;
    std::vector<ImageSource> images;
    std::vector<std::string> diagnostics;
};
ImageCatalog scan_image_catalog(const std::filesystem::path &dump,
                                const std::function<bool(unsigned, unsigned)> &progress = {});
class ImageDocument {
  public:
    explicit ImageDocument(ImageCatalog catalog) : catalog(std::move(catalog)) {
    }
    ImageCatalog catalog;
    Bytes original(std::size_t index) const;
    Bytes current(std::size_t index) const;
    void replace(std::size_t index, const TextureImage &image);
    void replace_encoded(std::size_t index, Bytes bytes);
    void reset(std::size_t index);
    bool edited(std::size_t index) const;
    bool dirty() const {
        return edits_ != saved_;
    }
    std::size_t size() const {
        return edits_.size();
    }
    void mark_saved() {
        saved_ = edits_;
    }
    bool undo();
    bool redo();
    void discard();
    void save(const std::filesystem::path &file);
    void load(const std::filesystem::path &file);
    void export_to(const std::filesystem::path &folder) const;

  private:
    using State = std::map<std::size_t, Bytes>;
    State edits_, saved_;
    std::vector<State> undo_, redo_;
};
}
