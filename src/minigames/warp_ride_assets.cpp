#include "minigames/warp_ride_assets.h"
#include "scene/model_decoder.h"
#include <limits>
namespace studio {
RideAssets load_ride_assets(const std::filesystem::path &dump, std::atomic_bool *cancel) {
    Archive archive(dump / WarpRideProfile::course_archive);
    require(archive.size() == 42,
            "This dump does not have the expected Ultra Warp Ride course resources");
    ModelDecoder decoder;
    decoder.cancel = cancel;
    decoder.keep_skeleton = true;
    RideAssets result;
    for (std::size_t i = 0; i < WarpRideProfile::assets.size(); ++i) {
        decoder.checkpoint();
        const auto &asset = WarpRideProfile::assets[i];
        auto &geometry = result.geometry[i];
        geometry.first = decoder.out.draws.size();
        std::string prefix = "ride/" + std::to_string(i) + "/";
        auto motion =
            i < 13 ? archive.decoded(asset.loop)
                   : Archive(dump / WarpRideProfile::ride_motions_archive).decoded(asset.loop);
        if (i < 15) {
            auto bytes =
                i < 13
                    ? archive.decoded(asset.model)
                    : Container::parse(
                          Archive(dump / WarpRideProfile::mount_archive).decoded(asset.model), "CM")
                          .files.at(0);
            auto pack = ModelPack::parse(bytes);
            for (const auto &resource : pack.resources) {
                if (resource.category == 1)
                    decoder.texture(resource.bytes, prefix);
                if (resource.category == 4)
                    decoder.shader(resource.bytes, prefix);
            }
            decoder.motion(motion, asset.name, prefix, false);
            for (const auto &resource : pack.resources)
                if (resource.category == 0)
                    decoder.placed_model(resource.bytes, resource.name, prefix);
        } else {
            auto player = load_player_assets(dump, unsigned(asset.model));
            const int body = int(decoder.out.skeletons.size());
            for (auto &part : player.parts) {
                auto scope = prefix + part.name + "/";
                decoder.skeletal_motions[scope] = {
                    part.attachment.empty() ? decode_skeletal_motion(motion) : part.motions[0],
                    false};
                decoder.skeletal_motions[scope].first.looping = true;
                auto pack = ModelPack::parse(part.model);
                for (const auto &resource : pack.resources)
                    if (resource.category == 4)
                        decoder.shader(resource.bytes, scope);
                for (auto &[name, image] : part.textures)
                    decoder.out.textures.emplace(scope + name, std::move(image));
                auto first = decoder.out.skeletons.size();
                for (const auto &resource : pack.resources)
                    if (resource.category == 0)
                        decoder.placed_model(resource.bytes, part.name, scope);
                require(decoder.out.skeletons.size() > first, "Rider outfit has no skeleton");
                for (auto rig = first; rig < decoder.out.skeletons.size(); ++rig)
                    if (!part.attachment.empty()) {
                        auto &joints = decoder.out.skeletons[body].joints;
                        auto joint = std::find_if(joints.begin(), joints.end(), [&](const auto &j) {
                            return j.name == part.attachment;
                        });
                        require(joint != joints.end(), "Rider outfit attachment is missing");
                        decoder.out.skeletons[rig].parent_skeleton = body;
                        decoder.out.skeletons[rig].parent_joint = int(joint - joints.begin());
                    }
            }
            std::erase_if(decoder.out.draws, [&](const auto &draw) {
                return draw.scope.starts_with(prefix) &&
                       draw.mesh.find("nohat") != std::string::npos;
            });
            const auto &rig = decoder.out.skeletons[body];
            auto joint = std::find_if(rig.joints.begin(), rig.joints.end(), [](const auto &j) {
                return j.name == "Spine2";
            });
            require(joint != rig.joints.end(), "Rider reference joint is missing");
            auto pose = evaluate_skeleton(rig, 0, 12, true);
            auto anchor =
                pose_multiply(pose.at(std::size_t(joint - rig.joints.begin())), joint->bind);
            result.rider_origins[i - 15] = {anchor[3], anchor[7], anchor[11]};
        }
        geometry.count = decoder.out.draws.size() - geometry.first;
        require(geometry.count != 0, std::string(asset.name) + " has no renderable meshes");
        geometry.low.fill(std::numeric_limits<float>::max());
        geometry.high.fill(std::numeric_limits<float>::lowest());
        for (auto d = geometry.first; d < geometry.first + geometry.count; ++d) {
            const auto &draw = decoder.out.draws[d];
            const unsigned samples = draw.skeleton >= 0 ? 16 : 1;
            for (unsigned sample = 0; sample < samples; ++sample) {
                std::vector<Matrix> pose;
                if (draw.skeleton >= 0) {
                    const auto &rig = decoder.out.skeletons.at(std::size_t(draw.skeleton));
                    pose = evaluate_skeleton(
                        rig, double(rig.motion.frames) * sample / (samples * 30), 12, true);
                }
                for (const auto &v : draw.vertices) {
                    std::array<float, 3> position{v.x, v.y, v.z};
                    if (!pose.empty()) {
                        position = {};
                        for (unsigned influence = 0; influence < 4; ++influence)
                            if (v.weights[influence] > 0) {
                                const auto &matrix =
                                    pose.at(draw.palette.at(unsigned(v.joints[influence])));
                                for (unsigned axis = 0; axis < 3; ++axis)
                                    position[axis] +=
                                        v.weights[influence] *
                                        (matrix[axis * 4] * v.x + matrix[axis * 4 + 1] * v.y +
                                         matrix[axis * 4 + 2] * v.z + matrix[axis * 4 + 3]);
                            }
                    }
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        geometry.low[axis] = std::min(geometry.low[axis], position[axis]);
                        geometry.high[axis] = std::max(geometry.high[axis], position[axis]);
                    }
                }
            }
        }
    }
    for (auto &animation : decoder.out.material_animations)
        for (std::size_t t = 0; t < animation.motion.tracks.size(); ++t)
            for (std::size_t m = 0; m < decoder.out.materials.size(); ++m)
                if (decoder.out.materials[m].resource_scope == animation.texture_prefix &&
                    decoder.out.materials[m].name == animation.motion.tracks[t].material)
                    animation.bindings.push_back({t, m});
    result.scene = std::make_shared<Environment>(std::move(decoder.out));
    return result;
}
}
