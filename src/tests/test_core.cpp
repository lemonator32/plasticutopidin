#include <cmath>
#include <cstring>

#include "blend.h"
#include "brush.h"
#include "document.h"
#include "png.h"
#include "test_main.h"
#include "undo.h"
#include "zlib_lite.h"
#include "zlib_vectors.h"

using namespace pu;

TEST(zlib_crc32_and_adler32_known_vectors) {
    const char* s = "123456789";
    CHECK_EQ(zlib::crc32(reinterpret_cast<const uint8_t*>(s), 9), 0xCBF43926u);
    CHECK_EQ(zlib::adler32(reinterpret_cast<const uint8_t*>(s), 9), 0x091E01DEu);
}

TEST(zlib_stored_roundtrip) {
    std::vector<uint8_t> data(200000);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i * 7 + (i >> 8));
    auto z = zlib::deflate_stored(data.data(), data.size());
    std::vector<uint8_t> back;
    auto r = zlib::inflate(z.data(), z.size(), back);
    CHECK(r.ok);
    CHECK(back == data);
}

namespace {
std::vector<uint8_t> sampleData(size_t n, uint32_t seed, int kind) {
    std::vector<uint8_t> v(n);
    uint32_t x = seed;
    for (size_t i = 0; i < n; ++i) {
        x = x * 1664525u + 1013904223u;
        switch (kind) {
            case 0: v[i] = static_cast<uint8_t>(x >> 24); break;                         // incompressible
            case 1: v[i] = static_cast<uint8_t>((i / 37) % 7); break;                     // long runs
            case 2: v[i] = static_cast<uint8_t>("the quick brown fox "[(i * 7 + (x >> 29)) % 20]); break;  // text-like
            default: v[i] = static_cast<uint8_t>(i % 256 < 200 ? 0 : (x >> 24)); break;  // mostly zeros
        }
    }
    return v;
}
}  // namespace

TEST(zlib_deflate_roundtrip_all_levels_and_shapes) {
    const size_t sizes[] = {0, 1, 2, 3, 258, 259, 4096, 70000, 327680};
    for (int kind = 0; kind < 4; ++kind)
        for (size_t n : sizes)
            for (int level : {1, 6, 9}) {
                std::vector<uint8_t> data = sampleData(n, 1234u + static_cast<uint32_t>(n), kind);
                std::vector<uint8_t> z = zlib::deflate(data.data(), data.size(), level);
                std::vector<uint8_t> back;
                auto r = zlib::inflate(z.data(), z.size(), back);
                if (!r.ok || back != data) {
                    std::printf("  deflate roundtrip failed: kind %d size %zu level %d (%s)\n", kind, n, level, r.error.c_str());
                    CHECK(false);
                    return;
                }
                if (kind == 1 && n >= 4096 && level >= 6) CHECK(z.size() < n / 20);  // runs compress well
                if (kind == 0 && n >= 4096) CHECK(z.size() < n + n / 100 + 64);  // incompressible: near stored size
            }
}

TEST(zlib_deflate_long_distances_and_matches) {
    // Repeats at the maximum window distance and maximum match length.
    std::vector<uint8_t> block = sampleData(32767, 99, 0);
    std::vector<uint8_t> data = block;
    data.insert(data.end(), block.begin(), block.end());
    data.insert(data.end(), size_t(1000), uint8_t(0x41));
    std::vector<uint8_t> z = zlib::deflate(data.data(), data.size(), 9);
    std::vector<uint8_t> back;
    CHECK(zlib::inflate(z.data(), z.size(), back).ok);
    CHECK(back == data);
    CHECK(z.size() < block.size() + 2000);
}

TEST(zlib_inflate_fixed_huffman_vector) {
    // Produced by CPython's zlib module (see zlib_vectors.h).
    std::vector<uint8_t> out;
    auto r = zlib::inflate(kZlibFixed, kZlibFixed_len, out);
    CHECK(r.ok);
    if (!r.ok) std::printf("  inflate error: %s\n", r.error.c_str());
    CHECK_EQ(std::string(out.begin(), out.end()), std::string("hello hello hello hello"));
}

TEST(zlib_inflate_dynamic_huffman_vector) {
    std::vector<uint8_t> out;
    auto r = zlib::inflate(kZlibDynamic, kZlibDynamic_len, out);
    CHECK(r.ok);
    if (!r.ok) std::printf("  inflate error: %s\n", r.error.c_str());
    CHECK_EQ(out.size(), kZlibDynamicSource_len);
    CHECK(out.size() == kZlibDynamicSource_len && std::memcmp(out.data(), kZlibDynamicSource, out.size()) == 0);
}

TEST(blend_normal_over_opaque) {
    Color d = Color::rgba(0, 0, 0, 255), s = Color::rgba(255, 255, 255, 128);
    Color r = blendPixel(d, s, BlendMode::Normal, 1.f);
    CHECK(r.a == 255);
    CHECK(r.r >= 127 && r.r <= 129);
}

