#include "formats/model.h"
#include <algorithm>
#include <map>
#include <set>

namespace studio {
ModelPack ModelPack::parse(View b) {
    require(u32(b, 0) == 0x10000, "Expected model resource pack");
    ModelPack pack;
    pack.original.assign(b.begin(), b.end());
    std::size_t count = 0;
    for (std::size_t i = 0; i < 5; ++i)
        count += u32(b, 4 + 4 * i);
    require(count <= 0x20000, "Model resource table is too large");
    slice(b, 24, count * 4);
    struct Entry {
        std::size_t category, index, field, address;
        std::string name;
    };
    std::vector<Entry> entries;
    std::size_t pointer = 24;
    std::set<std::size_t> addresses;
    for (std::size_t category = 0; category < 5; ++category) {
        for (std::size_t i = 0; i < u32(b, 4 + 4 * category); ++i, pointer += 4) {
            auto pos = u32(b, pointer);
            if (!pos)
                continue;
            require(pos >= 24 + count * 4, "Model name overlaps pointer table");
            auto length = slice(b, pos, 1)[0];
            auto name = text(slice(b, pos + 1, length));
            auto field = std::size_t(pos) + 1 + length;
            auto address = u32(b, field);
            require(address > field + 3 && address < b.size(), "Invalid model resource address");
            require(addresses.insert(address).second,
                    "Aliased model resource payloads are unsupported");
            entries.push_back({category, i, field, address, name});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &c) {
        return a.address < c.address;
    });
    for (std::size_t i = 0; i < entries.size(); ++i) {
        auto e = entries[i];
        auto end = i + 1 < entries.size() ? entries[i + 1].address : b.size();
        auto v = slice(b, e.address, end - e.address);
        pack.resources.push_back({e.category, e.index, e.name, Bytes(v.begin(), v.end()), e.field});
    }
    return pack;
}
Bytes ModelPack::replace(std::size_t resource, View replacement, std::size_t alignment) const {
    require(resource < resources.size(), "Model resource index is out of range");
    aligned(0, alignment);
    require(original.size() % alignment == 0,
            "Model pack size does not match the target alignment");
    for (const auto &r : resources)
        require(u32(original, r.address_field) % alignment == 0,
                "Model resource address does not match the target alignment");
    auto target = u32(original, resources[resource].address_field);
    auto size = resources[resource].bytes.size();
    Bytes padded(replacement.begin(), replacement.end());
    padded.resize(aligned(padded.size(), alignment));
    Bytes out;
    append(out, slice(original, 0, target));
    append(out, padded);
    append(out, slice(original, target + size, original.size() - target - size));
    auto delta = std::int64_t(padded.size()) - std::int64_t(size);
    for (const auto &r : resources) {
        require(r.address_field < target, "Model metadata is interleaved with resource payloads");
        auto addr = u32(original, r.address_field);
        if (addr > target)
            put32(out, r.address_field,
                  narrow(static_cast<std::size_t>(std::int64_t(addr) + delta)));
    }
    auto check = parse(out);
    require(check.resources.size() == resources.size(), "Model resource count changed");
    for (std::size_t i = 0; i < resources.size(); ++i)
        require(check.resources[i].bytes == (i == resource ? padded : resources[i].bytes),
                "Model pack readback mismatch");
    return out;
}
Model Model::parse(View b) {
    require(u32(b, 0) == 0x15122117, "Unsupported model version");
    Model m;
    m.original.assign(b.begin(), b.end());
    std::size_t pos = 16;
    while (pos + 16 <= b.size()) {
        auto kind = text(slice(b, pos, 8));
        if (kind.empty())
            break;
        require(kind == "gfmodel" || kind == "material" || kind == "mesh",
                "Unsupported model section: " + kind);
        auto size = std::size_t(u32(b, pos + 8)) + 16;
        slice(b, pos, size);
        m.sections.push_back({kind, pos, size});
        pos += size;
    }
    require(!m.sections.empty() && m.sections.front().kind == "gfmodel", "Missing model metadata");
    require(std::all_of(b.begin() + static_cast<std::ptrdiff_t>(pos), b.end(),
                        [](auto v) {
                            return v == 0;
                        }),
            "Unknown trailing model payload");
    auto end = m.sections.front().offset + m.sections.front().size;
    pos = 32;
    for (auto &table : m.names) {
        require(pos <= end && end - pos >= 4, "Model name table exceeds metadata");
        auto count = u32(b, pos);
        pos += 4;
        require(count < 4096 && std::size_t(count) * 68 <= end - pos, "Invalid model name table");
        for (std::size_t i = 0; i < count; ++i, pos += 68)
            table.push_back(text(slice(b, pos + 4, 64)));
    }
    m.bounds_offset = pos;
    pos += 96;
    auto opaque = std::size_t(u32(b, pos)) + u32(b, pos + 4);
    pos += 16;
    require(pos <= end && opaque <= end - pos, "Invalid model opaque metadata extent");
    pos += opaque;
    m.bones = u32(b, pos);
    require(pos + 16 <= end, "Missing model skeleton header");
    require(std::count_if(m.sections.begin(), m.sections.end(),
                          [](const auto &s) {
                              return s.kind == "material";
                          }) == static_cast<std::ptrdiff_t>(m.names[2].size()),
            "Material section count does not match model metadata");
    require(std::count_if(m.sections.begin(), m.sections.end(),
                          [](const auto &s) {
                              return s.kind == "mesh";
                          }) == static_cast<std::ptrdiff_t>(m.names[3].size()),
            "Mesh section count does not match model metadata");
    return m;
}
std::vector<CommandWrite> commands(View b) {
    require(b.size() % 8 == 0, "Command stream must contain whole command pairs");
    std::vector<CommandWrite> out;
    for (std::size_t pos = 0; pos < b.size();) {
        auto value = u32(b, pos), header = u32(b, pos + 4);
        auto count = ((header >> 20) & 0x7ff) + 1;
        auto reg = header & 0xffff, mask = (header >> 16) & 15;
        slice(b, pos, 4 * (std::size_t(count) + 1));
        out.push_back(
            {static_cast<std::uint16_t>(reg), static_cast<std::uint8_t>(mask), value, pos});
        for (std::size_t i = 1; i < count; ++i) {
            auto dest = reg + ((header >> 31) ? i : 0);
            require(dest <= 0xffff, "Command register overflow");
            out.push_back({static_cast<std::uint16_t>(dest), static_cast<std::uint8_t>(mask),
                           u32(b, pos + 4 * (i + 1)), pos + 4 * (i + 1)});
        }
        pos = aligned(pos + 4 * (std::size_t(count) + 1), 8);
    }
    return out;
}
VertexLayout vertex_layout(View b) {
    std::map<unsigned, std::uint32_t> regs;
    for (const auto &c : commands(b)) {
        auto &value = regs[c.reg];
        for (unsigned lane = 0; lane < 4; ++lane)
            if (c.mask & (1 << lane)) {
                auto mask = 0xffu << (lane * 8);
                value = (value & ~mask) | (c.value & mask);
            }
    }
    auto formats = std::uint64_t(regs[0x201]) | (std::uint64_t(regs[0x202]) << 32);
    auto mapping = std::uint64_t(regs[0x204]) | (std::uint64_t(regs[0x205] & 0xffff) << 32);
    auto semantics = std::uint64_t(regs[0x2bb]) | (std::uint64_t(regs[0x2bc]) << 32);
    auto count = (regs[0x205] >> 28) & 15;
    VertexLayout layout{(regs[0x205] >> 16) & 255, {}};
    require(count > 0 && count <= 12 && layout.stride > 0, "Unsupported vertex buffer layout");
    std::size_t offset = 0;
    std::set<unsigned> used;
    for (unsigned i = 0; i < count; ++i) {
        auto attribute = unsigned((mapping >> (4 * i)) & 15);
        require(attribute < 12, "Vertex buffer padding attributes are unsupported");
        auto format = unsigned((formats >> (4 * attribute)) & 15);
        auto semantic = unsigned((semantics >> (4 * attribute)) & 15);
        unsigned kind = format & 3, elements = (format >> 2) + 1;
        require(semantic <= 8 && used.insert(semantic).second,
                "Unsupported or repeated vertex semantic");
        if (kind >= 2)
            offset = aligned(offset, 2);
        layout.attributes.push_back({semantic, kind, elements, offset});
        offset += elements * (kind == 3 ? 4 : kind == 2 ? 2 : 1);
    }
    require(offset <= layout.stride, "Vertex attributes exceed stride");
    return layout;
}
}
