#include "gre/render/canvas.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gre {
namespace {

int circle_segments(double radius) {
    return std::clamp(static_cast<int>(std::ceil(radius * 6.0)), 8, 72);
}

std::vector<Point> circle_points(Point center, double radius) {
    const int n = circle_segments(radius);
    std::vector<Point> points;
    points.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double t = 2.0 * std::numbers::pi_v<double> * i / n;
        points.push_back(Point{center.x + radius * std::cos(t), center.y + radius * std::sin(t)});
    }
    return points;
}

}  // namespace

void Canvas::fill_circle(Point center, double radius, Color color) {
    if (radius <= 0.0 || color.transparent()) return;
    const std::vector<Point> points = circle_points(center, radius);
    fill_polygon(points, color);
}

void Canvas::stroke_circle(Point center, double radius, const StrokeStyle& style) {
    if (radius <= 0.0 || style.color.transparent()) return;
    std::vector<Point> points = circle_points(center, radius);
    points.push_back(points.front());
    stroke_polyline(points, style);
}

void Canvas::fill_rect_outlined(const Rect& rect, Color fill, const StrokeStyle& stroke) {
    if (!fill.transparent()) fill_rect(rect, fill);
    if (!stroke.color.transparent() && stroke.width > 0.0) stroke_rect(rect, stroke);
}

void Canvas::fill_arrow(Point tip, double width, double height, int direction, Color color) {
    if (color.transparent() || width <= 0.0 || height <= 0.0) return;
    const double back = tip.x - direction * width;
    const Point points[3] = {
        Point{tip.x, tip.y},
        Point{back, tip.y - height / 2.0},
        Point{back, tip.y + height / 2.0},
    };
    fill_polygon(std::span<const Point>(points, 3), color);
}

TextMetrics Canvas::measure_text(std::string_view text, const TextStyle& style) const {
    return fonts().get(style.font).measure(text, style.size);
}

Rect Canvas::text_extent(Point anchor, std::string_view text, const TextStyle& style) const {
    return text_bounds(fonts().get(style.font), text, style, anchor);
}

std::vector<std::vector<Point>> apply_dash(std::span<const Point> points,
                                           const std::vector<double>& pattern, double phase) {
    std::vector<std::vector<Point>> runs;
    if (points.size() < 2) return runs;
    double total = 0.0;
    for (double d : pattern) total += std::max(0.0, d);
    if (pattern.empty() || total <= 0.0) {
        runs.emplace_back(points.begin(), points.end());
        return runs;
    }

    // Walk the pattern to the requested phase before starting.
    std::size_t index = 0;
    double remaining = pattern[0];
    bool on = true;
    double skip = std::fmod(std::max(0.0, phase), total);
    while (skip > 0.0) {
        if (skip < remaining) {
            remaining -= skip;
            skip = 0.0;
        } else {
            skip -= remaining;
            index = (index + 1) % pattern.size();
            remaining = pattern[index];
            on = !on;
        }
    }

    std::vector<Point> current;
    if (on) current.push_back(points[0]);

    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        Point a = points[i];
        const Point b = points[i + 1];
        double segment = std::hypot(b.x - a.x, b.y - a.y);
        while (segment > 0.0) {
            if (remaining >= segment) {
                remaining -= segment;
                if (on) current.push_back(b);
                segment = 0.0;
            } else {
                const double t = remaining / segment;
                const Point split{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
                if (on) {
                    current.push_back(split);
                    if (current.size() >= 2) runs.push_back(current);
                    current.clear();
                } else {
                    current.clear();
                    current.push_back(split);
                }
                segment -= remaining;
                a = split;
                index = (index + 1) % pattern.size();
                remaining = pattern[index];
                on = !on;
            }
        }
    }
    if (on && current.size() >= 2) runs.push_back(current);
    return runs;
}

}  // namespace gre
