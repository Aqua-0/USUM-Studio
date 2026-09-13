#include "assets/skeleton_edit.h"
#include <cmath>
#include <algorithm>
#include <set>
namespace studio {
Bytes replace_skeleton(View original, const std::vector<Joint> &joints) {
    auto before = SkinnedModel::parse(original);
    if (before.joints.empty()) {
        require(joints.empty(),
                "Adding skinning to a static model requires a different shader layout");
        return Bytes(original.begin(), original.end());
    }
    require(joints.size() >= before.joints.size() && joints.size() <= 255,
            "Keep existing bones; this model supports at most 255 bones");
    std::set<std::string> names;
    bool changed = joints.size() != before.joints.size();
    for (std::size_t i = 0; i < joints.size(); ++i) {
        auto &j = joints[i];
        require(!j.name.empty() && j.name.size() < 64 && j.name.find('\0') == std::string::npos &&
                    names.insert(j.name).second,
                "Bone names must be unique and contain 1 to 63 bytes");
        require(j.parent >= -1 && j.parent < int(i),
                "Choose a parent earlier in the skeleton to preserve native bind ordering");
        for (unsigned k = 0; k < 3; ++k)
            require(j.scale[k] == 1 && std::isfinite(j.rotation[k]) &&
                        std::isfinite(j.translation[k]),
                    "Bind transforms need finite values and unit scale");
        require((j.flags & 0xfc) == 0, "Unsupported bone flags");
        if (i < before.joints.size()) {
            auto &old = before.joints[i];
            require(j.name == old.name && j.flags == old.flags,
                    "Existing bone names and flags must be preserved");
            changed |= j.parent != old.parent || j.rotation != old.rotation ||
                       j.translation != old.translation;
        }
    }
    for (std::size_t i = 0; i < joints.size(); ++i) {
        std::set<int> chain;
        for (int p = int(i); p >= 0; p = joints[p].parent)
            require(chain.insert(p).second, "Bone parenting would create a cycle");
    }
    require(std::count_if(joints.begin(), joints.end(),
                          [](auto &j) {
                              return j.parent < 0;
                          }) == 1,
            "Keep exactly one root bone");
    if (!changed)
        return Bytes(original.begin(), original.end());
    auto header = before.model.bounds_offset + 96;
    header += 16 + std::size_t(u32(original, header)) + u32(original, header + 4);
    auto end = before.joints.back().transform_offset + 36;
    auto metadata_end = before.model.sections.front().offset + before.model.sections.front().size;
    Bytes out(original.begin(), original.begin() + header + 16);
    put32(out, header, narrow(joints.size()));
    auto name = [&](const std::string &value) {
        out.push_back(std::uint8_t(value.size()));
        out.insert(out.end(), value.begin(), value.end());
    };
    for (auto &j : joints) {
        name(j.name);
        name(j.parent < 0 ? std::string{} : joints[j.parent].name);
        out.push_back(j.flags);
        for (auto values : {j.scale, j.rotation, j.translation})
            for (float value : values) {
                auto p = out.size();
                out.resize(p + 4);
                put_float(out, p, value);
            }
    }
    end = aligned(end, 16);
    require(end <= metadata_end, "Skeleton extends beyond model metadata");
    out.resize(aligned(out.size(), 16));
    append(out, slice(original, end, metadata_end - end));
    put32(out, 24, narrow(out.size() - 32));
    append(out, slice(original, metadata_end, original.size() - metadata_end));
    auto check = SkinnedModel::parse(out);
    require(check.joints.size() == joints.size(), "Skeleton did not round-trip");
    for (std::size_t i = 0; i < joints.size(); ++i)
        require(check.joints[i].name == joints[i].name &&
                    check.joints[i].parent == joints[i].parent &&
                    check.joints[i].translation == joints[i].translation &&
                    check.joints[i].rotation == joints[i].rotation,
                "Bind transform did not round-trip");
    return out;
}
}
