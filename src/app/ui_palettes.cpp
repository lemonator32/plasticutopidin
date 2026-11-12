// Menu bar, command bar and the palettes (Tool, Sub Tool, Tool Property, Brush Size,
// colour palettes, Color History, History, Navigator, Layer) and their docks.
#include "ui.h"

namespace pu::app::ui {

// ---- command bar and menu bar -------------------------------------------------------------
void paintMenuBar(const RECT& r) {
    g.menuTitles.clear();
    fillRect(r, T().frame);
    int x = r.left + S(6);
    for (int i = 0; i < kMenuCount; ++i) {
        int w = static_cast<int>(textWidth(kMenuNames[i])) + S(16);
        RECT t = rc(x, r.top + S(2), x + w, r.bottom - S(2));
        g.menuTitles.push_back(t);
        if (g.menuOpen == i) roundRect(t, T().buttonOn, S(3));
        else if (hovered(t)) roundRect(t, T().menuHover, S(3));
        text(t, kMenuNames[i], g.menuOpen == i ? RGB(255, 255, 255) : T().text, Center);
        addHit(t).press = [i](int, int) { deferPopup([i] { openMenu(i); }); };
        x += w;
    }
}

void paintCommandBar(const RECT& r) {
    if (H(r) <= 0) return;
    fillRect(r, T().header);
    fillRect(rc(r.left, r.bottom - 1, r.right, r.bottom), T().border);
    int bs = S(30), x = r.left + S(8), y = r.top + (H(r) - bs) / 2;
    auto btn = [&](Icon ic, const wchar_t* tip, int cmd, bool enabled = true) {
        x = button(rc(x, y, x + bs, y + bs), ic, tip, [cmd] { exec(cmd); }, false, enabled) + S(2);
    };
    auto sep = [&] {
        x += S(5);
        fillRect(rc(x, y + S(5), x + 1, y + bs - S(5)), T().border);
        x += S(6);
    };
    btn(Icon::New, L"New (Ctrl+N)", CmdNew);
    btn(Icon::Open, L"Open (Ctrl+O)", CmdOpen);
    btn(Icon::Save, L"Save (Ctrl+S)", CmdSave);
    btn(Icon::Export, L"Export as .png", CmdExportPng);
    sep();
    btn(Icon::Undo, L"Undo (Ctrl+Z)", CmdUndo, g.state.undo->canUndo());
    btn(Icon::Redo, L"Redo (Ctrl+Y)", CmdRedo, g.state.undo->canRedo());
    sep();
    btn(Icon::Clear, L"Clear (Delete)", CmdClear);
    btn(Icon::Fill, L"Fill with main color", CmdFill);
    sep();
    btn(Icon::NewLayer, L"New raster layer (Ctrl+Shift+N)", CmdLayerNew);
    btn(Icon::Merge, L"Merge with layer below (Ctrl+E)", CmdLayerMerge);
    sep();
    btn(Icon::FlipH, L"Flip view horizontally", CmdFlipView);
    btn(Icon::ResetView, L"Reset view (Ctrl+@)", CmdResetView);
}

// ---- Tool palette -------------------------------------------------------------------------------
void colorChips(const RECT& area, bool compact) {
    int cs = compact ? S(22) : S(26);
    int x = area.left, y = area.top;
    RECT mainR = rc(x, y, x + cs, y + cs);
    RECT subR = rc(x + cs * 3 / 5, y + cs * 3 / 5, x + cs * 8 / 5, y + cs * 8 / 5);
    RECT transR = compact ? rc(x, subR.bottom + S(5), x + cs * 2 / 3, subR.bottom + S(5) + cs * 2 / 3)
                          : rc(subR.right + S(8), subR.bottom - cs * 2 / 3, subR.right + S(8) + cs * 2 / 3, subR.bottom);
    RECT swapR = rc(mainR.right + S(1), y - S(1), mainR.right + S(13), y + S(11));
    ColorSlot slot = g.state.colorSlot;
    auto chip = [&](const RECT& r, Color c, bool active) {
        if (!need(inset(r, -S(3)))) return;
        fillRect(r, active ? T().accent : T().border);
        RECT in = inset(r, active ? S(2) : 1);
        fillRectC(in, c);
    };
    chip(subR, g.state.subColor, slot == ColorSlot::Sub);
    chip(mainR, g.state.mainColor, slot == ColorSlot::Main);
    if (need(transR)) {
        fillRect(transR, slot == ColorSlot::Transparent ? T().accent : T().border);
        checker(inset(transR, slot == ColorSlot::Transparent ? S(2) : 1), S(3));
    }
    icon(Icon::Swap, swapR, T().textDim, 0);
    addHit(subR, L"Sub color").press = [](int, int) { g.state.colorSlot = ColorSlot::Sub; syncHsv(); invalidateAll(); };
    addHit(mainR, L"Main color").press = [](int, int) { g.state.colorSlot = ColorSlot::Main; syncHsv(); invalidateAll(); };
    addHit(transR, L"Transparent color (C)").press = [](int, int) {
        g.state.colorSlot = g.state.colorSlot == ColorSlot::Transparent ? ColorSlot::Main : ColorSlot::Transparent;
        invalidateAll();
    };
    addHit(swapR, L"Switch main/sub color (X)").press = [](int, int) {
        std::swap(g.state.mainColor, g.state.subColor);
        syncHsv();
        invalidateAll();
    };
}

void paintToolStrip(const RECT& r) {
    if (W(r) <= 0) return;
    fillRect(r, T().palette);
    fillRect(rc(r.left, r.top, r.right, r.top + S(8)), T().header);
    fillRect(rc(r.right - 1, r.top, r.right, r.bottom), T().border);
    int bs = S(36), x = r.left + (W(r) - bs) / 2, y = r.top + S(14);
    for (int i = 0; i < static_cast<int>(g.state.tools.size()); ++i) {
        const ToolGroup& tg = g.state.tools[static_cast<size_t>(i)];
        RECT b = rc(x, y, x + bs, y + bs);
        std::wstring tip = widen(tg.name) + L" (" + widen(tg.shortcut) + L")";
        button(b, static_cast<Icon>(static_cast<int>(tg.tool)), tip, [i] { selectTool(i); }, i == g.state.toolIndex);
        y += bs + S(3);
        // Group separators, as in the default Tool palette: view | move | pick | draw | fill.
        if (tg.tool == Tool::Rotate || tg.tool == Tool::MoveLayer || tg.tool == Tool::Eyedropper || tg.tool == Tool::Eraser) {
            fillRect(rc(x + S(4), y, x + bs - S(4), y + 1), T().border);
            y += S(4);
        }
    }
    y += S(8);
    colorChips(rc(x, y, r.right, y + S(60)), true);
}

// ---- Sub Tool palette -------------------------------------------------------------------------
gp::Bitmap* subPreview(int gi, int si, int w, int h, COLORREF col) {
    const SubTool& st = g.state.tools[static_cast<size_t>(gi)].subs[static_cast<size_t>(si)];
    const BrushSettings& b = st.brush;
    size_t key = std::hash<float>()(b.size) ^ (std::hash<float>()(b.hardness) << 1) ^ (std::hash<float>()(b.opacity) << 2) ^
                 (std::hash<float>()(b.spacing) << 3) ^ (static_cast<size_t>(st.sizePressure) << 4) ^ (static_cast<size_t>(st.opacityPressure) << 5) ^
                 (static_cast<size_t>(w) << 8) ^ (static_cast<size_t>(h) << 20) ^ (static_cast<size_t>(col) << 28) ^ (static_cast<size_t>(b.antialias) << 6);
    App::Preview& p = g.previews[{gi, si}];
    if (p.bmp && p.key == key) return p.bmp.get();
    Surface s(w, h);
    BrushSettings bs = b;
    bs.eraser = false;
    bs.color = Color::rgba(GetRValue(col), GetGValue(col), GetBValue(col));
    bs.size = std::clamp(b.size * 0.6f, 2.f, h - 4.f);
    bs.minSizeRatio = st.sizePressure ? 0.1f : 1.f;
    bs.minOpacityRatio = st.opacityPressure ? 0.1f : 1.f;
    bs.smoothing = 0.f;
    {
        Stroke stroke(s, bs);
        const int n = 60;
        float amp = std::max(0.f, (h - bs.size) / 2.f - 2.f) * 0.8f;
        for (int k = 0; k <= n; ++k) {
            float t = k / static_cast<float>(n);
            stroke.addPoint({6 + t * (w - 12), h / 2.f - std::sin(t * 2 * kPi) * amp, std::sin(t * kPi)});
        }
        stroke.finish();
    }
    p.key = key;
    p.bmp = makeBitmap(w, h, [&](uint32_t* px, int stride) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) { const Color& c = s.at(x, y); px[y * stride + x] = argb(c.a, c.r, c.g, c.b); }
    });
    return p.bmp.get();
}

