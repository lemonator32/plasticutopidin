#include "blend.h"

#include <algorithm>
#include <cmath>

namespace pu {

namespace {

struct F3 {
    float r, g, b;
};

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float lum(F3 c) { return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b; }

F3 clipColor(F3 c) {
    float l = lum(c);
    float n = std::min({c.r, c.g, c.b});
    float x = std::max({c.r, c.g, c.b});
    if (n < 0.f) {
        c.r = l + (c.r - l) * l / (l - n);
        c.g = l + (c.g - l) * l / (l - n);
        c.b = l + (c.b - l) * l / (l - n);
    }
    if (x > 1.f) {
        c.r = l + (c.r - l) * (1.f - l) / (x - l);
        c.g = l + (c.g - l) * (1.f - l) / (x - l);
        c.b = l + (c.b - l) * (1.f - l) / (x - l);
    }
    return c;
}
F3 setLum(F3 c, float l) {
    float d = l - lum(c);
    return clipColor({c.r + d, c.g + d, c.b + d});
}
float sat(F3 c) { return std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b}); }
F3 setSat(F3 c, float s) {
    float* ch[3] = {&c.r, &c.g, &c.b};
    std::sort(ch, ch + 3, [](float* a, float* b) { return *a < *b; });
    float& mn = *ch[0];
    float& md = *ch[1];
    float& mx = *ch[2];
    if (mx > mn) {
        md = (md - mn) * s / (mx - mn);
        mx = s;
    } else {
        md = mx = 0.f;
    }
    mn = 0.f;
    return c;
}

inline float separable(BlendMode m, float cb, float cs) {
    switch (m) {
        case BlendMode::Multiply: return cb * cs;
        case BlendMode::Screen: return cb + cs - cb * cs;
        case BlendMode::Overlay: return separable(BlendMode::HardLight, cs, cb);
        case BlendMode::Darken: return std::min(cb, cs);
        case BlendMode::Lighten: return std::max(cb, cs);
        case BlendMode::ColorDodge:
            if (cb == 0.f) return 0.f;
            if (cs >= 1.f) return 1.f;
            return std::min(1.f, cb / (1.f - cs));
        case BlendMode::ColorBurn:
            if (cb >= 1.f) return 1.f;
            if (cs <= 0.f) return 0.f;
            return 1.f - std::min(1.f, (1.f - cb) / cs);
        case BlendMode::HardLight:
            return cs <= 0.5f ? cb * 2.f * cs : separable(BlendMode::Screen, cb, 2.f * cs - 1.f);
        case BlendMode::SoftLight: {
            float d = cb <= 0.25f ? ((16.f * cb - 12.f) * cb + 4.f) * cb : std::sqrt(cb);
            return cs <= 0.5f ? cb - (1.f - 2.f * cs) * cb * (1.f - cb) : cb + (2.f * cs - 1.f) * (d - cb);
        }
        case BlendMode::Difference: return std::fabs(cb - cs);
        case BlendMode::Exclusion: return cb + cs - 2.f * cb * cs;
        case BlendMode::Add:
        case BlendMode::AddGlow: return std::min(1.f, cb + cs);
        case BlendMode::Subtract: return std::max(0.f, cb - cs);
        case BlendMode::LinearBurn: return std::max(0.f, cb + cs - 1.f);
        case BlendMode::GlowDodge: return separable(BlendMode::ColorDodge, cb, cs);
        case BlendMode::VividLight:
            return cs <= 0.5f ? separable(BlendMode::ColorBurn, cb, 2.f * cs) : separable(BlendMode::ColorDodge, cb, 2.f * cs - 1.f);
        case BlendMode::LinearLight: return clamp01(cb + 2.f * cs - 1.f);
        case BlendMode::PinLight: return cs <= 0.5f ? std::min(cb, 2.f * cs) : std::max(cb, 2.f * cs - 1.f);
        case BlendMode::HardMix: return cb + cs >= 1.f ? 1.f : 0.f;
        case BlendMode::Divide: return cs <= 0.f ? (cb > 0.f ? 1.f : 0.f) : std::min(1.f, cb / cs);
        default: return cs;  // Normal
    }
}

