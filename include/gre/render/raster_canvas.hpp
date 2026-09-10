#pragma once

#include <vector>

#include "gre/render/canvas.hpp"
#include "gre/render/image.hpp"

namespace gre {

// Draws into an RGBA8 framebuffer.  Figure units are scaled by dpi/72 on the
// way to pixels, so the same figure renders identically at any resolution.
class RasterCanvas : public Canvas {
public:
    RasterCanvas(Size figure_size, double dpi, Color background = colors::white);

    [[nodiscard]] Size size() const override { return size_; }
    [[nodiscard]] double device_scale() const override { return scale_; }
    [[nodiscard]] double dpi() const noexcept { return dpi_; }

    void fill_rect(const Rect& rect, Color color) override;
    void stroke_rect(const Rect& rect, const StrokeStyle& style) override;
    void stroke_line(Point from, Point to, const StrokeStyle& style) override;
    void stroke_polyline(std::span<const Point> points, const StrokeStyle& style) override;
    void fill_polygon(std::span<const Point> points, Color color,
                      FillRule rule = FillRule::nonzero) override;
    void draw_text(Point anchor, std::string_view text, const TextStyle& style) override;
    void draw_image(const Rect& rect, const ImageView& image) override;
    void push_clip(const Rect& rect) override;
    void pop_clip() override;

    [[nodiscard]] const Image& image() const noexcept { return image_; }
    [[nodiscard]] Image& image() noexcept { return image_; }
    [[nodiscard]] Image take_image() { return std::move(image_); }

private:
    [[nodiscard]] Rect device_rect(const Rect& rect) const noexcept;
    [[nodiscard]] const Rect& clip() const noexcept { return clips_.back(); }

    Size size_;
    double dpi_;
    double scale_;
    Image image_;
    std::vector<Rect> clips_;  // device space; back() is the active clip
};

}  // namespace gre
