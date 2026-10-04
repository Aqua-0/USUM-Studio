#include "native/tutorial_widgets.h"
#include "native/battle_model_addition_editor.h"
#include "native/project_binding.h"
#include <imgui.h>
namespace studio {
bool BattleModelAdditionEditor::accepts(const ModelDocument &donor) {
    return donor.kind == ModelAssetKind::ArchiveModel && donor.area < 0 && !donor.clothing &&
           donor.independent_asset.empty() && !donor.sources.empty() &&
           donor.sources.front().archive.generic_string() == TargetProfile::battle_trainers_archive &&
           (donor.name.starts_with("tr") || model_category_matches(ModelCategory::PokeBalls, donor.name));
}
bool BattleModelAdditionEditor::draw(const ModelDocument &donor) {
    if (!accepts(donor)) return false;
    auto *project = project_store();
    const auto root = project ? project->root : std::filesystem::path{};
    if (root != project_root_) {
        project_root_ = root;
        message_.clear();
        created_.reset();
    }
    const bool ball = model_category_matches(ModelCategory::PokeBalls, donor.name);
    const auto category = ball ? ModelCategory::PokeBalls : ModelCategory::BattleCharacters;
    ImGui::SeparatorText(ball ? "New Poke Ball model" : "New battle character");
    ImGui::Text("Donor: %s / entry %zu", donor.name.c_str(), donor.sources.front().member);
    ImGui::TextWrapped("Copies this source entry's complete model package into an independent archive entry. "
                       "Open the new entry in Studio to edit its model, textures and motions.");
    ImGui::TextWrapped(ball
        ? "This adds a ball model. A new usable ball type also needs item, capture and effect setup plus a game-side model reference."
        : "This adds a battle model. Using it in battle also requires a trainer appearance reference in the game.");
    if (!project) {
        ImGui::TextWrapped("Open a project to add a model.");
        return false;
    }
    bool stage = false;
    try {
        std::string pending;
        std::optional<BattleModelAddition> latest;
        for (const auto &[key, edit] : project->edits) {
            if (edit.kind == "battle-model-addition") pending = key;
            if (edit.kind != "battle-model-added") continue;
            auto plan = BattleModelAddition::parse(text(read_file(project->document(key))));
            if (plan.category == category && plan.donor == donor.sources.front().member &&
                (!latest || latest->member < plan.member)) latest = std::move(plan);
        }
        if (pending.empty()) {
            if (TutorialWidgets::Button("battle_model_addition", ball ? "Save new Poke Ball model" : "Save new battle character", {-1, 0})) {
                save_editor_project();
                require(!project->unstaged() && project->base == project->current_overlay,
                        "Stage and reload existing edits before allocating a new model entry.");
                Archive archive(project->source / TargetProfile::battle_trainers_archive);
                auto plan = plan_battle_model_addition(archive, unsigned(donor.sources.front().member), category);
                require(archive.decoded(plan.donor) == donor.sources.front().original,
                        "The donor changed. Reload it in Models before adding a copy.");
                pending = "battle-model-addition/" + std::to_string(plan.member);
                project->capture({pending, "battle-model-addition",
                                  std::string(ball ? "New Poke Ball model " : "New battle character ") +
                                      std::to_string(plan.member), "", {}}, project_text(plan.serialize()));
                project->save();
                message_ = "Saved new archive entry " + std::to_string(plan.member) +
                           ". Stage and reload to open it; building is separate.";
            }
        }
        if (!pending.empty()) {
            ImGui::TextWrapped("A battle character or Poke Ball addition is saved. Stage and reload it before adding another.");
            if (TutorialWidgets::Button("battle_model_addition", "Stage and reload addition", {-1, 0})) stage = true;
            if (TutorialWidgets::Button("battle_model_addition", "Cancel saved addition")) {
                project->reset(pending);
                message_ = "Saved addition removed.";
            }
        }
        if (latest) {
            if (pending.empty() && message_.starts_with("Saved new archive entry "))
                message_.clear();
            ImGui::Text("Latest created archive entry: %u", latest->member);
            if (TutorialWidgets::Button("battle_model_addition", "Open new entry in Studio", {-1, 0})) {
                created_ = std::make_unique<ModelDocument>(load_library_model(
                    project->source, project->source / TargetProfile::battle_trainers_archive,
                    category, {latest->member, latest->name}));
                message_.clear();
            }
        }
    } catch (const std::exception &e) {
        message_ = e.what();
    }
    if (!message_.empty()) ImGui::TextWrapped("%s", message_.c_str());
    return stage;
}
}
