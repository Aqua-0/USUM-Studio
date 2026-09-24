#include "assets/asset_package.h"
#include "native/tutorial_widgets.h"
#include "native/map_authoring_workspace.h"
#include "core/digest.h"
#include "authoring/object_export.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

namespace studio {
namespace {
std::string lower(std::string text) {
    for (auto &c : text)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
std::string read_composition(const std::filesystem::path &path) {
    require(std::filesystem::file_size(path) <= 32 * 1024 * 1024,
            "Composition exceeds the 32 MiB document read limit");
    auto bytes = read_file(path);
    return {bytes.begin(), bytes.end()};
}
AuthoringGrid template_grid(const Environment &scene) {
    std::array<float, 2> low{INFINITY, INFINITY}, high{-INFINITY, -INFINITY};
    for (const auto &region : scene.spatial.regions)
        if (region.kind == SpatialKind::Ground)
            for (const auto &vertex : region.vertices)
                for (unsigned axis = 0; axis < 2; ++axis) {
                    low[axis] = std::min(low[axis], vertex.position[axis * 2]);
                    high[axis] = std::max(high[axis], vertex.position[axis * 2]);
                }
    if (!std::isfinite(low[0])) {
        low = {scene.low[0], scene.low[2]};
        high = {scene.high[0], scene.high[2]};
    }
    AuthoringGrid grid;
    for (unsigned axis = 0; axis < 2; ++axis)
        grid.origin[axis] = std::floor(low[axis] / grid.tile_size) * grid.tile_size;
    grid.width = std::max(1, int(std::ceil((high[0] - grid.origin[0]) / grid.tile_size)));
    grid.height = std::max(1, int(std::ceil((high[1] - grid.origin[1]) / grid.tile_size)));
    grid.validate();
    return grid;
}
}
MapAuthoringWorkspace::MapAuthoringWorkspace(const std::filesystem::path &shaders,
                                             SDL_Window *window)
    : window_(window), renderer_(shaders), shaders_(shaders) {
    renderer_.retain_frame_during_upload = true;
    renderer_.lighting.enabled = true;
    renderer_.characters_enabled = false;
    renderer_.sky_enabled = false;
    renderer_.particles_enabled = false;
}
MapAuthoringWorkspace::~MapAuthoringWorkspace() {
    clear_ground_thumbnails();
    cancel_ = true;
    if (load_job_.valid())
        load_job_.wait();
    if (resource_job_.valid())
        resource_job_.wait();
    if (library_job_.valid())
        library_job_.wait();
    if (surface_job_.valid())
        surface_job_.wait();
    if (review_job_.valid())
        review_job_.wait();
}
void MapAuthoringWorkspace::open_template(const std::filesystem::path &requested_dump,
                                          unsigned area,
                                          const std::filesystem::path &requested_path) {
    require(!busy(), "Finish the current file operation first");
    auto dump = requested_dump;
    auto path = requested_path;
    project_baseline_.clear();
    project_ground_base_.clear();
    if (!path.empty()) {
        std::istringstream header(read_composition(path));
        std::string magic, key;
        unsigned version;
        require(bool(header >> magic >> version >> key >> area) &&
                    magic == "USUMSTUDIO_COMPOSITION" && version >= 1 && version <= 15 &&
                    key == "area",
                "Unsupported composition document");
    }
    if (auto *project = project_store()) {
        project_baseline_ = project->base;
        auto key = "composition/" + std::to_string(area);
        auto saved = project->document(key);
        if (!saved.empty()) {
            std::istringstream params(project->edits.at(key).parameters);
            unsigned saved_area;
            params >> saved_area >> std::quoted(project_baseline_);
            params >> std::quoted(project_ground_base_);
            dump = project->source_for(project_baseline_);
            if (path.empty())
                path = saved;
        }
    }
    require(!busy(), "Finish the current file operation first");
    cancel_ = false;
    error_.clear();
    status_ = "Reading template and resource catalog...";
    collision_editor_.reset();
    collision_renderer_.reset();
    collision_request_ = false;
    load_job_ = std::async(std::launch::async, [this, dump, area, path] {
        Loaded loaded;
        loaded.dump = dump;
        loaded.path = path;
        auto selected_area = area;
        std::string saved;
        if (!path.empty()) {
            saved = read_composition(path);
            std::istringstream in(saved);
            std::string magic, key;
            unsigned version = 0;
            require(bool(in >> magic >> version >> key >> selected_area) &&
                        magic == "USUMSTUDIO_COMPOSITION" && (version >= 1 && version <= 15) &&
                        key == "area",
                    "Unsupported composition document");
        }
        loaded.base =
            std::make_shared<Environment>(load_environment(dump, selected_area, &cancel_));
        auto catalog = load_composition_catalog(dump, selected_area, saved, &cancel_);
        auto fingerprint = map_template_fingerprint(dump, catalog);
        require(sha256(loaded.base->placement_source) ==
                    sha256(Archive(dump / GameProfile::field_archive(dump))
                               .decoded(selected_area * TargetProfile::area_stride +
                                        TargetProfile::placement_slot)),
                "Template changed during loading; retry");
        loaded.document = std::make_unique<CompositionDocument>(std::move(catalog), fingerprint,
                                                                template_grid(*loaded.base));
        if (!saved.empty())
            loaded.document->restore(saved);
        loaded.maps = load_map_catalog(dump).locations;
        loaded.textures = load_ground_textures(dump, *loaded.base, &cancel_);
        for (auto &texture : loaded.textures)
            loaded.surface_keys.push_back(texture.key);
        if (loaded.document->ground())
            restore_ground_textures(dump, *loaded.base, *loaded.document->ground(), loaded.textures,
                                    &cancel_);
        for (const auto &asset : loaded.document->project_assets()) {
            if (asset.source != ProjectAsset::no_source && !loaded.resources.contains(asset.source))
                loaded.resources.emplace(
                    asset.source,
                    preview_map_resource(dump, loaded.document->catalog().entries.at(asset.source),
                                         &cancel_));
            loaded.resources.emplace(loaded.document->project_resource(asset.id),
                                     project_asset_preview(asset.source == ProjectAsset::no_source
                                                               ? Environment{}
                                                               : loaded.resources.at(asset.source),
                                                           asset));
        }
        for (const auto &instance : loaded.document->instances())
            if (!loaded.resources.contains(instance.resource))
                loaded.resources.emplace(
                    instance.resource,
                    preview_map_resource(
                        dump, loaded.document->catalog().entries.at(instance.resource), &cancel_));
        require(!cancel_.load(), "Template loading cancelled");
        return loaded;
    });
}
void MapAuthoringWorkspace::load_resource(std::size_t resource) {
    if (busy())
        return;
    cancel_preview();
    resource_ = int(resource);
    selected_ = 0;
    if (resources_.contains(resource)) {
        inspect_ = true;
        auto &asset = resources_.at(resource);
        asset_camera_.fit(asset.low, asset.high);
        renderer_.set_scene(std::make_shared<Environment>(asset));
        return;
    }
    cancel_ = false;
    error_.clear();
    status_ = "Decoding resource preview...";
    auto entry = document_->catalog().entries.at(resource);
    auto dump = dump_;
    resource_job_ = std::async(std::launch::async, [this, dump, entry, resource] {
        return std::make_pair(resource, preview_map_resource(dump, entry, &cancel_));
    });
}
void MapAuthoringWorkspace::poll() {
    if (!asset_edit_) {
        try {
            object_exchange_result();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    }
    if (review_job_.valid() &&
        review_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            status_ = review_job_.get();
            error_.clear();
        } catch (const std::exception &error) {
            error_ = error.what();
            status_.clear();
        }

    if (surface_job_.valid() &&
        surface_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            auto library = surface_job_.get();
            merge_ground_textures(*base_, ground_textures_, library.scene, library.textures);
            auto &keys = surface_keys_[library.area];
            for (auto &texture : library.textures)
                keys.push_back(texture.key);
            surface_area_ = library.area;
            status_ = "Surface library ready. Select a texture to paint the composition.";
            error_.clear();
        } catch (const std::exception &error) {
            error_ = error.what();
        }

    if (library_job_.valid() &&
        library_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            auto library = library_job_.get();
            document_->add_library(library);
            library_area_ = library.area;
            status_ = "Source map library ready.";
        } catch (const std::exception &error) {
            error_ = error.what();
        }

    if (load_job_.valid() &&
        load_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            auto loaded = load_job_.get();
            auto surface = loaded.document->ground()
                               ? std::make_shared<Environment>(
                                     ground_preview(*loaded.base, loaded.document->grid(),
                                                    *loaded.document->ground(), loaded.textures))
                               : loaded.base;
            auto scene = std::make_shared<Environment>(
                compose_map_preview(*surface, loaded.document->instances(), loaded.resources));
            project_cache_.clear();
            terrain_selection_.clear();
            terrain_elements_.clear();
            terrain_box_ = false;
            terrain_click_.reset();
            asset_edit_ = false;
            stage_ = existing_mode_ ? AuthoringStage::Objects : AuthoringStage::Terrain;
            focus_library_ = true;
            ground_mode_ = !existing_mode_;
            library_mode_ = existing_mode_ ? 1 : 0;
            ground_transform_tool_ = Height;
            brush_stroke_ = {};
            brush_hover_.reset();
            clear_ground_thumbnails();
            ground_vertex_.reset();
            authoring_base_ = std::move(surface);
            ground_textures_ = std::move(loaded.textures);
            surface_area_ = loaded.document->catalog().area;
            surface_keys_.clear();
            surface_keys_[surface_area_] = std::move(loaded.surface_keys);
            library_maps_ = std::move(loaded.maps);
            library_area_ = loaded.document->catalog().area;
            ground_texture_ = ground_blend_texture_ = -1;
            ground_handle_ = 0;
            ground_drag_preview_.reset();
            region_end_.reset();
            selecting_ground_ = false;
            base_ = std::move(loaded.base);
            studio_document_.reset();
            studio_baseline_.reset();
            document_ = std::move(loaded.document);
            resources_ = std::move(loaded.resources);
            dump_ = std::move(loaded.dump);
            path_ = std::move(loaded.path);
            composed_ = std::move(scene);
            bind_project();
            shown_ = document_->instances();
            resource_ = -1;
            selected_ = 0;
            inspect_ = preview_ = false;
            tile_.reset();
            grid_key_.clear();
            grid_edit_ = document_->grid();
            map_camera_.fit(authoring_base_->low, authoring_base_->high);
            if (document_->ground() && !document_->ground()->triangles.empty())
                map_camera_.distance *= 1.4f;
            if (!document_->ground())
                if (auto start = base_->start_position())
                    map_camera_.focus_start(*start);
            new_ground_grid_ = {
                {map_camera_.target[0] - 400, map_camera_.target[2] - 400}, 100, 8, 8};
            new_ground_height_ = 0;
            ground_ceiling_ =
                document_->ground() ? authoring_base_->high[1] + 100000 : base_->high[1] + 100;
            cut_height_ = map_camera_.target[1] + 250;
            refresh_scene();
            status_ = path_.empty() ? "Source map ready. Create a flat surface in Ground tools or "
                                      "use Objects to place assets."
                                    : "Composition reopened.";
        } catch (const std::exception &error) {
            error_ = error.what();
            status_ = "Template could not be opened; the previous composition is retained.";
        }
    if (resource_job_.valid() &&
        resource_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            auto [resource, asset] = resource_job_.get();
            resources_[resource] = std::move(asset);
            resource_ = int(resource);
            inspect_ = true;
            asset_camera_.fit(resources_.at(resource).low, resources_.at(resource).high);
            renderer_.set_scene(std::make_shared<Environment>(resources_.at(resource)));
            status_ = "Resource preview ready. Return to Map to choose a tile.";
        } catch (const std::exception &error) {
            error_ = error.what();
            status_ = "Resource preview failed.";
        }
    renderer_.upload_step();
}
void MapAuthoringWorkspace::refresh_scene() {
    if (!document_)
        return;
    collision_editor_.reset();
    collision_renderer_.reset();
    sync_project_assets();
    shown_ = document_->instances();
    auto map_base = existing_mode_ && existing_editor_ && existing_editor_->scene() &&
                            existing_editor_->area() == document_->catalog().area
                        ? existing_editor_->scene() : base_;
    authoring_base_ = !existing_mode_ && document_->ground()
                          ? std::make_shared<Environment>(ground_preview(
                                *base_, document_->grid(),
                                ground_drag_preview_ ? *ground_drag_preview_ : *document_->ground(),
                                ground_textures_))
                          : map_base;
    composed_ =
        std::make_shared<Environment>(compose_map_preview(*authoring_base_, shown_, resources_));
    renderer_.set_scene(composed_);
    inspect_ = false;
    grid_key_.clear();
    if (tile_ && (tile_->x >= document_->grid().width || tile_->z >= document_->grid().height)) {
        tile_.reset();
        region_end_.reset();
    }
    if (ground_vertex_ && (ground_vertex_->x > document_->grid().width ||
                           ground_vertex_->z > document_->grid().height))
        ground_vertex_.reset();
    if (region_end_ &&
        (region_end_->x >= document_->grid().width || region_end_->z >= document_->grid().height))
        region_end_.reset();
}

void MapAuthoringWorkspace::begin_asset_edit() {
    asset_section_search_[0] = 0;
    require(resource_ >= 0 && resources_.contains(std::size_t(resource_)), "Load a resource first");
    if (const auto *asset = document_->project_asset(std::size_t(resource_)))
        asset_draft_ = *asset;
    else {
        asset_draft_ =
            begin_project_asset(std::size_t(resource_), resources_.at(std::size_t(resource_)));
        asset_draft_.name =
            document_->resource_name(std::size_t(resource_)).substr(0, 100) + " extract";
        const auto &scene = resources_.at(std::size_t(resource_));
        for (unsigned a = 0; a < 3; ++a)
            asset_draft_.pivot[a] = a == 1 ? scene.low[a] : (scene.low[a] + scene.high[a]) * .5f;
    }
    object_exchange_state_.reset();
    object_exchange_signature_.clear();
    object_exchange_path_.clear();
    refresh_asset_editor();
    asset_camera_.fit(asset_edit_scene_->low, asset_edit_scene_->high);
    asset_history_ = {asset_draft_};
    asset_history_cursor_ = 0;
    asset_faces_selected_.clear();
    asset_box_drag_ = false;
    asset_edit_ = true;
    error_.clear();
    status_ = "Select faces, trim the mesh, then save to Project assets.";
}
void MapAuthoringWorkspace::refresh_asset_editor() {
    asset_edit_geometry_ = std::make_shared<Environment>(
        project_asset_geometry(asset_source(asset_draft_), asset_draft_));
    auto scene = std::make_shared<Environment>(
        project_asset_preview(asset_source(asset_draft_), asset_draft_));
    renderer_.set_scene(scene);
    asset_edit_scene_ = std::move(scene);
    std::fill(std::begin(asset_name_), std::end(asset_name_), 0);
    asset_draft_.name.copy(asset_name_, 120);
}
void MapAuthoringWorkspace::remember_asset_edit() {
    if (asset_history_[asset_history_cursor_] == asset_draft_)
        return;
    asset_history_.resize(asset_history_cursor_ + 1);
    asset_history_.push_back(asset_draft_);
    if (asset_history_.size() > 64)
        asset_history_.erase(asset_history_.begin());
    asset_history_cursor_ = asset_history_.size() - 1;
}
void MapAuthoringWorkspace::asset_edit_history(bool redo) {
    if (redo ? asset_history_cursor_ + 1 >= asset_history_.size() : asset_history_cursor_ == 0)
        return;
    if (redo)
        ++asset_history_cursor_;
    else
        --asset_history_cursor_;
    for (unsigned a = 0; a < 3; ++a)
        asset_camera_.target[a] +=
            asset_draft_.pivot[a] - asset_history_[asset_history_cursor_].pivot[a];
    asset_draft_ = asset_history_[asset_history_cursor_];
    asset_faces_selected_.clear();
    refresh_asset_editor();
}
void MapAuthoringWorkspace::trim_asset_faces(bool keep) {
    auto candidate = asset_draft_;
    for (std::size_t section = 0; section < candidate.faces.size(); ++section)
        std::erase_if(candidate.faces[section], [&](auto face) {
            return asset_faces_selected_.contains({section, face}) != keep;
        });
    validate_project_asset(candidate, document_->catalog());
    project_asset_preview(asset_source(candidate), candidate);
    asset_draft_ = std::move(candidate);
    remember_asset_edit();
    asset_faces_selected_.clear();
    refresh_asset_editor();
    error_.clear();
}
void MapAuthoringWorkspace::save_asset_edit(bool copy) {
    require(!object_dialog_kind_, "Finish the object file dialog before saving to the library");
    auto candidate = asset_draft_;
    candidate.name = asset_name_;
    if (copy)
        candidate.id = 0;
    validate_project_asset(candidate, document_->catalog());
    project_asset_preview(asset_source(candidate), candidate);
    const auto key = document_->save_project_asset(std::move(candidate));
    asset_edit_ = false;
    asset_box_drag_ = false;
    asset_edit_scene_.reset();
    asset_history_.clear();
    selected_ = 0;
    inspect_ = false;
    set_stage(AuthoringStage::Objects);
    resource_ = int(key);
    refresh_scene();
    status_ = "Object saved to Project assets. Save the composition to keep it; select a map tile "
              "to place it.";
    error_.clear();
}
void MapAuthoringWorkspace::object_exchange_dialog(bool save, bool new_model, bool package) {
    if (object_dialog_kind_)
        return;
    object_dialog_kind_ = package ? (save ? 4 : 5) : new_model ? 3 : save ? 1 : 2;
    auto *owner = new std::shared_ptr<FolderSelection>(object_dialog_);
    static const SDL_DialogFileFilter filters[] = {
        {"USUMStudio asset or legacy object", "usum-asset;usum-object"},
        {"New Blender asset or legacy model", "usum-asset;usum-model"},
        {"USUMStudio asset", "usum-asset"}};
    const auto *filter = filters + (package ? 2 : new_model ? 1 : 0);
    auto callback = [](void *userdata, const char *const *files, int) {
        std::unique_ptr<std::shared_ptr<FolderSelection>> owner(
            static_cast<std::shared_ptr<FolderSelection> *>(userdata));
        auto &result = **owner;
        std::lock_guard lock(result.mutex);
        result.path.clear();
        result.error.clear();
        if (!files)
            result.error = SDL_GetError();
        else if (files[0])
            result.path = files[0];
        result.ready = true;
    };
    const char *initial = object_exchange_path_.empty() ? nullptr : object_exchange_path_.c_str();
    if (save)
        SDL_ShowSaveFileDialog(callback, owner, window_, filter, 1, initial);
    else
        SDL_ShowOpenFileDialog(callback, owner, window_, filter, 1, initial, false);
}
void MapAuthoringWorkspace::object_exchange_result() {
    std::string file, failure;
    {
        std::lock_guard lock(object_dialog_->mutex);
        if (!object_dialog_->ready)
            return;
        object_dialog_->ready = false;
        file = object_dialog_->path;
        failure = object_dialog_->error;
    }
    const auto kind = object_dialog_kind_;
    object_dialog_kind_ = 0;
    require(failure.empty(), failure);
    if (file.empty())
        return;
    auto path = std::filesystem::u8path(file);
    if (kind == 4) {
        require(document_ && package_export_, "Choose a project asset to export");
        if (path.extension().empty())
            path += ".usum-asset";
        auto bytes = export_asset_package(dump_, document_->catalog(), *package_export_);
        write_file_atomic(path, bytes);
        package_export_.reset();
        error_.clear();
        status_ = "Asset exported. Share the .usum-asset file; no sidecar files are needed.";
        return;
    }
    if (kind == 5) {
        require(bool(document_), "Open a composition before importing an asset");
        auto asset = import_asset_package(read_file(path));
        const auto base_name = asset.name;
        unsigned suffix = 2;
        while (std::any_of(document_->project_assets().begin(), document_->project_assets().end(),
                           [&](const auto &a) {
                               return a.name == asset.name;
                           }))
            asset.name = base_name.substr(0, 100) + " (" + std::to_string(suffix++) + ")";
        const auto key = document_->save_project_asset(std::move(asset));
        sync_project_assets();
        set_stage(AuthoringStage::Objects);
        load_resource(key);
        error_.clear();
        status_ = "Asset imported. Place it on the map or choose Edit in Studio.";
        return;
    }
    if (kind == 3) {
        auto bytes = read_file(path);
        auto incoming = parse_model_exchange(
            is_asset_package(bytes) ? text(decode_asset_package(bytes).at("models/0.usum-model"))
                                    : text(bytes));
        require(incoming.standalone && incoming.joints.empty(),
                "Use Blender’s new-model export with unrigged meshes for a static asset");
        new_object_materials_.assign(incoming.meshes.size(), 0);
        new_object_ = std::move(incoming);
        ImGui::OpenPopup("Import new static model");
        return;
    }
    if (kind == 1) {
        if (path.extension().empty())
            path += ".usum-asset";
        write_file_atomic(path, export_asset_package(dump_, document_->catalog(), asset_draft_));
        object_exchange_state_ = asset_draft_;
        object_exchange_path_ = path.string();
        status_ = "Asset exported. Open it with the USUMStudio model add-on in Blender.";
    } else {
        require(object_exchange_state_ && *object_exchange_state_ == asset_draft_,
                "Object changed since export/import. Undo those edits or export a fresh Blender "
                "object first.");
        const auto bytes = read_file(path);
        auto candidate =
            is_asset_package(bytes)
                ? import_asset_package(bytes)
                : import_object_exchange(text(bytes), asset_draft_, object_exchange_signature_);
        candidate.id = asset_draft_.id;
        candidate.name = asset_draft_.name;
        validate_project_asset(candidate, document_->catalog());
        project_asset_preview(asset_source(candidate), candidate);
        asset_draft_ = candidate;
        object_exchange_state_ = candidate;
        asset_faces_selected_.clear();
        refresh_asset_editor();
        remember_asset_edit();
        asset_camera_.fit(asset_edit_scene_->low, asset_edit_scene_->high);
        status_ = "Edited object imported. Undo edit restores the previous mesh; save to the asset "
                  "library when ready.";
    }
    error_.clear();
}
void MapAuthoringWorkspace::new_object_import() {
    ImGui::SetNextWindowSize(ImVec2(620, 450), ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal("Import new static model", nullptr, ImGuiWindowFlags_None))
        return;
    if (new_object_) {
        const auto source = project_asset_geometry(asset_source(asset_draft_), asset_draft_);
        ImGui::TextWrapped(
            "Assign game materials to the new Blender geometry. This edits the asset draft; save "
            "it to the asset library to place it on the map. The source map model is unchanged.");
        ImGui::BeginChild("Static import assignments", ImVec2(0, -85));
        auto label = [&](std::size_t i) {
            return source.draws[i].mesh + " / " +
                   source.materials.at(source.draws[i].material).name;
        };
        for (std::size_t i = 0; i < new_object_->meshes.size(); ++i) {
            ImGui::PushID(int(i));
            ImGui::TextWrapped("%s", new_object_->meshes[i].name.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##Game material", label(new_object_materials_[i]).c_str())) {
                for (std::size_t j = 0; j < source.draws.size(); ++j)
                    if (ImGui::Selectable((label(j) + "##" + std::to_string(j)).c_str(),
                                          new_object_materials_[i] == j))
                        new_object_materials_[i] = j;
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        if (ImGui::Button("Import geometry")) {
            try {
                auto candidate =
                    import_new_project_asset(*new_object_, asset_draft_, new_object_materials_);
                validate_project_asset(candidate, document_->catalog());
                project_asset_preview(source, candidate);
                asset_draft_ = std::move(candidate);
                object_exchange_state_.reset();
                asset_faces_selected_.clear();
                refresh_asset_editor();
                remember_asset_edit();
                asset_camera_.fit(asset_edit_scene_->low, asset_edit_scene_->high);
                new_object_.reset();
                error_.clear();
                status_ = "New static model imported. Name it and save to the asset library, then "
                          "place it on the map.";
                ImGui::CloseCurrentPopup();
            } catch (const std::exception &e) {
                error_ = e.what();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            new_object_.reset();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}
void MapAuthoringWorkspace::asset_editor(std::uint32_t frame) {
    try {
        object_exchange_result();
        new_object_import();
    } catch (const std::exception &e) {
        error_ = e.what();
    }
    ImGui::Begin("Composition");
    ImGui::TextUnformatted("Extract object");
    ImGui::TextWrapped("Trim faces and choose a pivot. Your source resource stays intact.");
    ImGui::End();
    ImGui::Begin("Asset Library");
    ImGui::SeparatorText("Mesh sections");
    ImGui::TextWrapped("Click a section to select it. Ctrl+click adds or removes sections.");
    if (studio::TutorialWidgets::Button("map_authoring_workspace", "Select all")) {
        asset_faces_selected_.clear();
        for (std::size_t s = 0; s < asset_draft_.faces.size(); ++s)
            for (auto f : asset_draft_.faces[s])
                asset_faces_selected_.insert({s, f});
    }
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("map_authoring_workspace", "Clear"))
        asset_faces_selected_.clear();
    const auto &source = *asset_edit_geometry_;
    ImGui::InputTextWithHint("##mesh-section-search", "Find mesh or material",
                             asset_section_search_, sizeof(asset_section_search_));
    ImGui::BeginChild("Sections");
    for (std::size_t s = 0; s < asset_draft_.faces.size(); ++s) {
        if (asset_draft_.faces[s].empty())
            continue;
        ImGui::PushID(int(s));
        const auto &draw = source.draws[s];
        const auto label = draw.mesh + " / " + source.materials.at(draw.material).name + " (" +
                           std::to_string(asset_draft_.faces[s].size()) + " faces)";
        if (lower(label).find(lower(asset_section_search_)) == std::string::npos) {
            ImGui::PopID();
            continue;
        }
        const auto selected_faces = std::count_if(
            asset_draft_.faces[s].begin(), asset_draft_.faces[s].end(), [&](auto face) {
                return asset_faces_selected_.contains({s, face});
            });
        const bool fully_selected = std::size_t(selected_faces) == asset_draft_.faces[s].size();
        auto row_label = label;
        if (selected_faces && !fully_selected)
            row_label += " / " + std::to_string(selected_faces) + " selected";
        if (ImGui::Selectable(row_label.c_str(), selected_faces != 0)) {
            const bool toggle_off = ImGui::GetIO().KeyCtrl && fully_selected;
            if (!ImGui::GetIO().KeyCtrl)
                asset_faces_selected_.clear();
            for (auto f : asset_draft_.faces[s])
                if (toggle_off)
                    asset_faces_selected_.erase({s, f});
                else
                    asset_faces_selected_.insert({s, f});
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
    ImGui::Begin("Authoring tools");
    try {
        ImGui::SeparatorText(asset_draft_.id ? "Edit project asset" : "New project asset");
        ImGui::InputText("Name", asset_name_, sizeof(asset_name_));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            asset_draft_.name = asset_name_;
            remember_asset_edit();
        }
        std::size_t faces = 0, vertices = 0;
        for (const auto &draw : asset_edit_scene_->draws) {
            faces += draw.indices.size() / 3;
            vertices += draw.vertices.size();
        }
        ImGui::Text("%zu vertices / %zu faces", vertices, faces);
        ImGui::Text("%zu faces selected", asset_faces_selected_.size());
        ImGui::BeginDisabled(asset_faces_selected_.empty());
        const bool keep =
                       studio::TutorialWidgets::Button("map_authoring_workspace", "Keep selected"),
                   remove = (ImGui::SameLine(), studio::TutorialWidgets::Button(
                                                    "map_authoring_workspace", "Delete selected"));
        ImGui::EndDisabled();
        if (keep || remove)
            trim_asset_faces(keep);
        ImGui::SeparatorText("Pivot");
        auto pivot = asset_draft_.pivot;
        bool changed = ImGui::InputFloat3("Position", pivot.data());
        if (studio::TutorialWidgets::Button("map_authoring_workspace", "Bottom center")) {
            for (unsigned a = 0; a < 3; ++a)
                pivot[a] =
                    asset_draft_.pivot[a] +
                    (a == 1 ? asset_edit_scene_->low[a]
                            : (asset_edit_scene_->low[a] + asset_edit_scene_->high[a]) * .5f);
            changed = true;
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("map_authoring_workspace", "Center")) {
            for (unsigned a = 0; a < 3; ++a)
                pivot[a] = asset_draft_.pivot[a] +
                           (asset_edit_scene_->low[a] + asset_edit_scene_->high[a]) * .5f;
            changed = true;
        }
        if (changed) {
            auto candidate = asset_draft_;
            candidate.pivot = pivot;
            validate_project_asset(candidate, document_->catalog());
            project_asset_preview(asset_source(candidate), candidate);
            for (unsigned a = 0; a < 3; ++a)
                asset_camera_.target[a] += asset_draft_.pivot[a] - candidate.pivot[a];
            asset_draft_ = std::move(candidate);
            refresh_asset_editor();
            remember_asset_edit();
        }
        ImGui::TextWrapped(
            "Pivot controls rotation. Placements sit on the ground using the object's bottom.");
        ImGui::BeginDisabled(asset_history_cursor_ == 0);
        const bool undo = studio::TutorialWidgets::Button("map_authoring_workspace", "Undo edit");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(asset_history_cursor_ + 1 >= asset_history_.size());
        const bool redo = studio::TutorialWidgets::Button("map_authoring_workspace", "Redo edit");
        ImGui::EndDisabled();
        if (undo || redo)
            asset_edit_history(redo);
        if (studio::TutorialWidgets::CollapsingHeader("map_authoring_workspace",
                                                      "Blender asset exchange")) {
            ImGui::TextWrapped(
                "Export this object, edit it with the USUMStudio Blender add-on, then import the "
                "edited file. UVs and source material assignments are retained.");
            ImGui::BeginDisabled(object_dialog_kind_ != 0);
            if (studio::TutorialWidgets::Button("map_authoring_workspace",
                                                "Export asset for Blender...", {-1, 0}))
                object_exchange_dialog(true);
            if (studio::TutorialWidgets::Button("map_authoring_workspace",
                                                "Import new Blender model...", {-1, 0}))
                object_exchange_dialog(false, true);
            ImGui::BeginDisabled(!object_exchange_state_);
            if (studio::TutorialWidgets::Button("map_authoring_workspace", "Import edited asset...",
                                                {-1, 0}))
                object_exchange_dialog(false);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::TextWrapped("The asset package includes textures for Blender previews. "
                               "Materials keep their game settings on reimport.");
        }
        ImGui::Separator();
        if (asset_draft_.id)
            ImGui::TextWrapped("Saving updates every placement of this asset. Save as new creates "
                               "an independent variant.");
        const bool save_asset = studio::TutorialWidgets::Button("map_authoring_workspace",
                                                                "Save to asset library", {-1, 0});
        const bool copy =
            asset_draft_.id && studio::TutorialWidgets::Button("map_authoring_workspace",
                                                               "Save as new asset", {-1, 0});
        const bool cancel =
            studio::TutorialWidgets::Button("map_authoring_workspace", "Cancel edit", {-1, 0});
        if (save_asset || copy)
            save_asset_edit(copy);
        else if (cancel) {
            asset_edit_ = false;
            asset_box_drag_ = false;
            asset_edit_scene_.reset();
            asset_history_.clear();
            inspect_ = false;
            refresh_scene();
            status_ = "Asset edit canceled.";
            error_.clear();
        }
    } catch (const std::exception &error) {
        error_ = error.what();
    }
    ImGui::End();
    if (asset_edit_)
        asset_editor_viewport(frame);
    ImGui::Begin("Authoring tools");
    ImGui::TextWrapped("%s", status_.c_str());
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    ImGui::End();
}

void MapAuthoringWorkspace::sync_project_assets() {
    std::set<std::size_t> used;
    for (const auto &asset : document_->project_assets()) {
        const auto key = document_->project_resource(asset.id);
        used.insert(key);
        auto cached = project_cache_.find(key);
        if (cached == project_cache_.end() || cached->second != asset) {
            resources_[key] = project_asset_preview(asset_source(asset), asset);
            project_cache_[key] = asset;
        }
    }
    for (auto it = resources_.begin(); it != resources_.end();)
        if (it->first >= document_->catalog().entries.size() && !used.contains(it->first))
            it = resources_.erase(it);
        else
            ++it;
    for (auto it = project_cache_.begin(); it != project_cache_.end();)
        if (!used.contains(it->first))
            it = project_cache_.erase(it);
        else
            ++it;
    if (resource_ >= 0 && std::size_t(resource_) >= document_->catalog().entries.size() &&
        !document_->project_asset(std::size_t(resource_)))
        resource_ = -1;
}
void MapAuthoringWorkspace::cancel_preview() {
    if (!preview_)
        return;
    preview_ = false;
    preview_id_ = 0;
    refresh_scene();
}
void MapAuthoringWorkspace::preview_transform() {
    require(composed_ && !inspect_, "Return to Map before editing a placement");
    const auto matrix = composition_transform(draft_);
    auto it = std::find_if(shown_.begin(), shown_.end(), [&](const auto &instance) {
        return instance.id == (preview_ ? preview_id_ : selected_);
    });
    require(it != shown_.end(), "Select a placed asset first");
    preview_id_ = it->id;
    preview_ = true;
    it->transform = draft_;
    composed_->placement_transforms.at(base_->placement_transforms.size() +
                                       std::size_t(it - shown_.begin())) = matrix;
    renderer_.invalidate_selection_readback();
}
void MapAuthoringWorkspace::select_instance(std::uint64_t id) {
    cancel_preview();
    selected_ = id;
    auto it = std::find_if(document_->instances().begin(), document_->instances().end(),
                           [&](const auto &instance) {
                               return instance.id == id;
                           });
    if (it == document_->instances().end()) {
        selected_ = 0;
        return;
    }
    resource_ = int(it->resource);
    draft_ = it->transform;
    if (inspect_)
        refresh_scene();
}
void MapAuthoringWorkspace::place_preview() {
    require(document_ && (existing_mode_ ? bool(surface_position_) : bool(tile_)) && resource_ >= 0 && resources_.contains(std::size_t(resource_)),
            "Choose an asset and a placement point first");
    require(document_->can_place(std::size_t(resource_)),
            "Extract baked scenery into a project asset before placement");
    require(std::isfinite(height_offset_), "Height offset must be finite");
    auto ground = existing_mode_ ? surface_position_ :
        tile_ground_position(document_->grid(), *tile_, authoring_base_->spatial, ground_ceiling_);
    if (!existing_mode_ && document_->ground()) {
        const auto a = ground_vertex(document_->grid(), *document_->ground(), tile_->x,
                                     tile_->z + 1),
                   b = ground_vertex(document_->grid(), *document_->ground(), tile_->x + 1,
                                     tile_->z);
        ground = SpatialPoint{(a[0] + b[0]) * .5f, (a[1] + b[1]) * .5f, (a[2] + b[2]) * .5f};
        if ((*ground)[1] > ground_ceiling_)
            ground.reset();
    }
    require(bool(ground),
            "This tile has no ground below the selected height; choose another tile or floor");
    cancel_preview();
    inspect_ = false;
    selected_ = 0;
    draft_.position = *ground;
    draft_.position[1] += height_offset_ - resources_.at(std::size_t(resource_)).low[1];
    composition_transform(draft_);
    shown_ = document_->instances();
    shown_.push_back({0, std::size_t(resource_), draft_});
    composed_ =
        std::make_shared<Environment>(compose_map_preview(*authoring_base_, shown_, resources_));
    renderer_.set_scene(composed_);
    preview_ = true;
    preview_id_ = 0;
}
void MapAuthoringWorkspace::request_leave(std::function<void()> action) {
    if (project_store()) {
        try {
            save_editor_project();
            action();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
        return;
    }
    if (asset_edit_) {
        status_ = "Save to the asset library or cancel the asset edit before leaving.";
        return;
    }
    cancel_ground_drag();
    if (busy()) {
        status_ = "Finish or cancel the current loading/file operation before continuing.";
        return;
    }
    cancel_preview();
    if (!document_ || !document_->dirty()) {
        action();
        return;
    }
    leave_action_ = std::move(action);
    leave_modal_ = true;
}
void MapAuthoringWorkspace::file_dialog(bool save_file) {
    if (busy())
        return;
    require(!save_file || !preview_, "Apply or cancel the preview before saving");
    dialog_kind_ = save_file ? 1 : 2;
    {
        std::lock_guard lock(dialog_->mutex);
        dialog_->pending = true;
        dialog_->ready = false;
        dialog_->path.clear();
        dialog_->error.clear();
    }
    auto *owner = new std::shared_ptr<FolderSelection>(dialog_);
    static const SDL_DialogFileFilter filter[] = {{"Map composition", "usum-composition"}};
    auto callback = [](void *userdata, const char *const *files, int) {
        std::unique_ptr<std::shared_ptr<FolderSelection>> owner(
            static_cast<std::shared_ptr<FolderSelection> *>(userdata));
        auto &result = **owner;
        std::lock_guard lock(result.mutex);
        if (!files)
            result.error = SDL_GetError();
        else if (files[0])
            result.path = files[0];
        result.pending = false;
        result.ready = true;
    };
    const auto initial = path_.string();
    if (save_file)
        SDL_ShowSaveFileDialog(callback, owner, window_, filter, 1,
                               initial.empty() ? nullptr : initial.c_str());
    else
        SDL_ShowOpenFileDialog(callback, owner, window_, filter, 1,
                               initial.empty() ? nullptr : initial.c_str(), false);
}
void MapAuthoringWorkspace::save() {
    if (save_editor_project())
        return;
    require(document_ && !preview_, "Apply or cancel the placement preview before saving");
    if (path_.empty()) {
        file_dialog(true);
        return;
    }
    document_->save(path_);
    status_ = "Composition saved: " + path_.string();
    if (save_then_leave_) {
        save_then_leave_ = false;
        auto action = std::move(leave_action_);
        if (action)
            action();
    }
}
void MapAuthoringWorkspace::dialogs() {
    std::string path, error;
    int kind = 0;
    {
        std::lock_guard lock(dialog_->mutex);
        if (dialog_->ready) {
            dialog_->ready = false;
            path = dialog_->path;
            error = dialog_->error;
            kind = dialog_kind_;
            dialog_kind_ = 0;
        }
    }
    if (kind) {
        try {
            require(error.empty(), error);
            if (path.empty()) {
                save_then_leave_ = false;
                leave_action_ = {};
            } else if (kind == 1) {
                auto previous = path_;
                path_ = std::filesystem::u8path(path);
                if (path_.extension() != ".usum-composition")
                    path_ += ".usum-composition";
                try {
                    save();
                } catch (...) {
                    path_ = previous;
                    throw;
                }
            } else
                open_template(dump_, document_ ? document_->catalog().area : 0,
                              std::filesystem::u8path(path));
        } catch (const std::exception &failure) {
            error_ = failure.what();
            save_then_leave_ = false;
        }
    }
    if (leave_modal_) {
        ImGui::OpenPopup("Unsaved composition");
        leave_modal_ = false;
    }
    if (ImGui::BeginPopupModal("Unsaved composition", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save the composition before continuing?");
        if (studio::TutorialWidgets::Button("map_authoring_workspace", "Save")) {
            ImGui::CloseCurrentPopup();
            save_then_leave_ = true;
            try {
                save();
            } catch (const std::exception &error) {
                error_ = error.what();
                save_then_leave_ = false;
            }
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("map_authoring_workspace", "Discard")) {
            ImGui::CloseCurrentPopup();
            auto action = std::move(leave_action_);
            if (action)
                action();
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("map_authoring_workspace", "Cancel")) {
            ImGui::CloseCurrentPopup();
            leave_action_ = {};
        }
        ImGui::EndPopup();
    }
}
void MapAuthoringWorkspace::draw(std::uint32_t frame, const std::filesystem::path &dump,
                                 unsigned selected_area) {
    poll();
    if (!existing_editor_)
        existing_editor_ = std::make_unique<ExistingMapEditor>(shaders_, window_);
    if (!document_ && !existing_editor_->scene()) {
        focus_library_ = false;
        if (!authoring_seen_ || frame != last_authoring_frame_ + 1)
            ImGui::SetNextWindowFocus();
    }
    authoring_seen_ = true;
    last_authoring_frame_ = frame;
    ImGui::Begin("Composition");
    ImGui::BeginDisabled(busy() || preview_ || ground_handle_ || existing_editor_->busy());
    bool changed_mode = false;
    const bool previous_mode = existing_mode_;
    if (ImGui::RadioButton("Existing map", existing_mode_)) {
        changed_mode = !existing_mode_;
        existing_mode_ = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Grid terrain", !existing_mode_)) {
        changed_mode = existing_mode_;
        existing_mode_ = false;
    }
    if (changed_mode) {
        try {
            require(save_editor_project(), "Save edits before changing authoring mode");
            surface_position_.reset();
            refresh_scene();
        } catch (const std::exception &e) {
            existing_mode_ = previous_mode;
            error_ = e.what();
        }
    }
    ImGui::EndDisabled();
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    ImGui::End();
    if (!asset_edit_) {
        ImGui::Begin("Authoring viewport");
        ImGui::BeginDisabled(busy() || preview_ || ground_handle_ || existing_editor_->busy());
        const char *stages[] = {"1  Terrain", "2  Surfaces", "3  Objects", "4  Review"};
        for (int i = 0; i < 4; ++i) {
            if (i)
                ImGui::SameLine();
            if (int(stage_) == i)
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            bool clicked = ImGui::Button(stages[i], {104, 28});
            if (int(stage_) == i)
                ImGui::PopStyleColor();
            if (clicked) {
                auto previous_stage = stage_;
                set_stage(AuthoringStage(i));
                try {
                    if (existing_mode_ && i < 2 && previous_stage >= AuthoringStage::Objects &&
                        document_ && existing_editor_->scene() &&
                        existing_editor_->area() == document_->catalog().area) {
                        existing_editor_->camera() = map_camera_;
                        auto instances = document_->instances();
                        auto resources = resources_;
                        existing_editor_->context(
                            [instances = std::move(instances),
                             resources = std::move(resources)](const Environment &scene) {
                                return compose_map_preview(scene, instances, resources);
                            });
                    }
                    if (existing_mode_ && i >= 2) {
                        if ((!document_ ||
                             (existing_editor_->scene() &&
                              document_->catalog().area != existing_editor_->area())) &&
                            !busy())
                            open_template(dump, existing_editor_->scene() ? existing_editor_->area()
                                                                          : selected_area);
                        else {
                            if (previous_stage < AuthoringStage::Objects &&
                                existing_editor_->scene())
                                map_camera_ = existing_editor_->camera();
                            refresh_scene();
                        }
                    }
                } catch (const std::exception &e) {
                    error_ = e.what();
                }
            }
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::End();
    }
    if (existing_mode_ && stage_ < AuthoringStage::Objects && !asset_edit_) {
        existing_editor_->surface_tools(stage_ == AuthoringStage::Surfaces);
        existing_editor_->draw(frame, dump, selected_area);
        return;
    }
    if (asset_edit_) {
        asset_editor(frame);
        return;
    }
    ImGui::Begin("Composition");
    try {
        ImGui::BeginDisabled(busy() || ground_handle_);
        if (studio::TutorialWidgets::Button("map_authoring_workspace", "Load selected map"))
            request_leave([this, dump, selected_area] {
                open_template(dump, selected_area);
            });
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Uses the selected map's original dump resources.");
        if (studio::TutorialWidgets::Button("map_authoring_workspace", "Open composition..."))
            request_leave([this, dump] {
                if (!document_)
                    dump_ = dump;
                file_dialog(false);
            });
        ImGui::BeginDisabled(!document_ || preview_);
        if (studio::TutorialWidgets::Button("map_authoring_workspace",
                                            project_store() ? "Save Project" : "Save")) {
            try {
                save();
            } catch (const std::exception &error) {
                error_ = error.what();
            }
        }
        ImGui::SameLine();
        if (!project_store() &&
            studio::TutorialWidgets::Button("map_authoring_workspace", "Save as..."))
            file_dialog(true);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (busy()) {
            ImGui::TextUnformatted(review_job_.valid() ? "Checking export..."
                                                       : "Loading / file operation...");
            if (load_job_.valid() || resource_job_.valid()) {
                ImGui::SameLine();
                if (studio::TutorialWidgets::Button("map_authoring_workspace", "Cancel loading"))
                    cancel_ = true;
            }
        }
        if (document_) {
            ImGui::SeparatorText("Template and document");
            if (!base_->locations.empty())
                ImGui::TextWrapped("%s", base_->locations.front().name.c_str());
            ImGui::Text("Field area %u | %s", document_->catalog().area,
                        document_->dirty() ? "Unsaved changes"
                        : path_.empty()    ? "Unmodified template"
                                           : "Saved");
            if (ImGui::TreeNode("Document location")) {
                ImGui::TextWrapped("%s",
                                   path_.empty() ? "Untitled composition" : path_.string().c_str());
                ImGui::TextWrapped("Dump: %s", dump_.string().c_str());
                ImGui::TreePop();
            }
            ImGui::BeginDisabled(busy() || preview_ || ground_handle_);
            ImGui::BeginDisabled(!document_->can_undo());
            if (studio::TutorialWidgets::Button("map_authoring_workspace", "Undo")) {
                document_->undo();
                selected_ = 0;
                terrain_elements_.clear();
                terrain_selection_.clear();
                terrain_box_ = false;
                grid_edit_ = document_->grid();
                refresh_scene();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!document_->can_redo());
            if (studio::TutorialWidgets::Button("map_authoring_workspace", "Redo")) {
                document_->redo();
                selected_ = 0;
                terrain_elements_.clear();
                terrain_selection_.clear();
                terrain_box_ = false;
                grid_edit_ = document_->grid();
                refresh_scene();
            }
            ImGui::EndDisabled();
            if (!document_->ground() &&
                studio::TutorialWidgets::CollapsingHeader("map_authoring_workspace", "Tile grid")) {
                ImGui::TextUnformatted("Tile size");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat("##tile-size", &grid_edit_.tile_size, 10, 100);
                ImGui::TextUnformatted("Grid origin X / Z");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat2("##grid-origin", grid_edit_.origin.data());
                ImGui::SetNextItemWidth(100);
                ImGui::InputInt("Columns", &grid_edit_.width);
                ImGui::SetNextItemWidth(100);
                ImGui::InputInt("Rows", &grid_edit_.height);
                if (studio::TutorialWidgets::Button("map_authoring_workspace", "Apply grid")) {
                    try {
                        document_->set_grid(grid_edit_);
                        tile_.reset();
                    } catch (const std::exception &error) {
                        error_ = error.what();
                    }
                }
                ImGui::SameLine();
                if (studio::TutorialWidgets::Button("map_authoring_workspace",
                                                    "Reset grid controls"))
                    grid_edit_ = document_->grid();
                ImGui::TextWrapped("Grid changes do not move existing placements.");
            }
            ImGui::EndDisabled();
            ImGui::TextWrapped(
                document_->ground()
                    ? "Authored ground is shown in isolation with your added objects. Review can "
                      "stage the ground, its collision and supported static objects."
                    : "The source map supplies textures and objects. Use Ground tools to create a "
                      "flat authored surface. Review contains the supported ground patch export "
                      "settings.");
        }
        auto &io = ImGui::GetIO();
        if (document_ && !busy() && !preview_ && !ground_handle_ && !io.WantTextInput &&
            io.KeyCtrl) {
            if ((!project_store() && ImGui::IsKeyPressed(ImGuiKey_S, false)))
                save();


        }
    } catch (const std::exception &error) {
        error_ = error.what();
    }
    dialogs();
    ImGui::End();
    browser();
    details();
    viewport(frame);
    ImGui::Begin("Authoring tools");
    ImGui::TextWrapped("%s", status_.c_str());
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    if (document_ && !document_->catalog().issues.empty())
        for (const auto &issue : document_->catalog().issues)
            ImGui::TextWrapped("%s", issue.c_str());
    ImGui::End();
}
void MapAuthoringWorkspace::publish_library_asset(bool replace) {
    auto *store = project_store();
    require(store && document_ && resource_ >= 0, "Open a project and select a composition asset");
    capture_asset_studio();
    auto asset = document_->project_asset(std::size_t(resource_));
    require(asset != nullptr, "Select a composition asset to publish");
    const auto asset_name = asset->name;
    auto package = export_asset_package(dump_, document_->catalog(), *asset);
    import_asset_package(package);
    require(save_editor_project(), "Save the composition before publishing its asset");
    auto key = replace ? library_asset_key_ : "asset-library/" + sha256(package);
    if (replace)
        require(store->edits.contains(key) && store->edits.at(key).kind == "asset-library",
                "Select a project library entry to update");
    if (!replace) {
        const auto first_key = key;
        unsigned suffix = 2;
        while (store->edits.contains(key) && store->edits.at(key).blob != sha256(package))
            key = first_key + "/" + std::to_string(suffix++);
    }
    auto label = replace ? store->edits.at(key).label : asset_name;
    if (!replace) {
        unsigned suffix = 2;
        while (std::any_of(store->edits.begin(), store->edits.end(), [&](auto &item) {
            return item.first != key && item.second.kind == "asset-library" &&
                   item.second.label == label;
        }))
            label = asset_name.substr(0, 100) + " (" + std::to_string(suffix++) + ")";
    }
    auto previous = store->edits;
    try {
        store->capture({key, "asset-library", label, "", {}}, package);
        store->save();
    } catch (...) {
        store->edits = std::move(previous);
        throw;
    }
    library_asset_key_ = key;
    status_ =
        replace ? "Project library updated. Existing composition copies retain their current edits."
                : "Asset published to the project library. Open another composition to add a copy.";
    error_.clear();
}
void MapAuthoringWorkspace::project_library() {
    ImGui::SeparatorText("Project library");
    auto *store = project_store();
    if (!store) {
        ImGui::TextWrapped("Open a project to share assets between its maps.");
        return;
    }
    if (library_project_root_ != store->root) {
        library_project_root_ = store->root;
        library_asset_key_.clear();
        library_search_[0] = 0;
    }
    ImGui::TextDisabled("Available to every composition in this project");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##project-asset-filter", "Find project asset", library_search_,
                             sizeof(library_search_));
    unsigned total = 0, visible = 0;
    ImGui::BeginDisabled(busy() || preview_ || ground_handle_);
    ImGui::BeginChild("Shared assets", {0, 150}, ImGuiChildFlags_Borders);
    for (auto &[key, edit] : store->edits) {
        if (edit.kind != "asset-library")
            continue;
        ++total;
        if (lower(edit.label).find(lower(library_search_)) == std::string::npos)
            continue;
        ++visible;
        ImGui::PushID(key.c_str());
        if (ImGui::Selectable(edit.label.c_str(), library_asset_key_ == key))
            library_asset_key_ = key;
        ImGui::PopID();
    }
    if (!total)
        ImGui::TextWrapped("Select a composition asset, then Publish to project library.");
    else if (!visible)
        ImGui::TextDisabled("No matching assets");
    ImGui::EndChild();
    auto selected = store->edits.find(library_asset_key_);
    const bool available =
        selected != store->edits.end() && selected->second.kind == "asset-library";
    ImGui::BeginDisabled(!available);
    if (ImGui::Button("Add copy to composition", {-1, 0})) {
        try {
            auto asset = import_asset_package(read_file(store->document(library_asset_key_)));
            const auto original_name = asset.name;
            unsigned suffix = 2;
            while (std::any_of(document_->project_assets().begin(),
                               document_->project_assets().end(), [&](auto &existing) {
                                   return existing.name == asset.name;
                               }))
                asset.name = original_name.substr(0, 100) + " (" + std::to_string(suffix++) + ")";
            asset.id = 0;
            auto resource = document_->save_project_asset(std::move(asset));
            sync_project_assets();
            set_stage(AuthoringStage::Objects);
            load_resource(resource);
            error_.clear();
            status_ = "Library asset added. This composition owns an independent copy; save the "
                      "project to retain it.";
        } catch (const std::exception &error) {
            error_ = error.what();
        }
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
}
void MapAuthoringWorkspace::browser() {
    if (focus_library_) {
        ImGui::SetNextWindowFocus();
        focus_library_ = false;
    }
    ImGui::Begin("Asset Library");
    if (!document_) {
        ImGui::TextWrapped("Start from a map to browse resources from your dump.");
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("Dump resources | field area %u", document_->catalog().area);
    if (studio::TutorialWidgets::RadioButton("map_authoring_workspace", "Surfaces",
                                             library_mode_ == 0) &&
        !busy() && !preview_ && !ground_handle_)
        library_mode_ = 0;
    ImGui::SameLine();
    if (studio::TutorialWidgets::RadioButton("map_authoring_workspace", "Objects",
                                             library_mode_ == 1) &&
        !busy() && !preview_ && !ground_handle_)
        library_mode_ = 1;
    ImGui::Separator();
    if (library_mode_ == 0) {
        ground_browser();
        ImGui::End();
        return;
    }
    project_library();
    ImGui::SeparatorText("Composition assets");
    ImGui::TextDisabled("Drag an object onto the map to place it");
    ImGui::BeginDisabled(((busy() || preview_) && !object_drag_resource_) || ground_handle_);
    if (studio::TutorialWidgets::Button("map_authoring_workspace", "Import asset...", {-1, 0}))
        object_exchange_dialog(false, false, true);
    if (document_->project_assets().empty())
        ImGui::TextWrapped("Choose a dump object or baked terrain below, then Extract asset.");
    for (const auto &asset : document_->project_assets()) {
        const auto key = document_->project_resource(asset.id);
        ImGui::PushID(int(key));
        if (ImGui::Selectable(asset.name.c_str(), resource_ == int(key))) {
            set_stage(AuthoringStage::Objects);
            load_resource(key);
        }
        object_drag_source(key, asset.name);
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::SeparatorText("Game assets");
    std::string source_name = "Current map";
    for (const auto &location : library_maps_)
        if (location.area == int(library_area_)) {
            source_name = location.name + " (zone " + std::to_string(location.zone) + ")";
            break;
        }
    ImGui::BeginDisabled(((busy() || preview_) && !object_drag_resource_) || ground_handle_);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##source-map", source_name.c_str())) {
        std::set<int> shown;
        for (const auto &location : library_maps_) {
            if (location.area < 0 || location.zone < 0 || !shown.insert(location.area).second)
                continue;
            auto label = location.name + " (zone " + std::to_string(location.zone) + ")";
            if (ImGui::Selectable(label.c_str(), location.area == int(library_area_))) {
                const auto area = unsigned(location.area);
                const auto &entries = document_->catalog().entries;
                if (std::any_of(entries.begin(), entries.end(), [&](const auto &entry) {
                        return entry.area == area;
                    }))
                    library_area_ = area;
                else {
                    cancel_ = false;
                    const auto dump = dump_;
                    library_job_ = std::async(std::launch::async, [this, dump, area] {
                        return load_map_resources(dump, area, &cancel_);
                    });
                    status_ = "Reading source map assets...";
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::InputTextWithHint("##resource-search", "Search resource or mesh name", search_,
                             sizeof(search_));
    ImGui::Combo("Resource kind", &kind_,
                 "Static models\0Baked terrain\0Distant scenery\0All resources\0");
    const auto &entries = document_->catalog().entries;
    std::vector<std::size_t> matches;
    const auto filter = lower(search_);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto &entry = entries[i];
        if (entry.area != library_area_)
            continue;
        if (kind_ != 3 && int(entry.kind) != kind_)
            continue;
        bool match = filter.empty() || lower(entry.name).find(filter) != std::string::npos;
        for (const auto &mesh : entry.meshes)
            match |= lower(mesh.mesh).find(filter) != std::string::npos;
        if (match)
            matches.push_back(i);
    }
    std::stable_sort(matches.begin(), matches.end(), [&](auto a, auto b) {
        const auto &x = entries[a];
        const auto &y = entries[b];
        if (x.geometry_complete != y.geometry_complete)
            return x.geometry_complete;
        return x.vertices < y.vertices;
    });
    ImGui::TextDisabled("%zu resources | fewest vertices first", matches.size());
    ImGui::BeginDisabled(((busy() || preview_) && !object_drag_resource_) || ground_handle_);
    ImGui::BeginChild("Resource list", {0, 0}, ImGuiChildFlags_Borders);
    ImGuiListClipper clipper;
    clipper.Begin(int(matches.size()),
                  ImGui::GetTextLineHeightWithSpacing() * 2 + ImGui::GetStyle().ItemSpacing.y);
    while (clipper.Step())
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            auto i = matches[std::size_t(row)];
            const auto &entry = entries[i];
            ImGui::PushID(int(i));
            const auto label =
                entry.name + "\n" +
                (entry.geometry_complete ? std::to_string(entry.vertices) + " verts / " +
                                               std::to_string(entry.triangles) + " tris / " +
                                               std::to_string(entry.draws) + " draws"
                                         : "Unsupported geometry");
            if (ImGui::Selectable(label.c_str(), resource_ == int(i), 0,
                                  {0, ImGui::GetTextLineHeightWithSpacing() * 2})) {
                set_stage(AuthoringStage::Objects);
                load_resource(i);
            }
            if (document_->can_place(i)) object_drag_source(i, entry.name);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "%s", entry.reusable_static()
                              ? existing_mode_ ? "Click to inspect; return to Map to choose a surface point." : "Click to inspect; return to Map to place on a tile."
                          : entry.kind != MapResourceKind::StaticModel
                              ? "Inspect baked scenery, then Extract asset to trim and save a "
                                "reusable object."
                              : "This resource has validation issues; inspect its details.");
            ImGui::PopID();
        }
    ImGui::EndChild();
    ImGui::EndDisabled();
    ImGui::End();
}
void MapAuthoringWorkspace::details() {
    ImGui::Begin("Authoring tools");
    if (!document_) {
        ImGui::TextUnformatted("No composition open.");
        ImGui::End();
        return;
    }
    stage_toolbar();
    if (stage_ == AuthoringStage::Review) {
        ImGui::BeginDisabled(busy() || ground_handle_);
        review_tools();
        ImGui::EndDisabled();
        ImGui::End();
        return;
    }
    if (ground_mode_) {
        ground_tools();
        ImGui::End();
        return;
    }
    ImGui::SeparatorText("Objects and placements");
    try {
        if (resource_ >= 0 && document_->project_asset(std::size_t(resource_))) {
            const auto *asset = document_->project_asset(std::size_t(resource_));
            ImGui::TextWrapped("Project asset: %s", asset->name.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Editing updates placed copies. Save as new asset creates an "
                                  "independent variant.");
        }
        if (resource_ >= 0 && std::size_t(resource_) < document_->catalog().entries.size()) {
            const auto &entry = document_->catalog().entries.at(std::size_t(resource_));
            ImGui::TextWrapped("%s", entry.name.c_str());
            ImGui::Text("%zu vertices | %zu triangles", entry.vertices, entry.triangles);
            ImGui::Text("%zu draw sections | %zu materials", entry.draws, entry.materials);
            if (entry.kind != MapResourceKind::StaticModel)
                ImGui::TextWrapped(
                    "Baked scenery needs extraction before it can be placed as a prop.");
            for (const auto &issue : entry.issues)
                ImGui::TextWrapped("%s", issue.c_str());
            if (studio::TutorialWidgets::CollapsingHeader("map_authoring_workspace",
                                                          "Mesh sections"))
                for (std::size_t i = 0; i < entry.meshes.size(); ++i) {
                    const auto &mesh = entry.meshes[i];
                    ImGui::TextWrapped("%s / %s: %zu vertices", mesh.mesh.c_str(),
                                       mesh.material.c_str(), mesh.vertices);
                }
            if (studio::TutorialWidgets::CollapsingHeader("map_authoring_workspace", "Source")) {
                ImGui::TextWrapped("%s / member %zu",
                                   entry.source.location.archive.generic_string().c_str(),
                                   entry.source.location.member);
                ImGui::TextWrapped("SHA-256: %s", entry.source.member_hash.c_str());
                ImGui::Text("%zu linked dependencies", entry.dependencies.size());
            }
        }
        ImGui::BeginDisabled(busy() || preview_ || ground_handle_ || resource_ < 0 ||
                             !resources_.contains(std::size_t(resource_)));
        const bool extract = studio::TutorialWidgets::Button(
            "map_authoring_workspace",
            resource_ >= 0 && document_->project_asset(std::size_t(resource_))
                ? "Edit project asset"
                : "Extract asset",
            {-1, 0});
        const bool import_new = studio::TutorialWidgets::Button(
            "map_authoring_workspace", "Import new Blender model...", {-1, 0});
        ImGui::EndDisabled();
        ImGui::TextDisabled("New models use this resource’s game materials.");
        if (resource_ >= 0 && document_->project_asset(std::size_t(resource_))) {
            std::vector<std::string> models;
            const auto &asset = *document_->project_asset(std::size_t(resource_));
            if (!asset.native_resource.empty()) {
                const auto pack =
                    ModelPack::parse(Container::parse(asset.native_resource, "SM").files.at(1));
                for (const auto &r : pack.resources)
                    if (r.category == 0)
                        models.push_back(r.name);
            } else {
                const auto &scene = resources_.at(std::size_t(resource_));
                std::set<std::tuple<std::filesystem::path, std::size_t, std::vector<std::size_t>>>
                    used;
                for (const auto &draw : scene.draws)
                    if (draw.source &&
                        used.emplace(draw.source->archive, draw.source->member, draw.source->path)
                            .second)
                        models.push_back(draw.name);
            }
            studio_model_ = std::clamp(studio_model_, 0, std::max(0, int(models.size()) - 1));
            ImGui::BeginDisabled(busy() || preview_ || ground_handle_ || !project_store());
            if (models.size() > 1 &&
                ImGui::BeginCombo("Studio model", models[studio_model_].c_str())) {
                for (int i = 0; i < int(models.size()); ++i)
                    if (ImGui::Selectable((models[i] + "##" + std::to_string(i)).c_str(),
                                          studio_model_ == i))
                        studio_model_ = i;
                ImGui::EndCombo();
            }
            if (studio::TutorialWidgets::Button("map_authoring_workspace", "Edit in Studio",
                                                {-1, 0}))
                studio_request_ = std::size_t(resource_);
            ImGui::EndDisabled();
            ImGui::BeginDisabled(busy() || preview_ || ground_handle_);
            ImGui::BeginDisabled(!project_store());
            if (ImGui::Button("Publish to project library", {-1, 0})) {
                try {
                    publish_library_asset(false);
                } catch (const std::exception &error) {
                    error_ = error.what();
                }
            }
            auto *store = project_store();
            if (store && store->edits.contains(library_asset_key_) &&
                store->edits.at(library_asset_key_).kind == "asset-library") {
                ImGui::TextWrapped("Library destination: %s",
                                   store->edits.at(library_asset_key_).label.c_str());
                if (ImGui::Button("Replace selected library entry", {-1, 0})) {
                    try {
                        publish_library_asset(true);
                    } catch (const std::exception &error) {
                        error_ = error.what();
                    }
                }
            }
            ImGui::EndDisabled();
            if (studio::TutorialWidgets::Button("map_authoring_workspace", "Export asset...",
                                                {-1, 0})) {
                capture_asset_studio();
                package_export_ = *document_->project_asset(std::size_t(resource_));
                object_exchange_dialog(true, false, true);
            }
            ImGui::EndDisabled();
            if (!project_store())
                ImGui::TextWrapped("Open a project to save Studio edits with this asset.");
        }
        if (extract || import_new) {
            begin_asset_edit();
            if (import_new) {
                asset_draft_.id = 0;
                asset_draft_.name = "New Blender model";
                refresh_asset_editor();
                asset_history_ = {asset_draft_};
                asset_history_cursor_ = 0;
                object_exchange_dialog(false, true);
            }
            ImGui::End();
            return;
        }
        ImGui::SeparatorText(existing_mode_ ? "Surface placement" : "Tile placement");
        if (existing_mode_) {
            if(surface_position_) ImGui::Text("Position: %.1f, %.1f, %.1f", (*surface_position_)[0], (*surface_position_)[1], (*surface_position_)[2]);
            else ImGui::TextWrapped("Click a surface in Map view to choose the placement point.");
        }
        else
        if (tile_)
            ImGui::Text("Selected tile: %d, %d", tile_->x, tile_->z);
        else
            ImGui::TextWrapped("Return to Map, then click the ground to select a tile.");
        ImGui::BeginDisabled(busy() || ground_handle_);
        ImGui::TextUnformatted("Ground search ceiling");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputFloat("##ground-ceiling", &ground_ceiling_, 100, 1000);
        ImGui::TextUnformatted("Height offset");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputFloat("##height-offset", &height_offset_, 1, 10);
        ImGui::TextWrapped("Ground is chosen below the search ceiling. Height offset lifts or "
                           "sinks the model's base.");
        ImGui::BeginDisabled(preview_ || (existing_mode_ ? !surface_position_ : !tile_) || resource_ < 0 ||
                             !resources_.contains(std::size_t(resource_)) ||
                             !document_->can_place(std::size_t(resource_)));
        if (studio::TutorialWidgets::Button("map_authoring_workspace",
                                            existing_mode_ ? "Preview at selected point" : "Preview on selected tile")) {
            try {
                place_preview();
            } catch (const std::exception &error) {
                error_ = error.what();
            }
        }
        ImGui::EndDisabled();
        if (preview_ || selected_) {
            ImGui::SeparatorText(preview_ ? "Unapplied preview" : "Selected placement");
            auto proposed = draft_;
            bool changed = ImGui::DragFloat3("Position", proposed.position.data(), 1);
            changed |= ImGui::DragFloat("Heading", &proposed.turn, 1);
            if (changed) {
                try {
                    composition_transform(proposed);
                    draft_ = proposed;
                    preview_transform();
                } catch (const std::exception &error) {
                    error_ = error.what();
                }
            }
            if (preview_) {
                if (studio::TutorialWidgets::Button("map_authoring_workspace",
                                                    preview_id_ ? "Apply transform"
                                                                : "Place asset")) {
                    try {
                        if (preview_id_)
                            document_->transform(preview_id_, draft_);
                        else
                            selected_ = document_->add(std::size_t(resource_), draft_);
                        preview_ = false;
                        preview_id_ = 0;
                        refresh_scene();
                    } catch (const std::exception &error) {
                        error_ = error.what();
                    }
                }
                ImGui::SameLine();
                if (studio::TutorialWidgets::Button("map_authoring_workspace", "Cancel preview")) {
                    cancel_preview();
                    if (selected_)
                        select_instance(selected_);
                }
            } else {
                if (studio::TutorialWidgets::Button("map_authoring_workspace",
                                                    "Duplicate one tile over")) {
                    try {
                        auto next = draft_;
                        next.position[0] += document_->grid().tile_size;
                        selected_ = document_->duplicate(selected_, next);
                        draft_ = next;
                        refresh_scene();
                    } catch (const std::exception &error) {
                        error_ = error.what();
                    }
                }
                if (studio::TutorialWidgets::Button("map_authoring_workspace",
                                                    "Delete placement")) {
                    document_->erase(selected_);
                    selected_ = 0;
                    refresh_scene();
                }
            }
        }
        if (selected_ && !preview_) {
            auto it = std::find_if(document_->instances().begin(), document_->instances().end(),
                                   [&](const auto &i) {
                                       return i.id == selected_;
                                   });
            if (it != document_->instances().end()) {
                ImGui::SeparatorText("Object collision");
                if (studio::TutorialWidgets::Button(
                        "map_authoring_workspace",
                        it->collision ? "Refit box to model" : "Approximate collision", {-1, 0})) {
                    try {
                        document_->object_collision(
                            selected_, approximate_object_collision(resources_.at(it->resource)));
                        refresh_scene();
                    } catch (const std::exception &e) {
                        error_ = e.what();
                    }
                }
                it = std::find_if(document_->instances().begin(), document_->instances().end(),
                                  [&](const auto &i) {
                                      return i.id == selected_;
                                  });
                if (it->collision) {
                    auto box = *it->collision;
                    ImGui::TextUnformatted("Box size (X / Y / Z)");
                    ImGui::SetNextItemWidth(-1);
                    bool changed = ImGui::InputFloat3("##object-box-size", box.size.data(), "%.2f",
                                                      ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::TextUnformatted("Local offset (X / bottom Y / Z)");
                    ImGui::SetNextItemWidth(-1);
                    changed |= ImGui::InputFloat3("##object-box-offset", box.offset.data(), "%.2f",
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
                    if (changed)
                        try {
                            document_->object_collision(selected_, box);
                            refresh_scene();
                        } catch (const std::exception &e) {
                            error_ = e.what();
                        }
                    if (studio::TutorialWidgets::Button("map_authoring_workspace",
                                                        "Remove collision")) {
                        document_->object_collision(selected_, {});
                        refresh_scene();
                    }
                    ImGui::TextWrapped("Press Enter to apply dimensions. The box follows this "
                                       "object's position and heading and stages with it.");
                }
                ImGui::TextWrapped(
                    "One box approximates the visible mesh at rest. Shrink it for foliage or gaps; "
                    "it does not fit animation or make a walkable floor.");
            }
        }
        ImGui::EndDisabled();
        ImGui::SeparatorText("Added placements");
        ImGui::BeginDisabled(busy() || preview_ || ground_handle_);
        ImGui::BeginChild("Composition placements", {0, 130}, ImGuiChildFlags_Borders);
        for (const auto &instance : document_->instances()) {
            const auto identity = std::to_string(instance.id);
            ImGui::PushID(identity.c_str());
            const auto label =
                std::to_string(instance.id) + " - " + document_->resource_name(instance.resource);
            if (ImGui::Selectable(label.c_str(), selected_ == instance.id))
                select_instance(instance.id);
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndDisabled();
    } catch (const std::exception &error) {
        error_ = error.what();
    }
    ImGui::End();
}
std::string MapAuthoringWorkspace::report() const {
    if (!document_)
        return error_;
    std::ostringstream out;
    out << "Authoring stage: " << int(stage_) << "\nActive ground tool: " << ground_transform_tool_
        << "\n";
    out << "Map authoring\nAuthored ground: " << bool(document_->ground())
        << "\nArea: " << document_->catalog().area
        << "\nProject assets: " << document_->project_assets().size()
        << "\nAdded placements: " << document_->instances().size()
        << "\nDirty: " << document_->dirty() << "\n"
        << document_->catalog().report();
    if (composed_)
        out << composed_->report();
    return out.str();
}
ModelDocument MapAuthoringWorkspace::open_asset_studio(std::size_t resource) {
    require(document_ && !busy() && !asset_edit_ && !preview_,
            "Finish the Authoring operation first");
    const auto *asset = document_->project_asset(resource);
    require(asset != nullptr, "Select a saved project asset");
    auto model =
        project_asset_studio(dump_, document_->catalog(), *asset, std::size_t(studio_model_));
    studio_resource_ = resource;
    studio_baseline_ = *asset;
    studio_document_.reset();
    return model;
}
void MapAuthoringWorkspace::attach_asset_studio(std::shared_ptr<MaterialDocument> document,
                                                std::function<bool()> ready) {
    require(document && document->model.project_asset && studio_baseline_,
            "No project asset Studio session");
    studio_document_ = std::move(document);
    studio_ready_ = std::move(ready);
}
void MapAuthoringWorkspace::capture_asset_studio() {
    if (!studio_document_ || !studio_document_->dirty())
        return;
    require(!studio_ready_ || studio_ready_(),
            "Finish the Studio operation before saving the project asset");
    require(document_ && studio_baseline_, "Reopen the project asset before saving Studio edits");
    const auto *current = document_->project_asset(studio_resource_);
    require(
        current && *current == *studio_baseline_,
        "This project asset changed in Authoring. Reopen it in Studio before saving more edits");
    studio_document_->commit();
    auto candidate = save_project_asset_studio(*current, *studio_document_);
    document_->save_project_asset(candidate);
    studio_baseline_ = std::move(candidate);
    sync_project_assets();
    refresh_scene();
}
void MapAuthoringWorkspace::save_asset_studio() {
    require(project_store() && studio_document_, "Open a project asset in Studio first");
    save_editor_project();
    status_ = "Studio edits saved to the project asset. All placed copies are updated.";
}
void MapAuthoringWorkspace::bind_project() {
    if (!document_)
        return;
    project_.bind(
        "composition", "composition/" + std::to_string(document_->catalog().area),
        "Composition " + std::to_string(document_->catalog().area),
        (std::to_string(document_->catalog().area) + " \"" + project_baseline_ + "\" \"" +
         project_ground_base_ + "\""),
        [this] {
            return document_ &&
                   (document_->dirty() || (studio_document_ && studio_document_->dirty()));
        },
        [this] {
            require(!busy() && !preview_ && !ground_handle_ && !asset_edit_,
                    "Finish the authoring gesture or asset edit before saving");
            capture_asset_studio();
            return project_text(document_->serialize());
        },
        [this] {
            document_->mark_saved();
            if (studio_document_)
                studio_document_->mark_saved();
        });
    project_.autosave_when([this] {
        return !busy() && !preview_ && !ground_handle_ && !asset_edit_ &&
               (!studio_document_ || !studio_document_->dirty() || !studio_ready_ ||
                studio_ready_());
    });
    project_.ready([this] {
        if (collision_editor_)
            collision_editor_->prepare_save();
        if (document_) {
            require(!busy() && !preview_ && !ground_handle_ && !asset_edit_,
                    "Finish the authoring gesture or asset edit before saving");
        }
    });
    if (!path_.empty() && path_ != project_.document())
        project_.imported();
}
}
