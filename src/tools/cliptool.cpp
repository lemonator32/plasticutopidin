// cliptool: inspect, extract and create CLIP STUDIO FORMAT (.clip) files.
// Format work is done by libclip; the engine is used for compositing (export)
// and for building documents from PNGs (create).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "clip_bridge.h"
#include "png.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

using namespace pu;
using namespace pu::clip;

static int usage() {
    std::puts(
        "cliptool - CLIP STUDIO FORMAT (.clip) inspection and conversion\n"
        "\n"
        "  cliptool info <file.clip>                   container, canvas and layer summary\n"
        "  cliptool chunks <file.clip>                 list CHNK chunks with offsets\n"
        "  cliptool tables <file.clip>                 list SQLite tables and row counts\n"
        "  cliptool schema <file.clip> [table]         show column definitions\n"
        "  cliptool dump <file.clip> <table> [limit]   print rows of a table\n"
        "  cliptool extract-db <file.clip> <out.sqlite>\n"
        "  cliptool extract-chunk <file.clip> <extrnlid...> <out.bin>\n"
        "  cliptool offscreen <file.clip> <layer#>     decode a layer's offscreen attribute\n"
        "  cliptool preview <file.clip> <out.png>      save the embedded canvas preview\n"
        "  cliptool export <file.clip> <out.png>       flatten all layers to PNG\n"
        "  cliptool export-layers <file.clip> <dir>    one PNG per raster layer\n"
        "  cliptool create <out.clip> <layer1.png> [layer2.png ...]   build a .clip from PNG layers\n"
        "  cliptool roundtrip <in.clip> <out.clip>     read with this library and write again\n");
    return 2;
}

static int fail(const std::string& msg) {
    std::fprintf(stderr, "error: %s\n", msg.c_str());
    return 1;
}

static DocumentPtr openDoc(const char* path) {
    std::string err;
    DocumentPtr d = openDocument(path, &err);
    if (!d) fail(err);
    return d;
}

static bool readContainer(const char* path, clip_container& c) {
    clip_error e{};
    if (clip_container_read(&c, path, &e)) return true;
    fail(e.message);
    return false;
}

// Runs a query and collects rows (header first) as strings.
static std::vector<std::vector<std::string>> query(const clip_document* d, const std::string& sql, int limit = -1) {
    std::vector<std::vector<std::string>> rows;
    clip_error e{};
    auto cb = [](void* user, int, int n, const char* const* v) -> int {
        auto* out = static_cast<std::vector<std::vector<std::string>>*>(user);
        out->emplace_back(v, v + n);
        return 0;
    };
    if (!clip_document_query(d, sql.c_str(), limit, cb, &rows, &e)) std::fprintf(stderr, "error: %s\n", e.message);
    return rows;
}

