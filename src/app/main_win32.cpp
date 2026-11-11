// Plastic Paint: a small layer-based raster painting application (Win32).
//
// Input: pen/touch/mouse through the WM_POINTER family (pressure from
// GetPointerPenInfo), with a mouse fallback.  Output: the composited canvas
// is kept in a BGRA back buffer and blitted with StretchDIBits.
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "app_state.h"

using namespace pu;
using namespace pu::app;

namespace {

constexpr int kPanelWidth = 260;
constexpr int kStatusHeight = 24;

struct App {
    HWND hwnd = nullptr;
    AppState state;
    Surface composite;              // canvas-sized RGBA composite
    std::vector<uint8_t> bgra;      // canvas-sized BGRA for GDI
    bool painting = false;
    bool panning = false;
    bool spaceDown = false;
    bool userView = false;          // set once the user zooms or pans; stops auto-fit on resize
    POINT lastMouse{};
    bool usingPointerApi = false;
    UINT32 activePointer = 0;
    std::vector<Color> palette = {Color::rgba(0, 0, 0),      Color::rgba(255, 255, 255), Color::rgba(230, 40, 40),
                                  Color::rgba(255, 150, 40), Color::rgba(250, 220, 60),  Color::rgba(60, 180, 80),
                                  Color::rgba(50, 120, 230), Color::rgba(150, 70, 200),  Color::rgba(240, 130, 180),
                                  Color::rgba(120, 85, 60),  Color::rgba(128, 128, 128), Color::rgba(200, 200, 200)};
} g;

// ---- composite / presentation ---------------------------------------------
void rebuildBuffers() {
    g.composite.reset(g.state.doc.width(), g.state.doc.height());
    g.bgra.assign(static_cast<size_t>(g.state.doc.width()) * g.state.doc.height() * 4, 0);
}

void updateComposite(Rect r) {
    r = r.intersect(g.state.doc.bounds());
    if (r.empty()) return;
    if (g.composite.width() != g.state.doc.width() || g.composite.height() != g.state.doc.height()) rebuildBuffers();
    g.state.doc.composite(g.composite, r);
    int w = g.composite.width();
    for (int y = r.y; y < r.bottom(); ++y) {
        const Color* src = g.composite.row(y) + r.x;
        uint8_t* dst = g.bgra.data() + (static_cast<size_t>(y) * w + r.x) * 4;
        for (int x = 0; x < r.w; ++x) {
            // Show transparency against a light checkerboard.
            Color c = src[x];
            if (c.a < 255) {
                uint8_t check = (((r.x + x) >> 3) + (y >> 3)) & 1 ? 204 : 240;
                c = blendPixel(Color::rgba(check, check, check, 255), c, BlendMode::Normal, 1.f);
            }
            dst[x * 4 + 0] = c.b;
            dst[x * 4 + 1] = c.g;
            dst[x * 4 + 2] = c.r;
            dst[x * 4 + 3] = 255;
        }
    }
}

RECT canvasRectToWindow(Rect r) {
    const View& v = g.state.view;
    RECT out;
    out.left = static_cast<LONG>(std::floor(r.x * v.zoom + v.panX)) - 1;
    out.top = static_cast<LONG>(std::floor(r.y * v.zoom + v.panY)) - 1;
    out.right = static_cast<LONG>(std::ceil(r.right() * v.zoom + v.panX)) + 1;
    out.bottom = static_cast<LONG>(std::ceil(r.bottom() * v.zoom + v.panY)) + 1;
    return out;
}

void invalidateCanvas(Rect r) {
    RECT wr = canvasRectToWindow(r);
    InvalidateRect(g.hwnd, &wr, FALSE);
}

void invalidateAll() { InvalidateRect(g.hwnd, nullptr, FALSE); }

void fitView() {
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    int areaW = rc.right - kPanelWidth, areaH = rc.bottom - kStatusHeight;
    float zx = static_cast<float>(areaW - 40) / g.state.doc.width();
    float zy = static_cast<float>(areaH - 40) / g.state.doc.height();
    View& v = g.state.view;
    v.zoom = std::clamp(std::min(zx, zy), 0.05f, 8.f);
    v.panX = kPanelWidth + (areaW - g.state.doc.width() * v.zoom) / 2;
    v.panY = (areaH - g.state.doc.height() * v.zoom) / 2;
}

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}
std::string narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

