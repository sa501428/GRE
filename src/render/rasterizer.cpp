#include "render/rasterizer.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gre::raster {
namespace {

// Maximum deviation, in device pixels, allowed when flattening curves.
constexpr double kFlatnessTolerance = 0.08;

int quad_segments(double x0, double y0, double cx, double cy, double x1, double y1) {
    const double dx = x0 - 2.0 * cx + x1;
    const double dy = y0 - 2.0 * cy + y1;
    const double deviation = std::hypot(dx, dy);
    if (!(deviation > 0.0)) return 1;
    const int n = static_cast<int>(std::ceil(std::sqrt(deviation / (8.0 * kFlatnessTolerance))));
    return std::clamp(n, 1, 64);
}

int cubic_segments(double x0, double y0, double c1x, double c1y, double c2x, double c2y, double x1,
                   double y1) {
    const double d1 = std::hypot(x0 - 2.0 * c1x + c2x, y0 - 2.0 * c1y + c2y);
    const double d2 = std::hypot(c1x - 2.0 * c2x + x1, c1y - 2.0 * c2y + y1);
    const double deviation = std::max(d1, d2);
    if (!(deviation > 0.0)) return 1;
    const int n = static_cast<int>(std::ceil(std::sqrt(deviation * 3.0 / (4.0 * kFlatnessTolerance))));
    return std::clamp(n, 1, 96);
}

void accumulate_span(float* coverage, int x_lo, int x_hi, double xa, double xb, double weight) {
    xa = std::max(xa, static_cast<double>(x_lo));
    xb = std::min(xb, static_cast<double>(x_hi));
    if (!(xb > xa)) return;
    auto first = static_cast<int>(std::floor(xa));
    auto last = static_cast<int>(std::floor(xb));
    if (first == last) {
        coverage[first - x_lo] += static_cast<float>(weight * (xb - xa));
        return;
    }
    coverage[first - x_lo] += static_cast<float>(weight * (first + 1 - xa));
    for (int x = first + 1; x < last; ++x) {
        coverage[x - x_lo] += static_cast<float>(weight);
    }
    if (last < x_hi) {
        coverage[last - x_lo] += static_cast<float>(weight * (xb - last));
    }
}

struct Crossing {
    double x;
    int winding;
};

Point normal_of(Point a, Point b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length = std::hypot(dx, dy);
    if (length == 0.0) return Point{0.0, 0.0};
    return Point{-dy / length, dx / length};
}

void add_round(PathRasterizer& out, Point center, double radius, double from, double to) {
    constexpr int kArcSegments = 12;
    out.move_to(center.x, center.y);
    for (int i = 0; i <= kArcSegments; ++i) {
        const double t = from + (to - from) * i / kArcSegments;
        out.line_to(center.x + radius * std::cos(t), center.y + radius * std::sin(t));
    }
    out.close();
}

}  // namespace

void PathRasterizer::clear() {
    edges_.clear();
    has_current_ = false;
    has_bounds_ = false;
}

Rect PathRasterizer::bounds() const noexcept {
    if (!has_bounds_) return Rect{};
    return Rect::from_edges(min_x_, min_y_, max_x_, max_y_);
}

void PathRasterizer::add_line(double x0, double y0, double x1, double y1) {
    if (y0 == y1) return;  // horizontal edges never produce crossings
    Edge edge;
    if (y0 < y1) {
        edge = Edge{x0, y0, x1, y1, +1};
    } else {
        edge = Edge{x1, y1, x0, y0, -1};
    }
    edges_.push_back(edge);
}

void PathRasterizer::move_to(double x, double y) {
    close_current();
    start_x_ = current_x_ = x;
    start_y_ = current_y_ = y;
    has_current_ = true;
    if (!has_bounds_) {
        min_x_ = max_x_ = x;
        min_y_ = max_y_ = y;
        has_bounds_ = true;
    } else {
        min_x_ = std::min(min_x_, x);
        max_x_ = std::max(max_x_, x);
        min_y_ = std::min(min_y_, y);
        max_y_ = std::max(max_y_, y);
    }
}

void PathRasterizer::line_to(double x, double y) {
    if (!has_current_) {
        move_to(x, y);
        return;
    }
    add_line(current_x_, current_y_, x, y);
    current_x_ = x;
    current_y_ = y;
    min_x_ = std::min(min_x_, x);
    max_x_ = std::max(max_x_, x);
    min_y_ = std::min(min_y_, y);
    max_y_ = std::max(max_y_, y);
}