void paintSubTool(const RECT& r) {
    ToolGroup& tg = g.state.group();
    int gi = g.state.toolIndex;
    RECT head = rc(r.left, r.top, r.right, r.top + S(26));
    icon(static_cast<Icon>(static_cast<int>(tg.tool)), rc(head.left + S(6), head.top + S(4), head.left + S(24), head.bottom - S(4)), T().text, 0);
    text(rc(head.left + S(30), head.top, head.right - S(30), head.bottom), widen(tg.name), T().text, Left, g.fontBold.get());
    // Palette menu, like the ≡ button at the top-left of Clip Studio's Sub Tool palette.
    button(rc(head.right - S(26), head.top + S(3), head.right - S(4), head.bottom - S(3)), Icon::Menu, L"Sub tool menu", [] { deferPopup([] {
        HMENU m = CreatePopupMenu();
        bool single = g.state.group().subs.size() <= 1;
        AppendMenuW(m, MF_STRING, 1, L"Duplicate sub tool");
        AppendMenuW(m, MF_STRING | (single ? MF_GRAYED : 0), 2, L"Delete sub tool");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, 3, L"Reset to initial settings");
        POINT p;
        GetCursorPos(&p);
        int cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, g.hwnd, nullptr);
        DestroyMenu(m);
        if (cmd == 1) g.state.duplicateSubTool();
        else if (cmd == 2) g.state.deleteSubTool();
        else if (cmd == 3) g.state.resetSubTool();
        invalidateAll();
    }); });
    fillRect(rc(r.left + S(4), head.bottom - 1, r.right - S(4), head.bottom), T().border);
    RECT list = rc(r.left, head.bottom, r.right, r.bottom);
    auto rowHeight = [](const SubTool& st) { return st.kind == SubKind::Paint || st.kind == SubKind::Erase ? S(46) : S(28); };
    int contentH = S(4);
    for (const SubTool& st : tg.subs) contentH += rowHeight(st) + S(2);
    g.subToolScroll = std::clamp(g.subToolScroll, 0, std::max(0, contentH - H(list)));
    gp::Region old;
    P.gx->GetClip(&old);
    P.gx->SetClip(rf(list), gp::CombineModeIntersect);
    auto scroll = [](int d) { g.subToolScroll -= d / 3; invalidateAll(); };
    addHit(list).wheel = scroll;
    int y = list.top + S(4) - g.subToolScroll;
    for (int i = 0; i < static_cast<int>(tg.subs.size()); ++i) {
        const SubTool& st = tg.subs[static_cast<size_t>(i)];
        bool paint = st.kind == SubKind::Paint || st.kind == SubKind::Erase;
        int rowH = rowHeight(st);
        RECT row = rc(r.left + S(4), y, r.right - S(4), y + rowH);
        y += rowH + S(2);
        if (row.top >= list.bottom || row.bottom <= list.top) continue;
        bool sel = i == tg.active;
        if (need(row)) {
            if (sel) fillRect(row, T().selection);
            else if (hovered(row)) fillRect(row, mix(T().palette, T().buttonHover, 0.6f));
            if (sel) fillRect(rc(row.left, row.top, row.left + S(3), row.bottom), T().accent);
            text(rc(row.left + S(10), row.top + S(2), row.right - S(4), row.top + S(20)), widen(st.name), T().text);
            if (paint) {
                int pw = W(row) - S(20), ph = S(22);
                if (pw > 8) {
                    gp::Bitmap* bmp = subPreview(gi, i, pw, ph, T().text);
                    P.gx->DrawImage(bmp, row.left + S(10), row.top + S(20), pw, ph);
                }
            }
        }
        RECT hitR;
        IntersectRect(&hitR, &row, &list);
        Hit& h = addHit(hitR);
        h.press = [gi, i](int, int) {
            g.state.tools[static_cast<size_t>(gi)].active = i;
            invalidateAll();
        };
        h.wheel = scroll;
    }
    P.gx->SetClip(&old);
}

// ---- Tool Property palette ----------------------------------------------------------------------
void paintToolProperty(const RECT& r) {
    ToolGroup& tg = g.state.group();
    SubTool& st = g.state.sub();
    int gi = g.state.toolIndex, si = tg.active;
    RECT head = rc(r.left, r.top, r.right, r.top + S(26));
    text(rc(head.left + S(8), head.top, head.right - S(6), head.bottom), widen(st.name), T().text, Left, g.fontBold.get());
    fillRect(rc(r.left + S(4), head.bottom - 1, r.right - S(4), head.bottom), T().border);
    int y = head.bottom + S(6);
    int rowH = S(26), labelW = std::min(S(96), W(r) * 2 / 5), right = r.right - S(6), pb = S(22);
    auto sub = [gi, si]() -> SubTool& { return g.state.tools[static_cast<size_t>(gi)].subs[static_cast<size_t>(si)]; };
    auto row = [&](SliderSpec s, int pressure /* -1 none, 0 off, 1 on */, std::function<void()> togglePressure) {
        int sr = pressure >= 0 ? right - pb - S(4) : right;
        slider(rc(r.left + S(8), y, sr, y + rowH), s, labelW);
        if (pressure >= 0)
            button(rc(right - pb, y + (rowH - pb) / 2, right, y + (rowH + pb) / 2), Icon::Pressure, L"Pen pressure", togglePressure, pressure == 1);
        y += rowH + S(2);
    };
    auto changed = [] { invalidateAll(); };
    switch (st.kind) {
        case SubKind::Paint:
        case SubKind::Erase: {
            row({L"Brush size", st.brush.size, 0.5f, 1000.f, 1, true, L"", [sub, changed](float v) { sub().brush.size = v; changed(); }}, st.sizePressure,
                [sub] { sub().sizePressure = !sub().sizePressure; invalidateAll(); });
            row({L"Opacity", std::round(st.brush.opacity * 100), 1, 100, 0, false, L"", [sub, changed](float v) { sub().brush.opacity = v / 100.f; changed(); }},
                st.opacityPressure, [sub] { sub().opacityPressure = !sub().opacityPressure; invalidateAll(); });
            row({L"Hardness", std::round(st.brush.hardness * 100), 0, 100, 0, false, L"", [sub, changed](float v) { sub().brush.hardness = v / 100.f; changed(); }},
                -1, nullptr);
            row({L"Stabilization", std::round(st.brush.smoothing / 0.95f * 100), 0, 100, 0, false, L"",
                 [sub, changed](float v) { sub().brush.smoothing = v / 100.f * 0.95f; changed(); }},
                -1, nullptr);
            row({L"Spacing", std::round(st.brush.spacing * 100), 1, 100, 0, false, L"%", [sub, changed](float v) { sub().brush.spacing = v / 100.f; changed(); }},
                -1, nullptr);
            // Anti-aliasing: two-state toggle shown as a check row.
            RECT cr = rc(r.left + S(8), y, right, y + rowH);
            text(rc(cr.left, cr.top, cr.left + labelW - S(4), cr.bottom), L"Anti-aliasing", T().textDim);
            RECT box = rc(cr.left + labelW, cr.top + S(5), cr.left + labelW + S(16), cr.top + S(21));
            fillRect(box, st.brush.antialias ? T().accent : T().field);
            frameRect(box, T().border);
            if (st.brush.antialias) icon(Icon::Check, box, RGB(255, 255, 255), S(1));
            addHit(cr, L"Anti-aliasing").press = [sub](int, int) { sub().brush.antialias = !sub().brush.antialias; invalidateAll(); };
            y += rowH;
            break;
        }
        case SubKind::FillRefAll:
        case SubKind::FillLayer:
            row({L"Color margin", static_cast<float>(st.tolerance), 0, 100, 0, false, L"", [sub](float v) { sub().tolerance = static_cast<int>(v); invalidateAll(); }},
                -1, nullptr);
            row({L"Area scaling", static_cast<float>(st.expand), 0, 20, 0, false, L" px", [sub](float v) { sub().expand = static_cast<int>(v); invalidateAll(); }},
                -1, nullptr);
            text(rc(r.left + S(8), y, right, y + rowH), st.kind == SubKind::FillRefAll ? L"Refers to: all layers" : L"Refers to: editing layer",
                 T().textDim);
            y += rowH;
            break;
        case SubKind::GradientToSub:
        case SubKind::GradientToTransparent:
            row({L"Opacity", std::round(st.brush.opacity * 100), 1, 100, 0, false, L"", [sub](float v) { sub().brush.opacity = v / 100.f; invalidateAll(); }},
                -1, nullptr);
            text(rc(r.left + S(8), y, right, r.bottom - S(4)),
                 st.kind == SubKind::GradientToSub ? L"Drag across the canvas to draw from the main color to the sub color. Hold Shift to snap the angle."
                                                   : L"Drag across the canvas to draw from the main color to transparent. Hold Shift to snap the angle.",
                 T().textDim, Left, nullptr, true);
            break;
        default: {
            const wchar_t* hint = L"";
            switch (st.kind) {
                case SubKind::ZoomIn: hint = L"Click to zoom in. Alt+click zooms out. Drag left or right to zoom smoothly."; break;
                case SubKind::ZoomOut: hint = L"Click to zoom out. Alt+click zooms in. Drag left or right to zoom smoothly."; break;
                case SubKind::Hand: hint = L"Drag to scroll the canvas. Hold Space to use the Hand from any tool."; break;
                case SubKind::Rotate: hint = L"Drag to rotate the view. Hold Shift to snap to 15°. Shift+Space selects it temporarily."; break;
                case SubKind::PickDisplay: hint = L"Click to pick the color shown on the canvas. Hold Alt to use the Eyedropper from a drawing tool."; break;
                case SubKind::PickLayer: hint = L"Click to pick the color of the editing layer."; break;
                case SubKind::SelectRect:
                case SubKind::SelectEllipse:
                    hint = L"Drag to select an area. Shift+drag adds to the selection, Alt+drag subtracts. Click to deselect. "
                           L"Ctrl+drag moves the selected pixels. Ctrl+D deselects."; break;
                case SubKind::SelectLasso:
                    hint = L"Drag around the area to select. Shift+drag adds to the selection, Alt+drag subtracts. Click to deselect. "
                           L"Ctrl+drag moves the selected pixels."; break;
                case SubKind::MoveLayer: hint = L"Drag to move the editing layer. Hold Shift to move along one axis. Ctrl+drag moves it from any tool."; break;
                default: break;
            }
            text(rc(r.left + S(8), y, right, r.bottom - S(4)), hint, T().textDim, Left, nullptr, true);
            break;
        }
    }
}

