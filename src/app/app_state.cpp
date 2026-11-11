#include "app_state.h"

#include <algorithm>
#include <cstdlib>

#include "clip_bridge.h"
#include "png.h"

namespace pu::app {

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

SubTool paintSub(const char* name, float size, float hardness, float opacity, bool sizeP, bool opP, float spacing = 0.12f,
                 float smoothing = 0.3f) {
    SubTool s;
    s.name = name;
    s.kind = SubKind::Paint;
    s.brush.size = size;
    s.brush.hardness = hardness;
    s.brush.opacity = opacity;
    s.brush.spacing = spacing;
    s.brush.smoothing = smoothing;
    s.sizePressure = sizeP;
    s.opacityPressure = opP;
    return s;
}
SubTool simpleSub(const char* name, SubKind k) {
    SubTool s;
    s.name = name;
    s.kind = k;
    return s;
}
}  // namespace

std::vector<ToolGroup> defaultToolGroups() {
    std::vector<ToolGroup> g;
    g.push_back({Tool::Zoom, "Zoom", "/", {simpleSub("Zoom in", SubKind::ZoomIn), simpleSub("Zoom out", SubKind::ZoomOut)}, 0});
    g.push_back({Tool::Hand, "Hand", "Space", {simpleSub("Hand", SubKind::Hand)}, 0});
    g.push_back({Tool::Rotate, "Rotate", "Shift+Space", {simpleSub("Rotate", SubKind::Rotate)}, 0});
    g.push_back({Tool::MoveLayer, "Move layer", "K", {simpleSub("Move layer", SubKind::MoveLayer)}, 0});
    g.push_back({Tool::Selection, "Selection area", "M",
                 {simpleSub("Rectangle", SubKind::SelectRect), simpleSub("Ellipse", SubKind::SelectEllipse),
                  simpleSub("Lasso", SubKind::SelectLasso)},
                 0});
    g.push_back({Tool::Eyedropper, "Eyedropper", "I",
                 {simpleSub("Obtain display color", SubKind::PickDisplay), simpleSub("Obtain layer color", SubKind::PickLayer)}, 0});
    g.push_back({Tool::Pen, "Pen", "P",
                 {paintSub("G-pen", 10, 0.95f, 1.f, true, false), paintSub("Mapping pen", 5, 1.f, 1.f, true, false, 0.08f, 0.5f),
                  paintSub("Turnip pen", 14, 0.9f, 1.f, true, false, 0.1f, 0.45f),
                  paintSub("Marker", 8, 1.f, 1.f, false, false, 0.08f, 0.2f)},
                 0});
    g.push_back({Tool::Pencil, "Pencil", "P",
                 {paintSub("Real pencil", 6, 0.55f, 0.8f, true, true, 0.1f, 0.25f),
                  paintSub("Rough pencil", 12, 0.4f, 0.7f, true, true, 0.15f, 0.2f)},
                 0});
    g.push_back({Tool::Brush, "Brush", "B",
                 {paintSub("Opaque watercolor", 40, 0.5f, 0.9f, true, true, 0.1f, 0.3f),
                  paintSub("Transparent watercolor", 50, 0.3f, 0.5f, true, true, 0.1f, 0.3f),
                  paintSub("Thick paint", 30, 0.85f, 1.f, true, false, 0.1f, 0.3f)},
                 0});
    g.push_back({Tool::Airbrush, "Airbrush", "B",
                 {paintSub("Soft", 120, 0.f, 0.35f, false, true, 0.08f, 0.2f),
                  paintSub("Strong", 80, 0.25f, 0.7f, false, true, 0.08f, 0.2f)},
                 0});
    {
        SubTool hard = paintSub("Hard", 30, 0.95f, 1.f, true, false);
        hard.kind = SubKind::Erase;
        SubTool soft = paintSub("Soft", 70, 0.1f, 1.f, false, true);
        soft.kind = SubKind::Erase;
        g.push_back({Tool::Eraser, "Eraser", "E", {hard, soft}, 0});
    }
    {
        SubTool all = simpleSub("Refer other layers", SubKind::FillRefAll);
        SubTool one = simpleSub("Refer editing layer only", SubKind::FillLayer);
        g.push_back({Tool::Fill, "Fill", "G", {all, one}, 0});
    }
    {
        SubTool toSub = simpleSub("Main to sub color", SubKind::GradientToSub);
        SubTool toClear = simpleSub("Main to transparent", SubKind::GradientToTransparent);
        toSub.brush.opacity = toClear.brush.opacity = 1.f;
        g.push_back({Tool::Gradient, "Gradient", "G", {toSub, toClear}, 0});
    }
    return g;
}