void PathRasterizer::quad_to(double cx, double cy, double x, double y) {
    if (!has_current_) {
        move_to(cx, cy);
    }
    const double x0 = current_x_;
    const double y0 = current_y_;
    const int n = quad_segments(x0, y0, cx, cy, x, y);
    for (int i = 1; i <= n; ++i) {
        const double t = static_cast<double>(i) / n;
        const double u = 1.0 - t;
        line_to(u * u * x0 + 2.0 * u * t * cx + t * t * x,
                u * u * y0 + 2.0 * u * t * cy + t * t * y);
    }
}

void PathRasterizer::cubic_to(double c1x, double c1y, double c2x, double c2y, double x, double y) {
    if (!has_current_) move_to(c1x, c1y);
    const double x0 = current_x_;
    const double y0 = current_y_;
    const int n = cubic_segments(x0, y0, c1x, c1y, c2x, c2y, x, y);
    for (int i = 1; i <= n; ++i) {
        const double t = static_cast<double>(i) / n;
        const double u = 1.0 - t;
        line_to(u * u * u * x0 + 3.0 * u * u * t * c1x + 3.0 * u * t * t * c2x + t * t * t * x,
                u * u * u * y0 + 3.0 * u * u * t * c1y + 3.0 * u * t * t * c2y + t * t * t * y);
    }
}

void PathRasterizer::close_current() {
    if (!has_current_) return;
    if (current_x_ != start_x_ || current_y_ != start_y_) {
        add_line(current_x_, current_y_, start_x_, start_y_);
    }
    current_x_ = start_x_;
    current_y_ = start_y_;
    has_current_ = false;
}

void PathRasterizer::close() { close_current(); }

void PathRasterizer::add_rect(const Rect& rect) {
    move_to(rect.left(), rect.top());
    line_to(rect.right(), rect.top());
    line_to(rect.right(), rect.bottom());
    line_to(rect.left(), rect.bottom());
    close();
}

void PathRasterizer::add_polygon(std::span<const Point> points) {
    if (points.size() < 2) return;
    move_to(points[0].x, points[0].y);
    for (std::size_t i = 1; i < points.size(); ++i) line_to(points[i].x, points[i].y);
    close();
}

void PathRasterizer::rasterize(const Rect& clip, FillRule rule, const SpanCallback& emit,
                               int samples) const {
    // A path left open still bounds a region, so add its closing edge here
    // rather than requiring callers to remember close().
    std::vector<Edge> all = edges_;
    if (has_current_ && (current_x_ != start_x_ || current_y_ != start_y_)) {
        if (current_y_ < start_y_) {
            all.push_back(Edge{current_x_, current_y_, start_x_, start_y_, +1});
        } else if (current_y_ > start_y_) {
            all.push_back(Edge{start_x_, start_y_, current_x_, current_y_, -1});
        }
    }
    if (all.empty()) return;
    samples = std::clamp(samples, 1, 64);

    const int clip_x0 = static_cast<int>(std::floor(clip.left()));
    const int clip_y0 = static_cast<int>(std::floor(clip.top()));
    const int clip_x1 = static_cast<int>(std::ceil(clip.right()));
    const int clip_y1 = static_cast<int>(std::ceil(clip.bottom()));
    if (clip_x1 <= clip_x0 || clip_y1 <= clip_y0) return;

    const int x_lo = std::max(clip_x0, static_cast<int>(std::floor(min_x_)));
    const int x_hi = std::min(clip_x1, static_cast<int>(std::ceil(max_x_)) + 1);
    const int y_lo = std::max(clip_y0, static_cast<int>(std::floor(min_y_)));
    const int y_hi = std::min(clip_y1, static_cast<int>(std::ceil(max_y_)) + 1);
    if (x_hi <= x_lo || y_hi <= y_lo) return;

    std::sort(all.begin(), all.end(), [](const Edge& a, const Edge& b) { return a.y0 < b.y0; });

    const auto width = static_cast<std::size_t>(x_hi - x_lo);
    std::vector<float> coverage(width);
    std::vector<const Edge*> active;
    std::vector<Crossing> crossings;
    std::size_t next_edge = 0;
    const double weight = 1.0 / samples;

    for (int y = y_lo; y < y_hi; ++y) {
        const double row_top = y;
        const double row_bottom = y + 1.0;

        while (next_edge < all.size() && all[next_edge].y0 < row_bottom) {
            active.push_back(&all[next_edge++]);
        }
        active.erase(std::remove_if(active.begin(), active.end(),
                                    [row_top](const Edge* e) { return e->y1 <= row_top; }),
                     active.end());
        if (active.empty()) {
            continue;
        }

        std::fill(coverage.begin(), coverage.end(), 0.0f);
        bool any = false;

        for (int s = 0; s < samples; ++s) {
            const double sample_y = row_top + (s + 0.5) * weight;
            crossings.clear();
            for (const Edge* edge : active) {
                if (sample_y < edge->y0 || sample_y >= edge->y1) continue;
                const double t = (sample_y - edge->y0) / (edge->y1 - edge->y0);
                crossings.push_back(
                    Crossing{edge->x0 + t * (edge->x1 - edge->x0), edge->winding});
            }
            if (crossings.size() < 2) continue;
            std::sort(crossings.begin(), crossings.end(),
                      [](const Crossing& a, const Crossing& b) { return a.x < b.x; });

            int winding = 0;
            double span_start = 0.0;
            bool inside = false;
            for (const Crossing& crossing : crossings) {
                const bool was_inside = inside;
                if (rule == FillRule::nonzero) {
                    winding += crossing.winding;
                    inside = winding != 0;
                } else {
                    winding ^= 1;
                    inside = winding != 0;
                }
                if (!was_inside && inside) {
                    span_start = crossing.x;
                } else if (was_inside && !inside) {
                    accumulate_span(coverage.data(), x_lo, x_hi, span_start, crossing.x, weight);
                    any = true;
                }
            }
        }

        if (any) emit(y, x_lo, x_hi, coverage.data());
    }
}

