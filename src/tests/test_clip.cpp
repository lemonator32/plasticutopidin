// Tests for the bridge between libclip and the engine's Document.  The
// format itself is covered by libclip's own tests (libclip/tests).
#include <cstring>

#include "clip_bridge.h"
#include "test_main.h"

using namespace pu;
using namespace pu::clip;

TEST(clip_write_then_read_document) {
    Document doc(320, 200);
    doc.dpi = 300;
    Layer* bg = doc.addLayerTop("Background");
    bg->surface.fill(Color::rgba(200, 220, 240, 255));
    Layer* ink = doc.addLayerTop("Ink");
    ink->surface.fillRect(Rect{10, 10, 100, 50}, Color::rgba(10, 20, 30, 255));
    ink->opacity = 0.5f;
    ink->mode = BlendMode::Multiply;
    Layer* hidden = doc.addLayerTop("Hidden");
    hidden->visible = false;
    hidden->clipToBelow = true;
    doc.setActiveIndex(1);

    std::string err;
    std::vector<uint8_t> file = buildClipFile(doc, {}, &err);
    CHECK(!file.empty());
    if (file.empty()) { std::printf("  %s\n", err.c_str()); return; }

    clip_document* raw = nullptr;
    clip_error e{};
    CHECK(clip_document_open_memory(&raw, file.data(), file.size(), &e));
    DocumentPtr cd(raw);
    if (!cd) { std::printf("  %s\n", e.message); return; }
    CHECK_EQ(clip_document_layer_count(cd.get()), size_t(4));  // 3 raster + Paper
    CHECK_EQ(clip_document_layer(cd.get(), 1)->opacity, 128);
    CHECK(blendModeFromComposite(clip_document_layer(cd.get(), 1)->composite) == BlendMode::Multiply);

    // The preview is the engine's flattened composite.
    clip_image pv{};
    CHECK(clip_document_canvas_preview(cd.get(), &pv, &e));
    Surface preview = toSurface(pv);
    clip_image_free(&pv);
    CHECK(preview.width() == 320 && preview.height() == 200);
    CHECK(preview.at(50, 30) == doc.flatten().at(50, 30));

    Document back;
    CHECK(toDocument(cd.get(), back, &err));
    CHECK_EQ(back.width(), 320);
    CHECK_EQ(back.dpi, 300.0);
    CHECK_EQ(back.layerCount(), size_t(3));
    CHECK(back.paperVisible);
    CHECK(back.paperColor == Color::rgba(255, 255, 255));
    CHECK_EQ(back.activeIndex(), size_t(1));
    CHECK_EQ(back.layer(1)->name, std::string("Ink"));
    CHECK(back.layer(1)->mode == BlendMode::Multiply);
    CHECK(!back.layer(2)->visible);
    CHECK(back.layer(2)->clipToBelow);
    CHECK(back.layer(1)->surface.at(50, 30) == Color::rgba(10, 20, 30, 255));
    Surface flat = back.flatten();
    Color expect = blendPixel(Color::rgba(200, 220, 240, 255), Color::rgba(10, 20, 30, 255), BlendMode::Multiply, 0.5f);
    CHECK(flat.at(50, 30) == expect);

    // Without paper and preview.
    doc.paperVisible = false;
    WriteOptions noPreview;
    noPreview.includePreview = false;
    file = buildClipFile(doc, noPreview, &err);
    raw = nullptr;
    CHECK(clip_document_open_memory(&raw, file.data(), file.size(), &e));
    cd.reset(raw);
    CHECK(cd && clip_document_layer_count(cd.get()) == 3);
    CHECK(cd && clip_document_row_count(cd.get(), "CanvasPreview") == 0);
}