const std::vector<float>& brushSizeSteps() {
    static const std::vector<float> steps = {0.7f, 1, 1.5f, 2, 2.5f, 3, 4, 5, 6, 7, 8, 10, 12, 15, 17, 20, 25, 30, 40, 50,
                                             60, 70, 80, 100, 120, 150, 170, 200, 250, 300, 400, 500, 600, 700, 800, 1000};
    return steps;
}

AppState::AppState() : tools(defaultToolGroups()) { newDocument(1200, 900); }

namespace {

// Partial selection coverage mixes the edited pixel with the original.
Color maskMix(Color orig, Color edited, uint8_t m) {
    if (m == 255) return edited;
    if (m == 0) return orig;
    auto mx = [m](uint8_t a, uint8_t b) { return static_cast<uint8_t>(a + (b - a) * m / 255); };
    return Color::rgba(mx(orig.r, edited.r), mx(orig.g, edited.g), mx(orig.b, edited.b), mx(orig.a, edited.a));
}

// Scanline-fills a polygon into a coverage mask with 4x4 supersampling (anti-aliased edges).
std::vector<uint8_t> rasterPolygon(const std::vector<std::pair<float, float>>& pts, int w, int h) {
    std::vector<uint8_t> out(static_cast<size_t>(w) * h, 0);
    if (pts.size() < 3) return out;
    float minY = pts[0].second, maxY = minY;
    for (auto& p : pts) { minY = std::min(minY, p.second); maxY = std::max(maxY, p.second); }
    int y0 = std::max(0, static_cast<int>(std::floor(minY))), y1 = std::min(h - 1, static_cast<int>(std::ceil(maxY)));
    constexpr int SS = 4;
    std::vector<uint16_t> acc(static_cast<size_t>(w));
    std::vector<float> xs;
    for (int y = y0; y <= y1; ++y) {
        std::fill(acc.begin(), acc.end(), uint16_t{0});
        for (int sy = 0; sy < SS; ++sy) {
            float fy = y + (sy + 0.5f) / SS;
            xs.clear();
            for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
                auto [xi, yi] = pts[i];
                auto [xj, yj] = pts[j];
                if ((yi <= fy) != (yj <= fy)) xs.push_back(xi + (fy - yi) / (yj - yi) * (xj - xi));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2) {
                // Even-odd spans, sampled at SS points per pixel horizontally.
                int sx0 = std::max(0, static_cast<int>(std::ceil(xs[k] * SS - 0.5f)));
                int sx1 = std::min(w * SS - 1, static_cast<int>(std::floor(xs[k + 1] * SS - 0.5f)));
                for (int sx = sx0; sx <= sx1; ++sx) ++acc[static_cast<size_t>(sx / SS)];
            }
        }
        uint8_t* row = &out[static_cast<size_t>(y) * w];
        for (int x = 0; x < w; ++x) row[x] = static_cast<uint8_t>(std::min(255, acc[static_cast<size_t>(x)] * 255 / (SS * SS)));
    }
    return out;
}

}  // namespace

void AppState::newDocument(int w, int h, double dpi) {
    stroke_.reset();
    propEdit_.reset();
    doc = Document(w, h);
    doc.dpi = dpi;
    undo = std::make_unique<UndoStack>(doc);
    doc.addLayerTop("Layer 1");
    filePath.clear();
    dirty = false;
    status = "New canvas " + std::to_string(w) + " x " + std::to_string(h) + " px";
}

bool AppState::openFile(const std::string& path, std::string* error) {
    std::string lp = lower(path);
    if (endsWith(lp, ".clip")) {
        Document d;
        if (!clip::readClipFile(path, d, error)) return false;
        stroke_.reset();
        doc = std::move(d);
        if (doc.layerCount() == 0) doc.addLayerTop("Layer 1");
    } else {
        Surface s;
        if (!png::readFile(path, s, error)) return false;
        stroke_.reset();
        doc = Document(s.width(), s.height());
        Layer* l = doc.addLayerTop("Image");
        l->surface = s;
    }
    propEdit_.reset();
    undo = std::make_unique<UndoStack>(doc);
    filePath = path;
    dirty = false;
    status = "Opened " + path;
    return true;
}

bool AppState::saveFile(const std::string& path, std::string* error) {
    std::string lp = lower(path);
    bool ok;
    if (endsWith(lp, ".clip")) ok = clip::writeClipFile(path, doc, {}, error);
    else ok = png::writeFile(path, doc.flatten(), error);
    if (ok) {
        filePath = path;
        dirty = false;
        status = "Saved " + path;
    }
    return ok;
}

bool AppState::editableLayer(Layer*& l) {
    l = active();
    if (!l) { status = "No layer"; return false; }
    if (l->locked) { status = "The layer is locked"; return false; }
    return true;
}

