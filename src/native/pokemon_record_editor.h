#pragma once
#include "field/pokemon_record.h"
#include "native/project_binding.h"
namespace studio {
PokemonRecordDocument project_pokemon_record(const std::filesystem::path &, PokemonRecordKind, unsigned);
class PokemonRecordEditor {
  public:
    void open(const std::filesystem::path &, PokemonRecordKind, unsigned);
    void reset();
    bool draw();
    bool editing(PokemonRecordKind kind, unsigned row) const {
        return document_ && document_->kind() == kind && document_->row() == row;
    }
    PokemonRecordDocument record(const std::filesystem::path &, PokemonRecordKind, unsigned) const;
  private:
    ProjectBinding binding_;
    std::unique_ptr<PokemonRecordDocument> document_;
    std::filesystem::path source_;
    std::vector<unsigned> draft_;
    std::map<PokemonRecordField::Names, std::vector<std::string>> names_;
    bool open_ = false;
    std::string error_;
    void refresh();
    bool pending() const;
};
}
