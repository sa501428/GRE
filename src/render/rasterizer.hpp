#pragma once

// Scanline polygon rasterizer with analytic horizontal coverage and vertical
// supersampling.  Everything the raster backend draws -- rectangles, strokes,
// glyph outlines -- becomes a path and goes through here, so anti-aliasing is
// consistent across primitives.

#include <functional>
#include <span>
#include <vector>

#include "gre/core/geometry.hpp"
#include "gre/render/canvas.hpp"
#include "render/truetype.hpp"

namespace gre::raster {

// coverage[x - x0] holds the fraction of pixel (x, y) covered, in [0, 1].
using SpanCallback = std::function<void(int y, int x0, int x1, const float* coverage)>;

class PathRasterizer : public ttf::OutlineSink {
public:
    void move_to(double x, double y) override;
    void line_to(double x, double y) override;
    void quad_to(double cx, double cy, double x, double y) override;
    void close() override;

    void cubic_to(double c1x, double c1y, double c2x, double c2y, double x, double y);
    void add_rect(const Rect& rect);
    void add_polygon(std::span<const Point> points);

    void clear();
    [[nodiscard]] bool empty() const noexcept { return edges_.empty(); }
    // Device-space bounds of everything added so far.
    [[nodiscard]] Rect bounds() const noexcept;

    // `samples` is the number of sub-scanlines per pixel row; more gives
    // smoother near-horizontal edges at proportional cost.
    void rasterize(const Rect& clip, FillRule rule, const SpanCallback& emit,
                   int samples = 5) const;

private:
    struct Edge {
        double x0{}, y0{}, x1{}, y1{};  // y0 < y1 after normalisation
        int winding{};
    };

    void add_line(double x0, double y0, double x1, double y1);
    void close_current();

    std::vector<Edge> edges_;
    double start_x_{0.0}, start_y_{0.0};
    double current_x_{0.0}, current_y_{0.0};
    bool has_current_{false};
    double min_x_{0.0}, min_y_{0.0}, max_x_{0.0}, max_y_{0.0};
    bool has_bounds_{false};
};

// Converts a stroked polyline into fillable polygons (one per segment plus
// join and cap geometry).  Kept separate so both dashed and solid strokes
// share it.
void stroke_to_path(std::span<const Point> points, const StrokeStyle& style, bool closed,
                    PathRasterizer& out);

}  // namespace gre::raster