// ---- Brush Size palette -----------------------------------------------------------------------------
void paintBrushSize(const RECT& r) {
    SubTool& st = g.state.sub();
    bool enabled = st.kind == SubKind::Paint || st.kind == SubKind::Erase;
    const auto& steps = brushSizeSteps();
    int cw = S(42), ch = S(46);
    int cols = std::max(1, (W(r) - S(8)) / cw);
    int rows = (static_cast<int>(steps.size()) + cols - 1) / cols;
    int maxScroll = std::max(0, rows * ch + S(8) - H(r));
    g.brushSizeScroll = std::clamp(g.brushSizeScroll, 0, maxScroll);
    int x0 = r.left + (W(r) - cols * cw) / 2, y0 = r.top + S(4) - g.brushSizeScroll;
    // Nearest step to the current size is highlighted.
    size_t cur = 0;
    for (size_t i = 0; i < steps.size(); ++i)
        if (std::fabs(std::log(steps[i] / st.brush.size)) < std::fabs(std::log(steps[cur] / st.brush.size))) cur = i;
    gp::Region old;
    P.gx->GetClip(&old);
    P.gx->SetClip(rf(r), gp::CombineModeIntersect);
    for (size_t i = 0; i < steps.size(); ++i) {
        int cx = x0 + static_cast<int>(i % static_cast<size_t>(cols)) * cw, cy = y0 + static_cast<int>(i / static_cast<size_t>(cols)) * ch;
        RECT cell = rc(cx + S(1), cy + S(1), cx + cw - S(1), cy + ch - S(1));
        if (cell.bottom < r.top || cell.top > r.bottom) continue;
        if (need(cell)) {
            if (enabled && i == cur) roundRect(cell, T().selection, S(3));
            else if (enabled && hovered(cell)) roundRect(cell, mix(T().palette, T().buttonHover, 0.6f), S(3));
            float t = (std::log(steps[i]) - std::log(0.7f)) / (std::log(1000.f) - std::log(0.7f));
            float d = S(2) + t * S(26);
            gp::SolidBrush b(gc(enabled ? T().text : T().textDim));
            P.gx->FillEllipse(&b, (cell.left + cell.right) / 2.f - d / 2, cell.top + S(16) - d / 2, d, d);
            text(rc(cell.left, cell.bottom - S(14), cell.right, cell.bottom), fmt(steps[i] < 1 ? L"%.1f" : steps[i] < 10 && steps[i] != std::floor(steps[i]) ? L"%.1f" : L"%.0f", steps[i]),
                 enabled ? T().textDim : mix(T().textDim, T().palette, 0.5f), Center, g.fontSmall.get());
        }
        if (enabled) {
            float v = steps[i];
            RECT hitR = cell;
            IntersectRect(&hitR, &cell, &r);
            Hit& h = addHit(hitR);
            h.press = [v](int, int) { g.state.sub().brush.size = v; invalidateAll(); };
            h.wheel = [](int delta) { g.brushSizeScroll -= delta / 3; invalidateAll(); };
        }
    }
    P.gx->SetClip(&old);
    Hit& bg = addHit(r);
    bg.wheel = [](int delta) { g.brushSizeScroll -= delta / 3; invalidateAll(); };
    // Background hit must not shadow the cells: move it to the front of the list.
    Hit tmp = std::move(g.hits.back());
    g.hits.pop_back();
    g.hits.insert(g.hits.begin(), std::move(tmp));
}

// ---- colour palettes -----------------------------------------------------------------------------------
void applyHsv() {
    setEditColor(hsvToRgb(g.hue, g.sat, g.val));
    invalidateAll();
}

void paintColorWheel(const RECT& r) {
    int stripH = S(36);
    RECT wheelArea = rc(r.left, r.top + S(4), r.right, r.bottom - stripH);
    int size = std::max(S(60), std::min(W(wheelArea), H(wheelArea)) - S(8));
    int cx = (wheelArea.left + wheelArea.right) / 2, cy = (wheelArea.top + wheelArea.bottom) / 2;
    float R = size / 2.f, ringW = static_cast<float>(S(14)), Ri = R - ringW;
    int sq = static_cast<int>((Ri - S(4)) * 1.41421356f);
    RECT sqR = rc(cx - sq / 2, cy - sq / 2, cx - sq / 2 + sq, cy - sq / 2 + sq);
    RECT ringR = rc(cx - size / 2, cy - size / 2, cx - size / 2 + size, cy - size / 2 + size);
    if (need(ringR)) {
        if (!g.wheelRing || g.wheelRingSize != size) {
            g.wheelRingSize = size;
            g.wheelRing = makeBitmap(size, size, [&](uint32_t* px, int stride) {
                for (int y = 0; y < size; ++y)
                    for (int x = 0; x < size; ++x) {
                        float dx = x + 0.5f - size / 2.f, dy = y + 0.5f - size / 2.f, d = std::sqrt(dx * dx + dy * dy);
                        float a = std::clamp(R - d + 0.5f, 0.f, 1.f) * std::clamp(d - Ri + 0.5f, 0.f, 1.f);
                        if (a <= 0.f) { px[y * stride + x] = 0; continue; }
                        float h = std::atan2(dy, dx) / (2 * kPi) + 0.25f;
                        Color c = hsvToRgb(h - std::floor(h), 1, 1);
                        px[y * stride + x] = argb(static_cast<uint8_t>(a * 255), c.r, c.g, c.b);
                    }
            });
        }
        P.gx->DrawImage(g.wheelRing.get(), ringR.left, ringR.top, size, size);
    }
    if (need(sqR)) {
        if (!g.svSquare || g.svSize != sq || g.svHue != g.hue) {
            g.svSize = sq;
            g.svHue = g.hue;
            float hue = g.hue;
            g.svSquare = makeBitmap(sq, sq, [&](uint32_t* px, int stride) {
                for (int y = 0; y < sq; ++y)
                    for (int x = 0; x < sq; ++x) {
                        Color c = hsvToRgb(hue, x / static_cast<float>(sq - 1), 1.f - y / static_cast<float>(sq - 1));
                        px[y * stride + x] = argb(255, c.r, c.g, c.b);
                    }
            });
        }
        P.gx->DrawImage(g.svSquare.get(), sqR.left, sqR.top, sq, sq);
    }
    // Markers
    {
        float ang = (g.hue - 0.25f) * 2 * kPi, mr = (R + Ri) / 2;
        float mx = cx + std::cos(ang) * mr, my = cy + std::sin(ang) * mr, ms = ringW * 0.45f;
        gp::Pen pw(gc(RGB(255, 255, 255)), static_cast<float>(S(2)));
        gp::Pen pb(gc(RGB(0, 0, 0)), 1.f);
        P.gx->DrawEllipse(&pb, mx - ms - 1, my - ms - 1, 2 * ms + 2, 2 * ms + 2);
        P.gx->DrawEllipse(&pw, mx - ms, my - ms, 2 * ms, 2 * ms);
        float sx = sqR.left + g.sat * (sq - 1), sy = sqR.top + (1 - g.val) * (sq - 1);
        float rr = static_cast<float>(S(5));
        P.gx->DrawEllipse(&pb, sx - rr - 1, sy - rr - 1, 2 * rr + 2, 2 * rr + 2);
        P.gx->DrawEllipse(&pw, sx - rr, sy - rr, 2 * rr, 2 * rr);
    }
    Hit& h = addHit(ringR);
    h.cursor = IDC_CROSS;
    struct Mode { int m = 0; };
    auto mode = std::make_shared<Mode>();
    auto apply = [=](int x, int y) {
        if (mode->m == 1) {
            float hh = std::atan2(y - static_cast<float>(cy), x - static_cast<float>(cx)) / (2 * kPi) + 0.25f;
            g.hue = hh - std::floor(hh);
            if (g.sat == 0.f) g.sat = 1.f, g.val = std::max(g.val, 0.5f);
        } else if (mode->m == 2) {
            g.sat = std::clamp((x - sqR.left) / static_cast<float>(sq - 1), 0.f, 1.f);
            g.val = std::clamp(1.f - (y - sqR.top) / static_cast<float>(sq - 1), 0.f, 1.f);
        }
        if (mode->m) applyHsv();
    };
    h.press = [=](int x, int y) {
        float dx = x - static_cast<float>(cx), dy = y - static_cast<float>(cy), d = std::sqrt(dx * dx + dy * dy);
        POINT p{x, y};
        mode->m = d >= Ri - S(2) && d <= R + S(2) ? 1 : PtInRect(&sqR, p) ? 2 : 0;
        apply(x, y);
    };
    h.drag = apply;
    // Bottom strip: colour chips and HSV readout.
    RECT strip = rc(r.left, r.bottom - stripH, r.right, r.bottom);
    colorChips(rc(strip.left + S(8), strip.top + S(2), strip.left + S(80), strip.bottom), true);
    Color c = editColor();
    wchar_t buf[96];
    swprintf(buf, 96, L"H %d  S %d  V %d", static_cast<int>(g.hue * 360 + 0.5f), static_cast<int>(g.sat * 100 + 0.5f), static_cast<int>(g.val * 100 + 0.5f));
    text(rc(strip.left + S(70), strip.top, strip.right - S(8), strip.top + stripH / 2 + S(2)), buf, T().textDim, Right, g.fontSmall.get());
    swprintf(buf, 96, L"#%02X%02X%02X", c.r, c.g, c.b);
    text(rc(strip.left + S(70), strip.top + stripH / 2 - S(2), strip.right - S(8), strip.bottom), buf, T().textDim, Right, g.fontSmall.get());
}

