#include "gre/figure/theme.hpp"

#include "gre/core/error.hpp"

namespace gre {

Theme Theme::light() {
    Theme theme;
    theme.heatmap_colors = ColorMap::named("juicebox");
    return theme;
}

Theme Theme::dark() {
    Theme theme;
    theme.background = rgb(24, 26, 30);
    theme.foreground = rgb(232, 232, 236);
    theme.muted = rgb(150, 152, 158);
    theme.grid = rgb(56, 59, 66);
    theme.axis = rgb(190, 192, 198);
    theme.heatmap_colors = ColorMap::named("magma");
    theme.signal_color = rgb(96, 170, 245);
    theme.gene_color = rgb(200, 208, 230);
    theme.interval_color = rgb(150, 160, 190);
    return theme;
}

Theme Theme::publication() {
    Theme theme;
    theme.font_size = 7.0;
    theme.small_font_size = 6.0;
    theme.title_font_size = 8.5;
    theme.panel_title_font_size = 7.5;
    theme.axis_line_width = 0.5;
    theme.grid_line_width = 0.3;
    theme.tick_length = 2.5;
    theme.figure_margins = Insets{10.0};
    theme.track_spacing = 3.0;
    theme.panel_spacing = 10.0;
    theme.label_width = 62.0;
    theme.muted = rgb(90, 90, 90);
    theme.heatmap_colors = ColorMap::named("juicebox");
    return theme;
}

Theme Theme::named(const std::string& name) {
    if (name == "light" || name == "default") return light();
    if (name == "dark") return dark();
    if (name == "publication") return publication();
    throw Error(ErrorCode::not_found, "unknown theme: '" + name + "'");
}

}  // namespace gre
