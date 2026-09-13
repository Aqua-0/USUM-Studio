#include "assets/model_exchange.h"
#include "assets/skeleton_edit.h"
#include "core/digest.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
namespace studio {
namespace {
struct MeshStorage {
    std::array<Bytes, 3> streams;
    Bytes descriptor, vertices, indices;
    VertexLayout layout;
    unsigned vertex_count = 0, index_count = 0;
    std::size_t palette = 0, vertex_offset = 0;
};
struct MeshGroup {
    Bytes header, tail;
    std::vector<MeshStorage> meshes;
};
std::string geometry_hash(View bytes) {
    auto model = Model::parse(bytes);
    Bytes data;
    for (auto &section : model.sections)
        if (section.kind != "material")
            append(data, slice(bytes, section.offset, section.size));
    return sha256(data);
}
std::vector<MeshGroup> storage(View bytes) {
    auto model = Model::parse(bytes);
    std::vector<MeshGroup> groups;
    for (auto &section : model.sections)
        if (section.kind == "mesh") {
            auto b = slice(bytes, section.offset, section.size);
            MeshGroup group;
            group.header = Bytes(b.begin(), b.begin() + 144);
            auto count = u32(b, 120);
            group.meshes.resize(count);
            std::size_t p = 144;
            for (unsigned i = 0; i < count; ++i)
                for (unsigned s = 0; s < 3; ++s) {
                    auto length = u32(b, p);
                    auto stream = slice(b, p + 16, length);
                    group.meshes[i].streams[s] = Bytes(stream.begin(), stream.end());
                    p += 16 + length;
                }
            for (auto &mesh : group.meshes) {
                auto start = p;
                auto length = u32(b, p + 4);
                mesh.palette = 8 + length;
                p += 8 + length + 32;
                mesh.vertex_count = u32(b, p);
                mesh.index_count = u32(b, p + 4);
                p += 16;
                auto data = slice(b, start, p - start);
                mesh.descriptor = Bytes(data.begin(), data.end());
                mesh.layout = vertex_layout(mesh.streams[0]);
            }
            for (auto &mesh : group.meshes) {
                auto counts = mesh.palette + 32;
                auto vertices = u32(mesh.descriptor, counts + 8),
                     indices = u32(mesh.descriptor, counts + 12);
                mesh.vertex_offset = section.offset + p;
                auto vb = slice(b, p, vertices), ib = slice(b, p + vertices, indices);
                mesh.vertices = Bytes(vb.begin(), vb.end());
                mesh.indices = Bytes(ib.begin(), ib.end());
                p += vertices + indices;
            }
            auto tail = slice(b, p, b.size() - p);
            group.tail = Bytes(tail.begin(), tail.end());
            groups.push_back(std::move(group));
        }
    return groups;
}
float read_component(View data, std::size_t p, unsigned format) {
    if (format == 3)
        return f32(data, p);
    if (format == 2)
        return float(std::int16_t(u16(data, p)));
    return format == 1 ? float(data[p]) : float(std::int8_t(data[p]));
}
void write_component(Bytes &bytes, std::size_t p, unsigned format, float value) {
    require(std::isfinite(value) && std::abs(value) <= 1e7f,
            "Mesh attributes must be finite and within the supported coordinate range");
    if (format == 3) {
        put_float(bytes, p, value);
        return;
    }
    float low = format == 1   ? 0.f
                : format == 2 ? -32768.f
                              : -128.f,
          high = format == 1   ? 255.f
                 : format == 2 ? 32767.f
                               : 127.f;
    require(value >= low - .001f && value <= high + .001f,
            "An imported attribute exceeds its native range");
    auto integer = int(std::round(std::clamp(value, low, high)));
    if (format == 2)
        put16(bytes, p, std::uint16_t(integer));
    else
        bytes[p] = std::uint8_t(integer);
}
std::array<unsigned, 4> quantize(const ExchangeVertex &vertex, std::size_t bones) {
    float sum = 0;
    unsigned assigned = 0;
    std::array<unsigned, 4> values{};
    std::array<float, 4> fractions{};
    std::set<unsigned> used;
    for (unsigned i = 0; i < 4; ++i) {
        auto weight = vertex.weights[i];
        require(std::isfinite(weight) && weight >= 0 && weight <= 1, "Invalid imported weight");
        if (weight > 0)
            require(vertex.joints[i] < bones && used.insert(vertex.joints[i]).second,
                    "Missing or duplicate weighted bone");
        sum += weight;
        auto raw = weight * 255;
        values[i] = unsigned(std::floor(raw));
        fractions[i] = raw - values[i];
        assigned += values[i];
    }
    require(std::abs(sum - 1) < 1e-5f && assigned <= 255, "Imported weights must sum to one");
    while (assigned < 255) {
        auto index =
            std::size_t(std::max_element(fractions.begin(), fractions.end()) - fractions.begin());
        ++values[index];
        fractions[index] = -1;
        ++assigned;
    }
    return values;
}
void write_fixed(Bytes &stream, unsigned semantic, std::array<unsigned, 4> values) {
    std::map<unsigned, unsigned> regs;
    for (auto c : commands(stream))
        regs[c.reg] = c.value;
    auto semantics = std::uint64_t(regs[0x2bb]) | (std::uint64_t(regs[0x2bc]) << 32);
    std::array<unsigned, 4> packed{};
    for (unsigned i = 0; i < 4; ++i)
        if (values[i]) {
            int exponent = 0;
            auto fraction = std::frexp(float(values[i]), &exponent);
            packed[i] = (unsigned(exponent + 62) << 16) | unsigned((fraction * 2 - 1) * 65536);
        }
    std::array<unsigned, 3> words{(packed[3] << 8) | (packed[2] >> 16),
                                  (packed[2] << 16) | (packed[1] >> 8),
                                  (packed[1] << 24) | packed[0]};
    unsigned index = 0, found = 0;
    for (auto c : commands(stream)) {
        if (c.reg == 0x232)
            index = c.value & 15;
        if (c.reg >= 0x233 && c.reg <= 0x235) {
            if (index < 12 && unsigned((semantics >> (index * 4)) & 15) == semantic) {
                require(c.mask == 15, "Unsupported partial fixed skin attribute");
                put32(stream, c.offset, words[c.reg - 0x233]);
                ++found;
            }
            if (c.reg == 0x235)
                ++index;
        }
    }
    require(found == 3, "Missing writable fixed skin attribute");
}
}
ModelExchange decode_model_exchange(View bytes) {
    auto skin = SkinnedModel::parse(bytes);
    ModelExchange result;
    result.source = geometry_hash(bytes);
    result.joints = skin.joints;
    unsigned index = 0;
    for (auto &group : storage(bytes))
        for (auto &data : group.meshes) {
            auto &mesh = skin.meshes.at(index++);
            ExchangeMesh out;
            out.name = mesh.name;
            out.influences = mesh.influences;
            out.indices = mesh.indices;
            for (auto a : data.layout.attributes)
                if (a.semantic < 7)
                    out.formats[a.semantic] = {a.format, a.elements};
            for (unsigned v = 0; v < mesh.vertices.size(); ++v) {
                ExchangeVertex vertex;
                vertex.joints = mesh.vertices[v].joints;
                vertex.weights = mesh.vertices[v].weights;
                for (auto a : data.layout.attributes)
                    if (a.semantic < 7)
                        for (unsigned k = 0; k < a.elements; ++k)
                            vertex.channels[a.semantic][k] =
                                read_component(data.vertices,
                                               v * data.layout.stride + a.offset +
                                                   k * (a.format == 3   ? 4
                                                        : a.format == 2 ? 2
                                                                        : 1),
                                               a.format);
                out.vertices.push_back(vertex);
            }
            result.meshes.push_back(std::move(out));
        }
    return result;
}
Bytes replace_model_uvs(View original, unsigned channel, const MeshUvEdits &edits) {
    require(channel < 3, "Choose UV set 0, 1 or 2");
    auto groups = storage(original);
    std::vector<const MeshStorage *> meshes;
    for (auto &group : groups)
        for (auto &mesh : group.meshes)
            meshes.push_back(&mesh);
    Bytes result(original.begin(), original.end());
    for (auto &[index, vertices] : edits) {
        require(index < meshes.size(), "UV mesh is missing");
        auto &mesh = *meshes[index];
        auto attribute =
            std::find_if(mesh.layout.attributes.begin(), mesh.layout.attributes.end(), [&](auto a) {
                return a.semantic == channel + 4;
            });
        require(attribute != mesh.layout.attributes.end() && attribute->elements >= 2,
                "This mesh has no editable UV channel");
        float scale = attribute->format == 3   ? 1.f
                      : attribute->format == 2 ? 32767.f
                      : attribute->format == 1 ? 255.f
                                               : 127.f;
        auto width = attribute->format == 3 ? 4 : attribute->format == 2 ? 2 : 1;
        for (auto &[vertex, uv] : vertices) {
            require(vertex < mesh.vertex_count, "UV vertex is missing");
            for (unsigned k = 0; k < 2; ++k)
                write_component(result,
                                mesh.vertex_offset + vertex * mesh.layout.stride +
                                    attribute->offset + k * width,
                                attribute->format, uv[k] * scale);
        }
    }
    return result;
}
Bytes replace_model_exchange(View original, const ModelExchange &replacement) {
    auto before = decode_model_exchange(original);
    require(replacement.source == before.source,
            "The source geometry changed after export; export it again before importing");
    require(replacement.meshes.size() == before.meshes.size(),
            "Keep the exported mesh slots; edit their vertices and faces");
    auto bones = replace_skeleton(original, replacement.joints);
    auto groups = storage(bones);
    std::size_t index = 0;
    bool changed = bones != Bytes(original.begin(), original.end());
    std::array<float, 3> low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
    for (auto &group : groups) {
        std::array<float, 3> mesh_low{INFINITY, INFINITY, INFINITY},
            mesh_high{-INFINITY, -INFINITY, -INFINITY};
        for (auto &data : group.meshes) {
            auto &old = before.meshes.at(index);
            auto &mesh = replacement.meshes.at(index++);
            require(mesh.name == old.name && mesh.formats == old.formats &&
                        mesh.influences == old.influences,
                    "Keep native mesh names, channels and influence layouts");
            require(!mesh.vertices.empty() && mesh.vertices.size() <= 65536 &&
                        !mesh.indices.empty() && mesh.indices.size() < 1000000 &&
                        mesh.indices.size() % 3 == 0,
                    "Each mesh needs 1–65536 vertices and a valid triangle list");
            for (auto i : mesh.indices)
                require(i < mesh.vertices.size(), "Triangle references a missing vertex");
            changed |= mesh.vertices != old.vertices || mesh.indices != old.indices;
            std::vector<unsigned> palette;
            std::set<unsigned> needed;
            for (auto &vertex : mesh.vertices)
                for (unsigned k = 0; k < 4; ++k)
                    if (vertex.weights[k] > 0)
                        needed.insert(vertex.joints[k]);
            if (mesh.influences) {
                for (unsigned i = 0; i < data.descriptor[data.palette]; ++i) {
                    auto bone = data.descriptor[data.palette + 1 + i];
                    if (needed.erase(bone))
                        palette.push_back(bone);
                }
                palette.insert(palette.end(), needed.begin(), needed.end());
                require(!palette.empty() && palette.size() <= 31,
                        "A mesh can use at most 31 bones; split or reduce its influences");
                data.descriptor[data.palette] = std::uint8_t(palette.size());
                for (unsigned i = 0; i < palette.size(); ++i)
                    data.descriptor[data.palette + 1 + i] = std::uint8_t(palette[i]);
            } else
                require(needed.empty(), "This static mesh has no skinning channels");
            auto has = [&](unsigned semantic) {
                return std::any_of(data.layout.attributes.begin(), data.layout.attributes.end(),
                                   [&](auto a) {
                                       return a.semantic == semantic;
                                   });
            };
            std::array<unsigned, 4> fixed_indices{}, fixed_weights{};
            Bytes vertices(aligned(mesh.vertices.size() * data.layout.stride, 16));
            for (unsigned v = 0; v < mesh.vertices.size(); ++v) {
                auto &vertex = mesh.vertices[v];
                for (unsigned k = 0; k < 3; ++k) {
                    auto value = vertex.channels[0][k];
                    mesh_low[k] = std::min(mesh_low[k], value);
                    mesh_high[k] = std::max(mesh_high[k], value);
                }
                std::array<unsigned, 4> weights{}, indices{};
                if (mesh.influences) {
                    weights = quantize(vertex, replacement.joints.size());
                    for (unsigned k = 0; k < 4; ++k)
                        if (weights[k]) {
                            require(k < mesh.influences,
                                    "Too many influences for this native mesh");
                            indices[k] = unsigned(
                                std::find(palette.begin(), palette.end(), vertex.joints[k]) -
                                palette.begin());
                        }
                    if (!has(7)) {
                        if (v)
                            require(indices == fixed_indices, "This mesh shares one attachment; "
                                                              "all imported vertices must use it");
                        fixed_indices = indices;
                    }
                    if (!has(8)) {
                        if (v)
                            require(weights == fixed_weights,
                                    "This mesh shares its weights; all imported vertices must use "
                                    "the same weights");
                        fixed_weights = weights;
                    }
                }
                for (auto a : data.layout.attributes) {
                    if (a.semantic >= 7)
                        for (unsigned k = a.elements; k < 4; ++k)
                            require(weights[k] == 0,
                                    "The native skin channel cannot hold that many influences");
                    for (unsigned k = 0; k < a.elements; ++k) {
                        float value = a.semantic < 7    ? vertex.channels[a.semantic][k]
                                      : a.semantic == 7 ? float(indices[k])
                                                        : float(weights[k]);
                        write_component(vertices,
                                        v * data.layout.stride + a.offset +
                                            k * (a.format == 3   ? 4
                                                 : a.format == 2 ? 2
                                                                 : 1),
                                        a.format, value);
                    }
                }
            }
            if (mesh.influences) {
                if (!has(7))
                    write_fixed(data.streams[0], 7, fixed_indices);
                if (!has(8))
                    write_fixed(data.streams[0], 8, fixed_weights);
            }
            Bytes indices(aligned(mesh.indices.size() * 2, 16));
            for (unsigned i = 0; i < mesh.indices.size(); ++i)
                put16(indices, i * 2, mesh.indices[i]);
            bool count = false, wide = false;
            for (auto c : commands(data.streams[2])) {
                if (c.reg == 0x228) {
                    require(c.mask == 15, "Unsupported partial index count command");
                    put32(data.streams[2], c.offset, narrow(mesh.indices.size()));
                    count = true;
                }
                if (c.reg == 0x227) {
                    require(c.mask == 15, "Unsupported partial index format command");
                    put32(data.streams[2], c.offset, c.value | 0x80000000u);
                    wide = true;
                }
            }
            require(count && wide, "Missing native triangle draw commands");
            auto counts = data.palette + 32;
            put32(data.descriptor, counts, narrow(mesh.vertices.size()));
            put32(data.descriptor, counts + 4, narrow(mesh.indices.size()));
            put32(data.descriptor, counts + 8, narrow(vertices.size()));
            put32(data.descriptor, counts + 12, narrow(indices.size()));
            data.vertices = std::move(vertices);
            data.indices = std::move(indices);
        }
        for (unsigned k = 0; k < 3; ++k) {
            put_float(group.header, 88 + k * 4,
                      std::min(f32(group.header, 88 + k * 4), mesh_low[k]));
            put_float(group.header, 104 + k * 4,
                      std::max(f32(group.header, 104 + k * 4), mesh_high[k]));
            low[k] = std::min(low[k], mesh_low[k]);
            high[k] = std::max(high[k], mesh_high[k]);
        }
    }
    if (!changed)
        return Bytes(original.begin(), original.end());
    auto model = Model::parse(bones);
    Bytes out(bones.begin(), bones.begin() + 16);
    index = 0;
    for (auto &section : model.sections) {
        if (section.kind != "mesh") {
            append(out, slice(bones, section.offset, section.size));
            continue;
        }
        auto &group = groups.at(index++);
        Bytes data = group.header;
        unsigned stream_index = 0;
        for (auto &mesh : group.meshes)
            for (auto &stream : mesh.streams) {
                append32(data, narrow(stream.size()));
                append32(data, stream_index++);
                append32(data, narrow(group.meshes.size() * 3));
                append32(data, 0);
                append(data, stream);
            }
        for (auto &mesh : group.meshes)
            append(data, mesh.descriptor);
        for (auto &mesh : group.meshes) {
            append(data, mesh.vertices);
            append(data, mesh.indices);
        }
        append(data, group.tail);
        data.resize(aligned(data.size(), 16));
        put32(data, 8, narrow(data.size() - 16));
        append(out, data);
    }
    for (unsigned k = 0; k < 3; ++k) {
        put_float(out, model.bounds_offset + k * 4,
                  std::min(f32(out, model.bounds_offset + k * 4), low[k]));
        put_float(out, model.bounds_offset + 16 + k * 4,
                  std::max(f32(out, model.bounds_offset + 16 + k * 4), high[k]));
    }
    auto checked = decode_model_exchange(out);
    require(checked.meshes.size() == replacement.meshes.size(),
            "Imported mesh count did not round-trip");
    for (unsigned i = 0; i < checked.meshes.size(); ++i) {
        require(checked.meshes[i].indices == replacement.meshes[i].indices &&
                    checked.meshes[i].vertices.size() == replacement.meshes[i].vertices.size(),
                "Imported topology did not round-trip");
        for (unsigned v = 0; v < checked.meshes[i].vertices.size(); ++v) {
            auto &actual = checked.meshes[i].vertices[v];
            auto &expected = replacement.meshes[i].vertices[v];
            for (unsigned channel = 0; channel < 7; ++channel)
                for (unsigned k = 0; k < replacement.meshes[i].formats[channel][1]; ++k) {
                    auto value = expected.channels[channel][k];
                    if (replacement.meshes[i].formats[channel][0] != 3)
                        value = std::round(value);
                    require(actual.channels[channel][k] == value,
                            "Imported vertex attributes did not round-trip");
                }
            if (replacement.meshes[i].influences) {
                auto weights = quantize(expected, replacement.joints.size());
                for (unsigned k = 0; k < 4; ++k) {
                    require(std::abs(actual.weights[k] - weights[k] / 255.f) < 1e-6f,
                            "Imported weights did not round-trip");
                    if (weights[k])
                        require(actual.joints[k] == expected.joints[k],
                                "Imported bone influences did not round-trip");
                }
            }
        }
    }
    return out;
}
std::string serialize_model_exchange(const ModelExchange &exchange) {
    std::ostringstream out;
    out << "USUMSTUDIO_MODEL 1\nsource " << exchange.source << "\nbones " << exchange.joints.size()
        << '\n'
        << std::setprecision(9);
    for (auto &bone : exchange.joints) {
        out << "bone " << std::quoted(bone.name) << ' ' << bone.parent << ' '
            << unsigned(bone.flags);
        for (auto values : {bone.scale, bone.rotation, bone.translation})
            for (float value : values)
                out << ' ' << value;
        out << '\n';
    }
    out << "meshes " << exchange.meshes.size() << '\n';
    for (auto &mesh : exchange.meshes) {
        out << "mesh " << std::quoted(mesh.name) << ' ' << mesh.influences << ' '
            << mesh.vertices.size() << ' ' << mesh.indices.size() << "\nchannels";
        for (auto channel : mesh.formats)
            out << ' ' << channel[0] << ' ' << channel[1];
        out << "\ntexture " << std::quoted(mesh.texture) << '\n';
        for (auto &vertex : mesh.vertices) {
            out << "vertex";
            for (auto channel : vertex.channels)
                for (float value : channel)
                    out << ' ' << value;
            for (auto joint : vertex.joints)
                out << ' ' << joint;
            for (auto weight : vertex.weights)
                out << ' ' << weight;
            out << '\n';
        }
        out << "indices";
        for (auto i : mesh.indices)
            out << ' ' << i;
        out << '\n';
    }
    out << "end\n";
    return out.str();
}
ModelExchange parse_model_exchange(const std::string &text) {
    require(text.size() <= 256 * 1024 * 1024, "Model exchange file is too large");
    std::istringstream in(text);
    std::string word;
    auto token = [&](const char *expected) {
        require(bool(in >> word) && word == expected,
                std::string("Expected ") + expected + " in model exchange");
    };
    token("USUMSTUDIO_MODEL");
    unsigned version;
    require(bool(in >> version) && version == 1, "Unsupported model exchange version");
    ModelExchange out;
    token("source");
    require(bool(in >> out.source) && out.source.size() == 64, "Missing model fingerprint");
    token("bones");
    std::size_t count;
    require(bool(in >> count) && count <= 255, "Invalid imported bone count");
    for (std::size_t i = 0; i < count; ++i) {
        token("bone");
        Joint bone;
        unsigned flags;
        require(bool(in >> std::quoted(bone.name) >> bone.parent >> flags) && flags <= 255,
                "Invalid imported bone");
        bone.flags = std::uint8_t(flags);
        for (auto values : {&bone.scale, &bone.rotation, &bone.translation})
            for (auto &value : *values)
                require(bool(in >> value) && std::isfinite(value), "Invalid bone transform");
        out.joints.push_back(bone);
    }
    token("meshes");
    require(bool(in >> count) && count > 0 && count < 4096, "Invalid imported mesh count");
    std::size_t total = 0;
    for (std::size_t i = 0; i < count; ++i) {
        token("mesh");
        ExchangeMesh mesh;
        std::size_t vertices, indices;
        require(bool(in >> std::quoted(mesh.name) >> mesh.influences >> vertices >> indices) &&
                    vertices > 0 && vertices <= 65536 && indices > 0 && indices < 1000000 &&
                    indices % 3 == 0,
                "Invalid imported geometry counts");
        total += vertices;
        require(total < 4000000, "Imported model has too many vertices");
        token("channels");
        for (auto &channel : mesh.formats)
            require(bool(in >> channel[0] >> channel[1]) && channel[0] <= 3 && channel[1] <= 4,
                    "Invalid imported vertex channel");
        token("texture");
        require(bool(in >> std::quoted(mesh.texture)), "Invalid preview texture path");
        mesh.vertices.resize(vertices);
        for (auto &vertex : mesh.vertices) {
            token("vertex");
            for (auto &channel : vertex.channels)
                for (auto &value : channel)
                    require(bool(in >> value) && std::isfinite(value),
                            "Invalid imported vertex value");
            for (auto &joint : vertex.joints) {
                unsigned value;
                require(bool(in >> value) && value < 255, "Invalid imported joint index");
                joint = std::uint16_t(value);
            }
            for (auto &weight : vertex.weights)
                require(bool(in >> weight) && std::isfinite(weight), "Invalid imported weight");
        }
        token("indices");
        for (std::size_t n = 0; n < indices; ++n) {
            unsigned index;
            require(bool(in >> index) && index < vertices,
                    "Imported triangle index is out of range");
            mesh.indices.push_back(std::uint16_t(index));
        }
        out.meshes.push_back(std::move(mesh));
    }
    token("end");
    require(!(in >> word), "Unexpected model exchange content");
    return out;
}
}