TEST(clip_layer_effects_roundtrip) {
    Document doc(64, 64);
    Layer* a = doc.addLayerTop("Bordered");
    a->surface.fillRect(Rect{8, 8, 20, 20}, Color::rgba(200, 40, 10, 255));
    a->borderOn = true;
    a->borderWidth = 7;
    a->borderColor = Color::rgba(0x12, 0x34, 0x56);
    a->layerColorOn = true;
    a->layerColor = Color::rgba(40, 120, 230);
    Layer* b = doc.addLayerTop("Gray");
    b->surface.fillRect(Rect{8, 8, 20, 20}, Color::rgba(200, 40, 10, 255));
    b->gray = true;
    Layer* c = doc.addLayerTop("Plain");

    std::string err;
    std::vector<uint8_t> file = buildClipFile(doc, {}, &err);
    CHECK(!file.empty());
    if (file.empty()) { std::printf("  %s\n", err.c_str()); return; }
    clip_document* raw = nullptr;
    clip_error e{};
    CHECK(clip_document_open_memory(&raw, file.data(), file.size(), &e));
    DocumentPtr cd(raw);
    if (!cd) { std::printf("  %s\n", e.message); return; }
    Document back;
    CHECK(toDocument(cd.get(), back, &err));
    CHECK_EQ(back.layerCount(), size_t(3));

    const Layer* ba = back.layer(0);
    CHECK(ba->borderOn);
    CHECK_EQ(ba->borderWidth, 7);
    CHECK(ba->borderColor == Color::rgba(0x12, 0x34, 0x56));
    CHECK(ba->layerColorOn);
    CHECK(ba->layerColor == Color::rgba(40, 120, 230));
    CHECK(!ba->gray);
    CHECK(ba->surface.at(10, 10) == Color::rgba(200, 40, 10, 255));

    const Layer* bb = back.layer(1);
    CHECK(bb->gray && !bb->borderOn && !bb->layerColorOn);
    CHECK(bb->surface.at(10, 10) == Color::rgba(84, 84, 84, 255));  // stored as luma

    const Layer* bc = back.layer(2);
    CHECK(!bc->hasEffects() && !bc->layerColorOn);
    (void)c;
}

namespace {

// Writes `doc` and reads it back.
bool roundTrip(const Document& doc, Document& back) {
    std::string err;
    std::vector<uint8_t> file = buildClipFile(doc, {}, &err);
    if (file.empty()) { std::printf("  write: %s\n", err.c_str()); return false; }
    clip_document* raw = nullptr;
    clip_error e{};
    if (!clip_document_open_memory(&raw, file.data(), file.size(), &e)) { std::printf("  open: %s\n", e.message); return false; }
    DocumentPtr cd(raw);
    if (!toDocument(cd.get(), back, &err)) { std::printf("  read: %s\n", err.c_str()); return false; }
    return true;
}

}  // namespace

TEST(clip_correction_layers_roundtrip) {
    Document doc(32, 32);
    Layer* base = doc.addLayerTop("Base");
    base->surface.fill(Color::rgba(100, 150, 200, 255));

    Layer* curve = doc.addLayerTop("Curve");
    curve->kind = LayerKind::Adjustment;
    curve->adjustment = Adjustment::make(Adjustment::Kind::ToneCurve);
    curve->adjustment.curve = {{0, 0}, {128, 200}, {255, 255}};
    curve->adjustment.rebuild();

    Layer* bal = doc.addLayerTop("Balance");
    bal->kind = LayerKind::Adjustment;
    bal->adjustment = Adjustment::make(Adjustment::Kind::ColorBalance);
    bal->adjustment.balance[3] = 40;   // midtones towards red
    bal->adjustment.balance[8] = -25;  // highlights towards yellow
    bal->adjustment.preserveLuminosity = false;
    bal->adjustment.rebuild();

    Layer* post = doc.addLayerTop("Post");
    post->kind = LayerKind::Adjustment;
    post->adjustment = Adjustment::make(Adjustment::Kind::Posterize);
    post->adjustment.a = 3;
    post->adjustment.rebuild();

    Layer* bin = doc.addLayerTop("Bin");
    bin->kind = LayerKind::Adjustment;
    bin->adjustment = Adjustment::make(Adjustment::Kind::Binarize);
    bin->adjustment.a = 77;
    bin->adjustment.rebuild();
    bin->visible = false;

    Document back;
    CHECK(roundTrip(doc, back));
    CHECK_EQ(back.layerCount(), size_t(5));
    if (back.layerCount() != 5) return;

    const Adjustment& c = back.layer(1)->adjustment;
    CHECK(back.layer(1)->kind == LayerKind::Adjustment && c.kind == Adjustment::Kind::ToneCurve);
    CHECK(c.raw.empty());  // a master-only curve is editable again
    CHECK(c.curve == curve->adjustment.curve);
    CHECK(c.lut[0] == curve->adjustment.lut[0]);
    CHECK(c.apply(Color::rgba(128, 128, 128)).r == 200);

    const Adjustment& b = back.layer(2)->adjustment;
    CHECK(b.kind == Adjustment::Kind::ColorBalance);
    CHECK_EQ(b.balance[3], 40);
    CHECK_EQ(b.balance[8], -25);
    CHECK(!b.preserveLuminosity);
    CHECK(b.apply(Color::rgba(128, 128, 128)).r > 128);
    CHECK(b.apply(Color::rgba(128, 128, 128)).g == 128);

    const Adjustment& p = back.layer(3)->adjustment;
    CHECK(p.kind == Adjustment::Kind::Posterize && p.a == 3);
    CHECK(p.apply(Color::rgba(100, 100, 100)).r == 127);  // the middle of three levels
    CHECK(p.apply(Color::rgba(250, 250, 250)).r == 255);

    const Adjustment& z = back.layer(4)->adjustment;
    CHECK(z.kind == Adjustment::Kind::Binarize && z.a == 77);
    CHECK(z.apply(Color::rgba(76, 76, 76)).r == 0);
    CHECK(z.apply(Color::rgba(77, 77, 77)).r == 255);
    CHECK(!back.layer(4)->visible);

    // The stack composites the same before and after.
    CHECK(doc.flatten().at(5, 5) == back.flatten().at(5, 5));
}