void AppState::beginStroke(float x, float y, float pressure, const BrushSettings& bs, const char* label) {
    Layer* l;
    if (!editableLayer(l)) return;
    if (!l->visible) { status = "The layer is hidden"; return; }
    stroke_ = std::make_unique<Stroke>(l->surface, bs);
    strokeLayer_ = l->id;
    strokeLabel_ = label;
    stroke_->addPoint({x, y, pressure});
    applyAlphaLock(stroke_->lastDirtyRect());
}

void AppState::continueStroke(float x, float y, float pressure) {
    if (!stroke_) return;
    stroke_->addPoint({x, y, pressure});
    applyAlphaLock(stroke_->lastDirtyRect());
}

// Constrains the stroke's latest dabs: with "lock transparent pixels" a stroke may change colour but
// never alpha, and with a selection it only changes the selected area.
void AppState::applyAlphaLock(Rect r) {
    Layer* l = doc.findLayer(strokeLayer_);
    const Selection& sel = doc.selection;
    if (!l || !stroke_ || (!l->lockAlpha && !sel.active)) return;
    r = r.intersect(doc.bounds());
    const Surface& orig = stroke_->original();
    for (int y = r.y; y < r.bottom(); ++y) {
        Color* d = l->surface.row(y);
        const Color* o = orig.row(y);
        for (int x = r.x; x < r.right(); ++x) {
            if (l->lockAlpha) {
                if (o[x].a == 0) d[x] = o[x];
                else if (d[x].a == 0) { d[x] = o[x]; }
                else d[x].a = o[x].a;
            }
            if (sel.active) d[x] = maskMix(o[x], d[x], sel.at(x, y));
        }
    }
}

Rect AppState::endStroke() {
    if (!stroke_) return Rect{};
    stroke_->finish();
    Rect r = stroke_->dirtyRect();
    applyAlphaLock(r);
    Layer* l = doc.findLayer(strokeLayer_);
    if (l && !r.empty()) {
        Surface before = stroke_->original().crop(r);
        Surface after = l->surface.crop(r);
        undo->push(std::make_unique<PixelPatchCommand>(l->id, r, std::move(before), std::move(after), strokeLabel_));
        dirty = true;
    }
    stroke_.reset();
    return r;
}

Rect AppState::floodFill(int sx, int sy, const Surface& ref, int tolerance, int expand, Color color, bool erase) {
    Layer* l;
    if (!editableLayer(l)) return Rect{};
    int w = doc.width(), h = doc.height();
    if (sx < 0 || sy < 0 || sx >= w || sy >= h || ref.width() != w || ref.height() != h) return Rect{};
    const Color seed = ref.at(sx, sy);
    const int tol = tolerance * 255 / 100;
    auto similar = [&](const Color& c) {
        return std::abs(c.r - seed.r) <= tol && std::abs(c.g - seed.g) <= tol && std::abs(c.b - seed.b) <= tol &&
               std::abs(c.a - seed.a) <= tol;
    };
    std::vector<uint8_t> mask(static_cast<size_t>(w) * h, 0);
    std::vector<std::pair<int, int>> stack{{sx, sy}};
    int minX = sx, maxX = sx, minY = sy, maxY = sy;
    while (!stack.empty()) {
        auto [x, y] = stack.back();
        stack.pop_back();
        // Scanline fill: walk left and right, then queue the rows above and below.
        int x0 = x;
        while (x0 > 0 && !mask[static_cast<size_t>(y) * w + x0 - 1] && similar(ref.at(x0 - 1, y))) --x0;
        int x1 = x;
        while (x1 < w - 1 && !mask[static_cast<size_t>(y) * w + x1 + 1] && similar(ref.at(x1 + 1, y))) ++x1;
        if (mask[static_cast<size_t>(y) * w + x] || !similar(ref.at(x, y))) continue;
        for (int i = x0; i <= x1; ++i) mask[static_cast<size_t>(y) * w + i] = 1;
        minX = std::min(minX, x0); maxX = std::max(maxX, x1);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
        for (int ny : {y - 1, y + 1}) {
            if (ny < 0 || ny >= h) continue;
            bool run = false;
            for (int i = x0; i <= x1; ++i) {
                bool ok = !mask[static_cast<size_t>(ny) * w + i] && similar(ref.at(i, ny));
                if (ok && !run) stack.push_back({i, ny});
                run = ok;
            }
        }
    }
    // Grow the area so it tucks under anti-aliased line art.
    for (int pass = 0; pass < expand; ++pass) {
        std::vector<uint8_t> grown = mask;
        int gx0 = std::max(0, minX - 1), gx1 = std::min(w - 1, maxX + 1);
        int gy0 = std::max(0, minY - 1), gy1 = std::min(h - 1, maxY + 1);
        for (int y = gy0; y <= gy1; ++y)
            for (int x = gx0; x <= gx1; ++x) {
                if (mask[static_cast<size_t>(y) * w + x]) continue;
                bool n = (x > 0 && mask[static_cast<size_t>(y) * w + x - 1]) || (x < w - 1 && mask[static_cast<size_t>(y) * w + x + 1]) ||
                         (y > 0 && mask[static_cast<size_t>(y - 1) * w + x]) || (y < h - 1 && mask[static_cast<size_t>(y + 1) * w + x]);
                if (n) grown[static_cast<size_t>(y) * w + x] = 1;
            }
        mask.swap(grown);
        minX = gx0; maxX = gx1; minY = gy0; maxY = gy1;
    }
    Rect r = Rect::fromLTRB(minX, minY, maxX + 1, maxY + 1);
    if (doc.selection.active) r = r.intersect(doc.selection.bounds);
    if (r.empty()) { status = "The fill area is outside the selection"; return Rect{}; }
    Surface before = l->surface.crop(r);
    for (int y = r.y; y < r.bottom(); ++y) {
        Color* d = l->surface.row(y);
        for (int x = r.x; x < r.right(); ++x) {
            if (!mask[static_cast<size_t>(y) * w + x]) continue;
            Color o = d[x];
            if (erase) d[x] = Color{};
            else if (l->lockAlpha) { if (d[x].a) d[x] = Color::rgba(color.r, color.g, color.b, d[x].a); }
            else d[x] = color;
            d[x] = maskMix(o, d[x], doc.selection.at(x, y));
        }
    }
    undo->push(std::make_unique<PixelPatchCommand>(l->id, r, std::move(before), l->surface.crop(r), "Fill"));
    dirty = true;
    return r;
}

