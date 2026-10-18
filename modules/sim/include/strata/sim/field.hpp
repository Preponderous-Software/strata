// strata/sim/field.hpp -- scalar fields on a 2D grid: diffusion, decay,
// sources and sampling.
//
// A Field2D is width x height float concentrations, cell (x, y) at
// values()[y * width + x]. An optional Mask2D marks blocked cells (obstacles,
// walls): they hold nothing and nothing flows into or out of them, and the
// grid's outer edge is closed the same way (zero flux). Diffusion is the
// explicit five-point scheme written in flux form: each open edge between two
// cells moves rate * (difference) from the fuller cell to the emptier one, so
// with no decay and no sources the total is conserved (to float rounding).
//
// Every operation visits cells in a fixed order with no threads and no fused
// multiply-adds (the library is built with -ffp-contract=off), so the same
// inputs give bit-identical fields on every run.
//
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "strata/sim/hash.hpp"

namespace strata::sim {

// Which cells of a grid are blocked. Shared by every field on that grid.
class Mask2D {
public:
    Mask2D() = default;
    Mask2D(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }
    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }

    // Out-of-bounds cells count as blocked.
    bool blocked(int x, int y) const { return !inBounds(x, y) || cells_[index(x, y)] != 0; }
    void setBlocked(int x, int y, bool blocked);
    // Blocks every cell whose centre lies within `radius` of (cx, cy).
    void blockDisc(float cx, float cy, float radius);
    // True when the cell is open and at least one 4-neighbour is blocked
    // (the grid edge counts): a surface something could attach to.
    bool touchesSurface(int x, int y) const;
    size_t blockedCount() const;

    const std::vector<uint8_t>& cells() const { return cells_; }

private:
    size_t index(int x, int y) const { return static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x); }
    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> cells_;
};

class Field2D {
public:
    Field2D() = default;
    Field2D(int width, int height, float initial = 0.0f);

    int width() const { return width_; }
    int height() const { return height_; }
    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }

    // Cell value; out-of-bounds reads return 0.
    float at(int x, int y) const { return inBounds(x, y) ? values_[index(x, y)] : 0.0f; }
    void set(int x, int y, float value);
    void fill(float value);
    // Adds amount (may be negative; the cell is clamped at 0).
    void add(int x, int y, float amount);
    // Removes up to `amount` from the cell and returns what was removed.
    float take(int x, int y, float amount);

    // Bilinear sample at a continuous position, cell centres at (x + 0.5, y + 0.5).
    float sample(float x, float y) const;
    // Central-difference gradient at the cell containing (x, y). A blocked or
    // out-of-bounds neighbour contributes the cell's own value (no slope
    // towards a wall).
    void gradient(float x, float y, const Mask2D* mask, float& gx, float& gy) const;

    // Explicit diffusion. rate is the fraction of the difference exchanged
    // across each open edge per step; it must be in [0, 0.25] for stability,
    // and larger values are clamped. substeps > 1 repeats the step, so a
    // field can spread further per tick while every step stays stable.
    // Blocked cells are zeroed.
    void diffuse(float rate, const Mask2D* mask = nullptr, int substeps = 1);
    // Multiplies every cell by (1 - fraction); fraction is clamped to [0, 1].
    void decay(float fraction);
    // Clamps every cell to at most `cap`.
    void clampMax(float cap);

    // Sum of every cell, accumulated in double in row order.
    double total() const;
    float maxValue() const;

    const std::vector<float>& values() const { return values_; }
    std::vector<float>& values() { return values_; }

    void hashInto(StateHasher& h) const;

private:
    size_t index(int x, int y) const { return static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x); }
    void diffuseOnce(float rate, const Mask2D* mask);

    int width_ = 0;
    int height_ = 0;
    std::vector<float> values_;
    std::vector<float> scratch_;
};

}  // namespace strata::sim
