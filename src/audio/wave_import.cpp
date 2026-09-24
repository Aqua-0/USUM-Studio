#include "audio/wave_import.h"
#include <algorithm>
#include <cmath>
namespace studio {
MusicSamples import_audio_wav(View b) {
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
    require(frames <= rate * 120ull, "Import a WAV of two minutes or less");
    MusicSamples out;
    out.rate = rate;
    out.channels = channels;
    out.pcm.resize(frames * channels);
    for (std::size_t i = 0; i < frames; ++i) {
        for (unsigned c = 0; c < channels; ++c) {
            auto p = i * stride + c * (bits / 8);
            double v = 0;
            if (type == 3) {
                float f = f32(data, p);
                require(std::isfinite(f), "WAV contains non-finite samples");
                v = f * 32768.;
            } else if (bits == 8)
                v = (int(data[p]) - 128) * 256.;
            else if (bits == 16) {
                auto sample = u16(data, p);
                v = sample < 32768 ? int(sample) : int(sample) - 65536;
            } else if (bits == 24) {
                int x = data[p] | (data[p + 1] << 8) | (data[p + 2] << 16);
                if (x & 0x800000)
                    x -= 0x1000000;
                v = x / 256.;
            } else {
                auto x = u32(data, p);
                v = (x < 0x80000000 ? double(x) : double(x) - 4294967296.) / 65536.;
            }
            out.pcm[i * channels + c] = std::int16_t(std::lround(std::clamp(v, -32768., 32767.)));
        }
    }
    return out;
}
}
