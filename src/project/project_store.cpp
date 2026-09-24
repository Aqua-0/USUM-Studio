#include "core/filesystem.h"
#include "project/project_store.h"
#include "core/digest.h"
#include "formats/archive.h"
#include "field/area.h"
#include "formats/compression.h"
#include "formats/container.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <optional>
#include <tuple>
#include <set>
#include <memory>
#include <ctime>
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace studio {
namespace {
std::filesystem::path game_file_relative(const std::filesystem::path &path) {
    if (path.empty())
        return {};
    auto part = path.begin();
    auto folder = part->string();
    std::transform(folder.begin(), folder.end(), folder.begin(), [](unsigned char c) {
        return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : char(c);
    });
    if (folder != "romfs" && folder != "exefs")
        return {};
    std::filesystem::path result(folder);
    for (++part; part != path.end(); ++part)
        result /= *part;
    return result;
}
bool equivalent_member(const Bytes &a, const Bytes &b) {
    if (a == b)
        return true;
    try {
        std::size_t used_a = 0, used_b = 0;
        const auto decoded_a = decompress(a, &used_a);
        const auto decoded_b = decompress(b, &used_b);
        return used_a == a.size() && used_b == b.size() && decoded_a == decoded_b;
    } catch (const std::runtime_error &) {
        // Raw records can start with the compression marker too.
        return false;
    }
}
std::optional<std::set<std::pair<std::size_t, unsigned>>>
changed_reference_members(const std::vector<std::filesystem::path> &paths) {
    std::filesystem::path original;
    std::set<std::pair<std::size_t, unsigned>> members;
    for (const auto &path : paths) {
        auto source = resource_source(path);
        auto resolved = std::filesystem::absolute(source.original).lexically_normal();
        if (!original.empty() && resolved != original)
            return std::nullopt;
        original = resolved;
        for (const auto &[member, replacement] : source.members)
            members.insert(member);
    }
    return members;
}
bool stages_composition(const std::string &document) {
    return document.find("\ngame_ground 1 ") != std::string::npos ||
           document.find("\ngame_objects 1 ") != std::string::npos;
}
}

namespace {
std::shared_ptr<void> project_lease(const std::filesystem::path &root) {
    static std::mutex mutex;
    static std::map<std::filesystem::path, std::weak_ptr<void>> leases;
    std::lock_guard lock(mutex);
    auto key = std::filesystem::weakly_canonical(root);
    if (auto lease = leases[key].lock())
        return lease;
#ifdef _WIN32
    auto handle = CreateFileW((key / "project.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(handle != INVALID_HANDLE_VALUE,
            "This project is open in another editor or operation. Close it before continuing.");
    auto lease = std::shared_ptr<void>(handle, [](void *h) {
        CloseHandle(h);
    });
#else
    auto fd = ::open((key / "project.lock").c_str(), O_CREAT | O_RDWR, 0600);
    require(fd >= 0, "Cannot open project lock");
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        require(false, "This project is open in another editor or operation");
    }
    auto lease = std::shared_ptr<void>(new int(fd), [](void *p) {
        auto fd = static_cast<int *>(p);
        flock(*fd, LOCK_UN);
        ::close(*fd);
        delete fd;
    });
#endif
    if (std::filesystem::is_directory(key / "objects"))
        for (auto &entry : std::filesystem::directory_iterator(key / "objects")) {
            auto name = entry.path().filename().string();
            if (entry.is_regular_file() && name.size() == 71 && name.substr(64) == ".saving" &&
                name.substr(0, 64).find_first_not_of("0123456789abcdef") == std::string::npos)
                std::filesystem::remove(entry.path());
        }
    leases[key] = lease;
    return lease;
}
Bytes bytes(const std::string &s) {
    return Bytes(s.begin(), s.end());
}
std::string string(View b) {
    return std::string(b.begin(), b.end());
}
void safe_relative(const std::filesystem::path &p) {
    require(!p.empty() && !p.has_root_name() && !p.has_root_directory(),
            "Project resource path must be relative");
    for (auto &c : p)
        require(c != "..", "Project resource path escapes its directory");
}
std::filesystem::path blob_path(const std::filesystem::path &root, const std::string &hash) {
    require(hash.size() == 64 && hash.find_first_not_of("0123456789abcdef") == std::string::npos,
            "Invalid project object identity");
    return root / "objects" / hash;
}
std::string put(const std::filesystem::path &root, View data) {
    auto hash = sha256(data);
    auto p = blob_path(root, hash);
    std::filesystem::create_directories(p.parent_path());
    if (!std::filesystem::exists(p))
        try {
            write_file_atomic(p, data);
        } catch (...) {
            if (!std::filesystem::exists(p) || sha256(read_file(p)) != hash)
                throw;
        }
    else
        require(sha256(read_file(p)) == hash, "Project object is damaged: " + hash);
    return hash;
}
Bytes get(const std::filesystem::path &root, const std::string &hash) {
    auto b = read_file(blob_path(root, hash));
    require(sha256(b) == hash, "Project object is damaged: " + hash);
    return b;
}
std::vector<ProjectChange> read_overlay(const std::filesystem::path &root,
                                        const std::string &hash) {
    std::vector<ProjectChange> out;
    if (hash.empty())
        return out;
    std::istringstream in(string(get(root, hash)));
    std::string tag;
    unsigned version;
    require(bool(in >> tag >> version) && tag == "USUM_OVERLAY" && version == 1,
            "Unsupported project overlay");
    ProjectChange c;
    while (in >> std::quoted(c.path)) {
        require(bool(in >> c.member >> c.subfile >> c.blob), "Incomplete project overlay");
        safe_relative(c.path);
        require(c.member >= -1 && c.subfile >= 0 && c.subfile < 32, "Invalid project member");
        out.push_back(c);
    }
    require(in.eof(), "Malformed project overlay");
    return out;
}
std::string write_overlay(const std::filesystem::path &root,
                          const std::vector<ProjectChange> &changes) {
    std::ostringstream out;
    out << "USUM_OVERLAY 1\n";
    for (auto &c : changes)
        out << std::quoted(c.path) << ' ' << c.member << ' ' << c.subfile << ' ' << c.blob << '\n';
    return put(root, bytes(out.str()));
}
void materialize(const std::filesystem::path &root, const std::filesystem::path &original,
                 const std::string &overlay, const std::filesystem::path &destination) {
    std::map<std::string, std::vector<ProjectChange>> files;
    for (auto &c : read_overlay(root, overlay))
        files[c.path].push_back(c);
    for (auto &[name, changes] : files) {
        auto out = destination / name;
        std::filesystem::create_directories(out.parent_path());
        if (changes[0].member < 0 && changes.size() == 1) {
            write_new_file(out, get(root, changes[0].blob));
        } else {
            Archive archive(changes[0].member < 0 ? blob_path(root, changes[0].blob)
                                                  : original / name);
            std::map<std::pair<std::size_t, unsigned>, Bytes> replacements;
            for (auto &c : changes)
                if (c.member >= 0)
                    replacements[{std::size_t(c.member), unsigned(c.subfile)}] = get(root, c.blob);
            archive.export_appended(out, replacements);
        }
    }
}
std::string stamp() {
    return std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count());
}
std::string revision_stamp() {
    auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::ostringstream out;
    std::tm utc{};
#ifdef _WIN32
    require(gmtime_s(&utc, &time) == 0, "Cannot format revision time");
#else
    require(gmtime_r(&time, &utc) != nullptr, "Cannot format revision time");
#endif
    out << std::put_time(&utc, "%Y-%m-%d %H:%M:%S UTC");
    return out.str();
}
bool inside(const std::filesystem::path &p, const std::filesystem::path &root) {
    auto rel = std::filesystem::weakly_canonical(p).lexically_relative(
        std::filesystem::weakly_canonical(root));
    return !rel.empty() && !rel.is_absolute() && *rel.begin() != "..";
}
Bytes original_inventory(const std::filesystem::path &original) {
    std::map<std::string, std::pair<std::uintmax_t, long long>> files;
    for (auto &entry : std::filesystem::recursive_directory_iterator(original)) {
        require(!entry.is_symlink(), "Original dump contains symbolic links");
        if (entry.is_regular_file())
            files[entry.path().lexically_relative(original).generic_string()] = {
                entry.file_size(), entry.last_write_time().time_since_epoch().count()};
    }
    std::ostringstream out;
    out << "USUM_ORIGINAL 1\n";
    for (auto &[name, identity] : files)
        out << std::quoted(name) << ' ' << identity.first << ' ' << identity.second << '\n';
    return bytes(out.str());
}
std::string settings_text(const ProjectStore &p) {
    std::ostringstream out;
    out << "USUM_PROJECT " << p.format_version << "\n"
        << std::quoted(p.original.generic_string()) << '\n'
        << std::quoted(p.original_identity) << '\n';
    if (p.format_version >= 4)
        out << game_target_id(p.target) << '\n';
    out << p.settings.autosave << ' ' << p.settings.auto_stage << ' ' << p.settings.save_seconds
        << ' ' << p.settings.stage_seconds << ' ' << p.settings.history << '\n'
        << std::quoted(p.current_state) << ' ' << std::quoted(p.staged_state) << ' '
        << std::quoted(p.current_overlay) << '\n';
    for (auto &r : p.revisions)
        out << std::quoted(r.state) << ' ' << std::quoted(r.overlay) << ' ' << std::quoted(r.label)
            << '\n';
    return out.str();
}
std::optional<Bytes> merge_static_additions(View baseline, View first, View second,
                                            const std::string &label) {
    if (baseline.size() < 60 || first.size() < 4 || second.size() < 4 || u32(baseline, 4) != 4)
        return {};
    const auto count = u32(baseline, 0);
    View moved = first, added = second;
    if (u32(second, 0) == count)
        std::swap(moved, added);
    if (moved.size() < baseline.size() || u32(moved, 0) != count || u32(added, 0) <= count)
        return {};
    const auto original_rows = read_placements(baseline), moved_rows = read_placements(moved),
               added_rows = read_placements(added);
    const auto shift = (added_rows.size() - original_rows.size()) * 56;
    require(added.size() >= baseline.size() + shift,
            "Static placement payload was removed: " + label);
    for (std::size_t i = 0; i < original_rows.size(); ++i) {
        if (u32(baseline, 4 + i * 56) != 4 || u32(moved, 4 + i * 56) != 4 ||
            u32(added, 4 + i * 56) != 4)
            return {};
        require((moved_rows[i].collision == original_rows[i].collision ||
                 (moved_rows[i].collision >= baseline.size() &&
                  moved_rows[i].collision < moved.size())) &&
                    added_rows[i].collision ==
                        (original_rows[i].collision ? original_rows[i].collision + shift : 0),
                "Competing changes to existing placement collision storage: " + label);
    }
    const auto table_end = 4 + original_rows.size() * 56;
    Bytes result(added.begin(), added.end());
    for (std::size_t i = 0; i < original_rows.size(); ++i)
        if (moved_rows[i].collision != original_rows[i].collision)
            put32(result, 4 + i * 56 + 52,
                  narrow(added.size() + moved_rows[i].collision - baseline.size()));
    append(result, slice(moved, baseline.size(), moved.size() - baseline.size()));
    for (std::size_t i = 4; i < baseline.size(); i += 4) {
        if (i < table_end && (i - 4) % 56 == 52)
            continue;
        const auto size = std::min(std::size_t(4), baseline.size() - i);
        const auto destination = i < table_end ? i : i + shift;
        const auto original = slice(baseline, i, size), change = slice(moved, i, size),
                   current = slice(added, destination, size);
        if (std::equal(original.begin(), original.end(), change.begin()))
            continue;
        require(std::equal(original.begin(), original.end(), current.begin()) ||
                    std::equal(change.begin(), change.end(), current.begin()),
                "Conflicting existing placement edits: " + label + " at byte " + std::to_string(i));
        std::copy(change.begin(), change.end(), result.begin() + std::ptrdiff_t(destination));
    }
    read_placements(result);
    return result;
}

std::optional<Bytes> merge_interaction_references(View base, View scripts, View placements,
                                                  unsigned size, unsigned script, unsigned type,
                                                  bool row_identity, const std::string &label) {
    if (base.size() < 4 || scripts.size() != base.size() || placements.size() < 4)
        return std::nullopt;
    auto count = u32(base, 0), next_count = u32(placements, 0);
    if (u32(scripts, 0) != count || count > (base.size() - 4) / size ||
        next_count > (placements.size() - 4) / size)
        return std::nullopt;
    auto unchanged = Bytes(scripts.begin(), scripts.end());
    std::vector<unsigned> changed;
    for (unsigned row = 0; row < count; ++row) {
        unsigned at = 4 + row * size;
        if (u32(base, at) != type)
            return std::nullopt;
        if (u32(base, at + script) != u32(scripts, at + script))
            changed.push_back(row);
        put32(unchanged, at + script, u32(base, at + script));
    }
    if (!std::equal(base.begin(), base.end(), unchanged.begin()))
        return std::nullopt;
    if (changed.empty())
        return Bytes(placements.begin(), placements.end());
    require(!row_identity || count == next_count,
            "Position-trigger rows changed while authoring scripts: " + label +
                ". Keep the authored trigger rows intact before staging.");
    std::map<unsigned, unsigned> rows;
    for (unsigned row = 0; row < next_count; ++row) {
        unsigned at = 4 + row * size;
        require(u32(placements, at) == type, "Unexpected placement type: " + label);
        require(rows.emplace(row_identity ? row : u32(placements, at + 44), row).second,
                "Duplicate interaction event ID: " + label);
    }
    Bytes out(placements.begin(), placements.end());
    for (auto row : changed) {
        unsigned at = 4 + row * size, identity = row_identity ? row : u32(base, at + 44);
        auto found = rows.find(identity);
        require(found != rows.end(),
                "An authored interaction target was deleted: " + label +
                    ". Remove its authored interaction or restore the placement before staging.");
        unsigned destination = 4 + found->second * size + script;
        auto old = u32(base, at + script), desired = u32(scripts, at + script),
             current = u32(out, destination);
        require(current == old || current == desired,
                "Conflicting interaction script assignments: " + label + " / target " +
                    std::to_string(identity));
        put32(out, destination, desired);
    }
    return out;
}
std::optional<Bytes> merge_interaction_groups(View base, View first, View second, unsigned category,
                                              const std::string &label, unsigned depth) {
    unsigned size, script, type;
    bool row_identity = false;
    if (category == TargetProfile::character_placement_pack) {
        size = 120;
        script = 56;
        type = 1;
    } else if (category == TargetProfile::interaction_placement_pack) {
        size = 60;
        script = 48;
        type = 2;
    } else if (category == TargetProfile::position_event_pack) {
        size = 60;
        script = 44;
        type = 0;
        row_identity = true;
    } else
        return std::nullopt;
    std::optional<Container> a, b, c;
    try {
        a = Container::parse(base);
        b = Container::parse(first);
        c = Container::parse(second);
    } catch (const std::exception &) {
        return std::nullopt;
    }
    if (a->tag != b->tag || a->tag != c->tag || a->files.size() != b->files.size() ||
        a->files.size() != c->files.size())
        return std::nullopt;
    for (unsigned zone = 0; zone < a->files.size(); ++zone) {
        auto &original = a->files[zone], &left = b->files[zone], &right = c->files[zone];
        auto name = label + " / zone " + std::to_string(zone);
        auto merged = merge_interaction_references(original, left, right, size, script, type,
                                                   row_identity, name);
        if (!merged)
            merged = merge_interaction_references(original, right, left, size, script, type,
                                                  row_identity, name);
        left = merged ? std::move(*merged)
                      : merge_project_resource(original, left, right, name, depth + 1);
    }
    return b->write();
}

}
Bytes merge_project_resource(View baseline, View first, View second, const std::string &label,
                             unsigned depth) {
    auto eq = [](View a, View b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
    };
    if (eq(first, second) || eq(second, baseline))
        return Bytes(first.begin(), first.end());
    if (eq(first, baseline))
        return Bytes(second.begin(), second.end());
    require(depth < 32, "Resource nesting is too deep: " + label);
    std::optional<Container> a, b, c;
    try {
        a = Container::parse(baseline);
        b = Container::parse(first);
        c = Container::parse(second);
    } catch (const std::exception &) {
    }
    if (a && b && c && a->tag == b->tag && a->tag == c->tag && a->files.size() == b->files.size() &&
        a->files.size() == c->files.size()) {
        for (std::size_t i = 0; i < a->files.size(); ++i) {
            const auto child = label + " / resource " + std::to_string(i);
            if (a->tag == std::array<std::uint8_t, 2>{'E', 'D'}) {
                auto merged = merge_interaction_groups(a->files[i], b->files[i], c->files[i],
                                                       unsigned(i), child, depth + 1);
                if (merged) {
                    b->files[i] = std::move(*merged);
                    continue;
                }
            }
            if (a->tag == std::array<std::uint8_t, 2>{'E', 'S'}) {
                auto merged = merge_static_additions(a->files[i], b->files[i], c->files[i], child);
                if (merged) {
                    b->files[i] = std::move(*merged);
                    continue;
                }
            }
            b->files[i] =
                merge_project_resource(a->files[i], b->files[i], c->files[i], child, depth + 1);
        }
        return b->write();
    }
    require(baseline.size() == first.size() && baseline.size() == second.size(),
            "Conflicting structural edits: " + label +
                ". Save the competing edits separately or reset one before staging.");
    Bytes out(first.begin(), first.end());
    for (std::size_t i = 0; i < out.size(); ++i)
        if (second[i] != baseline[i]) {
            require(first[i] == baseline[i] || first[i] == second[i],
                    "Conflicting edits: " + label + " at byte " + std::to_string(i));
            out[i] = second[i];
        }
    return out;
}
ProjectStore ProjectStore::create(const std::filesystem::path &directory,
                                  const std::filesystem::path &dump,
                                  const SourceProgress &progress) {
    ProjectStore p;
    p.root = std::filesystem::absolute(directory);
    p.original = std::filesystem::canonical(dump);
    p.target = detect_game_target(p.original);
    if (p.target == GameTarget::UltraSun)
        p.format_version = 4;
    require(std::filesystem::is_directory(p.original / "romfs"),
            "Choose a game dump containing romfs");
    require(!inside(p.root, p.original) && !inside(p.original, p.root),
            "Keep the project directory separate from the original dump");
    require(!std::filesystem::exists(p.root) || std::filesystem::is_empty(p.root),
            "Choose an empty project directory");
    std::filesystem::create_directories(p.root);
    p.lease_ = project_lease(p.root);
    p.original_identity = put(p.root, hash_source_inventory(p.original, progress));
    p.save();
    return p;
}
ProjectStore ProjectStore::open(const std::filesystem::path &directory, bool trust_legacy_source,
                                const SourceProgress &progress) {
    ProjectStore p;
    p.root = std::filesystem::absolute(directory);
    p.lease_ = project_lease(p.root);
    std::istringstream in(string(read_file(p.root / "project.usum")));
    std::string tag, path;
    unsigned version;
    require(bool(in >> tag >> version) && tag == "USUM_PROJECT" && (version >= 1 && version <= 5),
            "Unsupported editor project");
    require(bool(in >> std::quoted(path)), "Missing original dump");
    if (version >= 2)
        require(bool(in >> std::quoted(p.original_identity)), "Missing original identity");
    std::string target;
    if (version >= 4)
        require(bool(in >> target) && (target == "ultra-sun" || target == "ultra-moon"),
                "Invalid project game target");
    require(bool(in >> p.settings.autosave >> p.settings.auto_stage >> p.settings.save_seconds >>
                 p.settings.stage_seconds >> p.settings.history >> std::quoted(p.current_state) >>
                 std::quoted(p.staged_state) >> std::quoted(p.current_overlay)),
            "Incomplete project settings");
    p.original = path_from_utf8(path);
    require(std::filesystem::is_directory(p.original / "romfs"),
            "Original dump is unavailable: " + path);
    require(!inside(p.root, p.original) && !inside(p.original, p.root),
            "Project and original dump directories overlap");
    ProjectRevision r;
    while (in >> std::quoted(r.state)) {
        require(bool(in >> std::quoted(r.overlay) >> std::quoted(r.label)),
                "Incomplete project revision");
        p.revisions.push_back(r);
    }
    require(in.eof(), "Malformed project settings");
    require(p.settings.history >= 1 && p.settings.history <= 100 && p.settings.save_seconds >= 1 &&
                p.settings.stage_seconds >= 1,
            "Invalid project settings");
    p.target = detect_game_target(p.original);
    require(version < 4 || target == game_target_id(p.target),
            "Project game target differs from the original dump");
    if (version >= 2) {
        if (trust_legacy_source) {
            require(!p.uses_content_hashes(),
                    "This project already has content hashes. Its original content must match.");
            p.upgrade_source_verification(true, progress);
        } else {
            p.verify_original(false, progress);
        }
    }
    p.format_version = std::max(p.target == GameTarget::UltraSun ? 4u : 2u, version);
    p.read_state(p.current_state);
    if (version == 1)
        p.migrate();
    p.recover_export();
    p.finish_migration_cleanup();
    return p;
}
void ProjectStore::manifest() {
    write_file_atomic(root / "project.usum", bytes(settings_text(*this)));
}
std::string ProjectStore::state_text() const {
    std::ostringstream out;
    out << "USUM_PROJECT_STATE 1\n" << std::quoted(base) << '\n';
    for (auto &[key, e] : edits)
        out << std::quoted(key) << ' ' << std::quoted(e.kind) << ' ' << std::quoted(e.label) << ' '
            << std::quoted(e.parameters) << ' ' << e.blob << '\n';
    return out.str();
}
void ProjectStore::read_state(const std::string &hash) {
    std::istringstream in(string(get(root, hash)));
    std::string tag;
    unsigned version;
    require(bool(in >> tag >> version >> std::quoted(base)) && tag == "USUM_PROJECT_STATE" &&
                version == 1,
            "Unsupported project save");
    edits.clear();
    ProjectEdit e;
    while (in >> std::quoted(e.key)) {
        require(bool(in >> std::quoted(e.kind) >> std::quoted(e.label) >>
                     std::quoted(e.parameters) >> e.blob),
                "Incomplete saved edit");
        get(root, e.blob);
        require(edits.emplace(e.key, e).second, "Duplicate saved edit");
    }
    require(in.eof(), "Malformed saved edits");
}
void ProjectStore::save() {
    auto previous = current_state;
    current_state = put(root, bytes(state_text()));
    try {
        manifest();
    } catch (...) {
        current_state = previous;
        throw;
    }
}
void ProjectStore::save_settings() {
    require(settings.history >= 1 && settings.history <= 100 && settings.save_seconds >= 1 &&
                settings.stage_seconds >= 1,
            "Invalid project settings");
    if (revisions.size() > std::size_t(settings.history))
        revisions.resize(std::size_t(settings.history));
    manifest();
}
void ProjectStore::capture(ProjectEdit edit, View data) {
    if (edit.kind == "authored-conversations")
        format_version = std::max(format_version, 5u);
    if (edit.kind == "field-map" || edit.kind == "character-registration")
        format_version = std::max(format_version, 3u);
    edit.blob = put(root, data);
    edits[edit.key] = std::move(edit);
}
std::filesystem::path ProjectStore::document(const std::string &key) const {
    auto it = edits.find(key);
    return it == edits.end() ? std::filesystem::path{} : blob_path(root, it->second.blob);
}
bool ProjectStore::uses_content_hashes() const {
    return hashed_source_inventory(
        get(root, source_inventory_identity(root / "objects", original_identity)));
}
void ProjectStore::verify_original(bool full, const SourceProgress &progress) const {
    const auto identity = source_inventory_identity(root / "objects", original_identity);
    const auto verified = verified_source_inventory(get(root, identity), original, full, progress);
    const auto refreshed = put(root, verified);
    if (refreshed != identity) {
        const auto marker =
            "USUM_SOURCE_VERIFICATION 1\n" + original_identity + "\n" + refreshed + "\n";
        write_file_atomic(root / "source-verification.usum", bytes(marker));
    }
}
void ProjectStore::upgrade_source_verification(bool trust_current, const SourceProgress &progress) {
    require(!uses_content_hashes(), "This project already uses content hashes");
    if (!trust_current)
        verify_original();
    const auto hashed = put(root, hash_source_inventory(original, progress));
    const auto marker = "USUM_SOURCE_VERIFICATION 1\n" + original_identity + "\n" + hashed + "\n";
    write_file_atomic(root / "source-verification.usum", bytes(marker));
}
void ProjectStore::prepare_source() {
    source = source_for(base);
    for (auto &[key, e] : edits)
        if (e.kind == "composition" || e.kind == "authored-conversations") {
            std::istringstream in(e.parameters);
            unsigned area;
            std::string baseline;
            require(bool(in >> area >> std::quoted(baseline)), "Invalid composition baseline");
            source_for(baseline);
        }
}
std::filesystem::path ProjectStore::source_for(const std::string &overlay) {
    auto destination = root / "member-sources" / (overlay.empty() ? "original" : overlay);
    auto marker = destination / "source.usum";
    std::ostringstream descriptor;
    descriptor << "USUM_SOURCE 1\n"
               << std::quoted(original.generic_string()) << '\n'
               << std::quoted(std::string("../../objects")) << '\n'
               << original_identity << '\n';
    for (auto &c : read_overlay(root, overlay)) {
        get(root, c.blob);
        descriptor << std::quoted(c.path) << ' ' << c.member << ' ' << c.subfile << ' ' << c.blob
                   << '\n';
    }
    auto value = bytes(descriptor.str());
    if (std::filesystem::exists(marker)) {
        require(read_file(marker) == value, "Project source descriptor changed");
        return destination;
    }
    if (std::filesystem::exists(destination)) {
        for (auto &entry : std::filesystem::recursive_directory_iterator(destination))
            require(entry.is_directory(),
                    "Unrecognized partial project source; inspect before retrying");
    }
    std::filesystem::create_directories(destination / "romfs");
    std::filesystem::create_directories(destination / "exefs");
    write_file_atomic(marker, value);
    return destination;
}

