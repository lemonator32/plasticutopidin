#include "clip_bridge.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

namespace pu::clip {

static_assert(sizeof(Color) == 4, "Surface pixels must be tightly packed RGBA8 to share memory with clip_image");

namespace {

bool setErr(std::string* e, const std::string& m) {
    if (e) *e = m;
    return false;
}

// Owns a clip_image for the duration of a scope.
struct ImageHolder {
    clip_image img{};
    ~ImageHolder() { clip_image_free(&img); }
};

}  // namespace

clip_image imageView(const Surface& s) {
    clip_image v{};
    v.width = s.width();
    v.height = s.height();
    v.pixels = const_cast<uint8_t*>(s.bytes());  // libclip only reads write inputs
    return v;
}

Surface toSurface(const clip_image& img) {
    Surface s(img.width, img.height);
    if (img.pixels && !s.empty()) std::memcpy(s.bytes(), img.pixels, s.byteSize());
    return s;
}

DocumentPtr openDocument(const std::string& path, std::string* error) {
    clip_document* d = nullptr;
    clip_error e{};
    if (!clip_document_open(&d, path.c_str(), &e)) {
        setErr(error, e.message);
        return nullptr;
    }
    return DocumentPtr(d);
}

namespace {
int g_overrideCode = -1;
BlendMode g_overrideMode = BlendMode::Normal;
}  // namespace

void setCompositeOverride(int32_t code, BlendMode mode) {
    g_overrideCode = code;
    g_overrideMode = mode;
}

// LayerComposite codes as used by CSP (see libclip/include/libclip/clip.h).
BlendMode blendModeFromComposite(int32_t c) {
    if (c == g_overrideCode) return g_overrideMode;
    switch (c) {
        case CLIP_COMPOSITE_NORMAL: return BlendMode::Normal;
        case CLIP_COMPOSITE_DARKEN: return BlendMode::Darken;
        case CLIP_COMPOSITE_MULTIPLY: return BlendMode::Multiply;
        case CLIP_COMPOSITE_COLOR_BURN: return BlendMode::ColorBurn;
        case CLIP_COMPOSITE_LINEAR_BURN: return BlendMode::LinearBurn;
        case CLIP_COMPOSITE_SUBTRACT: return BlendMode::Subtract;
        case CLIP_COMPOSITE_DARKER_COLOR: return BlendMode::DarkerColor;
        case CLIP_COMPOSITE_LIGHTEN: return BlendMode::Lighten;
        case CLIP_COMPOSITE_SCREEN: return BlendMode::Screen;
        case CLIP_COMPOSITE_COLOR_DODGE: return BlendMode::ColorDodge;
        case CLIP_COMPOSITE_GLOW_DODGE: return BlendMode::GlowDodge;
        case CLIP_COMPOSITE_ADD: return BlendMode::Add;
        case CLIP_COMPOSITE_ADD_GLOW: return BlendMode::AddGlow;
        case CLIP_COMPOSITE_LIGHTER_COLOR: return BlendMode::LighterColor;
        case CLIP_COMPOSITE_OVERLAY: return BlendMode::Overlay;
        case CLIP_COMPOSITE_SOFT_LIGHT: return BlendMode::SoftLight;
        case CLIP_COMPOSITE_HARD_LIGHT: return BlendMode::HardLight;
        case CLIP_COMPOSITE_VIVID_LIGHT: return BlendMode::VividLight;
        case CLIP_COMPOSITE_LINEAR_LIGHT: return BlendMode::LinearLight;
        case CLIP_COMPOSITE_PIN_LIGHT: return BlendMode::PinLight;
        case CLIP_COMPOSITE_HARD_MIX: return BlendMode::HardMix;
        case CLIP_COMPOSITE_DIVIDE: return BlendMode::Divide;
        case CLIP_COMPOSITE_DIFFERENCE: return BlendMode::Difference;
        case CLIP_COMPOSITE_EXCLUSION: return BlendMode::Exclusion;
        case CLIP_COMPOSITE_HUE: return BlendMode::Hue;
        case CLIP_COMPOSITE_SATURATION: return BlendMode::Saturation;
        case CLIP_COMPOSITE_COLOR: return BlendMode::Color;
        case CLIP_COMPOSITE_LUMINOSITY: return BlendMode::Luminosity;
        default: return BlendMode::Normal;
    }
}

int32_t compositeFromBlendMode(BlendMode m) {
    switch (m) {
        case BlendMode::Normal: return CLIP_COMPOSITE_NORMAL;
        case BlendMode::Darken: return CLIP_COMPOSITE_DARKEN;
        case BlendMode::Multiply: return CLIP_COMPOSITE_MULTIPLY;
        case BlendMode::ColorBurn: return CLIP_COMPOSITE_COLOR_BURN;
        case BlendMode::Subtract: return CLIP_COMPOSITE_SUBTRACT;
        case BlendMode::Lighten: return CLIP_COMPOSITE_LIGHTEN;
        case BlendMode::Screen: return CLIP_COMPOSITE_SCREEN;
        case BlendMode::ColorDodge: return CLIP_COMPOSITE_COLOR_DODGE;
        case BlendMode::Add: return CLIP_COMPOSITE_ADD;
        case BlendMode::Overlay: return CLIP_COMPOSITE_OVERLAY;
        case BlendMode::SoftLight: return CLIP_COMPOSITE_SOFT_LIGHT;
        case BlendMode::HardLight: return CLIP_COMPOSITE_HARD_LIGHT;
        case BlendMode::Difference: return CLIP_COMPOSITE_DIFFERENCE;
        case BlendMode::Exclusion: return CLIP_COMPOSITE_EXCLUSION;
        case BlendMode::Hue: return CLIP_COMPOSITE_HUE;
        case BlendMode::Saturation: return CLIP_COMPOSITE_SATURATION;
        case BlendMode::Color: return CLIP_COMPOSITE_COLOR;
        case BlendMode::Luminosity: return CLIP_COMPOSITE_LUMINOSITY;
        case BlendMode::LinearBurn: return CLIP_COMPOSITE_LINEAR_BURN;
        case BlendMode::DarkerColor: return CLIP_COMPOSITE_DARKER_COLOR;
        case BlendMode::LighterColor: return CLIP_COMPOSITE_LIGHTER_COLOR;
        case BlendMode::GlowDodge: return CLIP_COMPOSITE_GLOW_DODGE;
        case BlendMode::AddGlow: return CLIP_COMPOSITE_ADD_GLOW;
        case BlendMode::VividLight: return CLIP_COMPOSITE_VIVID_LIGHT;
        case BlendMode::LinearLight: return CLIP_COMPOSITE_LINEAR_LIGHT;
        case BlendMode::PinLight: return CLIP_COMPOSITE_PIN_LIGHT;
        case BlendMode::HardMix: return CLIP_COMPOSITE_HARD_MIX;
        case BlendMode::Divide: return CLIP_COMPOSITE_DIVIDE;
        default: return CLIP_COMPOSITE_NORMAL;
    }
}

namespace {

// Layer Property effects.  The engine's border width is whole pixels (1..50);
// CSP stores a double, so it is rounded and clamped on the way in.
void readEffects(const clip_layer_effects& fx, Layer& l) {
    l.borderOn = fx.border != 0;
    if (l.borderOn) {
        int w = static_cast<int>(fx.border_width + 0.5);
        l.borderWidth = w < 1 ? 1 : (w > 50 ? 50 : w);
        l.borderColor = Color::rgba(fx.border_color[0], fx.border_color[1], fx.border_color[2]);
    }
    l.layerColorOn = fx.layer_color != 0;
    if (l.layerColorOn) l.layerColor = Color::rgba(fx.layer_color_rgb[0], fx.layer_color_rgb[1], fx.layer_color_rgb[2]);
    l.gray = fx.expression_color == CLIP_EXPRESSION_GRAY;  // Monochrome has no engine equivalent
}

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
int32_t sbe32(const uint8_t* p) { return static_cast<int32_t>(be32(p)); }
uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int k = 3; k >= 0; --k) b.push_back(static_cast<uint8_t>(v >> (8 * k)));
}
void put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}

