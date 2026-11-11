// Platform-independent application state: document, tools, view transform.
#pragma once
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "brush.h"
#include "document.h"
#include "undo.h"

namespace pu::app {

// Tool groups.  The values match ui_icons' Icon order; the Tool palette shows
// the groups in the order of defaultToolGroups().
enum class Tool { Zoom, Hand, Rotate, Eyedropper, Pen, Pencil, Brush, Airbrush, Eraser, Fill, MoveLayer, Gradient, Selection, Count };

// What a sub tool does when it is applied to the canvas.
enum class SubKind { ZoomIn, ZoomOut, Hand, Rotate, PickDisplay, PickLayer, Paint, Erase, FillRefAll, FillLayer, MoveLayer, GradientToSub, GradientToTransparent,
                     SelectRect, SelectEllipse, SelectLasso };

// How a new selection shape combines with the existing selection.
enum class SelectMode { Replace, Add, Subtract };

// A sub tool: a named preset inside a tool group.  Each keeps its own
// settings, as in Clip Studio Paint, so switching sub tools restores them.
struct SubTool {
    std::string name;
    SubKind kind = SubKind::Paint;
    BrushSettings brush;
    bool sizePressure = true;     // pen pressure changes the size
    bool opacityPressure = false; // pen pressure changes the opacity
    int tolerance = 10;           // fill: colour margin, 0..100
    int expand = 1;               // fill: grow the filled area by this many pixels
};

struct ToolGroup {
    Tool tool;
    const char* name;
    const char* shortcut;          // shown in tooltips/menus
    std::vector<SubTool> subs;
    int active = 0;
};

std::vector<ToolGroup> defaultToolGroups();

// The steps of the Brush Size palette (and of the [ ] keys).
const std::vector<float>& brushSizeSteps();

enum class ColorSlot { Main, Sub, Transparent };

// Canvas-to-window transform: window = R(rotation) * S(zoom) * F(flip) * canvas + pan.
struct View {
    float zoom = 1.f;
    float rotation = 0.f;  // radians
    bool flipH = false;
    float panX = 0.f, panY = 0.f;

    void windowFromCanvas(float cx, float cy, float& wx, float& wy) const {
        float x = flipH ? -cx : cx;
        float c = std::cos(rotation), s = std::sin(rotation);
        wx = (x * c - cy * s) * zoom + panX;
        wy = (x * s + cy * c) * zoom + panY;
    }
    void canvasFromWindow(float wx, float wy, float& cx, float& cy) const {
        float x = (wx - panX) / zoom, y = (wy - panY) / zoom;
        float c = std::cos(rotation), s = std::sin(rotation);
        float ux = x * c + y * s, uy = -x * s + y * c;
        cx = flipH ? -ux : ux;
        cy = uy;
    }
    // Change zoom/rotation/flip while keeping canvas point under window point (wx, wy) fixed.
    void setKeeping(float wx, float wy, float newZoom, float newRotation, bool newFlip) {
        float cx, cy;
        canvasFromWindow(wx, wy, cx, cy);
        zoom = newZoom;
        rotation = newRotation;
        flipH = newFlip;
        float nx, ny;
        windowFromCanvas(cx, cy, nx, ny);
        panX += wx - nx;
        panY += wy - ny;
    }
};

class AppState {
public:
    AppState();

    Document doc;
    std::unique_ptr<UndoStack> undo;
    std::vector<ToolGroup> tools;
    int toolIndex = 5;            // Pen
    std::vector<Color> colorHistory;  // most recent first (Color History palette)
    Color mainColor = Color::rgba(0, 0, 0);
    Color subColor = Color::rgba(255, 255, 255);
    ColorSlot colorSlot = ColorSlot::Main;
    View view;
    std::string filePath;   // last opened/saved path
    bool dirty = false;
    std::string status;     // one-line status message

    ToolGroup& group() { return tools[static_cast<size_t>(toolIndex)]; }
    SubTool& sub() { ToolGroup& g = group(); return g.subs[static_cast<size_t>(g.active)]; }
    Color drawColor() const { return colorSlot == ColorSlot::Sub ? subColor : mainColor; }
    void setDrawColor(Color c) { if (colorSlot == ColorSlot::Sub) subColor = c; else { mainColor = c; if (colorSlot == ColorSlot::Transparent) colorSlot = ColorSlot::Main; } }

