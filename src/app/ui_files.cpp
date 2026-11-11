// File open/save/new (including the New canvas dialog) and inline layer rename.
#include "ui.h"

namespace pu::app::ui {

// ---- files ---------------------------------------------------------------------------------------------------------
std::string fileDialog(bool save, bool pngOnly) {
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = pngOnly ? L"PNG (*.png)\0*.png\0"
                      : save ? L"CLIP STUDIO FORMAT (*.clip)\0*.clip\0PNG (*.png)\0*.png\0"
                             : L"Images (*.clip;*.png)\0*.clip;*.png\0CLIP STUDIO FORMAT (*.clip)\0*.clip\0PNG (*.png)\0*.png\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST) : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    ofn.lpstrDefExt = pngOnly ? L"png" : L"clip";
    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? narrow(buf) : std::string();
}

void documentReplaced() {
    g.thumbs.clear();
    g.layerScroll = 0;
    updateComposite(g.state.doc.bounds());
    g.userView = false;
    fitView();
    g.thumbsDirty = g.navDirty = true;
    setTitle();
    invalidateAll();
}

bool doSave(bool forcePrompt) {
    std::string path = g.state.filePath;
    if (forcePrompt || path.empty()) path = fileDialog(true);
    if (path.empty()) return false;
    std::string err;
    bool ok = g.state.saveFile(path, &err);
    if (!ok) MessageBoxW(g.hwnd, widen(err).c_str(), L"Save failed", MB_ICONERROR);
    setTitle();
    invalidateAll();
    return ok;
}

bool confirmDiscard() {
    if (!g.state.dirty) return true;
    int r = MessageBoxW(g.hwnd, L"Do you want to save changes to the canvas?", L"Plastic Paint", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return false;
    if (r == IDNO) return true;
    return doSave(false);
}

void doOpen(const std::string& path) {
    std::string err;
    if (!g.state.openFile(path, &err)) {
        MessageBoxW(g.hwnd, widen(err).c_str(), L"Open failed", MB_ICONERROR);
        return;
    }
    documentReplaced();
}

// New canvas dialog, built from an in-memory template.
struct DlgBuilder {
    std::vector<WORD> buf;
    void align() { if (buf.size() % 2) buf.push_back(0); }
    void dw(DWORD v) { buf.push_back(LOWORD(v)); buf.push_back(HIWORD(v)); }
    void str(const wchar_t* s) { while (*s) buf.push_back(*s++); buf.push_back(0); }
    void begin(const wchar_t* title, short w, short h, WORD count) {
        dw(DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU);
        dw(0);
        buf.push_back(count);
        buf.push_back(0); buf.push_back(0); buf.push_back(static_cast<WORD>(w)); buf.push_back(static_cast<WORD>(h));
        buf.push_back(0);  // menu
        buf.push_back(0);  // class
        str(title);
        buf.push_back(9);
        str(L"Segoe UI");
    }
    void item(DWORD style, short x, short y, short w, short h, WORD id, WORD cls, const wchar_t* t) {
        align();
        dw(style | WS_CHILD | WS_VISIBLE);
        dw(0);
        buf.push_back(static_cast<WORD>(x)); buf.push_back(static_cast<WORD>(y)); buf.push_back(static_cast<WORD>(w)); buf.push_back(static_cast<WORD>(h));
        buf.push_back(id);
        buf.push_back(0xFFFF);
        buf.push_back(cls);
        str(t);
        buf.push_back(0);
    }
};

struct NewCanvas {
    int w = 1200, h = 900, dpi = 350;
};

INT_PTR CALLBACK newDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            auto* nc = reinterpret_cast<NewCanvas*>(lp);
            SetDlgItemInt(dlg, 101, static_cast<UINT>(nc->w), FALSE);
            SetDlgItemInt(dlg, 102, static_cast<UINT>(nc->h), FALSE);
            SetDlgItemInt(dlg, 103, static_cast<UINT>(nc->dpi), FALSE);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                auto* nc = reinterpret_cast<NewCanvas*>(GetWindowLongPtrW(dlg, DWLP_USER));
                BOOL a, b, c;
                int w = static_cast<int>(GetDlgItemInt(dlg, 101, &a, FALSE));
                int h = static_cast<int>(GetDlgItemInt(dlg, 102, &b, FALSE));
                int d = static_cast<int>(GetDlgItemInt(dlg, 103, &c, FALSE));
                if (!a || !b || !c || w < 1 || h < 1 || w > 8000 || h > 8000 || d < 1 || d > 4000) {
                    MessageBoxW(dlg, L"Width and height must be 1 to 8000 px; resolution 1 to 4000 dpi.", L"New", MB_ICONWARNING);
                    return TRUE;
                }
                nc->w = w; nc->h = h; nc->dpi = d;
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
            break;
    }
    return FALSE;
}

