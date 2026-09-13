#include "field/collision_document.h"
#include "field/collision_surfaces.h"
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
namespace studio {
namespace {
const char *roles[] = {"ground", "wall", "surf", "ride", "mudsdale"};
std::uint64_t number(const std::string &word) {
    std::uint64_t result = 0;
    auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), result);
    require(error == std::errc{} && end == word.data() + word.size(),
            "Invalid collision OBJ identifier");
    return result;
}
std::string material(const CollisionState &state) {
    return "collision_" + std::string(roles[unsigned(state.kind)]) + "_attr_" +
           std::to_string(state.attribute);
}
CollisionState parse_material(const std::string &name) {
    for (unsigned role = 0; role < 5; ++role) {
        auto prefix = "collision_" + std::string(roles[role]) + "_attr_";
        if (name.starts_with(prefix)) {
            auto attribute = number(name.substr(prefix.size()));
            require(attribute <= 0xffffffffu, "Collision attribute exceeds 32 bits");
            CollisionState state;
            state.kind = SpatialKind(role);
            state.attribute = std::uint32_t(attribute);
            return state;
        }
    }
    throw std::runtime_error(
        "Assign a collision_TYPE_attr_NUMBER material before exporting from Blender");
}
View bytes(const std::string &text) {
    return View(reinterpret_cast<const std::uint8_t *>(text.data()), text.size());
}
}
void CollisionDocument::export_obj(const std::filesystem::path &path, bool reference) const {
    require(!preview_, "Finish the current transform before exporting");
    require(!sources_.empty(), "This area has no collision terrain resources to export");
    require(path.extension() == ".obj", "Choose an .obj filename");
    auto reference_path = path.parent_path() / (path.stem().string() + "-reference.obj");
    if (reference)
        require(!std::filesystem::exists(reference_path),
                "Choose a new filename; map reference geometry already exists");
    auto mtl = path;
    mtl.replace_extension(".mtl");
    auto patch = path;
    patch.replace_extension(".usum-collision");
    for (auto &file : {path, mtl, patch})
        require(!std::filesystem::exists(file),
                "Choose a new OBJ filename; its OBJ, MTL or baseline patch already exists");
    std::ostringstream obj, materials;
    obj << std::setprecision(9) << "# usum_collision 1\n# baseline " << exchange_signature()
        << "\nmtllib " << mtl.filename().string() << '\n';
    materials << std::setprecision(9);
    for (unsigned attribute = 0; attribute < collision_surfaces.size(); ++attribute)
        obj << "# surface_name " << attribute << ' ' << collision_surfaces[attribute] << '\n';
    std::ostringstream visual;
    visual << std::setprecision(9) << "# USUMStudio map reference; game Y-up coordinates\n";
    std::size_t vertex_base = 1, draw_id = 0;
    if (reference) {
        obj << "# reference " << reference_path.filename().string() << '\n';
        for (auto &draw : scene_->draws) {
            if (draw.character || draw.player >= 0 || draw.sky_part >= 0 || draw.weather_mask ||
                draw.indices.empty())
                continue;
            visual << "o map_reference_" << draw_id++ << '\n';
            auto transform = draw.placement >= 0 ? scene_->placement_transforms.at(draw.placement)
                                                 : pose_identity();
            for (auto &v : draw.vertices) {
                std::array<float, 3> point{v.x, v.y, v.z};
                visual << "v";
                for (unsigned row = 0; row < 3; ++row) {
                    float value = transform[row * 4 + 3];
                    for (unsigned column = 0; column < 3; ++column)
                        value += transform[row * 4 + column] * point[column];
                    require(std::isfinite(value), "Invalid map reference coordinate");
                    visual << ' ' << value;
                }
                visual << '\n';
            }
            require(draw.indices.size() % 3 == 0, "Map reference has incomplete triangles");
            for (std::size_t i = 0; i < draw.indices.size(); i += 3) {
                visual << "f";
                for (unsigned corner = 0; corner < 3; ++corner) {
                    auto index = draw.indices[i + corner];
                    require(index < draw.vertices.size(), "Map reference index is out of range");
                    visual << ' ' << vertex_base + index;
                }
                visual << '\n';
            }
            vertex_base += draw.vertices.size();
        }
    }
    std::map<std::string, CollisionState> palette;
    for (unsigned attribute = 0; attribute < collision_surfaces.size(); ++attribute) {
        CollisionState state;
        state.attribute = attribute;
        palette[material(state)] = state;
    }
    for (unsigned kind = 1; kind < 5; ++kind) {
        CollisionState state;
        state.kind = SpatialKind(kind);
        palette[material(state)] = state;
    }
    std::set<std::size_t> members;
    for (auto &source : sources_)
        members.insert(source.link->member);
    std::size_t index = 1;
    for (auto terrain : members) {
        obj << "o terrain_" << terrain << '\n';
        for (unsigned id = 0; id < size(); ++id)
            if (live(id) && member(id) == terrain) {
                auto &value = state(id);
                palette[material(value)] = value;
                obj << "g collision_" << face_token(id) << "\nusemtl " << material(value) << '\n';
                for (auto p : value.vertices)
                    obj << "v " << p[0] << ' ' << p[1] << ' ' << p[2] << '\n';
                obj << "f " << index << ' ' << index + 1 << ' ' << index + 2 << '\n';
                index += 3;
            }
    }
    obj << "# complete\n";
    for (auto &[name, value] : palette) {
        auto color = collision_surface_color(value.attribute);
        materials << "newmtl " << name << "\nKd " << color[0] << ' ' << color[1] << ' ' << color[2]
                  << "\nd 1\nillum 1\n\n";
    }
    auto geometry = obj.str(), colors = materials.str(), baseline = serialize();
    std::vector<std::filesystem::path> written;
    try {
        if (reference) {
            auto reference_text = visual.str();
            write_new_file(reference_path, bytes(reference_text));
            written.push_back(reference_path);
        }
        write_new_file(mtl, bytes(colors));
        written.push_back(mtl);
        write_new_file(patch, bytes(baseline));
        written.push_back(patch);
        write_new_file(path, bytes(geometry));
    } catch (...) {
        for (auto &file : written) {
            std::error_code error;
            std::filesystem::remove(file, error);
        }
        throw;
    }
}
CollisionImportResult CollisionDocument::import_obj(const std::filesystem::path &path) {
    require(!preview_, "Finish the current transform before importing");
    require(std::filesystem::file_size(path) <= 512ull * 1024 * 1024, "Collision OBJ is too large");
    std::ifstream input(path);
    require(bool(input), "Cannot open collision OBJ");
    std::vector<SpatialPoint> vertices;
    std::map<std::string, unsigned> tokens;
    std::set<unsigned> baseline_faces;
    std::string baseline_signature;
    auto current_signature = exchange_signature();
    std::set<std::size_t> members;
    for (auto &source : sources_)
        members.insert(source.link->member);
    std::map<unsigned, CollisionState> edits;
    std::vector<CollisionAddition> additions;
    std::string line, group;
    std::optional<CollisionState> surface;
    std::optional<std::size_t> terrain;
    bool header = false, baseline = false, complete = false;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        try {
            std::istringstream fields(line);
            std::string tag;
            if (!(fields >> tag))
                continue;
            if (tag == "#") {
                std::string key;
                fields >> key;
                if (key == "usum_collision") {
                    unsigned version;
                    require(!header && bool(fields >> version) && version == 1,
                            "Unsupported collision OBJ version");
                    header = true;
                } else if (key == "baseline") {
                    require(!baseline && bool(fields >> baseline_signature) &&
                                (baseline_signature == current_signature ||
                                 (baseline_signature == obj_baseline_ &&
                                  current_signature == obj_result_)),
                            "Collision baseline differs. Open the .usum-collision patch exported "
                            "beside the original OBJ, or export again from the current map");
                    if (baseline_signature == current_signature) {
                        for (unsigned id = 0; id < size(); ++id)
                            if (live(id))
                                baseline_faces.insert(id);
                    } else
                        baseline_faces = obj_faces_;
                    for (auto id : baseline_faces)
                        tokens[face_token(id)] = id;
                    baseline = true;
                } else if (key == "complete") {
                    require(!complete, "Duplicate collision completion marker");
                    complete = true;
                }
                continue;
            }
            require(!complete, "Geometry appears after the collision completion marker");
            if (tag == "v") {
                SpatialPoint p;
                for (auto &value : p)
                    require(bool(fields >> value) && std::isfinite(value) && std::abs(value) < 1e9f,
                            "Invalid collision vertex coordinate");
                std::string extra;
                require(!(fields >> extra),
                        "Collision OBJ vertices must have exactly three coordinates");
                require(vertices.size() < 6000000, "Too many collision vertices");
                vertices.push_back(p);
            } else if (tag == "o") {
                std::string name;
                fields >> name;
                require(name.starts_with("terrain_"),
                        "Use the USUMStudio collision exporter in Blender");
                auto member_id = number(name.substr(8));
                require(members.contains(member_id),
                        "OBJ references a terrain resource outside this area");
                terrain = std::size_t(member_id);
                group.clear();
                surface.reset();
            } else if (tag == "g") {
                std::string name;
                fields >> name;
                require(name.starts_with("collision_"),
                        "Collision triangle IDs are missing; use the Blender collision helper");
                group = name.substr(10);
            } else if (tag == "usemtl") {
                std::string name;
                fields >> name;
                surface = parse_material(name);
            } else if (tag == "f") {
                require(header && baseline && terrain && surface && !group.empty(),
                        "Collision source, triangle ID or attribute is missing");
                auto value = *surface;
                for (auto &p : value.vertices) {
                    std::string ref;
                    require(bool(fields >> ref), "Collision faces must be triangles");
                    auto slash = ref.find('/');
                    auto index = number(ref.substr(0, slash));
                    require(index > 0 && index <= vertices.size(),
                            "Collision face vertex is out of range");
                    p = vertices[index - 1];
                }
                std::string extra;
                require(!(fields >> extra), "Triangulate collision faces before import");
                if (group == "new")
                    additions.push_back({*terrain, value});
                else {
                    auto found = tokens.find(group);
                    require(found != tokens.end(), "Unknown collision triangle ID");
                    require(member(found->second) == *terrain,
                            "Existing triangles cannot move between terrain resources");
                    require(edits.emplace(found->second, value).second,
                            "Duplicate collision triangle IDs; use the Blender collision exporter");
                }
                require(edits.size() + additions.size() <= 2000000, "Too many collision triangles");
            } else
                require(tag == "mtllib" || tag == "vn" || tag == "vt" || tag == "s",
                        "Unsupported collision OBJ statement");
        } catch (const std::exception &error) {
            throw std::runtime_error("Collision OBJ line " + std::to_string(line_number) + ": " +
                                     error.what());
        }
    }
    require(input.eof() && header && baseline && complete,
            "Incomplete collision OBJ; export using the Blender collision helper");
    for (auto &[token, id] : tokens)
        if (!edits.contains(id)) {
            auto value = state(id);
            value.deleted = true;
            edits[id] = value;
        }
    for (unsigned id = 0; id < size(); ++id)
        if (live(id) && !baseline_faces.contains(id)) {
            auto value = state(id);
            value.deleted = true;
            edits[id] = value;
        }
    CollisionImportResult result;
    result.added = additions.size();
    for (auto &[id, value] : edits) {
        if (value.deleted)
            result.removed += live(id);
        else
            result.modified += value != state(id);
    }
    replace_faces(edits, additions);
    obj_baseline_ = baseline_signature;
    obj_faces_ = std::move(baseline_faces);
    obj_result_ = exchange_signature();
    return result;
}
}
