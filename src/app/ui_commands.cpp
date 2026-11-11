// Menu/shortcut commands (exec) and keyboard handling.
#include "ui.h"

namespace pu::app::ui {

// ---- commands ------------------------------------------------------------------------------------------------------------
void layerOp(void (AppState::*op)()) {
    (g.state.*op)();
    contentChanged();
}

void showShortcuts() {
    MessageBoxW(g.hwnd,
                L"Tools\n"
                L"  P  Pen / Pencil      B  Brush / Airbrush      E  Eraser\n"
                L"  G  Fill      I  Eyedropper      H  Hand      R  Rotate      /  Zoom\n"
                L"  Pressing a tool key again switches between tools sharing it.\n"
                L"  , .  Previous / next sub tool\n\n"
                L"Temporary tools (hold)\n"
                L"  Space  Hand      Shift+Space  Rotate      Ctrl+Space  Zoom in\n"
                L"  Ctrl+Alt+Space  Zoom out      Alt  Eyedropper\n"
                L"  Ctrl+Alt+drag  Change brush size\n\n"
                L"Brush\n"
                L"  [ ]  Brush size      Ctrl+[ ]  Opacity\n"
                L"  X  Switch main/sub color      C  Transparent color\n\n"
                L"File and edit\n"
                L"  Ctrl+N New   Ctrl+O Open   Ctrl+S Save   Ctrl+Shift+S Save as\n"
                L"  Ctrl+Z Undo   Ctrl+Y / Ctrl+Shift+Z Redo   Delete Clear   Ctrl+I Reverse gradient\n\n"
                L"Layers\n"
                L"  Ctrl+Shift+N New raster layer   Ctrl+E Merge with layer below\n"
                L"  Ctrl+Shift+E Merge visible   Ctrl+Alt+G Clip at layer below\n"
                L"  Alt+] / Alt+[ Select upper / lower layer\n\n"
                L"View\n"
                L"  Ctrl++ / Ctrl+- Zoom   Ctrl+0 Fit   Ctrl+Alt+0 100%\n"
                L"  - / ^ Rotate   Ctrl+@ Reset rotation   Tab Hide palettes",
                L"Shortcut list", MB_OK);
}

void applyTheme() {
    g.th = g.dark ? &kDark : &kLight;
    BOOL dark = g.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(g.hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
    if (g.editBrush) DeleteObject(g.editBrush);
    g.editBrush = CreateSolidBrush(T().field);
    g.previews.clear();
    g.wheelRing.reset();
    invalidateAll();
}

void exec(int cmd) {
    AppState& s = g.state;
    if (cmd >= CmdBlendBase && cmd < CmdBlendBase + static_cast<int>(BlendMode::Count)) {
        s.setBlendMode(static_cast<BlendMode>(cmd - CmdBlendBase));
        contentChanged();
        return;
    }
    if (cmd >= CmdPalBase && cmd < static_cast<int>(CmdPalBase) + static_cast<int>(PalCount)) {
        g.palVisible[cmd - CmdPalBase] = !g.palVisible[cmd - CmdPalBase];
        g.hideAll = false;
        invalidateAll();
        return;
    }
    switch (cmd) {
        case CmdNew: doNew(); break;
        case CmdOpen: {
            if (!confirmDiscard()) break;
            std::string p = fileDialog(false);
            if (!p.empty()) doOpen(p);
            break;
        }
        case CmdClose:
            if (!confirmDiscard()) break;
            s.newDocument(s.doc.width(), s.doc.height(), s.doc.dpi);
            documentReplaced();
            break;
        case CmdSave: doSave(false); break;
        case CmdSaveAs: doSave(true); break;
        case CmdExportPng: {
            std::string p = fileDialog(true, true);
            if (p.empty()) break;
            std::string err;
            Surface flat = s.doc.flatten();
            if (!png::writeFile(p, flat, &err)) MessageBoxW(g.hwnd, widen(err).c_str(), L"Export failed", MB_ICONERROR);
            else s.status = "Exported " + p;
            invalidateAll();
            break;
        }
        case CmdExit: PostMessageW(g.hwnd, WM_CLOSE, 0, 0); break;
        case CmdUndo:
            if (s.undo->canUndo()) {
                std::string n = s.undo->undoName();
                s.undo->undo();
                s.dirty = true;
                s.status = "Undo: " + n;
                contentChanged();
            }
            break;
        case CmdRedo:
            if (s.undo->canRedo()) {
                std::string n = s.undo->redoName();
                s.undo->redo();
                s.dirty = true;
                s.status = "Redo: " + n;
                contentChanged();
            }
            break;
        case CmdClear: layerOp(&AppState::clearLayer); break;
        case CmdClearOutside:
            layerOp(&AppState::clearOutsideSelection);
            break;
        case CmdSelectAll: s.selectAll(); contentChanged(); break;
        case CmdDeselect: s.deselect(); contentChanged(); break;
        case CmdInvertSelection: s.invertSelection(); contentChanged(); break;
        case CmdLayerProperty: showPalette(PalLayerProperty); invalidateAll(); break;
        case CmdFill: s.fillLayer(s.mainColor); contentChanged(); break;
        case CmdInvert: layerOp(&AppState::invertLayer); break;
        case CmdFlipLayerH: s.flipLayer(true); contentChanged(); break;
        case CmdFlipLayerV: s.flipLayer(false); contentChanged(); break;
        case CmdLayerNew: layerOp(&AppState::addLayer); break;
        case CmdLayerDup: layerOp(&AppState::duplicateLayer); break;
        case CmdLayerDelete: layerOp(&AppState::deleteLayer); break;
        case CmdLayerRename: defer(startRename); break;
        case CmdLayerClip: layerOp(&AppState::toggleClip); break;
        case CmdLayerLock: layerOp(&AppState::toggleLock); break;
        case CmdLayerLockAlpha: layerOp(&AppState::toggleLockAlpha); break;
        case CmdLayerMerge: layerOp(&AppState::mergeDown); break;
        case CmdLayerMergeVisible: layerOp(&AppState::mergeVisible); break;
        case CmdLayerSelectUp: s.selectLayer(+1); invalidateAll(); break;
        case CmdLayerSelectDown: s.selectLayer(-1); invalidateAll(); break;
        case CmdLayerMoveUp: s.moveLayer(+1); contentChanged(); break;
        case CmdLayerMoveDown: s.moveLayer(-1); contentChanged(); break;
        case CmdFilterBlur: {
            HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
            s.blurLayer(2);
            SetCursor(old);
            contentChanged();
            break;
        }
        case CmdFilterBlurStrong: {
            HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
            s.blurLayer(6);
            SetCursor(old);
            contentChanged();
            break;
        }
        case CmdZoomIn: zoomStepCenter(1); break;
        case CmdZoomOut: zoomStepCenter(-1); break;
        case CmdZoom100: {
            POINT c = viewCenter();
            zoomAt(static_cast<float>(c.x), static_cast<float>(c.y), 1.f);
            break;
        }
        case CmdZoomFit: g.userView = false; fitView(); invalidateAll(); break;
        case CmdRotateLeft: setRotation(s.view.rotation - kPi / 12); break;
        case CmdRotateRight: setRotation(s.view.rotation + kPi / 12); break;
        case CmdFlipView: toggleFlipView(); break;
        case CmdResetView: {
            POINT c = viewCenter();
            s.view.setKeeping(static_cast<float>(c.x), static_cast<float>(c.y), s.view.zoom, 0.f, false);
            invalidateAll();
            break;
        }
        case CmdPaper: s.doc.paperVisible = !s.doc.paperVisible; s.dirty = true; contentChanged(); break;
        case CmdHideAll: g.hideAll = !g.hideAll; invalidateAll(); break;
        case CmdCommandBar: g.showCommandBar = !g.showCommandBar; invalidateAll(); break;
        case CmdThemeDark: g.dark = true; applyTheme(); break;
        case CmdThemeLight: g.dark = false; applyTheme(); break;
        case CmdShortcuts: showShortcuts(); break;
        case CmdAbout:
            MessageBoxW(g.hwnd,
                        L"Plastic Paint\n\nA layer-based painting program that opens and saves CLIP STUDIO FORMAT (.clip) files.\n"
                        L"Not affiliated with CELSYS. \"Clip Studio Paint\" is a trademark of CELSYS, Inc.",
                        L"About Plastic Paint", MB_OK | MB_ICONINFORMATION);
            break;
    }
}

// ---- keyboard --------------------------------------------------------------------------------------------------------
// True when virtual key `vk` (with the current Shift state) types `ch` on the active layout.
bool isChar(WPARAM vk, wchar_t ch) {
    SHORT r = VkKeyScanW(ch);
    if (r == -1) return false;
    bool needShift = (HIBYTE(r) & 1) != 0;
    return LOBYTE(r) == vk && needShift == keyDown(VK_SHIFT);
}

void toolKey(std::initializer_list<Tool> tools) {
    // Pressing a tool key again cycles through the tool groups that share it.
    std::vector<int> idx;
    for (Tool t : tools)
        for (int i = 0; i < static_cast<int>(g.state.tools.size()); ++i)
            if (g.state.tools[static_cast<size_t>(i)].tool == t) idx.push_back(i);
    if (idx.empty()) return;
    auto it = std::find(idx.begin(), idx.end(), g.state.toolIndex);
    selectTool(it == idx.end() || it + 1 == idx.end() ? idx.front() : *(it + 1));
}

bool onKey(WPARAM vk, bool repeat) {
    bool ctrl = keyDown(VK_CONTROL), alt = keyDown(VK_MENU), shift = keyDown(VK_SHIFT);
    AppState& s = g.state;
    if (vk == VK_SPACE) {
        if (!repeat) { g.spaceDown = true; invalidate(g.cursorRect); }
        return true;
    }
    if (ctrl && !alt) {
        switch (vk) {
            case 'N': exec(shift ? CmdLayerNew : CmdNew); return true;
            case 'O': exec(CmdOpen); return true;
            case 'W': exec(CmdClose); return true;
            case 'S': exec(shift ? CmdSaveAs : CmdSave); return true;
            case 'Q': exec(CmdExit); return true;
            case 'Z': exec(shift ? CmdRedo : CmdUndo); return true;
            case 'Y': exec(CmdRedo); return true;
            case 'I': exec(shift ? CmdInvertSelection : CmdInvert); return true;
            case 'A': exec(CmdSelectAll); return true;
            case 'D': exec(CmdDeselect); return true;
            case 'E': exec(shift ? CmdLayerMergeVisible : CmdLayerMerge); return true;
            case '0': exec(CmdZoomFit); return true;
            case VK_ADD: exec(CmdZoomIn); return true;
            case VK_SUBTRACT: exec(CmdZoomOut); return true;
            case VK_DELETE: case VK_BACK: exec(CmdClearOutside); return true;
        }
        if (isChar(vk, L';') || isChar(vk, L'+') || (vk == VK_OEM_PLUS)) { exec(CmdZoomIn); return true; }
        if (vk == VK_OEM_MINUS || isChar(vk, L'-')) { exec(CmdZoomOut); return true; }
        if (isChar(vk, L'@')) { exec(CmdResetView); return true; }
        if (isChar(vk, L'[')) { s.sub().brush.opacity = std::max(0.01f, s.sub().brush.opacity - 0.05f); invalidateAll(); return true; }
        if (isChar(vk, L']')) { s.sub().brush.opacity = std::min(1.f, s.sub().brush.opacity + 0.05f); invalidateAll(); return true; }
        return false;
    }
    if (ctrl && alt) {
        if (vk == 'G') { exec(CmdLayerClip); return true; }
        if (vk == '0') { exec(CmdZoom100); return true; }
        return false;
    }
    if (alt) {
        if (vk == VK_OEM_6 || isChar(vk, L']')) { exec(CmdLayerSelectUp); return true; }
        if (vk == VK_OEM_4 || isChar(vk, L'[')) { exec(CmdLayerSelectDown); return true; }
        static const struct { WPARAM vk; int menu; } mn[] = {{'F', MenuFile}, {'E', MenuEdit}, {'L', MenuLayer}, {'S', MenuSelect}, {'T', MenuFilter}, {'V', MenuView}, {'W', MenuWindow}, {'H', MenuHelp}};
        for (auto& m : mn)
            if (vk == m.vk) { int i = m.menu; defer([i] { openMenu(i); }); return true; }
        return false;
    }
    switch (vk) {
        case 'P': toolKey({Tool::Pen, Tool::Pencil}); return true;
        case 'B': toolKey({Tool::Brush, Tool::Airbrush}); return true;
        case 'E': toolKey({Tool::Eraser}); return true;
        case 'G': toolKey({Tool::Fill, Tool::Gradient}); return true;
        case 'K': toolKey({Tool::MoveLayer}); return true;
        case 'M': toolKey({Tool::Selection}); return true;
        case 'I': toolKey({Tool::Eyedropper}); return true;
        case 'H': toolKey({Tool::Hand}); return true;
        case 'R': toolKey({Tool::Rotate}); return true;
        case 'X': std::swap(s.mainColor, s.subColor); syncHsv(); invalidateAll(); return true;
        case 'C': s.colorSlot = s.colorSlot == ColorSlot::Transparent ? ColorSlot::Main : ColorSlot::Transparent; invalidateAll(); return true;
        case VK_TAB: exec(CmdHideAll); return true;
        case VK_F1: exec(CmdShortcuts); return true;
        case VK_DELETE: case VK_BACK: exec(CmdClear); return true;
    }
    if (isChar(vk, L'/')) { toolKey({Tool::Zoom}); return true; }
    if (isChar(vk, L'[') || isChar(vk, L']')) {
        const auto& steps = brushSizeSteps();
        float cur = s.sub().brush.size;
        bool up = isChar(vk, L']');
        float next = cur;
        if (up) { for (float v : steps) if (v > cur * 1.001f) { next = v; break; } }
        else for (int i = static_cast<int>(steps.size()) - 1; i >= 0; --i) if (steps[static_cast<size_t>(i)] < cur * 0.999f) { next = steps[static_cast<size_t>(i)]; break; }
        s.sub().brush.size = next;
        invalidateAll();
        return true;
    }
    if (isChar(vk, L',') || isChar(vk, L'.')) {
        ToolGroup& tg = s.group();
        int n = static_cast<int>(tg.subs.size());
        tg.active = (tg.active + (isChar(vk, L'.') ? 1 : n - 1)) % n;
        s.status = std::string("Sub tool: ") + tg.subs[static_cast<size_t>(tg.active)].name;
        invalidateAll();
        return true;
    }
    if (isChar(vk, L'-')) { exec(CmdRotateLeft); return true; }
    if (isChar(vk, L'^') || isChar(vk, L'=')) { exec(CmdRotateRight); return true; }
    return false;
}

}  // namespace pu::app::ui
