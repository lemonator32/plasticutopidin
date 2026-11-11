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

TEST(clip_blend_mode_mapping_roundtrips) {
    const BlendMode modes[] = {BlendMode::Normal,   BlendMode::Multiply,  BlendMode::Screen,     BlendMode::Overlay,
                               BlendMode::Darken,   BlendMode::Lighten,   BlendMode::ColorDodge, BlendMode::ColorBurn,
                               BlendMode::HardLight, BlendMode::SoftLight, BlendMode::Difference, BlendMode::Exclusion,
                               BlendMode::Add,      BlendMode::Subtract,  BlendMode::Hue,        BlendMode::Saturation,
                               BlendMode::Color,    BlendMode::Luminosity};
    for (BlendMode m : modes) CHECK(blendModeFromComposite(compositeFromBlendMode(m)) == m);
}
