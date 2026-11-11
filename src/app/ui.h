// Internal header shared by the Win32 UI translation units (main_win32.cpp, ui_*.cpp).
// Everything lives in pu::app::ui; App `g` holds the window's state and Painter `P` the
// GDI+ context of the paint in progress.
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app_state.h"
#include "png.h"
#include "ui_icons.h"


namespace pu::app::ui {

namespace gp = Gdiplus;

inline constexpr float kPi = 3.14159265358979f;

// ---- theme -------------------------------------------------------------------
struct Theme {
    COLORREF frame, palette, header, tab, tabActive, buttonHover, buttonOn, field, accent, selection, text, textDim, border,
        canvasBg, menuHover;
};
inline const Theme kDark{RGB(38, 38, 38),  RGB(57, 57, 57),    RGB(45, 45, 45),    RGB(48, 48, 48), RGB(62, 62, 62),
                  RGB(80, 80, 80),  RGB(46, 98, 160),   RGB(36, 36, 36),    RGB(66, 134, 214), RGB(56, 88, 132),
                  RGB(226, 226, 226), RGB(148, 148, 148), RGB(28, 28, 28), RGB(32, 32, 32),  RGB(70, 70, 70)};
inline const Theme kLight{RGB(205, 205, 205), RGB(238, 238, 238), RGB(222, 222, 222), RGB(212, 212, 212), RGB(238, 238, 238),
                   RGB(210, 220, 234), RGB(160, 196, 238), RGB(255, 255, 255), RGB(52, 120, 210),  RGB(186, 210, 242),
                   RGB(32, 32, 32),    RGB(108, 108, 108), RGB(168, 168, 168), RGB(136, 136, 136), RGB(214, 224, 238)};

// ---- palettes and docks ---------------------------------------------------------
enum Pal { PalTool, PalSubTool, PalToolProperty, PalBrushSize, PalColorWheel, PalColorSlider, PalColorSet, PalNavigator, PalLayer,
           PalColorHistory, PalHistory, PalLayerProperty, PalCount };
inline const wchar_t* const kPalNames[PalCount] = {L"Tool",        L"Sub Tool",    L"Tool Property", L"Brush Size", L"Color Wheel",
                                            L"Color Slider", L"Color Set", L"Navigator",     L"Layer",      L"Color History",
                                            L"History",      L"Layer Property"};

struct Dock {
    std::vector<Pal> tabs;
    int active = 0;
    int height = 0;  // pixels, for fixed docks
    bool flex = false;
};

// ---- commands -------------------------------------------------------------------
enum Cmd : int {
    CmdNew = 100, CmdOpen, CmdSave, CmdSaveAs, CmdExportPng, CmdClose, CmdExit,
    CmdUndo, CmdRedo, CmdClear, CmdFill, CmdInvert, CmdFlipLayerH, CmdFlipLayerV,
    CmdLayerNew, CmdLayerDup, CmdLayerDelete, CmdLayerRename, CmdLayerClip, CmdLayerLock, CmdLayerLockAlpha,
    CmdLayerMerge, CmdLayerMergeVisible, CmdLayerSelectUp, CmdLayerSelectDown, CmdLayerMoveUp, CmdLayerMoveDown,
    CmdFilterBlur, CmdFilterBlurStrong,
    CmdZoomIn, CmdZoomOut, CmdZoom100, CmdZoomFit, CmdRotateLeft, CmdRotateRight, CmdFlipView, CmdResetView, CmdPaper,
    CmdHideAll, CmdCommandBar, CmdThemeDark, CmdThemeLight,
    CmdShortcuts, CmdAbout,
    CmdSelectAll, CmdDeselect, CmdInvertSelection, CmdClearOutside, CmdLayerProperty,
    CmdPalBase = 300,    // + Pal
    CmdBlendBase = 400,  // + BlendMode
};

inline const wchar_t* const kBlendNames[] = {L"Normal",     L"Multiply",   L"Screen",     L"Overlay",    L"Darken",     L"Lighten",
                                      L"Color dodge", L"Color burn", L"Hard light", L"Soft light", L"Difference", L"Exclusion",
                                      L"Add",        L"Subtract",   L"Hue",        L"Saturation", L"Color",      L"Brightness"};
static_assert(sizeof(kBlendNames) / sizeof(kBlendNames[0]) == static_cast<size_t>(BlendMode::Count));

// ---- hits -------------------------------------------------------------------------
struct Hit {
    RECT r{};
    std::wstring tip;
    LPCWSTR cursor = IDC_ARROW;
    std::function<void(int, int)> press, drag, rpress;
    std::function<void()> release, dbl;
    std::function<void(int)> wheel;
};

enum class CanvasDrag { None, Paint, Pan, Rotate, Zoom, Pick, BrushSize, Gradient, MoveLayer, Select };

struct Layout {
    RECT client, menu, cmd, tool, left, right, leftSplit, rightSplit, tab, view, nav;
};

struct App {
    HWND hwnd = nullptr;
    AppState state;
    Surface composite;
    float scale = 1.f;
    const Theme* th = &kDark;
    bool dark = true;
    ULONG_PTR gdipToken = 0;

