# Plastic Paint

Plastic Paint is a small layer-based painting program for Windows, written
in C++20. It has pressure-sensitive pen input, 18 blend modes, clipping
layers and undo. It opens and saves Clip Studio Paint `.clip` files and PNG.

`.clip` support comes from [libclip](libclip/), a standalone C library for
the CLIP STUDIO FORMAT that is included as a git submodule. Plastic Paint is
one application built on it. Files Plastic Paint saves open in Clip Studio
Paint 5.1.

Plastic Paint is original code. It is not a port of Clip Studio Paint and
is not affiliated with CELSYS. "Clip Studio Paint" and "CLIP STUDIO FORMAT"
are trademarks of CELSYS, Inc.

<img width="1967" height="1223" alt="image" src="https://github.com/user-attachments/assets/f14ec738-facc-438c-9279-5bc8c969b146" />

## Features

* **Pen input** through `WM_POINTER`, with pressure controlling brush size
  and opacity. A mouse paints at full pressure.
* **Brush and eraser** with size, hardness and opacity, plus smoothing that
  damps pen jitter.
* **Layers**: add, delete, duplicate, merge down, reorder, hide, clear. Each
  layer has opacity, a blend mode and an optional "clip to layer below".
* **Folders, layer masks and non-raster layers**: folders (with
  pass-through), layer masks that every pixel tool can paint on, fill
  layers, gradient layers (linear or radial, with repeat and flip) and
  correction layers: Brightness/Contrast, Level correction, Tone curve,
  Hue/Saturation/Luminosity, Color balance, Reverse gradient, Posterization,
  Binarization and Gradient map. All of them are edited live from the Layer
  settings dialog and saved to `.clip`.
* **Selections**: rectangle, ellipse and lasso, with add, subtract and
  invert; tools, fills and gradients respect them, and a selection can
  become a layer mask.
* **28 blend modes**: the W3C *Compositing and Blending* set (Normal,
  Multiply, Screen, Overlay, Darken, Lighten, Color Dodge, Color Burn, Hard
  Light, Soft Light, Difference, Exclusion, Add, Subtract, Hue, Saturation,
  Color, Luminosity) plus Clip Studio Paint's Linear Burn, Darker Color,
  Lighter Color, Glow Dodge, Add (Glow), Vivid Light, Linear Light, Pin
  Light, Hard Mix and Divide.
* **Layer Property effects**: gray expression colour, layer colour and
  border effect, shown without changing the pixels.
* **Undo and redo** of the last 200 edits, including layer changes, with a
  History palette.
* **Files**: opens `.clip` and `.png`, and saves `.clip` and `.png`. Layer
  names, folders, masks, blend modes, opacity, visibility, effects and the
  settings of fill, gradient and correction layers survive the round trip to
  Clip Studio Paint.
* **Unicode paths** everywhere: file dialogs, drag and drop and the command
  line.

## Building

You need Windows 10 or later, Visual Studio 2026 (or its Build Tools) with
the C++ workload, and CMake 3.20 or later. Ninja ships with Visual Studio.

```bat
git clone --recurse-submodules <repository URL>
cd "Plastic Utopia Din"
```

If you already cloned without `--recurse-submodules`, fetch libclip with
`git submodule update --init`.

From an **x64 Native Tools Command Prompt**:

