#include "field/field_activity_document.h"
#include "field/area.h"
#include "field/overworld_document.h"
#include "core/digest.h"
#include "formats/compression.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
namespace studio {
namespace {
struct Layout {
    unsigned size, event;
    std::vector<unsigned> shapes;
};
Layout layout(unsigned category) {
    if (category == 11)
        return {68, 32, {}};
    if (category == 12)
        return {88, 40, {80, 84}};
    require(category == 14, "This field activity cannot be edited");
    return {152, 44, {112, 116, 132, 148}};
}
void available_event(View source, unsigned zone, unsigned event) {
    require(event > 0 && event <= 65535, "Choose a nonzero event ID up to 65535");
    OverworldDocument placements(0, Bytes(source.begin(), source.end()), {}, {});
    for (auto &entry : placements.entries())
        require(entry.zone != zone || entry.event != event,
                "Event ID is already used by a placement in this zone");
    for (auto &entry : inspect_field_systems(source))
        require(entry.local_zone != zone || !entry.region.overworld ||
                    entry.region.overworld->event != event,
                "Event ID is already used by a field activity in this zone");
}
Bytes moved_shape(View source, unsigned pointer, SpatialPoint delta) {
    unsigned count = u32(source, pointer);
    require(count <= 64, "Too many activity shapes");
    std::size_t at = pointer + 4 + count * 4;
    constexpr unsigned sizes[] = {20, 40, 28, 36};
    for (unsigned i = 0; i < count; ++i) {
        auto type = u32(source, pointer + 4 + i * 4);
        require(type < 4, "Unknown activity shape");
        slice(source, at, sizes[type]);
        at += sizes[type];
    }
    auto data = slice(source, pointer, at - pointer);
    Bytes result(data.begin(), data.end());
    at = 4 + count * 4;
    for (unsigned i = 0; i < count; ++i) {
        auto type = u32(result, 4 + i * 4);
        for (unsigned j = 0; j < (type == 3 ? 9u : type == 2 ? 6u : 3u); ++j)
            put_float(result, at + j * 4, f32(result, at + j * 4) + delta[j % 3]);
        at += sizes[type];
    }
    return result;
}
void validate(View bytes) {
    auto ed = Container::parse(bytes, "ED");
    for (auto category : {11u, 12u, 14u}) {
        if (ed.files.size() <= category || ed.files[category].empty())
            continue;
        auto l = layout(category);
        auto zones = Container::parse(ed.files[category]);
        for (auto &zone : zones.files) {
            if (zone.empty())
                continue;
            unsigned n = u32(zone, 0);
            require(n <= 4096, "Too many field activities");
            slice(zone, 0, 4 + std::size_t(n) * l.size);
            std::set<unsigned> events;
            for (unsigned i = 0; i < n; ++i) {
                auto at = 4 + i * l.size;
                require(events.insert(u32(zone, at + l.event)).second,
                        "Activity event ID is already used in this zone");
                for (unsigned k = 0; k < 3; ++k)
                    require(std::isfinite(f32(zone, at + 4 + k * 4)) &&
                                std::abs(f32(zone, at + 4 + k * 4)) < 1e7f,
                            "Activity position is outside the supported range");
                for (auto field : l.shapes)
                    if (auto p = u32(zone, at + field)) {
                        require(p >= 4 + n * l.size, "Activity shape overlaps records");
                        moved_shape(zone, p, {});
                    }
            }
        }
    }
}
}
const std::vector<FieldActivitySetting> &field_activity_settings(unsigned category) {
    static const std::vector<FieldActivitySetting> berry = {{"Event ID", 32},
                                                            {"Persistent pile ID", 36},
                                                            {"Encounter table", 40},
                                                            {"Encounter chance (%)", 44, 2},
                                                            {"Normal level", 46, 1},
                                                            {"Boss level", 47, 1},
                                                            {"Abundant pile chance (%)", 48, 2},
                                                            {"Normal minimum", 50, 1},
                                                            {"Normal maximum", 51, 1},
                                                            {"Abundant minimum", 52, 1},
                                                            {"Abundant maximum", 53, 1},
                                                            {"Berry item 1", 54, 2},
                                                            {"Berry item 2", 56, 2},
                                                            {"Berry item 3", 58, 2},
                                                            {"Berry item 4", 60, 2},
                                                            {"Berry item 5", 62, 2},
                                                            {"Berry item 6", 64, 2},
                                                            {"Rare berry", 66, 2}};
    static const std::vector<FieldActivitySetting> fishing = {{"Flag / work", 32},
                                                              {"Expected value", 36},
                                                              {"Event ID", 40},
                                                              {"Focus ID", 44},
                                                              {"Static resource", 48, 2},
                                                              {"Rare spot chance (%)", 50, 2},
                                                              {"Normal encounter table", 52},
                                                              {"Normal item table", 56},
                                                              {"Normal encounter chance (%)", 60},
                                                              {"Rare encounter table", 64},
                                                              {"Rare item table", 68},
                                                              {"Rare encounter chance (%)", 72}};
    static const std::vector<FieldActivitySetting> contact = {{"Game version mask", 32},
                                                              {"Flag / work", 36},
                                                              {"Expected value", 40},
                                                              {"Event ID", 44},
                                                              {"Movement code", 48},
                                                              {"Character resource", 52},
                                                              {"Script ID", 56},
                                                              {"Default motion", 60},
                                                              {"Motion count", 64},
                                                              {"Associated NPC event", 120},
                                                              {"Saved contact state", 124},
                                                              {"Contact movement", 128},
                                                              {"Follow ground", 144}};
    layout(category);
    return category == 11 ? berry : category == 12 ? fishing : contact;
}
FieldActivityDocument::FieldActivityDocument(Bytes source)
    : original_(std::move(source)), current_(original_), saved_(original_), history_{original_} {
}
Bytes FieldActivityDocument::record(unsigned category, unsigned zone, unsigned row) const {
    auto l = layout(category);
    auto ed = Container::parse(current_, "ED");
    auto zones = Container::parse(ed.files.at(category));
    auto &b = zones.files.at(zone);
    require(row < u32(b, 0), "Activity row is missing");
    auto r = slice(b, 4 + row * l.size, l.size);
    return {r.begin(), r.end()};
}
void FieldActivityDocument::commit(Bytes next) {
    validate(next);
    if (next == current_)
        return;
    current_ = std::move(next);
    history_.resize(cursor_ + 1);
    history_.push_back(current_);
    ++cursor_;
}
void FieldActivityDocument::update(unsigned category, unsigned zone, unsigned row, const Bytes &r) {
    auto old = record(category, zone, row);
    auto l = layout(category);
    require(r.size() == old.size(), "Activity record size changed");
    require(u32(r, l.event) == u32(old, l.event), "Existing activity event IDs are preserved");
    for (auto &field : field_activity_settings(category)) {
        auto value = [&](const Bytes &b) {
            return field.width == 1   ? unsigned(b[field.offset])
                   : field.width == 2 ? unsigned(u16(b, field.offset))
                                      : u32(b, field.offset);
        };
        if (value(r) == value(old))
            continue;
        if (field.name.find("(%)") != std::string::npos)
            require(value(r) <= 100, "Activity chance must be from 0 to 100 percent");
        if (field.name == "Normal level" || field.name == "Boss level")
            require(value(r) > 0 && value(r) <= 100, "Encounter level must be from 1 to 100");
    }
    if (category == 11)
        require(r[50] <= r[51] && r[52] <= r[53], "Berry minimum cannot exceed maximum");
    auto allowed = old;
    for (unsigned k = 0; k < 3; ++k)
        put_float(allowed, 4 + k * 4, f32(r, 4 + k * 4));
    for (auto &f : field_activity_settings(category))
        std::copy_n(r.begin() + f.offset, f.width, allowed.begin() + f.offset);
    require(allowed == r, "Unsupported activity field changed");
    auto ed = Container::parse(current_, "ED");
    auto zones = Container::parse(ed.files.at(category));
    auto &b = zones.files.at(zone);
    auto next = r;
    SpatialPoint delta{};
    for (unsigned k = 0; k < 3; ++k)
        delta[k] = f32(r, 4 + k * 4) - f32(old, 4 + k * 4);
    if (delta != SpatialPoint{})
        for (auto f : l.shapes)
            if (auto p = u32(old, f)) {
                auto shape = moved_shape(b, p, delta);
                put32(next, f, narrow(b.size()));
                append(b, shape);
            }
    std::copy(next.begin(), next.end(), b.begin() + 4 + row * l.size);
    ed.files[category] = zones.write();
    commit(ed.write());
}
unsigned FieldActivityDocument::duplicate(unsigned category, unsigned zone, unsigned row,
                                          unsigned event, SpatialPoint position) {
    auto r = record(category, zone, row);
    auto l = layout(category);
    available_event(current_, zone, event);
    require(category != 14 || u32(r, 104) == 0,
            "Create contact Pokemon from an owning record, not a shared alias");
    auto ed = Container::parse(current_, "ED");
    auto zones = Container::parse(ed.files.at(category));
    auto original = zones.files.at(zone);
    unsigned count = u32(original, 0);
    require(count < 4096, "Too many activities in this zone");
    unsigned tail = 4 + count * l.size;
    slice(original, 0, tail);
    Bytes next(original.begin(), original.begin() + tail);
    next.resize(tail + l.size);
    append(next, slice(original, tail, original.size() - tail));
    put32(next, 0, count + 1);
    for (unsigned i = 0; i < count; ++i)
        for (auto f : l.shapes)
            if (auto p = u32(next, 4 + i * l.size + f))
                put32(next, 4 + i * l.size + f, p + l.size);
    SpatialPoint delta{};
    for (unsigned k = 0; k < 3; ++k) {
        delta[k] = position[k] - f32(r, 4 + k * 4);
        put_float(r, 4 + k * 4, position[k]);
    }
    put32(r, l.event, event);
    for (auto f : l.shapes)
        if (auto p = u32(r, f)) {
            auto shape = moved_shape(original, p, delta);
            put32(r, f, narrow(next.size()));
            append(next, shape);
        }
    std::copy(r.begin(), r.end(), next.begin() + tail);
    zones.files[zone] = std::move(next);
    ed.files[category] = zones.write();
    commit(ed.write());
    return count;
}
void FieldActivityDocument::undo() {
    if (can_undo())
        current_ = history_[--cursor_];
}
void FieldActivityDocument::redo() {
    if (can_redo())
        current_ = history_[++cursor_];
}
std::string FieldActivityDocument::serialize() const {
    auto original = Container::parse(original_, "ED"), current = Container::parse(current_, "ED");
    std::ostringstream out;
    out << "USUMSTUDIO_FIELD_ACTIVITIES 1\n";
    for (auto category : {11u, 12u, 14u})
        if (category < current.files.size() &&
            current.files[category] != original.files[category]) {
            auto &b = current.files[category];
            out << "category " << category << ' ' << sha256(original.files[category]) << ' '
                << b.size() << '\n';
            for (auto v : b)
                out << std::hex << std::setw(2) << std::setfill('0') << unsigned(v);
            out << std::dec << '\n';
        }
    out << "end\n";
    return out.str();
}
void FieldActivityDocument::restore(const std::string &patch) {
    std::istringstream in(patch);
    std::string word;
    unsigned version;
    require(bool(in >> word >> version) && word == "USUMSTUDIO_FIELD_ACTIVITIES" && version == 1,
            "Unsupported activity document");
    auto ed = Container::parse(original_, "ED");
    std::set<unsigned> seen;
    bool end = false;
    while (in >> word) {
        if (word == "end") {
            end = true;
            break;
        }
        unsigned category;
        std::size_t size;
        std::string hash, hex;
        require(word == "category" && bool(in >> category >> hash >> size >> hex) &&
                    size <= 16 * 1024 * 1024 && hex.size() == size * 2,
                "Invalid activity data");
        layout(category);
        require(seen.insert(category).second && category < ed.files.size() &&
                    sha256(ed.files[category]) == hash,
                "Activity source changed");
        Bytes b;
        b.reserve(size);
        auto digit = [](char c) -> unsigned {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            throw std::runtime_error("Invalid activity encoding");
        };
        for (std::size_t i = 0; i < hex.size(); i += 2)
            b.push_back(std::uint8_t(digit(hex[i]) * 16 + digit(hex[i + 1])));
        ed.files[category] = std::move(b);
    }
    in >> std::ws;
    require(end && in.eof(), "Incomplete activity document");
    auto next = ed.write();
    validate(next);
    current_ = std::move(next);
    history_ = {current_};
    cursor_ = 0;
    mark_saved();
}
Bytes FieldActivityDocument::compile(View source) const {
    auto base = Container::parse(original_, "ED"), edited = Container::parse(current_, "ED"),
         out = Container::parse(source, "ED");
    for (auto c : {11u, 12u, 14u})
        if (c < edited.files.size() && edited.files[c] != base.files[c]) {
            require(c < out.files.size() && out.files[c] == base.files[c],
                    "Activity source changed during staging");
            out.files[c] = edited.files[c];
        }
    return current_ == original_ ? Bytes(source.begin(), source.end()) : out.write();
}
void FieldActivityDocument::export_to(const std::filesystem::path &dump,
                                      const std::filesystem::path &output, unsigned area) const {
    auto relative = GameProfile::field_archive(dump);
    auto target = output / relative;
    require(std::filesystem::absolute(target).lexically_normal() !=
                std::filesystem::absolute(dump / relative).lexically_normal(),
            "Choose a separate staging output");
    Archive archive(std::filesystem::exists(target) ? target : dump / relative);
    auto member = area * TargetProfile::area_stride + TargetProfile::placement_slot;
    auto raw = archive.raw(member);
    auto result = compile(archive.decoded(member));
    auto encoded = !raw.empty() && raw[0] == 0x11 ? compress(result) : result;
    require(decompress(encoded) == result, "Activity compression readback failed");
    std::filesystem::create_directories(target.parent_path());
    archive.export_to(target, {{member, std::move(encoded)}}, std::filesystem::exists(target));
}
}
