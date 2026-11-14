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
│  ├─ blend.{h,cpp}         28 blend modes (W3C formulas plus CSP's extra modes)
│  ├─ document.{h,cpp}      Layer (raster, folder, fill, gradient, correction; masks),
│  │                        Document (stack, folders, compositing, selection)
│  ├─ brush.{h,cpp}         Stroke: dab-based brush with pressure dynamics
│  ├─ undo.{h,cpp}          Command, PixelPatchCommand, layer/mask/selection commands, UndoStack
│  ├─ png.{h,cpp}           PNG load/save for Surface (wraps libclip's PNG code)
│  └─ zlib_lite.{h,cpp}     crc32, adler32, inflate, deflate (wraps libclip's zlib code)
├─ clip/clip_bridge.*       libclip ⇄ Document (layer conversion, blend modes, preview)
├─ tools/cliptool.cpp       command-line inspector/converter
├─ tools/previewprobe.cpp   loads CSP's preview handler to check a file opens
├─ app/                     Windows application "Plastic Paint"
│  ├─ app_state.*           tools, strokes, selections, layer operations, open/save (OS-free)
│  ├─ main_win32.cpp        window, WM_POINTER pen input, GDI presentation
│  ├─ ui_*.cpp              palettes, menus, canvas view, commands, dialogs
│  └─ ui_layer_settings.cpp the Layer settings dialog for fill, gradient and correction layers
└─ tests/                   self-contained test runner (core, bridge, app state)

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
  Blending* specification, plus CSP's Linear burn, Darker/Lighter color,
  Glow dodge, Add (Glow), Vivid/Linear/Pin light, Hard mix and Divide, then
  applies source-over with the backdrop alpha.
  `clip::blendModeFromComposite()` maps CSP's `LayerComposite` indices onto
  these modes.
* **Layer kinds** (`LayerKind`): a raster layer owns a canvas-sized
  `Surface`; a folder owns nothing and groups the layers below it with a
  greater `depth` (the stack is flat, a folder sits directly above its
  contents); a fill layer is one colour; a gradient layer is a stop list with
  a start and end point, linear or radial, with none/repeat/flip beyond the
  ends; a correction layer (`Adjustment`) recolours everything composited
  below it.  `Document::renderLayer()` produces a layer's pixels for a clip
  rectangle whatever its kind, so the compositor and thumbnails treat them
  alike.
* **Corrections**: Brightness/Contrast, Level correction, Tone curve (a
  monotone cubic spline through control points, `applyCurve()`),
  Hue/Saturation/Luminosity, Color balance (per-channel shadows / midtones /
  highlights with optional luminance preservation), Reverse gradient,
  Posterization, Binarization and Gradient map.  Table-driven kinds build
  R, G, B lookup tables in `rebuild()`; the parameters CSP stored are kept in
  `raw` so a file round-trips byte-for-byte until the layer is edited.
* **Layer masks**: any layer may own a canvas-sized mask `Surface` whose
  alpha is how much it *hides* (CSP's convention: painting on the mask
  masks, erasing reveals), so every pixel tool works on a mask unchanged.
  `maskEnabled` turns it off without deleting it.
* **Document** composites bottom-up over an optional paper colour.  Folders
  are composited into their own buffer and blended as one layer unless
  `passThrough` ("Through").  A layer flagged `clipToBelow` is masked by the
  alpha of the layer beneath it (CSP's "clip to layer below").  Layer
  Property effects (gray expression colour, layer colour, border) are applied
  at composite time and leave the pixels unchanged.  A `Selection` (a
  coverage mask) limits where pixel tools act.
* **Brush**: a stroke snapshots the target layer, accumulates dab coverage
  in a float buffer with `max()`, and rewrites the dirty region as
  `original ⊕ (colour, coverage × opacity)`.  Pressure scales size
  (`minSizeRatio`) and opacity (`minOpacityRatio`); dabs are spaced at
  `spacing × diameter`; hardness sets the solid core radius with a
  smoothstep falloff; exponential smoothing damps pen jitter; eraser mode
  attenuates alpha instead of painting.
* **Undo**: every edit is a `Command` object that has already been applied
  when it is pushed.  `PixelPatchCommand` stores before/after sub-rectangles
  (of the pixels or of the mask) so redo never re-runs the brush;
  `AddLayerCommand`, `RemoveLayerCommand`, `MoveLayerCommand` and
  `ReorderCommand` cover structure; `LayerPropertyCommand` covers every
  non-pixel property including fill colour, gradient and correction
  settings; `LayerMaskCommand` adds, deletes or applies a mask;
  `LayerSnapshotCommand` swaps a whole layer (rasterize, merge);
  `SelectionCommand` changes the selection; `MacroCommand` groups several.
  Live edits from a dialog (dragging a slider) are folded into one step by
  `AppState::beginPropertyEdit()/endPropertyEdit()`.  The stack keeps the
  last 200 commands.

## 3. How Plastic Paint uses libclip

libclip (`libclip/`, documented in `libclip/README.md`) reads and writes
`.clip` files; it knows nothing about the engine.  The application side is
`src/clip/clip_bridge.*`, which converts between libclip's structs and
`Document`: layers, blend modes (`clip_composite` ⇄ the engine's
`BlendMode`), opacity and visibility.  libclip does not composite, so the
bridge supplies the engine's flattened image as the `CanvasPreview`.

What the bridge converts, in both directions:

* folders and nesting (libclip's `depth`), pass-through, open/closed state;
* layer masks (libclip gives the mask with 255 = shown; the engine stores
  how much is hidden, so the bridge inverts it) and whether they are enabled;
* Layer Property effects: border, layer colour, Gray expression colour;
  1-bit Monochrome layers are read as pixels and written back as Gray;
* gradient and fill layers from `GradationFillInfo` (linear shape, repeat
  mode, stops or single colour); a radial gradient has no CSP equivalent
  and is written as a raster layer of its rendering;
* correction layers from `FilterLayerInfo`: every `CLIP_FILTER_*` kind maps
  to an `Adjustment::Kind`.  On read, the stored parameters are kept in
  `Adjustment::raw` and written back unchanged unless the layer was edited;
  tone curves with per-channel curves keep their lookup tables until edited.
  Correction and gradient layers are written with the engine's rendering as
  their pixels, which CSP shows until it re-renders them;
* vector, text, tone and other special layers, which have no engine model:
  their stored rendering becomes a raster layer.
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
cliptool compare <file.clip> [sheet.png]   diff the composite against CSP's saved preview
cliptool create <out.clip> <a.png> [b.png …]
cliptool roundtrip <in.clip> <out.clip>
```

`info` lists every layer with its type bits, mask state, and the decoded
correction (`filter=kind(size bytes…)`) or gradient settings.  `schema`,
`tables` and `dump` work on any `.clip`, including ones from CSP, and are
the quickest way to confirm or extend `libclip/docs/FORMAT.md` when a
CSP-written file is available.  `compare` is how the blend-mode table and
the correction renderers were calibrated against CSP's own output.

Paths may contain any Unicode characters (e.g. Japanese file and folder
names).  On Windows, `cliptool` reads its arguments from the UTF-16 command
line (`CommandLineToArgvW`) rather than the ANSI `argv` the C runtime
provides, converts them to UTF-8 for libclip, and sets the console output to
UTF-8 so layer names and paths print correctly.

## 5. `plastic_paint` (Windows)

A Clip Studio Paint-style workspace: a menu bar and command bar, tool and
sub tool palettes, colour palettes, the canvas with a navigation bar, and a
Layer palette with a History palette.  Palettes dock left or right, can be
hidden, and the theme is dark or light.  Pen input arrives through
`WM_POINTERDOWN/UPDATE/UP` with pressure from `GetPointerPenInfo` (mouse
falls back to full pressure).  The composite is cached in a BGRA buffer and
only the stroke's dirty rectangle is recomposited and invalidated per input
event.

The Layer palette shows a thumbnail per layer (a mask thumbnail beside it
when the layer has one; clicking either chooses what the pixel tools edit),
drag-and-drop reordering into and out of folders, and a context menu.  The
**Layer settings** dialog (`ui_layer_settings.cpp`, opened from the Layer
menu or by double-clicking a non-raster layer's thumbnail) edits a fill
layer's colour, a gradient layer's stops, shape and repeat mode, and every
correction layer's parameters live, as one undo step.  Dragging the
Gradient tool on a gradient layer sets its direction.

| Keys | Action |
|---|---|
| `P`, `B`, `E`, `G`, `K`, `M`, `I`, `H`, `R`, `/` | pen, brush, eraser, fill/gradient, move layer, selection, eyedropper, hand, rotate, zoom (press again for the next tool in the group) |
| `[` `]` | brush size · `,` `.` next/previous sub tool |
| `Ctrl+[` `Ctrl+]` | brush opacity |
| `X`, `C` | swap main and sub colour / transparent colour |
| `Ctrl+Z` / `Ctrl+Y` (`Ctrl+Shift+Z`) | undo / redo |
| `Ctrl+Shift+N`, `Ctrl+E`, `Ctrl+Shift+E` | new raster layer, merge down, merge visible |
| `Alt+]` / `Alt+[` | select upper / lower layer |
| `Ctrl+Alt+G` | clip at layer below |
| `Ctrl+M` | edit the layer mask |
| `Delete`, `Ctrl+Delete` | clear layer, clear outside the selection |
| `Ctrl+A`, `Ctrl+D`, `Ctrl+Shift+I` | select all, deselect, invert selection |
| `Ctrl+I` | reverse gradient (invert the layer) |
| `Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Shift+S`, `Ctrl+W` | new, open (.clip/.png), save, save as, close |
| wheel, `Ctrl++`/`Ctrl+-`, `Ctrl+0`, `Ctrl+Alt+0`, `-`/`^`, `Ctrl+@` | zoom, zoom in/out, fit, 100 %, rotate view, reset rotation |
| `Space`+drag or right-drag | pan |
| `Tab`, `F1`, `Alt+letter` | hide palettes, shortcut list, open a menu |

Drag-and-drop of a `.clip` or `.png` onto the window opens it, and so does
passing a path on the command line (`plastic_paint.exe "C:rt\絵.clip"`).
The command line is split with `CommandLineToArgvW`: the first argument is
opened and any further ones are ignored.  Paths containing spaces must be
quoted, as Explorer's "Open with" does.  File dialogs, drag-and-drop and the
command line all hand UTF-8 paths to the loaders, so non-ASCII names work
everywhere.

## 6. Verification status

* `libclip_tests` (C): zlib and PNG, container round trip, tile
  encode/decode, a full write → read of a document (UTF-16 names,
  `ExternalChunk` offsets, preview, determinism), layer effects, legacy
  `.lip` files, and decoding `yuzuko.clip` when present.
* `pu_tests` (25 tests): checksums, inflate against CPython-generated
  fixed- and dynamic-Huffman streams, blend formulas, compositing order,
  clipping layers, brush opacity/pressure/eraser, undo/redo, PNG round-trip,
  selections (rectangle, lasso, ellipse, add/subtract/invert, moving
  pixels), Layer Property effects, one-step property edits, blend-mode
  mapping, and Document → .clip → Document round-trips of raster layers,
  layer effects, every correction kind (tone curve, colour balance,
  posterization, binarization among them) and gradient repeat modes.
  All pass.
* Layer kinds were surveyed over 692 CSP-saved files (9 790 layers) with
  `cliptool info`: 5 768 raster, 857 masked, 116 correction (hue/saturation
  38, gradient map 33, tone curve 13, colour balance 12,
  brightness/contrast 11, levels 8, reverse gradient 1), 380 gradient / fill
  layers (all linear, no repeat), and no posterization or binarization layer.
  Files with tone curves and colour balance round-trip through `cliptool`
  and still render in CSP's preview handler.
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
