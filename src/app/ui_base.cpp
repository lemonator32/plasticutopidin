// UI foundations: string/colour helpers, invalidation, the canvas DIB, view control,
// GDI+ painting primitives, widgets (buttons, sliders), bitmaps and dock layout.
#include "ui.h"

namespace pu::app::ui {

App g;
Painter P;

// ---- small helpers -------------------------------------------------------------
std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(std::max(n, 1)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}
std::string narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(std::max(n, 1)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}
std::wstring fmt(const wchar_t* f, double v) {
    wchar_t buf[64];
    swprintf(buf, 64, f, v);
    return buf;
}
std::wstring baseName(const std::string& path) {
    if (path.empty()) return L"Untitled";
    std::wstring w = widen(path);
    size_t p = w.find_last_of(L"\\/");
    return p == std::wstring::npos ? w : w.substr(p + 1);
}
RECT rc(int l, int t, int r, int b) { return RECT{l, t, r, b}; }
RECT inset(RECT r, int d) { return RECT{r.left + d, r.top + d, r.right - d, r.bottom - d}; }
int W(const RECT& r) { return r.right - r.left; }
int H(const RECT& r) { return r.bottom - r.top; }
bool keyDown(int vk) { return GetKeyState(vk) < 0; }

void rgbToHsv(Color c, float& h, float& s, float& v) {
    float r = c.r / 255.f, gg = c.g / 255.f, b = c.b / 255.f;
    float mx = std::max({r, gg, b}), mn = std::min({r, gg, b}), d = mx - mn;
    v = mx;
    s = mx > 0.f ? d / mx : 0.f;
    if (d <= 0.f) return;  // keep the previous hue for greys
    if (mx == r) h = std::fmod((gg - b) / d + 6.f, 6.f) / 6.f;
    else if (mx == gg) h = ((b - r) / d + 2.f) / 6.f;
    else h = ((r - gg) / d + 4.f) / 6.f;
}
Color hsvToRgb(float h, float s, float v) {
    h = h - std::floor(h);
    float i = std::floor(h * 6.f), f = h * 6.f - i;
    float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float r, gg, b;
    switch (static_cast<int>(i) % 6) {
        case 0: r = v; gg = t; b = p; break;
        case 1: r = q; gg = v; b = p; break;
        case 2: r = p; gg = v; b = t; break;
        case 3: r = p; gg = q; b = v; break;
        case 4: r = t; gg = p; b = v; break;
        default: r = v; gg = p; b = q; break;
    }
    auto u = [](float x) { return static_cast<uint8_t>(std::clamp(x * 255.f + 0.5f, 0.f, 255.f)); };
    return Color::rgba(u(r), u(gg), u(b));
}

// The colour shown in the colour palettes: the active slot, or main when transparent is active.
Color editColor() { return g.state.colorSlot == ColorSlot::Sub ? g.state.subColor : g.state.mainColor; }
void syncHsv() { rgbToHsv(editColor(), g.hue, g.sat, g.val); }
void setEditColor(Color c) {
    if (g.state.colorSlot == ColorSlot::Sub) g.state.subColor = c;
    else { g.state.mainColor = c; g.state.colorSlot = ColorSlot::Main; }
}

// ---- invalidation ----------------------------------------------------------------
void invalidateAll() { if (g.hwnd) InvalidateRect(g.hwnd, nullptr, FALSE); }
void invalidate(const RECT& r) { if (g.hwnd && !IsRectEmpty(&r)) InvalidateRect(g.hwnd, &r, FALSE); }

bool palShown(Pal p) { return !g.hideAll && g.palVisible[p]; }

void setTitle() {
    std::wstring t = baseName(g.state.filePath) + (g.state.dirty ? L" *" : L"") + L" - Plastic Paint";
    SetWindowTextW(g.hwnd, t.c_str());
}

void defer(std::function<void()> fn) {
    g.deferred.push_back(std::move(fn));
    PostMessageW(g.hwnd, WM_APP + 1, 0, 0);
}

// Popup menus cannot start while a pointer is pressed on our window (the
// implicit pointer capture makes TrackPopupMenu return at once), so popups
// requested from a press open when the pointer is released.
void deferPopup(std::function<void()> fn) {
    if (g.pointerDown) g.afterRelease.push_back(std::move(fn));
    else defer(std::move(fn));
}

void flushAfterRelease() {
    auto work = std::move(g.afterRelease);
    g.afterRelease.clear();
    for (auto& fn : work) defer(std::move(fn));
}

// ---- canvas image -----------------------------------------------------------------
void ensureCanvasDib() {
    int w = g.state.doc.width(), h = g.state.doc.height();
    if (g.canvasBmp && g.canvasW == w && g.canvasH == h) return;
    if (g.canvasDC) {
        SelectObject(g.canvasDC, g.canvasOld);
        DeleteObject(g.canvasBmp);
        DeleteDC(g.canvasDC);
    }
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = std::max(w, 1);
    bmi.bmiHeader.biHeight = -std::max(h, 1);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    g.canvasDC = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    void* bits = nullptr;
    g.canvasBmp = CreateDIBSection(g.canvasDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    g.canvasBits = static_cast<uint8_t*>(bits);
    g.canvasOld = SelectObject(g.canvasDC, g.canvasBmp);
    g.canvasW = w;
    g.canvasH = h;
    g.composite.reset(w, h);
}

void updateComposite(Rect r) {
    ensureCanvasDib();
    r = r.intersect(g.state.doc.bounds());
    if (r.empty() || !g.canvasBits) return;
    GdiFlush();
    g.state.doc.composite(g.composite, r);
    int w = g.composite.width();
    for (int y = r.y; y < r.bottom(); ++y) {
        const Color* src = g.composite.row(y) + r.x;
        uint8_t* dst = g.canvasBits + (static_cast<size_t>(y) * w + r.x) * 4;
        for (int x = 0; x < r.w; ++x) {
            Color c = src[x];
            if (c.a < 255) {  // transparency shows as a checkerboard
                uint8_t check = (((r.x + x) >> 3) + (y >> 3)) & 1 ? 204 : 255;
                c = blendPixel(Color::rgba(check, check, check, 255), c, BlendMode::Normal, 1.f);
            }
            dst[x * 4 + 0] = c.b;
            dst[x * 4 + 1] = c.g;
            dst[x * 4 + 2] = c.r;
            dst[x * 4 + 3] = 255;
        }
    }
}

// After any document change: recomposite, mark caches stale, repaint.
void contentChanged() {
    updateComposite(g.state.doc.bounds());
    g.thumbsDirty = g.navDirty = true;
    setTitle();
    invalidateAll();
}

RECT windowBoundsOfCanvasRect(Rect r) {
    const View& v = g.state.view;
    float xs[4], ys[4];
    v.windowFromCanvas(static_cast<float>(r.x), static_cast<float>(r.y), xs[0], ys[0]);
    v.windowFromCanvas(static_cast<float>(r.right()), static_cast<float>(r.y), xs[1], ys[1]);
    v.windowFromCanvas(static_cast<float>(r.right()), static_cast<float>(r.bottom()), xs[2], ys[2]);
    v.windowFromCanvas(static_cast<float>(r.x), static_cast<float>(r.bottom()), xs[3], ys[3]);
    RECT out{static_cast<LONG>(std::floor(*std::min_element(xs, xs + 4))) - 2, static_cast<LONG>(std::floor(*std::min_element(ys, ys + 4))) - 2,
             static_cast<LONG>(std::ceil(*std::max_element(xs, xs + 4))) + 2, static_cast<LONG>(std::ceil(*std::max_element(ys, ys + 4))) + 2};
    return out;
}

void invalidateCanvas(Rect r) {
    RECT wr = windowBoundsOfCanvasRect(r);
    RECT view = computeLayout().view, out;
    if (IntersectRect(&out, &wr, &view)) invalidate(out);
}

// ---- view ---------------------------------------------------------------------------
POINT viewCenter() {
    RECT v = computeLayout().view;
    return POINT{(v.left + v.right) / 2, (v.top + v.bottom) / 2};
}

void fitView() {
    RECT vr = computeLayout().view;
    View& v = g.state.view;
    float w = static_cast<float>(g.state.doc.width()), h = static_cast<float>(g.state.doc.height());
    float c = std::fabs(std::cos(v.rotation)), s = std::fabs(std::sin(v.rotation));
    float bw = w * c + h * s, bh = w * s + h * c;
    float zx = (W(vr) - S(40)) / bw, zy = (H(vr) - S(40)) / bh;
    v.zoom = std::clamp(std::min(zx, zy), 0.01f, 64.f);
    v.panX = 0;
    v.panY = 0;
    float cx, cy;
    v.windowFromCanvas(w / 2, h / 2, cx, cy);
    v.panX = (vr.left + vr.right) / 2.f - cx;
    v.panY = (vr.top + vr.bottom) / 2.f - cy;
}

const float kZoomSteps[] = {0.01f, 0.015f, 0.02f, 0.03f, 0.05f, 0.0625f, 0.083f, 0.125f, 0.167f, 0.25f, 0.333f, 0.5f, 0.667f, 1.f,
                            1.5f,  2.f,    3.f,   4.f,   6.f,   8.f,     12.f,   16.f,   24.f,   32.f,  48.f,   64.f};

void zoomAt(float wx, float wy, float newZoom) {
    View& v = g.state.view;
    g.userView = true;
    v.setKeeping(wx, wy, std::clamp(newZoom, 0.01f, 64.f), v.rotation, v.flipH);
    invalidateAll();
}

void zoomStep(int dir, float wx, float wy) {
    float z = g.state.view.zoom;
    float next = z;
    if (dir > 0) {
        for (float s : kZoomSteps) if (s > z * 1.001f) { next = s; break; }
    } else {
        for (int i = static_cast<int>(std::size(kZoomSteps)) - 1; i >= 0; --i)
            if (kZoomSteps[i] < z * 0.999f) { next = kZoomSteps[i]; break; }
    }
    zoomAt(wx, wy, next);
}

void zoomStepCenter(int dir) {
    POINT c = viewCenter();
    zoomStep(dir, static_cast<float>(c.x), static_cast<float>(c.y));
}

void setRotation(float radians) {
    POINT c = viewCenter();
    float r = std::remainder(radians, 2 * kPi);
    View& v = g.state.view;
    g.userView = true;
    v.setKeeping(static_cast<float>(c.x), static_cast<float>(c.y), v.zoom, r, v.flipH);
    invalidateAll();
}

void toggleFlipView() {
    POINT c = viewCenter();
    View& v = g.state.view;
    v.setKeeping(static_cast<float>(c.x), static_cast<float>(c.y), v.zoom, v.rotation, !v.flipH);
    invalidateAll();
}

// ---- painting helpers ---------------------------------------------------------------

bool need(const RECT& r) {
    RECT t;
    return IntersectRect(&t, &r, &P.clip) != 0;
}
gp::Color gc(COLORREF c, BYTE a) { return gp::Color(a, GetRValue(c), GetGValue(c), GetBValue(c)); }
gp::Color gc(Color c) { return gp::Color(255, c.r, c.g, c.b); }
gp::RectF rf(const RECT& r) { return gp::RectF(static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(W(r)), static_cast<float>(H(r))); }
COLORREF mix(COLORREF a, COLORREF b, float t) {
    auto m = [t](int x, int y) { return static_cast<int>(x + (y - x) * t + 0.5f); };
    return RGB(m(GetRValue(a), GetRValue(b)), m(GetGValue(a), GetGValue(b)), m(GetBValue(a), GetBValue(b)));
}

void fillRect(const RECT& r, COLORREF c) {
    if (!need(r)) return;
    gp::SolidBrush b(gc(c));
    P.gx->FillRectangle(&b, rf(r));
}
void fillRectC(const RECT& r, Color c) {
    if (!need(r)) return;
    gp::SolidBrush b(gc(c));
    P.gx->FillRectangle(&b, rf(r));
}
void frameRect(const RECT& r, COLORREF c, int w) {
    if (!need(r)) return;
    fillRect(rc(r.left, r.top, r.right, r.top + w), c);
    fillRect(rc(r.left, r.bottom - w, r.right, r.bottom), c);
    fillRect(rc(r.left, r.top, r.left + w, r.bottom), c);
    fillRect(rc(r.right - w, r.top, r.right, r.bottom), c);
}
void roundRect(const RECT& r, COLORREF c, int radiusPx) {
    float radius = static_cast<float>(radiusPx);
    if (!need(r)) return;
    gp::GraphicsPath path;
    float d = radius * 2, x = static_cast<float>(r.left), y = static_cast<float>(r.top), w = static_cast<float>(W(r)), h = static_cast<float>(H(r));
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
    gp::SolidBrush b(gc(c));
    P.gx->FillPath(&b, &path);
}
void checker(const RECT& r, int cell) {
    if (!need(r)) return;
    fillRect(r, RGB(255, 255, 255));
    gp::SolidBrush b(gc(RGB(204, 204, 204)));
    for (int y = r.top; y < r.bottom; y += cell)
        for (int x = r.left + (((y - r.top) / cell) & 1) * cell; x < r.right; x += 2 * cell)
            P.gx->FillRectangle(&b, x, y, std::min(cell, static_cast<int>(r.right - x)), std::min(cell, static_cast<int>(r.bottom - y)));
}

void text(const RECT& r, const std::wstring& s, COLORREF c, Align a, gp::Font* f, bool wrap) {
    if (!need(r) || s.empty()) return;
    gp::StringFormat sf;
    sf.SetAlignment(a == Left ? gp::StringAlignmentNear : a == Center ? gp::StringAlignmentCenter : gp::StringAlignmentFar);
    sf.SetLineAlignment(wrap ? gp::StringAlignmentNear : gp::StringAlignmentCenter);
    sf.SetTrimming(gp::StringTrimmingEllipsisCharacter);
    if (!wrap) sf.SetFormatFlags(gp::StringFormatFlagsNoWrap);
    gp::SolidBrush b(gc(c));
    P.gx->DrawString(s.c_str(), -1, f ? f : g.font.get(), rf(r), &sf, &b);
}
float textWidth(const std::wstring& s, gp::Font* f) {
    gp::RectF out;
    P.gx->MeasureString(s.c_str(), -1, f ? f : g.font.get(), gp::PointF(0, 0), &out);
    return out.Width;
}
void icon(Icon ic, const RECT& r, COLORREF c, int pad) {
    if (!need(r)) return;
    RECT i = inset(r, pad);
    drawIcon(*P.gx, ic, rf(i), gc(c));
}

bool hovered(const RECT& r) { return g.mouseInside && !g.uiCapture && g.canvasDrag == CanvasDrag::None && PtInRect(&r, g.mouse); }

Hit& addHit(const RECT& r, std::wstring tip) {
    g.hits.emplace_back();
    Hit& h = g.hits.back();
    h.r = r;
    h.tip = std::move(tip);
    return h;
}

// An icon button.  Returns the rect's right edge for chaining.
int button(const RECT& r, Icon ic, const std::wstring& tip, std::function<void()> fn, bool on, bool enabled) {
    if (need(r)) {
        bool hov = enabled && hovered(r);
        if (on) roundRect(r, T().buttonOn, S(3));
        else if (hov) roundRect(r, T().buttonHover, S(3));
        icon(ic, r, enabled ? (on ? RGB(255, 255, 255) : T().text) : T().textDim, std::max(S(4), H(r) / 5));
    }
    if (enabled && fn) addHit(r, tip).press = [fn](int, int) { fn(); };
    else if (!tip.empty()) addHit(r, tip);
    return r.right;
}


float sliderT(const SliderSpec& s, float v) {
    if (s.log) return std::clamp(std::log(v / s.lo) / std::log(s.hi / s.lo), 0.f, 1.f);
    return std::clamp((v - s.lo) / (s.hi - s.lo), 0.f, 1.f);
}
float sliderV(const SliderSpec& s, float t) {
    t = std::clamp(t, 0.f, 1.f);
    float v = s.log ? s.lo * std::pow(s.hi / s.lo, t) : s.lo + (s.hi - s.lo) * t;
    float q = std::pow(10.f, static_cast<float>(s.decimals));
    return std::round(v * q) / q;
}

// A Clip Studio style slider: label on the left, a bar filled to the value
// with the number drawn inside it.
void slider(const RECT& r, const SliderSpec& s, int labelW) {
    RECT bar = rc(r.left + labelW, r.top + S(3), r.right, r.bottom - S(3));
    if (need(r)) {
        if (labelW > 0) text(rc(r.left, r.top, r.left + labelW - S(4), r.bottom), s.label, T().textDim);
        fillRect(bar, T().field);
        float t = sliderT(s, s.value);
        RECT fillR = rc(bar.left, bar.top, bar.left + static_cast<int>(W(bar) * t), bar.bottom);
        fillRect(fillR, hovered(bar) ? mix(T().accent, RGB(255, 255, 255), 0.12f) : T().accent);
        wchar_t buf[48];
        swprintf(buf, 48, s.decimals ? L"%.*f%s" : L"%.*f%s", s.decimals, static_cast<double>(s.value), s.suffix.c_str());
        text(rc(bar.left + S(4), bar.top, bar.right - S(5), bar.bottom), buf, RGB(240, 240, 240), Right);
    }
    SliderSpec spec = s;
    Hit& h = addHit(bar, s.label);
    h.cursor = IDC_SIZEWE;
    auto setAt = [spec, bar](int x, int) { spec.set(sliderV(spec, (x - bar.left) / static_cast<float>(std::max(1, W(bar))))); };
    h.press = [spec, setAt](int x, int y) { if (spec.begin) spec.begin(); setAt(x, y); };
    h.drag = setAt;
    h.release = [spec] { if (spec.end) spec.end(); };
    h.wheel = [spec](int delta) {
        if (spec.log) {
            float t = sliderT(spec, spec.value) + (delta > 0 ? 0.02f : -0.02f);
            float v = sliderV(spec, t);
            if (v == spec.value) v = std::clamp(spec.value + (delta > 0 ? 1.f : -1.f) * std::pow(10.f, -static_cast<float>(spec.decimals)), spec.lo, spec.hi);
            spec.set(v);
        } else {
            float step = spec.wheelStep > 0 ? spec.wheelStep : std::max((spec.hi - spec.lo) / 50.f, std::pow(10.f, -static_cast<float>(spec.decimals)));
            if (spec.begin) spec.begin();
            spec.set(std::clamp(spec.value + (delta > 0 ? step : -step), spec.lo, spec.hi));
            if (spec.end) spec.end();
        }
    };
}

// ---- bitmaps --------------------------------------------------------------------------
std::unique_ptr<gp::Bitmap> makeBitmap(int w, int h, const std::function<void(uint32_t*, int stride)>& fill) {
    auto bmp = std::make_unique<gp::Bitmap>(w, h, PixelFormat32bppARGB);
    gp::BitmapData data;
    gp::Rect lr(0, 0, w, h);
    if (bmp->LockBits(&lr, gp::ImageLockModeWrite, PixelFormat32bppARGB, &data) == gp::Ok) {
        fill(static_cast<uint32_t*>(data.Scan0), data.Stride / 4);
        bmp->UnlockBits(&data);
    }
    return bmp;
}
uint32_t argb(uint8_t a, uint8_t r, uint8_t gg, uint8_t b) { return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(gg) << 8) | b; }

// Thumbnail of a surface with transparency over a checkerboard, fit into w x h.
std::unique_ptr<gp::Bitmap> thumbnail(const Surface& s, int w, int h, bool checkerBg) {
    return makeBitmap(w, h, [&](uint32_t* px, int stride) {
        float sx = static_cast<float>(s.width()) / w, sy = static_cast<float>(s.height()) / h;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                // 2x2 point samples per thumbnail pixel
                int r = 0, gg = 0, b = 0, a = 0;
                for (int k = 0; k < 4; ++k) {
                    int px0 = std::min(s.width() - 1, static_cast<int>((x + 0.25f + 0.5f * (k & 1)) * sx));
                    int py0 = std::min(s.height() - 1, static_cast<int>((y + 0.25f + 0.5f * (k >> 1)) * sy));
                    const Color& c = s.at(px0, py0);
                    r += c.r * c.a; gg += c.g * c.a; b += c.b * c.a; a += c.a;
                }
                Color c = a ? Color::rgba(static_cast<uint8_t>(r / a), static_cast<uint8_t>(gg / a), static_cast<uint8_t>(b / a), static_cast<uint8_t>(a / 4))
                            : Color{};
                if (checkerBg) {
                    uint8_t ch = ((x >> 2) + (y >> 2)) & 1 ? 204 : 255;
                    c = blendPixel(Color::rgba(ch, ch, ch), c, BlendMode::Normal, 1.f);
                }
                px[y * stride + x] = argb(c.a, c.r, c.g, c.b);
            }
    });
}

