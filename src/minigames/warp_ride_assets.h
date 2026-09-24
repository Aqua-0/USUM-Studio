#pragma once
#include "scene/environment.h"
#include "minigames/warp_ride_profile.h"
namespace studio {
struct RideAssetGeometry {
    std::size_t first = 0, count = 0;
    std::array<float, 3> low{}, high{};
};
struct RideAssets {
    std::shared_ptr<Environment> scene;
    std::array<std::array<float, 3>, 4> rider_origins{};
    std::array<RideAssetGeometry, WarpRideProfile::assets.size()> geometry;
};
RideAssets load_ride_assets(const std::filesystem::path &dump, std::atomic_bool *cancel = nullptr);
}
