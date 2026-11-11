// Applying tools to the canvas: press, drag and release for every sub tool kind.
#include "ui.h"

namespace pu::app::ui {

// ---- tools ------------------------------------------------------------------------------------------------------
void selectTool(int index) {
    if (index < 0 || index >= static_cast<int>(g.state.tools.size())) return;
    if (index != g.state.toolIndex) g.subToolScroll = 0;
    g.state.toolIndex = index;
    g.state.status = std::string("Tool: ") + g.state.group().name + " / " + g.state.sub().name;
    invalidateAll();
}

BrushSettings effectiveBrush() {
    SubTool& st = g.state.sub();
    BrushSettings b = st.brush;
    b.minSizeRatio = st.sizePressure ? 0.1f : 1.f;
    b.minOpacityRatio = st.opacityPressure ? 0.1f : 1.f;
    b.eraser = st.kind == SubKind::Erase || g.state.colorSlot == ColorSlot::Transparent;
    b.color = g.state.drawColor();
    return b;
}

// The sub tool kind in effect, given the held modifier keys (Clip Studio's temporary tool switching).
SubKind effectiveKind() {
    bool ctrl = keyDown(VK_CONTROL), alt = keyDown(VK_MENU), shift = keyDown(VK_SHIFT);
    if (g.spaceDown) {
        if (ctrl && alt) return SubKind::ZoomOut;
        if (ctrl) return SubKind::ZoomIn;
        if (shift) return SubKind::Rotate;
        return SubKind::Hand;
    }
    SubKind k = g.state.sub().kind;
    bool selecting = k == SubKind::SelectRect || k == SubKind::SelectEllipse || k == SubKind::SelectLasso;
    if (selecting) return ctrl && !alt ? SubKind::MoveLayer : k;  // Shift/Alt add to / subtract from the selection
    bool drawing = k == SubKind::Paint || k == SubKind::Erase || k == SubKind::FillRefAll || k == SubKind::FillLayer ||
                   k == SubKind::GradientToSub || k == SubKind::GradientToTransparent;
    if (drawing && alt && !ctrl) return SubKind::PickDisplay;
    if (drawing && ctrl && !alt) return SubKind::MoveLayer;  // Ctrl+drag: temporary Move layer, as in Clip Studio
    return k;
}

void pickColor(int x, int y, bool layerOnly) {
    float cx, cy;
    g.state.view.canvasFromWindow(static_cast<float>(x), static_cast<float>(y), cx, cy);
    int px = static_cast<int>(std::floor(cx)), py = static_cast<int>(std::floor(cy));
    if (!g.state.doc.bounds().contains(px, py)) return;
    Color c;
    if (layerOnly) {
        Layer* l = g.state.active();
        if (!l) return;
        c = l->surface.at(px, py);
        if (c.a == 0) return;
    } else {
        c = g.composite.at(px, py);
        if (c.a < 255) c = blendPixel(g.state.doc.paperVisible ? g.state.doc.paperColor : Color::rgba(255, 255, 255), c, BlendMode::Normal, 1.f);
    }
    c.a = 255;
    setEditColor(c);
    syncHsv();
    invalidateAll();
}

void canvasPress(int x, int y, float pressure, int button) {
    g.dragStart = {x, y};
    g.dragView = g.state.view;
    g.dragMoved = false;
    SetCapture(g.hwnd);
    if (button != 0) { g.canvasDrag = CanvasDrag::Pan; return; }
    SubKind k = effectiveKind();
    g.dragKind = k;
    switch (k) {
        case SubKind::Hand: g.canvasDrag = CanvasDrag::Pan; break;
        case SubKind::Rotate: g.canvasDrag = CanvasDrag::Rotate; break;
        case SubKind::ZoomIn:
        case SubKind::ZoomOut: g.canvasDrag = CanvasDrag::Zoom; break;
        case SubKind::PickDisplay:
        case SubKind::PickLayer:
            g.canvasDrag = CanvasDrag::Pick;
            pickColor(x, y, k == SubKind::PickLayer);
            break;
        case SubKind::FillRefAll:
        case SubKind::FillLayer: {
            ReleaseCapture();
            float cx, cy;
            g.state.view.canvasFromWindow(static_cast<float>(x), static_cast<float>(y), cx, cy);
            SubTool& st = g.state.sub();
            Layer* l = g.state.active();
            if (!l) return;
            const Surface& ref = k == SubKind::FillRefAll ? g.composite : l->surface;
            Rect r = g.state.floodFill(static_cast<int>(std::floor(cx)), static_cast<int>(std::floor(cy)), ref, st.tolerance, st.expand,
                                       g.state.drawColor(), g.state.colorSlot == ColorSlot::Transparent);
            if (!r.empty()) contentChanged();
            else invalidateAll();
            break;
        }
        case SubKind::GradientToSub:
        case SubKind::GradientToTransparent: {
            Layer* l = g.state.active();
            if (!l || l->locked) {
                ReleaseCapture();
                g.state.status = l ? "The layer is locked" : "No layer";
                invalidateAll();
                break;
            }
            g.canvasDrag = CanvasDrag::Gradient;
            break;
        }
        case SubKind::SelectRect:
        case SubKind::SelectEllipse:
        case SubKind::SelectLasso: {
            // Shift adds to the selection and Alt subtracts from it, as in Clip Studio.
            bool add = keyDown(VK_SHIFT), sub = keyDown(VK_MENU);
            g.selectMode = add ? SelectMode::Add : sub ? SelectMode::Subtract : SelectMode::Replace;
            float cx, cy;
            g.state.view.canvasFromWindow(static_cast<float>(x), static_cast<float>(y), cx, cy);
            g.lasso.clear();
            g.lasso.push_back({cx, cy});
            g.canvasDrag = CanvasDrag::Select;
            break;
        }
        case SubKind::MoveLayer:
            if (g.state.beginLayerMove()) {
                g.canvasDrag = CanvasDrag::MoveLayer;
            } else {
                ReleaseCapture();
                invalidateAll();
            }
            break;
        case SubKind::Paint:
        case SubKind::Erase: {
            if (keyDown(VK_CONTROL) && keyDown(VK_MENU)) {
                g.canvasDrag = CanvasDrag::BrushSize;
                g.dragSize = g.state.sub().brush.size;
                break;
            }
            float cx, cy;
            g.state.view.canvasFromWindow(static_cast<float>(x), static_cast<float>(y), cx, cy);
            g.state.beginStroke(cx, cy, pressure, effectiveBrush(), g.state.group().name);
            if (g.state.strokeActive()) {
                g.canvasDrag = CanvasDrag::Paint;
                Rect d = g.state.strokeDirty();
                updateComposite(d);
                invalidateCanvas(d);
            } else {
                ReleaseCapture();
                invalidateAll();  // status message (locked/hidden layer)
            }
            break;
        }
    }
}

void canvasMove(int x, int y, float pressure) {
    if (std::abs(x - g.dragStart.x) + std::abs(y - g.dragStart.y) > 3) g.dragMoved = true;
    View& v = g.state.view;
    switch (g.canvasDrag) {
        case CanvasDrag::Paint: {
            float cx, cy;
            v.canvasFromWindow(static_cast<float>(x), static_cast<float>(y), cx, cy);
            g.state.continueStroke(cx, cy, pressure);
            Rect d = g.state.strokeDirty();
            if (!d.empty()) {
                updateComposite(d);
                invalidateCanvas(d);
            }
            break;
        }
        case CanvasDrag::Pan:
            g.userView = true;
            v.panX = g.dragView.panX + (x - g.dragStart.x);
            v.panY = g.dragView.panY + (y - g.dragStart.y);
            invalidateAll();
            break;
        case CanvasDrag::Rotate: {
            POINT c = viewCenter();
            float a0 = std::atan2(static_cast<float>(g.dragStart.y - c.y), static_cast<float>(g.dragStart.x - c.x));
            float a1 = std::atan2(static_cast<float>(y - c.y), static_cast<float>(x - c.x));
            float rot = g.dragView.rotation + (a1 - a0);
            if (keyDown(VK_SHIFT)) rot = std::round(rot / (kPi / 12)) * (kPi / 12);
            v = g.dragView;
            v.setKeeping(static_cast<float>(c.x), static_cast<float>(c.y), v.zoom, std::remainder(rot, 2 * kPi), v.flipH);
            g.userView = true;
            invalidateAll();
            break;
        }
        case CanvasDrag::Zoom:
            if (g.dragMoved) {
                v = g.dragView;
                float z = g.dragView.zoom * std::exp((x - g.dragStart.x) / static_cast<float>(S(150)));
                v.setKeeping(static_cast<float>(g.dragStart.x), static_cast<float>(g.dragStart.y), std::clamp(z, 0.01f, 64.f), v.rotation, v.flipH);
                g.userView = true;
                invalidateAll();
            }
            break;
        case CanvasDrag::Pick: pickColor(x, y, g.dragKind == SubKind::PickLayer); break;
        case CanvasDrag::Gradient: invalidateAll(); break;
        case CanvasDrag::Select: {
            if (g.dragKind == SubKind::SelectLasso) {
                float px, py;
                v.canvasFromWindow(static_cast<float>(x), static_cast<float>(y), px, py);
                auto [lx, ly] = g.lasso.back();
                if (std::hypot(px - lx, py - ly) * v.zoom >= 2.f) g.lasso.push_back({px, py});
            }
            invalidateAll();
            break;
        }
        case CanvasDrag::MoveLayer: {
            float ax, ay, bx, by;
            v.canvasFromWindow(static_cast<float>(g.dragStart.x), static_cast<float>(g.dragStart.y), ax, ay);
            v.canvasFromWindow(static_cast<float>(x), static_cast<float>(y), bx, by);
            int dx = static_cast<int>(std::lround(bx - ax)), dy = static_cast<int>(std::lround(by - ay));
            if (keyDown(VK_SHIFT)) (std::abs(dx) >= std::abs(dy) ? dy : dx) = 0;  // constrain to one axis
            g.state.previewLayerMove(dx, dy);
            updateComposite(g.state.doc.bounds());
            invalidateAll();
            break;
        }
        case CanvasDrag::BrushSize:
            g.state.sub().brush.size = std::clamp(g.dragSize * std::exp((x - g.dragStart.x) / static_cast<float>(S(120))), 0.5f, 1000.f);
            invalidateAll();
            break;
        default: break;
    }
}

void canvasRelease() {
    CanvasDrag d = g.canvasDrag;
    g.canvasDrag = CanvasDrag::None;
    ReleaseCapture();
    if (d == CanvasDrag::Paint) {
        Rect r = g.state.endStroke();
        updateComposite(r);
        if (!r.empty() && g.state.colorSlot != ColorSlot::Transparent && g.dragKind == SubKind::Paint) g.state.noteColorUsed(g.state.drawColor());
        contentChanged();
    } else if (d == CanvasDrag::Select) {
        AppState& s = g.state;
        if (!g.dragMoved) {
            // A click without a drag deselects (Replace mode), as in Clip Studio.
            if (g.selectMode == SelectMode::Replace) s.deselect();
        } else if (g.dragKind == SubKind::SelectLasso) {
            s.selectPolygon(g.lasso, g.selectMode);
        } else {
            float x1, y1;
            g.state.view.canvasFromWindow(static_cast<float>(g.mouse.x), static_cast<float>(g.mouse.y), x1, y1);
            auto [x0, y0] = g.lasso.front();
            if (g.dragKind == SubKind::SelectEllipse) s.selectEllipse(x0, y0, x1, y1, g.selectMode);
            else s.selectRect(x0, y0, x1, y1, g.selectMode);
        }
        g.lasso.clear();
        contentChanged();
    } else if (d == CanvasDrag::MoveLayer) {
        g.state.endLayerMove();
        contentChanged();
    } else if (d == CanvasDrag::Gradient) {
        if (g.dragMoved) {
            View& v = g.state.view;
            float ax, ay, bx, by;
            v.canvasFromWindow(static_cast<float>(g.dragStart.x), static_cast<float>(g.dragStart.y), ax, ay);
            v.canvasFromWindow(static_cast<float>(g.mouse.x), static_cast<float>(g.mouse.y), bx, by);
            if (keyDown(VK_SHIFT)) {  // snap the direction to 45° steps
                float ang = std::atan2(by - ay, bx - ax), len = std::hypot(bx - ax, by - ay);
                ang = std::round(ang / (kPi / 4)) * (kPi / 4);
                bx = ax + std::cos(ang) * len;
                by = ay + std::sin(ang) * len;
            }
            Color a = g.state.mainColor, b = g.state.subColor;
            if (g.dragKind == SubKind::GradientToTransparent) b = Color::rgba(a.r, a.g, a.b, 0);
            Rect r = g.state.drawGradient(ax, ay, bx, by, a, b, g.state.sub().brush.opacity);
            if (!r.empty()) {
                g.state.noteColorUsed(a);
                updateComposite(r);
            }
            contentChanged();
        } else {
            invalidateAll();
        }
    } else if (d == CanvasDrag::Zoom && !g.dragMoved) {
        int dir = g.dragKind == SubKind::ZoomIn ? 1 : -1;
        if (keyDown(VK_MENU) && !g.spaceDown) dir = -dir;
        zoomStep(dir, static_cast<float>(g.dragStart.x), static_cast<float>(g.dragStart.y));
    } else {
        invalidateAll();
    }
}

}  // namespace pu::app::ui
