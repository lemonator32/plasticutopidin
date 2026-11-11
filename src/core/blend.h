// Layer blend modes and per-pixel compositing (W3C compositing model,
// straight alpha, 8-bit channels).
#pragma once
#include <cstdint>
#include <string>

#include "image.h"

namespace pu {

enum class BlendMode : uint8_t {
    Normal,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    ColorDodge,
    ColorBurn,
    HardLight,
    SoftLight,
    Difference,
    Exclusion,
    Add,
    Subtract,
    Hue,
    Saturation,
    Color,
    Luminosity,
    Count
};

const char* blendModeName(BlendMode m);
bool blendModeFromName(const std::string& name, BlendMode& out);

// Composite `src` over `dst` using the given mode and an extra opacity in [0,1].
Color blendPixel(Color dst, Color src, BlendMode mode, float opacity);

// Composite a whole surface `src` (positioned at offset ox,oy) over `dst`,
// restricted to `clip` in dst coordinates.
void blendSurface(Surface& dst, const Surface& src, int ox, int oy, BlendMode mode, float opacity, Rect clip);

}  // namespace pu
