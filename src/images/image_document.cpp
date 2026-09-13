#include "core/game_profile.h"
#include "core/resource_source.h"
#include "images/image_document.h"
#include "images/image_profile.h"
#include "formats/compression.h"
#include "core/digest.h"
#include <algorithm>
#include <set>
namespace studio {
namespace {
Bytes unpack(View raw) {
    return is_native_image(raw) ? Bytes(raw.begin(), raw.end()) : decompress(raw);
}
void string_out(Bytes &b, const std::string &s) {
    append32(b, narrow(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}
std::string string_in(View b, std::size_t &p) {
    auto size = u32(b, p);
    p += 4;
    require(size <= 4096, "Project string is too long");
    auto s = text(slice(b, p, size));
    p += size;
    return s;
}
Bytes package(const ImageCatalog &catalog, const ImageSource &image) {
    Archive archive(catalog.dump / image.archive);
    auto bytes = unpack(archive.raw(image.member, image.subfile));
    require(sha256(bytes) == image.package_hash,
            "Image source changed; rescan or use the original dump");
    return bytes;
}
}
std::string ImageSource::key() const {
    return archive + ":" + std::to_string(member) + ":" + std::to_string(subfile) + ":" +
           std::to_string(offset) + ":" + name;
}
ImageCatalog scan_image_catalog(const std::filesystem::path &dump,
                                const std::function<bool(unsigned, unsigned)> &progress) {
    require(std::filesystem::is_directory(resource_source(dump / "romfs/a").original),
            "Choose an extracted game dump containing romfs/a");
    ImageCatalog result;
    result.dump = dump;
    unsigned done = 0;
    for (auto &profile : image_archives) {
        if (progress && !progress(done, unsigned(std::size(image_archives))))
            throw std::runtime_error("Image scan cancelled");
        ++done;
        const auto archive_path = GameProfile::image_archive(dump, profile.path).generic_string();
        if (!resource_exists(dump / archive_path))
            continue;
        try {
            Archive archive(dump / archive_path);
            for (unsigned member = 0; member < archive.size(); ++member)
                for (auto sub : archive.subfiles(member)) {
                    if (progress && !progress(done - 1, unsigned(std::size(image_archives))))
                        throw std::runtime_error("Image scan cancelled");
                    try {
                        auto raw = archive.raw(member, sub);
                        if (raw.size() > 16 * 1024 * 1024)
                            continue;
                        auto b = unpack(raw);
                        auto files = embedded_images(b);
                        if (files.empty())
                            continue;
                        auto hash = sha256(b);
                        for (auto &file : files)
                            try {
                                auto info = native_image_info(slice(b, file.offset, file.size));
                                result.images.push_back(
                                    {archive_path, profile.category,
                                     file.name.empty() ? std::string(profile.category) +
                                                             " / image " + std::to_string(member)
                                                       : file.name,
                                     hash, member, sub, file.offset, file.size, info});
                            } catch (const std::exception &e) {
                                result.diagnostics.push_back(archive_path + " / " + file.name +
                                                             ": " + e.what());
                            }
                    } catch (const std::exception &e) {
                        result.diagnostics.push_back(archive_path + " / " + std::to_string(member) +
                                                     " / " + std::to_string(sub) + ": " + e.what());
                    }
                }
        } catch (const std::exception &e) {
            result.diagnostics.push_back(archive_path + ": " + e.what());
        }
    }
    if (progress)
        progress(done, done);
    return result;
}
Bytes ImageDocument::original(std::size_t index) const {
    require(index < catalog.images.size(), "Image selection is out of range");
    auto &image = catalog.images[index];
    auto b = package(catalog, image);
    auto data = slice(b, image.offset, image.size);
    return {data.begin(), data.end()};
}
Bytes ImageDocument::current(std::size_t index) const {
    auto e = edits_.find(index);
    return e == edits_.end() ? original(index) : e->second;
}
bool ImageDocument::edited(std::size_t index) const {
    return edits_.contains(index);
}
void ImageDocument::replace(std::size_t index, const TextureImage &image) {
    replace_encoded(index, replace_native_image(current(index), image));
}
void ImageDocument::replace_encoded(std::size_t index, Bytes encoded) {
    auto source = original(index);
    validate_image_replacement(source, encoded);
    if (encoded == current(index))
        return;
    undo_.push_back(edits_);
    redo_.clear();
    if (encoded == source)
        edits_.erase(index);
    else
        edits_[index] = std::move(encoded);
}
void ImageDocument::reset(std::size_t index) {
    if (!edited(index))
        return;
    undo_.push_back(edits_);
    redo_.clear();
    edits_.erase(index);
}
bool ImageDocument::undo() {
    if (undo_.empty())
        return false;
    redo_.push_back(edits_);
    edits_ = std::move(undo_.back());
    undo_.pop_back();
    return true;
}
bool ImageDocument::redo() {
    if (redo_.empty())
        return false;
    undo_.push_back(edits_);
    edits_ = std::move(redo_.back());
    redo_.pop_back();
    return true;
}
void ImageDocument::discard() {
    edits_ = saved_;
    undo_.clear();
    redo_.clear();
}
void ImageDocument::save(const std::filesystem::path &file) {
    Bytes out{'U', 'S', 'U', 'M', 'I', 'M', 'G', 0};
    append32(out, 1);
    append32(out, narrow(edits_.size()));
    for (auto &[index, bytes] : edits_) {
        auto &image = catalog.images.at(index);
        string_out(out, image.key());
        string_out(out, image.package_hash);
        append32(out, narrow(bytes.size()));
        append(out, bytes);
    }
    write_file_atomic(file, out);
    saved_ = edits_;
}
void ImageDocument::load(const std::filesystem::path &file) {
    auto b = read_file(file);
    require(text(slice(b, 0, 7)) == "USUMIMG" && b[7] == 0 && u32(b, 8) == 1,
            "Unsupported image project");
    auto count = u32(b, 12);
    require(count <= catalog.images.size(), "Invalid image project count");
    std::map<std::string, std::size_t> keys;
    for (std::size_t n = 0; n < catalog.images.size(); ++n)
        keys[catalog.images[n].key()] = n;
    State state;
    std::size_t p = 16;
    for (unsigned n = 0; n < count; ++n) {
        auto key = string_in(b, p), hash = string_in(b, p);
        require(keys.contains(key), "Project image is not available in this dump");
        auto index = keys.at(key);
        require(!state.contains(index) && catalog.images[index].package_hash == hash,
                "Project source differs from the scanned dump");
        auto size = u32(b, p);
        p += 4;
        auto bytes = slice(b, p, size);
        p += size;
        auto source = original(index);
        require(source.size() == bytes.size(), "Replacement image size changed");
        validate_image_replacement(source, bytes);
        state[index] = {bytes.begin(), bytes.end()};
    }
    require(p == b.size(), "Unexpected image project trailing data");
    edits_ = saved_ = std::move(state);
    undo_.clear();
    redo_.clear();
}
void ImageDocument::export_to(const std::filesystem::path &folder) const {
    require(!edits_.empty(), "No image replacements to export");
    auto source = std::filesystem::weakly_canonical(catalog.dump);
    for (auto p = std::filesystem::weakly_canonical(folder); !p.empty();) {
        require(p != source, "Choose an export folder outside the source dump");
        auto next = p.parent_path();
        if (next == p)
            break;
        p = next;
    }
    using Member = std::pair<std::size_t, unsigned>;
    std::map<std::string, std::map<Member, Bytes>> archives;
    for (auto &[index, bytes] : edits_) {
        auto &image = catalog.images.at(index);
        auto &members = archives[image.archive];
        Member key{image.member, image.subfile};
        if (!members.contains(key))
            members[key] = package(catalog, image);
        auto &b = members.at(key);
        require(bytes.size() == image.size, "Replacement image extent changed");
        std::copy(bytes.begin(), bytes.end(), b.begin() + std::ptrdiff_t(image.offset));
    }
    for (auto &[path, members] : archives) {
        (void)members;
        require(!std::filesystem::exists(folder / path),
                "An archive already exists here. Choose a fresh export folder");
    }
    std::vector<std::filesystem::path> written;
    try {
        for (auto &[path, members] : archives) {
            Archive archive(catalog.dump / path);
            for (auto &[member, b] : members) {
                auto raw = archive.raw(member.first, member.second);
                auto source_image = std::find_if(
                    catalog.images.begin(), catalog.images.end(), [&](const ImageSource &image) {
                        return image.archive == path && image.member == member.first &&
                               image.subfile == member.second;
                    });
                require(source_image != catalog.images.end() &&
                            sha256(unpack(raw)) == source_image->package_hash,
                        "Image source changed while preparing export");
                if (!is_native_image(raw) && !raw.empty() && raw[0] == 0x11)
                    b = compress(b);
            }
            archive.export_subfiles(folder / path, members);
            written.push_back(folder / path);
        }
    } catch (...) {
        for (auto &path : written) {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
        throw;
    }
}
}
