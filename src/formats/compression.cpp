#include "formats/compression.h"
#include <algorithm>
#include <array>

namespace studio {
Bytes decompress(View b) {
    if (b.empty() || b[0] != 0x11)
        return {b.begin(), b.end()};
    std::size_t size = u32(b, 0) >> 8, pos = 4;
    if (!size) {
        size = u32(b, pos);
        pos += 4;
    }
    require(size <= 256 * 1024 * 1024, "Compressed resource exceeds 256 MiB limit");
    Bytes out;
    out.reserve(size);
    auto byte = [&]() {
        return slice(b, pos++, 1)[0];
    };
    while (out.size() < size) {
        auto flags = byte();
        for (int bit = 7; bit >= 0 && out.size() < size; --bit) {
            if (!(flags & (1 << bit))) {
                out.push_back(byte());
                continue;
            }
            unsigned a = byte(), length = 0, distance = 0;
            if ((a >> 4) == 0) {
                unsigned c = byte(), d = byte();
                length = ((a & 15) << 4) + (c >> 4) + 17;
                distance = ((c & 15) << 8) + d + 1;
            } else if ((a >> 4) == 1) {
                unsigned c = byte(), d = byte(), e = byte();
                length = ((a & 15) << 12) + (c << 4) + (d >> 4) + 273;
                distance = ((d & 15) << 8) + e + 1;
            } else {
                unsigned c = byte();
                length = (a >> 4) + 1;
                distance = ((a & 15) << 8) + c + 1;
            }
            require(distance <= out.size() && length <= size - out.size(),
                    "Invalid compressed back-reference");
            for (unsigned i = 0; i < length; ++i)
                out.push_back(out[out.size() - distance]);
        }
    }
    return out;
}
Bytes compress(View b) {
    require(!b.empty() && b.size() <= 256 * 1024 * 1024, "Unsupported compression input size");
    Bytes out;
    if (b.size() < 0x1000000)
        append32(out, (narrow(b.size()) << 8) | 0x11);
    else {
        append32(out, 0x11);
        append32(out, narrow(b.size()));
    }
    std::array<int, 65536> heads;
    heads.fill(-1);
    std::vector<int> previous(b.size(), -1);
    auto hash = [&](std::size_t p) {
        return ((unsigned(b[p]) * 251 + unsigned(b[p + 1])) * 251 + b[p + 2]) & 65535;
    };
    auto remember = [&](std::size_t p) {
        if (p + 2 < b.size()) {
            auto h = hash(p);
            previous[p] = heads[h];
            heads[h] = static_cast<int>(p);
        }
    };
    for (std::size_t pos = 0; pos < b.size();) {
        auto flagpos = out.size();
        out.push_back(0);
        for (int bit = 7; bit >= 0 && pos < b.size(); --bit) {
            std::size_t best = 0, distance = 0;
            if (pos + 2 < b.size()) {
                int candidate = heads[hash(pos)];
                for (int tries = 0; candidate >= 0 && tries < 64; ++tries) {
                    auto from = static_cast<std::size_t>(candidate);
                    if (pos - from > 4096)
                        break;
                    std::size_t length = 0, limit = std::min<std::size_t>(65808, b.size() - pos);
                    while (length < limit && b[from + length] == b[pos + length])
                        ++length;
                    if (length > best) {
                        best = length;
                        distance = pos - from;
                    }
                    if (length == limit)
                        break;
                    candidate = previous[from];
                }
            }
            auto emit = [&](std::size_t value) {
                out.push_back(static_cast<std::uint8_t>(value));
            };
            if (best < 3) {
                emit(b[pos]);
                remember(pos++);
                continue;
            }
            out[flagpos] |= static_cast<std::uint8_t>(1 << bit);
            --distance;
            if (best <= 16) {
                emit(((best - 1) << 4) | (distance >> 8));
                emit(distance);
            } else if (best <= 272) {
                auto n = best - 17;
                emit(n >> 4);
                emit((n << 4) | (distance >> 8));
                emit(distance);
            } else {
                auto n = best - 273;
                emit(0x10 | (n >> 12));
                emit(n >> 4);
                emit((n << 4) | (distance >> 8));
                emit(distance);
            }
            for (std::size_t i = 0; i < best; ++i)
                remember(pos++);
        }
    }
    return out;
}
}
