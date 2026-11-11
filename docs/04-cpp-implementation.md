# 04 · The C++ implementation in `src/`

Original code, written for this project.  It is a compact layer-based raster
painting program plus a reader/writer for CSP's `.clip` interchange format.
It is *not* a port of Clip Studio Paint: the architecture facts in doc 01
informed the shape (tiled rasters, command-based undo, blend-mode table,
SQLite-in-container files) but no CSP code was consulted or reproduced.

## 1. Layout

```
src/
├─ CMakeLists.txt
├─ core/                    painting engine, no OS dependencies
│  ├─ image.h               Color, Rect, Surface (straight-alpha RGBA8)
│  ├─ blend.{h,cpp}         18 blend modes (W3C compositing formulas)
│  ├─ document.{h,cpp}      Layer, Document (stack, compositing, clipping layers)
│  ├─ brush.{h,cpp}         Stroke: dab-based brush with pressure dynamics
│  ├─ undo.{h,cpp}          Command, PixelPatchCommand, layer commands, UndoStack
│  ├─ png.{h,cpp}           PNG load/save for Surface (wraps libclip's PNG code)
│  └─ zlib_lite.{h,cpp}     crc32, adler32, inflate, deflate (wraps libclip's zlib code)
├─ clip/clip_bridge.*       libclip ⇄ Document (layer conversion, blend modes, preview)
├─ tools/cliptool.cpp       command-line inspector/converter
├─ app/                     Windows application "Plastic Paint"
│  ├─ app_state.*           tools, strokes, layer operations, open/save (OS-free)
│  └─ main_win32.cpp        window, WM_POINTER pen input, GDI presentation
└─ tests/                   self-contained test runner

libclip/                    the .clip library, a separate project used as a
                            git submodule (see libclip/README.md)
```

`src/CMakeLists.txt` pulls libclip in with `add_subdirectory(../libclip)`
and links `libclip::clip`.  libclip is independent of everything in `src/`:
it has its own README, format reference (`libclip/docs/FORMAT.md`, formerly
doc 02), examples and tests, and Plastic Paint is one application built on
it.  Clone with `git clone --recurse-submodules`, or run
`git submodule update --init` in an existing checkout.
`src/CMakeLists.txt` pulls libclip in with `add_subdirectory(../libclip)`;
libclip also builds on its own (`cmake -S libclip -B build-libclip`, add
`-DBUILD_SHARED_LIBS=ON` for a DLL).

Build (Visual Studio 2026 Build Tools / any C++20 compiler):

```
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
build\pu_tests.exe
build\libclip\libclip_tests.exe yuzuko.clip
```

## 2. Painting engine (`core/`)

* **Surface** is a plain RGBA8 buffer with straight alpha; `crop`/`blit`
  give cheap rectangular copies used by undo patches.
* **Blend modes**: `blendPixel()` implements the separable modes (Multiply,
  Screen, Overlay, Darken, Lighten, ColorDodge, ColorBurn, HardLight,
  SoftLight, Difference, Exclusion, Add, Subtract) and the non-separable
  ones (Hue, Saturation, Color, Luminosity) from the W3C *Compositing and
  Blending* specification, then applies source-over with the backdrop alpha.
  `clip::blendModeFromComposite()` maps CSP's `LayerComposite` indices onto
  these modes.
