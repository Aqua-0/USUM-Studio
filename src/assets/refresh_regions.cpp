#include "assets/refresh_regions.h"
#include "scene/environment.h"
#include <algorithm>
namespace studio {
namespace {
constexpr std::array<std::string_view, 41> labels{"Background",
                                                  "Body",
                                                  "Left ear",
                                                  "Right ear",
                                                  "Left eye (upper)",
                                                  "Left eye (lower)",
                                                  "Right eye (upper)",
                                                  "Right eye (lower)",
                                                  "Face",
                                                  "Chin",
                                                  "Left hand / high five",
                                                  "Painful (upper)",
                                                  "Painful (lower)",
                                                  "Ghost (upper)",
                                                  "Ghost (lower)",
                                                  "Burning (upper)",
                                                  "Burning (lower)",
                                                  "Electric (upper)",
                                                  "Electric (lower)",
                                                  "Gel (upper)",
                                                  "Gel (lower)",
                                                  "Sludge (upper)",
                                                  "Sludge (lower)",
                                                  "Cold (upper)",
                                                  "Cold (lower)",
                                                  "Disliked (upper)",
                                                  "Disliked (lower)",
                                                  "Liked (upper)",
                                                  "Liked (lower)",
                                                  "Ghost, disliked (upper)",
                                                  "Ghost, disliked (lower)",
                                                  "Ghost, liked (upper)",
                                                  "Ghost, liked (lower)",
                                                  "Gel, disliked (upper)",
                                                  "Gel, disliked (lower)",
                                                  "Gel, liked (upper)",
                                                  "Gel, liked (lower)",
                                                  "Sludge, disliked (upper)",
                                                  "Sludge, disliked (lower)",
                                                  "Sludge, liked (upper)",
                                                  "Sludge, liked (lower)"};
}
bool known_refresh_region(std::uint8_t id) {
    return id < labels.size() || id == 255;
}
std::string_view refresh_region_label(std::uint8_t id) {
    return id < labels.size() ? labels[id] : id == 255 ? "No hit" : "Unknown reaction";
}
std::string_view refresh_material_mode_label(std::uint8_t mode) {
    switch (mode) {
    case 0:
        return "Authored shading";
    case 1:
        return "Touch mask";
    case 2:
        return "Excluded";
    case 255:
        return "Unresolved";
    default:
        return "Unknown rendering rule";
    }
}
std::array<float, 4> refresh_region_color(std::uint8_t id) {
    if (id == 0 || id == 255)
        return {.18f, .2f, .23f, 1};
    if (!known_refresh_region(id))
        return {1, 0, 1, 1};
    constexpr std::array<std::array<float, 4>, 10> anatomy{{{.4f, .7f, .88f, 1},
                                                            {.75f, .5f, .9f, 1},
                                                            {.5f, .35f, .8f, 1},
                                                            {.95f, .75f, .25f, 1},
                                                            {.7f, .5f, .15f, 1},
                                                            {.5f, .85f, .35f, 1},
                                                            {.3f, .6f, .2f, 1},
                                                            {.25f, .8f, .8f, 1},
                                                            {.2f, .55f, .65f, 1},
                                                            {.9f, .6f, .4f, 1}}};
    if (id <= 10)
        return anatomy[id - 1];
    unsigned group = (id - 11) / 2;
    constexpr std::array<std::array<float, 4>, 15> effects{{{.95f, .25f, .3f, 1},
                                                            {.7f, .4f, .95f, 1},
                                                            {1, .45f, .1f, 1},
                                                            {1, .9f, .15f, 1},
                                                            {.2f, .75f, .95f, 1},
                                                            {.6f, .65f, .2f, 1},
                                                            {.65f, .9f, 1, 1},
                                                            {.95f, .3f, .55f, 1},
                                                            {.25f, .9f, .45f, 1},
                                                            {.8f, .25f, .65f, 1},
                                                            {.45f, .8f, .75f, 1},
                                                            {.9f, .4f, .65f, 1},
                                                            {.25f, .9f, .7f, 1},
                                                            {.8f, .45f, .3f, 1},
                                                            {.6f, .8f, .3f, 1}}};
    auto color = effects[group];
    if (id % 2 == 0)
        for (unsigned c = 0; c < 3; ++c)
            color[c] *= .75f;
    return color;
}
RefreshMaterialBinding bind_refresh_material(const RefreshRegionPack &pack,
                                             const SceneMaterial &material,
                                             const std::string &prefix) {
    auto rule = std::find_if(pack.materials.rbegin(), pack.materials.rend(), [&](const auto &item) {
        return item.material == material.name;
    });
    if (rule == pack.materials.rend())
        return {-1, false, "No Refresh rendering rule for this material."};
    if (rule->mode == 2)
        return {-1, true, "Excluded by its Refresh rendering rule."};
    if (rule->mode != 1)
        return {-1, false,
                "This Refresh rendering rule requires authored shading; category preview is "
                "unavailable."};
    if (material.inputs[0].transform[2] != 0)
        return {-1, false, "Rotated touch texture coordinates need further runtime verification."};
    for (std::size_t i = 0; i < pack.masks.size(); ++i)
        if (prefix + pack.masks[i].texture == material.texture_inputs[0])
            return {int(i), false, {}};
    return {-1, false, "No Refresh mask matches the material's first texture binding."};
}
RefreshRegionPack decode_refresh_regions(View source) {
    auto container = Container::parse(source, "PC");
    require(!container.files.empty(), "Refresh pack has no material settings resource");
    RefreshRegionPack result;
    auto &settings = container.files[0];
    for (std::size_t at = 0; at + 33 <= settings.size(); at += 33) {
        auto name = text(slice(settings, at, 32));
        if (!name.empty())
            result.materials.push_back({std::move(name), settings[at + 32]});
    }
    for (std::size_t child = 1; child < container.files.size(); ++child) {
        auto &bytes = container.files[child];
        if (bytes.empty())
            continue;
        require(u32(bytes, 0) == 0x15041213, "Invalid Refresh mask texture");
        auto format = u16(bytes, 108);
        require((format == 0x25 || format == 4) && u16(bytes, 110) == 1,
                "Refresh masks require an L8 or RGBA8 texture with one authored mip level");
        auto width = u16(bytes, 104), height = u16(bytes, 106);
        require(u32(bytes, 24) == std::size_t(width) * height * (format == 4 ? 4 : 1),
                "Refresh mask payload does not match its dimensions");
        auto image = decode_field_texture(bytes);
        RefreshRegionMask mask;
        mask.child = child;
        mask.texture = text(slice(bytes, 40, 64));
        mask.width = width;
        mask.height = height;
        mask.format = format;
        if (format != 0x25)
            mask.read_only_reason =
                "RGBA8 touch masks are inspectable; pixel editing currently requires L8.";
        require(!mask.texture.empty(), "Refresh mask texture has no resource name");
        mask.ids.reserve(std::size_t(width) * height);
        for (std::size_t at = 0; at < image.rgba.size(); at += 4) {
            auto id = image.rgba[at];
            mask.ids.push_back(id);
            ++mask.counts[id];
        }
        result.masks.push_back(std::move(mask));
    }
    return result;
}
Bytes replace_refresh_region_pixels(View source, std::size_t child, View ids) {
    auto pack = decode_refresh_regions(source);
    auto found = std::find_if(pack.masks.begin(), pack.masks.end(), [&](const auto &mask) {
        return mask.child == child;
    });
    require(found != pack.masks.end(), "Refresh texture resource was not found");
    require(ids.size() == found->ids.size(),
            "Refresh edits must retain the source texture dimensions");
    if (std::equal(ids.begin(), ids.end(), found->ids.begin()))
        return Bytes(source.begin(), source.end());
    require(found->read_only_reason.empty(), found->read_only_reason);
    for (std::size_t i = 0; i < ids.size(); ++i)
        require(ids[i] == found->ids[i] || known_refresh_region(ids[i]),
                "Refresh edit introduces an unknown reaction ID");
    Bytes result(source.begin(), source.end());
    auto payload = std::size_t(u32(source, 4 + 4 * child)) + 128;
    // Touch IDs are categorical; patch L8 texels without filtering or rebuilding headers.
    for (unsigned y = 0; y < found->height; ++y)
        for (unsigned x = 0; x < found->width; ++x) {
            auto native_y = unsigned(found->height) - 1 - y;
            auto tile = (native_y / 8) * (found->width / 8) + x / 8;
            unsigned morton = 0;
            for (unsigned bit = 0; bit < 3; ++bit)
                morton |= ((x >> bit) & 1) << (2 * bit) | ((native_y >> bit) & 1) << (2 * bit + 1);
            result[payload + std::size_t(tile) * 64 + morton] =
                ids[std::size_t(y) * found->width + x];
        }
    return result;
}
}
