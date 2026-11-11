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

## Features

* **Pen input** through `WM_POINTER`, with pressure controlling brush size
  and opacity. A mouse paints at full pressure.
* **Brush and eraser** with size, hardness and opacity, plus smoothing that
  damps pen jitter.
* **Layers**: add, delete, duplicate, merge down, reorder, hide, clear. Each
  layer has opacity, a blend mode and an optional "clip to layer below".
* **18 blend modes**: Normal, Multiply, Screen, Overlay, Darken, Lighten,
  Color Dodge, Color Burn, Hard Light, Soft Light, Difference, Exclusion,
  Add, Subtract, Hue, Saturation, Color and Luminosity, following the W3C
  *Compositing and Blending* formulas.
* **Undo and redo** of the last 200 edits, including layer changes.
* **Files**: opens `.clip` and `.png`, and saves `.clip`. Layer names, blend
  modes, opacity and visibility survive the round trip to Clip Studio Paint.
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

You can also drag a `.clip` or `.png` onto the window. The window has the
layer and tool panel on the left, the canvas on the right and a status bar
below.

| Keys | Action |
|---|---|
| `B` / `E` | Brush / eraser |
| `[` `]` (`Shift` for bigger steps) | Brush size |
| `{` `}` | Hardness |
| `,` `.` | Brush opacity |
| Click a swatch, `X` | Pick a colour / swap with white |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo |
| `N`, `Delete`, `Ctrl+D`, `Ctrl+E` | New layer, delete, duplicate, merge down |
| `PgUp` / `PgDn`, `Ctrl+Up` / `Ctrl+Down` | Select / move layer |
| `V`, `M` / `Shift+M`, `+` / `-`, `C` | Visibility, blend mode, opacity, clip to layer below |
| `Ctrl+Delete` | Clear layer |
| `Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Shift+S` | New, open, save, save as |
| Mouse wheel, `Space`+drag or right-drag, `F` | Zoom, pan, fit to window |

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
cliptool create <out.clip> <a.png> [b.png …]
cliptool roundtrip <in.clip> <out.clip>
```

`tables`, `schema` and `dump` work on any `.clip`, including files saved by
Clip Studio Paint, so they are the quickest way to look at the parts of the
format libclip doesn't model yet.

## Repository layout

```
src/
  core/        painting engine: images, blend modes, documents, brush, undo
               (no OS dependencies)
  clip/        clip_bridge: converts between libclip and the engine's Document
  app/         the Windows application
  tools/       cliptool, previewprobe
  tests/       engine and bridge tests
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

* Raster layers only. Vector, text, tone and other special layers in a
  `.clip` file open as empty layers, with the reason added to the layer
  name.
* 8-bit RGBA documents only, not 16-bit or CMYK.
* No folders or masks. When a `.clip` opens, folders are dropped (their
  layers stay, in order) and layer masks are applied to the layer's pixels.
  Plastic Paint saves a flat list of layers.
* Windows only for the application. The engine and `cliptool` are portable
  C++20.

## License

Plastic Paint is released under the [MIT License](LICENSE). libclip has its
own copy of the same license in [`libclip/LICENSE`](libclip/LICENSE), and its
bundled SQLite is in the public domain.