TEST(blend_modes_match_definitions) {
    Color d = Color::rgba(100, 150, 200, 255), s = Color::rgba(50, 200, 20, 255);
    CHECK_EQ(int(blendPixel(d, s, BlendMode::Multiply, 1.f).r), int(std::round(100.0 * 50 / 255)));
    CHECK_EQ(int(blendPixel(d, s, BlendMode::Screen, 1.f).g), int(std::round(255 * (1 - (1 - 150 / 255.) * (1 - 200 / 255.)))));
    CHECK_EQ(int(blendPixel(d, s, BlendMode::Darken, 1.f).b), 20);
    CHECK_EQ(int(blendPixel(d, s, BlendMode::Lighten, 1.f).r), 100);
    CHECK_EQ(int(blendPixel(d, s, BlendMode::Difference, 1.f).r), 50);
    CHECK_EQ(int(blendPixel(d, s, BlendMode::Add, 1.f).g), 255);
    CHECK_EQ(int(blendPixel(d, s, BlendMode::Subtract, 1.f).r), 50);
    // Luminosity keeps the backdrop's hue: result luminance equals source luminance.
    Color l = blendPixel(d, s, BlendMode::Luminosity, 1.f);
    float lumS = 0.3f * 50 + 0.59f * 200 + 0.11f * 20, lumR = 0.3f * l.r + 0.59f * l.g + 0.11f * l.b;
    CHECK(std::fabs(lumS - lumR) < 2.f);
    // Transparent source never changes the backdrop.
    CHECK(blendPixel(d, Color::rgba(1, 2, 3, 0), BlendMode::Multiply, 1.f) == d);
    BlendMode m;
    CHECK(blendModeFromName("SoftLight", m) && m == BlendMode::SoftLight);
}

TEST(document_composites_layers_in_order) {
    Document doc(4, 4);
    Layer* a = doc.addLayerTop("A");
    a->surface.fill(Color::rgba(255, 0, 0, 255));
    Layer* b = doc.addLayerTop("B");
    b->surface.fillRect(Rect{0, 0, 2, 4}, Color::rgba(0, 0, 255, 255));
    b->opacity = 0.5f;
    Surface out = doc.flatten();
    CHECK(out.at(3, 0) == Color::rgba(255, 0, 0, 255));
    CHECK(out.at(0, 0).r > 120 && out.at(0, 0).r < 136 && out.at(0, 0).b > 120);
    b->visible = false;
    out = doc.flatten();
    CHECK(out.at(0, 0) == Color::rgba(255, 0, 0, 255));
    // Clipping layer only shows where the layer below has alpha.
    b->visible = true;
    b->opacity = 1.f;
    a->surface.fill(Color{});
    a->surface.fillRect(Rect{0, 0, 1, 4}, Color::rgba(0, 255, 0, 255));
    b->clipToBelow = true;
    doc.paperVisible = false;
    out = doc.flatten();
    CHECK(out.at(0, 0) == Color::rgba(0, 0, 255, 255));
    CHECK(out.at(1, 0).a == 0);
}

TEST(brush_stroke_paints_and_respects_opacity) {
    Surface s(64, 64);
    BrushSettings bs;
    bs.size = 10;
    bs.opacity = 0.5f;
    bs.color = Color::rgba(0, 0, 0);
    bs.hardness = 1.f;
    bs.smoothing = 0.f;
    Stroke st(s, bs);
    st.addPoint({10, 32, 1.f});
    st.addPoint({50, 32, 1.f});
    st.addPoint({10, 32, 1.f});  // retrace: coverage must not accumulate past opacity
    st.finish();
    CHECK(!st.dirtyRect().empty());
    CHECK(s.at(30, 32).a >= 126 && s.at(30, 32).a <= 129);
    CHECK(s.at(30, 2).a == 0);
    // Zero pressure with minSizeRatio 0.15 paints a small dab.
    Surface s2(64, 64);
    bs.opacity = 1.f;
    Stroke st2(s2, bs);
    st2.addPoint({32, 32, 0.f});
    st2.finish();
    CHECK(s2.at(32, 32).a == 255);
    CHECK(s2.at(32 + 4, 32).a == 0);
    // Eraser removes alpha.
    bs.eraser = true;
    Stroke st3(s2, bs);
    st3.addPoint({32, 32, 1.f});
    st3.finish();
    CHECK(s2.at(32, 32).a == 0);
}

TEST(undo_redo_pixel_patch_and_layers) {
    Document doc(8, 8);
    UndoStack undo(doc);
    Layer* l = doc.addLayerTop("L");
    Surface before = l->surface.crop(Rect{0, 0, 8, 8});
    l->surface.fillRect(Rect{2, 2, 2, 2}, Color::rgba(9, 9, 9, 255));
    Surface after = l->surface.crop(Rect{0, 0, 8, 8});
    undo.push(std::make_unique<PixelPatchCommand>(l->id, Rect{0, 0, 8, 8}, before, after));
    CHECK(undo.canUndo());
    undo.undo();
    CHECK(l->surface.at(2, 2).a == 0);
    undo.redo();
    CHECK(l->surface.at(2, 2).r == 9);

    auto nl = std::make_unique<Layer>();
    nl->name = "N";
    nl->surface.reset(8, 8);
    doc.insertLayer(std::move(nl), 1);
    undo.push(std::make_unique<AddLayerCommand>(nullptr, 1));
    CHECK_EQ(doc.layerCount(), size_t(2));
    undo.undo();
    CHECK_EQ(doc.layerCount(), size_t(1));
    undo.redo();
    CHECK_EQ(doc.layerCount(), size_t(2));
    CHECK_EQ(doc.layer(1)->name, std::string("N"));
}

TEST(png_roundtrip) {
    Surface s(37, 11);
    for (int y = 0; y < 11; ++y)
        for (int x = 0; x < 37; ++x) s.at(x, y) = Color::rgba(uint8_t(x * 6), uint8_t(y * 20), uint8_t(x ^ y), uint8_t(255 - x));
    auto bytes = png::encode(s);
    Surface back;
    std::string err;
    CHECK(png::decode(bytes.data(), bytes.size(), back, &err));
    CHECK_EQ(back.width(), 37);
    CHECK_EQ(back.height(), 11);
    bool same = true;
    for (int y = 0; y < 11; ++y)
        for (int x = 0; x < 37; ++x) same = same && back.at(x, y) == s.at(x, y);
    CHECK(same);
}
