from pathlib import Path
import argparse
import ast
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def add(files, name, source):
    source = Path(source)
    if not source.is_file():
        raise RuntimeError(f"Missing package input: {source}")
    files[name] = source.read_bytes()


def archive(destination, files, executable=None):
    destination.parent.mkdir(parents=True, exist_ok=True)
    executables = {executable} if isinstance(executable, str) else set(executable or ())
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            entry.create_system = 3
            entry.external_attr = (0o100755 if name in executables else 0o100644) << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            output.writestr(entry, data)
    with zipfile.ZipFile(destination) as output:
        if output.testzip() is not None:
            raise RuntimeError("Package integrity check failed")
    print(destination.name)


def editor(args):
    dependencies = args.dependencies.resolve()
    build = args.build.resolve() / "bin"
    windows = args.platform == "windows-x64"
    name = "usum-viewport.exe" if windows else "usum-viewport"
    files = {}
    add(files, name, build / name)
    compiler = "gf-pawncc.exe" if windows else "gf-pawncc"
    compiler_path = args.build.resolve() / "pawn/bin" / compiler
    if windows:
        add(files, "SDL3.dll", build / "SDL3.dll")
    for folder in ("resources", "viewport-shaders"):
        source = build / folder
        if not source.is_dir():
            raise RuntimeError(f"Missing runtime folder: {folder}")
        for file in source.rglob("*"):
            if file.is_file():
                add(files, file.relative_to(build).as_posix(), file)
    add(files, "resources/pawn/" + compiler, compiler_path)
    for profile in ("glsl", "spirv"):
        for effect in ("environment", "post", "particle", "spatial"):
            for stage in ("vs", "fs"):
                if f"viewport-shaders/{profile}/{stage}_{effect}.bin" not in files:
                    raise RuntimeError("Incomplete shader package")
    add(files, "LICENSE", ROOT / "LICENSE")
    notices = {
        "Pawn.txt": ROOT / "licenses/PAWN_DISASSEMBLY.txt",
        "Pawn-compiler-LICENSE.txt": dependencies / "pawn-compiler/compiler/LICENSE",
        "Pawn-compiler-NOTICE.txt": dependencies / "pawn-compiler/compiler/NOTICE",
        "Pawn-Linux-support-LICENSE.txt": ROOT / "cmake/pawn-compiler/support/LICENSE",
        "Pawn-Linux-support-NOTICE.txt": ROOT / "cmake/pawn-compiler/support/NOTICE",
        "Pawn-Linux-support-provenance.md": ROOT / "cmake/pawn-compiler/support/PROVENANCE.md",
        "BinReloc.txt": ROOT / "cmake/pawn-compiler/support/binreloc.h",
        "DejaVuSans.txt": ROOT / "resources/fonts/LICENSE.txt",
        "Dear-ImGui.txt": dependencies / "imgui/LICENSE.txt",
        "SDL3.txt": dependencies / "sdl3/LICENSE.txt",
        "SDL-HIDAPI-BSD.txt": dependencies / "sdl3/src/hidapi/LICENSE-bsd.txt",
        "SDL-yuv2rgb.txt": dependencies / "sdl3/src/video/yuv2rgb/LICENSE",
        "bgfx.txt": dependencies / "source/bgfx/LICENSE",
        "bx.txt": dependencies / "source/bx/LICENSE",
        "bimg.txt": dependencies / "source/bimg/LICENSE",
        "ASTC-encoder.txt": dependencies / "source/bimg/3rdparty/astc-encoder/LICENSE.txt",
        "miniz.txt": dependencies / "source/bimg/3rdparty/tinyexr/deps/miniz/LICENSE",
        "DirectX-Headers.txt": dependencies / "source/bgfx/3rdparty/directx-headers/LICENSE",
    }
    for name_in_package, source in notices.items():
        add(files, "licenses/" + name_in_package, source)
    proggy = (dependencies / "imgui/LICENSE.txt").read_text().replace(
        "Copyright (c) 2014-2025 Omar Cornut", "Copyright (c) 2004, 2005 Tristan Grimmer")
    files["licenses/ProggyClean.txt"] = proggy.encode()
    files["licenses/Japanese-glyph-ranges.txt"] = (
        "Dear ImGui Japanese glyph ranges include Joyo Kanji and Jinmeiyo Kanji lists from "
        "Japan's Agency for Cultural Affairs and Ministry of Justice, under CC BY 4.0. "
        "The lists are represented as compressed code-point ranges in Dear ImGui.\n"
        "https://www.bunka.go.jp/kokugo_nihongo/sisaku/joho/joho/kijun/naikaku/kanji/\n"
        "http://www.moj.go.jp/MINJI/minji86.html\n"
        "https://creativecommons.org/licenses/by/4.0/legalcode\n"
    ).encode()
    # Keep the exact bundled header notices, including their copyright holders.
    for name_in_header in ("imstb_rectpack.h", "imstb_textedit.h", "imstb_truetype.h"):
        text = (dependencies / "imgui" / name_in_header).read_text()
        match = re.search(r"ALTERNATIVE A - MIT License.*?(?=ALTERNATIVE B)", text, re.S)
        if not match:
            raise RuntimeError(f"Missing embedded notice in {name_in_header}")
        files["licenses/" + name_in_header + ".txt"] = match.group().encode()
    text = (dependencies / "source/bgfx/3rdparty/khronos/KHR/khrplatform.h").read_text()
    start = text.index("/*")
    files["licenses/Khronos.txt"] = text[start:text.index("*/", start) + 2].encode()
    for folder in ("source/bimg/3rdparty/astc-encoder", "source/bgfx/3rdparty/directx-headers"):
        for notice in (dependencies / folder).rglob("NOTICE*"):
            if notice.is_file():
                add(files, "licenses/" + folder.split("/")[-1] + "/" + notice.relative_to(dependencies / folder).as_posix(), notice)
    instructions = (
        "USUMStudio\n\nExtract the entire ZIP before starting the editor. Keep resources and "
        "viewport-shaders beside the executable. Game data is not included.\n\n"
        "The editor's original code is GPL-3.0-only licensed. Third-party components retain their "
        "own terms in licenses/. HIDAPI is distributed under its BSD option and the "
        "embedded stb code under its MIT option. Blender add-ons are distributed separately.\n\n"
        "Matching application source is supplied as USUMStudio-source.zip in the same GitHub Release. "
        "It includes build instructions and the pinned dependency source trees.\n\n"
    )
    if windows:
        instructions += "Run usum-viewport.exe. Install the Microsoft Visual C++ 2015-2022 x64 Redistributable if the runtime is missing.\n"
    else:
        instructions += (
            "Built on Ubuntu 24.04 x86-64; requires glibc 2.39 or newer and compatible system libraries. "
            "Uses X11 or XWayland and system graphics/audio drivers. This is not a fully static portable build.\n"
            "On Ubuntu: sudo apt-get install libx11-6 libxext6 libgl1 libasound2t64 libpulse0 libudev1\n"
            "Run ./usum-viewport. If your archive extractor drops executable permissions, run chmod +x usum-viewport.\n"
        )
    files["README.txt"] = instructions.encode()
    prefix = "USUMStudio-" + args.platform
    archive(args.output / (prefix + ".zip"), {prefix + "/" + k: v for k, v in files.items()}, [prefix + "/" + name, prefix + "/resources/pawn/" + compiler])