* **Document** composites bottom-up over an optional paper colour; a layer
  flagged `clipToBelow` is masked by the alpha of the layer beneath it
  (CSP's "clip to layer below").
* **Brush**: a stroke snapshots the target layer, accumulates dab coverage
  in a float buffer with `max()`, and rewrites the dirty region as
  `original ⊕ (colour, coverage × opacity)`.  Pressure scales size
  (`minSizeRatio`) and opacity (`minOpacityRatio`); dabs are spaced at
  `spacing × diameter`; hardness sets the solid core radius with a
  smoothstep falloff; exponential smoothing damps pen jitter; eraser mode
  attenuates alpha instead of painting.
* **Undo**: every edit is a `Command` object that has already been applied
  when it is pushed.  `PixelPatchCommand` stores before/after sub-rectangles
  so redo never re-runs the brush; `AddLayerCommand`, `RemoveLayerCommand`,
  `MoveLayerCommand` and `LayerPropertyCommand` cover structure and
  properties.  The stack keeps the last 200 commands.

## 3. How Plastic Paint uses libclip

libclip (`libclip/`, documented in `libclip/README.md`) reads and writes
`.clip` files; it knows nothing about the engine.  The application side is
`src/clip/clip_bridge.*`, which converts between libclip's structs and
`Document`: layers, blend modes (`clip_composite` ⇄ the engine's
`BlendMode`), opacity and visibility.  libclip does not composite, so the
bridge supplies the engine's flattened image as the `CanvasPreview`.
`core/zlib_lite.*` and `core/png.*` are thin wrappers over libclip's
zlib and PNG functions, so the app carries no second implementation.

Notes on libclip's implementation that matter to this project:

* The SQLite image is opened with `sqlite3_deserialize(…, READONLY)`
  directly from the container bytes, so no temporary file is written.
* All column reads are by name and tolerant of missing columns, because
  CSP evolves its schema by `ALTER TABLE … ADD COLUMN`.
* Tile layout is inferred from the inflated size (5, 4, 2 or 1 planes of
  256 × 256) and reported through `clip_pixel_layout`.
* `csp_schema_data.h` is generated by `gen/gen_csp_schema.py` from a CSP
  document and holds only schema metadata (table definitions and the
  `ElemScheme`/`ParamScheme` registry) so written files declare what CSP expects.
* The writer computes `ExternalChunk.Offset` before serialising the database
  so the bookkeeping table is correct; a test verifies that each offset
  lands on a `CHNKExta` tag.
* `clip_zlib.c` is the project's only zlib implementation: `core/` calls it
  through `zlib_lite.h` and `png.h`.  It compresses with LZ77 (hash chains over the 32 KiB window,
  one-step lazy matching) and, per block, emits whichever of dynamic
  Huffman, fixed Huffman or stored is smallest.  Code lengths are limited to
  15 bits by halving frequencies until the limit holds.  Output is checked by
  round-trip tests and against CPython's zlib.  The C port produces
  byte-identical `.clip` files to the earlier C++ implementation
  (checked on a round trip of `yuzuko.clip` and a PNG import).

## 4. `cliptool`

```
cliptool info <file.clip>                  canvas and layer tree
cliptool chunks <file.clip>                CHNK list with offsets and external ids
cliptool tables | schema [table] | dump <table> [limit]
cliptool extract-db <file.clip> <out.sqlite>
cliptool extract-chunk <file.clip> <extrnlid…> <out.bin>
cliptool offscreen <file.clip> <layer#>    decode one layer, report pixel stats
cliptool preview <file.clip> <out.png>     embedded CanvasPreview
cliptool export <file.clip> <out.png>      flattened composite
cliptool export-layers <file.clip> <dir>   one PNG per raster layer
cliptool create <out.clip> <a.png> [b.png …]
cliptool roundtrip <in.clip> <out.clip>
```

`schema`, `tables` and `dump` work on any `.clip`, including ones from CSP,
and are the quickest way to confirm or extend doc 02 when a CSP-written file
is available.

Paths may contain any Unicode characters (e.g. Japanese file and folder
names).  On Windows, `cliptool` reads its arguments from the UTF-16 command
line (`CommandLineToArgvW`) rather than the ANSI `argv` the C runtime
provides, converts them to UTF-8 for libclip, and sets the console output to
UTF-8 so layer names and paths print correctly.

## 5. `plastic_paint` (Windows)

A single window: canvas view on the right, panel on the left, status bar
below.  Pen input arrives through `WM_POINTERDOWN/UPDATE/UP` with pressure
from `GetPointerPenInfo` (mouse falls back to full pressure).  The composite
is cached in a BGRA buffer and only the stroke's dirty rectangle is
recomposited and invalidated per input event.

| Keys | Action |
|---|---|
| `B` / `E` | brush / eraser |
| `[` `]` (`Shift` for bigger steps) | brush size |
| `{` `}` | hardness · `,` `.` brush opacity |
| click swatch, `X` | pick colour / swap with white |
| `Ctrl+Z` / `Ctrl+Y` | undo / redo |
| `N`, `Delete`, `Ctrl+D`, `Ctrl+E` | new layer, delete, duplicate, merge down |
| `PgUp` / `PgDn`, `Ctrl+Up/Down` | select / move layer |
| `V`, `M`/`Shift+M`, `+`/`-`, `C` | visibility, blend mode, opacity, clip to below |
| `Ctrl+Delete` | clear layer |
| `Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Shift+S` | new, open (.clip/.png), save, save as |
| wheel, `Space`+drag or right-drag, `F` | zoom, pan, fit |

Drag-and-drop of a `.clip` or `.png` onto the window opens it, and so does
passing a path on the command line (`plastic_paint.exe "C:rt\絵.clip"`).
The command line is split with `CommandLineToArgvW`: the first argument is
opened and any further ones are ignored.  Paths containing spaces must be
quoted, as Explorer's "Open with" does.  File dialogs, drag-and-drop and the
command line all hand UTF-8 paths to the loaders, so non-ASCII names work
everywhere.

## 6. Verification status

* `libclip_tests` (C): container round trip, tile encode/decode, a full
  write → read of a document (UTF-16 names, `ExternalChunk` offsets,
  preview, determinism), and decoding `yuzuko.clip` when present.
* `pu_tests`: tests covering checksums, inflate against CPython-generated
  fixed- and dynamic-Huffman streams, blend formulas, compositing order,
  clipping layers, brush opacity/pressure/eraser, undo/redo, PNG round-trip,
  blend-mode mapping and a full Document → .clip → Document round-trip (including `ExternalChunk` offsets
  and blend/opacity/visibility preservation).  All pass.
* `cliptool create/info/export/preview` were run on generated PNGs and the
  flattened result matched the inputs visually.
* `plastic_paint` opened the generated `.clip` and rendered it.
* Non-ASCII paths: `plastic_paint` saved to and reopened
  `テスト\保存テスト.clip` (Save As dialog and command line, including a
  command line with an extra argument), and every `cliptool` command that
  takes a path was run on Japanese file and folder names.  A `.clip` created
  through such a path is byte-identical to one created through an ASCII path.
* Reading a file written by Clip Studio Paint 5.1 (`yuzuko.clip`): the
  decoded, flattened canvas matches CSP's own embedded preview of that file
  on all 1 920 000 pixels (within one 8-bit level).
* `previewprobe.exe` loads CSP's `ClipPreview.dll` in-process and reports
  whether it renders a given file; it is the automated acceptance check.
* Clip Studio Paint 5.1 opens files written by `cliptool` and `plastic_paint`
  (round trip of a CSP document and new documents), confirmed 2026-11-10.
  An earlier writer (wrong header order, big-endian tile length, minimal
  schema) was rejected with "Unsupported file format"; the current writer
  mirrors the CSP document structure, and it is the one CSP opens.  The C
  port in `libclip/` writes byte-identical files, so that result carries
  over to it.
