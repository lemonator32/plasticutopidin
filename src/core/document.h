// Document model: a canvas with an ordered stack of raster layers.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "blend.h"
#include "image.h"

namespace pu {

// A colour stop of a gradient: position 0..1 along the gradient.
struct GradientStop {
    float pos = 0.f;
    Color color;
    bool operator==(const GradientStop&) const = default;
};

// Samples sorted stops at t (0..1), interpolating straight RGBA.
Color sampleStops(const std::vector<GradientStop>& stops, float t);

// The settings of a gradient layer.  The gradient runs from (x0, y0) to
// (x1, y1) in canvas pixels: across that line (Linear) or out from (x0, y0)
// with radius |(x1, y1) - (x0, y0)| (Radial).  Beyond the ends the end colours continue.
struct GradientFill {
    enum class Shape : uint8_t { Linear, Radial };
    Shape shape = Shape::Linear;
    std::vector<GradientStop> stops{{0.f, Color::rgba(0, 0, 0)}, {1.f, Color::rgba(255, 255, 255)}};
    float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
    bool operator==(const GradientFill&) const = default;
};

// A correction ("filter") layer: instead of pixels, it recolours everything
// composited below it.  The adjusted result is blended over the stack with
// the layer's blend mode and opacity, through the layer mask if it has one.
struct Adjustment {
    enum class Kind : uint8_t {
        None,
        BrightnessContrast,  // a = brightness, b = contrast (-100..100)
        Levels,              // a, b, c = input black, gamma point, input white (0..255); d, e = output black, white
        ToneCurve,           // lut only (read from files)
        HueSaturation,       // a = hue (degrees, -180..180), b = saturation, c = luminosity (-100..100)
        ColorBalance,        // lut only (read from files)
        Invert,
        GradientMap,         // stops (indexed by the pixel's luminance)
    };
    Kind kind = Kind::None;
    int a = 0, b = 0, c = 0, d = 0, e = 0;
    std::vector<GradientStop> stops;  // GradientMap
    // Per-channel lookup tables (R, G, B) for the table-driven kinds; for
    // GradientMap the index is the pixel's luminance.  Built by rebuild().
    std::vector<uint8_t> lut[3];
    std::vector<uint8_t> lutAlpha;  // GradientMap: the stop alpha
    // The parameters as Clip Studio Paint stored them (FilterLayerInfo), kept
    // so a file round-trips exactly.  Cleared when the parameters are edited.
    std::vector<uint8_t> raw;

    bool active() const { return kind != Kind::None; }
    // Can the parameters be edited (and written) from a, b, c, d, e and stops?
    bool editable() const { return kind != Kind::ToneCurve && kind != Kind::ColorBalance; }
    // Regenerates the lookup tables from the parameters (the editable kinds).
    void rebuild();
    Color apply(Color c) const;
    bool operator==(const Adjustment& o) const {
        return kind == o.kind && a == o.a && b == o.b && c == o.c && d == o.d && e == o.e && stops == o.stops && raw == o.raw &&
               lut[0] == o.lut[0] && lut[1] == o.lut[1] && lut[2] == o.lut[2] && lutAlpha == o.lutAlpha;
    }
    static Adjustment make(Kind k);  // default settings for a new correction layer
    static const char* kindName(Kind k);
};

// What a layer holds.  Only raster layers have pixels; the others are
// generated from their settings (and are painted on only through a mask).
// A folder holds the layers below it that are nested one level deeper.
enum class LayerKind : uint8_t { Raster, Fill, Gradient, Adjustment, Folder };
const char* layerKindName(LayerKind k);

struct Layer {
    std::string name = "Layer";
    LayerKind kind = LayerKind::Raster;
    Surface surface;             // canvas-sized pixel data (raster layers; empty otherwise)
    Color fillColor = Color::rgba(255, 255, 255);  // Fill layers
    GradientFill gradient;       // Gradient layers
    Adjustment adjustment;       // Correction layers
    // Layer mask: canvas-sized, empty = no mask.  A pixel's ALPHA is how much
    // of the layer it hides (0 shown .. 255 hidden), so painting on the mask
    // masks and erasing reveals, as in Clip Studio Paint, and every pixel tool
    // works on it unchanged.
    Surface mask;
    bool maskEnabled = true;
    bool editingMask = false;    // pixel tools work on the mask (UI state, not saved)
    BlendMode mode = BlendMode::Normal;
    float opacity = 1.f;         // 0..1
    bool visible = true;
    bool locked = false;
    bool lockAlpha = false;      // "lock transparent pixels": painting keeps the layer's existing alpha
    bool clipToBelow = false;    // clipping layer: only paints where the layer below is opaque
    uint64_t id = 0;             // stable identity for undo commands

    // Folders.  The stack is flat: a folder sits directly ABOVE its contents,
    // which are the layers below it with a greater depth (a contiguous run).
    int depth = 0;               // nesting level, 0 = not in a folder
    bool passThrough = false;    // folder: blend its layers straight into what is below ("Through")
    bool expanded = true;        // folder: its layers are listed in the Layer palette (UI state)

    // Layer Property effects (display only; the pixels are unchanged).
    bool gray = false;                                   // expression color: gray
    bool layerColorOn = false;                           // "Layer color": dark tones show in layerColor
    Color layerColor = Color::rgba(40, 120, 230);
    bool borderOn = false;                               // "Border effect": an outline around the layer's pixels
    int borderWidth = 4;                                 // pixels, 1..50
    Color borderColor = Color::rgba(255, 255, 255);
    bool hasEffects() const { return gray || layerColorOn || (borderOn && borderWidth > 0); }