// ---- dock layout ------------------------------------------------------------------------
bool dockVisible(const Dock& d) {
    for (Pal p : d.tabs) if (palShown(p)) return true;
    return false;
}
bool columnVisible(const std::vector<Dock>& docks) {
    for (const Dock& d : docks) if (dockVisible(d)) return true;
    return false;
}

// Lays out a column's visible docks; splitter i sits between places i and i+1.
std::vector<DockPlace> placeDocks(std::vector<Dock>& docks, const RECT& col, std::vector<RECT>* splitters) {
    std::vector<int> vis;
    for (int i = 0; i < static_cast<int>(docks.size()); ++i) if (dockVisible(docks[i])) vis.push_back(i);
    std::vector<DockPlace> out;
    if (vis.empty()) return out;
    int gap = S(4);
    int flex = -1;
    for (int i : vis) if (docks[static_cast<size_t>(i)].flex) flex = i;
    if (flex < 0) flex = vis.back();
    int fixedTotal = 0;
    for (int i : vis) if (i != flex) fixedTotal += docks[static_cast<size_t>(i)].height;
    fixedTotal += gap * (static_cast<int>(vis.size()) - 1);
    int flexH = std::max(S(60), H(col) - fixedTotal);
    int y = col.top;
    for (size_t k = 0; k < vis.size(); ++k) {
        int i = vis[k];
        int h = i == flex ? flexH : docks[static_cast<size_t>(i)].height;
        RECT r = rc(col.left, y, col.right, std::min<int>(y + h, col.bottom));
        out.push_back({i, r});
        y += h;
        if (k + 1 < vis.size()) {
            if (splitters) splitters->push_back(rc(col.left, y, col.right, y + gap));
            y += gap;
        }
    }
    return out;
}

