#include "document.h"

#include <algorithm>
#include <cmath>

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

namespace {

void rgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
    float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    l = (mx + mn) * 0.5f;
    if (mx == mn) {
        h = s = 0.f;
        return;
    }
    float d = mx - mn;
    s = l > 0.5f ? d / (2.f - mx - mn) : d / (mx + mn);
    if (mx == r) h = (g - b) / d + (g < b ? 6.f : 0.f);
    else if (mx == g) h = (b - r) / d + 2.f;
    else h = (r - g) / d + 4.f;
    h /= 6.f;
}

float hue2rgb(float p, float q, float t) {
    if (t < 0.f) t += 1.f;
    if (t > 1.f) t -= 1.f;
    if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
    if (t < 0.5f) return q;
    if (t < 2.f / 3.f) return p + (q - p) * (2.f / 3.f - t) * 6.f;
    return p;
}

uint8_t u8(float v) { return static_cast<uint8_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); }

inline uint8_t mul255(int a, int b) { return static_cast<uint8_t>((a * b + 127) / 255); }

void identity(std::vector<uint8_t>& t) {
    t.resize(256);
    for (int i = 0; i < 256; ++i) t[i] = static_cast<uint8_t>(i);
}

// A 1024-entry colour table for a gradient: sampling is then one lookup per pixel.
std::vector<Color> gradientTable(const std::vector<GradientStop>& stops) {
    std::vector<Color> t(1024);
    for (int i = 0; i < 1024; ++i) t[static_cast<size_t>(i)] = sampleStops(stops, i / 1023.f);
    return t;
}

}  // namespace

Color sampleStops(const std::vector<GradientStop>& stops, float t) {
    if (stops.empty()) return Color{};
    if (t <= stops.front().pos) return stops.front().color;
    if (t >= stops.back().pos) return stops.back().color;
    for (size_t k = 1; k < stops.size(); ++k) {
        if (t > stops[k].pos) continue;
        float span = stops[k].pos - stops[k - 1].pos;
        float f = span > 0.f ? (t - stops[k - 1].pos) / span : 0.f;
        const Color a = stops[k - 1].color, b = stops[k].color;
        auto mix = [f](uint8_t x, uint8_t y) { return static_cast<uint8_t>(x + (y - x) * f + 0.5f); };
        return Color::rgba(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a));
    }
    return stops.back().color;
}

const char* layerKindName(LayerKind k) {
    switch (k) {
        case LayerKind::Raster: return "Raster";
        case LayerKind::Fill: return "Fill";
        case LayerKind::Gradient: return "Gradient";
        case LayerKind::Adjustment: return "Correction";
        case LayerKind::Folder: return "Folder";
    }
    return "?";
}

const char* Adjustment::kindName(Kind k) {
    switch (k) {
        case Kind::None: return "None";
        case Kind::BrightnessContrast: return "Brightness/Contrast";
        case Kind::Levels: return "Level correction";
        case Kind::ToneCurve: return "Tone curve";
        case Kind::HueSaturation: return "Hue/Saturation/Luminosity";
        case Kind::ColorBalance: return "Color balance";
        case Kind::Invert: return "Reverse gradient";
        case Kind::GradientMap: return "Gradient map";
    }
    return "?";
}

Adjustment Adjustment::make(Kind k) {
    Adjustment a;
    a.kind = k;
    switch (k) {
        case Kind::Levels: a.a = 0; a.b = 128; a.c = 255; a.d = 0; a.e = 255; break;
        case Kind::GradientMap: a.stops = {{0.f, Color::rgba(0, 0, 0)}, {1.f, Color::rgba(255, 255, 255)}}; break;
        default: break;
    }
    a.rebuild();
    return a;
}

void Adjustment::rebuild() {
    switch (kind) {
        case Kind::Levels: {
            // Input black/white stretch, then a gamma that maps the gamma point
            // to mid grey, then the output range.  Same curve on R, G and B.
            for (auto& t : lut) t.resize(256);
            float ib = a / 255.f, mid = b / 255.f, iw = c / 255.f, ob = d / 255.f, ow = e / 255.f;
            float frac = iw > ib ? std::clamp((mid - ib) / (iw - ib), 0.01f, 0.99f) : 0.5f;
            float gamma = std::log(0.5f) / std::log(frac);
            for (int i = 0; i < 256; ++i) {
                float v = iw > ib ? std::clamp((i / 255.f - ib) / (iw - ib), 0.f, 1.f) : (i / 255.f >= ib ? 1.f : 0.f);
                v = std::pow(v, gamma);
                lut[0][i] = lut[1][i] = lut[2][i] = u8(ob + (ow - ob) * v);
            }
            break;
        }
        case Kind::GradientMap: {
            for (auto& t : lut) t.resize(256);
            lutAlpha.resize(256);
            for (int i = 0; i < 256; ++i) {
                Color col = sampleStops(stops, i / 255.f);
                lut[0][i] = col.r;
                lut[1][i] = col.g;
                lut[2][i] = col.b;
                lutAlpha[i] = col.a;
            }
            break;
        }
        case Kind::ToneCurve:
        case Kind::ColorBalance:
            for (auto& t : lut)
                if (t.size() != 256) identity(t);
            break;
        default: break;
    }
}

