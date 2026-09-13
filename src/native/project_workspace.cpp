#include "native/tutorial_widgets.h"
#include "native/project_workspace.h"
#include <imgui.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <set>
#include "field/area.h"
#include "field/warp_document.h"
#include "assets/clothing_profile.h"
#include "assets/model_library.h"
namespace studio {
namespace {
std::string project_error(const std::exception &error) {
    auto message = std::string(error.what());
    auto *file = dynamic_cast<const std::filesystem::filesystem_error *>(&error);
    if (file && (file->code() == std::errc::permission_denied ||
                 file->code() == std::errc::device_or_resource_busy ||
                 file->code() == std::errc::text_file_busy))
        message += "\nClose any programs using the target folder or files (including File Explorer "
                   "and emulators), then try again.";
    return message;
}
std::string storage_report(const ProjectStore &store) {
    std::ostringstream out;
    const char *folders[] = {"objects", "exports", "scratch"};
    const char *labels[] = {"Saved documents, members and history", "Current game export",
                            "Temporary operations"};
    for (unsigned i = 0; i < 3; ++i) {
        std::uintmax_t total = 0;
        std::vector<std::filesystem::path> counted;
        auto directory = store.root / folders[i];
        if (std::filesystem::is_directory(directory))
            for (auto &entry : std::filesystem::recursive_directory_iterator(directory))
                if (entry.is_regular_file()) {
                    bool duplicate = false;
                    if (std::filesystem::hard_link_count(entry.path()) > 1)
                        for (auto &file : counted)
                            if (std::filesystem::equivalent(file, entry.path())) {
                                duplicate = true;
                                break;
                            }
                    if (!duplicate) {
                        total += entry.file_size();
                        counted.push_back(entry.path());
                    }
                }
        out << labels[i] << ": " << total / 1024 << " KiB\n";
    }
    return out.str();
}
}
ProjectWorkspace::ProjectWorkspace(SDL_Window *window, Preferences &prefs,
                                   const std::string &directory, bool required)
    : window_(window), preferences_(prefs), required_(required) {
    if (!directory.empty())
        try {
            store_ = ProjectStore::open(std::filesystem::u8path(directory));
            if (!store_->unstaged())
                store_->advance();
            store_->prepare_source();
            store_->collect();
            set_project_store(&*store_);
            remember(store_->root);
        } catch (const std::exception &e) {
            error_ = project_error(e);
            set_project_store(nullptr);
            store_.reset();
            required_ = true;
        }
    std::snprintf(dump_, sizeof(dump_), "%s", prefs.get("dump").c_str());
}
ProjectWorkspace::~ProjectWorkspace() {
    if (stage_.valid())
        stage_.wait();
    if (export_.valid())
        export_.wait();
    set_project_store(nullptr);
}
std::string ProjectWorkspace::source() const {
    return store_ ? store_->source.string() : std::string{};
}
void ProjectWorkspace::remember(const std::filesystem::path &path) {
    auto s = path.string();
    if (preferences_.get("last_project") != s)
        preferences_.set("previous_project", preferences_.get("last_project"));
    preferences_.set("last_project", s);
    require(preferences_.save(), preferences_.error);
}
void ProjectWorkspace::switch_to(const std::filesystem::path &path) {
    require(!busy(), "Wait for the project operation to finish before switching projects");
    if (!store_)
        for (auto *binding : ProjectBinding::all())
            require(!binding->pending(),
                    "Save existing edit documents before opening a project from inspection mode");
    save_editor_project();
    auto next = ProjectStore::open(path);
    remember(next.root);
    restart_ = true;
}
bool ProjectWorkspace::close() {
    try {
        require(!busy(), "Wait for the project operation to finish before closing");
        save_editor_project();
        return true;
    } catch (const std::exception &e) {
        error_ = project_error(e);
        manager_ = true;
        return false;
    }
}
void ProjectWorkspace::save() {
    save_editor_project();
    saved_ = std::chrono::steady_clock::now();
    notice_ = "Project saved";
    error_.clear();
}
void ProjectWorkspace::stage() {
    require(store_.has_value(), "Open a project first");
    require(!busy(), "A project operation is already running");
    save();
    auto build = store_->build();
    stage_ = std::async(std::launch::async, [build] {
        return ProjectStore::stage(build, export_project_edit);
    });
    notice_ = "Staging changed members...";
}
void ProjectWorkspace::stage_and_reload() {
    stage();
    reload_after_stage_ = true;
}
void ProjectWorkspace::reload() {
    require(store_.has_value(), "Open a project first");
    require(!busy(), "Wait for the project operation to finish");
    save();
    store_->advance();
    restart_ = true;
}
void ProjectWorkspace::choose(int which) {
    picking_ = which;
    choose_folder(window_, picker_, nullptr);
}
void ProjectWorkspace::update() {
    try {
        {
            std::lock_guard lock(picker_->mutex);
            if (picker_->ready) {
                picker_->ready = false;
                require(picker_->error.empty(), picker_->error);
                if (!picker_->path.empty()) {
                    if (picking_ == 3) {
                        save();
                        require(!store_->unstaged() || store_->edits.empty(),
                                "Stage current edits before importing a source archive");
                        require(std::all_of(store_->edits.begin(), store_->edits.end(),
                                            [](const auto &edit) {
                                                return edit.second.kind == "composition" ||
                                                       edit.second.kind == "field-map-created";
                                            }),
                                "Reload staged assets before importing a source archive");
                        auto selected = std::filesystem::u8path(picker_->path);
                        Archive candidate(selected);
                        if (GameProfile::is_field_archive(import_target_))
                            require(candidate.size() > 0 &&
                                        candidate.size() % TargetProfile::area_stride == 0,
                                    "Expected a complete field archive");
                        if (import_target_ == TargetProfile::pokemon_archive)
                            decode_pokemon_catalog(candidate.decoded(0), candidate.size(), {});
                        store_->import_file(import_target_, selected);
                        restart_ = true;
                        return;
                    }
                    auto &buffer = picking_ == 2 ? dump_ : directory_;
                    require(picker_->path.size() < 4096, "Selected directory is too long");
                    std::snprintf(buffer, 4096, "%s", picker_->path.c_str());
                }
            }
        }
        if (export_.valid() &&
            export_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            export_.get();
            notice_ = "Game export built in exports";
        }
        if (stage_.valid() &&
            stage_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto result = stage_.get();
            store_->accept(result);
            store_->collect();
            notice_ = "Overlay staged successfully";
            for (auto &note : result.notes)
                notice_ += "\n" + note;
            staged_ = std::chrono::steady_clock::now();
            for (auto &[key, edit] : store_->edits)
                if (edit.kind == "field-map" && !store_->unstaged()) {
                    auto plan = MapCreation::parse(text(read_file(store_->document(key))));
                    preferences_.set("area", std::to_string(plan.area));
                    preferences_.set("map_zone", std::to_string(plan.zone));
                    require(preferences_.save(), preferences_.error);
                    store_->advance();
                    restart_ = true;
                    break;
                }
            if (reload_after_stage_ && !restart_) {
                save_editor_project();
                require(!store_->unstaged(),
                        "Edits changed during staging. Apply again before reloading.");
                store_->advance();
                reload_after_stage_ = false;
                restart_ = true;
            }
        }
        if (store_ && !restart_) {
            auto now = std::chrono::steady_clock::now();
            const auto &bindings = ProjectBinding::all();
            const bool automatic_ready =
                std::all_of(bindings.begin(), bindings.end(), [](const auto *binding) {
                    return binding->ready_for_autosave();
                });
            if (automatic_ready && store_->settings.autosave &&
                now - saved_ >= std::chrono::seconds(store_->settings.save_seconds)) {
                saved_ = now;
                save_editor_project();
            }
            if (automatic_ready && store_->settings.auto_stage && !busy() && store_->unstaged() &&
                now - staged_ >= std::chrono::seconds(store_->settings.stage_seconds)) {
                staged_ = now;
                stage();
            }
        }
    } catch (const std::exception &e) {
        reload_after_stage_ = false;
        manager_ = true;
        error_ = project_error(e);
        staged_ = std::chrono::steady_clock::now();
    }
}
void ProjectWorkspace::build_game_export() {
    if (!store_ || busy() || store_->current_overlay.empty())
        return;
    auto snapshot = *store_;
    export_ = std::async(std::launch::async, [snapshot]() mutable {
        snapshot.build_export();
    });
    error_.clear();
    notice_ = "Building game export...";
}
void ProjectWorkspace::menu(int area) {
    try {
        if (store_) {
            ImGui::TextUnformatted(store_->root.filename().string().c_str());
            ImGui::TextDisabled("Target: %s", game_target_name(store_->target));
            ImGui::Separator();
            if (studio::TutorialWidgets::MenuItem("project_workspace", "Save Project",
                                                  "Ctrl+Shift+S"))
                save();
            if (studio::TutorialWidgets::MenuItem("project_workspace", "Stage Project",
                                                  "Ctrl+Shift+T", false, !busy()))
                stage();
            if (studio::TutorialWidgets::MenuItem(
                    "project_workspace", "Create map from template...", nullptr, false, !busy())) {
                map_templates_ = load_map_catalog(store_->source);
                map_template_ = -1;
                map_entrance_ = -1;
                for (std::size_t i = 0; i < map_templates_.locations.size(); ++i)
                    if (map_templates_.locations[i].zone >= 0 &&
                        map_templates_.locations[i].area == area) {
                        map_template_ = int(i);
                        break;
                    }
                map_dialog_ = true;
                map_inherited_ = false;
                map_plan_.reset();
                error_.clear();
            }
            if (studio::TutorialWidgets::MenuItem("project_workspace",
                                                  "Changes, history and settings..."))
                manager_ = true;
            if (studio::TutorialWidgets::MenuItem("project_workspace", "Reload staged assets",
                                                  nullptr, false, !busy() && !store_->unstaged()))
                reload();
            if (studio::TutorialWidgets::MenuItem("project_workspace", "Open project folder"))
                SDL_OpenURL(("file:///" + store_->root.generic_string()).c_str());
            if (studio::TutorialWidgets::MenuItem("project_workspace", "Build game export",
                                                  "Ctrl+Shift+B", false,
                                                  !busy() && !store_->current_overlay.empty()))
                build_game_export();
            if (studio::TutorialWidgets::MenuItem(
                    "project_workspace", "Open exports folder", nullptr, false,
                    std::filesystem::exists(store_->overlay_directory())))
                SDL_OpenURL(("file:///" + store_->overlay_directory().generic_string()).c_str());
            if (ImGui::BeginMenu("Import source archive", !busy())) {
                auto import = [&](const char *label, const char *relative) {
                    if (studio::TutorialWidgets::MenuItem("project_import", label)) {
                        import_target_ = relative;
                        picking_ = 3;
                        choose_archive(window_, picker_, nullptr);
                    }
                };
                import("Field areas", GameProfile::field_archive(store_->source));
                import("Terrain resources", TargetProfile::terrain_archive);
                import("Pokemon", TargetProfile::pokemon_archive);
                const char *library[] = {"Battle characters", "Field characters", "Poke Balls",
                                         "Battle props",      "Poke Beans",       "Battle arenas"};
                for (int i = 0; i < 6; ++i)
                    import(library[i], model_category_archive(ModelCategory(i)));
                const char *profiles[] = {"Ultra male", "Ultra female", "Sun/Moon male",
                                          "Sun/Moon female"};
                for (unsigned i = 0; i < 4; ++i)
                    if (ImGui::BeginMenu(profiles[i])) {
                        import("Colors", ClothingProfile::colors[i]);
                        for (unsigned part = 0; part < ClothingProfile::parts; ++part)
                            if (ClothingProfile::archives[i][part][0])
                                import(ClothingProfile::names[part],
                                       ClothingProfile::archives[i][part]);
                        ImGui::EndMenu();
                    }
                ImGui::EndMenu();
            }
            ImGui::Separator();
        }
        if (studio::TutorialWidgets::MenuItem("project_workspace", "New Project...", nullptr, false,
                                              !busy())) {
            creating_ = true;
            setup_ = true;
            directory_[0] = 0;
        }
        if (studio::TutorialWidgets::MenuItem("project_workspace", "Open Project...", nullptr,
                                              false, !busy())) {
            creating_ = false;
            setup_ = true;
            directory_[0] = 0;
        }
        auto recent = preferences_.get("previous_project");
        if (!recent.empty() && ImGui::BeginMenu("Recent project")) {
            if (studio::TutorialWidgets::MenuItem("project_workspace", recent.c_str()))
                switch_to(std::filesystem::u8path(recent));
            ImGui::EndMenu();
        }
    } catch (const std::exception &e) {
        error_ = project_error(e);
        manager_ = true;
    }
}
void ProjectWorkspace::draw_map_creation() {
    if (!map_dialog_ || !store_)
        return;
    ImGui::SetNextWindowPos({360, 160}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({700, 580}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Create map from template", &map_dialog_);
    ImGui::TextWrapped("Choose a template for the new map.");
    ImGui::InputText("Project map name", map_name_, sizeof(map_name_));
    auto label = [](const MapLocation &location) {
        return location.name + " / zone " + std::to_string(location.zone) + " / area " +
               std::to_string(location.area);
    };
    if (ImGui::BeginCombo("Template",
                          map_template_ >= 0 &&
                                  std::size_t(map_template_) < map_templates_.locations.size()
                              ? label(map_templates_.locations[std::size_t(map_template_)]).c_str()
                              : "Choose a map")) {
        for (std::size_t i = 0; i < map_templates_.locations.size(); ++i) {
            auto &location = map_templates_.locations[i];
            if (location.zone < 0)
                continue;
            ImGui::PushID(int(i));
            if (ImGui::Selectable(label(location).c_str(), map_template_ == int(i))) {
                map_template_ = int(i);
                map_entrance_ = -1;
                map_plan_.reset();
                error_.clear();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    try {
        if (map_template_ >= 0 && std::size_t(map_template_) < map_templates_.locations.size()) {
            auto &location = map_templates_.locations[std::size_t(map_template_)];
            Archive fields(store_->source / GameProfile::field_archive(store_->source));
            WarpDocument warps(unsigned(location.area), fields.decoded(unsigned(location.area) *
                                                                       TargetProfile::area_stride));
            auto entrance_label = [&](int i) {
                return i < 0 ? std::string("Leave unconnected")
                             : "Replace template entrance " +
                                   std::to_string(warps.records().at(std::size_t(i)).event);
            };
            if (ImGui::BeginCombo("Entry and return route",
                                  entrance_label(map_entrance_).c_str())) {
                if (ImGui::Selectable("Leave unconnected", map_entrance_ < 0))
                    map_entrance_ = -1;
                for (std::size_t i = 0; i < warps.records().size(); ++i)
                    if (!warps.records()[i].shapes.empty()) {
                        ImGui::PushID(int(i));
                        if (ImGui::Selectable(entrance_label(int(i)).c_str(),
                                              map_entrance_ == int(i)))
                            map_entrance_ = int(i);
                        ImGui::PopID();
                    }
                ImGui::EndCombo();
            }
            if (map_entrance_ >= 0)
                ImGui::TextWrapped(
                    "This replaces the selected entrance's destination in the template map. Its "
                    "matching entrance in the new map returns to the template. Trigger shapes and "
                    "arrival positions are retained.");
            else
                ImGui::TextWrapped("The new map has no new entry route. Use the existing entrance "
                                   "editor to connect a map to it later.");
            if (studio::TutorialWidgets::Button("project_workspace", "Review new map")) {
                map_plan_ = plan_map_creation(store_->source, unsigned(location.zone), map_name_,
                                              map_entrance_);
                error_.clear();
            }
            if (map_plan_ && (map_plan_->name != map_name_ || map_plan_->entrance != map_entrance_))
                map_plan_.reset();
        }
        ImGui::Separator();
        ImGui::TextWrapped("Terrain, collision and field resources are copied. Actors, scripts, "
                           "encounters, music, cameras and in-game location text are inherited "
                           "from the template. The project name does not change in-game text.");
        studio::TutorialWidgets::Checkbox("project_workspace",
                                          "Keep the template's existing behavior", &map_inherited_);
        ImGui::TextWrapped("This version supports one zone per world and one terrain resource. New "
                           "IDs require your corresponding executable changes for in-game use.");
        if (map_plan_) {
            auto &plan = *map_plan_;
            ImGui::Text("New field area %u | zone %u | world %u | terrain %u", plan.area, plan.zone,
                        plan.world, plan.terrain);
        }
        if (store_->unstaged())
            ImGui::TextWrapped("Stage your current project changes before creating a map.");
        else if (store_->base != store_->current_overlay)
            ImGui::TextWrapped(
                "Reload staged assets before creating a map so it uses your latest changes.");
        ImGui::BeginDisabled(!map_plan_ || !map_inherited_ || busy() || store_->unstaged() ||
                             store_->base != store_->current_overlay);
        if (studio::TutorialWidgets::Button("project_workspace", "Create, stage and open map"))
            try {
                save();
                require(!store_->unstaged() && store_->base == store_->current_overlay,
                        "Stage and reload current edits before creating a map");
                auto plan = plan_map_creation(store_->source, map_plan_->template_zone, map_name_,
                                              map_entrance_);
                require(plan == *map_plan_, "Template changed. Review the new map again.");
                auto old = *store_;
                try {
                    store_->capture(
                        {"field-map/" + std::to_string(plan.zone), "field-map", plan.name, "", {}},
                        project_text(plan.serialize()));
                    store_->save();
                } catch (...) {
                    *store_ = std::move(old);
                    throw;
                }
                stage();
                map_dialog_ = false;
                manager_ = true;
            } catch (const std::exception &e) {
                error_ = project_error(e);
            }
        ImGui::EndDisabled();
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
    } catch (const std::exception &e) {
        map_plan_.reset();
        error_ = project_error(e);
        ImGui::TextWrapped("%s", error_.c_str());
    }
    ImGui::End();
}
void ProjectWorkspace::draw() {
    draw_map_creation();
    auto &io = ImGui::GetIO();
    if (store_ && io.KeyCtrl && io.KeyShift && !io.KeyAlt)
        try {
            if (ImGui::IsKeyPressed(ImGuiKey_S, false))
                save();
            if (ImGui::IsKeyPressed(ImGuiKey_T, false))
                stage();
            if (ImGui::IsKeyPressed(ImGuiKey_B, false))
                build_game_export();
        } catch (const std::exception &e) {
            error_ = project_error(e);
            manager_ = true;
        }
    if (busy() && !ImGui::IsPopupOpen("Project progress"))
        ImGui::OpenPopup("Project progress");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {.5f, .5f});
    if (ImGui::BeginPopupModal("Project progress", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings)) {
        if (!busy())
            ImGui::CloseCurrentPopup();
        else {
            ImGui::TextUnformatted(export_.valid() ? "Building game export..."
                                                   : "Staging project...");
            ImGui::ProgressBar(-float(ImGui::GetTime()) - 1.f, {320, 0}, "");
        }
        ImGui::EndPopup();
    }
    if (gate() || setup_) {
        ImGui::SetNextWindowSize({660, 350}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Open a project", gate() ? nullptr : &setup_, ImGuiWindowFlags_NoCollapse);
        ImGui::TextWrapped("Open a project or create one for your edits.");
        if (studio::TutorialWidgets::RadioButton("project_workspace", "Open existing", !creating_))
            creating_ = false;
        ImGui::SameLine();
        if (studio::TutorialWidgets::RadioButton("project_workspace", "Create new", creating_))
            creating_ = true;
        ImGui::InputText("Project directory", directory_, sizeof(directory_));
        TutorialWidgets::item("project_workspace", "Project directory");
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("project_workspace", "Browse##project"))
            choose(1);
        if (creating_) {
            ImGui::InputText("Original dump", dump_, sizeof(dump_));
            TutorialWidgets::item("project_workspace", "Original dump");
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("project_workspace", "Browse##dump"))
                choose(2);
            ImGui::TextWrapped("Choose an empty project folder and a game dump containing romfs.");
        }
        if (studio::TutorialWidgets::Button("project_workspace",
                                            creating_ ? "Create Project" : "Open Project"))
            try {
                require(directory_[0], "Choose a project directory");
                require(!busy(),
                        "Wait for the project operation to finish before switching projects");
                if (creating_) {
                    if (!store_)
                        for (auto *binding : ProjectBinding::all())
                            require(!binding->pending(), "Save existing edit documents before "
                                                         "creating a project from inspection mode");
                    save_editor_project();
                    auto created = ProjectStore::create(std::filesystem::u8path(directory_),
                                                        std::filesystem::u8path(dump_));
                    switch_to(created.root);
                } else
                    switch_to(std::filesystem::u8path(directory_));
            } catch (const std::exception &e) {
                error_ = project_error(e);
            }
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::End();
    }
    if (manager_ && store_) {
        ImGui::SetNextWindowSize({780, 550}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Project changes", &manager_);
        ImGui::TextWrapped("%s", store_->root.string().c_str());
        ImGui::TextUnformatted(export_.valid()      ? "Building game export..."
                               : stage_.valid()     ? "Staging..."
                               : store_->unstaged() ? "Saved changes are waiting to be staged"
                                                    : "Saved changes match the staged version");
        try {
            if (studio::TutorialWidgets::Button("project_workspace", "Save Project"))
                save();
            ImGui::SameLine();
            ImGui::BeginDisabled(busy());
            if (studio::TutorialWidgets::Button("project_workspace", "Stage Project"))
                stage();
            ImGui::EndDisabled();
            ImGui::TextUnformatted(store_->export_current()
                                       ? "Game export matches staged members"
                                       : "Game export needs building after staging");
            ImGui::TextWrapped("%s", notice_.empty() ? " " : notice_.c_str());
            if (!error_.empty())
                ImGui::TextWrapped("%s", error_.c_str());
            if (ImGui::BeginTabBar("Project details")) {
                if (studio::TutorialWidgets::BeginTabItem("project_workspace", "Saved edits")) {
                    ImGui::TextWrapped("Saved documents remain available after switching assets. "
                                       "Select an edit to inspect its saved contents.");
                    for (auto &[key, e] : store_->edits) {
                        ImGui::PushID(key.c_str());
                        if (ImGui::Selectable((e.label + " (" + e.kind + ")").c_str(),
                                              selection_ == key)) {
                            selection_ = key;
                            diff_ = store_->diff(key);
                        }
                        ImGui::PopID();
                    }
                    if (!selection_.empty() && store_->edits.contains(selection_)) {
                        ImGui::BeginDisabled(busy());
                        if (store_->edits.at(selection_).kind == "field-map-created")
                            ImGui::TextWrapped(
                                "Map creation record. To remove the map and its registrations "
                                "together, restore a history version from before creation.");
                        else if (studio::TutorialWidgets::Button("project_workspace",
                                                                 "Reset this saved edit")) {
                            save();
                            store_->reset(selection_);
                            restart_ = true;
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Remove this document and return to its session "
                                              "source baseline. Other documents stay saved.");
                        ImGui::EndDisabled();
                    }
                    if (!diff_.empty()) {
                        ImGui::Separator();
                        ImGui::BeginChild("Saved content", {0, 180}, ImGuiChildFlags_Borders);
                        ImGui::TextUnformatted(diff_.c_str());
                        ImGui::EndChild();
                    }
                    ImGui::EndTabItem();
                }
                if (studio::TutorialWidgets::BeginTabItem("project_workspace",
                                                          "Staged resources")) {
                    ImGui::TextWrapped(
                        "These files and archive members differ from the original dump. Original "
                        "resets are saved as edits and applied by the next stage.");
                    for (auto &c : store_->changes()) {
                        auto label =
                            c.path + (c.member < 0
                                          ? " (whole file)"
                                          : " / member " + std::to_string(c.member) +
                                                " / language " + std::to_string(c.subfile));
                        ImGui::PushID(label.c_str());
                        ImGui::TextUnformatted(label.c_str());
                        ImGui::SameLine();
                        ImGui::BeginDisabled(busy());
                        if (studio::TutorialWidgets::SmallButton("project_workspace",
                                                                 "Restore original")) {
                            save();
                            store_->reset_original(c);
                            notice_ = "Original reset saved. Stage to apply it.";
                        }
                        ImGui::EndDisabled();
                        ImGui::PopID();
                    }
                    ImGui::EndTabItem();
                }
                if (studio::TutorialWidgets::BeginTabItem("project_workspace", "History")) {
                    ImGui::TextWrapped("Restore replaces the saved documents and overlay with a "
                                       "successful staged version. Save and stage current work "
                                       "first if you want to retain it in history.");
                    for (std::size_t i = 0; i < store_->revisions.size(); ++i) {
                        ImGui::PushID(int(i));
                        ImGui::Text("Version %zu (%s)", i + 1, store_->revisions[i].label.c_str());
                        ImGui::SameLine();
                        ImGui::BeginDisabled(busy() || store_->unstaged());
                        if (studio::TutorialWidgets::SmallButton("project_workspace", "Restore")) {
                            save();
                            require(!store_->unstaged(),
                                    "Stage current changes before restoring history");
                            store_->restore(i);
                            restart_ = true;
                        }
                        ImGui::EndDisabled();
                        ImGui::PopID();
                    }
                    ImGui::EndTabItem();
                }
                if (studio::TutorialWidgets::BeginTabItem("project_workspace", "Settings")) {
                    auto settings = store_->settings;
                    bool changed = studio::TutorialWidgets::Checkbox(
                        "project_workspace", "Autosave", &settings.autosave);
                    changed |= ImGui::InputInt("Save interval (seconds)", &settings.save_seconds);
                    changed |= studio::TutorialWidgets::Checkbox(
                        "project_workspace", "Automatically stage saved changes",
                        &settings.auto_stage);
                    changed |= ImGui::InputInt("Stage interval (seconds)", &settings.stage_seconds);
                    changed |= ImGui::InputInt("Keep staged versions", &settings.history);
                    if (changed) {
                        settings.save_seconds = std::max(1, settings.save_seconds);
                        settings.stage_seconds = std::max(1, settings.stage_seconds);
                        settings.history = std::clamp(settings.history, 1, 100);
                        store_->settings = settings;
                        store_->save_settings();
                    }
                    ImGui::TextWrapped("Original dump: %s", store_->original.string().c_str());
                    static std::filesystem::path measured;
                    static std::string storage;
                    if (measured != store_->root) {
                        measured = store_->root;
                        storage.clear();
                    }
                    ImGui::BeginDisabled(busy());
                    if (studio::TutorialWidgets::Button("project_workspace",
                                                        "Measure project storage"))
                        storage = storage_report(*store_);
                    ImGui::EndDisabled();
                    if (!storage.empty())
                        ImGui::TextUnformatted(storage.c_str());
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        } catch (const std::exception &e) {
            error_ = project_error(e);
        }
        ImGui::End();
    }
}
}
