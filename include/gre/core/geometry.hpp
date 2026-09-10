#pragma once

// Geometry in *figure coordinates*: logical units where 72 units = 1 inch,
// with the origin at the top-left of the page and y increasing downwards.
// Backends convert to their own device space (pixels for raster, PDF points
// with a y-flip for PDF).

#include <algorithm>

namespace gre {

struct Point {
    double x{};
    double y{};
};

struct Size {
    double width{};
    double height{};

    [[nodiscard]] bool empty() const noexcept { return width <= 0.0 || height <= 0.0; }
};

struct Insets {
    double top{};
    double right{};
    double bottom{};
    double left{};

    constexpr Insets() = default;
    constexpr explicit Insets(double all) noexcept
        : top(all), right(all), bottom(all), left(all) {}
    constexpr Insets(double vertical, double horizontal) noexcept
        : top(vertical), right(horizontal), bottom(vertical), left(horizontal) {}
    constexpr Insets(double t, double r, double b, double l) noexcept
        : top(t), right(r), bottom(b), left(l) {}

    [[nodiscard]] constexpr double horizontal() const noexcept { return left + right; }
    [[nodiscard]] constexpr double vertical() const noexcept { return top + bottom; }
};

struct Rect {
    double x{};
    double y{};
    double width{};
    double height{};

    [[nodiscard]] constexpr double left() const noexcept { return x; }
    [[nodiscard]] constexpr double top() const noexcept { return y; }
    [[nodiscard]] constexpr double right() const noexcept { return x + width; }
    [[nodiscard]] constexpr double bottom() const noexcept { return y + height; }
    [[nodiscard]] constexpr double center_x() const noexcept { return x + width / 2.0; }
    [[nodiscard]] constexpr double center_y() const noexcept { return y + height / 2.0; }
    [[nodiscard]] constexpr bool empty() const noexcept { return width <= 0.0 || height <= 0.0; }

    [[nodiscard]] static constexpr Rect from_edges(double x0, double y0, double x1,
                                                   double y1) noexcept {
        return Rect{x0, y0, x1 - x0, y1 - y0};
    }

    [[nodiscard]] Rect inset(const Insets& i) const noexcept {
        return Rect{x + i.left, y + i.top, width - i.horizontal(), height - i.vertical()};
    }

    [[nodiscard]] Rect inset(double all) const noexcept { return inset(Insets{all}); }

    [[nodiscard]] Rect translated(double dx, double dy) const noexcept {
        return Rect{x + dx, y + dy, width, height};
    }

    [[nodiscard]] Rect intersect(const Rect& other) const noexcept {
        const double x0 = std::max(left(), other.left());
        const double y0 = std::max(top(), other.top());
        const double x1 = std::min(right(), other.right());
        const double y1 = std::min(bottom(), other.bottom());
        if (x1 <= x0 || y1 <= y0) return Rect{x0, y0, 0.0, 0.0};
        return Rect::from_edges(x0, y0, x1, y1);
    }

    [[nodiscard]] Rect united(const Rect& other) const noexcept {
        if (empty()) return other;
        if (other.empty()) return *this;
        return Rect::from_edges(std::min(left(), other.left()), std::min(top(), other.top()),
                                std::max(right(), other.right()),
                                std::max(bottom(), other.bottom()));
    }

    [[nodiscard]] bool contains(const Point& p) const noexcept {
        return p.x >= left() && p.x < right() && p.y >= top() && p.y < bottom();
    }

    [[nodiscard]] bool intersects(const Rect& other) const noexcept {
        return left() < other.right() && other.left() < right() && top() < other.bottom() &&
               other.top() < bottom();
    }
};

// Unit conversions. Figure units are PDF points.
inline constexpr double kPointsPerInch = 72.0;

[[nodiscard]] constexpr double inches(double value) noexcept { return value * kPointsPerInch; }
[[nodiscard]] constexpr double millimeters(double value) noexcept {
    return value * kPointsPerInch / 25.4;
}
[[nodiscard]] constexpr double points_to_pixels(double points, double dpi) noexcept {
    return points * dpi / kPointsPerInch;
}
[[nodiscard]] constexpr double pixels_to_points(double pixels, double dpi) noexcept {
    return pixels * kPointsPerInch / dpi;
}

}  // namespace gre
