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
            renderer_.playback.seconds = 0;
            selection_ = {};
            bone_ = -1;
            fit();
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
                return candidate.member == source.member &&
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
    bool busy =
        job_.valid() || catalog_job_.valid() || library_job_.valid() || clothing_job_.valid();
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
    ImGui::Begin("Models");
    static const char *categories[] = {
        "Pokemon",      "Battle characters", "Field characters & props", "Poke Balls",
        "Battle props", "Poke Beans",        "Battle arena parts",       "Clothing"};
    ImGui::BeginDisabled(busy || library_dialog_category_ >= 0 || clothing_dialog_slot_ >= 0);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##model-category", &category_, categories, 8)) {
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
    if (job_.valid() || library_job_.valid()) {
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("model_workspace", "Cancel"))
            cancel_ = true;
    }
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
    if (!category_)
        studio::TutorialWidgets::Checkbox("model_workspace", "Shiny textures", &shiny_);
    if (chosen_ >= 0 && std::size_t(chosen_) < count && primary_button("Open model"))
        open_selected();
    ImGui::EndDisabled();
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
                if (!busy && !stale && library_dialog_category_ < 0 &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    open_selected();
            }
            ImGui::PopID();
        }
    ImGui::EndChild();
    ImGui::End();
}
void ModelWorkspace::draw(std::uint32_t frame, const char *dump, const ArchiveSources &archives) {
    clothing_leave_dialog();
    settings_editor_.update();
    poll();
    if (!studio_)
        browse(dump, archives);
    if (studio_) {
        if (editor_->document() && document_)
            editor_->document()->model.looping_effects = document_->looping_effects;
        editor_->draw(renderer_, selection_, std::filesystem::u8path(dump));
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
    if (studio_ && (!geometry_tab_ || !editor_->editing_available()) && document_)
        geometry_editor_.deactivate(*document_, renderer_);
    viewport(frame);
    if (studio_)
        editor_->uvs(renderer_, selection_);
    else
        material_inspector(document_ ? document_->scene.get() : nullptr, renderer_, selection_,
                           "Model materials");
    ImGui::Begin(studio_ ? "Studio source" : "Model source");
    if (document_) {
        if (ImGui::BeginTabBar("Model information")) {
            if (studio::TutorialWidgets::BeginTabItem("model_workspace", "Source & dependencies")) {
                ImGui::TextWrapped("%s", document_->name.c_str());
                ImGui::TextWrapped("Dump: %s", document_->dump.string().c_str());
                ImGui::TextWrapped(
                    "%s", document_->kind == ModelAssetKind::ArchiveModel
                              ? "Original member and resource locations retained for inspection."
                              : "Original members and nested resource locations are retained for "
                                "material editing and export.");
                for (auto &s : document_->sources) {
                    ImGui::PushID(int(&s - document_->sources.data()));
                    if (ImGui::TreeNode(s.role.c_str())) {
                        ImGui::TextWrapped(
                            "%s / member %zu",
                            document_->archive_sources.resolve(document_->dump, s.archive)
                                .string()
                                .c_str(),
                            s.member);
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
void ModelWorkspace::fit(int selected) {
    auto &scene = *document_->scene;
    auto visible = evaluate_visibility(scene, renderer_.playback.seconds, 12, true);
    auto poses = evaluate_scene_poses(scene.skeletons, renderer_.player, renderer_.playback.seconds,
                                      12, true);
    std::array<float, 3> low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
    bool found = false;
    for (unsigned i = 0; i < scene.draws.size(); ++i) {
        if (!renderer_.draw_visible(i) || (selected >= 0 ? i != unsigned(selected) : !visible[i]))
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
    ImGui::Begin(studio_ ? "Studio details" : "Model details");
    if (!document_) {
        ImGui::TextWrapped(
            "Choose a category and Open model. Double-clicking a list entry also opens it.");
        ImGui::End();
        return;
    }
    auto &scene = *document_->scene;
    ImGui::TextWrapped("%s", document_->name.c_str());
    ImGui::Text("%zu mesh parts | %zu materials", scene.draws.size(), scene.materials.size());
    if (document_->is_pokemon()) {
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
        bundle_editor_.draw(*document_, window_);
        if (auto created = bundle_editor_.take_created())
            studio_request_ = std::move(created);
        ImGui::BeginDisabled(job_.valid());
        if (primary_button("Send to Studio", {-1, 0}))
            send_to_studio();
        if (studio::TutorialWidgets::Button("model_workspace", "Use as material donor", {-1, 0}))
            try {
                shader_donor_ = std::make_unique<ModelDocument>(studio_asset());
            } catch (const std::exception &e) {
                error_ = e.what();
            }
        if (document_->kind == ModelAssetKind::ArchiveModel)
            ImGui::TextWrapped("Select a mesh to send its source model to Studio. Outfits open one "
                               "part at a time.");
        if (studio::TutorialWidgets::Button("model_workspace", "Reload from source", {-1, 0})) {
            if (document_->clothing)
                open_clothing(*document_->clothing, document_->clothing_part, document_->dump);
            else if (document_->kind == ModelAssetKind::ArchiveModel) {
                auto &source = document_->sources.at(0);
                open_library(source.archive,
                             source.original[0] == 'B' ? ModelCategory::BattleArenas
                                                       : ModelCategory::FieldCharacters,
                             {source.member, document_->name});
            } else
                open(document_->pokemon, document_->shiny);
        }
        ImGui::EndDisabled();
    }
    if (ImGui::BeginTabBar("Inspect model", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (document_->is_pokemon() &&
            studio::TutorialWidgets::BeginTabItem("model_workspace", "Settings")) {
            settings_editor_.draw();
            ImGui::EndTabItem();
        }
        if (studio::TutorialWidgets::BeginTabItem("model_workspace", "Meshes")) {
            if (studio::TutorialWidgets::Button("model_workspace", "Frame model"))
                fit();
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("model_workspace", "Show all"))
                renderer_.show_all_draws();
            ImGui::TextWrapped("Visibility is preview-only; motion visibility still applies.");
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
                                      selection_.draw == int(i))) {
                    selection_.draw = int(i);
                    selection_.material = int(d.material);
                }
                if (selection_.draw == int(i)) {
                    ImGui::TextDisabled("%zu vertices | %zu triangles", d.vertices.size(),
                                        d.indices.size() / 3);
                    ImGui::TextWrapped("Material: %s", scene.materials[d.material].name.c_str());
                    ImGui::Text("Bone palette: %zu", d.palette.size());
                    if (studio::TutorialWidgets::Button("model_workspace", "Frame mesh"))
                        fit(int(i));
                    ImGui::SameLine();
                    if (studio::TutorialWidgets::Button("model_workspace", "Isolate")) {
                        for (unsigned j = 0; j < scene.draws.size(); ++j)
                            renderer_.set_draw_visible(j, j == i);
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        if (studio_ && editor_->document() && document_->area < 0 && !document_->clothing &&
            studio::TutorialWidgets::BeginTabItem("model_workspace", "Geometry")) {
            geometry_tab_ = true;
            if (!was_geometry)
                refresh_inspector_.reset();
            ImGui::BeginDisabled(!editor_->editing_available());
            geometry_editor_.panel(*editor_->document(), *document_, renderer_, playing_,
                                   selection_.material);
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }
        if (studio::TutorialWidgets::BeginTabItem("model_workspace", "Skeleton")) {
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
            ImGui::EndTabItem();
        }
        if (studio_ && editor_->document() && !document_->clothing &&
            studio::TutorialWidgets::BeginTabItem("model_workspace", "Blender")) {
            ImGui::BeginDisabled(!editor_->editing_available());
            model_exchange_editor_.draw(*editor_->document(), *document_, renderer_, playing_,
                                        window_);
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }
        if (document_->is_pokemon() && !document_->shadow_model &&
            studio::TutorialWidgets::BeginTabItem("model_workspace", "Refresh")) {
            refresh_inspector_.draw(*document_, renderer_, selection_,
                                    studio_ && editor_->editing_available() ? editor_->document()
                                                                            : nullptr);
            ImGui::EndTabItem();
        }
        if (studio_ && editor_->document() && !document_->shadow_model &&
            studio::TutorialWidgets::BeginTabItem("model_workspace", "Motions")) {
            ImGui::BeginDisabled(!editor_->editing_available());
            if (document_->area < 0 && !document_->clothing) {
                studio::TutorialWidgets::RadioButton("model_workspace", "Materials", &motion_kind_,
                                                     0);
                ImGui::SameLine();
                if (studio::TutorialWidgets::RadioButton("model_workspace", "Bones", &motion_kind_,
                                                         1))
                    playing_ = false;
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
            ImGui::EndTabItem();
        }
        if (cry_editor_ && !document_->shadow_model && document_->area < 0 &&
            document_->pokemon.species &&
            studio::TutorialWidgets::BeginTabItem("model_workspace", "Cries")) {
            cry_editor_->draw(*document_);
            ImGui::EndTabItem();
        }
        if (studio::TutorialWidgets::BeginTabItem("model_workspace", "Lighting")) {
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
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
void ModelWorkspace::viewport(std::uint32_t frame) {
    ImGui::Begin(studio_ ? "Studio viewport" : "Model viewport");
    if (!document_) {
        ImGui::TextWrapped("Browse the dump in Models to open an isolated model. Materials, "
                           "textures and skeleton inspection share the map renderer.");
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
        selection_.draw = *picked;
        selection_.material = *picked >= 0 ? int(document_->scene->draws[*picked].material) : -1;
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
    const float toolbar_width = ImGui::GetContentRegionAvail().x;
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
    ImGui::DragFloat("Speed", &speed_, .05f, .05f, 4, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    if (document_->area < 0) {
        ImGui::SetNextItemWidth(150);
        if (document_->is_pokemon() &&
            ImGui::Combo("Motion set", &motion_group_, TargetProfile::pokemon_motion_names.data(),
                         4)) {
            auto it =
                std::find_if(document_->motions.begin(), document_->motions.end(), [&](auto &m) {
                    return int(m.group) == motion_group_ && m.error.empty();
                });
            select_motion(it == document_->motions.end() ? -1
                                                         : int(it - document_->motions.begin()));
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
                if (!m.error.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
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
            ImGui::SetTooltip("Play the motion set's looping overlay alongside its main action. "
                              "Select an overlay in Motion to inspect it alone.");
        if (document_->looping_overlay >= 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", document_->motions[document_->looping_overlay].name.c_str());
        }
    }
    if (studio_) {
        studio::TutorialWidgets::Checkbox("model_workspace", "Preview material motions",
                                          &renderer_.playback.materials);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Includes looping material effects. Disable to inspect the base "
                              "material values being edited");
    }
    bool bone_handles = !document_->shadow_model && !geometry_tab_ && studio_ &&
                        motion_kind_ == 1 && document_->area < 0 && !document_->clothing &&
                        !renderer_.refresh.active() && editor_->document() &&
                        editor_->editing_available();
    if (bone_handles)
        bone_gizmo_.toolbar();
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
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##model-time", &time, 0, std::max(duration, .01f), "%.2f seconds")) {
        renderer_.playback.seconds = time;
        playing_ = false;
    }
    if (settings_editor_.active) {
        settings_editor_.viewport(*document_, renderer_.playback.seconds);
        ImGui::End();
        return;
    }
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
    ImGui::SameLine();
    if (studio::TutorialWidgets::SmallButton("model_workspace", "Controls"))
        ImGui::OpenPopup("Viewport controls");
    if (ImGui::BeginPopup("Viewport controls")) {
        ImGui::TextUnformatted("Ctrl+click: select material or Refresh category\nLeft drag: "
                               "orbit\nMiddle / right drag: pan\nWheel: zoom\nF: frame model");
        ImGui::EndPopup();
    }
    if (renderer_.refresh.active())
        ImGui::TextDisabled(
            "Refresh: %s",
            renderer_.refresh.selected >= 0
                ? std::string(refresh_region_label(std::uint8_t(renderer_.refresh.selected)))
                      .c_str()
            : refresh_inspector_.painting() ? "painting | Shift+drag to orbit"
                                            : "all categories | Ctrl+click to select");
    auto size = ImGui::GetContentRegionAvail();
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
    if (hovered && !bone_captured && !geometry_captured && !io.WantTextInput) {
        if (io.KeyCtrl && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
            io.MouseDragMaxDistanceSqr[0] < io.MouseDragThreshold * io.MouseDragThreshold)
            renderer_.request_pick(resolution.pixel_x(io.MousePos.x - origin.x),
                                   resolution.pixel_y(io.MousePos.y - origin.y), width, height,
                                   view, projection, true, false, 0, false);
        if (!io.KeyCtrl && (!refresh_inspector_.painting() || io.KeyShift) &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            camera_.rotate(io.MouseDelta.x, io.MouseDelta.y, false);
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
            ImGui::IsMouseDragging(ImGuiMouseButton_Right))
            camera_.pan(io.MouseDelta.x, io.MouseDelta.y);
        camera_.wheel(io.MouseWheel, false);
        if (ImGui::IsKeyPressed(ImGuiKey_F))
            fit();
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