Rect AppState::drawGradient(float x0, float y0, float x1, float y1, Color a, Color b, float opacity) {
    Layer* l;
    if (!editableLayer(l)) return Rect{};
    float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    const Selection& sel = doc.selection;
    Rect area = sel.active ? sel.bounds : doc.bounds();
    Surface before = l->surface.crop(area);
    for (int y = area.y; y < area.bottom(); ++y) {
        Color* d = l->surface.row(y);
        for (int x = area.x; x < area.right(); ++x) {
            uint8_t m = sel.at(x, y);
            if (!m) continue;
            float t = len2 > 0.f ? std::clamp(((x + 0.5f - x0) * dx + (y + 0.5f - y0) * dy) / len2, 0.f, 1.f) : 0.f;
            auto lerp = [t](uint8_t p, uint8_t q) { return static_cast<uint8_t>(p + (q - p) * t + 0.5f); };
            Color src = Color::rgba(lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a));
            uint8_t oldA = d[x].a;
            if (l->lockAlpha && oldA == 0) continue;
            d[x] = blendPixel(d[x], src, BlendMode::Normal, opacity * m / 255.f);
            if (l->lockAlpha) d[x].a = oldA;
        }
    }
    undo->push(std::make_unique<PixelPatchCommand>(l->id, area, std::move(before), l->surface.crop(area), "Gradient"));
    dirty = true;
    return area;
}

bool AppState::beginLayerMove() {
    Layer* l;
    if (!editableLayer(l)) return false;
    moveLayer_ = l->id;
    moveOriginal_ = l->surface;
    moveSelOriginal_ = doc.selection;
    moveDx_ = moveDy_ = 0;
    return true;
}

// Without a selection the whole layer moves.  With one, only the selected pixels move (the hole they
// leave is cleared) and the selection travels with them.
void AppState::previewLayerMove(int dx, int dy) {
    Layer* l = doc.findLayer(moveLayer_);
    if (!l || (dx == moveDx_ && dy == moveDy_)) return;
    moveDx_ = dx;
    moveDy_ = dy;
    int w = doc.width(), h = doc.height();
    const Selection& s0 = moveSelOriginal_;
    if (!s0.active) {
        for (int y = 0; y < h; ++y) {
            Color* d = l->surface.row(y);
            int sy = y - dy;
            if (sy < 0 || sy >= h) { std::fill(d, d + w, Color{}); continue; }
            const Color* s = moveOriginal_.row(sy);
            for (int x = 0; x < w; ++x) {
                int sx = x - dx;
                d[x] = sx >= 0 && sx < w ? s[sx] : Color{};
            }
        }
        return;
    }
    // Lift: the original with the selected part cut out (partially for soft edges).
    for (int y = 0; y < h; ++y) {
        Color* d = l->surface.row(y);
        const Color* o = moveOriginal_.row(y);
        for (int x = 0; x < w; ++x) {
            d[x] = o[x];
            if (uint8_t m = s0.at(x, y)) d[x].a = static_cast<uint8_t>(o[x].a * (255 - m) / 255);
        }
    }
    // Drop: the selected pixels, shifted, over what remains.
    Selection& sel = doc.selection;
    std::fill(sel.mask.begin(), sel.mask.end(), uint8_t{0});
    Rect dst = Rect{s0.bounds.x + dx, s0.bounds.y + dy, s0.bounds.w, s0.bounds.h}.intersect(doc.bounds());
    for (int y = dst.y; y < dst.bottom(); ++y) {
        Color* d = l->surface.row(y);
        const Color* o = moveOriginal_.row(y - dy);
        for (int x = dst.x; x < dst.right(); ++x) {
            uint8_t m = s0.at(x - dx, y - dy);
            if (!m) continue;
            Color c = o[x - dx];
            c.a = static_cast<uint8_t>(c.a * m / 255);
            d[x] = blendPixel(d[x], c, BlendMode::Normal, 1.f);
            sel.mask[static_cast<size_t>(y) * w + x] = m;
        }
    }
    sel.bounds = dst;
    sel.active = !dst.empty();
    ++sel.version;
}

