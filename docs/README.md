# Plastic Utopia Din: documentation index

This folder documents what was learned about **Clip Studio Paint** (CSP) from a
static survey of the installed binaries, and describes the original C++
software in `src/` that was built from that knowledge.

| Document | Contents |
|---|---|
| [01-binary-architecture.md](01-binary-architecture.md) | What ships in the CSP install, how the executable is put together (modules, namespaces, subsystems, third-party libraries, rendering back ends, licensing endpoints). |
| [libclip/docs/FORMAT.md](../libclip/docs/FORMAT.md) (formerly 02) | Complete `.clip` reference: container and chunk layout, the SQLite schema and object graph, the tile and checksum encoding, which rules Clip Studio Paint enforces on load (verified by testing), pitfalls for implementers, and open questions. |
| [03-plugin-interfaces.md](03-plugin-interfaces.md) | The three extension mechanisms found: filter plug-ins (`TriglavPluginCall`), file plug-ins (`.cfpi`, `FPWFilePlugInEntryPoint`) and the Explorer preview handler. |
| [04-cpp-implementation.md](04-cpp-implementation.md) | Design and usage of the C++ code in `src/`: painting engine, `.clip` library, `cliptool`, and the `plastic_paint` Windows application. |
| [evidence/](evidence/) | Raw extracts that back the claims above (import tables, export tables, RTTI census, PE sections). |

## Scope and provenance

* Analysis target: `C:\Program Files\CELSYS\CLIP STUDIO 1.5\CLIP STUDIO PAINT\`,
  `CLIPStudioPaint.exe` version **5.1.4.0** (x64, SHA-256
  `14c5f8ab2bffe529deb120ef3ea2b69b41436019796d0082fd7886d5607facf5`),
  surveyed on 2026-11-10 with REA's portable PE inspectors
  (`inspect_pe_imports`, `inspect_pe_load_config`, `inspect_pe_certificates`,
  `inspect_native_exports`) plus string and RTTI extraction scripts.
* No decompilation of CSP's code was performed and nothing in `src/` is
  derived from CSP's implementation.  The documents describe *structure* and
  *data formats* (module layout, exported symbol names, embedded vocabulary,
  file layout), which is the kind of information needed for interoperability.
* Every statement is tagged as **observed** (read directly from the files),
  **inferred** (a conclusion drawn from observations) or **unverified**
  (plausible, from public knowledge, but not confirmed on this machine).
* The file-format document was verified against a document written by
  Clip Studio Paint 5.1 (`yuzuko.clip`): this project's decoder reproduces
  CSP's own embedded canvas preview of that file pixel-for-pixel.
