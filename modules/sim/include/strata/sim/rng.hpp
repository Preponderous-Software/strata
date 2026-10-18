// strata/sim/rng.hpp -- a seeded, platform-independent random number generator.
//
// PCG32 (O'Neill, XSH RR 64/32). The standard library's engines are portable
// but its distributions are not: std::uniform_int_distribution may give
// different numbers on libstdc++, libc++ and MSVC for the same seed. Every
// helper here is defined bit for bit, so a seed gives the same sequence on
// every compiler and platform.
//
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#pragma once

#include <cmath>
#include <cstdint>

namespace strata::sim {

class Rng {
public:
    // `stream` selects one of 2^63 independent sequences for the same seed.
    explicit Rng(uint64_t seed = 0x853c49e6748fea9bULL, uint64_t stream = 0xda3e39cb94b95bdbULL) { reseed(seed, stream); }

    void reseed(uint64_t seed, uint64_t stream = 0xda3e39cb94b95bdbULL) {
        state_ = 0u;
        inc_ = (stream << 1u) | 1u;
        nextU32();
        state_ += seed;
        nextU32();
    }

    uint32_t nextU32() {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    // Uniform in [0, bound) without modulo bias. bound 0 returns 0.
    uint32_t below(uint32_t bound) {
        if (bound == 0u) return 0u;
        const uint32_t threshold = (0u - bound) % bound;
        for (;;) {
            const uint32_t r = nextU32();
            if (r >= threshold) return r % bound;
        }
    }

    // Uniform integer in [lo, hi] (inclusive). hi < lo returns lo.
    int32_t range(int32_t lo, int32_t hi) {
        if (hi <= lo) return lo;
        return lo + static_cast<int32_t>(below(static_cast<uint32_t>(hi - lo) + 1u));
    }

    // Uniform float in [0, 1): the top 24 bits, so every value is exact.
    float uniform() { return static_cast<float>(nextU32() >> 8u) * (1.0f / 16777216.0f); }

    // Uniform float in [lo, hi).
    float uniform(float lo, float hi) { return lo + (hi - lo) * uniform(); }

    // True with probability p (p <= 0 never, p >= 1 always).
    bool chance(float p) { return uniform() < p; }

    // A uniformly distributed unit vector in the plane. Uses rejection
    // sampling and one square root (correctly rounded under IEEE 754), never
    // sin or cos, whose last bits differ between C libraries.
    void unitVector(float& x, float& y) {
        for (;;) {
            const float a = uniform(-1.0f, 1.0f);
            const float b = uniform(-1.0f, 1.0f);
            const float r2 = a * a + b * b;
            if (r2 > 1.0e-4f && r2 <= 1.0f) {
                const float inv = 1.0f / std::sqrt(r2);
                x = a * inv;
                y = b * inv;
                return;
            }
        }
    }

    uint64_t state() const { return state_; }
    uint64_t increment() const { return inc_; }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 1;
};

}  // namespace strata::sim