def blender(args):
    for module, suffix in (("usum_model", "model"), ("usum_collision", "collision")):
        files = {}
        source = ROOT / "tools/blender" / (module + ".py")
        ast.parse(source.read_text())
        add(files, module + "/__init__.py", source)
        add(files, module + "/LICENSE", ROOT / "tools/blender/LICENSE")
        files[module + "/README.txt"] = (
            "USUMStudio Blender add-on\n\nBlender 4.2 or newer: Preferences > Add-ons > "
            "Install from Disk. Select this ZIP from the GitHub Release, then enable the add-on.\n\n"
            "Licensed under GPL-3.0-or-later; see LICENSE. The included Python file is the add-on source.\n"
        ).encode()
        archive(args.output / ("USUMStudio-Blender-" + suffix + ".zip"), files)


def source(args):
    args.output.mkdir(parents=True, exist_ok=True)
    destination = args.output / "USUMStudio-source.zip"
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        for name in ("CMakeLists.txt", "README.md", "LICENSE", ".gitignore", ".gitattributes", "src", "cmake", "resources", "icon", "licenses", "tools/blender", "tools/pawn-release", ".github"):
            path = ROOT / name
            for file in ([path] if path.is_file() else sorted(path.rglob("*"))):
                if file.is_file() and "__pycache__" not in file.parts:
                    output.write(file, "USUMStudio/" + file.relative_to(ROOT).as_posix())
        for name in ("source", "imgui", "sdl3", "pawn-compiler"):
            path = args.dependencies / name
            if not path.is_dir():
                raise RuntimeError(f"Missing dependency source: {name}")
            for file in sorted(path.rglob("*")):
                if file.is_file() and ".git" not in file.parts and file.name != ".download-complete":
                    output.write(file, "USUMStudio/dependencies/" + name + "/" + file.relative_to(path).as_posix())
    with zipfile.ZipFile(destination) as output:
        if output.testzip() is not None:
            raise RuntimeError("Source archive integrity check failed")
    print(destination.name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=("editor", "blender", "source"))
    parser.add_argument("--platform", choices=("windows-x64", "linux-x64"))
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--dependencies", type=Path, default=ROOT / "dependencies")
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    if args.kind == "editor" and not args.platform:
        parser.error("editor packages require --platform")
    {"editor": editor, "blender": blender, "source": source}[args.kind](args)


if __name__ == "__main__":
    main()
