#include "assets/motion_table.h"
#include "assets/model_library.h"
#include "assets/skeleton_edit.h"
#include "assets/visibility_motion.h"
#include "assets/skeletal_motion.h"
#include "assets/material_motion.h"
#include "assets/material_document.h"
#include "assets/pokemon_shadow.h"
#include "assets/mesh_geometry.h"
#include "assets/material_effect.h"
#include "core/digest.h"
#include "scene/model_decoder.h"
#include "formats/compression.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <set>
#include <cstring>
namespace studio {
namespace {
std::uint32_t color_word(const MaterialColor &c) {
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) {
        require(std::isfinite(c[i]) && c[i] >= 0 && c[i] <= 1,
                "Color channels must be between zero and one");
        result |= std::uint32_t(std::lround(c[i] * 255)) << (i * 8);
    }
    return result;
}
std::size_t metadata(View b) {
    std::size_t p = 16;
    for (unsigned i = 0; i < 4; ++i)
        p += 5 + slice(b, p + 4, 1)[0];
    slice(b, p, 168);
    return p;
}
void named(Bytes &out, const std::string &name) {
    require(name.size() < 256, "Texture name is too long");
    std::uint32_t hash = 0x01000193;
    for (unsigned char c : name)
        hash = hash * 0x01000193 ^ c;
    append32(out, hash);
    out.push_back(std::uint8_t(name.size()));
    out.insert(out.end(), name.begin(), name.end());
}
void command(Bytes &out, unsigned reg, std::uint32_t value) {
    append32(out, value);
    append32(out, 0xf0000u | reg);
}
void uniform(Bytes &out, unsigned index, const MaterialColor &value) {
    command(out, 0x2c0, 0x80000000u | index);
    for (unsigned i = 0; i < 4; ++i) {
        std::uint32_t bits;
        std::memcpy(&bits, &value[3 - i], 4);
        command(out, 0x2c1, bits);
    }
}
std::string structure_hash(View bytes) {
    auto parsed = Model::parse(bytes);
    Bytes structure;
    append(structure, slice(bytes, 0, 16));
    for (auto &section : parsed.sections)
        if (section.kind != "material")
            append(structure, slice(bytes, section.offset, section.size));
    return sha256(structure);
}
const AssetResourceLink &model_link(const ModelDocument &model) {
    require(!model.material_resources.empty(), "This asset has no editable model source");
    return model.resources.at(model.material_resources.front());
}
std::uint32_t resource_hash(View bytes) {
    std::uint32_t h = 0x01000193;
    for (auto b : bytes)
        h = h * 0x01000193 ^ b;
    return h;
}
std::string fragment_name(const MaterialEdit &edit) {
    Bytes key;
    for (auto [reg, value] : combiner_registers(*edit.combiners)) {
        append32(key, reg);
        append32(key, value);
    }
    for (auto a : edit.combiners->assignments)
        append32(key, color_word(edit.colors[a]));
    return "studio_" + sha256(key).substr(0, 24);
}
Bytes upsert_fragment(View original, const std::string &name, View shader) {
    if (u32(original, 0) == 0x10000) {
        auto pack = ModelPack::parse(original);
        for (unsigned i = 0; i < pack.resources.size(); ++i)
            if (pack.resources[i].category == 4 && pack.resources[i].name == name)
                return pack.replace(i, shader);
        std::array<std::size_t, 5> counts{};
        std::size_t total = 0, alignment = 16;
        for (unsigned i = 0; i < 5; ++i) {
            counts[i] = u32(original, 4 + i * 4);
            if (i == 4)
                ++counts[i];
            total += counts[i];
        }
        for (std::size_t n = 32; n <= 128; n *= 2) {
            bool match = original.size() % n == 0;
            for (auto &r : pack.resources)
                match &= u32(original, r.address_field) % n == 0;
            if (!match)
                break;
            alignment = n;
        }
        pack.resources.push_back({4, counts[4] - 1, name, Bytes(shader.begin(), shader.end()), 0});
        Bytes out(24 + total * 4);
        put32(out, 0, 0x10000);
        for (unsigned i = 0; i < 5; ++i)
            put32(out, 4 + i * 4, narrow(counts[i]));
        std::vector<std::size_t> fields;
        for (auto &r : pack.resources) {
            std::size_t slot = r.index;
            for (unsigned i = 0; i < r.category; ++i)
                slot += counts[i];
            put32(out, 24 + slot * 4, narrow(out.size()));
            out.push_back(std::uint8_t(r.name.size()));
            out.insert(out.end(), r.name.begin(), r.name.end());
            fields.push_back(out.size());
            append32(out, 0);
        }
        out.resize(aligned(out.size(), alignment));
        for (unsigned i = 0; i < pack.resources.size(); ++i) {
            put32(out, fields[i], narrow(out.size()));
            append(out, pack.resources[i].bytes);
            out.resize(aligned(out.size(), alignment));
        }
        ModelPack::parse(out);
        return out;
    }
    auto pack = Container::parse(original);
    for (unsigned i = 0; i < pack.files.size(); ++i) {
        auto &bytes = pack.files[i];
        if (bytes.size() >= 192 && text(slice(bytes, 16, 8)) == "shader" &&
            text(slice(bytes, 32, 64)) == name)
            return replace_asset_resource(original, {i}, shader);
    }
    pack.files.emplace_back(shader.begin(), shader.end());
    std::size_t alignment = 1;
    for (std::size_t n = 2; n <= 128; n *= 2) {
        bool match = true;
        for (unsigned i = 0; i < pack.files.size(); ++i)
            match &= u32(original, 4 + i * 4) % n == 0;
        if (!match)
            break;
        alignment = n;
    }
    return pack.write(alignment);
}
std::string hex_bytes(View bytes) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (auto b : bytes) {
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}
Bytes unhex(const std::string &text) {
    require(text.size() % 2 == 0 && text.size() <= 32 * 1024 * 1024,
            "Invalid embedded resource size");
    Bytes out;
    out.reserve(text.size() / 2);
    auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        throw std::runtime_error("Invalid embedded resource encoding");
    };
    for (std::size_t i = 0; i < text.size(); i += 2)
        out.push_back(std::uint8_t(digit(text[i]) * 16 + digit(text[i + 1])));
    return out;
}
void apply(SceneMaterial &m, const MaterialEdit &edit) {
    m.shader_origin = edit.shader_origin;
    m.combiner_edit = edit.combiners;
    m.combiner =
        edit.combiners ? decode_combiner(combiner_registers(*edit.combiners)) : m.authored_combiner;
    m.constant_assignments =
        edit.combiners ? edit.combiners->assignments : m.authored_combiners.assignments;
    for (unsigned i = 0; i < 6; ++i)
        if (m.constant_assignments[i] < 6)
            m.combiner.stages[i].constant = edit.colors[m.constant_assignments[i]];
    m.emission = edit.colors[6];
    m.ambient = edit.colors[7];
    m.diffuse = edit.colors[8];
    m.cull = edit.cull;
    m.alpha_function = edit.alpha_function;
    m.alpha_reference = edit.alpha_reference;
    m.blend_state = edit.blend;
    m.depth_state = edit.depth;
    m.layer = edit.layer;
    m.priority = edit.priority;
    m.fragment_lighting = edit.fragment_lighting;
    m.edge_type = edit.edge_type;
    m.edge_id = edit.edge_id;
    m.id_edge_enabled = edit.id_edge_enabled;
    m.edge_alpha_mask = edit.edge_alpha_mask;
    for (unsigned i = 0; i < 3; ++i) {
        auto &t = edit.textures[i];
        m.texture_inputs[i] = t.name;
        m.inputs[i].transform = t.transform;
        m.inputs[i].wrap_u = t.wrap_u;
        m.inputs[i].wrap_v = t.wrap_v;
        m.inputs[i].min_filter = t.min_filter;
        m.inputs[i].mag_filter = t.mag_filter;
        update_texture_transform(m.inputs[i]);
    }
    m.texture = m.texture_inputs[0];
}
}
Bytes asset_resource(View member, const std::vector<std::size_t> &path) {
    Bytes bytes(member.begin(), member.end());
    for (std::size_t i = 0; i < path.size(); ++i) {
        auto index = path[i];
        if (index == motion_table_path) {
            require(++i < path.size(), "Motion slot path is incomplete");
            bytes = motion_table_resource(bytes, path[i]);
        } else if (bytes.size() >= 4 && u32(bytes, 0) == 0x10000)
            bytes = ModelPack::parse(bytes).resources.at(index).bytes;
        else
            bytes = Container::parse(bytes).files.at(index);
    }
    return bytes;
}
Bytes replace_asset_resource(View member, const std::vector<std::size_t> &path, View replacement) {
    if (path.empty())
        return Bytes(replacement.begin(), replacement.end());
    if (path.front() == motion_table_path) {
        require(path.size() == 2, "Invalid motion slot path");
        return replace_motion_table_resource(member, path[1], replacement);
    }
    auto tail = std::vector<std::size_t>(path.begin() + 1, path.end());
    if (u32(member, 0) == 0x10000) {
        auto pack = ModelPack::parse(member);
        auto next =
            replace_asset_resource(pack.resources.at(path.front()).bytes, tail, replacement);
        return pack.replace(path.front(), next);
    }
    auto pack = Container::parse(member);
    pack.files.at(path.front()) =
        replace_asset_resource(pack.files.at(path.front()), tail, replacement);
    std::size_t alignment = 1;
    for (std::size_t next = 2; next <= u32(member, 4); next *= 2) {
        bool matches = true;
        for (unsigned i = 0; i <= pack.files.size(); ++i)
            matches &= u32(member, 4 + i * 4) % next == 0;
        if (!matches)
            break;
        alignment = next;
    }
    Bytes out(member.begin(), member.begin() + u32(member, 4));
    for (unsigned i = 0; i < pack.files.size(); ++i) {
        put32(out, 4 + i * 4, narrow(out.size()));
        append(out, pack.files[i]);
        out.resize(aligned(out.size(), alignment), 0);
    }
    put32(out, 4 + pack.files.size() * 4, narrow(out.size()));
    return out;
}
MaterialDocument::MaterialDocument(ModelDocument document) : model(std::move(document)) {
    initial_feeding_ = model.refresh_feeding;
    if (initial_feeding_ && initial_feeding_->available) {
        auto original = std::make_shared<RefreshFeedingData>(*initial_feeding_);
        original->parameters = refresh_feeding_parameters(original->original, original->row);
        initial_feeding_ = original;
        if (model.refresh_feeding->parameters != original->parameters) {
            feeding_edit_ = model.refresh_feeding->parameters;
            touched_feeding_ = true;
        }
    }
    if (initial_feeding_ && !initial_feeding_->camera_default) {
        auto original = std::make_shared<RefreshFeedingData>(*initial_feeding_);
        for (unsigned slot = 0; slot < 10; ++slot)
            original->cameras[slot] =
                refresh_camera(original->camera_original, original->camera_row, slot);
        original->camera = original->cameras[9];
        initial_feeding_ = original;
        for (unsigned slot = 0; slot < 10; ++slot) {
            auto camera =
                slot == 9 ? model.refresh_feeding->camera : model.refresh_feeding->cameras[slot];
            if (camera != original->cameras[slot]) {
                camera_edit_[slot] = camera;
                touched_camera_.insert(slot);
            }
        }
    }
    initial_refresh_ = model.refresh_regions;
    if (model.refresh_regions && !model.refresh_resources.empty()) {
        auto &source = model.sources.at(model.resources.at(model.refresh_resources.front()).source);
        initial_refresh_ =
            std::make_shared<RefreshRegionPack>(decode_refresh_regions(source.original));
        require(initial_refresh_->masks.size() == model.refresh_regions->masks.size(),
                "Refresh preview source layout differs");
        for (std::size_t i = 0; i < initial_refresh_->masks.size(); ++i)
            if (initial_refresh_->masks[i].ids != model.refresh_regions->masks[i].ids) {
                refresh_edits_[i] = model.refresh_regions->masks[i].ids;
                touched_refresh_.insert(i);
            }
    }
    model.scene = std::make_shared<Environment>(*model.scene);
    auto &link = model_link(model);
    auto raw = asset_resource(model.sources.at(link.source).original, link.path);
    auto parsed = Model::parse(raw);
    for (auto &name : parsed.names[1]) {
        auto full = model.texture_prefix + name;
        if (model.scene->textures.contains(full))
            texture_names_.push_back(full);
    }
    std::vector<ModelSection> sections;
    for (auto &s : parsed.sections)
        if (s.kind == "material")
            sections.push_back(s);
    require(sections.size() == model.scene->materials.size(),
            "The isolated asset must contain every model material");
    ModelDecoder baseline;
    baseline.model(raw, model.name, model.texture_prefix);
    for (unsigned index = 0; index < sections.size(); ++index) {
        auto b = slice(raw, sections[index].offset, sections[index].size);
        auto p = metadata(b);
        auto snapshot = [&](const SceneMaterial &m) {
            MaterialEdit edit;
            edit.shader_origin = m.shader_origin;
            edit.combiners = m.combiner_edit;
            for (unsigned i = 0; i < 6; ++i)
                edit.colors[i] = material_color(u32(b, p + 24 + i * 4));
            for (unsigned i = 0; i < 6; ++i)
                if (m.constant_assignments[i] < 6)
                    edit.colors[m.constant_assignments[i]] = m.combiner.stages[i].constant;
            edit.colors[6] = m.emission;
            edit.colors[7] = m.ambient;
            edit.colors[8] = m.diffuse;
            for (unsigned i = 0; i < 3; ++i) {
                auto &t = m.inputs[i];
                edit.textures[i] = {m.texture_inputs[i], t.transform, t.wrap_u, t.wrap_v,
                                    t.min_filter,        t.mag_filter};
            }
            edit.cull = m.cull;
            edit.alpha_function = m.alpha_function;
            edit.alpha_reference = m.alpha_reference;
            edit.blend = m.blend_state;
            edit.depth = m.depth_state;
            edit.layer = m.layer;
            edit.priority = m.priority;
            edit.fragment_lighting = m.fragment_lighting;
            edit.edge_type = m.edge_type;
            edit.edge_id = m.edge_id;
            edit.id_edge_enabled = m.id_edge_enabled;
            edit.edge_alpha_mask = m.edge_alpha_mask;
            return edit;
        };
        initial_.push_back(snapshot(baseline.out.materials.at(index)));
        edits_.push_back(snapshot(model.scene->materials.at(index)));
    }
    saved_ = initial_;
    for (unsigned i = 0; i < edits_.size(); ++i)
        if (edits_[i] != initial_[i])
            touched_.insert(i);
    for (auto &[name, bytes] : model.scene->texture_overrides)
        if (model.texture_resources.contains(name)) {
            texture_edits_[name] = bytes;
            touched_textures_.insert(name);
        }
    history_.push_back({initial_, {}});
    if (model.area >= 0)
        for (unsigned i = 0; i < model.motions.size(); ++i) {
            auto &motion = model.motions[i];
            auto &resource = model.resources.at(motion.resource);
            auto original =
                asset_resource(model.sources.at(resource.source).original, resource.path);
            if (motion.material != decode_material_motion(original))
                set_map_motion(i, replace_material_motion(original, motion.material));
        }
    commit();
}
std::string MaterialDocument::identity() const {
    auto &link = model_link(model);
    auto &source = model.sources.at(link.source);
    std::ostringstream out;
    if (model.project_asset)
        out << "project_asset " << model.area << ' ';
    out << source.archive.generic_string() << ' ' << source.member;
    if (model.battle_effect)
        out << " subfile " << source.subfile;
    for (auto child : link.path)
        out << ' ' << child;
    return out.str();
}
void MaterialDocument::preview(std::size_t index, const MaterialEdit &edit) {
    if (edit.combiners) {
        auto &c = *edit.combiners;
        require((c.buffer_write & ~0xff00u) == 0, "Invalid combiner buffer mask");
        for (auto a : c.assignments)
            require(a < 6, "Invalid combiner constant selection");
        for (auto &stage : c.stages) {
            require((stage[0] & ~0x0fff0fffu) == 0 && (stage[1] & ~0x00777fffu) == 0 &&
                        (stage[2] & ~0x000f000fu) == 0 && (stage[3] & ~0x00030003u) == 0,
                    "Invalid combiner register fields");
            require((stage[2] & 15) <= 9 && ((stage[2] >> 16) & 15) <= 9 && (stage[3] & 3) <= 2 &&
                        ((stage[3] >> 16) & 3) <= 2,
                    "Unsupported combiner operation or scale");
        }
    }
    auto &original = initial_.at(index);
    auto &material = model.scene->materials.at(index);
    for (auto &c : edit.colors)
        color_word(c);
    require(edit.cull <= 2 && edit.alpha_function < 8 && edit.alpha_reference <= 255,
            "Invalid material render state");
    require(edit.edge_type < 6 && edit.edge_id <= 255 && edit.edge_alpha_mask >= -1 &&
                edit.edge_alpha_mask < 6,
            "Invalid material outline settings");
    if (edit.edge_alpha_mask >= 0 && edit.edge_alpha_mask != original.edge_alpha_mask)
        require(!edit.textures[unsigned(edit.edge_alpha_mask) % 3].name.empty(),
                "Bind a texture to that unit before using it as an outline mask");
    for (unsigned i = 0; i < 3; ++i) {
        auto &t = edit.textures[i];
        require(t.name.empty() == original.textures[i].name.empty(),
                "Adding or removing texture units is not supported yet");
        if (!t.name.empty()) {
            require(model.scene->textures.contains(t.name), "Texture is not loaded by this model");
            require(std::find(texture_names_.begin(), texture_names_.end(), t.name) !=
                        texture_names_.end(),
                    "Texture must belong to this model texture table");
        }
        for (auto value : t.transform)
            require(std::isfinite(value) && std::abs(value) <= 10000, "Invalid texture transform");
        require(t.wrap_u < 4 && t.wrap_v < 4 && t.mag_filter < 2 && t.min_filter < 6,
                "Invalid texture filter or wrapping");
        if (material.inputs[i].source > 2)
            require(t.transform == original.textures[i].transform,
                    "Projected and environment coordinates are read-only");
    }
    require((edit.layer == initial_[index].layer || (edit.layer >= 0 && edit.layer <= 7)) &&
                (edit.priority == initial_[index].priority ||
                 (edit.priority >= 0 && edit.priority <= 7)),
            "Draw layer and priority must be between 0 and 7");
    require((edit.blend & 255) < 5 && ((edit.blend >> 8) & 255) < 5, "Invalid blend equation");
    for (unsigned i = 16; i < 32; i += 4)
        require(((edit.blend >> i) & 15) < 15, "Invalid blend factor");
    if (edits_[index] != edit) {
        edits_[index] = edit;
        touched_.insert(index);
        synchronize();
    }
}
void MaterialDocument::preview_feeding(const RefreshFeedingParameters &parameters) {
    require(initial_feeding_ && initial_feeding_->available,
            "This Pokemon has no editable feeding row");
    validate_refresh_feeding(parameters);
    feeding_edit_ =
        parameters == initial_feeding_->parameters ? std::nullopt : std::optional(parameters);
    touched_feeding_ = true;
    synchronize_feeding();
    ++revision_;
}
void MaterialDocument::reset_feeding() {
    require(initial_feeding_ && initial_feeding_->available,
            "This Pokemon has no editable feeding row");
    feeding_edit_.reset();
    touched_feeding_ = true;
    synchronize_feeding();
    ++revision_;
    commit();
}
void MaterialDocument::preview_refresh_camera(unsigned slot, const RefreshCamera &camera) {
    require(slot < 10 && initial_feeding_ && !initial_feeding_->camera_default,
            "This Pokemon has no editable Refresh camera row");
    validate_refresh_feeding_camera(camera);
    if (camera == initial_feeding_->cameras[slot])
        camera_edit_.erase(slot);
    else
        camera_edit_[slot] = camera;
    touched_camera_.insert(slot);
    synchronize_feeding();
    ++revision_;
}
void MaterialDocument::preview_feeding_camera(const RefreshFeedingCamera &camera) {
    preview_refresh_camera(9, camera);
}
void MaterialDocument::reset_refresh_camera(unsigned slot) {
    require(slot < 10 && initial_feeding_ && !initial_feeding_->camera_default,
            "This Pokemon has no editable Refresh camera row");
    camera_edit_.erase(slot);
    touched_camera_.insert(slot);
    synchronize_feeding();
    ++revision_;
    commit();
}
void MaterialDocument::reset_feeding_camera() {
    reset_refresh_camera(9);
}
void MaterialDocument::synchronize_feeding() {
    if (!initial_feeding_)
        return;
    auto parameters = feeding_edit_.value_or(initial_feeding_->parameters);
    auto cameras = initial_feeding_->cameras;
    for (auto &[slot, camera] : camera_edit_)
        cameras[slot] = camera;
    auto camera = cameras[9];
    if (model.refresh_feeding && model.refresh_feeding->parameters == parameters &&
        model.refresh_feeding->camera == camera && model.refresh_feeding->cameras == cameras)
        return;
    auto next = std::make_shared<RefreshFeedingData>(*initial_feeding_);
    next->parameters = parameters;
    next->camera = camera;
    next->cameras = cameras;
    model.refresh_feeding = std::move(next);
}
void MaterialDocument::synchronize() {
    synchronize_map_motions();
    synchronize_feeding();
    synchronize_refresh();
    for (auto &name : touched_textures_) {
        auto &link = model.resources.at(model.texture_resources.at(name));
        auto it = texture_edits_.find(name);
        auto shown = model.scene->texture_overrides.find(name);
        if ((it == texture_edits_.end() && shown == model.scene->texture_overrides.end()) ||
            (it != texture_edits_.end() && shown != model.scene->texture_overrides.end() &&
             it->second == shown->second))
            continue;
        ++texture_revision_;
        auto bytes = it == texture_edits_.end()
                         ? asset_resource(model.sources.at(link.source).original, link.path)
                         : *it->second;
        model.scene->textures[name] = decode_field_texture(bytes);
        if (it == texture_edits_.end())
            model.scene->texture_overrides.erase(name);
        else
            model.scene->texture_overrides[name] = it->second;
    }
    for (unsigned i = 0; i < edits_.size(); ++i)
        apply(model.scene->materials[i], edits_[i]);
    ++revision_;
}
void MaterialDocument::commit() {
    if (edits_ == history_[cursor_].materials && texture_edits_ == history_[cursor_].textures &&
        refresh_edits_ == history_[cursor_].refresh && feeding_edit_ == history_[cursor_].feeding &&
        camera_edit_ == history_[cursor_].camera &&
        map_motion_edits_ == history_[cursor_].map_motions)
        return;
    structural_redo_.reset();
    history_.resize(cursor_ + 1);
    history_.push_back(
        {edits_, texture_edits_, refresh_edits_, feeding_edit_, camera_edit_, map_motion_edits_});
    ++cursor_;
    if (history_.size() > 257) {
        history_.erase(history_.begin());
        --cursor_;
    }
}
void MaterialDocument::undo() {
    if (cursor_ > 0) {
        --cursor_;
        edits_ = history_[cursor_].materials;
        texture_edits_ = history_[cursor_].textures;
        refresh_edits_ = history_[cursor_].refresh;
        feeding_edit_ = history_[cursor_].feeding;
        camera_edit_ = history_[cursor_].camera;
        map_motion_edits_ = history_[cursor_].map_motions;
        synchronize();
    } else if (structural_parent_) {
        auto next = *structural_parent_;
        next.structural_redo_ = std::make_shared<MaterialDocument>(*this);
        adopt_structure(std::move(next));
    }
}
void MaterialDocument::redo() {
    if (cursor_ + 1 < history_.size()) {
        ++cursor_;
        edits_ = history_[cursor_].materials;
        texture_edits_ = history_[cursor_].textures;
        refresh_edits_ = history_[cursor_].refresh;
        feeding_edit_ = history_[cursor_].feeding;
        camera_edit_ = history_[cursor_].camera;
        map_motion_edits_ = history_[cursor_].map_motions;
        synchronize();
    } else if (structural_redo_)
        adopt_structure(*structural_redo_);
}
void MaterialDocument::reset(std::size_t index) {
    preview(index, initial_.at(index));
    commit();
}
void MaterialDocument::borrow_shader(std::size_t index, const ModelDocument &donor,
                                     std::size_t donor_index, bool include_material_state) {
    auto &target = model.scene->materials.at(index);
    auto &source = donor.scene->materials.at(donor_index);
    require(source.combiner.present && source.combiner.unsupported.empty(),
            "The donor uses an unsupported fragment setup");
    bool found = false;
    for (auto &link : donor.resources)
        if (link.role == "Shader" && link.name == source.fragment_shader) {
            auto shader = decode_material_shader(
                asset_resource(donor.sources.at(link.source).original, link.path));
            require(shader.combiner.present, "The selected resource is not a fragment shader");
            for (auto [reg, value] : shader.registers) {
                bool supported = reg == 0 || reg == 0x23d || reg == 0xe0 || reg == 0xfd;
                for (unsigned stage = 0; stage < 6; ++stage) {
                    unsigned base = stage < 4 ? 0xc0 + stage * 8 : 0xf0 + (stage - 4) * 8;
                    supported |= reg >= base && reg <= base + 4;
                }
                require(supported,
                        "The donor fragment shader contains state that cannot be borrowed yet");
            }
            found = true;
            break;
        }
    require(found, "The donor fragment shader resource is unavailable");
    auto settings = source.combiner_edit.value_or(source.authored_combiners);
    auto decoded = decode_combiner(combiner_registers(settings));
    for (auto &stage : decoded.stages)
        for (unsigned channel = 0; channel < 2; ++channel) {
            if (channel && stage.operation[0] == 7)
                continue;
            auto operation = unsigned(stage.operation[channel]);
            unsigned count = operation == 0 ? 1 : operation == 4 || operation >= 8 ? 3 : 2;
            auto &inputs = channel ? stage.alpha_sources : stage.color_sources;
            for (unsigned i = 0; i < count; ++i) {
                auto input = unsigned(inputs[i]);
                if (input >= 3 && input <= 5)
                    require(!edits_.at(index).textures[input - 3].name.empty(),
                            "The donor requires Texture " + std::to_string(input - 3) +
                                ", but the target material has no binding for it");
                if (input == 0)
                    require(source.generated_lighting_color == target.generated_lighting_color &&
                                source.height_tint == target.height_tint,
                            "The donor needs a different primary-color vertex setup");
            }
        }
    require(source.screen_refraction == target.screen_refraction,
            "The donor needs a different screen-refraction vertex setup");
    auto edit = edits_.at(index);
    if (include_material_state) {
        MaterialDocument donor_document(donor);
        auto donor_edit = donor_document.edits().at(donor_index);
        auto textures = edit.textures;
        edit = donor_edit;
        edit.textures = std::move(textures);
    }
    edit.combiners = settings;
    edit.shader_origin = donor.name + " / " + source.name + " / " + source.fragment_shader;
    preview(index, edit);
    commit();
}
const AssetResourceLink &MaterialDocument::fragment_link(std::size_t index) const {
    auto &material = model.scene->materials.at(index);
    for (auto &link : model.resources)
        if (link.role == "Shader" && link.name == material.fragment_shader)
            return link;
    throw std::runtime_error("This material's fragment resource has no writable source link");
}
Bytes MaterialDocument::fragment_resource(std::size_t index) const {
    auto &edit = edits_.at(index);
    require(bool(edit.combiners), "Material has no edited fragment setup");
    Bytes stream;
    for (auto [reg, value] : combiner_registers(*edit.combiners)) {
        append32(stream, value);
        append32(stream, (reg == 0xe0 ? 0x20000u : 0xf0000u) | reg);
    }
    for (unsigned stage = 0; stage < 6; ++stage) {
        unsigned base = stage < 4 ? 0xc0 + stage * 8 : 0xf0 + (stage - 4) * 8;
        command(stream, base + 3, color_word(edit.colors[edit.combiners->assignments[stage]]));
    }
    if (stream.size() % 16 == 0)
        command(stream, 0, 0);
    command(stream, 0x23d, 1);
    Bytes out(192);
    put32(out, 0, 0x15041213);
    put32(out, 4, 1);
    std::copy_n("shader", 6, out.begin() + 16);
    put32(out, 24, narrow(160 + stream.size()));
    auto name = fragment_name(edit);
    std::copy(name.begin(), name.end(), out.begin() + 32);
    put32(out, 96,
          resource_hash(View(reinterpret_cast<const std::uint8_t *>(name.data()), name.size())));
    put32(out, 100, 1);
    put32(out, 112, narrow(stream.size()));
    put32(out, 116, 1);
    put32(out, 120, resource_hash(stream));
    std::copy(name.begin(), name.end(), out.begin() + 128);
    append(out, stream);
    require(decode_material_shader(out).combiner.unsupported.empty(),
            "Generated fragment setup is unsupported");
    return out;
}
Bytes MaterialDocument::texture_resource(const std::string &name) const {
    require(model.texture_resources.contains(name), "This texture has no writable source link");
    if (auto found = texture_edits_.find(name); found != texture_edits_.end())
        return *found->second;
    auto &link = model.resources.at(model.texture_resources.at(name));
    return asset_resource(model.sources.at(link.source).original, link.path);
}
void MaterialDocument::replace_texture(const std::string &name, const TextureImage &image,
                                       std::optional<TextureFormat> format) {
    auto current = texture_resource(name);
    auto encoded = encode_texture(current, image, text(slice(current, 40, 64)), format);
    replace_encoded_texture(name, encoded);
}
void MaterialDocument::replace_encoded_texture(const std::string &name, View encoded) {
    auto current = texture_resource(name);
    require(text(slice(encoded, 40, 64)) == text(slice(current, 40, 64)),
            "Replacement texture name changed");
    if (std::equal(encoded.begin(), encoded.end(), current.begin(), current.end()))
        return;
    decode_field_texture(encoded);
    auto &link = model.resources.at(model.texture_resources.at(name));
    auto original = asset_resource(model.sources.at(link.source).original, link.path);
    if (std::equal(encoded.begin(), encoded.end(), original.begin(), original.end()))
        texture_edits_.erase(name);
    else
        texture_edits_[name] = std::make_shared<const Bytes>(encoded.begin(), encoded.end());
    touched_textures_.insert(name);
    synchronize();
    commit();
}
void MaterialDocument::reset_texture(const std::string &name) {
    if (!texture_edits_.contains(name))
        return;
    texture_edits_.erase(name);
    touched_textures_.insert(name);
    synchronize();
    commit();
}
std::set<std::filesystem::path> MaterialDocument::archive_paths() const {
    std::set<std::filesystem::path> paths;
    if (structural_ || model.area < 0 || !touched_.empty() || touched_map_motions_.empty())
        paths.insert(archive_path());
    for (unsigned i = 0; i < edits_.size(); ++i)
        if (edits_[i].combiners)
            paths.insert(model.sources.at(fragment_link(i).source).archive);
    for (auto &name : touched_textures_)
        paths.insert(
            model.sources.at(model.resources.at(model.texture_resources.at(name)).source).archive);
    for (auto mask : touched_refresh_)
        paths.insert(
            model.sources.at(model.resources.at(model.refresh_resources.at(mask)).source).archive);
    for (auto index : touched_map_motions_)
        paths.insert(
            model.sources.at(model.resources.at(model.motions.at(index).resource).source).archive);
    if (touched_feeding_ || !touched_camera_.empty())
        paths.insert(TargetProfile::refresh_parameters_archive);
    return paths;
}
bool MaterialDocument::dirty() const {
    return map_motion_edits_ != saved_map_motions_ ||
           (structural_ && structural_version_ != structural_->saved_version) || edits_ != saved_ ||
           texture_edits_ != saved_textures_ || refresh_edits_ != saved_refresh_ ||
           feeding_edit_ != saved_feeding_ || camera_edit_ != saved_camera_;
}
void MaterialDocument::accept_written(const MaterialDocument &snapshot) {
    require(identity() == snapshot.identity(), "Written snapshot belongs to another asset");
    for (auto &[index, hashes] : snapshot.accepted_map_motions_)
        accepted_map_motions_[index].insert(hashes.begin(), hashes.end());
    for (auto &[mask, hashes] : snapshot.accepted_refresh_)
        accepted_refresh_[mask].insert(hashes.begin(), hashes.end());
    if (initial_feeding_ && snapshot.initial_feeding_ &&
        initial_feeding_->original == snapshot.initial_feeding_->original &&
        initial_feeding_->row == snapshot.initial_feeding_->row)
        accepted_feeding_.insert(snapshot.accepted_feeding_.begin(),
                                 snapshot.accepted_feeding_.end());
    if (initial_feeding_ && snapshot.initial_feeding_ &&
        initial_feeding_->camera_original == snapshot.initial_feeding_->camera_original &&
        initial_feeding_->camera_row == snapshot.initial_feeding_->camera_row)
        for (auto &[slot, hashes] : snapshot.accepted_camera_)
            accepted_camera_[slot].insert(hashes.begin(), hashes.end());
}
void MaterialDocument::mark_saved() {
    saved_map_motions_ = map_motion_edits_;
    saved_ = edits_;
    saved_textures_ = texture_edits_;
    saved_refresh_ = refresh_edits_;
    saved_feeding_ = feeding_edit_;
    saved_camera_ = camera_edit_;
    if (structural_) {
        structural_->saved_version = structural_version_;
        auto snapshot = std::make_shared<MaterialDocument>(*this);
        snapshot->model.scene = std::make_shared<Environment>(*model.scene);
        snapshot->structural_.reset();
        snapshot->structural_parent_.reset();
        snapshot->structural_redo_.reset();
        structural_->saved = std::move(snapshot);
    }
}
void MaterialDocument::adopt_structure(MaterialDocument next) {
    next.accept_written(*this);
    auto revision = revision_ + 1, geometry = model_revision_ + 1, textures = texture_revision_ + 1;
    next.structural_ = structural_;
    next.model.scene = std::make_shared<Environment>(*next.model.scene);
    *this = std::move(next);
    revision_ = revision;
    model_revision_ = geometry;
    texture_revision_ = textures;
}
void MaterialDocument::discard() {
    if (structural_ && structural_->saved) {
        adopt_structure(*structural_->saved);
        return;
    }
    map_motion_edits_ = saved_map_motions_;
    edits_ = saved_;
    texture_edits_ = saved_textures_;
    refresh_edits_ = saved_refresh_;
    feeding_edit_ = saved_feeding_;
    camera_edit_ = saved_camera_;
    commit();
    synchronize();
}
void MaterialDocument::import_native_members(const std::map<std::size_t, Bytes> &members) {
    commit();
    replace_members(members);
}
std::vector<Bytes> MaterialDocument::package_members() const {
    std::vector<Bytes> out;
    const auto compiled = compiled_members();
    for (const auto &source : model.sources) {
        auto found = compiled.find(source.member);
        out.push_back(source.archive == archive_path() && found != compiled.end()
                          ? found->second
                          : source.original);
    }
    const auto &link = model_link(model);
    out[link.source] = replace_asset_resource(out[link.source], link.path, compile());
    for (const auto &[name, index] : model.texture_resources) {
        const auto &resource = model.resources.at(index);
        out[resource.source] =
            replace_asset_resource(out[resource.source], resource.path, texture_resource(name));
    }
    for (unsigned i = 0; i < edits_.size(); ++i)
        if (edits_[i].combiners) {
            const auto &resource = fragment_link(i);
            auto path = resource.path;
            path.pop_back();
            auto &bytes = out[resource.source];
            bytes = replace_asset_resource(bytes, path,
                                           upsert_fragment(asset_resource(bytes, path),
                                                           fragment_name(edits_[i]),
                                                           fragment_resource(i)));
        }
    for (const auto &[index, bytes] : map_motion_edits_) {
        const auto &resource = model.resources.at(model.motions.at(index).resource);
        out[resource.source] = replace_asset_resource(out[resource.source], resource.path, bytes);
    }
    return out;
}
std::map<std::size_t, Bytes> MaterialDocument::compiled_members() const {
    std::map<std::size_t, Bytes> result;
    if (model.area >= 0 && !model.project_asset) {
        for (const auto &source : model.sources)
            if (source.archive == archive_path() &&
                (source.member == model.sources.at(model_link(model).source).member ||
                 (structural_ && structural_->originals.contains(source.member))))
                result[source.member] = source.original;
        return result;
    }
    if (structural_)
        for (auto &[index, original] : structural_->originals) {
            auto source = std::find_if(model.sources.begin(), model.sources.end(), [&](auto &s) {
                return s.member == index && s.archive == archive_path();
            });
            result[index] = source == model.sources.end() ? original : source->original;
        }
    auto member = [&](std::size_t source) -> Bytes & {
        auto &s = model.sources.at(source);
        auto [it, added] = result.try_emplace(s.member);
        if (added)
            it->second = s.original;
        return it->second;
    };
    auto &link = model_link(model);
    if (edits_ != initial_ || !touched_.empty()) {
        auto &bytes = member(link.source);
        bytes = replace_asset_resource(bytes, link.path, compile());
    }
    for (auto &name : touched_textures_) {
        auto &resource = model.resources.at(model.texture_resources.at(name));
        auto &current = member(resource.source);
        auto edit = texture_edits_.find(name);
        auto original = asset_resource(model.sources.at(resource.source).original, resource.path);
        current = replace_asset_resource(current, resource.path,
                                         edit == texture_edits_.end() ? View(original)
                                                                      : View(*edit->second));
    }
    if (model.project_asset)
        for (const auto &[index, bytes] : map_motion_edits_) {
            const auto &resource = model.resources.at(model.motions.at(index).resource);
            auto &current = member(resource.source);
            current = replace_asset_resource(current, resource.path, bytes);
        }
    for (auto mask : touched_refresh_) {
        auto &resource = model.resources.at(model.refresh_resources.at(mask));
        auto &bytes = member(resource.source);
        bytes = merge_refresh(mask, bytes);
    }
    for (unsigned i = 0; i < edits_.size(); ++i)
        if (edits_[i].combiners) {
            auto &resource = fragment_link(i);
            auto path = resource.path;
            require(!path.empty(), "Fragment shader has no containing pack");
            path.pop_back();
            auto &current = member(resource.source);
            auto parent = asset_resource(current, path);
            current = replace_asset_resource(
                current, path,
                upsert_fragment(parent, fragment_name(edits_[i]), fragment_resource(i)));
        }
    return result;
}
ModelDocument MaterialDocument::preview_model() const {
    auto preview = reload_editable_model(model, compiled_members());
    if (model.area >= 0)
        apply_to(*preview.scene);
    return preview;
}
std::string MaterialDocument::borrow_effect(std::size_t material, const MaterialDocument &donor,
                                            const std::vector<std::size_t> &passes,
                                            bool keep_original) {
    commit();
    auto target = preview_model(), source = donor.preview_model();
    require(
        !target.shadow_model && !source.shadow_model,
        "Borrow material effects using main models; shadow materials have their own render setup");
    auto effect = borrow_material_effect(target, material, source, passes, keep_original);
    auto previous = compiled_members();
    auto members = previous;
    for (auto &[index, bytes] : effect.members)
        members[index] = bytes;
    replace_members(members);
    return effect.summary;
}
void MaterialDocument::replace_members(const std::map<std::size_t, Bytes> &input) {
    auto members = input;
    if (model.is_pokemon())
        if (auto edited = members.find(model.pokemon.model_member); edited != members.end()) {
            auto &source = model.sources.at(model_link(model).source);
            edited->second =
                update_pokemon_shadow(source.original, edited->second, model.shadow_model);
        }
    auto previous = compiled_members();
    MaterialDocument next(reload_editable_model(model, members));
    if (model.area >= 0 && !model.project_asset) {
        next.edits_ = edits_;
        next.touched_ = touched_;
        next.texture_edits_ = texture_edits_;
        next.touched_textures_ = touched_textures_;
        next.map_motion_edits_ = map_motion_edits_;
        next.touched_map_motions_ = touched_map_motions_;
        next.synchronize();
    }
    next.initial_feeding_ = initial_feeding_;
    next.feeding_edit_ = feeding_edit_;
    next.saved_feeding_ = saved_feeding_;
    next.touched_feeding_ = touched_feeding_;
    next.camera_edit_ = camera_edit_;
    next.saved_camera_ = saved_camera_;
    next.touched_camera_ = touched_camera_;
    next.synchronize_feeding();
    next.history_.clear();
    next.history_.push_back({next.edits_, next.texture_edits_, next.refresh_edits_,
                             next.feeding_edit_, next.camera_edit_});
    next.cursor_ = 0;
    if (!structural_) {
        auto saved = std::make_shared<MaterialDocument>(*this);
        saved->edits_ = saved_;
        saved->texture_edits_ = saved_textures_;
        saved->refresh_edits_ = saved_refresh_;
        saved->feeding_edit_ = saved_feeding_;
        saved->camera_edit_ = saved_camera_;
        saved->model.scene = std::make_shared<Environment>(*saved->model.scene);
        saved->synchronize();
        structural_ = std::make_shared<StructuralSession>();
        structural_->saved = std::move(saved);
    }
    Archive archive(model.archive_sources.resolve(model.dump, archive_path()));
    for (auto &[index, bytes] : members) {
        if (!structural_->originals.contains(index)) {
            auto original = std::find_if(model.sources.begin(), model.sources.end(), [&](auto &s) {
                return s.member == index && s.archive == archive_path();
            });
            structural_->originals[index] =
                original == model.sources.end() ? archive.decoded(index) : original->original;
        }
        structural_->accepted[index].insert(sha256(structural_->originals.at(index)));
        structural_->accepted[index].insert(sha256(bytes));
        if (previous.contains(index))
            structural_->accepted[index].insert(sha256(previous.at(index)));
    }
    next.structural_parent_ = std::make_shared<MaterialDocument>(*this);
    next.structural_parent_->model.scene = std::make_shared<Environment>(*model.scene);
    next.structural_version_ = structural_->next_version++;
    adopt_structure(std::move(next));
}
SkinnedModel MaterialDocument::geometry() const {
    require(!model.clothing, "Select an individual model to edit geometry");
    auto &link = model_link(model);
    auto &source = model.sources.at(link.source);
    auto result = SkinnedModel::parse(asset_resource(source.original, link.path));
    if (!model.native_meshes.empty()) {
        require(model.native_meshes.size() == result.meshes.size(),
                "Native mesh mapping differs from geometry");
        auto meshes = std::move(result.meshes);
        for (auto i : model.native_meshes)
            result.meshes.push_back(std::move(meshes.at(i)));
    }
    return result;
}
ModelExchange MaterialDocument::model_exchange() const {
    return decode_model_exchange(geometry().model.original);
}
FaceMaterialEdit MaterialDocument::assign_material_faces(const MaterialFaces &faces,
                                                         std::size_t material) {
    require(!model.clothing, "Open an individual model to assign face materials");
    require(material < model.scene->materials.size(), "Choose a material in this model");
    auto link = model_link(model);
    auto source = model.sources.at(link.source);
    auto members = compiled_members();
    auto found = members.find(source.member);
    auto member = found == members.end() ? source.original : found->second;
    auto original = asset_resource(member, link.path);
    auto old_order = model.native_meshes;
    MaterialFaces native_faces;
    for (auto &[draw, selected] : faces)
        native_faces[old_order.empty() ? draw : old_order.at(draw)] = selected;
    auto edit = assign_face_material(original, native_faces, model.scene->materials[material].name);
    if (edit.bytes != original) {
        members[source.member] = replace_asset_resource(member, link.path, edit.bytes);
        commit();
        replace_members(members);
    }
    auto native_sources = std::move(edit.sources);
    auto native_selected = std::move(edit.selected);
    edit.sources.clear();
    edit.selected.clear();
    for (std::size_t draw = 0; draw < native_sources.size(); ++draw) {
        auto native = model.native_meshes.empty() ? draw : model.native_meshes.at(draw);
        std::set<std::size_t> sources;
        for (auto source_draw : native_sources.at(native)) {
            auto previous =
                old_order.empty()
                    ? source_draw
                    : std::size_t(std::find(old_order.begin(), old_order.end(), source_draw) -
                                  old_order.begin());
            sources.insert(previous);
        }
        edit.sources.push_back(std::move(sources));
        if (native_selected.contains(native))
            edit.selected[draw] = std::move(native_selected.at(native));
    }
    return edit;
}
MotionExchange MaterialDocument::motion_exchange(std::size_t index) const {
    require(model.area < 0 && !model.clothing, "Open an individual model to exchange motions");
    auto &motion = model.motions.at(index);
    require(motion.error.empty(), motion.error);
    auto link = model.resources.at(motion.resource);
    auto source = model.sources.at(link.source);
    auto members = compiled_members();
    auto found = members.find(source.member);
    auto bytes =
        asset_resource(found == members.end() ? source.original : found->second, link.path);
    return decode_motion_exchange(bytes, model_exchange().source, motion.name);
}
void MaterialDocument::import_motion_exchange(std::size_t index,
                                              const MotionExchange &replacement) {
    require(!model.shadow_model, "Import shared motions on the main model");
    auto baseline = motion_exchange(index);
    require(replacement.model == baseline.model,
            "The model changed; export its model and motion again");
    auto joints = geometry().joints;
    auto binding = [](const auto &track) {
        if constexpr (requires { track.name; })
            return track.name;
        else if constexpr (requires { track.material; })
            return track.material;
        else
            return track.mesh;
    };
    auto preserve = [&](const auto &before, const auto &after, const auto &known) {
        for (auto &track : before)
            if (!known(track))
                require(std::find(after.begin(), after.end(), track) != after.end(),
                        "Unmatched source motion track must remain unchanged: " + binding(track));
        for (auto &track : after)
            if (!known(track))
                require(std::find(before.begin(), before.end(), track) != before.end(),
                        "Motion track has no binding in this model: " + binding(track));
    };
    preserve(baseline.skeletal.tracks, replacement.skeletal.tracks, [&](auto &t) {
        return std::any_of(joints.begin(), joints.end(), [&](auto &j) {
            return j.name == t.name;
        });
    });
    preserve(baseline.material.tracks, replacement.material.tracks, [&](auto &t) {
        return std::any_of(model.scene->materials.begin(), model.scene->materials.end(),
                           [&](auto &m) {
                               return m.name == t.material;
                           });
    });
    preserve(baseline.visibility.tracks, replacement.visibility.tracks, [&](auto &t) {
        return std::any_of(model.scene->draws.begin(), model.scene->draws.end(), [&](auto &d) {
            return d.mesh == t.mesh;
        });
    });
    for (auto &t : replacement.material.tracks)
        for (auto &key : t.textures)
            require(model.scene->textures.contains(model.texture_prefix + key.texture),
                    "Motion uses a texture that is not loaded by the model");
    auto link = model.resources.at(model.motions.at(index).resource);
    auto source = model.sources.at(link.source);
    auto members = compiled_members();
    auto found = members.find(source.member);
    auto member = found == members.end() ? source.original : found->second;
    auto original = asset_resource(member, link.path);
    auto bytes = replace_motion_exchange(original, replacement);
    if (bytes == original)
        return;
    members[source.member] = replace_asset_resource(member, link.path, bytes);
    commit();
    replace_members(members);
}
void MaterialDocument::edit_uvs(unsigned channel, const MeshUvEdits &edits) {
    require((model.area < 0 || model.project_asset) && !model.clothing, "Open an individual model to edit UVs");
    auto link = model_link(model);
    auto source = model.sources.at(link.source);
    auto members = compiled_members();
    auto found = members.find(source.member);
    auto member = found == members.end() ? source.original : found->second;
    auto original = asset_resource(member, link.path);
    auto bytes = replace_model_uvs(original, channel, edits);
    if (bytes == original)
        return;
    members[source.member] = replace_asset_resource(member, link.path, bytes);
    commit();
    replace_members(members);
}
void MaterialDocument::import_model_exchange(const ModelExchange &replacement) {
    require(!model.clothing, "Open an individual model in Studio");
    auto link = model_link(model);
    auto source = model.sources.at(link.source);
    auto members = compiled_members();
    auto found = members.find(source.member);
    auto member = found == members.end() ? source.original : found->second;
    auto original = asset_resource(member, link.path);
    auto bytes = replace_model_exchange(original, replacement);
    if (bytes == original)
        return;
    members[source.member] = replace_asset_resource(member, link.path, bytes);
    commit();
    replace_members(members);
}
void MaterialDocument::import_new_model(const ModelExchange &replacement,
                                        const std::vector<std::size_t> &materials) {
    require(!model.clothing, "Open an individual model in Studio");
    auto link = model_link(model);
    auto source = model.sources.at(link.source);
    auto members = compiled_members();
    auto found = members.find(source.member);
    auto member = found == members.end() ? source.original : found->second;
    auto original = asset_resource(member, link.path);
    auto bound = bind_new_model(replacement, decode_model_exchange(original), materials);
    auto bytes = replace_model_exchange(original, bound, replacement.joints.empty());
    auto updated = replace_asset_resource(member, link.path, bytes);
    if (model.is_pokemon() && !replacement.joints.empty()) {
        require(!model.shadow_model, "Import a new skeleton into the main model first");
        auto pack = Container::parse(updated, "PC");
        if (pack.files.size() > 1 && !pack.files[1].empty()) {
            auto shadow = decode_model_exchange(pack.files[1]);
            auto old_joints = shadow.joints;
            shadow.joints = bound.joints;
            for (auto &mesh : shadow.meshes)
                for (auto &vertex : mesh.vertices) {
                    std::map<unsigned, float> weights;
                    for (unsigned k = 0; k < 4; ++k)
                        if (vertex.weights[k] > 0) {
                            const auto &name = old_joints.at(vertex.joints[k]).name;
                            auto found = std::find_if(bound.joints.begin(), bound.joints.end(),
                                                      [&](const auto &bone) {
                                                          return bone.name == name;
                                                      });
                            auto joint = found == bound.joints.end()
                                             ? 0u
                                             : unsigned(found - bound.joints.begin());
                            weights[joint] += vertex.weights[k];
                        }
                    vertex.joints = {};
                    vertex.weights = {};
                    float total = 0;
                    for (auto [joint, weight] : weights)
                        total += weight;
                    unsigned k = 0;
                    for (auto [joint, weight] : weights) {
                        vertex.joints[k] = std::uint16_t(joint);
                        vertex.weights[k++] = weight / total;
                    }
                }
            auto shadow_bytes = replace_model_exchange(pack.files[1], shadow, false);
            updated = replace_asset_resource(updated, {1}, shadow_bytes);
        }
    }
    members[source.member] = std::move(updated);
    commit();
    replace_members(members);
}
void MaterialDocument::edit_skeleton(const std::vector<Joint> &joints) {
    require(!model.shadow_model,
            "Edit shared bones on the main model, then stage and reload the shadow model");
    require(model.area < 0 && !model.clothing, "Select an individual model to edit its skeleton");
    auto link = model_link(model);
    auto source = model.sources.at(link.source);
    auto members = compiled_members();
    auto found = members.find(source.member);
    auto member = found == members.end() ? source.original : found->second;
    auto original = asset_resource(member, link.path);
    auto bytes = replace_skeleton(original, joints);
    if (bytes == original)
        return;
    members[source.member] = replace_asset_resource(member, link.path, bytes);
    commit();
    replace_members(members);
}
void MaterialDocument::edit_visibility_motion(std::size_t index,
                                              const VisibilityMotion &replacement) {
    require(!model.shadow_model, "Edit shared motions on the main model");
    require(model.area < 0 && !model.clothing, "Select an individual model motion");
    auto &motion = model.motions.at(index);
    require(motion.error.empty(), motion.error);
    auto link = model.resources.at(motion.resource);
    auto source = model.sources.at(link.source);
    auto known = [&](const VisibilityTrack &track) {
        return std::any_of(model.scene->draws.begin(), model.scene->draws.end(), [&](auto &draw) {
            return draw.mesh == track.mesh;
        });
    };
    for (auto &track : replacement.tracks)
        require(known(track) ||
                    std::find(motion.visibility.tracks.begin(), motion.visibility.tracks.end(),
                              track) != motion.visibility.tracks.end(),
                "Choose a mesh in this model; unmatched source tracks must remain unchanged");
    for (auto &track : motion.visibility.tracks)
        if (!known(track))
            require(std::find(replacement.tracks.begin(), replacement.tracks.end(), track) !=
                        replacement.tracks.end(),
                    "Unmatched source visibility tracks must remain unchanged");
    auto members = compiled_members();
    auto found = members.find(source.member);
    auto member = found == members.end() ? source.original : found->second;
    auto original = asset_resource(member, link.path);
    auto bytes = replace_visibility_motion(original, replacement);
    if (bytes == original)
        return;
    members[source.member] = replace_asset_resource(member, link.path, bytes);
    commit();
    model.select_motion(int(index));
    replace_members(members);
}
void MaterialDocument::edit_geometry(const SkinnedModel &replacement) {
    auto link = model_link(model);
    auto source = model.sources.at(link.source);
    auto current = geometry();
    require(current.model.original == replacement.model.original,
            "The model changed; reload the geometry selection before applying");
    auto members = compiled_members();
    auto found = members.find(source.member);
    auto member = found == members.end() ? source.original : found->second;
    auto original = asset_resource(member, link.path);
    auto bytes = replace_mesh_geometry(original, replacement);
    if (bytes == original)
        return;
    members[source.member] = replace_asset_resource(member, link.path, bytes);
    commit();
    replace_members(members);
}
void MaterialDocument::edit_skeletal_motion(std::size_t index, const SkeletalMotion &replacement) {
    require(!model.shadow_model, "Edit shared motions on the main model");
    require(model.area < 0 && !model.clothing, "Select an individual model motion");
    auto &motion = model.motions.at(index);
    require(motion.error.empty(), motion.error);
    auto link = model.resources.at(motion.resource);
    auto &source = model.sources.at(link.source);
    auto known = [&](const JointTrack &track) {
        for (auto &rig : model.scene->skeletons)
            for (auto &joint : rig.joints)
                if (joint.name == track.name)
                    return true;
        return false;
    };
    for (auto &track : replacement.tracks)
        require(known(track) ||
                    std::find(motion.skeletal.tracks.begin(), motion.skeletal.tracks.end(),
                              track) != motion.skeletal.tracks.end(),
                "Choose a bone in this model; unmatched source tracks must remain unchanged");
    for (auto &track : motion.skeletal.tracks)
        if (!known(track))
            require(std::find(replacement.tracks.begin(), replacement.tracks.end(), track) !=
                        replacement.tracks.end(),
                    "Unmatched source bone tracks must remain unchanged");
    auto members = compiled_members();
    auto it = members.find(source.member);
    auto member = it == members.end() ? source.original : it->second;
    auto original = asset_resource(member, link.path);
    auto bytes = replace_skeletal_motion(original, replacement);
    if (bytes == original)
        return;
    members[source.member] = replace_asset_resource(member, link.path, bytes);
    commit();
    model.select_motion(int(index));
    replace_members(members);
}
void MaterialDocument::edit_material_motion(std::size_t index, const MaterialMotion &replacement) {
    require(!model.shadow_model, "Edit shared motions on the main model");
    if (model.area >= 0) {
        auto &link = model.resources.at(model.motions.at(index).resource);
        auto original = asset_resource(model.sources.at(link.source).original, link.path);
        auto bytes = replace_material_motion(original, replacement);
        set_map_motion(index, std::move(bytes));
        commit();
        return;
    }
    auto &motion = model.motions.at(index);
    require(motion.error.empty(), motion.error);
    auto link = model.resources.at(motion.resource);
    auto &source = model.sources.at(link.source);
    auto known_material = [&](const MaterialTrack &track) {
        return std::any_of(model.scene->materials.begin(), model.scene->materials.end(),
                           [&](auto &m) {
                               return m.name == track.material;
                           });
    };
    for (auto &track : motion.material.tracks)
        if (!known_material(track))
            require(std::find(replacement.tracks.begin(), replacement.tracks.end(), track) !=
                        replacement.tracks.end(),
                    "Unmatched source material tracks must remain unchanged");
    for (auto &track : replacement.tracks) {
        if (!known_material(track)) {
            require(std::find(motion.material.tracks.begin(), motion.material.tracks.end(),
                              track) != motion.material.tracks.end(),
                    "Choose a material in this model");
            continue;
        }
        if (track.kind != MaterialTrack::Kind::ConstantColor) {
            auto material = std::find_if(model.scene->materials.begin(),
                                         model.scene->materials.end(), [&](auto &m) {
                                             return m.name == track.material;
                                         });
            require(track.slot < 3 && !material->texture_inputs.at(track.slot).empty(),
                    "Choose a texture unit already used by this material");
        }
        for (auto &key : track.textures)
            require(model.scene->textures.contains(model.texture_prefix + key.texture),
                    "Texture is not loaded by this model");
    }
    auto members = compiled_members();
    auto it = members.find(source.member);
    auto member = it == members.end() ? source.original : it->second;
    auto original = asset_resource(member, link.path);
    auto bytes = replace_material_motion(original, replacement);
    if (bytes == original)
        return;
    members[source.member] = replace_asset_resource(member, link.path, bytes);
    commit();
    model.select_motion(int(index));
    replace_members(members);
}