void paintColorSlider(const RECT& r) {
    Color c = editColor();
    int y = r.top + S(8), rowH = S(26), labelW = S(22);
    auto rgbRow = [&](const wchar_t* label, int channel) {
        uint8_t v = channel == 0 ? c.r : channel == 1 ? c.g : c.b;
        slider(rc(r.left + S(8), y, r.right - S(8), y + rowH),
               {label, static_cast<float>(v), 0, 255, 0, false, L"",
                [channel](float nv) {
                    Color cc = editColor();
                    uint8_t b = static_cast<uint8_t>(nv);
                    if (channel == 0) cc.r = b; else if (channel == 1) cc.g = b; else cc.b = b;
                    setEditColor(cc);
                    syncHsv();
                    invalidateAll();
                }, nullptr, nullptr, 1},
               labelW);
        y += rowH + S(2);
    };
    rgbRow(L"R", 0);
    rgbRow(L"G", 1);
    rgbRow(L"B", 2);
    y += S(8);
    auto hsvRow = [&](const wchar_t* label, float value, float hi, int which) {
        slider(rc(r.left + S(8), y, r.right - S(8), y + rowH),
               {label, value, 0, hi, 0, false, L"",
                [which, hi](float nv) {
                    if (which == 0) g.hue = nv / hi; else if (which == 1) g.sat = nv / hi; else g.val = nv / hi;
                    applyHsv();
                }, nullptr, nullptr, 1},
               labelW);
        y += rowH + S(2);
    };
    hsvRow(L"H", std::round(g.hue * 360), 360, 0);
    hsvRow(L"S", std::round(g.sat * 100), 100, 1);
    hsvRow(L"V", std::round(g.val * 100), 100, 2);
}

void paintColorSet(const RECT& r) {
    int cell = S(19);
    int cols = std::max(1, (W(r) - S(12)) / cell);
    int n = static_cast<int>(g.colorSet.size()) + 1;  // + "add" cell
    int rows = (n + cols - 1) / cols;
    int maxScroll = std::max(0, rows * cell + S(16) - H(r));
    g.colorSetScroll = std::clamp(g.colorSetScroll, 0, maxScroll);
    int x0 = r.left + S(6), y0 = r.top + S(6) - g.colorSetScroll;
    Color cur = editColor();
    gp::Region old;
    P.gx->GetClip(&old);
    P.gx->SetClip(rf(r), gp::CombineModeIntersect);
    Hit& bg = addHit(r);
    bg.wheel = [](int d) { g.colorSetScroll -= d / 3; invalidateAll(); };
    for (int i = 0; i < n; ++i) {
        int cx = x0 + (i % cols) * cell, cy = y0 + (i / cols) * cell;
        RECT sw = rc(cx, cy, cx + cell - S(2), cy + cell - S(2));
        if (sw.bottom < r.top || sw.top > r.bottom) continue;
        RECT hitR;
        IntersectRect(&hitR, &sw, &r);
        if (i == n - 1) {
            if (need(sw)) {
                fillRect(sw, hovered(sw) ? T().buttonHover : T().field);
                icon(Icon::Plus, sw, T().textDim, S(3));
            }
            Hit& h = addHit(hitR, L"Add the current color to the set");
            h.press = [](int, int) { g.colorSet.push_back(editColor()); invalidateAll(); };
            h.wheel = bg.wheel;
            continue;
        }
        Color c = g.colorSet[static_cast<size_t>(i)];
        if (need(sw)) {
            fillRectC(sw, c);
            if (c == cur) {
                frameRect(sw, RGB(255, 255, 255), S(2));
                frameRect(inset(sw, -1), RGB(0, 0, 0));
            } else {
                frameRect(sw, mix(T().border, RGB(0, 0, 0), 0.2f));
            }
        }
        Hit& h = addHit(hitR, L"Click to use, right-click to remove");
        h.press = [c](int, int) { setEditColor(c); syncHsv(); invalidateAll(); };
        h.rpress = [i](int, int) {
            if (i < static_cast<int>(g.colorSet.size())) g.colorSet.erase(g.colorSet.begin() + i);
            invalidateAll();
        };
        h.wheel = [](int d) { g.colorSetScroll -= d / 3; invalidateAll(); };
    }
    P.gx->SetClip(&old);
}

// ---- Color History palette: the colours drawn with, most recent first -----------------------------------

void paintColorHistory(const RECT& r) {
    const auto& hist = g.state.colorHistory;
    if (hist.empty()) {
        text(inset(r, S(10)), L"Colors you draw with appear here.", T().textDim, Center, nullptr, true);
        return;
    }
    int cell = S(19);
    int cols = std::max(1, (W(r) - S(12)) / cell);
    int x0 = r.left + S(6), y0 = r.top + S(6);
    Color cur = editColor();
    for (size_t i = 0; i < hist.size(); ++i) {
        int cx = x0 + static_cast<int>(i % cols) * cell, cy = y0 + static_cast<int>(i / cols) * cell;
        RECT sw = rc(cx, cy, cx + cell - S(2), cy + cell - S(2));
        if (sw.bottom > r.bottom) break;
        Color c = hist[i];
        if (need(sw)) {
            fillRectC(sw, c);
            if (c == cur) {
                frameRect(sw, RGB(255, 255, 255), S(2));
                frameRect(inset(sw, -1), RGB(0, 0, 0));
            } else {
                frameRect(sw, mix(T().border, RGB(0, 0, 0), 0.2f));
            }
        }
        Hit& h = addHit(sw, L"Click to use this color");
        h.press = [c](int, int) { setEditColor(c); syncHsv(); invalidateAll(); };
    }
}

// ---- History palette: every undo step, click one to go back to it -----------------------------------

