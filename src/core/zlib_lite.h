// zlib (RFC 1950) / DEFLATE (RFC 1951) for the engine.  A thin C++ wrapper
// over libclip's implementation (libclip/src/clip_zlib.c), so the app and the
// .clip format share one encoder and decoder.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pu::zlib {

struct Result {
    bool ok = true;
    std::string error;
    explicit operator bool() const { return ok; }
};

uint32_t crc32(const uint8_t* data, size_t len, uint32_t seed = 0);
uint32_t adler32(const uint8_t* data, size_t len, uint32_t seed = 1);

// Decode a zlib stream (2 byte header + deflate + adler32 trailer).
Result inflate(const uint8_t* data, size_t len, std::vector<uint8_t>& out);

// Encode `data` as a zlib stream made only of stored blocks.
std::vector<uint8_t> deflate_stored(const uint8_t* data, size_t len);

// Compress `data` into a zlib stream.  `level` 0 = stored only,
// 1 = fastest .. 9 = smallest; 6 is a good default.
std::vector<uint8_t> deflate(const uint8_t* data, size_t len, int level = 6);

}  // namespace pu::zlib
