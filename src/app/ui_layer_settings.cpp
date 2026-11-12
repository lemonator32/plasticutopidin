// Settings of Fill, Gradient and Correction layers.  Changes apply to the
// canvas while the dialog is open; OK keeps them as one undo step, Cancel
// restores the layer.
#include "ui.h"

#include <commctrl.h>  // after windows.h (via ui.h)

namespace pu::app::ui {

namespace {

enum : WORD {
    IdSlider = 200,     // + i
    IdSliderText = 220, // + i
    IdStops = 400,
    IdStopAdd,
    IdStopRemove,
    IdStopColor,
    IdStopPos,
    IdStopReverse,
    IdShape,
    IdNote,
};

struct SliderSpec {
    const wchar_t* label;
    int lo, hi;
    int* value;
};

struct Session {
    LayerKind kind = LayerKind::Raster;
    Adjustment adj;                 // Correction layers
    GradientFill grad;              // Gradient layers
    std::vector<GradientStop>* stops = nullptr;  // the stop list being edited (grad.stops or adj.stops)
    std::vector<SliderSpec> sliders;
    bool syncing = false;           // setting controls from code: ignore their notifications
};

std::wstring stopLabel(const GradientStop& s) {
    wchar_t buf[64];
    if (s.color.a == 255)
        swprintf(buf, 64, L"%3d %%    #%02X%02X%02X", static_cast<int>(s.pos * 100.f + 0.5f), s.color.r, s.color.g, s.color.b);
    else
        swprintf(buf, 64, L"%3d %%    #%02X%02X%02X  (alpha %d)", static_cast<int>(s.pos * 100.f + 0.5f), s.color.r, s.color.g, s.color.b,
                 s.color.a);
    return buf;
}

// Pushes the session's settings to the layer and redraws the canvas.
void apply(Session& s) {
    if (s.kind == LayerKind::Gradient) {
        g.state.setGradient(s.grad);
    } else {
        Adjustment a = s.adj;
        std::stable_sort(a.stops.begin(), a.stops.end(), [](auto& p, auto& q) { return p.pos < q.pos; });
        g.state.setAdjustment(std::move(a));
    }
    updateComposite(g.state.doc.bounds());
    contentChanged();
}

void refreshStops(HWND dlg, Session& s, int select) {
    if (!s.stops) return;
    HWND lb = GetDlgItem(dlg, IdStops);
    s.syncing = true;
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    for (const GradientStop& st : *s.stops) SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(stopLabel(st).c_str()));
    select = std::clamp(select, 0, static_cast<int>(s.stops->size()) - 1);
    SendMessageW(lb, LB_SETCURSEL, static_cast<WPARAM>(select), 0);
    SetDlgItemInt(dlg, IdStopPos, static_cast<UINT>((*s.stops)[static_cast<size_t>(select)].pos * 100.f + 0.5f), FALSE);
    EnableWindow(GetDlgItem(dlg, IdStopRemove), s.stops->size() > 2);
    s.syncing = false;
}

int selectedStop(HWND dlg) { return static_cast<int>(SendDlgItemMessageW(dlg, IdStops, LB_GETCURSEL, 0, 0)); }

void sliderText(HWND dlg, const Session& s, size_t i) {
    wchar_t buf[64];
    swprintf(buf, 64, L"%s: %d", s.sliders[i].label, *s.sliders[i].value);
    SetDlgItemTextW(dlg, static_cast<int>(IdSliderText + i), buf);
}

INT_PTR CALLBACK settingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* s = reinterpret_cast<Session*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            s = reinterpret_cast<Session*>(lp);
            s->syncing = true;
            for (size_t i = 0; i < s->sliders.size(); ++i) {
                HWND tb = GetDlgItem(dlg, static_cast<int>(IdSlider + i));
                SendMessageW(tb, TBM_SETRANGEMIN, FALSE, s->sliders[i].lo);
                SendMessageW(tb, TBM_SETRANGEMAX, FALSE, s->sliders[i].hi);
                SendMessageW(tb, TBM_SETPAGESIZE, 0, std::max(1, (s->sliders[i].hi - s->sliders[i].lo) / 20));
                SendMessageW(tb, TBM_SETPOS, TRUE, *s->sliders[i].value);
                sliderText(dlg, *s, i);
            }
            if (s->kind == LayerKind::Gradient) {
                HWND cb = GetDlgItem(dlg, IdShape);
                SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Linear"));
                SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Circle"));
                SendMessageW(cb, CB_SETCURSEL, s->grad.shape == GradientFill::Shape::Radial ? 1 : 0, 0);
            }
            s->syncing = false;
            refreshStops(dlg, *s, 0);
            g.state.beginPropertyEdit();
            return TRUE;
        }
        case WM_HSCROLL: {
            int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
            size_t i = static_cast<size_t>(id - IdSlider);
            if (!s || id < IdSlider || i >= s->sliders.size()) break;
            int v = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lp), TBM_GETPOS, 0, 0));
            if (v == *s->sliders[i].value) return TRUE;
            *s->sliders[i].value = v;
            sliderText(dlg, *s, i);
            apply(*s);
            return TRUE;
        }
        case WM_COMMAND: {
            if (!s) break;
            const int id = LOWORD(wp), code = HIWORD(wp);
            if (id == IDOK) {
                g.state.endPropertyEdit();
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (id == IDCANCEL) {
                g.state.cancelPropertyEdit();
                updateComposite(g.state.doc.bounds());
                contentChanged();
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            if (s->syncing) return TRUE;
            if (id == IdShape && code == CBN_SELCHANGE) {
                s->grad.shape = SendDlgItemMessageW(dlg, IdShape, CB_GETCURSEL, 0, 0) == 1 ? GradientFill::Shape::Radial
                                                                                          : GradientFill::Shape::Linear;
                apply(*s);
                return TRUE;
            }
            if (!s->stops) break;
            std::vector<GradientStop>& st = *s->stops;
            int sel = std::clamp(selectedStop(dlg), 0, static_cast<int>(st.size()) - 1);
            switch (id) {
                case IdStops:
                    if (code == LBN_SELCHANGE) refreshStops(dlg, *s, sel);
                    if (code == LBN_DBLCLK) PostMessageW(dlg, WM_COMMAND, IdStopColor, 0);
                    return TRUE;
                case IdStopPos:
                    if (code == EN_CHANGE) {
                        BOOL ok;
                        int v = static_cast<int>(GetDlgItemInt(dlg, IdStopPos, &ok, FALSE));
                        if (!ok) return TRUE;
                        st[static_cast<size_t>(sel)].pos = std::clamp(v, 0, 100) / 100.f;
                        s->syncing = true;
                        SendDlgItemMessageW(dlg, IdStops, LB_DELETESTRING, static_cast<WPARAM>(sel), 0);
                        SendDlgItemMessageW(dlg, IdStops, LB_INSERTSTRING, static_cast<WPARAM>(sel),
                                            reinterpret_cast<LPARAM>(stopLabel(st[static_cast<size_t>(sel)]).c_str()));
                        SendDlgItemMessageW(dlg, IdStops, LB_SETCURSEL, static_cast<WPARAM>(sel), 0);
                        s->syncing = false;
                        apply(*s);
                    }
                    return TRUE;
                case IdStopColor: {
                    Color c = st[static_cast<size_t>(sel)].color;
                    uint8_t a = c.a;
                    if (!pickColorDialog(c)) return TRUE;
                    c.a = a;
                    st[static_cast<size_t>(sel)].color = c;
                    refreshStops(dlg, *s, sel);
                    apply(*s);
                    return TRUE;
                }
                case IdStopAdd: {
                    // A new stop halfway to the next one, in the colour the gradient has there.
                    std::vector<GradientStop> sorted = st;
                    std::stable_sort(sorted.begin(), sorted.end(), [](auto& p, auto& q) { return p.pos < q.pos; });
                    float p0 = st[static_cast<size_t>(sel)].pos, p1 = 1.f;
                    for (const GradientStop& o : sorted)
                        if (o.pos > p0) { p1 = o.pos; break; }
                    float pos = p0 < 1.f ? (p0 + p1) / 2.f : p0 - 0.1f;
                    st.insert(st.begin() + sel + 1, GradientStop{std::clamp(pos, 0.f, 1.f), sampleStops(sorted, pos)});
                    refreshStops(dlg, *s, sel + 1);
                    apply(*s);
                    return TRUE;
                }
                case IdStopRemove:
                    if (st.size() <= 2) return TRUE;
                    st.erase(st.begin() + sel);
                    refreshStops(dlg, *s, sel);
                    apply(*s);
                    return TRUE;
                case IdStopReverse:
                    for (GradientStop& o : st) o.pos = 1.f - o.pos;
                    refreshStops(dlg, *s, sel);
                    apply(*s);
                    return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

}  // namespace

void appendCorrectionMenu(HMENU m) {
    using K = Adjustment::Kind;
    for (K k : {K::BrightnessContrast, K::Levels, K::HueSaturation, K::Invert, K::GradientMap})
        AppendMenuW(m, MF_STRING, static_cast<UINT_PTR>(CmdAdjustBase + static_cast<int>(k)), widen(Adjustment::kindName(k)).c_str());
}

void layerSettingsDialog() {
    Layer* l = g.state.active();
    if (!l || l->isRaster()) {
        g.state.status = "Settings are for fill, gradient and correction layers";
        invalidateAll();
        return;
    }
    if (l->locked) {
        g.state.status = "The layer is locked";
        invalidateAll();
        return;
    }
    if (l->kind == LayerKind::Fill) {
        Color c = l->fillColor;
        if (pickColorDialog(c)) {
            g.state.setFillColor(c);
            updateComposite(g.state.doc.bounds());
            contentChanged();
        }
        return;
    }
    static bool controlsReady = false;
    if (!controlsReady) {
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES};
        controlsReady = InitCommonControlsEx(&icc) != FALSE;
    }

    Session s;
    s.kind = l->kind;
    using K = Adjustment::Kind;
    const wchar_t* note = nullptr;
    if (l->kind == LayerKind::Gradient) {
        s.grad = l->gradient;
        s.stops = &s.grad.stops;
    } else {
        s.adj = l->adjustment;
        Adjustment& a = s.adj;
        switch (a.kind) {
            case K::BrightnessContrast: s.sliders = {{L"Brightness", -100, 100, &a.a}, {L"Contrast", -100, 100, &a.b}}; break;
            case K::HueSaturation:
                s.sliders = {{L"Hue", -180, 180, &a.a}, {L"Saturation", -100, 100, &a.b}, {L"Luminosity", -100, 100, &a.c}};
                break;
            case K::Levels:
                // Files can hold separate R, G and B levels; the sliders edit the master (RGB) levels.
                if (!a.raw.empty()) note = L"Editing replaces any per-channel levels read from the file with these.";
                s.sliders = {{L"Input shadow", 0, 255, &a.a}, {L"Gamma point", 0, 255, &a.b}, {L"Input highlight", 0, 255, &a.c},
                             {L"Output shadow", 0, 255, &a.d}, {L"Output highlight", 0, 255, &a.e}};
                break;
            case K::GradientMap:
                if (a.stops.empty()) a.stops = Adjustment::make(K::GradientMap).stops;
                s.stops = &a.stops;
                break;
            case K::Invert: note = L"Reverse gradient has no settings."; break;
            default: note = L"This correction can be shown and saved, but not edited here."; break;
        }
    }

    // Layout (dialog units).
    const short W = 250, pad = 8;
    short y = pad;
    std::vector<std::function<void(DlgBuilder&)>> items;
    WORD count = 2;  // OK, Cancel
    DlgBuilder b;
    auto add = [&](auto fn) { items.push_back(fn); ++count; };
    if (s.kind == LayerKind::Gradient) {
        const short yy = y;
        add([=](DlgBuilder& d) { d.item(SS_LEFT, pad, yy + 2, 40, 10, 0xFFFF, DlgBuilder::Static, L"Shape:"); });
        add([=](DlgBuilder& d) { d.item(CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 50, yy, 80, 60, IdShape, DlgBuilder::ComboBox, L""); });
        y += 20;
    }
    for (size_t i = 0; i < s.sliders.size(); ++i) {
        const short yy = y;
        const WORD id = static_cast<WORD>(i);
        add([=](DlgBuilder& d) { d.item(SS_LEFT, pad, yy + 4, 92, 10, static_cast<WORD>(IdSliderText + id), DlgBuilder::Static, L""); });
        add([=](DlgBuilder& d) {
            d.item(TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 102, yy, W - 102 - pad, 16, static_cast<WORD>(IdSlider + id), TRACKBAR_CLASSW, L"");
        });
        y += 20;
    }
    if (s.stops) {
        const short yy = y;
        add([=](DlgBuilder& d) { d.item(SS_LEFT, pad, yy, 120, 10, 0xFFFF, DlgBuilder::Static, L"Color stops (position, color):"); });
        add([=](DlgBuilder& d) {
            d.item(LBS_NOTIFY | WS_BORDER | WS_VSCROLL | WS_TABSTOP, pad, yy + 12, 150, 72, IdStops, DlgBuilder::ListBox, L"");
        });
        const short bx = pad + 156, bw = W - bx - pad;
        add([=](DlgBuilder& d) { d.item(BS_PUSHBUTTON | WS_TABSTOP, bx, yy + 12, bw, 14, IdStopColor, DlgBuilder::Button, L"Color..."); });
        add([=](DlgBuilder& d) { d.item(BS_PUSHBUTTON | WS_TABSTOP, bx, yy + 28, bw, 14, IdStopAdd, DlgBuilder::Button, L"Add stop"); });
        add([=](DlgBuilder& d) { d.item(BS_PUSHBUTTON | WS_TABSTOP, bx, yy + 44, bw, 14, IdStopRemove, DlgBuilder::Button, L"Remove stop"); });
        add([=](DlgBuilder& d) { d.item(BS_PUSHBUTTON | WS_TABSTOP, bx, yy + 60, bw, 14, IdStopReverse, DlgBuilder::Button, L"Reverse"); });
        add([=](DlgBuilder& d) { d.item(SS_LEFT, pad, yy + 90, 60, 10, 0xFFFF, DlgBuilder::Static, L"Position (%):"); });
        add([=](DlgBuilder& d) { d.item(ES_NUMBER | WS_BORDER | WS_TABSTOP, 70, yy + 88, 40, 13, IdStopPos, DlgBuilder::Edit, L""); });
        y += 106;
    }
    if (note) {
        const short yy = y;
        add([=](DlgBuilder& d) { d.item(SS_LEFT, pad, yy, W - 2 * pad, 20, IdNote, DlgBuilder::Static, note); });
        y += 22;
    }
    y += 4;
    const std::wstring title = widen(l->name) + L" - " +
                               (s.kind == LayerKind::Gradient ? std::wstring(L"Gradient") : widen(Adjustment::kindName(s.adj.kind)));
    b.begin(title.c_str(), W, static_cast<short>(y + 22), count);
    for (auto& fn : items) fn(b);
    b.item(BS_DEFPUSHBUTTON | WS_TABSTOP, W - 2 * (50 + pad) + pad, y, 50, 16, IDOK, DlgBuilder::Button, L"OK");
    b.item(BS_PUSHBUTTON | WS_TABSTOP, W - 50 - pad, y, 50, 16, IDCANCEL, DlgBuilder::Button, L"Cancel");
    DialogBoxIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<LPCDLGTEMPLATEW>(b.buf.data()), g.hwnd, settingsProc,
                            reinterpret_cast<LPARAM>(&s));
    invalidateAll();
}

}  // namespace pu::app::ui