static std::vector<std::string> tableNames(const clip_document* d) {
    std::vector<std::string> names;
    auto rows = query(d, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name");
    for (size_t i = 1; i < rows.size(); ++i) names.push_back(rows[i][0]);
    return names;
}

static std::string sqlString(const std::string& s) {
    std::string out = "'";
    for (char c : s) { if (c == '\'') out += '\''; out += c; }
    return out + "'";
}

static int cmdInfo(const char* path) {
    DocumentPtr doc = openDoc(path);
    if (!doc) return 1;
    const clip_container* c = clip_document_container(doc.get());
    std::printf("file: %s (%zu bytes)\n", path, c->file_size);
    std::printf("declared size: %llu, header size: %llu\n", (unsigned long long)c->declared_file_size, (unsigned long long)c->header_size);
    size_t ext = 0;
    for (size_t i = 0; i < c->chunk_count; ++i) ext += std::strcmp(c->chunks[i].tag, "Exta") == 0;
    std::printf("chunks: %zu (%zu external data chunks)\n", c->chunk_count, ext);
    const clip_canvas_info* cv = clip_document_canvas(doc.get());
    std::printf("canvas: %.0f x %.0f px, %.1f dpi, unit %d, root folder %lld, current layer %lld\n", cv->width, cv->height,
                cv->resolution, cv->unit, (long long)cv->root_folder, (long long)cv->current_layer);
    std::printf("layers (top to bottom):\n");
    for (size_t i = 0; i < clip_document_layer_count(doc.get()); ++i) {
        const clip_layer_info& l = *clip_document_layer(doc.get(), i);
        std::printf("  #%-3d %*s%s%-28s type=0x%x(%s) opacity=%3d%% blend=%s %s%s%s mipmap=%lld\n", (int)i, l.depth * 2, "",
                    clip_layer_is_folder(&l) ? "[+] " : "", l.name, l.type, clip_layer_type_name(l.type), l.opacity * 100 / 256,
                    blendModeName(blendModeFromComposite(l.composite)), l.visible ? "" : "hidden ", l.locked ? "locked " : "",
                    l.clip ? "clip " : "", (long long)l.render_mipmap);
    }
    return 0;
}

static int cmdChunks(const char* path) {
    clip_container c{};
    if (!readContainer(path, c)) return 1;
    std::printf("%-6s %-12s %-12s %s\n", "tag", "offset", "size", "detail");
    for (size_t i = 0; i < c.chunk_count; ++i) {
        const clip_chunk& ch = c.chunks[i];
        std::printf("%-6s %-12llu %-12llu", ch.tag, (unsigned long long)ch.offset, (unsigned long long)ch.size);
        if (std::strcmp(ch.tag, "Exta") == 0)
            std::printf(" id=%s data=%llu bytes @%llu", ch.external_id, (unsigned long long)ch.external_data_size,
                        (unsigned long long)ch.external_data_offset);
        std::printf("\n");
    }
    clip_container_free(&c);
    return 0;
}

static int cmdTables(const char* path) {
    DocumentPtr doc = openDoc(path);
    if (!doc) return 1;
    for (const std::string& t : tableNames(doc.get()))
        std::printf("%-40s %lld rows\n", t.c_str(), (long long)clip_document_row_count(doc.get(), t.c_str()));
    return 0;
}

static int cmdSchema(const char* path, const char* table) {
    DocumentPtr doc = openDoc(path);
    if (!doc) return 1;
    std::vector<std::string> tables = table ? std::vector<std::string>{table} : tableNames(doc.get());
    for (const std::string& t : tables) {
        std::printf("%s\n", t.c_str());
        auto rows = query(doc.get(), "SELECT name, type FROM pragma_table_info(" + sqlString(t) + ")");
        for (size_t i = 1; i < rows.size(); ++i) std::printf("    %-40s %s\n", rows[i][0].c_str(), rows[i][1].c_str());
    }
    return 0;
}

static int cmdDump(const char* path, const char* table, int limit) {
    DocumentPtr doc = openDoc(path);
    if (!doc) return 1;
    clip_error e{};
    auto print = [](void*, int, int n, const char* const* v) -> int {
        for (int i = 0; i < n; ++i) std::printf("%s%s", i ? " | " : "", v[i]);
        std::printf("\n");
        return 0;
    };
    if (!clip_document_dump_table(doc.get(), table, limit, print, nullptr, &e)) return fail(e.message);
    return 0;
}

static int cmdExtractDb(const char* path, const char* out) {
    clip_container c{};
    if (!readContainer(path, c)) return 1;
    size_t n = 0;
    const uint8_t* img = clip_container_sqlite(&c, &n);
    clip_error e{};
    int rc = 0;
    if (!clip_write_file(out, img, n, &e)) rc = fail(e.message);
    else std::printf("wrote %zu bytes to %s\n", n, out);
    clip_container_free(&c);
    return rc;
}

static int cmdExtractChunk(const char* path, const char* id, const char* out) {
    clip_container c{};
    if (!readContainer(path, c)) return 1;
    size_t n = 0;
    const uint8_t* d = clip_container_external(&c, id, &n);
    clip_error e{};
    int rc = 0;
    if (!d) { std::fprintf(stderr, "no external chunk %s\n", id); rc = 1; }
    else if (!clip_write_file(out, d, n, &e)) rc = fail(e.message);
    else std::printf("wrote %zu bytes to %s\n", n, out);
    clip_container_free(&c);
    return rc;
}

static int cmdOffscreen(const char* path, int layerIndex) {
    DocumentPtr doc = openDoc(path);
    if (!doc) return 1;
    if (layerIndex < 0 || (size_t)layerIndex >= clip_document_layer_count(doc.get())) { std::fprintf(stderr, "bad layer index\n"); return 1; }
    const clip_layer_info* l = clip_document_layer(doc.get(), (size_t)layerIndex);
    clip_image px{};
    clip_error e{};
    if (!clip_document_render_layer(doc.get(), l, &px, &e)) { std::fprintf(stderr, "render failed: %s\n", e.message); return 1; }
    size_t opaque = 0;
    for (size_t i = 0; i < (size_t)px.width * px.height; ++i) opaque += px.pixels[i * 4 + 3] != 0;
    std::printf("layer '%s': rendered %dx%d, %zu non-transparent pixels\n", l->name, px.width, px.height, opaque);
    clip_image_free(&px);
    return 0;
}

static int cmdPreview(const char* path, const char* out) {
    DocumentPtr doc = openDoc(path);
    if (!doc) return 1;
    clip_image img{};
    clip_error e{};
    if (!clip_document_canvas_preview(doc.get(), &img, &e)) { std::fprintf(stderr, "%s\n", e.message); return 1; }
    Surface pv = toSurface(img);
    clip_image_free(&img);
    std::string err;
    if (!png::writeFile(out, pv, &err)) return fail(err);
    std::printf("wrote %dx%d preview to %s\n", pv.width(), pv.height(), out);
    return 0;
}

static int cmdExport(const char* path, const char* out) {
    Document doc;
    std::string err;
    if (!readClipFile(path, doc, &err)) return fail(err);
    if (!png::writeFile(out, doc.flatten(), &err)) return fail(err);
    std::printf("wrote %dx%d composite to %s\n", doc.width(), doc.height(), out);
    return 0;
}

static int cmdExportLayers(const char* path, const char* dir) {
    DocumentPtr doc = openDoc(path);
    if (!doc) return 1;
    int written = 0;
    for (size_t i = 0; i < clip_document_layer_count(doc.get()); ++i) {
        const clip_layer_info* l = clip_document_layer(doc.get(), i);
        clip_image px{};
        clip_error e{};
        if (clip_document_render_layer(doc.get(), l, &px, &e)) {
            std::string file = std::string(dir) + "/layer" + std::to_string(i) + ".png", err;
            if (png::writeFile(file, toSurface(px), &err)) { std::printf("wrote %s (%s)\n", file.c_str(), l->name); ++written; }
            else std::fprintf(stderr, "%s: %s\n", file.c_str(), err.c_str());
        } else {
            std::printf("skip #%d %s: %s\n", (int)i, l->name, e.message);
        }
        clip_image_free(&px);
    }
    std::printf("%d layers written\n", written);
    return 0;
}

static int cmdCreate(const char* out, int n, char** pngs) {
    Document doc;
    for (int i = 0; i < n; ++i) {
        Surface s;
        std::string err;
        if (!png::readFile(pngs[i], s, &err)) { std::fprintf(stderr, "%s: %s\n", pngs[i], err.c_str()); return 1; }
        if (i == 0) doc = Document(s.width(), s.height());
        Layer* l = doc.addLayerTop(std::string("Layer ") + std::to_string(i + 1));
        l->surface.blit(s, 0, 0);
    }
    std::string err;
    if (!writeClipFile(out, doc, {}, &err)) return fail(err);
    std::printf("wrote %s (%dx%d, %d layers)\n", out, doc.width(), doc.height(), n);
    return 0;
}

static int cmdRoundtrip(const char* in, const char* out) {
    Document doc;
    std::string err;
    if (!readClipFile(in, doc, &err) || !writeClipFile(out, doc, {}, &err)) return fail(err);
    std::printf("wrote %s\n", out);
    return 0;
}

#ifdef _WIN32
// The CRT hands main() its arguments in the ANSI code page, which turns
// characters outside it (e.g. Japanese file names) into '?'.  Re-read the
// UTF-16 command line and convert to UTF-8, the encoding libclip and png::
// use for paths.
static std::vector<std::string> utf8Args() {
    std::vector<std::string> out;
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    if (!w) return out;
    for (int i = 0; i < n; ++i) {
        int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
        std::string a(len > 0 ? static_cast<size_t>(len - 1) : 0, char());
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, a.data(), len, nullptr, nullptr);
        out.push_back(std::move(a));
    }
    LocalFree(w);
    return out;
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);  // layer names and paths are printed as UTF-8
    std::vector<std::string> args = utf8Args();
    std::vector<char*> argvUtf8;
    for (std::string& a : args) argvUtf8.push_back(a.data());
    argvUtf8.push_back(nullptr);
    if (static_cast<int>(args.size()) == argc) argv = argvUtf8.data();
