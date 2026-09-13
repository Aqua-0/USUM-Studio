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
    out << std::put_time(std::gmtime(&time), "%Y-%m-%d %H:%M:%S UTC");
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
                                  const std::filesystem::path &dump) {
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
    p.original_identity = put(p.root, original_inventory(p.original));
    p.save();
    return p;
}
ProjectStore ProjectStore::open(const std::filesystem::path &directory) {
    ProjectStore p;
    p.root = std::filesystem::absolute(directory);
    p.lease_ = project_lease(p.root);
    std::istringstream in(string(read_file(p.root / "project.usum")));
    std::string tag, path;
    unsigned version;
    require(bool(in >> tag >> version) && tag == "USUM_PROJECT" && (version >= 1 && version <= 4),
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
    p.original = std::filesystem::u8path(path);
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
    if (version >= 2)
        require(get(p.root, p.original_identity) == original_inventory(p.original),
                "The original dump has changed. Restore the original files before opening this "
                "project.");
    p.target = detect_game_target(p.original);
    require(version < 4 || target == game_target_id(p.target),
            "Project game target differs from the original dump");
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
    if (edit.kind == "field-map")
        format_version = std::max(format_version, 3u);
    edit.blob = put(root, data);
    edits[edit.key] = std::move(edit);
}
std::filesystem::path ProjectStore::document(const std::string &key) const {
    auto it = edits.find(key);
    return it == edits.end() ? std::filesystem::path{} : blob_path(root, it->second.blob);
}
void ProjectStore::prepare_source() {
    source = source_for(base);
    for (auto &[key, e] : edits)
        if (e.kind == "composition") {
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
    for (auto &edit : build.edits) {
        if (edit.kind == "field-map-created")
            continue;
        if (edit.kind == "composition") {
            std::istringstream in(edit.parameters);
            unsigned area;
            std::string baseline, previous;
            require(bool(in >> area >> std::quoted(baseline)), "Invalid composition source");
            in >> std::quoted(previous);
            if (!stages_composition(text(get(build.root, edit.blob))) && previous.empty()) {
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
        if (edit.kind == "composition") {
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
                for (std::size_t i = 0; i < original.size(); ++i)
                    for (auto sub : original.subfiles(i)) {
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
                    require(after.size() == before.size() ||
                                (edit.kind == "field-map" && after.size() > before.size()),
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
        !edits.contains(key) || edits.at(key).kind != "field-map-created",
        "Created maps have dependent resources. Restore a pre-creation history version instead.");
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
            if (edit.kind == "composition") {
                std::istringstream in(edit.parameters);
                unsigned area;
                std::string baseline;
                require(bool(in >> area >> std::quoted(baseline)), "Invalid composition source");
                std::ostringstream parameters;
                parameters << area << ' ' << std::quoted(baseline) << ' '
                           << std::quoted(stages_composition(text(get(root, edit.blob)))
                                              ? edit.blob
                                              : std::string{});
                edit.parameters = parameters.str();
            }
        for (auto &[key, edit] : edits)
            if (edit.kind == "field-map")
                edit.kind = "field-map-created";
        std::erase_if(edits, [](const auto &e) {
            return e.second.kind != "composition" && e.second.kind != "field-map-created";
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
                                   e.second.kind == "field-map-created";
                        }),
            "Stage and reload saved edits before changing project source archives");
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
        auto relative = std::filesystem::u8path(name);
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
            if (e.kind == "composition") {
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
            if (e.kind == "composition") {
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