void stroke_to_path(std::span<const Point> points, const StrokeStyle& style, bool closed,
                    PathRasterizer& out) {
    if (points.size() < 2) {
        // A degenerate polyline still paints a dot for round and square caps.
        if (points.size() == 1 && style.cap != LineCap::butt) {
            const double radius = style.width / 2.0;
            if (style.cap == LineCap::round) {
                add_round(out, points[0], radius, 0.0, 2.0 * std::numbers::pi_v<double>);
            } else {
                out.add_rect(Rect{points[0].x - radius, points[0].y - radius, style.width,
                                  style.width});
            }
        }
        return;
    }

    const double half = std::max(style.width, 1e-6) / 2.0;

    // Each segment contributes a quad; joins and caps are added as separate
    // shapes.  Overlap is harmless under the nonzero fill rule.
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        Point a = points[i];
        Point b = points[i + 1];
        Point n = normal_of(a, b);
        if (n.x == 0.0 && n.y == 0.0) continue;

        if (!closed && style.cap == LineCap::square) {
            const double dx = (b.x - a.x);
            const double dy = (b.y - a.y);
            const double length = std::hypot(dx, dy);
            const Point unit{dx / length, dy / length};
            if (i == 0) {
                a = Point{a.x - unit.x * half, a.y - unit.y * half};
            }
            if (i + 2 == points.size()) {
                b = Point{b.x + unit.x * half, b.y + unit.y * half};
            }
        }

        out.move_to(a.x + n.x * half, a.y + n.y * half);
        out.line_to(b.x + n.x * half, b.y + n.y * half);
        out.line_to(b.x - n.x * half, b.y - n.y * half);
        out.line_to(a.x - n.x * half, a.y - n.y * half);
        out.close();
    }

    // Joins: a disc at the interior vertices covers every join style well
    // enough at the line widths figures use, and never spikes the way an
    // unclamped miter does.
    const std::size_t first_join = closed ? 0 : 1;
    const std::size_t last_join = closed ? points.size() : points.size() - 1;
    if (style.width > 1.2) {
        for (std::size_t i = first_join; i < last_join; ++i) {
            add_round(out, points[i % points.size()], half, 0.0,
                      2.0 * std::numbers::pi_v<double>);
        }
    }

    if (!closed && style.cap == LineCap::round) {
        add_round(out, points.front(), half, 0.0, 2.0 * std::numbers::pi_v<double>);
        add_round(out, points.back(), half, 0.0, 2.0 * std::numbers::pi_v<double>);
    }
    if (closed) {
        // Close the loop with the wrap-around segment.
        Point a = points.back();
        Point b = points.front();
        Point n = normal_of(a, b);
        if (n.x != 0.0 || n.y != 0.0) {
            out.move_to(a.x + n.x * half, a.y + n.y * half);
            out.line_to(b.x + n.x * half, b.y + n.y * half);
            out.line_to(b.x - n.x * half, b.y - n.y * half);
            out.line_to(a.x - n.x * half, a.y - n.y * half);
            out.close();
        }
    }
}

}  // namespace gre::raster
