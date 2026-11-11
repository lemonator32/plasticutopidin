// PNG reader/writer for the engine: a thin wrapper over libclip's PNG code
// (8-bit, non-interlaced; grey, grey+alpha, RGB, RGBA and palette images are
// read; RGBA is written).  File paths are UTF-8.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "image.h"

namespace pu::png {

bool decode(const uint8_t* data, size_t len, Surface& out, std::string* error = nullptr);
std::vector<uint8_t> encode(const Surface& img);

bool readFile(const std::string& path, Surface& out, std::string* error = nullptr);
bool writeFile(const std::string& path, const Surface& img, std::string* error = nullptr);

}  // namespace pu::png
