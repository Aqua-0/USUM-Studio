#include "assets/material_document.h"
#include <algorithm>
#include "core/digest.h"
namespace studio {
void MaterialDocument::preview_refresh(std::size_t index, View ids) {
    preview_refresh_batch({{index, Bytes(ids.begin(), ids.end())}});
}
void MaterialDocument::preview_refresh_batch(std::map<std::size_t, Bytes> masks) {
    for (auto &[index, ids] : masks) {
        require(initial_refresh_ && index < initial_refresh_->masks.size(),
                "Refresh mask is unavailable");
        auto &mask = initial_refresh_->masks[index];
        require(mask.read_only_reason.empty(), mask.read_only_reason);
        require(ids.size() == mask.ids.size(), "Refresh mask dimensions cannot change");
        for (std::size_t i = 0; i < ids.size(); ++i)
            require(ids[i] == mask.ids[i] || known_refresh_region(ids[i]),
                    "Cannot introduce an unknown reaction category");
    }
    bool changed = false;
    for (auto &[index, ids] : masks) {
        auto &original = initial_refresh_->masks[index].ids;
        auto existing = refresh_edits_.find(index);
        auto &current = existing == refresh_edits_.end() ? original : existing->second;
        if (ids == current)
            continue;
        if (ids == original)
            refresh_edits_.erase(index);
        else
            refresh_edits_[index] = std::move(ids);
        touched_refresh_.insert(index);
        changed = true;
    }
    if (changed) {
        synchronize_refresh();
        ++revision_;
    }
}
void MaterialDocument::reset_refresh(std::size_t mask) {
    require(initial_refresh_ && mask < initial_refresh_->masks.size(),
            "Refresh mask is unavailable");
    preview_refresh(mask, initial_refresh_->masks[mask].ids);
    commit();
}
void MaterialDocument::synchronize_refresh() {
    if (!initial_refresh_)
        return;
    bool changed = false;
    for (std::size_t i = 0; i < initial_refresh_->masks.size(); ++i) {
        auto it = refresh_edits_.find(i);
        auto &ids = it == refresh_edits_.end() ? initial_refresh_->masks[i].ids : it->second;
        if (model.refresh_regions->masks[i].ids != ids) {
            changed = true;
            break;
        }
    }
    if (!changed)
        return;
    auto next = std::make_shared<RefreshRegionPack>(*initial_refresh_);
    for (auto &[index, ids] : refresh_edits_) {
        auto &mask = next->masks.at(index);
        mask.ids = ids;
        mask.counts = {};
        for (auto id : ids)
            ++mask.counts[id];
    }
    model.refresh_regions = std::move(next);
}
Bytes MaterialDocument::merge_refresh(std::size_t index, View destination) const {
    auto &link = model.resources.at(model.refresh_resources.at(index));
    auto &original = initial_refresh_->masks.at(index);
    require(link.path == std::vector<std::size_t>{original.child},
            "Unsupported Refresh resource path");
    auto target = decode_refresh_regions(destination);
    auto it = std::find_if(target.masks.begin(), target.masks.end(), [&](auto &m) {
        return m.child == original.child;
    });
    require(it != target.masks.end() && it->texture == original.texture &&
                it->width == original.width && it->height == original.height &&
                it->format == original.format,
            "Destination Refresh mask differs from this asset");
    auto desired = refresh_edits_.find(index);
    auto &ids = desired == refresh_edits_.end() ? original.ids : desired->second;
    require(
        it->ids == original.ids || it->ids == ids ||
            (accepted_refresh_.contains(index) &&
             accepted_refresh_.at(index).contains(sha256(it->ids))),
        "Destination Refresh mask has other edits; reload that version or use a fresh override");
    return replace_refresh_region_pixels(destination, original.child, ids);
}
}
