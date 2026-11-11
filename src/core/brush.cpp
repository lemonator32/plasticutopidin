#include "brush.h"

#include <algorithm>
#include <cmath>

namespace pu {

Stroke::Stroke(Surface& target, const BrushSettings& settings)
    : target_(target), settings_(settings), original_(target) {
    coverage_.assign(static_cast<size_t>(target.width()) * target.height(), 0.f);
}

void Stroke::addPoint(StrokePoint p) {
    lastDirty_ = Rect{};
    p.pressure = std::clamp(p.pressure, 0.f, 1.f);
    if (!hasLast_) {
        hasLast_ = true;
        last_ = smoothed_ = p;
        stamp(p.x, p.y, p.pressure);
        return;
    }
    // Exponential smoothing of the input position reduces jitter from pens.
    float s = std::clamp(settings_.smoothing, 0.f, 0.95f);
    smoothed_.x = smoothed_.x * s + p.x * (1.f - s);
    smoothed_.y = smoothed_.y * s + p.y * (1.f - s);
    smoothed_.pressure = p.pressure;

    float dx = smoothed_.x - last_.x, dy = smoothed_.y - last_.y;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist <= 0.0001f) return;

    float pr = std::max(0.f, settings_.spacing) * std::max(1.f, settings_.size);
    float step = std::max(0.5f, pr);
    float t = distanceCarry_;
    while (t < dist) {
        float f = t / dist;
        float pressure = last_.pressure + (smoothed_.pressure - last_.pressure) * f;
        stamp(last_.x + dx * f, last_.y + dy * f, pressure);
        t += step;
    }
    distanceCarry_ = t - dist;
    last_ = smoothed_;
}

void Stroke::finish() {
    if (hasLast_ && dirty_.empty()) stamp(last_.x, last_.y, last_.pressure);
}

void Stroke::stamp(float cx, float cy, float pressure) {
    float sizeRatio = settings_.minSizeRatio + (1.f - settings_.minSizeRatio) * pressure;
    float diameter = std::max(1.f, settings_.size * sizeRatio);
    float radius = diameter * 0.5f;
    float opacityRatio = settings_.minOpacityRatio + (1.f - settings_.minOpacityRatio) * pressure;

    int x0 = static_cast<int>(std::floor(cx - radius - 1)), y0 = static_cast<int>(std::floor(cy - radius - 1));
    int x1 = static_cast<int>(std::ceil(cx + radius + 1)), y1 = static_cast<int>(std::ceil(cy + radius + 1));
    Rect r = Rect::fromLTRB(x0, y0, x1, y1).intersect(target_.bounds());
    if (r.empty()) return;

    // Hardness controls where the falloff starts: inner `hardness` fraction is solid.
    float hard = std::clamp(settings_.hardness, 0.f, 1.f);
    float inner = radius * hard;
    float aa = settings_.antialias ? 0.75f : 0.f;
    for (int y = r.y; y < r.bottom(); ++y) {
        float* cov = coverage_.data() + static_cast<size_t>(y) * target_.width();
        for (int x = r.x; x < r.right(); ++x) {
            float px = x + 0.5f - cx, py = y + 0.5f - cy;
            float d = std::sqrt(px * px + py * py);
            float v;
            if (d <= inner) {
                v = 1.f;
            } else if (d >= radius + aa) {
                continue;
            } else {
                float span = (radius + aa) - inner;
                float t = span > 0.f ? (d - inner) / span : 1.f;
                v = hard >= 0.999f ? (1.f - t) : (1.f - t) * (1.f - t) * (3.f - 2.f * (1.f - t)) ;  // smoothstep
                v = std::clamp(v, 0.f, 1.f);
            }
            v *= opacityRatio;
            if (v > cov[x]) cov[x] = v;
        }
    }
    dirty_ = dirty_.unite(r);
    lastDirty_ = lastDirty_.unite(r);
    applyCoverage(r);
}

void Stroke::applyCoverage(Rect r) {
    float strokeOpacity = std::clamp(settings_.opacity, 0.f, 1.f);
    for (int y = r.y; y < r.bottom(); ++y) {
        const float* cov = coverage_.data() + static_cast<size_t>(y) * target_.width();
        const Color* orig = original_.row(y);
        Color* dst = target_.row(y);
        for (int x = r.x; x < r.right(); ++x) {
            float c = cov[x] * strokeOpacity;
            if (c <= 0.f) continue;
            if (settings_.eraser) {
                Color o = orig[x];
                o.a = static_cast<uint8_t>(o.a * (1.f - c) + 0.5f);
                dst[x] = o;
            } else {
                Color s = settings_.color;
                s.a = static_cast<uint8_t>(c * 255.f + 0.5f);
                dst[x] = blendPixel(orig[x], s, BlendMode::Normal, 1.f);
            }
        }
    }
}

}  // namespace pu