std::filesystem::path ProjectStore::overlay_directory() const {
    return root / "exports";
}
ProjectBuild ProjectStore::build() const {
    ProjectBuild out{root, source, current_state, base, {}};
    for (auto &[key, e] : edits)
        out.edits.push_back(e);
    return out;
}
ProjectBuildResult ProjectStore::stage(const ProjectBuild &build, const ProjectExporter &exporter) {
    auto lease = project_lease(build.root);
    MemberCapture capture(build.root / "objects");
    auto scratch = build.root / "scratch" / stamp();
    std::filesystem::create_directories(scratch);
    write_new_file(scratch / "operation.usum", bytes("USUM_STAGE_OPERATION 1"));
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(p, e);
        }
    } cleanup{scratch};
    std::map<std::string, Bytes> files;
    std::map<std::string, std::map<std::pair<std::size_t, unsigned>, Bytes>> members;
    ProjectBuildResult result;
    result.state = build.state;
    require(std::count_if(build.edits.begin(), build.edits.end(),
                          [](const auto &edit) {
                              return edit.kind == "field-map";
                          }) <= 1,
            "Create and reload one map before allocating the next");
    require(std::count_if(build.edits.begin(), build.edits.end(), [](const auto &edit) {
                return edit.kind == "character-registration";
            }) <= 1, "Stage and reload one character registration before adding another");
    for (auto &edit : build.edits) {
        require(!edit.replay_staged, "Historical export context cannot stage a current edit");
        if (edit.kind == "field-map-created" || edit.kind == "character-registration-created" ||
            (edit.kind == "asset-library" || edit.kind == "studio-asset"))
            continue;
        if (edit.kind == "composition" || edit.kind == "authored-conversations") {
            std::istringstream in(edit.parameters);
            unsigned area;
            std::string baseline, previous;
            require(bool(in >> area >> std::quoted(baseline)), "Invalid composition source");
            in >> std::quoted(previous);
            if (edit.kind == "composition" &&
                !stages_composition(text(get(build.root, edit.blob))) && previous.empty()) {
                result.notes.push_back(
                    edit.label +
                    ": saved; ground and object staging are disabled in Authoring Review.");
                continue;
            }
        }
        auto output = scratch / sha256(bytes(edit.key));
        std::filesystem::create_directories(output);
        get(build.root, edit.blob);
        exporter(edit, build.source, blob_path(build.root, edit.blob), output);
        if (edit.kind == "composition" || edit.kind == "authored-conversations") {
            std::istringstream in(edit.parameters);
            unsigned area;
            std::string baseline, previous;
            require(bool(in >> area >> std::quoted(baseline)), "Invalid composition source");
            in >> std::quoted(previous);
            require(baseline.empty() ||
                        (baseline.size() == 64 &&
                         baseline.find_first_not_of("0123456789abcdef") == std::string::npos),
                    "Invalid composition baseline");
            auto pinned =
                build.root / "member-sources" / (baseline.empty() ? "original" : baseline);
            auto prior = output.parent_path() / (output.filename().string() + "-previous");
            std::filesystem::create_directories(prior);
            if (!previous.empty()) {
                auto old = edit;
                old.blob = previous;
                old.replay_staged = true;
                get(build.root, previous);
                exporter(old, build.source, blob_path(build.root, previous), prior);
            }
            std::set<std::filesystem::path> paths;
            for (auto folder : {output, prior})
                for (auto &entry : std::filesystem::recursive_directory_iterator(folder))
                    if (entry.is_regular_file())
                        paths.insert(entry.path().lexically_relative(folder));
            for (auto &path : paths) {
                safe_relative(path);
                require(*path.begin() == "romfs", "Unsupported composition export destination");
                Archive original(pinned / path), current(build.source / path);
                std::unique_ptr<Archive> before, after;
                if (std::filesystem::exists(prior / path))
                    before = std::make_unique<Archive>(prior / path);
                if (std::filesystem::exists(output / path))
                    after = std::make_unique<Archive>(output / path);
                require(current.size() >= original.size() &&
                            (!before || before->size() == original.size()) &&
                            (!after || after->size() == original.size()),
                        "Composition export cannot change archive allocation");
                std::map<std::pair<std::size_t, unsigned>, Bytes> changes;
                std::optional<std::set<std::pair<std::size_t, unsigned>>> candidates;
                if (edit.kind == "authored-conversations") {
                    std::vector<std::filesystem::path> sources{pinned / path};
                    if (before)
                        sources.push_back(prior / path);
                    if (after)
                        sources.push_back(output / path);
                    candidates = changed_reference_members(sources);
                }
                for (std::size_t i = 0; i < original.size(); ++i)
                    for (auto sub : original.subfiles(i)) {
                        if (candidates && !candidates->contains({i, sub}))
                            continue;
                        auto a = before ? before->raw(i, sub) : original.raw(i, sub),
                             b = after ? after->raw(i, sub) : original.raw(i, sub);
                        if (a == b)
                            continue;
                        auto c = current.raw(i, sub);
                        auto merged =
                            merge_project_resource(decompress(a), decompress(c), decompress(b),
                                                   edit.label + " / " + path.generic_string() +
                                                       " member " + std::to_string(i));
                        changes[{i, sub}] = merged == decompress(b)      ? b
                                            : merged == decompress(c)    ? c
                                            : !c.empty() && c[0] == 0x11 ? compress(merged)
                                                                         : merged;
                    }
                auto destination = output / path;
                std::filesystem::create_directories(destination.parent_path());
                if (std::filesystem::exists(destination))
                    std::filesystem::remove(destination);
                current.export_subfiles(destination, changes);
            }
        }
        for (auto &entry : std::filesystem::recursive_directory_iterator(output))
            if (entry.is_regular_file()) {
                auto relative = entry.path().lexically_relative(output);
                safe_relative(relative);
                if (*relative.begin() != "romfs" && *relative.begin() != "exefs")
                    continue;
                auto name = relative.generic_string();
                auto original = build.source / relative;
                require(resource_exists(original),
                        "New game file export needs a source mapping: " + name);
                std::ifstream header(entry.path(), std::ios::binary);
                char tag[4]{};
                header.read(tag, 4);
                if ((archive_reference(entry.path()) || std::string(tag, 4) == "GARC" ||
                     std::string(tag, 4) == "CRAG")) {
                    Archive before(original), after(entry.path());
                    auto candidates = edit.kind == "authored-conversations"
                                          ? changed_reference_members({original, entry.path()})
                                          : std::nullopt;
                    require(after.size() == before.size() ||
                                ((edit.kind == "field-map" || edit.kind == "character-registration") &&
                                 after.size() > before.size()),
                            "Archive member-count changes require an explicit project adapter: " +
                                name);
                    for (std::size_t i = 0; i < after.size(); ++i) {
                        if (i >= before.size()) {
                            for (auto sub : after.subfiles(i)) {
                                auto raw = after.raw(i, sub);
                                auto &changes = members[name];
                                auto key = std::pair{i, sub};
                                require(!changes.contains(key) || changes.at(key) == raw,
                                        "Conflicting new-member allocations: " + name);
                                changes[key] = std::move(raw);
                            }
                            continue;
                        }
                        require(before.subfiles(i) == after.subfiles(i),
                                "Archive language layout changed: " + name);
                        for (auto sub : before.subfiles(i)) {
                            if (candidates && !candidates->contains({i, sub}))
                                continue;
                            auto raw = after.raw(i, sub), base_raw = before.raw(i, sub);
                            if (raw == base_raw)
                                continue;
                            auto key = std::pair{i, sub};
                            auto &changes = members[name];
                            if (auto found = changes.find(key); found != changes.end()) {
                                auto merged = merge_project_resource(
                                    decompress(base_raw), decompress(found->second),
                                    decompress(raw), name + " / member " + std::to_string(i));
                                raw = merged == decompress(raw)                  ? raw
                                      : merged == decompress(found->second)      ? found->second
                                      : !base_raw.empty() && base_raw[0] == 0x11 ? compress(merged)
                                                                                 : merged;
                            }
                            changes[key] = std::move(raw);
                        }
                    }
                } else {
                    auto original_bytes = read_file(original), next = read_file(entry.path());
                    if (next == original_bytes)
                        continue;
                    if (files.contains(name))
                        next = merge_project_resource(original_bytes, files[name], next, name);
                    files[name] = std::move(next);
                }
            }
    }
    std::map<std::tuple<std::string, int, int>, ProjectChange> overlay;
    for (auto &c : read_overlay(build.root, build.base))
        overlay[{c.path, c.member, c.subfile}] = c;
    for (auto &[name, data] : files) {
        std::erase_if(overlay, [&](const auto &e) {
            return e.second.path == name;
        });
        overlay[{name, -1, 0}] = {name, -1, 0, put(build.root, data)};
    }
    for (auto &[name, changes] : members)
        for (auto &[key, data] : changes)
            overlay[{name, int(key.first), int(key.second)}] = {
                name, int(key.first), int(key.second), put(build.root, data)};
    auto project = ProjectStore::open(build.root);
    std::map<std::string, std::unique_ptr<Archive>> originals;
    std::erase_if(overlay, [&](const auto &entry) {
        const auto &c = entry.second;
        if (c.member < 0)
            return sha256(read_file(project.original / c.path)) == c.blob;
        if (overlay.contains({c.path, -1, 0}))
            return false;
        auto &archive = originals[c.path];
        if (!archive)
            archive = std::make_unique<Archive>(project.original / c.path);
        if (std::size_t(c.member) >= archive->size())
            return false;
        return sha256(archive->raw(std::size_t(c.member), unsigned(c.subfile))) == c.blob;
    });
    std::vector<ProjectChange> changes;
    for (auto &[key, c] : overlay)
        changes.push_back(c);
    result.overlay = write_overlay(build.root, changes);
    result.directory = project.source_for(result.overlay);
    return result;
}
void ProjectStore::accept(const ProjectBuildResult &result) {
    if (result.state == staged_state && result.overlay == current_overlay)
        return;
    auto old = *this;
    staged_state = result.state;
    current_overlay = result.overlay;
    revisions.insert(revisions.begin(), {result.state, result.overlay, revision_stamp()});
    if (revisions.size() > std::size_t(settings.history))
        revisions.resize(std::size_t(settings.history));
    try {
        manifest();
    } catch (...) {
        *this = std::move(old);
        throw;
    }
}

