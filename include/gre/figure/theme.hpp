#pragma once

#include <string>

#include "gre/core/color.hpp"
#include "gre/core/geometry.hpp"
#include "gre/render/text.hpp"

namespace gre {

// Shared visual defaults.  Tracks read from the theme rather than hard-coding
// colours, so a whole figure can be restyled in one place.
struct Theme {
    Color background{colors::white};
    Color foreground{colors::black};
    Color muted{rgb(110, 110, 110)};
    Color grid{rgb(232, 232, 232)};
    Color axis{rgb(60, 60, 60)};
    Color panel_border{colors::transparent};

    FontId font{};
    double font_size{8.0};
    double small_font_size{6.5};
    double title_font_size{11.0};
    double panel_title_font_size{9.0};

    double axis_line_width{0.6};
    double grid_line_width{0.4};
    double tick_length{3.0};

    Insets figure_margins{16.0};
    double track_spacing{4.0};
    double panel_spacing{14.0};
    // Left gutter reserved for track names and y-axes.
    double label_width{74.0};
    double right_gutter{4.0};

    // Track defaults.
    ColorMap heatmap_colors;
    Color signal_color{rgb(31, 119, 180)};
    Color gene_color{rgb(50, 60, 90)};
    Color interval_color{rgb(120, 130, 160)};

    [[nodiscard]] static Theme light();
    [[nodiscard]] static Theme dark();
    // Tighter spacing, smaller type, hairline rules: sized for a journal figure.
    [[nodiscard]] static Theme publication();

    [[nodiscard]] static Theme named(const std::string& name);
};

}  // namespace gre
