#include "core/resource_source.h"
#include "audio/cry.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numbers>
#include <stdexcept>
namespace studio {
namespace {
std::size_t ref(View b, std::size_t at, std::size_t base) {
    auto v = u32(b, at + 4);
    require(v != 0xffffffff, "Missing cry reference");
    auto p = base + std::size_t(v);
    slice(b, p, 1);
    return p;
}
std::size_t block(View b, unsigned type) {
    require(u16(b, 4) == 0xfeff, "Unsupported cry byte order");
    auto n = u16(b, 16);
    require(n <= 8, "Invalid cry block count");
    for (unsigned i = 0; i < n; ++i) {
        auto p = 20 + i * 12;
        if (u16(b, p) == type) {
            auto at = u32(b, p + 4);
            slice(b, at, u32(b, p + 8));
            return at;
        }
    }
    throw std::runtime_error("Missing cry block");
}
int signed16(View b, std::size_t p) {
    auto v = u16(b, p);
    return v < 32768 ? int(v) : int(v) - 65536;
}
struct Wave {
    std::size_t at = 0, size = 0, info = 0, data = 0, samples = 0, coeff = 0;
    unsigned frames = 0, rate = 0;
};
Wave wave_info(View archive) {
    require(text(slice(archive, 0, 4)) == "CWAR" && u32(archive, 12) == archive.size(),
            "Invalid cry wave archive");
    auto info = block(archive, 0x6800), file = block(archive, 0x6801);
    require(u32(archive, info + 8) == 1, "Only single-sample cry archives are supported");
    Wave w;
    w.at = ref(archive, info + 12, file + 8);
    w.size = u32(archive, info + 20);
    auto b = slice(archive, w.at, w.size);
    require(text(slice(b, 0, 4)) == "CWAV" && u32(b, 12) == b.size(), "Invalid cry wave");
    w.info = block(b, 0x7000) + 8;
    w.data = block(b, 0x7001);
    require(slice(b, w.info, 1)[0] == 2 && u32(b, w.info + 20) == 1,
            "Cry preview requires mono DSP ADPCM");
    w.rate = u32(b, w.info + 4);
    w.frames = u32(b, w.info + 12);
    require(w.rate >= 8000 && w.rate <= 192000 && w.frames && w.frames <= 1000000,
            "Invalid cry sample metadata");
    auto table = w.info + 20;
    auto channel = ref(b, table + 4, table);
    w.samples = ref(b, channel, w.data + 8);
    w.coeff = ref(b, channel + 8, channel);
    slice(b, w.coeff, 44);
    auto bytes = std::size_t(w.frames / 14) * 8 + (w.frames % 14 ? 1 + (w.frames % 14 + 1) / 2 : 0);
    slice(b, w.samples, bytes);
    require(w.samples + bytes <= w.data + u32(b, w.data + 4),
            "Cry samples exceed their data block");
    return w;
}
int predict(int value, int scale, int c1, int c2, int h1, int h2) {
    auto sum =
        std::int64_t(value) * scale * 2048 + std::int64_t(c1) * h1 + std::int64_t(c2) * h2 + 1024;
    return int(
        std::clamp<std::int64_t>(sum >= 0 ? sum / 2048 : -((-sum + 2047) / 2048), -32768, 32767));
}
}
CryLibrary::CryLibrary(const std::filesystem::path &dump) : archive(dump / CryProfile::archive) {
    std::ifstream f(resource_file_path(dump / "exefs/code.bin"), std::ios::binary);
    require(bool(f), "Cry mappings require the dump's ExeFS/code.bin");
    Bytes table(CryProfile::table_rows * 20);
    f.seekg(CryProfile::table_offset);
    f.read(reinterpret_cast<char *>(table.data()), std::streamsize(table.size()));
    require(bool(f), "Cannot read the Ultra Moon cry table");
    require(u32(table, 24) == (4368u << 16) && u32(table, 44) == ((4369u << 16) | 5),
            "This executable's cry table is not supported");
    for (unsigned row = 0; row < CryProfile::table_rows; ++row) {
        std::array<unsigned, 5> values{};
        for (unsigned i = 0; i < 5; ++i)
            values[i] = u32(table, row * 20 + i * 4);
        require(values[0] < CryProfile::table_rows, "Invalid cry form mapping");
        if (row)
            for (unsigned i = 1; i < 5; ++i)
                require((values[i] & 65535) < archive.size() && (values[i] >> 16) < archive.size(),
                        "Cry table references a missing member");
        rows_.push_back(values);
    }
}
unsigned CryLibrary::forms(unsigned species) const {
    auto start = rows_.at(species)[0];
    if (!start)
        return 0;
    unsigned end = unsigned(rows_.size());
    for (unsigned i = 1; i <= CryProfile::species_max; ++i)
        if (rows_[i][0] > start)
            end = std::min(end, rows_[i][0]);
    return end - start;
}
CryBinding CryLibrary::binding(unsigned species, unsigned form, unsigned context) const {
    require(species > 0 && species <= CryProfile::species_max && context < 4,
            "This Pokemon is outside the supported retail cry table");
    unsigned row = species;
    if (form && rows_[species][0]) {
        require(form <= forms(species), "This form has no supported cry mapping");
        row = rows_[species][0] + form - 1;
    }
    auto packed = rows_[row][context + 1];
    return {packed & 65535, packed >> 16};
}
std::vector<std::string> CryLibrary::uses(unsigned member) const {
    std::vector<std::string> out;
    for (unsigned species = 1; species <= CryProfile::species_max; ++species)
        for (unsigned form = 0; form <= forms(species); ++form)
            for (unsigned context = 0; context < 4; ++context)
                if (binding(species, form, context).wave_archive == member)
                    out.push_back("Species " + std::to_string(species) + ", " +
                                  (form ? "form " + std::to_string(form) : "base cry") + ": " +
                                  cry_contexts[context]);
    return out;
}
MusicSamples decode_cry(View archive) {
    auto w = wave_info(archive);
    auto b = slice(archive, w.at, w.size);
    MusicSamples out;
    out.rate = w.rate;
    out.channels = 1;
    out.pcm.resize(w.frames);
    int h1 = signed16(b, w.coeff + 34), h2 = signed16(b, w.coeff + 36);
    for (unsigned n = 0; n < w.frames; ++n) {
        auto packet = w.samples + (n / 14) * 8;
        unsigned header = b[packet], p = header >> 4;
        if (p >= 8)
            throw std::runtime_error("Invalid cry ADPCM predictor");
        int v = (b[packet + 1 + (n % 14) / 2] >> ((n % 2) ? 0 : 4)) & 15;
        if (v >= 8)
            v -= 16;
        int decoded = predict(v, 1 << (header & 15), signed16(b, w.coeff + p * 4),
                              signed16(b, w.coeff + p * 4 + 2), h1, h2);
        out.pcm[n] = std::int16_t(decoded);
        h2 = h1;
        h1 = decoded;
    }
    return out;
}
Bytes encode_cry(View original, const MusicSamples &mono) {
    require(mono.channels == 1 && mono.rate == 16000 && !mono.pcm.empty(),
            "Replacement must be mono 16 kHz audio");
    auto w = wave_info(original);
    auto src = slice(original, w.at, w.size);
    require(w.at == 128 && w.data == 192 && w.samples == 224 && block(original, 0x6800) == 64 &&
                block(original, 0x6801) == 96,
            "Unsupported cry archive layout for replacement");
    auto frames = mono.frames();
    auto encoded = (frames + 13) / 14 * 8;
    require(w.at + w.samples + encoded <= CryProfile::wave_limit,
            "Cry exceeds the game's 40,000-byte buffer. Trim the replacement to about 4.3 seconds "
            "or less.");
    Bytes wave(src.begin(), src.begin() + w.samples);
    wave.resize(w.samples + encoded);
    put32(wave, 12, narrow(wave.size()));
    put32(wave, 40, narrow(wave.size() - w.data));
    put32(wave, w.data + 4, narrow(wave.size() - w.data));
    wave[w.info + 1] = 0;
    put32(wave, w.info + 4, mono.rate);
    put32(wave, w.info + 8, 0);
    put32(wave, w.info + 12, narrow(frames));
    put32(wave, w.info + 16, 0);
    constexpr std::array<int, 16> co{0,    0,     2048, 0, 4096, -2048, 3072, -1024,
                                     3584, -1536, 1536, 0, 1024, 0,     0,    -1024};
    for (unsigned i = 0; i < 16; ++i)
        put16(wave, w.coeff + i * 2, std::uint16_t(co[i]));
    for (unsigned i = 32; i < 44; i += 2)
        put16(wave, w.coeff + i, 0);
    int h1 = 0, h2 = 0;
    for (std::size_t start = 0; start < frames; start += 14) {
        std::uint64_t best = ~std::uint64_t(0);
        unsigned best_header = 0;
        std::array<int, 14> best_n{};
        int best_h1 = 0, best_h2 = 0;
        auto count = std::min<std::size_t>(14, frames - start);
        for (unsigned p = 0; p < 8; ++p)
            for (unsigned exponent = 0; exponent < 16; ++exponent) {
                int a = h1, b = h2;
                std::uint64_t error = 0;
                std::array<int, 14> values{};
                int scale = 1 << exponent;
                for (unsigned i = 0; i < count; ++i) {
                    double residual = double(mono.pcm[start + i]) -
                                      (double(co[p * 2]) * a + double(co[p * 2 + 1]) * b) / 2048.;
                    int n = std::clamp(int(std::lround(residual / scale)), -8, 7);
                    int decoded = predict(n, scale, co[p * 2], co[p * 2 + 1], a, b);
                    auto diff = std::int64_t(mono.pcm[start + i]) - decoded;
                    error += std::uint64_t(diff * diff);
                    values[i] = n;
                    b = a;
                    a = decoded;
                }
                if (error < best) {
                    best = error;
                    best_header = (p << 4) | exponent;
                    best_n = values;
                    best_h1 = a;
                    best_h2 = b;
                }
            }
        auto at = w.samples + (start / 14) * 8;
        wave[at] = std::uint8_t(best_header);
        for (unsigned i = 0; i < 14; ++i)
            wave[at + 1 + i / 2] |= std::uint8_t((best_n[i] & 15) << ((i % 2) ? 0 : 4));
        h1 = best_h1;
        h2 = best_h2;
    }
    put16(wave, w.coeff + 32, wave[w.samples]);
    Bytes out(original.begin(), original.begin() + w.at);
    append(out, wave);
    put32(out, 12, narrow(out.size()));
    put32(out, 40, narrow(out.size() - 96));
    put32(out, 100, narrow(out.size() - 96));
    put32(out, 84, narrow(wave.size()));
    return out;
}
MusicSamples import_cry_wav(View b) {
    require(text(slice(b, 0, 4)) == "RIFF" && text(slice(b, 8, 4)) == "WAVE" &&
                u32(b, 4) + std::uint64_t(8) == b.size(),
            "Expected a RIFF WAV file");
    View format, data;
    for (std::size_t p = 12; p + 8 <= b.size();) {
        auto size = u32(b, p + 4);
        auto chunk = slice(b, p + 8, size);
        auto tag = text(slice(b, p, 4));
        if (tag == "fmt ")
            format = chunk;
        if (tag == "data")
            data = chunk;
        p += 8 + std::size_t(size) + (size & 1);
    }
    auto type = u16(format, 0), channels = u16(format, 2), bits = u16(format, 14);
    auto rate = u32(format, 4);
    require(channels >= 1 && channels <= 2 && rate >= 8000 && rate <= 192000,
            "WAV must have one or two channels at 8–192 kHz");
    require((type == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) ||
                (type == 3 && bits == 32),
            "Use uncompressed PCM or 32-bit float WAV");
    unsigned stride = channels * (bits / 8);
    require(u16(format, 12) == stride && !data.empty() && data.size() % stride == 0,
            "Invalid WAV sample layout");
    auto frames = data.size() / stride;
    require(frames <= rate * 120ull, "Import a WAV of two minutes or less, then trim the cry");
    MusicSamples out;
    out.rate = rate;
    out.channels = 1;
    out.pcm.resize(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        double sum = 0;
        for (unsigned c = 0; c < channels; ++c) {
            auto p = i * stride + c * (bits / 8);
            double v = 0;
            if (type == 3) {
                float f = f32(data, p);
                require(std::isfinite(f), "WAV contains non-finite samples");
                v = f * 32768.;
            } else if (bits == 8)
                v = (int(data[p]) - 128) * 256.;
            else if (bits == 16)
                v = signed16(data, p);
            else if (bits == 24) {
                int x = data[p] | (data[p + 1] << 8) | (data[p + 2] << 16);
                if (x & 0x800000)
                    x -= 0x1000000;
                v = x / 256.;
            } else {
                auto x = u32(data, p);
                v = (x < 0x80000000 ? double(x) : double(x) - 4294967296.) / 65536.;
            }
            sum += std::clamp(v, -32768., 32767.);
        }
        out.pcm[i] = std::int16_t(std::lround(sum / channels));
    }
    return out;
}
MusicSamples prepare_cry(const MusicSamples &source, float start, float end, float gain) {
    require(source.channels == 1 && source.rate > 0 && std::isfinite(start) && std::isfinite(end) &&
                std::isfinite(gain) && start >= 0 && end > start && gain >= 0 && gain <= 4,
            "Invalid cry trim or gain");
    auto first = std::size_t(std::llround(double(start) * source.rate)),
         last = std::min(source.frames(), std::size_t(std::llround(double(end) * source.rate)));
    require(first < last && last <= source.frames(), "Cry trim is outside the source clip");
    MusicSamples out;
    out.rate = 16000;
    out.channels = 1;
    auto count = std::size_t(std::llround(double(last - first) * out.rate / source.rate));
    require(count && count <= 70000, "Trim the replacement to about 4.3 seconds or less");
    out.pcm.resize(count);
    double ratio = double(source.rate) / out.rate, cutoff = std::min(1., 1. / ratio) * .94;
    int radius = 32;
    for (std::size_t i = 0; i < count; ++i) {
        double pos = first + i * ratio, sum = 0, weight = 0;
        int center = int(std::floor(pos));
        for (int k = -radius; k <= radius; ++k) {
            int index = center + k;
            if (index < int(first) || index >= int(last))
                continue;
            double x = index - pos, z = std::numbers::pi * x * cutoff;
            double sinc = std::abs(z) < 1e-10 ? 1. : std::sin(z) / z;
            double window = .5 + .5 * std::cos(std::numbers::pi * x / (radius + 1));
            double w = sinc * window;
            sum += source.pcm[std::size_t(index)] * w;
            weight += w;
        }
        double v = weight ? sum / weight * gain : 0;
        out.pcm[i] = std::int16_t(std::lround(std::clamp(v, -32768., 32767.)));
    }
    return out;
}
}
