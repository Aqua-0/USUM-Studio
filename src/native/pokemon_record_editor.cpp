#include "native/pokemon_record_editor.h"
#include "native/tutorial_widgets.h"
#include "field/area.h"
#include "field/map_catalog.h"
#include <imgui.h>
#include <algorithm>
namespace studio {
namespace {
std::string key(PokemonRecordKind kind, unsigned row) {
    return "pokemon-record/" + std::to_string(unsigned(kind)) + "/" + std::to_string(row);
}
}
PokemonRecordDocument project_pokemon_record(const std::filesystem::path &source, PokemonRecordKind kind, unsigned row) {
    auto document = PokemonRecordDocument::load(source, kind, row);
    if (auto *store = project_store())
        if (auto path = store->document(key(kind, row)); !path.empty()) document.restore(text(read_file(path)));
    return document;
}
PokemonRecordDocument PokemonRecordEditor::record(const std::filesystem::path &source, PokemonRecordKind kind, unsigned row) const {
    if (document_ && source_ == source && document_->kind() == kind && document_->row() == row) return *document_;
    return project_pokemon_record(source, kind, row);
}
void PokemonRecordEditor::reset() {
    binding_.unbind(); document_.reset(); names_.clear(); open_ = false; error_.clear();
}
bool PokemonRecordEditor::pending() const {
    if (!document_) return false;
    for (std::size_t i = 0; i < draft_.size(); ++i)
        if (draft_[i] != document_->value(i)) return true;
    return false;
}
void PokemonRecordEditor::refresh() {
    draft_.clear();
    for (std::size_t i = 0; i < document_->fields().size(); ++i) draft_.push_back(document_->value(i));
}
void PokemonRecordEditor::open(const std::filesystem::path &source, PokemonRecordKind kind, unsigned row) {
    open_ = true;
    try {
        require(project_store(), "Open a project to edit Pokemon records");
        if (document_ && source == source_ && document_->kind() == kind && document_->row() == row) return;
        save_editor_project();
        auto next = std::make_unique<PokemonRecordDocument>(project_pokemon_record(source, kind, row));
        using Names = PokemonRecordField::Names;
        Archive text_archive(source / TargetProfile::location_text_archive);
        std::map<Names, std::vector<std::string>> names;
        names[Names::Species] = decode_location_text(text_archive.decoded(TargetProfile::pokemon_names_member));
        names[Names::Item] = decode_location_text(text_archive.decoded(TargetProfile::item_names_member));
        names[Names::Move] = decode_location_text(text_archive.decoded(TargetProfile::move_names_member));
        document_ = std::move(next); source_ = source; names_ = std::move(names);
        refresh(); error_.clear();
        binding_.bind("pokemon-record", key(kind, row), std::string(pokemon_record_name(kind)) + " " + std::to_string(row),
                      std::to_string(unsigned(kind)) + " " + std::to_string(row),
                      [this] { return document_->dirty(); },
                      [this] { return project_text(document_->serialize()); },
                      [this] { document_->mark_saved(); });
        binding_.ready([this] {
            if (pending()) {
                open_ = true;
                throw std::runtime_error("Apply or cancel the Pokemon record fields before saving");
            }
        });
    } catch (const std::exception &error) { error_ = error.what(); }
}
bool PokemonRecordEditor::draw() {
    if (!open_) return false;
    bool changed = false;
    ImGui::SetNextWindowSize({560, 600}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Pokemon records", &open_)) {
        if (document_) {
            ImGui::SeparatorText((std::string(pokemon_record_name(document_->kind())) + " " + std::to_string(document_->row())).c_str());
            ImGui::TextWrapped("This edits a shared record. Every interaction using this record receives the change. Other fields are preserved.");
            if (document_->kind() == PokemonRecordKind::Gift)
                ImGui::TextWrapped("Special gift IDs keep their existing game behavior. Changing the Pokemon does not change that behavior.");
            ImGui::BeginDisabled(!document_->can_undo());
            if (TutorialWidgets::Button("pokemon_records", "Undo")) { document_->undo(); refresh(); changed = true; }
            ImGui::EndDisabled(); ImGui::SameLine();
            ImGui::BeginDisabled(!document_->can_redo());
            if (TutorialWidgets::Button("pokemon_records", "Redo")) { document_->redo(); refresh(); changed = true; }
            ImGui::EndDisabled();
            ImGui::BeginChild("record-fields", {0, -140});
            static ImGuiTextFilter filter;
            const auto &fields = document_->fields();
            for (std::size_t i = 0; i < fields.size(); ++i) {
                const auto &field = fields[i];
                ImGui::PushID(int(i));
                auto found = names_.find(field.names);
                if (found != names_.end()) {
                    const auto &names = found->second;
                    auto label = draft_[i] < names.size() ? names[draft_[i]] : "ID " + std::to_string(draft_[i]);
                    if (field.names == PokemonRecordField::Names::Move && draft_[i] == 0) label = "No explicit move";
                    if (field.names == PokemonRecordField::Names::Item && draft_[i] == 65535) label = "None (stored sentinel)";
                    if (ImGui::BeginCombo(field.label.c_str(), label.c_str())) {
                        if (ImGui::IsWindowAppearing()) filter.Clear();
                        filter.Draw("Search name or ID");
                        ImGui::BeginChild("choices", {0, 200});
                        if (field.names == PokemonRecordField::Names::Item && document_->kind() == PokemonRecordKind::Trade &&
                            ImGui::Selectable("None", draft_[i] == 65535)) { draft_[i] = 65535; ImGui::CloseCurrentPopup(); }
                        for (unsigned value = field.minimum; value < names.size(); ++value) {
                            auto choice = field.names == PokemonRecordField::Names::Move && value == 0 ? std::string("No explicit move / 0") : names[value] + " / " + std::to_string(value);
                            if (!filter.PassFilter(choice.c_str())) continue;
                            if (ImGui::Selectable(choice.c_str(), draft_[i] == value)) { draft_[i] = value; ImGui::CloseCurrentPopup(); }
                        }
                        ImGui::EndChild(); ImGui::EndCombo();
                    }
                } else if (field.key == "egg" && draft_[i] <= 1) {
                    bool egg = draft_[i] != 0;
                    if (ImGui::Checkbox("Give as an egg", &egg)) draft_[i] = egg ? 1 : 0;
                } else {
                    int value = int(draft_[i]);
                    if (ImGui::InputInt(field.label.c_str(), &value)) draft_[i] = unsigned(std::max(0, value));
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            bool pending = this->pending();
            ImGui::BeginDisabled(!pending);
            if (TutorialWidgets::Button("pokemon_records", "Apply record")) {
                try {
                    auto next = *document_; next.set(draft_); next.validate_catalogs(source_);
                    *document_ = std::move(next); error_.clear(); changed = true;
                } catch (const std::exception &error) { error_ = error.what(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel changes")) { refresh(); error_.clear(); }
            ImGui::EndDisabled();
            if (pending) ImGui::TextDisabled("Apply these fields before saving the project.");
            ImGui::TextWrapped("Save Project stores applied edits. Stage Project and Build current changes remain separate actions.");
            if (document_->kind() == PokemonRecordKind::Trainer)
                ImGui::TextWrapped("Team size and battle rules retain their existing values. Zero move slots have no explicit move override.");
            if (document_->kind() == PokemonRecordKind::Trade)
                ImGui::TextWrapped("Nickname, trainer identity and additional trade filters retain their existing values.");
        }
        if (!error_.empty()) ImGui::TextWrapped("%s", error_.c_str());
    }
    ImGui::End();
    return changed;
}
}
