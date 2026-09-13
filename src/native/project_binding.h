#pragma once
#include "project/project_store.h"
#include "assets/model_document.h"
#include <set>
namespace studio {
class ProjectBinding {
  public:
    ProjectBinding();
    ~ProjectBinding();
    ProjectBinding(const ProjectBinding &) = delete;
    ProjectBinding &operator=(const ProjectBinding &) = delete;
    void bind(std::string kind, std::string key, std::string label, std::string parameters,
              std::function<bool()> dirty, std::function<Bytes()> encode,
              std::function<void()> saved);
    std::filesystem::path document() const;
    bool capture();
    void acknowledge();
    bool pending() const {
        return imported_ || (dirty_ && dirty_());
    }
    void imported() {
        imported_ = true;
    }
    void restored() {
        imported_ = false;
    }
    void ready(std::function<void()> check) {
        ready_ = std::move(check);
    }
    void autosave_when(std::function<bool()> check) {
        autosave_ready_ = std::move(check);
    }
    bool ready_for_autosave() const {
        return !autosave_ready_ || autosave_ready_();
    }
    const std::string &key() const {
        return edit_.key;
    }
    static std::set<ProjectBinding *> &all();

  private:
    std::string baseline_;
    std::function<void()> ready_;
    std::function<bool()> autosave_ready_;
    ProjectEdit edit_;
    std::function<bool()> dirty_;
    std::function<Bytes()> encode_;
    std::function<void()> saved_;
    bool captured_ = false, imported_ = false;
};
ProjectStore *project_store();
void set_project_store(ProjectStore *store);
bool save_editor_project();
Bytes project_text(const std::string &value);
Bytes project_encode_file(const std::function<void(const std::filesystem::path &)> &writer);
std::string project_model_parameters(const ModelDocument &model);
void export_project_edit(const ProjectEdit &edit, const std::filesystem::path &source,
                         const std::filesystem::path &document,
                         const std::filesystem::path &output);
}
