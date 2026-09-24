#include "field/placement_document.h"
#include "core/digest.h"
#include "formats/compression.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <set>
namespace studio {
namespace {
constexpr unsigned shape_sizes[]{20, 40, 28, 36};
std::size_t collision_end(View bytes, std::size_t offset) {
    auto count = u32(bytes, offset);
    require(count <= 4096, "Too many collision shapes");
    slice(bytes, offset + 4, count * 4);
    auto p = offset + 4 + count * 4;
    for (unsigned i = 0; i < count; ++i) {
        auto type = u32(bytes, offset + 4 + i * 4);
        require(type < 4, "Unknown collision shape; this placement is read-only");
        slice(bytes, p, shape_sizes[type]);
        p += shape_sizes[type];
    }
    return p;
}
void move_point(Bytes &b, std::size_t p, const Matrix &m) {
    auto x = f32(b, p), y = f32(b, p + 4), z = f32(b, p + 8);
    for (unsigned k = 0; k < 3; ++k)
        put_float(b, p + k * 4, m[k * 4] * x + m[k * 4 + 1] * y + m[k * 4 + 2] * z + m[k * 4 + 3]);
}
std::array<float, 4> turned(std::array<float, 4> q, float degrees) {
    float s = std::sin(degrees * .00872664626f), c = std::cos(degrees * .00872664626f);
    return {c * q[0] + s * q[2], c * q[1] + s * q[3], c * q[2] - s * q[0], c * q[3] - s * q[1]};
}
}
PlacementDocument::PlacementDocument(unsigned area, Bytes source)
    : area_(area), source_(std::move(source)), digest_(sha256(source_)) {
    auto ed = Container::parse(source_, "ED");
    auto es = Container::parse(ed.files.at(TargetProfile::static_pack), "ES");
    auto pack_offset = u32(source_, 4 + TargetProfile::static_pack * 4);
    for (std::size_t zone = 0; zone < es.files.size(); ++zone) {
        auto rows = read_placements(es.files[zone]);
        auto zone_offset = pack_offset + u32(ed.files[TargetProfile::static_pack], 4 + zone * 4);
        std::vector<std::pair<std::size_t, std::size_t>> spans;
        for (std::size_t row = 0; row < rows.size(); ++row) {
            PlacementEntry e{zone, row, zone_offset + 4 + row * 56, rows[row], {}};
            if (e.source.collision)
                try {
                    auto end = collision_end(es.files[zone], e.source.collision);
                    spans.push_back({zone_offset + e.source.collision, zone_offset + end});
                } catch (const std::exception &error) {
                    e.restriction = error.what();
                    spans.push_back({0, 0});
                }
            else
                spans.push_back({0, 0});
            entries_.push_back(e);
            states_.push_back({e.source.position, 0});
        }
        auto first = entries_.size() - rows.size();
        for (unsigned i = 0; i < spans.size(); ++i)
            if (spans[i].second)
                for (unsigned j = 0; j < i; ++j)
                    if (spans[j].second && spans[i].first < spans[j].second &&
                        spans[j].first < spans[i].second) {
                        entries_[first + i].restriction = entries_[first + j].restriction =
                            "Shared collision payload; independent movement is not supported yet";
                    }
    }
    for (const auto slot :
         {TargetProfile::character_placement_pack, TargetProfile::trainer_placement_pack}) {
        const bool trainer = slot == TargetProfile::trainer_placement_pack;
        const unsigned stride = trainer ? 84 : 120;
        if (slot < ed.files.size() && ed.files[slot].size() >= 4) {
            const auto pack = Container::parse(ed.files[slot]);
            const auto pack_base = u32(source_, 4 + slot * 4);
            for (std::size_t zone = 0; zone < pack.files.size(); ++zone) {
                const auto &bytes = pack.files[zone];
                if (bytes.empty())
                    continue;
                const auto count = u32(bytes, 0);
                require(count <= 4096, "Excessive NPC placements");
                slice(bytes, 4, std::size_t(count) * stride);
                const auto base = pack_base + u32(ed.files[slot], 4 + zone * 4);
                struct Span {
                    std::size_t entry, begin, end;
                };
                std::vector<Span> spans;
                for (unsigned row = 0; row < count; ++row) {
                    const auto at = 4 + row * stride;
                    require(u32(bytes, at) == (trainer ? 7u : 1u),
                            "Unsupported NPC placement type");
                    PlacementEntry e;
                    e.character = true;
                    e.trainer = trainer;
                    e.character_model = u32(bytes, at + (trainer ? 48 : 52));
                    e.zone = zone;
                    e.row = row;
                    e.offset = base + at;
                    e.source.event = u32(bytes, at + 44);
                    for (unsigned k = 0; k < 3; ++k)
                        e.source.position[k] = f32(bytes, at + 4 + k * 4);
                    float norm = 0;
                    for (unsigned k = 0; k < 4; ++k) {
                        e.source.rotation[k] = f32(bytes, at + 16 + k * 4);
                        norm += e.source.rotation[k] * e.source.rotation[k];
                    }
                    if (!std::isfinite(norm) || norm < .9f || norm > 1.1f)
                        e.restriction = "Invalid NPC orientation";
                    for (auto v : e.source.position)
                        if (!std::isfinite(v) || std::abs(v) >= 1e7f)
                            e.restriction = "Invalid NPC position";
                    if (u32(bytes, at + (trainer ? 56 : 104)))
                        e.restriction = "Shared alias NPC: edit its owning zone instead";
                    for (unsigned field : {trainer ? 64u : 112u, trainer ? 68u : 116u}) {
                        const auto offset = u32(bytes, at + field);
                        if (!offset)
                            continue;
                        try {
                            require(offset >= 4 + count * stride,
                                    "NPC shape overlaps placement records");
                            auto end = collision_end(bytes, offset);
                            e.shapes.push_back(base + offset);
                            spans.push_back({entries_.size(), offset, end});
                        } catch (const std::exception &error) {
                            e.restriction = error.what();
                        }
                    }
                    if (trainer && u32(bytes, at + 76)) {
                        auto route = u32(bytes, at + 76);
                        require(route >= 4 + count * stride,
                                "Trainer route overlaps placement records");
                        slice(bytes, route, 8 + std::size_t(u16(bytes, route)) * 12);
                        e.patrol = base + route;
                    }
                    std::sort(e.shapes.begin(), e.shapes.end());
                    e.shapes.erase(std::unique(e.shapes.begin(), e.shapes.end()), e.shapes.end());
                    entries_.push_back(e);
                    states_.push_back({e.source.position, 0});
                }
                for (std::size_t i = 0; i < spans.size(); ++i)
                    for (std::size_t j = 0; j < i; ++j)
                        if (spans[i].begin < spans[j].end && spans[j].begin < spans[i].end &&
                            (spans[i].entry != spans[j].entry ||
                             spans[i].begin != spans[j].begin)) {
                            entries_[spans[i].entry].restriction =
                                entries_[spans[j].entry].restriction =
                                    "Shared NPC shape payload: independent movement is unavailable";
                        }
            }
        }
    }
    initial_ = saved_ = states_;
    history_.push_back(states_);
}
Matrix PlacementDocument::transform(std::size_t i) const {
    auto &a = entries_.at(i).source.position;
    auto &b = states_.at(i);
    float c = std::cos(b.turn * .01745329252f), s = std::sin(b.turn * .01745329252f);
    return {c,  0, s, b.position[0] - c * a[0] - s * a[2], 0, 1, 0, b.position[1] - a[1],
            -s, 0, c, b.position[2] + s * a[0] - c * a[2], 0, 0, 0, 1};
}
void PlacementDocument::preview(std::size_t i, PlacementState state) {
    require(entries_.at(i).restriction.empty(), entries_.at(i).restriction);
    for (auto v : state.position)
        require(std::isfinite(v) && std::abs(v) < 1e7f,
                "Position must be finite and within the map coordinate range");
    require(std::isfinite(state.turn) && std::abs(state.turn) <= 360000, "Invalid turn angle");
    if (states_[i] != state) {
        states_[i] = state;
        ++revision_;
    }
}
void PlacementDocument::commit() {
    if (history_.empty())
        return;
    if (states_ == history_[cursor_])
        return;
    history_.resize(cursor_ + 1);
    history_.push_back(states_);
    ++cursor_;
    if (history_.size() > 257) {
        history_.erase(history_.begin());
        --cursor_;
    }
}
void PlacementDocument::cancel() {
    if (!history_.empty() && states_ != history_[cursor_]) {
        states_ = history_[cursor_];
        ++revision_;
    }
}
void PlacementDocument::undo() {
    cancel();
    if (can_undo()) {
        states_ = history_[--cursor_];
        ++revision_;
    }
}
void PlacementDocument::redo() {
    cancel();
    if (can_redo()) {
        states_ = history_[++cursor_];
        ++revision_;
    }
}
void PlacementDocument::reset(std::size_t i) {
    preview(i, initial_.at(i));
    commit();
}
std::size_t PlacementDocument::changed_count() const {
    std::size_t n = 0;
    for (unsigned i = 0; i < states_.size(); ++i)
        n += states_[i] != initial_[i];
    return n;
}
std::string PlacementDocument::serialize() const {
    std::ostringstream out;
    bool characters = false;
    for (unsigned i = 0; i < entries_.size(); ++i)
        characters |= entries_[i].character && states_[i] != initial_[i];
    out << "USUMSTUDIO_PLACEMENTS " << (characters ? 3 : 1) << "\narea " << area_ << "\nsource "
        << digest_ << "\n"
        << std::setprecision(9);
    for (unsigned i = 0; i < entries_.size(); ++i)
        if (states_[i] != initial_[i]) {
            auto &e = entries_[i];
            auto &s = states_[i];
            out << (e.trainer     ? "trainer "
                    : e.character ? "character "
                                  : "placement ")
                << e.zone << ' ' << e.row << ' '
                << (e.character ? e.character_model : e.source.model) << ' ' << e.source.event
                << ' ' << s.position[0] << ' ' << s.position[1] << ' ' << s.position[2] << ' '
                << s.turn << '\n';
        }
    return out.str();
}
void PlacementDocument::restore(const std::string &text) {
    std::istringstream in(text);
    std::string word, hash;
    unsigned version = 0, area = 0;
    require(bool(in >> word >> version) && word == "USUMSTUDIO_PLACEMENTS" &&
                (version >= 1 && version <= 3),
            "Unsupported placement patch");
    require(bool(in >> word >> area) && word == "area" && area == area_,
            "Patch belongs to a different field area; load that area first");
    require(bool(in >> word >> hash) && word == "source" && hash == digest_,
            "Patch source differs from this map's placement data");
    auto replacement = initial_;
    std::set<std::size_t> seen;
    while (in >> word) {
        std::size_t zone = 0, row = 0;
        unsigned model = 0, event = 0;
        PlacementState s;
        const bool trainer = word == "trainer" && version >= 3;
        const bool character = (word == "character" && version >= 2) || trainer;
        require((word == "placement" || character) &&
                    bool(in >> zone >> row >> model >> event >> s.position[0] >> s.position[1] >>
                         s.position[2] >> s.turn),
                "Malformed placement patch entry");
        auto it = std::find_if(entries_.begin(), entries_.end(), [&](const auto &e) {
            return e.character == character && e.trainer == trainer && e.zone == zone &&
                   e.row == row;
        });
        require(it != entries_.end() &&
                    (it->character ? it->character_model : it->source.model) == model &&
                    it->source.event == event,
                "Patch placement identity does not match the map");
        auto i = std::size_t(it - entries_.begin());
        require(seen.insert(i).second, "Duplicate patch placement");
        require(it->restriction.empty(), it->restriction);
        for (auto v : s.position)
            require(std::isfinite(v) && std::abs(v) < 1e7f, "Invalid patch position");
        require(std::isfinite(s.turn) && std::abs(s.turn) <= 360000, "Invalid patch angle");
        replacement[i] = s;
    }
    require(in.eof(), "Malformed patch");
    states_ = std::move(replacement);
    ++revision_;
    commit();
    saved_ = states_;
}
Bytes PlacementDocument::compile() const {
    auto result = source_;
    auto ed = Container::parse(source_, "ED");
    auto es = Container::parse(ed.files.at(TargetProfile::static_pack), "ES");
    std::size_t aliases = 0;
    for (auto &e : entries_)
        aliases += !e.character && e.source.alias != 0;
    for (unsigned zone = 0; zone < es.files.size(); ++zone) {
        auto rows = read_placements(es.files[zone]);
        auto count = std::count_if(rows.begin(), rows.end(), [](const auto &p) {
            return !p.alias;
        });
        require(std::size_t(count) + aliases <= TargetProfile::actor_capacity,
                "Zone " + std::to_string(zone) + " exceeds the static actor capacity (" +
                    std::to_string(TargetProfile::actor_capacity) +
                    "); reduce placements before exporting");
    }
    for (unsigned i = 0; i < entries_.size(); ++i)
        if (states_[i] != initial_[i]) {
            auto &e = entries_[i];
            auto &s = states_[i];
            require(e.restriction.empty(), e.restriction);
            for (unsigned k = 0; k < 3; ++k)
                put_float(result, e.offset + 4 + k * 4, s.position[k]);
            auto q = turned(e.source.rotation, s.turn);
            for (unsigned k = 0; k < 4; ++k)
                put_float(result, e.offset + 16 + k * 4, q[k]);
            if (e.patrol)
                for (unsigned point = 0; point < u16(result, e.patrol); ++point)
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        auto at = e.patrol + 8 + point * 12 + axis * 4;
                        put_float(result, at,
                                  f32(result, at) + s.position[axis] - e.source.position[axis]);
                    }
            auto shapes = e.shapes;
            if (!e.character && e.source.collision)
                shapes.push_back(e.offset - 4 - e.row * 56 + e.source.collision);
            for (auto start : shapes) {
                auto count = u32(result, start);
                auto p = start + 4 + count * 4;
                auto m = transform(i);
                for (unsigned k = 0; k < count; ++k) {
                    auto type = u32(result, start + 4 + k * 4);
                    require(type < 4, "Unsupported collision shape");
                    move_point(result, p, m);
                    if (type == 1) {
                        std::array<float, 4> shape;
                        for (unsigned j = 0; j < 4; ++j)
                            shape[j] = f32(result, p + 12 + j * 4);
                        shape = turned(shape, s.turn);
                        for (unsigned j = 0; j < 4; ++j)
                            put_float(result, p + 12 + j * 4, shape[j]);
                    } else if (type == 2)
                        move_point(result, p + 12, m);
                    else if (type == 3) {
                        move_point(result, p + 12, m);
                        move_point(result, p + 24, m);
                    }
                    p += shape_sizes[type];
                }
            }
        }
    auto check = Container::parse(result, "ED");
    auto zones = Container::parse(check.files.at(TargetProfile::static_pack), "ES");
    for (auto &bytes : zones.files)
        read_placements(bytes);
    require(result.size() == source_.size(), "Placement editing unexpectedly changed member size");
    return result;
}
void export_placements(const std::filesystem::path &dump, const std::filesystem::path &folder,
                       const PlacementDocument &document, const ArchiveSources &archives) {
    require(document.changed_count() > 0, "There are no placement changes to export");
    auto path = archives.resolve(dump, GameProfile::field_archive(dump));
    auto destination = folder / GameProfile::field_archive(dump);
    require(!std::filesystem::exists(destination),
            "Export already contains a field archive; choose an empty output folder");
    require(!std::filesystem::exists(folder / "placements.usum-map") &&
                !std::filesystem::exists(folder / "export-report.txt"),
            "Output contains a previous patch; choose an empty output folder");
    Archive archive(path);
    auto index = document.area() * TargetProfile::area_stride + TargetProfile::placement_slot;
    require(archive.decoded(index) == document.source(),
            "Source placements changed since loading; reload the map before exporting");
    auto bytes = document.compile();
    auto packed = compress(bytes);
    require(decompress(packed) == bytes, "Placement compression readback failed");
    archive.export_to(destination, {{index, std::move(packed)}});
    auto recipe = document.serialize();
    write_new_file(folder / "placements.usum-map",
                   View(reinterpret_cast<const std::uint8_t *>(recipe.data()), recipe.size()));
    auto report = "USUMStudio placement export\nArea: " + std::to_string(document.area()) +
                  "\nChanged placements: " + std::to_string(document.changed_count()) +
                  "\nOnly the field placement member was changed. All archive members were "
                  "verified by readback.\nSource dump was not modified. Remove this exported "
                  "override to restore the original map.\nRegenerate with placements.usum-map and "
                  "the source field archive recorded below.\n";
    report += "Source field archive: " + path.string() + "\n";
    write_new_file(folder / "export-report.txt",
                   View(reinterpret_cast<const std::uint8_t *>(report.data()), report.size()));
}
}
