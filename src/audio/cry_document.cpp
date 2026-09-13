#include "audio/cry_document.h"
#include "core/digest.h"
#include <algorithm>
namespace studio {
Bytes CryDocument::current(unsigned member) const {
    auto i = edits_.find(member);
    return i == edits_.end() ? library.archive.decoded(member) : i->second.bytes;
}
void CryDocument::replace(unsigned member, Bytes bytes) {
    decode_cry(bytes);
    require(bytes.size() <= CryProfile::wave_limit, "Replacement exceeds the game's cry buffer");
    auto original = library.archive.decoded(member);
    decode_cry(original);
    if (bytes == current(member))
        return;
    undo_.push_back(edits_);
    redo_.clear();
    if (bytes == original)
        edits_.erase(member);
    else
        edits_[member] = {sha256(original), std::move(bytes)};
}
void CryDocument::reset(unsigned member) {
    if (!edits_.contains(member))
        return;
    undo_.push_back(edits_);
    redo_.clear();
    edits_.erase(member);
}
bool CryDocument::undo() {
    if (undo_.empty())
        return false;
    redo_.push_back(edits_);
    edits_ = std::move(undo_.back());
    undo_.pop_back();
    return true;
}
bool CryDocument::redo() {
    if (redo_.empty())
        return false;
    undo_.push_back(edits_);
    edits_ = std::move(redo_.back());
    redo_.pop_back();
    return true;
}
void CryDocument::discard() {
    edits_ = saved_;
    undo_.clear();
    redo_.clear();
}
void CryDocument::save(const std::filesystem::path &file) {
    Bytes out{'U', 'S', 'U', 'M', 'C', 'R', 'Y', 0};
    append32(out, 1);
    append32(out, narrow(edits_.size()));
    for (auto &[member, edit] : edits_) {
        append32(out, member);
        append32(out, narrow(edit.bytes.size()));
        out.insert(out.end(), edit.original_hash.begin(), edit.original_hash.end());
        append(out, edit.bytes);
    }
    write_file_atomic(file, out);
    saved_ = edits_;
}
void CryDocument::load(const std::filesystem::path &file) {
    auto b = read_file(file);
    require(text(slice(b, 0, 7)) == "USUMCRY" && u32(b, 8) == 1, "Unsupported cry project");
    auto n = u32(b, 12);
    require(n <= library.archive.size(), "Invalid cry project member count");
    State edits;
    std::size_t p = 16;
    for (unsigned i = 0; i < n; ++i) {
        auto member = u32(b, p), size = u32(b, p + 4);
        auto hash = text(slice(b, p + 8, 64));
        require(member < library.archive.size() && size <= CryProfile::wave_limit &&
                    !edits.contains(member),
                "Invalid cry replacement entry");
        auto bytes = slice(b, p + 72, size);
        decode_cry(bytes);
        auto original = library.archive.decoded(member);
        decode_cry(original);
        require(sha256(original) == hash, "Cry project source differs from the loaded dump");
        edits[member] = {hash, Bytes(bytes.begin(), bytes.end())};
        p += 72 + size;
    }
    require(p == b.size(), "Unexpected cry project trailing data");
    edits_ = saved_ = std::move(edits);
    undo_.clear();
    redo_.clear();
}
void CryDocument::export_to(const std::filesystem::path &folder) const {
    require(!edits_.empty(), "No cry replacements to export");
    auto source = std::filesystem::weakly_canonical(dump),
         destination = std::filesystem::weakly_canonical(folder);
    for (auto parent = destination; !parent.empty();) {
        if (std::filesystem::exists(parent))
            require(!std::filesystem::equivalent(parent, source),
                    "Choose an export folder outside the source dump");
        auto next = parent.parent_path();
        if (next == parent)
            break;
        parent = next;
    }
    std::map<std::size_t, Bytes> changes;
    for (auto &[member, edit] : edits_) {
        require(sha256(library.archive.decoded(member)) == edit.original_hash,
                "Source cry changed; reopen the project against the correct dump");
        changes[member] = edit.bytes;
    }
    auto path = folder / CryProfile::archive;
    require(!std::filesystem::exists(path),
            "An archive already exists here. Choose a fresh export folder");
    std::filesystem::create_directories(path.parent_path());
    library.archive.export_to(path, changes);
}
}
