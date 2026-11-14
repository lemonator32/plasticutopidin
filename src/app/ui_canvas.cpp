// Canvas view, its tab, the navigation bar, tooltips and WM_PAINT.
#include "ui.h"

namespace pu::app::ui {

// ---- canvas, tab and navigation bar ------------------------------------------------------------------------------
void paintCanvas(HDC dc, const RECT& view, const RECT& clip) {
    RECT area;
    if (!IntersectRect(&area, &view, &clip)) return;
    SetDCBrushColor(dc, T().canvasBg);
    FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    if (!g.canvasDC) return;
    const View& v = g.state.view;
    int w = g.state.doc.width(), h = g.state.doc.height();
    // Canvas-space bounds of the area to redraw (inverse transform of its corners).
    float xs[4], ys[4];
    v.canvasFromWindow(static_cast<float>(area.left), static_cast<float>(area.top), xs[0], ys[0]);
    v.canvasFromWindow(static_cast<float>(area.right), static_cast<float>(area.top), xs[1], ys[1]);
    v.canvasFromWindow(static_cast<float>(area.right), static_cast<float>(area.bottom), xs[2], ys[2]);
    v.canvasFromWindow(static_cast<float>(area.left), static_cast<float>(area.bottom), xs[3], ys[3]);
    int sx0 = std::max(0, static_cast<int>(std::floor(*std::min_element(xs, xs + 4))) - 2);
    int sy0 = std::max(0, static_cast<int>(std::floor(*std::min_element(ys, ys + 4))) - 2);
    int sx1 = std::min(w, static_cast<int>(std::ceil(*std::max_element(xs, xs + 4))) + 2);
    int sy1 = std::min(h, static_cast<int>(std::ceil(*std::max_element(ys, ys + 4))) + 2);
    if (sx1 <= sx0 || sy1 <= sy0) return;
    int saved = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    SetGraphicsMode(dc, GM_ADVANCED);
    float c = std::cos(v.rotation), s = std::sin(v.rotation), f = v.flipH ? -1.f : 1.f;
    XFORM xf{f * c * v.zoom, f * s * v.zoom, -s * v.zoom, c * v.zoom, v.panX, v.panY};
    SetWorldTransform(dc, &xf);
    bool smooth = v.zoom < 1.f;
    SetStretchBltMode(dc, smooth ? HALFTONE : COLORONCOLOR);
    if (smooth) SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchBlt(dc, sx0, sy0, sx1 - sx0, sy1 - sy0, g.canvasDC, sx0, sy0, sx1 - sx0, sy1 - sy0, SRCCOPY);
    RestoreDC(dc, saved);
}

// Rebuilds the selection outline: every edge between a selected and an unselected pixel, with
// collinear runs merged so a large rectangle is four segments rather than thousands.
void rebuildAnts() {
    const Selection& s = g.state.doc.selection;
    if (g.antsVersion == s.version) return;
    g.antsVersion = s.version;
    g.antsSegs.clear();
    if (!s.active) return;
    const Rect b = s.bounds;
    auto in = [&](int x, int y) { return x >= 0 && y >= 0 && x < g.state.doc.width() && y < g.state.doc.height() && s.at(x, y) >= 128; };
    // Horizontal edges at y (between rows y-1 and y), runs along x.
    for (int y = b.y; y <= b.bottom(); ++y) {
        int run = -1;
        for (int x = b.x; x <= b.right(); ++x) {
            bool edge = x < b.right() && in(x, y - 1) != in(x, y);
            if (edge && run < 0) run = x;
            if (!edge && run >= 0) { g.antsSegs.push_back({POINT{run, y}, POINT{x, y}}); run = -1; }
        }
    }
    for (int x = b.x; x <= b.right(); ++x) {
        int run = -1;
        for (int y = b.y; y <= b.bottom(); ++y) {
            bool edge = y < b.bottom() && in(x - 1, y) != in(x, y);
            if (edge && run < 0) run = y;
            if (!edge && run >= 0) { g.antsSegs.push_back({POINT{x, run}, POINT{x, y}}); run = -1; }
        }
    }
}

// Draws marching ants along window-space segments (black dashes over white, offset by the phase).
void drawAnts(const std::vector<std::pair<gp::PointF, gp::PointF>>& segs) {
    gp::Pen white(gp::Color(255, 255, 255, 255), 1.f), black(gp::Color(255, 0, 0, 0), 1.f);
    gp::REAL dash[] = {4.f, 4.f};
    black.SetDashPattern(dash, 2);
    black.SetDashOffset(static_cast<gp::REAL>(g.antsPhase));
    for (auto& [a, b] : segs) {
        P.gx->DrawLine(&white, a, b);
        P.gx->DrawLine(&black, a, b);
    }
}

void paintSelection(const RECT& view) {
    rebuildAnts();
    const View& v = g.state.view;
    auto win = [&v](float cx, float cy) {
        float wx, wy;
        v.windowFromCanvas(cx, cy, wx, wy);
        return gp::PointF(wx, wy);
    };
    std::vector<std::pair<gp::PointF, gp::PointF>> segs;
    segs.reserve(g.antsSegs.size());
    for (auto& [a, b] : g.antsSegs)
        segs.push_back({win(static_cast<float>(a.x), static_cast<float>(a.y)), win(static_cast<float>(b.x), static_cast<float>(b.y))});
    // The shape being dragged.
    if (g.canvasDrag == CanvasDrag::Select && !g.lasso.empty()) {
        if (g.dragKind == SubKind::SelectLasso) {
            for (size_t i = 1; i < g.lasso.size(); ++i)
                segs.push_back({win(g.lasso[i - 1].first, g.lasso[i - 1].second), win(g.lasso[i].first, g.lasso[i].second)});
            float mx, my;
            v.canvasFromWindow(static_cast<float>(g.mouse.x), static_cast<float>(g.mouse.y), mx, my);
            segs.push_back({win(g.lasso.back().first, g.lasso.back().second), win(mx, my)});
        } else {
            float x0 = g.lasso.front().first, y0 = g.lasso.front().second, x1, y1;
            v.canvasFromWindow(static_cast<float>(g.mouse.x), static_cast<float>(g.mouse.y), x1, y1);
            std::vector<gp::PointF> pts;
            if (g.dragKind == SubKind::SelectEllipse) {
                float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = std::abs(x1 - x0) / 2, ry = std::abs(y1 - y0) / 2;
                for (int i = 0; i <= 72; ++i) pts.push_back(win(cx + rx * std::cos(i * kPi / 36), cy + ry * std::sin(i * kPi / 36)));
            } else {
                pts = {win(x0, y0), win(x1, y0), win(x1, y1), win(x0, y1), win(x0, y0)};
            }
            for (size_t i = 1; i < pts.size(); ++i) segs.push_back({pts[i - 1], pts[i]});
        }
    }
    if (segs.empty()) return;
    gp::Region old;
    P.gx->GetClip(&old);
    P.gx->SetClip(rf(view), gp::CombineModeIntersect);
    drawAnts(segs);
    P.gx->SetClip(&old);
}

void paintCanvasOverlay(const RECT& view) {
    paintSelection(view);
    // Brush cursor: a circle showing the brush size, as Clip Studio draws it.
    g.cursorRect = RECT{};
    if (g.canvasDrag == CanvasDrag::Gradient) {
        // Gradient guide: start and end handles joined by a line, as Clip Studio shows while dragging.
        gp::Region old;
        P.gx->GetClip(&old);
        P.gx->SetClip(rf(view), gp::CombineModeIntersect);
        gp::Pen dark(gp::Color(220, 0, 0, 0), 3.f), light(gp::Color(255, 255, 255, 255), 1.f);
        gp::PointF a(static_cast<float>(g.dragStart.x), static_cast<float>(g.dragStart.y)), b(static_cast<float>(g.mouse.x), static_cast<float>(g.mouse.y));
        P.gx->DrawLine(&dark, a, b);
        P.gx->DrawLine(&light, a, b);
        for (const gp::PointF& p : {a, b}) {
            P.gx->DrawEllipse(&dark, p.X - 4, p.Y - 4, 8.f, 8.f);
            P.gx->DrawEllipse(&light, p.X - 4, p.Y - 4, 8.f, 8.f);
        }
        P.gx->SetClip(&old);
        return;
    }
    if (!g.mouseInside || !PtInRect(&view, g.mouse) || g.uiCapture || g.canvasDrag == CanvasDrag::Pan || g.canvasDrag == CanvasDrag::Rotate) return;
    SubKind k = g.canvasDrag == CanvasDrag::BrushSize ? SubKind::Paint : g.state.sub().kind;
    if (g.spaceDown || (keyDown(VK_MENU) && !keyDown(VK_CONTROL))) return;
    if (k != SubKind::Paint && k != SubKind::Erase) return;
    float r = std::max(1.f, g.state.sub().brush.size * g.state.view.zoom / 2.f);
    POINT c = g.canvasDrag == CanvasDrag::BrushSize ? g.dragStart : g.mouse;
    g.cursorRect = rc(static_cast<int>(c.x - r) - 3, static_cast<int>(c.y - r) - 3, static_cast<int>(c.x + r) + 4, static_cast<int>(c.y + r) + 4);
    if (!need(g.cursorRect)) return;
    gp::Region old;
    P.gx->GetClip(&old);
    P.gx->SetClip(rf(view), gp::CombineModeIntersect);
    gp::Pen dark(gp::Color(200, 0, 0, 0), 1.f), light(gp::Color(200, 255, 255, 255), 1.f);
    P.gx->DrawEllipse(&dark, c.x - r, c.y - r, 2 * r, 2 * r);
    P.gx->DrawEllipse(&light, c.x - r + 1, c.y - r + 1, 2 * r - 2, 2 * r - 2);
    P.gx->SetClip(&old);
}

void paintCanvasTab(const RECT& r) {
    fillRect(r, T().frame);
    std::wstring name = baseName(g.state.filePath) + (g.state.dirty ? L" *" : L"");
    wchar_t info[160];
    swprintf(info, 160, L"  (%d x %dpx %.0fdpi %.1f%%)", g.state.doc.width(), g.state.doc.height(), g.state.doc.dpi, g.state.view.zoom * 100.0);
    std::wstring label = name + info;
    int w = std::min(W(r) - S(8), static_cast<int>(textWidth(label)) + S(44));
    RECT tab = rc(r.left + S(4), r.top + S(3), r.left + S(4) + w, r.bottom);
    fillRect(tab, T().palette);
    fillRect(rc(tab.left, tab.top, tab.right, tab.top + S(2)), T().accent);
    text(rc(tab.left + S(10), tab.top, tab.right - S(26), tab.bottom), label, T().text);
    button(rc(tab.right - S(22), tab.top + S(3), tab.right - S(4), tab.bottom - S(2)), Icon::Close, L"Close canvas", [] { exec(CmdClose); });
}

void paintNavBar(const RECT& r) {
    fillRect(r, T().header);
    fillRect(rc(r.left, r.top, r.right, r.top + 1), T().border);
    int bs = S(22), y = r.top + (H(r) - bs) / 2 + 1, x = r.left + S(6);
    auto btn = [&](Icon ic, const wchar_t* tip, int cmd, bool on = false) { x = button(rc(x, y, x + bs, y + bs), ic, tip, [cmd] { exec(cmd); }, on) + S(1); };
    btn(Icon::ZoomOut, L"Zoom out (Ctrl+-)", CmdZoomOut);
    RECT zt = rc(x, r.top, x + S(56), r.bottom);
    text(zt, fmt(L"%.1f", g.state.view.zoom * 100.0), T().text, Center);
    addHit(zt, L"Zoom").wheel = [](int d) { zoomStepCenter(d > 0 ? 1 : -1); };
    x = zt.right;
    btn(Icon::ZoomIn, L"Zoom in (Ctrl++)", CmdZoomIn);
    btn(Icon::Actual, L"100% (Ctrl+Alt+0)", CmdZoom100);
    btn(Icon::Fit, L"Fit to navigator (Ctrl+0)", CmdZoomFit);
    x += S(10);
    btn(Icon::RotateLeft, L"Rotate left (-)", CmdRotateLeft);
    RECT rt = rc(x, r.top, x + S(44), r.bottom);
    text(rt, fmt(L"%.0f°", g.state.view.rotation * 180.0 / kPi), T().text, Center);
    x = rt.right;
    btn(Icon::RotateRight, L"Rotate right (^)", CmdRotateRight);
    btn(Icon::ResetView, L"Reset rotation (Ctrl+@)", CmdResetView);
    btn(Icon::FlipH, L"Flip horizontal", CmdFlipView, g.state.view.flipH);
    std::wstring st = widen(g.state.status);
    text(rc(x + S(16), r.top, r.right - S(8), r.bottom), st, T().textDim, Right);
}

void paintTooltip(const RECT& client) {
    g.tipRect = RECT{};
    if (!g.tipShown || g.hoverTip.empty()) return;
    int w = static_cast<int>(textWidth(g.hoverTip, g.fontSmall.get())) + S(14), h = S(22);
    int x = g.mouse.x + S(12), y = g.mouse.y + S(20);
    if (x + w > client.right) x = client.right - w - 2;
    if (y + h > client.bottom) y = g.mouse.y - h - S(6);
    g.tipRect = rc(x, y, x + w, y + h);
    fillRect(g.tipRect, RGB(250, 250, 236));
    frameRect(g.tipRect, RGB(110, 110, 110));
    text(g.tipRect, g.hoverTip, RGB(20, 20, 20), Center, g.fontSmall.get());
}

// ---- WM_PAINT ---------------------------------------------------------------------------------------------------------
void ensureBackBuffer(HDC dc, int w, int h) {
    if (g.backBmp && g.backW == w && g.backH == h) return;
    if (g.backDC) {
        SelectObject(g.backDC, g.backOld);
        DeleteObject(g.backBmp);
        DeleteDC(g.backDC);
    }
    g.backDC = CreateCompatibleDC(dc);
    g.backBmp = CreateCompatibleBitmap(dc, std::max(w, 1), std::max(h, 1));
    g.backOld = SelectObject(g.backDC, g.backBmp);
    g.backW = w;
    g.backH = h;
}

void onPaint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(g.hwnd, &ps);
    Layout L = computeLayout();
    const RECT& c = L.client;
    ensureBackBuffer(dc, c.right, c.bottom);
    // Every paint rebuilds the hit list; palettes outside the repaint region
    // skip their drawing (need()) but still register hits.
    P.clip = ps.rcPaint;
    paintCanvas(g.backDC, L.view, ps.rcPaint);
    {
        gp::Graphics gx(g.backDC);
        gx.SetClip(rf(ps.rcPaint));
        gx.SetSmoothingMode(gp::SmoothingModeAntiAlias);
        gx.SetPixelOffsetMode(gp::PixelOffsetModeHalf);
        gx.SetTextRenderingHint(gp::TextRenderingHintClearTypeGridFit);
        gx.SetInterpolationMode(gp::InterpolationModeHighQualityBilinear);
        P.gx = &gx;
        g.hits.clear();
        paintMenuBar(L.menu);
        paintCommandBar(L.cmd);
        paintToolStrip(L.tool);
        paintColumn(g.leftDocks, L.left);
        paintColumn(g.rightDocks, L.right);
        // Column splitters (drag to resize).
        for (int side = 0; side < 2; ++side) {
            RECT sp = side == 0 ? L.leftSplit : L.rightSplit;
            if (W(sp) <= 0) continue;
            fillRect(sp, T().frame);
            Hit& h = addHit(sp);
            h.cursor = IDC_SIZEWE;
            auto last = std::make_shared<int>(0);
            h.press = [last](int x, int) { *last = x; };
            h.drag = [last, side](int x, int) {
                int dx = x - *last;
                *last = x;
                int& w = side == 0 ? g.leftW : g.rightW;
                w = std::clamp(w + (side == 0 ? dx : -dx), S(170), S(520));
                invalidateAll();
            };
        }
        paintCanvasTab(L.tab);
        paintNavBar(L.nav);
        paintCanvasOverlay(L.view);
        paintTooltip(c);
        P.gx = nullptr;
    }
    BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top, g.backDC,
           ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
    EndPaint(g.hwnd, &ps);
}

}  // namespace pu::app::ui
