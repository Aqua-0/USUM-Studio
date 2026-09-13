#include "audio/sound_wave.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace studio {
namespace {
std::size_t ref(View b, std::size_t p, std::size_t base) {
    auto result = base + std::size_t(u32(b, p + 4));
    slice(b, result, 1);
    return result;
}
std::size_t block(View b, unsigned type) {
    require(u16(b, 4) == 0xfeff && u16(b, 16) <= 16, "Unsupported sound wave header");
    for (unsigned i = 0; i < u16(b, 16); ++i)
        if (u16(b, 20 + i * 12) == type) {
            auto p = u32(b, 24 + i * 12);
            slice(b, p, u32(b, 28 + i * 12));
            return p;
        }
    throw std::runtime_error("Sound wave block missing");
}
int s16(View b, std::size_t p) {
    return std::int16_t(u16(b, p));
}
int predict(int n, int scale, int a, int c, int h1, int h2) {
    auto sum = std::int64_t(n) * scale * 2048 + std::int64_t(a) * h1 + std::int64_t(c) * h2 + 1024;
    return int(
        std::clamp<std::int64_t>(sum >= 0 ? sum / 2048 : -((-sum + 2047) / 2048), -32768, 32767));
}
struct Channel {
    std::size_t samples, coeff;
};
struct Wave {
    MusicSamples audio;
    std::vector<Channel> channels;
};
Wave describe(View b) {
    require(text(slice(b, 0, 4)) == "CWAV" && u32(b, 12) == b.size(), "Invalid sound wave");
    auto info = block(b, 0x7000) + 8, data = block(b, 0x7001);
    require(b[info] == 2, "Sound effects currently support DSP ADPCM samples");
    Wave w;
    auto &a = w.audio;
    a.looping = b[info + 1] != 0;
    a.rate = u32(b, info + 4);
    a.loop_start = u32(b, info + 8);
    auto frames = u32(b, info + 12);
    a.channels = u32(b, info + 20);
    require(a.channels >= 1 && a.channels <= 2 && a.rate >= 8000 && a.rate <= 192000 &&
                frames > 0 && frames <= 10000000,
            "Unsupported sound sample dimensions");
    require(!a.looping || a.loop_start < frames, "Invalid sound loop point");
    a.pcm.resize(std::size_t(frames) * a.channels);
    auto table = info + 20;
    for (unsigned c = 0; c < a.channels; ++c) {
        auto p = ref(b, table + 4 + c * 8, table);
        Channel ch{ref(b, p, data + 8), ref(b, p + 8, p)};
        slice(b, ch.coeff, 44);
        auto count = frames / 14 * 8 + (frames % 14 ? 1 + (frames % 14 + 1) / 2 : 0);
        slice(b, ch.samples, count);
        require(ch.samples + count <= data + u32(b, data + 4),
                "Sound samples exceed their data block");
        w.channels.push_back(ch);
    }
    return w;
}
}
MusicSamples decode_sound_wave(View bytes) {
    auto w = describe(bytes);
    for (unsigned c = 0; c < w.channels.size(); ++c) {
        auto ch = w.channels[c];
        int h1 = s16(bytes, ch.coeff + 34), h2 = s16(bytes, ch.coeff + 36);
        for (std::size_t i = 0; i < w.audio.frames(); ++i) {
            auto p = ch.samples + i / 14 * 8;
            auto header = bytes[p];
            require(header >> 4 < 8, "Invalid sound sample predictor");
            int n = (bytes[p + 1 + i % 14 / 2] >> ((i % 2) ? 0 : 4)) & 15;
            if (n >= 8)
                n -= 16;
            auto sample = predict(n, 1 << (header & 15), s16(bytes, ch.coeff + (header >> 4) * 4),
                                  s16(bytes, ch.coeff + (header >> 4) * 4 + 2), h1, h2);
            h2 = h1;
            h1 = sample;
            w.audio.pcm[i * w.audio.channels + c] = std::int16_t(sample);
        }
    }
    return w.audio;
}
void validate_sound_wave_replacement(View original, View replacement) {
    auto w = describe(original);
    decode_sound_wave(replacement);
    require(original.size() == replacement.size(), "Keep the original sound sample allocation");
    std::vector<bool> allowed(original.size());
    auto frames = w.audio.frames();
    auto count = frames / 14 * 8 + (frames % 14 ? 1 + (frames % 14 + 1) / 2 : 0);
    for (auto &ch : w.channels) {
        std::fill(allowed.begin() + ch.coeff, allowed.begin() + ch.coeff + 44, true);
        std::fill(allowed.begin() + ch.samples, allowed.begin() + ch.samples + count, true);
    }
    for (std::size_t i = 0; i < original.size(); ++i)
        require(allowed[i] || original[i] == replacement[i],
                "Sound replacement changes protected metadata or padding");
}
Bytes replace_sound_wave(View original, const MusicSamples &input) {
    auto w = describe(original);
    require(input.channels >= 1 && input.channels <= 2 && input.rate && !input.pcm.empty() &&
                input.pcm.size() % input.channels == 0,
            "Import mono or stereo WAV audio");
    auto decoded_original = decode_sound_wave(original);
    if (input.rate == decoded_original.rate && input.channels == decoded_original.channels &&
        input.pcm == decoded_original.pcm)
        return {original.begin(), original.end()};
    auto frames = w.audio.frames();
    require(std::uint64_t(input.frames()) * w.audio.rate <= std::uint64_t(frames) * input.rate,
            "Replacement is longer than this sample. Trim it externally before importing; shorter "
            "audio is padded with silence.");
    Bytes out(original.begin(), original.end());
    constexpr int co[16] = {0,    0,     2048, 0, 4096, -2048, 3072, -1024,
                            3584, -1536, 1536, 0, 1024, 0,     0,    -1024};
    for (unsigned c = 0; c < w.channels.size(); ++c) {
        auto ch = w.channels[c];
        std::vector<int> target(frames), decoded(frames);
        for (std::size_t i = 0; i < frames; ++i) {
            double position = double(i) * input.rate / w.audio.rate;
            if (position >= input.frames())
                break;
            auto left = std::size_t(position), right = std::min(left + 1, input.frames() - 1);
            auto get = [&](std::size_t f) {
                if (w.audio.channels == 1 && input.channels == 2)
                    return (int(input.pcm[f * 2]) + int(input.pcm[f * 2 + 1])) * .5;
                return double(input.pcm[f * input.channels + std::min(c, input.channels - 1)]);
            };
            target[i] = int(std::lround(get(left) + (get(right) - get(left)) * (position - left)));
        }
        for (unsigned i = 0; i < 16; ++i)
            put16(out, ch.coeff + i * 2, std::uint16_t(co[i]));
        for (unsigned i = 32; i < 44; i += 2)
            put16(out, ch.coeff + i, 0);
        int h1 = 0, h2 = 0;
        for (std::size_t start = 0; start < frames; start += 14) {
            auto count = std::min<std::size_t>(14, frames - start);
            std::uint64_t best = ~std::uint64_t(0);
            unsigned header = 0;
            std::array<int, 14> chosen{};
            for (unsigned p = 0; p < 8; ++p)
                for (unsigned exponent = 0; exponent < 16; ++exponent) {
                    int a = h1, b = h2;
                    std::uint64_t error = 0;
                    std::array<int, 14> values{};
                    for (unsigned i = 0; i < count; ++i) {
                        double residual =
                            target[start + i] -
                            (double(co[p * 2]) * a + double(co[p * 2 + 1]) * b) / 2048;
                        int n = std::clamp(int(std::lround(residual / (1 << exponent))), -8, 7);
                        int value = predict(n, 1 << exponent, co[p * 2], co[p * 2 + 1], a, b);
                        auto delta = std::int64_t(target[start + i]) - value;
                        error += std::uint64_t(delta * delta);
                        values[i] = n;
                        b = a;
                        a = value;
                    }
                    if (error < best) {
                        best = error;
                        header = (p << 4) | exponent;
                        chosen = values;
                    }
                }
            auto p = ch.samples + start / 14 * 8;
            out[p] = std::uint8_t(header);
            for (unsigned i = 0; i < count; ++i) {
                auto shift = (i % 2) ? 0 : 4;
                auto &byte = out[p + 1 + i / 2];
                byte = std::uint8_t((byte & ~(15 << shift)) | ((chosen[i] & 15) << shift));
                int value = predict(chosen[i], 1 << (header & 15), co[(header >> 4) * 2],
                                    co[(header >> 4) * 2 + 1], h1, h2);
                h2 = h1;
                h1 = value;
                decoded[start + i] = value;
            }
        }
        put16(out, ch.coeff + 32, out[ch.samples]);
        if (w.audio.looping) {
            auto loop = w.audio.loop_start;
            put16(out, ch.coeff + 38, out[ch.samples + loop / 14 * 8]);
            put16(out, ch.coeff + 40, std::uint16_t(loop ? decoded[loop - 1] : 0));
            put16(out, ch.coeff + 42, std::uint16_t(loop > 1 ? decoded[loop - 2] : 0));
        }
    }
    decode_sound_wave(out);
    return out;
}
}