void AppState::endLayerMove() {
    Layer* l = doc.findLayer(moveLayer_);
    if (l && (moveDx_ || moveDy_)) {
        auto m = std::make_unique<MacroCommand>(moveSelOriginal_.active ? "Move selection" : "Move layer");
        m->add(std::make_unique<PixelPatchCommand>(l->id, doc.bounds(), std::move(moveOriginal_), l->surface, ""));
        if (moveSelOriginal_.active) m->add(std::make_unique<SelectionCommand>(std::move(moveSelOriginal_), doc.selection, ""));
        undo->push(std::move(m));
        dirty = true;
    }
    moveLayer_ = 0;
    moveOriginal_ = Surface();
    moveSelOriginal_ = Selection{};
}

void AppState::duplicateSubTool() {
    ToolGroup& g = group();
    SubTool copy = sub();
    copy.name += " copy";
    g.subs.insert(g.subs.begin() + g.active + 1, copy);
    ++g.active;
    status = "Duplicated sub tool";
}

void AppState::deleteSubTool() {
    ToolGroup& g = group();
    if (g.subs.size() <= 1) { status = "A tool needs at least one sub tool"; return; }
    g.subs.erase(g.subs.begin() + g.active);
    g.active = std::min(g.active, static_cast<int>(g.subs.size()) - 1);
    status = "Deleted sub tool";
}

bool AppState::resetSubTool() {
    for (const ToolGroup& ig : initialTools_) {
        if (ig.tool != group().tool) continue;
        for (const SubTool& s : ig.subs)
            if (s.name == sub().name) { sub() = s; status = "Restored the initial settings"; return true; }
    }
    status = "This sub tool has no initial settings";
    return false;
}

void AppState::noteColorUsed(Color c) {
    c.a = 255;
    auto it = std::find(colorHistory.begin(), colorHistory.end(), c);
    if (it != colorHistory.end()) colorHistory.erase(it);
    colorHistory.insert(colorHistory.begin(), c);
    if (colorHistory.size() > 64) colorHistory.resize(64);
}

void AppState::historyJump(size_t doneCount) {
    bool changed = false;
    while (undo->size() > doneCount && undo->canUndo()) { undo->undo(); changed = true; }
    while (undo->size() < doneCount && undo->canRedo()) { undo->redo(); changed = true; }
    if (changed) dirty = true;
}

void AppState::pushProps(Layer& l, const LayerPropertyCommand::Props& before) {
    if (propEdit_) return;  // folded into the property edit when it ends
    undo->push(std::make_unique<LayerPropertyCommand>(l.id, before, LayerPropertyCommand::Props::from(l)));
    dirty = true;
}

void AppState::beginPropertyEdit() {
    if (Layer* l = active()) propEdit_ = std::make_pair(l->id, LayerPropertyCommand::Props::from(*l));
}

void AppState::endPropertyEdit() {
    if (!propEdit_) return;
    auto [id, before] = *propEdit_;
    propEdit_.reset();
    if (Layer* l = doc.findLayer(id)) {
        auto after = LayerPropertyCommand::Props::from(*l);
        if (!(after == before)) {
            undo->push(std::make_unique<LayerPropertyCommand>(id, before, after));
            dirty = true;
        }
    }
}

void AppState::addLayer() {
    size_t idx = doc.layerCount() ? doc.activeIndex() + 1 : 0;
    int n = 1;
    for (size_t i = 0; i < doc.layerCount(); ++i) {
        const std::string& nm = doc.layer(i)->name;
        if (nm.rfind("Layer ", 0) == 0) n = std::max(n, std::atoi(nm.c_str() + 6) + 1);
    }
    doc.addLayer("Layer " + std::to_string(n), idx);
    undo->push(std::make_unique<AddLayerCommand>(nullptr, idx));
    dirty = true;
    status = "New raster layer";
}

void AppState::deleteLayer() {
    if (doc.layerCount() <= 1) { status = "Cannot delete the last layer"; return; }
    size_t idx = doc.activeIndex();
    auto cmd = std::make_unique<RemoveLayerCommand>(idx);
    cmd->redo(doc);
    undo->push(std::move(cmd));
    dirty = true;
    status = "Deleted layer";
}

