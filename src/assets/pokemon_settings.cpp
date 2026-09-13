#include "assets/pokemon_settings.h"
#include "core/digest.h"
#include "formats/compression.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
namespace studio {
namespace {
struct ShinyRecord {
    std::size_t slot, offset;
    std::string name;
};
std::vector<ShinyRecord> shiny_records(View b) {
    require(!b.empty(), "Missing shiny settings");
    std::vector<ShinyRecord> out;
    std::size_t p = 1;
    for (unsigned i = 0; i < b[0]; ++i) {
        std::string name;
        while (slice(b, p, 1)[0]) {
            require(name.size() < 64, "Shiny material name is too long");
            name += char(b[p++]);
        }
        ++p;
        require(!name.empty(), "Empty shiny material name");
        auto slot = p++;
        p = aligned(p, 4);
        slice(b, p, 16);
        out.push_back({slot, p, name});
        p += 16;
    }
    return out;
}
}
PokemonSettings decode_pokemon_settings(View member) {
    auto pack = Container::parse(member, "PC");
    require(pack.files.size() >= 2 && pack.files[0].size() >= 96,
            "Pokemon settings container is missing its records");
    auto &b = pack.files[0];
    PokemonSettings out;
    for (unsigned i = 0; i < 3; ++i)
        out.heights[i] = int(std::int32_t(u32(b, i * 4)));
    out.size = u32(b, 12);
    for (unsigned i = 0; i < 6; ++i)
        out.bounds[i] = f32(b, 16 + i * 4);
    for (unsigned i = 0; i < 5; ++i)
        out.camera[i] = f32(b, 40 + i * 4);
    for (unsigned i = 0; i < 2; ++i)
        out.idle[i] = std::int16_t(u16(b, 60 + i * 2));
    for (unsigned i = 0; i < 8; ++i) {
        out.eyes[i] = std::int16_t(u16(b, 64 + i * 2));
        out.mouths[i] = std::int16_t(u16(b, 80 + i * 2));
    }
    for (auto &record : shiny_records(pack.files[1])) {
        PokemonShinyColor color;
        color.material = record.name;
        color.slot = pack.files[1][record.slot];
        for (unsigned i = 0; i < 4; ++i)
            color.offset[i] = f32(pack.files[1], record.offset + i * 4);
        out.shiny.push_back(color);
    }
    validate_pokemon_settings(out);
    return out;
}
void validate_pokemon_settings(const PokemonSettings &s) {
    for (int h : s.heights)
        require(h >= 0, "Heights must be nonnegative; zero uses the runtime fallback");
    require(s.size < 3, "Choose a small, medium or large size category");
    for (auto v : s.bounds)
        require(std::isfinite(v), "Bounds must be finite");
    for (unsigned i = 0; i < 3; ++i)
        require(s.bounds[i] <= s.bounds[i + 3], "Minimum bounds must not exceed maximum bounds");
    for (auto v : s.camera)
        require(std::isfinite(v), "Camera values must be finite");
    require(s.camera[0] > 0, "Framing size must be positive");
    for (int v : s.idle)
        require(v >= 0 && v <= 1000, "Idle rates must be between 0 and 1000");
    require(s.idle[0] + s.idle[1] <= 1000, "Combined idle rates must not exceed 1000");
    for (auto *flags : {&s.eyes, &s.mouths})
        for (int flag : *flags)
            require(flag == 0 || flag == 1, "Expression availability must be on or off");
    for (auto &color : s.shiny) {
        require(color.slot < 6, "Shiny color slot must be between 0 and 5");
        for (float v : color.offset)
            require(std::isfinite(v), "Shiny color offsets must be finite");
    }
}
Bytes replace_pokemon_settings(View member, const PokemonSettings &s) {
    validate_pokemon_settings(s);
    auto before = decode_pokemon_settings(member);
    if (before == s)
        return {member.begin(), member.end()};
    require(before.shiny.size() == s.shiny.size(), "Keep the existing shiny material entries");
    auto pack = Container::parse(member, "PC");
    Bytes out(member.begin(), member.end());
    auto a = u32(member, 4), b = u32(member, 8);
    for (unsigned i = 0; i < 3; ++i)
        if (s.heights[i] != before.heights[i])
            put32(out, a + i * 4, unsigned(s.heights[i]));
    if (s.size != before.size)
        put32(out, a + 12, s.size);
    for (unsigned i = 0; i < 6; ++i)
        if (s.bounds[i] != before.bounds[i])
            put_float(out, a + 16 + i * 4, s.bounds[i]);
    for (unsigned i = 0; i < 5; ++i)
        if (s.camera[i] != before.camera[i])
            put_float(out, a + 40 + i * 4, s.camera[i]);
    for (unsigned i = 0; i < 2; ++i)
        if (s.idle[i] != before.idle[i])
            put16(out, a + 60 + i * 2, std::uint16_t(s.idle[i]));
    for (unsigned i = 0; i < 8; ++i) {
        if (s.eyes[i] != before.eyes[i])
            put16(out, a + 64 + i * 2, std::uint16_t(s.eyes[i]));
        if (s.mouths[i] != before.mouths[i])
            put16(out, a + 80 + i * 2, std::uint16_t(s.mouths[i]));
    }
    auto records = shiny_records(pack.files[1]);
    for (unsigned n = 0; n < records.size(); ++n) {
        auto &color = s.shiny[n];
        auto &old = before.shiny[n];
        require(color.material == old.material, "Keep the source shiny material binding");
        if (color.slot != old.slot)
            out[b + records[n].slot] = std::uint8_t(color.slot);
        for (unsigned i = 0; i < 4; ++i)
            if (color.offset[i] != old.offset[i])
                put_float(out, b + records[n].offset + i * 4, color.offset[i]);
    }
    return out;
}
float pokemon_adjusted_scale(const PokemonSettings &s, bool field) {
    float base = s.heights[0] ? s.heights[0] * .01f : 1.f;
    int height = s.heights[field ? 2 : 1];
    return height ? height * .01f / base : std::pow(base, -.43f);
}
PokemonSettingsDocument::PokemonSettingsDocument(PokemonEntry entry, Bytes original)
    : pokemon(std::move(entry)), original_(std::move(original)) {
    initial_ = current_ = saved_ = decode_pokemon_settings(original_);
    history_.push_back(initial_);
}
void PokemonSettingsDocument::preview(const PokemonSettings &s) {
    replace_pokemon_settings(original_, s);
    current_ = s;
}
void PokemonSettingsDocument::commit() {
    if (current_ == history_[cursor_])
        return;
    history_.resize(cursor_ + 1);
    history_.push_back(current_);
    ++cursor_;
    if (history_.size() > 257) {
        history_.erase(history_.begin());
        --cursor_;
    }
}
void PokemonSettingsDocument::undo() {
    commit();
    if (cursor_)
        current_ = history_[--cursor_];
}
void PokemonSettingsDocument::redo() {
    if (can_redo())
        current_ = history_[++cursor_];
}
void PokemonSettingsDocument::reset() {
    current_ = initial_;
    commit();
}
void PokemonSettingsDocument::discard() {
    current_ = saved_;
    commit();
}
std::string PokemonSettingsDocument::serialize() const {
    auto compiled = compile();
    std::ostringstream out;
    out << "USUMSTUDIO_POKEMON_SETTINGS 1\n"
        << pokemon.species << ' ' << pokemon.form << ' ' << pokemon.female << ' '
        << pokemon.data_index << ' ' << member() << '\n'
        << sha256(original_) << '\n';
    out << std::hex << std::setfill('0');
    for (auto b : compiled)
        out << std::setw(2) << unsigned(b);
    out << '\n';
    return out.str();
}
void PokemonSettingsDocument::restore(const std::string &text) {
    std::istringstream in(text);
    std::string magic, hash, hex;
    unsigned version, species, form, female, index;
    std::size_t member_id;
    require(bool(in >> magic >> version) && magic == "USUMSTUDIO_POKEMON_SETTINGS" && version == 1,
            "Unsupported Pokemon settings document");
    require(bool(in >> species >> form >> female >> index >> member_id) &&
                species == pokemon.species && form == pokemon.form &&
                female == unsigned(pokemon.female) && index == pokemon.data_index &&
                member_id == member(),
            "Open the originating Pokemon form first");
    require(bool(in >> hash >> hex) && hash == sha256(original_),
            "Pokemon settings source changed; open the original archive");
    in >> std::ws;
    require(in.eof() && hex.size() == original_.size() * 2, "Invalid settings document length");
    Bytes bytes;
    auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        throw std::runtime_error("Invalid settings data");
    };
    for (std::size_t i = 0; i < hex.size(); i += 2)
        bytes.push_back(std::uint8_t(digit(hex[i]) * 16 + digit(hex[i + 1])));
    auto next = decode_pokemon_settings(bytes);
    require(replace_pokemon_settings(original_, next) == bytes,
            "Settings document changes unsupported data");
    preview(next);
    commit();
    mark_saved();
}
void PokemonSettingsDocument::export_archive(const std::filesystem::path &path,
                                             const std::filesystem::path &output) const {
    require(changed(), "There are no Pokemon settings changes to export");
    require(std::filesystem::weakly_canonical(path) != std::filesystem::weakly_canonical(output),
            "Choose a separate output archive");
    Archive source(path);
    auto catalog = decode_pokemon_catalog(source.decoded(0), source.size(), {});
    auto entry = std::find_if(catalog.begin(), catalog.end(), [&](auto &e) {
        return e.species == pokemon.species && e.form == pokemon.form && e.female == pokemon.female;
    });
    require(entry != catalog.end() && entry->data_index == pokemon.data_index,
            "Pokemon source catalog changed");
    require(source.decoded(member()) == original_,
            "Pokemon settings changed since loading; reload before exporting");
    auto bytes = compile();
    auto raw = source.raw(member());
    if (!raw.empty() && (raw[0] == 0x11 || raw[0] == 0x10))
        bytes = compress(bytes);
    source.export_to(output, {{member(), std::move(bytes)}});
}
}