void drawText(HDC dc, int x, int y, const std::string& s, COLORREF color = RGB(230, 230, 230)) {
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    std::wstring w = widen(s);
    TextOutW(dc, x, y, w.c_str(), static_cast<int>(w.size()));
}

void paintPanel(HDC dc, const RECT& rc) {
    RECT panel{0, 0, kPanelWidth, rc.bottom - kStatusHeight};
    HBRUSH bg = CreateSolidBrush(RGB(45, 45, 48));
    FillRect(dc, &panel, bg);
    DeleteObject(bg);

    int y = 10;
    const BrushSettings& b = g.state.brush;
    drawText(dc, 10, y, g.state.tool == Tool::Brush ? "Tool: Brush (B)   Eraser (E)" : "Tool: Eraser (E)   Brush (B)"); y += 20;
    char buf[160];
    std::snprintf(buf, sizeof buf, "Size %.0f px  [ ]     Hardness %.0f%%  { }", b.size, b.hardness * 100); drawText(dc, 10, y, buf); y += 18;
    std::snprintf(buf, sizeof buf, "Opacity %.0f%%  , .    Zoom %.0f%%", b.opacity * 100, g.state.view.zoom * 100); drawText(dc, 10, y, buf); y += 24;

    // Colour swatches
    drawText(dc, 10, y, "Colour (click)  current:");
    HBRUSH cur = CreateSolidBrush(RGB(b.color.r, b.color.g, b.color.b));
    RECT crc{180, y, 220, y + 16};
    FillRect(dc, &crc, cur);
    DeleteObject(cur);
    y += 22;
    for (size_t i = 0; i < g.palette.size(); ++i) {
        int sx = 10 + static_cast<int>(i % 6) * 40, sy = y + static_cast<int>(i / 6) * 26;
        HBRUSH hb = CreateSolidBrush(RGB(g.palette[i].r, g.palette[i].g, g.palette[i].b));
        RECT src{sx, sy, sx + 34, sy + 22};
        FillRect(dc, &src, hb);
        DeleteObject(hb);
    }
    y += 2 * 26 + 10;

    drawText(dc, 10, y, "Layers  (PgUp/PgDn select, N new, Del delete)"); y += 18;
    drawText(dc, 10, y, "V show/hide  M blend  + - opacity  C clip  Ctrl+E merge", RGB(170, 170, 170)); y += 22;
    const Document& doc = g.state.doc;
    for (int i = static_cast<int>(doc.layerCount()) - 1; i >= 0; --i) {
        const Layer& l = *doc.layer(static_cast<size_t>(i));
        bool active = static_cast<size_t>(i) == doc.activeIndex();
        RECT row{4, y - 2, kPanelWidth - 4, y + 18};
        if (active) {
            HBRUSH hb = CreateSolidBrush(RGB(70, 90, 130));
            FillRect(dc, &row, hb);
            DeleteObject(hb);
        }
        std::snprintf(buf, sizeof buf, "%s %s%s", l.visible ? "[x]" : "[ ]", l.clipToBelow ? "^ " : "", l.name.c_str());
        drawText(dc, 10, y, buf, l.visible ? RGB(235, 235, 235) : RGB(140, 140, 140));
        std::snprintf(buf, sizeof buf, "%s %d%%", blendModeName(l.mode), static_cast<int>(l.opacity * 100 + 0.5f));
        drawText(dc, 150, y, buf, RGB(170, 170, 170));
        y += 20;
    }
    y += 10;
    drawText(dc, 10, y, "Ctrl+N new  Ctrl+O open  Ctrl+S save", RGB(170, 170, 170)); y += 18;
    drawText(dc, 10, y, "Ctrl+Z undo  Ctrl+Y redo  Space+drag pan", RGB(170, 170, 170)); y += 18;
    drawText(dc, 10, y, "Wheel zoom  F fit  Ctrl+Del clear layer", RGB(170, 170, 170)); y += 18;
    drawText(dc, 10, y, "Opens/saves .clip and .png", RGB(170, 170, 170));
}