Layout computeLayout() {
    Layout L{};
    GetClientRect(g.hwnd, &L.client);
    const RECT& c = L.client;
    L.menu = rc(0, 0, c.right, S(24));
    int y = L.menu.bottom;
    L.cmd = rc(0, y, c.right, g.showCommandBar ? y + S(36) : y);
    y = L.cmd.bottom;
    int x0 = 0, x1 = c.right;
    if (palShown(PalTool)) { L.tool = rc(0, y, S(46), c.bottom); x0 = L.tool.right; }
    else L.tool = rc(0, y, 0, c.bottom);
    int gap = S(4);
    if (columnVisible(g.leftDocks)) {
        L.left = rc(x0 + 1, y, x0 + 1 + g.leftW, c.bottom);
        L.leftSplit = rc(L.left.right, y, L.left.right + gap, c.bottom);
        x0 = L.leftSplit.right;
    } else {
        L.left = L.leftSplit = rc(x0, y, x0, c.bottom);
    }
    if (columnVisible(g.rightDocks)) {
        L.right = rc(x1 - g.rightW, y, x1, c.bottom);
        L.rightSplit = rc(L.right.left - gap, y, L.right.left, c.bottom);
        x1 = L.rightSplit.left;
    } else {
        L.right = L.rightSplit = rc(x1, y, x1, c.bottom);
    }
    L.tab = rc(x0, y, x1, y + S(26));
    L.nav = rc(x0, c.bottom - S(26), x1, c.bottom);
    L.view = rc(x0, L.tab.bottom, x1, L.nav.top);
    return L;
}

}  // namespace pu::app::ui
