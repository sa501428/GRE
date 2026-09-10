#include "gre/render/raster_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "gre/core/error.hpp"
#include "render/rasterizer.hpp"
#include "render/truetype.hpp"

namespace gre {
namespace {

// Text is rasterized with more sub-scanlines than filled shapes: glyph stems
// are near-horizontal at small sizes and show banding otherwise.
constexpr int kShapeSamples = 5;
constexpr int kTextSamples = 16;

double axis_coverage(double pixel, double lo, double hi) {
    return std::clamp(std::min(pixel + 1.0, hi) - std::max(pixel, lo), 0.0, 1.0);
}

void fill_path(Image& image, const Rect& clip, const raster::PathRasterizer& path, Color color,
               FillRule rule, int samples) {
    if (color.transparent()) return;
    const double alpha = color.a / 255.0;
    path.rasterize(
        clip, rule,
        [&](int y, int x0, int x1, const float* coverage) {
            if (y < 0 || y >= image.height()) return;
            const double row_clip = axis_coverage(y, clip.top(), clip.bottom());
            if (row_clip <= 0.0) return;
            const int begin = std::max(x0, 0);
            const int end = std::min(x1, image.width());
            for (int x = begin; x < end; ++x) {
                double c = coverage[x - x0];
                if (c <= 0.0) continue;
                c *= row_clip * axis_coverage(x, clip.left(), clip.right());
                if (c <= 0.0) continue;
                Color source = color;
                source.a = static_cast<std::uint8_t>(
                    std::lround(std::clamp(c, 0.0, 1.0) * alpha * 255.0));
                image.blend(x, y, source);
            }
        },
        samples);
}

}  // namespace

RasterCanvas::RasterCanvas(Size figure_size, double dpi, Color background)
    : size_(figure_size), dpi_(dpi), scale_(dpi / kPointsPerInch) {
    if (!(dpi > 0.0)) throw Error(ErrorCode::invalid_argument, "dpi must be positive");
    const int width = std::max(1, static_cast<int>(std::lround(figure_size.width * scale_)));
    const int height = std::max(1, static_cast<int>(std::lround(figure_size.height * scale_)));
    image_ = Image(width, height, background);
    clips_.push_back(Rect{0.0, 0.0, static_cast<double>(width), static_cast<double>(height)});
}

Rect RasterCanvas::device_rect(const Rect& rect) const noexcept {
    return Rect{rect.x * scale_, rect.y * scale_, rect.width * scale_, rect.height * scale_};
}

void RasterCanvas::push_clip(const Rect& rect) {
    clips_.push_back(clips_.back().intersect(device_rect(rect)));
}

void RasterCanvas::pop_clip() {
    if (clips_.size() > 1) clips_.pop_back();
}

void RasterCanvas::fill_rect(const Rect& rect, Color color) {
    if (color.transparent() || rect.empty()) return;
    raster::PathRasterizer path;
    path.add_rect(device_rect(rect));
    fill_path(image_, clip(), path, color, FillRule::nonzero, kShapeSamples);
}

void RasterCanvas::stroke_rect(const Rect& rect, const StrokeStyle& style) {
    if (style.color.transparent() || style.width <= 0.0) return;
    const Point corners[4] = {
        {rect.left(), rect.top()},
        {rect.right(), rect.top()},
        {rect.right(), rect.bottom()},
        {rect.left(), rect.bottom()},
    };
    if (style.dashed()) {
        std::vector<Point> loop(corners, corners + 4);
        loop.push_back(corners[0]);
        stroke_polyline(loop, style);
        return;
    }
    StrokeStyle device_style = style;
    device_style.width = style.width * scale_;
    std::vector<Point> device_points;
    device_points.reserve(4);
    for (const Point& p : corners) device_points.push_back({p.x * scale_, p.y * scale_});

    raster::PathRasterizer path;
    raster::stroke_to_path(device_points, device_style, /*closed=*/true, path);
    fill_path(image_, clip(), path, style.color, FillRule::nonzero, kShapeSamples);
}

void RasterCanvas::stroke_line(Point from, Point to, const StrokeStyle& style) {
    const Point points[2] = {from, to};
    stroke_polyline(std::span<const Point>(points, 2), style);
}

void RasterCanvas::stroke_polyline(std::span<const Point> points, const StrokeStyle& style) {
    if (points.size() < 2 || style.color.transparent() || style.width <= 0.0) return;

    StrokeStyle device_style = style;
    device_style.width = std::max(style.width * scale_, 0.6);  // keep hairlines visible

    const auto stroke_run = [&](std::span<const Point> run) {
        std::vector<Point> device_points;
        device_points.reserve(run.size());
        for (const Point& p : run) device_points.push_back({p.x * scale_, p.y * scale_});
        raster::PathRasterizer path;
        raster::stroke_to_path(device_points, device_style, /*closed=*/false, path);
        fill_path(image_, clip(), path, style.color, FillRule::nonzero, kShapeSamples);
    };

    if (style.dashed()) {
        for (const std::vector<Point>& run : apply_dash(points, style.dash, style.dash_phase)) {
            stroke_run(run);
        }
        return;
    }
    stroke_run(points);
}

void RasterCanvas::fill_polygon(std::span<const Point> points, Color color, FillRule rule) {
    if (points.size() < 3 || color.transparent()) return;
    raster::PathRasterizer path;
    std::vector<Point> device_points;
    device_points.reserve(points.size());
    for (const Point& p : points) device_points.push_back({p.x * scale_, p.y * scale_});
    path.add_polygon(device_points);
    fill_path(image_, clip(), path, color, rule, kShapeSamples);
}

void RasterCanvas::draw_text(Point anchor, std::string_view text, const TextStyle& style) {
    if (text.empty() || style.color.transparent() || style.size <= 0.0) return;
    const Font& font = fonts().get(style.font);
    const Point origin = text_origin(font, text, style, anchor);
    const auto glyphs = font.shape(text, style.size);
    if (glyphs.empty()) return;

    const ttf::Face& face = font.face();
    const double units = static_cast<double>(face.metrics().units_per_em);
    const double glyph_scale = style.size * scale_ / units;

    // Font outlines are y-up; figure space is y-down, hence the negative d.
    const double radians = style.rotation * std::numbers::pi_v<double> / 180.0;
    const double c = std::cos(radians);
    const double s = std::sin(radians);

    raster::PathRasterizer path;
    for (const PositionedGlyph& glyph : glyphs) {
        if (glyph.gid == 0) continue;
        // Advance along the (rotated) baseline, then place the glyph.
        const double pen_x = glyph.x * scale_;
        const double dx = pen_x * c;
        const double dy = -pen_x * s;
        const ttf::Affine transform{glyph_scale * c,
                                    -glyph_scale * s,
                                    -glyph_scale * s,
                                    -glyph_scale * c,
                                    origin.x * scale_ + dx,
                                    origin.y * scale_ + dy};
        face.outline(glyph.gid, transform, path);
    }
    fill_path(image_, clip(), path, style.color, FillRule::nonzero, kTextSamples);
}

void RasterCanvas::draw_image(const Rect& rect, const ImageView& source) {
    if (source.empty() || rect.empty()) return;
    const Rect destination = device_rect(rect);
    const Rect region = destination.intersect(clip());
    if (region.empty()) return;

    const int x0 = std::max(0, static_cast<int>(std::floor(region.left())));
    const int y0 = std::max(0, static_cast<int>(std::floor(region.top())));
    const int x1 = std::min(image_.width(), static_cast<int>(std::ceil(region.right())));
    const int y1 = std::min(image_.height(), static_cast<int>(std::ceil(region.bottom())));
    if (x1 <= x0 || y1 <= y0) return;

    const double scale_x = source.width / destination.width;
    const double scale_y = source.height / destination.height;

    ImageScaling mode = source.scaling;
    if (mode == ImageScaling::automatic) {
        mode = (scale_x > 1.2 || scale_y > 1.2) ? ImageScaling::bilinear : ImageScaling::nearest;
    }
    // "bilinear" when shrinking means a box average over the source footprint,
    // which is what actually removes aliasing from downsampled matrices.
    const bool box_filter = mode == ImageScaling::bilinear && (scale_x > 1.0 || scale_y > 1.0);

    const auto sample_nearest = [&](double u, double v) {
        const int sx = std::clamp(static_cast<int>(u), 0, source.width - 1);
        const int sy = std::clamp(static_cast<int>(v), 0, source.height - 1);
        const std::uint8_t* p = source.row(sy) + static_cast<std::ptrdiff_t>(sx) * 4;
        return Color{p[0], p[1], p[2], p[3]};
    };

    const auto sample_bilinear = [&](double u, double v) {
        const double fx = std::clamp(u - 0.5, 0.0, source.width - 1.0);
        const double fy = std::clamp(v - 0.5, 0.0, source.height - 1.0);
        const int ix = static_cast<int>(fx);
        const int iy = static_cast<int>(fy);
        const int ix1 = std::min(ix + 1, source.width - 1);
        const int iy1 = std::min(iy + 1, source.height - 1);
        const double tx = fx - ix;
        const double ty = fy - iy;
        double acc[4] = {0, 0, 0, 0};
        const auto add = [&](int sx, int sy, double weight) {
            const std::uint8_t* p = source.row(sy) + static_cast<std::ptrdiff_t>(sx) * 4;
            const double a = p[3] / 255.0;
            acc[0] += p[0] * a * weight;
            acc[1] += p[1] * a * weight;
            acc[2] += p[2] * a * weight;
            acc[3] += p[3] * weight;
        };
        add(ix, iy, (1 - tx) * (1 - ty));
        add(ix1, iy, tx * (1 - ty));
        add(ix, iy1, (1 - tx) * ty);
        add(ix1, iy1, tx * ty);
        const double alpha = acc[3] / 255.0;
        if (alpha <= 0.0) return colors::transparent;
        return Color{static_cast<std::uint8_t>(std::lround(std::clamp(acc[0] / alpha, 0.0, 255.0))),
                     static_cast<std::uint8_t>(std::lround(std::clamp(acc[1] / alpha, 0.0, 255.0))),
                     static_cast<std::uint8_t>(std::lround(std::clamp(acc[2] / alpha, 0.0, 255.0))),
                     static_cast<std::uint8_t>(std::lround(std::clamp(acc[3], 0.0, 255.0)))};
    };

    const auto sample_box = [&](double u0, double v0, double u1, double v1) {
        const int sx0 = std::clamp(static_cast<int>(std::floor(u0)), 0, source.width - 1);
        const int sy0 = std::clamp(static_cast<int>(std::floor(v0)), 0, source.height - 1);
        const int sx1 = std::clamp(static_cast<int>(std::ceil(u1)), sx0 + 1, source.width);
        const int sy1 = std::clamp(static_cast<int>(std::ceil(v1)), sy0 + 1, source.height);
        double acc[4] = {0, 0, 0, 0};
        double weight_total = 0.0;
        for (int sy = sy0; sy < sy1; ++sy) {
            const double wy = axis_coverage(sy, v0, v1);
            if (wy <= 0.0) continue;
            const std::uint8_t* row = source.row(sy);
            for (int sx = sx0; sx < sx1; ++sx) {
                const double w = wy * axis_coverage(sx, u0, u1);
                if (w <= 0.0) continue;
                const std::uint8_t* p = row + static_cast<std::ptrdiff_t>(sx) * 4;
                const double a = p[3] / 255.0;
                acc[0] += p[0] * a * w;
                acc[1] += p[1] * a * w;
                acc[2] += p[2] * a * w;
                acc[3] += p[3] * w;
                weight_total += w;
            }
        }
        if (weight_total <= 0.0) return colors::transparent;
        const double alpha = acc[3] / weight_total / 255.0;
        if (alpha <= 0.0) return colors::transparent;
        const double norm = weight_total * alpha;
        return Color{
            static_cast<std::uint8_t>(std::lround(std::clamp(acc[0] / norm, 0.0, 255.0))),
            static_cast<std::uint8_t>(std::lround(std::clamp(acc[1] / norm, 0.0, 255.0))),
            static_cast<std::uint8_t>(std::lround(std::clamp(acc[2] / norm, 0.0, 255.0))),
            static_cast<std::uint8_t>(
                std::lround(std::clamp(acc[3] / weight_total, 0.0, 255.0)))};
    };

    for (int y = y0; y < y1; ++y) {
        const double row_weight = axis_coverage(y, region.top(), region.bottom());
        if (row_weight <= 0.0) continue;
        const double v0 = (y - destination.top()) * scale_y;
        const double v1 = (y + 1 - destination.top()) * scale_y;
        for (int x = x0; x < x1; ++x) {
            const double weight = row_weight * axis_coverage(x, region.left(), region.right());
            if (weight <= 0.0) continue;
            const double u0 = (x - destination.left()) * scale_x;
            const double u1 = (x + 1 - destination.left()) * scale_x;

            Color color;
            if (box_filter) {
                color = sample_box(u0, v0, u1, v1);
            } else if (mode == ImageScaling::bilinear) {
                color = sample_bilinear((u0 + u1) / 2.0, (v0 + v1) / 2.0);
            } else {
                color = sample_nearest((u0 + u1) / 2.0, (v0 + v1) / 2.0);
            }
            if (color.transparent()) continue;
            if (weight < 1.0) {
                color.a = static_cast<std::uint8_t>(std::lround(color.a * weight));
            }
            image_.blend(x, y, color);
        }
    }
}

}  // namespace gre
