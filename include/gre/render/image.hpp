#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gre/core/color.hpp"

namespace gre {

enum class ImageScaling {
    // Box-average when shrinking, nearest when enlarging.  Keeps heatmap bins
    // crisp while avoiding aliasing on downsampled data.
    automatic,
    nearest,
    bilinear,
};

// A borrowed view of straight-alpha RGBA8 pixels.
struct ImageView {
    const std::uint8_t* pixels{nullptr};
    int width{0};
    int height{0};
    std::ptrdiff_t stride{0};  // bytes per row
    ImageScaling scaling{ImageScaling::automatic};

    [[nodiscard]] bool empty() const noexcept {
        return pixels == nullptr || width <= 0 || height <= 0;
    }
    [[nodiscard]] const std::uint8_t* row(int y) const noexcept {
        return pixels + static_cast<std::ptrdiff_t>(y) * stride;
    }
};

// An owned RGBA8 buffer.  This is what the raster backend draws into and what
// heatmap tracks hand to draw_image.
class Image {
public:
    Image() = default;
    Image(int width, int height, Color fill = colors::transparent);

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] std::ptrdiff_t stride() const noexcept {
        return static_cast<std::ptrdiff_t>(width_) * 4;
    }
    [[nodiscard]] bool empty() const noexcept { return width_ <= 0 || height_ <= 0; }

    [[nodiscard]] std::uint8_t* data() noexcept { return pixels_.data(); }
    [[nodiscard]] const std::uint8_t* data() const noexcept { return pixels_.data(); }
    [[nodiscard]] std::uint8_t* row(int y) noexcept { return pixels_.data() + y * stride(); }
    [[nodiscard]] const std::uint8_t* row(int y) const noexcept {
        return pixels_.data() + y * stride();
    }

    [[nodiscard]] Color get(int x, int y) const noexcept;
    void set(int x, int y, Color color) noexcept;
    // Source-over composite of `color` at (x, y).
    void blend(int x, int y, Color color) noexcept;

    void fill(Color color) noexcept;

    [[nodiscard]] bool fully_opaque() const noexcept;

    [[nodiscard]] ImageView view(ImageScaling scaling = ImageScaling::automatic) const noexcept {
        return ImageView{pixels_.data(), width_, height_, stride(), scaling};
    }

    // Flattens transparency onto `background`, which is what PNG export does
    // when the caller asked for an opaque figure.
    void flatten_onto(Color background) noexcept;

private:
    int width_{0};
    int height_{0};
    std::vector<std::uint8_t> pixels_;
};

}  // namespace gre