```bat
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

This builds:

| Output | What it is |
|---|---|
| `build\plastic_paint.exe` | The painting application |
| `build\cliptool.exe` | Command-line `.clip` inspector and converter |
| `build\pu_tests.exe` | Tests for the painting engine and the `.clip` bridge |
| `build\libclip\libclip_tests.exe` | libclip's own tests |
| `build\previewprobe.exe` | Checks whether Clip Studio Paint's Explorer preview handler can read a file (needs Clip Studio Paint installed) |

The engine, `cliptool` and the tests don't depend on Windows. Only the
application and `previewprobe` do.

If a Clip Studio Paint document named `yuzuko.clip` is in the repository
root, the libclip tests also check that it reads correctly. The file isn't
included.

## Using Plastic Paint

Run `plastic_paint.exe`, optionally with a file to open:

```bat
plastic_paint.exe "C:\art\picture.clip"
```

You can also drag a `.clip` or `.png` onto the window. The workspace follows
Clip Studio Paint: tool and colour palettes on the left, the canvas in the
middle, and the Layer and History palettes on the right. Palettes can be
docked on either side or hidden, and the Window menu switches between the
dark and light theme.

Non-raster layers are created from the Layer menu (new fill, gradient or
correction layer) and edited in the **Layer settings** dialog, which also
opens by double-clicking the layer's thumbnail. Right-click a layer for its
context menu, including the layer mask commands. Press `F1` for the full
shortcut list.

| Keys | Action |
|---|---|
| `P`, `B`, `E`, `G`, `K`, `M`, `I`, `H`, `R`, `/` | Pen, brush, eraser, fill/gradient, move layer, selection, eyedropper, hand, rotate, zoom |
| `[` `]` | Brush size |
| `,` `.` | Next / previous sub tool |
| `Ctrl+[` `Ctrl+]` | Brush opacity |
| `X`, `C` | Swap main and sub colour / transparent colour |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo |
| `Ctrl+Shift+N`, `Ctrl+E`, `Ctrl+Shift+E` | New raster layer, merge down, merge visible |
| `Alt+]` / `Alt+[` | Select upper / lower layer |
| `Ctrl+Alt+G`, `Ctrl+M` | Clip at layer below, edit layer mask |
| `Delete`, `Ctrl+Delete` | Clear layer, clear outside the selection |
| `Ctrl+A`, `Ctrl+D`, `Ctrl+Shift+I` | Select all, deselect, invert selection |
| `Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Shift+S`, `Ctrl+W` | New, open, save, save as, close |
| Mouse wheel, `Ctrl+0`, `Ctrl+Alt+0`, `-` / `^` | Zoom, fit, 100 %, rotate the view |
| `Space`+drag or right-drag | Pan |
| `Tab` | Hide all palettes |

## cliptool

`cliptool` inspects and converts `.clip` files from the command line:

```
cliptool info <file.clip>                  canvas and layer tree
cliptool chunks <file.clip>                chunk list with offsets and external ids
cliptool tables | schema [table] | dump <table> [limit]
cliptool extract-db <file.clip> <out.sqlite>
cliptool extract-chunk <file.clip> <extrnlid…> <out.bin>
cliptool offscreen <file.clip> <layer#>    decode one layer, report pixel stats
cliptool preview <file.clip> <out.png>     embedded preview image
cliptool export <file.clip> <out.png>      flattened image
cliptool export-layers <file.clip> <dir>   one PNG per raster layer
cliptool compare <file.clip> [sheet.png]   diff the composite against CSP's saved preview
cliptool create <out.clip> <a.png> [b.png …]
cliptool roundtrip <in.clip> <out.clip>
```

`info` shows each layer's kind, mask state and the decoded settings of
correction and gradient layers. `tables`, `schema` and `dump` work on any
`.clip`, including files saved by Clip Studio Paint, so they are the
quickest way to look at the parts of the format libclip doesn't model yet.

## Repository layout

```
src/
  core/        painting engine: images, blend modes, documents, brush, undo
               (no OS dependencies)
  clip/        clip_bridge: converts between libclip and the engine's Document
  app/         the Windows application (app_state: OS-free logic; ui_*: Win32 workspace)
  tools/       cliptool, previewprobe
  tests/       engine, bridge and app-state tests
libclip/       the .clip library (git submodule; has its own README)
docs/          research notes on Clip Studio Paint and design notes for src/
```

## Documentation

* [`docs/04-cpp-implementation.md`](docs/04-cpp-implementation.md): how
  the engine, the bridge, `cliptool` and the application work.
* [`libclip/README.md`](libclip/README.md): the `.clip` library and its API.
* [`libclip/docs/FORMAT.md`](libclip/docs/FORMAT.md): the `.clip` file
  format, including the rules Clip Studio Paint checks when it opens a file.
* [`docs/README.md`](docs/README.md): index of the research notes on how
  Clip Studio Paint is built (architecture and plug-in interfaces), from a
  static survey of the installed program. No Clip Studio Paint code was
  decompiled or reused.

## Status and limits

* Vector, text, tone, 3-D and other special layers in a `.clip` file open
  as their rendered pixels (a raster layer); they cannot be edited as
  vectors or text, and are saved back as raster layers.
* 8-bit RGBA documents only, not 16-bit or CMYK.
* Gradient layers from Clip Studio Paint are read when they are linear
  (the only shape seen in CSP files); a radial gradient layer is saved as
  pixels. A tone curve with per-channel curves is shown as read, and editing
  it replaces those curves with one master curve. The Posterization and
  Binarization file layouts are assumed, as no Clip Studio Paint file with
  one was available.
* Clip Studio Paint renders correction and gradient layers itself when it
  opens a file, so small differences from Plastic Paint's rendering of the
  same settings are expected.
* Windows only for the application. The engine and `cliptool` are portable
  C++20.

## License

Plastic Paint is released under the [MIT License](LICENSE). libclip has its
own copy of the same license in [`libclip/LICENSE`](libclip/LICENSE), and its
bundled SQLite is in the public domain.
