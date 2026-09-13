#pragma once
#include "compiler/attachment.h"
#include "compiler/motion.h"
namespace studio {
struct AssetMaterial {
    std::string name;
    TextureImage image;
};
struct AssetVertex {
    std::array<float, 3> position, normal;
    std::array<float, 2> uv;
    std::array<std::uint8_t, 4> joints, weights;
};
struct AssetMesh {
    std::string name;
    std::uint32_t material;
    std::vector<std::uint8_t> palette;
    std::vector<AssetVertex> vertices;
    std::vector<std::uint16_t> indices;
};
struct AssetDocument {
    std::string name;
    std::vector<Joint> joints;
    std::vector<AssetMaterial> materials;
    std::vector<AssetMesh> meshes;
    std::vector<JointMotion> tracks;
    static AssetDocument read(View bytes);
};
GeneratedSkin compile_asset(const ModelPack &donor, const AssetDocument &asset);
struct SkeletonMotion {
    std::uint16_t frames = 0;
    std::vector<JointMotion> tracks;
    Bytes write() const;
    static SkeletonMotion read(View bytes);
};
}