void identityLuts(Adjustment& a) {
    for (auto& t : a.lut) {
        t.resize(256);
        for (int i = 0; i < 256; ++i) t[i] = static_cast<uint8_t>(i);
    }
}

// Applies a 0..65535 curve given as sorted (in, out) control points to `t`,
// piecewise-linearly (CSP's curves are splines; close for gentle curves).
void applyCurve(std::vector<uint8_t>& t, const std::vector<std::pair<int, int>>& pts) {
    if (pts.size() < 2) return;
    std::vector<uint8_t> src = t;
    for (int i = 0; i < 256; ++i) {
        int v = src[i] * 257;
        size_t k = 1;
        while (k + 1 < pts.size() && pts[k].first < v) ++k;
        auto [x0, y0] = pts[k - 1];
        auto [x1, y1] = pts[k];
        double f = x1 > x0 ? std::clamp(double(v - x0) / (x1 - x0), 0.0, 1.0) : 0.0;
        double out = y0 + (y1 - y0) * f;
        t[i] = static_cast<uint8_t>(std::clamp(out / 257.0 + 0.5, 0.0, 255.0));
    }
}

// Applies one Levels record (u16 in black, gamma point, in white, out black,
// out white) to a table, the same curve as Adjustment::rebuild.
void applyLevels(std::vector<uint8_t>& t, const uint8_t* r) {
    double ib = be16(r) / 65535.0, mid = be16(r + 2) / 65535.0, iw = be16(r + 4) / 65535.0;
    double ob = be16(r + 6) / 65535.0, ow = be16(r + 8) / 65535.0;
    if (iw <= ib) return;
    double frac = std::clamp((mid - ib) / (iw - ib), 0.01, 0.99);
    double gamma = std::log(0.5) / std::log(frac);
    for (int i = 0; i < 256; ++i) {
        double v = std::clamp((t[i] / 255.0 - ib) / (iw - ib), 0.0, 1.0);
        v = std::pow(v, gamma);
        t[i] = static_cast<uint8_t>(std::clamp((ob + (ow - ob) * v) * 255.0 + 0.5, 0.0, 255.0));
    }
}

