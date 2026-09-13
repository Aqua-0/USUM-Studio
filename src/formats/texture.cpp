#include "formats/texture.h"
#include "formats/texture_codec.h"
#include "core/digest.h"
#include <algorithm>
#include <cmath>

namespace studio {
namespace {
void validate(const TextureImage &image) {
    auto valid = [](unsigned n) {
        return n >= 8 && n <= 1024 && (n & (n - 1)) == 0;
    };
    require(valid(image.width) && valid(image.height),
            "Texture dimensions must be powers of two from 8 to 1024");
    require(image.rgba.size() == std::size_t(image.width) * image.height * 4,
            "Texture pixel count mismatch");
}
std::uint32_t name_hash(const std::string &name) {
    std::uint32_t h = 0x01000193;
    for (unsigned char c : name)
        h = (h * 0x01000193) ^ c;
    return h;
}
void replace_name(Bytes &bytes, std::size_t offset, const std::string &name) {
    slice(bytes, offset, name.size());
    std::copy(name.begin(), name.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}
}
TextureImage read_tga(View b) {
    slice(b, 0, 18);
    require(b[1] == 0 && b[2] == 2, "Texture import requires an uncompressed true-color TGA");
    require((b[16] == 24 || b[16] == 32) && (b[17] & 0xc0) == 0, "Unsupported TGA pixel layout");
    require((b[17] & 15) == 0 || (b[16] == 32 && (b[17] & 15) == 8),
            "Unsupported TGA alpha layout");
    TextureImage image{u16(b, 12), u16(b, 14), {}};
    require(image.width >= 8 && image.width <= 1024 && image.height >= 8 && image.height <= 1024,
            "Texture dimensions must be from 8 to 1024");
    image.rgba.resize(std::size_t(image.width) * image.height * 4);
    validate(image);
    const auto stride = b[16] / 8;
    auto pixels = slice(b, 18 + b[0], std::size_t(image.width) * image.height * stride);
    for (std::size_t y = 0; y < image.height; ++y)
        for (std::size_t x = 0; x < image.width; ++x) {
            auto sx = (b[17] & 16) ? image.width - 1 - x : x,
                 sy = (b[17] & 32) ? y : image.height - 1 - y;
            auto src = (sy * image.width + sx) * stride, dst = (y * image.width + x) * 4;
            image.rgba[dst] = pixels[src + 2];
            image.rgba[dst + 1] = pixels[src + 1];
            image.rgba[dst + 2] = pixels[src];
            image.rgba[dst + 3] = stride == 4 && (b[17] & 15) == 8 ? pixels[src + 3] : 255;
        }
    return image;
}
Bytes write_tga(const TextureImage &image) {
    validate(image);
    Bytes out(18);
    out[2] = 2;
    put16(out, 12, image.width);
    put16(out, 14, image.height);
    out[16] = 32;
    out[17] = 40;
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        out.push_back(image.rgba[i + 2]);
        out.push_back(image.rgba[i + 1]);
        out.push_back(image.rgba[i]);
        out.push_back(image.rgba[i + 3]);
    }
    return out;
}
Bytes encode_texture(View donor, const TextureImage &image, const std::string &name,
                     std::optional<TextureFormat> requested_format) {
    validate(image);
    require(u32(donor, 0) == 0x15041213 && u32(donor, 4) == 1 &&
                text(slice(donor, 8, 8)) == "texture",
            "Unsupported texture container");
    require(name.size() < 64 && !name.empty(), "Texture name exceeds its resource field");
    auto header = slice(donor, 0, 128);
    bool existing = u32(donor, 24) != 0;
    auto format =
        requested_format.value_or(existing ? TextureFormat(u16(donor, 108)) : TextureFormat::RGBA8);
    texture_format_name(format);
    unsigned levels = existing ? u16(donor, 110) : 1;
    require(levels >= 1 && levels <= 12, "Invalid texture mip count");
    unsigned possible = 1;
    for (unsigned extent = std::max(image.width, image.height); extent > 1; extent /= 2)
        ++possible;
    require(levels <= possible, "Replacement is too small to retain the original mip count");
    bool same_layout = existing && u16(donor, 104) == image.width &&
                       u16(donor, 106) == image.height && u16(donor, 108) == unsigned(format);
    if (existing) {
        auto decoded = decode_field_texture(donor);
        if (same_layout && decoded.rgba == image.rgba) {
            Bytes out(donor.begin(), donor.end());
            if (text(slice(donor, 40, 64)) != name) {
                std::fill(out.begin() + 40, out.begin() + 104, 0);
                replace_name(out, 40, name);
            }
            return out;
        }
    }
    Bytes out(header.begin(), header.end());
    std::fill(out.begin() + 40, out.begin() + 104, 0);
    replace_name(out, 40, name);
    put16(out, 104, image.width);
    put16(out, 106, image.height);
    put16(out, 108, std::uint16_t(format));
    put16(out, 110, std::uint16_t(levels));
    TextureImage level{image.width, image.height, image.rgba};
    std::size_t source_offset = 128;
    bool alpha_color = format == TextureFormat::RGBA8 || format == TextureFormat::RGBA4 ||
                       format == TextureFormat::RGB5A1 || format == TextureFormat::ETC1A4 ||
                       format == TextureFormat::LA8 || format == TextureFormat::LA4;
    for (unsigned mip = 0; mip < levels; ++mip) {
        auto size = texture_level_size(level.width, level.height, format);
        auto baseline = same_layout ? slice(donor, source_offset, size) : View{};
        append(out, encode_texture_pixels(baseline, level, format));
        source_offset += size;
        if (mip + 1 == levels)
            break;
        TextureImage next{std::uint16_t(std::max(1u, unsigned(level.width) / 2)),
                          std::uint16_t(std::max(1u, unsigned(level.height) / 2)),
                          {}};
        next.rgba.resize(std::size_t(next.width) * next.height * 4);
        for (unsigned y = 0; y < next.height; ++y)
            for (unsigned x = 0; x < next.width; ++x) {
                unsigned sum[4]{}, weighted[3]{}, count = 0;
                for (unsigned yy = y * level.height / next.height;
                     yy < (y + 1) * level.height / next.height; ++yy)
                    for (unsigned xx = x * level.width / next.width;
                         xx < (x + 1) * level.width / next.width; ++xx) {
                        auto pixel = level.rgba.data() + (std::size_t(yy) * level.width + xx) * 4;
                        for (unsigned c = 0; c < 4; ++c)
                            sum[c] += pixel[c];
                        for (unsigned c = 0; c < 3; ++c)
                            weighted[c] += unsigned(pixel[c]) * pixel[3];
                        ++count;
                    }
                auto pixel = next.rgba.data() + (std::size_t(y) * next.width + x) * 4;
                for (unsigned c = 0; c < 4; ++c)
                    pixel[c] = std::uint8_t(alpha_color && c < 3 && sum[3]
                                                ? (weighted[c] + sum[3] / 2) / sum[3]
                                                : (sum[c] + count / 2) / count);
            }
        level = std::move(next);
    }
    put32(out, 24, narrow(out.size() - 128));
    put32(out, 16, narrow(out.size() - 24));
    return out;
}
Bytes bind_texture(View bytes, const TextureImage &image) {
    auto pack = ModelPack::parse(bytes);
    std::size_t texture = pack.resources.size(), model = texture;
    for (std::size_t i = 0; i < pack.resources.size(); ++i) {
        if (pack.resources[i].category == 0) {
            require(model == pack.resources.size(), "Texture profile requires one model");
            model = i;
        }
        if (pack.resources[i].category == 1) {
            require(texture == pack.resources.size(), "Texture profile requires one texture");
            texture = i;
        }
    }
    require(texture < pack.resources.size() && model < pack.resources.size(),
            "Texture profile needs one embedded model and texture");
    auto old_name = text(slice(pack.resources[texture].bytes, 40, 64));
    auto base = pack.resources[texture].name;
    require(base.size() >= 12 && base.size() <= 60 && old_name == base + ".tga",
            "Unsupported texture naming layout");
    auto new_base = "tex_" + sha256(image.rgba).substr(0, base.size() - 4),
         name = new_base + ".tga";
    auto m = Model::parse(pack.resources[model].bytes);
    require(m.names[1].size() == 1 && m.names[1][0] == old_name && m.names[2].size() == 1,
            "Texture profile requires one named texture and material");
    auto off = std::size_t(32) + 4 + m.names[0].size() * 68 + 4;
    put32(m.original, off, name_hash(name));
    replace_name(m.original, off + 4, name);
    for (const auto &section : m.sections)
        if (section.kind == "material") {
            auto pos = section.offset + 16;
            for (unsigned i = 0; i < 4; ++i) {
                slice(m.original, pos, 5);
                pos += 5 + m.original[pos + 4];
            }
            pos += 168;
            require(u32(m.original, pos) == 1, "Texture profile requires one material binding");
            pos += 4;
            require(m.original.at(pos + 4) == old_name.size() &&
                        text(slice(m.original, pos + 5, old_name.size())) == old_name,
                    "Material texture binding mismatch");
            auto state = pos + 5 + old_name.size();
            slice(m.original, state, 42);
            require(m.original[state] == 0 && m.original[state + 1] == 0,
                    "Texture profile requires texture slot zero and UV mapping");
            require(f32(m.original, state + 2) == 1 && f32(m.original, state + 6) == 1 &&
                        f32(m.original, state + 10) == 0 && f32(m.original, state + 14) == 0 &&
                        f32(m.original, state + 18) == 0,
                    "Texture profile requires an untransformed UV mapping");
            require(state + 42 <= section.offset + section.size,
                    "Material binding exceeds its section");
            put32(m.original, pos, name_hash(name));
            replace_name(m.original, pos + 5, name);
        }
    auto out = pack.replace(model, m.original, 128);
    pack = ModelPack::parse(out);
    out = pack.replace(texture, encode_texture(pack.resources[texture].bytes, image, name), 128);
    auto field = pack.resources[texture].address_field;
    replace_name(out, field - base.size(), new_base);
    auto checked = ModelPack::parse(out);
    require(checked.resources[texture].name == new_base &&
                text(slice(checked.resources[texture].bytes, 40, 64)) == name,
            "Texture binding readback failed");
    return out;
}
}