void paintCanvas(HDC dc, const RECT& rc) {
    RECT area{kPanelWidth, 0, rc.right, rc.bottom - kStatusHeight};
    HBRUSH bg = CreateSolidBrush(RGB(30, 30, 32));
    FillRect(dc, &area, bg);
    DeleteObject(bg);
    const View& v = g.state.view;
    int w = g.state.doc.width(), h = g.state.doc.height();
    int dx = static_cast<int>(std::floor(v.panX)), dy = static_cast<int>(std::floor(v.panY));
    int dw = static_cast<int>(std::ceil(w * v.zoom)), dh = static_cast<int>(std::ceil(h * v.zoom));
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    SetStretchBltMode(dc, v.zoom < 1.f ? HALFTONE : COLORONCOLOR);
    StretchDIBits(dc, dx, dy, dw, dh, 0, 0, w, h, g.bgra.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    SelectClipRgn(dc, nullptr);
}

void paintStatus(HDC dc, const RECT& rc) {
    RECT bar{0, rc.bottom - kStatusHeight, rc.right, rc.bottom};
    HBRUSH bg = CreateSolidBrush(RGB(60, 60, 64));
    FillRect(dc, &bar, bg);
    DeleteObject(bg);
    std::string s = g.state.status;
    if (g.state.undo && g.state.undo->canUndo()) s += "   |  undo: " + std::string(g.state.undo->undoName());
    if (g.state.dirty) s += "   |  modified";
    drawText(dc, 8, bar.top + 4, s);
}

void onPaint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(g.hwnd, &ps);
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    // Double-buffer the whole client area to avoid flicker.
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    SelectObject(mem, GetStockObject(DEFAULT_GUI_FONT));
    paintCanvas(mem, rc);
    paintPanel(mem, rc);
    paintStatus(mem, rc);
    BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top, mem,
           ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(g.hwnd, &ps);
}

// ---- input --------------------------------------------------------------------
void setTitle() {
    std::string t = "Plastic Paint - " + (g.state.filePath.empty() ? std::string("untitled") : g.state.filePath) + (g.state.dirty ? " *" : "");
    SetWindowTextW(g.hwnd, widen(t).c_str());
}

bool inCanvasArea(int x, int y) {
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    return x >= kPanelWidth && y < rc.bottom - kStatusHeight;
}

void panelClick(int x, int y) {
    // Colour swatches start at y = 10+20+18+24+22 = 94
    int swY = 94;
    for (size_t i = 0; i < g.palette.size(); ++i) {
        int sx = 10 + static_cast<int>(i % 6) * 40, sy = swY + static_cast<int>(i / 6) * 26;
        if (x >= sx && x < sx + 34 && y >= sy && y < sy + 22) {
            g.state.brush.color = g.palette[i];
            invalidateAll();
            return;
        }
    }
    // Layer rows start at swY + 62 + 40 = 196
    int rowY = swY + 2 * 26 + 10 + 18 + 22;
    int n = static_cast<int>(g.state.doc.layerCount());
    if (y >= rowY - 2 && y < rowY - 2 + n * 20) {
        int row = (y - (rowY - 2)) / 20;
        size_t idx = static_cast<size_t>(n - 1 - row);
        if (x < 40) {  // click on the [x] box toggles visibility
            g.state.doc.setActiveIndex(idx);
            g.state.toggleVisible();
            updateComposite(g.state.doc.bounds());
        } else {
            g.state.doc.setActiveIndex(idx);
        }
        invalidateAll();
    }
}

void beginInput(int x, int y, float pressure, bool pan) {
    if (!inCanvasArea(x, y)) { panelClick(x, y); return; }
    g.lastMouse = {x, y};
    if (pan || g.spaceDown) { g.panning = true; SetCapture(g.hwnd); return; }
    float cx, cy;
    g.state.view.canvasFromWindow(static_cast<float>(x), static_cast<float>(y), cx, cy);
    g.state.beginStroke(cx, cy, pressure);
    g.painting = g.state.strokeActive();
    if (g.painting) {
        SetCapture(g.hwnd);
        Rect d = g.state.strokeDirty();
        updateComposite(d);
        invalidateCanvas(d);
    } else {
        invalidateAll();
    }
}