void AppState::duplicateLayer() {
    Layer* l = active();
    if (!l) return;
    auto copy = l->clone();
    copy->id = 0;
    copy->name += " copy";
    size_t idx = doc.activeIndex() + 1;
    doc.insertLayer(std::move(copy), idx);
    undo->push(std::make_unique<AddLayerCommand>(nullptr, idx));
    dirty = true;
    status = "Duplicated layer";
}

void AppState::moveLayer(int delta) {
    int to = static_cast<int>(doc.activeIndex()) + delta;
    if (to < 0 || to >= static_cast<int>(doc.layerCount())) return;
    moveLayerTo(doc.activeIndex(), static_cast<size_t>(to));
}

void AppState::moveLayerTo(size_t from, size_t to) {
    if (from >= doc.layerCount() || to >= doc.layerCount() || from == to) return;
    doc.moveLayer(from, to);
    undo->push(std::make_unique<MoveLayerCommand>(from, to));
    dirty = true;
}

void AppState::selectLayer(int delta) {
    int i = static_cast<int>(doc.activeIndex()) + delta;
    i = std::clamp(i, 0, static_cast<int>(doc.layerCount()) - 1);
    doc.setActiveIndex(static_cast<size_t>(i));
}

void AppState::toggleVisible(size_t index) {
    if (Layer* l = doc.layer(index)) { auto b = LayerPropertyCommand::Props::from(*l); l->visible = !l->visible; pushProps(*l, b); }
}

void AppState::cycleBlendMode(int delta) {
    if (Layer* l = active()) {
        int n = static_cast<int>(BlendMode::Count);
        setBlendMode(static_cast<BlendMode>(((static_cast<int>(l->mode) + delta) % n + n) % n));
    }
}

void AppState::setBlendMode(BlendMode m) {
    if (Layer* l = active()) {
        auto b = LayerPropertyCommand::Props::from(*l);
        l->mode = m;
        pushProps(*l, b);
        status = std::string("Blending mode: ") + blendModeName(l->mode);
    }
}

void AppState::adjustOpacity(float delta) {
    if (Layer* l = active()) {
        auto b = LayerPropertyCommand::Props::from(*l);
        l->opacity = std::clamp(l->opacity + delta, 0.f, 1.f);
        pushProps(*l, b);
    }
}

void AppState::toggleClip() {
    if (Layer* l = active()) { auto b = LayerPropertyCommand::Props::from(*l); l->clipToBelow = !l->clipToBelow; pushProps(*l, b); }
}

void AppState::toggleLock() {
    if (Layer* l = active()) { auto b = LayerPropertyCommand::Props::from(*l); l->locked = !l->locked; pushProps(*l, b); }
}

void AppState::toggleLockAlpha() {
    if (Layer* l = active()) { auto b = LayerPropertyCommand::Props::from(*l); l->lockAlpha = !l->lockAlpha; pushProps(*l, b); }
}

void AppState::renameLayer(const std::string& name) {
    if (Layer* l = active()) { auto b = LayerPropertyCommand::Props::from(*l); l->name = name; pushProps(*l, b); }
}

void AppState::setSelection(std::vector<uint8_t> shape, SelectMode mode, const char* label) {
    int w = doc.width(), h = doc.height();
    Selection before = doc.selection;
    Selection& s = doc.selection;
    if (mode != SelectMode::Replace && s.active) {
        for (size_t i = 0; i < shape.size(); ++i)
            shape[i] = mode == SelectMode::Add ? std::max(s.mask[i], shape[i])
                                               : static_cast<uint8_t>(s.mask[i] * (255 - shape[i]) / 255);
    } else if (mode == SelectMode::Subtract) {
        return;  // nothing to subtract from
    }
    int l = w, t = h, r = -1, b = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (shape[static_cast<size_t>(y) * w + x]) { l = std::min(l, x); r = std::max(r, x); t = std::min(t, y); b = std::max(b, y); }
    Selection next;
    next.version = before.version + 1;
    if (r >= 0) {
        next.active = true;
        next.mask = std::move(shape);
        next.width = w;
        next.bounds = Rect::fromLTRB(l, t, r + 1, b + 1);
    }
    if (!before.active && !next.active) return;
    s = next;
    undo->push(std::make_unique<SelectionCommand>(std::move(before), s, label));
    status = s.active ? std::string(label) : "Deselected";
}

void AppState::selectRect(float x0, float y0, float x1, float y1, SelectMode mode) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    selectPolygon({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, mode);
}

