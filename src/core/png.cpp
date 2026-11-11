#include "png.h"

#include <cstring>

#include "libclip/clip.h"

namespace pu::png {

static_assert(sizeof(Color) == 4, "Surface pixels must be tightly packed RGBA8");

namespace {
bool setErr(std::string* e, const char* msg) {
    if (e) *e = msg;
    return false;
}
}  // namespace

bool decode(const uint8_t* data, size_t len, Surface& out, std::string* error) {
    clip_image img{};
    clip_error e{};
    if (!clip_png_decode(data, len, &img, &e)) return setErr(error, e.message);
    out.reset(img.width, img.height);
    std::memcpy(out.bytes(), img.pixels, out.byteSize());
    clip_image_free(&img);
    return true;
}

std::vector<uint8_t> encode(const Surface& img) {
    clip_image view{img.width(), img.height(), const_cast<uint8_t*>(img.bytes())};  // read only
    clip_buffer b{};
    std::vector<uint8_t> out;
    if (clip_png_encode(&view, &b)) out.assign(b.data, b.data + b.size);
    clip_buffer_free(&b);
    return out;
}

bool readFile(const std::string& path, Surface& out, std::string* error) {
    clip_buffer b{};
    clip_error e{};
    if (!clip_read_file(path.c_str(), &b, &e)) return setErr(error, e.message);
    bool ok = decode(b.data, b.size, out, error);
    clip_buffer_free(&b);
    return ok;
}

bool writeFile(const std::string& path, const Surface& img, std::string* error) {
    std::vector<uint8_t> data = encode(img);
    if (data.empty()) return setErr(error, "cannot encode PNG");
    clip_error e{};
    return clip_write_file(path.c_str(), data.data(), data.size(), &e) || setErr(error, e.message);
}

}  // namespace pu::png