void moveInput(int x, int y, float pressure) {
    if (g.panning) {
        g.userView = true;
        g.state.view.panX += static_cast<float>(x - g.lastMouse.x);
        g.state.view.panY += static_cast<float>(y - g.lastMouse.y);
        g.lastMouse = {x, y};
        invalidateAll();
        return;
    }
    if (!g.painting) return;
    float cx, cy;
    g.state.view.canvasFromWindow(static_cast<float>(x), static_cast<float>(y), cx, cy);
    g.state.continueStroke(cx, cy, pressure);
    Rect d = g.state.strokeDirty();
    if (!d.empty()) {
        updateComposite(d);
        invalidateCanvas(d);
    }
}

void endInput() {
    if (g.panning) { g.panning = false; ReleaseCapture(); return; }
    if (!g.painting) return;
    g.painting = false;
    ReleaseCapture();
    Rect r = g.state.endStroke();
    updateComposite(r);
    invalidateAll();
    setTitle();
}

void zoomAt(int x, int y, float factor) {
    g.userView = true;
    View& v = g.state.view;
    float cx, cy;
    v.canvasFromWindow(static_cast<float>(x), static_cast<float>(y), cx, cy);
    v.zoom = std::clamp(v.zoom * factor, 0.05f, 16.f);
    v.panX = x - cx * v.zoom;
    v.panY = y - cy * v.zoom;
    invalidateAll();
}

std::string fileDialog(bool save) {
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"Images (*.clip;*.png)\0*.clip;*.png\0CLIP STUDIO FORMAT (*.clip)\0*.clip\0PNG (*.png)\0*.png\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST) : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    ofn.lpstrDefExt = L"clip";
    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? narrow(buf) : std::string();
}

void doOpen(const std::string& path) {
    std::string err;
    if (!g.state.openFile(path, &err)) {
        MessageBoxW(g.hwnd, widen(err).c_str(), L"Open failed", MB_ICONERROR);
        return;
    }
    rebuildBuffers();
    updateComposite(g.state.doc.bounds());
    g.userView = false;
    fitView();
    setTitle();
    invalidateAll();
}

void doSave(bool forcePrompt) {
    std::string path = g.state.filePath;
    if (forcePrompt || path.empty()) path = fileDialog(true);
    if (path.empty()) return;
    std::string err;
    if (!g.state.saveFile(path, &err)) MessageBoxW(g.hwnd, widen(err).c_str(), L"Save failed", MB_ICONERROR);
    setTitle();
    invalidateAll();
}

void doNew() {
    g.state.newDocument(1200, 900);
    rebuildBuffers();
    updateComposite(g.state.doc.bounds());
    fitView();
    setTitle();
    invalidateAll();
}

void onKey(WPARAM key) {
    bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    AppState& s = g.state;
    bool recomposite = false;
    switch (key) {
        case 'B': s.tool = Tool::Brush; break;
        case 'E': if (ctrl) { s.mergeDown(); recomposite = true; } else s.tool = Tool::Eraser; break;
        case VK_OEM_4: /* [ */ s.brush.size = std::max(1.f, s.brush.size * (shift ? 0.5f : 0.8f)); break;
        case VK_OEM_6: /* ] */ s.brush.size = std::min(1000.f, s.brush.size * (shift ? 2.f : 1.25f)); break;
        case VK_OEM_COMMA: s.brush.opacity = std::max(0.05f, s.brush.opacity - 0.1f); break;
        case VK_OEM_PERIOD: s.brush.opacity = std::min(1.f, s.brush.opacity + 0.1f); break;
        case 'Z': if (ctrl) { if (shift) s.undo->redo(); else s.undo->undo(); s.dirty = true; recomposite = true; } break;
        case 'Y': if (ctrl) { s.undo->redo(); s.dirty = true; recomposite = true; } break;
        case 'N': if (ctrl) { doNew(); return; } s.addLayer(); recomposite = true; break;
        case 'O': if (ctrl) { std::string p = fileDialog(false); if (!p.empty()) doOpen(p); return; } break;
        case 'S': if (ctrl) { doSave(shift); return; } break;
        case VK_DELETE: if (ctrl) s.clearLayer(); else s.deleteLayer(); recomposite = true; break;
        case VK_PRIOR: s.selectLayer(+1); break;
        case VK_NEXT: s.selectLayer(-1); break;
        case VK_UP: if (ctrl) { s.moveLayer(+1); recomposite = true; } break;
        case VK_DOWN: if (ctrl) { s.moveLayer(-1); recomposite = true; } break;
        case 'V': s.toggleVisible(); recomposite = true; break;
        case 'M': s.cycleBlendMode(shift ? -1 : 1); recomposite = true; break;
        case 'C': s.toggleClip(); recomposite = true; break;
        case 'D': if (ctrl) { s.duplicateLayer(); recomposite = true; } break;
        case VK_OEM_PLUS: case VK_ADD: s.adjustOpacity(+0.1f); recomposite = true; break;
        case VK_OEM_MINUS: case VK_SUBTRACT: s.adjustOpacity(-0.1f); recomposite = true; break;
        case 'F': g.userView = false; fitView(); break;
        case VK_SPACE: g.spaceDown = true; break;
        case 'X': std::swap(s.brush.color, g.palette[1]); break;
        default: return;
    }
    if (recomposite) updateComposite(s.doc.bounds());
    setTitle();
    invalidateAll();
}