Color Adjustment::apply(Color px) const {
    switch (kind) {
        case Kind::None: return px;
        case Kind::Invert: return Color::rgba(255 - px.r, 255 - px.g, 255 - px.b, px.a);
        case Kind::BrightnessContrast: {
            // Brightness shifts by up to +-50%; contrast scales around the midpoint.
            float br = a / 200.f;
            float ct = b >= 0 ? 1.f / std::max(0.01f, 1.f - b / 100.f) : 1.f + b / 100.f;
            auto f = [&](uint8_t v) { return u8((v / 255.f + br - 0.5f) * ct + 0.5f); };
            return Color::rgba(f(px.r), f(px.g), f(px.b), px.a);
        }
        case Kind::HueSaturation: {
            float h, s, l;
            rgbToHsl(px.r / 255.f, px.g / 255.f, px.b / 255.f, h, s, l);
            h = h + a / 360.f;
            h -= std::floor(h);
            s = std::clamp(s * (1.f + b / 100.f), 0.f, 1.f);
            if (c > 0) l = l + (1.f - l) * c / 100.f;
            else l = l * (1.f + c / 100.f);
            if (s <= 0.f) return Color::rgba(u8(l), u8(l), u8(l), px.a);
            float q = l < 0.5f ? l * (1.f + s) : l + s - l * s, p = 2.f * l - q;
            return Color::rgba(u8(hue2rgb(p, q, h + 1.f / 3.f)), u8(hue2rgb(p, q, h)), u8(hue2rgb(p, q, h - 1.f / 3.f)), px.a);
        }
        case Kind::GradientMap: {
            if (lut[0].size() < 256) return px;
            int y = (px.r * 299 + px.g * 587 + px.b * 114 + 500) / 1000;
            Color m = Color::rgba(lut[0][y], lut[1][y], lut[2][y], px.a);
            if (lutAlpha.size() == 256 && lutAlpha[y] < 255) {
                int t = lutAlpha[y];
                m.r = static_cast<uint8_t>((m.r * t + px.r * (255 - t)) / 255);
                m.g = static_cast<uint8_t>((m.g * t + px.g * (255 - t)) / 255);
                m.b = static_cast<uint8_t>((m.b * t + px.b * (255 - t)) / 255);
            }
            return m;
        }
        default:  // table-driven
            if (lut[0].size() < 256 || lut[1].size() < 256 || lut[2].size() < 256) return px;
            return Color::rgba(lut[0][px.r], lut[1][px.g], lut[2][px.b], px.a);
    }
}

namespace {

// Fills `out` (clip-sized) with a gradient layer's colours.
void renderGradient(const GradientFill& g, Rect clip, Surface& out) {
    const std::vector<Color> table = gradientTable(g.stops);
    const float dx = g.x1 - g.x0, dy = g.y1 - g.y0, len2 = dx * dx + dy * dy, len = std::sqrt(len2);
    for (int y = 0; y < clip.h; ++y) {
        Color* o = out.row(y);
        const float py = clip.y + y + 0.5f - g.y0;
        for (int x = 0; x < clip.w; ++x) {
            const float px = clip.x + x + 0.5f - g.x0;
            float t;
            if (len2 <= 0.f) t = 0.f;
            else if (g.shape == GradientFill::Shape::Radial) t = std::sqrt(px * px + py * py) / len;
            else t = (px * dx + py * dy) / len2;
            o[x] = table[static_cast<size_t>(std::clamp(t, 0.f, 1.f) * 1023.f + 0.5f)];
        }
    }
}

}  // namespace

// ---- folder structure -----------------------------------------------------------

size_t Document::blockStart(size_t i) const {
    if (i >= layers_.size() || !layers_[i]->isFolder()) return i;
    const int d = layers_[i]->depth;
    size_t j = i;
    while (j > 0 && layers_[j - 1]->depth > d) --j;
    return j;
}

