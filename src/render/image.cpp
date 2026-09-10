#include "gre/render/image.hpp"

#include <algorithm>

#include "gre/core/error.hpp"

namespace gre {

Image::Image(int width, int height, Color fill_color) : width_(width), height_(height) {
    if (width < 0 || height < 0) {
        throw Error(ErrorCode::invalid_argument, "image dimensions cannot be negative");
    }
    pixels_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
    if (!fill_color.transparent()) fill(fill_color);
}

Color Image::get(int x, int y) const noexcept {
    const std::uint8_t* p = row(y) + static_cast<std::ptrdiff_t>(x) * 4;
    return Color{p[0], p[1], p[2], p[3]};
}

void Image::set(int x, int y, Color color) noexcept {
    std::uint8_t* p = row(y) + static_cast<std::ptrdiff_t>(x) * 4;
    p[0] = color.r;
    p[1] = color.g;
    p[2] = color.b;
    p[3] = color.a;
}

void Image::blend(int x, int y, Color color) noexcept {
    if (color.transparent()) return;
    if (color.opaque()) {
        set(x, y, color);
        return;
    }
    set(x, y, composite(get(x, y), color));
}

void Image::fill(Color color) noexcept {
    const std::uint8_t bytes[4] = {color.r, color.g, color.b, color.a};
    for (std::size_t i = 0; i < pixels_.size(); i += 4) {
        pixels_[i] = bytes[0];
        pixels_[i + 1] = bytes[1];
        pixels_[i + 2] = bytes[2];
        pixels_[i + 3] = bytes[3];
    }
}

bool Image::fully_opaque() const noexcept {
    for (std::size_t i = 3; i < pixels_.size(); i += 4) {
        if (pixels_[i] != 255) return false;
    }
    return true;
}

void Image::flatten_onto(Color background) noexcept {
    for (std::size_t i = 0; i < pixels_.size(); i += 4) {
        const Color over{pixels_[i], pixels_[i + 1], pixels_[i + 2], pixels_[i + 3]};
        if (over.opaque()) continue;
        const Color result = composite(background, over);
        pixels_[i] = result.r;
        pixels_[i + 1] = result.g;
        pixels_[i + 2] = result.b;
        pixels_[i + 3] = 255;
    }
}

}  // namespace gre
