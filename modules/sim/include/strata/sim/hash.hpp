// strata/sim/hash.hpp -- a state fingerprint for determinism checks.
//
// FNV-1a, 64 bit, over the exact bytes it is fed. Floats are hashed by their
// bit pattern, so two states hash equal only if they are bit-identical. Feed
// values in a fixed order (by entity index, row by row) and the fingerprint is
// the same on every run of the same seed.
//
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace strata::sim {

class StateHasher {
public:
    void bytes(const void* data, size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash_ ^= p[i];
            hash_ *= 0x100000001b3ULL;
        }
    }

    void u8(uint8_t v) { bytes(&v, 1); }
    void u32(uint32_t v) {
        const unsigned char b[4] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8),
                                    static_cast<unsigned char>(v >> 16), static_cast<unsigned char>(v >> 24)};
        bytes(b, 4);
    }
    void u64(uint64_t v) {
        u32(static_cast<uint32_t>(v));
        u32(static_cast<uint32_t>(v >> 32));
    }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        u32(bits);
    }
    void f64(double v) {
        uint64_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        u64(bits);
    }

    uint64_t value() const { return hash_; }

private:
    uint64_t hash_ = 0xcbf29ce484222325ULL;
};

}  // namespace strata::sim
