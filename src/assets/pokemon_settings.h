#pragma once
#include "assets/pokemon_catalog.h"
namespace studio {
struct PokemonShinyColor {
    std::string material;
    unsigned slot = 0;
    std::array<float, 4> offset{};
    bool operator==(const PokemonShinyColor &) const = default;
};
struct PokemonSettings {
    std::array<int, 3> heights{};
    unsigned size = 0;
    std::array<float, 6> bounds{};
    std::array<float, 5> camera{};
    std::array<int, 2> idle{};
    std::array<int, 8> eyes{}, mouths{};
    std::vector<PokemonShinyColor> shiny;
    bool operator==(const PokemonSettings &) const = default;
};
PokemonSettings decode_pokemon_settings(View member);
void validate_pokemon_settings(const PokemonSettings &settings);
Bytes replace_pokemon_settings(View member, const PokemonSettings &settings);
float pokemon_adjusted_scale(const PokemonSettings &settings, bool field = false);
class PokemonSettingsDocument {
  public:
    PokemonSettingsDocument(PokemonEntry entry, Bytes original);
    PokemonEntry pokemon;
    const PokemonSettings &settings() const {
        return current_;
    }
    const PokemonSettings &original_settings() const {
        return initial_;
    }
    std::size_t member() const {
        return 1 + pokemon.data_index * TargetProfile::pokemon_stride +
               TargetProfile::pokemon_settings_slot;
    }
    void preview(const PokemonSettings &settings);
    void commit();
    void undo();
    void redo();
    void reset();
    void discard();
    bool dirty() const {
        return current_ != saved_;
    }
    bool changed() const {
        return current_ != initial_;
    }
    bool can_undo() const {
        return cursor_ > 0 || current_ != history_[cursor_];
    }
    bool can_redo() const {
        return cursor_ + 1 < history_.size();
    }
    void mark_saved() {
        saved_ = current_;
    }
    Bytes compile() const {
        return replace_pokemon_settings(original_, current_);
    }
    std::string serialize() const;
    void restore(const std::string &text);
    void export_archive(const std::filesystem::path &source,
                        const std::filesystem::path &output) const;

  private:
    Bytes original_;
    PokemonSettings initial_, current_, saved_;
    std::vector<PokemonSettings> history_;
    std::size_t cursor_ = 0;
};
}
