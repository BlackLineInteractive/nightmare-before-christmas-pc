# OogieEngine - *The Nightmare Before Christmas: Oogie's Revenge*

### Native C++17 Reverse-Engineering Toolkit & PC Reimplementation Engine

[![C++17](https://img.shields.io/badge/Language-C%2B%2B17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-lightgrey.svg)]()
[![Graphics](https://img.shields.io/badge/Renderer-OpenGL%203.3%20%2F%20Raylib-orange.svg)]()
[![UI](https://img.shields.io/badge/GUI-Dear%20ImGui-purple.svg)](https://github.com/ocornut/imgui)

<p align="center">
  <img src="media/cover.webp" alt="The Nightmare Before Christmas: Oogie's Revenge" width="320">
</p>

**OogieEngine** is a modern, high-performance reverse-engineering toolkit and native game engine recreation for Capcom's 2004–2005 classic: ***Tim Burton's The Nightmare Before Christmas: Oogie's Revenge*** (Original Xbox / PS2).

Inspired by modern engine reconstruction projects such as [*OpenGOAL*](https://github.com/open-goal/jak-project), [*OpenLara*](https://github.com/XProger/OpenLara), and [*Climax Game Engine Toolkit*](https://github.com/BlackLineInteractive/Climax_Game_Engine_Toolkit), this project extracts, decodes, and renders the game's proprietary **Capcom RD3 Engine** formats at high framerates with customizable shaders, a comprehensive Dear ImGui debugging suite, and a roadmap toward a full playable PC port.

---

## Key Features

- **Bitstream LZ Type 2 Decompressor:** Fast, pure Python & C++ bit-level dictionary decompressor for all `.lz` game archives.
- **Recursive `@STM` Unpacker:** Parses multi-file Capcom stage packages containing geometry, textures, scripts, sounds, and collision meshes.
- **3D Level & Mesh Renderer:** Full hardware-accelerated rendering of Capcom's `P2OD` 3D model formats with coordinate system correction (negative Y to OpenGL Y-up) and automatic surface normal recalculation.
- **Triangle Strip `0x8000` Reconstruction:** Reverse-engineered 4th vertex coordinate flag to cleanly reconstruct triangle strips, handle restart points, and eliminate degenerate polygon spikes.
- **PS2 GS Vertex Color Modulation:** Decodes baked vertex ambient occlusion and light values mapped to the PS2 Graphics Synthesizer $0..128$ intensity scale.
- **Two-Pass Scene Graph:**
  - **Pass 1 (Opaque & Cutout):** Renders environmental geometry with alpha masking (`discard if alpha < 0.20`) and depth writes.
  - **Pass 2 (Additive & Blended):** Renders spotlights, lantern cones, glows, and glares (`ADD100`, `GLARE`) with `GL_ONE, GL_ONE` additive blending and disabled depth writes.
- **Dual Camera Navigation:**
  - **Turntable Orbit (LMB Drag):** Natural orbit with configurable pitch/yaw inversion and exponential wheel zoom.
  - **Unreal/Unity Fly Navigation (RMB Hold + WASD):** 6-DOF freecam flight with Shift boost, Q/E vertical traversal, and dynamic speed adjustment.
- **Modern UI Suite (Dear ImGui):**
  - **Stage Browser:** Explore 365 cataloged stages categorized across Halloween Town, Hinterlands, Dr. Finkelstein's Lab, Oogie's Casino, Christmas Town, and the Graveyard.
  - **Model Hierarchy Inspector:** Toggle visibility of individual props, inspect vertex/triangle counts, and focus camera bounds with one click.
  - **Texture Gallery:** Real-time thumbnail preview of all DXT1/DXT5 and `.pig` textures.
  - **Story & Dialogue Reader:** View Japanese/English dialogue scripts from stage `.bms` and `.txt` files.

---

## Technical Documentation

Full reverse-engineering analysis and format specifications are documented in:
👉 **[`docs/REVERSE_ENGINEERING_SPECIFICATION.md`](docs/REVERSE_ENGINEERING_SPECIFICATION.md)**

Topics covered:

1. **Executable Analysis (`default.xbe`):** Native 32-bit x86 Intel architecture, 31 sections, Capcom source code tree (`\Project\nightmareXbox\app\...`), player states (`JACK_00`..`JACK_16`).
2. **Container Formats:** Capcom LZ Type 2 decompression algorithm and `@STM` format specification.
3. **Model Formats:** `P2OD` (static stages), `BMD` (skinned skeletal characters), and `PMT` (134 skeletal animation files).
4. **Material Grammar:** Semantic flags in `scene/bg_list.txt` (`ADD100`, `GLARE`, `ADDZERO_64`, `ALPHA`, `Z_OFF`, `TEX_SCROLL`).
5. **Collision & Audio:** `BGatari.amd` environment collision, `CAMatari.amd` camera obstruction, and Microsoft XACT audio banks (`.xwb` / `.xsb`).
6. **Xbox Dependency Map:** Section layout, XDK 5849 libraries, where the Xbox kernel is called from, NV2A register access in D3D.
7. **Native PC Port Roadmap:** Binary port of `default.xbe` plus the phased reimplementation plan.

---

## Building & Running

### Prerequisites

- **C++17 Compiler:** Clang 14+ (macOS Xcode), GCC 10+, or MSVC 2019+
- **CMake:** Version 3.20 or newer
- **Raylib:** Version 5.0+ (`brew install raylib` on macOS)

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/vovavovchok/oogie-capcom-engine.git
cd oogie-capcom-engine

# Configure and compile
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
```

### Running the Viewer

```bash
# Launch interactive viewer (loads Halloween Town by default)
./build/oogie_viewer

# Or launch directly into a specific stage (e.g. Dr. Finkelstein's Lab)
./build/oogie_viewer rom/structured_data/stages/020a
```

---

## Native PC Executable (Binary Port of `default.xbe`)

Goal: run the **original game code** on PC by patching the executable itself — no emulator, no HLE layer, no recompilation. Game logic stays byte-for-byte original, the game reads its original files through its original file system, and the Xbox-only libraries (graphics, sound, input) are replaced with our own.

### Generate the flat PE

```bash
python3 tools/build_flat_pe.py
```

Produces `rom/extracted/default_pc.exe`: the XBE mapped 1:1 at its native base `0x00010000` as a single PE section, so every absolute address in the game code stays valid. Patches applied on the image:

1. **NV2A spin-wait (`0x001dc611`):** `D3D_WaitGPUReady` → `xor eax, eax; ret`.
2. **NV2A device init (`0x001dc2d1`):** stripped.
3. **NV2A chip detection (`0x001dc2fc`):** returns a fixed chip ID.
4. **PMC interrupt status reads (`0xFD40014C`)** at `0x001dbc1d`, `0x001dbe5f`, `0x001dc00f`: zeroed.
5. **XPP USB MMIO write (`0x0025c41f`):** NOPed.
6. **KPCR accesses (`fs:[0x20]`, `fs:[0x28]`):** redirected to a dummy block.
7. **Boot:** XBE entry (`0x001bf647`) jumps straight to the RD3 main thread routine (`0x001bf5d3`).

> **Status:** the PE still imports the 125 Xbox kernel ordinals (`xboxkrnl.dll`), which does not exist on Windows, so the exe does **not** start yet. See *Porting route* below.

### Locate Xbox SDK library functions

```bash
git clone --depth 1 https://github.com/Cxbx-Reloaded/XbSymbolDatabase.git /path/to/XbSymbolDatabase
python3 tools/xbsymscan.py /path/to/XbSymbolDatabase/src/OOVPADatabase rom/extracted/default.xbe syms.txt
```

Reads the XbSymbolDatabase signature tables as data (nothing is compiled) and prints the address, calling convention and parameter count of each XDK function found in `default.xbe`. On this build (XDK 5849) it locates **361** symbols, e.g. `Direct3D_CreateDevice` `0x001d8ee0`, `D3DDevice_Clear` `0x001d7610`, `D3DDevice_Swap` `0x001d7460`, `DirectSoundCreate` `0x002076aa`, `XInputGetState` `0x00259aa5`.

### Porting route

Static analysis shows **the game code itself (`0x12000`–`0x1A0000`) never calls the Xbox kernel**. All 125 kernel imports are used only by statically linked Microsoft libraries:

| Library | Location | Kernel call sites | Replacement |
| :--- | :--- | ---: | :--- |
| XAPI + LIBC | `.text` `0x1A0000`–`0x1D22A8` | 320 | XAPI is a Win32 subset → jump to the same function in Windows `kernel32.dll` |
| D3D8 | `D3D` | 37 | own renderer |
| DirectSound | `DSOUND` | 186 | own audio |
| XACT | `XACTENG` | 140 | own audio |
| XPP (gamepad) | `XPP` | 160 | own input |
| WMA decoder | `WMADEC` | 13 | own audio |

So the Xbox kernel can be dropped entirely: XAPI functions become direct jumps into Windows, `D:\` paths are pointed at the game folder, XAPI start-up (`XapiInitProcess`, partition checks, cache drive) is skipped, and D3D / DSound / XACT / XPP are swapped for our renderer, audio and input.

---

## Camera Controls

| Action | Control |
| :--- | :--- |
| **Turntable Orbit** | **LMB Drag** (Left Mouse Button) |
| **First-Person Fly** | **Hold RMB + WASD** (Right Mouse Button + Keys) |
| **Vertical Fly (Up/Down)** | **Q / E** (while holding RMB) |
| **Fly Speed Boost** | **Shift** |
| **Fly Speed Adjust** | **Mouse Wheel** (while holding RMB) |
| **Pan View** | **MMB Drag** (Middle Click) or **Shift + LMB** |
| **Zoom View** | **Mouse Wheel** |
| **Focus All Bounds** | **F** |
| **Toggle Ground Grid** | **G** |
| **Render Modes** | **1** (Textured), **2** (Vertex Color), **3** (Wireframe), **4** (Flat), **5** (Normals) |

---

## Project Structure

```
├── include/OogieEngine/     # Public C++ Header API
│   ├── Core/                # CPU Data Types (Vec3, Mat4, Vertex, ModelData, StageConfig)
│   ├── Loader/              # Format Parsers (PodLoader, SceneLoader)
│   ├── Rendering/           # GPU Abstractions (GPUMesh, GPUTexture, Camera)
│   ├── SG/                  # Scene Graph (SceneNode, StageScene)
│   ├── UI/                  # Panels (StageBrowser, ModelInspector, TextureGallery, Dialogue)
│   └── Viewer/              # Application State & ViewerApp
├── src/                     # C++ Implementation Files
├── docs/                    # Reverse-Engineering Specifications & Documentation
├── rom/
│   ├── extracted/           # 100% Unpacked Xbox Disc (ISO payload, default.xbe)
│   └── structured_data/     # Structured Stages (0000..0c00) with Catalog
└── tools/
    ├── capcom_tool.py       # Bitstream LZ & @STM Extraction Utility
    ├── structure_stm.py     # Stage Structuring & Catalog Generation Tool
    ├── build_flat_pe.py     # default.xbe -> flat 1:1 PE + hardware patches
    └── xbsymscan.py         # Locate XDK library functions via XbSymbolDatabase
```

---

## License

This reverse-engineering toolkit is developed for educational and preservation purposes.  
*Tim Burton's The Nightmare Before Christmas: Oogie's Revenge* is the property of Disney Interactive, Buena Vista Games, and Capcom Co., Ltd.