std::vector<GradientStop> toStops(const clip_gradient& g) {
    std::vector<GradientStop> out;
    for (size_t i = 0; i < g.stop_count; ++i) {
        const clip_gradient_stop& s = g.stops[i];
        out.push_back({static_cast<float>(s.position), Color::rgba(s.rgba[0], s.rgba[1], s.rgba[2], s.rgba[3])});
    }
    return out;
}

void fromStops(const std::vector<GradientStop>& stops, clip_gradient& g) {
    std::vector<GradientStop> sorted = stops;
    std::stable_sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.pos < b.pos; });
    g.stop_count = std::min<size_t>(sorted.size(), CLIP_GRADIENT_MAX_STOPS);
    for (size_t i = 0; i < g.stop_count; ++i) {
        const Color c = sorted[i].color;
        g.stops[i].rgba[0] = c.r;
        g.stops[i].rgba[1] = c.g;
        g.stops[i].rgba[2] = c.b;
        g.stops[i].rgba[3] = c.a;
        g.stops[i].position = std::clamp(sorted[i].pos, 0.f, 1.f);
    }
}

// FilterLayerInfo -> engine adjustment.  Returns false for kinds the engine
// cannot reproduce (posterize, binarize, unknown).  The blob is kept in
// `raw` so an unedited layer is written back byte for byte.
bool readFilter(const clip_layer_info& li, Adjustment& adj) {
    const uint8_t* b = li.filter_info;
    const size_t n = li.filter_info_size;
    if (!b || n < 8) return false;
    const uint32_t kind = be32(b), size = be32(b + 4);
    const uint8_t* p = b + 8;
    if (8 + size_t(size) > n) return false;
    switch (kind) {
        case CLIP_FILTER_BRIGHTNESS_CONTRAST:
            if (size < 8) return false;
            adj.kind = Adjustment::Kind::BrightnessContrast;
            adj.a = sbe32(p);
            adj.b = sbe32(p + 4);
            break;
        case CLIP_FILTER_HSL:
            if (size < 12) return false;
            adj.kind = Adjustment::Kind::HueSaturation;
            adj.a = sbe32(p);
            adj.b = sbe32(p + 4);
            adj.c = sbe32(p + 8);
            break;
        case CLIP_FILTER_REVERSE_GRADIENT: adj.kind = Adjustment::Kind::Invert; break;
        case CLIP_FILTER_LEVELS: {
            // Records of 5 u16; record 0 is the master (RGB), 1..3 are R, G, B.
            if (size < 4 * 10) return false;
            adj.kind = Adjustment::Kind::Levels;
            adj.a = (be16(p) + 128) / 257;
            adj.b = (be16(p + 2) + 128) / 257;
            adj.c = (be16(p + 4) + 128) / 257;
            adj.d = (be16(p + 6) + 128) / 257;
            adj.e = (be16(p + 8) + 128) / 257;
            identityLuts(adj);
            for (int c = 0; c < 3; ++c) {
                applyLevels(adj.lut[c], p);
                applyLevels(adj.lut[c], p + 10 * (c + 1));
            }
            adj.raw.assign(b, b + 8 + size);
            return true;  // per-channel tables: not rebuilt from a..e
        }
        case CLIP_FILTER_TONE_CURVE: {
            // Curves of u16 point count + 32 (u16 in, u16 out); 0 master, 1..3 R, G, B.
            const size_t curve = 2 + 32 * 4;
            if (size < 4 * curve) return false;
            adj.kind = Adjustment::Kind::ToneCurve;
            identityLuts(adj);
            auto read = [&](int k) {
                const uint8_t* c = p + curve * k;
                std::vector<std::pair<int, int>> pts;
                int count = std::min<int>(be16(c), 32);
                for (int i = 0; i < count; ++i) pts.push_back({be16(c + 2 + 4 * i), be16(c + 4 + 4 * i)});
                return pts;
            };
            auto master = read(0);
            for (int c = 0; c < 3; ++c) {
                applyCurve(adj.lut[c], master);
                applyCurve(adj.lut[c], read(c + 1));
            }
            adj.raw.assign(b, b + 8 + size);
            return true;
        }
        case CLIP_FILTER_COLOR_BALANCE: {
            // i32 preserve luminosity, then shadows/midtones/highlights as
            // (cyan-red, magenta-green, yellow-blue), each -100..100.
            if (size < 40) return false;
            adj.kind = Adjustment::Kind::ColorBalance;
            identityLuts(adj);
            for (int c = 0; c < 3; ++c)
                for (int i = 0; i < 256; ++i) {
                    double v = i / 255.0;
                    double ws = std::clamp(1.0 - v * 3.0, 0.0, 1.0);  // shadows
                    double wh = std::clamp(v * 3.0 - 2.0, 0.0, 1.0);  // highlights
                    double wm = 1.0 - ws - wh;                         // midtones
                    double shift = (sbe32(p + 4 + 4 * c) * ws + sbe32(p + 16 + 4 * c) * wm + sbe32(p + 28 + 4 * c) * wh) / 100.0;
                    adj.lut[c][i] = static_cast<uint8_t>(
                        std::clamp((v + shift * 0.4 * (1 - std::abs(2 * v - 1)) + shift * 0.1) * 255.0 + 0.5, 0.0, 255.0));
                }
            adj.raw.assign(b, b + 8 + size);
            return true;
        }
        case CLIP_FILTER_GRADIENT_MAP: {
            // u32 size, then the stop table shared with gradient layers.
            clip_gradient gr{};
            if (size < 4 || !clip_gradient_stops_parse(p + 4, std::min<size_t>(be32(p), size - 4), &gr, nullptr)) return false;
            adj.kind = Adjustment::Kind::GradientMap;
            adj.stops = toStops(gr);
            break;
        }
        default: return false;
    }
    adj.rebuild();
    adj.raw.assign(b, b + 8 + size);
    return true;
}

