#include "core/source_inventory.h"
#include "core/digest.h"
#include <map>
#include <mutex>
#include <sstream>
#include <iomanip>
namespace studio {
namespace {
struct FileIdentity {
    std::uintmax_t size;
    long long time;
    std::string hash;
};
std::map<std::string, FileIdentity> parse(View inventory) {
    std::istringstream in(text(inventory));
    std::string tag, name;
    unsigned version;
    require(bool(in >> tag >> version) && tag == "USUM_ORIGINAL" && (version == 1 || version == 2),
            "Invalid original dump inventory");
    std::map<std::string, FileIdentity> result;
    while (in >> std::quoted(name)) {
        FileIdentity file{};
        require(bool(in >> file.size >> file.time), "Incomplete original inventory");
        if (version == 2)
            require(bool(in >> file.hash) && file.hash.size() == 64 &&
                        file.hash.find_first_not_of("0123456789abcdef") == std::string::npos,
                    "Invalid original content hash");
        require(result.emplace(name, std::move(file)).second, "Duplicate original file");
    }
    require(in.eof(), "Malformed original inventory");
    return result;
}
std::map<std::string, FileIdentity> inventory_files(const std::filesystem::path &directory) {
    std::map<std::string, FileIdentity> result;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(directory)) {
        require(!entry.is_symlink(), "Original dump contains symbolic links");
        if (entry.is_regular_file())
            result.emplace(entry.path().lexically_relative(directory).generic_string(),
                           FileIdentity{entry.file_size(),
                                        entry.last_write_time().time_since_epoch().count(),
                                        {}});
    }
    return result;
}
}
bool hashed_source_inventory(View inventory) {
    std::istringstream in(text(inventory));
    std::string tag;
    unsigned version = 0;
    return bool(in >> tag >> version) && tag == "USUM_ORIGINAL" && version == 2;
}
void verify_source_content(const std::filesystem::path &file, std::uintmax_t size, long long time,
                           const std::string &hash, bool full) {
    require(std::filesystem::is_regular_file(file) && std::filesystem::file_size(file) == size,
            "Original resource size changed: " + file.string());
    auto current = std::filesystem::last_write_time(file).time_since_epoch().count();
    if (hash.empty()) {
        require(current == time, "Original resource timestamp changed: " + file.string());
        return;
    }
    if (!full && current == time)
        return;
    static std::mutex mutex;
    static std::map<std::pair<std::filesystem::path, std::string>, long long> verified;
    const auto key = std::pair{std::filesystem::absolute(file).lexically_normal(), hash};
    {
        std::lock_guard lock(mutex);
        const auto found = verified.find(key);
        if (!full && found != verified.end() && found->second == current)
            return;
    }
    require(sha256_file(file) == hash, "Original resource content changed: " + file.string());
    require(std::filesystem::file_size(file) == size &&
                std::filesystem::last_write_time(file).time_since_epoch().count() == current,
            "Original resource changed while hashing: " + file.string());
    std::lock_guard lock(mutex);
    verified[key] = current;
}
Bytes hash_source_inventory(const std::filesystem::path &directory,
                            const SourceProgress &progress) {
    auto files = inventory_files(directory);
    std::ostringstream out;
    out << "USUM_ORIGINAL 2\n";
    std::size_t completed = 0;
    for (auto &[name, file] : files) {
        if (progress)
            progress(completed, files.size(), name);
        file.hash = sha256_file(directory / name);
        require(std::filesystem::file_size(directory / name) == file.size &&
                    std::filesystem::last_write_time(directory / name).time_since_epoch().count() ==
                        file.time,
                "Original file changed while hashing: " + name);
        out << std::quoted(name) << ' ' << file.size << ' ' << file.time << ' ' << file.hash
            << '\n';
        ++completed;
    }
    const auto after = inventory_files(directory);
    require(after.size() == files.size(), "Original file list changed while hashing");
    for (const auto &[name, file] : files) {
        const auto found = after.find(name);
        require(found != after.end() && found->second.size == file.size &&
                    found->second.time == file.time,
                "Original dump changed while hashing: " + name);
    }
    if (progress)
        progress(completed, files.size(), {});
    const auto value = out.str();
    return Bytes(value.begin(), value.end());
}
Bytes verified_source_inventory(View inventory, const std::filesystem::path &directory, bool full,
                                const SourceProgress &progress) {
    const auto expected = parse(inventory), actual = inventory_files(directory);
    require(expected.size() == actual.size(), "Original dump file list changed");
    std::ostringstream out;
    out << "USUM_ORIGINAL 2\n";
    std::size_t completed = 0;
    for (const auto &[name, file] : expected) {
        require(actual.contains(name), "Original file is missing: " + name);
        if (progress)
            progress(completed, expected.size(), name);
        verify_source_content(directory / name, file.size, file.time, file.hash, full);
        const auto &now = actual.at(name);
        require(std::filesystem::file_size(directory / name) == now.size &&
                    std::filesystem::last_write_time(directory / name).time_since_epoch().count() ==
                        now.time,
                "Original file changed during verification: " + name);
        out << std::quoted(name) << ' ' << file.size << ' ' << now.time << ' ' << file.hash << '\n';
        ++completed;
    }
    if (progress)
        progress(completed, expected.size(), {});
    if (!hashed_source_inventory(inventory))
        return Bytes(inventory.begin(), inventory.end());
    const auto value = out.str();
    return Bytes(value.begin(), value.end());
}
void verify_source_inventory(View inventory, const std::filesystem::path &directory, bool full,
                             const SourceProgress &progress) {
    verified_source_inventory(inventory, directory, full, progress);
}
std::string source_inventory_identity(const std::filesystem::path &objects,
                                      const std::string &identity) {
    const auto marker = objects.parent_path() / "source-verification.usum";
    if (!std::filesystem::exists(marker))
        return identity;
    std::istringstream in(text(read_file(marker)));
    std::string tag, original, hashed;
    unsigned version;
    require(bool(in >> tag >> version >> original >> hashed) && tag == "USUM_SOURCE_VERIFICATION" &&
                version == 1 && hashed.size() == 64 &&
                hashed.find_first_not_of("0123456789abcdef") == std::string::npos,
            "Invalid source verification upgrade");
    return original == identity ? hashed : identity;
}
}
