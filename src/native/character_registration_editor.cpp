#include "native/character_registration_editor.h"
#include "assets/character_registration.h"
#include "assets/model_library.h"
#include "native/project_binding.h"
#include <imgui.h>
namespace studio {
bool CharacterRegistrationEditor::accepts(const ModelDocument &donor) {
    return donor.kind == ModelAssetKind::ArchiveModel && donor.area < 0 && !donor.sources.empty() &&
           donor.sources.front().archive.generic_string() == TargetProfile::character_archive &&
           donor.sources.front().member > 1;
}
bool CharacterRegistrationEditor::draw(const ModelDocument &donor) {
    if (!accepts(donor))
        return false;
    auto *project = project_store();
    auto root = project ? project->root : std::filesystem::path{};
    if (root != project_root_) {
        project_root_ = root;
        registered_.reset();
        message_.clear();
        created_.reset();
    }
    ImGui::SeparatorText("New overworld character");
    ImGui::Text("Donor: %s / ID %zu", donor.name.c_str(), donor.sources.front().member);
    ImGui::TextWrapped("Creates an independent character ID with this donor's model, textures, "
                       "skeleton, animations and behavior metadata. The donor is retained.");
    if (!project) {
        ImGui::TextWrapped("Open a project to register a new character.");
        return false;
    }
    bool stage = false;
    std::string pending_key;
    registered_.reset();
    for (const auto &[key, edit] : project->edits) {
        if (edit.kind == "character-registration")
            pending_key = key;
        if (edit.kind == "character-registration-created") {
            if (!registration_ids_.contains(edit.blob)) {
                auto bytes = read_file(project->document(key));
                auto plan = CharacterRegistration::parse(std::string(bytes.begin(), bytes.end()));
                registration_ids_[edit.blob] = {plan.donor, plan.character};
            }
            const auto [source, character] = registration_ids_.at(edit.blob);
            if (source == donor.sources.front().member &&
                (!registered_ || character > *registered_)) {
                registered_ = character;
                registered_name_ = donor.name;
            }
        }
    }
    const bool pending = !pending_key.empty();
    ImGui::BeginDisabled(pending);
    if (ImGui::Button("Register character and reload", {-1, 0})) {
        try {
            save_editor_project();
            require(!project->unstaged() && project->base == project->current_overlay,
                    "Stage and reload existing project edits before registering a character.");
            Archive archive(project->source / TargetProfile::character_archive);
            auto plan =
                plan_character_registration(archive, unsigned(donor.sources.front().member));
            require(
                archive.decoded(plan.donor) == donor.sources.front().original,
                "The donor source changed. Reload it in Models before registering a character.");
            auto key = "character-registration/" + std::to_string(plan.character);
            project->capture({key,
                              "character-registration",
                              "Register character " + std::to_string(plan.character),
                              "",
                              {}},
                             project_text(plan.serialize()));
            project->save();
            registered_ = plan.character;
            registered_name_ = donor.name;
            message_ = "Registering character ID " + std::to_string(plan.character) + ".";
            stage = true;
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    }
    ImGui::EndDisabled();
    if (pending) {
        ImGui::TextWrapped("Registration is queued. Project > Stage and reload completes it.");
        if (ImGui::Button("Cancel queued registration")) {
            try {
                project->reset(pending_key);
                registered_.reset();
                message_ = "Queued registration removed.";
            } catch (const std::exception &e) {
                message_ = e.what();
            }
        }
    } else if (registered_) {
        ImGui::Text("New character ID: %u", *registered_);
        if (ImGui::Button("Open new character in Studio", {-1, 0})) {
            try {
                created_ = std::make_unique<ModelDocument>(load_library_model(
                    project->source, project->source / TargetProfile::character_archive,
                    ModelCategory::FieldCharacters, {*registered_, registered_name_}));
                message_.clear();
            } catch (const std::exception &e) {
                message_ = e.what();
            }
        }
    }
    ImGui::TextWrapped("After reloading, choose the new ID in Maps > Map settings > NPCs and "
                       "triggers > Add from selected. Refresh the character list if needed. "
                       "NPC placement and dialogue are configured there.");
    if (!message_.empty())
        ImGui::TextWrapped("%s", message_.c_str());
    return stage;
}
bool CharacterRegistrationEditor::draw_conversion(MaterialDocument &document,
                                                  ModelDocument &preview) {
    auto *project = project_store();
    ImGui::SeparatorText("Create overworld character");
    ImGui::TextWrapped("Keep this model, materials, textures and motions. Choose a "
                       "behavior donor for collision and shadow settings.");
    if (!project) {
        ImGui::TextWrapped("Open a project first.");
        return false;
    }
    try {
        if (donor_source_ != project->source || conversion_pokemon_ != document.model.is_pokemon()) {
            donor_source_ = project->source;
            conversion_donors_.clear();
            conversion_donor_types_.clear();
            conversion_pokemon_ = document.model.is_pokemon();
            conversion_donor_ = -1;
            Archive archive(project->source / TargetProfile::character_archive);
            for (const auto &entry :
                 load_model_library(project->source / TargetProfile::character_archive,
                                    ModelCategory::FieldCharacters)) {
                auto cm = Container::parse(archive.decoded(entry.member), "CM");
                if (cm.files.size() != 6 || cm.files[3].size() < 128)
                    continue;
                auto type = u32(cm.files[3], 0);
                if (type == 1 || (conversion_pokemon_ && type == 2 && cm.files[3].size() >= 132)) {
                    conversion_donors_.push_back(entry);
                    conversion_donor_types_.push_back(type);
                }
            }
            if (!conversion_donors_.empty()) {
                conversion_donor_ = 0;
                if (conversion_pokemon_)
                    for (unsigned i = 0; i < conversion_donor_types_.size(); ++i)
                        if (conversion_donor_types_[i] == 2) { conversion_donor_ = int(i); break; }
            }
        }
        if (conversion_source_ != document.identity()) {
            conversion_source_ = document.identity();
            conversion_motions_ = default_overworld_motions(document.model);
            message_.clear();
            if (!document.model.is_pokemon())
                for (unsigned i = 0; i < conversion_donors_.size(); ++i)
                    if (conversion_donors_[i].name == document.model.name) {
                        conversion_donor_ = int(i);
                        break;
                    }
        }
        auto label = [&](const LibraryModel &entry) {
            return entry.name + " / ID " + std::to_string(entry.member);
        };
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##Behavior donor",
                              conversion_donor_ < 0
                                  ? "Choose behavior donor"
                                  : label(conversion_donors_[conversion_donor_]).c_str())) {
            for (unsigned i = 0; i < conversion_donors_.size(); ++i)
                if (ImGui::Selectable(label(conversion_donors_[i]).c_str(),
                                      conversion_donor_ == int(i)))
                    conversion_donor_ = int(i);
            ImGui::EndCombo();
        }
        if (conversion_donor_ >= 0 && conversion_donor_types_[conversion_donor_] == 2)
            ImGui::SliderInt("Scale (%)", &conversion_scale_, 1, 300);
        else
            ImGui::TextWrapped("NPC behavior uses the model's authored size. Donor attachments are removed.");
        auto motion = [&](const char *name, int &selected) {
            ImGui::TextUnformatted(name);
            ImGui::SetNextItemWidth(-1);
            ImGui::PushID(name);
            if (ImGui::BeginCombo(
                    "##Motion", selected < 0 ? "Bind pose (no animation)"
                                             : document.model.motions.at(selected).name.c_str())) {
                if (ImGui::Selectable("Bind pose (no animation)", selected < 0))
                    selected = -1;
                for (unsigned i = 0; i < document.model.motions.size(); ++i) {
                    const auto &m = document.model.motions[i];
                    if (!m.error.empty() || m.skeletal.tracks.empty())
                        continue;
                    auto title = (document.model.is_pokemon()
                                      ? std::string(TargetProfile::pokemon_motion_names.at(m.group))
                                      : (m.group == 0 ? std::string("Body") : std::string("Face"))) +
                                 " / " + m.name;
                    if (ImGui::Selectable((title + "##" + std::to_string(i)).c_str(),
                                          selected == int(i)))
                        selected = int(i);
                }
                ImGui::EndCombo();
            }
            ImGui::BeginDisabled(selected < 0);
            if (ImGui::Button("Preview motion")) {
                preview.select_motion(selected);
                motion_preview_ = true;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        };
        motion("Idle", conversion_motions_.idle);
        motion("Walking", conversion_motions_.walk);
        motion("Running", conversion_motions_.run);
        ImGui::TextWrapped(
            "Other generic action slots use idle. Source clips remain available after those slots. "
            "Donor facial motions and bone constraints are removed; separate looping overlays are "
            "not combined with the selected clips.");
        const bool pending =
            std::any_of(project->edits.begin(), project->edits.end(), [](const auto &e) {
                return e.second.kind == "character-registration";
            });
        ImGui::BeginDisabled(pending || conversion_donor_ < 0);
        const bool create = ImGui::Button("Create character and reload", {-1, 0});
        ImGui::EndDisabled();
        if (create) {
            save_editor_project();
            Archive archive(project->source / TargetProfile::character_archive);
            auto plan = plan_character_registration(
                archive, unsigned(conversion_donors_.at(conversion_donor_).member));
            plan.replacement_model =
                convert_overworld_character(document, archive.decoded(plan.donor),
                                          conversion_motions_, unsigned(conversion_scale_));
            auto key = "character-registration/" + std::to_string(plan.character);
            project->capture(
                {key,
                 "character-registration",
                 document.model.name + " / character " + std::to_string(plan.character),
                 "",
                 {}},
                project_text(plan.serialize()));
            project->save();
            message_ = "Created character ID " + std::to_string(plan.character) +
                       ". Choose it in the map's NPC editor after reloading.";
            return true;
        }
        if (pending)
            ImGui::TextWrapped(
                "A registration is queued. Stage and reload before creating another character.");
    } catch (const std::exception &e) {
        message_ = e.what();
    }
    if (!message_.empty())
        ImGui::TextWrapped("%s", message_.c_str());
    return false;
}

}
