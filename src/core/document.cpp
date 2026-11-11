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

namespace {

// Sliding-window maximum of `n` values (stride `step`) over a window of radius r, in O(n).
void maxFilter1D(const uint8_t* src, uint8_t* dst, int n, int step, int r) {
    std::vector<int> dq(static_cast<size_t>(n));
    int head = 0, tail = 0, next = 0;
    for (int i = 0; i < n; ++i) {
        // Push indices up to i + r.
        for (; next <= std::min(n - 1, i + r); ++next) {
            while (tail > head && src[dq[static_cast<size_t>(tail - 1)] * step] <= src[next * step]) --tail;
            dq[static_cast<size_t>(tail++)] = next;
        }
        while (dq[static_cast<size_t>(head)] < i - r) ++head;
        dst[i * step] = src[dq[static_cast<size_t>(head)] * step];
    }
}

}  // namespace

Surface Document::layerDisplay(const Layer& l, Rect clip) const {
    Surface tmp = l.surface.crop(clip);
    if (l.gray || l.layerColorOn) {
        for (int y = 0; y < clip.h; ++y) {
            Color* p = tmp.row(y);
            for (int x = 0; x < clip.w; ++x) {
                if (!p[x].a) continue;
                int lum = (p[x].r * 299 + p[x].g * 587 + p[x].b * 114) / 1000;
                if (l.layerColorOn) {
                    // Black shows as the layer colour, white stays white, as in Clip Studio's layer color.
                    auto ch = [lum](uint8_t c) { return static_cast<uint8_t>(c + (255 - c) * lum / 255); };
                    p[x] = Color::rgba(ch(l.layerColor.r), ch(l.layerColor.g), ch(l.layerColor.b), p[x].a);
                } else {
                    p[x] = Color::rgba(static_cast<uint8_t>(lum), static_cast<uint8_t>(lum), static_cast<uint8_t>(lum), p[x].a);
                }
            }
        }
    }
    if (l.borderOn && l.borderWidth > 0) {
        // Dilate the alpha by the border width (square-ish max filter over a margin around the clip),
        // then put the border colour under the layer's own pixels.
        int r = std::clamp(l.borderWidth, 1, 50);
        Rect m = Rect::fromLTRB(clip.x - r, clip.y - r, clip.right() + r, clip.bottom() + r).intersect(bounds());
        std::vector<uint8_t> a(static_cast<size_t>(m.w) * m.h), b(a.size());
        for (int y = 0; y < m.h; ++y) {
            const Color* s = l.surface.row(m.y + y) + m.x;
            for (int x = 0; x < m.w; ++x) a[static_cast<size_t>(y) * m.w + x] = s[x].a;
        }
        for (int y = 0; y < m.h; ++y) maxFilter1D(&a[static_cast<size_t>(y) * m.w], &b[static_cast<size_t>(y) * m.w], m.w, 1, r);
        for (int x = 0; x < m.w; ++x) maxFilter1D(&b[static_cast<size_t>(x)], &a[static_cast<size_t>(x)], m.h, m.w, r);
        for (int y = 0; y < clip.h; ++y) {
            Color* p = tmp.row(y);
            const uint8_t* d = &a[static_cast<size_t>(clip.y + y - m.y) * m.w + (clip.x - m.x)];
            for (int x = 0; x < clip.w; ++x) {
                Color under = l.borderColor;
                under.a = d[x];
                p[x] = blendPixel(under, p[x], BlendMode::Normal, 1.f);
            }
        }
    }
    return tmp;
}

void Document::composite(Surface& out, Rect clip) const {
    clip = clip.intersect(bounds());
    if (clip.empty()) return;
    out.fillRect(clip, paperVisible ? paperColor : Color{});
    for (size_t i = 0; i < layers_.size(); ++i) {
        const Layer& l = *layers_[i];
        if (!l.visible || l.opacity <= 0.f) continue;
        bool clipped = l.clipToBelow && i > 0;
        if (clipped || l.hasEffects()) {
            Surface tmp = layerDisplay(l, clip);
            if (!clipped) {
                blendSurface(out, tmp, clip.x, clip.y, l.mode, l.opacity, clip);
                continue;
            }
            // Clip the layer's alpha by the alpha of the layer below, then composite.
            const Layer& below = *layers_[i - 1];
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
