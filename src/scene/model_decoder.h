#pragma once
#include "scene/environment.h"
#include "scene/player_assets.h"
#include "field/area.h"
#include "formats/compression.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
namespace studio {
struct ModelDecoder {
    Environment out;
    std::atomic_bool *cancel = nullptr;
    bool keep_skeleton = false;
    std::shared_ptr<const SceneModelSource> source;
    std::map<std::string, MaterialShader> shaders;
    void shader(View b, const std::string &prefix) {
        try {
            auto shader = decode_material_shader(b);
            auto key = prefix + shader.name;
            shaders[key] = std::move(shader);
        } catch (const std::exception &e) {
            note("Shader resource: " + std::string(e.what()));
        }
    }
    std::map<std::string, std::pair<SkeletalMotion, bool>> skeletal_motions;
    void motion(View bytes, const std::string &name, const std::string &prefix, bool daily,
                std::shared_ptr<const SceneModelSource> motion_source = {}) {
        if (bytes.empty())
            return;
        try {
            auto decoded = decode_visibility_motion(bytes);
            decoded.clock.looping = !daily;
            if (!decoded.tracks.empty())
                out.visibility_animations.push_back({prefix, std::move(decoded), daily});
        } catch (const std::exception &e) {
            note(name + ": " + e.what());
        }
        try {
            auto decoded = decode_skeletal_motion(bytes);
            decoded.looping = !daily;
            if (!decoded.tracks.empty())
                skeletal_motions[prefix] = {std::move(decoded), daily};
        } catch (const std::exception &e) {
            note(name + ": " + e.what());
        }
        try {
            auto decoded = decode_material_motion(bytes);
            decoded.looping = !daily;
            if (decoded.tracks.empty() && !motion_source)
                return;
            out.material_animations.push_back(
                {name, prefix, std::move(decoded), daily, {}, std::move(motion_source)});
        } catch (const std::exception &e) {
            note(name + ": " + e.what());
        }
    }
    void checkpoint() {
        if (cancel && cancel->load())
            throw std::runtime_error("Loading cancelled");
    }
    void note(const std::string &message) {
        if (std::find(out.diagnostics.begin(), out.diagnostics.end(), message) ==
            out.diagnostics.end())
            out.diagnostics.push_back(message);
    }
    void texture(View b, const std::string &prefix) {
        checkpoint();
        auto name = prefix + text(slice(b, 40, 64));
        if (out.textures.contains(name))
            return;
        try {
            out.textures.emplace(name, decode_field_texture(b));
        } catch (const std::exception &e) {
            note(name + ": " + e.what());
        }
    }
    std::vector<std::size_t> model(View bytes, const std::string &name, const std::string &prefix) {
        checkpoint();
        auto m = Model::parse(bytes);
        std::map<std::string, std::size_t> materials;
        auto model_tables = model_lighting_tables(m);
        std::map<std::uint32_t, int> table_rows;
        for (auto &[hash, table] : model_tables) {
            table_rows[hash] = int(out.lighting_tables.size());
            out.lighting_tables.push_back(table);
        }
        for (const auto &section : m.sections)
            if (section.kind == "material") {
                auto b = slice(bytes, section.offset, section.size);
                std::size_t p = 16;
                auto string = [&]() {
                    auto n = slice(b, p + 4, 1)[0];
                    auto s = text(slice(b, p + 5, n));
                    p += 5 + n;
                    return s;
                };
                SceneMaterial mat;
                mat.resource_scope = prefix;
                mat.name = string();
                string();
                mat.vertex_shader = string();
                mat.fragment_shader = string();
                auto metadata = p;
                std::array<MaterialColor, 6> constants{};
                std::array<unsigned, 6> assignments{};
                for (unsigned i = 0; i < 6; ++i) {
                    assignments[i] = slice(b, metadata + 17 + i, 1)[0];
                    constants[i] = material_color(u32(b, metadata + 24 + i * 4));
                }
                mat.constant_assignments = assignments;
                mat.fog_enabled = b[metadata + 50] != 0;
                mat.fog_slot = std::int8_t(b[metadata + 51]);
                mat.light_set = std::int8_t(b[metadata + 23]);
                mat.fragment_lighting = b[metadata + 49] != 0;
                mat.specular0 = {};
                mat.specular1 = {1, 1, 1, 1};
                mat.emission = material_color(u32(b, metadata + 60));
                mat.ambient = material_color(u32(b, metadata + 64));
                mat.diffuse = material_color(u32(b, metadata + 68));
                mat.edge_type = u32(b, metadata + 72);
                mat.id_edge_enabled = u32(b, metadata + 76) != 0;
                mat.edge_id = u32(b, metadata + 80);
                mat.edge_alpha_mask = std::int32_t(u32(b, metadata + 108));
                auto vertex_type = u32(b, metadata + 148);
                mat.object_space_normals = vertex_type == 1 || vertex_type == 2;
                mat.height_tint = vertex_type == 8;
                mat.screen_refraction = vertex_type == 16394;
                mat.generated_lighting_color = vertex_type == 1 || vertex_type == 2 ||
                                               mat.height_tint || mat.screen_refraction;
                if (mat.height_tint || mat.screen_refraction)
                    for (unsigned i = 0; i < 4; ++i) {
                        mat.vertex_parameters[i] = f32(b, metadata + 152 + i * 4);
                        require(std::isfinite(mat.vertex_parameters[i]),
                                "Invalid specialized vertex parameter");
                    }
                if (mat.generated_lighting_color)
                    for (unsigned i = 0; i < 4; ++i) {
                        mat.lighting_channels[i] = b[metadata + 52 + i] != 0;
                        mat.rim_phong[i] = f32(b, metadata + 88 + i * 4);
                        require(std::isfinite(mat.rim_phong[i]) && mat.rim_phong[i] >= 0,
                                "Invalid vertex lighting parameter");
                    }
                auto shader = shaders.find(prefix + mat.fragment_shader);
                if (shader == shaders.end())
                    shader = shaders.find("terrain/" + mat.fragment_shader);
                auto combined = shader == shaders.end() ? std::map<unsigned, std::uint32_t>{}
                                                        : shader->second.registers;
                p += 168;
                mat.texture_count = u32(b, p);
                p += 4;
                require(mat.texture_count <= 3, "Too many material texture units");
                for (unsigned unit = 0; unit < mat.texture_count; ++unit) {
                    auto tex = string();
                    auto slot = slice(b, p, 42)[0];
                    require(slot < 3, "Invalid texture unit");
                    mat.texture_inputs[slot] = prefix + tex;
                    auto &input = mat.inputs[slot];
                    if (b[p + 1] != 0 && b[p + 1] != 2 && !(b[p + 1] == 3 && slot < 2))
                        mat.unsupported_mapping = true;
                    float sx = f32(b, p + 2), sy = f32(b, p + 6), rotation = f32(b, p + 10),
                          tx = f32(b, p + 14), ty = f32(b, p + 18);
                    for (float v : {sx, sy, rotation, tx, ty})
                        require(std::isfinite(v), "Non-finite material UV transform");
                    input.transform = {sx, sy, rotation, tx, ty};
                    update_texture_transform(input);
                    for (auto row : {input.row_u, input.row_v})
                        for (float v : row)
                            require(std::isfinite(v), "Excessive material UV transform");
                    input.wrap_u = u32(b, p + 22);
                    input.wrap_v = u32(b, p + 26);
                    input.mag_filter = u32(b, p + 30);
                    input.min_filter = u32(b, p + 34);
                    if (slot == 0)
                        mat.texture = prefix + tex;
                    p += 42;
                }
                p = aligned(p, 16);
                auto command_size = u32(b, p);
                mat.priority = std::int32_t(u32(b, p + 4));
                mat.layer = std::int32_t(u32(b, p + 12));
                auto stream = slice(b, p + 32, command_size);
                std::map<unsigned, std::uint32_t> registers;
                for (auto c : commands(stream)) {
                    auto &value = registers[c.reg];
                    for (unsigned lane = 0; lane < 4; ++lane)
                        if (c.mask & (1 << lane)) {
                            auto mask = 255u << (lane * 8);
                            value = (value & ~mask) | (c.value & mask);
                            auto &inherited = combined[c.reg];
                            inherited = (inherited & ~mask) | (c.value & mask);
                        }
                }
                for (unsigned channel = 0; channel < 3; ++channel) {
                    auto hash = u32(b, metadata + channel * 4);
                    if (hash) {
                        auto found = table_rows.find(hash);
                        if (found == table_rows.end())
                            note(name + " / " + mat.name + ": missing lighting lookup table " +
                                 std::to_string(hash));
                        else
                            mat.reflection_tables[channel] = found->second;
                    }
                    auto shift = 16 + channel * 4;
                    unsigned input = (combined[0x1d1] >> shift) & 7,
                             scale = (combined[0x1d2] >> shift) & 7;
                    static constexpr float scales[] = {1, 2, 4, 8, 0, 0, .25f, .5f};
                    if (input > 5 || input == 4 || scales[scale] == 0) {
                        note(name + " / " + mat.name + ": unsupported lighting lookup input");
                        mat.reflection_tables[channel] = -1;
                    }
                    mat.reflection_inputs[channel] = {
                        float(input), (combined[0x1d0] & (1u << (shift + 1))) ? 0.f : 1.f,
                        scales[scale], 0};
                }
                mat.combiner = decode_combiner(combined);
                mat.bump = decode_bump(combined, std::int8_t(b[metadata + 16]));
                if (!mat.bump.unsupported.empty())
                    note(name + " / " + mat.name + ": " + mat.bump.unsupported);
                else if (mat.bump.mode && mat.texture_inputs[mat.bump.texture].empty())
                    note(name + " / " + mat.name + ": missing normal-map texture binding");
                for (unsigned i = 0; i < 6; ++i) {
                    if (assignments[i] < 6)
                        mat.combiner.stages[i].constant = constants[assignments[i]];
                    else
                        mat.combiner.unsupported = "Unknown constant-color assignment";
                }
                mat.authored_combiner = mat.combiner;
                mat.authored_combiners = combiner_settings(combined, assignments);
                unsigned uniform_index = 0, component = 0;
                bool float_uniform = false;
                std::array<std::uint32_t, 4> uniform_words{};
                std::array<bool, 6> projection_rows{};
                auto apply_uv = [&](std::array<float, 4> values) {
                    if (uniform_index == 0)
                        for (unsigned slot = 0; slot < 3; ++slot) {
                            auto value = values[slot];
                            if (std::isfinite(value) && value >= 0 &&
                                (value <= 2 || value == 4 || (value == 5 && slot < 2)) &&
                                value == std::floor(value))
                                mat.inputs[slot].source = unsigned(value);
                            else
                                mat.unsupported_mapping = true;
                        }
                    if (uniform_index >= 1 && uniform_index <= 6) {
                        auto row = uniform_index - 1;
                        mat.inputs[row / 3].projection[row % 3] = values;
                        projection_rows[row] =
                            std::all_of(values.begin(), values.end(), [](float value) {
                                return std::isfinite(value);
                            });
                    }
                    if (uniform_index == 9)
                        for (unsigned slot = 0; slot < 2; ++slot)
                            mat.inputs[slot].projection_offset = {values[slot * 2],
                                                                  values[slot * 2 + 1]};
                };
                auto unpack = [](std::uint32_t v) {
                    v &= 0xffffff;
                    auto bits = (v & 0x7fffff)
                                    ? ((v & 0xffff) << 7) | ((((v >> 16) & 127) + 64) << 23) |
                                          ((v & 0x800000) << 8)
                                    : (v & 0x800000) << 8;
                    float f;
                    std::memcpy(&f, &bits, 4);
                    return f;
                };
                for (auto c : commands(stream)) {
                    if (c.reg == 0x2c0) {
                        uniform_index = c.value & 255;
                        component = 0;
                        float_uniform = (c.value >> 31) != 0;
                    } else if (c.reg >= 0x2c1 && c.reg <= 0x2c8) {
                        uniform_words[component++] = c.value;
                        if (component == (float_uniform ? 4u : 3u)) {
                            std::array<float, 4> values{};
                            if (float_uniform)
                                for (unsigned k = 0; k < 4; ++k)
                                    std::memcpy(&values[3 - k], &uniform_words[k], 4);
                            else
                                values = {
                                    unpack(uniform_words[2]),
                                    unpack((uniform_words[2] >> 24) | (uniform_words[1] << 8)),
                                    unpack((uniform_words[1] >> 16) | (uniform_words[0] << 16)),
                                    unpack(uniform_words[0] >> 8)};
                            apply_uv(values);
                            component = 0;
                            ++uniform_index;
                        }
                    }
                }
                for (unsigned slot = 0; slot < 2; ++slot)
                    if (mat.inputs[slot].source == 5)
                        for (unsigned row = 0; row < 3; ++row)
                            if (!projection_rows[slot * 3 + row])
                                mat.unsupported_mapping = true;
                if (registers.contains(0x107))
                    mat.depth_state = registers[0x107];
                if (registers.contains(0x101))
                    mat.blend_state = registers[0x101];
                mat.blend_color = registers[0x103];
                for (unsigned shift = 16; shift < 32; shift += 4) {
                    auto factor = (mat.blend_state >> shift) & 15;
                    if (factor == 12 || factor == 13)
                        note(name + " / " + mat.name +
                             ": constant-alpha blend factor is approximated");
                }
                mat.stencil_test = registers[0x105];
                mat.stencil_operations = registers[0x106];
                mat.stencil_write = !registers.contains(0x115) || (registers[0x115] & 1) != 0;
                if ((mat.stencil_test & 1) && mat.stencil_write &&
                    ((mat.stencil_test >> 8) & 255) != 0 && ((mat.stencil_test >> 8) & 255) != 255)
                    note(name + " / " + mat.name + ": partial stencil write masks are unsupported");
                mat.cull = registers[0x40] & 3;
                auto alpha = registers[0x104];
                mat.alpha_function = (alpha & 1) ? ((alpha >> 4) & 7) : 1;
                mat.alpha_reference = (alpha >> 8) & 255;
                if (!mat.combiner.present)
                    note(name + " / " + mat.name + ": missing fragment combiner " +
                         mat.fragment_shader);
                else if (!mat.combiner.unsupported.empty())
                    note(name + " / " + mat.name + ": " + mat.combiner.unsupported);
                if (mat.unsupported_mapping)
                    note(name + " / " + mat.name + ": unsupported texture mapping");
                materials.emplace(mat.name, out.materials.size());
                out.materials.push_back(std::move(mat));
            }
        std::vector<std::size_t> result;
        for (const auto &section : m.sections)
            if (section.kind == "mesh") {
                checkpoint();
                auto b = slice(bytes, section.offset, section.size);
                auto count = u32(b, 120);
                require(count > 0 && count < 1024, "Invalid scene submesh count");
                std::size_t p = 144;
                std::vector<View> streams;
                for (std::size_t i = 0; i < count * 3; ++i) {
                    auto n = u32(b, p);
                    require(u32(b, p + 4) == i && u32(b, p + 8) == count * 3,
                            "Invalid scene command ordering");
                    streams.push_back(slice(b, p + 16, n));
                    p += 16 + n;
                }
                struct Record {
                    std::string mat;
                    std::size_t vc, ic, vb, ib;
                };
                std::vector<Record> records;
                for (std::size_t i = 0; i < count; ++i) {
                    auto n = u32(b, p + 4);
                    auto mat = text(slice(b, p + 8, n));
                    p += 8 + n;
                    slice(b, p, 32);
                    p += 32;
                    records.push_back(
                        {mat, u32(b, p), u32(b, p + 4), u32(b, p + 8), u32(b, p + 12)});
                    p += 16;
                }
                for (std::size_t i = 0; i < count; ++i) {
                    auto &r = records[i];
                    auto vb = slice(b, p, r.vb);
                    p += r.vb;
                    auto ib = slice(b, p, r.ib);
                    p += r.ib;
                    require(r.vc <= 65536 && r.ic < 3000000, "Excessive scene geometry");
                    auto layout = vertex_layout(streams[i * 3]);
                    require(r.vc * layout.stride <= vb.size(), "Truncated scene vertex data");
                    bool wide = false;
                    unsigned primitive = 0;
                    std::uint32_t draw_count = 0;
                    for (auto c : commands(streams[i * 3 + 2])) {
                        if (c.reg == 0x227)
                            wide = (c.value >> 31) != 0;
                        if (c.reg == 0x228)
                            draw_count = c.value;
                        if (c.reg == 0x25e && (c.mask & 8))
                            primitive = c.value >> 28;
                    }
                    require(primitive == 0 && r.ic % 3 == 0 && draw_count == r.ic,
                            "Scene geometry needs triangle lists");
                    require(r.ic * (wide ? 2 : 1) <= ib.size(), "Truncated scene indices");
                    SceneDraw draw;
                    draw.source = source;
                    draw.mesh = text(slice(b, 20, 64));
                    draw.scope = prefix;
                    draw.name = name + " / " + text(slice(b, 20, 64)) + " / " + std::to_string(i);
                    require(materials.contains(r.mat), "Missing scene material");
                    draw.material = materials.at(r.mat);
                    for (std::size_t v = 0; v < r.vc; ++v) {
                        SceneVertex vertex{};
                        vertex.color = 0xffffffff;
                        bool position = false;
                        for (auto a : layout.attributes) {
                            std::array<float, 4> values{};
                            for (unsigned k = 0; k < a.elements; ++k) {
                                auto at = v * layout.stride + a.offset;
                                values[k] = a.format == 3 ? f32(vb, at + k * 4)
                                            : a.format == 2
                                                ? float(std::int16_t(u16(vb, at + k * 2)))
                                            : a.format == 1 ? float(vb[at + k])
                                                            : float(std::int8_t(vb[at + k]));
                                require(std::isfinite(values[k]) && std::abs(values[k]) <= 1e7f,
                                        "Non-finite or excessive scene vertex");
                            }
                            if (a.semantic == 0) {
                                require(a.format == 3 && a.elements == 3,
                                        "Scene position format requires float3");
                                vertex.x = values[0];
                                vertex.y = values[1];
                                vertex.z = values[2];
                                position = true;
                            }
                            if (a.semantic == 2) {
                                auto scale = a.format == 3   ? 1.f
                                             : a.format == 2 ? 1.f / 32767
                                             : a.format == 1 ? 1.f / 255
                                                             : 1.f / 127;
                                vertex.tx = values[0] * scale;
                                vertex.ty = values[1] * scale;
                                vertex.tz = values[2] * scale;
                            }
                            if (a.semantic == 1) {
                                auto scale = a.format == 3   ? 1.f
                                             : a.format == 2 ? 1.f / 32767
                                             : a.format == 1 ? 1.f / 255
                                                             : 1.f / 127;
                                vertex.nx = values[0] * scale;
                                vertex.ny = values[1] * scale;
                                vertex.nz = values[2] * scale;
                            }
                            if (a.semantic >= 4 && a.semantic <= 6) {
                                auto scale = a.format == 3   ? 1.f
                                             : a.format == 2 ? 1.f / 32767
                                             : a.format == 1 ? 1.f / 255
                                                             : 1.f / 127;
                                auto u = values[0] * scale, vv = values[1] * scale;
                                if (a.semantic == 4) {
                                    vertex.u = u;
                                    vertex.v = vv;
                                } else if (a.semantic == 5) {
                                    vertex.u1 = u;
                                    vertex.v1 = vv;
                                } else {
                                    vertex.u2 = u;
                                    vertex.v2 = vv;
                                }
                            }
                            if (a.semantic == 3) {
                                vertex.color = 0;
                                for (unsigned k = 0; k < 4; ++k) {
                                    auto c = k < a.elements ? values[k] : 255.f;
                                    if (a.format == 3)
                                        c *= 255;
                                    vertex.color |= std::uint32_t(std::clamp(c, 0.f, 255.f))
                                                    << (k * 8);
                                }
                            }
                        }
                        require(position, "Missing scene positions");
                        draw.vertices.push_back(vertex);
                    }
                    for (std::size_t j = 0; j < r.ic; ++j) {
                        auto index = wide ? u16(ib, j * 2) : ib[j];
                        require(index < r.vc, "Scene index out of range");
                        draw.indices.push_back(std::uint16_t(index));
                    }
                    complete_normals(draw);
                    if (!out.materials[draw.material].object_space_normals &&
                        out.materials[draw.material].bump.mode == 1 &&
                        std::any_of(draw.vertices.begin(), draw.vertices.end(), [](const auto &v) {
                            auto x = v.ny * v.tz - v.nz * v.ty, y = v.nz * v.tx - v.nx * v.tz,
                                 z = v.nx * v.ty - v.ny * v.tx;
                            return x * x + y * y + z * z < .000001f;
                        }))
                        note(draw.name + ": missing or degenerate tangent; normal mapping falls "
                                         "back to mesh normals there");
                    result.push_back(out.draws.size());
                    out.draws.push_back(std::move(draw));
                }
            }
        return result;
    }
    void placed_model(View bytes, const std::string &name, const std::string &prefix,
                      const Placement *placement = nullptr) {
        auto initial = out.draws.size(), material_initial = out.materials.size();
        try {
            auto ids = model(bytes, name, prefix);
            if (auto motion = skeletal_motions.find(prefix); motion != skeletal_motions.end())
                try {
                    auto skin = SkinnedModel::parse(bytes);
                    require(skin.meshes.size() == ids.size(), "Skin submesh count mismatch");
                    SceneSkeleton rig;
                    rig.joints = skin.joints;
                    rig.motion = motion->second.first;
                    rig.daily = motion->second.second;
                    rig.placement = pose_identity();
                    if (placement) {
                        auto q = placement->rotation;
                        float x = q[0], y = q[1], z = q[2], w = q[3];
                        rig.placement = {1 - 2 * (y * y + z * z),
                                         2 * (x * y - z * w),
                                         2 * (x * z + y * w),
                                         placement->position[0],
                                         2 * (x * y + z * w),
                                         1 - 2 * (x * x + z * z),
                                         2 * (y * z - x * w),
                                         placement->position[1],
                                         2 * (x * z - y * w),
                                         2 * (y * z + x * w),
                                         1 - 2 * (x * x + y * y),
                                         placement->position[2],
                                         0,
                                         0,
                                         0,
                                         1};
                    }
                    rig.inverse_placement = pose_inverse(rig.placement);
                    bool bound = false;
                    for (auto &joint : rig.joints) {
                        auto track = std::find_if(rig.motion.tracks.begin(),
                                                  rig.motion.tracks.end(), [&](const auto &t) {
                                                      return t.name == joint.name;
                                                  });
                        rig.tracks.push_back(track == rig.motion.tracks.end()
                                                 ? -1
                                                 : int(track - rig.motion.tracks.begin()));
                        bound |= track != rig.motion.tracks.end();
                    }
                    if (!rig.joints.empty() && (bound || keep_skeleton)) {
                        for (unsigned i = 0; i < ids.size(); ++i) {
                            auto &draw = out.draws[ids[i]];
                            auto &mesh = skin.meshes[i];
                            if (mesh.influences == 0)
                                continue;
                            require(mesh.vertices.size() == draw.vertices.size(),
                                    "Skin vertex count mismatch");
                            draw.skeleton = int(out.skeletons.size());
                            draw.palette = mesh.palette;
                            for (unsigned v = 0; v < mesh.vertices.size(); ++v)
                                for (unsigned k = 0; k < 4; ++k) {
                                    auto &source = mesh.vertices[v];
                                    draw.vertices[v].weights[k] = source.weights[k];
                                    auto at = std::find(mesh.palette.begin(), mesh.palette.end(),
                                                        source.joints[k]);
                                    draw.vertices[v].joints[k] =
                                        source.weights[k] > 0 ? float(at - mesh.palette.begin())
                                                              : 0;
                                }
                        }
                        out.skeletons.push_back(std::move(rig));
                    }
                } catch (const std::exception &e) {
                    for (auto id : ids)
                        out.draws[id].skeleton = -1;
                    note(name + ": skeletal playback unavailable: " + e.what());
                }

            if (placement) {
                auto q = placement->rotation;
                float x = q[0], y = q[1], z = q[2], w = q[3];
                for (auto id : ids)
                    out.draws[id].object_basis = {
                        MaterialColor{1 - 2 * (y * y + z * z), 2 * (x * y + z * w),
                                      2 * (x * z - y * w), 0},
                        MaterialColor{2 * (x * z + y * w), 2 * (y * z - x * w),
                                      1 - 2 * (x * x + y * y), 0}};
            }
            for (auto id : ids)
                for (auto &v : out.draws[id].vertices) {
                    if (placement) {
                        auto q = placement->rotation;
                        auto x = v.x, y = v.y, z = v.z;
                        auto tx = 2 * (q[1] * z - q[2] * y), ty = 2 * (q[2] * x - q[0] * z),
                             tz = 2 * (q[0] * y - q[1] * x);
                        v.x += q[3] * tx + q[1] * tz - q[2] * ty + placement->position[0];
                        v.y += q[3] * ty + q[2] * tx - q[0] * tz + placement->position[1];
                        v.z += q[3] * tz + q[0] * ty - q[1] * tx + placement->position[2];
                        auto nx = v.nx, ny = v.ny, nz = v.nz;
                        auto rx = 2 * (q[1] * nz - q[2] * ny), ry = 2 * (q[2] * nx - q[0] * nz),
                             rz = 2 * (q[0] * ny - q[1] * nx);
                        v.nx += q[3] * rx + q[1] * rz - q[2] * ry;
                        v.ny += q[3] * ry + q[2] * rx - q[0] * rz;
                        v.nz += q[3] * rz + q[0] * ry - q[1] * rx;
                        auto ux = v.tx, uy = v.ty, uz = v.tz;
                        auto ax = 2 * (q[1] * uz - q[2] * uy), ay = 2 * (q[2] * ux - q[0] * uz),
                             az = 2 * (q[0] * uy - q[1] * ux);
                        v.tx += q[3] * ax + q[1] * az - q[2] * ay;
                        v.ty += q[3] * ay + q[2] * ax - q[0] * az;
                        v.tz += q[3] * az + q[0] * ay - q[1] * ax;
                    }
                }
        } catch (const std::exception &e) {
            out.draws.resize(initial);
            out.materials.resize(material_initial);
            checkpoint();
            note(name + ": skipped geometry: " + e.what());
        }
    }
};
}
