#include "render/rotated_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "gre/core/error.hpp"

namespace gre {
namespace {

// Rotates the pixels so the caller can keep using an axis-aligned draw_image
// on the target.  `quarters` counts anti-clockwise quarter turns of the page.
Image rotate_image(const ImageView& source, int quarters) {
    if (quarters == 0) {
        Image copy(source.width, source.height);
        for (int y = 0; y < source.height; ++y) {
            std::copy(source.row(y), source.row(y) + static_cast<std::ptrdiff_t>(source.width) * 4,
                      copy.row(y));
        }
        return copy;
    }

    const bool swapped = quarters % 2 == 1;
    const int width = swapped ? source.height : source.width;
    const int height = swapped ? source.width : source.height;
    Image out(width, height);

    for (int y = 0; y < height; ++y) {
        std::uint8_t* destination = out.row(y);
        for (int x = 0; x < width; ++x) {
            int sx = 0;
            int sy = 0;
            switch (quarters) {
                case 1:  // local +x -> page -y
                    sx = source.width - 1 - y;
                    sy = x;
                    break;
                case 2:
                    sx = source.width - 1 - x;
                    sy = source.height - 1 - y;
                    break;
                default:  // 3: local +x -> page +y
                    sx = y;
                    sy = source.height - 1 - x;
                    break;
            }
            const std::uint8_t* pixel = source.row(sy) + static_cast<std::ptrdiff_t>(sx) * 4;
            std::copy(pixel, pixel + 4, destination + static_cast<std::ptrdiff_t>(x) * 4);
        }
    }
    return out;
}

}  // namespace

RotatedCanvas::RotatedCanvas(Canvas& target, Point origin, double degrees)
    : target_(target), origin_(origin), degrees_(degrees) {
    const double normalised = std::fmod(std::fmod(degrees, 360.0) + 360.0, 360.0);
    const double turns = normalised / 90.0;
    if (std::fabs(turns - std::round(turns)) > 1e-9) {
        throw Error(ErrorCode::invalid_argument,
                    "RotatedCanvas only supports quarter turns");
    }
    quarters_ = static_cast<int>(std::lround(turns)) % 4;
}

Point RotatedCanvas::map(Point local) const {
    switch (quarters_) {
        case 1:  // 90 degrees anti-clockwise on the page
            return Point{origin_.x + local.y, origin_.y - local.x};
        case 2:
            return Point{origin_.x - local.x, origin_.y - local.y};
        case 3:  // 90 degrees clockwise: local +x runs down the page
            return Point{origin_.x - local.y, origin_.y + local.x};
        default:
            return Point{origin_.x + local.x, origin_.y + local.y};
    }
}

Rect RotatedCanvas::map_rect(const Rect& local) const {
    const Point a = map(Point{local.left(), local.top()});
    const Point b = map(Point{local.right(), local.bottom()});
    return Rect::from_edges(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x),
                            std::max(a.y, b.y));
}

void RotatedCanvas::fill_rect(const Rect& rect, Color color) {
    target_.fill_rect(map_rect(rect), color);
}

void RotatedCanvas::stroke_rect(const Rect& rect, const StrokeStyle& style) {
    target_.stroke_rect(map_rect(rect), style);
}

void RotatedCanvas::stroke_line(Point from, Point to, const StrokeStyle& style) {
    target_.stroke_line(map(from), map(to), style);
}

void RotatedCanvas::stroke_polyline(std::span<const Point> points, const StrokeStyle& style) {
    std::vector<Point> mapped;
    mapped.reserve(points.size());
    for (const Point& point : points) mapped.push_back(map(point));
    target_.stroke_polyline(mapped, style);
}

void RotatedCanvas::fill_polygon(std::span<const Point> points, Color color, FillRule rule) {
    std::vector<Point> mapped;
    mapped.reserve(points.size());
    for (const Point& point : points) mapped.push_back(map(point));
    target_.fill_polygon(mapped, color, rule);
}

void RotatedCanvas::draw_text(Point anchor, std::string_view text, const TextStyle& style) {
    TextStyle rotated = style;
    // Alignment resolves in the target's frame; composing the rotations there
    // gives the same result as aligning locally and then mapping.
    rotated.rotation += degrees_;
    target_.draw_text(map(anchor), text, rotated);
}

void RotatedCanvas::draw_image(const Rect& rect, const ImageView& image) {
    if (image.empty()) return;
    const Image turned = rotate_image(image, quarters_);
    target_.draw_image(map_rect(rect), turned.view(image.scaling));
}

void RotatedCanvas::push_clip(const Rect& rect) { target_.push_clip(map_rect(rect)); }

void RotatedCanvas::pop_clip() { target_.pop_clip(); }

}  // namespace gre
