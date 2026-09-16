# USUMStudio

A C++ editor for Pokémon Ultra Sun and Ultra Moon assets and maps. Browse field areas, edit models, textures and motions in Studio, compose maps, edit collision and overworld placements, inspect interaction scripts, and stage project changes for game export. Game data is supplied separately through a project.

## Gallery
<img width="1600" height="1000" alt="cameras" src="https://github.com/user-attachments/assets/a4ff9604-6346-4c40-bdf4-46a1f0f2967c" />
<img width="1600" height="1000" alt="collision" src="https://github.com/user-attachments/assets/216fd3ee-8ebf-44d5-bf9c-cc7c8cf3bac1" />
<img width="1600" height="1000" alt="blender" src="https://github.com/user-attachments/assets/bb4572ea-c6be-43f6-9a5f-d434aa84a220" />
<img width="1600" height="1000" alt="authoring" src="https://github.com/user-attachments/assets/e349cfa4-735d-40ff-92a3-ba93e493ccc9" />


## Build requirements

- CMake 3.20 or newer and Ninja.
- Windows: Visual Studio 2022 C++ desktop workload and Windows SDK. Run commands in an x64 Native Tools command prompt.
- Linux: a C++20 compiler, X11 and OpenGL development libraries, and SDL3 build dependencies. The viewport currently uses X11, including XWayland on Wayland desktops.
- Internet access for the initial dependency download.

Pinned dependencies and SHA-256 hashes are in `cmake/dependencies.json`. Their downloads retain upstream license files. Keep the matching graphics headers, libraries and shader compiler together.

From the repository root, download dependencies:

```sh
cmake -P cmake/DownloadDependencies.cmake
```

Build the graphics libraries and shader compiler:

```sh
cmake -S dependencies/source -B dependencies/graphics -G Ninja -DCMAKE_BUILD_TYPE=Release -DBGFX_BUILD_EXAMPLES=OFF -DBGFX_WITH_WAYLAND=OFF
cmake --build dependencies/graphics --target bgfx bimg bx shaderc --parallel
```

On Windows, configure the editor:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTUDIO_GRAPHICS_SOURCE=dependencies/source -DSTUDIO_GRAPHICS_BUILD=dependencies/graphics -DSTUDIO_IMGUI_SOURCE=dependencies/imgui -DSDL3_DIR=dependencies/SDL3-3.4.16/cmake
```

On Linux, configure with SDL built from source:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTUDIO_GRAPHICS_SOURCE=dependencies/source -DSTUDIO_GRAPHICS_BUILD=dependencies/graphics -DSTUDIO_IMGUI_SOURCE=dependencies/imgui -DSTUDIO_SDL_SOURCE=dependencies/sdl3
```

Build on either platform:

```sh
cmake --build build --target usum-viewport --parallel
```

The application is in `build/bin`. Keep its `resources` and `viewport-shaders` folders beside it, plus `SDL3.dll` on Windows. Windows builds use the Microsoft Visual C++ x64 runtime. Package the applicable dependency notices with binary distributions.

## Blender exchange

Install `tools/blender/usum_model.py` and `tools/blender/usum_collision.py` through Blender's add-on preferences (Blender 4.2 or newer). The model add-on supports model exchange; the mesh add-on supports collision and authored object geometry. Export from the editor, import in Blender, then export the edited file and import it back into the editor.

## Included notices

The bundled font notice is in `resources/fonts/LICENSE.txt`. Adapted Pawn opcode metadata attribution is in `licenses/PAWN_DISASSEMBLY.txt`.

Both Blender add-ons are licensed under GPL-3.0-or-later; see `tools/blender/LICENSE`. This license applies to the add-ons only.

Special Thanks: 
- SPICA devs for research on the gfmodel format and image formats
- pk3DS Devs for model and image format research 
- ZioruaS2 for some research on particles, various formats, and the idea for a Map Authoring system
- Omikaye for the cursor coordinates idea and extensive Zone Research
- ABZB for research on general RE research and table compilation
- VGAudio for BCSTM encode and decode

AI was used in the making of this project for both frontend, some housekeeping, and tutorials.