void ProjectStore::restore(std::size_t index) {
    auto r = revisions.at(index);
    auto old = *this;
    try {
        read_state(r.state);
        current_state = r.state;
        staged_state = r.state;
        current_overlay = r.overlay;
        source_for(r.overlay);
        manifest();
    } catch (...) {
        *this = std::move(old);
        throw;
    }
}
void ProjectStore::reset(const std::string &key) {
    require(
        !edits.contains(key) || (edits.at(key).kind != "field-map-created" &&
                                 edits.at(key).kind != "character-registration-created"),
        "Created resources have dependencies. Restore a pre-creation history version instead.");
    auto old = edits;
    edits.erase(key);
    try {
        save();
    } catch (...) {
        edits = std::move(old);
        throw;
    }
}
std::vector<ProjectChange> ProjectStore::changes() const {
    return read_overlay(root, current_overlay);
}
void ProjectStore::advance() {
    require(!unstaged(), "Stage saved changes before reloading the overlay");
    if (current_overlay.empty() || base == current_overlay)
        return;
    auto old = *this;
    try {
        base = current_overlay;
        for (auto &[key, edit] : edits)
            if (edit.kind == "composition" || edit.kind == "authored-conversations") {
                std::istringstream in(edit.parameters);
                unsigned area;
                std::string baseline;
                require(bool(in >> area >> std::quoted(baseline)), "Invalid composition source");
                std::ostringstream parameters;
                parameters << area << ' ' << std::quoted(baseline) << ' '
                           << std::quoted((edit.kind == "authored-conversations" ||
                                           stages_composition(text(get(root, edit.blob))))
                                              ? edit.blob
                                              : std::string{});
                edit.parameters = parameters.str();
            }
        for (auto &[key, edit] : edits)
            if (edit.kind == "field-map")
                edit.kind = "field-map-created";
            else if (edit.kind == "character-registration")
                edit.kind = "character-registration-created";
        std::erase_if(edits, [](const auto &e) {
            return e.second.kind != "composition" && e.second.kind != "authored-conversations" &&
                   e.second.kind != "field-map-created" &&
                   e.second.kind != "character-registration-created" &&
                   e.second.kind != "asset-library" && e.second.kind != "studio-asset";
        });
        current_state = put(root, bytes(state_text()));
        staged_state = current_state;
        manifest();
    } catch (...) {
        *this = std::move(old);
        throw;
    }
}

