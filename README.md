# MasterFilm OFX Plugin

Film emulation OFX plugin for DaVinci Resolve.  
Two products, one codebase: **MasterFilm Pro** (colorists) and **MasterFilm Lite** (content creators).

---

## Project Structure

```
MasterFilm/
├── CMakeLists.txt              Root build
├── cmake/
│   ├── BundleLayout.cmake      .ofx.bundle assembly
│   └── Info.plist.in           macOS bundle plist template
├── src/
│   ├── MasterFilmPlugin.cpp/h  OFX entry points, plugin factory
│   ├── processors/             Five GPU shader pass classes
│   │   ├── GrainProcessor
│   │   ├── HalationProcessor
│   │   ├── AcutanceProcessor
│   │   ├── ToneProcessor
│   │   └── ColorProcessor
│   ├── presets/
│   │   ├── FilmPreset.h        Central data structure
│   │   ├── StockLibrary        8 Phase-1 stocks (fully data-sourced)
│   ├── ui/
│   │   ├── ProUI               Full OFX parameter surface
│   │   └── LiteUI              5-slider perceptual surface
│   └── platform/
│       ├── GLSLDispatch        OpenGL shader compilation + fullscreen quad
│       └── MetalDispatch.mm    Metal compute pipeline (macOS only)
├── shaders/
│   ├── glsl/
│   │   ├── tone_color.glsl
│   │   ├── halation_h.glsl
│   │   ├── halation_v.glsl
│   │   ├── grain.glsl
│   │   └── acutance.glsl
│   └── metal/                  (Metal shaders — compile to masterfilm.metallib)
└── openfx-sdk/                 Git submodule — AcademySoftwareFoundation/openfx
```

---

## Build — macOS

### Prerequisites
- Xcode 15+ (for Metal and Obj-C++)
- CMake 3.21+
- DaVinci Resolve installed (for testing — OFX bundle goes into `~/Library/OFX/Plugins`)

### 1. Clone and pull submodule

```bash
git clone <your-repo-url> MasterFilm
cd MasterFilm
git submodule update --init --recursive
```

> **Alternative:** If you have the OpenFX SDK locally already:
> ```bash
> cmake -DOFX_SDK_DIR=/path/to/openfx-sdk ..
> ```

### 2. Configure

```bash
mkdir build && cd build

# Standard (Pro + Lite, OpenGL + Metal)
cmake .. -DCMAKE_BUILD_TYPE=Release

# Pro only, skip Metal
cmake .. -DMASTERFILM_BUILD_LITE=OFF -DMASTERFILM_ENABLE_METAL=OFF

# Universal binary (Apple Silicon + Intel)
cmake .. -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
```

### 3. Build

```bash
cmake --build . --config Release -j$(sysctl -n hw.logicalcpu)
```

### 4. Install (copies bundle to ~/Library/OFX/Plugins)

```bash
cmake --install .
```

### 5. Test in Resolve

Relaunch DaVinci Resolve. In the Effects Library, search for **MasterFilm** — both Pro and Lite should appear under OpenFX.

---

## Build — Windows

### Prerequisites
- Visual Studio 2022
- CMake 3.21+
- OpenGL (ships with Windows SDK)

```bat
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DMASTERFILM_ENABLE_METAL=OFF
cmake --build . --config Release
cmake --install .
```

Plugin installs to `C:\Program Files\Common Files\OFX\Plugins\MasterFilm.ofx.bundle`.

---

## Metal Shaders (macOS only)

Metal `.metal` files in `shaders/metal/` must be compiled to a `.metallib` and placed in the bundle's `Resources/shaders/` directory.

A build script (`shaders/metal/compile.sh`) will be added in the next phase. For now:

```bash
xcrun -sdk macosx metal -c shaders/metal/grain.metal -o /tmp/grain.air
# ... repeat for each shader ...
xcrun -sdk macosx metallib /tmp/*.air -o build/MasterFilm.ofx.bundle/Contents/Resources/shaders/masterfilm.metallib
```

---

## Processing Pipeline

All five passes execute in order per frame:

| Pass | Processor      | GPU Shader          |
|------|----------------|---------------------|
| 1    | Color + Tone   | `tone_color.glsl`   |
| 2    | Halation H     | `halation_h.glsl`   |
| 3    | Halation V + composite | `halation_v.glsl` |
| 4    | Grain          | `grain.glsl`        |
| 5    | Acutance       | `acutance.glsl`     |

CPU fallback is available for all passes (used for parameter validation and headless testing).

---

## Phase 1 Film Stocks

| Stock                  | Category | Key validation target                   |
|------------------------|----------|-----------------------------------------|
| Ilford HP5 Plus 400    | B&W      | Foundational cubic grain reference      |
| Ilford FP4 Plus 125    | B&W      | PSD model at low ISO/RMS               |
| Kodak T-Max 100        | B&W      | T-grain morphology vs cubic            |
| Kodak Tri-X 400        | B&W      | Academic PSD literature match          |
| Kodak Vision3 500T     | Cinema   | Inter-layer coupling from SMPTE data   |
| Kodak Vision3 250D     | Cinema   | White balance path validation          |
| Fujifilm Velvia 50     | Slide    | MTF > 1.0 / Kostinsky adjacency        |
| Fujifilm Provia 100F   | Slide    | Neutral slide reference vs Velvia      |

---

## Open Questions (from design doc)

- [ ] Collapsible sections vs always-visible scrollable panel in Resolve UI
- [ ] Does selecting a film stock drive sliders to preset values, or is it a reference-only hint?
- [ ] Master blend/intensity slider — Pro only, Lite only, or both?

---

## Roadmap

- **Now:** Build Pro engine + Pro UI — all five characteristics, 8 stocks, GPU shaders
- **Next:** Lite UI mode, licensing layer
- **Phase 2:** Film stock identification algorithm (closest-stock matching)