TEST(clip_gradient_repeat_roundtrips) {
    Document doc(64, 16);
    Layer* g = doc.addLayerTop("Grad");
    g->kind = LayerKind::Gradient;
    g->gradient.stops = {{0.f, Color::rgba(0, 0, 0)}, {1.f, Color::rgba(255, 255, 255)}};
    g->gradient.x0 = 0; g->gradient.y0 = 8; g->gradient.x1 = 16; g->gradient.y1 = 8;
    g->gradient.repeat = GradientFill::Repeat::Flip;
    Surface before = doc.flatten();
    // Flip: 0..16 rises, 16..32 falls, 32..48 rises again (sampled at pixel centres).
    CHECK(before.at(8, 8).r > 100 && before.at(8, 8).r < 156);
    CHECK(before.at(15, 8).r > 230 && before.at(16, 8).r > 230);
    CHECK(before.at(31, 8).r < 25 && before.at(32, 8).r < 25);
    CHECK(before.at(47, 8).r > 230 && before.at(48, 8).r > 230);

    Document back;
    CHECK(roundTrip(doc, back));
    CHECK_EQ(back.layerCount(), size_t(1));
    if (back.layerCount() != 1) return;
    CHECK(back.layer(0)->kind == LayerKind::Gradient);
    CHECK(back.layer(0)->gradient.repeat == GradientFill::Repeat::Flip);
    CHECK(back.flatten().at(32, 8) == before.at(32, 8));

    g->gradient.repeat = GradientFill::Repeat::Repeat;
    Surface rep = doc.flatten();
    CHECK(rep.at(15, 8).r > 200);
    CHECK(rep.at(17, 8).r < 40);
}

TEST(clip_blend_mode_mapping_roundtrips) {
    const BlendMode modes[] = {BlendMode::Normal,   BlendMode::Multiply,  BlendMode::Screen,     BlendMode::Overlay,
                               BlendMode::Darken,   BlendMode::Lighten,   BlendMode::ColorDodge, BlendMode::ColorBurn,
                               BlendMode::HardLight, BlendMode::SoftLight, BlendMode::Difference, BlendMode::Exclusion,
                               BlendMode::Add,      BlendMode::Subtract,  BlendMode::Hue,        BlendMode::Saturation,
                               BlendMode::Color,    BlendMode::Luminosity};
    for (BlendMode m : modes) CHECK(blendModeFromComposite(compositeFromBlendMode(m)) == m);
}
