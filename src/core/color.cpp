#include "gre/core/color.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "gre/core/error.hpp"

namespace gre {
namespace {

int hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::uint8_t to_byte(double value) noexcept {
    const double scaled = std::lround(std::clamp(value, 0.0, 1.0) * 255.0);
    return static_cast<std::uint8_t>(scaled);
}

// Evenly spaced anchors for the built-in maps.  Ten stops reproduce the
// perceptual maps closely enough that banding is invisible once interpolated
// into a 1024-entry table.
const std::map<std::string, std::vector<const char*>, std::less<>>& builtin_stops() {
    static const std::map<std::string, std::vector<const char*>, std::less<>> maps = {
        {"viridis",
         {"#440154", "#482878", "#3E4A89", "#31688E", "#26828E", "#1F9E89", "#35B779", "#6DCD59",
          "#B4DE2C", "#FDE725"}},
        {"magma",
         {"#000004", "#180F3D", "#440F76", "#721F81", "#9E2F7F", "#CD4071", "#F1605D", "#FD9668",
          "#FECA8D", "#FCFDBF"}},
        {"inferno",
         {"#000004", "#1B0C42", "#4B0C6B", "#781C6D", "#A52C60", "#CF4446", "#ED6925", "#FB9A06",
          "#F7D03C", "#FCFFA4"}},
        {"plasma",
         {"#0D0887", "#47039F", "#7301A8", "#9C179E", "#BD3786", "#D8576B", "#ED7953", "#FA9E3B",
          "#FDC926", "#F0F921"}},
        {"cividis",
         {"#00204D", "#00306F", "#39486B", "#575D6D", "#707173", "#8A8779", "#A69D75", "#C4B56C",
          "#E4CF5B", "#FFEA46"}},
        {"gray", {"#000000", "#FFFFFF"}},
        {"gray_r", {"#FFFFFF", "#000000"}},
        // HiGlass "fall": white through YlOrRd to dark maroon.
        {"fall",
         {"#FFFFFF", "#FFFFCC", "#FFEDA0", "#FED976", "#FEB24C", "#FD8D3C", "#FC4E2A", "#E31A1C",
          "#BD0026", "#800026"}},
        // Juicebox's classic linear white-to-red contact map ramp.
        {"juicebox", {"#FFFFFF", "#FF0000"}},
        {"reds", {"#FFF5F0", "#FEE0D2", "#FCBBA1", "#FC9272", "#FB6A4A", "#EF3B2C", "#CB181D",
                  "#99000D"}},
        {"blues", {"#F7FBFF", "#DEEBF7", "#C6DBEF", "#9ECAE1", "#6BAED6", "#4292C6", "#2171B5",
                   "#08306B"}},
        // Diverging maps, for observed/expected ratios in log space.
        {"rd_bu",
         {"#053061", "#2166AC", "#4393C3", "#92C5DE", "#D1E5F0", "#F7F7F7", "#FDDBC7", "#F4A582",
          "#D6604D", "#B2182B", "#67001F"}},
        {"bwr", {"#0000FF", "#FFFFFF", "#FF0000"}},
    };
    return maps;
}

}  // namespace

bool try_color_from_hex(std::string_view text, Color& out) noexcept {
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    const std::size_t n = text.size();
    if (n != 3 && n != 4 && n != 6 && n != 8) return false;
    int digits[8];
    for (std::size_t i = 0; i < n; ++i) {
        digits[i] = hex_digit(text[i]);
        if (digits[i] < 0) return false;
    }
    if (n == 3 || n == 4) {
        out.r = static_cast<std::uint8_t>(digits[0] * 17);
        out.g = static_cast<std::uint8_t>(digits[1] * 17);
        out.b = static_cast<std::uint8_t>(digits[2] * 17);
        out.a = n == 4 ? static_cast<std::uint8_t>(digits[3] * 17) : std::uint8_t{255};
    } else {
        out.r = static_cast<std::uint8_t>(digits[0] * 16 + digits[1]);
        out.g = static_cast<std::uint8_t>(digits[2] * 16 + digits[3]);
        out.b = static_cast<std::uint8_t>(digits[4] * 16 + digits[5]);
        out.a = n == 8 ? static_cast<std::uint8_t>(digits[6] * 16 + digits[7]) : std::uint8_t{255};
    }
    return true;
}

Color color_from_hex(std::string_view text) {
    Color color;
    if (!try_color_from_hex(text, color)) {
        throw Error(ErrorCode::invalid_argument,
                    "not a hexadecimal colour: '" + std::string(text) + "'");
    }
    return color;
}

std::string color_to_hex(const Color& color) {
    char buffer[10];
    if (color.opaque()) {
        std::snprintf(buffer, sizeof buffer, "#%02X%02X%02X", color.r, color.g, color.b);
    } else {
        std::snprintf(buffer, sizeof buffer, "#%02X%02X%02X%02X", color.r, color.g, color.b,
                      color.a);
    }
    return std::string(buffer);
}

Color with_alpha(Color color, double alpha) noexcept {
    color.a = to_byte(std::clamp(alpha, 0.0, 1.0) * (color.a / 255.0));
    return color;
}

Color composite(Color under, Color over) noexcept {
    if (over.a == 255 || under.a == 0) return over;
    if (over.a == 0) return under;
    const double sa = over.a / 255.0;
    const double da = under.a / 255.0;
    const double out_a = sa + da * (1.0 - sa);
    if (out_a <= 0.0) return colors::transparent;
    const auto channel = [&](std::uint8_t s, std::uint8_t d) {
        return to_byte(((s / 255.0) * sa + (d / 255.0) * da * (1.0 - sa)) / out_a);
    };
    return Color{channel(over.r, under.r), channel(over.g, under.g), channel(over.b, under.b),
                 to_byte(out_a)};
}

Color lerp(Color a, Color b, double t) noexcept {
    t = std::clamp(t, 0.0, 1.0);
    const auto mix = [t](std::uint8_t lo, std::uint8_t hi) {
        return static_cast<std::uint8_t>(std::lround(lo + (hi - lo) * t));
    };
    return Color{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

ColorMap::ColorMap() { *this = named("viridis"); }

ColorMap ColorMap::from_stops(const std::vector<Color>& stops) {
    if (stops.empty()) {
        throw Error(ErrorCode::invalid_argument, "a colour map needs at least one stop");
    }
    std::vector<std::pair<double, Color>> positioned;
    positioned.reserve(stops.size());
    if (stops.size() == 1) {
        positioned.emplace_back(0.0, stops.front());
    } else {
        for (std::size_t i = 0; i < stops.size(); ++i) {
            positioned.emplace_back(static_cast<double>(i) / static_cast<double>(stops.size() - 1),
                                    stops[i]);
        }
    }
    return from_stops(positioned);
}

ColorMap ColorMap::from_stops(const std::vector<std::pair<double, Color>>& stops) {
    if (stops.empty()) {
        throw Error(ErrorCode::invalid_argument, "a colour map needs at least one stop");
    }
    ColorMap map{Uninitialized{}};
    for (std::size_t i = 0; i < kLutSize; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kLutSize - 1);
        if (t <= stops.front().first) {
            map.lut_[i] = stops.front().second;
            continue;
        }
        if (t >= stops.back().first) {
            map.lut_[i] = stops.back().second;
            continue;
        }
        std::size_t upper = 1;
        while (upper < stops.size() && stops[upper].first < t) ++upper;
        const auto& lo = stops[upper - 1];
        const auto& hi = stops[upper];
        const double span = hi.first - lo.first;
        const double local = span > 0.0 ? (t - lo.first) / span : 0.0;
        map.lut_[i] = lerp(lo.second, hi.second, local);
    }
    return map;
}

bool ColorMap::has(std::string_view name) noexcept {
    return builtin_stops().find(name) != builtin_stops().end();
}

std::vector<std::string> ColorMap::available() {
    std::vector<std::string> names;
    names.reserve(builtin_stops().size());
    for (const auto& [name, stops] : builtin_stops()) names.push_back(name);
    return names;
}

ColorMap ColorMap::named(std::string_view name) {
    const auto it = builtin_stops().find(name);
    if (it == builtin_stops().end()) {
        throw Error(ErrorCode::not_found, "unknown colour map: '" + std::string(name) + "'");
    }
    std::vector<Color> stops;
    stops.reserve(it->second.size());
    for (const char* hex : it->second) stops.push_back(color_from_hex(hex));
    ColorMap map = from_stops(stops);
    map.name_ = it->first;
    return map;
}

ColorMap ColorMap::reversed() const {
    ColorMap map = *this;
    std::reverse(map.lut_.begin(), map.lut_.end());
    map.name_ = name_.empty() ? std::string() : name_ + "_r";
    return map;
}

Color ColorMap::at(double t) const noexcept {
    if (std::isnan(t)) return bad_;
    t = std::clamp(t, 0.0, 1.0);
    const auto index =
        static_cast<std::size_t>(std::lround(t * static_cast<double>(kLutSize - 1)));
    return lut_[index];
}

}  // namespace gre
