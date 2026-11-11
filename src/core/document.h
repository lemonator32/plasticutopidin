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
    bool clipToBelow = false;    // clipping layer: only paints where the layer below is opaque
    uint64_t id = 0;             // stable identity for undo commands

    std::unique_ptr<Layer> clone() const {
        auto l = std::make_unique<Layer>(*this);
        return l;
    }
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
    Surface flatten() const;

    uint64_t nextId() { return ++idCounter_; }

private:
    int w_ = 0, h_ = 0;
    std::vector<std::unique_ptr<Layer>> layers_;
    size_t active_ = 0;
    uint64_t idCounter_ = 0;
};

}  // namespace pu
