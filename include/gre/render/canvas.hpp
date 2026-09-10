#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "gre/core/color.hpp"
#include "gre/core/geometry.hpp"
#include "gre/render/image.hpp"
#include "gre/render/text.hpp"

namespace gre {

enum class FillRule { nonzero, even_odd };
enum class LineCap { butt, round, square };
enum class LineJoin { miter, round, bevel };

struct StrokeStyle {
    Color color{colors::black};
    double width{0.75};
    LineCap cap{LineCap::butt};
    LineJoin join{LineJoin::miter};
    // Dash lengths in figure units; empty means solid.
    std::vector<double> dash;
    double dash_phase{0.0};

    [[nodiscard]] bool dashed() const noexcept { return !dash.empty(); }
};

// The drawing surface tracks see.  Deliberately small: everything a genomics
// figure needs, and nothing that would leak a particular graphics library into
// track code.  All coordinates are figure units (72 per inch, y downwards).
class Canvas {
public:
    virtual ~Canvas() = default;

    [[nodiscard]] virtual Size size() const = 0;

    // Device pixels per figure unit.  1.0 for the vector backends; dpi/72 for
    // raster.  Tracks use it to size data requests, never to place geometry.
    [[nodiscard]] virtual double device_scale() const { return 1.0; }

    virtual void fill_rect(const Rect& rect, Color color) = 0;
    virtual void stroke_rect(const Rect& rect, const StrokeStyle& style) = 0;
    virtual void stroke_line(Point from, Point to, const StrokeStyle& style) = 0;
    virtual void stroke_polyline(std::span<const Point> points, const StrokeStyle& style) = 0;
    virtual void fill_polygon(std::span<const Point> points, Color color,
                              FillRule rule = FillRule::nonzero) = 0;
    virtual void draw_text(Point anchor, std::string_view text, const TextStyle& style) = 0;
    virtual void draw_image(const Rect& rect, const ImageView& image) = 0;

    // Clips are rectangular and nest.  Tracks are clipped to their own rect by
    // the figure, so a track cannot bleed into its neighbours.
    virtual void push_clip(const Rect& rect) = 0;
    virtual void pop_clip() = 0;

    // Helpers built on the primitives above.
    void fill_circle(Point center, double radius, Color color);
    void stroke_circle(Point center, double radius, const StrokeStyle& style);
    void fill_rect_outlined(const Rect& rect, Color fill, const StrokeStyle& stroke);
    // A triangle pointing right (+1) or left (-1); used for strand arrows.
    void fill_arrow(Point tip, double width, double height, int direction, Color color);

    [[nodiscard]] TextMetrics measure_text(std::string_view text, const TextStyle& style) const;
    [[nodiscard]] Rect text_extent(Point anchor, std::string_view text,
                                   const TextStyle& style) const;
};

// RAII clip; keeps push/pop balanced when a draw method returns early.
class ClipGuard {
public:
    ClipGuard(Canvas& canvas, const Rect& rect) : canvas_(&canvas) { canvas.push_clip(rect); }
    ~ClipGuard() {
        if (canvas_ != nullptr) canvas_->pop_clip();
    }
    ClipGuard(const ClipGuard&) = delete;
    ClipGuard& operator=(const ClipGuard&) = delete;
    ClipGuard(ClipGuard&& other) noexcept : canvas_(other.canvas_) { other.canvas_ = nullptr; }
    ClipGuard& operator=(ClipGuard&&) = delete;

private:
    Canvas* canvas_;
};

// Splits a polyline into the "on" runs of a dash pattern.  Shared by the
// backends that have no native dashing.
[[nodiscard]] std::vector<std::vector<Point>> apply_dash(std::span<const Point> points,
                                                         const std::vector<double>& pattern,
                                                         double phase);

}  // namespace gre