void doNew() {
    if (!confirmDiscard()) return;
    static NewCanvas nc;
    DlgBuilder b;
    b.begin(L"New", 180, 112, 8);
    const WORD Static = 0x0082, Edit = 0x0081, Button = 0x0080;
    b.item(SS_LEFT, 10, 12, 60, 10, 0xFFFF, Static, L"Width (px):");
    b.item(ES_NUMBER | WS_BORDER | WS_TABSTOP, 80, 10, 88, 13, 101, Edit, L"");
    b.item(SS_LEFT, 10, 32, 60, 10, 0xFFFF, Static, L"Height (px):");
    b.item(ES_NUMBER | WS_BORDER | WS_TABSTOP, 80, 30, 88, 13, 102, Edit, L"");
    b.item(SS_LEFT, 10, 52, 60, 10, 0xFFFF, Static, L"Resolution (dpi):");
    b.item(ES_NUMBER | WS_BORDER | WS_TABSTOP, 80, 50, 88, 13, 103, Edit, L"");
    b.item(BS_DEFPUSHBUTTON | WS_TABSTOP, 62, 86, 50, 16, IDOK, Button, L"OK");
    b.item(BS_PUSHBUTTON | WS_TABSTOP, 118, 86, 50, 16, IDCANCEL, Button, L"Cancel");
    if (DialogBoxIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<LPCDLGTEMPLATEW>(b.buf.data()), g.hwnd, newDlgProc,
                                reinterpret_cast<LPARAM>(&nc)) != IDOK)
        return;
    g.state.newDocument(nc.w, nc.h, nc.dpi);
    documentReplaced();
}

// ---- inline rename ----------------------------------------------------------------------------------------------------
LRESULT CALLBACK editWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) { commitRename(true); return 0; }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) { commitRename(false); return 0; }
    if (msg == WM_CHAR && (wp == L'\r' || wp == 27)) return 0;
    if (msg == WM_KILLFOCUS) { LRESULT r = CallWindowProcW(g.editProc, hwnd, msg, wp, lp); defer([] { commitRename(true); }); return r; }
    return CallWindowProcW(g.editProc, hwnd, msg, wp, lp);
}

void startRename() {
    Layer* l = g.state.active();
    if (!l || g.edit) return;
    // Make sure the active row is visible so its rect is current.
    invalidateAll();
    UpdateWindow(g.hwnd);
    RECT r = g_renameRect;
    if (IsRectEmpty(&r)) return;
    g.renameId = l->id;
    g.edit = CreateWindowExW(0, L"EDIT", widen(l->name).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, r.left, r.top, W(r), H(r), g.hwnd,
                             nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g.edit, WM_SETFONT, reinterpret_cast<WPARAM>(g.editFont), TRUE);
    g.editProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g.edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(editWndProc)));
    SendMessageW(g.edit, EM_SETSEL, 0, -1);
    SetFocus(g.edit);
}

void commitRename(bool apply) {
    if (!g.edit) return;
    HWND e = g.edit;
    g.edit = nullptr;
    if (apply) {
        int n = GetWindowTextLengthW(e);
        std::wstring s(static_cast<size_t>(n) + 1, L'\0');
        GetWindowTextW(e, s.data(), n + 1);
        s.resize(static_cast<size_t>(n));
        int idx = g.state.doc.indexOf(g.renameId);
        if (idx >= 0 && !s.empty() && narrow(s) != g.state.doc.layer(static_cast<size_t>(idx))->name) {
            size_t prev = g.state.doc.activeIndex();
            g.state.doc.setActiveIndex(static_cast<size_t>(idx));
            g.state.renameLayer(narrow(s));
            g.state.doc.setActiveIndex(prev);
        }
    }
    DestroyWindow(e);
    SetFocus(g.hwnd);
    setTitle();
    invalidateAll();
}

}  // namespace pu::app::ui