// Engine adjustment -> FilterLayerInfo (the stored blob when unedited).
std::vector<uint8_t> writeFilter(const Adjustment& a) {
    if (!a.raw.empty()) return a.raw;
    std::vector<uint8_t> params;
    uint32_t kind = 0;
    switch (a.kind) {
        case Adjustment::Kind::BrightnessContrast:
            kind = CLIP_FILTER_BRIGHTNESS_CONTRAST;
            put32(params, static_cast<uint32_t>(a.a));
            put32(params, static_cast<uint32_t>(a.b));
            break;
        case Adjustment::Kind::HueSaturation:
            kind = CLIP_FILTER_HSL;
            put32(params, static_cast<uint32_t>(a.a));
            put32(params, static_cast<uint32_t>(a.b));
            put32(params, static_cast<uint32_t>(a.c));
            break;
        case Adjustment::Kind::Invert: kind = CLIP_FILTER_REVERSE_GRADIENT; break;
        case Adjustment::Kind::Levels: {
            // 32 records as CSP writes them: the master, then neutral ones.
            kind = CLIP_FILTER_LEVELS;
            auto v16 = [](int v) { return static_cast<uint16_t>(std::clamp(v, 0, 255) * 257); };
            for (int v : {a.a, a.b, a.c, a.d, a.e}) put16(params, v16(v));
            for (int r = 1; r < 32; ++r)
                for (uint16_t v : {uint16_t(0), uint16_t(32767), uint16_t(65535), uint16_t(0), uint16_t(65535)}) put16(params, v);
            break;
        }
        case Adjustment::Kind::GradientMap: {
            kind = CLIP_FILTER_GRADIENT_MAP;
            clip_gradient gr{};
            fromStops(a.stops, gr);
            clip_buffer t{};
            if (!clip_gradient_stops_encode(&gr, &t)) {
                clip_buffer_free(&t);
                return {};
            }
            put32(params, static_cast<uint32_t>(t.size));
            params.insert(params.end(), t.data, t.data + t.size);
            clip_buffer_free(&t);
            break;
        }
        default: return {};  // tone curve / colour balance always keep `raw`
    }
    std::vector<uint8_t> out;
    put32(out, kind);
    put32(out, static_cast<uint32_t>(params.size()));
    out.insert(out.end(), params.begin(), params.end());
    return out;
}

