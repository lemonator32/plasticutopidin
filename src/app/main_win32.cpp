// Plastic Paint: a layer-based raster painting application (Win32).
//
// The window is laid out like Clip Studio Paint's default workspace: menu bar
// and command bar on top; the Tool palette, Sub Tool, Tool Property / Brush
// Size and Color Wheel / Color Slider / Color Set palettes on the left; the
// canvas with its tab and navigation bar in the middle; Navigator and Layer
// palettes on the right.  Keyboard shortcuts follow Clip Studio Paint's
// default English shortcut set.
//
// Drawing: the chrome is drawn with GDI+ into a persistent back buffer.  Each
// paint registers "hits" (rectangles with press/drag/release handlers) that
// the input code dispatches to, so a widget's drawing and behaviour live side
// by side.  The canvas is a DIB section blitted with the view's world
// transform (zoom, rotation, flip).
//
// Input: pen/touch/mouse through the WM_POINTER family (pressure from
// GetPointerPenInfo).
//
// Source layout: ui.h (shared types and declarations), ui_base.cpp, ui_palettes.cpp,
// ui_canvas.cpp, ui_files.cpp, ui_menus.cpp, ui_tools.cpp, ui_commands.cpp; this file holds
// pointer dispatch, the window procedure and wWinMain.
#include "ui.h"

