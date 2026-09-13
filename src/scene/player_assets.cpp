#include "scene/player_assets.h"
#include "scene/clothing_colors.h"
#include "scene/environment.h"
#include "field/area.h"
#include <algorithm>
namespace studio {
PlayerAssets load_player_assets(const std::filesystem::path &dump, unsigned appearance) {
    require(appearance < 2, "Unknown player appearance");
    PlayerAssets out;
    Archive characters(dump / TargetProfile::character_archive);
    auto base = Container::parse(characters.decoded(appearance), "CM");
    auto &motions = base.files.at(1);
    for (unsigned i = 0; i < 3; ++i) {
        auto slot = TargetProfile::player_motions[i];
        require(slot < u32(motions, 0), "Missing player motion");
        auto offset = 4 + std::size_t(u32(motions, 4 + slot * 4));
        require(offset > 4, "Empty player motion");
        out.motions[i] = decode_skeletal_motion(slice(motions, offset, motions.size() - offset));
        out.motions[i].looping = true;
    }
    Archive color_archive(dump / TargetProfile::player_colors[appearance]);
    auto colors = Container::parse(color_archive.decoded(0));
    auto color = [&](unsigned type, unsigned index) {
        auto &b = colors.files.at(type);
        require(index < u32(b, 0), "Outfit color index out of range");
        std::array<float, 3> c;
        for (unsigned k = 0; k < 3; ++k)
            c[k] = float(b[4 + index * (type == 6 ? 8 : 4) + k]) / 255;
        return c;
    };
    auto &bottoms = TargetProfile::player_outfits[appearance][4];
    Archive bottom_archive(dump / bottoms.archive);
    auto bottom_footer = bottom_archive.decoded(bottom_archive.size() - 1);
    require(bottoms.item < u32(bottom_footer, 0), "Default bottoms item is missing");
    bool short_legs = (slice(bottom_footer, 4 + bottoms.item * 8, 8)[6] & 1) != 0;
    for (auto &binding : TargetProfile::player_outfits[appearance]) {
        auto item_index =
            TargetProfile::outfit_item_index(binding.item, binding.variant, true, short_legs);
        Archive archive(dump / binding.archive);
        auto footer = archive.decoded(archive.size() - 1);
        require(item_index < u32(footer, 0), "Default outfit item is missing");
        auto item = slice(footer, 4 + item_index * 8, 8);
        auto resource = Container::parse(archive.decoded(u16(item, 0)), "CM");
        PlayerPart part;
        part.name = binding.name;
        part.model = resource.files.at(0);
        auto pack = ModelPack::parse(part.model);
        part.motions = out.motions;
        for (auto &r : pack.resources)
            if (r.category == 0) {
                auto skin = SkinnedModel::parse(r.bytes);
                if (!skin.joints.empty() && skin.joints.front().name.ends_with("_attach")) {
                    part.attachment =
                        skin.joints.front().name.substr(0, skin.joints.front().name.size() - 7);
                    auto &local = resource.files.at(1);
                    for (unsigned k = 0; k < 3; ++k) {
                        auto slot = TargetProfile::player_motions[k];
                        auto offset = slot < u32(local, 0) ? u32(local, 4 + slot * 4) : 0;
                        require(offset > 0, "Missing attached outfit motion");
                        part.motions[k] = decode_skeletal_motion(
                            slice(local, 4 + offset, local.size() - 4 - offset));
                        part.motions[k].looping = true;
                    }
                }
            }
        for (auto &r : pack.resources)
            if (r.category == 1) {
                auto name = text(slice(r.bytes, 40, 64));
                part.textures.emplace(name, decode_field_texture(r.bytes));
            }
        std::array<std::array<float, 3>, 6> tints;
        for (unsigned type = 0; type < 6; ++type)
            tints[type] = type == 1 ? (std::int8_t(item[4]) < 0 ? std::array<float, 3>{1, 1, 1}
                                                                : color(item[4], item[5]))
                                    : color(type, type == 0   ? 1
                                                  : type == 3 ? 4
                                                              : 0);
        apply_clothing_colors(part.textures, resource, tints);
        out.parts.push_back(std::move(part));
    }
    return out;
}
}
