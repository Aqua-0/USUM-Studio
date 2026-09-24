#include "core/filesystem.h"
#include "core/resource_source.h"
#include "audio/music.h"
#include "audio/music_profile.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
namespace studio {
namespace {
std::size_t reference(View b, std::size_t at, std::size_t base) {
    auto offset = u32(b, at + 4);
    require(offset != 0xffffffff, "Missing sound resource reference");
    auto p = base + std::size_t(offset);
    slice(b, p, 1);
    return p;
}
std::size_t block(View b, unsigned type) {
    require(u16(b, 4) == 0xfeff, "Only little-endian sound resources are supported");
    auto n = u16(b, 16);
    require(n <= 16, "Invalid sound block count");
    for (unsigned i = 0; i < n; ++i) {
        auto p = 20 + i * 12;
        if (u16(b, p) == type) {
            auto at = u32(b, p + 4);
            slice(b, at, u32(b, p + 8));
            return at;
        }
    }
    throw std::runtime_error("Missing sound resource block");
}
int signed16(View b, std::size_t p) {
    auto v = u16(b, p);
    return v < 32768 ? int(v) : int(v) - 65536;
}
}
unsigned map_music_sound(unsigned id) {
    require((id >> 16) == 1 && (id & 65535) < map_music_count,
            "This map music ID is not supported by the Ultra Sun/Ultra Moon profile");
    return id & 65535;
}
MusicSamples decode_music(View b) {
    require(text(slice(b, 0, 4)) == "CSTM" && u32(b, 12) == b.size(),
            "Invalid music stream header");
    auto info = block(b, 0x4000), data = block(b, 0x4002);
    auto body = info + 8, stream = reference(b, body, body);
    auto encoding = slice(b, stream, 4)[0];
    MusicSamples out;
    out.looping = b[stream + 1] != 0;
    out.channels = b[stream + 2];
    out.rate = u32(b, stream + 4);
    out.loop_start = u32(b, stream + 8);
    auto frames = u32(b, stream + 12);
    require(encoding == 2, "Music preview currently supports DSP ADPCM streams");
    require(out.channels >= 1 && out.channels <= 2 && out.rate >= 8000 && out.rate <= 192000,
            "Unsupported music channel count or sample rate");
    require(frames > 0 && std::uint64_t(frames) * out.channels * 2 <= 512 * 1024 * 1024,
            "Music stream is too large to preview");
    require(!out.looping || out.loop_start < frames, "Invalid music loop start");
    auto blocks = u32(b, stream + 16), block_bytes = u32(b, stream + 20),
         block_samples = u32(b, stream + 24), last_bytes = u32(b, stream + 28),
         last_samples = u32(b, stream + 32), last_padded = u32(b, stream + 36);
    require(blocks && block_samples && block_samples % 14 == 0 &&
                block_bytes == std::uint64_t(block_samples / 14) * 8,
            "Invalid music block layout");
    require(last_samples && last_samples <= block_samples &&
                last_bytes >= std::uint64_t(last_samples / 14) * 8 +
                                  (last_samples % 14 ? 1 + (last_samples % 14 + 1) / 2 : 0) &&
                last_padded >= last_bytes,
            "Invalid final music block");
    require(std::uint64_t(blocks - 1) * block_samples + last_samples == frames,
            "Music sample count does not match its blocks");
    auto samples = reference(b, stream + 48, data + 8);
    auto end = samples + std::uint64_t(blocks - 1) * block_bytes * out.channels +
               std::uint64_t(last_padded) * out.channels;
    require(end <= std::uint64_t(data) + u32(b, data + 4), "Music samples exceed the data block");
    slice(b, samples, std::size_t(end - samples));
    auto table = reference(b, body + 16, body);
    require(u32(b, table) == out.channels, "Music channel table mismatch");
    out.pcm.resize(std::size_t(frames) * out.channels);
    for (unsigned ch = 0; ch < out.channels; ++ch) {
        auto channel = reference(b, table + 4 + ch * 8, table),
             adpcm = reference(b, channel, channel);
        std::array<int, 16> coefficients{};
        for (unsigned i = 0; i < 16; ++i)
            coefficients[i] = signed16(b, adpcm + i * 2);
        int h1 = signed16(b, adpcm + 34), h2 = signed16(b, adpcm + 36);
        std::size_t frame = 0;
        for (unsigned k = 0; k < blocks; ++k) {
            auto count = k + 1 == blocks ? last_samples : block_samples;
            auto stride = k + 1 == blocks ? last_padded : block_bytes;
            auto start =
                samples + std::size_t(k) * block_bytes * out.channels + std::size_t(ch) * stride;
            for (unsigned n = 0; n < count; ++n) {
                auto packet = start + (n / 14) * 8;
                unsigned header = b[packet], predictor = header >> 4;
                if (predictor >= 8)
                    throw std::runtime_error("Invalid ADPCM predictor");
                int value = (b[packet + 1 + (n % 14) / 2] >> ((n % 2) ? 0 : 4)) & 15;
                if (value >= 8)
                    value -= 16;
                auto sum = std::int64_t(value) * (std::int64_t{1} << (header & 15)) * 2048 +
                           std::int64_t(coefficients[predictor * 2]) * h1 +
                           std::int64_t(coefficients[predictor * 2 + 1]) * h2 + 1024;
                auto decoded = std::clamp<std::int64_t>(
                    sum >= 0 ? sum / 2048 : -((-sum + 2047) / 2048), -32768, 32767);
                h2 = h1;
                h1 = int(decoded);
                out.pcm[frame * out.channels + ch] = std::int16_t(decoded);
                ++frame;
            }
        }
    }
    return out;
}
std::map<unsigned, MusicTrack> read_music_catalog(const std::filesystem::path &dump) {
    std::ifstream file(resource_file_path(dump / "romfs/data/sound/niji_sound.bcsar"),
                       std::ios::binary);
    require(bool(file), "Sound archive not found in the loaded dump");
    Bytes header(64);
    file.read(reinterpret_cast<char *>(header.data()), 64);
    require(bool(file) && text(slice(header, 0, 4)) == "CSAR" && u16(header, 4) == 0xfeff,
            "Invalid sound archive header");
    std::size_t offset = 0, size = 0;
    require(u16(header, 16) <= 3, "Unsupported sound archive header");
    for (unsigned i = 0; i < u16(header, 16); ++i)
        if (u16(header, 20 + i * 12) == 0x2001) {
            offset = u32(header, 24 + i * 12);
            size = u32(header, 28 + i * 12);
        }
    require(size >= 72 && size <= 16 * 1024 * 1024, "Invalid sound archive metadata size");
    Bytes b(size);
    file.seekg(std::streamoff(offset));
    file.read(reinterpret_cast<char *>(b.data()), std::streamsize(size));
    require(bool(file) && text(slice(b, 0, 4)) == "INFO", "Cannot read sound archive metadata");
    auto sounds = reference(b, 8, 8), files = reference(b, 56, 8);
    auto count = u32(b, sounds), file_count = u32(b, files);
    slice(b, sounds + 4, std::size_t(count) * 8);
    slice(b, files + 4, std::size_t(file_count) * 8);
    std::map<unsigned, MusicTrack> out;
    for (unsigned i = 0; i < count; ++i) {
        auto sound = reference(b, sounds + 4 + i * 8, sounds);
        auto id = u32(b, sound);
        require(id < file_count, "Sound references a missing file");
        auto f = reference(b, files + 4 + id * 8, files);
        if (u16(b, f) != 0x220d)
            continue;
        auto path = reference(b, f, f);
        auto end = path;
        while (end < b.size() && b[end])
            ++end;
        require(end < b.size(), "Unterminated sound filename");
        auto name = text(slice(b, path, end - path));
        std::filesystem::path relative = path_from_utf8(name);
        require(!relative.is_absolute() && relative.filename() == relative &&
                    relative.extension() == ".bcstm",
                "Unsupported external music path");
        out.emplace(i, MusicTrack{name, float(slice(b, sound + 8, 1)[0]) / 127.f});
    }
    return out;
}
}