int Document::parentOf(size_t i) const {
    if (i >= layers_.size()) return -1;
    const int d = layers_[i]->depth;
    for (size_t j = i + 1; j < layers_.size(); ++j)
        if (layers_[j]->depth < d) return layers_[j]->isFolder() ? static_cast<int>(j) : -1;
    return -1;
}

bool Document::shownInTree(size_t i) const {
    for (int k = static_cast<int>(i); k >= 0; k = parentOf(static_cast<size_t>(k)))
        if (!layers_[static_cast<size_t>(k)]->visible) return false;
    return i < layers_.size();
}

std::vector<std::pair<uint64_t, int>> Document::order() const {
    std::vector<std::pair<uint64_t, int>> o;
    for (const auto& l : layers_) o.push_back({l->id, l->depth});
    return o;
}

void Document::setOrder(const std::vector<std::pair<uint64_t, int>>& order) {
    const uint64_t activeId = active_ < layers_.size() ? layers_[active_]->id : 0;
    std::vector<std::unique_ptr<Layer>> next;
    for (auto [id, depth] : order)
        for (auto& l : layers_)
            if (l && l->id == id) {
                l->depth = depth;
                next.push_back(std::move(l));
                break;
            }
    for (auto& l : layers_)
        if (l) next.push_back(std::move(l));  // not in `order` (should not happen): keep on top
    layers_ = std::move(next);
    int a = indexOf(activeId);
    if (a >= 0) active_ = static_cast<size_t>(a);
}

const Layer* Document::clipBase(size_t i) const {
    // A clipping layer clips to the base of its group: the nearest lower
    // sibling (same folder) that is not itself clipping.
    if (i >= layers_.size() || !layers_[i]->clipToBelow) return nullptr;
    const int d = layers_[i]->depth;
    size_t j = blockStart(i);
    while (j > 0) {
        --j;
        if (layers_[j]->depth != d) return nullptr;  // reached the edge of the folder
        if (!layers_[j]->clipToBelow) return layers_[j].get();
        j = blockStart(j);
    }
    return nullptr;
}

// ---- rendering ----------------------------------------------------------------------

namespace {

// Multiplies `t`'s alpha by what the mask shows, `t` covering `clip`.
void applyMask(Surface& t, const Layer& l, Rect clip) {
    if (!l.maskApplies()) return;
    for (int y = 0; y < clip.h; ++y) {
        Color* p = t.row(y);
        const Color* m = l.mask.row(clip.y + y) + clip.x;
        for (int x = 0; x < clip.w; ++x) p[x].a = mul255(p[x].a, 255 - m[x].a);
    }
}

}  // namespace

Surface Document::renderLayer(const Layer& l, Rect clip) const {
    Surface tmp;
    switch (l.kind) {
        case LayerKind::Raster: tmp = layerDisplay(l, clip); break;
        case LayerKind::Fill: tmp.reset(clip.w, clip.h, l.fillColor); break;
        case LayerKind::Gradient:
            tmp.reset(clip.w, clip.h);
            renderGradient(l.gradient, clip, tmp);
            break;
        case LayerKind::Adjustment: return Surface(clip.w, clip.h);
        case LayerKind::Folder: {
            int i = indexOf(l.id);
            tmp = i >= 0 ? renderFolder(static_cast<size_t>(i), clip) : Surface(clip.w, clip.h);
            break;
        }
    }
    applyMask(tmp, l, clip);
    return tmp;
}

void Document::coverageRow(const Layer& l, int y, int x0, int w, uint8_t* out) const {
    switch (l.kind) {
        case LayerKind::Raster: {
            const Color* s = l.surface.row(y) + x0;
            for (int x = 0; x < w; ++x) out[x] = s[x].a;
            break;
        }
        case LayerKind::Gradient: {
            Surface tmp(w, 1);
            renderGradient(l.gradient, Rect{x0, y, w, 1}, tmp);
            for (int x = 0; x < w; ++x) out[x] = tmp.row(0)[x].a;
            break;
        }
        case LayerKind::Folder: {
            Surface tmp = renderLayer(l, Rect{x0, y, w, 1});  // includes the mask
            for (int x = 0; x < w; ++x) out[x] = tmp.row(0)[x].a;
            return;
        }
        default: std::fill(out, out + w, uint8_t{255}); break;
    }
    if (l.maskApplies()) {
        const Color* m = l.mask.row(y) + x0;
        for (int x = 0; x < w; ++x) out[x] = mul255(out[x], 255 - m[x].a);
    }
}

