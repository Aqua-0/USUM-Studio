#pragma once
#include "native/renderer.h"
#include <set>
namespace studio {
struct MaterialSelection {
    int material = -1, draw = -1;
    bool focus = false, reveal_scene = false;
    char filter[128]{};
    std::set<int> meshes;
    bool mesh_selected(int mesh) const {
        return meshes.empty() ? draw == mesh : meshes.contains(mesh);
    }
    void select_mesh(int mesh, int material_index, bool toggle) {
        if (!toggle)
            meshes.clear();
        else if (meshes.empty() && draw >= 0)
            meshes.insert(draw);
        if (mesh < 0) {
            meshes.clear();
            draw = material = -1;
            return;
        }
        if (toggle && meshes.contains(mesh)) {
            meshes.erase(mesh);
            draw = meshes.empty() ? -1 : *meshes.rbegin();
        } else {
            meshes.insert(mesh);
            draw = mesh;
            material = material_index;
        }
    }
};
void material_inspector(const Environment *scene, const EnvironmentRenderer &renderer,
                        MaterialSelection &selection, const char *title = "Map materials",
                        bool embedded = false);
}
