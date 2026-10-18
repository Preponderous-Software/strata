// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#include "strata/sim/field.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace strata::sim {

// ---------------------------------------------------------------- Mask2D

Mask2D::Mask2D(int width, int height) : width_(width), height_(height) {
    if (width <= 0 || height <= 0) throw std::invalid_argument("strata::sim::Mask2D: size must be positive");
    cells_.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
}

void Mask2D::setBlocked(int x, int y, bool blocked) {
    if (!inBounds(x, y)) return;
    cells_[index(x, y)] = blocked ? 1 : 0;
}

void Mask2D::blockDisc(float cx, float cy, float radius) {
    if (radius <= 0.0f) return;
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius)));
    const int x1 = std::min(width_ - 1, static_cast<int>(std::ceil(cx + radius)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius)));
    const int y1 = std::min(height_ - 1, static_cast<int>(std::ceil(cy + radius)));
    const float r2 = radius * radius;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float dx = static_cast<float>(x) + 0.5f - cx;
            const float dy = static_cast<float>(y) + 0.5f - cy;
            if (dx * dx + dy * dy <= r2) cells_[index(x, y)] = 1;
        }
    }
}

bool Mask2D::touchesSurface(int x, int y) const {
    if (blocked(x, y)) return false;
    return blocked(x - 1, y) || blocked(x + 1, y) || blocked(x, y - 1) || blocked(x, y + 1);
}

size_t Mask2D::blockedCount() const {
    size_t n = 0;
    for (uint8_t c : cells_) n += c != 0 ? 1u : 0u;
    return n;
}

// ---------------------------------------------------------------- Field2D

Field2D::Field2D(int width, int height, float initial) : width_(width), height_(height) {
    if (width <= 0 || height <= 0) throw std::invalid_argument("strata::sim::Field2D: size must be positive");
    values_.assign(static_cast<size_t>(width) * static_cast<size_t>(height), initial);
}

void Field2D::set(int x, int y, float value) {
    if (inBounds(x, y)) values_[index(x, y)] = value;
}

void Field2D::fill(float value) { std::fill(values_.begin(), values_.end(), value); }

void Field2D::add(int x, int y, float amount) {
    if (!inBounds(x, y)) return;
    float& v = values_[index(x, y)];
    v += amount;
    if (v < 0.0f) v = 0.0f;
}

float Field2D::take(int x, int y, float amount) {
    if (!inBounds(x, y) || amount <= 0.0f) return 0.0f;
    float& v = values_[index(x, y)];
    const float taken = std::min(v, amount);
    v -= taken;
    return taken;
}

float Field2D::sample(float x, float y) const {
    const float fx = x - 0.5f;
    const float fy = y - 0.5f;
    const int ix = static_cast<int>(std::floor(fx));
    const int iy = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(ix);
    const float ty = fy - static_cast<float>(iy);
    auto clampedAt = [this](int cx, int cy) {
        cx = std::min(std::max(cx, 0), width_ - 1);
        cy = std::min(std::max(cy, 0), height_ - 1);
        return values_[index(cx, cy)];
    };
    const float a = clampedAt(ix, iy);
    const float b = clampedAt(ix + 1, iy);
    const float c = clampedAt(ix, iy + 1);
    const float d = clampedAt(ix + 1, iy + 1);
    const float top = a + (b - a) * tx;
    const float bottom = c + (d - c) * tx;
    return top + (bottom - top) * ty;
}

void Field2D::gradient(float x, float y, const Mask2D* mask, float& gx, float& gy) const {
    const int cx = std::min(std::max(static_cast<int>(std::floor(x)), 0), width_ - 1);
    const int cy = std::min(std::max(static_cast<int>(std::floor(y)), 0), height_ - 1);
    const float centre = values_[index(cx, cy)];
    auto neighbour = [&](int nx, int ny) {
        if (!inBounds(nx, ny)) return centre;
        if (mask != nullptr && mask->blocked(nx, ny)) return centre;
        return values_[index(nx, ny)];
    };
    gx = (neighbour(cx + 1, cy) - neighbour(cx - 1, cy)) * 0.5f;
    gy = (neighbour(cx, cy + 1) - neighbour(cx, cy - 1)) * 0.5f;
}

void Field2D::diffuse(float rate, const Mask2D* mask, int substeps) {
    if (mask != nullptr && (mask->width() != width_ || mask->height() != height_))
        throw std::invalid_argument("strata::sim::Field2D::diffuse: mask size does not match the field");
    if (!(rate > 0.0f)) return;
    if (rate > 0.25f) rate = 0.25f;
    if (substeps < 1) substeps = 1;
    for (int s = 0; s < substeps; ++s) diffuseOnce(rate, mask);
}

void Field2D::diffuseOnce(float rate, const Mask2D* mask) {
    scratch_ = values_;
    const uint8_t* blocked = mask != nullptr ? mask->cells().data() : nullptr;
    const size_t w = static_cast<size_t>(width_);
    const size_t h = static_cast<size_t>(height_);
    // Flux form: every open edge is visited once (to the right and down) and
    // moves the same amount out of one cell and into the other.
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            const size_t i = y * w + x;
            if (blocked != nullptr && blocked[i] != 0) continue;
            const float ci = values_[i];
            if (x + 1 < w) {
                const size_t j = i + 1;
                if (blocked == nullptr || blocked[j] == 0) {
                    const float flux = rate * (values_[j] - ci);
                    scratch_[i] += flux;
                    scratch_[j] -= flux;
                }
            }
            if (y + 1 < h) {
                const size_t j = i + w;
                if (blocked == nullptr || blocked[j] == 0) {
                    const float flux = rate * (values_[j] - ci);
                    scratch_[i] += flux;
                    scratch_[j] -= flux;
                }
            }
        }
    }
    if (blocked != nullptr) {
        for (size_t i = 0; i < scratch_.size(); ++i)
            if (blocked[i] != 0) scratch_[i] = 0.0f;
    }
    values_.swap(scratch_);
}

void Field2D::decay(float fraction) {
    if (!(fraction > 0.0f)) return;
    if (fraction > 1.0f) fraction = 1.0f;
    const float keep = 1.0f - fraction;
    for (float& v : values_) v *= keep;
}

void Field2D::clampMax(float cap) {
    for (float& v : values_)
        if (v > cap) v = cap;
}

double Field2D::total() const {
    double sum = 0.0;
    for (float v : values_) sum += static_cast<double>(v);
    return sum;
}

float Field2D::maxValue() const {
    float m = 0.0f;
    for (float v : values_) m = std::max(m, v);
    return m;
}

void Field2D::hashInto(StateHasher& h) const {
    h.i32(width_);
    h.i32(height_);
    for (float v : values_) h.f32(v);
}

}  // namespace strata::sim
