// Bridge between libclip (the C .clip reader/writer in ../libclip) and the
// painting engine's Document model.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "document.h"
#include "image.h"
#include "libclip/clip.h"

namespace pu::clip {

struct DocumentCloser {
    void operator()(clip_document* d) const { clip_document_close(d); }
};
using DocumentPtr = std::unique_ptr<clip_document, DocumentCloser>;

// Opens a .clip file; returns null (and sets *error) on failure.
DocumentPtr openDocument(const std::string& path, std::string* error = nullptr);

// Convert into the engine's model: raster layers only, folders flattened,
// the Paper layer becomes the paper colour, masks are applied to alpha.
bool toDocument(const clip_document* cd, Document& doc, std::string* error = nullptr);
bool readClipFile(const std::string& path, Document& doc, std::string* error = nullptr);

struct WriteOptions {
    bool includePreview = true;  // CanvasPreview row (flattened document as PNG)
    int previewMaxSize = 0;      // 0 = full size, as CSP writes it
};
std::vector<uint8_t> buildClipFile(const Document& doc, const WriteOptions& opt = {}, std::string* error = nullptr);
bool writeClipFile(const std::string& path, const Document& doc, const WriteOptions& opt = {}, std::string* error = nullptr);

// CSP LayerComposite codes <-> engine blend modes (best effort).
BlendMode blendModeFromComposite(int32_t composite);
int32_t compositeFromBlendMode(BlendMode mode);
// Calibration hook for `cliptool calibrate`: read composite `code` as `mode`
// (code -1 turns the override off).
void setCompositeOverride(int32_t code, BlendMode mode);

// Surface <-> clip_image.  The view does not copy and must not outlive `s`.
clip_image imageView(const Surface& s);
Surface toSurface(const clip_image& img);

}  // namespace pu::clip
