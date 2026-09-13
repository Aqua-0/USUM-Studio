#include "formats/container.h"
#include <algorithm>
namespace studio {
Container Container::parse(View b, const std::string &expected) {
    slice(b, 0, 8);
    Container c;
    c.tag = {b[0], b[1]};
    require(expected.empty() || std::string(c.tag.begin(), c.tag.end()) == expected,
            "Unexpected container tag; expected " + expected);
    auto count = u16(b, 2);
    auto table = 4 + 4 * (std::size_t(count) + 1);
    slice(b, 0, table);
    auto start = u32(b, 4);
    require(start >= table, "Container overlaps its offset table");
    for (std::size_t i = 0; i < count; ++i) {
        auto end = u32(b, 8 + 4 * i);
        require(end >= start, "Container offsets are not ordered");
        auto v = slice(b, start, end - start);
        c.files.emplace_back(v.begin(), v.end());
        start = end;
    }
    require(start == b.size(), "Container has unaccounted trailing bytes");
    c.original.assign(b.begin(), b.end());
    return c;
}
Bytes Container::write(std::size_t alignment) const {
    require(files.size() <= 65535, "Too many nested resources");
    aligned(0, alignment);
    if (!original.empty()) {
        auto old = parse(original);
        bool compatible = true;
        for (std::size_t i = 0; i <= old.files.size(); ++i)
            compatible &= u32(original, 4 + 4 * i) % alignment == 0;
        if (compatible && old.tag == tag && old.files == files)
            return original;
    }
    Bytes b(aligned(4 + 4 * (files.size() + 1), alignment));
    b[0] = tag[0];
    b[1] = tag[1];
    put16(b, 2, static_cast<std::uint16_t>(files.size()));
    for (std::size_t i = 0; i < files.size(); ++i) {
        put32(b, 4 + 4 * i, narrow(b.size()));
        append(b, files[i]);
        b.resize(aligned(b.size(), alignment));
    }
    put32(b, 4 + 4 * files.size(), narrow(b.size()));
    return b;
}
}