F3 blendRGB(BlendMode m, F3 cb, F3 cs) {
    switch (m) {
        case BlendMode::Hue: return setLum(setSat(cs, sat(cb)), lum(cb));
        case BlendMode::Saturation: return setLum(setSat(cb, sat(cs)), lum(cb));
        case BlendMode::Color: return setLum(cs, lum(cb));
        case BlendMode::Luminosity: return setLum(cb, lum(cs));
        case BlendMode::DarkerColor: return lum(cs) < lum(cb) ? cs : cb;
        case BlendMode::LighterColor: return lum(cs) > lum(cb) ? cs : cb;
        default: return {separable(m, cb.r, cs.r), separable(m, cb.g, cs.g), separable(m, cb.b, cs.b)};
    }
}

inline uint8_t to8(float v) { return static_cast<uint8_t>(clamp01(v) * 255.f + 0.5f); }

}  // namespace

const char* blendModeName(BlendMode m) {
    static const char* names[] = {"Normal",     "Multiply",   "Screen",     "Overlay",  "Darken",     "Lighten",
                                  "ColorDodge", "ColorBurn",  "HardLight",  "SoftLight", "Difference", "Exclusion",
                                  "Add",        "Subtract",   "Hue",        "Saturation", "Color",     "Luminosity",
                                  "LinearBurn", "DarkerColor", "LighterColor", "GlowDodge", "AddGlow", "VividLight",
                                  "LinearLight", "PinLight",  "HardMix",    "Divide"};
    static_assert(sizeof(names) / sizeof(names[0]) == static_cast<size_t>(BlendMode::Count));
    size_t i = static_cast<size_t>(m);
    return i < static_cast<size_t>(BlendMode::Count) ? names[i] : "?";
}

bool blendModeFromName(const std::string& name, BlendMode& out) {
    for (int i = 0; i < static_cast<int>(BlendMode::Count); ++i) {
        if (name == blendModeName(static_cast<BlendMode>(i))) {
            out = static_cast<BlendMode>(i);
            return true;
        }
    }
    return false;
}

Color blendPixel(Color dst, Color src, BlendMode mode, float opacity) {
    float as = src.a / 255.f * clamp01(opacity);
    if (as <= 0.f) return dst;
    float ab = dst.a / 255.f;
    F3 cs{src.r / 255.f, src.g / 255.f, src.b / 255.f};
    F3 cb{dst.r / 255.f, dst.g / 255.f, dst.b / 255.f};
    // W3C: Cs' = (1 - ab) * Cs + ab * B(Cb, Cs); then source-over.
    F3 mixed = mode == BlendMode::Normal ? cs : blendRGB(mode, cb, cs);
    F3 csp{(1.f - ab) * cs.r + ab * mixed.r, (1.f - ab) * cs.g + ab * mixed.g, (1.f - ab) * cs.b + ab * mixed.b};
    float ao = as + ab * (1.f - as);
    if (ao <= 0.f) return Color{};
    F3 co{(as * csp.r + ab * (1.f - as) * cb.r) / ao, (as * csp.g + ab * (1.f - as) * cb.g) / ao,
          (as * csp.b + ab * (1.f - as) * cb.b) / ao};
    return Color{to8(co.r), to8(co.g), to8(co.b), to8(ao)};
}

void blendSurface(Surface& dst, const Surface& src, int ox, int oy, BlendMode mode, float opacity, Rect clip) {
    Rect r = Rect{ox, oy, src.width(), src.height()}.intersect(dst.bounds()).intersect(clip);
    if (r.empty() || opacity <= 0.f) return;
    for (int y = r.y; y < r.bottom(); ++y) {
        Color* d = dst.row(y) + r.x;
        const Color* s = src.row(y - oy) + (r.x - ox);
        for (int x = 0; x < r.w; ++x) {
            if (s[x].a == 0) continue;
            if (mode == BlendMode::Normal && opacity >= 1.f && s[x].a == 255) {
                d[x] = s[x];
            } else {
                d[x] = blendPixel(d[x], s[x], mode, opacity);
            }
        }
    }
}

}  // namespace pu
