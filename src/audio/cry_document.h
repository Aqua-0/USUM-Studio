#pragma once
#include "audio/cry.h"
namespace studio {
struct CryReplacement {
    std::string original_hash;
    Bytes bytes;
    bool operator==(const CryReplacement &) const = default;
};
class CryDocument {
  public:
    explicit CryDocument(const std::filesystem::path &dump) : dump(dump), library(dump) {
    }
    std::filesystem::path dump;
    CryLibrary library;
    void mark_saved() {
        saved_ = edits_;
    }
    bool dirty() const {
        return edits_ != saved_;
    }
    std::size_t size() const {
        return edits_.size();
    }
    Bytes current(unsigned member) const;
    void replace(unsigned member, Bytes bytes);
    void reset(unsigned member);
    bool undo();
    bool redo();
    void discard();
    void save(const std::filesystem::path &file);
    void load(const std::filesystem::path &file);
    void export_to(const std::filesystem::path &folder) const;

  private:
    using State = std::map<unsigned, CryReplacement>;
    State edits_, saved_;
    std::vector<State> undo_, redo_;
};
}
