#include "authoring/composition_document.h"
#include "core/digest.h"
#include <algorithm>
#include <cmath>
#include <charconv>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>

namespace studio {
namespace {
void valid_transform(const PlacementState &transform) {
    for (float value : transform.position)
        require(std::isfinite(value) && std::abs(value) < 1e7f,
                "Placement position is outside the supported map range");
    require(std::isfinite(transform.turn) && std::abs(transform.turn) <= 360000,
            "Invalid placement heading");
}
std::string source_key(const MapSourceReference &source) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << source.location.archive.generic_string() << ' ' << source.location.member;
    for (auto part : source.location.path)
        out << '/' << part;
    out << ' ' << source.member_hash;
    return out.str();
}
void valid_hash(const std::string &hash) {
    require(hash.size() == 64 && std::all_of(hash.begin(), hash.end(),
                                             [](char c) {
                                                 return (c >= '0' && c <= '9') ||
                                                        (c >= 'a' && c <= 'f');
                                             }),
            "Invalid template fingerprint");
}
}
CompositionDocument::CompositionDocument(MapResourceCatalog catalog, std::string template_hash,
                                         AuthoringGrid grid)
    : catalog_(std::move(catalog)), template_hash_(std::move(template_hash)) {
    valid_hash(template_hash_);
    grid.validate();
    state_.grid = grid;
    saved_ = state_;
    history_.push_back(state_);
}
void CompositionDocument::add_library(const MapResourceCatalog &library) {
    for (const auto &entry : library.entries) {
        auto found = std::find_if(
            catalog_.entries.begin(), catalog_.entries.end(), [&](const auto &existing) {
                return source_key(existing.source) == source_key(entry.source);
            });
        if (found == catalog_.entries.end())
            catalog_.entries.push_back(entry);
    }
}
void CompositionDocument::commit(State next) {
    if (next == state_)
        return;
    history_.resize(cursor_ + 1);
    history_.push_back(next);
    state_ = std::move(next);
    ++cursor_;
    if (history_.size() > 257) {
        history_.erase(history_.begin());
        --cursor_;
    }
}
const ProjectAsset *CompositionDocument::project_asset(std::size_t resource) const {
    if (resource < project_resource_base)
        return nullptr;
    const auto id = resource - project_resource_base;
    for (const auto &asset : state_.assets)
        if (asset.id == id)
            return &asset;
    return nullptr;
}
std::string CompositionDocument::resource_name(std::size_t resource) const {
    if (const auto *asset = project_asset(resource))
        return asset->name;
    return catalog_.entries.at(resource).name;
}
bool CompositionDocument::can_place(std::size_t resource) const {
    return project_asset(resource) ||
           (resource < catalog_.entries.size() && catalog_.entries[resource].reusable_static());
}
std::size_t CompositionDocument::save_project_asset(ProjectAsset asset) {
    validate_project_asset(asset, catalog_);
    auto next = state_;
    if (asset.id) {
        auto found = std::find_if(next.assets.begin(), next.assets.end(), [&](const auto &item) {
            return item.id == asset.id;
        });
        require(found != next.assets.end(), "Project asset no longer exists");
        *found = asset;
    } else {
        require(next_asset_id_ <
                    std::size_t(std::numeric_limits<int>::max()) - catalog_.entries.size(),
                "Project asset identity space is exhausted");
        asset.id = next_asset_id_++;
        next.assets.push_back(asset);
    }
    commit(std::move(next));
    return project_resource(asset.id);
}
std::uint64_t CompositionDocument::add(std::size_t resource, PlacementState transform) {
    require(can_place(resource),
            "Choose a static resource with resolved dependencies; baked scenery needs extraction");
    valid_transform(transform);
    require(next_id_ < std::numeric_limits<std::uint64_t>::max(),
            "Placement identity space is exhausted");
    auto next = state_;
    const auto id = next_id_;
    next.instances.push_back({id, resource, transform});
    commit(std::move(next));
    ++next_id_;
    return id;
}
std::uint64_t CompositionDocument::duplicate(std::uint64_t id, PlacementState transform) {
    auto it =
        std::find_if(state_.instances.begin(), state_.instances.end(), [&](const auto &instance) {
            return instance.id == id;
        });
    require(it != state_.instances.end(), "Placement no longer exists");
    valid_transform(transform);
    require(next_id_ < std::numeric_limits<std::uint64_t>::max(),
            "Placement identity space is exhausted");
    auto copy = *it;
    copy.id = next_id_;
    copy.transform = transform;
    auto next = state_;
    next.instances.push_back(copy);
    commit(std::move(next));
    return next_id_++;
}
void CompositionDocument::transform(std::uint64_t id, PlacementState transform) {
    valid_transform(transform);
    auto next = state_;
    auto it = std::find_if(next.instances.begin(), next.instances.end(), [&](const auto &instance) {
        return instance.id == id;
    });
    require(it != next.instances.end(), "Placement no longer exists");
    it->transform = transform;
    commit(std::move(next));
}
void CompositionDocument::object_collision(std::uint64_t id,
                                           std::optional<ObjectCollisionBox> box) {
    if (box)
        validate_object_collision(*box);
    auto next = state_;
    auto it = std::find_if(next.instances.begin(), next.instances.end(), [&](const auto &instance) {
        return instance.id == id;
    });
    require(it != next.instances.end(), "Placement no longer exists");
    it->collision = box;
    commit(std::move(next));
}
void CompositionDocument::erase(std::uint64_t id) {
    auto next = state_;
    auto size = next.instances.size();
    std::erase_if(next.instances, [&](const auto &instance) {
        return instance.id == id;
    });
    require(next.instances.size() != size, "Placement no longer exists");
    commit(std::move(next));
}
void CompositionDocument::set_grid(AuthoringGrid grid) {
    require(!state_.ground, "Create a new ground surface to change its grid dimensions");
    grid.validate();
    auto next = state_;
    next.grid = grid;
    commit(std::move(next));
}
void CompositionDocument::create_ground(AuthoringGrid grid, float height) {
    grid.validate();
    require(std::size_t(grid.width) * grid.height <= 16384,
            "Ground editing currently supports up to 16,384 cells; reduce the width or height");
    GroundSurface ground;
    ground.heights.assign((std::size_t(grid.width) + 1) * (std::size_t(grid.height) + 1), height);
    ground.textures.resize(std::size_t(grid.width) * grid.height);
    ground.validate(grid);
    auto next = state_;
    next.grid = grid;
    next.ground = std::move(ground);
    commit(std::move(next));
}
void CompositionDocument::apply_ground_brush(const GroundBrushStroke &stroke) {
    require(bool(state_.ground), "Create ground before brushing");
    auto next = state_;
    next.ground = brush_ground(next.grid, *next.ground, stroke);
    commit(std::move(next));
}
void CompositionDocument::paint_ground(AuthoringTile first, AuthoringTile last,
                                       const std::string &texture) {
    require(bool(state_.ground), "Create ground before painting");
    state_.grid.center(first);
    state_.grid.center(last);
    auto next = state_;
    for (int z = std::min(first.z, last.z); z <= std::max(first.z, last.z); ++z)
        for (int x = std::min(first.x, last.x); x <= std::max(first.x, last.x); ++x) {
            const auto cell = std::size_t(z) * next.grid.width + x;
            next.ground->textures[cell] = texture;
            if (!next.ground->blends.empty())
                next.ground->blends[cell] = {};
        }
    next.ground->validate(next.grid);
    commit(std::move(next));
}
void CompositionDocument::blend_ground(AuthoringTile first, AuthoringTile last,
                                       const std::string &texture, float coverage, int direction) {
    require(bool(state_.ground), "Create ground before blending");
    state_.grid.center(first);
    state_.grid.center(last);
    require(std::isfinite(coverage) && coverage >= 0 && coverage <= 1 && direction >= -1 &&
                direction <= 3,
            "Invalid ground blend coverage or direction");
    auto next = state_;
    auto &ground = *next.ground;
    if (ground.blends.empty())
        ground.blends.resize(ground.textures.size());
    const int x0 = std::min(first.x, last.x), x1 = std::max(first.x, last.x) + 1,
              z0 = std::min(first.z, last.z), z1 = std::max(first.z, last.z) + 1;
    for (int z = z0; z < z1; ++z)
        for (int x = x0; x < x1; ++x) {
            const auto cell = std::size_t(z) * next.grid.width + x;
            require(texture.empty() || !ground.textures[cell].empty(),
                    "Paint a base texture on every selected cell before adding a blend");
            auto &blend = ground.blends[cell];
            blend.texture = texture;
            unsigned i = 0;
            for (auto corner : {AuthoringTile{x, z}, AuthoringTile{x, z + 1},
                                AuthoringTile{x + 1, z + 1}, AuthoringTile{x + 1, z}}) {
                float weight = 1;
                if (direction == 0 || direction == 2)
                    weight = float(corner.x - x0) / float(x1 - x0);
                if (direction == 1 || direction == 3)
                    weight = float(corner.z - z0) / float(z1 - z0);
                if (direction >= 2)
                    weight = 1 - weight;
                blend.weights[i++] = texture.empty() ? 0 : weight * coverage;
            }
        }
    ground.validate(next.grid);
    commit(std::move(next));
}
void CompositionDocument::edit_terrain(GroundSurface ground) {
    require(bool(state_.ground), "Create terrain before editing");
    ground.validate(state_.grid);
    auto next = state_;
    next.ground = std::move(ground);
    commit(std::move(next));
}
void CompositionDocument::move_vertex(AuthoringTile vertex, SpatialPoint delta) {
    require(bool(state_.ground), "Create ground before editing vertices");
    auto next = state_;
    next.ground = move_ground_vertex(next.grid, *next.ground, vertex, delta);
    commit(std::move(next));
}
void CompositionDocument::scale_ground_texture(AuthoringTile first, AuthoringTile last,
                                               std::array<float, 2> size) {
    require(bool(state_.ground), "Create ground before sizing textures");
    state_.grid.center(first);
    state_.grid.center(last);
    auto next = state_;
    if (next.ground->texture_scales.empty())
        next.ground->texture_scales.assign(next.ground->textures.size(), {1, 1});
    for (int z = std::min(first.z, last.z); z <= std::max(first.z, last.z); ++z)
        for (int x = std::min(first.x, last.x); x <= std::max(first.x, last.x); ++x)
            next.ground->texture_scales[std::size_t(z) * next.grid.width + x] = size;
    next.ground->validate(next.grid);
    commit(std::move(next));
}
void CompositionDocument::extend_ground(int edge, int cells) {
    require(bool(state_.ground), "Create ground before extending");
    require(state_.ground->triangles.empty(),
            "Use boundary extrusion to extend subdivided terrain");
    require(edge >= 0 && edge < 4 && cells > 0 && cells <= 128,
            "Choose an outer edge and between 1 and 128 new cells");
    auto next = state_;
    const auto old = state_.grid;
    auto &grid = next.grid;
    const auto &source = *state_.ground;
    const bool along_x = edge == 0 || edge == 2;
    const int shift_x = edge == 2 ? cells : 0, shift_z = edge == 3 ? cells : 0;
    grid.width += along_x ? cells : 0;
    grid.height += along_x ? 0 : cells;
    grid.origin[0] -= shift_x * grid.tile_size;
    grid.origin[1] -= shift_z * grid.tile_size;
    grid.validate();
    require(std::size_t(grid.width) * grid.height <= 16384,
            "Extension exceeds 16,384 cells; use a smaller extension");
    GroundSurface ground;
    ground.uv_origin = {source.uv_origin[0] - shift_x, source.uv_origin[1] - shift_z};
    for (int z = 0; z <= grid.height; ++z)
        for (int x = 0; x <= grid.width; ++x) {
            const int sx = std::clamp(x - shift_x, 0, old.width),
                      sz = std::clamp(z - shift_z, 0, old.height);
            const auto index = std::size_t(sz) * (std::size_t(old.width) + 1) + sx;
            ground.heights.push_back(source.heights[index]);
            if (!source.offsets.empty())
                ground.offsets.push_back(source.offsets[index]);
        }
    for (int z = 0; z < grid.height; ++z)
        for (int x = 0; x < grid.width; ++x) {
            const int sx = std::clamp(x - shift_x, 0, old.width - 1),
                      sz = std::clamp(z - shift_z, 0, old.height - 1);
            const auto index = std::size_t(sz) * old.width + sx;
            ground.textures.push_back(source.textures[index]);
            if (!source.texture_scales.empty())
                ground.texture_scales.push_back(source.texture_scales[index]);
            if (!source.blends.empty()) {
                auto blend = source.blends[index];
                if (x - shift_x < 0)
                    blend.weights = {blend.weights[0], blend.weights[1], blend.weights[1],
                                     blend.weights[0]};
                if (x - shift_x >= old.width)
                    blend.weights = {blend.weights[3], blend.weights[2], blend.weights[2],
                                     blend.weights[3]};
                if (z - shift_z < 0)
                    blend.weights = {blend.weights[0], blend.weights[0], blend.weights[3],
                                     blend.weights[3]};
                if (z - shift_z >= old.height)
                    blend.weights = {blend.weights[1], blend.weights[1], blend.weights[2],
                                     blend.weights[2]};
                ground.blends.push_back(blend);
            }
        }
    ground.validate(grid);
    next.ground = std::move(ground);
    commit(std::move(next));
}
void CompositionDocument::tilt_ground(AuthoringTile first, AuthoringTile last, float x_degrees,
                                      float z_degrees) {
    require(bool(state_.ground), "Create ground before tilting");
    auto next = state_;
    next.ground = transform_ground(next.grid, *next.ground, first, last, 0, x_degrees, z_degrees);
    commit(std::move(next));
}
void CompositionDocument::influence_ground_height(AuthoringTile first, AuthoringTile last,
                                                  float value, float surrounding_cells) {
    require(bool(state_.ground), "Create ground before changing its height");
    auto next = state_;
    next.ground = influence_ground(next.grid, *next.ground, first, last, value, surrounding_cells);
    commit(std::move(next));
}
void CompositionDocument::shape_ground(AuthoringTile first, AuthoringTile last, float value,
                                       bool flatten) {
    require(bool(state_.ground), "Create ground before shaping");
    if (flatten && !state_.ground->triangles.empty()) {
        edit_terrain(
            shape_terrain_selection(state_.grid, *state_.ground, first, last, false, value, false));
        return;
    }
    require(std::isfinite(value), "Height must be finite");
    state_.grid.center(first);
    state_.grid.center(last);
    auto next = state_;
    for (int z = std::min(first.z, last.z); z <= std::max(first.z, last.z) + 1; ++z)
        for (int x = std::min(first.x, last.x); x <= std::max(first.x, last.x) + 1; ++x) {
            auto &height =
                next.ground->heights[std::size_t(z) * (std::size_t(next.grid.width) + 1) + x];
            height = flatten ? value : height + value;
        }
    next.ground->validate(next.grid);
    commit(std::move(next));
}
void CompositionDocument::customize_collision(std::vector<CollisionState> faces) {
    require(state_.ground.has_value(), "Create ground before customizing collision");
    require(faces.size() <= 2000000, "Authored collision exceeds the supported triangle count");
    for (auto &face : faces)
        validate_collision_state(face);
    auto next = state_;
    next.custom_collision = std::move(faces);
    commit(std::move(next));
}
void CompositionDocument::follow_terrain_collision() {
    auto next = state_;
    next.custom_collision.reset();
    commit(std::move(next));
}
void CompositionDocument::object_export_settings(bool enabled, unsigned zone) {
    require(zone < 65536, "Invalid object zone group");
    auto next = state_;
    next.stage_objects = enabled;
    next.object_zone = zone;
    commit(std::move(next));
}
void CompositionDocument::ground_export_settings(bool enabled, std::uint32_t attribute) {
    auto next = state_;
    next.stage_ground = enabled;
    next.ground_attribute = attribute;
    commit(std::move(next));
}
void CompositionDocument::replace_terrain(bool enabled) {
    auto next = state_;
    next.replace_terrain = enabled;
    commit(std::move(next));
}
void CompositionDocument::undo() {
    if (can_undo())
        state_ = history_[--cursor_];
}
void CompositionDocument::redo() {
    if (can_redo())
        state_ = history_[++cursor_];
}
std::string CompositionDocument::serialize() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    std::set<unsigned> library_areas;
    for (const auto &instance : state_.instances) {
        const auto *asset = project_asset(instance.resource);
        const auto source = asset ? asset->source : instance.resource;
        if (source != ProjectAsset::no_source && catalog_.entries.at(source).area != catalog_.area)
            library_areas.insert(catalog_.entries.at(source).area);
    }
    for (const auto &asset : state_.assets)
        if (asset.source != ProjectAsset::no_source &&
            catalog_.entries.at(asset.source).area != catalog_.area)
            library_areas.insert(catalog_.entries.at(asset.source).area);
    const unsigned version =
        std::any_of(state_.assets.begin(), state_.assets.end(),
                    [](const auto &a) {
                        return a.source == ProjectAsset::no_source;
                    })
            ? 15
        : std::any_of(state_.assets.begin(), state_.assets.end(),
                      [](const auto &a) {
                          return !a.native_resource.empty();
                      })
            ? 14
        : !library_areas.empty() ? 13
        : state_.replace_terrain ? 12
        : std::any_of(state_.instances.begin(), state_.instances.end(),
                      [](const auto &i) {
                          return i.collision.has_value();
                      })
            ? 11
        : state_.stage_objects || state_.object_zone          ? 10
        : state_.custom_collision                             ? 9
        : state_.stage_ground || state_.ground_attribute != 1 ? 8
        : std::any_of(state_.assets.begin(), state_.assets.end(),
                      [](const auto &a) {
                          return !a.meshes.empty();
                      })
            ? 7
        : state_.ground && !state_.ground->triangles.empty() ? 6
        : !state_.assets.empty()                             ? 5
        : !state_.ground                                     ? 1
        : (!state_.ground->offsets.empty() || !state_.ground->texture_scales.empty() ||
           state_.ground->uv_origin != std::array<float, 2>{})
            ? 4
        : state_.ground->blends.empty() ? 2
                                        : 3;
    const auto &grid = state_.grid;
    out << "USUMSTUDIO_COMPOSITION " << version << "\narea " << catalog_.area << "\ntemplate "
        << template_hash_ << "\ngrid " << grid.origin[0] << ' ' << grid.origin[1] << ' '
        << grid.tile_size << ' ' << grid.width << ' ' << grid.height << '\n';
    for (auto area : library_areas)
        out << "library_area " << area << '\n';
    for (const auto &asset : state_.assets) {
        out << "asset " << asset.id << ' ' << std::quoted(asset.name) << ' '
            << std::quoted(asset.source == ProjectAsset::no_source
                               ? std::string{}
                               : source_key(catalog_.entries.at(asset.source).source));
        for (float value : asset.pivot)
            out << ' ' << value;
        out << ' ' << asset.faces.size();
        for (const auto &faces : asset.faces) {
            out << ' ' << faces.size();
            for (auto face : faces)
                out << ' ' << face;
        }
        out << '\n';
        if (!asset.native_resource.empty()) {
            out << "asset_resource " << asset.id << ' ';
            constexpr char digits[] = "0123456789abcdef";
            for (auto b : asset.native_resource)
                out << digits[b >> 4] << digits[b & 15];
            out << '\n';
        }
        if (!asset.meshes.empty()) {
            out << "asset_mesh " << asset.id << ' ' << asset.meshes.size() << '\n';
            for (const auto &mesh : asset.meshes) {
                out << mesh.vertices.size() << ' ' << mesh.indices.size() << '\n';
                for (const auto &v : mesh.vertices) {
                    for (auto x : v.values)
                        out << x << ' ';
                    out << v.color << '\n';
                }
                for (auto i : mesh.indices)
                    out << i << ' ';
                out << '\n';
            }
        }
    }
    for (const auto &instance : state_.instances) {
        if (const auto *asset = project_asset(instance.resource))
            out << "project_instance " << instance.id << ' ' << asset->id;
        else {
            const auto &entry = catalog_.entries.at(instance.resource);
            out << "instance " << instance.id << ' ' << *entry.static_model << ' '
                << std::quoted(source_key(entry.source));
        }
        for (float value : instance.transform.position)
            out << ' ' << value;
        out << ' ' << instance.transform.turn << '\n';
    }
    for (const auto &instance : state_.instances)
        if (instance.collision) {
            out << "object_collision " << instance.id;
            for (auto v : instance.collision->offset)
                out << ' ' << v;
            for (auto v : instance.collision->size)
                out << ' ' << v;
            out << '\n';
        }
    if (state_.ground) {
        const auto &ground = *state_.ground;
        out << "ground\nheights";
        for (float height : ground.heights)
            out << ' ' << height;
        out << "\ntextures";
        for (const auto &texture : ground.textures)
            out << ' ' << std::quoted(texture);
        out << "\n";
        if (version >= 3) {
            out << "blends";
            for (std::size_t cell = 0; cell < ground.textures.size(); ++cell) {
                const auto blend = ground.blends.empty() ? GroundBlend{} : ground.blends[cell];
                out << ' ' << std::quoted(blend.texture);
                for (float weight : blend.weights)
                    out << ' ' << weight;
            }
            out << "\n";
        }
        if (version >= 4) {
            out << "offsets";
            for (std::size_t i = 0; i < ground.heights.size(); ++i) {
                const auto offset =
                    ground.offsets.empty() ? std::array<float, 2>{} : ground.offsets[i];
                out << ' ' << offset[0] << ' ' << offset[1];
            }
            out << "\ntexture_sizes";
            for (std::size_t i = 0; i < ground.textures.size(); ++i) {
                const auto size = ground.texture_scales.empty() ? std::array<float, 2>{1, 1}
                                                                : ground.texture_scales[i];
                out << ' ' << size[0] << ' ' << size[1];
            }
            out << "\nuv_origin " << ground.uv_origin[0] << ' ' << ground.uv_origin[1] << '\n';
        }
    }
    if (version >= 6 && state_.ground && !state_.ground->triangles.empty()) {
        out << "terrain_points " << state_.ground->points.size() << '\n';
        for (const auto &p : state_.ground->points) {
            for (auto i : p.parents)
                out << i << ' ';
            for (auto w : p.weights)
                out << w << ' ';
            for (auto v : p.offset)
                out << v << ' ';
            for (auto v : p.texture_offset)
                out << v << ' ';
            out << '\n';
        }
        out << "terrain_faces " << state_.ground->triangles.size() << '\n';
        for (const auto &t : state_.ground->triangles)
            out << t.cell << ' ' << t.vertices[0] << ' ' << t.vertices[1] << ' ' << t.vertices[2]
                << '\n';
    }
    if (version >= 10)
        out << "game_objects " << state_.stage_objects << ' ' << state_.object_zone << '\n';
    if (version >= 8) {
        out << "game_ground " << state_.stage_ground << ' ' << state_.ground_attribute;
        if (version >= 12)
            out << ' ' << state_.replace_terrain;
        out << '\n';
    }
    if (state_.custom_collision) {
        out << "custom_collision " << state_.custom_collision->size() << '\n';
        for (auto &face : *state_.custom_collision) {
            out << unsigned(face.kind) << ' ' << face.attribute;
            for (auto p : face.vertices)
                for (auto v : p)
                    out << ' ' << v;
            out << '\n';
        }
    }
    out << "end\n";
    return out.str();
}
void CompositionDocument::restore(const std::string &text) {
    require(text.size() <= 32 * 1024 * 1024, "Composition exceeds the 32 MiB document read limit");
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    std::string word, hash;
    unsigned version = 0, area = 0;
    State next;
    require(bool(in >> word >> version) && word == "USUMSTUDIO_COMPOSITION" &&
                (version >= 1 && version <= 15),
            "Unsupported composition document");
    require(bool(in >> word >> area) && word == "area" && area == catalog_.area,
            "Load the composition's template area before opening it");
    require(bool(in >> word >> hash) && word == "template" && hash == template_hash_,
            "Template sources changed; reopen with the matching original dump");
    auto &grid = next.grid;
    require(bool(in >> word >> grid.origin[0] >> grid.origin[1] >> grid.tile_size >> grid.width >>
                 grid.height) &&
                word == "grid",
            "Invalid composition grid");
    grid.validate();
    std::set<std::uint64_t> identities;
    std::uint64_t next_id = 1;
    std::size_t next_asset_id = 1;
    bool ended = false, export_seen = false, objects_seen = false;
    std::set<unsigned> library_areas;
    while (in >> word) {
        if (word == "library_area") {
            unsigned source_area;
            require(version >= 13 && bool(in >> source_area) &&
                        library_areas.insert(source_area).second &&
                        std::any_of(catalog_.entries.begin(), catalog_.entries.end(),
                                    [&](const auto &entry) {
                                        return entry.area == source_area;
                                    }),
                    "Source map library is unavailable or duplicated");
            continue;
        }
        if (word == "object_collision") {
            std::uint64_t id;
            ObjectCollisionBox box;
            require(version >= 11 && bool(in >> id), "Invalid object collision identity");
            for (auto &v : box.offset)
                require(bool(in >> v), "Invalid object collision offset");
            for (auto &v : box.size)
                require(bool(in >> v), "Invalid object collision size");
            validate_object_collision(box);
            auto it =
                std::find_if(next.instances.begin(), next.instances.end(), [&](const auto &i) {
                    return i.id == id;
                });
            require(it != next.instances.end() && !it->collision,
                    "Object collision is duplicated or has no placement");
            it->collision = box;
            continue;
        }
        if (word == "game_objects") {
            unsigned enabled;
            require(version >= 10 && !objects_seen && bool(in >> enabled >> next.object_zone) &&
                        enabled <= 1 && next.object_zone < 65536,
                    "Invalid object export settings");
            next.stage_objects = enabled != 0;
            objects_seen = true;
            continue;
        }
        if (word == "game_ground") {
            unsigned enabled;
            require(version >= 8 && !export_seen && bool(in >> enabled >> next.ground_attribute) &&
                        enabled <= 1,
                    "Invalid ground export settings");
            next.stage_ground = enabled != 0;
            if (version >= 12) {
                unsigned replacement;
                require(bool(in >> replacement) && replacement <= 1,
                        "Invalid terrain replacement setting");
                next.replace_terrain = replacement != 0;
            }
            export_seen = true;
            continue;
        }
        if (word == "custom_collision") {
            std::size_t count;
            require(version >= 9 && !next.custom_collision && bool(in >> count) && count <= 2000000,
                    "Invalid custom collision block");
            next.custom_collision.emplace(count);
            for (auto &face : *next.custom_collision) {
                unsigned kind;
                require(bool(in >> kind >> face.attribute) && kind < 5,
                        "Invalid custom collision type");
                face.kind = SpatialKind(kind);
                for (auto &p : face.vertices)
                    for (auto &v : p)
                        require(bool(in >> v), "Truncated custom collision triangle");
                validate_collision_state(face);
            }
            continue;
        }
        if (word == "end") {
            ended = true;
            break;
        }
        if (word == "terrain_points") {
            require(version >= 6 && next.ground && next.ground->triangles.empty(),
                    "Unexpected terrain topology");
            std::size_t count = 0;
            require(bool(in >> count) && count <= 60000, "Invalid terrain point count");
            next.ground->points.resize(count);
            for (auto &p : next.ground->points) {
                for (auto &i : p.parents)
                    require(bool(in >> i), "Invalid terrain parent");
                for (auto &w : p.weights)
                    require(bool(in >> w), "Invalid terrain weight");
                for (auto &v : p.offset)
                    require(bool(in >> v), "Invalid terrain offset");
                for (auto &v : p.texture_offset)
                    require(bool(in >> v), "Invalid terrain texture offset");
            }
            require(bool(in >> word >> count) && word == "terrain_faces" && count > 0 &&
                        count <= 120000,
                    "Invalid terrain face count");
            next.ground->triangles.resize(count);
            for (auto &t : next.ground->triangles)
                require(bool(in >> t.cell >> t.vertices[0] >> t.vertices[1] >> t.vertices[2]),
                        "Invalid terrain face");
            next.ground->validate(grid);
            continue;
        }
        if (word == "asset") {
            require(version >= 5, "Unexpected project asset");
            ProjectAsset asset;
            std::string source;
            std::size_t sections = 0;
            require(bool(in >> asset.id >> std::quoted(asset.name) >> std::quoted(source) >>
                         asset.pivot[0] >> asset.pivot[1] >> asset.pivot[2] >> sections),
                    "Malformed project asset");
            require(asset.id > 0 &&
                        asset.id < std::size_t(std::numeric_limits<int>::max()) -
                                       catalog_.entries.size() &&
                        sections > 0 && sections <= 65536,
                    "Invalid project asset identity or section count");
            require(std::none_of(next.assets.begin(), next.assets.end(),
                                 [&](const auto &item) {
                                     return item.id == asset.id;
                                 }),
                    "Repeated project asset identity");
            auto found = std::find_if(catalog_.entries.begin(), catalog_.entries.end(),
                                      [&](const auto &entry) {
                                          return source_key(entry.source) == source;
                                      });
            const bool portable = version >= 15 && source.empty();
            require(portable || found != catalog_.entries.end(),
                    "Project asset source is missing or changed");
            asset.source =
                portable ? ProjectAsset::no_source : std::size_t(found - catalog_.entries.begin());
            asset.faces.resize(sections);
            std::size_t total = 0;
            for (auto &faces : asset.faces) {
                std::size_t count = 0;
                require(bool(in >> count) && count <= 4000000 - total,
                        "Invalid project asset face count");
                total += count;
                faces.resize(count);
                for (auto &face : faces)
                    require(bool(in >> face), "Truncated asset faces");
            }
            if (!portable)
                validate_project_asset(asset, catalog_);
            next_asset_id = std::max(next_asset_id, asset.id + 1);
            next.assets.push_back(std::move(asset));
            continue;
        }
        if (word == "asset_resource") {
            std::size_t id;
            std::string encoded;
            require(version >= 14 && bool(in >> id >> encoded) && !next.assets.empty() &&
                        next.assets.back().id == id && next.assets.back().native_resource.empty() &&
                        encoded.size() % 2 == 0 && encoded.size() <= 512u * 1024 * 1024,
                    "Invalid project asset resource block");
            auto digit = [](char c) -> unsigned {
                require((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'),
                        "Invalid asset resource encoding");
                return c <= '9' ? c - '0' : c - 'a' + 10;
            };
            auto &bytes = next.assets.back().native_resource;
            bytes.reserve(encoded.size() / 2);
            for (std::size_t i = 0; i < encoded.size(); i += 2)
                bytes.push_back(std::uint8_t(digit(encoded[i]) * 16 + digit(encoded[i + 1])));
            project_asset_resource_preview(bytes);
            continue;
        }
        if (word == "asset_mesh") {
            std::size_t id = 0, count = 0;
            require(version >= 7 && bool(in >> id >> count) && !next.assets.empty() &&
                        next.assets.back().id == id && next.assets.back().meshes.empty() &&
                        count == next.assets.back().faces.size(),
                    "Invalid edited asset mesh block");
            auto &asset = next.assets.back();
            asset.meshes.resize(count);
            std::size_t total = 0;
            for (auto &mesh : asset.meshes) {
                std::size_t vertices = 0, indices = 0;
                require(bool(in >> vertices >> indices) && vertices <= 65536 && indices % 3 == 0 &&
                            indices <= 12000000 - total,
                        "Invalid edited mesh counts");
                total += indices;
                mesh.vertices.resize(vertices);
                mesh.indices.resize(indices);
                for (auto &v : mesh.vertices) {
                    for (auto &x : v.values)
                        require(bool(in >> x), "Invalid authored vertex");
                    require(bool(in >> v.color), "Invalid authored color");
                }
                for (auto &index : mesh.indices)
                    require(bool(in >> index), "Invalid authored triangle");
            }
            validate_project_asset(asset, catalog_);
            continue;
        }
        if (word == "project_instance") {
            CompositionInstance instance;
            std::size_t asset_id = 0;
            require(version >= 5 &&
                        bool(in >> instance.id >> asset_id >> instance.transform.position[0] >>
                             instance.transform.position[1] >> instance.transform.position[2] >>
                             instance.transform.turn),
                    "Malformed project placement");
            require(instance.id > 0 && instance.id < std::numeric_limits<std::uint64_t>::max() &&
                        identities.insert(instance.id).second,
                    "Repeated or invalid placement identity");
            valid_transform(instance.transform);
            require(std::any_of(next.assets.begin(), next.assets.end(),
                                [&](const auto &asset) {
                                    return asset.id == asset_id;
                                }),
                    "Project placement asset is missing");
            instance.resource = project_resource(asset_id);
            next_id = std::max(next_id, instance.id + 1);
            next.instances.push_back(instance);
            continue;
        }
        if (word == "ground") {
            require(version >= 2 && !next.ground, "Unexpected or repeated ground surface");
            require(std::size_t(grid.width) * grid.height <= 16384,
                    "Ground editing currently supports up to 16,384 cells");
            GroundSurface ground;
            require(bool(in >> word) && word == "heights", "Missing ground heights");
            ground.heights.resize((std::size_t(grid.width) + 1) * (std::size_t(grid.height) + 1));
            for (auto &height : ground.heights)
                require(bool(in >> height), "Invalid or truncated ground heights");
            require(bool(in >> word) && word == "textures", "Missing ground textures");
            ground.textures.resize(std::size_t(grid.width) * grid.height);
            for (auto &texture : ground.textures)
                require(bool(in >> std::quoted(texture)), "Invalid or truncated ground textures");
            if (version >= 3) {
                require(bool(in >> word) && word == "blends", "Missing ground blends");
                ground.blends.resize(ground.textures.size());
                for (auto &blend : ground.blends) {
                    require(bool(in >> std::quoted(blend.texture)), "Invalid blend texture");
                    for (auto &weight : blend.weights)
                        require(bool(in >> weight), "Invalid or truncated blend weights");
                }
            }
            if (version >= 4) {
                require(bool(in >> word) && word == "offsets", "Missing vertex offsets");
                ground.offsets.resize(ground.heights.size());
                for (auto &offset : ground.offsets)
                    require(bool(in >> offset[0] >> offset[1]), "Invalid vertex offsets");
                require(bool(in >> word) && word == "texture_sizes", "Missing texture sizes");
                ground.texture_scales.resize(ground.textures.size());
                for (auto &size : ground.texture_scales)
                    require(bool(in >> size[0] >> size[1]), "Invalid texture sizes");
                require(bool(in >> word >> ground.uv_origin[0] >> ground.uv_origin[1]) &&
                            word == "uv_origin",
                        "Invalid ground UV origin");
            }
            if (version < 6)
                ground.validate(grid);
            next.ground = std::move(ground);
            continue;
        }
        CompositionInstance instance;
        unsigned model = 0;
        std::string source, identity;
        require(word == "instance" &&
                    bool(in >> identity >> model >> std::quoted(source) >>
                         instance.transform.position[0] >> instance.transform.position[1] >>
                         instance.transform.position[2] >> instance.transform.turn),
                "Malformed composition placement");
        auto parsed =
            std::from_chars(identity.data(), identity.data() + identity.size(), instance.id);
        require(parsed.ec == std::errc{} && parsed.ptr == identity.data() + identity.size(),
                "Invalid placement identity");
        require(instance.id > 0 && instance.id < std::numeric_limits<std::uint64_t>::max() &&
                    identities.insert(instance.id).second,
                "Repeated or invalid placement identity");
        valid_transform(instance.transform);
        auto found =
            std::find_if(catalog_.entries.begin(), catalog_.entries.end(), [&](const auto &entry) {
                return entry.static_model == model && source_key(entry.source) == source;
            });
        require(found != catalog_.entries.end() && found->reusable_static(),
                "Composition resource is missing, changed, or unsupported");
        instance.resource = std::size_t(found - catalog_.entries.begin());
        next_id = std::max(next_id, instance.id + 1);
        next.instances.push_back(instance);
    }
    require(!next.custom_collision || next.ground.has_value(),
            "Custom collision needs authored ground");
    if (next.ground)
        next.ground->validate(grid);
    require(version != 6 || (next.ground && !next.ground->triangles.empty()),
            "Terrain mesh is missing");
    in >> std::ws;
    require(version == 1 || version >= 5 || bool(next.ground),
            "Ground composition is missing its ground surface");
    require(ended && in.eof(), "Composition is truncated or contains unexpected trailing data");
    for (const auto &asset : next.assets)
        validate_project_asset(asset, catalog_);
    state_ = std::move(next);
    saved_ = state_;
    history_ = {state_};
    cursor_ = 0;
    next_id_ = next_id;
    next_asset_id_ = next_asset_id;
}
void CompositionDocument::save(const std::filesystem::path &path) {
    auto text = serialize();
    require(text.size() <= 32 * 1024 * 1024,
            "Composition exceeds the 32 MiB document limit; reduce extracted assets before saving");
    write_file_atomic(path, View(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()));
    mark_saved();
}
MapResourceCatalog load_composition_catalog(const std::filesystem::path &dump, unsigned area,
                                            const std::string &document, std::atomic_bool *cancel) {
    require(document.size() <= 32 * 1024 * 1024, "Composition exceeds the document read limit");
    auto catalog = load_map_resources(dump, area, cancel);
    std::set<unsigned> areas{area};
    std::istringstream lines(document);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string key;
        unsigned source;
        if (fields >> key && key == "library_area") {
            require(bool(fields >> source), "Invalid source map library");
            if (areas.insert(source).second) {
                auto library = load_map_resources(dump, source, cancel);
                catalog.entries.insert(catalog.entries.end(), library.entries.begin(),
                                       library.entries.end());
            }
        }
    }
    return catalog;
}
std::string map_template_fingerprint(const std::filesystem::path &dump,
                                     const MapResourceCatalog &catalog) {
    Archive field(dump / GameProfile::field_archive(dump));
    require(catalog.area < field.size() / TargetProfile::area_stride, "Invalid template area");
    std::map<std::pair<std::filesystem::path, std::size_t>, std::string> members;
    for (std::size_t slot = 0; slot < TargetProfile::area_stride; ++slot) {
        const auto member = catalog.area * TargetProfile::area_stride + slot;
        members[{GameProfile::field_archive(dump), member}] = sha256(field.decoded(member));
    }
    for (const auto &entry : catalog.entries) {
        if (entry.area != catalog.area)
            continue;
        const auto add = [&](const MapSourceReference &source) {
            auto key = std::make_pair(source.location.archive, source.location.member);
            auto [it, added] = members.emplace(key, source.member_hash);
            require(added || it->second == source.member_hash,
                    "Dump changed while building the catalog; reload the template");
        };
        add(entry.source);
        for (const auto &dependency : entry.dependencies)
            add(dependency.source);
    }
    for (const auto &[key, hash] : members)
        if (key.first != GameProfile::field_archive(dump))
            require(sha256(Archive(dump / key.first).decoded(key.second)) == hash,
                    "Terrain changed while building the template; reload it");
    std::ostringstream text;
    text << "Field template " << catalog.area << '\n';
    for (const auto &[key, hash] : members)
        text << key.first.generic_string() << ' ' << key.second << ' ' << hash << '\n';
    auto bytes = text.str();
    return sha256(View(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()));
}
}
