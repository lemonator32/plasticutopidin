# 03 · Extension interfaces

Three distinct extension mechanisms are visible in the install.  Everything
here is **[observed]** from export tables, import tables, RTTI names and
embedded PDB paths unless marked otherwise.

## 1. Paint plug-ins (`PlugIn\PAINT\*.dll`): the "Triglav" plug-in ABI

* 37 DLLs, all versioned 5.1.4.0 and built from
  `TriglavBuild03.git\PaintPlugIn\Programs\Output\<Name>\x64-v142\Release\`.
* Every DLL exports exactly **one** function, `TriglavPluginCall`
  (ordinal 1), and imports nothing but `KERNEL32.dll` (72 CRT-support
  functions, no USER32/GDI).  Consequently the host passes *everything* the
  plug-in needs (memory, property dialogs, progress, image access) through
  that single entry point, as callback tables [inferred; this matches
  CELSYS's public filter plug-in SDK, whose filters are also
  `TriglavPluginCall(int* result, void** data, int selector, void* server)`].
* The host side is `Planeswalker::PWExtensionFilterModule` /
  `PWExtensionFilterModuleManager` for filters and
  `PWExtensionImportModule`, `PWExtensionExportModule`,
  `PWExtension{Import,Export}MultiPageModule` (+ `Manager`) for the
  `ImportPSD/PSB/PDFX` and `ExportPSD/PSB/PDFX` DLLs, with
  `PWExtensionServiceOffscreen`, `PWExtensionServiceImportStream` and
  `PWExtensionServiceExportStream` providing the service tables.
* RTTI in the plug-ins shows the implementation pattern: a C++ class per
  plug-in (`CFilterGaussBlur`), and for the PSD exporter `PsDescriptor`,
  `PIExportPsOffscreen`, `SPSLayer` held in `boost::shared_ptr`.
* Filters shipped: Dust, GaussBlur, MotionBlur, RadialBlur, Sharp/SharpStrong/
  SharpOff/SharpOffStrong, UnSharpMask, Smoothing, Mosaic, PerlinNoise,
  LineWidth, Pinch, Sphere, Spiral, Waveform, SimpleWave, Zigzag, PolarCd,
  LensDistortion, GeometricDistortion{FishEye,Panorama}, PencilArtFilter,
  FlowBasedAbstraction, TPP{AddNoise,ChromaticAberration,FilmCameraStyle,
  NormalMap,RotationBlur}, TPReduceNoiseFilter; importers/exporters for
  PSD, PSB and PDF/X.

## 2. File plug-ins for the CLIP STUDIO launcher (`*.cfpi`)

* `TGXPGPlugIn.cfpi`, `TGMaterialPlugIn.cfpi`, `TGCxsPlugIn.cfpi`,
  `TGTosPlugIn.cfpi` are ordinary x64 DLLs with a custom extension.  Each
  exports `FPWFilePlugInEntryPoint` plus the C++ class `XPGPlugInCore`
  (copy constructor, assignment, vtable) and links `TGXPGPlugInCore.dll` by
  name (`LoadLibraryExA`).
* `TGXPGPlugInCore.dll` exports the real work: `XPGPlugInCore::Create(SFilePlugInRecord*)`,
  `ReadPageFile`, `ReadProjectFile`, `ReadMaterialFile`, `ReadPatternSetFile`,
  `ReadToolSetFile`, `ReadToolManagementFile`, `ReadTextStyleFile`,
  `ReadImageInfo`, `PrepareTemporaryFolder` / `EndUseTemporaryFolder`,
  `ReadMaterialInfoFile`.  Its RTTI lists the objects it hands back:
  `PWPlugInCanvas`, `PWPlugInLayer`, `PWPlugInOffscreen`, `PWPlugInProject`,
  `PWPlugInTextObject`, `PWPlugInToolVariant`, `PWPlugInVectorManager`,
  `PWPlugInGradation(Fill)`, `PWPlugInMixedFont`, `PWPlugInResizableImage`,
  and brush vocabulary (`BrushSize`, `BrushHeadType`, `BrushTex*`, `BrushBlur*`).
* The same `.cfpi` set is installed next to `CLIPStudio.exe` (the launcher),
  so these plug-ins exist to let the launcher's material/library browser read
  PAINT's file types without loading the 94 MB paint binary [inferred].
* The host-side record type is `Planeswalker::SFilePlugInRecord` and the
  base class `Planeswalker::PWFilePlugInObject`.

## 3. Explorer preview handlers (`ClipPreview.dll`, `LipPreview.dll`)

* Standard COM in-proc servers (`DllGetClassObject`, `DllRegisterServer`,
  `DllInstall`, …) built with ATL (`CAtlPreviewCtrlImpl`,
  `CPreviewHandlerImpl<CCsvfPreviewHandler>`).  Registered under
  `CLSID {9E6FA56B-AE86-49F9-AA1F-2517D81DE85C}` as
  "CLIP STUDIO FORMAT PreviewHandler Class" for `.clip`.
* They embed their own SQLite and the `Planeswalker` database layer
  (`PWDBSQLite`, `PWDBSQLiteChunkRead`, `PWSQLiteVFS`, `PWDBBlobSQLite`) and
  read only `CanvasPreview(PreviewWidth, PreviewHeight, PreviewData)` from the
  `CHNKSQLi` chunk, which is the authoritative hint that a `.clip` preview
  can be produced without decoding any layer.
* `LipExt.exe` registers them (`shell\open\command`, `%s\%sPreview.dll`).

## 4. Hardware integrations

* `QmPdkDll.dll`: QUMARION posing-doll SDK (`QmPdkInit`, `QmPdkCharacterCreate`,
  `QmLowGetSensor`, `QmLowGetAccelerometer`, …).
* `triglavwinrt.dll`: Surface Dial (`dial_initialize`, `dial_controller_create`,
  `dial_menu_item_create_with_icon`, rotation/contact callbacks).
* `WKBridge.dll`: embeds Edge WebView2 or, as a fallback, Internet Explorer
  for in-app web content (`CanUseEdgeForWebView`, `GetEdgeVersion`,
  `UseInternetExplorerForWebView`).
