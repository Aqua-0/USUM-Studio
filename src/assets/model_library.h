#pragma once
#include "assets/model_document.h"
namespace studio {
enum class ModelCategory {
    BattleCharacters,
    FieldCharacters,
    PokeBalls,
    BattleProps,
    PokeBeans,
    BattleArenas
};
struct LibraryModel {
    std::size_t member = 0;
    std::string name;
};
const char *model_category_archive(ModelCategory category);
bool model_category_matches(ModelCategory category, const std::string &name);
std::vector<LibraryModel> load_model_library(const std::filesystem::path &archive,
                                             ModelCategory category,
                                             std::atomic_bool *cancel = nullptr);
ModelDocument load_library_model(const std::filesystem::path &dump,
                                 const std::filesystem::path &archive, ModelCategory category,
                                 const LibraryModel &entry, std::atomic_bool *cancel = nullptr,
                                 const std::string &prefix = "model/",
                                 const Bytes *replacement = nullptr,
                                 std::optional<std::size_t> selected_model = {},
                                 const std::vector<std::size_t> &container_path = {});
ModelDocument studio_library_model(const ModelDocument &source, std::size_t resource);
ModelDocument reload_editable_model(const ModelDocument &source,
                                    const std::map<std::size_t, Bytes> &members);
}
