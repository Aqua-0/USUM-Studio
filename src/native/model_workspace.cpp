#include "native/undo_shortcuts.h"
#include "native/inspector_selector.h"
#include "assets/overworld_character_asset.h"
#include "core/digest.h"
#include "authoring/object_export.h"
#include "native/viewport_navigation.h"
#include "native/tutorial_widgets.h"
#include "native/cry_editor.h"
#include "native/theme.h"
#include "native/model_workspace.h"
#include "native/imgui_renderer.h"
#include <SDL3/SDL.h>
#include <bx/math.h>
#include <algorithm>
#include <cctype>
namespace studio {
namespace {
std::string lower(std::string s) {
    for (auto &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}
ModelWorkspace::ModelWorkspace(const std::filesystem::path &shaders, SDL_Window *window,
                               bool studio)
    : settings_editor_(window, shaders), refresh_inspector_(shaders), studio_(studio),
      editor_(studio ? std::make_unique<MaterialEditor>(window) : nullptr), window_(window),
      renderer_(shaders) {
    if (editor_)
        editor_->external_operation = [this] {
            return model_exchange_editor_.pending();
        };
    renderer_.highlight_object = false;
    renderer_.lighting.enabled = true;
    renderer_.lighting.game = false;
    renderer_.lighting.soft = true;
    renderer_.lighting.camera_relative = true;
    renderer_.lighting.ambient = .75f;
    renderer_.lighting.strength = .25f;
    renderer_.lighting.highlights = .06f;
    renderer_.lighting.direction = {-.35f, .5f, 1.f};
    renderer_.fog_enabled = false;
    renderer_.sky_enabled = false;
    camera_.pitch = .2f;
}
ModelWorkspace::~ModelWorkspace() {
    cancel_ = true;
    if (job_.valid())
        job_.wait();
    if (catalog_job_.valid())
        catalog_job_.wait();
    if (library_job_.valid())
        library_job_.wait();
    if (clothing_job_.valid())
        clothing_job_.wait();
    if (effects_job_.valid())
        effects_job_.wait();
    if (bgfx::isValid(effect_texture_))
        bgfx::destroy(effect_texture_);
}
void ModelWorkspace::refresh(const std::filesystem::path &dump, const ArchiveSources &archives) {
    if (job_.valid() || catalog_job_.valid())
        return;
    dump_ = dump;
    archive_sources_ = archives;
    catalog_.clear();
    chosen_ = -1;
    error_.clear();
    status_ = "Reading Pokemon catalog...";
    catalog_job_ = std::async(std::launch::async, [dump, archives] {
        return load_pokemon_catalog(dump, archives);
    });
}
void ModelWorkspace::open_species(const std::filesystem::path &dump, unsigned species,
                                  unsigned form, bool female, bool shiny,
                                  const ArchiveSources &archives) {
    category_ = 0;
    pending_species_ = species;
    pending_form_ = form;
    pending_female_ = female;
    pending_shiny_ = shiny;
    refresh(dump, archives);
}
void ModelWorkspace::open(const PokemonEntry &entry, bool shiny) {
    if (job_.valid())
        return;
    settings_editor_.request_leave([this, entry, shiny] {
        cancel_ = false;
        error_.clear();
        status_ = "Loading " + entry.label + "...";
        auto dump = dump_;
        auto archives = archive_sources_;
        job_ = std::async(std::launch::async, [this, dump, entry, shiny, archives] {
            return load_pokemon(dump, entry, shiny, &cancel_, archives);
        });
    });
}
void ModelWorkspace::request_leave(std::function<void()> action) {
    if (project_store()) {
        try {
            save_editor_project();
            action();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
        return;
    }
    if (std::any_of(clothing_palettes_.begin(), clothing_palettes_.end(), [](auto &p) {
            return p.dirty();
        })) {
        clothing_leave_action_ = std::move(action);
        clothing_leave_pending_ = true;
        return;
    }
    settings_editor_.request_leave([this, action = std::move(action)] {
        if (studio_)
            editor_->request_leave(action);
        else
            action();
    });
}
void ModelWorkspace::poll() {
    if (effects_job_.valid() &&
        effects_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            effects_ = effects_job_.get();
            status_ = std::to_string(effects_.size()) + " effect resources available";
        } catch (const std::exception &e) {
            error_ = e.what();
            status_ = "Effect catalog could not be loaded";
        }

    if (clothing_job_.valid() &&
        clothing_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            clothing_catalog_ = clothing_job_.get();
            status_ = "Clothing items ready";
            if (category_ == 7)
                open_clothing(clothing_profiles_[clothing_profile_],
                              clothing_assembled_ ? -1 : clothing_slot_);
        } catch (const std::exception &e) {
            error_ = e.what();
            status_ = "Clothing catalog could not be loaded";
        }

    if (library_job_.valid() &&
        library_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            library_ = library_job_.get();
            status_ = std::to_string(library_.size()) + " models available";
        } catch (const std::exception &e) {
            error_ = e.what();
            status_ = "Model catalog could not be loaded";
        }
    if (catalog_job_.valid() &&
        catalog_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            catalog_ = catalog_job_.get();
            status_ = std::to_string(catalog_.size()) + " model variants available";
            if (pending_species_) {
                auto it = std::find_if(catalog_.begin(), catalog_.end(), [&](auto &e) {
                    return e.species == pending_species_ && e.form == pending_form_ &&
                           e.female == pending_female_;
                });
                require(it != catalog_.end(), "Requested Pokemon variant is unavailable");
                chosen_ = int(it - catalog_.begin());
                shiny_ = pending_shiny_;
                pending_species_ = 0;
                open(*it, shiny_);
            }
        } catch (const std::exception &e) {
            error_ = e.what();
            status_ = "Pokemon catalog could not be loaded";
            pending_species_ = 0;
        }
    if (job_.valid() && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            auto next = std::make_unique<ModelDocument>(job_.get());
            document_ = std::move(next);
            settings_editor_.bind(*document_);
            settings_editor_.active = pending_battle_ && document_->is_pokemon();
            pending_battle_ = false;
            if (!pending_settings_.empty()) {
                settings_editor_.open(pending_settings_);
                pending_settings_.clear();
            }
            refresh_inspector_.reset();
            renderer_.set_scene(document_->scene);
            renderer_.weather_effect = document_->scene->weather_particles.empty() ? 0 : 1;
            renderer_.playback.seconds = 0;
            selection_ = {};
            bone_ = -1;
            fit();
            if (document_->scene->draws.empty())
                camera_.fit({-100, -100, -100}, {100, 100, 100});
            camera_.yaw = .4f;
            camera_.pitch = .2f;
            status_ = document_->name;
            if (document_->is_pokemon())
                status_ += document_->shiny ? " / shiny" : " / normal";
            motion_group_ = 0;
            playing_ = true;
        } catch (const std::exception &e) {
            error_ = e.what();
            status_ = "Model could not be loaded";
        }
    renderer_.upload_step();
}
void ModelWorkspace::open_document(ModelDocument document) {
    refresh_inspector_.reset();
    editor_->open(std::move(document));
    document_ = std::make_unique<ModelDocument>(editor_->document()->model);
    settings_editor_.bind(*document_);
    settings_editor_.active = pending_battle_ && document_->is_pokemon();
    pending_battle_ = false;
    if (!pending_settings_.empty()) {
        settings_editor_.open(pending_settings_);
        pending_settings_.clear();
    }
    renderer_.set_scene(document_->scene);
    renderer_.playback.materials = false;
    renderer_.playback.seconds = 0;
    selection_ = {};
    bone_ = -1;
    error_.clear();
    fit();
    camera_.yaw = .4f;
    camera_.pitch = .2f;
}
bool ModelWorkspace::apply(const MaterialDocument &edit) {
    if (document_ && edit.model.kind == ModelAssetKind::ArchiveModel &&
        document_->dump == edit.model.dump && !edit.model.material_resources.empty()) {
        auto &link = edit.model.resources.at(edit.model.material_resources.front());
        auto &source = edit.model.sources.at(link.source);
        bool matches =
            std::any_of(document_->sources.begin(), document_->sources.end(), [&](auto &candidate) {
                return candidate.member == source.member && candidate.subfile == source.subfile &&
                       document_->archive_sources.resolve(document_->dump, candidate.archive) ==
                           edit.model.archive_sources.resolve(edit.model.dump, source.archive);
            });
        if (!matches)
            return false;
        document_ = std::make_unique<ModelDocument>(edit.preview_model());
        renderer_.set_scene(document_->scene);
        selection_ = {};
        bone_ = -1;
        return true;
    }

    if (!document_ || !document_->is_pokemon() || !edit.model.is_pokemon() ||
        document_->dump != edit.model.dump ||
        document_->archive_sources.resolve(document_->dump, TargetProfile::pokemon_archive) !=
            edit.model.archive_sources.resolve(edit.model.dump, TargetProfile::pokemon_archive) ||
        document_->pokemon.model_member != edit.model.pokemon.model_member ||
        document_->shadow_model != edit.model.shadow_model)
        return false;
    if (edit.has_structural_edits()) {
        document_ = std::make_unique<ModelDocument>(edit.preview_model());
        renderer_.set_scene(document_->scene);
        selection_ = {};
        bone_ = -1;
        return true;
    }
    auto &target_link = document_->resources.at(document_->material_resources.at(0));
    if (!edit.compatible_model(
            asset_resource(document_->sources.at(target_link.source).original, target_link.path)))
        return false;
    refresh_inspector_.feeding.reset();
    if (document_->refresh_feeding && edit.model.refresh_feeding &&
        document_->refresh_feeding->row == edit.model.refresh_feeding->row &&
        document_->refresh_feeding->original == edit.model.refresh_feeding->original &&
        document_->refresh_feeding->camera_row == edit.model.refresh_feeding->camera_row &&
        document_->refresh_feeding->camera_original == edit.model.refresh_feeding->camera_original)
        document_->refresh_feeding = edit.model.refresh_feeding;
    edit.apply_to(*document_->scene);
    if (document_->pokemon.texture_member == edit.model.pokemon.texture_member)
        document_->refresh_regions = edit.model.refresh_regions;
    renderer_.refresh_materials();
    renderer_.refresh_textures();
    return true;
}
void ModelWorkspace::refresh_library() {
    if (category_ == 8) {
        refresh_effects();
        return;
    }
    if (category_ == 7) {
        refresh_clothing();
        return;
    }
    if (job_.valid() || catalog_job_.valid() || library_job_.valid() || category_ == 0)
        return;
    library_.clear();
    chosen_ = -1;
    cancel_ = false;
    error_.clear();
    status_ = "Reading model catalog...";
    auto category = ModelCategory(category_ - 1);
    auto path = library_archives_[category_ - 1];
    if (path.empty())
        path = dump_ / model_category_archive(category);
    library_job_ = std::async(std::launch::async, [this, path, category] {
        return load_model_library(path, category, &cancel_);
    });
}
void ModelWorkspace::open_library(const std::filesystem::path &path, ModelCategory category,
                                  const LibraryModel &entry) {
    if (job_.valid())
        return;
    settings_editor_.request_leave([this, path, category, entry] {
        cancel_ = false;
        error_.clear();
        status_ = "Loading " + entry.name + "...";
        auto dump = dump_;
        job_ = std::async(std::launch::async, [this, dump, path, category, entry] {
            return load_library_model(dump, path, category, entry, &cancel_);
        });
    });
}
void ModelWorkspace::browse(const char *dump, const ArchiveSources &archives) {
    bool busy = job_.valid() || catalog_job_.valid() || library_job_.valid() ||
                clothing_job_.valid() || effects_job_.valid();
    bool stale =
        dump_ != std::filesystem::u8path(dump) || archive_sources_.pokemon != archives.pokemon;
    if (!busy && *dump && (stale || refresh_requested_)) {
        refresh_requested_ = false;
        search_[0] = 0;
        dump_ = std::filesystem::u8path(dump);
        archive_sources_ = archives;
        if (category_)
            refresh_library();
        else
            refresh(dump_, archives);
        busy = true;
        stale = false;
    }
    {
        std::lock_guard lock(library_dialog_->mutex);
        if (library_dialog_->ready) {
            library_dialog_->ready = false;
            auto selected = library_dialog_category_;
            library_dialog_category_ = -1;
            if (!library_dialog_->error.empty())
                error_ = library_dialog_->error;
            else if (!library_dialog_->path.empty() && selected >= 0) {
                library_archives_[selected] = std::filesystem::u8path(library_dialog_->path);
                refresh_library();
                busy = true;
            }
        }
    }
    ImGui::Begin("Model browser");
    static const char *categories[] = {
        "Pokemon",       "Battle characters", "Field characters & props", "Poke Balls",
        "Battle props",  "Poke Beans",        "Battle arena parts",       "Clothing",
        "Battle effects"};
    ImGui::BeginDisabled(busy || library_dialog_category_ >= 0 || clothing_dialog_slot_ >= 0);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##model-category", &category_, categories, 9)) {
        chosen_ = -1;
        search_[0] = 0;
        error_.clear();
        if (category_)
            refresh_library();
        else
            refresh(dump_, archives);
        busy = true;
    }
    ImGui::EndDisabled();
    if (category_ == 8) {
        browse_effects(busy);
        ImGui::End();
        return;
    }
    if (category_ == 7) {
        browse_clothing(busy);
        ImGui::End();
        return;
    }
    std::filesystem::path source =
        category_ ? library_archives_[category_ - 1]
                  : archives.resolve(std::filesystem::u8path(dump), TargetProfile::pokemon_archive);
    if (category_ && source.empty())
        source = dump_ / model_category_archive(ModelCategory(category_ - 1));
    if (ImGui::SmallButton("Library options"))
        ImGui::OpenPopup("Model library options");
    if (ImGui::BeginPopup("Model library options")) {
        if (!project_store() && !category_ && ImGui::Button("Choose source archive")) {
            inspector_page_ = "Source";
            ImGui::CloseCurrentPopup();
        }
        ImGui::TextWrapped("%s", source.string().c_str());
        ImGui::BeginDisabled(busy || library_dialog_category_ >= 0 || (!*dump && !category_));
        if (studio::TutorialWidgets::Button("model_workspace", "Refresh catalog")) {
            if (category_)
                refresh_library();
            else
                refresh(std::filesystem::u8path(dump), archives);
            busy = true;
        }
        if (category_) {
            ImGui::SameLine();
            if (!project_store() &&
                studio::TutorialWidgets::Button("model_workspace", "External GARC...")) {
                library_dialog_category_ = category_ - 1;
                choose_archive(window_, library_dialog_, source.string().c_str());
            }
            if (!project_store() && !library_archives_[category_ - 1].empty() &&
                studio::TutorialWidgets::Button("model_workspace", "Use dump archive")) {
                library_archives_[category_ - 1].clear();
                refresh_library();
                busy = true;
            }
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (job_.valid() || library_job_.valid()) {
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("model_workspace", "Cancel"))
            cancel_ = true;
    }
    if (busy)
        ImGui::TextWrapped("%s", status_.c_str());
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##model-search",
                             category_ ? "Search resource name or member"
                                       : "Search name, species number or form",
                             search_, sizeof(search_));
    auto open_selected = [&] {
        if (category_)
            open_library(source, ModelCategory(category_ - 1), library_.at(chosen_));
        else
            open(catalog_.at(chosen_), shiny_);
    };
    auto count = category_ ? library_.size() : catalog_.size();
    ImGui::BeginDisabled(busy || stale || library_dialog_category_ >= 0);
    if (!category_ &&
        studio::TutorialWidgets::Checkbox("model_workspace", "Shiny textures", &shiny_) &&
        chosen_ >= 0 && std::size_t(chosen_) < count)
        open_selected();
    ImGui::EndDisabled();
    ImGui::BeginDisabled(busy || stale || library_dialog_category_ >= 0);
    ImGui::BeginChild("Model list", ImVec2(0, 0), ImGuiChildFlags_Borders);
    auto filter = lower(search_);
    std::vector<int> matches;
    auto label = [&](int i) {
        return category_ ? library_[i].name + " / " + std::to_string(library_[i].member)
                         : catalog_[i].label;
    };
    for (unsigned i = 0; i < count; ++i)
        if (filter.empty() || lower(label(i)).find(filter) != std::string::npos)
            matches.push_back(int(i));
    if (matches.empty() && !busy)
        ImGui::TextWrapped("%s",
                           count ? "No matching models."
                                 : "No models found for this category. Check the source archive.");
    ImGuiListClipper clipper;
    clipper.Begin(int(matches.size()));
    while (clipper.Step())
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            int i = matches[row];
            ImGui::PushID(i);
            if (ImGui::Selectable(label(i).c_str(), chosen_ == i,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                chosen_ = i;
                if (!busy && !stale && library_dialog_category_ < 0)
                    open_selected();
            }
            ImGui::PopID();
        }
    ImGui::EndChild();
    ImGui::EndDisabled();
    ImGui::End();
}
void ModelWorkspace::draw(std::uint32_t frame, const char *dump, const ArchiveSources &archives) {
    clothing_leave_dialog();
    settings_editor_.update();
    poll();
    if (!studio_)
        browse(dump, archives);
    if (studio_) {
        asset_actions(std::filesystem::u8path(dump));
        if (editor_->document() && document_)
            editor_->document()->model.looping_effects = document_->looping_effects;
        editor_->draw(renderer_, selection_, std::filesystem::u8path(dump));
        if (editor_->take_uv_request())
            inspector_page_ = "UVs";
        if (editor_->document() && document_ &&
            document_->scene != editor_->document()->model.scene) {
            std::string selected_bone;
            if (bone_ >= 0 && !document_->scene->skeletons.empty() &&
                std::size_t(bone_) < document_->scene->skeletons[0].joints.size())
                selected_bone = document_->scene->skeletons[0].joints[bone_].name;
            auto preview_motion = document_->motion;
            auto looping_effects = document_->looping_effects;
            document_ = std::make_unique<ModelDocument>(editor_->document()->model);
            if (document_->area < 0) {
                document_->looping_effects = looping_effects;
                document_->select_motion(preview_motion >= 0 && std::size_t(preview_motion) >=
                                                                    document_->motions.size()
                                             ? -1
                                             : preview_motion,
                                         repeat_);
                if (document_->motion >= 0)
                    motion_group_ = int(document_->motions[document_->motion].group);
            }
            bone_ = -1;
            if (!document_->scene->skeletons.empty())
                for (unsigned i = 0; i < document_->scene->skeletons[0].joints.size(); ++i)
                    if (document_->scene->skeletons[0].joints[i].name == selected_bone)
                        bone_ = int(i);
        }
        if (editor_->document() && document_) {
            document_->refresh_regions = editor_->document()->model.refresh_regions;
            document_->refresh_feeding = editor_->document()->model.refresh_feeding;
        }
    }
    details();
    if (!studio_)
        selection_.focus = false;
    if (studio_ && (!geometry_tab_ || !editor_->editing_available()) && document_)
        geometry_editor_.deactivate(*document_, renderer_);
    if (studio_ && editor_->document() && document_) {
        if (motion_editor_.take_mapping_request() || editor_->take_mapping_request())
            uv_motion_editor_.open(selection_, editor_->texture_unit());
        if (editor_->editing_available())
            uv_motion_editor_.draw(*editor_->document(), *document_, renderer_, playing_, repeat_);
        else
            uv_motion_editor_.close(renderer_);
        if (auto keyed = uv_motion_editor_.take_keyed())
            motion_editor_.focus_uv(keyed->first, keyed->second);
    }
    viewport(frame);
    if (studio_ && inspector_page_ == "UVs")
        editor_->uvs(renderer_, selection_);
    if (inspector_page_ == "Source") {
        ImGui::Begin(studio_ ? "Studio inspector" : "Model inspector");
        if (document_) {
            if (ImGui::BeginTabBar("Model information")) {
                if (studio::TutorialWidgets::BeginTabItem("model_workspace",
                                                          "Source & dependencies")) {
                    ImGui::TextWrapped("%s", document_->name.c_str());
                    ImGui::TextWrapped("Dump: %s", document_->dump.string().c_str());
                    ImGui::TextWrapped(
                        "%s",
                        document_->kind == ModelAssetKind::ArchiveModel
                            ? "Original member and resource locations retained for inspection."
                            : "Original members and nested resource locations are retained for "
                              "material editing and export.");
                    for (auto &s : document_->sources) {
                        ImGui::PushID(int(&s - document_->sources.data()));
                        if (ImGui::TreeNode(s.role.c_str())) {
                            ImGui::TextWrapped(
                                "%s / member %zu / subfile %u",
                                document_->archive_sources.resolve(document_->dump, s.archive)
                                    .string()
                                    .c_str(),
                                s.member, s.subfile);
                            ImGui::TextWrapped("SHA-256: %s", s.hash.c_str());
                            ImGui::Text("%zu decoded bytes", s.original.size());
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTabItem();
                }
                if (studio::TutorialWidgets::BeginTabItem("model_workspace", "Preview coverage")) {
                    for (auto &d : document_->scene->diagnostics) {
                        ImGui::Bullet();
                        ImGui::SameLine();
                        ImGui::TextWrapped("%s", d.c_str());
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        } else
            ImGui::TextWrapped("Load a model to inspect its source and preview coverage.");
        ImGui::End();
    }
}
void ModelWorkspace::fit(int selected) {
    auto &scene = *document_->scene;
    auto visible = evaluate_visibility(scene, renderer_.playback.seconds, 12, true);
    auto poses = evaluate_scene_poses(scene.skeletons, renderer_.player, renderer_.playback.seconds,
                                      12, true);
    std::array<float, 3> low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
    bool found = false;
    for (unsigned i = 0; i < scene.draws.size(); ++i) {
        if (!renderer_.draw_visible(i) ||
            (selected >= 0 ? (selection_.meshes.empty() ? i != unsigned(selected)
                                                        : !selection_.mesh_selected(int(i)))
                           : !visible[i]))
            continue;
        auto &d = scene.draws[i];
        for (auto &v : d.vertices) {
            std::array<float, 3> p{v.x, v.y, v.z};
            if (d.skeleton >= 0) {
                p = {};
                for (unsigned j = 0; j < 4; ++j)
                    if (v.weights[j] > 0) {
                        auto &m = poses.at(d.skeleton).at(d.palette.at(unsigned(v.joints[j])));
                        for (unsigned k = 0; k < 3; ++k)
                            p[k] += v.weights[j] * (m[k * 4] * v.x + m[k * 4 + 1] * v.y +
                                                    m[k * 4 + 2] * v.z + m[k * 4 + 3]);
                    }
            }
            found = true;
            for (unsigned k = 0; k < 3; ++k) {
                low[k] = std::min(low[k], p[k]);
                high[k] = std::max(high[k], p[k]);
            }
        }
    }
    if (found) {
        camera_.fit(low, high);
        camera_.distance *= 1.25f;
    }
}
ModelDocument ModelWorkspace::studio_asset() const {
    require(bool(document_), "Open a model first");
    if (document_->kind != ModelAssetKind::ArchiveModel)
        return *document_;
    require(!document_->draw_resources.empty(), "This preview has no editable source model");
    auto draw = selection_.draw >= 0 ? std::size_t(selection_.draw) : 0;
    return studio_library_model(*document_, document_->draw_resources.at(draw));
}
void ModelWorkspace::send_to_studio() {
    if (document_)
        try {
            studio_request_ = std::make_unique<ModelDocument>(studio_asset());
        } catch (const std::exception &e) {
            error_ = e.what();
        }
}
void ModelWorkspace::details() {
    bool was_geometry = geometry_tab_;
    geometry_tab_ = false;
    ImGui::Begin(studio_ ? "Studio inspector" : "Model inspector");
    if (!document_) {
        if (!studio_)
            ImGui::TextWrapped("Choose a model in the browser.");
        ImGui::End();
        return;
    }
    auto &scene = *document_->scene;
    ImGui::TextWrapped("%s", document_->name.c_str());
    ImGui::Text("%zu mesh parts | %zu materials", scene.draws.size(), scene.materials.size());
    if (document_->is_pokemon() && document_->independent_asset.empty()) {
        int part = document_->shadow_model ? 1 : 0;
        ImGui::BeginDisabled(job_.valid() || (studio_ && !editor_->editing_available()));
        bool changed =
            studio::TutorialWidgets::RadioButton("model_workspace", "Main model", &part, 0);
        ImGui::SameLine();
        ImGui::BeginDisabled(!document_->has_shadow_model);
        changed |=
            studio::TutorialWidgets::RadioButton("model_workspace", "Shadow model", &part, 1);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (document_->shadow_model)
            ImGui::TextWrapped(
                "Shadow geometry uses the main model's bones and textures. Edit shared bones and "
                "motions on Main model; stage and reload after changing bones. This shows the "
                "shadow mesh, not its projected game appearance.");
        if (changed) {
            auto dump = document_->dump;
            auto entry = document_->pokemon;
            auto shiny = document_->shiny;
            auto archives = document_->archive_sources;
            request_leave([this, dump, entry, shiny, archives, part] {
                try {
                    auto next = load_pokemon(dump, entry, shiny, nullptr, archives, part == 1);
                    if (studio_)
                        open_document(std::move(next));
                    else {
                        document_ = std::make_unique<ModelDocument>(std::move(next));
                        settings_editor_.bind(*document_);
                        refresh_inspector_.reset();
                        renderer_.set_scene(document_->scene);
                        selection_ = {};
                        bone_ = -1;
                        fit();
                    }
                } catch (const std::exception &e) {
                    error_ = e.what();
                }
            });
            ImGui::End();
            return;
        }
    }
    if (!studio_) {
        ImGui::BeginDisabled(job_.valid() || scene.draws.empty());
        if (primary_button("Send to Studio", {-1, 0}))
            send_to_studio();
        if (CharacterRegistrationEditor::accepts(*document_) &&
            ImGui::Button("Register new character...", {-1, 0}))
            inspector_page_ = "Register character";
        if (ImGui::Button("More actions", {-1, 0}))
            ImGui::OpenPopup("Model actions");
        ImGui::SetNextWindowSize({320, 0});
        if (ImGui::BeginPopup("Model actions")) {
            if (studio::TutorialWidgets::Button("model_workspace", "Use as material donor",
                                                {-1, 0}))
                try {
                    shader_donor_ = std::make_unique<ModelDocument>(studio_asset());
                } catch (const std::exception &e) {
                    error_ = e.what();
                }
            if (document_->kind == ModelAssetKind::ArchiveModel)
                ImGui::TextWrapped(
                    "Select a mesh to send its source model to Studio. Outfits open one "
                    "part at a time.");
            if (studio::TutorialWidgets::Button("model_workspace", "Reload from source", {-1, 0})) {
                if (document_->clothing)
                    open_clothing(*document_->clothing, document_->clothing_part, document_->dump);
                else if (document_->battle_effect) {
                    auto model = *document_;
                    job_ = std::async(std::launch::async, [model] {
                        const auto &source = model.sources.front();
                        return load_effect_model(model.dump, source.member, source.subfile, {},
                                                 model.effect_motions);
                    });
                } else if (document_->kind == ModelAssetKind::ArchiveModel) {
                    auto &source = document_->sources.at(0);
                    open_library(source.archive,
                                 source.original[0] == 'B' ? ModelCategory::BattleArenas
                                                           : ModelCategory::FieldCharacters,
                                 {source.member, document_->name});
                } else
                    open(document_->pokemon, document_->shiny);
            }
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
    }
    {
        ImGui::Separator();
        auto available_page = [&](const std::string &page) {
            if (page == "Materials")
                return !studio_;
            if (page == "UVs")
                return studio_;
            if (page == "Overworld character")
                return studio_ && can_convert_overworld_character(*document_);
            if (page == "Register character")
                return !studio_ && CharacterRegistrationEditor::accepts(*document_);
            if (page == "Settings")
                return document_->is_pokemon() && document_->independent_asset.empty();
            if (page == "Cries")
                return document_->independent_asset.empty() && cry_editor_ && !document_->shadow_model && document_->area < 0 &&
                       document_->pokemon.species;
            if (page == "Refresh")
                return document_->independent_asset.empty() && document_->is_pokemon() && !document_->shadow_model;
            if (page == "Geometry")
                return studio_ && editor_->document() &&
                       !document_->clothing;
            if (page == "Blender")
                return studio_ && editor_->document() && !document_->clothing;
            if (page == "Motions")
                return studio_ && editor_->document() && !document_->shadow_model;
            return true;
        };
        if (!available_page(inspector_page_))
            inspector_page_ = "Meshes";
        InspectorSelectorStyle selector_style("Choose editing tool");
        if (ImGui::BeginCombo("##inspector-page", inspector_page_.c_str())) {
            for (const char *page :
                 {"Meshes", "Materials", "Geometry", "UVs", "Skeleton", "Motions", "Blender",
                  "Lighting", "Settings", "Overworld character", "Register character", "Cries", "Refresh", "Source"}) {
                if (!available_page(page))
                    continue;
                if (ImGui::Selectable(page, inspector_page_ == page))
                    inspector_page_ = page;
            }
            ImGui::EndCombo();
        }
        selector_style.end();
        if (!studio_ && inspector_page_ == "Materials")
            material_inspector(document_->scene.get(), renderer_, selection_, "Model inspector",
                               true);
        if (studio_ && inspector_page_ == "Overworld character" && editor_->document()) {
            character_stage_requested_ |= character_registration_.draw_conversion(*editor_->document(), *document_);
            if (character_registration_.take_motion_preview()) {
                playing_ = true;
                renderer_.playback.seconds = 0;
                motion_group_ = int(document_->motions.at(document_->motion).group);
            }
        }
        if (!studio_ && inspector_page_ == "Register character") {
            character_stage_requested_ |= character_registration_.draw(*document_);
            if (auto created = character_registration_.take_created())
                studio_request_ = std::move(created);
        }
        if (!studio_ && inspector_page_ == "Settings") {
            bundle_editor_.draw(*document_, window_);
            if (auto created = bundle_editor_.take_created())
                studio_request_ = std::move(created);
        }
    }
    auto begin_page = [&](const char *name) {
        return inspector_page_ == name;
    };
    {
        if (document_->is_pokemon() && begin_page("Settings")) {
            settings_editor_.draw();
        }
        if (begin_page("Meshes")) {
            if (studio::TutorialWidgets::Button("model_workspace", "Frame model"))
                fit();
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("model_workspace", "Show all"))
                renderer_.show_all_draws();

            if (ImGui::Button("Select all meshes")) {
                selection_.meshes.clear();
                for (unsigned i = 0; i < scene.draws.size(); ++i)
                    selection_.meshes.insert(int(i));
                if (!scene.draws.empty()) {
                    selection_.draw = 0;
                    selection_.material = int(scene.draws[0].material);
                }
            }
            if (ImGui::GetWindowWidth() >= 330.f)
                ImGui::SameLine();
            if (ImGui::Button("Clear mesh selection")) {
                selection_.meshes.clear();
                selection_.draw = -1;
            }
            ImGui::TextDisabled("Visibility     Mesh selection");
            float list_height = std::max(120.f, ImGui::GetContentRegionAvail().y - 125.f);
            ImGui::BeginChild("##mesh-list", {0, list_height}, ImGuiChildFlags_Borders);
            for (unsigned i = 0; i < scene.draws.size(); ++i) {
                auto &d = scene.draws[i];
                ImGui::PushID(int(i));
                bool visible = renderer_.draw_visible(i);
                if (studio::TutorialWidgets::Checkbox("model_workspace", "##visible", &visible)) {
                    renderer_.set_draw_visible(i, visible);
                    if (!visible && selection_.draw == int(i))
                        selection_ = {};
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Show or hide this mesh part");
                ImGui::SameLine();
                if (ImGui::Selectable((d.mesh + " / " + std::to_string(i)).c_str(),
                                      selection_.mesh_selected(int(i)))) {
                    selection_.select_mesh(int(i), int(d.material), ImGui::GetIO().KeyCtrl);
                    if (selection_.draw >= 0)
                        selection_.material = int(scene.draws[selection_.draw].material);
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            if (selection_.draw >= 0 && std::size_t(selection_.draw) < scene.draws.size()) {
                auto &d = scene.draws[selection_.draw];
                ImGui::Text("Mesh part %d | %zu selected", selection_.draw,
                            selection_.meshes.empty() ? std::size_t(1) : selection_.meshes.size());
                ImGui::TextDisabled("%zu vertices | %zu triangles", d.vertices.size(),
                                    d.indices.size() / 3);
                ImGui::Text("Material: %s", scene.materials[d.material].name.c_str());
                ImGui::Text("Bone palette: %zu", d.palette.size());
                if (studio::TutorialWidgets::Button("model_workspace", "Frame mesh"))
                    fit(selection_.draw);
                ImGui::SameLine();
                if (studio::TutorialWidgets::Button("model_workspace", "Isolate")) {
                    for (unsigned j = 0; j < scene.draws.size(); ++j)
                        renderer_.set_draw_visible(j, selection_.mesh_selected(int(j)));
                }
            } else {
                ImGui::TextDisabled("Select a mesh part to inspect it.");
            }
        }
        if (studio_ && editor_->document() && !document_->clothing && begin_page("Geometry")) {
            geometry_tab_ = true;
            if (!was_geometry)
                refresh_inspector_.reset();
            ImGui::BeginDisabled(!editor_->editing_available());
            geometry_editor_.panel(*editor_->document(), *document_, renderer_, playing_,
                                   selection_.material);
            ImGui::EndDisabled();
        }
        if (begin_page("Skeleton")) {
            studio::TutorialWidgets::Checkbox("model_workspace", "Show bones through model",
                                              &show_bones_);
            studio::TutorialWidgets::Checkbox("model_workspace", "Show influenced vertices",
                                              &show_weights_);
            if (show_weights_)
                ImGui::TextWrapped("Selected bone: blue = low weight, red = full weight. Dots show "
                                   "through the surface.");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##bone-search", "Filter bones", bone_search_,
                                     sizeof(bone_search_));
            if (scene.skeletons.empty())
                ImGui::TextWrapped("No skeleton was decoded; see Preview coverage.");
            else {
                auto &rig = scene.skeletons[0];
                ImGui::Text("%zu bones", rig.joints.size());
                auto filter = lower(bone_search_);
                ImGui::BeginChild("Bone list", ImVec2(0, 200), ImGuiChildFlags_Borders);
                for (unsigned i = 0; i < rig.joints.size(); ++i) {
                    auto &j = rig.joints[i];
                    if (!filter.empty() && lower(j.name).find(filter) == std::string::npos)
                        continue;
                    unsigned depth = 0;
                    for (int p = j.parent; p >= 0 && depth < 16; p = rig.joints[p].parent)
                        ++depth;
                    ImGui::Indent(float(depth) * 8);
                    ImGui::PushID(int(i));
                    if (ImGui::Selectable(j.name.c_str(), bone_ == int(i)))
                        bone_ = int(i);
                    ImGui::PopID();
                    ImGui::Unindent(float(depth) * 8);
                }
                ImGui::EndChild();
                if (bone_ >= 0) {
                    auto &j = rig.joints[bone_];
                    ImGui::TextWrapped("%s", j.name.c_str());
                    ImGui::TextWrapped("Parent: %s",
                                       j.parent < 0 ? "None" : rig.joints[j.parent].name.c_str());
                    ImGui::Text("Bind position %.2f, %.2f, %.2f", j.bind[3], j.bind[7], j.bind[11]);
                    unsigned vertices = 0;
                    for (auto &d : scene.draws)
                        if (d.skeleton == 0)
                            for (auto &v : d.vertices)
                                for (unsigned k = 0; k < 4; ++k)
                                    if (v.weights[k] > 0 &&
                                        unsigned(v.joints[k]) < d.palette.size() &&
                                        d.palette[unsigned(v.joints[k])] == bone_) {
                                        ++vertices;
                                        break;
                                    }
                    ImGui::Text("Influences %u vertices", vertices);
                }
            }
            if (studio_ && document_->area < 0 && !document_->clothing && editor_->document()) {
                ImGui::BeginDisabled(!editor_->editing_available());
                if (!document_->shadow_model)
                    skeleton_editor_.draw(*editor_->document(), *document_, renderer_, playing_,
                                          bone_);
                else
                    ImGui::TextWrapped("Edit shared bones on Main model.");
                ImGui::EndDisabled();
            }
        }
        if (studio_ && editor_->document() && !document_->clothing && begin_page("Blender")) {
            ImGui::BeginDisabled(!editor_->editing_available());
            model_exchange_editor_.draw(*editor_->document(), *document_, renderer_, playing_,
                                        window_);
            ImGui::EndDisabled();
        }
        if (document_->is_pokemon() && !document_->shadow_model && begin_page("Refresh")) {
            refresh_inspector_.draw(*document_, renderer_, selection_,
                                    studio_ && editor_->editing_available() ? editor_->document()
                                                                            : nullptr);
        }
        if (studio_ && editor_->document() && !document_->shadow_model && begin_page("Motions")) {
            ImGui::BeginDisabled(!editor_->editing_available());
            if (document_->area < 0 && !document_->clothing) {
                studio::TutorialWidgets::RadioButton("model_workspace", "Materials", &motion_kind_,
                                                     0);
                ImGui::SameLine();
                if (studio::TutorialWidgets::RadioButton("model_workspace", "Pose", &motion_kind_,
                                                         1)) {
                    playing_ = false;
                    show_bones_ = true;
                    document_->looping_effects = false;
                    document_->select_motion(document_->motion, repeat_);
                }
                ImGui::SameLine();
                if (studio::TutorialWidgets::RadioButton("model_workspace", "Visibility",
                                                         &motion_kind_, 2))
                    playing_ = false;
            }
            if (document_->area < 0 && !document_->clothing && motion_kind_ == 1)
                skeletal_motion_editor_.draw(*editor_->document(), *document_, renderer_, playing_,
                                             repeat_, bone_, show_bones_);
            else if (document_->area < 0 && !document_->clothing && motion_kind_ == 2)
                visibility_motion_editor_.draw(*editor_->document(), *document_, renderer_,
                                               playing_, repeat_);
            else
                motion_editor_.draw(*editor_->document(), *document_, renderer_, playing_, repeat_);
            ImGui::EndDisabled();
        }
        if (cry_editor_ && !document_->shadow_model && document_->area < 0 &&
            document_->pokemon.species && begin_page("Cries")) {
            cry_editor_->draw(*document_);
        }
        if (begin_page("Lighting")) {
            ImGui::TextWrapped("Preview controls; these do not change game materials.");
            studio::TutorialWidgets::Checkbox("model_workspace", "Lighting",
                                              &renderer_.lighting.enabled);
            if (studio::TutorialWidgets::Button("model_workspace", "Soft studio preset")) {
                renderer_.lighting.soft = true;
                renderer_.lighting.camera_relative = true;
                renderer_.lighting.ambient = .75f;
                renderer_.lighting.strength = .25f;
                renderer_.lighting.highlights = .06f;
                renderer_.lighting.direction = {-.35f, .5f, 1.f};
            }
            studio::TutorialWidgets::Checkbox("model_workspace", "Soft diffuse",
                                              &renderer_.lighting.soft);
            studio::TutorialWidgets::Checkbox("model_workspace", "Light follows camera",
                                              &renderer_.lighting.camera_relative);
            ImGui::SliderFloat("Ambient", &renderer_.lighting.ambient, 0, 1);
            ImGui::SliderFloat("Strength", &renderer_.lighting.strength, 0, 2);
            ImGui::SliderFloat("Fallback highlights", &renderer_.lighting.highlights, 0, 1);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Only affects materials without lighting tables. Authored tables retain their "
                    "full values because materials can use them for color and opacity.");
            ImGui::SliderFloat3("Direction", renderer_.lighting.direction.data(), -1, 1);
        }
    }
    ImGui::End();
}
void ModelWorkspace::open_independent(AssetPackage package, const std::filesystem::path &dump, const std::string &key) {
    auto *store = project_store();
    require(store != nullptr, "Open a project to edit independent assets");
    auto load = [&](const AssetPackage &value) {
        return text(value.at("type")) == "static" ? open_studio_object(value, dump)
                                                  : open_independent_asset(value, dump);
    };
    auto model = load(package);
    if (!key.empty()) model.independent_asset = key;
    auto id = "studio-asset/" + model.independent_asset;
    if (key.empty() && store->edits.contains(id)) {
        model = load(decode_asset_package(read_file(store->document(id))));
        model.independent_asset = id.substr(13);
    } else if (key.empty()) {
        store->capture({id, "studio-asset", model.name, "", {}}, encode_asset_package(package));
        store->save();
    }
    open_document(std::move(model));
}
void ModelWorkspace::asset_actions(const std::filesystem::path &dump) {
    ImGui::Begin("Studio viewport");
    auto *store = project_store();
    bool publish_object = false;
    bool picking;
    { std::lock_guard lock(asset_dialog_->mutex); picking = asset_dialog_->pending; }
    ImGui::BeginDisabled(!store || !editor_->editing_available() || picking);
    if (ImGui::Button("Asset...")) ImGui::OpenPopup("Studio asset actions");
    if (ImGui::BeginPopup("Studio asset actions")) {
        if (ImGui::MenuItem("Open asset file...")) editor_->request_leave([this] {
            auto *owner = new std::shared_ptr<FolderSelection>(asset_dialog_);
            static const SDL_DialogFileFilter filters[] = {{"Studio asset", "usum-asset"}};
            asset_dialog_->pending = true;
            SDL_ShowOpenFileDialog([](void *data, const char *const *files, int) {
                std::unique_ptr<std::shared_ptr<FolderSelection>> owner(static_cast<std::shared_ptr<FolderSelection> *>(data));
                auto &state = **owner; std::lock_guard lock(state.mutex);
                state.pending = false; state.ready = true; state.path.clear(); state.error.clear();
                if (!files) state.error = SDL_GetError(); else if (files[0]) state.path = files[0];
            }, owner, window_, filters, 1, nullptr, false);
        });
        if (ImGui::MenuItem("Make independent copy", nullptr, false, document_ && !document_->shadow_model &&
            (document_->is_pokemon() || (document_->area < 0 && !document_->clothing &&
                !document_->sources.empty() && document_->sources.front().original.size() >= 2 &&
                document_->sources.front().original[0] == 'C' && document_->sources.front().original[1] == 'M')))) {
            auto package = model_asset_package(*editor_->document());
            editor_->request_leave([this, package = std::move(package), dump] { open_independent(package, dump); });
        }
        if (ImGui::BeginMenu("Project assets")) {
            std::string selected;
            for (const auto &[key, edit] : store->edits)
                if (edit.kind == "studio-asset" && ImGui::MenuItem((edit.label + "##" + key).c_str())) selected = key;
            if (!selected.empty()) editor_->request_leave([this, selected, dump] {
                open_independent(decode_asset_package(read_file(project_store()->document(selected))), dump, selected.substr(13));
            });
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Send to asset library...", nullptr, false,
                            document_ && document_->project_asset)) {
            auto name = document_->name;
            std::snprintf(object_name_.data(), object_name_.size(), "%s", name.c_str());
            publish_object = true;
        }
        if (ImGui::MenuItem("Create overworld character...", nullptr, false, document_ && can_convert_overworld_character(*document_)))
            inspector_page_ = "Overworld character";
        if (ImGui::MenuItem("Close model", nullptr, false, bool(document_))) editor_->request_leave([this] {
            editor_->clear(); document_.reset(); renderer_.set_scene(std::make_shared<Environment>()); selection_ = {};
        });
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    if (publish_object) ImGui::OpenPopup("Save Studio object to library");
        if (ImGui::BeginPopupModal("Save Studio object to library", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Object name", object_name_.data(), object_name_.size());
            ImGui::TextWrapped("Save a static object copy to this project's library. Add it to a map from Authoring > Asset Library > Project library.");
            if (ImGui::Button("Save object")) {
                try {
                    auto package = export_studio_object(*editor_->document(), object_name_.data());
                    auto key = "asset-library/" + sha256(package);
                    auto previous = store->edits;
                    try {
                        store->capture({key, "asset-library", object_name_.data(), "", {}}, package);
                        store->save();
                    } catch (...) { store->edits = std::move(previous); throw; }
                    error_ = "Object saved. Find it in Authoring > Asset Library > Project library.";
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception &e) { error_ = e.what(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    object_import_dialog(dump);
    if (!error_.empty()) ImGui::TextWrapped("%s", error_.c_str());
    ImGui::End();
    std::string path, error;
    {
        std::lock_guard lock(asset_dialog_->mutex);
        if (!asset_dialog_->ready) return;
        asset_dialog_->ready = false; path = asset_dialog_->path; error = asset_dialog_->error;
    }
    try {
        require(error.empty(), error);
        if (!path.empty()) {
            auto package = decode_asset_package(read_file(std::filesystem::u8path(path)));
            if (text(package.at("type")) == "new") {
                auto geometry = parse_model_exchange(text(package.at("models/0.usum-model")));
                require(geometry.joints.empty(), "Rigged assets need a character template. Open one in Studio and use Blender > Import from Blender.");
                new_object_geometry_ = std::move(geometry);
                new_object_materials_.assign(new_object_geometry_->meshes.size(), 0);
                std::snprintf(object_name_.data(), object_name_.size(), "%s", text(package.at("name")).c_str());
                object_templates_.clear();
                new_object_template_.reset();
                object_template_index_ = -1;
                for (const auto &[key, edit] : store->edits)
                    if (edit.kind == "asset-library")
                        object_templates_.push_back({edit.label, decode_asset_package(read_file(store->document(key)))});
                if (document_ && document_->project_asset)
                    object_templates_.insert(object_templates_.begin(), {"Current Studio object", decode_asset_package(export_studio_object(*editor_->document(), document_->name))});
                if (object_templates_.empty()) {
                    auto catalog = load_map_resources(dump, 0);
                    for (std::size_t i = 0; i < catalog.entries.size(); ++i) {
                        auto &entry = catalog.entries[i];
                        if (!entry.reusable_static()) continue;
                        try {
                            auto scene = preview_map_resource(dump, entry);
                            auto asset = begin_project_asset(i, scene);
                            asset.name = entry.name;
                            object_templates_.push_back({entry.name, decode_asset_package(export_asset_package(dump, catalog, asset))});
                        } catch (const std::exception &) { }
                        if (object_templates_.size() >= 12) break;
                    }
                }
                error_.clear();
            } else
                editor_->request_leave([this, package = std::move(package), dump] { open_independent(package, dump); });
        }
    } catch (const std::exception &e) { error_ = e.what(); }
}
void ModelWorkspace::object_import_dialog(const std::filesystem::path &dump) {
    if (!new_object_geometry_) return;
    if (!ImGui::IsPopupOpen("Open Blender object")) ImGui::OpenPopup("Open Blender object");
    ImGui::SetNextWindowSize({620, 500}, ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal("Open Blender object", nullptr, ImGuiWindowFlags_None)) return;
    ImGui::InputText("Object name", object_name_.data(), object_name_.size());
    ImGui::TextWrapped("Choose game materials for this Blender object. The template remains unchanged; textures and materials can be edited in Studio afterward.");
    auto choose = [&](int i) {
        try {
            new_object_template_ = std::make_unique<ModelDocument>(open_studio_object(object_templates_.at(i).second, dump));
            object_template_exchange_ = MaterialDocument(*new_object_template_).model_exchange();
            object_template_index_ = i;
            std::fill(new_object_materials_.begin(), new_object_materials_.end(), 0);
            error_.clear();
        } catch (const std::exception &e) { error_ = e.what(); }
    };
    if (object_template_index_ < 0 && !object_templates_.empty()) choose(0);
    if (ImGui::BeginCombo("Material template", object_template_index_ < 0 ? "No templates available" : object_templates_[object_template_index_].first.c_str())) {
        for (unsigned i = 0; i < object_templates_.size(); ++i)
            if (ImGui::Selectable(object_templates_[i].first.c_str(), object_template_index_ == int(i))) choose(int(i));
        ImGui::EndCombo();
    }
    if (new_object_template_) {
        const auto &exchange = object_template_exchange_;
        ImGui::BeginChild("Object materials", {0, -110});
        for (std::size_t i = 0; i < new_object_geometry_->meshes.size(); ++i) {
            ImGui::PushID(int(i));
            ImGui::TextWrapped("%s", new_object_geometry_->meshes[i].name.c_str());
            auto label = [&](std::size_t m) { return exchange.meshes[m].name + " / " + exchange.meshes[m].texture; };
            if (ImGui::BeginCombo("Game material", label(new_object_materials_[i]).c_str())) {
                for (std::size_t m = 0; m < exchange.meshes.size(); ++m)
                    if (ImGui::Selectable((label(m) + "##" + std::to_string(m)).c_str(), new_object_materials_[i] == m)) new_object_materials_[i] = m;
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (ImGui::Button("Open in Studio")) {
            try {
                auto package = new_studio_object(*new_object_template_, *new_object_geometry_, new_object_materials_, object_name_.data());
                editor_->request_leave([this, package = std::move(package), dump] { open_independent(package, dump); });
                new_object_geometry_.reset();
                new_object_template_.reset();
                object_templates_.clear();
                error_.clear();
                ImGui::CloseCurrentPopup();
            } catch (const std::exception &e) { error_ = e.what(); }
        }
        ImGui::SameLine();
    } else ImGui::TextWrapped("No static templates found. Add a native object to the project library first.");
    if (ImGui::Button("Cancel")) {
        new_object_geometry_.reset(); new_object_template_.reset(); object_templates_.clear();
        ImGui::CloseCurrentPopup();
    }
    if (!error_.empty()) ImGui::TextWrapped("%s", error_.c_str());
    ImGui::EndPopup();
}
void ModelWorkspace::viewport(std::uint32_t frame) {
    ImGui::Begin(studio_ ? "Studio viewport" : "Model viewport");
    if (!document_) {
        ImGui::TextWrapped(studio_ ? "Open an asset to begin."
                                   : "Select a model in the browser to preview it.");
        ImGui::End();
        return;
    }
    if (refresh_inspector_.feeding.active) {
        if (studio_ && editor_->document())
            document_->refresh_feeding = editor_->document()->model.refresh_feeding;
        refresh_inspector_.feeding.viewport(
            *document_, renderer_,
            studio_ && editor_->document() ? editor_->document()->revision() : 0);
        ImGui::End();
        return;
    }
    refresh_inspector_.sync(*document_, renderer_);
    auto &io = ImGui::GetIO();
    if (auto picked = renderer_.poll_pick(frame)) {
        selection_.select_mesh(*picked,
                               *picked >= 0 ? int(document_->scene->draws[*picked].material) : -1,
                               pick_additive_);
        if (selection_.draw >= 0)
            selection_.material = int(document_->scene->draws[selection_.draw].material);
        selection_.focus = *picked >= 0 && !renderer_.refresh.active();
        if (renderer_.refresh.active()) {
            renderer_.refresh.selected = -1;
            if (*picked >= 0 && document_->refresh_regions) {
                auto binding = bind_refresh_material(
                    *document_->refresh_regions,
                    document_->scene->materials[std::size_t(selection_.material)],
                    document_->texture_prefix);
                if (binding.mask >= 0)
                    renderer_.refresh.selected = renderer_.picked_refresh_region();
            }
        }
    }
    if (settings_editor_.active && settings_editor_.sendout_active()) {
        settings_editor_.viewport(*document_, renderer_.playback.seconds);
        ImGui::End();
        return;
    }
    {
        if (ImGui::Button("Frame"))
            fit(selection_.draw);
        ImGui::SameLine();
        if (ImGui::Button("Animation"))
            animation_controls_open_ = !animation_controls_open_;
        ImGui::SameLine();
        if (ImGui::Button("Display"))
            ImGui::OpenPopup("Model display");
        ImGui::SameLine();
        if (ImGui::Button("Controls"))
            ImGui::OpenPopup("Model navigation");
        if (ImGui::BeginPopup("Model navigation")) {
            ImGui::TextUnformatted(viewport_navigation_help);
            ImGui::TextUnformatted("Click: select | Ctrl+click: add/remove | F: frame selection");
            ImGui::EndPopup();
        }
    }
    const float toolbar_width = 460.f;
    if (animation_controls_open_) {
        ImGui::SetNextWindowSize({480, 235}, ImGuiCond_FirstUseEver);
        auto origin = ImGui::GetWindowPos();
        ImGui::SetNextWindowPos({origin.x + 20, origin.y + 65}, ImGuiCond_FirstUseEver);
        const bool visible = ImGui::Begin(studio_ ? "Studio animation" : "Model animation",
                                          &animation_controls_open_, ImGuiWindowFlags_NoDocking);
        if (visible) {
            if (document_->is_pokemon() && !document_->shadow_model) {
                studio::TutorialWidgets::Checkbox("model_workspace", "Battle arena preview",
                                                  &settings_editor_.active);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Actual game arena and trainer; edit Pokemon settings in the Settings tab");
            }
            auto select_motion = [&](int index) {
                document_->select_motion(index, repeat_);
                renderer_.refresh_materials();
            };
            if (studio::TutorialWidgets::Button("model_workspace", playing_ ? "Pause" : "Play"))
                playing_ = !playing_;
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("model_workspace", "Restart"))
                renderer_.playback.seconds = 0;
            ImGui::SameLine();
            if (studio::TutorialWidgets::Checkbox("model_workspace", "Repeat", &repeat_) &&
                document_->area < 0)
                select_motion(document_->motion);
            if (toolbar_width >= 27 * ImGui::GetFontSize())
                ImGui::SameLine();
            ImGui::SetNextItemWidth(85);
            ImGui::DragFloat("Speed", &speed_, .05f, .05f, 4, "%.2fx",
                             ImGuiSliderFlags_AlwaysClamp);
            if (document_->area < 0) {
                ImGui::SetNextItemWidth(150);
                if (document_->is_pokemon() &&
                    ImGui::Combo("Motion set", &motion_group_,
                                 TargetProfile::pokemon_motion_names.data(), 4)) {
                    auto it = std::find_if(
                        document_->motions.begin(), document_->motions.end(), [&](auto &m) {
                            return int(m.group) == motion_group_ && m.error.empty();
                        });
                    select_motion(
                        it == document_->motions.end() ? -1 : int(it - document_->motions.begin()));
                    renderer_.playback.seconds = 0;
                }
                if (toolbar_width >= 36 * ImGui::GetFontSize())
                    ImGui::SameLine();
                ImGui::SetNextItemWidth(std::max(100.f, ImGui::GetContentRegionAvail().x -
                                                            ImGui::CalcTextSize("Motion").x -
                                                            ImGui::GetStyle().ItemInnerSpacing.x));
                const char *current = document_->motion < 0
                                          ? "Bind pose"
                                          : document_->motions[document_->motion].name.c_str();
                if (ImGui::BeginCombo("Motion", current)) {
                    if (ImGui::Selectable("Bind pose", document_->motion < 0)) {
                        select_motion(-1);
                        renderer_.playback.seconds = 0;
                    }
                    for (unsigned i = 0; i < document_->motions.size(); ++i) {
                        auto &m = document_->motions[i];
                        if (document_->is_pokemon() && int(m.group) != motion_group_)
                            continue;
                        ImGui::BeginDisabled(!m.error.empty());
                        if (ImGui::Selectable(m.name.c_str(), document_->motion == int(i))) {
                            select_motion(int(i));
                            renderer_.playback.seconds = 0;
                        }
                        ImGui::EndDisabled();
                        if (!m.error.empty() &&
                            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            ImGui::SetTooltip("%s", m.error.c_str());
                    }
                    ImGui::EndCombo();
                }
            }
            if (document_->is_pokemon()) {
                if (studio::TutorialWidgets::Checkbox("model_workspace", "Looping effects",
                                                      &document_->looping_effects))
                    select_motion(document_->motion);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Play the motion set's looping overlay alongside its main action. "
                        "Select an overlay in Motion to inspect it alone.");
                if (document_->looping_overlay >= 0) {
                    ImGui::SameLine();
                    ImGui::TextDisabled(
                        "%s", document_->motions[document_->looping_overlay].name.c_str());
                }
            }
            if (studio_) {
                studio::TutorialWidgets::Checkbox("model_workspace", "Preview material motions",
                                                  &renderer_.playback.materials);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Includes looping material effects. Disable to inspect the base "
                        "material values being edited");
            }
        }
        ImGui::End();
    }
    bool bone_handles = !document_->shadow_model && !geometry_tab_ && studio_ &&
                        inspector_page_ == "Motions" && motion_kind_ == 1 && document_->area < 0 &&
                        !document_->clothing && !renderer_.refresh.active() &&
                        editor_->document() && editor_->editing_available();
    if (bone_handles)
        bone_gizmo_.toolbar();
    else if (editor_ && editor_->document())
        bone_gizmo_.cancel(*editor_->document(), *document_, repeat_);
    float duration =
        document_->motion >= 0
            ? std::max({document_->motions[document_->motion].skeletal.frames,
                        document_->motions[document_->motion].material.frames,
                        document_->motions[document_->motion].visibility.clock.frames}) /
                  30.f
            : 0;
    if (document_->area >= 0) {
        for (auto &a : document_->scene->material_animations)
            if (!a.daily) {
                a.motion.looping = repeat_;
                duration = std::max(duration, a.motion.frames / 30);
            }
        for (auto &rig : document_->scene->skeletons)
            if (!rig.daily)
                duration = std::max(duration, rig.motion.frames / 30);
    }
    if (refresh_inspector_.painting())
        playing_ = false;
    if (!document_->scene->weather_particles.empty())
        duration = std::max(duration, 30.f);
    if (playing_ && renderer_.ready() && duration > 0) {
        renderer_.playback.seconds += std::min(io.DeltaTime, .1f) * speed_;
        if (!repeat_ && document_->looping_overlay < 0 && renderer_.playback.seconds >= duration) {
            renderer_.playback.seconds = duration;
            playing_ = false;
        }
    }
    float time = duration > 0
                     ? float(repeat_ ? std::fmod(renderer_.playback.seconds, duration)
                                     : std::min(renderer_.playback.seconds, double(duration)))
                     : 0;
    auto time_slider = [&] {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##model-time", &time, 0, std::max(duration, .01f),
                               "%.2f seconds")) {
            renderer_.playback.seconds = time;
            playing_ = false;
        }
    };
    if (settings_editor_.active) {
        time_slider();
        settings_editor_.viewport(*document_, renderer_.playback.seconds);
        ImGui::End();
        return;
    }
    ImGui::SetNextWindowSize({380, 0}, ImGuiCond_Appearing);
    if (ImGui::BeginPopup("Model display")) {
        ImGui::Checkbox("PICA float24 UV precision", &renderer_.pica_texture_precision);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Round texture coordinates to 16 fraction bits before sampling. "
                              "Experimental approximation; preview only.");
        ImGui::BeginDisabled(renderer_.refresh.active());
        studio::TutorialWidgets::Checkbox("model_workspace", "Outlines", &renderer_.outlines);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Game-style material outlines. Preview only; does not change exported assets.");
        ImGui::SameLine();
        studio::TutorialWidgets::Checkbox("model_workspace", "Wireframe", &renderer_.wireframe);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Triangle edges, including back faces. Ctrl+click still selects surfaces.");
        ImGui::EndDisabled();
        if (toolbar_width >= 27 * ImGui::GetFontSize())
            ImGui::SameLine();
        if (studio::TutorialWidgets::SmallButton("model_workspace", "Background"))
            ImGui::OpenPopup("Viewport background");
        if (ImGui::BeginPopup("Viewport background")) {
            auto packed = renderer_.background_color;
            float color[3] = {float((packed >> 24) & 255) / 255, float((packed >> 16) & 255) / 255,
                              float((packed >> 8) & 255) / 255};
            if (ImGui::ColorPicker3("##background", color,
                                    ImGuiColorEditFlags_NoSidePreview |
                                        ImGuiColorEditFlags_NoSmallPreview))
                renderer_.background_color = (std::uint32_t(color[0] * 255 + .5f) << 24) |
                                             (std::uint32_t(color[1] * 255 + .5f) << 16) |
                                             (std::uint32_t(color[2] * 255 + .5f) << 8) | 255u;
            if (studio::TutorialWidgets::Button("model_workspace", "Dark"))
                renderer_.background_color = 0x17232bffu;
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("model_workspace", "Gray"))
                renderer_.background_color = 0x808080ffu;
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("model_workspace", "Light"))
                renderer_.background_color = 0xe8e8e8ffu;
            ImGui::TextUnformatted("Preview only");
            ImGui::EndPopup();
        }
        ImGui::EndPopup();
    }
    if (renderer_.refresh.active())
        ImGui::TextDisabled(
            "Refresh: %s",
            renderer_.refresh.selected >= 0
                ? std::string(refresh_region_label(std::uint8_t(renderer_.refresh.selected)))
                      .c_str()
            : refresh_inspector_.painting() ? "painting | Middle drag to orbit"
                                            : "all categories | Ctrl+click to select");
    auto size = ImGui::GetContentRegionAvail();
    size.y = std::max(1.f, size.y - ImGui::GetFrameHeightWithSpacing());
    auto resolution = ImGuiRenderer::viewport_resolution(size.x, size.y);
    size = {resolution.display_width, resolution.display_height};
    unsigned width = resolution.width, height = resolution.height;
    float view[16], projection[16];
    auto eye = camera_.eye();
    bx::mtxLookAt(view, {eye[0], eye[1], eye[2]},
                  {camera_.target[0], camera_.target[1], camera_.target[2]}, {0, 1, 0},
                  bx::Handedness::Right);
    bx::mtxProj(projection, 45, float(width) / height, camera_.near_clip(), camera_.far_clip(),
                bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
    if (!studio_ && category_ == 8 && effect_camera_preview_ && effect_camera_) {
        auto pose = effect_camera_->sample(effect_camera_frame_);
        bx::mtxLookAt(view, {pose.eye[0], pose.eye[1], pose.eye[2]},
                      {pose.target[0], pose.target[1], pose.target[2]},
                      {pose.up[0], pose.up[1], pose.up[2]}, bx::Handedness::Right);
        bx::mtxProj(projection, pose.fov, float(width) / height, pose.near_clip, pose.far_clip,
                    bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
    }
    if (!selection_.meshes.empty())
        renderer_.select_draws(selection_.meshes);
    else
        renderer_.select_draw(selection_.draw);
    auto texture = renderer_.render(width, height, view, projection, true, false, 0, false);
    bool flip = bgfx::getCaps()->originBottomLeft;
    ImGui::Image(ImTextureID(ImGuiRenderer::image_id(texture, false, ImGuiRenderer::preview_3ds)),
                 {std::max(size.x, 1.f), std::max(size.y, 1.f)}, {0, flip ? 1.f : 0.f},
                 {1, flip ? 0.f : 1.f});
    auto origin = ImGui::GetItemRectMin();
    bool hovered = ImGui::IsItemHovered();
    bool bone_captured =
        bone_handles &&
        bone_gizmo_.draw(*editor_->document(), *document_, renderer_, camera_, view, projection,
                         origin, size, hovered, playing_, repeat_, bone_);
    bool geometry_captured =
        geometry_tab_ && editor_->editing_available() &&
        geometry_editor_.viewport(*editor_->document(), *document_, renderer_, camera_, view,
                                  projection, origin, size, hovered);
    if (bone_captured || geometry_captured) studio::UndoShortcuts::block("material_editor");
    if (hovered && (!bone_captured || viewport_navigating()) &&
        (!geometry_captured || viewport_navigating()) && !io.WantTextInput) {
        if (!viewport_navigating() && (!refresh_inspector_.painting() || io.KeyCtrl) &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
            io.MouseDragMaxDistanceSqr[0] < io.MouseDragThreshold * io.MouseDragThreshold) {
            pick_additive_ = io.KeyCtrl;
            renderer_.request_pick(resolution.pixel_x(io.MousePos.x - origin.x),
                                   resolution.pixel_y(io.MousePos.y - origin.y), width, height,
                                   view, projection, true, false, 0, false);
        }
        viewport_navigation(camera_, window_, hovered);
        if (ImGui::IsKeyPressed(ImGuiKey_F))
            fit(selection_.draw);
    }
    refresh_inspector_.paint(studio_ && editor_->editing_available() ? editor_->document()
                                                                     : nullptr,
                             renderer_, view, projection, origin, size, hovered, selection_);
    if (studio_ && editor_->document()) {
        document_->refresh_regions = editor_->document()->model.refresh_regions;
        document_->refresh_feeding = editor_->document()->model.refresh_feeding;
    }
    if (show_bones_ || show_weights_)
        bones(view, projection, origin, size);
    {
        if (ImGui::Button(playing_ ? "Pause" : "Play"))
            playing_ = !playing_;
        ImGui::SameLine();
        if (ImGui::Button("Restart"))
            renderer_.playback.seconds = 0;
        ImGui::SameLine();
        time_slider();
    }
    ImGui::End();
}
void ModelWorkspace::bones(const float *view, const float *projection, ImVec2 origin, ImVec2 size) {
    if (document_->scene->skeletons.empty())
        return;
    auto &rig = document_->scene->skeletons[0];
    auto pose = evaluate_skeleton(rig, renderer_.playback.seconds, 12, true);
    std::vector<ImVec2> points(pose.size());
    std::vector<bool> visible(pose.size());
    for (unsigned i = 0; i < pose.size(); ++i) {
        auto world = pose_multiply(pose[i], rig.joints[i].bind);
        float a[4]{}, b[4]{};
        for (unsigned k = 0; k < 4; ++k)
            a[k] = view[k] * world[3] + view[k + 4] * world[7] + view[k + 8] * world[11] +
                   view[k + 12];
        for (unsigned k = 0; k < 4; ++k)
            for (unsigned j = 0; j < 4; ++j)
                b[k] += projection[j * 4 + k] * a[j];
        if (b[3] > .001f) {
            visible[i] = true;
            points[i] = {origin.x + (b[0] / b[3] * .5f + .5f) * size.x,
                         origin.y + (.5f - b[1] / b[3] * .5f) * size.y};
        }
    }
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    for (unsigned i = 0; i < points.size(); ++i)
        if (show_bones_ && visible[i]) {
            auto parent = rig.joints[i].parent;
            auto color =
                int(i) == bone_ ? IM_COL32(255, 160, 60, 255) : IM_COL32(100, 235, 240, 220);
            if (parent >= 0 && visible[parent])
                draw->AddLine(points[parent], points[i], color, int(i) == bone_ ? 3 : 1);
            draw->AddCircleFilled(points[i], int(i) == bone_ ? 5 : 2, color);
            if (int(i) == bone_)
                draw->AddText(points[i], color, rig.joints[i].name.c_str());
        }
    if (show_weights_ && bone_ >= 0) {
        for (auto &mesh : document_->scene->draws)
            if (renderer_.draw_visible(std::size_t(&mesh - document_->scene->draws.data())) &&
                mesh.skeleton == 0)
                for (auto &v : mesh.vertices) {
                    float weight = 0;
                    std::array<float, 3> p{};
                    for (unsigned k = 0; k < 4; ++k)
                        if (v.weights[k] > 0 && unsigned(v.joints[k]) < mesh.palette.size()) {
                            auto joint = mesh.palette[unsigned(v.joints[k])];
                            if (joint == bone_)
                                weight += v.weights[k];
                            auto &m = pose.at(joint);
                            for (unsigned j = 0; j < 3; ++j)
                                p[j] += v.weights[k] * (m[j * 4] * v.x + m[j * 4 + 1] * v.y +
                                                        m[j * 4 + 2] * v.z + m[j * 4 + 3]);
                        }
                    if (weight <= 0)
                        continue;
                    float a[4]{}, b[4]{};
                    for (unsigned k = 0; k < 4; ++k)
                        a[k] =
                            view[k] * p[0] + view[k + 4] * p[1] + view[k + 8] * p[2] + view[k + 12];
                    for (unsigned k = 0; k < 4; ++k)
                        for (unsigned j = 0; j < 4; ++j)
                            b[k] += projection[j * 4 + k] * a[j];
                    if (b[3] > .001f)
                        draw->AddCircleFilled(
                            {origin.x + (b[0] / b[3] * .5f + .5f) * size.x,
                             origin.y + (.5f - b[1] / b[3] * .5f) * size.y},
                            2, ImGui::ColorConvertFloat4ToU32({weight, .25f, 1 - weight, .9f}));
                }
    }
    draw->PopClipRect();
}
}
