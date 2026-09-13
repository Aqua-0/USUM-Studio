#include "images/image_format.h"
#include "formats/texture_codec.h"
#include <algorithm>
#include <array>
#include <limits>
namespace studio {
namespace {
constexpr unsigned gpu_formats[] = {0x25, 0x26, 0x27, 0x23, 0,    2,    3,
                                    0x17, 0x16, 4,    0x2a, 0x2b, 0x28, 0x29};
constexpr unsigned pixel_bits[] = {8, 8, 8, 16, 16, 16, 24, 16, 16, 32, 4, 8, 4, 4};
unsigned power_size(unsigned n) {
    unsigned p = 8;
    while (p < n)
        p *= 2;
    return p;
}
std::pair<unsigned, unsigned> storage_pixel(const NativeImageInfo &info, unsigned x, unsigned y) {
    if (info.texture_resource)
        return {x, info.storage_height - 1 - y};
    switch ((info.flags >> 2) & 3) {
    case 1:
        return {info.storage_width - 1 - y, x};
    case 2:
        return {y, x};
    default:
        return {x, y};
    }
}

}
bool is_native_image(View b) {
    return (b.size() >= 128 && u32(b, 0) == 0x15041213) ||
           (b.size() >= 40 && text(b.subspan(b.size() - 40, 4)) == "FLIM" &&
            u32(b, b.size() - 28) == b.size());
}
const char *native_image_format_name(unsigned f) {
    static const char *names[] = {"L8",     "A8",    "LA4",   "LA8",  "HILO8",  "RGB565", "RGB8",
                                  "RGB5A1", "RGBA4", "RGBA8", "ETC1", "ETC1A4", "L4",     "A4"};
    return f < 14 ? names[f] : "Unsupported";
}
NativeImageInfo native_image_info(View b) {
    if (b.size() >= 128 && u32(b, 0) == 0x15041213) {
        auto gpu = u16(b, 108);
        auto found = std::find(std::begin(gpu_formats), std::end(gpu_formats), gpu);
        require(found != std::end(gpu_formats) && gpu != 0,
                "Unsupported texture resource pixel format");
        NativeImageInfo i{u16(b, 104), u16(b, 106), unsigned(found - std::begin(gpu_formats)),
                          0,           0,           0,
                          u32(b, 24),  128,         true};
        require(i.width >= 8 && i.height >= 8 && i.width <= 2048 && i.height <= 2048 &&
                    power_size(i.width) == i.width && power_size(i.height) == i.height,
                "Unsupported texture resource dimensions");
        require(u16(b, 110) == 1, "Image replacement requires a single texture level");
        i.storage_width = i.width;
        i.storage_height = i.height;
        require(i.payload == std::size_t(i.width) * i.height * pixel_bits[i.format] / 8 &&
                    b.size() == i.data_offset + i.payload,
                "Unsupported texture resource extent");
        return i;
    }
    require(is_native_image(b), "Not a native layout image");
    auto p = b.size() - 40;
    require(u16(b, p + 4) == 0xfeff && u16(b, p + 6) == 20 && u32(b, p + 12) == b.size() &&
                text(slice(b, p + 20, 4)) == "imag",
            "Unsupported image header");
    NativeImageInfo i{u16(b, p + 28), u16(b, p + 30), b[p + 34], b[p + 35], 0, 0, u32(b, p + 36)};
    require(i.width && i.height && i.width <= 2048 && i.height <= 2048,
            "Unsupported image dimensions");
    require(i.format < 14 && i.format != 4, "Unsupported native pixel format");
    require(((i.flags >> 2) & 3) != 3, "Unsupported image orientation");
    i.storage_width = std::max(i.format == 10 ? 16u : 8u, power_size(i.width));
    i.storage_height = std::max(i.format == 10 ? 16u : 8u, power_size(i.height));
    if (i.flags & 12)
        std::swap(i.storage_width, i.storage_height);
    require(std::size_t(i.storage_width) * i.storage_height * pixel_bits[i.format] / 8 ==
                    i.payload &&
                i.payload <= p,
            "Unsupported image storage extent");
    return i;
}
TextureImage decode_native_image(View b) {
    auto i = native_image_info(b);
    auto storage = decode_texture_pixels(b.subspan(i.data_offset, i.payload), i.storage_width,
                                         i.storage_height, gpu_formats[i.format]);
    TextureImage out{std::uint16_t(i.width), std::uint16_t(i.height),
                     Bytes(std::size_t(i.width) * i.height * 4)};
    for (unsigned y = 0; y < i.height; ++y)
        for (unsigned x = 0; x < i.width; ++x) {
            auto [sx, sy] = storage_pixel(i, x, y);
            auto source = (std::size_t(i.storage_height - 1 - sy) * i.storage_width + sx) * 4;
            std::copy_n(storage.rgba.begin() + std::ptrdiff_t(source), 4,
                        out.rgba.begin() + std::ptrdiff_t((std::size_t(y) * i.width + x) * 4));
        }
    return out;
}
void validate_image_replacement(View original, View replacement) {
    auto source = native_image_info(original), changed = native_image_info(replacement);
    require(original.size() == replacement.size() && source.data_offset == changed.data_offset &&
                source.payload == changed.payload,
            "Replacement image extent changed");
    require(
        std::equal(original.begin(), original.begin() + std::ptrdiff_t(source.data_offset),
                   replacement.begin()) &&
            std::equal(original.begin() + std::ptrdiff_t(source.data_offset + source.payload),
                       original.end(),
                       replacement.begin() + std::ptrdiff_t(source.data_offset + source.payload)),
        "Replacement image metadata changed");
    decode_native_image(replacement);
}
Bytes replace_native_image(View original, const TextureImage &image) {
    auto i = native_image_info(original);
    require(image.width == i.width && image.height == i.height,
            "Expected " + std::to_string(i.width) + " x " + std::to_string(i.height) +
                " pixels; received " + std::to_string(image.width) + " x " +
                std::to_string(image.height));
    require(image.rgba.size() == std::size_t(i.width) * i.height * 4, "Invalid replacement pixels");
    auto previous = decode_native_image(original);
    if (previous.rgba == image.rgba)
        return Bytes(original.begin(), original.end());
    auto payload = original.subspan(i.data_offset, i.payload);
    auto storage =
        decode_texture_pixels(payload, i.storage_width, i.storage_height, gpu_formats[i.format]);
    for (unsigned y = 0; y < i.height; ++y)
        for (unsigned x = 0; x < i.width; ++x) {
            auto [sx, sy] = storage_pixel(i, x, y);
            auto destination = (std::size_t(i.storage_height - 1 - sy) * i.storage_width + sx) * 4;
            auto source = (std::size_t(y) * i.width + x) * 4;
            std::copy_n(image.rgba.data() + source, 4, storage.rgba.data() + destination);
        }
    auto encoded = encode_texture_pixels(payload, storage, TextureFormat(gpu_formats[i.format]));
    Bytes out(original.begin(), original.end());
    std::copy(encoded.begin(), encoded.end(), out.begin() + std::ptrdiff_t(i.data_offset));
    return out;
}

std::vector<EmbeddedImage> embedded_images(View b) {
    std::vector<EmbeddedImage> out;
    if (is_native_image(b)) {
        out.push_back({"", 0, b.size()});
        return out;
    }
    if (b.size() < 4 || text(b.first(4)) != "ALYT")
        return out;
    require(u16(b, 4) == 2, "Unsupported application image package");
    auto start = std::search(b.begin(), b.end(), "SARC", "SARC" + 4);
    require(start != b.end(), "Image package has no resource archive");
    auto base = std::size_t(start - b.begin());
    require(u16(b, base + 6) == 0xfeff, "Unsupported resource archive byte order");
    auto end = base + u32(b, base + 8), data = base + u32(b, base + 12),
         table = base + u16(b, base + 4);
    require(end <= b.size() && data <= end && text(slice(b, table, 4)) == "SFAT",
            "Invalid image package table");
    auto count = u16(b, table + 6);
    auto nodes = table + u16(b, table + 4), strings = nodes + std::size_t(count) * 16;
    require(text(slice(b, strings, 4)) == "SFNT", "Invalid image package names");
    strings += u16(b, strings + 4);
    for (unsigned n = 0; n < count; ++n) {
        auto at = nodes + n * 16, first = data + u32(b, at + 8), last = data + u32(b, at + 12);
        require(data <= first && first <= last && last <= end,
                "Image package member out of bounds");
        if (!is_native_image(b.subspan(first, last - first)))
            continue;
        auto attr = u32(b, at + 4);
        require(attr >> 24, "Unnamed package image");
        auto name_at = strings + (attr & 0xffffff) * 4;
        require(name_at < data, "Image name out of bounds");
        auto names = b.subspan(name_at, data - name_at);
        auto zero = std::find(names.begin(), names.end(), 0);
        require(zero != names.end(), "Unterminated image name");
        out.push_back({text(names.first(std::size_t(zero - names.begin()))), first, last - first});
    }
    return out;
}
}