    bool isRaster() const { return kind == LayerKind::Raster; }
    bool isFolder() const { return kind == LayerKind::Folder; }
    bool hasMask() const { return !mask.empty(); }
    bool maskApplies() const { return hasMask() && maskEnabled; }
    // How much of the layer shows at (x, y) through its mask: 255 = all.
    uint8_t maskShown(int x, int y) const { return maskApplies() ? static_cast<uint8_t>(255 - mask.at(x, y).a) : 255; }
    // True when pixel tools work on the mask rather than on the layer pixels.
    bool paintsMask() const { return hasMask() && (editingMask || !isRaster()); }
    // The surface pixel tools change: the mask or the layer pixels.
    Surface& paintTarget() { return paintsMask() ? mask : surface; }
    const Surface& paintTarget() const { return paintsMask() ? mask : surface; }

    std::unique_ptr<Layer> clone() const {
        auto l = std::make_unique<Layer>(*this);
        return l;
    }
};

// The selected area: a canvas-sized coverage mask (0 = outside, 255 = inside).
// Inactive means "no selection": everything is editable.
struct Selection {
    bool active = false;
    Rect bounds;                 // bounding box of the non-zero mask
    std::vector<uint8_t> mask;   // width * height when active
    int width = 0;
    uint64_t version = 0;        // changes whenever the mask does (for cached outlines)

    uint8_t at(int x, int y) const { return active ? mask[static_cast<size_t>(y) * width + x] : 255; }
    bool contains(int x, int y) const { return at(x, y) != 0; }
};

class Document {
public:
    Document() = default;
    Document(int w, int h) : w_(w), h_(h) {}

    int width() const { return w_; }
    int height() const { return h_; }
    Rect bounds() const { return Rect{0, 0, w_, h_}; }
    double dpi = 350.0;
    Color paperColor = Color::rgba(255, 255, 255);
    bool paperVisible = true;
    Selection selection;

    // Layer stack. Index 0 is the bottom-most layer.
    size_t layerCount() const { return layers_.size(); }
    Layer* layer(size_t i) { return i < layers_.size() ? layers_[i].get() : nullptr; }
    const Layer* layer(size_t i) const { return i < layers_.size() ? layers_[i].get() : nullptr; }
    Layer* findLayer(uint64_t id);
    int indexOf(uint64_t id) const;

    Layer* addLayer(const std::string& name, size_t index);  // inserts at index (clamped), returns it
    Layer* addLayerTop(const std::string& name) { return addLayer(name, layers_.size()); }
    std::unique_ptr<Layer> removeLayer(size_t index);
    void insertLayer(std::unique_ptr<Layer> layer, size_t index);
    void moveLayer(size_t from, size_t to);

    // Folder structure.  blockStart(i) is the lowest index of layer i's block
    // (its contents when it is a folder, else i itself); the block is
    // [blockStart(i), i].  parentOf(i) is the folder containing layer i, or -1.
    size_t blockStart(size_t i) const;
    int parentOf(size_t i) const;
    // Visible, and so are all the folders it is in.
    bool shownInTree(size_t i) const;
    // The whole stack's order and nesting, for undoing structural edits.
    std::vector<std::pair<uint64_t, int>> order() const;
    void setOrder(const std::vector<std::pair<uint64_t, int>>& order);

    size_t activeIndex() const { return active_; }
    void setActiveIndex(size_t i) { active_ = layers_.empty() ? 0 : std::min(i, layers_.size() - 1); }
    Layer* activeLayer() { return layer(active_); }

    // Render the visible layer stack (optionally over the paper colour) into `out`,
    // limited to `clip` in canvas coordinates. `out` must be canvas-sized.
    void composite(Surface& out, Rect clip) const;
    // A layer's pixels within `clip` as displayed: with its Layer Property effects applied.
    Surface layerDisplay(const Layer& l, Rect clip) const;
    // What a layer contributes within `clip`, before opacity and blending: its
    // pixels with effects (raster), its colour (fill) or gradient, through its
    // mask.  Correction layers have no content of their own (transparent).
    Surface renderLayer(const Layer& l, Rect clip) const;
    // One row of a layer's coverage (alpha through its mask) from x0, w pixels:
    // what clipping layers above it are clipped to.
    void coverageRow(const Layer& l, int y, int x0, int w, uint8_t* out) const;
    Surface flatten() const;
    // The layer a clipping layer at index i clips to (its nearest lower
    // sibling that is not clipping), or nullptr.
    const Layer* clipBase(size_t i) const;

    uint64_t nextId() { return ++idCounter_; }

private:
    // Composites the items of one level within [lo, hi) into `dst`, which
    // covers `clip` (clip-sized, origin at clip.x, clip.y).
    void compositeRange(Surface& dst, Rect clip, size_t lo, size_t hi) const;
    // A folder's contents composited on their own (transparent backdrop), clip-sized.
    Surface renderFolder(size_t i, Rect clip) const;
    // Coverage (alpha through the mask) of the item at index i, clip-sized, row-major.
    std::vector<uint8_t> coverage(size_t i, Rect clip) const;

    int w_ = 0, h_ = 0;
    std::vector<std::unique_ptr<Layer>> layers_;
    size_t active_ = 0;
    uint64_t idCounter_ = 0;
};

}  // namespace pu
