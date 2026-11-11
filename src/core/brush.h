// Dab-based brush engine with pressure dynamics and stroke-level opacity.
//
// A stroke owns a coverage buffer (float per canvas pixel in the touched
// region).  Each input point is interpolated from the previous point at
// `spacing * size` intervals; every dab is a radial falloff disc whose
// coverage is merged with max() into the buffer.  The target layer is then
// recomputed as   layer = original ⊕ (colour, coverage * opacity)
// so that overlapping dabs never exceed the stroke opacity, which is the
// behaviour painters expect from a "pen" style brush.
#pragma once
#include <vector>

#include "blend.h"
#include "image.h"

namespace pu {

struct BrushSettings {
    float size = 24.f;            // diameter in canvas pixels at full pressure
    float minSizeRatio = 0.15f;   // size at zero pressure, as a ratio of `size`
    float hardness = 0.7f;        // 0 = fully soft, 1 = hard edge
    float opacity = 1.f;          // stroke opacity
    float minOpacityRatio = 1.f;  // opacity at zero pressure, as a ratio
    float spacing = 0.12f;        // dab spacing as a ratio of the dab diameter
    float smoothing = 0.3f;       // 0..1 input position smoothing
    bool antialias = true;
    bool eraser = false;          // when set, the stroke removes alpha instead of painting colour
    Color color = Color::rgba(0, 0, 0);
};

struct StrokePoint {
    float x, y, pressure;
};

class Stroke {
public:
    Stroke(Surface& target, const BrushSettings& settings);

    void addPoint(StrokePoint p);
    void finish();

    // Region of the target that this stroke has modified so far.
    Rect dirtyRect() const { return dirty_; }
    // Region modified by the most recent addPoint() call (for incremental redraw).
    Rect lastDirtyRect() const { return lastDirty_; }
    // Pixels of the target as they were before the stroke, for the dirty region.
    const Surface& original() const { return original_; }
    const BrushSettings& settings() const { return settings_; }

private:
    void stamp(float x, float y, float pressure);
    void applyCoverage(Rect r);
    void ensureCoverage(Rect r);

    Surface& target_;
    BrushSettings settings_;
    Surface original_;              // snapshot of the whole target taken at stroke start
    std::vector<float> coverage_;   // canvas-sized accumulation buffer
    Rect dirty_, lastDirty_;
    bool hasLast_ = false;
    StrokePoint last_{};
    StrokePoint smoothed_{};
    float distanceCarry_ = 0.f;
};

}  // namespace pu
