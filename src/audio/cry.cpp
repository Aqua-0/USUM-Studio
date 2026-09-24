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
CryExpansionRouting read_cry_expansion_routing(View code, std::size_t archive_size) {
    CryExpansionRouting out;
    auto target = [&](std::size_t at) {
        auto word = u32(code, at);
        auto displacement = std::int32_t((word & 0xffffff) << 8) / 64;
        auto destination = std::int64_t(at) + 8 + displacement;
        require((word >> 24) == 0xea && destination >= 0 &&
                    std::uint64_t(destination) + 4 <= code.size(),
                "Unsupported cry routing branch");
        return std::size_t(destination);
    };
    for (auto hook : {0x2be1c0u, 0x2be1c4u}) {
        if (hook + 4 > code.size() || (u32(code, hook) >> 24) != 0xea)
            continue;
        auto start = target(hook);
        constexpr unsigned common[] = {0xe3560000, 0x0a000012, 0xe356001f, 0x8a000010, 0xe3540b02,
                                       0x2a000011, 0xe1840586, 0,          0xe4912004, 0xe6ff3072,
                                       0xe1530000, 0x3afffffb, 0x1a000007, 0xe1a02822, 0xe3120902,
                                       0x0a000009, 0xe2442fca, 0xe35200d9, 0x9a000006, 0xe3a06000,
                                       0xea000002, 0xe2442fca, 0xe35200d9, 0x9a000001, 0xe3540000};
        slice(code, start, 120);
        for (unsigned i = 0; i < std::size(common); ++i)
            if (i != 7)
                require(u32(code, start + i * 4) == common[i], "Unsupported patched cry lookup");
        auto adr = u32(code, start + 28);
        require(adr == 0xe28f1054 || adr == 0xe28f1060, "Unsupported cry form table location");
        out.redirect_meltan = adr == 0xe28f1060;
        unsigned length = out.redirect_meltan ? 132 : 120;
        slice(code, start, length);
        require(target(start + 100) == hook + 4 && target(start + length - 8) == hook + 172,
                "Cry lookup returns to an unsupported location");
        unsigned route = 104;
        if (out.redirect_meltan) {
            require(u32(code, start + route) == 0xe3540fca &&
                        u32(code, start + route + 4) == 0x03a02c01 &&
                        u32(code, start + route + 8) == 0x0282201d,
                    "Unsupported Meltan cry redirect");
            route += 12;
        }
        require(u32(code, start + route) == 0xe59f3004 &&
                    u32(code, start + route + 4) == 0xe0835802,
                "Unsupported expanded cry binding");
        auto packed = u32(code, start + length - 4);
        out.sequence = packed & 65535;
        out.first_wave = packed >> 16;
        require(out.sequence < archive_size && out.first_wave + 217 < archive_size &&
                    (!out.redirect_meltan || out.first_wave + 285 < archive_size),
                "Expansion cry routing references missing audio; use a matching code.bin and cry "
                "archive");
        unsigned previous = 0;
        bool ended = false;
        for (std::size_t at = start + length; at + 4 <= code.size() && at < start + length + 8192;
             at += 4) {
            auto key = u16(code, at), clip = u16(code, at + 2);
            if (key == 65535) {
                ended = true;
                break;
            }
            require(key > previous && (key & 2047) > 0 && (key >> 11) > 0 &&
                        (clip == 0x8000 || out.first_wave + clip < archive_size),
                    "Invalid expansion cry form mapping");
            out.forms.emplace(key, clip);
            previous = key;
        }
        require(ended, "Expansion cry form table is unterminated");
        out.enabled = true;
        return out;
    }
    return out;
}
CryLibrary::CryLibrary(const std::filesystem::path &dump) : archive(dump / CryProfile::archive) {
    auto code = read_file(resource_file_path(executable_resource_path(dump)));
    expansion_ = read_cry_expansion_routing(code, archive.size());
    std::size_t offset = CryProfile::table_offset;
    auto matches = [&](std::size_t at) {
        return at + CryProfile::table_rows * 20 <= code.size() &&
               u32(code, at + 24) == (4368u << 16) && u32(code, at + 44) == ((4369u << 16) | 5);
    };
    if (!matches(offset))
        offset -= 4;
    require(matches(offset), "This executable's retail cry table is not supported");
    auto table = slice(code, offset, CryProfile::table_rows * 20);
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
    auto start = species <= CryProfile::species_max ? rows_.at(species)[0] : 0;
    if (!start)
        return 0;
    unsigned end = unsigned(rows_.size());
    for (unsigned i = 1; i <= CryProfile::species_max; ++i)
        if (rows_[i][0] > start)
            end = std::min(end, rows_[i][0]);
    return end - start;
}
CryBinding CryLibrary::binding(unsigned species, unsigned form, unsigned context) const {
    require(species > 0 && species <= (expansion_.enabled ? 1025u : CryProfile::species_max) &&
                context < 4,
            "No cry mapping for this species. Expansion Pokemon require the matching patched "
            "code.bin.");
    if (expansion_.enabled) {
        unsigned clip = 0x8000;
        if (form > 0 && form <= 31) {
            auto found = expansion_.forms.find(species | (form << 11));
            if (found != expansion_.forms.end()) {
                clip = found->second;
                if (clip == 0x8000)
                    form = 0;
            }
        }
        if (clip == 0x8000 && species >= 808)
            clip = species - 808;
        if (clip != 0x8000) {
            if (expansion_.redirect_meltan && species == 808)
                clip = 285;
            return {expansion_.sequence, expansion_.first_wave + clip};
        }
    }
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
    for (unsigned species = 1; species <= (expansion_.enabled ? 1025u : CryProfile::species_max);
         ++species) {
        std::vector<unsigned> known_forms;
        for (unsigned form = 0; form <= forms(species); ++form)
            known_forms.push_back(form);
        for (const auto &[key, clip] : expansion_.forms)
            if ((key & 2047) == species &&
                std::find(known_forms.begin(), known_forms.end(), key >> 11) == known_forms.end())
                known_forms.push_back(key >> 11);
        for (auto form : known_forms)
            for (unsigned context = 0; context < 4; ++context)
                if (binding(species, form, context).wave_archive == member)
                    out.push_back("Species " + std::to_string(species) + ", " +
                                  (form ? "form " + std::to_string(form) : "base cry") + ": " +
                                  cry_contexts[context]);
    }
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
MusicSamples prepare_cry(const MusicSamples &source, float start, float end, float gain) {
    require((source.channels == 1 || source.channels == 2) &&
                source.pcm.size() % source.channels == 0 && source.rate > 0 &&
                std::isfinite(start) && std::isfinite(end) && std::isfinite(gain) && start >= 0 &&
                end > start && gain >= 0 && gain <= 4,
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
            double sample = 0;
            for (unsigned channel = 0; channel < source.channels; ++channel)
                sample += source.pcm[std::size_t(index) * source.channels + channel];
            sum += sample / source.channels * w;
            weight += w;
        }
        double v = weight ? sum / weight * gain : 0;
        out.pcm[i] = std::int16_t(std::lround(std::clamp(v, -32768., 32767.)));
    }
    return out;
}
}
