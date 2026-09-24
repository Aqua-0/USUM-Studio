#include "formats/texture_codec.h"
#include <limits>
#include <algorithm>
namespace studio {
namespace {
std::size_t tiled_pixel(unsigned x, unsigned y, unsigned width) {
    unsigned morton = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) |
                      ((y & 4) << 3);
    return (std::size_t(y / 8) * (width / 8) + x / 8) * 64 + morton;
}
unsigned quant(unsigned value, unsigned bits) {
    return (value * ((1u << bits) - 1) + 127) / 255;
}
constexpr int modifiers[8][4] = {{2, 8, -2, -8},       {5, 17, -5, -17},    {9, 29, -9, -29},
                                 {13, 42, -13, -42},   {18, 60, -18, -60},  {24, 80, -24, -80},
                                 {33, 106, -33, -106}, {47, 183, -47, -183}};
void encode_block(Bytes &out, std::size_t offset,
                  const std::array<std::array<unsigned, 4>, 16> &pixels, bool alpha) {
    if (alpha) {
        std::uint64_t bits = 0;
        for (unsigned x = 0; x < 4; ++x)
            for (unsigned y = 0; y < 4; ++y)
                bits |= std::uint64_t(quant(pixels[y * 4 + x][3], 4)) << ((x * 4 + y) * 4);
        put32(out, offset, std::uint32_t(bits));
        put32(out, offset + 4, std::uint32_t(bits >> 32));
        offset += 8;
    }
    std::uint64_t best_error = std::numeric_limits<std::uint64_t>::max();
    std::uint32_t best_hi = 0, best_lo = 0;
    for (unsigned flip = 0; flip < 2; ++flip) {
        std::uint64_t error = 0;
        std::uint32_t hi = flip, lo = 0;
        for (unsigned half = 0; half < 2; ++half) {
            std::array<unsigned, 3> average{};
            for (unsigned y = 0; y < 4; ++y)
                for (unsigned x = 0; x < 4; ++x)
                    if (unsigned(flip ? y >= 2 : x >= 2) == half)
                        for (unsigned c = 0; c < 3; ++c)
                            average[c] += pixels[y * 4 + x][c];
            for (auto &c : average)
                c = quant((c + 4) / 8, 4);
            std::uint64_t half_best = std::numeric_limits<std::uint64_t>::max();
            std::uint32_t half_hi = 0, half_lo = 0;
            for (int dr = -1; dr <= 1; ++dr)
                for (int dg = -1; dg <= 1; ++dg)
                    for (int db = -1; db <= 1; ++db) {
                        std::array<int, 3> base{std::clamp(int(average[0]) + dr, 0, 15),
                                                std::clamp(int(average[1]) + dg, 0, 15),
                                                std::clamp(int(average[2]) + db, 0, 15)};
                        for (unsigned table = 0; table < 8; ++table) {
                            std::uint64_t total = 0;
                            std::uint32_t selectors = 0;
                            for (unsigned y = 0; y < 4; ++y)
                                for (unsigned x = 0; x < 4; ++x)
                                    if (unsigned(flip ? y >= 2 : x >= 2) == half) {
                                        unsigned selected = 0, lowest = ~0u;
                                        for (unsigned sel = 0; sel < 4; ++sel) {
                                            unsigned cost = 0;
                                            for (unsigned c = 0; c < 3; ++c) {
                                                int delta =
                                                    std::clamp(base[c] * 17 + modifiers[table][sel],
                                                               0, 255) -
                                                    int(pixels[y * 4 + x][c]);
                                                cost += unsigned(delta * delta);
                                            }
                                            if (cost < lowest) {
                                                lowest = cost;
                                                selected = sel;
                                            }
                                        }
                                        total += lowest;
                                        unsigned bit = x * 4 + y;
                                        selectors |= (selected & 1) << bit;
                                        selectors |= (selected >> 1) << (bit + 16);
                                    }
                            if (total < half_best) {
                                half_best = total;
                                half_lo = selectors;
                                half_hi = table << (half ? 2 : 5);
                                for (unsigned c = 0; c < 3; ++c)
                                    half_hi |= unsigned(base[c]) << (28 - c * 8 - half * 4);
                            }
                        }
                    }
            error += half_best;
            hi |= half_hi;
            lo |= half_lo;
        }
        if (error < best_error) {
            best_error = error;
            best_hi = hi;
            best_lo = lo;
        }
    }
    put32(out, offset, best_lo);
    put32(out, offset + 4, best_hi);
}

TextureImage decode_level(View b, unsigned width, unsigned height, unsigned format,
                          std::size_t &cursor) {
    TextureImage out{std::uint16_t(width), std::uint16_t(height), {}};
    out.rgba.resize(std::size_t(width) * height * 4);
    auto put = [&](unsigned x, unsigned y, std::array<int, 4> pixel) {
        if (x >= width || y >= height)
            return;
        auto p = (std::size_t(height - 1 - y) * width + x) * 4;
        for (unsigned c = 0; c < 4; ++c)
            out.rgba[p + c] = std::uint8_t(std::clamp(pixel[c], 0, 255));
    };
    auto expand = [](int v, int bits) {
        return (v << (8 - bits)) | (v >> (2 * bits - 8));
    };
    for (unsigned ty = 0; ty < out.height; ty += 8)
        for (unsigned tx = 0; tx < out.width; tx += 8) {
            if (format == 0x2a || format == 0x2b) {
                static constexpr int mods[8][4] = {{2, 8, -2, -8},       {5, 17, -5, -17},
                                                   {9, 29, -9, -29},     {13, 42, -13, -42},
                                                   {18, 60, -18, -60},   {24, 80, -24, -80},
                                                   {33, 106, -33, -106}, {47, 183, -47, -183}};
                for (unsigned block = 0; block < 4; ++block) {
                    std::uint64_t alpha = ~std::uint64_t(0);
                    if (format == 0x2b) {
                        alpha = std::uint64_t(u32(b, cursor)) |
                                (std::uint64_t(u32(b, cursor + 4)) << 32);
                        cursor += 8;
                    }
                    auto lo = u32(b, cursor), hi = u32(b, cursor + 4);
                    cursor += 8;
                    int colors[2][3]{};
                    bool differential = (hi & 2) != 0, flip = (hi & 1) != 0;
                    for (unsigned c = 0; c < 3; ++c) {
                        auto v = int((hi >> (24 - c * 8)) & 255);
                        if (differential) {
                            auto base = v >> 3, delta = v & 7;
                            if (delta >= 4)
                                delta -= 8;
                            require(base + delta >= 0 && base + delta <= 31,
                                    "Invalid ETC1 differential block");
                            colors[0][c] = expand(base, 5);
                            colors[1][c] = expand(base + delta, 5);
                        } else {
                            colors[0][c] = expand(v >> 4, 4);
                            colors[1][c] = expand(v & 15, 4);
                        }
                    }
                    for (unsigned y = 0; y < 4; ++y)
                        for (unsigned x = 0; x < 4; ++x) {
                            auto bit = x * 4 + y, half = unsigned(flip ? y >= 2 : x >= 2);
                            auto table = (hi >> (half ? 2 : 5)) & 7;
                            auto selector = ((lo >> bit) & 1) | (((lo >> (bit + 16)) & 1) << 1);
                            std::array<int, 4> pixel{};
                            for (unsigned c = 0; c < 3; ++c)
                                pixel[c] = colors[half][c] + mods[table][selector];
                            pixel[3] = int((alpha >> (bit * 4)) & 15) * 17;
                            put(tx + (block % 2) * 4 + x, ty + (block / 2) * 4 + y, pixel);
                        }
                }
            } else {
                unsigned bits = 0;
                switch (format) {
                case 4:
                    bits = 32;
                    break;
                case 3:
                    bits = 24;
                    break;
                case 2:
                case 0x16:
                case 0x17:
                case 0x23:
                    bits = 16;
                    break;
                case 0x25:
                case 0x26:
                case 0x27:
                    bits = 8;
                    break;
                case 0x28:
                case 0x29:
                    bits = 4;
                    break;
                default:
                    throw std::runtime_error("Unsupported field texture format " +
                                             std::to_string(format));
                }
                auto tile = slice(b, cursor, 64 * bits / 8);
                cursor += tile.size();
                for (unsigned i = 0; i < 64; ++i) {
                    unsigned x = (i & 1) | ((i >> 1) & 2) | ((i >> 2) & 4),
                             y = ((i >> 1) & 1) | ((i >> 2) & 2) | ((i >> 3) & 4);
                    std::array<int, 4> p{255, 255, 255, 255};
                    auto at = i * bits / 8;
                    if (format == 4)
                        p = {tile[at + 3], tile[at + 2], tile[at + 1], tile[at]};
                    else if (format == 3)
                        p = {tile[at + 2], tile[at + 1], tile[at], 255};
                    else if (format == 2) {
                        auto v = u16(tile, at);
                        p = {expand(v >> 11, 5), expand((v >> 5) & 63, 6), expand(v & 31, 5), 255};
                    } else if (format == 0x16) {
                        auto v = u16(tile, at);
                        p = {int((v >> 12) & 15) * 17, int((v >> 8) & 15) * 17,
                             int((v >> 4) & 15) * 17, int(v & 15) * 17};
                    } else if (format == 0x17) {
                        auto v = u16(tile, at);
                        p = {expand(v >> 11, 5), expand((v >> 6) & 31, 5), expand((v >> 1) & 31, 5),
                             int(v & 1) * 255};
                    } else if (format == 0x23)
                        p = {tile[at + 1], tile[at + 1], tile[at + 1], tile[at]};
                    else if (format == 0x25)
                        p = {tile[at], tile[at], tile[at], 255};
                    else if (format == 0x26)
                        p[3] = tile[at];
                    else if (format == 0x27)
                        p = {int(tile[at] >> 4) * 17, int(tile[at] >> 4) * 17,
                             int(tile[at] >> 4) * 17, int(tile[at] & 15) * 17};
                    else {
                        auto v = int((tile[at] >> ((i % 2) * 4)) & 15) * 17;
                        if (format == 0x28)
                            p = {v, v, v, 255};
                        else
                            p[3] = v;
                    }
                    put(tx + x, ty + y, p);
                }
            }
        }
    return out;
}
}
TextureImage decode_texture_pixels(View b, unsigned width, unsigned height, unsigned format) {
    std::size_t cursor = 0;
    return decode_level(b, width, height, format, cursor);
}
TextureImage decode_field_texture(View b) {
    require(u32(b, 0) == 0x15041213, "Invalid field texture");
    unsigned width = u16(b, 104), height = u16(b, 106), format = u16(b, 108),
             levels = u16(b, 110);
    require(width >= 8 && height >= 8 && width <= 2048 && height <= 2048 && width % 8 == 0 &&
                height % 8 == 0,
            "Invalid field texture dimensions");
    require(levels >= 1 && levels <= 12, "Invalid texture mip count");
    auto payload = slice(b, 128, u32(b, 24));
    std::size_t cursor = 0;
    auto out = decode_level(payload, width, height, format, cursor);
    out.authored_levels = levels;
    for (unsigned i = 1; i < levels; ++i) {
        require(width > 1 || height > 1, "Too many authored mip levels");
        width = std::max(1u, unsigned(width) / 2);
        height = std::max(1u, unsigned(height) / 2);
        out.mipmaps.push_back(decode_level(payload, width, height, format, cursor).rgba);
    }
    return out;
}
Bytes texture_mip_chain(const TextureImage &image) {
    require(image.width && image.height &&
                image.rgba.size() == std::size_t(image.width) * image.height * 4,
            "Invalid mip source");
    Bytes result = image.rgba, previous = image.rgba;
    unsigned width = image.width, height = image.height;
    std::size_t authored = 0;
    while (width > 1 || height > 1) {
        unsigned w = std::max(1u, width / 2), h = std::max(1u, height / 2);
        Bytes next(std::size_t(w) * h * 4);
        for (unsigned y = 0; y < h; ++y)
            for (unsigned x = 0; x < w; ++x)
                for (unsigned c = 0; c < 4; ++c) {
                    unsigned sum = 0, count = 0;
                    for (unsigned yy = y * height / h; yy < (y + 1) * height / h; ++yy)
                        for (unsigned xx = x * width / w; xx < (x + 1) * width / w; ++xx) {
                            sum += previous[(std::size_t(yy) * width + xx) * 4 + c];
                            ++count;
                        }
                    next[(std::size_t(y) * w + x) * 4 + c] =
                        std::uint8_t((sum + count / 2) / count);
                }
        if (authored < image.mipmaps.size()) {
            require(image.mipmaps[authored].size() == next.size(),
                    "Invalid authored mip dimensions");
            next = image.mipmaps[authored++];
        }
        result.insert(result.end(), next.begin(), next.end());
        previous = std::move(next);
        width = w;
        height = h;
    }
    return result;
}

