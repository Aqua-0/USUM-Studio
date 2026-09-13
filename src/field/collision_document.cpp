#include "field/collision_document.h"
#include "assets/material_document.h"
#include "core/digest.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
namespace studio {
namespace {
std::size_t face_count(View bytes) {
    require(u32(bytes, 0) == 0x14120500, "Unsupported terrain collision version");
    auto count = u32(bytes, 4);
    require(count <= 2000000, "Excessive collision triangle count");
    slice(bytes, 8, std::size_t(count) * 72);
    return count;
}
SpatialPoint normal(const CollisionFace &face) {
    SpatialPoint a{}, b{}, n{};
    for (unsigned k = 0; k < 3; ++k) {
        for (auto &p : face)
            require(std::isfinite(p[k]) && std::abs(p[k]) < 1e9f,
                    "Collision coordinates must be finite and within the supported map range");
        a[k] = face[1][k] - face[0][k];
        b[k] = face[2][k] - face[0][k];
    }
    for (unsigned k = 0; k < 3; ++k)
        n[k] = a[(k + 1) % 3] * b[(k + 2) % 3] - a[(k + 2) % 3] * b[(k + 1) % 3];
    float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    require(std::isfinite(length) && length > 1e-6f, "Collision triangle has zero or invalid area");
    for (auto &v : n)
        v /= length;
    return n;
}
}
void validate_collision_state(const CollisionState &state) {
    require(!state.deleted && unsigned(state.kind) < 5, "Invalid authored collision type");
    normal(state.vertices);
}
CollisionFace read_collision_face(View bytes, std::size_t face) {
    require(face < face_count(bytes), "Collision triangle is out of range");
    CollisionFace result;
    for (unsigned v = 0; v < 3; ++v)
        for (unsigned k = 0; k < 3; ++k)
            result[v][k] = f32(bytes, 8 + face * 72 + v * 16 + k * 4);
    return result;
}
Bytes edit_collision_faces(View bytes, const std::map<std::size_t, CollisionFace> &edits) {
    auto count = face_count(bytes);
    Bytes out(bytes.begin(), bytes.end());
    for (auto &[face, vertices] : edits) {
        require(face < count, "Collision triangle is out of range");
        auto n = normal(vertices);
        auto offset = 8 + face * 72;
        for (unsigned v = 0; v < 3; ++v)
            for (unsigned k = 0; k < 3; ++k)
                put_float(out, offset + v * 16 + k * 4, vertices[v][k]);
        for (unsigned k = 0; k < 3; ++k)
            put_float(out, offset + 48 + k * 4, n[k]);
    }
    return out;
}
CollisionDocument::CollisionDocument(std::shared_ptr<Environment> scene, unsigned area,
                                     std::filesystem::path dump)
    : scene_(std::move(scene)), dump_(std::move(dump)), area_(area),
      base_regions_(scene_->spatial.regions.size()) {
    std::map<std::shared_ptr<const CollisionSource>, unsigned> ids;
    for (unsigned r = 0; r < base_regions_; ++r) {
        const auto &region = scene_->spatial.regions[r];
        if (!region.collision_source)
            continue;
        auto [it, added] = ids.emplace(region.collision_source, unsigned(sources_.size()));
        auto s = it->second;
        if (added) {
            auto link = region.collision_source;
            auto kind = region.kind;
            if (link->path == std::vector<std::size_t>{TargetProfile::terrain_ground_slot})
                kind = SpatialKind::Ground;
            else if (link->path.size() == 2 && link->path[0] == TargetProfile::terrain_wall_slot &&
                     link->path[1] < 4)
                kind = SpatialKind(unsigned(SpatialKind::Wall) + unsigned(link->path[1]));
            sources_.push_back({link, sha256(link->original), unsigned(original_.size()), kind});
            auto count = face_count(link->original);
            for (std::size_t i = 0; i < count; ++i) {
                origins_.push_back({s, i});
                original_.push_back({read_collision_face(link->original, i),
                                     u32(link->original, 8 + i * 72 + 64), kind});
            }
        }
        for (auto f : region.collision_faces)
            bindings_[r].push_back(sources_[s].first + unsigned(f));
        base_groups_[{s, region.kind, region.attribute}] = r;
    }
    current_ = original_;
}
std::size_t CollisionDocument::active_size() const {
    return std::count_if(current_.begin(), current_.end(), [](auto &value) {
        return !value.deleted;
    });
}
std::size_t CollisionDocument::member(unsigned id) const {
    return sources_.at(origins_.at(id).source).link->member;
}
CollisionVertexGroup CollisionDocument::vertex_group(CollisionVertex v) const {
    return {member(v.face), state(v.face).kind, state(v.face).vertices.at(v.corner)};
}
const CollisionState &CollisionDocument::from(const Edits &edits, unsigned id) const {
    auto it = edits.find(id);
    return it == edits.end() ? original_.at(id) : it->second;
}
void CollisionDocument::store(Edits &edits, unsigned id, const CollisionState &value) const {
    if (value == original_.at(id) || (value.deleted && original_.at(id).deleted))
        edits.erase(id);
    else
        edits[id] = value;
}
void CollisionDocument::edit(unsigned region, std::size_t index, const CollisionFace &vertices,
                             bool shared) {
    auto id = face_id(region, index);
    std::map<CollisionVertex, SpatialPoint> updates;
    for (unsigned v = 0; v < 3; ++v)
        updates[{id, v}] = vertices[v];
    edit_vertices(updates, shared);
}
void CollisionDocument::edit_vertices(const std::map<CollisionVertex, SpatialPoint> &vertices,
                                      bool shared) {
    const auto &base = preview_ ? *preview_ : edits_;
    auto next = base;
    std::map<CollisionVertexGroup, SpatialPoint> welded;
    for (auto &[v, p] : vertices) {
        require(live(v.face) && v.corner < 3, "Collision vertex is unavailable");
        const auto &before = from(base, v.face);
        if (shared) {
            auto [it, added] = welded.emplace(
                CollisionVertexGroup{member(v.face), before.kind, before.vertices[v.corner]}, p);
            require(added || it->second == p, "Shared vertex received conflicting positions");
        }
    }
    for (unsigned i = 0; i < size(); ++i) {
        auto before = from(base, i), value = before;
        if (before.deleted)
            continue;
        for (unsigned v = 0; v < 3; ++v) {
            auto own = vertices.find({i, v});
            if (own != vertices.end())
                value.vertices[v] = own->second;
            else if (shared) {
                auto it = welded.find({member(i), before.kind, before.vertices[v]});
                if (it != welded.end())
                    value.vertices[v] = it->second;
            }
        }
        if (value.vertices == before.vertices)
            continue;
        normal(value.vertices);
        store(next, i, value);
    }
    if (preview_) {
        if (next != edits_) {
            edits_ = std::move(next);
            synchronize();
        }
    } else
        accept(std::move(next));
}
void CollisionDocument::properties(const std::set<unsigned> &faces, std::optional<SpatialKind> kind,
                                   std::optional<std::uint32_t> attribute) {
    require(!preview_, "Finish the current transform first");
    if (kind)
        require(unsigned(*kind) <= unsigned(SpatialKind::MudsdaleWall),
                "Unsupported collision type");
    auto next = edits_;
    for (auto id : faces) {
        require(live(id), "Collision triangle was deleted");
        auto value = state(id);
        if (kind && *kind != value.kind) {
            auto &path = sources_[origins_[id].source].link->path;
            require(path == std::vector<std::size_t>{TargetProfile::terrain_ground_slot} ||
                        (path.size() == 2 && path[0] == TargetProfile::terrain_wall_slot &&
                         path[1] < 4),
                    "This collision resource cannot change type");
            value.kind = *kind;
        }
        if (attribute)
            value.attribute = *attribute;
        store(next, id, value);
    }
    accept(std::move(next));
}
void CollisionDocument::begin_preview() {
    require(!preview_, "A collision transform is already active");
    preview_ = edits_;
}
void CollisionDocument::commit_preview() {
    if (!preview_)
        return;
    auto next = std::move(edits_);
    edits_ = std::move(*preview_);
    preview_.reset();
    accept(std::move(next));
}
void CollisionDocument::cancel_preview() {
    if (!preview_)
        return;
    bool changed = edits_ != *preview_;
    edits_ = std::move(*preview_);
    preview_.reset();
    if (changed)
        synchronize();
}
void CollisionDocument::accept(Edits edits) {
    if (edits == edits_)
        return;
    history_.resize(cursor_ + 1);
    history_.push_back(std::move(edits));
    if (history_.size() > 129)
        history_.erase(history_.begin());
    cursor_ = history_.size() - 1;
    edits_ = history_[cursor_];
    synchronize();
}
void CollisionDocument::synchronize() {
    current_ = original_;
    for (auto &[id, value] : edits_)
        current_[id] = value;
    auto &regions = scene_->spatial.regions;
    std::vector<SpatialRegion> preserved_regions;
    for (std::size_t i = base_regions_; i < regions.size(); ++i)
        if (regions[i].kind == SpatialKind::Camera || regions[i].kind == SpatialKind::ScrollStop ||
            regions[i].kind == SpatialKind::Encounter)
            preserved_regions.push_back(std::move(regions[i]));
    regions.resize(base_regions_);
    for (auto &region : preserved_regions)
        regions.push_back(std::move(region));
    for (auto &r : regions)
        if (r.collision_source) {
            r.vertices.clear();
            r.triangles.clear();
            r.lines.clear();
            r.collision_faces.clear();
        }
    auto groups = base_groups_;
    for (unsigned id = 0; id < size(); ++id) {
        auto &value = current_[id];
        if (value.deleted)
            continue;
        auto origin = origins_[id];
        auto key = std::tuple{origin.source, value.kind, value.attribute};
        auto it = groups.find(key);
        if (it == groups.end()) {
            auto index = unsigned(regions.size());
            SpatialRegion region;
            region.collision_source = sources_[origin.source].link;
            regions.push_back(std::move(region));
            it = groups.emplace(key, index).first;
        }
        auto &r = regions[it->second];
        r.kind = value.kind;
        r.attribute = value.attribute;
        r.name = std::string(spatial_kind_name(value.kind)) + " / terrain " +
                 std::to_string(member(id)) + " / attribute " + std::to_string(value.attribute);
        r.collision_faces.push_back(origin.added ? std::size_t(-1) : origin.face);
        auto first = narrow(r.vertices.size());
        for (auto p : value.vertices)
            r.vertices.push_back({p, 0});
        for (unsigned v = 0; v < 3; ++v) {
            r.triangles.push_back(first + v);
            r.lines.push_back(first + v);
            r.lines.push_back(first + (v + 1) % 3);
        }
    }
    ++revision_;
}
void CollisionDocument::undo() {
    cancel_preview();
    if (can_undo()) {
        edits_ = history_[--cursor_];
        synchronize();
    }
}
void CollisionDocument::redo() {
    cancel_preview();
    if (can_redo()) {
        edits_ = history_[++cursor_];
        synchronize();
    }
}
void CollisionDocument::reset() {
    cancel_preview();
    accept({});
}
void CollisionDocument::replace_faces(const std::map<unsigned, CollisionState> &faces,
                                      const std::vector<CollisionAddition> &additions) {
    require(!preview_, "Finish the current transform first");
    auto next = edits_;
    std::vector<unsigned> sources;
    for (auto &[id, value] : faces) {
        require(id < size(), "Imported collision triangle is unavailable");
        require(unsigned(value.kind) < 5, "Invalid collision type");
        auto &path = sources_[origins_[id].source].link->path;
        if (value.deleted || value.kind != state(id).kind)
            require(path == std::vector<std::size_t>{TargetProfile::terrain_ground_slot} ||
                        (path.size() == 2 && path[0] == TargetProfile::terrain_wall_slot &&
                         path[1] < 4),
                    "Unsupported collision topology source");
        if (!value.deleted && value.vertices != state(id).vertices)
            normal(value.vertices);
        store(next, id, value);
    }
    for (auto &addition : additions) {
        require(!addition.state.deleted && unsigned(addition.state.kind) < 5,
                "Invalid new collision triangle");
        normal(addition.state.vertices);
        auto it = std::find_if(sources_.begin(), sources_.end(), [&](auto &source) {
            auto &path = source.link->path;
            return source.link->member == addition.member &&
                   (path == std::vector<std::size_t>{TargetProfile::terrain_ground_slot} ||
                    (path.size() == 2 && path[0] == TargetProfile::terrain_wall_slot &&
                     path[1] < 4));
        });
        require(it != sources_.end(), "New triangles must belong to an exported terrain resource");
        sources.push_back(unsigned(it - sources_.begin()));
    }
    std::vector<std::size_t> keys(sources_.size());
    for (auto origin : origins_)
        if (origin.added)
            keys[origin.source] = std::max(keys[origin.source], origin.face + 1);
    for (unsigned i = 0; i < additions.size(); ++i) {
        auto value = additions[i].state;
        auto key = keys[sources[i]]++;
        auto id = unsigned(original_.size());
        origins_.push_back({sources[i], key, true});
        value.deleted = true;
        original_.push_back(value);
        current_.push_back(value);
        next[id] = additions[i].state;
    }
    accept(std::move(next));
}
std::string CollisionDocument::face_token(unsigned id) const {
    auto origin = origins_.at(id);
    return std::string(origin.added ? "n" : "f") + std::to_string(origin.source) + "_" +
           std::to_string(origin.face);
}
std::string CollisionDocument::exchange_signature() const {
    std::ostringstream out;
    out << area_ << '\n';
    for (auto &source : sources_)
        out << source.link->archive.generic_string() << ' ' << source.link->member << ' '
            << source.hash << '\n';
    out << serialize();
    auto data = out.str();
    return sha256(View(reinterpret_cast<const std::uint8_t *>(data.data()), data.size()));
}
std::string CollisionDocument::serialize() const {
    require(!preview_, "Finish the current transform before saving");
    bool topology = false;
    for (auto &[id, value] : edits_)
        topology |= origins_[id].added || value.deleted;
    std::ostringstream out;
    out << std::setprecision(9) << "USUMSTUDIO_COLLISION " << (topology ? 3 : 2) << "\narea "
        << area_ << '\n';
    std::map<std::tuple<unsigned, bool, std::size_t>, unsigned> order;
    for (auto &[id, value] : edits_) {
        auto origin = origins_[id];
        order[{origin.source, origin.added, origin.face}] = id;
    }
    for (auto [key, id] : order) {
        auto &value = edits_.at(id);
        auto origin = origins_[id];
        auto &source = sources_[origin.source];
        out << (origin.added ? "new " : "face ")
            << std::quoted(source.link->archive.generic_string()) << ' ' << source.link->member
            << ' ' << source.link->path.size();
        for (auto child : source.link->path)
            out << ' ' << child;
        out << ' ' << source.hash << ' ' << origin.face << ' ' << unsigned(value.kind) << ' '
            << value.attribute;
        if (topology)
            out << ' ' << value.deleted;
        for (auto p : value.vertices)
            for (auto v : p)
                out << ' ' << v;
        out << '\n';
    }
    out << "end\n";
    return out.str();
}
void CollisionDocument::restore(const std::string &text) {
    require(!preview_, "Finish the current transform before opening a patch");
    std::istringstream in(text);
    std::string word;
    unsigned version, area;
    require(bool(in >> word >> version) && word == "USUMSTUDIO_COLLISION" && version >= 1 &&
                version <= 3,
            "Unsupported collision patch");
    require(bool(in >> word >> area) && word == "area" && area == area_,
            "Open the originating field area before this collision patch");
    Edits next;
    std::set<unsigned> seen;
    bool ended = false;
    auto originals = original_;
    auto origins = origins_;
    std::map<std::pair<unsigned, std::size_t>, unsigned> new_ids;
    for (unsigned id = 0; id < origins.size(); ++id)
        if (origins[id].added)
            new_ids[{origins[id].source, origins[id].face}] = id;
    while (in >> word) {
        if (word == "end") {
            ended = true;
            break;
        }
        bool added = word == "new";
        std::string archive, hash;
        std::size_t member, depth, index;
        require((word == "face" || (added && version == 3)) &&
                    bool(in >> std::quoted(archive) >> member >> depth) && depth <= 8,
                "Invalid collision source record");
        std::vector<std::size_t> path(depth);
        for (auto &child : path)
            require(bool(in >> child), "Truncated collision source path");
        require(bool(in >> hash >> index), "Missing collision source hash or face");
        auto it = std::find_if(sources_.begin(), sources_.end(), [&](auto &source) {
            return source.link->archive.generic_string() == archive &&
                   source.link->member == member && source.link->path == path;
        });
        require(it != sources_.end(), "Collision resource is not part of this area");
        require(hash == it->hash, "Collision source changed; open the original terrain archive");
        unsigned source = unsigned(it - sources_.begin()), id = 0;
        if (added) {
            require(index < 2000000, "New collision identifier is out of range");
            auto [found, inserted] =
                new_ids.emplace(std::pair{source, index}, unsigned(origins.size()));
            id = found->second;
        } else {
            require(index < face_count(it->link->original), "Collision triangle is out of range");
            id = it->first + unsigned(index);
        }
        auto value = added ? CollisionState{} : originals[id];
        if (version >= 2) {
            unsigned kind;
            std::uint64_t attribute;
            require(bool(in >> kind >> attribute) && kind < 5 && attribute <= 0xffffffffu,
                    "Invalid collision type or surface attribute");
            value.kind = SpatialKind(kind);
            value.attribute = std::uint32_t(attribute);
        }
        if (version == 3) {
            unsigned deleted;
            require(bool(in >> deleted) && deleted <= 1 && (!added || !deleted),
                    "Invalid deleted collision state");
            value.deleted = deleted != 0;
        }
        if (added || value.deleted || value.kind != originals[id].kind)
            require(path == std::vector<std::size_t>{TargetProfile::terrain_ground_slot} ||
                        (path.size() == 2 && path[0] == TargetProfile::terrain_wall_slot &&
                         path[1] < 4),
                    "Unsupported collision topology source");
        for (auto &p : value.vertices)
            for (auto &v : p)
                require(bool(in >> v) && std::isfinite(v) && std::abs(v) < 1e9f,
                        "Invalid collision vertices");
        if (!value.deleted && (added || value.vertices != originals[id].vertices))
            normal(value.vertices);
        require(seen.insert(id).second, "Duplicate collision face");
        if (added && id == origins.size()) {
            auto base = value;
            base.deleted = true;
            originals.push_back(base);
            origins.push_back({source, index, true});
        }
        if (value != originals[id])
            next[id] = value;
    }
    require(ended && !(in >> word), "Invalid collision patch ending");
    original_ = std::move(originals);
    origins_ = std::move(origins);
    current_.resize(original_.size());
    accept(std::move(next));
    saved_ = edits_;
}
void CollisionDocument::save(const std::filesystem::path &path) {
    auto text = serialize();
    write_file_atomic(path, View(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()));
    saved_ = edits_;
}
namespace {
Bytes replace_child(View original, std::size_t child, const Bytes &bytes, std::size_t minimum = 0) {
    if (!original.empty()) {
        auto pack = Container::parse(original, "BG");
        if (child < pack.files.size())
            return replace_asset_resource(original, {child}, bytes);
        pack.files.resize(std::max(child + 1, minimum));
        pack.files[child] = bytes;
        return pack.write();
    }
    Container pack;
    pack.tag = {'B', 'G'};
    pack.files.resize(std::max(child + 1, minimum));
    pack.files[child] = bytes;
    return pack.write();
}
Bytes triangle_record(View original, std::size_t face, const CollisionState &before,
                      const CollisionState &after) {
    auto record = slice(original, 8 + face * 72, 72);
    Bytes out(record.begin(), record.end());
    if (after.vertices != before.vertices) {
        auto n = normal(after.vertices);
        for (unsigned v = 0; v < 3; ++v)
            for (unsigned k = 0; k < 3; ++k)
                put_float(out, v * 16 + k * 4, after.vertices[v][k]);
        for (unsigned k = 0; k < 3; ++k)
            put_float(out, 48 + k * 4, n[k]);
    }
    put32(out, 64, after.attribute);
    return out;
}
}
Bytes CollisionDocument::compile_member(std::size_t member_id, View original) const {
    bool migration = false;
    for (auto &[id, value] : edits_)
        if (member(id) == member_id &&
            (value.kind != original_[id].kind || value.deleted || origins_[id].added))
            migration = true;
    Bytes result(original.begin(), original.end());
    for (auto &source : sources_)
        if (source.link->member == member_id)
            require(sha256(asset_resource(original, source.link->path)) == source.hash,
                    "Source collision changed after loading; reload before export");
    if (!migration) {
        for (unsigned s = 0; s < sources_.size(); ++s) {
            auto &source = sources_[s];
            if (source.link->member != member_id)
                continue;
            Bytes out = source.link->original;
            bool changed = false;
            for (auto &[id, value] : edits_)
                if (origins_[id].source == s) {
                    auto index = origins_[id].face;
                    auto record =
                        triangle_record(source.link->original, index, original_[id], value);
                    std::copy(record.begin(), record.end(), out.begin() + 8 + index * 72);
                    changed = true;
                }
            if (changed)
                result = replace_asset_resource(result, source.link->path, out);
        }
        return result;
    }
    auto outer = Container::parse(original, "BG");
    require(outer.files.size() >= 6, "Incomplete terrain resource");
    Container walls;
    walls.tag = {'B', 'G'};
    if (outer.files.size() > TargetProfile::terrain_wall_slot &&
        !outer.files[TargetProfile::terrain_wall_slot].empty())
        walls = Container::parse(outer.files[TargetProfile::terrain_wall_slot], "BG");
    for (unsigned role = 0; role < 5; ++role) {
        Bytes input;
        if (role == 0) {
            if (outer.files.size() > TargetProfile::terrain_ground_slot)
                input = outer.files[TargetProfile::terrain_ground_slot];
        } else if (walls.files.size() >= role)
            input = walls.files[role - 1];
        const Source *source = nullptr;
        for (auto &candidate : sources_)
            if (candidate.link->member == member_id && unsigned(candidate.kind) == role) {
                source = &candidate;
                break;
            }
        std::vector<Bytes> records;
        auto count = input.empty() ? 0 : face_count(input);
        for (std::size_t i = 0; i < count; ++i) {
            if (source) {
                auto id = source->first + unsigned(i);
                auto &value = state(id);
                if (!value.deleted && unsigned(value.kind) == role)
                    records.push_back(triangle_record(input, i, original_[id], value));
            } else {
                auto record = slice(input, 8 + i * 72, 72);
                records.emplace_back(record.begin(), record.end());
            }
        }
        for (unsigned id = 0; id < size(); ++id)
            if (member(id) == member_id && !state(id).deleted &&
                (origins_[id].added || unsigned(original_[id].kind) != role) &&
                unsigned(state(id).kind) == role) {
                auto origin = origins_[id];
                if (origin.added) {
                    Bytes raw(80);
                    put32(raw, 0, 0x14120500);
                    put32(raw, 4, 1);
                    CollisionState empty;
                    auto record = triangle_record(raw, 0, empty, state(id));
                    records.push_back(std::move(record));
                } else
                    records.push_back(triangle_record(sources_[origin.source].link->original,
                                                      origin.face, original_[id], state(id)));
            }
        if (input.empty() && records.empty())
            continue;
        Bytes out(8);
        if (input.empty())
            put32(out, 0, 0x14120500);
        else
            std::copy_n(input.begin(), 8, out.begin());
        require(records.size() <= 2000000,
                "Exported collision layer exceeds the supported triangle count");
        put32(out, 4, narrow(records.size()));
        for (auto &record : records)
            append(out, record);
        if (!input.empty())
            append(out, slice(input, 8 + count * 72, input.size() - 8 - count * 72));
        if (out == input)
            continue;
        if (role == 0)
            result = replace_child(result, TargetProfile::terrain_ground_slot, out);
        else {
            auto current = Container::parse(result, "BG");
            Bytes nested;
            if (current.files.size() > TargetProfile::terrain_wall_slot)
                nested = current.files[TargetProfile::terrain_wall_slot];
            nested = replace_child(nested, role - 1, out, 4);
            result = replace_child(result, TargetProfile::terrain_wall_slot, nested);
        }
    }
    return result;
}
void CollisionDocument::export_to(const std::filesystem::path &folder) const {
    require(!preview_, "Finish the current transform before exporting");
    require(changed(), "No collision edits to export");
    auto physical = scene_->archive_sources.resolve(dump_, TargetProfile::terrain_archive);
    Archive archive(physical);
    auto destination = folder / TargetProfile::terrain_archive;
    require(std::filesystem::absolute(destination).lexically_normal() !=
                std::filesystem::absolute(physical).lexically_normal(),
            "Choose a separate override folder, not the source archive");
    std::map<std::size_t, Bytes> members;
    for (auto &[id, value] : edits_) {
        auto m = member(id);
        if (!members.contains(m)) {
            auto original = archive.decoded(m);
            members[m] = compile_member(m, original);
        }
    }
    std::filesystem::create_directories(destination.parent_path());
    archive.export_to(destination, members);
}
}