    void newDocument(int w, int h, double dpi = 350.0);
    bool openFile(const std::string& path, std::string* error);
    bool saveFile(const std::string& path, std::string* error);   // .clip or .png by extension

    // Stroke lifecycle (canvas coordinates).  `bs` is the effective brush for this stroke.
    void beginStroke(float x, float y, float pressure, const BrushSettings& bs, const char* label);
    void continueStroke(float x, float y, float pressure);
    Rect endStroke();  // returns the modified rect
    bool strokeActive() const { return stroke_ != nullptr; }
    Rect strokeDirty() const { return stroke_ ? stroke_->lastDirtyRect() : Rect{}; }

    // Flood fill at (x, y) on the active layer.  `reference` is the surface
    // whose colours bound the area (the composite, or the layer itself).
    Rect floodFill(int x, int y, const Surface& reference, int tolerance, int expand, Color color, bool erase);

    // Linear gradient from (x0, y0) to (x1, y1) on the active layer, from `a` to `b`.
    Rect drawGradient(float x0, float y0, float x1, float y1, Color a, Color b, float opacity);

    // Move layer tool: the layer shows shifted by (dx, dy) during the drag; end commits one undo step.
    bool beginLayerMove();
    void previewLayerMove(int dx, int dy);
    void endLayerMove();
    bool layerMoveActive() const { return moveLayer_ != 0; }

    // Selection (all undoable).  Shapes are in canvas coordinates.
    void selectRect(float x0, float y0, float x1, float y1, SelectMode mode);
    void selectEllipse(float x0, float y0, float x1, float y1, SelectMode mode);
    void selectPolygon(const std::vector<std::pair<float, float>>& pts, SelectMode mode);
    void selectAll();
    void deselect();
    void invertSelection();
    bool hasSelection() const { return doc.selection.active; }
    void clearOutsideSelection();

    // Sub tool management (Sub Tool palette).
    void duplicateSubTool();
    void deleteSubTool();
    bool resetSubTool();  // restore the active sub tool's initial settings

    // Records a colour used for drawing in the Color History.
    void noteColorUsed(Color c);

    // History palette: undo/redo until `doneCount` steps are done.
    void historyJump(size_t doneCount);

    // Layer operations (all undoable)
    void addLayer();
    void deleteLayer();
    void duplicateLayer();
    void moveLayer(int delta);
    void moveLayerTo(size_t from, size_t to);
    void selectLayer(int delta);
    void toggleVisible(size_t index);
    void toggleVisible() { toggleVisible(doc.activeIndex()); }
    void cycleBlendMode(int delta);
    void setBlendMode(BlendMode m);
    void adjustOpacity(float delta);
    void toggleClip();
    void toggleLock();
    void toggleLockAlpha();
    void renameLayer(const std::string& name);
    void clearLayer();
    void fillLayer(Color c);
    void mergeDown();
    void mergeVisible();

    // Live property edits (e.g. dragging the opacity slider) become one undo step.
    void beginPropertyEdit();
    void endPropertyEdit();

    // Pixel operations on the active layer.
    void flipLayer(bool horizontal);
    void invertLayer();
    void blurLayer(int radius);

    Layer* active() { return doc.activeLayer(); }

private:
    void pushProps(Layer& l, const LayerPropertyCommand::Props& before);
    bool editableLayer(Layer*& l);
    void replaceLayerPixels(Layer& l, Surface after, const char* label, bool masked = true);
    void applyAlphaLock(Rect r);
    void setSelection(std::vector<uint8_t> shape, SelectMode mode, const char* label);
    Selection moveSelOriginal_;
    std::unique_ptr<Stroke> stroke_;
    uint64_t strokeLayer_ = 0;
    std::string strokeLabel_;
    std::optional<std::pair<uint64_t, LayerPropertyCommand::Props>> propEdit_;
    uint64_t moveLayer_ = 0;
    Surface moveOriginal_;
    int moveDx_ = 0, moveDy_ = 0;
    std::vector<ToolGroup> initialTools_ = defaultToolGroups();
};

}  // namespace pu::app