const char *texture_format_name(TextureFormat format) {
    switch (format) {
#define TEXTURE_NAME(name)                                                                         \
    case TextureFormat::name:                                                                      \
        return #name;
        TEXTURE_NAME(RGBA8)
        TEXTURE_NAME(RGB8) TEXTURE_NAME(RGB565) TEXTURE_NAME(RGB5A1) TEXTURE_NAME(RGBA4)
            TEXTURE_NAME(LA8) TEXTURE_NAME(L8) TEXTURE_NAME(A8) TEXTURE_NAME(LA4) TEXTURE_NAME(L4)
                TEXTURE_NAME(A4) TEXTURE_NAME(ETC1) TEXTURE_NAME(ETC1A4)
#undef TEXTURE_NAME
    }
    throw std::runtime_error("Unsupported native texture format");
}
std::size_t texture_level_size(unsigned width, unsigned height, TextureFormat format) {
    texture_format_name(format);
    unsigned bits = format == TextureFormat::RGBA8  ? 32
                    : format == TextureFormat::RGB8 ? 24
                    : (format == TextureFormat::RGB565 || format == TextureFormat::RGB5A1 ||
                       format == TextureFormat::RGBA4 || format == TextureFormat::LA8)
                        ? 16
                    : (format == TextureFormat::L4 || format == TextureFormat::A4 ||
                       format == TextureFormat::ETC1)
                        ? 4
                        : 8;
    return std::size_t((width + 7) / 8) * ((height + 7) / 8) * 64 * bits / 8;
}
Bytes encode_texture_pixels(View original, const TextureImage &image, TextureFormat format) {
    constexpr unsigned native[] = {0x25, 0x26, 0x27, 0x23, 0,    2,    3,
                                   0x17, 0x16, 4,    0x2a, 0x2b, 0x28, 0x29};
    constexpr unsigned pixel_bits[] = {8, 8, 8, 16, 16, 16, 24, 16, 16, 32, 4, 8, 4, 4};
    texture_format_name(format);
    unsigned mapped = unsigned(std::find(std::begin(native), std::end(native), unsigned(format)) -
                               std::begin(native));
    struct Layout {
        unsigned width, height, storage_width, storage_height, format;
    };
    Layout i{image.width, image.height, std::max(8u, unsigned(image.width)),
             std::max(8u, unsigned(image.height)), mapped};
    require(image.width && image.height && (image.width & (image.width - 1)) == 0 &&
                (image.height & (image.height - 1)) == 0,
            "Texture levels require power-of-two dimensions");
    require(image.rgba.size() == std::size_t(image.width) * image.height * 4,
            "Invalid texture level pixels");
    auto size = texture_level_size(image.width, image.height, format);
    require(original.empty() || original.size() == size, "Texture level storage size changed");
    Bytes out = original.empty() ? Bytes(size) : Bytes(original.begin(), original.end());
    auto storage = decode_texture_pixels(out, i.storage_width, i.storage_height, unsigned(format));
    TextureImage previous{image.width, image.height, Bytes(image.rgba.size())};
    for (unsigned y = 0; y < image.height; ++y)
        std::copy_n(storage.rgba.data() +
                        std::size_t(i.storage_height - image.height + y) * i.storage_width * 4,
                    std::size_t(image.width) * 4,
                    previous.rgba.data() + std::size_t(y) * image.width * 4);
    std::vector<bool> changed(std::size_t(i.storage_width / 4) * (i.storage_height / 4));
    for (unsigned y = 0; y < i.height; ++y)
        for (unsigned x = 0; x < i.width; ++x) {
            auto at = (std::size_t(y) * i.width + x) * 4;
            auto p = image.rgba.data() + at;
            if (!original.empty() && std::equal(p, p + 4, previous.rgba.data() + at))
                continue;
            auto sx = x, sy = i.height - 1 - y;
            if (i.format == 10 || i.format == 11) {
                auto s = (std::size_t(i.storage_height - 1 - sy) * i.storage_width + sx) * 4;
                std::copy_n(p, 4, storage.rgba.data() + s);
                changed[std::size_t(sy / 4) * (i.storage_width / 4) + sx / 4] = true;
                continue;
            }
            auto pixel = tiled_pixel(sx, sy, i.storage_width),
                 offset = pixel * pixel_bits[i.format] / 8;
            unsigned l = (unsigned(p[0]) * 77 + unsigned(p[1]) * 150 + unsigned(p[2]) * 29 + 128) >>
                         8,
                     v = 0;
            switch (i.format) {
            case 0:
                out[offset] = std::uint8_t(l);
                break;
            case 1:
                out[offset] = p[3];
                break;
            case 2:
                out[offset] = std::uint8_t((quant(l, 4) << 4) | quant(p[3], 4));
                break;
            case 3:
                out[offset] = p[3];
                out[offset + 1] = std::uint8_t(l);
                break;
            case 5:
                v = (quant(p[0], 5) << 11) | (quant(p[1], 6) << 5) | quant(p[2], 5);
                put16(out, offset, std::uint16_t(v));
                break;
            case 6:
                out[offset] = p[2];
                out[offset + 1] = p[1];
                out[offset + 2] = p[0];
                break;
            case 7:
                v = (quant(p[0], 5) << 11) | (quant(p[1], 5) << 6) | (quant(p[2], 5) << 1) |
                    quant(p[3], 1);
                put16(out, offset, std::uint16_t(v));
                break;
            case 8:
                v = (quant(p[0], 4) << 12) | (quant(p[1], 4) << 8) | (quant(p[2], 4) << 4) |
                    quant(p[3], 4);
                put16(out, offset, std::uint16_t(v));
                break;
            case 9:
                for (unsigned c = 0; c < 4; ++c)
                    out[offset + c] = p[3 - c];
                break;
            case 12:
            case 13: {
                auto shift = unsigned(pixel % 2) * 4;
                v = quant(i.format == 12 ? l : p[3], 4);
                out[offset] = std::uint8_t((out[offset] & ~(15u << shift)) | (v << shift));
                break;
            }
            }
        }
    if (i.format == 10 || i.format == 11)
        for (unsigned y = 0; y < i.storage_height; y += 4)
            for (unsigned x = 0; x < i.storage_width; x += 4)
                if (changed[std::size_t(y / 4) * (i.storage_width / 4) + x / 4]) {
                    std::array<std::array<unsigned, 4>, 16> block{};
                    for (unsigned yy = 0; yy < 4; ++yy)
                        for (unsigned xx = 0; xx < 4; ++xx)
                            for (unsigned c = 0; c < 4; ++c)
                                block[yy * 4 + xx][c] =
                                    storage.rgba[(std::size_t(i.storage_height - 1 -
                                                              std::min(y + yy, i.height - 1)) *
                                                      i.storage_width +
                                                  std::min(x + xx, i.width - 1)) *
                                                     4 +
                                                 c];
                    auto tile = std::size_t(y / 8) * (i.storage_width / 8) + x / 8;
                    auto sub = (y % 8) / 4 * 2 + (x % 8) / 4;
                    encode_block(out, (tile * 4 + sub) * (i.format == 11 ? 16 : 8), block,
                                 i.format == 11);
                }
    return out;
}
}
