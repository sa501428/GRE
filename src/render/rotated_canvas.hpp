#pragma once

// Draws a track into a quarter-turned frame on another canvas.
//
// Track code always works in its own space -- x is the genomic axis, y is the
// value axis -- so putting a signal or gene track down the side of a square
// contact map is a matter of remapping coordinates, not of teaching every
// track about orientation.
//
// Only quarter turns are supported.  At other angles an axis-aligned rectangle
// stops mapping to one, and fill_rect / clipping / draw_image all rely on that.

#include "gre/render/canvas.hpp"

namespace gre {

class RotatedCanvas : public Canvas {
public:
    // `origin` is where local (0, 0) lands on the target, and `degrees` is the
    // anti-clockwise page rotation applied to local axes: -90 turns local +x
    // (genomic) into page +y (downwards), which is the left-hand column of a
    // square map.  Must be a multiple of 90.
    RotatedCanvas(Canvas& target, Point origin, double degrees);

    [[nodiscard]] Size size() const override { return target_.size(); }
    [[nodiscard]] double device_scale() const override { return target_.device_scale(); }

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

    [[nodiscard]] Point map(Point local) const;
    [[nodiscard]] Rect map_rect(const Rect& local) const;

private:
    Canvas& target_;
    Point origin_;
    double degrees_;
    int quarters_;  // 0..3, anti-clockwise
};

}  // namespace gre
