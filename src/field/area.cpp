#include "compiler/asset.h"
#include "field/area.h"
#include "formats/compression.h"
#include "compiler/skinned.h"
#include "compiler/motion.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace studio {
void validate_static_alignment(View resources) {
    const auto alignment = TargetProfile::resource_alignment;
    auto validate_container = [&](View bytes, const std::string &tag) {
        auto c = Container::parse(bytes, tag);
        for (std::size_t i = 0; i <= c.files.size(); ++i)
            require(u32(bytes, 4 + 4 * i) % alignment == 0,
                    tag + " resource offset violates 128-byte alignment");
        return c;
    };
    auto area = validate_container(resources, "AS");
    for (const auto &bytes : area.files) {
        auto object = validate_container(bytes, "SM");
        require(object.files.size() == 29, "Unsupported static resource pack shape");
        for (auto lod : {1u, 10u, 19u}) {
            if (object.files[lod].empty())
                continue;
            auto pack = ModelPack::parse(object.files[lod]);
            for (const auto &r : pack.resources)
                require(u32(pack.original, r.address_field) % alignment == 0,
                        "Model, texture or shader payload violates 128-byte alignment");
        }
    }
}
Bytes append_placement(View b, std::size_t template_index, std::uint16_t model, std::uint32_t event,
                       std::array<float, 3> position, bool unconditional) {
    auto placements = read_placements(b);
    require(template_index < placements.size(), "Template placement index is out of range");
    const auto &donor = placements[template_index];
    require(donor.alias == 0, "Alias placements cannot be used as insertion templates");
    for (const auto &p : placements)
        require(p.event != event, "Event ID already exists in target static placements");
    for (auto v : position)
        require(std::isfinite(v) && std::abs(v) < 1e7, "Invalid insertion position");
    auto end = 4 + placements.size() * 56;
    Bytes out(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(end));
    auto original = slice(b, 4 + template_index * 56, 56);
    Bytes record(original.begin(), original.end());
    put16(record, 48, model);
    put32(record, 44, event);
    put32(record, 52, 0);
    if (unconditional) {
        put32(record, 32, 0);
        put32(record, 36, 0);
        put32(record, 40, 0);
    }
    for (std::size_t i = 0; i < 3; ++i)
        put_float(record, 4 + i * 4, position[i]);
    append(out, record);
    append(out, slice(b, end, b.size() - end));
    put32(out, 0, narrow(placements.size() + 1));
    for (std::size_t i = 0; i < placements.size(); ++i)
        if (placements[i].collision)
            put32(out, 4 + i * 56 + 52, placements[i].collision + 56);
    auto check = read_placements(out);
    require(check.size() == placements.size() + 1, "Placement count mismatch after insertion");
    return out;
}
Bytes attach_box_collision(View zone, std::size_t placement, BoxSize size) {
    auto rows = read_placements(zone);
    require(placement < rows.size(), "Collision placement is out of range");
    const auto &row = rows[placement];
    require(!row.collision, "Placement already has collision");
    for (auto v : {size.x, size.y, size.z})
        require(std::isfinite(v) && v > 0 && v < 1e6, "Invalid box collision size");
    require(std::abs(row.rotation[0]) < 1e-5f && std::abs(row.rotation[2]) < 1e-5f,
            "Box collision supports upright placements only");
    Bytes out(zone.begin(), zone.end());
    out.resize(aligned(out.size(), 4));
    auto offset = out.size();
    Bytes shape(48);
    put32(shape, 0, 1);
    put32(shape, 4, 1);
    for (std::size_t i = 0; i < 3; ++i)
        put_float(shape, 8 + i * 4, row.position[i]);
    for (std::size_t i = 0; i < 4; ++i)
        put_float(shape, 20 + i * 4, row.rotation[i]);
    put_float(shape, 36, size.x / 2);
    put_float(shape, 40, size.y);
    put_float(shape, 44, size.z / 2);
    append(out, shape);
    put32(out, 4 + placement * 56 + 52, narrow(offset));
    require(read_placements(out)[placement].collision == offset,
            "Collision pointer readback failed");
    return out;
}
FieldArea::FieldArea(const Archive &archive, std::size_t index) : archive_(archive), index_(index) {
    require(index < archive.size() / TargetProfile::area_stride,
            "Field area index is out of range");
    placements_ = Container::parse(
        archive.decoded(index * TargetProfile::area_stride + TargetProfile::placement_slot), "ED");
    auto static_bytes =
        archive.decoded(index * TargetProfile::area_stride + TargetProfile::static_resource_slot);
    validate_static_alignment(static_bytes);
    resources_ = Container::parse(static_bytes, "AS");
    require(placements_.files.size() > TargetProfile::static_pack, "Missing static placement pack");
}
std::size_t FieldArea::resource_index(std::uint16_t model) const {
    std::optional<std::size_t> match;
    for (std::size_t i = 0; i < resources_.files.size(); ++i) {
        auto pack = Container::parse(resources_.files[i], "SM");
        require(pack.files.size() == 29, "Unsupported static resource pack shape");
        if (u16(pack.files[0], 0) == model) {
            require(!match, "Duplicate static model identity");
            match = i;
        }
    }
    require(match.has_value(), "Static model ID is absent from area resources");
    return *match;
}
std::string FieldArea::inspect() const {
    std::ostringstream out;
    out << "Field area " << index_ << "\nStatic resources: " << resources_.files.size() << "\n";
    for (const auto &bytes : resources_.files) {
        auto pack = Container::parse(bytes, "SM");
        auto id = u16(pack.files.at(0), 0);
        out << "  Model " << id << " behavior=" << unsigned(pack.files[0].at(2))
            << " animated=" << unsigned(pack.files[0].at(3))
            << " door=" << unsigned(pack.files[0].at(4));
        try {
            auto models = ModelPack::parse(pack.files.at(1));
            for (const auto &r : models.resources)
                if (r.category == 0) {
                    auto m = Model::parse(r.bytes);
                    out << "  " << r.name << "  meshes=" << m.names[3].size()
                        << " materials=" << m.names[2].size() << " joints=" << m.bones;
                    try {
                        compile_box(m, {});
                        out << " geometry-compatible";
                        std::string preview;
                        compile_resource(id, {}, preview);
                        out << " box-compatible";
                    } catch (const std::exception &e) {
                        out << "  [" << e.what() << "]";
                    }
                }
        } catch (const std::exception &e) {
            out << "  [" << e.what() << "]";
        }
        out << '\n';
    }
    auto zones = Container::parse(placements_.files[TargetProfile::static_pack], "ES");
    std::set<std::uint16_t> aliases;
    for (std::size_t z = 0; z < zones.files.size(); ++z) {
        auto rows = read_placements(zones.files[z]);
        out << "Zone index " << z << ": " << rows.size() << " placements\n";
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto &p = rows[i];
            resource_index(p.model);
            if (p.alias)
                aliases.insert(p.alias);
            out << "  [" << i << "] model=" << p.model << " event=" << p.event
                << " alias=" << p.alias << " version=" << p.version << " condition=" << p.flags
                << ':' << p.condition << " collision=" << p.collision
                << " position=" << p.position[0] << ',' << p.position[1] << ',' << p.position[2]
                << '\n';
        }
    }
    out << "Source profile actor capacity: " << TargetProfile::actor_capacity
        << "; distinct alias values: " << aliases.size() << "\n";
    return out.str();
}
Bytes FieldArea::compile_resource(std::uint16_t donor, BoxSize size, std::string &preview,
                                  const TextureImage *texture) const {
    auto pack = Container::parse(resources_.files[resource_index(donor)], "SM");
    require(pack.files[0].size() >= 8, "Truncated static model metadata");
    require(pack.files[0][2] == 255 && pack.files[0][3] == 0 && pack.files[0][4] == 0,
            "Box profile requires a plain, non-animated, non-door static resource");
    for (std::size_t i = 2; i < pack.files.size(); ++i)
        require(pack.files[i].empty(),
                "Box profile requires no animation, alternate LOD or animated collision payloads");
    auto models = ModelPack::parse(pack.files[1]);
    std::optional<std::size_t> primary;
    for (std::size_t i = 0; i < models.resources.size(); ++i)
        if (models.resources[i].category == 0) {
            require(!primary, "Box profile requires one model per pack");
            primary = i;
        }
    require(primary.has_value(), "Static resource has no model");
    auto build = compile_box(Model::parse(models.resources[*primary].bytes), size);
    preview = build.preview_obj;
    pack.files[1] = models.replace(*primary, build.model, TargetProfile::resource_alignment);
    if (texture)
        pack.files[1] = bind_texture(pack.files[1], *texture);
    return pack.write(TargetProfile::resource_alignment);
}
std::map<std::size_t, Bytes> FieldArea::build_box(std::uint16_t donor, BoxSize size,
                                                  std::string &preview,
                                                  const TextureImage *texture) const {
    auto resources = resources_;
    resources.files[resource_index(donor)] = compile_resource(donor, size, preview, texture);
    auto bytes = resources.write(TargetProfile::resource_alignment);
    require(Container::parse(bytes, "AS").files == resources.files,
            "Static resource readback mismatch");
    validate_static_alignment(bytes);
    auto compressed = compress(bytes);
    require(decompress(compressed) == bytes, "Resource compression mismatch");
    return {{index_ * TargetProfile::area_stride + TargetProfile::static_resource_slot,
             std::move(compressed)}};
}
std::map<std::size_t, Bytes> FieldArea::add_box(std::uint16_t donor, BoxSize size, std::size_t zone,
                                                std::size_t placement, std::uint16_t model,
                                                std::uint32_t event, std::array<float, 3> position,
                                                std::string &preview, bool unconditional,
                                                const TextureImage *texture, bool collision) const {
    auto resource = Container::parse(compile_resource(donor, size, preview, texture), "SM");
    return insert_resource(resource, donor, size, zone, placement, model, event, position,
                           unconditional, collision);
}
std::map<std::size_t, Bytes> FieldArea::add_animated(std::uint16_t donor, View model_pack,
                                                     View motion, std::size_t zone,
                                                     std::size_t placement, std::uint16_t model,
                                                     std::uint32_t event,
                                                     std::array<float, 3> position) const {
    auto resource = Container::parse(resources_.files[resource_index(donor)], "SM");
    require(resource.files[0][2] == 255 && resource.files[0][3] == 0 && resource.files[0][4] == 0,
            "Animated prop needs a plain static wrapper template");
    for (std::size_t i = 2; i < resource.files.size(); ++i)
        require(resource.files[i].empty(),
                "Animated prop template must have no alternate resources");
    auto pack = ModelPack::parse(model_pack);
    std::size_t models = 0;
    auto clip = SkeletonMotion::read(motion);
    for (const auto &entry : pack.resources)
        if (entry.category == 0) {
            ++models;
            auto skin = SkinnedModel::parse(entry.bytes);
            for (auto &track : clip.tracks)
                require(std::any_of(skin.joints.begin(), skin.joints.end(),
                                    [&](auto &joint) {
                                        return joint.name == track.joint;
                                    }),
                        "Motion target joint is missing from prop rig");
        }
    require(models == 1, "Animated prop needs one model");
    resource.files[0][3] = 1;
    resource.files[1] = Bytes(model_pack.begin(), model_pack.end());
    resource.files[2] = Bytes(motion.begin(), motion.end());
    return insert_resource(resource, donor, {}, zone, placement, model, event, position, true,
                           false);
}
std::map<std::size_t, Bytes> FieldArea::insert_resource(Container resource, std::uint16_t donor,
                                                        BoxSize size, std::size_t zone,
                                                        std::size_t placement, std::uint16_t model,
                                                        std::uint32_t event,
                                                        std::array<float, 3> position,
                                                        bool unconditional, bool collision) const {
    for (const auto &b : resources_.files)
        require(u16(Container::parse(b, "SM").files.at(0), 0) != model,
                "New model ID already exists in this area");
    auto zones = Container::parse(placements_.files[TargetProfile::static_pack], "ES");
    require(zone < zones.files.size(), "Zone index is out of range");
    std::size_t aliases = 0;
    for (const auto &b : zones.files)
        for (const auto &p : read_placements(b)) {
            if (p.alias)
                ++aliases;
            require(p.event != event, "Event ID already exists in this area's static placements");
        }
    auto rows = read_placements(zones.files[zone]);
    require(placement < rows.size() && rows[placement].model == donor,
            "Placement template must reference the selected donor");
    auto ordinary = std::count_if(rows.begin(), rows.end(), [](const auto &p) {
        return p.alias == 0;
    });
    require(std::size_t(ordinary) + aliases + 1 <= TargetProfile::actor_capacity,
            "New placement exceeds conservative static actor budget");
    put16(resource.files[0], 0, model);
    auto resources = resources_;
    resources.files.push_back(resource.write(TargetProfile::resource_alignment));
    zones.files[zone] =
        append_placement(zones.files[zone], placement, model, event, position, unconditional);
    if (collision)
        zones.files[zone] = attach_box_collision(zones.files[zone], rows.size(), size);
    auto placements = placements_;
    placements.files[TargetProfile::static_pack] = zones.write();
    auto r = resources.write(TargetProfile::resource_alignment), p = placements.write();
    auto cr = compress(r), cp = compress(p);
    validate_static_alignment(r);
    require(decompress(cr) == r && decompress(cp) == p, "Area compression mismatch");
    return {
        {index_ * TargetProfile::area_stride + TargetProfile::static_resource_slot, std::move(cr)},
        {index_ * TargetProfile::area_stride + TargetProfile::placement_slot, std::move(cp)}};
}
}
