// Command-based undo/redo.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "document.h"

namespace pu {

class Command {
public:
    virtual ~Command() = default;
    virtual const char* name() const = 0;
    virtual void undo(Document& doc) = 0;
    virtual void redo(Document& doc) = 0;
};

// Restores a rectangle of pixels on one layer.  Stores both before and after
// so redo does not need to re-run the brush engine.
class PixelPatchCommand : public Command {
public:
    PixelPatchCommand(uint64_t layerId, Rect rect, Surface before, Surface after, std::string label = "Paint");
    const char* name() const override { return label_.c_str(); }
    void undo(Document& doc) override;
    void redo(Document& doc) override;
    Rect rect() const { return rect_; }

private:
    uint64_t layerId_;
    Rect rect_;
    Surface before_, after_;
    std::string label_;
};

// Adds (redo) / removes (undo) a whole layer at an index.
class AddLayerCommand : public Command {
public:
    AddLayerCommand(std::unique_ptr<Layer> layer, size_t index);
    const char* name() const override { return "Add layer"; }
    void undo(Document& doc) override;
    void redo(Document& doc) override;

private:
    std::unique_ptr<Layer> layer_;
    size_t index_;
};

class RemoveLayerCommand : public Command {
public:
    explicit RemoveLayerCommand(size_t index) : index_(index) {}
    const char* name() const override { return "Delete layer"; }
    void undo(Document& doc) override;
    void redo(Document& doc) override;

private:
    std::unique_ptr<Layer> layer_;
    size_t index_;
};

class MoveLayerCommand : public Command {
public:
    MoveLayerCommand(size_t from, size_t to) : from_(from), to_(to) {}
    const char* name() const override { return "Move layer"; }
    void undo(Document& doc) override { doc.moveLayer(to_, from_); }
    void redo(Document& doc) override { doc.moveLayer(from_, to_); }

private:
    size_t from_, to_;
};

// Captures non-pixel properties of a layer (name, mode, opacity, flags).
class LayerPropertyCommand : public Command {
public:
    struct Props {
        std::string name;
        BlendMode mode;
        float opacity;
        bool visible, locked, clipToBelow, lockAlpha;
        bool gray, layerColorOn, borderOn;
        Color layerColor, borderColor;
        int borderWidth;
        bool operator==(const Props&) const = default;
        static Props from(const Layer& l) {
            return {l.name, l.mode, l.opacity, l.visible, l.locked, l.clipToBelow, l.lockAlpha,
                    l.gray, l.layerColorOn, l.borderOn, l.layerColor, l.borderColor, l.borderWidth};
        }
        void applyTo(Layer& l) const {
            l.name = name; l.mode = mode; l.opacity = opacity; l.visible = visible; l.locked = locked; l.clipToBelow = clipToBelow;
            l.lockAlpha = lockAlpha;
            l.gray = gray; l.layerColorOn = layerColorOn; l.borderOn = borderOn;
            l.layerColor = layerColor; l.borderColor = borderColor; l.borderWidth = borderWidth;
        }
    };
    LayerPropertyCommand(uint64_t layerId, Props before, Props after) : id_(layerId), before_(before), after_(after) {}
    const char* name() const override { return "Layer property"; }
    void undo(Document& doc) override { if (Layer* l = doc.findLayer(id_)) before_.applyTo(*l); }
    void redo(Document& doc) override { if (Layer* l = doc.findLayer(id_)) after_.applyTo(*l); }

private:
    uint64_t id_;
    Props before_, after_;
};

// A change of the selected area.
class SelectionCommand : public Command {
public:
    SelectionCommand(Selection before, Selection after, std::string label)
        : before_(std::move(before)), after_(std::move(after)), label_(std::move(label)) {}
    const char* name() const override { return label_.c_str(); }
    void undo(Document& doc) override { doc.selection = before_; }
    void redo(Document& doc) override { doc.selection = after_; }

private:
    Selection before_, after_;
    std::string label_;
};

// Several commands that undo and redo as one step.  The parts must already
// have been applied, in order; undo runs them in reverse.
class MacroCommand : public Command {
public:
    explicit MacroCommand(std::string label) : label_(std::move(label)) {}
    void add(std::unique_ptr<Command> c) { parts_.push_back(std::move(c)); }
    bool empty() const { return parts_.empty(); }
    const char* name() const override { return label_.c_str(); }
    void undo(Document& doc) override { for (auto it = parts_.rbegin(); it != parts_.rend(); ++it) (*it)->undo(doc); }
    void redo(Document& doc) override { for (auto& c : parts_) c->redo(doc); }

private:
    std::string label_;
    std::vector<std::unique_ptr<Command>> parts_;
};

class UndoStack {
public:
    explicit UndoStack(Document& doc, size_t limit = 200) : doc_(doc), limit_(limit) {}

    // Push a command that has ALREADY been applied to the document.
    void push(std::unique_ptr<Command> cmd);
    bool canUndo() const { return !done_.empty(); }
    bool canRedo() const { return !undone_.empty(); }
    const char* undoName() const { return canUndo() ? done_.back()->name() : ""; }
    const char* redoName() const { return canRedo() ? undone_.back()->name() : ""; }
    bool undo();
    bool redo();
    void clear() { done_.clear(); undone_.clear(); }
    size_t size() const { return done_.size(); }
    // For the History palette: done steps oldest first; undone steps in redo order (next redo first).
    size_t redoSize() const { return undone_.size(); }
    const char* doneName(size_t i) const { return done_[i]->name(); }
    const char* redoNameAt(size_t i) const { return undone_[undone_.size() - 1 - i]->name(); }

private:
    Document& doc_;
    size_t limit_;
    std::vector<std::unique_ptr<Command>> done_, undone_;
};

}  // namespace pu
