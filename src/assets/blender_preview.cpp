#include "assets/blender_preview.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
namespace studio {
void export_blender_model(const MaterialDocument &document, const std::filesystem::path &file) {
    auto exchange = document.model_exchange();
    auto preview = document.preview_model();
    auto folder = file.parent_path() / (file.stem().string() + ".textures");
    std::ostringstream metadata;
    metadata << "USUMSTUDIO_PREVIEW 1\n"
             << std::quoted(exchange.source) << ' ' << exchange.meshes.size() << '\n';
    metadata << std::setprecision(9);
    for (std::size_t i = 0; i < exchange.meshes.size(); ++i) {
        auto draw = preview.native_meshes.empty()
                        ? i
                        : std::size_t(std::find(preview.native_meshes.begin(),
                                                preview.native_meshes.end(), i) -
                                      preview.native_meshes.begin());
        auto &material = preview.scene->materials.at(preview.scene->draws.at(draw).material);
        auto &uv = material.inputs[0];
        metadata << uv.source << ' ' << uv.wrap_u << ' ' << uv.wrap_v << ' ' << uv.mag_filter << ' '
                 << material.alpha_function << ' ' << material.alpha_reference << ' '
                 << material.blend_state;
        for (auto value : uv.row_u)
            metadata << ' ' << value;
        for (auto value : uv.row_v)
            metadata << ' ' << value;
        metadata << '\n';
        auto found = preview.scene->textures.find(material.texture);
        if (found == preview.scene->textures.end())
            continue;
        std::filesystem::create_directories(folder);
        auto image = folder / (std::to_string(i) + ".tga");
        write_file_atomic(image, write_tga(found->second));
        exchange.meshes[i].texture = image.lexically_relative(file.parent_path()).generic_string();
    }
    auto write = [](const std::filesystem::path &path, const std::string &data) {
        write_file_atomic(path,
                          View(reinterpret_cast<const std::uint8_t *>(data.data()), data.size()));
    };
    write(file.string() + ".preview", metadata.str());
    write(file, serialize_model_exchange(exchange));
}
}
