#include "compiler/skinned.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <iomanip>
namespace studio {
namespace {
std::string quoted(const std::string &s) {
    std::ostringstream o;
    o << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\')
            o << '\\' << char(c);
        else if (c < 32 || c == '<' || c == '>' || c == '&')
            o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        else
            o << char(c);
    }
    o << '"';
    return o.str();
}
template <class T> void array_json(std::ostream &out, const T &values) {
    out << '[';
    bool first = true;
    for (auto v : values) {
        if (!first)
            out << ',';
        first = false;
        out << v;
    }
    out << ']';
}
}
Bytes SkinnedModel::rebuild(const std::string &joint, std::array<float, 3> delta,
                            bool descendants) const {
    int selected = -1;
    if (!joint.empty()) {
        for (std::size_t i = 0; i < joints.size(); ++i)
            if (joints[i].name == joint)
                selected = int(i);
        require(selected >= 0, "Selected joint was not found");
    }
    for (auto x : delta)
        require(std::isfinite(x) && std::abs(x) <= 100,
                "Skinned edit offset must be finite and within 100 game units");
    require(selected >= 0 || delta == std::array<float, 3>{}, "An edit offset requires a joint");
    Bytes result = model.original;
    for (const auto &j : joints) {
        auto pos = j.transform_offset;
        for (auto values : {j.scale, j.rotation, j.translation})
            for (auto v : values) {
                put_float(result, pos, v);
                pos += 4;
            }
    }
    std::vector<bool> affected(joints.size(), false);
    for (std::size_t i = 0; i < joints.size(); ++i) {
        for (int ancestor = int(i); ancestor >= 0;
             ancestor = joints[std::size_t(ancestor)].parent) {
            if (ancestor == selected) {
                affected[i] = true;
                break;
            }
            if (!descendants)
                break;
        }
    }
    for (const auto &mesh : meshes)
        for (const auto &v : mesh.vertices) {
            auto p = v.position;
            float weight = 0;
            for (unsigned k = 0; k < 4; ++k)
                if (affected[v.joints[k]])
                    weight += v.weights[k];
            weight = std::clamp(weight, 0.f, 1.f);
            if (weight > 0 && delta != std::array<float, 3>{})
                for (unsigned k = 0; k < 3; ++k)
                    p[k] += delta[k] * weight;
            for (unsigned k = 0; k < 3; ++k)
                put_float(result, v.position_offset + k * 4, p[k]);
        }
    if (delta != std::array<float, 3>{}) {
        auto expand = [&](std::size_t bounds) {
            for (unsigned k = 0; k < 3; ++k) {
                put_float(result, bounds + k * 4, f32(result, bounds + k * 4) - std::abs(delta[k]));
                put_float(result, bounds + 16 + k * 4,
                          f32(result, bounds + 16 + k * 4) + std::abs(delta[k]));
            }
        };
        expand(model.bounds_offset);
        std::set<std::size_t> sections;
        for (const auto &mesh : meshes)
            sections.insert(mesh.section_offset);
        for (auto s : sections)
            expand(s + 88);
    }
    auto checked = parse(result);
    require(checked.joints.size() == joints.size() && checked.meshes.size() == meshes.size(),
            "Skinned rebuild changed structure");
    return result;
}
std::string SkinnedModel::report() const {
    std::ostringstream o;
    o << "Joints: " << joints.size() << "\nSubmeshes: " << meshes.size() << '\n';
    for (std::size_t i = 0; i < joints.size(); ++i)
        o << "  [" << i << "] " << joints[i].name << " parent=" << joints[i].parent_name << '\n';
    for (auto &m : meshes)
        o << m.name << ": vertices=" << m.vertices.size() << " indices=" << m.indices.size()
          << " palette=" << m.palette.size() << " rigid=" << m.vertices[0].rigid << '\n';
    return o.str();
}
std::string SkinnedModel::json() const {
    std::ostringstream o;
    o << std::setprecision(9) << "{\"joints\":[";
    for (std::size_t i = 0; i < joints.size(); ++i) {
        if (i)
            o << ',';
        auto &j = joints[i];
        o << "{\"name\":" << quoted(j.name) << ",\"parent\":" << j.parent << ",\"bind\":";
        array_json(o, j.bind);
        o << ",\"inverse\":";
        array_json(o, j.inverse_bind);
        o << '}';
    }
    o << "],\"meshes\":[";
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        if (i)
            o << ',';
        auto &m = meshes[i];
        o << "{\"name\":" << quoted(m.name) << ",\"vertices\":[";
        for (std::size_t v = 0; v < m.vertices.size(); ++v) {
            if (v)
                o << ',';
            auto &vertex = m.vertices[v];
            o << "{\"p\":";
            array_json(o, vertex.bind_position);
            o << ",\"j\":";
            array_json(o, vertex.joints);
            o << ",\"w\":";
            array_json(o, vertex.weights);
            o << ",\"uv\":";
            array_json(o, vertex.uv);
            o << ",\"normal\":";
            array_json(o, vertex.normal);
            o << '}';
        }
        o << "],\"indices\":";
        array_json(o, m.indices);
        o << '}';
    }
    o << "]}";
    return o.str();
}
}
