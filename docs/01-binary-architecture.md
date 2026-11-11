# 01 · Clip Studio Paint: binary architecture

Target: `CLIPStudioPaint.exe` 5.1.4.0 and its companion modules in
`C:\Program Files\CELSYS\CLIP STUDIO 1.5\CLIP STUDIO PAINT\`.
Tags: **[observed]** read from the files · **[inferred]** conclusion · **[unverified]** not confirmed here.

## 1. What is installed

### 1.1 Executables and first-party libraries [observed]

| File | Size | Version resource | Role |
|---|---|---|---|
| `CLIPStudioPaint.exe` | 94.5 MB | 5.1.4.0, "CLIP STUDIO PAINT", CELSYS,Inc. | The application. Almost all of the program is statically linked into this one PE32+ image. |
| `iswCoreTG.dll` | 4.5 MB | 1.10.0.0, "IllustStudio Libraries For Trigraph" | Legacy IllustStudio engine: 5 919 C++ exports (`CSLayer`, `CSBitmapLayer`, `CSVectorMath`, `CSText`, `CSTone`, `CSRuler*`, `CSPageDocument`, …). |
| `iswCmnTG.dll` | 1.2 MB | 1.10.0.0, "IllustStudio Libraries Common Class For Trigraph" | Legacy common classes: 2 887 exports (`RCVOffscreen*` tiled bitmaps in 1/2/8/16/24/32-bit variants, `RCFilePath`, `RCArchive`, `RCZLib`, `RCPixelMixer`, …). |
| `VEDll.dll` | 3.8 MB | "VEDll" | Video-engine style module with a C API (`InitMdulCoreInit/Call/Exit`, `RegisterModel`, `RegisterAsyncStateCallback`, `ExecTurnBackClock`). |
| `QmPdkDll.dll` | 5.7 MB | "QUMARION Pdk Module" | Driver for the QUMARION 3D posing doll (`QmPdk*`, `QmLow*` C API: bones, sensors, accelerometer). |
| `WKBridge.dll` | 344 KB | 1.11.0.0 | Bridge to an embedded web view (`CanUseEdgeForWebView`, `UseInternetExplorerForWebView`, `SetEdgeUserDataPath`, `CreateBoard`, `TellBoard`). |
| `triglavwinrt.dll` | 182 KB | "triglavwinrt" | WinRT shim: Surface Dial (`dial_controller_*`, `dial_menu_item_*`) and `winrt_initialize/terminate`. |
| `AudioUtl.dll` | 137 KB | | Audio playback helper (`AudioUtl*Player*`, master volume) over OpenAL (`OpenAL32.dll`, `wrap_oal.dll`). |
| `rtwUtl.dll`, `rtwUtlTG.dll` | 258 KB / 74 KB | | "RETAS" era utility C API (`rtAlloc`, `rtOpenFile`, `rtCreateSplashWindow_J/E/K/C`, `rtCheckRetasTag`, …). |
| `ClipPreview.dll`, `LipPreview.dll` | 1.2 MB each | "CLIP STUDIO FORMAT Shell Extension" | Explorer preview handlers for `.clip` and the older `.lip` format (COM in-proc servers). |
| `TG*PlugIn.cfpi` + `TGXPGPlugInCore.dll` | 18–232 KB | | File plug-ins for CLIP STUDIO (the launcher) to read page/project/tool-set/material files. See doc 03. |
| `PlugIn\PAINT\*.dll` (37 files) | | 5.1.4.0 each | Filter and import/export plug-ins using the public "Triglav" plug-in ABI. See doc 03. |
| `InitInfo.exe`, `LipExt.exe` | | | Install-time helpers (hardware/drive enumeration; preview-handler registration). |

### 1.2 Third-party libraries shipped as DLLs [observed]

| Library | DLL(s) | Used for (inferred from imports and RTTI) |
|---|---|---|
| Skia | `skia.dll`, `skshaper.dll`, `skunicode.dll` | 2-D rasterisation and text shaping (89 + 4 imports). |
| ICU 65 | `icuuc65.dll`, `icuin65.dll`, `icudt65.dll` (28 MB data) | Unicode, collation, text boundaries. |
| Boost 1.89 | `boost_thread/regex/filesystem/...` | Threads, regex, filesystem, geometry (source paths of Boost.Geometry appear in exception strings). |
| Xerces-C 3.1 | `xerces-c_3_1.dll` | XML parsing (RTTI shows `xercesc_3_2` classes also linked statically). |
| Autodesk FBX SDK 2020.2 | `libfbxsdk.dll` (138 imports) | 3-D model import. |
| ailia 1.2.13 (AXELL) | `ailia.dll`, `ailia_blas/cuda/vulkan/amp.dll` | Neural-network inference (13 imports); pretrained weights live in `Settings\PAINT\PretrainedWeights\SuperResolution`. |
| OSQP | `osqp.dll` | Quadratic-programming solver (5 imports), used by 3-D/pose fitting [inferred]. |
| Image codecs | `libpng16`, `jpeg62`/`libjpeg`, `tiff`/`libtiff`, `libwebp*`, `giflib`, `libsharpyuv` | PNG/JPEG/TIFF/WebP/GIF import and export. |
| Audio | `libogg`, `libvorbis`, `libvorbisfile`, `OpenAL32`, `wrap_oal` | Sound in animation timelines. |
| Intel OpenMP | `libiomp5md.dll` | Parallel loops. |
| Compression | `zlib.dll` | Deflate (11 imports). |
| MSVC runtimes | `msvcp140*`, `vcruntime140*`, `ucrtbase`, `concrt140`, `vcamp140`, plus `msvcp100`/`msvcr100` for the legacy ISW DLLs | |
| Direct3D compiler | `d3dcompiler_47.dll` | Shader compilation for the D3D11 path. |

### 1.3 Code signing and image properties [observed]

* Signed by **CELSYS, Inc.** (Tokyo, Shinjuku-ku) with a GlobalSign EV
  code-signing certificate (serial `33c36bd159664a74099c0f2d`, valid
  2025-12-08 → 2027-02-11), SHA-256 digest, RSA.
* PE32+ x64, seven sections. `.text` is **71.2 MB**; `.rdata` 14.3 MB;
  `.data` 5.1 MB virtual (2.4 MB on disk); `.rsrc` 3.9 MB. Base address `0x140000000`.
* Load-config declares CFG instrumentation (`IMAGE_GUARD_CF_INSTRUMENTED`)
  but ships an empty guard function table; one TLS directory, no TLS callbacks.
* Import hash (pefile) `cc7be2a4c347aa3dfd884d3c1d5122ad`; 64 import DLLs, 1 591
  imported functions, no delay-load table.
  Full per-DLL counts: [evidence/clipstudiopaint-exe-imports.txt](evidence/clipstudiopaint-exe-imports.txt).

## 2. How the executable is organised

### 2.1 Namespaces (from RTTI type descriptors) [observed]

MSVC emits `.?AV<Class>@<Namespace>@@` type descriptors for every polymorphic
class, so the namespace census is a reliable map of the code base.
Full list: [evidence/rtti-namespace-census.txt](evidence/rtti-namespace-census.txt).

| Namespace | Classes | What it is [inferred from class names] |
|---|---|---|
| `Planeswalker` | 1 621 | The application framework and paint engine proper. Class prefix `PW`. |
| `Odyssey` | 675 | The 3-D subsystem (`ODCOpenGLRendering*`, `ODC*3DObjectImporter`, lights, cameras, materials). |
| `C3DCore`, `PWIO3DCore` | 125 + 16 | 3-D core math/scene graph and its I/O. |
| `LaSys`, `LaGame` | 71 + 4 | Smaller engine layer under Odyssey [unverified]. |
| `C2F` | 53 | Character/figure format handling (`default_model_*.c2fr`) [inferred]. |
| `GaRoku` | 37 | Unknown module (37 classes) [unverified]. |
| `xercesc_3_2`, `boost`, `std`, `ATL`, `Gdiplus`, `SkShaper`, `Imath`, `Iex` | | Third-party code linked statically. |

Internal project names visible in PDB paths embedded in the plug-ins and
helpers [observed]: `TriglavBuild03` (the build tree of the whole product),
`PAINT\Urza` (the preview handler project), `XPGPlugIn`, `PaintPlugIn`.
"Triglav" is the product code name used throughout the plug-in ABI
(`TriglavPluginCall`, `triglavwinrt.dll`).

### 2.2 Subsystems inside `Planeswalker` [observed names, inferred grouping]

* **Document model**: `PWClipProjectDocument`, `PWDocumentManager`,
  `PWDocumentModel`, `PWCanvas`, `PWCanvasController`, `PWLayer*`,
  `PWCanvasItem*` (3-D characters/rooms/small objects, audio, binary items).
* **Raster storage**: `PWVOffscreen`, `PWVOffscreenBlock`, `PWVOffscreenAccessor`,
  `PWVOffscreenCursor`, `PWVOffscreenGraphicContext`, `PWMipmap`,
  `PWMipmapInfo`, `PWBlockRender`, `PWLayerRender`. The "VOffscreen" is a
  tiled bitmap made of 256×256 blocks with per-level mipmaps; the same
  structure is what gets serialised into `.clip` files (doc 02).
* **Brush engine**: `PWBrushTool`, `PWBrushToolStroke`, `PWBrushDraw`,
  `PWBrushDrawUnit`, `PWBrushComposite`, `PWBrushBlur`, `PWBrushHardnessEffect`,
  `PWBrushParameterEffector`, `PWBrushEffectorGraphData`, `PWBrushPatternImage`,
  `PWBrushFixedSpray`, `PWBrushOffscreenSet`.
* **Tools** (89 classes): `PWCloseFillTool`, `PWContourFillTool`,
  `PWComicFrameTool`, `PWBalloonTool`, `PWConcentrateLineTool`,
  `PWLiquifyToolStroke`, `PWLightTableTool`, `PWTimeLineEditTool`, …
* **Commands / undo** (76 + 67 classes): every edit is a `*DoCommand`
  dispatched by `PWCommandDispatcher`; 3-D edits carry `PW3DOdUndoData*`.
* **Vector layers** (41): `PWVectorStroke`, `PWVectorBezierCurve`,
  `PWVectorCubicBezierCurve`, `PWVectorConnect`, `PWVectorCut`, …
* **Animation / timeline** (15 + 29): `PWTimeLine*`, `PWAnimationCutBank`,
  `PWAnimationFCurve`, `PWAnimationWebp`, `PWMovieExport`, `PWMovieImportData`.
* **Filters** (44): `PW*FilterPass` (chromatic aberration, crystalize, drop
  shadow, film camera style, …) plus `PWExtensionFilterModule` that hosts the
  external filter plug-ins.
* **Import/export** (20 + 33): `PWImportAbr` (Photoshop brushes),
  `PWODC{Fbx,Gltf,Stl,Vrm}3DObjectImporter`, `PWImageSeriesExportFormat*`,
  `PWExtensionImportModule`/`PWExtensionExportModule` (hosts `ImportPSD.dll`
  etc.).
* **Materials / catalog**: `PWMaterial*`, `PWCatalogDocument*`,
  `PWFileMaterial`, with a job system (`PWJob`, `SNetworkJobEventParameter*`)
  for downloads.
* **Database layer**: `PWDB`, `PWDBSQLite`, `PWDBSQLiteChunk`,
  `PWDBSQLiteChunkRead`, `PWDBBlobSQLite`, `PWSQLiteVFS`, `PWDBSQLiteOutline`.
  SQLite **3.30.1** is statically linked (version string observed next to the
  format vocabulary) and accessed through a custom VFS so that a database can
  live inside a chunked container file (doc 02).

### 2.3 Rendering back ends [observed imports, inferred use]

* **GDI / GDI+** (50 + 53 imports) for the window system and classic UI.
* **OpenGL** (112 imports from `OPENGL32.dll`, plus `GLU32`): the 3-D viewport
  (`PW3DCOpenGLRenderer`, `PW3DCOpenGLRenderingManager`, `PW3DCOpenGLGLShaderManager`,
  `ODCOpenGLRendering*`) and `PWOpenGLOffscreen`.
* **Direct3D 11 / DXGI / DirectComposition / Direct2D / DirectWrite** (one
  import each: `D3D11CreateDevice`, `CreateDXGIFactory1/2`, `DCompositionCreateDevice`,
  `D2D1CreateFactory`, `DWriteCreateFactory`): a modern composition path,
  most likely for the canvas presentation on Windows 10+ [inferred].
* **Skia** for CPU rasterisation of 2-D content and text shaping.
* **Windows Media Foundation** (`MF`, `MFPlat`, `MFReadWrite`) and `AVIFIL32`
  for movie import/export; `WINMM` for timers/sound.
* **Pen/touch input**: no `WINTAB32.dll` in the import table; tablet input is
  received through the Windows pointer API (`USER32` imports include the
  `*Pointer*` family) [inferred]. The Surface Dial is handled via `triglavwinrt.dll`.
* **Colour management**: `mscms.dll` (ICM, 11 imports) plus the bundled
  `IccProfLib.dll` and ICC profiles in `Settings\PAINT\ICCProfiles`.

### 2.4 Network, licensing and telemetry endpoints [observed strings]

The executable talks to CELSYS services over WinINet/WinSock
(`WININET.dll` 13 imports, `WS2_32.dll` 21, `CRYPT32`/`bcrypt` for TLS/signing):

* `https://api.lac.celsys.co.jp/v1/licapi.php` — licence API
* `https://license.celsys.co.jp/gd/index.php`, `https://us-lic.celsys.co.jp/gd/index.php` — licence/plan pages
* `https://api.lac.celsys.co.jp/v1/poseapi.php` — pose estimation from photos (AI feature)
* `https://sr.lac.celsys.co.jp/v1/srapi.php` — super-resolution service
* Custom URI schemes: `clipstudiopaint://closetutorial`,
  `toeianimationdigitaltimesheet://clipstudio/refresh` (integration with Toei's digital timesheet tool).

On this machine the application starts into a purchase/trial gate (observed
at run time), confirming that document creation is licence-gated.

### 2.5 Settings and data directories [observed]

`Settings\PAINT\` contains per-language `Tool`, `BrushPreset`, `ColorSet`,
`Action`, `GradationSet`, `DialMenu`, `MixPalette` presets; `WorkSpace\{Win,Mac,Android}`;
`3D\` (`*.c2fr` figure models, `*.cs3o` hand/head objects, `*.c3dd` dessin
dolls, `*.pep` poses, `*.3dp` bone-scale presets); `ICCProfiles`; `HTML`
(offline help pages); `PretrainedWeights\SuperResolution`; and `Material`
catalogue repair tables. User data lives under `%APPDATA%\CELSYSUserData`,
`%APPDATA%\CELSYS` and `%ProgramData%\CELSYS`.

## 3. Summary diagram

```
┌──────────────────────────────────────────────────────────────────────────┐
│ CLIPStudioPaint.exe (71 MB of code)                                        │
│                                                                            │
│  Planeswalker (app + paint engine)      Odyssey / C3DCore (3-D)            │
│  ├─ Document / Canvas / Layer           ├─ OpenGL renderer                │
│  ├─ VOffscreen tiles + Mipmaps          ├─ FBX / glTF / STL / VRM import  │
│  ├─ Brush engine, Tools, Commands       └─ QUMARION doll (QmPdkDll)       │
│  ├─ Vector, Text, Timeline, Filters                                        │
│  ├─ PWDB / PWSQLiteVFS  ──► SQLite 3.30.1 (static) ──► .clip files         │
│  └─ Extension host ──► PlugIn\PAINT\*.dll  (TriglavPluginCall)             │
│                                                                            │
│  Third-party: Skia, ICU, Boost, Xerces, FBX SDK, ailia, OSQP, codecs       │
└──────────────────────────────────────────────────────────────────────────┘
   GDI/GDI+ · OpenGL · D3D11+DXGI+DComp · Direct2D/DWrite · Media Foundation
```
