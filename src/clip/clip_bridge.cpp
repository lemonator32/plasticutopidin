#include "clip_bridge.h"

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

// LayerComposite codes as used by CSP (see libclip/include/libclip/clip.h).
BlendMode blendModeFromComposite(int32_t c) {
    switch (c) {
        case CLIP_COMPOSITE_NORMAL: return BlendMode::Normal;
        case CLIP_COMPOSITE_DARKEN: return BlendMode::Darken;
        case CLIP_COMPOSITE_MULTIPLY: return BlendMode::Multiply;
        case CLIP_COMPOSITE_COLOR_BURN: return BlendMode::ColorBurn;
        case CLIP_COMPOSITE_LINEAR_BURN: return BlendMode::Add;  // closest available
        case CLIP_COMPOSITE_SUBTRACT: return BlendMode::Subtract;
        case CLIP_COMPOSITE_LIGHTEN: return BlendMode::Lighten;
        case CLIP_COMPOSITE_SCREEN: return BlendMode::Screen;
        case CLIP_COMPOSITE_COLOR_DODGE: return BlendMode::ColorDodge;
        case CLIP_COMPOSITE_ADD: return BlendMode::Add;
        case CLIP_COMPOSITE_ADD_GLOW: return BlendMode::Add;
        case CLIP_COMPOSITE_OVERLAY: return BlendMode::Overlay;
        case CLIP_COMPOSITE_SOFT_LIGHT: return BlendMode::SoftLight;
        case CLIP_COMPOSITE_HARD_LIGHT: return BlendMode::HardLight;
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

}  // namespace

bool toDocument(const clip_document* cd, Document& doc, std::string* error) {
    const clip_canvas_info* cv = clip_document_canvas(cd);
    int w = static_cast<int>(cv->width), h = static_cast<int>(cv->height);
    if (w <= 0 || h <= 0) return setErr(error, "canvas has no pixel size");
    doc = Document(w, h);
    doc.dpi = cv->resolution;
    doc.paperVisible = false;
    size_t activeIndex = 0;
    // libclip lists layers top-to-bottom; the engine stacks bottom-up.
    for (size_t i = clip_document_layer_count(cd); i-- > 0;) {
        const clip_layer_info& li = *clip_document_layer(cd, i);
        if (clip_layer_is_folder(&li)) continue;
        if (clip_layer_is_paper(&li)) {
            // The Paper layer is a solid fill; the engine models it as the paper colour.
            doc.paperVisible = li.visible != 0;
            doc.paperColor = Color::rgba(li.draw_color[0], li.draw_color[1], li.draw_color[2]);
            continue;
        }
        Layer* l = doc.addLayerTop(li.name[0] ? li.name : clip_layer_type_name(li.type));
        l->visible = li.visible != 0;
        l->locked = li.locked != 0;
        l->clipToBelow = li.clip != 0;
        l->opacity = li.opacity / 256.f;
        l->mode = blendModeFromComposite(li.composite);
        readEffects(li.effects, *l);
        ImageHolder px;
        clip_error e{};
        if (clip_document_render_layer(cd, &li, &px.img, &e)) {
            l->surface = toSurface(px.img);
            ImageHolder mask;
            if (li.mask_mipmap && clip_document_render_layer_mask(cd, &li, &mask.img, &e) && mask.img.width == w && mask.img.height == h) {
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        uint8_t m = mask.img.pixels[(static_cast<size_t>(y) * w + x) * 4 + 3];
                        Color& c = l->surface.at(x, y);
                        c.a = static_cast<uint8_t>(c.a * m / 255);
                    }
            }
        } else {
            l->name += std::string(" [") + e.message + "]";
        }
        if (li.main_id == cv->current_layer) activeIndex = doc.layerCount() - 1;
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
    Surface preview;
    clip_image previewView{};
    clip_write_document doc{};

    WriteInput(const Document& d, const WriteOptions& opt) {
        layers.resize(d.layerCount());
        for (size_t i = 0; i < layers.size(); ++i) {
            const Layer& l = *d.layer(i);
            clip_write_layer& o = layers[i];
            o.name = l.name.c_str();
            o.pixels = imageView(l.surface);
            o.visible = l.visible;
            o.locked = l.locked;
            o.clip = l.clipToBelow;
            o.opacity = static_cast<int32_t>(l.opacity * 256 + 0.5f);
            o.composite = compositeFromBlendMode(l.mode);
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