// GradationFillInfo -> Fill or Gradient layer.  Returns false for shapes the
// engine does not know (the layer then keeps CSP's rendering as pixels).
bool readGradation(const clip_layer_info& li, Layer& l) {
    if (std::getenv("PU_NO_GRADIENTS")) return false;  // DEBUG
    clip_gradient gr{};
    if (!clip_gradation_parse(li.gradation_info, li.gradation_info_size, &gr, nullptr) || gr.shape != 0 || gr.stop_count == 0)
        return false;
    if (gr.fill) {
        l.kind = LayerKind::Fill;
        l.fillColor = Color::rgba(gr.fill_rgb[0], gr.fill_rgb[1], gr.fill_rgb[2]);
        return true;
    }
    l.kind = LayerKind::Gradient;
    l.gradient.shape = GradientFill::Shape::Linear;
    l.gradient.stops = toStops(gr);
    // The end points are relative to the layer's position.
    l.gradient.x0 = static_cast<float>(gr.x0 + li.offset_x);
    l.gradient.y0 = static_cast<float>(gr.y0 + li.offset_y);
    l.gradient.x1 = static_cast<float>(gr.x1 + li.offset_x);
    l.gradient.y1 = static_cast<float>(gr.y1 + li.offset_y);
    return true;
}

// Fill / linear Gradient layer -> clip_gradient (false: not representable).
bool writeGradation(const Layer& l, clip_gradient& g) {
    g = clip_gradient{};
    g.antialias = 1;
    g.ellipse[0] = g.ellipse[1] = 100.0;
    if (l.kind == LayerKind::Fill) {
        // CSP's fill layers are gradients flagged "single colour".
        g.fill = 1;
        g.fill_rgb[0] = l.fillColor.r;
        g.fill_rgb[1] = l.fillColor.g;
        g.fill_rgb[2] = l.fillColor.b;
        fromStops({{0.f, l.fillColor}, {1.f, l.fillColor}}, g);
        return true;
    }
    if (l.kind != LayerKind::Gradient || l.gradient.shape != GradientFill::Shape::Linear) return false;
    fromStops(l.gradient.stops, g);
    g.x0 = l.gradient.x0;
    g.y0 = l.gradient.y0;
    g.x1 = l.gradient.x1;
    g.y1 = l.gradient.y1;
    return g.stop_count > 0;
}

clip_layer_effects writeEffects(const Layer& l) {
    clip_layer_effects fx{};
    fx.border = l.borderOn && l.borderWidth > 0;
    fx.border_width = l.borderWidth;
    fx.border_color[0] = l.borderColor.r;
    fx.border_color[1] = l.borderColor.g;
    fx.border_color[2] = l.borderColor.b;
    fx.layer_color = l.layerColorOn;
    fx.layer_color_rgb[0] = l.layerColor.r;
    fx.layer_color_rgb[1] = l.layerColor.g;
    fx.layer_color_rgb[2] = l.layerColor.b;
    // A Gray layer is stored as gray in the file, as CSP does: its colours are not kept.
    fx.expression_color = l.gray ? CLIP_EXPRESSION_GRAY : CLIP_EXPRESSION_COLOR;
    return fx;
}

