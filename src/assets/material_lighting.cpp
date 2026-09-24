#include "assets/material_document.h"
#include <algorithm>
#include <cmath>
namespace studio {
namespace {
std::size_t metadata(View bytes) {
    std::size_t p = 16;
    for (unsigned i = 0; i < 4; ++i)
        p += 5 + slice(bytes, p + 4, 1)[0];
    slice(bytes, p, 168);
    return p;
}
std::size_t command_header(View bytes) {
    auto p = metadata(bytes) + 168;
    auto count = u32(bytes, p);
    p += 4;
    for (unsigned i = 0; i < count; ++i)
        p += 5 + slice(bytes, p + 4, 1)[0] + 42;
    return aligned(p, 16);
}
void command(Bytes &stream, unsigned reg, std::uint32_t value) {
    append32(stream, value);
    append32(stream, 0xf0000u | reg);
}
}
std::map<std::uint32_t, LightingTable> MaterialDocument::lighting_tables() const {
    return model_lighting_tables(Model::parse(compile()));
}
std::array<MaterialLightingBinding, 3>
MaterialDocument::lighting_bindings(std::size_t material) const {
    auto bytes = compile();
    auto parsed = Model::parse(bytes);
    std::size_t index = 0;
    for (auto &section : parsed.sections)
        if (section.kind == "material") {
            if (index++ != material)
                continue;
            auto b = slice(bytes, section.offset, section.size);
            auto p = metadata(b);
            auto &scene = model.scene->materials.at(material);
            std::array<MaterialLightingBinding, 3> result;
            for (unsigned channel = 0; channel < 3; ++channel)
                result[channel] = {
                    u32(b, p + channel * 4), unsigned(scene.reflection_inputs[channel][0]),
                    scene.reflection_inputs[channel][1] > .5f, scene.reflection_inputs[channel][2]};
            return result;
        }
    throw std::runtime_error("Select a model material");
}
void MaterialDocument::edit_lighting_bindings(
    std::size_t material, const std::array<MaterialLightingBinding, 3> &bindings) {
    auto previous = lighting_bindings(material);
    if (bindings == previous)
        return;
    auto tables = lighting_tables();
    const float scales[] = {1, 2, 4, 8, 0, 0, .25f, .5f};
    for (unsigned channel = 0; channel < 3; ++channel) {
        const auto &binding = bindings[channel];
        if (binding == previous[channel])
            continue;
        require(!binding.table || tables.contains(binding.table),
                "Lighting table is not in this model");
        require(binding.input <= 5 && std::isfinite(binding.scale) && binding.scale > 0 &&
                    std::find(std::begin(scales), std::end(scales), binding.scale) !=
                        std::end(scales),
                "Unsupported lighting input or scale");
    }
    auto bytes = compile();
    auto parsed = Model::parse(bytes);
    Bytes result(bytes.begin(), bytes.begin() + 16);
    std::size_t index = 0;
    for (auto &section : parsed.sections) {
        auto b = slice(bytes, section.offset, section.size);
        if (section.kind != "material" || index++ != material) {
            append(result, b);
            continue;
        }
        auto header = command_header(b);
        Bytes edited(b.begin(), b.begin() + header + 32);
        auto p = metadata(b);
        auto original = slice(b, header + 32, u32(b, header));
        Bytes stream(original.begin(), original.end());
        std::map<unsigned, std::uint32_t> registers;
        if (edits_.at(material).combiners)
            registers = decode_material_shader(fragment_resource(material)).registers;
        else
            for (const auto &link : model.resources)
                if (link.role == "Shader" &&
                    link.name == model.scene->materials.at(material).fragment_shader) {
                    registers =
                        decode_material_shader(
                            asset_resource(model.sources.at(link.source).original, link.path))
                            .registers;
                    break;
                }
        for (auto c : commands(original)) {
            for (unsigned lane = 0; lane < 4; ++lane)
                if (c.mask & (1u << lane)) {
                    auto mask = 255u << (lane * 8);
                    registers[c.reg] = (registers[c.reg] & ~mask) | (c.value & mask);
                }
        }
        for (auto c : commands(original))
            if (c.reg == 0x23d) {
                stream.resize(c.offset);
                break;
            }
        for (unsigned channel = 0; channel < 3; ++channel) {
            const auto &binding = bindings[channel];
            if (binding == previous[channel])
                continue;
            auto shift = 16 + channel * 4;
            put32(edited, p + channel * 4, binding.table);
            registers[0x1d0] = (registers[0x1d0] & ~(1u << (shift + 1))) |
                               (binding.unsigned_range ? 0 : 1u << (shift + 1));
            registers[0x1d1] = (registers[0x1d1] & ~(7u << shift)) | (binding.input << shift);
            auto scale = unsigned(std::find(std::begin(scales), std::end(scales), binding.scale) -
                                  std::begin(scales));
            registers[0x1d2] = (registers[0x1d2] & ~(7u << shift)) | (scale << shift);
        }
        for (unsigned reg : {0x1d0u, 0x1d1u, 0x1d2u})
            command(stream, reg, registers[reg]);
        if (stream.size() % 16 == 0)
            command(stream, 0, 0);
        command(stream, 0x23d, 1);
        put32(edited, header, narrow(stream.size()));
        append(edited, stream);
        edited.resize(aligned(edited.size(), 16), 0);
        put32(edited, 8, narrow(edited.size() - 16));
        append(result, edited);
    }
    Model::parse(result);
    const auto link = model.resources.at(model.material_resources.at(material));
    const auto source = model.sources.at(link.source);
    auto members = compiled_members();
    auto member = members.contains(source.member) ? members.at(source.member) : source.original;
    members[source.member] = replace_asset_resource(member, link.path, result);
    commit();
    replace_members(members);
}
void MaterialDocument::edit_lighting_table(std::uint32_t table, const LightingTable &values) {
    auto tables = lighting_tables();
    require(tables.contains(table), "Lighting table is not in this model");
    for (auto value : values.values)
        require(std::isfinite(value) && value >= 0 && value <= 1,
                "Lighting samples must be between zero and one");
    if (values.values == tables.at(table).values)
        return;
    auto bytes = compile();
    auto parsed = Model::parse(bytes);
    auto p = parsed.bounds_offset + 96;
    p += 16 + u32(bytes, p) + u32(bytes, p + 4);
    auto bones = u32(bytes, p);
    p += 16;
    for (unsigned i = 0; i < bones; ++i) {
        p += 1 + slice(bytes, p, 1)[0];
        p += 1 + slice(bytes, p, 1)[0];
        p += 37;
    }
    p = aligned(p, 16);
    auto count = u32(bytes, p), length = u32(bytes, p + 4);
    p = aligned(p + 8, 16);
    std::array<unsigned, 256> quantized{};
    for (unsigned i = 0; i < 256; ++i)
        quantized[i] = unsigned(std::lround(values.values[i] * 4095));
    bool replaced = false;
    for (unsigned record = 0; record < count; ++record, p += 16 + length) {
        if (u32(bytes, p) != table)
            continue;
        unsigned index = 0;
        std::array<bool, 256> written{};
        for (auto c : commands(slice(bytes, p + 16, length))) {
            if (c.reg == 0x1c5)
                index = c.value & 255;
            else if (c.reg >= 0x1c8 && c.reg <= 0x1cf) {
                require(index < 256 && (c.mask & 7) == 7, "Unsupported lighting sample command");
                float delta =
                    (float(quantized[(index + 1) % 256]) - float(quantized[index])) / 4095.f;
                auto magnitude = unsigned(std::lround(std::abs(delta) * 2047));
                auto value = (c.value & 0xff000000u) | quantized[index] | (magnitude << 12) |
                             (delta < 0 ? 1u << 23 : 0);
                put32(bytes, p + 16 + c.offset, value);
                written[index++] = true;
            }
        }
        require(std::all_of(written.begin(), written.end(),
                            [](bool v) {
                                return v;
                            }),
                "Incomplete lighting table");
        replaced = true;
        break;
    }
    require(replaced, "Lighting table record is missing");
    model_lighting_tables(Model::parse(bytes));
    const auto link = model.resources.at(model.material_resources.front());
    const auto source = model.sources.at(link.source);
    auto members = compiled_members();
    auto member = members.contains(source.member) ? members.at(source.member) : source.original;
    members[source.member] = replace_asset_resource(member, link.path, bytes);
    commit();
    replace_members(members);
}
}
