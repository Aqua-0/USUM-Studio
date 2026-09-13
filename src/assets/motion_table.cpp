#include "assets/motion_table.h"
#include <map>
#include <algorithm>
namespace studio {
namespace {
std::map<std::size_t, std::vector<std::size_t>> records(View bytes) {
    auto count = u32(bytes, 0);
    require(count < 65536, "Invalid motion slot count");
    slice(bytes, 4, std::size_t(count) * 4);
    std::map<std::size_t, std::vector<std::size_t>> result;
    for (unsigned i = 0; i < count; ++i)
        if (auto offset = u32(bytes, 4 + i * 4)) {
            auto start = std::size_t(offset) + 4;
            require(start >= 4 + std::size_t(count) * 4 && start + 4 <= bytes.size(),
                    "Invalid motion slot offset");
            require(u32(bytes, start) == 0x60000, "Unsupported motion slot version");
            result[start].push_back(i);
        }
    return result;
}
}
Bytes motion_table_resource(View bytes, std::size_t slot) {
    auto entries = records(bytes);
    require(slot < u32(bytes, 0) && u32(bytes, 4 + slot * 4), "Motion slot is empty");
    auto start = std::size_t(u32(bytes, 4 + slot * 4)) + 4;
    auto next = entries.upper_bound(start);
    auto end = next == entries.end() ? bytes.size() : next->first;
    auto part = slice(bytes, start, end - start);
    return Bytes(part.begin(), part.end());
}
Bytes replace_motion_table_resource(View bytes, std::size_t slot, View replacement) {
    auto original = motion_table_resource(bytes, slot);
    if (std::equal(original.begin(), original.end(), replacement.begin(), replacement.end()))
        return Bytes(bytes.begin(), bytes.end());
    require(u32(replacement, 0) == 0x60000, "Replacement must be a motion");
    auto entries = records(bytes);
    Bytes out(bytes.begin(), bytes.begin() + entries.begin()->first);
    auto write = [&](const std::vector<std::size_t> &slots, View data) {
        for (auto index : slots)
            put32(out, 4 + index * 4, narrow(out.size() - 4));
        append(out, data);
        out.resize(aligned(out.size(), 4));
    };
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        auto next = std::next(it);
        auto end = next == entries.end() ? bytes.size() : next->first;
        std::vector<std::size_t> unchanged;
        bool edited = false;
        for (auto index : it->second) {
            if (index == slot)
                edited = true;
            else
                unchanged.push_back(index);
        }
        if (!unchanged.empty())
            write(unchanged, slice(bytes, it->first, end - it->first));
        if (edited)
            write({slot}, replacement);
    }
    return out;
}
}