void paintHistory(const RECT& r) {
    UndoStack& u = *g.state.undo;
    size_t done = u.size(), total = done + u.redoSize();
    int rowH = S(24);
    // Row 0 is the initial state, then each step oldest first; the current state is highlighted.
    int n = static_cast<int>(total) + 1;
    int maxScroll = std::max(0, n * rowH - H(r));
    // Keep the current step in view when the palette is not being scrolled by hand.
    int curTop = static_cast<int>(done) * rowH;
    if (curTop < g.historyScroll) g.historyScroll = curTop;
    if (curTop + rowH > g.historyScroll + H(r)) g.historyScroll = curTop + rowH - H(r);
    g.historyScroll = std::clamp(g.historyScroll, 0, maxScroll);
    gp::Region old;
    P.gx->GetClip(&old);
    P.gx->SetClip(rf(r), gp::CombineModeIntersect);
    Hit& bg = addHit(r);
    bg.wheel = [](int d) { g.historyScroll -= d / 3; invalidateAll(); };
    for (int i = 0; i < n; ++i) {
        int y = r.top + i * rowH - g.historyScroll;
        RECT row = rc(r.left, y, r.right, y + rowH);
        if (row.bottom < r.top || row.top > r.bottom) continue;
        bool current = static_cast<size_t>(i) == done;
        bool undone = static_cast<size_t>(i) > done;
        std::wstring name = i == 0 ? L"Open" : widen(static_cast<size_t>(i) <= done ? u.doneName(static_cast<size_t>(i - 1))
                                                                                   : u.redoNameAt(static_cast<size_t>(i) - done - 1));
        if (need(row)) {
            if (current) fillRect(row, T().selection);
            else if (hovered(row)) fillRect(row, T().buttonHover);
            icon(i == 0 ? Icon::New : Icon::History, rc(row.left + S(6), row.top + S(4), row.left + S(22), row.bottom - S(4)),
                 undone ? T().textDim : T().text, 0);
            text(rc(row.left + S(28), row.top, row.right - S(6), row.bottom), name, undone ? T().textDim : T().text, Left);
            fillRect(rc(row.left, row.bottom - 1, row.right, row.bottom), T().border);
        }
        RECT hitR;
        IntersectRect(&hitR, &row, &r);
        Hit& h = addHit(hitR);
        h.press = [i](int, int) {
            if (g.state.strokeActive()) return;
            g.state.historyJump(static_cast<size_t>(i));
            g.state.status = "History: step " + std::to_string(i);
            contentChanged();
        };
        h.wheel = bg.wheel;
    }
    P.gx->SetClip(&old);
}

// ---- Navigator palette --------------------------------------------------------------------------------
void paintNavigator(const RECT& r) {
    int ctrlH = S(58);
    RECT img = rc(r.left + S(8), r.top + S(8), r.right - S(8), r.bottom - ctrlH);
    int dw = g.state.doc.width(), dh = g.state.doc.height();
    float k = std::min(W(img) / static_cast<float>(dw), H(img) / static_cast<float>(dh));
    int tw = std::max(1, static_cast<int>(dw * k)), th = std::max(1, static_cast<int>(dh * k));
    RECT tr = rc(img.left + (W(img) - tw) / 2, img.top + (H(img) - th) / 2, 0, 0);
    tr.right = tr.left + tw;
    tr.bottom = tr.top + th;
    if (need(img) && H(img) > 4 && W(img) > 4) {
        fillRect(img, T().canvasBg);
        if (!g.navThumb || g.navDirty || g.navThumbW != tw || g.navThumbH != th) {
            g.navThumb = thumbnail(g.composite, tw, th, true);
            g.navThumbW = tw;
            g.navThumbH = th;
            g.navDirty = false;
        }
        P.gx->DrawImage(g.navThumb.get(), tr.left, tr.top, tw, th);
        // Visible area of the canvas view as a polygon.
        RECT v = computeLayout().view;
        gp::PointF pts[4];
        float wx[4] = {static_cast<float>(v.left), static_cast<float>(v.right), static_cast<float>(v.right), static_cast<float>(v.left)};
        float wy[4] = {static_cast<float>(v.top), static_cast<float>(v.top), static_cast<float>(v.bottom), static_cast<float>(v.bottom)};
        for (int i = 0; i < 4; ++i) {
            float cx, cy;
            g.state.view.canvasFromWindow(wx[i], wy[i], cx, cy);
            pts[i] = gp::PointF(tr.left + cx * k, tr.top + cy * k);
        }
        gp::Region old;
        P.gx->GetClip(&old);
        P.gx->SetClip(rf(img), gp::CombineModeIntersect);
        gp::Pen pen(gc(RGB(230, 50, 50)), static_cast<float>(S(2)));
        P.gx->DrawPolygon(&pen, pts, 4);
        P.gx->SetClip(&old);
    }
    Hit& h = addHit(img, L"Drag to move the view");
    h.cursor = IDC_HAND;
    auto centerOn = [tr, k](int x, int y) {
        float cx = (x - tr.left) / k, cy = (y - tr.top) / k;
        POINT c = viewCenter();
        float wx, wy;
        g.state.view.windowFromCanvas(cx, cy, wx, wy);
        g.state.view.panX += c.x - wx;
        g.state.view.panY += c.y - wy;
        g.userView = true;
        invalidateAll();
    };
    h.press = centerOn;
    h.drag = centerOn;
    h.wheel = [](int d) { zoomStepCenter(d > 0 ? 1 : -1); };
    // Zoom and rotation rows.
    int y = r.bottom - ctrlH + S(4), bs = S(22), rowH = S(24);
    int x = r.left + S(6);
    button(rc(x, y, x + bs, y + bs), Icon::ZoomOut, L"Zoom out", [] { zoomStepCenter(-1); });
    button(rc(r.right - S(6) - 2 * bs - S(2), y, r.right - S(6) - bs - S(2), y + bs), Icon::ZoomIn, L"Zoom in", [] { zoomStepCenter(1); });
    button(rc(r.right - S(6) - bs, y, r.right - S(6), y + bs), Icon::Fit, L"Fit to navigator (Ctrl+0)", [] { exec(CmdZoomFit); });
    slider(rc(x + bs + S(4), y - S(1), r.right - S(10) - 2 * bs - S(4), y + rowH - S(1)),
           {L"Zoom", std::round(g.state.view.zoom * 1000) / 10, 1, 6400, 1, true, L"",
            [](float v) {
                POINT c = viewCenter();
                zoomAt(static_cast<float>(c.x), static_cast<float>(c.y), v / 100.f);
            }},
           0);
    y += rowH + S(4);
    button(rc(x, y, x + bs, y + bs), Icon::RotateLeft, L"Rotate left (-)", [] { exec(CmdRotateLeft); });
    button(rc(r.right - S(6) - 2 * bs - S(2), y, r.right - S(6) - bs - S(2), y + bs), Icon::RotateRight, L"Rotate right (^)", [] { exec(CmdRotateRight); });
    button(rc(r.right - S(6) - bs, y, r.right - S(6), y + bs), Icon::FlipH, L"Flip horizontal", [] { exec(CmdFlipView); }, g.state.view.flipH);
    float deg = g.state.view.rotation * 180.f / kPi;
    slider(rc(x + bs + S(4), y - S(1), r.right - S(10) - 2 * bs - S(4), y + rowH - S(1)),
           {L"Rotation", std::round(deg), -180, 180, 0, false, L"°", [](float v) { setRotation(v * kPi / 180.f); }, nullptr, nullptr, 15}, 0);
}

// ---- Layer palette ----------------------------------------------------------------------------------------
void blendMenu() {
    HMENU m = CreatePopupMenu();
    Layer* l = g.state.active();
    for (int i = 0; i < static_cast<int>(BlendMode::Count); ++i) {
        AppendMenuW(m, MF_STRING | (l && static_cast<int>(l->mode) == i ? MF_CHECKED : 0), CmdBlendBase + i, kBlendNames[i]);
        if (i == 0 || i == 3 || i == 5 || i == 9 || i == 11 || i == 13) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    }
    POINT p;
    GetCursorPos(&p);
    int cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, g.hwnd, nullptr);
    DestroyMenu(m);
    if (cmd) exec(cmd);
}

