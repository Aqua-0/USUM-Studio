#pragma once
#include "core/binary.h"
#include "core/game_profile.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
namespace studio {
struct ProjectEdit {
    std::string key, kind, label, parameters, blob;
    bool operator==(const ProjectEdit &) const = default;
};
struct ProjectChange {
    std::string path;
    int member = -1, subfile = 0;
    std::string blob;
    bool operator==(const ProjectChange &) const = default;
};
struct ProjectRevision {
    std::string state, overlay, label;
};
struct ProjectSettings {
    bool autosave = true, auto_stage = false;
    int save_seconds = 30, stage_seconds = 10, history = 3;
};
struct ProjectBuild {
    std::filesystem::path root, source;
    std::string state, base;
    std::vector<ProjectEdit> edits;
};
struct ProjectBuildResult {
    std::string state, overlay;
    std::filesystem::path directory;
    std::vector<std::string> notes;
};
using ProjectExporter =
    std::function<void(const ProjectEdit &, const std::filesystem::path &,
                       const std::filesystem::path &, const std::filesystem::path &)>;
class ProjectStore {
  public:
    static ProjectStore create(const std::filesystem::path &root,
                               const std::filesystem::path &original);
    static ProjectStore open(const std::filesystem::path &root);
    std::filesystem::path root, original, source;
    unsigned format_version = 2;
    GameTarget target = GameTarget::UltraMoon;
    ProjectSettings settings;
    std::map<std::string, ProjectEdit> edits;
    std::vector<ProjectRevision> revisions;
    std::string original_identity;
    std::string base, current_state, staged_state, current_overlay;
    void prepare_source();
    std::filesystem::path source_for(const std::string &overlay);
    std::string diff(const std::string &key) const;
    void save();
    void save_settings();
    void capture(ProjectEdit edit, View bytes);
    std::filesystem::path document(const std::string &key) const;
    std::filesystem::path overlay_directory() const;
    bool unstaged() const {
        return current_state != staged_state;
    }
    ProjectBuild build() const;
    void build_export();
    bool export_current() const;
    void accept(const ProjectBuildResult &result);
    void restore(std::size_t revision);
    void reset(const std::string &key);
    void advance();
    void collect();
    void import_file(const std::filesystem::path &relative, const std::filesystem::path &file,
                     bool allow_append = false);
    void reset_original(const ProjectChange &change);
    std::vector<ProjectChange> changes() const;
    static ProjectBuildResult stage(const ProjectBuild &build, const ProjectExporter &exporter);

  private:
    std::shared_ptr<void> lease_;
    void migrate();
    void recover_export();
    void finish_migration_cleanup();
    std::string state_text() const;
    void read_state(const std::string &hash);
    void manifest();
};
Bytes merge_project_resource(View baseline, View first, View second, const std::string &label,
                             unsigned depth = 0);
}
