#include "native/tutorial_widgets.h"
#include "native/material_editor.h"
#include "formats/texture_codec.h"
#include "assets/pokemon_motions.h"
#include "native/imgui_renderer.h"
#include "native/texture_uv_preview.h"
#include <imgui.h>
#include <SDL3/SDL.h>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cmath>
namespace studio {
namespace {
TextureImage read_png(const std::filesystem::path &path) {
    auto bytes = read_file(path);
    require(bytes.size() >= 24 && bytes.size() <= 16 * 1024 * 1024 && u32(bytes, 0) == 0x474e5089,
            "Choose a PNG texture");
    auto big = [&](unsigned p) {
        return (unsigned(bytes[p]) << 24) | (unsigned(bytes[p + 1]) << 16) |
               (unsigned(bytes[p + 2]) << 8) | bytes[p + 3];
    };
    auto width = big(16), height = big(20);
    auto valid = [](unsigned n) {
        return n >= 8 && n <= 1024 && (n & (n - 1)) == 0;
    };
    require(valid(width) && valid(height), "PNG dimensions must be powers of two from 8 to 1024");
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> source(
        SDL_LoadPNG_IO(SDL_IOFromConstMem(bytes.data(), bytes.size()), true), SDL_DestroySurface);
    require(bool(source), "PNG decode failed: " + std::string(SDL_GetError()));
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
        SDL_ConvertSurface(source.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    require(bool(rgba), "Cannot convert PNG pixels");
    require(unsigned(rgba->w) == width && unsigned(rgba->h) == height,
            "PNG dimensions differ from header");
    TextureImage image{std::uint16_t(width), std::uint16_t(height), Bytes(width * height * 4)};
    for (unsigned y = 0; y < height; ++y)
        std::memcpy(image.rgba.data() + y * width * 4,
                    static_cast<const std::uint8_t *>(rgba->pixels) + y * rgba->pitch, width * 4);
    return image;
}
struct CombinerChoice {
    unsigned value;
    const char *name;
};
bool combiner_choice(const char *label, std::uint32_t &word, unsigned shift, unsigned mask,
                     std::initializer_list<CombinerChoice> choices) {
    auto value = (word >> shift) & mask;
    std::string current = "Authored value " + std::to_string(value);
    for (auto choice : choices)
        if (choice.value == value)
            current = choice.name;
    bool changed = false;
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##value", current.c_str())) {
        for (auto choice : choices)
            if (ImGui::Selectable(choice.name, choice.value == value)) {
                word = (word & ~(mask << shift)) | (choice.value << shift);
                changed = true;
            }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}
bool combiners_editor(MaterialEdit &edit, const SceneMaterial &material) {
    if (!studio::TutorialWidgets::CollapsingHeader("material_editor", "Texture combiners"))
        return false;
    ImGui::TextWrapped("Six stages mix textures, lighting and colors. RGB and alpha are "
                       "independent; the final stage becomes the material output.");
    if (studio::TutorialWidgets::Button("material_editor", "Restore authored combiners")) {
        edit.combiners.reset();
        edit.shader_origin.clear();
        return true;
    }
    auto settings = edit.combiners.value_or(material.authored_combiners);
    bool changed = false;
    static const char *expressions[] = {"A",
                                        "A * B",
                                        "A + B",
                                        "A + B - 0.5",
                                        "A * C + B * (1 - C)",
                                        "A - B",
                                        "4 * dot(A - 0.5, B - 0.5)",
                                        "4 * dot(A - 0.5, B - 0.5), also alpha",
                                        "A * B + C",
                                        "min(A + B, 1) * C"};
    for (unsigned stage = 0; stage < 6; ++stage) {
        ImGui::PushID(int(stage));
        auto &words = settings.stages[stage];
        auto rgb = words[2] & 15, alpha = (words[2] >> 16) & 15;
        std::string title = "Stage " + std::to_string(stage + 1);
        if (ImGui::TreeNode(title.c_str())) {
            ImGui::TextWrapped("RGB: %s", rgb < 10 ? expressions[rgb] : "Unsupported operation");
            ImGui::TextWrapped("Alpha: %s", rgb == 7     ? "RGB dot product"
                                            : alpha < 10 ? expressions[alpha]
                                                         : "Unsupported operation");
            if (stage == 0)
                ImGui::TextWrapped(
                    "In the first stage, Previous reads input C instead of a preceding stage.");
            if (ImGui::BeginTabBar("Channels")) {
                for (unsigned channel = 0; channel < 2; ++channel)
                    if (studio::TutorialWidgets::BeginTabItem("material_editor",
                                                              channel ? "Alpha" : "RGB")) {
                        ImGui::PushID(int(channel));
                        unsigned shift = channel * 16;
                        changed |= combiner_choice("Operation", words[2], shift, 15,
                                                   {{0, "Replace"},
                                                    {1, "Multiply"},
                                                    {2, "Add"},
                                                    {3, "Add signed"},
                                                    {4, "Interpolate"},
                                                    {5, "Subtract"},
                                                    {6, "Dot product RGB"},
                                                    {7, "Dot product RGBA"},
                                                    {8, "Multiply then add"},
                                                    {9, "Add then multiply"}});
                        unsigned operation = (words[2] >> shift) & 15;
                        unsigned inputs = operation == 0                     ? 1
                                          : operation == 4 || operation >= 8 ? 3
                                                                             : 2;
                        if (channel && (words[2] & 15) == 7)
                            ImGui::TextWrapped("RGB dot product overrides this alpha result.");
                        bool previous_input = false;
                        for (unsigned input = 0; input < inputs; ++input)
                            previous_input |= ((words[0] >> (shift + input * 4)) & 15) == 15;
                        for (unsigned input = 0; input < 3; ++input) {
                            ImGui::PushID(int(input));
                            if (input < inputs || (stage == 0 && input == 2 && previous_input)) {
                                std::string name = "Input " + std::string(1, char('A' + input));
                                changed |=
                                    combiner_choice(name.c_str(), words[0], shift + input * 4, 15,
                                                    {{0, "Vertex / primary color"},
                                                     {1, "Primary lighting"},
                                                     {2, "Secondary lighting"},
                                                     {3, "Texture 0"},
                                                     {4, "Texture 1"},
                                                     {5, "Texture 2"},
                                                     {13, "Combiner buffer"},
                                                     {14, "Constant"},
                                                     {15, "Previous stage"}});
                                if (channel)
                                    changed |=
                                        combiner_choice("Component", words[1], 12 + input * 4, 7,
                                                        {{0, "Alpha"},
                                                         {1, "1 - alpha"},
                                                         {2, "Red"},
                                                         {3, "1 - red"},
                                                         {4, "Green"},
                                                         {5, "1 - green"},
                                                         {6, "Blue"},
                                                         {7, "1 - blue"}});
                                else
                                    changed |= combiner_choice("Component", words[1], input * 4, 15,
                                                               {{0, "RGB"},
                                                                {1, "1 - RGB"},
                                                                {2, "Alpha"},
                                                                {3, "1 - alpha"},
                                                                {4, "Red"},
                                                                {5, "1 - red"},
                                                                {8, "Green"},
                                                                {9, "1 - green"},
                                                                {12, "Blue"},
                                                                {13, "1 - blue"}});
                            }
                            ImGui::PopID();
                        }
                        changed |= combiner_choice("Output scale", words[3], shift, 3,
                                                   {{0, "1x"}, {1, "2x"}, {2, "4x"}});
                        ImGui::PopID();
                        ImGui::EndTabItem();
                    }
                ImGui::EndTabBar();
            }
            std::uint32_t assignment = settings.assignments[stage];
            if (combiner_choice("Constant color", assignment, 0, 255,
                                {{0, "Constant 0"},
                                 {1, "Constant 1"},
                                 {2, "Constant 2"},
                                 {3, "Constant 3"},
                                 {4, "Constant 4"},
                                 {5, "Constant 5"}})) {
                settings.assignments[stage] = assignment;
                changed = true;
            }
            if (assignment < 6) {
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::ColorEdit4("##stage-constant", edit.colors[assignment].data(),
                                             ImGuiColorEditFlags_AlphaBar);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Shared by every stage using this constant");
            }
            if (stage >= 1 && stage <= 4 &&
                studio::TutorialWidgets::TreeNode("material_editor", "Buffer writes")) {
                for (unsigned channel = 0; channel < 2; ++channel) {
                    unsigned bit = 1u << (7 + stage + channel * 4);
                    bool enabled = (settings.buffer_write & bit) != 0;
                    if (studio::TutorialWidgets::Checkbox(
                            "material_editor", channel ? "Write alpha" : "Write RGB", &enabled)) {
                        settings.buffer_write =
                            (settings.buffer_write & ~bit) | (enabled ? bit : 0);
                        changed = true;
                    }
                }
                ImGui::TextWrapped("Copies the preceding stage's result into the buffer for the "
                                   "next stage. Unwritten channels keep their earlier value.");
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (studio::TutorialWidgets::TreeNode("material_editor", "Initial buffer color")) {
        auto color = material_color(settings.buffer);
        if (ImGui::ColorEdit4("Buffer", color.data(), ImGuiColorEditFlags_AlphaBar)) {
            settings.buffer = 0;
            for (unsigned i = 0; i < 4; ++i)
                settings.buffer |= std::uint32_t(color[i] * 255.f + .5f) << (i * 8);
            changed = true;
        }
        ImGui::TextWrapped(
            "Stage 1 sees a zero buffer. This initial color is available from stage 2.");
        ImGui::TreePop();
    }
    if (!material.combiner.unsupported.empty())
        ImGui::TextWrapped("Preview limitation: %s", material.combiner.unsupported.c_str());
    if (changed)
        edit.combiners = settings;
    return changed;
}
}

MaterialEditor::~MaterialEditor() {
    if (export_.valid())
        export_.wait();
}
void MaterialEditor::open(ModelDocument model) {
    require(!texture_encoding_.valid(), "Finish texture encoding before opening another model");
    texture_import_.reset();
    texture_import_open_ = false;
    memory_sizes_.clear();
    memory_.reset();
    memory_checked_ = false;
    memory_error_.clear();
    pending_resource_ = {};
    resource_texture_.clear();
    resource_replacement_.clear();
    uv_editor_ = {};
    edit_uvs_ = false;
    document_ = std::make_unique<MaterialDocument>(std::move(model));
    bind_project();
    file_.clear();
    message_.clear();
    rendered_revision_ = 0;
    rendered_texture_revision_ = 0;
    rendered_model_revision_ = 0;
    pending_effect_.reset();
    effect_selection_ = -1;
    effect_preview_ = false;
}
void MaterialEditor::dialog(int kind) {
    if (kind == 1 && project_store())
        save_editor_project();
    if (dialog_kind_ || export_.valid())
        return;
    dialog_kind_ = kind;
    if (kind == 5 || kind == 6)
        choose_png(window_, dialog_);
    else if (kind == 3)
        choose_folder(window_, dialog_, override_folder_.string().c_str());
    else if (kind == 4)
        choose_archive(window_, dialog_, existing_archive_.string().c_str());
    else
        choose_material_document(window_, dialog_,
                                 file_.empty() ? "materials.usum-material" : file_.string().c_str(),
                                 kind == 1);
}
void MaterialEditor::save() {
    if (save_editor_project()) {
        message_ = "Project saved. Stage Project builds the overlay.";
        return;
    }
    document_->commit();
    if (file_.empty()) {
        dialog(1);
        return;
    }
    auto data = document_->serialize();
    write_file_atomic(file_,
                      View(reinterpret_cast<const std::uint8_t *>(data.data()), data.size()));
    document_->mark_saved();
    message_ = "Saved edits: " + file_.string();
    if (save_then_leave_) {
        save_then_leave_ = false;
        auto action = std::move(leave_action_);
        if (action)
            action();
    }
}
void MaterialEditor::set_shader_donor(ModelDocument model) {
    donor_ = std::make_unique<ModelDocument>(std::move(model));
    donor_->scene = std::make_shared<Environment>(*donor_->scene);
    donor_material_ = 0;
    effect_materials_.clear();
    for (auto &motion : donor_->motions)
        if (motion.group < pokemon_main_motion_counts.size() &&
            motion.slot == pokemon_main_motion_counts[motion.group] + 7)
            for (auto &track : motion.material.tracks)
                for (unsigned i = 0; i < donor_->scene->materials.size(); ++i)
                    if (donor_->scene->materials[i].name == track.material)
                        effect_materials_.insert(i);
}
void MaterialEditor::poll() {
    if (pending_resource_) {
        auto action = std::move(pending_resource_);
        pending_resource_ = {};
        try {
            action();
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    }
    if (pending_effect_) {
        auto target = *pending_effect_;
        pending_effect_.reset();
        try {
            require(bool(donor_), "Choose a donor model first");
            auto selected = int(document_->model.scene->materials.size());
            MaterialDocument donor(*donor_);
            message_ = document_->borrow_effect(
                target, donor, {effect_materials_.begin(), effect_materials_.end()},
                keep_effect_original_);
            effect_selection_ = selected;
            effect_preview_ = true;
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    }

    std::string path, error;
    bool ready = false;
    {
        std::lock_guard lock(dialog_->mutex);
        if (dialog_->ready) {
            ready = true;
            dialog_->ready = false;
            path = dialog_->path;
            error = dialog_->error;
        }
    }
    if (ready) {
        int kind = std::exchange(dialog_kind_, 0);
        try {
            require(error.empty(), error);
            if (path.empty()) {
                save_then_leave_ = false;
                return;
            }
            if (kind == 6 || kind == 5) {
                texture_import_ = read_png(std::filesystem::u8path(path));
                texture_add_ = kind == 6;
                texture_format_ = texture_add_ ? 1 : 0;
                texture_import_error_.clear();
                if (texture_add_) {
                    texture_import_source_ = Bytes(128);
                    put32(texture_import_source_, 0, 0x15041213);
                    put32(texture_import_source_, 4, 1);
                    put32(texture_import_source_, 20, 0xffffffff);
                    std::copy_n("texture", 7, texture_import_source_.begin() + 8);
                } else
                    texture_import_source_ = document_->texture_resource(pending_texture_);
                texture_import_open_ = true;
            } else if (kind == 1) {
                auto previous = file_;
                file_ = std::filesystem::u8path(path);
                if (file_.extension().empty())
                    file_ += ".usum-material";
                try {
                    save();
                } catch (...) {
                    file_ = previous;
                    throw;
                }
            } else if (kind == 2) {
                auto bytes = read_file(std::filesystem::u8path(path));
                document_->restore(std::string(bytes.begin(), bytes.end()));
                project_.imported();
                file_ = std::filesystem::u8path(path);
                message_ = "Loaded edits: " + file_.string();
            } else if (kind == 3)
                override_folder_ = std::filesystem::u8path(path);
            else if (kind == 4)
                existing_archive_ = std::filesystem::u8path(path);
        } catch (const std::exception &e) {
            message_ = e.what();
            save_then_leave_ = false;
        }
    }
    if (export_.valid() && export_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            auto result = export_.get();
            document_->accept_written(*result.second);
            message_ = std::move(result.first);
        } catch (const std::exception &e) {
            message_ = e.what();
        }
}
void MaterialEditor::texture_import_panel() {
    if (texture_import_open_) {
        ImGui::OpenPopup("Import texture");
        texture_import_open_ = false;
    }
    if (!ImGui::BeginPopupModal("Import texture", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    if (!texture_import_ || !document_) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const TextureFormat formats[] = {
        TextureFormat::RGBA8, TextureFormat::RGB8, TextureFormat::RGB565, TextureFormat::RGB5A1,
        TextureFormat::RGBA4, TextureFormat::LA8,  TextureFormat::L8,     TextureFormat::A8,
        TextureFormat::LA4,   TextureFormat::L4,   TextureFormat::A4,     TextureFormat::ETC1,
        TextureFormat::ETC1A4};
    bool busy = texture_encoding_.valid();
    ImGui::Text("%s: %s", texture_add_ ? "Add" : "Replace", pending_texture_.c_str());
    ImGui::Text("PNG: %u x %u", unsigned(texture_import_->width),
                unsigned(texture_import_->height));
    unsigned levels = texture_add_ ? 1 : u16(texture_import_source_, 110);
    if (!texture_add_)
        ImGui::Text("Current: %s, %u x %u, %u mip level%s",
                    texture_format_name(TextureFormat(u16(texture_import_source_, 108))),
                    unsigned(u16(texture_import_source_, 104)),
                    unsigned(u16(texture_import_source_, 106)), levels, levels == 1 ? "" : "s");
    ImGui::BeginDisabled(busy);
    if (ImGui::BeginCombo("Output format", texture_format_
                                               ? texture_format_name(formats[texture_format_ - 1])
                                               : "Keep current format")) {
        if (!texture_add_ && ImGui::Selectable("Keep current format", texture_format_ == 0))
            texture_format_ = 0;
        for (int i = 0; i < 13; ++i)
            if (ImGui::Selectable(texture_format_name(formats[i]), texture_format_ == i + 1))
                texture_format_ = i + 1;
        ImGui::EndCombo();
    }
    auto format = texture_format_ ? formats[texture_format_ - 1]
                                  : TextureFormat(u16(texture_import_source_, 108));
    bool color = false, alpha = false;
    for (std::size_t i = 0; i < texture_import_->rgba.size(); i += 4) {
        auto p = texture_import_->rgba.data() + i;
        color |= p[0] != p[1] || p[1] != p[2];
        alpha |= p[3] != 255;
    }
    bool luminance = format == TextureFormat::L8 || format == TextureFormat::L4 ||
                     format == TextureFormat::LA8 || format == TextureFormat::LA4;
    bool alpha_only = format == TextureFormat::A8 || format == TextureFormat::A4;
    bool opaque = format == TextureFormat::RGB8 || format == TextureFormat::RGB565 ||
                  format == TextureFormat::ETC1 || format == TextureFormat::L8 ||
                  format == TextureFormat::L4;
    if (color && luminance)
        ImGui::TextUnformatted("This format converts color to grayscale.");
    if (alpha_only)
        ImGui::TextUnformatted("This format stores alpha only; color is discarded.");
    if (alpha && opaque)
        ImGui::TextUnformatted("This format discards transparency.");
    if (format == TextureFormat::RGB5A1)
        ImGui::TextUnformatted("Transparency becomes fully opaque or fully transparent.");
    if (format == TextureFormat::ETC1 || format == TextureFormat::ETC1A4)
        ImGui::TextUnformatted("ETC color compression is lossy. Review the encoded result.");
    if (levels > 1)
        ImGui::Text("Keeps %u mip levels; changed textures regenerate lower levels.", levels);
    std::size_t size = 0;
    unsigned w = texture_import_->width, h = texture_import_->height;
    for (unsigned i = 0; i < levels; ++i) {
        size += texture_level_size(w, h, format);
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    ImGui::Text("Pixel data: %.1f KiB", double(size) / 1024.0);
    if (studio::TutorialWidgets::Button("material_editor", "Apply texture")) {
        auto source = texture_import_source_;
        auto pixels = *texture_import_;
        auto name = texture_add_ ? pending_texture_ : text(slice(source, 40, 64));
        texture_import_error_.clear();
        texture_encoding_ =
            std::async(std::launch::async,
                       [source = std::move(source), pixels = std::move(pixels), name, format] {
                           return encode_texture(source, pixels, name, format);
                       });
    }
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("material_editor", "Cancel")) {
        texture_import_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    if (busy) {
        ImGui::TextUnformatted("Encoding texture...");
        if (texture_encoding_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto encoded = texture_encoding_.get();
                if (texture_add_)
                    document_->add_encoded_texture(pending_texture_, encoded);
                else {
                    require(document_->texture_resource(pending_texture_) == texture_import_source_,
                            "Texture changed during import; cancel and import again");
                    document_->replace_encoded_texture(pending_texture_, encoded);
                }
                message_ = texture_add_ ? "Added texture. Bind it under Textures and UVs."
                                        : "Texture imported. Review the encoded result; Undo "
                                          "restores the previous texture.";
                texture_import_.reset();
                ImGui::CloseCurrentPopup();
            } catch (const std::exception &e) {
                texture_import_error_ = e.what();
            }
        }
    }
    if (!texture_import_error_.empty())
        ImGui::TextWrapped("%s", texture_import_error_.c_str());
    ImGui::EndPopup();
}
void MaterialEditor::start_write() {
    document_->commit();
    auto snapshot = std::make_shared<MaterialDocument>(*document_);
    snapshot->model.scene = std::make_shared<Environment>(*snapshot->model.scene);
    auto mode = write_mode_;
    auto path = mode == 0 ? override_folder_ : mode == 1 ? existing_archive_ : working_dump_;
    export_ = std::async(std::launch::async, [snapshot = std::move(snapshot), path, mode] {
        if (mode == 0)
            snapshot->export_to(path, true);
        else if (mode == 1)
            snapshot->write_archive(path, true);
        else
            snapshot->write_to_dump(path);
        return std::pair{"Wrote and verified " + path.string() +
                             ". Unrelated archive members were preserved.",
                         snapshot};
    });
    message_ = "Writing and verifying archive...";
}
void MaterialEditor::write_panel() {
    if (write_open_) {
        ImGui::OpenPopup("Write game files");
        write_open_ = false;
        message_.clear();
    }
    ImGui::SetNextWindowSize({600, 0}, ImGuiCond_Appearing);
    write_visible_ =
        ImGui::BeginPopupModal("Write game files", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    if (!write_visible_)
        return;
    auto primary_archive = *document_->archive_paths().begin();
    bool busy = dialog_kind_ || export_.valid() || texture_import_ || texture_encoding_.valid();
    ImGui::BeginDisabled(busy);
    ImGui::TextWrapped("%s | %zu edit(s)", document_->model.name.c_str(), document_->write_count());
    ImGui::TextWrapped("Asset archive: %s", primary_archive.generic_string().c_str());
    ImGui::Separator();
    studio::TutorialWidgets::RadioButton("material_editor", "Export override folder", &write_mode_,
                                         0);
    studio::TutorialWidgets::RadioButton("material_editor", "Update an existing GARC", &write_mode_,
                                         1);
    studio::TutorialWidgets::RadioButton("material_editor", "Write to working dump", &write_mode_,
                                         2);
    if (document_->archive_paths().size() > 1)
        ImGui::TextWrapped(
            "These edits use %zu archives. Choose a folder or working dump to write them together.",
            document_->archive_paths().size());
    if (write_mode_ == 0) {
        ImGui::TextWrapped("Choose an override folder. An existing archive there will be updated, "
                           "preserving unrelated edits.");
        if (studio::TutorialWidgets::Button("material_editor", "Choose override folder..."))
            dialog(3);
    } else if (write_mode_ == 1) {
        ImGui::TextWrapped(
            "Choose the matching archive file. Game archives may have no file extension.");
        if (studio::TutorialWidgets::Button("material_editor", "Choose GARC file..."))
            dialog(4);
    } else
        ImGui::TextWrapped("Write directly to the dump selected in Project. Reload affected "
                           "viewers after writing.");
    auto root = write_mode_ == 0   ? override_folder_
                : write_mode_ == 1 ? existing_archive_
                                   : working_dump_;
    auto destination = root.empty()       ? std::filesystem::path{}
                       : write_mode_ == 1 ? root
                                          : root / primary_archive;
    bool exists = false, valid = !destination.empty();
    std::string problem;
    if (write_mode_ == 1 && document_->archive_paths().size() > 1) {
        valid = false;
        problem = "Choose a folder or working dump for edits spanning multiple archives.";
    }
    try {
        if (valid) {
            exists = std::filesystem::exists(destination);
            if (write_mode_ != 0 && !std::filesystem::is_regular_file(destination)) {
                valid = false;
                problem = "Choose a working dump or matching archive that exists.";
            } else if (exists && !std::filesystem::is_regular_file(destination)) {
                valid = false;
                problem = "The destination is not a file.";
            }
        }
    } catch (const std::exception &e) {
        valid = false;
        problem = e.what();
    }
    ImGui::SeparatorText("Destination");
    ImGui::TextWrapped("%s", destination.empty() ? "No destination selected"
                                                 : destination.string().c_str());
    if (write_mode_ != 1 && !root.empty())
        for (auto &path : document_->archive_paths())
            if (path != primary_archive)
                ImGui::TextWrapped("%s", (root / path).string().c_str());
    if (!problem.empty())
        ImGui::TextWrapped("%s", problem.c_str());
    if (exists)
        ImGui::TextWrapped(
            "The existing GARC will be replaced after verification. Edited resources are updated. "
            "Unrelated assets are kept; conflicting resource changes are reported.");
    else
        ImGui::TextWrapped("A new archive will be created from the source dump.");
    ImGui::TextWrapped("Shared placements use the same material edits. Saving an edit document is "
                       "separate from writing game files.");
    if (!message_.empty())
        ImGui::TextWrapped("%s", message_.c_str());
    ImGui::BeginDisabled(!valid);
    if (studio::TutorialWidgets::Button("material_editor", write_mode_ == 2
                                                               ? "Write to working dump##commit"
                                                           : exists ? "Update existing GARC##commit"
                                                                    : "Create override##commit")) {
        try {
            start_write();
            ImGui::CloseCurrentPopup();
            write_visible_ = false;
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("material_editor", "Cancel")) {
        ImGui::CloseCurrentPopup();
        write_visible_ = false;
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}
void MaterialEditor::request_leave(std::function<void()> action) {
    if (texture_import_ || texture_encoding_.valid()) {
        message_ = "Finish or cancel the texture import first.";
        return;
    }
    if (external_operation && external_operation()) {
        message_ = "Finish the Blender exchange operation first.";
        return;
    }
    if (project_store()) {
        try {
            save_editor_project();
            action();
        } catch (const std::exception &e) {
            message_ = e.what();
        }
        return;
    }
    if (export_.valid() || dialog_kind_ || write_visible_) {
        message_ = "Finish the current save or export first.";
        return;
    }
    if (document_ && document_->dirty()) {
        leave_action_ = std::move(action);
        leave_ = true;
    } else
        action();
}
void MaterialEditor::leave_popup() {
    if (leave_) {
        ImGui::OpenPopup("Unsaved Studio edits");
        leave_ = false;
    }
    if (ImGui::BeginPopupModal("Unsaved Studio edits", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save this asset's material edits before leaving?");
        if (studio::TutorialWidgets::Button("material_editor", "Save")) {
            ImGui::CloseCurrentPopup();
            save_then_leave_ = true;
            try {
                save();
            } catch (const std::exception &e) {
                message_ = e.what();
                save_then_leave_ = false;
            }
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("material_editor", "Discard")) {
            ImGui::CloseCurrentPopup();
            if (document_)
                document_->discard();
            auto action = std::move(leave_action_);
            if (action)
                action();
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("material_editor", "Cancel")) {
            ImGui::CloseCurrentPopup();
            leave_action_ = {};
        }
        ImGui::EndPopup();
    }
}
void MaterialEditor::draw_memory() {
    auto &doc = *document_;
    if (!doc.model.is_pokemon())
        return;
    if (!memory_checked_ || memory_revision_ != doc.revision() ||
        memory_texture_revision_ != doc.texture_revision() ||
        memory_model_revision_ != doc.model_revision()) {
        memory_checked_ = true;
        memory_revision_ = doc.revision();
        memory_texture_revision_ = doc.texture_revision();
        memory_model_revision_ = doc.model_revision();
        memory_.reset();
        memory_error_.clear();
        try {
            auto edited = doc.compiled_members();
            std::optional<Archive> archive;
            memory_ = estimate_pokemon_summary_memory(doc.model.pokemon, [&](std::size_t member) {
                if (auto it = edited.find(member); it != edited.end())
                    return it->second.size();
                for (auto &source : doc.model.sources)
                    if (source.member == member)
                        return source.original.size();
                if (auto it = memory_sizes_.find(member); it != memory_sizes_.end())
                    return it->second;
                if (!archive)
                    archive.emplace(doc.model.archive_sources.resolve(
                        doc.model.dump, TargetProfile::pokemon_archive));
                auto size = archive->decoded(member).size();
                memory_sizes_[member] = size;
                return size;
            });
        } catch (const std::exception &e) {
            memory_error_ = e.what();
        }
    }
    if (!memory_) {
        ImGui::TextWrapped("Summary memory estimate unavailable: %s", memory_error_.c_str());
        return;
    }
    auto &memory = *memory_;
    if (memory.warning()) {
        ImGui::PushStyleColor(ImGuiCol_Text, memory.exceeds() ? ImVec4{1.f, .38f, .35f, 1}
                                                              : ImVec4{1.f, .75f, .3f, 1});
        ImGui::TextWrapped(
            "%s", memory.exceeds() ? "Summary memory warning: resource data reaches or exceeds the "
                                     "stock model heap. The game may freeze loading this Pokemon."
                                   : "Summary memory warning: little room remains for runtime "
                                     "objects. The game may freeze loading this Pokemon.");
        ImGui::PopStyleColor();
        ImGui::TextWrapped("Normal %.2f MiB | Shiny %.2f MiB | Stock heap 3.25 MiB",
                           double(memory.total(false)) / 1048576,
                           double(memory.total(true)) / 1048576);
    }
    if (studio::TutorialWidgets::TreeNode("material_editor", "Summary memory details")) {
        auto kib = [](std::size_t bytes) {
            return double(bytes) / 1024;
        };
        ImGui::Text("Model and shaders: %.1f KiB", kib(memory.model));
        ImGui::Text("Normal textures: %.1f KiB", kib(memory.normal_textures));
        ImGui::Text("Shiny textures: %.1f KiB", kib(memory.shiny_textures));
        ImGui::Text("%s motions: %.1f KiB", memory.refresh_motions ? "Refresh" : "Battle",
                    kib(memory.motions));
        ImGui::Text("Settings: %.1f KiB", kib(memory.settings));
        for (bool shiny : {false, true}) {
            auto remaining = double(PokemonMemoryEstimate::heap) - double(memory.total(shiny));
            ImGui::Text("%s: %.2f MiB | %s %.1f KiB", shiny ? "Shiny" : "Normal",
                        double(memory.total(shiny)) / 1048576,
                        remaining < 0 ? "over budget" : "remaining", std::abs(remaining) / 1024);
        }
        ImGui::TextWrapped("Decompressed resources, with one texture variant loaded at a time. "
                           "Stock summary model heap: 3.25 MiB. Warning margin: 256 KiB; this is a "
                           "heuristic, not measured free memory.");
        ImGui::TextWrapped("Runtime objects, allocation overhead and additional form-specific "
                           "loads need extra space. Below the warning threshold does not guarantee "
                           "a fit. Patched executables may use another budget.");
        if (memory.warning())
            ImGui::TextWrapped("Reduce texture dimensions or remove unneeded borrowed resources. "
                               "Removing a material pass alone may leave its textures in the "
                               "archive. Save and export remain available.");
        ImGui::TreePop();
    }
}
void MaterialEditor::draw(EnvironmentRenderer &renderer, MaterialSelection &selection,
                          const std::filesystem::path &working_dump) {
    working_dump_ = working_dump;
    poll();
    texture_import_panel();
    ImGui::Begin("Studio materials");
    if (!document_) {
        ImGui::TextWrapped("Select a model in Models or Maps, then send it to Studio.");
        ImGui::End();
        leave_popup();
        return;
    }
    auto &doc = *document_;
    bool busy = dialog_kind_ || export_.valid() || texture_import_ || texture_encoding_.valid();
    ImGui::TextWrapped("%s%s", doc.model.name.c_str(), doc.dirty() ? " *" : "");
    draw_memory();
    ImGui::TextDisabled("%s", doc.dirty()       ? "Unsaved document changes"
                              : project_store() ? "No unsaved project edits"
                              : file_.empty()   ? "Document not saved"
                                                : "Document saved");
    ImGui::BeginDisabled(busy);
    if (studio::TutorialWidgets::Button("material_editor",
                                        project_store() ? "Save Project" : "Save edits"))
        try {
            save();
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Save the editable document (Ctrl+Shift+S)");
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("material_editor", "Document..."))
        ImGui::OpenPopup("Document actions");
    if (ImGui::BeginPopup("Document actions")) {
        if (!project_store() && studio::TutorialWidgets::MenuItem("material_editor", "Save as..."))
            dialog(1);
        if (studio::TutorialWidgets::MenuItem("material_editor", "Open edit document..."))
            request_leave([this] {
                dialog(2);
            });
        ImGui::EndPopup();
    }
    ImGui::BeginDisabled(!doc.can_undo());
    if (studio::TutorialWidgets::Button("material_editor", "Undo"))
        doc.undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!doc.can_redo());
    if (studio::TutorialWidgets::Button("material_editor", "Redo"))
        doc.redo();
    ImGui::EndDisabled();
    if (doc.model.originating_map >= 0 || doc.model.area >= 0) {
        if (project_store() &&
            studio::TutorialWidgets::Button("material_editor", "Stage and return to map", {-1, 0}))
            apply_ = true;
    } else {
        if (studio::TutorialWidgets::Button("material_editor", "Preview in source viewer", {-1, 0}))
            apply_ = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Update the open source viewer without writing a game file");
    }
    ImGui::BeginDisabled(doc.write_count() == 0);
    if (!project_store() &&
        studio::TutorialWidgets::Button("material_editor", "Write game files...", {-1, 0}))
        write_open_ = true;
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (!message_.empty())
        ImGui::TextWrapped("%s", message_.c_str());
    if (export_.valid())
        ImGui::ProgressBar(-float(ImGui::GetTime()), {-1, 0}, "Writing and verifying...");
    ImGui::Separator();
    ImGui::BeginDisabled(busy);
    auto &io = ImGui::GetIO();
    if (!busy && !write_visible_ && !io.WantTextInput && !io.MouseDown[0] && io.KeyCtrl) {
        if (io.KeyShift && (!project_store() && ImGui::IsKeyPressed(ImGuiKey_S, false)))
            try {
                save();
            } catch (const std::exception &e) {
                message_ = e.what();
            }
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            if (io.KeyShift)
                doc.redo();
            else
                doc.undo();
        }
    }
    auto &scene = *doc.model.scene;
    if (effect_selection_ >= 0) {
        selection.material = effect_selection_;
        selection.draw = -1;
        effect_selection_ = -1;
    }
    if (selection.material >= int(scene.materials.size()))
        selection.material = 0;
    if (selection.draw >= int(scene.draws.size()))
        selection.draw = -1;
    ImGui::Separator();
    if (selection.material < 0 && !scene.materials.empty())
        selection.material = 0;
    ImGui::SetNextItemWidth(-1);
    const char *current =
        selection.material >= 0 ? scene.materials.at(selection.material).name.c_str() : "Material";
    if (ImGui::BeginCombo("##edit-material", current)) {
        for (unsigned i = 0; i < scene.materials.size(); ++i)
            if (ImGui::Selectable(scene.materials[i].name.c_str(), selection.material == int(i))) {
                selection.material = int(i);
                selection.draw = -1;
            }
        ImGui::EndCombo();
    }
    resource_panel(selection);
    if (selection.material >= 0) {
        auto index = std::size_t(selection.material);
        auto edit = doc.edits().at(index);
        auto &material = scene.materials.at(index);
        bool changed = false;
        if (studio::TutorialWidgets::Button("material_editor", "Reset material"))
            doc.reset(index);
        if (studio::TutorialWidgets::CollapsingHeader("material_editor", "Colors",
                                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            studio::TutorialWidgets::Checkbox("material_editor", "Show unused constants",
                                              &unused_constants_);
            std::array<bool, 6> used{};
            for (unsigned stage = 0; stage < 6; ++stage) {
                auto &c = material.combiner.stages[stage];
                if (material.constant_assignments[stage] < 6 &&
                    (std::find(c.color_sources.begin(), c.color_sources.end(), 14.f) !=
                         c.color_sources.end() ||
                     std::find(c.alpha_sources.begin(), c.alpha_sources.end(), 14.f) !=
                         c.alpha_sources.end()))
                    used[material.constant_assignments[stage]] = true;
            }
            for (unsigned i = 0; i < 9; ++i) {
                if (i < 6 && !unused_constants_ && !used[i])
                    continue;
                ImGui::PushID(int(i));
                std::string name = i < 6    ? "Constant " + std::to_string(i)
                                   : i == 6 ? "Emission"
                                   : i == 7 ? "Ambient"
                                            : "Diffuse";
                changed |= ImGui::ColorEdit4(name.c_str(), edit.colors[i].data(),
                                             ImGuiColorEditFlags_AlphaBar);
                ImGui::PopID();
            }
            changed |= studio::TutorialWidgets::Checkbox("material_editor", "Fragment lighting",
                                                         &edit.fragment_lighting);
        }
        if (studio::TutorialWidgets::CollapsingHeader("material_editor", "Textures and UVs",
                                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Combo("Texture unit", &texture_unit_, "Texture 0\0Texture 1\0Texture 2\0");
            auto &t = edit.textures[texture_unit_];
            if (t.name.empty())
                ImGui::TextDisabled("No texture bound to this unit");
            else {
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##texture-binding", t.name.c_str())) {
                    for (auto &name : doc.texture_names())
                        if (ImGui::Selectable(name.c_str(), t.name == name)) {
                            t.name = name;
                            changed = true;
                        }
                    ImGui::EndCombo();
                }
                auto image = scene.textures.find(t.name);
                auto texture = renderer.texture(t.name);
                if (image != scene.textures.end() && bgfx::isValid(texture)) {
                    const auto &pixels = image->second;
                    float limit = std::max(1.f, std::min(192.f, ImGui::GetContentRegionAvail().x));
                    float scale = limit / std::max(pixels.width, pixels.height);
                    ImGui::Image(ImTextureID(ImGuiRenderer::image_id(texture, false)),
                                 {pixels.width * scale, pixels.height * scale});
                    ImGui::TextDisabled("%u x %u", pixels.width, pixels.height);
                } else
                    ImGui::TextDisabled("Texture preview unavailable");
                ImGui::BeginDisabled(!doc.model.texture_resources.contains(t.name));
                if (studio::TutorialWidgets::Button("material_editor", "Replace with PNG...")) {
                    pending_texture_ = t.name;
                    dialog(5);
                }
                ImGui::SameLine();
                if (studio::TutorialWidgets::Button("material_editor", "Reset texture"))
                    doc.reset_texture(t.name);
                ImGui::EndDisabled();
                if (!doc.model.texture_resources.contains(t.name))
                    ImGui::TextWrapped("This texture has no writable source link.");
                bool uv = material.inputs[texture_unit_].source <= 2;
                ImGui::BeginDisabled(!uv);
                changed |= ImGui::DragFloat2("Scale", t.transform.data(), .01f, -1000, 1000);
                float angle = t.transform[2] * 57.2957795f;
                if (ImGui::DragFloat("Rotation", &angle, .2f, -36000, 36000, "%.2f deg")) {
                    t.transform[2] = angle * .0174532925f;
                    changed = true;
                }
                changed |= ImGui::DragFloat2("Offset", t.transform.data() + 3, .002f, -1000, 1000);
                ImGui::EndDisabled();
                if (!uv)
                    ImGui::TextWrapped(
                        "Projected/environment coordinates are read-only.");
            }
        }
        if (studio::TutorialWidgets::CollapsingHeader("material_editor",
                                                      "Borrow fragment shader")) {
            ImGui::TextWrapped("Uses this material's existing texture units and vertex/lighting "
                               "inputs. Shared shader resources stay unchanged.");
            if (!donor_)
                ImGui::TextWrapped("Open a Pokemon in Models, then choose Use as material donor. "
                                   "Your Studio asset stays open.");
            else {
                ImGui::TextWrapped("Donor: %s", donor_->name.c_str());
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##donor-material",
                                      donor_->scene->materials.at(donor_material_).name.c_str())) {
                    for (unsigned i = 0; i < donor_->scene->materials.size(); ++i)
                        if (ImGui::Selectable(donor_->scene->materials[i].name.c_str(),
                                              donor_material_ == int(i)))
                            donor_material_ = int(i);
                    ImGui::EndCombo();
                }
                ImGui::TextWrapped(
                    "Fragment: %s",
                    donor_->scene->materials.at(donor_material_).fragment_shader.c_str());
                studio::TutorialWidgets::Checkbox(
                    "material_editor", "Include colors and render state", &borrow_material_state_);
                if (studio::TutorialWidgets::Button("material_editor",
                                                    "Borrow into selected material", {-1, 0}))
                    try {
                        doc.borrow_shader(index, *donor_, std::size_t(donor_material_),
                                          borrow_material_state_);
                        edit = doc.edits().at(index);
                        message_ =
                            "Borrowed fragment setup. Preview now; save or write when ready.";
                    } catch (const std::exception &e) {
                        message_ = e.what();
                    }
            }
            if (!edit.shader_origin.empty())
                ImGui::TextWrapped("Started from: %s", edit.shader_origin.c_str());
        }
        if (studio::TutorialWidgets::CollapsingHeader("material_editor",
                                                      "Borrow material effect")) {
            ImGui::TextWrapped("Apply linked material passes, textures, shaders, lighting tables "
                               "and any looping motions to every mesh using the selected material. "
                               "The target keeps its vertices, UVs and skinning.");
            if (!donor_)
                ImGui::TextWrapped("Choose Use as material donor in Models first.");
            else {
                ImGui::TextWrapped("Donor: %s", donor_->name.c_str());
                for (unsigned i = 0; i < donor_->scene->materials.size(); ++i) {
                    bool selected = effect_materials_.contains(i);
                    ImGui::PushID(int(i));
                    if (studio::TutorialWidgets::Checkbox("material_editor",
                                                          donor_->scene->materials[i].name.c_str(),
                                                          &selected)) {
                        if (selected)
                            effect_materials_.insert(i);
                        else
                            effect_materials_.erase(i);
                    }
                    ImGui::PopID();
                }
                ImGui::TextWrapped("Target: %s. Importing creates one render pass per selected "
                                   "donor material. Materials without animation are supported.",
                                   material.name.c_str());
                studio::TutorialWidgets::Checkbox("material_editor", "Keep original pass",
                                                  &keep_effect_original_);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Keep the current material draw alongside the imported "
                                      "passes. Donor depth, blending and draw order still apply.");
                ImGui::BeginDisabled(effect_materials_.empty() || doc.model.area >= 0);
                if (studio::TutorialWidgets::Button(
                        "material_editor", "Borrow effect into selected material", {-1, 0}))
                    pending_effect_ = index;
                ImGui::EndDisabled();
            }
        }
        changed |= combiners_editor(edit, material);
        if (studio::TutorialWidgets::CollapsingHeader("material_editor", "Outlines")) {
            studio::TutorialWidgets::Checkbox("material_editor", "Preview outlines",
                                              &renderer.outlines);
            if (renderer.wireframe)
                ImGui::TextWrapped("Turn off Wireframe to see the outline preview.");
            int type = int(edit.edge_type);
            if (ImGui::Combo("Edge type", &type,
                             "Normals\0Vertex colors\0None\0Erase\0Shader colors\0Decorative\0")) {
                edit.edge_type = unsigned(type);
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Controls normal/color edges independently of ID edges. Erase "
                                  "masks normal/color edges beneath this surface.");
            changed |= studio::TutorialWidgets::Checkbox("material_editor", "ID edges",
                                                         &edit.id_edge_enabled);
            ImGui::BeginDisabled(!edit.id_edge_enabled);
            int id = int(edit.edge_id);
            if (ImGui::SliderInt("Edge ID", &id, 0, 255)) {
                edit.edge_id = unsigned(std::clamp(id, 0, 255));
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::TextWrapped("Matching IDs share a region; different IDs create boundaries. "
                               "Normal/color edges can still appear with matching IDs.");
            const char *masks[] = {
                "None",          "Texture 0 alpha", "Texture 1 alpha", "Texture 2 alpha",
                "Texture 0 red", "Texture 1 red",   "Texture 2 red"};
            int mask = edit.edge_alpha_mask + 1;
            if (ImGui::BeginCombo("Edge mask", mask >= 0 && mask < 7 ? masks[mask] : "Unknown")) {
                for (int i = 0; i < 7; ++i) {
                    bool available = i == 0 || !edit.textures[unsigned(i - 1) % 3].name.empty();
                    ImGui::BeginDisabled(!available);
                    if (ImGui::Selectable(masks[i], mask == i)) {
                        edit.edge_alpha_mask = i - 1;
                        changed = true;
                    }
                    ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }
            ImGui::TextWrapped("Material settings are saved and exported. The preview toggle only "
                               "affects this viewport.");
            if (edit.edge_type == 5)
                ImGui::TextWrapped("Decorative offsets are preserved in game; preview currently "
                                   "uses normal edges.");
        }
        if (studio::TutorialWidgets::CollapsingHeader("material_editor", "Rendering")) {
            int cull = int(edit.cull);
            if (ImGui::Combo("Culling", &cull, "Two-sided\0Cull front faces\0Cull back faces\0")) {
                edit.cull = unsigned(cull);
                changed = true;
            }
            int alpha = int(edit.alpha_function);
            if (ImGui::Combo("Alpha test", &alpha,
                             "Never\0Always\0Equal\0Not equal\0Less\0Less or "
                             "equal\0Greater\0Greater or equal\0")) {
                edit.alpha_function = unsigned(alpha);
                changed = true;
            }
            int reference = int(edit.alpha_reference);
            if (ImGui::SliderInt("Alpha threshold", &reference, 0, 255)) {
                edit.alpha_reference = unsigned(reference);
                changed = true;
            }
            bool depth = (edit.depth & 0x1000) != 0;
            if (studio::TutorialWidgets::Checkbox("material_editor", "Write depth", &depth)) {
                edit.depth = (edit.depth & ~0x1000u) | (depth ? 0x1000u : 0);
                changed = true;
            }
            static constexpr std::uint32_t blends[] = {0x01010000, 0x76760000, 0x16160000,
                                                       0x11110000};
            const char *names[] = {"Opaque", "Alpha blend", "Alpha additive", "Additive"};
            int blend = -1;
            for (unsigned i = 0; i < 4; ++i)
                if (edit.blend == blends[i])
                    blend = int(i);
            if (ImGui::BeginCombo("Blending", blend < 0 ? "Custom authored blend" : names[blend])) {
                for (unsigned i = 0; i < 4; ++i)
                    if (ImGui::Selectable(names[i], blend == int(i))) {
                        edit.blend = blends[i];
                        changed = true;
                    }
                ImGui::EndCombo();
            }
        }
        if (changed)
            try {
                doc.preview(index, edit);
            } catch (const std::exception &e) {
                message_ = e.what();
            }
        if (!ImGui::IsAnyItemActive())
            doc.commit();
    }
    ImGui::EndDisabled();
    if (rendered_model_revision_ != doc.model_revision()) {
        renderer.set_scene(doc.model.scene);
        rendered_model_revision_ = doc.model_revision();
    }
    if (effect_preview_) {
        renderer.playback.materials = true;
        renderer.playback.seconds = 0;
        effect_preview_ = false;
    }
    if (rendered_revision_ != doc.revision()) {
        renderer.refresh_materials();
        rendered_revision_ = doc.revision();
    }
    if (rendered_texture_revision_ != doc.texture_revision()) {
        renderer.refresh_textures();
        rendered_texture_revision_ = doc.texture_revision();
    }
    ImGui::End();
    leave_popup();
    write_panel();
}
void MaterialEditor::uvs(const EnvironmentRenderer &renderer, const MaterialSelection &selection) {
    ImGui::Begin("Studio UVs");
    if (!document_ || selection.material < 0) {
        ImGui::TextWrapped("Select a material to inspect its UVs.");
        ImGui::End();
        return;
    }
    if (document_->model.area < 0 && !document_->model.clothing) {
        studio::TutorialWidgets::Checkbox("material_editor", "Edit UVs", &edit_uvs_);
        if (edit_uvs_) {
            if (editing_available())
                uv_editor_.draw(*document_, renderer, selection.material, unsigned(texture_unit_));
            else {
                uv_editor_ = {};
                ImGui::TextWrapped("Finish the current operation before editing UVs.");
            }
            ImGui::End();
            return;
        }
    }
    auto &scene = *document_->model.scene;
    auto &materials = renderer.materials();
    auto &m = materials.at(selection.material);
    auto &input = m.inputs[texture_unit_];
    if (input.source > 2) {
        ImGui::TextWrapped("This texture uses generated coordinates, not a mesh UV set.");
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("All meshes using this material");
    studio::TutorialWidgets::Checkbox("material_editor", "Show transformed UVs", &transformed_uvs_);
    ImGui::TextDisabled("UVs follow the texture wrapping mode");
    ImGui::TextDisabled("Texture %d / UV %u", texture_unit_, input.source);
    auto texture = renderer.texture(m.texture_inputs[texture_unit_]);
    if (!bgfx::isValid(texture)) {
        ImGui::TextUnformatted("No texture available");
        ImGui::End();
        return;
    }
    auto available = ImGui::GetContentRegionAvail();
    float size = std::max(32.f, std::min(available.x, available.y));
    auto origin = ImGui::GetCursorScreenPos();
    ImGui::Image(ImTextureID(ImGuiRenderer::image_id(texture, false)), {size, size});
    draw_texture_uvs(scene, std::size_t(selection.material), input, transformed_uvs_, origin, size);
    ImGui::End();
}
void MaterialEditor::bind_project() {
    if (!document_)
        return;
    project_.bind(
        "material", "material/" + document_->identity(), document_->model.name,
        project_model_parameters(document_->model),
        [this] {
            return document_ && document_->dirty();
        },
        [this] {
            require(editing_available(), "Finish the Studio operation before saving the project");
            document_->commit();
            return project_text(document_->serialize());
        },
        [this] {
            document_->mark_saved();
        });
    project_.ready([this] {
        if (document_) {
            require(editing_available(), "Finish the Studio operation before saving the project");
        }
    });
    if (auto file = project_.document(); !file.empty()) {
        document_->restore(text(read_file(file)));
    }
}
}

namespace studio {
void MaterialEditor::resource_panel(const MaterialSelection &selection) {
    if (!studio::TutorialWidgets::CollapsingHeader("material_editor", "Model resources"))
        return;
    auto &doc = *document_;
    ImGui::BeginDisabled(doc.model.area >= 0 || bool(doc.model.clothing));
    ImGui::TextWrapped("Copy a material, then assign faces in Geometry > Materials. Removing a "
                       "material moves its faces to the replacement.");
    ImGui::InputText("New material name", material_name_, sizeof(material_name_));
    if (studio::TutorialWidgets::Button("material_editor", "Copy selected material") &&
        selection.material >= 0) {
        auto donor = std::size_t(selection.material);
        std::string name = material_name_;
        pending_resource_ = [this, donor, name] {
            auto selected = int(document_->edits().size());
            document_->add_material(donor, name);
            effect_selection_ = selected;
            message_ = "Added material. Assign its faces in Geometry > Materials.";
        };
    }
    auto &materials = doc.model.scene->materials;
    if (remove_material_target_ >= int(materials.size()))
        remove_material_target_ = 0;
    if (ImGui::BeginCombo("Move faces to", materials.at(remove_material_target_).name.c_str())) {
        for (unsigned i = 0; i < materials.size(); ++i)
            if (int(i) != selection.material &&
                ImGui::Selectable(materials[i].name.c_str(), remove_material_target_ == int(i)))
                remove_material_target_ = int(i);
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(materials.size() < 2 || selection.material == remove_material_target_ ||
                         selection.material < 0);
    if (studio::TutorialWidgets::Button("material_editor", "Remove selected material")) {
        auto selected = std::size_t(selection.material),
             replacement = std::size_t(remove_material_target_);
        pending_resource_ = [this, selected, replacement] {
            document_->remove_material(selected, replacement);
            effect_selection_ = int(replacement - (replacement > selected));
            message_ = "Removed material and reassigned its faces. Undo restores the original.";
        };
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::InputText("New texture name", texture_name_, sizeof(texture_name_));
    if (studio::TutorialWidgets::Button("material_editor", "Add texture from PNG...")) {
        pending_texture_ = texture_name_;
        dialog(6);
    }
    ImGui::TextWrapped("PNG dimensions: powers of two from 8 to 1024. Additions start with the "
                       "same pixels for normal and shiny Pokemon.");
    if (!doc.model.texture_resources.contains(resource_texture_))
        resource_texture_ = doc.model.texture_resources.empty()
                                ? std::string{}
                                : doc.model.texture_resources.begin()->first;
    if (ImGui::BeginCombo("Texture resource", resource_texture_.c_str())) {
        for (auto &[name, index] : doc.model.texture_resources)
            if (ImGui::Selectable(name.c_str(), name == resource_texture_))
                resource_texture_ = name;
        ImGui::EndCombo();
    }
    if (!doc.model.texture_resources.contains(resource_replacement_) ||
        resource_replacement_ == resource_texture_)
        resource_replacement_.clear();
    if (ImGui::BeginCombo("Replace references with", resource_replacement_.empty()
                                                         ? "None (unused textures only)"
                                                         : resource_replacement_.c_str())) {
        if (ImGui::Selectable("None (unused textures only)", resource_replacement_.empty()))
            resource_replacement_.clear();
        for (auto &[name, index] : doc.model.texture_resources)
            if (name != resource_texture_ &&
                ImGui::Selectable(name.c_str(), name == resource_replacement_))
                resource_replacement_ = name;
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(resource_texture_.empty());
    if (studio::TutorialWidgets::Button("material_editor", "Remove texture")) {
        auto name = resource_texture_, replacement = resource_replacement_;
        pending_resource_ = [this, name, replacement] {
            document_->remove_texture(name, replacement);
            message_ = "Removed texture and updated its model/motion references. Undo restores it.";
        };
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
}
}
