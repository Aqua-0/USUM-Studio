#include "field/pokemon_record.h"
#include "field/area.h"
#include "field/map_catalog.h"
#include "core/digest.h"
#include <sstream>
#include <algorithm>
namespace studio {
namespace {
struct Location { const char *archive; unsigned member, stride, offset; };
Location location(PokemonRecordKind kind, unsigned row) {
    if (kind == PokemonRecordKind::Trainer) return {TargetProfile::trainer_teams_archive, row, 0, 0};
    require(kind == PokemonRecordKind::Gift || kind == PokemonRecordKind::Trade, "Invalid Pokemon record kind");
    unsigned stride = kind == PokemonRecordKind::Gift ? TargetProfile::pokemon_gift_record_size : TargetProfile::pokemon_trade_record_size;
    require(row <= 65535, "Pokemon record index is out of range");
    return {TargetProfile::script_events_archive,
            kind == PokemonRecordKind::Gift ? TargetProfile::pokemon_gifts_member : TargetProfile::pokemon_trades_member,
            stride, row * stride};
}
}
const char *pokemon_record_name(PokemonRecordKind kind) {
    return kind == PokemonRecordKind::Gift ? "Pokemon gift" : kind == PokemonRecordKind::Trade ? "Pokemon trade" : "Trainer team";
}
PokemonRecordDocument PokemonRecordDocument::load(const std::filesystem::path &source, PokemonRecordKind kind, unsigned row) {
    auto where = location(kind, row);
    auto bytes = Archive(source / where.archive).raw(where.member);
    if (where.stride) {
        require(bytes.size() % where.stride == 0, "Invalid Pokemon record table size");
        auto record = slice(bytes, where.offset, where.stride);
        bytes = Bytes(record.begin(), record.end());
    } else {
        auto header = Archive(source / TargetProfile::trainer_records_archive).raw(row);
        require(row > 0 && header.size() == TargetProfile::trainer_record_size && header[3] * TargetProfile::trainer_pokemon_size == bytes.size(),
                "Trainer team does not match its trainer record");
    }
    return PokemonRecordDocument(kind, row, std::move(bytes));
}
PokemonRecordDocument::PokemonRecordDocument(PokemonRecordKind kind, unsigned row, Bytes original)
    : kind_(kind), row_(row), original_(std::move(original)), current_(original_), saved_(original_) {
    auto where = location(kind, row);
    require(where.stride ? original_.size() == where.stride :
                row > 0 && original_.size() >= TargetProfile::trainer_pokemon_size &&
                original_.size() <= 6 * TargetProfile::trainer_pokemon_size &&
                original_.size() % TargetProfile::trainer_pokemon_size == 0, "Unsupported Pokemon record size");
    using Names = PokemonRecordField::Names;
    auto add = [&](std::string key, std::string label, unsigned at, unsigned size, unsigned minimum, unsigned maximum, Names names = Names::None) {
        fields_.push_back({std::move(key), std::move(label), at, size, minimum, maximum, names});
    };
    if (kind == PokemonRecordKind::Trainer) {
        for (unsigned slot = 0; slot < original_.size() / TargetProfile::trainer_pokemon_size; ++slot) {
            unsigned at = slot * TargetProfile::trainer_pokemon_size;
            auto key = std::to_string(slot) + "/";
            auto label = "Pokemon " + std::to_string(slot + 1) + " / ";
            add(key + "species", label + "Species", at + 16, 2, 1, 65535, Names::Species);
            add(key + "form", label + "Form", at + 18, 1, 0, 255);
            add(key + "level", label + "Level", at + 14, 1, 1, 100);
            add(key + "item", label + "Held item", at + 20, 2, 0, 65535, Names::Item);
            for (unsigned move = 0; move < 4; ++move)
                add(key + "move" + std::to_string(move), label + "Move " + std::to_string(move + 1), at + 24 + move * 2, 2, 0, 65535, Names::Move);
        }
    } else {
        const bool gift = kind == PokemonRecordKind::Gift;
        add("species", gift ? "Species" : "Offered species", 0, 2, 1, 65535, Names::Species);
        add("form", "Form", gift ? 2 : 4, 1, 0, 255);
        add("level", "Level", gift ? 3 : 5, 1, 1, 100);
        add("item", "Held item", gift ? 8 : 20, 2, 0, 65535, Names::Item);
        if (gift) {
            add("egg", "Egg (0 = no, 1 = yes)", 10, 1, 0, 1);
            add("move", "Special move", 12, 2, 0, 65535, Names::Move);
        } else add("requested", "Requested species", 44, 2, 0, 65535, Names::Species);
    }
    history_.push_back(current_);
}
unsigned PokemonRecordDocument::value(std::size_t index) const {
    const auto &field = fields_.at(index);
    return field.size == 2 ? u16(current_, field.offset) : current_.at(field.offset);
}
void PokemonRecordDocument::set(const std::vector<unsigned> &values) {
    require(values.size() == fields_.size(), "Pokemon record fields changed");
    auto next = current_;
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        if (values[i] == value(i)) continue;
        const auto &f = fields_[i];
        require(values[i] >= f.minimum && values[i] <= f.maximum, f.label + " is out of range");
        if (f.size == 2) put16(next, f.offset, std::uint16_t(values[i]));
        else next[f.offset] = std::uint8_t(values[i]);
    }
    if (next == current_) return;
    history_.resize(cursor_ + 1);
    history_.push_back(next); ++cursor_; current_ = std::move(next);
}
void PokemonRecordDocument::undo() { if (can_undo()) current_ = history_[--cursor_]; }
void PokemonRecordDocument::redo() { if (can_redo()) current_ = history_[++cursor_]; }
std::string PokemonRecordDocument::serialize() const {
    std::ostringstream out;
    out << "USUMSTUDIO_POKEMON_RECORD 1 " << unsigned(kind_) << ' ' << row_ << ' ' << sha256(original_) << '\n';
    for (std::size_t i = 0; i < fields_.size(); ++i) out << fields_[i].key << ' ' << value(i) << '\n';
    out << "end\n";
    return out.str();
}
void PokemonRecordDocument::restore(const std::string &patch) {
    std::istringstream in(patch);
    std::string tag, hash; unsigned version, kind, row;
    require(bool(in >> tag >> version >> kind >> row >> hash) && tag == "USUMSTUDIO_POKEMON_RECORD" && version == 1 &&
            kind == unsigned(kind_) && row == row_ && hash == sha256(original_), "Pokemon record source changed");
    std::vector<unsigned> values;
    for (const auto &field : fields_) {
        unsigned value;
        require(bool(in >> tag >> value) && tag == field.key, "Invalid Pokemon record field");
        values.push_back(value);
    }
    require(bool(in >> tag) && tag == "end" && !(in >> tag), "Unexpected Pokemon record data");
    auto next = *this;
    next.current_ = original_;
    next.set(values);
    current_ = saved_ = std::move(next.current_);
    history_ = {current_}; cursor_ = 0;
}
void PokemonRecordDocument::validate_catalogs(const std::filesystem::path &source) const {
    for (std::size_t i = 0; i + 1 < fields_.size(); ++i) {
        const auto &species = fields_[i];
        const auto &form = fields_[i + 1];
        if (!species.key.ends_with("species") || !form.key.ends_with("form")) continue;
        if (value(i) == u16(original_, species.offset) && value(i + 1) == original_[form.offset]) continue;
        Archive personal(source / GameProfile::personal_archive);
        auto record = personal.raw(value(i));
        require(record.size() == TargetProfile::personal_record_size,
                "Species is absent from the project's personal data");
        require(value(i + 1) < std::max(1u, unsigned(record[TargetProfile::personal_form_count_offset])),
                "Form is absent from this species; choose an existing form");
    }
    Archive archive(source / TargetProfile::location_text_archive);
    std::map<PokemonRecordField::Names, unsigned> counts;
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        const auto &f = fields_[i];
        if (f.names == PokemonRecordField::Names::None) continue;
        unsigned before = f.size == 2 ? u16(original_, f.offset) : original_[f.offset];
        unsigned after = value(i);
        if (before == after || after == 0 || (f.names == PokemonRecordField::Names::Item && after == 65535 && kind_ == PokemonRecordKind::Trade)) continue;
        if (!counts.contains(f.names)) {
            unsigned member = f.names == PokemonRecordField::Names::Species ? TargetProfile::pokemon_names_member :
                              f.names == PokemonRecordField::Names::Item ? TargetProfile::item_names_member : TargetProfile::move_names_member;
            counts[f.names] = unsigned(decode_location_text(archive.decoded(member)).size());
        }
        require(after < counts[f.names], f.label + " is absent from the project catalog");
    }
}
void PokemonRecordDocument::export_to(const std::filesystem::path &source, const std::filesystem::path &output) const {
    validate_catalogs(source);
    auto where = location(kind_, row_);
    Archive archive(source / where.archive);
    auto bytes = archive.raw(where.member);
    auto record = slice(bytes, where.offset, original_.size());
    require(Bytes(record.begin(), record.end()) == original_,
            "Pokemon record source changed before export");
    std::copy(current_.begin(), current_.end(), bytes.begin() + where.offset);
    std::filesystem::create_directories((output / where.archive).parent_path());
    archive.export_to(output / where.archive, {{where.member, bytes}});
}
}
