#include "core/resource_source.h"
#include "core/digest.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cctype>
namespace studio {
namespace {
thread_local std::filesystem::path capture_objects;
std::atomic<unsigned> active_captures{0};
std::mutex pins_mutex;
std::vector<std::weak_ptr<ResourceMembers>> pins;
std::string resource_key(std::string value) {
#ifdef _WIN32
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
#endif
    return value;
}
void valid_hash(const std::string &hash) {
    require(hash.size() == 64 && hash.find_first_not_of("0123456789abcdef") == std::string::npos,
            "Invalid resource object identity");
}
void relative_path(const std::filesystem::path &path) {
    require(!path.empty() && !path.is_absolute() && !path.has_root_name(),
            "Invalid source resource path");
    for (auto &c : path)
        require(c != "..", "Source resource escapes project");
}
void validate_original(const std::filesystem::path &objects, const std::string &identity,
                       const std::filesystem::path &original,
                       const std::filesystem::path &relative) {
    using Inventory = std::map<std::string, std::pair<std::uintmax_t, long long>>;
    static std::mutex mutex;
    static std::map<std::filesystem::path, Inventory> cache;
    std::lock_guard lock(mutex);
    valid_hash(identity);
    auto object = (objects / identity).lexically_normal();
    auto found = cache.find(object);
    if (found == cache.end()) {
        auto data = resource_object(object);
        std::istringstream in(text(data));
        std::string tag, name;
        unsigned version;
        require(bool(in >> tag >> version) && tag == "USUM_ORIGINAL" && version == 1,
                "Invalid original dump inventory");
        Inventory entries;
        std::uintmax_t size;
        long long time;
        while (in >> std::quoted(name)) {
            require(bool(in >> size >> time), "Incomplete original dump inventory");
            entries[resource_key(name)] = {size, time};
        }
        require(in.eof(), "Malformed original dump inventory");
        found = cache.emplace(object, std::move(entries)).first;
    }
    auto file = found->second.find(resource_key(relative.generic_string()));
    if (file == found->second.end())
        return;
    auto path = original / relative;
    require(std::filesystem::is_regular_file(path) &&
                std::filesystem::file_size(path) == file->second.first &&
                std::filesystem::last_write_time(path).time_since_epoch().count() ==
                    file->second.second,
            "Original resource changed: " + relative.generic_string());
}
ResourceSource reference(const std::filesystem::path &path) {
    std::ifstream in(path);
    std::string tag, original, object;
    unsigned version;
    ResourceSource result;
    require(bool(in >> tag >> version >> std::quoted(original)) && tag == "USUM_ARCHIVE" &&
                version == 1,
            "Invalid staged archive reference");
    result.original = std::filesystem::u8path(original);
    std::size_t member;
    unsigned sub;
    while (in >> member) {
        require(bool(in >> sub >> std::quoted(object)) && sub < 32,
                "Invalid staged member reference");
        auto p = std::filesystem::u8path(object);
        valid_hash(p.filename().string());
        require(result.members.emplace(std::pair{member, sub}, p).second,
                "Duplicate staged member");
    }
    require(in.eof(), "Malformed staged archive reference");
    return result;
}
}
bool archive_reference(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    char tag[13]{};
    in.read(tag, 12);
    return std::string(tag, 12) == "USUM_ARCHIVE";
}
ResourceSource resource_source(const std::filesystem::path &path) {
    if (archive_reference(path))
        return reference(path);
    if (std::filesystem::is_regular_file(path))
        return {path, {}};
    auto absolute = std::filesystem::absolute(path).lexically_normal();
    for (auto directory = absolute.parent_path(); !directory.empty();) {
        auto marker = directory / "source.usum";
        if (std::filesystem::is_regular_file(marker)) {
            std::ifstream in(marker);
            std::string tag, original, objects, name, hash, identity;
            unsigned version;
            require(bool(in >> tag >> version >> std::quoted(original) >> std::quoted(objects) >>
                         identity) &&
                        tag == "USUM_SOURCE" && version == 1,
                    "Invalid project source descriptor");
            auto relative = absolute.lexically_relative(directory);
            relative_path(relative);
            validate_original(directory / std::filesystem::u8path(objects), identity,
                              std::filesystem::u8path(original), relative);
            ResourceSource result{std::filesystem::u8path(original) / relative, {}};
            int member, sub;
            while (in >> std::quoted(name)) {
                require(bool(in >> member >> sub >> hash) && member >= -1 && sub >= 0 && sub < 32,
                        "Invalid project source member");
                relative_path(std::filesystem::u8path(name));
                valid_hash(hash);
                if (resource_key(name) != resource_key(relative.generic_string()))
                    continue;
                auto object =
                    (directory / std::filesystem::u8path(objects) / hash).lexically_normal();
                if (member < 0)
                    result.original = object;
                else
                    require(result.members
                                .emplace(std::pair{std::size_t(member), unsigned(sub)}, object)
                                .second,
                            "Duplicate project source member");
            }
            require(in.eof(), "Malformed project source descriptor");
            return result;
        }
        auto parent = directory.parent_path();
        if (parent == directory)
            break;
        directory = parent;
    }
    return {path, {}};
}
std::filesystem::path resource_file_path(const std::filesystem::path &path) {
    auto source = resource_source(path);
    require(source.members.empty(),
            "Read this staged archive through its members: " + path.string());
    if (source.original != path && source.original.parent_path().filename() == "objects")
        resource_object(source.original);
    return source.original;
}
bool resource_exists(const std::filesystem::path &path) {
    return std::filesystem::is_regular_file(resource_source(path).original);
}
Bytes resource_object(const std::filesystem::path &path) {
    auto data = read_file(path);
    require(sha256(data) == path.filename().string(), "Damaged project member: " + path.string());
    return data;
}
std::shared_ptr<void> pin_resources(const ResourceMembers &members) {
    if (members.empty())
        return {};
    auto pin = std::make_shared<ResourceMembers>(members);
    std::lock_guard lock(pins_mutex);
    if (pins.size() > 1024)
        std::erase_if(pins, [](auto &p) {
            return p.expired();
        });
    pins.push_back(pin);
    return pin;
}
std::set<std::string> pinned_resource_objects(const std::filesystem::path &objects) {
    std::lock_guard lock(pins_mutex);
    std::set<std::string> result;
    std::erase_if(pins, [](auto &p) {
        return p.expired();
    });
    for (auto &weak : pins)
        if (auto pin = weak.lock())
            for (auto &[key, path] : *pin) {
                (void)key;
                if (path.parent_path() == objects)
                    result.insert(path.filename().string());
            }
    return result;
}
bool member_capture_active() {
    return active_captures.load() != 0;
}
MemberCapture::MemberCapture(const std::filesystem::path &objects) : previous_(capture_objects) {
    capture_objects = objects;
    ++active_captures;
}
MemberCapture::~MemberCapture() {
    capture_objects = previous_;
    --active_captures;
}
bool capture_archive(const std::filesystem::path &output, const ResourceSource &source,
                     const std::map<std::pair<std::size_t, unsigned>, Bytes> &replacements,
                     bool replace) {
    if (capture_objects.empty())
        return false;
    auto members = source.members;
    std::filesystem::create_directories(capture_objects);
    for (auto &[key, data] : replacements) {
        auto hash = sha256(data);
        auto path = capture_objects / hash;
        if (!std::filesystem::exists(path))
            write_file_atomic(path, data);
        else
            require(resource_object(path) == data, "Project object mismatch");
        members[key] = path;
    }
    std::ostringstream out;
    out << "USUM_ARCHIVE 1\n" << std::quoted(source.original.generic_string()) << '\n';
    for (auto &[key, path] : members)
        out << key.first << ' ' << key.second << ' ' << std::quoted(path.generic_string()) << '\n';
    auto value = out.str();
    Bytes data(value.begin(), value.end());
    if (replace)
        write_file_atomic(output, data);
    else
        write_new_file(output, data);
    return true;
}
}
