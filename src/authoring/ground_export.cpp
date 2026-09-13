#include "authoring/ground_export.h"
#include "compiler/terrain.h"
#include "assets/material_document.h"
#include "formats/compression.h"
#include "audio/audio_document.h"
#include <algorithm>
#include <cmath>
#include <set>
namespace studio {
std::vector<CollisionFace> ground_collision_faces(const AuthoringGrid &grid,
                                                  const GroundSurface &ground) {
    ground.validate(grid);
    auto positions = terrain_positions(grid, ground);
    std::vector<CollisionFace> result;
    for (auto &triangle : terrain_triangles(grid, ground)) {
        CollisionFace face;
        for (unsigned i = 0; i < 3; ++i)
            face[i] = positions.at(triangle.vertices[i]);
        const double x1 = double(face[1][0]) - face[0][0], z1 = double(face[1][2]) - face[0][2],
                     x2 = double(face[2][0]) - face[0][0], z2 = double(face[2][2]) - face[0][2];
        require(z1 * x2 - x1 * z2 > 1e-6,
                "Ground export needs upward-facing surfaces. Remove vertical walls, overhangs or "
                "reversed faces from this ground patch.");
        result.push_back(face);
    }
    require(!result.empty(), "There are no ground triangles to export");
    return result;
}
std::vector<CollisionState> composition_collision(const CompositionDocument &document) {
    if (document.custom_collision())
        return *document.custom_collision();
    require(document.ground().has_value(), "Create authored ground first");
    std::vector<CollisionState> result;
    for (auto &face : ground_collision_faces(document.grid(), *document.ground()))
        result.push_back({face, document.ground_attribute(), SpatialKind::Ground, false});
    return result;
}
std::shared_ptr<Environment> authored_collision_scene(const Environment &context,
                                                      const std::vector<CollisionState> &faces) {
    auto result = std::make_shared<Environment>(context);
    std::set<std::size_t> members;
    for (auto &region : context.spatial.regions)
        if (region.collision_source)
            members.insert(region.collision_source->member);
    require(members.size() == 1, "Authored collision editing currently needs one terrain resource");
    result->spatial.regions.clear();
    for (unsigned kind = 0; kind < 5; ++kind) {
        Bytes bytes(8);
        put32(bytes, 0, 0x14120500);
        std::map<std::size_t, CollisionFace> vertices;
        for (auto &face : faces) {
            validate_collision_state(face);
            if (unsigned(face.kind) != kind)
                continue;
            auto index = vertices.size();
            vertices[index] = face.vertices;
            bytes.resize(8 + vertices.size() * 72);
            put32(bytes, 8 + index * 72 + 64, face.attribute);
        }
        put32(bytes, 4, narrow(vertices.size()));
        bytes = edit_collision_faces(bytes, vertices);
        auto link = std::make_shared<CollisionSource>();
        link->archive = TargetProfile::terrain_archive;
        link->member = *members.begin();
        link->path = kind ? std::vector<std::size_t>{TargetProfile::terrain_wall_slot, kind - 1}
                          : std::vector<std::size_t>{TargetProfile::terrain_ground_slot};
        link->original = bytes;
        auto before = result->spatial.regions.size();
        decode_collision_mesh(result->spatial, bytes, SpatialKind(kind), "Authored collision",
                              link);
        if (result->spatial.regions.size() == before) {
            SpatialRegion region;
            region.kind = SpatialKind(kind);
            region.collision_source = link;
            result->spatial.regions.push_back(std::move(region));
        }
    }
    return result;
}
GroundExportResult compile_ground_patch(const std::filesystem::path &dump,
                                        const CompositionDocument &document,
                                        std::uint32_t attribute) {
    require(document.ground().has_value(), "Create authored ground before enabling ground export");
    auto faces = ground_collision_faces(document.grid(), *document.ground());
    auto custom = composition_collision(document);
    auto scene = std::make_shared<Environment>(load_environment(dump, document.catalog().area));
    Archive field(dump / GameProfile::field_archive(dump)),
        terrain(dump / TargetProfile::terrain_archive);
    std::set<unsigned> ids;
    auto members = [&](unsigned area) {
        auto layout = Container::parse(
            field.decoded(area * TargetProfile::area_stride + TargetProfile::terrain_layout_slot),
            "TR");
        require(layout.files.size() == 1, "Ground export does not support terrain layout variants");
        auto &g = layout.files[0];
        require(u32(g, 16) == 0, "Ground export does not support terrain replacement variants");
        std::set<unsigned> found;
        auto count = std::uint64_t(u32(g, 0)) * u32(g, 4);
        require(count <= 65536, "Invalid terrain layout");
        for (std::size_t i = 0; i < count; ++i) {
            auto id = u16(g, 20 + i * TargetProfile::terrain_cell_size);
            if (id != 65535)
                found.insert(id);
        }
        return found;
    };
    ids = members(document.catalog().area);
    require(ids.size() == 1, "Ground export requires a map with one terrain resource. "
                             "Choose a map such as Berry Fields.");
    const auto member = *ids.begin();
    for (unsigned area = 0; area < field.size() / TargetProfile::area_stride; ++area)
        if (area != document.catalog().area) {
            auto layout = Container::parse(field.decoded(area * TargetProfile::area_stride +
                                                         TargetProfile::terrain_layout_slot),
                                           "TR");
            for (auto &g : layout.files) {
                auto count = std::uint64_t(u32(g, 0)) * u32(g, 4);
                require(count <= 65536, "Invalid terrain layout while checking shared resources");
                for (std::size_t i = 0; i < count; ++i)
                    require(u16(g, 20 + i * TargetProfile::terrain_cell_size) != member,
                            "This terrain resource is shared with another area; independent "
                            "terrain allocation is required before export");
            }
        }
    if (!document.replaces_terrain())
        for (auto &face : faces)
            for (auto &p : face) {
                auto hit = pick_authoring_ground(scene->spatial, {p[0], p[1] + 100000, p[2]},
                                                 {0, -1, 0}, p[1] + 100000);
                require(hit.has_value(),
                        "Keep the ground patch over the template's existing walkable ground");
                require(
                    p[1] >= (*hit)[1] - .01f,
                    "Adding a ground patch retains the original floor. Raise the patch "
                    "to the original ground or above it.");
            }
    auto ground = ground_preview(*scene, document.grid(), *document.ground(),
                                 load_ground_textures(dump, *scene));
    std::vector<TerrainExportMesh> meshes;
    for (auto &draw : ground.draws) {
        auto &surface = ground.materials.at(draw.material);
        TerrainExportMesh mesh;
        mesh.name = "authored_ground_" + std::to_string(meshes.size());
        auto texture = [&](const std::string &key) {
            require(key.starts_with("terrain/") && scene->texture_sources.contains(key),
                    "Paint every cell using the source map's terrain textures before export");
            return key.substr(8);
        };
        mesh.base_texture = texture(surface.texture_inputs[0]);
        if (surface.texture_count > 1)
            mesh.overlay_texture = texture(surface.texture_inputs[1]);
        for (auto &v : draw.vertices)
            mesh.vertices.push_back({{v.x, v.y, v.z}, {v.nx, v.ny, v.nz}, {v.u, v.v}, v.color});
        mesh.indices = draw.indices;
        meshes.push_back(std::move(mesh));
    }
    auto original = terrain.decoded(member);
    Bytes model_bytes;
    std::vector<std::size_t> model_path;
    std::string failure = "No compatible two-texture terrain material in this map";
    for (auto &draw : scene->draws) {
        if (!model_bytes.empty())
            break;
        if (!draw.source || draw.source->archive != TargetProfile::terrain_archive ||
            draw.source->member != member)
            continue;
        auto &material = scene->materials.at(draw.material);
        auto &mix = material.combiner.stages[0];
        if (material.vertex_shader != "Default" || material.generated_lighting_color ||
            material.unsupported_mapping || material.texture_count != 2 ||
            material.inputs[0].source != 0 || material.inputs[1].source != 0 ||
            mix.operation[0] != 4 || mix.color_sources[0] != 3 || mix.color_sources[1] != 4 ||
            mix.color_sources[2] != 0 || mix.color_operands[2] != 2)
            continue;
        auto model = Model::parse(asset_resource(original, draw.source->path));
        for (auto &vertex : scene->draws) {
            if (!vertex.source || vertex.source->archive != TargetProfile::terrain_archive ||
                vertex.source->member != member)
                continue;
            try {
                auto geometry = Model::parse(asset_resource(original, vertex.source->path));
                model_bytes = append_terrain_meshes(model, material.name, geometry, vertex.mesh,
                                                    meshes, document.replaces_terrain());
                model_path = draw.source->path;
                break;
            } catch (const std::exception &e) {
                failure = e.what();
            }
        }
    }
    require(!model_bytes.empty(), "No compatible terrain export preset: " + failure);
    CollisionDocument collision(scene, document.catalog().area, dump);
    std::vector<CollisionAddition> additions;
    for (auto face : custom) {
        if (!document.custom_collision())
            face.attribute = attribute;
        additions.push_back({member, face});
    }
    std::map<unsigned, CollisionState> removed;
    if (document.replaces_terrain())
        for (unsigned i = 0; i < collision.size(); ++i) {
            auto face = collision.state(i);
            face.deleted = true;
            removed.emplace(i, face);
        }
    collision.replace_faces(removed, additions);
    auto compiled = collision.compile_member(member, original);
    if (document.replaces_terrain()) {
        require(model_path.size() == 1,
                "Terrain replacement needs a direct terrain model resource");
        auto block = Container::parse(compiled, "BG");
        for (unsigned i = 0; i < 6; ++i)
            block.files.at(i).clear();
        block.files.at(model_path.front()) = model_bytes;
        compiled = block.write(16);
    } else
        compiled = replace_asset_resource(compiled, model_path, model_bytes);
    return {member, faces.size(), std::move(original), std::move(compiled)};
}
void export_ground_patch(const std::filesystem::path &dump, const CompositionDocument &document,
                         std::uint32_t attribute, const std::filesystem::path &output) {
    require_audio_output(dump, output);
    auto result = compile_ground_patch(dump, document, attribute);
    Archive archive(dump / TargetProfile::terrain_archive);
    auto raw = archive.raw(result.member);
    auto stored = !raw.empty() && raw.front() == 0x11 ? compress(result.compiled) : result.compiled;
    require(decompress(stored) == result.compiled, "Ground export compression verification failed");
    auto file = output / TargetProfile::terrain_archive;
    std::filesystem::create_directories(file.parent_path());
    archive.export_to(file, {{result.member, stored}});
    if (document.replaces_terrain()) {
        const auto relative = GameProfile::field_archive(dump);
        Archive field(dump / relative);
        const auto member = document.catalog().area * TargetProfile::area_stride +
                            TargetProfile::background_resource_slot;
        auto background = Container::parse(field.decoded(member), "BG");
        if (!background.files.at(4).empty()) {
            background.files[4].clear();
            const auto original = field.raw(member);
            auto bytes = background.write(16);
            if (!original.empty() && original.front() == 0x11)
                bytes = compress(bytes);
            const auto target = output / relative;
            std::filesystem::create_directories(target.parent_path());
            field.export_to(target, {{member, std::move(bytes)}});
        }
    }
}
}
