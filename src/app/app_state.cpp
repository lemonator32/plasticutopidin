#include "app_state.h"

#include <algorithm>

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
}  // namespace

AppState::AppState() { newDocument(1200, 900); }

void AppState::newDocument(int w, int h) {
    stroke_.reset();
    doc = Document(w, h);
    undo = std::make_unique<UndoStack>(doc);
    doc.addLayerTop("Layer 1");
    filePath.clear();
    dirty = false;
    status = "New document " + std::to_string(w) + "x" + std::to_string(h);
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

void AppState::beginStroke(float x, float y, float pressure) {
    Layer* l = active();
    if (!l || l->locked) { status = l ? "Layer is locked" : "No layer"; return; }
    BrushSettings bs = brush;
    bs.eraser = tool == Tool::Eraser;
    stroke_ = std::make_unique<Stroke>(l->surface, bs);
    strokeLayer_ = l->id;
    stroke_->addPoint({x, y, pressure});
}

void AppState::continueStroke(float x, float y, float pressure) {
    if (stroke_) stroke_->addPoint({x, y, pressure});
}

Rect AppState::endStroke() {
    if (!stroke_) return Rect{};
    stroke_->finish();
    Rect r = stroke_->dirtyRect();
    Layer* l = doc.findLayer(strokeLayer_);
    if (l && !r.empty()) {
        Surface before = stroke_->original().crop(r);
        Surface after = l->surface.crop(r);
        undo->push(std::make_unique<PixelPatchCommand>(l->id, r, std::move(before), std::move(after),
                                                       tool == Tool::Eraser ? "Erase" : "Paint"));
        dirty = true;
    }
    stroke_.reset();
    return r;
}

void AppState::pushProps(Layer& l, const LayerPropertyCommand::Props& before) {
    undo->push(std::make_unique<LayerPropertyCommand>(l.id, before, LayerPropertyCommand::Props::from(l)));
    dirty = true;
}

void AppState::addLayer() {
    size_t idx = doc.layerCount() ? doc.activeIndex() + 1 : 0;
    doc.addLayer("Layer " + std::to_string(doc.layerCount() + 1), idx);
    undo->push(std::make_unique<AddLayerCommand>(nullptr, idx));
    dirty = true;
    status = "Added layer";
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
}

void AppState::moveLayer(int delta) {
    size_t from = doc.activeIndex();
    int to = static_cast<int>(from) + delta;
    if (to < 0 || to >= static_cast<int>(doc.layerCount())) return;
    doc.moveLayer(from, static_cast<size_t>(to));
    undo->push(std::make_unique<MoveLayerCommand>(from, static_cast<size_t>(to)));
    dirty = true;
}

void AppState::selectLayer(int delta) {
    int i = static_cast<int>(doc.activeIndex()) + delta;
    i = std::clamp(i, 0, static_cast<int>(doc.layerCount()) - 1);
    doc.setActiveIndex(static_cast<size_t>(i));
}

void AppState::toggleVisible() {
    if (Layer* l = active()) { auto b = LayerPropertyCommand::Props::from(*l); l->visible = !l->visible; pushProps(*l, b); }
}

void AppState::cycleBlendMode(int delta) {
    if (Layer* l = active()) {
        auto b = LayerPropertyCommand::Props::from(*l);
        int n = static_cast<int>(BlendMode::Count);
        l->mode = static_cast<BlendMode>(((static_cast<int>(l->mode) + delta) % n + n) % n);
        pushProps(*l, b);
        status = std::string("Blend: ") + blendModeName(l->mode);
    }
}

void AppState::adjustOpacity(float delta) {
    if (Layer* l = active()) {
        auto b = LayerPropertyCommand::Props::from(*l);
        l->opacity = std::clamp(l->opacity + delta, 0.f, 1.f);
        pushProps(*l, b);
        status = "Opacity " + std::to_string(static_cast<int>(l->opacity * 100 + 0.5f)) + "%";
    }
}

void AppState::toggleClip() {
    if (Layer* l = active()) { auto b = LayerPropertyCommand::Props::from(*l); l->clipToBelow = !l->clipToBelow; pushProps(*l, b); }
}

void AppState::renameLayer(const std::string& name) {
    if (Layer* l = active()) { auto b = LayerPropertyCommand::Props::from(*l); l->name = name; pushProps(*l, b); }
}

void AppState::clearLayer() {
    Layer* l = active();
    if (!l || l->locked) return;
    Surface before = l->surface;
    l->surface.fill(Color{});
    undo->push(std::make_unique<PixelPatchCommand>(l->id, doc.bounds(), std::move(before), l->surface, "Clear layer"));
    dirty = true;
}

void AppState::mergeDown() {
    size_t idx = doc.activeIndex();
    if (idx == 0) { status = "No layer below to merge into"; return; }
    Layer* top = doc.layer(idx);
    Layer* below = doc.layer(idx - 1);
    Surface before = below->surface;
    blendSurface(below->surface, top->surface, 0, 0, top->mode, top->visible ? top->opacity : 0.f, doc.bounds());
    undo->push(std::make_unique<PixelPatchCommand>(below->id, doc.bounds(), std::move(before), below->surface, "Merge down"));
    auto cmd = std::make_unique<RemoveLayerCommand>(idx);
    cmd->redo(doc);
    undo->push(std::move(cmd));
    doc.setActiveIndex(idx - 1);
    dirty = true;
    status = "Merged down";
}

}  // namespace pu::app