// A libclip mask image (value 255 = shown, in every channel) as an engine mask
// (alpha = how much is hidden).
Surface toMask(const clip_image& m) {
    Surface s(m.width, m.height);
    for (int y = 0; y < m.height; ++y) {
        Color* d = s.row(y);
        const uint8_t* src = m.pixels + static_cast<size_t>(y) * m.width * 4;
        for (int x = 0; x < m.width; ++x) d[x].a = static_cast<uint8_t>(255 - src[x * 4 + 3]);
    }
    return s;
}

}  // namespace

bool toDocument(const clip_document* cd, Document& doc, std::string* error) {
    const clip_canvas_info* cv = clip_document_canvas(cd);
    int w = static_cast<int>(cv->width), h = static_cast<int>(cv->height);
    if (w <= 0 || h <= 0) return setErr(error, "canvas has no pixel size");
    doc = Document(w, h);
    doc.dpi = cv->resolution;
    doc.paperVisible = false;
    size_t activeIndex = 0;
    const size_t count = clip_document_layer_count(cd);
    // libclip lists layers top-to-bottom in pre-order (a folder, then its
    // contents) with a nesting depth.  The engine stacks bottom-up with each
    // folder directly above its contents, which is exactly that list reversed.
    int minDepth = INT32_MAX;
    for (size_t i = 0; i < count; ++i)
        if (clip_document_layer(cd, i)->main_id != cv->root_folder) minDepth = std::min(minDepth, clip_document_layer(cd, i)->depth);
    for (size_t i = count; i-- > 0;) {
        const clip_layer_info& li = *clip_document_layer(cd, i);
        if (li.main_id == cv->root_folder) continue;
        if (clip_layer_is_paper(&li)) {
            // The Paper layer is a solid fill; the engine models it as the paper colour.
            doc.paperVisible = li.visible != 0;
            doc.paperColor = Color::rgba(li.draw_color[0], li.draw_color[1], li.draw_color[2]);
            continue;
        }
        const bool folder = clip_layer_is_folder(&li) != 0;
        Layer* l = doc.addLayerTop(li.name[0] ? li.name : clip_layer_type_name(li.type));
        if (li.main_id == cv->current_layer) activeIndex = doc.layerCount() - 1;
        l->depth = li.depth - minDepth;
        l->visible = li.visible != 0;
        l->locked = li.locked != 0;
        l->clipToBelow = li.clip != 0;
        l->opacity = li.opacity / 256.f;
        l->mode = blendModeFromComposite(li.composite);
        readEffects(li.effects, *l);
        clip_error e{};
        if (li.mask_mipmap && (li.type & 0x2)) {
            ImageHolder m;
            if (clip_document_render_layer_mask(cd, &li, &m.img, &e) && m.img.width == w && m.img.height == h) {
                l->mask = toMask(m.img);
                l->maskEnabled = clip_layer_mask_enabled(&li) != 0;
            }
        }
        if (folder) {
            l->kind = LayerKind::Folder;
            l->surface = Surface();
            l->passThrough = li.composite == CLIP_COMPOSITE_THROUGH;
            if (l->passThrough) l->mode = BlendMode::Normal;
            l->expanded = (li.folder & 0x10) != 0;
            continue;
        }
        if (clip_layer_is_filter(&li)) {
            if (readFilter(li, l->adjustment)) {
                l->kind = LayerKind::Adjustment;
                l->surface = Surface();
                continue;
            }
            // Not reproducible: fall back to the stored pixels (CSP keeps a blank render).
            l->name += " [unsupported correction]";
        } else if (li.gradation_info && readGradation(li, *l)) {
            l->surface = Surface();
            continue;
        }
        ImageHolder px;
        if (clip_document_render_layer(cd, &li, &px.img, &e)) {
            l->surface = toSurface(px.img);
            // A monochrome layer showing only white draws its 1-bit ink in white.
            if (li.effects.expression_color == CLIP_EXPRESSION_MONOCHROME && !li.mono_black && li.mono_white)
                for (int y = 0; y < h; ++y)
                    for (Color *c = l->surface.row(y), *end = c + w; c != end; ++c) c->r = c->g = c->b = 255;
        } else {
            l->name += std::string(" [") + e.message + "]";
        }
    }
    doc.setActiveIndex(activeIndex);
    return true;
}

bool readClipFile(const std::string& path, Document& doc, std::string* error) {
    DocumentPtr cd = openDocument(path, error);
    return cd && toDocument(cd.get(), doc, error);
}

