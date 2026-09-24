#include "field/zone_document.h"
#include "field/area.h"
#include "field/map_catalog.h"
#include "formats/container.h"
#include "scene/spatial.h"
#include "formats/compression.h"
#include "core/digest.h"
#include <algorithm>
#include <limits>
#include <set>
#include <sstream>
namespace studio {
const std::vector<ZoneProperty> &zone_properties() {
    static const std::vector<ZoneProperty> fields = [] {
        std::vector<ZoneProperty> f;
        auto add = [&](const char *key, const char *label, const char *group, unsigned at,
                       unsigned width, std::int64_t max, std::int64_t min = 0) {
            f.push_back({key, label, group, at, width, -1, min, max});
        };
        add("location_name", "Location name", "Identity", 28, 2, 65535);
        add("transition_category", "Map-change category", "Identity", 35, 1, 4);
        add("name_banner", "Location banner style", "Identity", 30, 1, 255);
        add("music_day", "Day music", "Presentation", 0, 4, 0xffffffffLL);
        add("music_night", "Night music", "Presentation", 4, 4, 0xffffffffLL);
        add("environment", "Lighting environment", "Presentation", 60, 1, 255);
        add("weather_environment", "Weather environment", "Presentation", 34, 1, 255);
        add("camera", "Default camera", "Presentation", 61, 1, 255);
        add("camera_support", "Camera support", "Presentation", 62, 1, 255);
        add("battle_near", "Battle foreground pattern", "Presentation", 64, 2, 65535);
        add("battle_far", "Battle distant background", "Presentation", 66, 2, 65535);
        const char *keys[] = {"ride_run",
                              "ride_fly",
                              "ride_search",
                              "ride_music",
                              "escape",
                              "skybox",
                              "run",
                              "flash",
                              "reverb",
                              "cold_breath",
                              "communication",
                              "bloom",
                              "stereo",
                              "save_escape",
                              "lower_map",
                              "lower_sleep",
                              "townmap_position",
                              "actor_limit",
                              "pedestrian_priority",
                              "visitor_goggles"};
        const char *labels[] = {"Allow ground riding",
                                "Allow flying ride",
                                "Allow item-search ride",
                                "Change music while riding",
                                "Allow escape",
                                "Show skybox",
                                "Allow running",
                                "Flash flag",
                                "Sound reverb",
                                "Visible breath",
                                "Allow communication",
                                "Bloom",
                                "Stereoscopic rendering",
                                "Save escape destination",
                                "Lower screen map mode",
                                "Lower screen sleep",
                                "Town-map positioning mode",
                                "Limit visible characters",
                                "Lower pedestrian rendering priority",
                                "Visitor goggles"};
        for (unsigned i = 0; i < 20; ++i)
            f.push_back({keys[i], labels[i],
                         i < 5 || i == 6 || i == 10 || i == 13 ? "Gameplay" : "Display flags", 56,
                         4, int(i), 0, 1});
        add("town_group", "Town-map group", "Map display", 10, 2, 65535);
        add("island", "Island ID", "Map display", 63, 1, 255);
        add("map_basis_zone", "Map reference zone", "Map display", 76, 2, 65535);
        add("lower_map_type", "Lower screen map type", "Map display", 78, 1, 255);
        add("rotom_effect", "Rotom display effect", "Map display", 79, 1, 255);
        add("map_basis_x", "Map reference X", "Map display", 68, 4, 2147483647, -2147483648LL);
        add("map_basis_z", "Map reference Z", "Map display", 72, 4, 2147483647, -2147483648LL);
        add("start_x", "Map start X", "Start position", 16, 4, 2147483647, -2147483648LL);
        add("start_y", "Map start Y", "Start position", 20, 4, 2147483647, -2147483648LL);
        add("start_z", "Map start Z", "Start position", 24, 4, 2147483647, -2147483648LL);
        add("message_file", "Dialogue message file", "Advanced links", 12, 2, 65535);
        add("gimmick", "Special gameplay handler", "Advanced links", 14, 2, 65535);
        add("map_resource_kind", "Map resource kind", "Advanced links", 31, 1, 255);
        add("memory_scenery", "Pokemon memory scenery", "Advanced links", 33, 1, 255);
        return f;
    }();
    return fields;
}
ZoneDocument::ZoneDocument(Bytes source) : source_(std::move(source)), hash_(sha256(source_)) {
    require(!source_.empty() && source_.size() % 84 == 0, "Unsupported zone settings table");
}
unsigned ZoneDocument::size() const {
    return unsigned(source_.size() / 84);
}
std::int64_t ZoneDocument::original(unsigned zone, unsigned property) const {
    require(zone < size(), "Zone is out of range");
    const auto &f = zone_properties().at(property);
    auto at = std::size_t(zone) * 84 + f.offset;
    std::uint32_t raw = f.width == 1   ? source_[at]
                        : f.width == 2 ? u16(source_, at)
                                       : u32(source_, at);
    if (f.bit >= 0)
        return (raw >> f.bit) & 1;
    return f.minimum < 0 ? std::int64_t(std::int32_t(raw)) : raw;
}
std::int64_t ZoneDocument::value(unsigned zone, unsigned property) const {
    auto base = original(zone, property);
    auto it = current_.find({zone, property});
    return it == current_.end() ? base : it->second;
}
std::vector<std::int64_t> ZoneDocument::source_values(unsigned property) const {
    std::set<std::int64_t> result;
    for (unsigned z = 0; z < size(); ++z)
        result.insert(original(z, property));
    return {result.begin(), result.end()};
}
void ZoneDocument::validate(unsigned zone, unsigned property, std::int64_t v) const {
    auto base = original(zone, property);
    const auto &f = zone_properties().at(property);
    require(v == base || (v >= f.minimum && v <= f.maximum),
            "Zone setting is outside its supported range");
    if (std::string_view(f.key) == "map_basis_zone")
        require(v == base || v < size(), "Map reference zone does not exist");
}
void ZoneDocument::commit(Changes next) {
    if (next == current_)
        return;
    current_ = std::move(next);
    history_.resize(++cursor_);
    history_.push_back(current_);
}
void ZoneDocument::set(unsigned zone, unsigned property, std::int64_t v) {
    validate(zone, property, v);
    auto next = current_;
    if (v == original(zone, property))
        next.erase({zone, property});
    else
        next[{zone, property}] = v;
    commit(std::move(next));
}
void ZoneDocument::reset(unsigned zone) {
    require(zone < size(), "Zone is out of range");
    auto next = current_;
    std::erase_if(next, [zone](auto &v) {
        return v.first.first == zone;
    });
    commit(std::move(next));
}
void ZoneDocument::undo() {
    if (can_undo())
        current_ = history_[--cursor_];
}
void ZoneDocument::redo() {
    if (can_redo())
        current_ = history_[++cursor_];
}
Bytes ZoneDocument::compile() const {
    auto bytes = source_;
    for (auto &[key, v] : current_) {
        auto &f = zone_properties().at(key.second);
        auto at = std::size_t(key.first) * 84 + f.offset;
        auto raw = std::uint32_t(v);
        if (f.bit >= 0)
            raw = (u32(bytes, at) & ~(1u << f.bit)) | (raw << f.bit);
        if (f.width == 1)
            bytes[at] = std::uint8_t(raw);
        else if (f.width == 2)
            put16(bytes, at, std::uint16_t(raw));
        else
            put32(bytes, at, raw);
    }
    return bytes;
}
std::string ZoneDocument::serialize() const {
    std::ostringstream out;
    out << "USUMSTUDIO_ZONE_SETTINGS 1\nsource " << hash_ << '\n';
    for (auto &[key, v] : current_)
        out << "setting " << key.first << ' ' << zone_properties()[key.second].key << ' ' << v
            << '\n';
    out << "end\n";
    return out.str();
}
void ZoneDocument::restore(const std::string &patch) {
    std::istringstream in(patch);
    std::string word, hash, key;
    unsigned version, zone;
    std::int64_t v;
    require(bool(in >> word >> version) && word == "USUMSTUDIO_ZONE_SETTINGS" && version == 1,
            "Unsupported zone settings document");
    require(bool(in >> word >> hash) && word == "source" && hash == hash_,
            "Zone settings source changed");
    Changes next;
    bool end = false;
    while (in >> word) {
        if (word == "end") {
            end = true;
            break;
        }
        require(word == "setting" && bool(in >> zone >> key >> v), "Invalid zone setting");
        const auto &fields = zone_properties();
        auto it = std::find_if(fields.begin(), fields.end(), [&](auto &f) {
            return key == f.key;
        });
        require(it != fields.end(), "Unknown zone property");
        unsigned field = unsigned(it - fields.begin());
        validate(zone, field, v);
        require(next.emplace(std::pair{zone, field}, v).second, "Duplicate zone setting");
    }
    in >> std::ws;
    require(end && in.eof(), "Incomplete zone settings document");
    current_ = std::move(next);
    saved_ = current_;
    history_ = {current_};
    cursor_ = 0;
}
void ZoneDocument::validate_resources(const std::filesystem::path &dump) const {
    std::map<unsigned, std::size_t> environment_counts;
    std::optional<MapCatalog> catalog;
    for (auto &[key, v] : current_) {
        const auto &f = zone_properties()[key.second];
        std::string_view name = f.key;
        if (name == "location_name") {
            auto names = decode_location_text(Archive(dump / TargetProfile::location_text_archive)
                                                  .decoded(TargetProfile::location_text_member));
            require(std::size_t(v) < names.size(),
                    "Location name does not exist in the message table");
        } else if (name == "camera" || name == "camera_support") {
            auto resident =
                Container::parse(Archive(dump / TargetProfile::resident_archive).decoded(0), "FR");
            auto cameras = decode_camera_settings(
                resident.files.at(name == "camera" ? TargetProfile::camera_defaults
                                                   : TargetProfile::camera_support_defaults));
            require(std::size_t(v) < cameras.size(), "Zone references a missing default camera");
        } else if (name == "weather_environment") {
            Archive motions(dump / TargetProfile::light_motion_archive);
            require(std::size_t(v) < motions.size(),
                    "Zone references a missing weather environment");
            Container::parse(motions.decoded(unsigned(v)), "WE");
        } else if (name == "environment") {
            if (!catalog)
                catalog = load_map_catalog(dump);
            bool found = false;
            for (const auto &location : catalog->locations)
                if (location.zone == int(key.first)) {
                    found = true;
                    unsigned area = unsigned(location.area);
                    if (!environment_counts.contains(area)) {
                        Archive field(dump / GameProfile::field_archive(dump));
                        auto environments =
                            Container::parse(field.decoded(area * TargetProfile::area_stride +
                                                           TargetProfile::environment_slot),
                                             "AE");
                        environment_counts[area] = environments.files.size();
                    }
                    require(std::size_t(v) < environment_counts.at(area),
                            "Lighting environment does not exist in this zone's field area");
                }
            require(found, "Cannot resolve a field area for this zone's lighting environment");
        } else if (name == "message_file") {
            Archive messages(dump / TargetProfile::interaction_text_archive);
            require(std::size_t(v) < messages.size(), "Zone dialogue message file does not exist");
        } else if (f.bit < 0 && f.minimum == 0 && name != "transition_category" &&
                   name != "map_basis_zone") {
            auto known = source_values(key.second);
            require(std::find(known.begin(), known.end(), v) != known.end(),
                    std::string(f.label) +
                        " must reference an ID already used by the source zones");
        }
    }
}
void ZoneDocument::export_to(const std::filesystem::path &dump,
                             const std::filesystem::path &output) const {
    if (!changed())
        return;
    auto source = dump / TargetProfile::zone_archive, target = output / TargetProfile::zone_archive;
    require(std::filesystem::absolute(source).lexically_normal() !=
                std::filesystem::absolute(target).lexically_normal(),
            "Choose a separate zone export folder");
    validate_resources(dump);
    Archive archive(source);
    require(sha256(archive.decoded(0)) == hash_,
            "Zone settings source changed; reload before exporting");
    auto bytes = compile();
    auto raw = archive.raw(0);
    if (!raw.empty() && raw.front() == 0x11)
        bytes = compress(bytes);
    require(decompress(bytes) == compile(), "Zone settings compression readback failed");
    std::filesystem::create_directories(target.parent_path());
    archive.export_to(target, {{0, std::move(bytes)}});
}
}
