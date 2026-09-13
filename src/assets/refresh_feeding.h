#pragma once
#include "core/binary.h"
#include <array>
#include <optional>
namespace studio {
struct RefreshFeedingParameters {
    int distance = 130, scale_percent = 100, head_angle = 0, animation_count = 5;
    bool operator==(const RefreshFeedingParameters &) const = default;
};
struct RefreshCamera {
    std::array<int, 3> position{0, 35, 105}, focus{0, 27, 0};
    bool operator==(const RefreshCamera &) const = default;
};
using RefreshFeedingCamera = RefreshCamera;
struct RefreshFeedingData {
    Bytes original;
    std::size_t row = 0;
    unsigned species = 0, form = 0, sex = 0;
    RefreshFeedingParameters parameters;
    RefreshCamera camera;
    std::array<RefreshCamera, 10> cameras;
    Bytes camera_original;
    std::size_t camera_row = 0;
    bool camera_default = true;
    bool available = false;
};
std::optional<std::size_t> refresh_parameter_row(View table, unsigned record_size, unsigned species,
                                                 unsigned form, unsigned sex);
View refresh_feeding_record(View table, std::size_t row, unsigned size = 32);
RefreshCamera refresh_camera(View table, std::size_t row, unsigned slot);
Bytes replace_refresh_camera(View table, std::size_t row, unsigned slot,
                             const RefreshCamera &camera);
RefreshFeedingCamera refresh_feeding_camera(View table, std::size_t row);
void validate_refresh_feeding_camera(const RefreshFeedingCamera &camera);
Bytes replace_refresh_feeding_camera(View table, std::size_t row,
                                     const RefreshFeedingCamera &camera);
RefreshFeedingParameters refresh_feeding_parameters(View table, std::size_t row);
void validate_refresh_feeding(const RefreshFeedingParameters &parameters);
Bytes replace_refresh_feeding(View table, std::size_t row,
                              const RefreshFeedingParameters &parameters);
RefreshFeedingData decode_refresh_feeding(View options, View cameras, unsigned species,
                                          unsigned form, unsigned sex);
}
