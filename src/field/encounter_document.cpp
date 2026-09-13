#include "field/encounter_document.h"
#include "field/area.h"
#include "field/collision_surfaces.h"
#include "field/map_catalog.h"
#include "formats/compression.h"
#include "core/digest.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <iomanip>
namespace studio {
namespace {
constexpr unsigned record_size = 28, period_size = 356;
std::size_t shape_end(View b) {
    auto count = u32(b, 0);
    require(count <= 64, "Too many encounter shapes");
    slice(b, 4, count * 4);
    std::size_t end = 4 + count * 4;
    constexpr unsigned sizes[] = {20, 40, 28, 36};
    for (unsigned i = 0; i < count; ++i) {
        auto type = u32(b, 4 + i * 4);
        require(type < 4, "Unknown encounter shape type");
        slice(b, end, sizes[type]);
        end += sizes[type];
    }
    return end;
}
std::size_t shape_offset(View b, unsigned index) {
    require(index < u32(b, 0), "Select an encounter shape");
    constexpr unsigned sizes[] = {20, 40, 28, 36};
    std::size_t p = 4 + u32(b, 0) * 4;
    for (unsigned i = 0; i < index; ++i)
        p += sizes[u32(b, 4 + i * 4)];
    return p;
}
void finite(float v) {
    require(std::isfinite(v) && std::abs(v) < 1e7f,
            "Encounter coordinates must be finite and within world bounds");
}
Bytes shape_bytes(const EncounterShape &s) {
    require(s.type < 2, "Only box and cylinder shapes can be edited");
    for (auto v : s.position)
        finite(v);
    for (auto v : s.size) {
        finite(v);
        require(v > 0, "Encounter dimensions must be positive");
    }
    Bytes b(s.type ? 40 : 20);
    for (unsigned k = 0; k < 3; ++k)
        put_float(b, k * 4, s.position[k]);
    if (!s.type) {
        put_float(b, 12, s.size[0] * .5f);
        put_float(b, 16, s.size[1]);
    } else {
        float norm = 0;
        for (auto v : s.rotation) {
            finite(v);
            norm += v * v;
        }
        require(std::abs(norm - 1) < .01f, "Encounter box orientation must be normalized");
        for (unsigned k = 0; k < 4; ++k)
            put_float(b, 12 + k * 4, s.rotation[k]);
        put_float(b, 28, s.size[0] * .5f);
        put_float(b, 32, s.size[1]);
        put_float(b, 36, s.size[2] * .5f);
    }
    return b;
}
bool editable_table_byte(std::size_t at) {
    if (at < 4)
        return false;
    at = (at - 4) % period_size;
    return at < 12 || (at < 52 && (at - 12) % 4 < 2);
}
std::string hex(View b) {
    static const char *digits = "0123456789abcdef";
    std::string s;
    for (auto v : b) {
        s += digits[v >> 4];
        s += digits[v & 15];
    }
    return s;
}
Bytes unhex(const std::string &s) {
    require(s.size() % 2 == 0 && s.size() <= 65536, "Invalid encounter shape payload");
    Bytes b;
    auto digit = [](char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        throw std::runtime_error("Invalid encounter hex digit");
    };
    for (std::size_t i = 0; i < s.size(); i += 2)
        b.push_back(std::uint8_t(digit(s[i]) * 16 + digit(s[i + 1])));
    return b;
}
EncounterPeriod read_period(View b, bool night) {
    auto p = 4 + unsigned(night) * period_size;
    slice(b, p, period_size);
    EncounterPeriod v;
    v.minimum = b[p];
    v.maximum = b[p + 1];
    for (unsigned i = 0; i < 10; ++i) {
        auto packed = u16(b, p + 12 + i * 4);
        v.slots[i] = {unsigned(packed & 2047), unsigned(packed >> 11), b[p + 2 + i]};
    }
    return v;
}
void validate_period(const EncounterPeriod &v, unsigned species_count) {
    require(v.minimum >= 1 && v.maximum <= 100 && v.minimum <= v.maximum,
            "Levels must be ordered from 1 to 100");
    unsigned total = 0;
    for (auto &slot : v.slots) {
        require(slot.weight <= 100, "Slot weight must be from 0 to 100");
        total += slot.weight;
        require(slot.species < species_count && slot.species <= 2047 &&
                    (!slot.weight || slot.species),
                "Choose a valid species for every weighted slot");
        require(slot.form <= 31, "Encounter form must fit the native range 0 to 31");
    }
    require(total == 100, "Encounter slot weights must total 100");
}
}
EncounterDocument::EncounterDocument(unsigned area, Bytes placements, Bytes tables)
    : area_(area), placement_source_(std::move(placements)), table_source_(std::move(tables)) {
    hash_ = sha256(placement_source_) + sha256(table_source_);
    placements_ = Container::parse(placement_source_, "ED");
    require(placements_.files.size() > TargetProfile::encounter_placement_pack,
            "Area has no encounter category");
    auto &pack = placements_.files[TargetProfile::encounter_placement_pack];
    if (!pack.empty())
        zones_ = Container::parse(pack);
    else {
        zones_.tag = {'E', 'A'};
        for (auto &category : placements_.files)
            if (!category.empty())
                zones_.files.resize(
                    std::max(zones_.files.size(), Container::parse(category).files.size()));
    }
    tables_ = Container::parse(table_source_);
    for (unsigned i = 0; i < tables_.files.size(); ++i)
        original_.tables.push_back({i, tables_.files[i]});
    for (unsigned z = 0; z < zones_.files.size(); ++z) {
        auto &b = zones_.files[z];
        if (b.empty())
            continue;
        auto n = u32(b, 0);
        require(n <= 4096, "Excessive encounter regions");
        slice(b, 4, n * record_size);
        for (unsigned i = 0; i < n; ++i) {
            auto p = 4 + i * record_size;
            require(u32(b, p) == TargetProfile::encounter_record_type,
                    "Unexpected encounter record type");
            EncounterRegion r;
            r.source = int(original_.regions.size());
            r.zone = z;
            r.table = u32(b, p + 4);
            for (unsigned k = 0; k < 4; ++k)
                r.attributes[k] = u32(b, p + 8 + k * 4);
            auto off = u32(b, p + 24);
            require(off >= 4 + n * record_size, "Encounter shape overlaps its record list");
            auto view = slice(b, off, b.size() - off);
            view = slice(view, 0, shape_end(view));
            r.shapes.assign(view.begin(), view.end());
            original_.regions.push_back(std::move(r));
        }
    }
    state_ = saved_ = original_;
    validate(state_);
    history_ = {state_};
}
bool EncounterDocument::has_table(unsigned t) const {
    return t < state_.tables.size() && state_.tables[t].bytes.size() >= 716;
}
unsigned EncounterDocument::table_users(unsigned t) const {
    return unsigned(std::count_if(state_.regions.begin(), state_.regions.end(), [&](auto &r) {
        return r.table == t;
    }));
}
EncounterPeriod EncounterDocument::period(unsigned t, bool night) const {
    require(has_table(t), "Choose a populated encounter table");
    return read_period(state_.tables[t].bytes, night);
}
unsigned EncounterDocument::shape_count(unsigned r) const {
    return u32(state_.regions.at(r).shapes, 0);
}
EncounterShape EncounterDocument::shape(unsigned r, unsigned index) const {
    auto &b = state_.regions.at(r).shapes;
    auto p = shape_offset(b, index);
    EncounterShape s;
    s.type = u32(b, 4 + index * 4);
    require(s.type < 2, "This shape is preserved but is not a box or cylinder");
    for (unsigned k = 0; k < 3; ++k)
        s.position[k] = f32(b, p + k * 4);
    if (!s.type)
        s.size = {f32(b, p + 12) * 2, f32(b, p + 16), f32(b, p + 12) * 2};
    else {
        for (unsigned k = 0; k < 4; ++k)
            s.rotation[k] = f32(b, p + 12 + k * 4);
        s.size = {f32(b, p + 28) * 2, f32(b, p + 32), f32(b, p + 36) * 2};
    }
    return s;
}
void EncounterDocument::validate(const State &s) const {
    require(s.regions.size() <= 65535 && s.tables.size() <= 65535 &&
                s.tables.size() >= original_.tables.size(),
            "Encounter document exceeds supported counts");
    std::set<int> sources;
    for (auto &r : s.regions) {
        require(r.source >= -1 && (r.source < 0 || unsigned(r.source) < original_.regions.size()),
                "Invalid source encounter region");
        if (r.source >= 0) {
            require(sources.insert(r.source).second, "Duplicate source encounter identity");
            require(r.zone == original_.regions[r.source].zone,
                    "Existing encounter region cannot change zone");
        }
        require(r.zone < zones_.files.size(), "Choose an existing local zone");
        require(r.table < s.tables.size() && s.tables[r.table].bytes.size() >= 716,
                "Encounter region references a missing table");
        require(shape_end(r.shapes) == r.shapes.size(),
                "Unexpected encounter shape trailing bytes");
        SpatialScene check;
        decode_placement_collision(check, r.shapes, "", -1);
        if (r.source < 0 || r != original_.regions[r.source]) {
            require(u32(r.shapes, 0) > 0, "Encounter region needs a shape");
            for (unsigned i = 0; i < u32(r.shapes, 0); ++i) {
                auto type = u32(r.shapes, 4 + i * 4);
                auto p = shape_offset(r.shapes, i);
                if (type == 0) {
                    require(f32(r.shapes, p + 12) > 0 && f32(r.shapes, p + 16) > 0,
                            "Cylinder dimensions must be positive");
                } else if (type == 1) {
                    for (unsigned k = 0; k < 3; ++k)
                        require(f32(r.shapes, p + 28 + k * 4) > 0,
                                "Box dimensions must be positive");
                    float norm = 0;
                    for (unsigned k = 0; k < 4; ++k) {
                        auto v = f32(r.shapes, p + 12 + k * 4);
                        norm += v * v;
                    }
                    require(std::abs(norm - 1) < .01f,
                            "Encounter box orientation must be normalized");
                }
            }
            require(std::any_of(r.attributes.begin(), r.attributes.end(),
                                [](auto mask) {
                                    return mask != 0;
                                }),
                    "Choose at least one ground type");
        }
    }
    for (unsigned i = 0; i < s.tables.size(); ++i) {
        auto &t = s.tables[i];
        require(t.source < original_.tables.size() &&
                    (i >= original_.tables.size() || t.source == i),
                "Invalid encounter table source");
        auto &baseline = original_.tables[t.source].bytes;
        require(t.bytes.size() == baseline.size(), "Encounter table size changed");
        for (std::size_t p = 0; p < t.bytes.size(); ++p)
            if (t.bytes[p] != baseline[p])
                require(p < 716 && editable_table_byte(p),
                        "Protected encounter table data changed");
        for (unsigned night = 0; night < 2; ++night) {
            auto begin = 4 + night * period_size;
            if (t.bytes.size() >= 716 &&
                !std::equal(t.bytes.begin() + begin, t.bytes.begin() + begin + period_size,
                            baseline.begin() + begin))
                validate_period(read_period(t.bytes, night != 0), 2048);
        }
    }
}
void EncounterDocument::commit(State next) {
    validate(next);
    if (next == state_)
        return;
    state_ = std::move(next);
    history_.resize(++cursor_);
    history_.push_back(state_);
}
void EncounterDocument::set_region(unsigned index, const EncounterRegion &r) {
    auto next = state_;
    require(r.source == next.regions.at(index).source, "Region identity cannot be changed");
    next.regions[index] = r;
    commit(std::move(next));
}
void EncounterDocument::set_shape(unsigned region, unsigned index, const EncounterShape &shape) {
    preview_shape(region, index, shape);
    commit_preview();
}
void EncounterDocument::commit_preview() {
    auto next = state_;
    state_ = history_[cursor_];
    commit(std::move(next));
}
void EncounterDocument::cancel_preview() {
    state_ = history_[cursor_];
}
void EncounterDocument::preview_shape(unsigned region, unsigned index,
                                      const EncounterShape &shape) {
    auto bytes = shape_bytes(shape);
    auto r = state_.regions.at(region);
    auto p = shape_offset(r.shapes, index);
    require(u32(r.shapes, 4 + index * 4) == shape.type, "Shape type cannot be changed in place");
    std::copy(bytes.begin(), bytes.end(), r.shapes.begin() + p);
    auto next = state_;
    next.regions[region] = std::move(r);
    validate(next);
    state_ = std::move(next);
}
unsigned EncounterDocument::add_region(unsigned zone, unsigned table, const EncounterShape &shape) {
    auto next = state_;
    EncounterRegion r;
    r.zone = zone;
    r.table = table;
    r.attributes[0] = 1u << 31;
    r.shapes.resize(8);
    put32(r.shapes, 0, 1);
    put32(r.shapes, 4, shape.type);
    append(r.shapes, shape_bytes(shape));
    next.regions.push_back(r);
    commit(std::move(next));
    return unsigned(state_.regions.size() - 1);
}
unsigned EncounterDocument::duplicate(unsigned region) {
    auto next = state_;
    auto r = next.regions.at(region);
    r.source = -1;
    next.regions.push_back(r);
    commit(std::move(next));
    return unsigned(state_.regions.size() - 1);
}
void EncounterDocument::erase(unsigned region) {
    auto next = state_;
    require(region < next.regions.size(), "Select an encounter region");
    next.regions.erase(next.regions.begin() + region);
    commit(std::move(next));
}
unsigned EncounterDocument::make_table_unique(unsigned region) {
    auto next = state_;
    auto &r = next.regions.at(region);
    auto t = next.tables.at(r.table);
    r.table = unsigned(next.tables.size());
    auto result = r.table;
    next.tables.push_back(std::move(t));
    commit(std::move(next));
    return result;
}
void EncounterDocument::set_period(unsigned table, bool night, const EncounterPeriod &v,
                                   unsigned species_count) {
    validate_period(v, species_count);
    require(has_table(table), "Choose a populated encounter table");
    auto next = state_;
    auto &b = next.tables[table].bytes;
    auto p = 4 + unsigned(night) * period_size;
    b[p] = std::uint8_t(v.minimum);
    b[p + 1] = std::uint8_t(v.maximum);
    for (unsigned i = 0; i < 10; ++i) {
        b[p + 2 + i] = std::uint8_t(v.slots[i].weight);
        put16(b, p + 12 + i * 4, std::uint16_t(v.slots[i].species | (v.slots[i].form << 11)));
    }
    commit(std::move(next));
}
void EncounterDocument::undo() {
    if (can_undo())
        state_ = history_[--cursor_];
}
void EncounterDocument::redo() {
    if (can_redo())
        state_ = history_[++cursor_];
}
Bytes EncounterDocument::placements() const {
    if (state_.regions == original_.regions)
        return placement_source_;
    auto ed = placements_, zs = zones_;
    for (unsigned z = 0; z < zs.files.size(); ++z) {
        std::vector<EncounterRegion> before, after;
        for (auto &r : original_.regions)
            if (r.zone == z)
                before.push_back(r);
        for (auto &r : state_.regions)
            if (r.zone == z)
                after.push_back(r);
        if (before == after)
            continue;
        auto &source = zs.files[z];
        auto count = unsigned(after.size());
        auto old_count = unsigned(before.size());
        auto tail = source.empty() ? 0 : 4 + old_count * record_size;
        Bytes extras(source.begin() + tail, source.end()), b(4 + count * record_size);
        put32(b, 0, count);
        for (unsigned i = 0; i < count; ++i) {
            auto &r = after[i];
            auto p = 4 + i * record_size;
            put32(b, p, TargetProfile::encounter_record_type);
            put32(b, p + 4, r.table);
            for (unsigned k = 0; k < 4; ++k)
                put32(b, p + 8 + k * 4, r.attributes[k]);
            auto found = std::find_if(before.begin(), before.end(), [&](auto &old) {
                return old.source == r.source && r.source >= 0 && old.shapes == r.shapes;
            });
            if (found != before.end()) {
                auto row = unsigned(found - before.begin());
                auto old = u32(source, 4 + row * record_size + 24);
                put32(b, p + 24,
                      narrow(std::size_t(std::int64_t(old) +
                                         (std::int64_t(count) - old_count) * record_size)));
            } else {
                put32(b, p + 24, narrow(b.size() + extras.size()));
                append(extras, r.shapes);
            }
        }
        append(b, extras);
        zs.files[z] = std::move(b);
    }
    ed.files[TargetProfile::encounter_placement_pack] = zs.write(4);
    return ed.write(4);
}
Bytes EncounterDocument::tables() const {
    if (state_.tables == original_.tables)
        return table_source_;
    auto out = tables_;
    out.files.clear();
    for (auto &t : state_.tables)
        out.files.push_back(t.bytes);
    return out.write(TargetProfile::resource_alignment);
}
std::string EncounterDocument::serialize() const {
    std::ostringstream s;
    s << "USUMSTUDIO_ENCOUNTERS 1\narea " << area_ << "\nsource " << hash_ << '\n';
    for (auto &r : state_.regions) {
        s << "region " << r.source << ' ' << r.zone << ' ' << r.table;
        for (auto mask : r.attributes)
            s << ' ' << mask;
        s << ' ' << hex(r.shapes) << '\n';
    }
    for (unsigned i = 0; i < state_.tables.size(); ++i) {
        auto &t = state_.tables[i];
        s << "table " << i << ' ' << t.source << '\n';
        auto &b = original_.tables[t.source].bytes;
        for (std::size_t p = 0; p < t.bytes.size(); ++p)
            if (t.bytes[p] != b[p])
                s << "byte " << p << ' ' << unsigned(t.bytes[p]) << '\n';
    }
    s << "end\n";
    return s.str();
}
void EncounterDocument::restore(const std::string &patch) {
    std::istringstream s(patch);
    std::string tag, hash;
    unsigned version, area;
    require(bool(s >> tag >> version) && tag == "USUMSTUDIO_ENCOUNTERS" && version == 1,
            "Unsupported encounter document");
    require(bool(s >> tag >> area) && tag == "area" && area == area_,
            "Encounter document belongs to another area");
    require(bool(s >> tag >> hash) && tag == "source" && hash == hash_,
            "Encounter source changed; reopen matching source");
    State next;
    bool ended = false;
    std::set<std::size_t> seen;
    while (s >> tag) {
        if (tag == "end") {
            ended = true;
            break;
        }
        if (tag == "region") {
            require(next.tables.empty(), "Region appears after table data");
            EncounterRegion r;
            std::string payload;
            require(bool(s >> r.source >> r.zone >> r.table >> r.attributes[0] >> r.attributes[1] >>
                         r.attributes[2] >> r.attributes[3] >> payload),
                    "Invalid encounter region");
            r.shapes = unhex(payload);
            next.regions.push_back(std::move(r));
            require(next.regions.size() <= 65535, "Too many encounter regions");
        } else if (tag == "table") {
            unsigned index, source;
            require(bool(s >> index >> source) && index == next.tables.size() &&
                        source < original_.tables.size() && index < 65535,
                    "Invalid encounter table");
            next.tables.push_back({source, original_.tables[source].bytes});
            seen.clear();
        } else if (tag == "byte") {
            std::size_t p;
            unsigned value;
            require(bool(s >> p >> value) && value <= 255 && !next.tables.empty() &&
                        p < next.tables.back().bytes.size() && seen.insert(p).second,
                    "Invalid encounter table byte");
            next.tables.back().bytes[p] = std::uint8_t(value);
        } else
            throw std::runtime_error("Unknown encounter document entry");
    }
    s >> std::ws;
    require(ended && s.eof(), "Incomplete encounter document");
    validate(next);
    state_ = std::move(next);
    saved_ = state_;
    history_ = {state_};
    cursor_ = 0;
}
void EncounterDocument::export_to(const std::filesystem::path &dump, const ArchiveSources &sources,
                                  const std::filesystem::path &output) const {
    validate(state_);
    if (!changed())
        return;
    for (unsigned z = 0; z < zone_count(); ++z) {
        auto count = std::count_if(state_.regions.begin(), state_.regions.end(), [&](auto &r) {
            return r.zone == z;
        });
        require(count <= TargetProfile::encounter_actor_capacity,
                "Zone slot " + std::to_string(z) + " has " + std::to_string(count) +
                    " encounter regions; runtime limit is " +
                    std::to_string(TargetProfile::encounter_actor_capacity));
    }
    Archive text_archive(dump / TargetProfile::location_text_archive);
    auto names = decode_location_text(text_archive.decoded(TargetProfile::pokemon_names_member));
    for (auto &t : state_.tables) {
        auto &baseline = original_.tables[t.source].bytes;
        for (unsigned night = 0; night < 2; ++night)
            if (t.bytes.size() >= 716) {
                auto begin = 4 + night * period_size;
                if (!std::equal(t.bytes.begin() + begin, t.bytes.begin() + begin + period_size,
                                baseline.begin() + begin))
                    validate_period(read_period(t.bytes, night != 0), unsigned(names.size()));
            }
    }
    auto source = sources.resolve(dump, GameProfile::field_archive(dump)),
         target = output / GameProfile::field_archive(dump);
    require(std::filesystem::absolute(source).lexically_normal() !=
                std::filesystem::absolute(target).lexically_normal(),
            "Encounter export must not replace the source");
    Archive field(source);
    auto base = area_ * TargetProfile::area_stride;
    require(field.decoded(base) == placement_source_ &&
                field.decoded(base + TargetProfile::encounter_table_slot) == table_source_,
            "Encounter source changed after opening");
    std::map<std::size_t, Bytes> changes;
    auto put = [&](std::size_t member, Bytes bytes, const Bytes &original) {
        if (bytes == original)
            return;
        auto raw = field.raw(member);
        auto packed = !raw.empty() && raw[0] == 0x11 ? compress(bytes) : bytes;
        require(decompress(packed) == bytes, "Encounter compression readback failed");
        changes[member] = std::move(packed);
    };
    put(base, placements(), placement_source_);
    put(base + TargetProfile::encounter_table_slot, tables(), table_source_);
    std::filesystem::create_directories(target.parent_path());
    field.export_to(target, changes);
}
void decode_encounter_regions(SpatialScene &scene, View placements,
                              const std::map<unsigned, int> &zone_ids) {
    auto ed = Container::parse(placements, "ED");
    if (ed.files.size() <= TargetProfile::encounter_placement_pack ||
        ed.files[TargetProfile::encounter_placement_pack].empty())
        return;
    auto zones = Container::parse(ed.files[TargetProfile::encounter_placement_pack]);
    SpatialScene next;
    for (unsigned z = 0; z < zones.files.size(); ++z) {
        auto &b = zones.files[z];
        if (b.empty())
            continue;
        auto count = u32(b, 0);
        require(count <= 4096, "Excessive encounter regions");
        slice(b, 4, count * record_size);
        for (unsigned row = 0; row < count; ++row) {
            auto p = 4 + row * record_size;
            require(u32(b, p) == TargetProfile::encounter_record_type,
                    "Unexpected encounter region type");
            auto off = u32(b, p + 24);
            require(off >= 4 + count * record_size, "Encounter shape overlaps records");
            SpatialScene shapes;
            auto zi = zone_ids.find(z);
            int zone = zi == zone_ids.end() ? -1 : zi->second;
            decode_placement_collision(
                shapes, slice(b, off, shape_end(slice(b, off, b.size() - off))), "", zone);
            SpatialRegion r;
            r.kind = SpatialKind::Encounter;
            r.zone = zone;
            r.encounter_zone = int(z);
            r.encounter_row = int(row);
            r.attribute = u32(b, p + 4);
            r.name = "Encounter / zone slot " + std::to_string(z) + " / region " +
                     std::to_string(row) + " / table " + std::to_string(r.attribute);
            r.detail = "Allowed ground: ";
            bool first = true;
            for (unsigned attr = 0; attr < 128; ++attr)
                if (u32(b, p + 8 + (attr / 32) * 4) & (1u << (attr % 32))) {
                    if (!first)
                        r.detail += ", ";
                    r.detail += collision_surface_name(attr);
                    first = false;
                }
            if (first)
                r.detail += "none";
            for (auto &sh : shapes.regions) {
                auto base = narrow(r.vertices.size());
                r.vertices.insert(r.vertices.end(), sh.vertices.begin(), sh.vertices.end());
                for (auto i : sh.triangles)
                    r.triangles.push_back(base + i);
                for (auto i : sh.lines)
                    r.lines.push_back(base + i);
            }
            next.regions.push_back(std::move(r));
        }
    }
    scene.regions.insert(scene.regions.end(), std::make_move_iterator(next.regions.begin()),
                         std::make_move_iterator(next.regions.end()));
}
}
