#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gre {

// Straight (non-premultiplied) 8-bit sRGB with alpha.
struct Color {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{255};

    [[nodiscard]] constexpr bool opaque() const noexcept { return a == 255; }
    [[nodiscard]] constexpr bool transparent() const noexcept { return a == 0; }

    friend constexpr bool operator==(const Color& lhs, const Color& rhs) noexcept {
        return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
    }
};

[[nodiscard]] constexpr Color rgb(int r, int g, int b) noexcept {
    return Color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                 static_cast<std::uint8_t>(b), 255};
}

[[nodiscard]] constexpr Color rgba(int r, int g, int b, int a) noexcept {
    return Color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                 static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a)};
}

// Accepts "#rgb", "#rrggbb" and "#rrggbbaa", with or without the leading '#'.
// Throws Error(invalid_argument) on anything else.
[[nodiscard]] Color color_from_hex(std::string_view text);

// Returns std::nullopt-like sentinel behaviour without <optional>: false when
// the string could not be parsed, leaving `out` untouched.
[[nodiscard]] bool try_color_from_hex(std::string_view text, Color& out) noexcept;

[[nodiscard]] std::string color_to_hex(const Color& color);

// alpha in [0, 1]; multiplies the existing alpha.
[[nodiscard]] Color with_alpha(Color color, double alpha) noexcept;

// Source-over composite of `over` onto `under`, both straight alpha.
[[nodiscard]] Color composite(Color under, Color over) noexcept;

// Linear interpolation in sRGB space; t is clamped to [0, 1].
[[nodiscard]] Color lerp(Color a, Color b, double t) noexcept;

namespace colors {
inline constexpr Color transparent = rgba(0, 0, 0, 0);
inline constexpr Color black = rgb(0, 0, 0);
inline constexpr Color white = rgb(255, 255, 255);
inline constexpr Color red = rgb(214, 39, 40);
inline constexpr Color blue = rgb(31, 119, 180);
inline constexpr Color green = rgb(44, 160, 44);
inline constexpr Color orange = rgb(255, 127, 14);
inline constexpr Color purple = rgb(148, 103, 189);
inline constexpr Color gray = rgb(127, 127, 127);
inline constexpr Color light_gray = rgb(217, 217, 217);
inline constexpr Color dark_gray = rgb(64, 64, 64);
}  // namespace colors

// A continuous colour map, evaluated through a precomputed lookup table so that
// per-cell heatmap rendering never interpolates.
class ColorMap {
public:
    static constexpr std::size_t kLutSize = 1024;

    ColorMap();  // defaults to "viridis"

    // Built-in maps: viridis, magma, inferno, plasma, cividis, gray, gray_r,
    // fall, juicebox, reds, blues, rd_bu, bwr.  Throws for unknown names.
    [[nodiscard]] static ColorMap named(std::string_view name);
    [[nodiscard]] static bool has(std::string_view name) noexcept;
    [[nodiscard]] static std::vector<std::string> available();

    // Builds a map by interpolating evenly spaced stops.
    [[nodiscard]] static ColorMap from_stops(const std::vector<Color>& stops);
    // Stops with explicit positions in [0, 1]; must be sorted ascending.
    [[nodiscard]] static ColorMap from_stops(const std::vector<std::pair<double, Color>>& stops);

    [[nodiscard]] ColorMap reversed() const;

    // t is clamped to [0, 1]. NaN maps to `bad()`.
    [[nodiscard]] Color at(double t) const noexcept;

    [[nodiscard]] const std::array<Color, kLutSize>& lut() const noexcept { return lut_; }

    // Colour used for missing/NaN values; defaults to fully transparent.
    [[nodiscard]] Color bad() const noexcept { return bad_; }
    ColorMap& bad(Color color) noexcept {
        bad_ = color;
        return *this;
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

private:
    // Builders use this instead of the public default constructor, which would
    // otherwise recurse: ColorMap() -> named() -> from_stops() -> ColorMap().
    struct Uninitialized {};
    explicit ColorMap(Uninitialized) noexcept {}

    std::array<Color, kLutSize> lut_{};
    Color bad_{colors::transparent};
    std::string name_;
};

}  // namespace gre