void layerContextMenu() {
    HMENU m = CreatePopupMenu();
    Layer* l = g.state.active();
    AppendMenuW(m, MF_STRING, CmdLayerNew, L"New raster layer\tCtrl+Shift+N");
    AppendMenuW(m, MF_STRING, CmdLayerNewFill, L"New fill layer...");
    AppendMenuW(m, MF_STRING, CmdLayerNewGradient, L"New gradient layer");
    HMENU corr = CreatePopupMenu();
    appendCorrectionMenu(corr);
    AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(corr), L"New correction layer");
    AppendMenuW(m, MF_STRING, CmdLayerDup, L"Duplicate layer");
    AppendMenuW(m, MF_STRING, CmdLayerDelete, L"Delete layer");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CmdLayerRename, L"Rename layer");
    AppendMenuW(m, MF_STRING | (l && l->clipToBelow ? MF_CHECKED : 0), CmdLayerClip, L"Clip at layer below\tCtrl+Alt+G");
    AppendMenuW(m, MF_STRING | (l && l->locked ? MF_CHECKED : 0), CmdLayerLock, L"Lock layer");
    AppendMenuW(m, MF_STRING | (l && l->lockAlpha ? MF_CHECKED : 0), CmdLayerLockAlpha, L"Lock transparent pixels");
    if (l && !l->isRaster()) AppendMenuW(m, MF_STRING, CmdLayerSettings, L"Layer settings...");
    if (l && (l->kind == LayerKind::Fill || l->kind == LayerKind::Gradient)) AppendMenuW(m, MF_STRING, CmdLayerRasterize, L"Rasterize");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    if (l && !l->hasMask()) {
        AppendMenuW(m, MF_STRING, CmdMaskAdd, L"Create layer mask");
    } else if (l) {
        AppendMenuW(m, MF_STRING | (l->paintsMask() ? MF_CHECKED : 0), CmdMaskEdit, L"Edit layer mask\tCtrl+M");
        AppendMenuW(m, MF_STRING | (l->maskEnabled ? MF_CHECKED : 0), CmdMaskEnable, L"Enable mask");
        if (l->isRaster()) AppendMenuW(m, MF_STRING, CmdMaskApply, L"Apply mask to layer");
        AppendMenuW(m, MF_STRING, CmdMaskDelete, L"Delete layer mask");
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CmdLayerMerge, L"Merge with layer below\tCtrl+E");
    AppendMenuW(m, MF_STRING, CmdLayerMergeVisible, L"Merge visible layers\tCtrl+Shift+E");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CmdClear, L"Clear\tDelete");
    AppendMenuW(m, MF_STRING, CmdFill, L"Fill");
    POINT p;
    GetCursorPos(&p);
    int cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, g.hwnd, nullptr);
    DestroyMenu(m);
    if (cmd) exec(cmd);
}

RECT g_renameRect{};

namespace {

// A small stand-in for a layer's content to make its thumbnail from: raster
// layers use their pixels; the others are generated at thumbnail size.
Surface layerPreview(const Document& doc, const Layer& l, int w, int h) {
    switch (l.kind) {
        case LayerKind::Raster: return l.surface;
        case LayerKind::Fill: return Surface(w, h, l.fillColor);
        case LayerKind::Gradient: {
            Document tiny(w, h);
            Layer t;
            t.kind = LayerKind::Gradient;
            t.gradient = l.gradient;
            float sx = static_cast<float>(w) / std::max(1, doc.width()), sy = static_cast<float>(h) / std::max(1, doc.height());
            t.gradient.x0 *= sx; t.gradient.x1 *= sx;
            t.gradient.y0 *= sy; t.gradient.y1 *= sy;
            return tiny.renderLayer(t, tiny.bounds());
        }
        case LayerKind::Adjustment: {
            // A hue (across) by lightness (down) chart, corrected: shows what the layer does.
            Surface s(w, h);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    float hue = 6.f * x / w, v = 1.f - static_cast<float>(y) / std::max(1, h - 1);
                    auto ch = [&](float off) {
                        float k = std::fmod(hue + off, 6.f);
                        float c = std::clamp(std::min(k, 4.f - k), 0.f, 1.f);
                        return static_cast<uint8_t>(std::clamp(v < 0.5f ? c * v * 2.f : c + (1.f - c) * (v - 0.5f) * 2.f, 0.f, 1.f) * 255.f);
                    };
                    s.at(x, y) = l.adjustment.apply(Color::rgba(ch(5.f), ch(3.f), ch(1.f)));
                }
            return s;
        }
    }
    return Surface(w, h);
}

// The mask as grey (white shows the layer, black hides it), at thumbnail size.
Surface maskPreview(const Surface& mask, int w, int h) {
    Surface s(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t v = 255 - mask.at(std::min(mask.width() - 1, x * mask.width() / w), std::min(mask.height() - 1, y * mask.height() / h)).a;
            s.at(x, y) = Color::rgba(v, v, v);
        }
    return s;
}

}  // namespace

