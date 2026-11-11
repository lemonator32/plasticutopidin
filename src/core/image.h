// Basic raster types: colours, rectangles and an 8-bit RGBA surface.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace pu {

struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 0;
    static Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return Color{r, g, b, a}; }
    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const Color& o) const { return !(*this == o); }
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool empty() const { return w <= 0 || h <= 0; }
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    static Rect fromLTRB(int l, int t, int r, int b) { return Rect{l, t, r - l, b - t}; }
    Rect intersect(const Rect& o) const {
        int l = std::max(x, o.x), t = std::max(y, o.y);
        int r = std::min(right(), o.right()), b = std::min(bottom(), o.bottom());
        if (r <= l || b <= t) return Rect{};
        return fromLTRB(l, t, r, b);
    }
    Rect unite(const Rect& o) const {
        if (empty()) return o;
        if (o.empty()) return *this;
        return fromLTRB(std::min(x, o.x), std::min(y, o.y), std::max(right(), o.right()), std::max(bottom(), o.bottom()));
    }
    bool contains(int px, int py) const { return px >= x && py >= y && px < right() && py < bottom(); }
};

// Straight-alpha (non-premultiplied) 8-bit RGBA surface, row-major, no padding.
class Surface {
public:
    Surface() = default;
    Surface(int w, int h, Color fill = Color{}) { reset(w, h, fill); }

    void reset(int w, int h, Color fill = Color{}) {
        w_ = std::max(0, w);
        h_ = std::max(0, h);
        px_.assign(static_cast<size_t>(w_) * h_, fill);
    }
    int width() const { return w_; }
    int height() const { return h_; }
    Rect bounds() const { return Rect{0, 0, w_, h_}; }
    bool empty() const { return w_ == 0 || h_ == 0; }

    Color* row(int y) { return px_.data() + static_cast<size_t>(y) * w_; }
    const Color* row(int y) const { return px_.data() + static_cast<size_t>(y) * w_; }
    Color& at(int x, int y) { return px_[static_cast<size_t>(y) * w_ + x]; }
    const Color& at(int x, int y) const { return px_[static_cast<size_t>(y) * w_ + x]; }
    Color get(int x, int y) const { return bounds().contains(x, y) ? at(x, y) : Color{}; }
    uint8_t* bytes() { return reinterpret_cast<uint8_t*>(px_.data()); }
    const uint8_t* bytes() const { return reinterpret_cast<const uint8_t*>(px_.data()); }
    size_t byteSize() const { return px_.size() * sizeof(Color); }

    void fill(Color c) { std::fill(px_.begin(), px_.end(), c); }
    void fillRect(Rect r, Color c) {
        r = r.intersect(bounds());
        for (int y = r.y; y < r.bottom(); ++y) std::fill(row(y) + r.x, row(y) + r.right(), c);
    }
    // Copy the sub-rectangle `r` of this surface into a new surface.
    Surface crop(Rect r) const {
        r = r.intersect(bounds());
        Surface out(r.w, r.h);
        for (int y = 0; y < r.h; ++y) std::memcpy(out.row(y), row(r.y + y) + r.x, static_cast<size_t>(r.w) * sizeof(Color));
        return out;
    }
    // Paste `src` so that its origin lands at (dx, dy), overwriting pixels (no blending).
    void blit(const Surface& src, int dx, int dy) {
        Rect dst = Rect{dx, dy, src.width(), src.height()}.intersect(bounds());
        for (int y = dst.y; y < dst.bottom(); ++y)
            std::memcpy(row(y) + dst.x, src.row(y - dy) + (dst.x - dx), static_cast<size_t>(dst.w) * sizeof(Color));
    }

private:
    int w_ = 0, h_ = 0;
    std::vector<Color> px_;
};

}  // namespace pu