void AppState::selectEllipse(float x0, float y0, float x1, float y1, SelectMode mode) {
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = std::abs(x1 - x0) / 2, ry = std::abs(y1 - y0) / 2;
    std::vector<std::pair<float, float>> pts;
    int n = std::clamp(static_cast<int>((rx + ry) * 0.75f), 24, 720);
    for (int i = 0; i < n; ++i) {
        float a = 6.2831853f * i / n;
        pts.push_back({cx + rx * std::cos(a), cy + ry * std::sin(a)});
    }
    selectPolygon(pts, mode);
}

void AppState::selectPolygon(const std::vector<std::pair<float, float>>& pts, SelectMode mode) {
    setSelection(rasterPolygon(pts, doc.width(), doc.height()), mode, "Selection");
}

void AppState::selectAll() {
    setSelection(std::vector<uint8_t>(static_cast<size_t>(doc.width()) * doc.height(), 255), SelectMode::Replace, "Select all");
}

void AppState::deselect() {
    if (!doc.selection.active) return;
    Selection before = doc.selection;
    doc.selection = Selection{};
    doc.selection.version = before.version + 1;
    undo->push(std::make_unique<SelectionCommand>(std::move(before), doc.selection, "Deselect"));
    status = "Deselected";
}

void AppState::invertSelection() {
    std::vector<uint8_t> m(static_cast<size_t>(doc.width()) * doc.height(), 255);
    if (doc.selection.active)
        for (size_t i = 0; i < m.size(); ++i) m[i] = 255 - doc.selection.mask[i];
    setSelection(std::move(m), SelectMode::Replace, "Invert selection");
}

void AppState::clearOutsideSelection() {
    Layer* l;
    if (!doc.selection.active) { status = "There is no selection"; return; }
    if (!editableLayer(l)) return;
    Surface s = l->surface;
    for (int y = 0; y < s.height(); ++y) {
        Color* p = s.row(y);
        for (int x = 0; x < s.width(); ++x) p[x].a = static_cast<uint8_t>(p[x].a * doc.selection.at(x, y) / 255);
    }
    replaceLayerPixels(*l, std::move(s), "Clear outside selection", false);
}

void AppState::replaceLayerPixels(Layer& l, Surface after, const char* label, bool masked) {
    if (masked && doc.selection.active) {
        for (int y = 0; y < after.height(); ++y) {
            Color* d = after.row(y);
            const Color* o = l.surface.row(y);
            for (int x = 0; x < after.width(); ++x) d[x] = maskMix(o[x], d[x], doc.selection.at(x, y));
        }
    }
    Surface before = l.surface;
    l.surface = after;
    undo->push(std::make_unique<PixelPatchCommand>(l.id, doc.bounds(), std::move(before), std::move(after), label));
    dirty = true;
}

void AppState::clearLayer() {
    Layer* l;
    if (!editableLayer(l)) return;
    replaceLayerPixels(*l, Surface(doc.width(), doc.height()), "Clear");
}

void AppState::fillLayer(Color c) {
    Layer* l;
    if (!editableLayer(l)) return;
    Surface s = l->surface;
    if (l->lockAlpha) {
        for (int y = 0; y < s.height(); ++y)
            for (int x = 0; x < s.width(); ++x) { Color& p = s.at(x, y); if (p.a) p = Color::rgba(c.r, c.g, c.b, p.a); }
    } else {
        s.fill(c);
    }
    replaceLayerPixels(*l, std::move(s), "Fill");
}

void AppState::mergeDown() {
    size_t idx = doc.activeIndex();
    if (idx == 0) { status = "There is no layer below to merge into"; return; }
    Layer* top = doc.layer(idx);
    Layer* below = doc.layer(idx - 1);
    if (below->locked) { status = "The layer below is locked"; return; }
    Surface before = below->surface;
    Surface src = top->surface;
    if (top->clipToBelow) {
        for (int y = 0; y < src.height(); ++y)
            for (int x = 0; x < src.width(); ++x) src.at(x, y).a = static_cast<uint8_t>(src.at(x, y).a * below->surface.at(x, y).a / 255);
    }
    blendSurface(below->surface, src, 0, 0, top->mode, top->visible ? top->opacity : 0.f, doc.bounds());
    auto macro = std::make_unique<MacroCommand>("Merge with layer below");
    macro->add(std::make_unique<PixelPatchCommand>(below->id, doc.bounds(), std::move(before), below->surface));
    auto rm = std::make_unique<RemoveLayerCommand>(idx);
    rm->redo(doc);
    macro->add(std::move(rm));
    undo->push(std::move(macro));
    doc.setActiveIndex(idx - 1);
    dirty = true;
    status = "Merged with layer below";
}