void ProjectStore::import_file(const std::filesystem::path &relative,
                               const std::filesystem::path &file, bool allow_append) {
    safe_relative(relative);
    require(std::all_of(edits.begin(), edits.end(),
                        [](const auto &e) {
                            return e.second.kind == "composition" ||
                                   (e.second.kind == "asset-library" || e.second.kind == "studio-asset") ||
                                   e.second.kind == "field-map-created" ||
                                   e.second.kind == "character-registration-created";
                        }),
            "Stage and reload saved edits before changing project source archives");
    require(relative.generic_string() != TargetProfile::character_archive ||
                std::none_of(edits.begin(), edits.end(), [](const auto &entry) {
                    return entry.second.kind == "character-registration-created";
                }), "Use character registration to extend this archive. Restore project history "
                    "to replace registered characters and their dependencies together.");
    require(std::filesystem::is_regular_file(original / relative),
            "This resource is not part of the selected dump");
    require(relative.generic_string() != TargetProfile::zone_archive ||
                std::none_of(edits.begin(), edits.end(),
                             [](const auto &entry) {
                                 return entry.second.kind == "field-map-created";
                             }),
            "Zone registrations for created maps cannot be replaced by an imported archive");
    Archive validation(file), original_archive(original / relative);
    Archive current(source / relative);
    if (allow_append) {
        require(validation.size() > current.size(),
                "The bundle must add archive members without removing existing additions");
        for (std::size_t i = 0; i < current.size(); ++i)
            require(validation.subfiles(i) == current.subfiles(i),
                    "Bundle changed an existing archive language layout");
    } else {
        require(current.size() == original_archive.size(),
                "This archive contains added resources. Importing an original-layout archive would "
                "remove them.");
        require(
            validation.layout_identity() == original_archive.layout_identity(),
            "Imported archive changes unsupported allocation, language layout or header settings");
    }
    auto changes = read_overlay(root, base);
    std::erase_if(changes, [&](const auto &c) {
        return c.path == relative.generic_string();
    });
    for (std::size_t member = 0; member < validation.size(); ++member)
        for (auto sub : validation.subfiles(member)) {
            auto data = validation.raw(member, sub);
            if (member >= original_archive.size() || data != original_archive.raw(member, sub))
                changes.push_back(
                    {relative.generic_string(), int(member), int(sub), put(root, data)});
        }
    auto next = write_overlay(root, changes);
    auto prepared = source_for(next);
    auto previous = *this;
    try {
        if (allow_append)
            format_version = std::max(format_version, 3u);
        base = next;
        source = prepared;
        save();
    } catch (...) {
        *this = std::move(previous);
        throw;
    }
}
namespace {
void validate_external_directory(const ProjectStore &project,
                                 const std::filesystem::path &directory) {
    require(!inside(directory, project.original) && !inside(project.original, directory),
            "Use a separate dump. Never edit USUMStudio's original dump in pk3DS.");
    require(!inside(directory, project.root) && !inside(project.root, directory),
            "Keep the external dump outside the Studio project folder.");
    require(std::filesystem::is_directory(directory / "romfs"),
            "Choose the dump folder containing romfs, not the romfs folder itself.");
    for (const auto &entry : std::filesystem::recursive_directory_iterator(directory)) {
        require(!entry.is_symlink(),
                "External dump contains a symbolic link: " + entry.path().string());
        if (!entry.is_regular_file())
            continue;
        const auto source = project.original / entry.path().lexically_relative(directory);
        require(!std::filesystem::exists(source) ||
                    !std::filesystem::equivalent(source, entry.path()),
                "External dump shares a file with the original. Use independent copies, not hard "
                "links.");
    }
    require(detect_game_target(directory) == project.target,
            "External dump must use the same game as this project.");
}
bool same_disk_file(const std::filesystem::path &a, const std::filesystem::path &b) {
    if (std::filesystem::file_size(a) != std::filesystem::file_size(b))
        return false;
    std::ifstream left(a, std::ios::binary), right(b, std::ios::binary);
    require(bool(left) && bool(right), "Cannot read external comparison files");
    std::array<char, 65536> x{}, y{};
    do {
        left.read(x.data(), x.size());
        right.read(y.data(), y.size());
        if (left.gcount() != right.gcount() ||
            !std::equal(x.begin(), x.begin() + left.gcount(), y.begin()))
            return false;
    } while (left.gcount());
    require(!left.bad() && !right.bad(), "External comparison read failed");
    return true;
}
bool is_game_archive(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    std::array<char, 4> magic{};
    input.read(magic.data(), magic.size());
    return magic == std::array<char, 4>{'C', 'R', 'A', 'G'};
}
void replace_overlay_change(std::vector<ProjectChange> &changes, const ProjectChange &change) {
    std::erase_if(changes, [&](const auto &existing) {
        return existing.path == change.path && existing.member == change.member &&
               existing.subfile == change.subfile;
    });
    changes.push_back(change);
}
void write_external_connection(const std::filesystem::path &root,
                               const ExternalEditConnection &connection) {
    std::ostringstream out;
    out << "USUM_EXTERNAL_EDITING 1\n"
        << std::quoted(connection.directory.generic_string()) << '\n'
        << std::quoted(connection.baseline) << '\n';
    write_file_atomic(root / "external-editing.usum", bytes(out.str()));
}
}
ExternalEditConnection ProjectStore::external_connection() const {
    auto file = root / "external-editing.usum";
    if (!std::filesystem::exists(file))
        return {};
    std::istringstream in(text(read_file(file)));
    std::string tag, directory;
    unsigned version;
    ExternalEditConnection result;
    require(bool(in >> tag >> version >> std::quoted(directory) >> std::quoted(result.baseline)) &&
                tag == "USUM_EXTERNAL_EDITING" && version == 1,
            "Invalid external editing connection");
    result.directory = path_from_utf8(directory);
    if (!result.baseline.empty())
        read_overlay(root, result.baseline);
    return result;
}
void ProjectStore::connect_external(const std::filesystem::path &directory, bool create_copy) {
    require(!unstaged(), "Stage and reload project changes before setting up external editing.");
    verify_original();
    auto destination = std::filesystem::absolute(directory).lexically_normal();
    require(!inside(destination, original) && !inside(original, destination) &&
                !inside(destination, root) && !inside(root, destination),
            "Choose an independent folder outside the original dump and Studio project.");
    if (create_copy) {
        require(!std::filesystem::exists(destination) || std::filesystem::is_empty(destination),
                "Choose a new or empty folder for the external dump.");
        std::uintmax_t required_space = 0;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(original))
            if (entry.is_regular_file())
                required_space += entry.file_size();
        auto parent = destination;
        while (!std::filesystem::exists(parent))
            parent = parent.parent_path();
        require(std::filesystem::space(parent).available >= required_space,
                "Not enough free space for a full external dump copy.");
        std::filesystem::create_directories(destination);
        for (const auto &entry : std::filesystem::recursive_directory_iterator(original)) {
            require(!entry.is_symlink(), "Original dump contains symbolic links");
            const auto output = destination / entry.path().lexically_relative(original);
            if (entry.is_directory())
                std::filesystem::create_directories(output);
            else if (entry.is_regular_file()) {
                std::filesystem::create_directories(output.parent_path());
                std::filesystem::copy_file(entry.path(), output);
            }
        }
        std::map<std::string, std::vector<ProjectChange>> changed;
        for (const auto &change : read_overlay(root, current_overlay))
            changed[change.path].push_back(change);
        for (const auto &[name, changes] : changed) {
            auto output = destination / name;
            std::filesystem::create_directories(output.parent_path());
            if (changes.front().member < 0 && changes.size() == 1) {
                write_file_atomic(output, get(root, changes.front().blob));
            } else {
                Archive archive(changes.front().member < 0 ? blob_path(root, changes.front().blob)
                                                           : original / name);
                std::map<std::pair<std::size_t, unsigned>, Bytes> replacements;
                for (const auto &change : changes)
                    if (change.member >= 0)
                        replacements[{std::size_t(change.member), unsigned(change.subfile)}] =
                            get(root, change.blob);
                archive.export_appended(output, replacements, true);
            }
        }
    }
    validate_external_directory(*this, destination);
    const auto previous = external_connection();
    const auto baseline = create_copy                         ? current_overlay
                          : previous.directory == destination ? previous.baseline
                                                              : std::string{};
    write_external_connection(root, {destination, baseline});
}
ExternalEditReview ProjectStore::scan_external() {
    require(!unstaged(), "Stage and reload project changes before reviewing external edits.");
    const auto connection = external_connection();
    require(!connection.directory.empty(), "Connect an external dump first.");
    validate_external_directory(*this, connection.directory);
    verify_original();
    ExternalEditReview review{
        current_state, current_overlay, connection.baseline, connection.directory, {}, {}, {}};
    const auto baseline_source = source_for(connection.baseline);
    const auto current_source = source_for(current_overlay);
    auto compare = [&](const std::string &name, int member, unsigned subfile, const Bytes &before,
                       const Bytes &current, const Bytes &incoming) {
        if (incoming == before)
            return;
        auto equivalent = [member](const Bytes &a, const Bytes &b) {
            return member < 0 ? a == b : equivalent_member(a, b);
        };
        if (equivalent(incoming, before))
            return;
        const bool conflict = !equivalent(current, before) && !equivalent(current, incoming);
        review.changes.push_back(
            {{name, member, int(subfile), put(root, incoming)}, conflict, conflict ? 2 : 0});
    };
    for (const auto &entry : std::filesystem::recursive_directory_iterator(original)) {
        if (!entry.is_regular_file())
            continue;
        const auto relative = game_file_relative(entry.path().lexically_relative(original));
        if (relative.empty())
            continue;
        const auto name = relative.generic_string();
        const auto external = connection.directory / relative;
        if (!std::filesystem::is_regular_file(external)) {
            review.notes.push_back(name + ": missing externally; deletion is not imported.");
            continue;
        }
        const auto before_path = baseline_source / relative,
                   current_path = current_source / relative;
        if (resource_source(before_path).members.empty() &&
            same_disk_file(resource_file_path(before_path), external))
            continue;
        if (is_game_archive(entry.path())) {
            try {
                Archive before(before_path), current(current_path), incoming(external);
                require(before.size() == incoming.size() && before.size() == current.size(),
                        "Archive member count changed; resource additions/removals need a "
                        "dedicated importer.");
                for (std::size_t member = 0; member < before.size(); ++member)
                    require(before.subfiles(member) == incoming.subfiles(member) &&
                                before.subfiles(member) == current.subfiles(member),
                            "Archive language/subfile layout changed.");
                const auto start = review.changes.size();
                try {
                    for (std::size_t member = 0; member < before.size(); ++member) {
                        if (name == GameProfile::personal_archive && member + 1 == before.size()) {
                            require(before.size() > 1 &&
                                        before.subfiles(member) == std::vector<unsigned>{0} &&
                                        before.decoded(member).size() ==
                                            before.decoded(0).size() * member,
                                    "Unrecognized Pokémon personal summary table.");
                            continue;
                        }
                        for (const auto subfile : before.subfiles(member))
                            compare(name, int(member), subfile, before.raw(member, subfile),
                                    current.raw(member, subfile), incoming.raw(member, subfile));
                    }
                } catch (...) {
                    review.changes.resize(start);
                    throw;
                }
            } catch (const std::exception &e) {
                review.notes.push_back(name + ": " + e.what());
            }
        } else {
            compare(name, -1, 0, read_file(resource_file_path(before_path)),
                    read_file(resource_file_path(current_path)), read_file(external));
        }
    }
    for (const auto &entry : std::filesystem::recursive_directory_iterator(connection.directory)) {
        if (!entry.is_regular_file())
            continue;
        const auto relative =
            game_file_relative(entry.path().lexically_relative(connection.directory));
        if (!relative.empty() && !std::filesystem::exists(original / relative) &&
            relative.extension() != ".json")
            review.notes.push_back(relative.generic_string() + ": new file; not imported.");
    }
    for (auto &change : review.changes) {
        if (change.incoming.path != TargetProfile::trainer_records_archive)
            continue;
        auto paired =
            std::find_if(review.changes.begin(), review.changes.end(), [&](const auto &other) {
                return other.incoming.path == TargetProfile::trainer_teams_archive &&
                       other.incoming.member == change.incoming.member &&
                       other.incoming.subfile == change.incoming.subfile;
            });
        if (paired != review.changes.end() && (change.conflict || paired->conflict)) {
            change.conflict = paired->conflict = true;
            change.choice = paired->choice = 2;
        }
    }
    ResourceMembers resources;
    for (std::size_t i = 0; i < review.changes.size(); ++i)
        resources[{i, 0}] = blob_path(root, review.changes[i].incoming.blob);
    review.resources = pin_resources(resources);
    return review;
}
std::size_t ProjectStore::update_external() {
    const auto review = scan_external();
    require(review.changes.empty() && review.notes.empty(),
            "Review and import or acknowledge external edits before sending Studio changes. "
            "Missing files and unsupported archive layouts must be resolved first.");
    const auto connection = external_connection();
    const auto current = source_for(current_overlay);
    std::set<std::string> files;
    for (const auto &change : read_overlay(root, current_overlay))
        files.insert(change.path);
    for (const auto &change : read_overlay(root, connection.baseline))
        files.insert(change.path);
    std::vector<std::string> changed;
    for (const auto &name : files) {
        const auto output = connection.directory / name;
        if (is_game_archive(original / name)) {
            Archive wanted(current / name), external(output);
            bool equal = wanted.size() == external.size();
            for (std::size_t member = 0; equal && member < wanted.size(); ++member) {
                equal = wanted.subfiles(member) == external.subfiles(member);
                for (const auto subfile : wanted.subfiles(member)) {
                    if (!equal)
                        break;
                    equal = equivalent_member(wanted.raw(member, subfile),
                                              external.raw(member, subfile));
                }
            }
            if (!equal)
                changed.push_back(name);
        } else if (!same_disk_file(resource_file_path(current / name), output)) {
            changed.push_back(name);
        }
    }
    // Validate every file before writing any of them. pk3DS must be closed during this operation.
    validate_external_directory(*this, connection.directory);
    for (const auto &name : changed) {
        const auto output = connection.directory / name;
        if (is_game_archive(original / name))
            Archive(current / name).export_appended(output, {}, true);
        else
            write_file_atomic(output, read_file(resource_file_path(current / name)));
    }
    write_external_connection(root, {connection.directory, current_overlay});
    return changed.size();
}
void ProjectStore::import_external(const ExternalEditReview &review) {
    require(!unstaged() && current_state == review.state && current_overlay == review.overlay,
            "Project changed since the review. Scan external edits again.");
    const auto connection = external_connection();
    require(connection.directory == review.directory && connection.baseline == review.baseline,
            "External connection changed. Scan again.");
    validate_external_directory(*this, connection.directory);
    verify_original();
    for (const auto &change : review.changes) {
        if (change.incoming.path != TargetProfile::trainer_records_archive)
            continue;
        const auto paired =
            std::find_if(review.changes.begin(), review.changes.end(), [&](const auto &other) {
                return other.incoming.path == TargetProfile::trainer_teams_archive &&
                       other.incoming.member == change.incoming.member &&
                       other.incoming.subfile == change.incoming.subfile;
            });
        require(paired == review.changes.end() || paired->choice == change.choice,
                "Trainer settings and team changes for the same trainer must use the same import "
                "choice.");
    }
    auto merged = read_overlay(root, current_overlay);
    auto baseline = read_overlay(root, connection.baseline);
    for (const auto &change : review.changes) {
        require(change.choice >= 0 && change.choice <= 2, "Invalid external edit choice");
        require(!change.conflict || change.choice != 2,
                "Resolve each conflict by choosing Import or Keep project.");
        if (change.choice == 2)
            continue;
        const auto &incoming = change.incoming;
        safe_relative(incoming.path);
        const auto file = connection.directory / incoming.path;
        const auto now = incoming.member < 0 ? read_file(file)
                                             : Archive(file).raw(std::size_t(incoming.member),
                                                                 unsigned(incoming.subfile));
        require(sha256(now) == incoming.blob,
                "External files changed during review. Close the pk3DS editor and scan again.");
        if (change.choice == 0)
            replace_overlay_change(merged, incoming);
        replace_overlay_change(baseline, incoming);
    }
    const bool personal_changed =
        std::any_of(review.changes.begin(), review.changes.end(), [](const auto &change) {
            return change.choice != 2 && change.incoming.path == GameProfile::personal_archive;
        });
    if (personal_changed) {
        auto rebuild_summary = [&](std::vector<ProjectChange> &changes) {
            Archive personal(source_for(write_overlay(root, changes)) /
                             GameProfile::personal_archive);
            require(personal.size() > 1, "Missing Pokémon personal records");
            const auto last = personal.size() - 1;
            const auto record_size = personal.decoded(0).size();
            const auto summary = personal.decoded(last);
            require(record_size && summary.size() == record_size * last,
                    "Unrecognized Pokémon personal summary table");
            Bytes data;
            data.reserve(summary.size());
            for (std::size_t i = 0; i < last; ++i) {
                const auto record = personal.decoded(i);
                require(personal.subfiles(i) == std::vector<unsigned>{0} &&
                            record.size() == record_size,
                        "Inconsistent Pokémon personal record layout");
                append(data, record);
            }
            if (personal.raw(last) != summary)
                data = compress(data);
            replace_overlay_change(changes,
                                   {GameProfile::personal_archive, int(last), 0, put(root, data)});
        };
        rebuild_summary(merged);
        rebuild_summary(baseline);
    }
    const auto next = write_overlay(root, merged);
    const auto next_baseline = write_overlay(root, baseline);
    const auto prepared = source_for(next);
    auto previous = *this;
    try {
        base = current_overlay = next;
        source = prepared;
        current_state = put(root, bytes(state_text()));
        staged_state = current_state;
        revisions.insert(revisions.begin(),
                         {current_state, current_overlay, "Imported external edits"});
        if (revisions.size() > std::size_t(settings.history))
            revisions.resize(std::size_t(settings.history));
        manifest();
    } catch (...) {
        *this = std::move(previous);
        throw;
    }
    // If this write fails, repeating the review is safe: the project already contains the import.
    write_external_connection(root, {connection.directory, next_baseline});
}

