// The owner-drawn menu bar's popup menus.
#include "ui.h"

namespace pu::app::ui {

// ---- menus ---------------------------------------------------------------------------------------------------------
void buildMenus() {
    auto add = [](HMENU m, int id, const wchar_t* s) { AppendMenuW(m, MF_STRING, static_cast<UINT_PTR>(id), s); };
    auto sep = [](HMENU m) { AppendMenuW(m, MF_SEPARATOR, 0, nullptr); };
    HMENU f = CreatePopupMenu();
    add(f, CmdNew, L"New...\tCtrl+N");
    add(f, CmdOpen, L"Open...\tCtrl+O");
    add(f, CmdClose, L"Close\tCtrl+W");
    sep(f);
    add(f, CmdSave, L"Save\tCtrl+S");
    add(f, CmdSaveAs, L"Save as...\tCtrl+Shift+S");
    HMENU ex = CreatePopupMenu();
    add(ex, CmdExportPng, L".png (PNG)...");
    AppendMenuW(f, MF_POPUP, reinterpret_cast<UINT_PTR>(ex), L"Export (single layer)");
    sep(f);
    add(f, CmdExit, L"Exit\tCtrl+Q");
    g.menus[MenuFile] = f;

    HMENU e = CreatePopupMenu();
    add(e, CmdUndo, L"Undo\tCtrl+Z");
    add(e, CmdRedo, L"Redo\tCtrl+Y");
    sep(e);
    add(e, CmdClear, L"Clear\tDelete");
    add(e, CmdClearOutside, L"Clear outside selection\tCtrl+Delete");
    add(e, CmdFill, L"Fill");
    sep(e);
    HMENU tone = CreatePopupMenu();
    add(tone, CmdInvert, L"Reverse gradient\tCtrl+I");
    AppendMenuW(e, MF_POPUP, reinterpret_cast<UINT_PTR>(tone), L"Tonal correction");
    HMENU tr = CreatePopupMenu();
    add(tr, CmdFlipLayerH, L"Flip horizontal");
    add(tr, CmdFlipLayerV, L"Flip vertical");
    AppendMenuW(e, MF_POPUP, reinterpret_cast<UINT_PTR>(tr), L"Transform");
    g.menus[MenuEdit] = e;

    HMENU l = CreatePopupMenu();
    add(l, CmdLayerNew, L"New raster layer\tCtrl+Shift+N");
    add(l, CmdLayerDup, L"Duplicate layer");
    add(l, CmdLayerDelete, L"Delete layer");
    sep(l);
    add(l, CmdLayerRename, L"Rename layer");
    add(l, CmdLayerProperty, L"Layer property...");
    add(l, CmdLayerClip, L"Clip at layer below\tCtrl+Alt+G");
    add(l, CmdLayerLock, L"Lock layer");
    add(l, CmdLayerLockAlpha, L"Lock transparent pixels");
    sep(l);
    add(l, CmdLayerMerge, L"Merge with layer below\tCtrl+E");
    add(l, CmdLayerMergeVisible, L"Merge visible layers\tCtrl+Shift+E");
    sep(l);
    add(l, CmdLayerSelectUp, L"Select upper layer\tAlt+]");
    add(l, CmdLayerSelectDown, L"Select lower layer\tAlt+[");
    add(l, CmdLayerMoveUp, L"Move layer up");
    add(l, CmdLayerMoveDown, L"Move layer down");
    g.menus[MenuLayer] = l;

    HMENU sl = CreatePopupMenu();
    add(sl, CmdSelectAll, L"Select all\tCtrl+A");
    add(sl, CmdDeselect, L"Deselect\tCtrl+D");
    add(sl, CmdInvertSelection, L"Invert selected area\tCtrl+Shift+I");
    g.menus[MenuSelect] = sl;

    HMENU fl = CreatePopupMenu();
    HMENU blur = CreatePopupMenu();
    add(blur, CmdFilterBlur, L"Blur");
    add(blur, CmdFilterBlurStrong, L"Blur (strong)");
    AppendMenuW(fl, MF_POPUP, reinterpret_cast<UINT_PTR>(blur), L"Blur");
    g.menus[MenuFilter] = fl;

    HMENU v = CreatePopupMenu();
    HMENU rot = CreatePopupMenu();
    add(rot, CmdRotateLeft, L"Rotate left\t-");
    add(rot, CmdRotateRight, L"Rotate right\t^");
    add(rot, CmdFlipView, L"Flip horizontal");
    add(rot, CmdResetView, L"Reset rotation\tCtrl+@");
    AppendMenuW(v, MF_POPUP, reinterpret_cast<UINT_PTR>(rot), L"Rotate/Invert");
    sep(v);
    add(v, CmdZoomIn, L"Zoom in\tCtrl++");
    add(v, CmdZoomOut, L"Zoom out\tCtrl+-");
    add(v, CmdZoom100, L"100%\tCtrl+Alt+0");
    add(v, CmdZoomFit, L"Fit to navigator\tCtrl+0");
    sep(v);
    add(v, CmdPaper, L"Paper");
    g.menus[MenuView] = v;

    HMENU w = CreatePopupMenu();
    for (int p = 0; p < PalCount; ++p) add(w, static_cast<int>(CmdPalBase) + p, kPalNames[p]);
    sep(w);
    add(w, CmdCommandBar, L"Command bar");
    add(w, CmdHideAll, L"Hide all palettes\tTab");
    sep(w);
    HMENU theme = CreatePopupMenu();
    add(theme, CmdThemeDark, L"Dark");
    add(theme, CmdThemeLight, L"Light");
    AppendMenuW(w, MF_POPUP, reinterpret_cast<UINT_PTR>(theme), L"Color theme");
    g.menus[MenuWindow] = w;

    HMENU h = CreatePopupMenu();
    add(h, CmdShortcuts, L"Shortcut list\tF1");
    add(h, CmdAbout, L"About Plastic Paint");
    g.menus[MenuHelp] = h;
}

void updateMenuStates() {
    auto check = [](HMENU m, int id, bool on) { CheckMenuItem(m, static_cast<UINT>(id), MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
    auto enable = [](HMENU m, int id, bool on) { EnableMenuItem(m, static_cast<UINT>(id), MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED)); };
    Layer* l = g.state.active();
    enable(g.menus[MenuEdit], CmdUndo, g.state.undo->canUndo());
    enable(g.menus[MenuEdit], CmdRedo, g.state.undo->canRedo());
    check(g.menus[MenuLayer], CmdLayerClip, l && l->clipToBelow);
    check(g.menus[MenuLayer], CmdLayerLock, l && l->locked);
    check(g.menus[MenuLayer], CmdLayerLockAlpha, l && l->lockAlpha);
    enable(g.menus[MenuLayer], CmdLayerMerge, g.state.doc.activeIndex() > 0);
    enable(g.menus[MenuLayer], CmdLayerDelete, g.state.doc.layerCount() > 1);
    check(g.menus[MenuView], CmdFlipView, g.state.view.flipH);
    check(g.menus[MenuView], CmdPaper, g.state.doc.paperVisible);
    for (int p = 0; p < PalCount; ++p) check(g.menus[MenuWindow], CmdPalBase + p, g.palVisible[p]);
    check(g.menus[MenuWindow], CmdHideAll, g.hideAll);
    check(g.menus[MenuWindow], CmdCommandBar, g.showCommandBar);
    check(g.menus[MenuWindow], CmdThemeDark, g.dark);
    check(g.menus[MenuWindow], CmdThemeLight, !g.dark);
}

int menuTitleAt(POINT p) {
    for (int i = 0; i < static_cast<int>(g.menuTitles.size()); ++i)
        if (PtInRect(&g.menuTitles[static_cast<size_t>(i)], p)) return i;
    return -1;
}

// While a menu is open, moving over another title switches to that menu.
LRESULT CALLBACK menuHookProc(int code, WPARAM wp, LPARAM lp) {
    if (code == MSGF_MENU) {
        MSG* m = reinterpret_cast<MSG*>(lp);
        if (m->message == WM_MOUSEMOVE) {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(g.hwnd, &p);
            int i = menuTitleAt(p);
            if (i >= 0 && i != g.menuOpen) {
                g.menuNext = i;
                EndMenu();
            }
        }
    }
    return CallNextHookEx(g.menuHook, code, wp, lp);
}

void openMenu(int index) {
    // Menus run their own modal loop; a pointer capture from the click that
    // opened them would swallow the popup's mouse input.
    if (g.uiCapture) {
        g.uiCapture = false;
        g.dragFn = nullptr;
        g.releaseFn = nullptr;
    }
    g.pointerDown = false;
    ReleaseCapture();
    while (index >= 0 && index < kMenuCount) {
        if (g.menuTitles.size() != static_cast<size_t>(kMenuCount)) break;
        g.menuOpen = index;
        g.menuNext = -1;
        updateMenuStates();
        invalidate(computeLayout().menu);
        UpdateWindow(g.hwnd);
        RECT t = g.menuTitles[static_cast<size_t>(index)];
        POINT p{t.left, t.bottom};
        ClientToScreen(g.hwnd, &p);
        g.menuHook = SetWindowsHookExW(WH_MSGFILTER, menuHookProc, nullptr, GetCurrentThreadId());
        // The window must be foreground or the popup does not show/dismiss properly.
        SetForegroundWindow(g.hwnd);
        int cmd = TrackPopupMenuEx(g.menus[index], TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NOANIMATION, p.x, p.y, g.hwnd, nullptr);
        UnhookWindowsHookEx(g.menuHook);
        g.menuHook = nullptr;
        g.menuOpen = -1;
        invalidate(computeLayout().menu);
        if (cmd) { exec(cmd); return; }
        index = g.menuNext;
    }
}

}  // namespace pu::app::ui
