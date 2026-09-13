#include "assets/refresh_feeding.h"
namespace studio {
namespace {
View record(View table, std::size_t row, unsigned size) {
    auto count = u32(table, 0);
    require(count <= (table.size() - 4) / 4, "Invalid Refresh parameter index");
    require(row < count, "Refresh parameter row is missing");
    auto offset = u32(table, 4 + row * 4);
    require(offset >= 4 + std::size_t(count) * 4, "Refresh parameter overlaps its index");
    return slice(table, offset, size);
}
int signed16(View bytes, std::size_t offset) {
    return std::int16_t(u16(bytes, offset));
}
}
std::optional<std::size_t> refresh_parameter_row(View table, unsigned size, unsigned species,
                                                 unsigned form, unsigned sex) {
    require(size >= 4, "Invalid Refresh record size");
    auto count = u32(table, 0);
    require(count <= (table.size() - 4) / 4, "Invalid Refresh parameter index");
    std::optional<std::size_t> result;
    unsigned previous = 0;
    for (std::size_t i = 0; i < count; ++i) {
        auto row = record(table, i, size);
        auto id = u16(row, 0);
        require(id >= previous, "Refresh species rows are not sorted");
        previous = id;
        if (id == species && (!result || (row[2] == form && (row[3] == sex || row[3] == 0))))
            result = i;
    }
    return result;
}
View refresh_feeding_record(View table, std::size_t row, unsigned size) {
    return record(table, row, size);
}
RefreshCamera refresh_camera(View table, std::size_t row, unsigned slot) {
    require(slot < 10, "Invalid Refresh camera slot");
    auto bytes = record(table, row, 126);
    RefreshCamera camera;
    for (unsigned k = 0; k < 3; ++k) {
        camera.position[k] = signed16(bytes, 6 + slot * 12 + k * 2);
        camera.focus[k] = signed16(bytes, 12 + slot * 12 + k * 2);
    }
    return camera;
}
RefreshFeedingCamera refresh_feeding_camera(View table, std::size_t row) {
    return refresh_camera(table, row, 9);
}
void validate_refresh_feeding_camera(const RefreshFeedingCamera &camera) {
    for (auto &point : {camera.position, camera.focus})
        for (int v : point)
            require(v >= -32768 && v <= 32767,
                    "Feeding camera coordinates must fit signed 16-bit values");
    require(camera.position != camera.focus, "Camera position and look-at must be different");
}
Bytes replace_refresh_camera(View table, std::size_t row, unsigned slot,
                             const RefreshCamera &camera) {
    require(slot < 10, "Invalid Refresh camera slot");
    validate_refresh_feeding_camera(camera);
    record(table, row, 126);
    auto offset = u32(table, 4 + row * 4);
    Bytes bytes(table.begin(), table.end());
    for (unsigned k = 0; k < 3; ++k) {
        put16(bytes, offset + 6 + slot * 12 + k * 2, std::uint16_t(camera.position[k]));
        put16(bytes, offset + 12 + slot * 12 + k * 2, std::uint16_t(camera.focus[k]));
    }
    return bytes;
}
RefreshFeedingParameters refresh_feeding_parameters(View table, std::size_t row) {
    auto bytes = record(table, row, 32);
    return {signed16(bytes, 28), int(u16(bytes, 30)), signed16(bytes, 6), int(bytes[12])};
}
void validate_refresh_feeding(const RefreshFeedingParameters &p) {
    require(p.distance >= -32768 && p.distance <= 32767,
            "Food distance must fit a signed 16-bit value");
    require(p.scale_percent >= 0 && p.scale_percent <= 65535,
            "Food scale must be between 0 and 65535 hundredths");
    require(p.head_angle >= -32768 && p.head_angle <= 32767,
            "Eating head angle must fit a signed 16-bit value");
    require(p.animation_count >= 1 && p.animation_count <= 255,
            "Eating loop count must be between 1 and 255");
}
Bytes replace_refresh_feeding(View table, std::size_t row, const RefreshFeedingParameters &p) {
    validate_refresh_feeding(p);
    record(table, row, 32);
    auto offset = u32(table, 4 + row * 4);
    Bytes bytes(table.begin(), table.end());
    put16(bytes, offset + 6, std::uint16_t(p.head_angle));
    bytes[offset + 12] = std::uint8_t(p.animation_count);
    put16(bytes, offset + 28, std::uint16_t(p.distance));
    put16(bytes, offset + 30, std::uint16_t(p.scale_percent));
    return bytes;
}
Bytes replace_refresh_feeding_camera(View table, std::size_t row,
                                     const RefreshFeedingCamera &camera) {
    return replace_refresh_camera(table, row, 9, camera);
}
RefreshFeedingData decode_refresh_feeding(View options, View cameras, unsigned species,
                                          unsigned form, unsigned sex) {
    RefreshFeedingData data;
    data.cameras[1] = {{0, 36, 144}, {0, 24, 0}};
    data.cameras[2] = {{0, 23, 105}, {0, 24, 0}};
    data.cameras[9] = data.camera;
    data.original.assign(options.begin(), options.end());
    if (auto row = refresh_parameter_row(options, 32, species, form, sex)) {
        data.available = true;
        data.row = *row;
        auto bytes = record(options, *row, 32);
        data.species = u16(bytes, 0);
        data.form = bytes[2];
        data.sex = bytes[3];
        data.parameters = refresh_feeding_parameters(options, *row);
    }
    if (!cameras.empty())
        if (auto row = refresh_parameter_row(cameras, 126, species, form, sex)) {
            data.camera_original.assign(cameras.begin(), cameras.end());
            data.camera_row = *row;
            for (unsigned slot = 0; slot < 10; ++slot)
                data.cameras[slot] = refresh_camera(cameras, *row, slot);
            data.camera = data.cameras[9];
            data.camera_default = false;
        }
    return data;
}
}
