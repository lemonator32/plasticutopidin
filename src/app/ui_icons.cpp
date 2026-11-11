#include "ui_icons.h"

#include <initializer_list>
#include <vector>

namespace pu::app {

namespace {
namespace gp = Gdiplus;

// Drawing context mapping the 16x16 design grid into the target square.
// `mirror` flips the design horizontally (used for undo/redo, rotate left/right).
struct D {
    gp::Graphics& g;
    float ox, oy, k;
    bool mirror;
    gp::Color c;
    gp::Pen pen;
    gp::SolidBrush br;

    D(gp::Graphics& gr, const gp::RectF& r, const gp::Color& col, bool mir)
        : g(gr),
          ox(r.X + (r.Width - std::min(r.Width, r.Height)) / 2),
          oy(r.Y + (r.Height - std::min(r.Width, r.Height)) / 2),
          k(std::min(r.Width, r.Height) / 16.f),
          mirror(mir),
          c(col),
          pen(col, std::max(1.f, 1.25f * std::min(r.Width, r.Height) / 16.f)),
          br(col) {
        pen.SetStartCap(gp::LineCapRound);
        pen.SetEndCap(gp::LineCapRound);
        pen.SetLineJoin(gp::LineJoinRound);
    }
    gp::PointF p(float x, float y) const { return gp::PointF(ox + (mirror ? 16.f - x : x) * k, oy + y * k); }
    void line(float x1, float y1, float x2, float y2, float w = 0.f) {
        if (w > 0.f) {
            gp::Pen q(c, w * k);
            q.SetStartCap(gp::LineCapRound);
            q.SetEndCap(gp::LineCapRound);
            g.DrawLine(&q, p(x1, y1), p(x2, y2));
        } else {
            g.DrawLine(&pen, p(x1, y1), p(x2, y2));
        }
    }
    void lines(std::initializer_list<float> xy) {
        std::vector<gp::PointF> pts;
        for (auto it = xy.begin(); it != xy.end(); it += 2) pts.push_back(p(*it, *(it + 1)));
        g.DrawLines(&pen, pts.data(), static_cast<INT>(pts.size()));
    }
    void poly(std::initializer_list<float> xy, bool fill) {
        std::vector<gp::PointF> pts;
        for (auto it = xy.begin(); it != xy.end(); it += 2) pts.push_back(p(*it, *(it + 1)));
        if (fill) g.FillPolygon(&br, pts.data(), static_cast<INT>(pts.size()));
        else g.DrawPolygon(&pen, pts.data(), static_cast<INT>(pts.size()));
    }
    void rect(float x1, float y1, float x2, float y2, bool fill) { poly({x1, y1, x2, y1, x2, y2, x1, y2}, fill); }
    void circle(float cx, float cy, float r, bool fill) {
        gp::PointF a = p(cx - r, cy - r);
        float left = mirror ? a.X - 2 * r * k : a.X;
        if (fill) g.FillEllipse(&br, left, a.Y, 2 * r * k, 2 * r * k);
        else g.DrawEllipse(&pen, left, a.Y, 2 * r * k, 2 * r * k);
    }
    // Arc on a circle; angles in degrees, clockwise from +x (screen space).
    void arc(float cx, float cy, float r, float start, float sweep) {
        if (mirror) { start = 180.f - start; sweep = -sweep; }
        gp::PointF a = p(cx - r, cy - r);
        float left = mirror ? a.X - 2 * r * k : a.X;
        g.DrawArc(&pen, left, a.Y, 2 * r * k, 2 * r * k, start, sweep);
    }
    void bezier(float x1, float y1, float cx1, float cy1, float cx2, float cy2, float x2, float y2) {
        g.DrawBezier(&pen, p(x1, y1), p(cx1, cy1), p(cx2, cy2), p(x2, y2));
    }
    void magnifier() {
        circle(6.5f, 6.5f, 4.5f, false);
        line(10.f, 10.f, 14.f, 14.f, 2.2f);
    }
};

void bucket(D& d) {
    d.poly({2, 7.5f, 7.5f, 2, 13, 7.5f, 7.5f, 13}, false);
    d.poly({2, 7.5f, 13, 7.5f, 7.5f, 13}, true);
    d.circle(14.2f, 11.5f, 1.3f, true);
}

void rotateArrow(D& d) {
    // Arc clockwise from the right, ending at the top with an arrowhead pointing right.
    d.arc(8, 8.5f, 5.5f, 0, 270);
    d.poly({8, 0.8f, 10.8f, 3, 8, 5.2f}, true);
}
}  // namespace

void drawIcon(Gdiplus::Graphics& g, Icon icon, const Gdiplus::RectF& r, const Gdiplus::Color& c) {
    bool mirror = icon == Icon::Redo || icon == Icon::RotateLeft;
    D d(g, r, c, mirror);
    switch (icon) {
        case Icon::Zoom: d.magnifier(); break;
        case Icon::ZoomIn: d.magnifier(); d.line(4.5f, 6.5f, 8.5f, 6.5f); d.line(6.5f, 4.5f, 6.5f, 8.5f); break;
        case Icon::ZoomOut: d.magnifier(); d.line(4.5f, 6.5f, 8.5f, 6.5f); break;
        case Icon::Hand:
            d.line(5.5f, 8, 5.5f, 3.8f);
            d.line(7.8f, 8, 7.8f, 2.2f);
            d.line(10.1f, 8, 10.1f, 2.8f);
            d.line(12.3f, 8.8f, 12.3f, 4.6f);
            d.line(4, 10.5f, 2.2f, 8);
            d.lines({4, 9, 4, 11.5f, 6.5f, 14.5f, 10.5f, 14.5f, 12.3f, 12, 12.3f, 8.8f});
            break;
        case Icon::Rotate: case Icon::RotateRight: case Icon::RotateLeft: rotateArrow(d); break;
        case Icon::ResetView:
            d.circle(8, 8, 6, false);
            d.poly({8, 2.8f, 10, 8, 6, 8}, true);
            d.line(8, 8, 8, 12.5f);
            break;
        case Icon::Eyedropper:
            d.line(2.5f, 13.5f, 9, 7, 2);
            d.line(10.2f, 5.8f, 12.6f, 3.4f, 4);
            d.line(7.6f, 4.6f, 11.4f, 8.4f, 1.6f);
            break;
        case Icon::Pen:
            d.poly({5, 1.5f, 11, 1.5f, 11, 6.5f, 8, 14.5f, 5, 6.5f}, false);
            d.line(8, 8.6f, 8, 13.6f);
            d.circle(8, 7.6f, 0.9f, true);
            break;
        case Icon::Pencil:
            d.poly({3.66f, 9.66f, 11.66f, 1.66f, 14.34f, 4.34f, 6.34f, 12.34f}, false);
            d.lines({3.66f, 9.66f, 2, 14, 6.34f, 12.34f});
            d.poly({2, 14, 2.6f, 12.2f, 3.8f, 13.4f}, true);
            break;
        case Icon::Brush: {
            d.line(14, 2, 9.4f, 6.6f, 2.2f);
            gp::PointF pts[] = {d.p(10.2f, 7.4f), d.p(8.6f, 5.8f), d.p(4.8f, 9.4f), d.p(1.8f, 14.2f), d.p(6.6f, 11.2f)};
            d.g.FillClosedCurve(&d.br, pts, 5, gp::FillModeAlternate, 0.3f);
            break;
        }
        case Icon::Airbrush:
            d.circle(9.6f, 8, 4.4f, false);
            d.line(2.8f, 8, 5.2f, 8);
            d.lines({9.6f, 3.6f, 9.6f, 1.8f, 12.6f, 1.8f});
            d.circle(1.2f, 5.6f, 0.7f, true);
            d.circle(0.9f, 8, 0.7f, true);
            d.circle(1.2f, 10.4f, 0.7f, true);
            break;
        case Icon::Eraser:
            d.poly({2.5f, 10.5f, 9, 4, 14, 9, 7.5f, 15.5f}, false);
            d.poly({2.5f, 10.5f, 5.5f, 7.5f, 10.5f, 12.5f, 7.5f, 15.5f}, true);
            break;
        case Icon::Fill: bucket(d); break;
        case Icon::MoveLayer:
            // Four-way arrow.
            d.line(8, 1.5f, 8, 14.5f);
            d.line(1.5f, 8, 14.5f, 8);
            d.poly({8, 0.8f, 10.2f, 3.4f, 5.8f, 3.4f}, true);
            d.poly({8, 15.2f, 10.2f, 12.6f, 5.8f, 12.6f}, true);
            d.poly({0.8f, 8, 3.4f, 5.8f, 3.4f, 10.2f}, true);
            d.poly({15.2f, 8, 12.6f, 5.8f, 12.6f, 10.2f}, true);
            break;
        case Icon::Gradient: {
            // A frame with a left-to-right ramp of bars.
            d.rect(1.5f, 3.5f, 14.5f, 12.5f, false);
            for (int i = 0; i < 5; ++i) {
                float x = 3.f + i * 2.2f, w = 2.f - i * 0.38f;
                d.rect(x, 5.f, x + w, 11.f, true);
            }
            break;
        }
        case Icon::New:
            d.poly({3, 1.5f, 9.5f, 1.5f, 13, 5, 13, 14.5f, 3, 14.5f}, false);
            d.lines({9.5f, 1.5f, 9.5f, 5, 13, 5});
            break;
        case Icon::Open: d.poly({1.5f, 3.5f, 6, 3.5f, 7.5f, 5.2f, 14.5f, 5.2f, 14.5f, 13, 1.5f, 13}, false); break;
        case Icon::Save:
            d.rect(2, 2, 14, 14, false);
            d.rect(5, 2, 11, 6, false);
            d.rect(4.5f, 9, 11.5f, 14, true);
            break;
        case Icon::Export:
            d.rect(1.5f, 3, 14.5f, 13, false);
            d.lines({2, 12.5f, 6.5f, 7.5f, 9.5f, 10.5f, 11.5f, 8.5f, 14.5f, 11.5f});
            d.circle(11, 5.8f, 1.1f, true);
            break;
        case Icon::Undo: case Icon::Redo:
            d.bezier(5, 5.5f, 11.5f, 5.5f, 14, 8, 12, 12.5f);
            d.poly({1.8f, 5.5f, 5.6f, 2.4f, 5.6f, 8.6f}, true);
            break;
        case Icon::Clear:
            d.rect(2.5f, 2.5f, 13.5f, 13.5f, false);
            d.line(5.5f, 5.5f, 10.5f, 10.5f);
            d.line(10.5f, 5.5f, 5.5f, 10.5f);
            break;
        case Icon::FlipH:
            d.poly({1.5f, 8, 6.5f, 3, 6.5f, 13}, false);
            d.poly({14.5f, 8, 9.5f, 3, 9.5f, 13}, true);
            d.line(8, 1.5f, 8, 14.5f, 0.8f);
            break;
        case Icon::Fit:
            d.lines({2, 5.5f, 2, 2, 5.5f, 2});
            d.lines({10.5f, 2, 14, 2, 14, 5.5f});
            d.lines({14, 10.5f, 14, 14, 10.5f, 14});
            d.lines({5.5f, 14, 2, 14, 2, 10.5f});
            d.rect(5, 5, 11, 11, false);
            break;
        case Icon::Actual: {
            gp::FontFamily fam(L"Segoe UI");
            gp::Font f(&fam, 7.5f * d.k, gp::FontStyleBold, gp::UnitPixel);
            gp::StringFormat sf;
            sf.SetAlignment(gp::StringAlignmentCenter);
            sf.SetLineAlignment(gp::StringAlignmentCenter);
            gp::RectF box(d.ox - 2 * d.k, d.oy, 20 * d.k, 16 * d.k);
            d.g.DrawString(L"1:1", -1, &f, box, &sf, &d.br);
            break;
        }
        case Icon::Eye:
            d.bezier(1.5f, 8, 5, 3, 11, 3, 14.5f, 8);
            d.bezier(14.5f, 8, 11, 13, 5, 13, 1.5f, 8);
            d.circle(8, 8, 2.2f, true);
            break;
        case Icon::Lock:
            d.arc(8, 6, 3, 180, 180);
            d.line(5, 6, 5, 7.5f);
            d.line(11, 6, 11, 7.5f);
            d.rect(3.5f, 7.5f, 12.5f, 14.5f, true);
            break;
        case Icon::LockAlpha:
            d.arc(8, 6, 3, 180, 180);
            d.line(5, 6, 5, 7.5f);
            d.line(11, 6, 11, 7.5f);
            d.rect(3.5f, 7.5f, 12.5f, 14.5f, false);
            d.rect(3.5f, 7.5f, 8, 11, true);
            d.rect(8, 11, 12.5f, 14.5f, true);
            break;
        case Icon::Clip:
            d.rect(6.5f, 2, 13.5f, 6.5f, false);
            d.rect(2.5f, 10, 13.5f, 14, true);
            d.line(4, 3.5f, 4, 7.5f);
            d.poly({2.3f, 7.2f, 5.7f, 7.2f, 4, 9.2f}, true);
            break;
        case Icon::NewLayer:
            d.rect(2.5f, 2.5f, 10, 13.5f, false);
            d.line(12.5f, 8.5f, 12.5f, 14.5f);
            d.line(9.5f, 11.5f, 15.5f, 11.5f);
            break;
        case Icon::Duplicate:
            d.rect(2.5f, 5.5f, 10.5f, 13.5f, false);
            d.rect(5.5f, 2.5f, 13.5f, 10.5f, false);
            break;
        case Icon::Merge:
            d.rect(2.5f, 2.5f, 13.5f, 5.5f, false);
            d.rect(2.5f, 11, 13.5f, 14, true);
            d.line(8, 6.5f, 8, 9.8f);
            d.lines({6, 8, 8, 10, 10, 8});
            break;
        case Icon::Delete:
            d.line(2.5f, 4, 13.5f, 4);
            d.lines({6, 4, 6.5f, 2, 9.5f, 2, 10, 4});
            d.poly({4, 4, 12, 4, 11.2f, 14.5f, 4.8f, 14.5f}, false);
            d.line(6.8f, 6.5f, 6.8f, 12);
            d.line(9.2f, 6.5f, 9.2f, 12);
            break;
        case Icon::Pressure:
            d.lines({2, 2, 2, 14, 14, 14});
            d.bezier(2, 14, 8, 14, 8.5f, 3, 14, 3);
            break;
        case Icon::ChevronDown: d.lines({4, 6, 8, 10, 12, 6}); break;
        case Icon::Swap:
            d.bezier(5, 3, 10.5f, 3, 13, 5.5f, 13, 11);
            d.lines({7, 1.3f, 5, 3, 7, 4.7f});
            d.lines({11.3f, 9, 13, 11, 14.7f, 9});
            break;
        case Icon::Plus: d.line(8, 3, 8, 13); d.line(3, 8, 13, 8); break;
        case Icon::Minus: d.line(3, 8, 13, 8); break;
        case Icon::Close: d.line(4, 4, 12, 12); d.line(12, 4, 4, 12); break;
        case Icon::Check: d.lines({3, 8.5f, 6.5f, 12, 13, 4.5f}); break;
        case Icon::Selection: {
            // Dashed rectangle (marquee).
            const float xs[] = {2, 5.5f, 9, 12.5f};
            for (float x : xs) { d.line(x, 2.5f, x + 1.5f, 2.5f); d.line(x, 13.5f, x + 1.5f, 13.5f); }
            for (float y : xs) { d.line(1.5f, y + 0.5f, 1.5f, y + 2); d.line(14.5f, y + 0.5f, 14.5f, y + 2); }
            break;
        }
        case Icon::SelectEllipse:
            for (int i = 0; i < 8; ++i) d.arc(8, 8, 6, i * 45.f, 26.f);
            break;
        case Icon::Lasso:
            d.bezier(4, 11, 0, 7, 3, 2, 9, 2.5f);
            d.bezier(9, 2.5f, 15, 3, 15, 9, 9, 10);
            d.bezier(9, 10, 6, 10.5f, 5, 12, 6, 14.5f);
            break;
        case Icon::SelectAdd:
            d.rect(1.5f, 1.5f, 9.5f, 9.5f, false);
            d.line(12, 8, 12, 15); d.line(8.5f, 11.5f, 15.5f, 11.5f);
            break;
        case Icon::SelectSubtract:
            d.rect(1.5f, 1.5f, 9.5f, 9.5f, false);
            d.line(8.5f, 12.5f, 15.5f, 12.5f);
            break;
        case Icon::LayerProperty:
            d.poly({8, 2, 14.5f, 5.5f, 8, 9, 1.5f, 5.5f}, false);
            d.lines({1.5f, 9, 8, 12.5f, 14.5f, 9});
            d.circle(13, 13, 2, true);
            break;
        case Icon::ColorWheelSmall:
            d.circle(8, 8, 6, false);
            d.circle(8, 5, 1.6f, true);
            d.circle(5.2f, 9.6f, 1.6f, true);
            d.circle(10.8f, 9.6f, 1.6f, true);
            break;
        case Icon::Gray:
            d.rect(2, 2, 14, 14, false);
            d.rect(2, 2, 8, 14, true);
            break;
        case Icon::Menu: d.line(3, 4.5f, 13, 4.5f); d.line(3, 8, 13, 8); d.line(3, 11.5f, 13, 11.5f); break;
        case Icon::History:
            d.circle(8.5f, 8.5f, 5.8f, false);
            d.lines({8.5f, 5, 8.5f, 8.5f, 11, 10});
            d.poly({1, 5.5f, 4.6f, 5.5f, 2.8f, 2.6f}, true);
            break;
    }
}

}  // namespace pu::app
