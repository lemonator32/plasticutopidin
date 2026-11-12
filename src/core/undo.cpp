#include "undo.h"

namespace pu {

PixelPatchCommand::PixelPatchCommand(uint64_t layerId, Rect rect, Surface before, Surface after, std::string label, bool mask)
    : layerId_(layerId), rect_(rect), before_(std::move(before)), after_(std::move(after)), label_(std::move(label)), mask_(mask) {}

void PixelPatchCommand::undo(Document& doc) {
    if (Layer* l = doc.findLayer(layerId_)) (mask_ ? l->mask : l->surface).blit(before_, rect_.x, rect_.y);
}

void PixelPatchCommand::redo(Document& doc) {
    if (Layer* l = doc.findLayer(layerId_)) (mask_ ? l->mask : l->surface).blit(after_, rect_.x, rect_.y);
}

AddLayerCommand::AddLayerCommand(std::unique_ptr<Layer> layer, size_t index) : layer_(std::move(layer)), index_(index) {}

void AddLayerCommand::undo(Document& doc) { layer_ = doc.removeLayer(index_); }
void AddLayerCommand::redo(Document& doc) { if (layer_) doc.insertLayer(std::move(layer_), index_); }

void RemoveLayerCommand::undo(Document& doc) { if (layer_) doc.insertLayer(std::move(layer_), index_); }
void RemoveLayerCommand::redo(Document& doc) { layer_ = doc.removeLayer(index_); }

void UndoStack::push(std::unique_ptr<Command> cmd) {
    undone_.clear();
    done_.push_back(std::move(cmd));
    if (done_.size() > limit_) done_.erase(done_.begin());
}

bool UndoStack::undo() {
    if (done_.empty()) return false;
    auto cmd = std::move(done_.back());
    done_.pop_back();
    cmd->undo(doc_);
    undone_.push_back(std::move(cmd));
    return true;
}

bool UndoStack::redo() {
    if (undone_.empty()) return false;
    auto cmd = std::move(undone_.back());
    undone_.pop_back();
    cmd->redo(doc_);
    done_.push_back(std::move(cmd));
    return true;
}

}  // namespace pu
