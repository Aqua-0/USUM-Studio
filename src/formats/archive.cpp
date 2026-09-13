#include "formats/archive.h"
#include "formats/compression.h"
#include <algorithm>
#include <fstream>

namespace studio {
namespace {
using Entries = std::vector<std::map<unsigned, ArchiveEntry>>;
template <class Changes> void extend(Entries &entries, const Changes &changes) {
    const auto original = entries.size();
    for (auto &[key, value] : changes) {
        (void)value;
        require(key.second < 32 && key.first < 65535,
                "Archive allocation exceeds the format range");
        if (key.first < original) {
            require(entries[key.first].contains(key.second),
                    "Existing archive subfile layout cannot change");
            continue;
        }
        require(key.first <= entries.size(), "Appended archive members must be contiguous");
        if (key.first == entries.size())
            entries.emplace_back();
        entries[key.first][key.second] = {};
    }
}
Bytes metadata(View original, Entries &entries, std::size_t &image) {
    std::size_t table_size = 12, subfiles = 0;
    for (auto &entry : entries) {
        table_size += 4 + entry.size() * 12;
        subfiles += entry.size();
    }
    auto fatb = 48 + entries.size() * 4;
    image = fatb + table_size;
    Bytes result(image + 12);
    std::copy_n(original.begin(), 36, result.begin());
    auto tag = [&](std::size_t at, const char *value) {
        std::copy_n(value, 4, result.begin() + at);
    };
    tag(36, "OTAF");
    put32(result, 40, narrow(12 + entries.size() * 4));
    put16(result, 44, std::uint16_t(entries.size()));
    put16(result, 46, u16(original, 46));
    tag(fatb, "BTAF");
    put32(result, fatb + 4, narrow(table_size));
    put32(result, fatb + 8, narrow(subfiles));
    auto record = fatb + 12;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        put32(result, 48 + i * 4, narrow(record - fatb - 12));
        std::uint32_t mask = 0;
        for (auto &[sub, e] : entries[i])
            mask |= 1u << sub;
        put32(result, record, mask);
        for (auto &[sub, e] : entries[i]) {
            (void)sub;
            e.record = record;
            put32(result, record + 4, e.start);
            put32(result, record + 8, e.end);
            put32(result, record + 12, e.size);
            record += 12;
        }
        record += 4;
    }
    tag(image, "BMIF");
    put32(result, image + 4, 12);
    put32(result, 16, narrow(result.size()));
    return result;
}
Bytes read_stream(std::ifstream &f, std::size_t offset, std::size_t count) {
    require(count <= 256 * 1024 * 1024, "Archive member exceeds read limit");
    f.seekg(static_cast<std::streamoff>(offset));
    Bytes b(count);
    f.read(reinterpret_cast<char *>(b.data()), static_cast<std::streamsize>(count));
    require(bool(f), "Archive read failed");
    return b;
}
Bytes read_range(const std::filesystem::path &path, std::size_t offset, std::size_t count) {
    std::ifstream f(path, std::ios::binary);
    require(bool(f), "Cannot open archive: " + path.string());
    return read_stream(f, offset, count);
}
}
Bytes read_archive_subfile(const std::filesystem::path &path, std::size_t member,
                           unsigned subfile) {
    return Archive(path).decoded(member, subfile);
}
Archive::Archive(const std::filesystem::path &requested) {
    auto resolved = resource_source(requested);
    path_ = resolved.original;
    overrides_ = std::move(resolved.members);
    resource_pin_ = pin_resources(overrides_);
    const auto &path = path_;
    modified_ = std::filesystem::last_write_time(path);
    file_size_ = std::filesystem::file_size(path);
    auto h = read_range(path, 0, 36);
    require(text(slice(h, 0, 4)) == "CRAG" && u16(h, 8) == 0xfeff,
            "Expected a little-endian GARC archive");
    require(u16(h, 10) == 0x600 && u32(h, 4) == 36 && u32(h, 12) == 4,
            "Unsupported archive profile");
    data_offset_ = u32(h, 16);
    alignment_ = u32(h, 32);
    aligned(0, alignment_);
    require(data_offset_ >= 36 && data_offset_ <= 16 * 1024 * 1024,
            "Invalid archive metadata size");
    require(std::filesystem::file_size(path) == u32(h, 20),
            "Archive file length does not match its header");
    prefix_ = read_range(path, 0, data_offset_);
    std::size_t fato = 36;
    require(text(slice(prefix_, fato, 4)) == "OTAF", "Missing archive offset table");
    auto count = u16(prefix_, fato + 8);
    require(u32(prefix_, fato + 4) == 12 + std::size_t(count) * 4,
            "Invalid archive offset table size");
    auto fatb = fato + u32(prefix_, fato + 4);
    require(text(slice(prefix_, fatb, 4)) == "BTAF", "Missing archive entry table");
    image_header_ = fatb + u32(prefix_, fatb + 4);
    require(text(slice(prefix_, image_header_, 4)) == "BMIF" &&
                u32(prefix_, image_header_ + 4) == 12,
            "Missing archive data block");
    require(image_header_ + 12 == data_offset_ &&
                std::uint64_t(data_offset_) + u32(prefix_, image_header_ + 8) == u32(h, 20),
            "Invalid archive data extent");
    std::uint32_t previous_end = 0;
    for (std::size_t i = 0; i < count; ++i) {
        auto record = fatb + 12 + u32(prefix_, fato + 12 + 4 * i);
        require(record >= fatb + 12 && record + 4 <= image_header_, "Archive entry exceeds table");
        auto mask = u32(prefix_, record);
        require(mask != 0, "Empty archive entry mask");
        std::map<unsigned, ArchiveEntry> entry;
        for (unsigned sub = 0; sub < 32; ++sub)
            if (mask & (1u << sub)) {
                require(record + 16 <= image_header_, "Archive subfile exceeds table");
                ArchiveEntry e{u32(prefix_, record + 4), u32(prefix_, record + 8),
                               u32(prefix_, record + 12), record};
                require(e.start >= previous_end && e.end >= e.start && e.size <= e.end - e.start,
                        "Invalid archive member range");
                require(std::uint64_t(data_offset_) + e.end <= u32(h, 20),
                        "Archive member exceeds file");
                previous_end = e.end;
                entry[sub] = e;
                record += 12;
            }
        entries_.push_back(std::move(entry));
    }
    extend(entries_, overrides_);
    if (entries_.size() != count)
        prefix_ = metadata(prefix_, entries_, image_header_);
}
Bytes Archive::layout_identity() const {
    auto result = prefix_;
    for (auto offset : {20u, 24u, 28u})
        put32(result, offset, 0);
    put32(result, image_header_ + 8, 0);
    for (auto &entry : entries_)
        for (auto &[sub, e] : entry) {
            (void)sub;
            for (unsigned n = 4; n <= 12; n += 4)
                put32(result, e.record + n, 0);
        }
    return result;
}
std::vector<unsigned> Archive::subfiles(std::size_t index) const {
    require(index < size(), "Archive member index is out of range");
    std::vector<unsigned> result;
    for (auto &[sub, entry] : entries_[index]) {
        (void)entry;
        result.push_back(sub);
    }
    return result;
}
Bytes Archive::raw(std::size_t index, unsigned subfile) const {
    require(std::filesystem::last_write_time(path_) == modified_ &&
                std::filesystem::file_size(path_) == file_size_,
            "Source archive changed; reopen against the original dump");
    require(index < size() && entries_[index].contains(subfile),
            "Archive member or subfile is unavailable");
    if (auto found = overrides_.find({index, subfile}); found != overrides_.end())
        return resource_object(found->second);
    auto e = entries_[index].at(subfile);
    return read_range(path_, std::size_t(data_offset_) + e.start, e.size);
}
Bytes Archive::decoded(std::size_t index, unsigned subfile) const {
    return decompress(raw(index, subfile));
}
void Archive::export_to(const std::filesystem::path &output,
                        const std::map<std::size_t, Bytes> &replacements,
                        bool replace_existing) const {
    std::map<std::pair<std::size_t, unsigned>, Bytes> changes;
    for (auto &[index, bytes] : replacements)
        changes[{index, 0}] = bytes;
    export_subfiles(output, changes, replace_existing);
}
void Archive::export_subfiles(const std::filesystem::path &output,
                              const std::map<std::pair<std::size_t, unsigned>, Bytes> &replacements,
                              bool replace_existing) const {
    write(output, replacements, replace_existing, false);
}
void Archive::export_appended(const std::filesystem::path &output,
                              const std::map<std::pair<std::size_t, unsigned>, Bytes> &replacements,
                              bool replace_existing) const {
    write(output, replacements, replace_existing, true);
}
void Archive::write(const std::filesystem::path &output,
                    const std::map<std::pair<std::size_t, unsigned>, Bytes> &replacements,
                    bool replace_existing, bool append) const {
    bool existed = std::filesystem::exists(output);
    require(replace_existing || !existed, "Output already exists: " + output.string());
    auto output_time =
        existed ? std::filesystem::last_write_time(output) : std::filesystem::file_time_type{};
    auto output_size = existed ? std::filesystem::file_size(output) : 0;
    require(std::filesystem::last_write_time(path_) == modified_ &&
                std::filesystem::file_size(path_) == file_size_,
            "Source archive changed; retry the write");
    auto entries = entries_;
    if (append)
        extend(entries, replacements);
    for (const auto &[index, bytes] : replacements) {
        require(index.first < entries.size() && entries[index.first].contains(index.second),
                "Replacement member or subfile is out of range");
    }
    if (capture_archive(output, {path_, overrides_}, replacements, replace_existing))
        return;
    auto image = image_header_;
    auto prefix = entries.size() == size() ? prefix_ : metadata(prefix_, entries, image);
    const auto data_offset = prefix.size();
    std::size_t end = 0;
    put32(prefix, 24, 0);
    put32(prefix, 28, 0);
    for (std::size_t i = 0; i < entries.size(); ++i)
        for (auto &[sub, e] : entries[i]) {
            auto found = replacements.find({i, sub});
            auto count = found == replacements.end()
                             ? (overrides_.contains({i, sub})
                                    ? std::filesystem::file_size(overrides_.at({i, sub}))
                                    : e.size)
                             : found->second.size();
            auto padded = aligned(count, alignment_);
            put32(prefix, e.record + 4, narrow(end));
            put32(prefix, e.record + 8, narrow(end + padded));
            put32(prefix, e.record + 12, narrow(count));
            end += padded;
            put32(prefix, 24, std::max(u32(prefix, 24), narrow(padded)));
            put32(prefix, 28, std::max(u32(prefix, 28), narrow(count)));
        }
    put32(prefix, 20, narrow(data_offset + end));
    put32(prefix, image + 8, narrow(end));
    auto temp = output;
    temp += ".tmp";
    require(!std::filesystem::exists(temp), "Temporary output already exists: " + temp.string());
    if (!output.parent_path().empty())
        std::filesystem::create_directories(output.parent_path());
    try {
        std::ifstream source(path_, std::ios::binary);
        require(bool(source), "Cannot reopen source archive");
        std::ofstream f(temp, std::ios::binary);
        require(bool(f), "Cannot create archive output");
        auto write = [&](View b) {
            f.write(reinterpret_cast<const char *>(b.data()),
                    static_cast<std::streamsize>(b.size()));
            require(bool(f), "Archive write failed");
        };
        write(prefix);
        for (std::size_t i = 0; i < entries.size(); ++i)
            for (auto &[sub, e] : entries[i]) {
                auto found = replacements.find({i, sub});
                Bytes b = found == replacements.end() ? raw(i, sub) : found->second;
                b.resize(aligned(b.size(), alignment_), 0xff);
                write(b);
            }
        f.close();
        require(bool(f), "Archive flush failed");
        Archive check(temp);
        require(check.size() == entries.size(), "Export member count changed");
        std::ifstream result(temp, std::ios::binary);
        require(bool(result), "Cannot reopen exported archive");
        for (std::size_t i = 0; i < entries.size(); ++i)
            for (auto &[sub, e] : entries[i]) {
                auto found = replacements.find({i, sub});
                const auto &output_entry = check.entries_[i].at(sub);
                auto expected = found == replacements.end() ? raw(i, sub) : found->second;
                auto actual =
                    read_stream(result, std::size_t(check.data_offset_) + output_entry.start,
                                output_entry.size);
                require(actual == expected,
                        "Archive readback mismatch at member " + std::to_string(i));
            }
        result.close();
        source.close();
        require(std::filesystem::last_write_time(path_) == modified_ &&
                    std::filesystem::file_size(path_) == file_size_,
                "Source archive changed while writing; retry");
        require(std::filesystem::exists(output) == existed &&
                    (!existed || (std::filesystem::last_write_time(output) == output_time &&
                                  std::filesystem::file_size(output) == output_size)),
                "Destination changed while writing; retry");
        if (replace_existing)
            replace_file(temp, output);
        else
            std::filesystem::rename(temp, output);
    } catch (...) {
        std::error_code error;
        std::filesystem::remove(temp, error);
        throw;
    }
}
}
