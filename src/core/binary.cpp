#include "core/binary.h"
#include "core/resource_source.h"
#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace studio {
void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
View slice(View b, std::size_t offset, std::size_t size) {
    require(offset <= b.size() && size <= b.size() - offset, "Binary range is out of bounds");
    return b.subspan(offset, size);
}
std::uint16_t u16(View b, std::size_t offset) {
    auto v = slice(b, offset, 2);
    return static_cast<std::uint16_t>(v[0] | (v[1] << 8));
}
std::uint32_t u32(View b, std::size_t offset) {
    auto v = slice(b, offset, 4);
    return std::uint32_t(v[0]) | (std::uint32_t(v[1]) << 8) | (std::uint32_t(v[2]) << 16) |
           (std::uint32_t(v[3]) << 24);
}
float f32(View b, std::size_t offset) {
    return std::bit_cast<float>(u32(b, offset));
}
void put16(Bytes &b, std::size_t offset, std::uint16_t value) {
    slice(b, offset, 2);
    for (std::size_t i = 0; i < 2; ++i)
        b[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}
void put32(Bytes &b, std::size_t offset, std::uint32_t value) {
    slice(b, offset, 4);
    for (std::size_t i = 0; i < 4; ++i)
        b[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}
void put_float(Bytes &b, std::size_t offset, float value) {
    put32(b, offset, std::bit_cast<std::uint32_t>(value));
}
void append(Bytes &b, View data) {
    b.insert(b.end(), data.begin(), data.end());
}
void append32(Bytes &b, std::uint32_t value) {
    auto off = b.size();
    b.resize(off + 4);
    put32(b, off, value);
}
std::uint32_t narrow(std::size_t value) {
    require(value <= std::numeric_limits<std::uint32_t>::max(),
            "Resource exceeds 32-bit format limit");
    return static_cast<std::uint32_t>(value);
}
std::size_t aligned(std::size_t value, std::size_t alignment) {
    require(alignment && (alignment & (alignment - 1)) == 0 && alignment <= 65536,
            "Invalid alignment");
    require(value <= std::numeric_limits<std::size_t>::max() - alignment, "Alignment overflow");
    return (value + alignment - 1) & ~(alignment - 1);
}
std::string text(View bytes) {
    auto end = std::find(bytes.begin(), bytes.end(), 0);
    return {bytes.begin(), end};
}
Bytes read_file(const std::filesystem::path &path) {
    std::ifstream f(resource_file_path(path), std::ios::binary | std::ios::ate);
    require(bool(f), "Cannot open " + path.string());
    auto size = f.tellg();
    require(size >= 0 && size <= 1024LL * 1024 * 1024, "Input exceeds 1 GiB read limit");
    Bytes b(static_cast<std::size_t>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char *>(b.data()), static_cast<std::streamsize>(b.size()));
    require(bool(f), "Cannot read " + path.string());
    return b;
}
void write_new_file(const std::filesystem::path &path, View data) {
    require(!std::filesystem::exists(path), "Output already exists: " + path.string());
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    require(bool(f), "Cannot create " + path.string());
    f.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    f.close();
    require(bool(f), "Cannot finish " + path.string());
}
void replace_file(const std::filesystem::path &temporary,
                  const std::filesystem::path &destination) {
#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::system_error(int(GetLastError()), std::system_category(),
                                "Cannot replace " + destination.string());
#else
    std::filesystem::rename(temporary, destination);
#endif
}
void write_file_atomic(const std::filesystem::path &path, View data) {
    auto temporary = path;
    temporary += ".saving";
    require(!std::filesystem::exists(temporary),
            "A previous save is unfinished: " + temporary.string());
    try {
        write_new_file(temporary, data);
        replace_file(temporary, path);
    } catch (...) {
        std::error_code error;
        std::filesystem::remove(temporary, error);
        throw;
    }
}

}
