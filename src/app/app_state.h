// Platform-independent application state: document, tools, view transform.
#pragma once
#include <memory>
#include <string>

#include "brush.h"
#include "document.h"
#include "undo.h"

namespace pu::app {

enum class Tool { Brush, Eraser };

struct View {
    float zoom = 1.f;
    float panX = 0.f, panY = 0.f;  // canvas origin in window pixels
    void canvasFromWindow(float wx, float wy, float& cx, float& cy) const { cx = (wx - panX) / zoom; cy = (wy - panY) / zoom; }
};

class AppState {
public:
    AppState();

    Document doc;
    std::unique_ptr<UndoStack> undo;
    BrushSettings brush;
    Tool tool = Tool::Brush;
    View view;
    std::string filePath;   // last opened/saved path
    bool dirty = false;
    std::string status;     // one-line status message

    void newDocument(int w, int h);
    bool openFile(const std::string& path, std::string* error);
    bool saveFile(const std::string& path, std::string* error);   // .clip or .png by extension

    // Stroke lifecycle (canvas coordinates)
    void beginStroke(float x, float y, float pressure);
    void continueStroke(float x, float y, float pressure);
    Rect endStroke();  // returns the modified rect
    bool strokeActive() const { return stroke_ != nullptr; }
    Rect strokeDirty() const { return stroke_ ? stroke_->lastDirtyRect() : Rect{}; }

    // Layer operations (all undoable)
    void addLayer();
    void deleteLayer();
    void duplicateLayer();
    void moveLayer(int delta);
    void selectLayer(int delta);
    void toggleVisible();
    void cycleBlendMode(int delta);
    void adjustOpacity(float delta);
    void toggleClip();
    void renameLayer(const std::string& name);
    void clearLayer();
    void mergeDown();

    BrushSettings& activeBrush() { return brush; }
    Layer* active() { return doc.activeLayer(); }

private:
    void pushProps(Layer& l, const LayerPropertyCommand::Props& before);
    std::unique_ptr<Stroke> stroke_;
    uint64_t strokeLayer_ = 0;
};

}  // namespace pu::app