void AppState::mergeVisible() {
    std::vector<size_t> visible;
    for (size_t i = 0; i < doc.layerCount(); ++i)
        if (doc.layer(i)->visible) visible.push_back(i);
    if (visible.size() < 2) { status = "Need two visible layers to merge"; return; }
    // Composite the visible layers (without paper) into the bottom-most visible one.
    Document tmp(doc.width(), doc.height());
    tmp.paperVisible = false;
    for (size_t i : visible) tmp.insertLayer(doc.layer(i)->clone(), tmp.layerCount());
    Surface merged = tmp.flatten();
    auto macro = std::make_unique<MacroCommand>("Merge visible layers");
    Layer* base = doc.layer(visible.front());
    Surface before = base->surface;
    base->surface = merged;
    auto b = LayerPropertyCommand::Props::from(*base);
    base->mode = BlendMode::Normal;
    base->opacity = 1.f;
    base->clipToBelow = false;
    macro->add(std::make_unique<PixelPatchCommand>(base->id, doc.bounds(), std::move(before), merged, "Merge"));
    macro->add(std::make_unique<LayerPropertyCommand>(base->id, b, LayerPropertyCommand::Props::from(*base)));
    for (size_t k = visible.size() - 1; k >= 1; --k) {
        auto rm = std::make_unique<RemoveLayerCommand>(visible[k]);
        rm->redo(doc);
        macro->add(std::move(rm));
    }
    undo->push(std::move(macro));
    doc.setActiveIndex(visible.front());
    dirty = true;
    status = "Merged visible layers";
}

void AppState::flipLayer(bool horizontal) {
    Layer* l;
    if (!editableLayer(l)) return;
    Surface s(doc.width(), doc.height());
    for (int y = 0; y < s.height(); ++y)
        for (int x = 0; x < s.width(); ++x)
            s.at(x, y) = horizontal ? l->surface.at(s.width() - 1 - x, y) : l->surface.at(x, s.height() - 1 - y);
    replaceLayerPixels(*l, std::move(s), horizontal ? "Flip horizontal" : "Flip vertical");
}

void AppState::invertLayer() {
    Layer* l;
    if (!editableLayer(l)) return;
    Surface s = l->surface;
    for (int y = 0; y < s.height(); ++y)
        for (Color* p = s.row(y), *e = p + s.width(); p != e; ++p) { p->r = 255 - p->r; p->g = 255 - p->g; p->b = 255 - p->b; }
    replaceLayerPixels(*l, std::move(s), "Reverse gradient");
}

void AppState::blurLayer(int radius) {
    Layer* l;
    if (!editableLayer(l)) return;
    int w = doc.width(), h = doc.height();
    // Box blur on premultiplied colour, horizontal then vertical.
    std::vector<float> a(static_cast<size_t>(w) * h * 4), b(a.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const Color& c = l->surface.at(x, y);
            float al = c.a / 255.f;
            float* p = &a[(static_cast<size_t>(y) * w + x) * 4];
            p[0] = c.r * al; p[1] = c.g * al; p[2] = c.b * al; p[3] = static_cast<float>(c.a);
        }
    auto pass = [&](const std::vector<float>& src, std::vector<float>& dst, bool horiz) {
        int len = horiz ? w : h, lines = horiz ? h : w;
        for (int line = 0; line < lines; ++line) {
            auto idx = [&](int i) { return (horiz ? static_cast<size_t>(line) * w + i : static_cast<size_t>(i) * w + line) * 4; };
            float sum[4] = {0, 0, 0, 0};
            for (int i = -radius; i <= radius; ++i) { size_t k = idx(std::clamp(i, 0, len - 1)); for (int c = 0; c < 4; ++c) sum[c] += src[k + c]; }
            for (int i = 0; i < len; ++i) {
                size_t k = idx(i);
                for (int c = 0; c < 4; ++c) dst[k + c] = sum[c] / (2 * radius + 1);
                size_t add = idx(std::min(i + radius + 1, len - 1)), sub = idx(std::max(i - radius, 0));
                for (int c = 0; c < 4; ++c) sum[c] += src[add + c] - src[sub + c];
            }
        }
    };
    pass(a, b, true);
    pass(b, a, false);
    Surface s(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float* p = &a[(static_cast<size_t>(y) * w + x) * 4];
            float al = p[3];
            Color& c = s.at(x, y);
            c.a = static_cast<uint8_t>(std::clamp(al + 0.5f, 0.f, 255.f));
            if (al > 0.f) {
                float inv = 255.f / al;
                c.r = static_cast<uint8_t>(std::clamp(p[0] * inv + 0.5f, 0.f, 255.f));
                c.g = static_cast<uint8_t>(std::clamp(p[1] * inv + 0.5f, 0.f, 255.f));
                c.b = static_cast<uint8_t>(std::clamp(p[2] * inv + 0.5f, 0.f, 255.f));
            }
        }
    if (l->lockAlpha)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) s.at(x, y).a = l->surface.at(x, y).a;
    replaceLayerPixels(*l, std::move(s), "Blur");
}

}  // namespace pu::app