#endif
    if (argc < 3) return usage();
    std::string cmd = argv[1];
    if (cmd == "info") return cmdInfo(argv[2]);
    if (cmd == "chunks") return cmdChunks(argv[2]);
    if (cmd == "tables") return cmdTables(argv[2]);
    if (cmd == "schema") return cmdSchema(argv[2], argc > 3 ? argv[3] : nullptr);
    if (cmd == "dump" && argc > 3) return cmdDump(argv[2], argv[3], argc > 4 ? std::atoi(argv[4]) : 50);
    if (cmd == "extract-db" && argc > 3) return cmdExtractDb(argv[2], argv[3]);
    if (cmd == "extract-chunk" && argc > 4) return cmdExtractChunk(argv[2], argv[3], argv[4]);
    if (cmd == "offscreen" && argc > 3) return cmdOffscreen(argv[2], std::atoi(argv[3]));
    if (cmd == "preview" && argc > 3) return cmdPreview(argv[2], argv[3]);
    if (cmd == "export" && argc > 3) return cmdExport(argv[2], argv[3]);
    if (cmd == "export-layers" && argc > 3) return cmdExportLayers(argv[2], argv[3]);
    if (cmd == "create" && argc > 3) return cmdCreate(argv[2], argc - 3, argv + 3);
    if (cmd == "roundtrip" && argc > 3) return cmdRoundtrip(argv[2], argv[3]);
    return usage();
}
