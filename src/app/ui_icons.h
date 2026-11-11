// Vector icons for the palettes, drawn with GDI+ on a 16x16 design grid.
#pragma once
#include <windows.h>
#include <objidl.h>

#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

namespace pu::app {

// The first thirteen match the order of app::Tool.
enum class Icon {
    Zoom, Hand, Rotate, Eyedropper, Pen, Pencil, Brush, Airbrush, Eraser, Fill, MoveLayer, Gradient, Selection,
    New, Open, Save, Export, Undo, Redo, Clear, FlipH, ZoomIn, ZoomOut, Fit, Actual,
    RotateLeft, RotateRight, ResetView, Eye, Lock, LockAlpha, Clip, NewLayer, Duplicate,
    Merge, Delete, Pressure, ChevronDown, Swap, Plus, Minus, Close, Check, Menu, History, SelectEllipse, Lasso, SelectAdd, SelectSubtract, LayerProperty, ColorWheelSmall, Gray,
};

void drawIcon(Gdiplus::Graphics& g, Icon icon, const Gdiplus::RectF& r, const Gdiplus::Color& c);

}  // namespace pu::app