    // Back buffer for the whole client area and the canvas image as a DIB.
    HDC backDC = nullptr;
    HBITMAP backBmp = nullptr;
    HGDIOBJ backOld = nullptr;
    int backW = 0, backH = 0;
    HDC canvasDC = nullptr;
    HBITMAP canvasBmp = nullptr;
    HGDIOBJ canvasOld = nullptr;
    uint8_t* canvasBits = nullptr;
    int canvasW = 0, canvasH = 0;

    std::unique_ptr<gp::FontFamily> family;
    std::unique_ptr<gp::Font> font, fontBold, fontSmall;
    HFONT editFont = nullptr;
    HBRUSH editBrush = nullptr;

    // UI interaction
    std::vector<Hit> hits;
    std::function<void(int, int)> dragFn;
    std::function<void()> releaseFn;
    bool uiCapture = false;
    POINT mouse{-10000, -10000};
    bool mouseInside = false;
    RECT hoverRect{};
    std::wstring hoverTip;
    bool tipShown = false;
    RECT tipRect{};
    DWORD lastPressTime = 0;
    POINT lastPressPt{};
    RECT lastPressRect{};
    std::vector<std::function<void()>> deferred;
    std::vector<std::function<void()>> afterRelease;  // popups waiting for the pointer to lift

    // Canvas interaction
    CanvasDrag canvasDrag = CanvasDrag::None;
    POINT dragStart{};
    std::vector<std::pair<float, float>> lasso;  // canvas points of the lasso being drawn
    SelectMode selectMode = SelectMode::Replace;
    uint64_t antsVersion = ~0ull;                // selection version the outline cache was built for
    std::vector<std::pair<POINT, POINT>> antsSegs;   // canvas-space boundary edges of the selection
    int antsPhase = 0;
    View dragView;
    float dragSize = 0.f;
    bool dragMoved = false;
    SubKind dragKind = SubKind::Paint;
    bool spaceDown = false;
    UINT32 activePointer = 0;
    bool pointerDown = false;
    RECT cursorRect{};

    // Layout
    bool palVisible[PalCount];
    int historyScroll = 0;
    bool hideAll = false;
    bool showCommandBar = true;
    int leftW = 0, rightW = 0;
    std::vector<Dock> leftDocks, rightDocks;
    int layerScroll = 0, brushSizeScroll = 0, colorSetScroll = 0, subToolScroll = 0;
    int layerDropSlot = -1;
    bool userView = false;

    // Colour editing (HSV of the colour being edited, so hue survives grey).
    float hue = 0.f, sat = 0.f, val = 0.f;
    std::vector<Color> colorSet;

