#pragma once
#include "native/project_binding.h"
#include "assets/material_document.h"
#include "assets/pokemon_memory.h"
#include "native/material_inspector.h"
#include "native/uv_editor.h"
#include "native/texture_painter.h"
#include "native/lighting_table_editor.h"
#include "native/folder_picker.h"
#include <future>
#include <utility>
#include <functional>
namespace studio {
class MaterialEditor {
  public:
    explicit MaterialEditor(SDL_Window *window) : window_(window) {
    }
    ~MaterialEditor();
    void open(ModelDocument model);
    void clear();
    void set_shader_donor(ModelDocument model);
    std::function<bool()> external_operation;
    bool editing_available() const {
        return (!external_operation || !external_operation()) && !export_.valid() &&
               !dialog_kind_ && !texture_import_ && !texture_encoding_.valid() && !write_visible_ &&
               !leave_;
    }
    std::shared_ptr<MaterialDocument> shared_document() const {
        return document_;
    }
    MaterialDocument *document() {
        return document_.get();
    }
    void draw(EnvironmentRenderer &renderer, MaterialSelection &selection,
              const std::filesystem::path &working_dump);
    void uvs(const EnvironmentRenderer &renderer, const MaterialSelection &selection);
    void request_leave(std::function<void()> action);
    bool take_uv_request() {
        return std::exchange(uv_request_, false);
    }
    bool take_mapping_request() {
        return std::exchange(mapping_request_, false);
    }
    unsigned texture_unit() const {
        return unsigned(texture_unit_);
    }
    bool take_apply() {
        return std::exchange(apply_, false);
    }

  private:
    void draw_memory(bool details = true);
    int material_page_ = 1;
    bool uv_request_ = false, mapping_request_ = false;
    std::map<std::size_t, std::size_t> memory_sizes_;
    std::optional<PokemonMemoryEstimate> memory_;
    std::string memory_error_;
    std::uint64_t memory_revision_ = 0, memory_texture_revision_ = 0, memory_model_revision_ = 0;
    bool memory_checked_ = false;
    void resource_panel(const MaterialSelection &selection, const EnvironmentRenderer &renderer);
    std::function<void()> pending_resource_;
    char material_name_[64] = "NewMaterial", texture_name_[64] = "NewTexture";
    int remove_material_target_ = 0;
    std::string resource_texture_, resource_replacement_;
    UvEditor uv_editor_;
    TexturePainter texture_painter_;
    LightingTableEditor lighting_editor_;
    bool edit_uvs_ = false;
    ProjectBinding project_;
    void bind_project();
    void poll();
    void texture_import_panel();
    std::optional<TextureImage> texture_import_;
    Bytes texture_import_source_;
    std::future<Bytes> texture_encoding_;
    int texture_format_ = 0;
    int texture_view_ = 1, texture_import_channel_ = 0;
    unsigned texture_replace_mask_ = 15;
    TextureImage texture_import_base_;
    bgfx::TextureHandle channel_preview_ = BGFX_INVALID_HANDLE;
    std::string channel_preview_name_;
    int channel_preview_mode_ = -1;
    std::uint64_t channel_preview_revision_ = 0;
    void clear_channel_preview();
    bool texture_import_open_ = false, texture_add_ = false;
    std::string texture_import_error_;
    void save();
    void dialog(int kind);
    void leave_popup();
    void write_panel();
    void start_write();
    SDL_Window *window_;
    std::shared_ptr<MaterialDocument> document_;
    std::unique_ptr<ModelDocument> donor_;
    int donor_material_ = 0;
    bool borrow_material_state_ = true;
    std::set<std::size_t> effect_materials_;
    std::optional<std::size_t> pending_effect_;
    int effect_selection_ = -1;
    std::uint64_t rendered_model_revision_ = 0;
    bool effect_preview_ = false, keep_effect_original_ = false;
    std::shared_ptr<FolderSelection> dialog_ = std::make_shared<FolderSelection>();
    std::future<std::pair<std::string, std::shared_ptr<MaterialDocument>>> export_;
    std::filesystem::path working_dump_, override_folder_, existing_archive_;
    int write_mode_ = 0;
    bool write_open_ = false, write_visible_ = false, unused_constants_ = false;
    std::filesystem::path file_;
    std::string message_, pending_texture_;
    int dialog_kind_ = 0, texture_unit_ = 0;
    bool apply_ = false, leave_ = false, save_then_leave_ = false, transformed_uvs_ = true;
    std::function<void()> leave_action_;
    std::uint64_t rendered_revision_ = 0, rendered_texture_revision_ = 0;
};
}
