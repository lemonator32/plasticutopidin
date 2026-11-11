// Tests for AppState editing operations: selections and Layer Property effects.
#include "app_state.h"
#include "test_main.h"

using namespace pu;
using namespace pu::app;

namespace {

const Color kRed = Color::rgba(255, 0, 0);

AppState fresh(int w = 40, int h = 30) {
    AppState s;
    s.newDocument(w, h, 72);
    return s;
}

}  // namespace

TEST(selection_rect_masks_fill_and_undoes) {
    AppState s = fresh();
    s.selectRect(10, 5, 20, 15, SelectMode::Replace);
    CHECK(s.hasSelection());
    CHECK_EQ(s.doc.selection.bounds.x, 10);
    CHECK_EQ(s.doc.selection.bounds.w, 10);
    s.fillLayer(kRed);
    const Surface& px = s.active()->surface;
    CHECK_EQ(px.at(12, 8).a, 255);
    CHECK_EQ(px.at(5, 8).a, 0);    // left of the selection
    CHECK_EQ(px.at(12, 20).a, 0);  // below it
    s.undo->undo();                // the fill
    CHECK_EQ(s.active()->surface.at(12, 8).a, 0);
    s.undo->undo();                // the selection
    CHECK(!s.hasSelection());
}

TEST(selection_add_subtract_invert) {
    AppState s = fresh();
    s.selectRect(0, 0, 10, 10, SelectMode::Replace);
    s.selectRect(20, 0, 30, 10, SelectMode::Add);
    CHECK(s.doc.selection.contains(5, 5));
    CHECK(s.doc.selection.contains(25, 5));
    CHECK(!s.doc.selection.contains(15, 5));
    s.selectRect(0, 0, 5, 10, SelectMode::Subtract);
    CHECK(!s.doc.selection.contains(2, 5));
    CHECK(s.doc.selection.contains(7, 5));
    s.invertSelection();
    CHECK(s.doc.selection.contains(2, 5));
    CHECK(!s.doc.selection.contains(7, 5));
    s.selectRect(0, 0, 40, 30, SelectMode::Subtract);  // subtracting everything leaves no selection
    CHECK(!s.hasSelection());
}

TEST(selection_lasso_and_ellipse) {
    AppState s = fresh();
    s.selectPolygon({{5.f, 5.f}, {35.f, 5.f}, {5.f, 25.f}}, SelectMode::Replace);  // triangle
    CHECK(s.doc.selection.contains(8, 8));
    CHECK(!s.doc.selection.contains(30, 22));
    s.selectEllipse(10, 5, 30, 25, SelectMode::Replace);
    CHECK(s.doc.selection.contains(20, 15));
    CHECK(!s.doc.selection.contains(11, 6));  // outside the curve, inside the box
}

TEST(stroke_is_clipped_to_selection) {
    AppState s = fresh();
    s.selectRect(0, 0, 20, 30, SelectMode::Replace);
    BrushSettings b;
    b.size = 6;
    b.color = kRed;
    s.beginStroke(5, 15, 1.f, b, "Pen");
    s.continueStroke(35, 15, 1.f);
    s.endStroke();
    CHECK(s.active()->surface.at(10, 15).a > 0);
    CHECK_EQ(s.active()->surface.at(30, 15).a, 0);
}

TEST(gradient_and_clear_outside_respect_selection) {
    AppState s = fresh();
    s.fillLayer(kRed);
    s.selectRect(10, 10, 20, 20, SelectMode::Replace);
    s.clearOutsideSelection();
    CHECK_EQ(s.active()->surface.at(15, 15).a, 255);
    CHECK_EQ(s.active()->surface.at(2, 2).a, 0);
    s.drawGradient(10, 0, 20, 0, Color::rgba(0, 0, 255), Color::rgba(0, 0, 255), 1.f);
    CHECK_EQ(s.active()->surface.at(15, 15).b, 255);
    CHECK_EQ(s.active()->surface.at(25, 25).a, 0);  // still cleared, the gradient did not leak
}

TEST(move_selection_moves_pixels_and_selection) {
    AppState s = fresh();
    s.selectRect(0, 0, 5, 5, SelectMode::Replace);
    s.fillLayer(kRed);
    CHECK(s.beginLayerMove());
    s.previewLayerMove(10, 10);
    s.endLayerMove();
    CHECK_EQ(s.active()->surface.at(2, 2).a, 0);
    CHECK_EQ(s.active()->surface.at(12, 12).a, 255);
    CHECK(s.doc.selection.contains(12, 12));
    CHECK(!s.doc.selection.contains(2, 2));
    s.undo->undo();
    CHECK_EQ(s.active()->surface.at(2, 2).a, 255);
    CHECK(s.doc.selection.contains(2, 2));
}

TEST(layer_property_effects_display_only) {
    AppState s = fresh(20, 20);
    Layer* l = s.active();
    l->surface.fillRect(Rect{8, 8, 4, 4}, Color::rgba(0, 0, 0));
    s.doc.paperVisible = false;

    l->borderOn = true;
    l->borderWidth = 2;
    l->borderColor = Color::rgba(0, 255, 0);
    Surface out = s.doc.flatten();
    CHECK_EQ(out.at(7, 10).g, 255);  // border just outside the pixels
    CHECK_EQ(out.at(10, 10).g, 0);   // the layer's own black pixel on top
    CHECK_EQ(out.at(2, 2).a, 0);     // far away: untouched
    CHECK_EQ(l->surface.at(7, 10).a, 0);  // the pixels themselves are unchanged

    l->borderOn = false;
    l->layerColorOn = true;
    l->layerColor = Color::rgba(40, 120, 230);
    out = s.doc.flatten();
    CHECK_EQ(out.at(10, 10).b, 230);  // black shows as the layer colour

    l->layerColorOn = false;
    l->surface.fillRect(Rect{0, 0, 2, 2}, kRed);
    l->gray = true;
    out = s.doc.flatten();
    CHECK_EQ(out.at(0, 0).r, out.at(0, 0).g);
}

TEST(layer_property_edit_is_one_undo_step) {
    AppState s = fresh();
    size_t before = s.undo->size();
    s.beginPropertyEdit();
    s.active()->borderOn = true;
    s.active()->borderWidth = 7;
    s.endPropertyEdit();
    CHECK_EQ(s.undo->size(), before + 1);
    s.undo->undo();
    CHECK(!s.active()->borderOn);
    CHECK_EQ(s.active()->borderWidth, 4);
}