void ProjectStore::recover_export() {
    auto journal = root / "export-transaction.usum";
    if (!std::filesystem::exists(journal))
        return;
    require(text(read_file(journal)) == "USUM_EXPORT_TRANSACTION 1",
            "Invalid export recovery journal");
    auto destination = overlay_directory(), previous = root / "scratch" / "export-previous",
         next = root / "scratch" / "export-next";
    if (!std::filesystem::exists(destination) && std::filesystem::exists(previous))
        std::filesystem::rename(previous, destination);
    if (std::filesystem::exists(destination))
        require(std::filesystem::is_regular_file(destination / "export.usum"),
                "Export recovery requires inspection: missing export identity");
    std::filesystem::remove_all(next);
    std::filesystem::remove_all(previous);
    std::filesystem::remove(journal);
}
void ProjectStore::build_export() {
    require(!current_overlay.empty(), "Stage changes before building a game export");
    recover_export();
    auto next = root / "scratch" / "export-next", previous = root / "scratch" / "export-previous",
         destination = overlay_directory();
    require(!std::filesystem::exists(next) && !std::filesystem::exists(previous),
            "Unrecognized export scratch data needs inspection");
    write_new_file(root / "export-transaction.usum", bytes("USUM_EXPORT_TRANSACTION 1"));
    try {
        std::filesystem::create_directories(next);
        materialize(root, original, current_overlay, next);
        write_new_file(next / "export.usum", bytes(current_overlay));
        if (std::filesystem::exists(destination)) {
            require(std::filesystem::is_regular_file(destination / "export.usum"),
                    "The exports directory contains unmanaged files; choose a separate project");
            std::filesystem::rename(destination, previous);
        }
        std::filesystem::rename(next, destination);
        recover_export();
    } catch (...) {
        recover_export();
        throw;
    }
}
bool ProjectStore::export_current() const {
    return std::filesystem::exists(overlay_directory() / "export.usum") &&
           text(read_file(overlay_directory() / "export.usum")) == current_overlay;
}
void ProjectStore::finish_migration_cleanup() {
    auto journal = root / "migration" / "cleanup.usum";
    if (!std::filesystem::exists(journal))
        return;
    std::istringstream in(text(read_file(journal)));
    std::string tag, name;
    unsigned version;
    require(bool(in >> tag >> version) && tag == "USUM_CLEANUP" && version == 1,
            "Invalid migration cleanup journal");
    std::uintmax_t size;
    long long modified;
    std::set<std::filesystem::path> directories;
    while (in >> std::quoted(name)) {
        require(bool(in >> size >> modified), "Incomplete migration cleanup journal");
        auto relative = path_from_utf8(name);
        safe_relative(relative);
        require(*relative.begin() == "sources" || *relative.begin() == "overlay",
                "Invalid migration cleanup destination");
        auto file = root / relative;
        if (std::filesystem::is_regular_file(file) && std::filesystem::file_size(file) == size &&
            std::filesystem::last_write_time(file).time_since_epoch().count() == modified)
            std::filesystem::remove(file);
        for (auto parent = file.parent_path(); parent != root && !parent.empty();
             parent = parent.parent_path())
            directories.insert(parent);
    }
    require(in.eof(), "Malformed migration cleanup journal");
    std::vector<std::filesystem::path> ordered(directories.begin(), directories.end());
    std::sort(ordered.begin(), ordered.end(), [](auto &a, auto &b) {
        return a.native().size() > b.native().size();
    });
    for (auto &directory : ordered)
        if (std::filesystem::is_directory(directory) && std::filesystem::is_empty(directory))
            std::filesystem::remove(directory);
    std::filesystem::remove(journal);
}
void ProjectStore::migrate() {
    auto legacy = read_file(root / "project.usum");
    std::map<std::string, std::string> overlays, states;
    auto convert_overlay = [&](const std::string &hash) {
        if (hash.empty())
            return hash;
        if (overlays.contains(hash))
            return overlays.at(hash);
        auto changes = read_overlay(root, hash);
        std::map<std::tuple<std::string, int, int>, ProjectChange> converted;
        for (auto &c : changes) {
            auto data = get(root, c.blob);
            if (c.member < 0 && data.size() >= 4 && text(slice(data, 0, 4)) == "CRAG") {
                Archive original_archive(original / c.path), imported(blob_path(root, c.blob));
                require(original_archive.layout_identity() == imported.layout_identity(),
                        "Cannot migrate imported archive layout: " + c.path);
                for (std::size_t member = 0; member < imported.size(); ++member)
                    for (auto sub : imported.subfiles(member)) {
                        auto raw = imported.raw(member, sub);
                        if (raw != original_archive.raw(member, sub)) {
                            ProjectChange replacement{c.path, int(member), int(sub),
                                                      put(root, raw)};
                            converted[{c.path, int(member), int(sub)}] = replacement;
                        }
                    }
            } else
                converted[{c.path, c.member, c.subfile}] = c;
        }
        std::vector<ProjectChange> result;
        for (auto &[key, c] : converted)
            result.push_back(c);
        auto next = write_overlay(root, result);
        overlays[hash] = next;
        return next;
    };
    auto convert_state = [&](const std::string &hash) {
        if (hash.empty())
            return hash;
        if (states.contains(hash))
            return states.at(hash);
        ProjectStore copy;
        copy.root = root;
        copy.read_state(hash);
        copy.base = convert_overlay(copy.base);
        for (auto &[key, e] : copy.edits)
            if (e.kind == "composition" || e.kind == "authored-conversations") {
                std::istringstream in(e.parameters);
                unsigned area;
                std::string baseline, previous;
                require(bool(in >> area >> std::quoted(baseline)),
                        "Invalid legacy composition baseline");
                in >> std::quoted(previous);
                if (!previous.empty())
                    get(root, previous);
                std::ostringstream out;
                out << area << ' ' << std::quoted(convert_overlay(baseline)) << ' '
                    << std::quoted(previous);
                e.parameters = out.str();
            }
        for (auto &[key, e] : copy.edits)
            if (e.kind == "original") {
                std::istringstream in(e.parameters);
                std::string path;
                int member, sub;
                require(bool(in >> std::quoted(path) >> member >> sub), "Invalid legacy reset");
                auto data = get(root, e.blob);
                if (member < 0 && data.size() >= 4 && text(slice(data, 0, 4)) == "CRAG") {
                    Archive reset(blob_path(root, e.blob)), original_archive(original / path);
                    require(reset.layout_identity() == original_archive.layout_identity(),
                            "Legacy reset archive metadata differs from original");
                    for (std::size_t i = 0; i < reset.size(); ++i)
                        for (auto language : reset.subfiles(i))
                            require(reset.raw(i, language) == original_archive.raw(i, language),
                                    "Legacy archive reset refers to a different original dump");
                    e.kind = "original-archive";
                    std::ostringstream parameters;
                    parameters << std::quoted(path);
                    e.parameters = parameters.str();
                    e.blob = put(root, bytes("USUM_RESET_ARCHIVE 1"));
                }
            }
        auto next = put(root, bytes(copy.state_text()));
        states[hash] = next;
        return next;
    };
    auto migrated = *this;
    migrated.original_identity = put(root, original_inventory(original));
    migrated.current_state = convert_state(current_state);
    migrated.staged_state = convert_state(staged_state);
    migrated.current_overlay = convert_overlay(current_overlay);
    for (auto &r : migrated.revisions) {
        r.state = convert_state(r.state);
        r.overlay = convert_overlay(r.overlay);
    }
    migrated.read_state(migrated.current_state);
    // Compare every effective stored member before publishing the new manifest.
    for (auto &[old, next] : overlays) {
        auto before = read_overlay(root, old);
        auto view = migrated.source_for(next);
        std::set<std::string> paths;
        for (auto &c : before)
            paths.insert(c.path);
        for (auto &path : paths) {
            std::filesystem::path archive_path = original / path;
            std::map<std::pair<std::size_t, unsigned>, std::string> replacements;
            bool archive = false;
            for (auto &c : before)
                if (c.path == path) {
                    if (c.member < 0)
                        archive_path = blob_path(root, c.blob);
                    else {
                        archive = true;
                        replacements[{std::size_t(c.member), unsigned(c.subfile)}] = c.blob;
                    }
                }
            std::ifstream header(archive_path, std::ios::binary);
            char tag[4]{};
            header.read(tag, 4);
            archive |= std::string(tag, 4) == "CRAG";
            if (archive) {
                Archive expected(archive_path), actual(view / path);
                require(expected.layout_identity() == actual.layout_identity(),
                        "Migration archive metadata mismatch: " + path);
                for (std::size_t member = 0; member < expected.size(); ++member)
                    for (auto sub : expected.subfiles(member)) {
                        auto key = std::pair{member, sub};
                        auto data = replacements.contains(key) ? get(root, replacements.at(key))
                                                               : expected.raw(member, sub);
                        require(actual.raw(member, sub) == data,
                                "Migration member mismatch: " + path);
                    }
            } else
                require(read_file(archive_path) == read_file(view / path),
                        "Migration resource mismatch: " + path);
        }
    }
    std::ostringstream cleanup;
    cleanup << "USUM_CLEANUP 1\n";
    auto verify_generated = [&](const std::filesystem::path &directory, const std::string &next) {
        if (!std::filesystem::is_directory(directory))
            return;
        auto view = migrated.source_for(next);
        for (auto &entry : std::filesystem::recursive_directory_iterator(directory)) {
            require(!entry.is_symlink(), "Legacy generated storage contains a symbolic link");
            if (!entry.is_regular_file())
                continue;
            auto relative = entry.path().lexically_relative(directory);
            if (!std::filesystem::is_regular_file(original / relative))
                continue;
            auto expected = resource_source(view / relative);
            bool equal = false;
            std::error_code error;
            if (expected.members.empty() &&
                std::filesystem::equivalent(entry.path(), expected.original, error))
                equal = true;
            else {
                std::ifstream header(entry.path(), std::ios::binary);
                char tag[4]{};
                header.read(tag, 4);
                if (std::string(tag, 4) == "CRAG") {
                    Archive actual(entry.path()), wanted(view / relative);
                    equal = actual.layout_identity() == wanted.layout_identity();
                    if (equal)
                        for (std::size_t member = 0; member < actual.size(); ++member)
                            for (auto sub : actual.subfiles(member))
                                require(
                                    actual.raw(member, sub) == wanted.raw(member, sub),
                                    "Legacy source differs from its recorded project overlay: " +
                                        entry.path().string());
                } else {
                    std::ifstream actual(entry.path(), std::ios::binary),
                        wanted(resource_file_path(view / relative), std::ios::binary);
                    equal = std::filesystem::file_size(entry.path()) ==
                            std::filesystem::file_size(expected.original);
                    char a[65536], b[65536];
                    while (equal && actual) {
                        actual.read(a, sizeof(a));
                        wanted.read(b, sizeof(b));
                        equal = actual.gcount() == wanted.gcount() &&
                                std::equal(a, a + actual.gcount(), b);
                    }
                }
            }
            require(equal, "Legacy generated resource differs from the recorded source: " +
                               entry.path().string());
            cleanup << std::quoted(entry.path().lexically_relative(root).generic_string()) << ' '
                    << entry.file_size() << ' '
                    << entry.last_write_time().time_since_epoch().count() << '\n';
        }
    };
    verify_generated(root / "sources" / "original", {});
    for (auto &[old, next] : overlays) {
        verify_generated(root / "sources" / old, next);
        verify_generated(root / "overlay" / old, next);
    }
    write_file_atomic(root / "migration" / "cleanup.usum", bytes(cleanup.str()));
    migrated.prepare_source();
    auto backup = root / "migration" / "schema1-project.usum";
    if (!std::filesystem::exists(backup))
        write_new_file(backup, legacy);
    require(read_file(root / "project.usum") == legacy, "Project changed during migration");
    migrated.manifest();
    *this = std::move(migrated);
    // Keep legacy files until their individual contents have been verified for cleanup.
    write_file_atomic(root / "migration" / "status.usum",
                      bytes("USUM_MIGRATION 1\nMember reads verified; schema 2 published.\n"));
    finish_migration_cleanup();
}
void ProjectStore::reset_original(const ProjectChange &change) {
    safe_relative(change.path);
    require(change.path != TargetProfile::zone_archive ||
                std::none_of(edits.begin(), edits.end(),
                             [](const auto &entry) {
                                 return entry.second.kind == "field-map" ||
                                        entry.second.kind == "field-map-created";
                             }),
            "Zone registration contains created maps. Restore a pre-creation history version to "
            "remove registrations and resources together.");
    require(change.path != TargetProfile::character_archive ||
                std::none_of(edits.begin(), edits.end(), [](const auto &entry) {
                    return entry.second.kind == "character-registration-created";
                }), "Registered characters depend on this archive layout. Restore project history "
                    "to remove a registration; reset model edits in Studio.");
    Bytes data;
    if (change.member < 0)
        data = read_file(original / change.path);
    else {
        Archive archive(original / change.path);
        require(std::size_t(change.member) < archive.size(),
                "This added member has no original. Restore a project revision from before its "
                "creation to remove dependent resources together.");
        data = archive.raw(std::size_t(change.member), unsigned(change.subfile));
    }
    std::ostringstream params;
    params << std::quoted(change.path) << ' ' << change.member << ' ' << change.subfile;
    capture({"original/" + change.path + "/" + std::to_string(change.member) + "/" +
                 std::to_string(change.subfile),
             "original",
             "Restore original: " + change.path,
             params.str(),
             {}},
            data);
    save();
}
void ProjectStore::collect() {
    if (member_capture_active())
        return;
    if (std::filesystem::is_directory(root / "scratch"))
        for (auto &entry : std::filesystem::directory_iterator(root / "scratch"))
            if (entry.is_directory() &&
                std::filesystem::is_regular_file(entry.path() / "operation.usum") &&
                text(read_file(entry.path() / "operation.usum")) == "USUM_STAGE_OPERATION 1")
                std::filesystem::remove_all(entry.path());
    auto keep = pinned_resource_objects(root / "objects");
    std::set<std::string> views{"original", source.filename().string()};
    std::function<void(const std::string &)> state;
    auto overlay = [&](const std::string &hash) {
        if (!hash.empty())
            views.insert(hash);
        if (hash.empty() || !keep.insert(hash).second)
            return;
        for (auto &c : read_overlay(root, hash))
            keep.insert(c.blob);
    };
    state = [&](const std::string &hash) {
        if (hash.empty() || !keep.insert(hash).second)
            return;
        ProjectStore copy;
        copy.root = root;
        copy.read_state(hash);
        overlay(copy.base);
        for (auto &[key, e] : copy.edits) {
            keep.insert(e.blob);
            if (e.kind == "composition" || e.kind == "authored-conversations") {
                std::istringstream in(e.parameters);
                unsigned area;
                std::string baseline;
                if (in >> area >> std::quoted(baseline)) {
                    overlay(baseline);
                    std::string previous;
                    if (in >> std::quoted(previous); !previous.empty()) {
                        get(root, previous);
                        keep.insert(previous);
                    }
                }
            }
        }
    };
    overlay(external_connection().baseline);
    keep.insert(source_inventory_identity(root / "objects", original_identity));
    keep.insert(original_identity);
    state(current_state);
    state(staged_state);
    overlay(base);
    overlay(current_overlay);
    for (auto &r : revisions) {
        state(r.state);
        overlay(r.overlay);
    }
    for (auto &file : std::filesystem::directory_iterator(root / "objects"))
        if (file.is_regular_file() && !keep.contains(file.path().filename().string())) {
            std::error_code error;
            std::filesystem::remove(file.path(), error);
        }
    if (std::filesystem::is_directory(root / "member-sources"))
        for (auto &view : std::filesystem::directory_iterator(root / "member-sources")) {
            auto name = view.path().filename().string();
            if (!view.is_directory() || views.contains(name) || name.size() != 64 ||
                name.find_first_not_of("0123456789abcdef") != std::string::npos)
                continue;
            auto marker = view.path() / "source.usum";
            if (std::filesystem::is_regular_file(marker) &&
                text(read_file(marker)).starts_with("USUM_SOURCE 1")) {
                std::filesystem::remove(marker);
                for (auto folder : {"romfs", "exefs"})
                    if (std::filesystem::is_directory(view.path() / folder) &&
                        std::filesystem::is_empty(view.path() / folder))
                        std::filesystem::remove(view.path() / folder);
                if (std::filesystem::is_empty(view.path()))
                    std::filesystem::remove(view.path());
            }
        }
}