namespace pu::app::ui {

// ---- pointer input dispatch --------------------------------------------------------------------------------------------
int hitAt(POINT p) {
    for (int i = static_cast<int>(g.hits.size()) - 1; i >= 0; --i)
        if (PtInRect(&g.hits[static_cast<size_t>(i)].r, p)) return i;
    return -1;
}

void hideTip() {
    KillTimer(g.hwnd, 1);
    if (g.tipShown) {
        g.tipShown = false;
        invalidate(g.tipRect);
    }
}

void onPress(int x, int y, float pressure, int button) {
    hideTip();
    if (g.edit) commitRename(true);
    POINT pt{x, y};
    Layout L = computeLayout();
    if (PtInRect(&L.view, pt)) {
        canvasPress(x, y, pressure, button);
        return;
    }
    int hi = hitAt(pt);
    if (hi < 0) return;
    Hit h = g.hits[static_cast<size_t>(hi)];
    if (button == 1) {
        if (h.rpress) h.rpress(x, y);
        return;
    }
    if (button != 0) return;
    DWORD now = GetTickCount();
    bool dbl = h.dbl && now - g.lastPressTime <= GetDoubleClickTime() && EqualRect(&h.r, &g.lastPressRect) &&
               std::abs(x - g.lastPressPt.x) <= GetSystemMetrics(SM_CXDOUBLECLK) && std::abs(y - g.lastPressPt.y) <= GetSystemMetrics(SM_CYDOUBLECLK);
    g.lastPressTime = dbl ? 0 : now;
    g.lastPressPt = pt;
    g.lastPressRect = h.r;
    if (h.press) h.press(x, y);
    if (dbl) { h.dbl(); return; }
    if (h.drag || h.release) {
        g.uiCapture = true;
        g.dragFn = h.drag;
        g.releaseFn = h.release;
        SetCapture(g.hwnd);
    }
}

void updateHover(int x, int y) {
    POINT old = g.mouse;
    g.mouse = {x, y};
    g.mouseInside = true;
    // Brush cursor follows the pointer.
    RECT oldCursor = g.cursorRect;
    Layout L = computeLayout();
    if (PtInRect(&L.view, g.mouse) || PtInRect(&L.view, old)) {
        invalidate(oldCursor);
        float r = std::max(1.f, g.state.sub().brush.size * g.state.view.zoom / 2.f);
        invalidate(rc(static_cast<int>(x - r) - 4, static_cast<int>(y - r) - 4, static_cast<int>(x + r) + 5, static_cast<int>(y + r) + 5));
    }
    int hi = PtInRect(&L.view, g.mouse) ? -1 : hitAt(g.mouse);
    RECT hr = hi >= 0 ? g.hits[static_cast<size_t>(hi)].r : RECT{};
    if (!EqualRect(&hr, &g.hoverRect)) {
        invalidate(g.hoverRect);
        invalidate(hr);
        g.hoverRect = hr;
        hideTip();
        g.hoverTip = hi >= 0 ? g.hits[static_cast<size_t>(hi)].tip : std::wstring();
        if (!g.hoverTip.empty()) SetTimer(g.hwnd, 1, 650, nullptr);
    }
}

void onMove(int x, int y, float pressure) {
    if (g.uiCapture) {
        g.mouse = {x, y};
        if (g.dragFn) g.dragFn(x, y);
        return;
    }
    if (g.canvasDrag != CanvasDrag::None) {
        RECT oldCursor = g.cursorRect;
        g.mouse = {x, y};
        canvasMove(x, y, pressure);
        if (g.canvasDrag == CanvasDrag::Paint) {
            invalidate(oldCursor);
            float r = std::max(1.f, g.state.sub().brush.size * g.state.view.zoom / 2.f);
            invalidate(rc(static_cast<int>(x - r) - 4, static_cast<int>(y - r) - 4, static_cast<int>(x + r) + 5, static_cast<int>(y + r) + 5));
        }
        return;
    }
    updateHover(x, y);
}

void onRelease() {
    if (g.uiCapture) {
        g.uiCapture = false;
        ReleaseCapture();
        auto rel = std::move(g.releaseFn);
        g.dragFn = nullptr;
        g.releaseFn = nullptr;
        if (rel) rel();
        invalidateAll();
        return;
    }
    if (g.canvasDrag != CanvasDrag::None) canvasRelease();
}

void onWheel(int x, int y, int delta) {
    POINT p{x, y};
    Layout L = computeLayout();
    if (PtInRect(&L.view, p)) {
        zoomAt(static_cast<float>(x), static_cast<float>(y), g.state.view.zoom * (delta > 0 ? 1.2f : 1.f / 1.2f));
        return;
    }
    int hi = hitAt(p);
    for (int i = hi; i >= 0; --i) {
        Hit& h = g.hits[static_cast<size_t>(i)];
        if (PtInRect(&h.r, p) && h.wheel) { auto fn = h.wheel; fn(delta); return; }
    }
}

// Pens that report pressure use it (0..1024); mouse, touch and pens without
// a pressure sensor draw at full pressure.
float pointerPressure(WPARAM wp) {
    UINT32 id = GET_POINTERID_WPARAM(wp);
    POINTER_INPUT_TYPE type = PT_POINTER;
    if (!GetPointerType(id, &type) || type != PT_PEN) return 1.f;
    POINTER_PEN_INFO pen{};
    if (!GetPointerPenInfo(id, &pen) || !(pen.penMask & PEN_MASK_PRESSURE)) return 1.f;
    return std::clamp(pen.pressure / 1024.f, 0.f, 1.f);
}

LPCWSTR canvasCursor() {
    if (g.canvasDrag == CanvasDrag::Pan) return IDC_SIZEALL;
    switch (g.canvasDrag == CanvasDrag::None ? effectiveKind() : g.dragKind) {
        case SubKind::Hand: return IDC_HAND;
        case SubKind::Rotate: return IDC_SIZEALL;
        case SubKind::ZoomIn: case SubKind::ZoomOut: return IDC_CROSS;
        case SubKind::MoveLayer: return IDC_SIZEALL;
        default: return IDC_CROSS;
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g.hwnd = hwnd;
            EnableMouseInPointer(TRUE);
            DragAcceptFiles(hwnd, TRUE);
            ensureCanvasDib();
            updateComposite(g.state.doc.bounds());
            applyTheme();
            return 0;
        case WM_SIZE:
            if (!g.userView) fitView();
            invalidateAll();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize = {S(900), S(600)};
            return 0;
        }
        case WM_PAINT: onPaint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                POINT p;
                GetCursorPos(&p);
                ScreenToClient(hwnd, &p);
                Layout L = computeLayout();
                LPCWSTR c = IDC_ARROW;
                if (PtInRect(&L.view, p)) c = canvasCursor();
                else if (int hi = hitAt(p); hi >= 0) c = g.hits[static_cast<size_t>(hi)].cursor;
                SetCursor(LoadCursorW(nullptr, c));
                return TRUE;
            }
            break;
        case WM_POINTERDOWN: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            if (g.pointerDown) return 0;  // ignore a second finger/pointer
            g.pointerDown = true;
            g.activePointer = GET_POINTERID_WPARAM(wp);
            int button = IS_POINTER_FIRSTBUTTON_WPARAM(wp) ? 0 : IS_POINTER_SECONDBUTTON_WPARAM(wp) ? 1 : 2;
            g.mouse = p;
            onPress(p.x, p.y, pointerPressure(wp), button);
            if (!g.uiCapture && g.canvasDrag == CanvasDrag::None && g.afterRelease.empty()) g.pointerDown = false;
            return 0;
        }
        case WM_POINTERUPDATE: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            if (g.pointerDown && GET_POINTERID_WPARAM(wp) != g.activePointer) return 0;
            onMove(p.x, p.y, pointerPressure(wp));
            return 0;
        }
        // WM_POINTERCAPTURECHANGED is not treated as an up: our own SetCapture
        // in the press handler raises it.  Losing capture to another window
        // arrives as WM_CAPTURECHANGED below.
        case WM_POINTERUP:
            if (g.pointerDown && GET_POINTERID_WPARAM(wp) == g.activePointer) {
                g.pointerDown = false;
                onRelease();
                flushAfterRelease();
            }
            return 0;
        case WM_POINTERLEAVE:
            if (!g.pointerDown) {
                g.mouseInside = false;
                invalidate(g.cursorRect);
                invalidate(g.hoverRect);
                g.hoverRect = RECT{};
                hideTip();
            }
            return 0;
        case WM_POINTERWHEEL:
        case WM_MOUSEWHEEL: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            onWheel(p.x, p.y, GET_WHEEL_DELTA_WPARAM(wp));
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lp) != hwnd) {
                g.pointerDown = false;
                g.afterRelease.clear();
                if (g.canvasDrag == CanvasDrag::Paint) canvasRelease();
                g.canvasDrag = CanvasDrag::None;
                if (g.uiCapture) {
                    g.uiCapture = false;
                    auto rel = std::move(g.releaseFn);
                    g.dragFn = nullptr;
                    if (rel) rel();
                }
            }
            return 0;
        case WM_TIMER:
            if (wp == 2) {  // marching ants
                if (g.state.hasSelection() || g.canvasDrag == CanvasDrag::Select) {
                    g.antsPhase = (g.antsPhase + 1) % 8;
                    invalidate(computeLayout().view);
                }
                return 0;
            }
            if (wp == 1) {
                KillTimer(hwnd, 1);
                if (!g.hoverTip.empty() && g.mouseInside) {
                    g.tipShown = true;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (msg == WM_SYSKEYDOWN && wp == VK_F4) break;
            if (wp == VK_MENU || wp == VK_CONTROL || wp == VK_SHIFT) { invalidate(g.cursorRect); return 0; }
            if (onKey(wp, (lp & (1 << 30)) != 0)) return 0;
            if (msg == WM_SYSKEYDOWN) return 0;
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (wp == VK_SPACE) g.spaceDown = false;
            invalidate(g.cursorRect);
            if (msg == WM_SYSKEYUP) return 0;
            break;
        case WM_SYSCHAR: return 0;  // no beep for Alt shortcuts
        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == SC_KEYMENU) {
                if (lp == 0) defer([] { openMenu(0); });  // F10 / Alt tap
                return 0;
            }
            break;
        case WM_CTLCOLOREDIT:
            SetTextColor(reinterpret_cast<HDC>(wp), T().text);
            SetBkColor(reinterpret_cast<HDC>(wp), T().field);
            return reinterpret_cast<LRESULT>(g.editBrush);
        case WM_APP + 1: {
            auto work = std::move(g.deferred);
            g.deferred.clear();
            for (auto& fn : work) fn();
            return 0;
        }
        case WM_DROPFILES: {
            wchar_t path[MAX_PATH];
            if (DragQueryFileW(reinterpret_cast<HDROP>(wp), 0, path, MAX_PATH) && confirmDiscard()) doOpen(narrow(path));
            DragFinish(reinterpret_cast<HDROP>(wp));
            return 0;
        }
        case WM_CLOSE:
            if (!confirmDiscard()) return 0;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void initUi(HWND hwnd) {
    SetTimer(hwnd, 2, 120, nullptr);  // marching ants
    UINT dpi = GetDpiForWindow(hwnd);
    g.scale = dpi ? dpi / 96.f : 1.f;
    g.family = std::make_unique<gp::FontFamily>(L"Segoe UI");
    g.font = std::make_unique<gp::Font>(g.family.get(), static_cast<float>(S(12)), gp::FontStyleRegular, gp::UnitPixel);
    g.fontBold = std::make_unique<gp::Font>(g.family.get(), static_cast<float>(S(12)), gp::FontStyleBold, gp::UnitPixel);
    g.fontSmall = std::make_unique<gp::Font>(g.family.get(), static_cast<float>(S(11)), gp::FontStyleRegular, gp::UnitPixel);
    g.editFont = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g.leftW = S(250);
    g.rightW = S(280);
    for (bool& v : g.palVisible) v = true;
    g.leftDocks = {Dock{{PalSubTool}, 0, S(196), false}, Dock{{PalToolProperty, PalBrushSize}, 0, 0, true},
                   Dock{{PalColorWheel, PalColorSlider, PalColorSet, PalColorHistory}, 0, S(300), false}};
    g.rightDocks = {Dock{{PalNavigator, PalLayerProperty, PalHistory}, 0, S(250), false}, Dock{{PalLayer}, 0, 0, true}};
    // A standard color set: greys, then light/pure/dark hues, then skin tones.
    for (int i = 0; i < 10; ++i) {
        uint8_t v = static_cast<uint8_t>(i * 255 / 9);
        g.colorSet.push_back(Color::rgba(v, v, v));
    }
    for (int row = 0; row < 3; ++row)
        for (int i = 0; i < 12; ++i) {
            float s = row == 0 ? 0.4f : 1.f, v = row == 2 ? 0.55f : 1.f;
            g.colorSet.push_back(hsvToRgb(i / 12.f, s, v));
        }
    for (Color c : {Color::rgba(255, 228, 206), Color::rgba(250, 206, 178), Color::rgba(232, 176, 140), Color::rgba(196, 136, 100),
                    Color::rgba(140, 92, 64), Color::rgba(92, 60, 44)})
        g.colorSet.push_back(c);
    syncHsv();
    buildMenus();
}

}  // namespace pu::app::ui

using namespace pu::app::ui;

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    gp::GdiplusStartupInput gsi;
    gp::GdiplusStartup(&g.gdipToken, &gsi, nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"PlasticPaintWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Plastic Paint", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1600, 1000, nullptr,
                                nullptr, inst, nullptr);
    initUi(hwnd);
    // Size the window for the DPI now that the scale is known.
    SetWindowPos(hwnd, nullptr, 0, 0, S(1600), S(1000), SWP_NOMOVE | SWP_NOZORDER);
    ShowWindow(hwnd, show);
    fitView();
    setTitle();
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
    // Release GDI+ objects before shutting GDI+ down.
    g.font.reset(); g.fontBold.reset(); g.fontSmall.reset(); g.family.reset();
    g.thumbs.clear(); g.navThumb.reset(); g.wheelRing.reset(); g.svSquare.reset(); g.previews.clear();
    gp::GdiplusShutdown(g.gdipToken);
    return 0;
}
