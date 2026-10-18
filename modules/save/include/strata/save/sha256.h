// SHA-256 (FIPS 180-4). Saves are named and checked by it (RFC 0019 §6): the
// hash is verified BEFORE the USD parser sees a byte, because a damaged crate
// file can make the parser load wrong state silently or try to allocate
// hundreds of GB.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace strata::save {

class Sha256 {
public:
    Sha256() { reset(); }
    void reset();
    void update(const void* data, size_t n);
    std::array<uint8_t, 32> finish();  // resets afterwards

private:
    void block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buf_[64];
    size_t used_ = 0;
    uint64_t total_ = 0;
};

std::string toHex(const std::array<uint8_t, 32>& d);
std::string sha256Hex(const void* data, size_t n);
std::string sha256Hex(const std::string& s);

// Hash a file's bytes as they are on disk. Returns false if it cannot be read.
// `size` (optional) receives the byte count.
bool sha256File(const std::string& path, std::string& hex, uint64_t* size = nullptr);

}  // namespace strata::save
