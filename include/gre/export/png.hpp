#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gre/render/image.hpp"

namespace gre::png {

struct Options {
    // Written as a pHYs chunk so viewers and page layout tools know the
    // intended physical size.  0 omits the chunk.
    int dpi{0};
    // zlib level, 0-9.
    int compression{6};
    // Store RGB instead of RGBA when no pixel is translucent.
    bool allow_rgb{true};
};

[[nodiscard]] std::vector<std::uint8_t> encode(const Image& image, const Options& options = {});

void write(const std::string& path, const Image& image, const Options& options = {});

}  // namespace gre::png