float pointerPressure(WPARAM wp) {
    UINT32 id = GET_POINTERID_WPARAM(wp);
    POINTER_PEN_INFO pen{};
    if (GetPointerPenInfo(id, &pen)) return pen.pressure ? pen.pressure / 1024.f : 1.f;
    return 1.f;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g.hwnd = hwnd;
            EnableMouseInPointer(TRUE);
            DragAcceptFiles(hwnd, TRUE);
            rebuildBuffers();
            updateComposite(g.state.doc.bounds());
            return 0;
        case WM_SIZE:
            if (!g.userView) fitView();
            invalidateAll();
            return 0;
        case WM_PAINT: onPaint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_POINTERDOWN: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            g.usingPointerApi = true;
            g.activePointer = GET_POINTERID_WPARAM(wp);
            bool pan = IS_POINTER_SECONDBUTTON_WPARAM(wp) || IS_POINTER_THIRDBUTTON_WPARAM(wp);
            beginInput(p.x, p.y, pointerPressure(wp), pan);
            return 0;
        }
        case WM_POINTERUPDATE: {
            if (GET_POINTERID_WPARAM(wp) != g.activePointer) return 0;
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            moveInput(p.x, p.y, pointerPressure(wp));
            return 0;
        }
        case WM_POINTERUP:
        case WM_POINTERCAPTURECHANGED:
            if (GET_POINTERID_WPARAM(wp) == g.activePointer) endInput();
            return 0;
        case WM_LBUTTONDOWN:
            if (!g.usingPointerApi) beginInput(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 1.f, false);
            return 0;
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
            if (!g.usingPointerApi) beginInput(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 1.f, true);
            return 0;
        case WM_MOUSEMOVE:
            if (!g.usingPointerApi) moveInput(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 1.f);
            return 0;
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
            if (!g.usingPointerApi) endInput();
            return 0;
        case WM_MOUSEWHEEL: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            zoomAt(p.x, p.y, GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1.2f : 1.f / 1.2f);
            return 0;
        }
        case WM_KEYDOWN: onKey(wp); return 0;
        case WM_KEYUP: if (wp == VK_SPACE) g.spaceDown = false; return 0;
        case WM_DROPFILES: {
            wchar_t path[MAX_PATH];
            if (DragQueryFileW(reinterpret_cast<HDROP>(wp), 0, path, MAX_PATH)) doOpen(narrow(path));
            DragFinish(reinterpret_cast<HDROP>(wp));
            return 0;
        }
        case WM_CLOSE:
            if (g.state.dirty && MessageBoxW(hwnd, L"Discard unsaved changes?", L"Plastic Paint", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int show) {
    SetProcessDPIAware();
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"PlasticPaintWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Plastic Paint", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1500, 1000,
                              nullptr, nullptr, inst, nullptr);
    ShowWindow(hwnd, show);
    fitView();
    setTitle();
    // Open the first argument as a file (UTF-16 from the OS, converted to UTF-8
    // for the loaders).  Further arguments are ignored.
    if (cmdLine && *cmdLine) {
        int argc = 0;
        if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
            if (argc > 1 && argv[1][0]) doOpen(narrow(argv[1]));
            LocalFree(argv);
        }
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