std::string ProjectStore::diff(const std::string &key) const {
    auto found = edits.find(key);
    require(found != edits.end(), "Saved edit is unavailable");
    auto next = get(root, found->second.blob);
    Bytes previous;
    if (!staged_state.empty()) {
        ProjectStore staged;
        staged.root = root;
        staged.read_state(staged_state);
        if (auto it = staged.edits.find(key); it != staged.edits.end())
            previous = get(root, it->second.blob);
    }
    if (next == previous)
        return "No changes since the last stage.";
    if (std::find(next.begin(), next.end(), 0) != next.end() ||
        std::find(previous.begin(), previous.end(), 0) != previous.end()) {
        std::size_t changed =
            std::max(next.size(), previous.size()) - std::min(next.size(), previous.size());
        for (std::size_t i = 0; i < std::min(next.size(), previous.size()); ++i)
            changed += next[i] != previous[i];
        return std::to_string(previous.size()) + " -> " + std::to_string(next.size()) + " bytes; " +
               std::to_string(changed) + " bytes differ from the last staged document.";
    }
    std::istringstream before(string(previous)), after(string(next));
    std::vector<std::string> oldlines, newlines;
    std::string line;
    while (std::getline(before, line))
        oldlines.push_back(line);
    while (std::getline(after, line))
        newlines.push_back(line);
    std::set<std::string> oldset(oldlines.begin(), oldlines.end()),
        newset(newlines.begin(), newlines.end());
    std::string out = previous.empty() ? "New saved document (no previous staged document):\n"
                                       : "Changes since the last stage:\n";
    for (auto &row : oldlines)
        if (!newset.contains(row))
            out += "- " + row + "\n";
    for (auto &row : newlines)
        if (!oldset.contains(row))
            out += "+ " + row + "\n";
    if (out.size() > 64000)
        out = out.substr(0, 64000) + "\nPreview truncated.";
    return out;
}

}
