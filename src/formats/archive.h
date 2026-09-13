#pragma once
#include "core/binary.h"
#include <map>
#include "core/resource_source.h"
namespace studio {
Bytes read_archive_subfile(const std::filesystem::path &path, std::size_t member,
                           unsigned subfile = 0);
struct ArchiveEntry {
    std::uint32_t start, end, size;
    std::size_t record;
};
class Archive {
  public:
    explicit Archive(const std::filesystem::path &path);
    Bytes layout_identity() const;
    std::size_t size() const {
        return entries_.size();
    }
    std::vector<unsigned> subfiles(std::size_t index) const;
    Bytes raw(std::size_t index, unsigned subfile = 0) const;
    Bytes decoded(std::size_t index, unsigned subfile = 0) const;
    void export_subfiles(const std::filesystem::path &output,
                         const std::map<std::pair<std::size_t, unsigned>, Bytes> &replacements,
                         bool replace_existing = false) const;
    void export_appended(const std::filesystem::path &output,
                         const std::map<std::pair<std::size_t, unsigned>, Bytes> &replacements,
                         bool replace_existing = false) const;
    void export_to(const std::filesystem::path &output,
                   const std::map<std::size_t, Bytes> &replacements,
                   bool replace_existing = false) const;

  private:
    void write(const std::filesystem::path &,
               const std::map<std::pair<std::size_t, unsigned>, Bytes> &, bool, bool) const;
    std::filesystem::path path_;
    Bytes prefix_;
    ResourceMembers overrides_;
    std::shared_ptr<void> resource_pin_;
    std::filesystem::file_time_type modified_;
    std::uintmax_t file_size_ = 0;
    std::vector<std::map<unsigned, ArchiveEntry>> entries_;
    std::uint32_t data_offset_ = 0, alignment_ = 0;
    std::size_t image_header_ = 0;
};
}
