#pragma once
#include "core/binary.h"
#include <map>
namespace studio {
enum class PokemonRecordKind { Gift, Trade, Trainer };
const char *pokemon_record_name(PokemonRecordKind kind);
struct PokemonRecordField {
    std::string key, label;
    unsigned offset = 0, size = 1, minimum = 0, maximum = 255;
    enum class Names { None, Species, Item, Move } names = Names::None;
};
class PokemonRecordDocument {
  public:
    PokemonRecordDocument(PokemonRecordKind kind, unsigned row, Bytes original);
    static PokemonRecordDocument load(const std::filesystem::path &, PokemonRecordKind, unsigned);
    const std::vector<PokemonRecordField> &fields() const { return fields_; }
    unsigned value(std::size_t field) const;
    void set(const std::vector<unsigned> &values);
    void undo();
    void redo();
    bool can_undo() const { return cursor_ > 0; }
    bool can_redo() const { return cursor_ + 1 < history_.size(); }
    bool dirty() const { return current_ != saved_; }
    void mark_saved() { saved_ = current_; }
    const Bytes &bytes() const { return current_; }
    PokemonRecordKind kind() const { return kind_; }
    unsigned row() const { return row_; }
    std::string serialize() const;
    void restore(const std::string &);
    void validate_catalogs(const std::filesystem::path &) const;
    void export_to(const std::filesystem::path &source, const std::filesystem::path &output) const;
  private:
    PokemonRecordKind kind_;
    unsigned row_;
    Bytes original_, current_, saved_;
    std::vector<PokemonRecordField> fields_;
    std::vector<Bytes> history_;
    std::size_t cursor_ = 0;
};
}