void paintLayer(const RECT& r) {
    Document& doc = g.state.doc;
    Layer* act = g.state.active();
    int y = r.top + S(6), rowH = S(26);
    // Blending mode dropdown and opacity slider.
    int split = r.left + W(r) * 9 / 20;
    RECT dd = rc(r.left + S(6), y + S(2), split - S(4), y + rowH - S(2));
    if (need(dd)) {
        fillRect(dd, hovered(dd) ? T().buttonHover : T().field);
        frameRect(dd, T().border);
        text(rc(dd.left + S(6), dd.top, dd.right - S(18), dd.bottom), act ? kBlendNames[static_cast<int>(act->mode)] : L"", T().text);
        icon(Icon::ChevronDown, rc(dd.right - S(18), dd.top + S(3), dd.right - S(4), dd.bottom - S(3)), T().textDim, 0);
    }
    addHit(dd, L"Blending mode").press = [](int, int) { deferPopup(blendMenu); };
    slider(rc(split, y, r.right - S(6), y + rowH),
           {L"Opacity", act ? std::round(act->opacity * 100) : 0, 0, 100, 0, false, L"",
            [](float v) {
                if (Layer* l = g.state.active()) {
                    l->opacity = v / 100.f;
                    contentChanged();
                }
            },
            [] { g.state.beginPropertyEdit(); }, [] { g.state.endPropertyEdit(); setTitle(); invalidateAll(); }, 5},
           0);
    y += rowH + S(4);
    // Toolbar row.
    int bs = S(24), x = r.left + S(6);
    auto tb = [&](Icon ic, const wchar_t* tip, int cmd, bool on = false) {
        x = button(rc(x, y, x + bs, y + bs), ic, tip, [cmd] { exec(cmd); }, on) + S(2);
    };
    tb(Icon::Clip, L"Clip at layer below (Ctrl+Alt+G)", CmdLayerClip, act && act->clipToBelow);
    tb(Icon::Lock, L"Lock layer", CmdLayerLock, act && act->locked);
    tb(Icon::LockAlpha, L"Lock transparent pixels", CmdLayerLockAlpha, act && act->lockAlpha);
    x = r.right - S(6) - 4 * (bs + S(2));
    tb(Icon::NewLayer, L"New raster layer (Ctrl+Shift+N)", CmdLayerNew);
    tb(Icon::Duplicate, L"Duplicate layer", CmdLayerDup);
    tb(Icon::Merge, L"Merge with layer below (Ctrl+E)", CmdLayerMerge);
    tb(Icon::Delete, L"Delete layer", CmdLayerDelete);
    y += bs + S(4);
    fillRect(rc(r.left, y, r.right, y + 1), T().border);
    y += 1;

    // Layer list, top layer first, then the paper.
    RECT list = rc(r.left, y, r.right, r.bottom);
    fillRect(list, mix(T().palette, T().frame, 0.35f));
    int n = static_cast<int>(doc.layerCount());
    int lrH = S(44);
    int contentH = (n + 1) * lrH;
    g.layerScroll = std::clamp(g.layerScroll, 0, std::max(0, contentH - H(list)));
    gp::Region old;
    P.gx->GetClip(&old);
    P.gx->SetClip(rf(list), gp::CombineModeIntersect);
    Hit& bgHit = addHit(list);
    bgHit.wheel = [](int d) { g.layerScroll -= d / 2; invalidateAll(); };
    if (g.thumbsDirty) g.thumbs.clear();
    int thW = S(46), thH = S(34);
    for (int k = 0; k <= n; ++k) {
        int top = list.top + k * lrH - g.layerScroll;
        RECT row = rc(list.left, top, list.right, top + lrH);
        if (row.bottom < list.top || row.top > list.bottom) continue;
        RECT visible;
        IntersectRect(&visible, &row, &list);
        bool paper = k == n;
        int idx = n - 1 - k;
        const Layer* l = paper ? nullptr : doc.layer(static_cast<size_t>(idx));
        bool sel = !paper && static_cast<size_t>(idx) == doc.activeIndex();
        bool vis = paper ? doc.paperVisible : l->visible;
        RECT eye = rc(row.left, row.top, row.left + S(28), row.bottom);
        int indent = (!paper && l->clipToBelow) ? S(10) : 0;
        RECT thumb = rc(eye.right + S(4) + indent, row.top + (lrH - thH) / 2, eye.right + S(4) + indent + thW, row.top + (lrH + thH) / 2);
        const bool masked = !paper && l->hasMask();
        RECT maskTh = rc(thumb.right + S(5), thumb.top, thumb.right + S(5) + thW, thumb.bottom);
        RECT txt = rc((masked ? maskTh.right : thumb.right) + S(8), row.top, row.right - S(28), row.bottom);
        if (need(row)) {
            fillRect(row, sel ? T().selection : hovered(row) ? mix(T().palette, T().buttonHover, 0.5f) : T().palette);
            fillRect(rc(row.left, row.bottom - 1, row.right, row.bottom), T().border);
            fillRect(rc(eye.right, row.top, eye.right + 1, row.bottom), T().border);
            if (vis) icon(Icon::Eye, eye, T().text, S(6));
            if (!paper && l->clipToBelow) fillRect(rc(thumb.left - S(6), row.top + S(4), thumb.left - S(3), row.bottom - S(4)), RGB(214, 64, 64));
            if (paper) {
                fillRectC(thumb, doc.paperColor);
            } else {
                auto& tb2 = g.thumbs[l->id];
                if (!tb2) tb2 = thumbnail(layerPreview(doc, *l, thW * 2, thH * 2), thW, thH, true);
                P.gx->DrawImage(tb2.get(), thumb.left, thumb.top, thW, thH);
                if (masked) {
                    auto& mb = g.thumbs[l->id | (1ull << 63)];
                    if (!mb) mb = thumbnail(maskPreview(l->mask, thW * 2, thH * 2), thW, thH, false);
                    P.gx->DrawImage(mb.get(), maskTh.left, maskTh.top, thW, thH);
                    frameRect(inset(maskTh, -1), T().border);
                    if (!l->maskEnabled) {  // a disabled mask: a red cross over its thumbnail
                        gp::Pen pen(gp::Color(255, 214, 64, 64), static_cast<float>(S(2)));
                        P.gx->DrawLine(&pen, static_cast<INT>(maskTh.left), static_cast<INT>(maskTh.top), static_cast<INT>(maskTh.right),
                                       static_cast<INT>(maskTh.bottom));
                        P.gx->DrawLine(&pen, static_cast<INT>(maskTh.left), static_cast<INT>(maskTh.bottom), static_cast<INT>(maskTh.right),
                                       static_cast<INT>(maskTh.top));
                    }
                }
            }
            frameRect(inset(thumb, -1), T().border);
            // The selected layer's edit target (pixels or mask) gets an accent frame.
            if (sel && masked) frameRect(inset(l->paintsMask() ? maskTh : thumb, -2), T().accent, 2);
            if (paper) {
                text(txt, L"Paper", T().text);
            } else {
                wchar_t buf[96];
                if (l->isRaster())
                    swprintf(buf, 96, L"%d %%  %s", static_cast<int>(l->opacity * 100 + 0.5f), kBlendNames[static_cast<int>(l->mode)]);
                else
                    swprintf(buf, 96, L"%hs  %d %%  %s", layerKindName(l->kind), static_cast<int>(l->opacity * 100 + 0.5f),
                             kBlendNames[static_cast<int>(l->mode)]);
                text(rc(txt.left, txt.top + S(4), txt.right, txt.top + lrH / 2), buf, T().textDim, Left, g.fontSmall.get());
                RECT nameR = rc(txt.left, txt.top + lrH / 2 - S(2), txt.right, txt.bottom - S(4));
                if (!(g.edit && g.renameId == l->id)) text(nameR, widen(l->name), T().text);
                if (l->locked) icon(Icon::Lock, rc(row.right - S(24), row.top + S(6), row.right - S(8), row.top + S(22)), T().textDim, 0);
                if (l->lockAlpha) icon(Icon::LockAlpha, rc(row.right - S(24), row.bottom - S(22), row.right - S(8), row.bottom - S(6)), T().textDim, 0);
            }
        }
        if (sel && !paper) g_renameRect = rc(txt.left - S(2), row.top + lrH / 2 - S(4), txt.right, row.bottom - S(4));
        // Hits: eye, then row (drag to reorder).
        RECT eyeHit;
        IntersectRect(&eyeHit, &eye, &list);
        Hit& he = addHit(eyeHit, paper ? L"Show/hide paper" : L"Show/hide layer");
        he.wheel = bgHit.wheel;
        if (paper) {
            he.press = [](int, int) { g.state.doc.paperVisible = !g.state.doc.paperVisible; g.state.dirty = true; contentChanged(); };
            continue;
        }
        he.press = [idx](int, int) { g.state.toggleVisible(static_cast<size_t>(idx)); contentChanged(); };
        // Thumbnails: choose what pixel tools edit (the layer or its mask);
        // double-click a generated layer's thumbnail for its settings.
        if (masked || !l->isRaster()) {
            RECT th;
            IntersectRect(&th, &thumb, &list);
            Hit& ht = addHit(th, l->isRaster() ? L"Edit the layer" : L"Double-click for the layer's settings");
            ht.wheel = bgHit.wheel;
            ht.press = [idx](int, int) {
                g.state.doc.setActiveIndex(static_cast<size_t>(idx));
                if (g.state.active()->isRaster()) g.state.setEditMask(false);
                invalidateAll();
            };
            ht.dbl = [] { defer(layerSettingsDialog); };
        }
        if (masked) {
            RECT mh;
            IntersectRect(&mh, &maskTh, &list);
            Hit& hm = addHit(mh, L"Edit the layer mask (Shift+click: enable/disable)");
            hm.wheel = bgHit.wheel;
            hm.press = [idx](int, int) {
                g.state.doc.setActiveIndex(static_cast<size_t>(idx));
                if (GetKeyState(VK_SHIFT) < 0) {
                    g.state.toggleMaskEnabled();
                    contentChanged();
                    return;
                }
                g.state.setEditMask(true);
                invalidateAll();
            };
        }
        Hit& hr = addHit(visible);
        hr.wheel = bgHit.wheel;
        int listTop = list.top, scroll = g.layerScroll;
        int fromTop = k;
        auto dragging = std::make_shared<bool>(false);
        auto startY = std::make_shared<int>(0);
        hr.press = [idx, startY, dragging](int, int y2) {
            g.state.doc.setActiveIndex(static_cast<size_t>(idx));
            *startY = y2;
            *dragging = false;
            g.layerDropSlot = -1;
            invalidateAll();
        };
        hr.drag = [=](int, int y2) {
            if (!*dragging && std::abs(y2 - *startY) > S(4)) *dragging = true;
            if (!*dragging) return;
            int slot = std::clamp(static_cast<int>(std::lround((y2 - listTop + scroll) / static_cast<float>(lrH))), 0, n);
            if (slot != g.layerDropSlot) { g.layerDropSlot = slot; invalidateAll(); }
        };
        hr.release = [=] {
            int slot = g.layerDropSlot;
            g.layerDropSlot = -1;
            if (*dragging && slot >= 0) {
                int newTop = slot > fromTop ? slot - 1 : slot;
                int to = n - 1 - newTop;
                if (to != idx) {
                    g.state.moveLayerTo(static_cast<size_t>(idx), static_cast<size_t>(to));
                    contentChanged();
                    return;
                }
            }
            invalidateAll();
        };
        hr.dbl = [] { defer(startRename); };
        hr.rpress = [idx](int, int) {
            g.state.doc.setActiveIndex(static_cast<size_t>(idx));
            invalidateAll();
            deferPopup(layerContextMenu);
        };
    }
    if (g.layerDropSlot >= 0) {
        int ly = list.top + g.layerDropSlot * lrH - g.layerScroll;
        fillRect(rc(list.left + S(2), ly - S(1), list.right - S(2), ly + S(2)), T().accent);
    }
    g.thumbsDirty = false;
    P.gx->SetClip(&old);
    // List background hit goes first so rows take precedence.
    auto it = std::find_if(g.hits.begin(), g.hits.end(), [&](const Hit& h) { return EqualRect(&h.r, &list) && h.wheel && !h.press; });
    if (it != g.hits.end()) {
        Hit tmp = std::move(*it);
        g.hits.erase(it);
        g.hits.insert(g.hits.begin(), std::move(tmp));
    }
}

// ---- docks ---------------------------------------------------------------------------------------------------
void paintPalette(Pal p, const RECT& r) {
    switch (p) {
        case PalSubTool: paintSubTool(r); break;
        case PalToolProperty: paintToolProperty(r); break;
        case PalBrushSize: paintBrushSize(r); break;
        case PalColorWheel: paintColorWheel(r); break;
        case PalColorSlider: paintColorSlider(r); break;
        case PalColorSet: paintColorSet(r); break;
        case PalNavigator: paintNavigator(r); break;
        case PalLayer: paintLayer(r); break;
        case PalColorHistory: paintColorHistory(r); break;
        case PalHistory: paintHistory(r); break;
        case PalLayerProperty: paintLayerProperty(r); break;
        default: break;
    }
}

