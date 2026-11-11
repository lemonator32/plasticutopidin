// Document model: a canvas with an ordered stack of raster layers.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "blend.h"
#include "image.h"

namespace pu {

struct Layer {
    std::string name = "Layer";
    Surface surface;             // canvas-sized pixel data
    BlendMode mode = BlendMode::Normal;
    float opacity = 1.f;         // 0..1
    bool visible = true;
    bool locked = false;
    bool lockAlpha = false;      // "lock transparent pixels": painting keeps the layer's existing alpha
    bool clipToBelow = false;    // clipping layer: only paints where the layer below is opaque
    uint64_t id = 0;             // stable identity for undo commands

    // Layer Property effects (display only; the pixels are unchanged).
    bool gray = false;                                   // expression color: gray
    bool layerColorOn = false;                           // "Layer color": dark tones show in layerColor
    Color layerColor = Color::rgba(40, 120, 230);
    bool borderOn = false;                               // "Border effect": an outline around the layer's pixels
    int borderWidth = 4;                                 // pixels, 1..50
    Color borderColor = Color::rgba(255, 255, 255);
    bool hasEffects() const { return gray || layerColorOn || (borderOn && borderWidth > 0); }

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

    size_t activeIndex() const { return active_; }
    void setActiveIndex(size_t i) { active_ = layers_.empty() ? 0 : std::min(i, layers_.size() - 1); }
    Layer* activeLayer() { return layer(active_); }

    // Render the visible layer stack (optionally over the paper colour) into `out`,
    // limited to `clip` in canvas coordinates. `out` must be canvas-sized.
    void composite(Surface& out, Rect clip) const;
    // A layer's pixels within `clip` as displayed: with its Layer Property effects applied.
    Surface layerDisplay(const Layer& l, Rect clip) const;
    Surface flatten() const;

    uint64_t nextId() { return ++idCounter_; }

private:
    int w_ = 0, h_ = 0;
    std::vector<std::unique_ptr<Layer>> layers_;
    size_t active_ = 0;
    uint64_t idCounter_ = 0;
};

}  // namespace pu