namespace {

// Holds everything a clip_write_document points at.
struct WriteInput {
    std::vector<clip_write_layer> layers;
    // Storage the layers point into (sized up front so the pointers stay valid).
    std::vector<Surface> rendered, masks;
    std::vector<std::vector<uint8_t>> filters;
    std::vector<clip_gradient> gradients;
    Surface preview;
    clip_image previewView{};
    clip_write_document doc{};

    WriteInput(const Document& d, const WriteOptions& opt) {
        layers.resize(d.layerCount());
        rendered.resize(layers.size());
        masks.resize(layers.size());
        filters.resize(layers.size());
        gradients.resize(layers.size());
        for (size_t i = 0; i < layers.size(); ++i) {
            const Layer& l = *d.layer(i);
            clip_write_layer& o = layers[i];
            o.name = l.name.c_str();
            o.depth = l.depth;
            switch (l.kind) {
                case LayerKind::Raster: o.pixels = imageView(l.surface); break;
                case LayerKind::Folder:
                    o.kind = CLIP_WRITE_FOLDER;
                    o.expanded = l.expanded;
                    break;
                case LayerKind::Adjustment:
                    filters[i] = writeFilter(l.adjustment);
                    if (!filters[i].empty()) {
                        o.kind = CLIP_WRITE_FILTER;
                        o.filter_info = filters[i].data();
                        o.filter_info_size = filters[i].size();
                    }
                    break;  // (an unwritable correction is saved as an empty layer)
                case LayerKind::Fill:
                case LayerKind::Gradient: {
                    // The rendering goes with it: CSP shows it until it renders the layer.
                    Layer plain = l;
                    plain.maskEnabled = false;
                    rendered[i] = d.renderLayer(plain, d.bounds());
                    o.pixels = imageView(rendered[i]);
                    // A shape CSP's files do not describe (circle) is saved as pixels.
                    if (writeGradation(l, gradients[i])) {
                        o.kind = CLIP_WRITE_GRADIENT;
                        o.gradient = &gradients[i];
                    }
                    break;
                }
            }
            if (l.hasMask()) {
                Surface& m = masks[i];
                m.reset(d.width(), d.height());
                for (int y = 0; y < d.height(); ++y) {
                    Color* dst = m.row(y);
                    const Color* src = l.mask.row(y);
                    for (int x = 0; x < d.width(); ++x) dst[x].a = static_cast<uint8_t>(255 - src[x].a);
                }
                o.mask = imageView(m);
                o.mask_enabled = l.maskEnabled;
            }
            o.visible = l.visible;
            o.locked = l.locked;
            o.clip = l.clipToBelow;
            o.opacity = static_cast<int32_t>(l.opacity * 256 + 0.5f);
            o.composite = l.isFolder() && l.passThrough ? CLIP_COMPOSITE_THROUGH : compositeFromBlendMode(l.mode);
            o.effects = writeEffects(l);
        }
        doc.width = d.width();
        doc.height = d.height();
        doc.dpi = d.dpi;
        doc.paper_visible = d.paperVisible;
        doc.paper_color[0] = d.paperColor.r;
        doc.paper_color[1] = d.paperColor.g;
        doc.paper_color[2] = d.paperColor.b;
        doc.layers = layers.data();
        doc.layer_count = layers.size();
        doc.active_layer = d.activeIndex();
        if (opt.includePreview) {
            preview = d.flatten();
            previewView = imageView(preview);
            doc.preview = &previewView;
        }
        doc.preview_max_size = opt.previewMaxSize;
    }
};

}  // namespace

std::vector<uint8_t> buildClipFile(const Document& doc, const WriteOptions& opt, std::string* error) {
    WriteInput in(doc, opt);
    clip_buffer out{};
    clip_error e{};
    std::vector<uint8_t> bytes;
    if (clip_write_memory(&in.doc, &out, &e)) bytes.assign(out.data, out.data + out.size);
    else setErr(error, e.message);
    clip_buffer_free(&out);
    return bytes;
}

bool writeClipFile(const std::string& path, const Document& doc, const WriteOptions& opt, std::string* error) {
    WriteInput in(doc, opt);
    clip_error e{};
    return clip_write_path(&in.doc, path.c_str(), &e) || setErr(error, e.message);
}

}  // namespace pu::clip