// ---- Layer Property palette: effects and expression color of the editing layer -------------------------

namespace {

// Applies `fn` to the editing layer as one undoable property change.
void editLayerProps(const std::function<void(Layer&)>& fn) {
    Layer* l = g.state.active();
    if (!l) return;
    g.state.beginPropertyEdit();
    fn(*l);
    g.state.endPropertyEdit();
    updateComposite(g.state.doc.bounds());
    contentChanged();
}

}  // namespace

bool pickColorDialog(Color& c) {
    static COLORREF custom[16]{};
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = g.hwnd;
    cc.rgbResult = RGB(c.r, c.g, c.b);
    cc.lpCustColors = custom;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (!ChooseColorW(&cc)) return false;
    c = Color::rgba(GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
    return true;
}

namespace {

// A colour well; clicking opens the colour dialog and stores the result through `set`.
void colorWell(const RECT& r, Color c, const wchar_t* tip, std::function<void(Color)> set) {
    if (need(r)) {
        fillRectC(r, c);
        frameRect(r, hovered(r) ? T().accent : T().border);
    }
    addHit(r, tip).press = [c, set](int, int) {
        deferPopup([c, set] {
            Color v = c;
            if (pickColorDialog(v)) set(v);
        });
    };
}

}  // namespace

void paintLayerProperty(const RECT& r) {
    Layer* l = g.state.active();
    if (!l) {
        text(inset(r, S(10)), L"No layer", T().textDim, Center);
        return;
    }
    int x = r.left + S(8), right = r.right - S(8), y = r.top + S(8), rowH = S(26);
    auto heading = [&](const wchar_t* s) {
        text(rc(x, y, right, y + S(20)), s, T().textDim, Left, g.fontSmall.get());
        y += S(20);
    };
    // A toggle button with an icon and a label, like the Effect buttons in Clip Studio's Layer Property.
    auto toggle = [&](RECT b, Icon ic, const wchar_t* label, bool on, std::function<void()> fn) {
        if (need(b)) {
            roundRect(b, on ? T().buttonOn : hovered(b) ? T().buttonHover : T().field, S(3));
            icon(ic, rc(b.left + S(6), b.top + S(5), b.left + S(22), b.bottom - S(5)), T().text, 0);
            text(rc(b.left + S(26), b.top, b.right - S(4), b.bottom), label, T().text, Left);
        }
        addHit(b, label).press = [fn](int, int) { fn(); };
    };

    text(rc(x, y, right, y + S(20)), widen(l->name), T().text, Left, g.fontBold.get());
    y += S(26);

    heading(L"Effect");
    int half = (right - x - S(6)) / 2;
    toggle(rc(x, y, x + half, y + rowH), Icon::Selection, L"Border", l->borderOn,
           [] { editLayerProps([](Layer& L) { L.borderOn = !L.borderOn; }); });
    toggle(rc(x + half + S(6), y, right, y + rowH), Icon::LayerProperty, L"Layer color", l->layerColorOn,
           [] { editLayerProps([](Layer& L) { L.layerColorOn = !L.layerColorOn; }); });
    y += rowH + S(10);

    if (l->borderOn) {
        heading(L"Border effect");
        int well = S(36);
        slider(rc(x, y, right - well - S(6), y + rowH),
               {L"Width", static_cast<float>(l->borderWidth), 1, 50, 0, false, L" px",
                [](float v) {
                    if (Layer* a = g.state.active()) {
                        a->borderWidth = static_cast<int>(std::lround(v));
                        updateComposite(g.state.doc.bounds());
                        invalidateAll();
                    }
                },
                [] { g.state.beginPropertyEdit(); }, [] { g.state.endPropertyEdit(); contentChanged(); }, 1},
               S(52));
        colorWell(rc(right - well, y + S(3), right, y + rowH - S(3)), l->borderColor, L"Border color",
                  [](Color c) { editLayerProps([c](Layer& L) { L.borderColor = c; }); });
        y += rowH + S(10);
    }
    if (l->layerColorOn) {
        heading(L"Layer color");
        colorWell(rc(x, y + S(3), x + S(48), y + rowH - S(3)), l->layerColor, L"Layer color",
                  [](Color c) { editLayerProps([c](Layer& L) { L.layerColor = c; }); });
        text(rc(x + S(56), y, right, y + rowH), L"Dark tones show in this color", T().textDim, Left, g.fontSmall.get());
        y += rowH + S(10);
    }

    heading(L"Expression color");
    toggle(rc(x, y, x + half, y + rowH), Icon::ColorWheelSmall, L"Color", !l->gray,
           [] { editLayerProps([](Layer& L) { L.gray = false; }); });
    toggle(rc(x + half + S(6), y, right, y + rowH), Icon::Gray, L"Gray", l->gray,
           [] { editLayerProps([](Layer& L) { L.gray = true; }); });
    y += rowH + S(10);

    heading(L"Layer");
    slider(rc(x, y, right, y + rowH),
           {L"Opacity", std::round(l->opacity * 100), 0, 100, 0, false, L"",
            [](float v) {
                if (Layer* a = g.state.active()) {
                    a->opacity = v / 100.f;
                    contentChanged();
                }
            },
            [] { g.state.beginPropertyEdit(); }, [] { g.state.endPropertyEdit(); setTitle(); invalidateAll(); }, 5},
           S(52));
}

void showPalette(Pal p) {
    g.palVisible[p] = true;
    g.hideAll = false;
    for (auto* col : {&g.leftDocks, &g.rightDocks})
        for (Dock& d : *col)
            for (size_t i = 0; i < d.tabs.size(); ++i)
                if (d.tabs[i] == p) d.active = static_cast<int>(i);
}

void paintDock(Dock& d, const RECT& r) {
    int headH = S(24);
    RECT head = rc(r.left, r.top, r.right, r.top + headH);
    RECT body = rc(r.left, head.bottom, r.right, r.bottom);
    fillRect(head, T().header);
    std::vector<int> vis;
    for (int i = 0; i < static_cast<int>(d.tabs.size()); ++i) if (palShown(d.tabs[static_cast<size_t>(i)])) vis.push_back(i);
    if (std::find(vis.begin(), vis.end(), d.active) == vis.end()) d.active = vis.front();
    int x = head.left + S(2);
    for (int i : vis) {
        const wchar_t* name = kPalNames[d.tabs[static_cast<size_t>(i)]];
        int w = std::min(static_cast<int>(textWidth(name, g.fontSmall.get())) + S(18), (W(head) - S(4)) / static_cast<int>(vis.size()));
        RECT t = rc(x, head.top + S(3), x + w, head.bottom);
        bool act = i == d.active;
        if (act) fillRect(t, T().palette);
        else if (hovered(t)) fillRect(t, T().tabActive);
        text(t, name, act ? T().text : T().textDim, Center, g.fontSmall.get());
        Dock* dp = &d;
        addHit(t).press = [dp, i](int, int) { dp->active = i; invalidateAll(); };
        x += w + S(1);
    }
    fillRect(body, T().palette);
    paintPalette(d.tabs[static_cast<size_t>(d.active)], body);
}

void paintColumn(std::vector<Dock>& docks, const RECT& col) {
    if (W(col) <= 0) return;
    fillRect(col, T().frame);
    std::vector<RECT> splitters;
    auto places = placeDocks(docks, col, &splitters);
    // Docks outside the repaint region still run, so their hits are registered.
    for (auto& pl : places) {
        gp::Region old;
        P.gx->GetClip(&old);
        P.gx->SetClip(rf(pl.r), gp::CombineModeIntersect);
        paintDock(docks[static_cast<size_t>(pl.index)], pl.r);
        P.gx->SetClip(&old);
    }
    std::vector<Dock>* dv = &docks;
    for (size_t s = 0; s < splitters.size(); ++s) {
        int above = places[s].index, below = places[s + 1].index;
        Hit& h = addHit(splitters[s]);
        h.cursor = IDC_SIZENS;
        auto last = std::make_shared<int>(0);
        h.press = [last](int, int y) { *last = y; };
        h.drag = [dv, above, below, last](int, int y) {
            int dy = y - *last;
            *last = y;
            Dock& a = (*dv)[static_cast<size_t>(above)];
            Dock& b = (*dv)[static_cast<size_t>(below)];
            // The fixed-height dock absorbs the change; the flexible one takes the rest.
            if (!a.flex) a.height = std::max(S(60), a.height + dy);
            else b.height = std::max(S(60), b.height - dy);
            invalidateAll();
        };
    }
}

}  // namespace pu::app::ui
