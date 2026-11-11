#include "document.h"

#include <algorithm>

namespace pu {

Layer* Document::findLayer(uint64_t id) {
    for (auto& l : layers_)
        if (l->id == id) return l.get();
    return nullptr;
}

int Document::indexOf(uint64_t id) const {
    for (size_t i = 0; i < layers_.size(); ++i)
        if (layers_[i]->id == id) return static_cast<int>(i);
    return -1;
}

Layer* Document::addLayer(const std::string& name, size_t index) {
    auto l = std::make_unique<Layer>();
    l->name = name;
    l->surface.reset(w_, h_);
    l->id = nextId();
    Layer* raw = l.get();
    insertLayer(std::move(l), index);
    return raw;
}

void Document::insertLayer(std::unique_ptr<Layer> layer, size_t index) {
    if (layer->id == 0) layer->id = nextId();
    idCounter_ = std::max(idCounter_, layer->id);
    index = std::min(index, layers_.size());
    layers_.insert(layers_.begin() + static_cast<std::ptrdiff_t>(index), std::move(layer));
    active_ = index;
}

std::unique_ptr<Layer> Document::removeLayer(size_t index) {
    if (index >= layers_.size()) return nullptr;
    auto l = std::move(layers_[index]);
    layers_.erase(layers_.begin() + static_cast<std::ptrdiff_t>(index));
    if (!layers_.empty()) active_ = std::min(active_, layers_.size() - 1);
    else active_ = 0;
    return l;
}

void Document::moveLayer(size_t from, size_t to) {
    if (from >= layers_.size() || to >= layers_.size() || from == to) return;
    auto l = std::move(layers_[from]);
    layers_.erase(layers_.begin() + static_cast<std::ptrdiff_t>(from));
    layers_.insert(layers_.begin() + static_cast<std::ptrdiff_t>(to), std::move(l));
    active_ = to;
}

void Document::composite(Surface& out, Rect clip) const {
    clip = clip.intersect(bounds());
    if (clip.empty()) return;
    out.fillRect(clip, paperVisible ? paperColor : Color{});
    for (size_t i = 0; i < layers_.size(); ++i) {
        const Layer& l = *layers_[i];
        if (!l.visible || l.opacity <= 0.f) continue;
        if (l.clipToBelow && i > 0) {
            // Clip the layer's alpha by the alpha of the layer below, then composite.
            const Layer& below = *layers_[i - 1];
            Surface tmp = l.surface.crop(clip);
            for (int y = 0; y < clip.h; ++y) {
                Color* t = tmp.row(y);
                const Color* b = below.surface.row(clip.y + y) + clip.x;
                for (int x = 0; x < clip.w; ++x) t[x].a = static_cast<uint8_t>(t[x].a * b[x].a / 255);
            }
            blendSurface(out, tmp, clip.x, clip.y, l.mode, l.opacity, clip);
        } else {
            blendSurface(out, l.surface, 0, 0, l.mode, l.opacity, clip);
        }
    }
}

Surface Document::flatten() const {
    Surface out(w_, h_);
    composite(out, bounds());
    return out;
}

}  // namespace pu
