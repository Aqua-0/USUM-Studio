#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace studio {
using Bytes = std::vector<std::uint8_t>;
using View = std::span<const std::uint8_t>;
void require(bool condition, const std::string &message);
View slice(View bytes, std::size_t offset, std::size_t size);
std::uint16_t u16(View bytes, std::size_t offset);
std::uint32_t u32(View bytes, std::size_t offset);
float f32(View bytes, std::size_t offset);
void put16(Bytes &bytes, std::size_t offset, std::uint16_t value);
void put32(Bytes &bytes, std::size_t offset, std::uint32_t value);
void put_float(Bytes &bytes, std::size_t offset, float value);
void append(Bytes &bytes, View data);
void append32(Bytes &bytes, std::uint32_t value);
std::uint32_t narrow(std::size_t value);
std::size_t aligned(std::size_t value, std::size_t alignment);
std::string text(View bytes);
Bytes read_file(const std::filesystem::path &path);
void replace_file(const std::filesystem::path &temporary, const std::filesystem::path &destination);
void write_file_atomic(const std::filesystem::path &path, View data);
void write_new_file(const std::filesystem::path &path, View data);
}