std::string MaterialDocument::serialize() const {
    if (!structural_)
        return serialize_materials();
    std::ostringstream out;
    out << "USUMSTUDIO_EFFECT 1\nidentity " << std::quoted(identity()) << "\n";
    for (auto &[index, original] : structural_->originals) {
        auto it = std::find_if(model.sources.begin(), model.sources.end(), [&](auto &s) {
            return s.member == index && s.archive == archive_path();
        });
        auto &bytes = it == model.sources.end() ? original : it->original;
        out << "member " << index << ' ' << sha256(original) << ' ' << std::quoted(hex_bytes(bytes))
            << '\n';
    }
    out << "materials " << std::quoted(serialize_materials()) << '\n';
    return out.str();
}
std::string MaterialDocument::serialize_materials() const {
    auto &link = model_link(model);
    auto &source = model.sources.at(link.source);
    std::ostringstream out;
    out << "USUMSTUDIO_MATERIALS " << 11 << "\nidentity " << std::quoted(identity()) << "\nsource "
        << sha256(asset_resource(source.original, link.path)) << "\nstructure "
        << structure_hash(asset_resource(source.original, link.path)) << '\n'
        << std::setprecision(9);
    for (auto index : touched_map_motions_) {
        auto &link = model.resources.at(model.motions.at(index).resource);
        auto original = asset_resource(model.sources.at(link.source).original, link.path);
        auto found = map_motion_edits_.find(index);
        out << "map_motion " << index << ' ' << sha256(original) << ' '
            << std::quoted(hex_bytes(found == map_motion_edits_.end() ? original : found->second))
            << '\n';
    }
    if (!touched_camera_.empty()) {
        auto row = initial_feeding_->camera_row;
        auto &original = initial_feeding_->camera_original;
        auto written = original;
        for (auto slot : touched_camera_)
            written =
                replace_refresh_camera(written, row, slot, model.refresh_feeding->cameras[slot]);
        for (auto slot : touched_camera_) {
            auto &camera = model.refresh_feeding->cameras[slot];
            if (touched_camera_ == std::set<unsigned>{9})
                out << "feeding_camera ";
            else
                out << "refresh_camera " << slot << ' ';
            out << row << ' ' << sha256(refresh_feeding_record(original, row, 126)) << ' '
                << sha256(refresh_feeding_record(written, row, 126));
            for (auto &point : {camera.position, camera.focus})
                for (int value : point)
                    out << ' ' << value;
            out << '\n';
        }
    }
    if (touched_feeding_) {
        auto &p = model.refresh_feeding->parameters;
        out << "feeding " << initial_feeding_->row << ' '
            << sha256(refresh_feeding_record(initial_feeding_->original, initial_feeding_->row))
            << ' '
            << sha256(refresh_feeding_record(
                   replace_refresh_feeding(initial_feeding_->original, initial_feeding_->row, p),
                   initial_feeding_->row))
            << ' ' << p.distance << ' ' << p.scale_percent << ' ' << p.head_angle << ' '
            << p.animation_count << '\n';
    }
    for (auto mask : touched_refresh_) {
        auto &resource = model.resources.at(model.refresh_resources.at(mask));
        auto &source = model.sources.at(resource.source);
        out << "refresh " << mask << ' ' << source.member << ' '
            << initial_refresh_->masks.at(mask).child << ' ' << source.hash << ' '
            << std::quoted(hex_bytes(model.refresh_regions->masks.at(mask).ids)) << '\n';
    }
    for (auto &name : touched_textures_) {
        auto it = texture_edits_.find(name);
        out << "image " << std::quoted(name) << ' '
            << std::quoted(it == texture_edits_.end() ? std::string{} : hex_bytes(*it->second))
            << '\n';
    }
    for (unsigned i = 0; i < edits_.size(); ++i)
        if (touched_.contains(i)) {
            auto &e = edits_[i];
            out << "material " << i << ' ' << std::quoted(model.scene->materials[i].name) << ' '
                << e.cull << ' ' << e.alpha_function << ' ' << e.alpha_reference << ' ' << e.blend
                << ' ' << e.depth << ' ' << e.fragment_lighting << '\n';
            out << "outlines " << e.edge_type << ' ' << e.id_edge_enabled << ' ' << e.edge_id << ' '
                << e.edge_alpha_mask << '\n';
            out << "draw_order " << e.layer << ' ' << e.priority << '\n';
            for (auto &c : e.colors) {
                out << "color";
                for (float v : c)
                    out << ' ' << v;
                out << '\n';
            }
            for (auto &t : e.textures) {
                out << "texture " << std::quoted(t.name);
                for (float v : t.transform)
                    out << ' ' << v;
                out << ' ' << t.wrap_u << ' ' << t.wrap_v << ' ' << t.min_filter << ' '
                    << t.mag_filter << '\n';
            }
            out << "combiners " << bool(e.combiners) << '\n';
            if (e.combiners) {
                auto &c = *e.combiners;
                out << c.buffer << ' ' << c.buffer_write << '\n';
                for (unsigned stage = 0; stage < 6; ++stage) {
                    out << c.assignments[stage];
                    for (auto v : c.stages[stage])
                        out << ' ' << v;
                    out << '\n';
                }
            }
            out << "borrowed_shader " << std::quoted(e.shader_origin) << '\n';
        }
    return out.str();
}
void MaterialDocument::restore(const std::string &text) {
    if (text.starts_with("USUMSTUDIO_EFFECT ")) {
        std::istringstream input(text);
        std::string word, id;
        unsigned version;
        require(bool(input >> word >> version) && version == 1,
                "Unsupported effect document version");
        require(bool(input >> word >> std::quoted(id)) && word == "identity" && id == identity(),
                "Open the originating model before loading these edits");
        std::map<std::size_t, Bytes> members;
        auto session = std::make_shared<StructuralSession>();
        Archive archive(model.archive_sources.resolve(model.dump, archive_path()));
        std::string inner;
        while (input >> word) {
            if (word == "materials") {
                require(bool(input >> std::quoted(inner)), "Missing material edits");
                break;
            }
            std::size_t index;
            std::string hash, data;
            require(word == "member" && bool(input >> index >> hash >> std::quoted(data)) &&
                        !members.contains(index),
                    "Malformed effect member");
            bool allowed = index == model.pokemon.texture_member +
                                        TargetProfile::pokemon_refresh_texture_slot ||
                           index == model.pokemon.model_member ||
                           index == model.pokemon.texture_member + 1 ||
                           index == model.pokemon.texture_member + 2;
            for (auto slot : TargetProfile::pokemon_motion_slots)
                allowed |= index == model.pokemon.motion_member + slot;
            if (!model.is_pokemon())
                allowed =
                    std::any_of(model.sources.begin(), model.sources.end(), [&](auto &source) {
                        return source.member == index && source.archive == archive_path();
                    });
            require(allowed, "Edited resource does not belong to this model");
            auto source = std::find_if(model.sources.begin(), model.sources.end(), [&](auto &s) {
                return s.member == index && s.archive == archive_path();
            });
            auto original =
                source == model.sources.end() ? archive.decoded(index) : source->original;
            require(sha256(original) == hash,
                    "The effect's source differs from this dump; open its original model");
            members[index] = unhex(data);
            session->originals[index] = original;
            session->accepted[index] = {hash, sha256(members[index])};
        }
        require(!inner.empty() && !(input >> word), "Invalid effect document ending");
        MaterialDocument next(reload_editable_model(model, members));
        next.restore(inner);
        session->saved = std::make_shared<MaterialDocument>(*this);
        next.structural_parent_ = std::make_shared<MaterialDocument>(*this);
        structural_ = session;
        next.structural_version_ = session->next_version++;
        adopt_structure(std::move(next));
        mark_saved();
        return;
    }
    auto previous_map_motions = map_motion_edits_;
    auto previous_touched_map_motions = touched_map_motions_;
    auto previous_camera = camera_edit_;
    auto previous_touched_camera = touched_camera_;
    auto previous_feeding = feeding_edit_;
    auto previous_touched_feeding = touched_feeding_;
    auto previous_refresh = refresh_edits_;
    auto previous_touched_refresh = touched_refresh_;
    auto saved = edits_;
    auto saved_images = texture_edits_;
    auto touched_images = touched_textures_;
    auto touched = touched_;
    std::istringstream in(text);
    std::string word, id, hash;
    unsigned version = 0;
    require(bool(in >> word >> version) && word == "USUMSTUDIO_MATERIALS" &&
                (version >= 1 && version <= 11),
            "Unsupported Studio material document");
    require(bool(in >> word >> std::quoted(id)) && word == "identity" && id == identity(),
            "Open the originating asset before loading this document");
    auto &link = model_link(model);
    require(bool(in >> word >> hash) && word == "source", "Missing document source");
    auto original = asset_resource(model.sources.at(link.source).original, link.path);
    if (version == 1)
        require(hash == sha256(original), "The asset source differs from this document");
    else {
        std::string structure;
        require(bool(in >> word >> structure) && word == "structure" &&
                    structure == structure_hash(original),
                "The model geometry or resource layout differs from this document");
    }
    std::set<unsigned> seen;
    try {
        map_motion_edits_.clear();
        std::set<std::size_t> map_motion_seen;
        camera_edit_.clear();
        std::set<unsigned> camera_seen;
        feeding_edit_.reset();
        bool feeding_seen = false;
        edits_ = initial_;
        texture_edits_.clear();
        refresh_edits_.clear();
        touched_.clear();
        std::set<std::string> images;
        std::set<std::size_t> refresh_seen;
        while (in >> word) {
            if (version >= 10 && word == "map_motion") {
                std::size_t index;
                std::string hash, data;
                require(bool(in >> index >> hash >> std::quoted(data)) && model.area >= 0 &&
                            index < model.motions.size() && map_motion_seen.insert(index).second,
                        "Invalid or duplicate map motion record");
                auto &link = model.resources.at(model.motions.at(index).resource);
                require(sha256(asset_resource(model.sources.at(link.source).original, link.path)) ==
                            hash,
                        "Map motion source differs from this document");
                set_map_motion(index, unhex(data));
                continue;
            }
            if ((version >= 8 && word == "feeding_camera") ||
                (version >= 9 && word == "refresh_camera")) {
                unsigned slot = 9;
                if (word == "refresh_camera")
                    require(bool(in >> slot) && slot < 10, "Invalid Refresh camera slot");
                std::size_t row;
                std::string hash, written_hash;
                RefreshFeedingCamera camera;
                require(camera_seen.insert(slot).second && bool(in >> row >> hash >> written_hash),
                        "Invalid or duplicate feeding camera record");
                for (auto *point : {&camera.position, &camera.focus})
                    for (int &value : *point)
                        require(bool(in >> value), "Missing feeding camera coordinate");
                require(initial_feeding_ && !initial_feeding_->camera_default &&
                            row == initial_feeding_->camera_row,
                        "Feeding camera row differs from this document");
                auto current =
                    sha256(refresh_feeding_record(initial_feeding_->camera_original, row, 126));
                require(current == hash || current == written_hash,
                        "Feeding camera source differs from this document");
                preview_refresh_camera(slot, camera);
                continue;
            }
            if (version >= 7 && word == "feeding") {
                std::size_t row;
                std::string hash, written_hash;
                RefreshFeedingParameters p;
                require(!feeding_seen && bool(in >> row >> hash >> written_hash >> p.distance >>
                                              p.scale_percent >> p.head_angle >> p.animation_count),
                        "Invalid or duplicate feeding record");
                require(initial_feeding_ && initial_feeding_->available &&
                            row == initial_feeding_->row,
                        "Feeding parameter row differs from this document");
                auto current_hash = sha256(refresh_feeding_record(initial_feeding_->original, row));
                require(current_hash == hash || current_hash == written_hash,
                        "Feeding parameter source differs from this document");
                feeding_seen = true;
                preview_feeding(p);
                continue;
            }
            if (version >= 6 && word == "refresh") {
                std::size_t mask, member, child;
                std::string hash, data;
                require(bool(in >> mask >> member >> child >> hash >> std::quoted(data)) &&
                            mask < model.refresh_resources.size() &&
                            refresh_seen.insert(mask).second,
                        "Invalid or duplicate Refresh mask");
                auto &resource = model.resources.at(model.refresh_resources.at(mask));
                auto &source = model.sources.at(resource.source);
                require(source.member == member && initial_refresh_ &&
                            initial_refresh_->masks.at(mask).child == child && source.hash == hash,
                        "Refresh source differs from this document");
                preview_refresh(mask, unhex(data));
                touched_refresh_.insert(mask);
                continue;
            }
            if (version >= 4 && word == "image") {
                std::string name, data;
                require(bool(in >> std::quoted(name) >> std::quoted(data)) &&
                            model.texture_resources.contains(name) && images.insert(name).second,
                        "Invalid or duplicate texture replacement");
                if (!data.empty()) {
                    auto bytes = unhex(data);
                    auto &link = model.resources.at(model.texture_resources.at(name));
                    auto original =
                        asset_resource(model.sources.at(link.source).original, link.path);
                    require(studio::text(slice(bytes, 40, 64)) ==
                                studio::text(slice(original, 40, 64)),
                            "Texture replacement name differs");
                    decode_field_texture(bytes);
                    texture_edits_[name] = std::make_shared<const Bytes>(std::move(bytes));
                }
                touched_textures_.insert(name);
                continue;
            }
            unsigned index;
            std::string name;
            MaterialEdit e;
            require(word == "material" &&
                        bool(in >> index >> std::quoted(name) >> e.cull >> e.alpha_function >>
                             e.alpha_reference >> e.blend >> e.depth >> e.fragment_lighting),
                    "Malformed material record");
            require(index < edits_.size() && model.scene->materials[index].name == name &&
                        seen.insert(index).second,
                    "Invalid or duplicate material");
            e.edge_type = initial_[index].edge_type;
            e.id_edge_enabled = initial_[index].id_edge_enabled;
            e.edge_id = initial_[index].edge_id;
            e.edge_alpha_mask = initial_[index].edge_alpha_mask;
            if (version >= 5)
                require(bool(in >> word >> e.edge_type >> e.id_edge_enabled >> e.edge_id >>
                             e.edge_alpha_mask) &&
                            word == "outlines",
                        "Missing material outline settings");
            e.layer = initial_[index].layer;
            e.priority = initial_[index].priority;
            if (version >= 11)
                require(bool(in >> word >> e.layer >> e.priority) && word == "draw_order",
                        "Missing material draw order");
            for (auto &c : e.colors) {
                require(bool(in >> word) && word == "color", "Missing material color");
                for (auto &v : c)
                    require(bool(in >> v), "Malformed color");
            }
            for (auto &t : e.textures) {
                require(bool(in >> word >> std::quoted(t.name)) && word == "texture",
                        "Missing material texture");
                for (auto &v : t.transform)
                    require(bool(in >> v), "Malformed UV transform");
                require(bool(in >> t.wrap_u >> t.wrap_v >> t.min_filter >> t.mag_filter),
                        "Malformed texture state");
            }
            if (version >= 3) {
                bool present;
                require(bool(in >> word >> present) && word == "combiners",
                        "Missing combiner state");
                if (present) {
                    CombinerSettings c;
                    require(bool(in >> c.buffer >> c.buffer_write), "Malformed combiner buffer");
                    for (unsigned stage = 0; stage < 6; ++stage) {
                        require(bool(in >> c.assignments[stage]), "Missing combiner constant");
                        for (auto &v : c.stages[stage])
                            require(bool(in >> v), "Malformed combiner stage");
                    }
                    e.combiners = c;
                }
            }
            if (version >= 4)
                require(bool(in >> word >> std::quoted(e.shader_origin)) &&
                            word == "borrowed_shader",
                        "Missing shader origin");
            preview(index, e);
            touched_.insert(index);
        }
        synchronize();
        commit();
        mark_saved();
    } catch (...) {
        map_motion_edits_ = previous_map_motions;
        touched_map_motions_.insert(previous_touched_map_motions.begin(),
                                    previous_touched_map_motions.end());
        camera_edit_ = previous_camera;
        touched_camera_ = previous_touched_camera;
        feeding_edit_ = previous_feeding;
        touched_feeding_ = previous_touched_feeding;
        refresh_edits_ = previous_refresh;
        touched_refresh_ = previous_touched_refresh;
        edits_ = saved;
        texture_edits_ = saved_images;
        touched_textures_.insert(touched_images.begin(), touched_images.end());
        touched_ = touched;
        synchronize();
        touched_textures_ = touched_images;
        touched_map_motions_ = previous_touched_map_motions;
        throw;
    }
}
Bytes MaterialDocument::compile() const {
    auto &link = model_link(model);
    auto original = asset_resource(model.sources.at(link.source).original, link.path);
    if (!changed())
        return original;
    auto parsed = Model::parse(original);
    Bytes result(original.begin(), original.begin() + 16);
    unsigned index = 0;
    for (auto &section : parsed.sections) {
        auto b = slice(original, section.offset, section.size);
        if (section.kind != "material") {
            append(result, b);
            continue;
        }
        auto &edit = edits_.at(index);
        auto &base = initial_.at(index);
        auto &scene = model.scene->materials.at(index++);
        if (edit == base) {
            append(result, b);
            continue;
        }
        auto p = metadata(b);
        Bytes material;
        std::size_t material_metadata = p;
        if (edit.combiners) {
            std::size_t name_at = 16;
            for (unsigned i = 0; i < 3; ++i)
                name_at += 5 + b[name_at + 4];
            append(material, slice(b, 0, name_at));
            named(material, fragment_name(edit));
            material_metadata = material.size();
            append(material, slice(b, p, 168));
        } else
            append(material, slice(b, 0, p + 168));
        for (unsigned i = 0; i < 6; ++i)
            put32(material, material_metadata + 24 + i * 4, color_word(edit.colors[i]));
        for (unsigned i = 0; i < 3; ++i)
            put32(material, material_metadata + 60 + i * 4, color_word(edit.colors[i + 6]));
        if (edit.combiners)
            for (unsigned i = 0; i < 6; ++i)
                material[material_metadata + 17 + i] = std::uint8_t(edit.combiners->assignments[i]);
        material[material_metadata + 49] = edit.fragment_lighting ? 1 : 0;
        put32(material, material_metadata + 72, edit.edge_type);
        put32(material, material_metadata + 76, edit.id_edge_enabled ? 1 : 0);
        put32(material, material_metadata + 80, edit.edge_id);
        put32(material, material_metadata + 108, std::uint32_t(edit.edge_alpha_mask));
        p += 168;
        auto count = u32(b, p);
        append32(material, count);
        p += 4;
        for (unsigned i = 0; i < count; ++i) {
            auto length = b[p + 4];
            auto at = p + 5 + length;
            auto unit = b[at];
            auto &t = edit.textures.at(unit);
            auto name = t.name;
            auto prefix = scene.resource_scope;
            if (name.starts_with(prefix))
                name.erase(0, prefix.size());
            require(std::find(parsed.names[1].begin(), parsed.names[1].end(), name) !=
                        parsed.names[1].end(),
                    "Texture binding must reference the model's existing texture table");
            named(material, name);
            auto state = material.size();
            append(material, slice(b, at, 42));
            for (unsigned j = 0; j < 5; ++j)
                put_float(material, state + 2 + j * 4, t.transform[j]);
            put32(material, state + 22, t.wrap_u);
            put32(material, state + 26, t.wrap_v);
            put32(material, state + 30, t.mag_filter);
            put32(material, state + 34, t.min_filter);
            p = at + 42;
        }
        p = aligned(p, 16);
        material.resize(aligned(material.size(), 16), 255);
        auto header = material.size();
        append(material, slice(b, p, 32));
        put32(material, header + 4, std::uint32_t(edit.priority));
        put32(material, header + 12, std::uint32_t(edit.layer));
        Bytes stream(slice(b, p + 32, u32(b, p)).begin(), slice(b, p + 32, u32(b, p)).end());
        for (auto c : commands(stream))
            if (c.reg == 0x23d) {
                require(c.offset + 8 <= stream.size() &&
                            ((u32(stream, c.offset + 4) >> 20) & 0x7ff) == 0,
                        "Unsupported material command terminator");
                stream.resize(c.offset);
                break;
            }
        command(stream, 0x40, edit.cull);
        command(stream, 0x104,
                edit.alpha_function == 1
                    ? 0x10 | (edit.alpha_reference << 8)
                    : 1u | (edit.alpha_function << 4) | (edit.alpha_reference << 8));
        if (edit.blend != base.blend) {
            append32(stream, 0x100);
            append32(stream, 0x20100);
        }
        command(stream, 0x101, edit.blend);
        command(stream, 0x107, edit.depth);
        for (unsigned unit = 0; unit < 3; ++unit)
            if (edit.textures[unit].transform != base.textures[unit].transform) {
                SceneTexture texture = scene.inputs[unit];
                texture.transform = edit.textures[unit].transform;
                update_texture_transform(texture);
                auto u = texture.row_u, v = texture.row_v;
                uniform(stream, 1 + unit * 3, {u[0], u[1], 0, u[2]});
                uniform(stream, 2 + unit * 3, {v[0], v[1], 0, v[2]});
            }
        if (edit.combiners) {
            for (auto [reg, value] : combiner_registers(*edit.combiners)) {
                append32(stream, value);
                append32(stream, (reg == 0xe0 ? 0x20000u : 0xf0000u) | reg);
            }
            for (unsigned i = 0; i < 6; ++i) {
                unsigned base = i < 4 ? 0xc0 + i * 8 : 0xf0 + (i - 4) * 8;
                command(stream, base + 3, color_word(edit.colors[edit.combiners->assignments[i]]));
            }
        }
        if (stream.size() % 16 == 0)
            command(stream, 0, 0);
        command(stream, 0x23d, 1);
        put32(material, header, narrow(stream.size()));
        append(material, stream);
        material.resize(aligned(material.size(), 16), 0);
        put32(material, 8, narrow(material.size() - 16));
        append(result, material);
    }
    Model::parse(result);
    return result;
}
bool MaterialDocument::compatible_model(View bytes) const {
    auto &link = model_link(model);
    return structure_hash(bytes) ==
           structure_hash(asset_resource(model.sources.at(link.source).original, link.path));
}
std::filesystem::path MaterialDocument::archive_path() const {
    auto path = model.sources.at(model_link(model).source).archive;
    require(!path.is_absolute(), "Asset archive must be relative to the dump");
    for (auto &part : path)
        require(part != "..", "Invalid asset archive path");
    return path;
}
namespace {
std::string feeding_camera_key(const RefreshFeedingCamera &camera) {
    std::ostringstream out;
    for (auto &point : {camera.position, camera.focus})
        for (int value : point)
            out << value << ',';
    return out.str();
}
}
void MaterialDocument::write_targets(
    const std::map<std::filesystem::path, std::filesystem::path> &targets,
    bool replace_existing) const {
    require(write_count() > 0, "There are no edits to write");
    struct Pending {
        std::filesystem::path path, temp;
        bool existed;
        std::filesystem::file_time_type time;
        std::uintmax_t size;
    };
    std::vector<Pending> pending;
    struct Cleanup {
        std::vector<Pending> &entries;
        ~Cleanup() {
            for (auto &p : entries) {
                std::error_code error;
                std::filesystem::remove(p.temp, error);
            }
        }
    } cleanup{pending};
    auto &link = model_link(model);
    auto model_archive = archive_path();
    auto model_member = model.sources.at(link.source).member;
    auto compiled = compile();
    for (auto &[relative, destination] : targets) {
        require(!relative.is_absolute(), "Archive path must be relative");
        for (auto &part : relative)
            require(part != "..", "Invalid archive path");
        bool exists = std::filesystem::exists(destination);
        require(!exists || replace_existing, "Destination archive already exists");
        Archive archive(exists ? destination : model.archive_sources.resolve(model.dump, relative));
        auto subfile = [&](std::size_t index) {
            for (const auto &source : model.sources)
                if (source.archive == relative && source.member == index)
                    return source.subfile;
            return 0u;
        };
        std::map<std::size_t, Bytes> decoded;
        auto member = [&](std::size_t index) -> Bytes & {
            auto [it, added] = decoded.try_emplace(index);
            if (added)
                it->second = archive.decoded(index, subfile(index));
            return it->second;
        };
        if (structural_ && relative == model_archive) {
            auto replacements = compiled_members();
            for (auto &[index, bytes] : replacements) {
                auto current = archive.decoded(index, subfile(index));
                auto digest = sha256(current);
                require(structural_->accepted[index].contains(digest) || current == bytes,
                        "This model resource has other changes; use a fresh override folder or "
                        "reload that version before borrowing");
                decoded[index] = bytes;
            }
        }
        if ((!structural_ || model.area >= 0) && relative == model_archive) {
            auto current = structural_ && model.area >= 0
                               ? member(model_member)
                               : archive.decoded(model_member, subfile(model_member));
            auto target = asset_resource(current, link.path);
            require(compatible_model(target), "Destination model geometry or resource layout "
                                              "differs; choose the matching game archive");
            if (!touched_.empty()) {
                auto a = Model::parse(target), b = Model::parse(compiled);
                require(a.sections.size() == b.sections.size(),
                        "Destination material layout differs");
                Bytes merged(target.begin(), target.begin() + 16);
                unsigned material = 0;
                for (unsigned i = 0; i < a.sections.size(); ++i) {
                    bool replace = a.sections[i].kind == "material" && touched_.contains(material);
                    if (a.sections[i].kind == "material")
                        ++material;
                    auto &section = replace ? b.sections[i] : a.sections[i];
                    append(merged, slice(replace ? View(compiled) : View(target), section.offset,
                                         section.size));
                }
                Model::parse(merged);
                decoded[model_member] = replace_asset_resource(current, link.path, merged);
            }
        }
        for (auto index : touched_map_motions_) {
            auto &resource = model.resources.at(model.motions.at(index).resource);
            auto &source = model.sources.at(resource.source);
            if (source.archive == relative) {
                auto &bytes = member(source.member);
                bytes = merge_map_motion(index, bytes);
            }
        }
        if (!structural_)
            for (auto mask : touched_refresh_) {
                auto &resource = model.resources.at(model.refresh_resources.at(mask));
                auto &source = model.sources.at(resource.source);
                if (source.archive == relative) {
                    auto &bytes = member(source.member);
                    bytes = merge_refresh(mask, bytes);
                }
            }
        if (!structural_ || model.area >= 0)
            for (auto &name : touched_textures_) {
                auto &resource = model.resources.at(model.texture_resources.at(name));
                auto &source = model.sources.at(resource.source);
                if (source.archive != relative)
                    continue;
                auto original = asset_resource(source.original, resource.path);
                auto &current = member(source.member);
                auto target = asset_resource(current, resource.path);
                require(text(slice(target, 40, 64)) == text(slice(original, 40, 64)),
                        "Destination texture differs from this asset");
                decode_field_texture(target);
                auto it = texture_edits_.find(name);
                current = replace_asset_resource(current, resource.path,
                                                 it == texture_edits_.end() ? View(original)
                                                                            : View(*it->second));
            }
        if (!structural_ || model.area >= 0)
            for (unsigned i = 0; i < edits_.size(); ++i)
                if (edits_[i].combiners) {
                    auto &resource = fragment_link(i);
                    auto &source = model.sources.at(resource.source);
                    if (source.archive != relative)
                        continue;
                    require(!resource.path.empty(),
                            "The fragment resource needs a containing resource pack");
                    auto path = resource.path;
                    path.pop_back();
                    auto &current = member(source.member);
                    auto parent = asset_resource(current, path);
                    auto original_parent = asset_resource(source.original, path);
                    require((u32(parent, 0) == 0x10000) == (u32(original_parent, 0) == 0x10000),
                            "Destination shader pack layout differs");
                    if (u32(parent, 0) != 0x10000)
                        require(parent[0] == original_parent[0] && parent[1] == original_parent[1],
                                "Destination shader container differs");
                    auto shader = fragment_resource(i);
                    current = replace_asset_resource(
                        current, path, upsert_fragment(parent, fragment_name(edits_[i]), shader));
                }
        if (touched_feeding_ && relative == TargetProfile::refresh_parameters_archive) {
            auto &current = member(TargetProfile::refresh_options_member);
            auto &source = *initial_feeding_;
            auto row = refresh_parameter_row(current, 32, source.species, source.form, source.sex);
            require(row && *row == source.row, "Destination feeding row identity differs");
            auto offset = u32(current, 4 + *row * 4);
            require(u16(current, offset) == source.species && current[offset + 2] == source.form &&
                        current[offset + 3] == source.sex,
                    "Destination feeding variant differs");
            auto present = refresh_feeding_parameters(current, *row);
            auto desired = model.refresh_feeding->parameters;
            auto signature = [](const RefreshFeedingParameters &p) {
                return std::to_string(p.distance) + "," + std::to_string(p.scale_percent) + "," +
                       std::to_string(p.head_angle) + "," + std::to_string(p.animation_count);
            };
            require(present == source.parameters || present == desired ||
                        accepted_feeding_.contains(signature(present)),
                    "This feeding row has other changes; reload that version or use a fresh "
                    "override folder");
            current = replace_refresh_feeding(current, *row, desired);
        }
        if (!touched_camera_.empty() && relative == TargetProfile::refresh_parameters_archive) {
            auto &current = member(TargetProfile::refresh_cameras_member);
            auto &source = *initial_feeding_;
            auto original_row =
                refresh_feeding_record(source.camera_original, source.camera_row, 126);
            auto row = refresh_parameter_row(current, 126, u16(original_row, 0), original_row[2],
                                             original_row[3]);
            require(row && *row == source.camera_row, "Destination feeding camera row differs");
            auto target_row = refresh_feeding_record(current, *row, 126);
            require(std::equal(original_row.begin(), original_row.begin() + 4, target_row.begin()),
                    "Destination feeding camera variant differs");
            for (auto slot : touched_camera_) {
                auto present = refresh_camera(current, *row, slot);
                auto desired = model.refresh_feeding->cameras[slot];
                require(present == source.cameras[slot] || present == desired ||
                            accepted_camera_[slot].contains(feeding_camera_key(present)),
                        "This Refresh camera has other changes; reload that version or use a fresh "
                        "override folder");
                current = replace_refresh_camera(current, *row, slot, desired);
            }
        }
        if (decoded.empty())
            continue;
        std::map<std::pair<std::size_t, unsigned>, Bytes> replacements;
        for (auto &[index, bytes] : decoded) {
            auto raw = archive.raw(index, subfile(index));
            bool compressed = !raw.empty() && (raw[0] == 0x11 || raw[0] == 0x10);
            replacements[{index, subfile(index)}] = compressed ? compress(bytes) : std::move(bytes);
        }
        auto temp = destination;
        temp += ".studio-writing";
        require(!std::filesystem::exists(temp),
                "An unfinished archive write exists: " + temp.string());
        pending.push_back({destination, temp, exists,
                           exists ? std::filesystem::last_write_time(destination)
                                  : std::filesystem::file_time_type{},
                           exists ? std::filesystem::file_size(destination) : 0});
        archive.export_subfiles(temp, replacements);
    }
    for (auto &p : pending)
        require(std::filesystem::exists(p.path) == p.existed &&
                    (!p.existed || (std::filesystem::last_write_time(p.path) == p.time &&
                                    std::filesystem::file_size(p.path) == p.size)),
                "Destination changed during verification; retry the write");
    std::size_t written = 0;
    try {
        for (auto &p : pending) {
            replace_file(p.temp, p.path);
            ++written;
        }
        for (auto index : touched_map_motions_) {
            auto &link = model.resources.at(model.motions.at(index).resource);
            auto &source = model.sources.at(link.source);
            Archive written(targets.at(source.archive));
            accepted_map_motions_[index].insert(
                sha256(asset_resource(written.decoded(source.member, source.subfile), link.path)));
        }
        for (auto slot : touched_camera_)
            accepted_camera_[slot].insert(feeding_camera_key(model.refresh_feeding->cameras[slot]));
        if (touched_feeding_) {
            auto &p = model.refresh_feeding->parameters;
            accepted_feeding_.insert(
                std::to_string(p.distance) + "," + std::to_string(p.scale_percent) + "," +
                std::to_string(p.head_angle) + "," + std::to_string(p.animation_count));
        }
        for (auto mask : touched_refresh_)
            accepted_refresh_[mask].insert(sha256(model.refresh_regions->masks.at(mask).ids));
        if (structural_)
            for (auto &[index, bytes] : compiled_members())
                structural_->accepted[index].insert(sha256(bytes));
    } catch (const std::exception &e) {
        throw std::runtime_error(std::to_string(written) +
                                 " archive(s) updated before replacement failed: " + e.what());
    }
}
void MaterialDocument::write_archive(const std::filesystem::path &destination,
                                     bool replace_existing) const {
    require(!model.project_asset, "Save this asset to its composition, then stage or build the project");
    require(archive_paths().size() == 1,
            "These edits span multiple archives; choose an override folder or working dump");
    write_targets({{*archive_paths().begin(), destination}}, replace_existing);
}
void MaterialDocument::write_to_dump(const std::filesystem::path &dump) const {
    require(!model.project_asset, "Save this asset to its composition, then stage or build the project");
    std::map<std::filesystem::path, std::filesystem::path> targets;
    for (auto &path : archive_paths()) {
        require(std::filesystem::is_regular_file(dump / path),
                "The working dump is missing archive: " + path.string());
        targets[path] = dump / path;
    }
    write_targets(targets, true);
}
void MaterialDocument::export_to(const std::filesystem::path &folder, bool replace_existing) const {
    require(!model.project_asset, "Save this asset to its composition, then stage or build the project");
    auto relative = std::filesystem::weakly_canonical(folder).lexically_relative(
        std::filesystem::weakly_canonical(model.dump));
    require(relative.empty() || *relative.begin() == "..",
            "Use Write to working dump for this destination");
    std::map<std::filesystem::path, std::filesystem::path> targets;
    for (auto &path : archive_paths())
        targets[path] = folder / path;
    write_targets(targets, replace_existing);
    try {
        auto receipt = folder / "studio-edits";
        std::filesystem::create_directories(receipt);
        auto key = identity();
        auto name = model.name;
        for (auto &c : name)
            if (static_cast<unsigned char>(c) < 32 ||
                std::string(R"(<>:"/\|?*)").find(c) != std::string::npos)
                c = '_';
        if (name.empty())
            name = "materials";
        auto filename = name + "-" +
                        sha256(View(reinterpret_cast<const std::uint8_t *>(key.data()), key.size()))
                            .substr(0, 8);
        auto saved = serialize();
        write_file_atomic(receipt / (filename + ".usum-material"),
                          View(reinterpret_cast<const std::uint8_t *>(saved.data()), saved.size()));
    } catch (const std::exception &e) {
        throw std::runtime_error("Archive was updated, but its edit receipt could not be saved: " +
                                 std::string(e.what()));
    }
}
void MaterialDocument::apply_to(Environment &scene) const {
    apply_map_motions(scene);
    for (auto &name : touched_textures_)
        if (scene.textures.contains(name)) {
            scene.textures[name] = model.scene->textures.at(name);
            if (auto it = texture_edits_.find(name); it != texture_edits_.end())
                scene.texture_overrides[name] = it->second;
            else
                scene.texture_overrides.erase(name);
        }
    std::set<std::size_t> targets;
    auto &link = model_link(model);
    auto &origin = model.sources.at(link.source);
    if (model.area >= 0) {
        for (auto &draw : scene.draws)
            if (draw.source && draw.source->archive == origin.archive &&
                draw.source->member == origin.member && draw.source->path == link.path)
                targets.insert(draw.material);
    } else
        for (unsigned i = 0; i < scene.materials.size(); ++i)
            targets.insert(i);
    for (auto index : targets) {
        auto &target = scene.materials.at(index);
        for (unsigned i = 0; i < model.scene->materials.size(); ++i)
            if (touched_.contains(i) &&
                target.resource_scope == model.scene->materials[i].resource_scope &&
                target.name == model.scene->materials[i].name)
                apply(target, edits_[i]);
    }
}
}
