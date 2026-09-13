#pragma once
#include "core/binary.h"
#include <map>
#include <memory>
#include <set>
namespace studio {
using ResourceMembers = std::map<std::pair<std::size_t, unsigned>, std::filesystem::path>;
struct ResourceSource {
    std::filesystem::path original;
    ResourceMembers members;
};
ResourceSource resource_source(const std::filesystem::path &path);
std::filesystem::path resource_file_path(const std::filesystem::path &path);
bool resource_exists(const std::filesystem::path &path);
bool archive_reference(const std::filesystem::path &path);
std::shared_ptr<void> pin_resources(const ResourceMembers &members);
std::set<std::string> pinned_resource_objects(const std::filesystem::path &objects);
bool member_capture_active();
Bytes resource_object(const std::filesystem::path &path);
class MemberCapture {
  public:
    explicit MemberCapture(const std::filesystem::path &objects);
    ~MemberCapture();
    MemberCapture(const MemberCapture &) = delete;
    MemberCapture &operator=(const MemberCapture &) = delete;

  private:
    std::filesystem::path previous_;
};
bool capture_archive(const std::filesystem::path &output, const ResourceSource &source,
                     const std::map<std::pair<std::size_t, unsigned>, Bytes> &replacements,
                     bool replace);
}
