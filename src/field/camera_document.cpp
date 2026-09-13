#include "field/camera_document.h"
#include "core/digest.h"
#include "field/collision_document.h"
#include "formats/archive.h"
#include <cmath>
#include <iomanip>
#include <sstream>
#include <set>
namespace studio {
CameraDocument::CameraDocument(unsigned area, Bytes original)
    : area_(area), baseline_(std::move(original)), hash_(sha256(baseline_)),
      pack_(Container::parse(baseline_, "BG")), current_(pack_.files), original_(current_),
      saved_(current_), history_{current_} {
    validate(pack_);
    describe();
}
void CameraDocument::validate(const Container &data) const {
    for (auto &f : fields_)
        if (f.type == CameraField::Float) {
            auto value = f32(data.files[f.part], f.offset);
            if (value == f32(original_[f.part], f.offset))
                continue;
            if (f.label.find("FOV") != std::string::npos)
                require(value > 0 && value < 180, "FOV must be between 0 and 180 degrees");
            if (f.label.find("distance") != std::string::npos || f.label == "Radius")
                require(value > 0, "Distance and radius must be positive");
        }
    if (!data.files.at(2).empty()) {
        std::map<std::size_t, CollisionFace> edits;
        for (unsigned i = 0; i < u32(data.files[2], 4); ++i) {
            auto before = read_collision_face(original_[2], i),
                 after = read_collision_face(data.files[2], i);
            if (before != after)
                edits[i] = after;
        }
        if (!edits.empty())
            edit_collision_faces(data.files[2], edits);
    }
    SpatialScene scene, baseline;
    decode_camera_regions(scene, data.write());
    decode_camera_regions(baseline, baseline_);
    for (std::size_t i = 0; i < scene.regions.size(); ++i) {
        auto &r = scene.regions[i];
        if (r.kind == SpatialKind::Camera)
            require(
                r.attribute == 65535 || r.attribute < scene.cameras.size() ||
                    (i < baseline.regions.size() && r.attribute == baseline.regions[i].attribute),
                "Camera region references a missing setting");
    }
    for (std::size_t i = 0; i < scene.replacements.size(); ++i)
        for (unsigned j = 0; j < 3; ++j) {
            auto id = scene.replacements[i][j];
            require(id == 65535 || id < scene.cameras.size() ||
                        (i < baseline.replacements.size() && id == baseline.replacements[i][j]),
                    "Replacement rule references a missing camera");
        }
}

void CameraDocument::describe() {
    auto field = [&](std::string group, std::string label, unsigned part, std::size_t offset,
                     CameraField::Type type = CameraField::Float) {
        fields_.push_back({group, label, part, offset, type});
    };
    auto vector = [&](std::string group, std::string label, unsigned part, std::size_t offset) {
        for (unsigned k = 0; k < 3; ++k)
            field(group, label + " " + "XYZ"[k], part, offset + k * 4);
    };
    auto &table = current_[0];
    auto settings = decode_camera_settings(table);
    std::size_t offset = 28;
    unsigned sizes[]{96, 136, 68, 68};
    for (unsigned i = 0; i < settings.size(); ++i) {
        auto type = settings[i].type;
        auto group = "Camera " + std::to_string(i) + " / " +
                     std::array<const char *, 4>{"Follow", "Blended follow", "Fixed", "Path"}[type];
        field(group, "Transition frames", 0, offset + 4, CameraField::Unsigned);
        field(group, "Priority", 0, offset + 8, CameraField::Signed);
        field(group, "Game variable", 0, offset + 12, CameraField::Signed);
        field(group, "Required value", 0, offset + 16, CameraField::Short);
        auto point = [&](std::string label, std::size_t p) {
            vector(group, label + " target offset", 0, p);
            vector(group, label + " rotation", 0, p + 12);
            field(group, label + " FOV", 0, p + 24);
            field(group, label + " distance", 0, p + 28);
            field(group, label + " uses zone default", 0, p + 32, CameraField::Boolean);
        };
        if (type < 2) {
            point("A", offset + 20);
            if (type == 1) {
                point("B", offset + 56);
                field(group, "Blend easing", 0, offset + 92, CameraField::Unsigned);
            }
            auto p = offset + (type == 1 ? 96 : 56);
            field(group, "Support uses zone default", 0, p, CameraField::Boolean);
            field(group, "Support type (0 none, 1 direction, 2 distance, 3 both)", 0, p + 4,
                  CameraField::Unsigned);
            field(group, "Support uses default parameters", 0, p + 8, CameraField::Boolean);
            field(group, "Support maximum", 0, p + 12);
            for (unsigned side = 0; side < 2; ++side) {
                auto name = side ? "Return" : "Pull back";
                field(group, std::string(name) + " delay frames", 0, p + 16 + side * 8,
                      CameraField::Short);
                field(group, std::string(name) + " duration frames", 0, p + 18 + side * 8,
                      CameraField::Short);
                field(group, std::string(name) + " easing", 0, p + 20 + side * 8,
                      CameraField::Unsigned);
            }
            field(group, "Support single axis", 0, p + 32, CameraField::Boolean);
            field(group, "Support axis angle", 0, p + 36);
        }
        if (type == 2) {
            field(group, "Track player", 0, offset + 20, CameraField::Boolean);
            vector(group, "Player target offset", 0, offset + 24);
            vector(group, "Fixed target", 0, offset + 36);
            vector(group, "Position", 0, offset + 48);
            field(group, "FOV", 0, offset + 60);
            field(group, "Bank", 0, offset + 64);
        }
        offset += sizes[type];
    }
    if (!settings.empty())
        for (unsigned i = 0; i < u32(table, 24); ++i)
            for (unsigned j = 0; j < 3; ++j)
                field("Replacement " + std::to_string(i),
                      std::array<const char *, 3>{"Previous camera", "Entered camera",
                                                  "Use camera"}[j],
                      0, offset + i * 12 + j * 4, CameraField::Unsigned);
    auto &circles = current_[1];
    if (!circles.empty())
        for (unsigned i = 0; i < u32(circles, 8); ++i) {
            auto group = "Circle " + std::to_string(i);
            auto p = 12 + i * 24;
            vector(group, "Center", 1, p);
            field(group, "Radius", 1, p + 12);
            field(group, "Camera setting", 1, p + 16, CameraField::Unsigned);
            field(group, "Center blend ratio", 1, p + 20);
        }
    auto &mesh = current_[2];
    if (!mesh.empty())
        for (unsigned i = 0; i < u32(mesh, 4); ++i) {
            auto group = "Trigger triangle " + std::to_string(i);
            auto p = 8 + i * 72;
            for (unsigned j = 0; j < 3; ++j) {
                vector(group, "Corner " + std::to_string(j + 1), 2, p + j * 16);
                field(group, "Corner " + std::to_string(j + 1) + " blend (0-255)", 2, p + 68 + j,
                      CameraField::Byte);
            }
            field(group, "Camera setting", 2, p + 64, CameraField::Unsigned);
        }
    auto &stops = current_[3];
    if (!stops.empty()) {
        std::size_t p = 16;
        for (unsigned i = 0; i < u32(stops, 4); ++i) {
            bool outside = i >= u32(stops, 8);
            auto group = std::string(outside ? "Keep-out " : "Clamp ") + std::to_string(i);
            for (unsigned j = 0; j < 4; ++j)
                vector(group, "Trigger corner " + std::to_string(j + 1), 3, p + 4 + j * 12);
            if (!outside)
                for (unsigned j = 0; j < 4; ++j)
                    vector(group, "Clamp corner " + std::to_string(j + 1), 3, p + 52 + j * 12);
            p += outside ? 52 : 100;
        }
    }
}
double CameraDocument::value(const CameraField &f) const {
    auto &b = current_.at(f.part);
    switch (f.type) {
    case CameraField::Float:
        return f32(b, f.offset);
    case CameraField::Signed:
        return std::int32_t(u32(b, f.offset));
    case CameraField::Short:
        return u16(b, f.offset);
    case CameraField::Byte:
        return b.at(f.offset);
    default:
        return u32(b, f.offset);
    }
}
void CameraDocument::set(const CameraField &f, double value) {
    require(std::isfinite(value), "Camera value must be finite");
    auto next = pack_;
    next.files = current_;
    auto &b = next.files.at(f.part);
    if (f.type == CameraField::Float) {
        require(std::abs(value) < 1e9, "Camera coordinate is too large");
        if (f.label.find("FOV") != std::string::npos)
            require(value > 0 && value < 180, "FOV must be between 0 and 180 degrees");
        if (f.label.find("distance") != std::string::npos || f.label == "Radius")
            require(value > 0, "Distance and radius must be positive");
        put_float(b, f.offset, float(value));
    } else {
        double low = f.type == CameraField::Signed ? -2147483648. : 0,
               high = f.type == CameraField::Signed    ? 2147483647.
                      : f.type == CameraField::Short   ? 65535.
                      : f.type == CameraField::Byte    ? 255.
                      : f.type == CameraField::Boolean ? 1.
                                                       : 4294967295.;
        require(value >= low && value <= high && std::floor(value) == value,
                "Camera integer is out of range");
        if (f.type == CameraField::Short)
            put16(b, f.offset, std::uint16_t(value));
        else if (f.type == CameraField::Byte)
            b.at(f.offset) = std::uint8_t(value);
        else
            put32(b, f.offset,
                  f.type == CameraField::Signed ? std::uint32_t(std::int32_t(value))
                                                : std::uint32_t(value));
    }
    validate(next);
    if (f.part == 2 && f.type == CameraField::Float) {
        auto face = (f.offset - 8) / 72;
        edit_collision_faces(next.files[2], {{face, read_collision_face(next.files[2], face)}});
    }
    current_ = std::move(next.files);
}
void CameraDocument::commit() {
    if (current_ == history_[cursor_])
        return;
    history_.resize(++cursor_);
    history_.push_back(current_);
}
void CameraDocument::cancel() {
    current_ = history_[cursor_];
}
void CameraDocument::undo() {
    cancel();
    if (can_undo())
        current_ = history_[--cursor_];
}
void CameraDocument::redo() {
    cancel();
    if (can_redo())
        current_ = history_[++cursor_];
}
Bytes CameraDocument::compile() const {
    auto data = pack_;
    data.files = current_;
    if (!current_[2].empty()) {
        std::map<std::size_t, CollisionFace> edits;
        for (unsigned i = 0; i < u32(current_[2], 4); ++i) {
            auto before = read_collision_face(original_[2], i),
                 after = read_collision_face(current_[2], i);
            if (before != after)
                edits[i] = after;
        }
        if (!edits.empty())
            data.files[2] = edit_collision_faces(current_[2], edits);
    }
    return data.write();
}
void CameraDocument::apply(SpatialScene &scene) const {
    SpatialScene camera;
    decode_camera_regions(camera, compile());
    scene.cameras = std::move(camera.cameras);
    scene.replacements = std::move(camera.replacements);
    scene.scroll_stops = std::move(camera.scroll_stops);
    for (auto kind : {SpatialKind::Camera, SpatialKind::ScrollStop}) {
        std::vector<SpatialRegion> updated;
        for (auto &region : camera.regions)
            if (region.kind == kind)
                updated.push_back(std::move(region));
        std::size_t index = 0;
        for (auto &region : scene.regions)
            if (region.kind == kind) {
                if (index < updated.size())
                    region = std::move(updated[index++]);
                else {
                    region = {};
                    region.kind = kind;
                    region.attribute = 65535;
                    region.name = "Unused camera region";
                }
            }
        while (index < updated.size())
            scene.regions.push_back(std::move(updated[index++]));
    }
}

std::string CameraDocument::serialize() const {
    std::ostringstream out;
    out << "USUMSTUDIO_CAMERA 1\narea " << area_ << "\nsource " << hash_ << '\n';
    for (unsigned p = 0; p < current_.size(); ++p)
        for (std::size_t i = 0; i < current_[p].size(); ++i)
            if (current_[p][i] != original_[p][i])
                out << "byte " << p << ' ' << i << ' ' << unsigned(current_[p][i]) << '\n';
    out << "end\n";
    return out.str();
}
void CameraDocument::restore(const std::string &patch) {
    std::istringstream in(patch);
    std::string word, hash;
    unsigned version, area;
    require(bool(in >> word >> version) && word == "USUMSTUDIO_CAMERA" && version == 1,
            "Unsupported camera patch");
    require(bool(in >> word >> area) && word == "area" && area == area_,
            "Camera patch belongs to another map");
    require(bool(in >> word >> hash) && word == "source" && hash == hash_,
            "Camera source differs; load the original source archive");
    auto next = pack_;
    std::set<std::pair<unsigned, std::size_t>> allowed, seen;
    for (auto &f : fields_)
        for (unsigned n = 0; n < (f.type == CameraField::Byte    ? 1
                                  : f.type == CameraField::Short ? 2
                                                                 : 4);
             ++n)
            allowed.emplace(f.part, f.offset + n);
    bool end = false;
    while (in >> word) {
        if (word == "end") {
            end = true;
            break;
        }
        unsigned part, value;
        std::size_t offset;
        require(word == "byte" && bool(in >> part >> offset >> value) && value <= 255 &&
                    allowed.contains({part, offset}) && seen.emplace(part, offset).second,
                "Invalid camera patch change");
        next.files[part][offset] = std::uint8_t(value);
    }
    in >> std::ws;
    require(end && in.eof(), "Incomplete camera patch");
    validate(next);
    current_ = std::move(next.files);
    commit();
    saved_ = current_;
}
void CameraDocument::save(const std::filesystem::path &path) {
    commit();
    auto patch = serialize();
    write_file_atomic(path,
                      View(reinterpret_cast<const std::uint8_t *>(patch.data()), patch.size()));
    saved_ = current_;
}
void CameraDocument::export_to(const std::filesystem::path &dump, const ArchiveSources &sources,
                               const std::filesystem::path &folder) const {
    auto source = sources.resolve(dump, GameProfile::field_archive(dump));
    auto target = folder / GameProfile::field_archive(dump);
    require(std::filesystem::absolute(source).lexically_normal() !=
                std::filesystem::absolute(target).lexically_normal(),
            "Choose a separate override folder");
    Archive archive(source);
    auto member = area_ * TargetProfile::area_stride + TargetProfile::camera_slot;
    require(sha256(archive.decoded(member)) == hash_,
            "Camera source changed after loading; reload before exporting");
    std::filesystem::create_directories(target.parent_path());
    archive.export_to(target, {{member, compile()}});
}
}