std::vector<uint8_t> Document::coverage(size_t i, Rect clip) const {
    std::vector<uint8_t> c(static_cast<size_t>(clip.w) * clip.h);
    const Layer& l = *layers_[i];
    if (l.isFolder()) {
        Surface t = renderLayer(l, clip);
        for (int y = 0; y < clip.h; ++y)
            for (int x = 0; x < clip.w; ++x) c[static_cast<size_t>(y) * clip.w + x] = t.row(y)[x].a;
        return c;
    }
    for (int y = 0; y < clip.h; ++y) coverageRow(l, clip.y + y, clip.x, clip.w, &c[static_cast<size_t>(y) * clip.w]);
    return c;
}

Surface Document::renderFolder(size_t i, Rect clip) const {
    Surface t(clip.w, clip.h);
    compositeRange(t, clip, blockStart(i), i);
    return t;
}

void Document::compositeRange(Surface& dst, Rect clip, size_t lo, size_t hi) const {
    // The items of this level, bottom to top: each is the top index of a block.
    std::vector<size_t> items;
    for (size_t k = hi; k > lo;) {
        size_t top = k - 1;
        items.push_back(top);
        k = blockStart(top);
    }
    std::reverse(items.begin(), items.end());

    // The current clipping base: clipping layers above it show only where it does.
    size_t base = SIZE_MAX;
    bool baseShown = true;
    std::vector<uint8_t> baseCover;  // computed when a clipping layer first needs it
    for (size_t i : items) {
        const Layer& l = *layers_[i];
        const bool clipped = l.clipToBelow && base != SIZE_MAX;
        if (!clipped) {
            base = i;
            baseShown = l.visible;
            baseCover.clear();
        }
        if (!l.visible || l.opacity <= 0.f || (clipped && !baseShown)) continue;
        if (clipped && baseCover.empty()) baseCover = coverage(base, clip);
        auto clipTo = [&](Surface& t) {
            if (!clipped) return;
            for (int y = 0; y < clip.h; ++y) {
                Color* p = t.row(y);
                const uint8_t* c = &baseCover[static_cast<size_t>(y) * clip.w];
                for (int x = 0; x < clip.w; ++x) p[x].a = mul255(p[x].a, c[x]);
            }
        };

        if (l.isFolder() && l.passThrough && !clipped) {
            // Through: the folder's layers blend straight into the backdrop; its
            // opacity and mask then fade between the backdrop and the result.
            const bool fade = l.opacity < 1.f || l.maskApplies();
            Surface before = fade ? dst : Surface();
            compositeRange(dst, clip, blockStart(i), i);
            if (fade)
                for (int y = 0; y < clip.h; ++y) {
                    Color* d = dst.row(y);
                    const Color* b = before.row(y);
                    for (int x = 0; x < clip.w; ++x) {
                        int t = static_cast<int>(l.opacity * l.maskShown(clip.x + x, clip.y + y) + 0.5f);
                        auto mix = [t](uint8_t p, uint8_t q) { return static_cast<uint8_t>(p + (q - p) * t / 255); };
                        d[x] = Color::rgba(mix(b[x].r, d[x].r), mix(b[x].g, d[x].g), mix(b[x].b, d[x].b), mix(b[x].a, d[x].a));
                    }
                }
            continue;
        }
        // Fast path: a plain raster layer blends straight from its pixels.
        if (l.isRaster() && !clipped && !l.hasEffects() && !l.maskApplies()) {
            blendSurface(dst, l.surface, -clip.x, -clip.y, l.mode, l.opacity, dst.bounds());
            continue;
        }
        Surface tmp;
        if (l.kind == LayerKind::Adjustment) {
            // Recolour what is below (within this folder); the mask decides where.
            tmp.reset(clip.w, clip.h);
            for (int y = 0; y < clip.h; ++y) {
                const Color* s = dst.row(y);
                Color* t = tmp.row(y);
                for (int x = 0; x < clip.w; ++x) t[x] = l.adjustment.apply(s[x]);
            }
            applyMask(tmp, l, clip);
        } else {
            tmp = renderLayer(l, clip);  // folders: their contents on their own
        }
        clipTo(tmp);
        blendSurface(dst, tmp, 0, 0, l.mode, l.opacity, dst.bounds());
    }
}

void Document::composite(Surface& out, Rect clip) const {
    clip = clip.intersect(bounds());
    if (clip.empty()) return;
    Surface dst(clip.w, clip.h, paperVisible ? paperColor : Color{});
    compositeRange(dst, clip, 0, layers_.size());
    out.blit(dst, clip.x, clip.y);
}

Surface Document::flatten() const {
    Surface out(w_, h_);
    composite(out, bounds());
    return out;
}

}  // namespace pu
