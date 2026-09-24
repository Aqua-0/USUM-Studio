#pragma once
#include "core/binary.h"
#include <functional>
namespace studio {
using SourceProgress = std::function<void(std::size_t, std::size_t, const std::string &)>;
Bytes hash_source_inventory(const std::filesystem::path &directory,
                            const SourceProgress &progress = {});
Bytes verified_source_inventory(View inventory, const std::filesystem::path &directory,
                                bool full = false, const SourceProgress &progress = {});
void verify_source_inventory(View inventory, const std::filesystem::path &directory,
                             bool full = false, const SourceProgress &progress = {});
std::string source_inventory_identity(const std::filesystem::path &objects,
                                      const std::string &identity);
bool hashed_source_inventory(View inventory);
void verify_source_content(const std::filesystem::path &file, std::uintmax_t size, long long time,
                           const std::string &hash, bool full = false);
}