    // Caches
    bool thumbsDirty = true, navDirty = true;
    std::map<uint64_t, std::unique_ptr<gp::Bitmap>> thumbs;
    std::unique_ptr<gp::Bitmap> navThumb;
    int navThumbW = 0, navThumbH = 0;
    std::unique_ptr<gp::Bitmap> wheelRing;
    int wheelRingSize = 0;
    COLORREF wheelRingBg = 0;
    std::unique_ptr<gp::Bitmap> svSquare;
    int svSize = 0;
    float svHue = -1.f;
    struct Preview {
        size_t key = 0;
        std::unique_ptr<gp::Bitmap> bmp;
    };
    std::map<std::pair<int, int>, Preview> previews;

    // Menus
    HMENU menus[8]{};  // indexed by Menu
    std::vector<RECT> menuTitles;
    int menuOpen = -1, menuNext = -1;
    HHOOK menuHook = nullptr;

    // Inline layer rename
    HWND edit = nullptr;
    WNDPROC editProc = nullptr;
    uint64_t renameId = 0;
};

enum Menu { MenuFile, MenuEdit, MenuLayer, MenuSelect, MenuFilter, MenuView, MenuWindow, MenuHelp, kMenuCount };
inline const wchar_t* const kMenuNames[kMenuCount] = {L"File", L"Edit", L"Layer", L"Select", L"Filter", L"View", L"Window", L"Help"};

extern App g;

inline int S(float v) { return static_cast<int>(std::lround(v * g.scale)); }
inline const Theme& T() { return *g.th; }

// ---- painting ------------------------------------------------------------------
struct Painter {
    gp::Graphics* gx = nullptr;
    RECT clip{};
};
extern Painter P;

enum Align { Left, Center, Right };

struct SliderSpec {
    std::wstring label;
    float value = 0, lo = 0, hi = 1;
    int decimals = 0;
    bool log = false;
    std::wstring suffix;
    std::function<void(float)> set;
    std::function<void()> begin, end;
    float wheelStep = 0;  // 0: 1/50 of the range
};

struct DockPlace {
    int index;
    RECT r;
};

// Screen rect of the layer being renamed (set by the Layer palette, read by startRename).
extern RECT g_renameRect;

// ---- functions, in definition order --------------------------------------------
std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& w);
std::wstring fmt(const wchar_t* f, double v);
std::wstring baseName(const std::string& path);
RECT rc(int l, int t, int r, int b);
RECT inset(RECT r, int d);
int W(const RECT& r);
int H(const RECT& r);
bool keyDown(int vk);
void rgbToHsv(Color c, float& h, float& s, float& v);
Color hsvToRgb(float h, float s, float v);
Color editColor();
void syncHsv();
void setEditColor(Color c);
void invalidateAll();
void invalidate(const RECT& r);
bool palShown(Pal p);
void setTitle();
void defer(std::function<void()> fn);
void deferPopup(std::function<void()> fn);
void flushAfterRelease();
void ensureCanvasDib();
void updateComposite(Rect r);
void contentChanged();
RECT windowBoundsOfCanvasRect(Rect r);
void invalidateCanvas(Rect r);
POINT viewCenter();
void fitView();
void zoomAt(float wx, float wy, float newZoom);
void zoomStep(int dir, float wx, float wy);
void zoomStepCenter(int dir);
void setRotation(float radians);
void toggleFlipView();
bool need(const RECT& r);
gp::Color gc(COLORREF c, BYTE a = 255);
gp::Color gc(Color c);
gp::RectF rf(const RECT& r);
COLORREF mix(COLORREF a, COLORREF b, float t);
void fillRect(const RECT& r, COLORREF c);
void fillRectC(const RECT& r, Color c);
void frameRect(const RECT& r, COLORREF c, int w = 1);
void roundRect(const RECT& r, COLORREF c, int radiusPx);
void checker(const RECT& r, int cell);
void text(const RECT& r, const std::wstring& s, COLORREF c, Align a = Left, gp::Font* f = nullptr, bool wrap = false);
float textWidth(const std::wstring& s, gp::Font* f = nullptr);
void icon(Icon ic, const RECT& r, COLORREF c, int pad);
bool hovered(const RECT& r);
Hit& addHit(const RECT& r, std::wstring tip = {});
int button(const RECT& r, Icon ic, const std::wstring& tip, std::function<void()> fn, bool on = false, bool enabled = true);
float sliderT(const SliderSpec& s, float v);
float sliderV(const SliderSpec& s, float t);
void slider(const RECT& r, const SliderSpec& s, int labelW);
std::unique_ptr<gp::Bitmap> makeBitmap(int w, int h, const std::function<void(uint32_t*, int stride)>& fill);
uint32_t argb(uint8_t a, uint8_t r, uint8_t gg, uint8_t b);
std::unique_ptr<gp::Bitmap> thumbnail(const Surface& s, int w, int h, bool checkerBg);
bool dockVisible(const Dock& d);
bool columnVisible(const std::vector<Dock>& docks);
std::vector<DockPlace> placeDocks(std::vector<Dock>& docks, const RECT& col, std::vector<RECT>* splitters);
Layout computeLayout();
void paintMenuBar(const RECT& r);
void paintCommandBar(const RECT& r);
void colorChips(const RECT& area, bool compact);
void paintToolStrip(const RECT& r);
gp::Bitmap* subPreview(int gi, int si, int w, int h, COLORREF col);
void paintSubTool(const RECT& r);
void paintToolProperty(const RECT& r);
void paintBrushSize(const RECT& r);
void applyHsv();
void paintColorWheel(const RECT& r);
void paintColorSlider(const RECT& r);
void paintColorSet(const RECT& r);
void paintColorHistory(const RECT& r);
void paintHistory(const RECT& r);
void paintLayerProperty(const RECT& r);
void showPalette(Pal p);
void paintNavigator(const RECT& r);
void blendMenu();
void layerContextMenu();
void paintLayer(const RECT& r);
void paintPalette(Pal p, const RECT& r);
void paintDock(Dock& d, const RECT& r);
void paintColumn(std::vector<Dock>& docks, const RECT& col);
void paintCanvas(HDC dc, const RECT& view, const RECT& clip);
void paintCanvasOverlay(const RECT& view);
void paintCanvasTab(const RECT& r);
void paintNavBar(const RECT& r);
void paintTooltip(const RECT& client);
void ensureBackBuffer(HDC dc, int w, int h);
void onPaint();
std::string fileDialog(bool save, bool pngOnly = false);
void documentReplaced();
bool doSave(bool forcePrompt);
bool confirmDiscard();
void doOpen(const std::string& path);
INT_PTR CALLBACK newDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp);
void doNew();
LRESULT CALLBACK editWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void startRename();
void commitRename(bool apply);
void buildMenus();
void updateMenuStates();
int menuTitleAt(POINT p);
LRESULT CALLBACK menuHookProc(int code, WPARAM wp, LPARAM lp);
void openMenu(int index);
void selectTool(int index);
BrushSettings effectiveBrush();
SubKind effectiveKind();
void pickColor(int x, int y, bool layerOnly);
void canvasPress(int x, int y, float pressure, int button);
void canvasMove(int x, int y, float pressure);
void canvasRelease();
void layerOp(void (AppState::*op)());
void showShortcuts();
void applyTheme();
void exec(int cmd);
bool isChar(WPARAM vk, wchar_t ch);
void toolKey(std::initializer_list<Tool> tools);
bool onKey(WPARAM vk, bool repeat);
int hitAt(POINT p);
void hideTip();
void onPress(int x, int y, float pressure, int button);
void updateHover(int x, int y);
void onMove(int x, int y, float pressure);
void onRelease();
void onWheel(int x, int y, int delta);
float pointerPressure(WPARAM wp);
LPCWSTR canvasCursor();
LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void initUi(HWND hwnd);

}  // namespace pu::app::ui
