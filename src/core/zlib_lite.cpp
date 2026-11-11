#include "zlib_lite.h"

#include "libclip/clip.h"

namespace pu::zlib {

namespace {
std::vector<uint8_t> take(clip_buffer& b) {
    std::vector<uint8_t> v(b.data, b.data + b.size);
    clip_buffer_free(&b);
    return v;
}
}  // namespace

uint32_t crc32(const uint8_t* data, size_t len, uint32_t seed) { return clip_crc32(data, len, seed); }
uint32_t adler32(const uint8_t* data, size_t len, uint32_t seed) { return clip_adler32(data, len, seed); }

Result inflate(const uint8_t* data, size_t len, std::vector<uint8_t>& out) {
    clip_buffer b{};
    clip_error e{};
    Result r;
    if (!clip_zlib_inflate(data, len, &b, &e)) r = Result{false, e.message};
    out = take(b);
    if (!r.ok) out.clear();
    return r;
}

std::vector<uint8_t> deflate_stored(const uint8_t* data, size_t len) { return deflate(data, len, 0); }

std::vector<uint8_t> deflate(const uint8_t* data, size_t len, int level) {
    clip_buffer b{};
    if (!clip_zlib_deflate(data, len, level, &b)) {
        clip_buffer_free(&b);
        return {};
    }
    return take(b);
}

}  // namespace pu::zlib
