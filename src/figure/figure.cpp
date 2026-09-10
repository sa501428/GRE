#include "gre/figure/figure.hpp"

#include <algorithm>
#include <cmath>

#include "gre/core/error.hpp"
#include "gre/core/scale.hpp"
#include "gre/export/png.hpp"
#include "gre/render/raster_canvas.hpp"

namespace gre {
namespace {

constexpr double kTitleGap = 5.0;
constexpr double kPanelTitleGap = 3.0;

}  // namespace

Figure::Figure() : theme_(Theme::light()) {}

Figure::Figure(Size size) : size_(size), theme_(Theme::light()) {}

Figure& Figure::set_size(double width, double height) { return set_size(Size{width, height}); }

Figure& Figure::set_size(Size size) {
    if (size.width <= 0.0) {
        throw Error(ErrorCode::invalid_argument, "figure width must be positive");
    }
    size_ = size;
    return *this;
}

Figure& Figure::set_width(double width) { return set_size(Size{width, 0.0}); }

Figure& Figure::set_margins(Insets margins) {
    margins_ = margins;
    margins_set_ = true;
    return *this;
}

Figure& Figure::set_theme(Theme theme) {
    theme_ = std::move(theme);
    return *this;
}

Figure& Figure::set_title(std::string title) {
    title_ = std::move(title);
    return *this;
}

Figure& Figure::set_subtitle(std::string subtitle) {
    subtitle_ = std::move(subtitle);
    return *this;
}

Figure& Figure::set_background(Color color) {
    background_ = color;
    background_set_ = true;
    return *this;
}

Figure& Figure::set_panel_spacing(double spacing) {
    panel_spacing_ = spacing;
    return *this;
}

Panel& Figure::add_panel() {
    panels_.push_back(std::make_unique<Panel>());
    return *panels_.back();
}

Figure::Layout Figure::build_layout(double device_scale) {
    if (panels_.empty()) {
        throw Error(ErrorCode::invalid_argument, "figure has no panels");
    }

    const Insets margins = margins_set_ ? margins_ : theme_.figure_margins;
    const double panel_spacing = panel_spacing_ >= 0.0 ? panel_spacing_ : theme_.panel_spacing;
    const Font& font = fonts().get(theme_.font);

    Layout layout;
    layout.size = size_;

    const double left = margins.left;
    const double right = std::max(size_.width - margins.right, left + 1.0);
    double cursor = margins.top;

    if (!title_.empty()) {
        const double height = font.line_height(theme_.title_font_size);
        layout.title_rect = Rect{left, cursor, right - left, height};
        cursor += height + kTitleGap;
    }
    if (!subtitle_.empty()) {
        const double height = font.line_height(theme_.font_size);
        layout.subtitle_rect = Rect{left, cursor, right - left, height};
        cursor += height + kTitleGap;
    }

    // ---- pass one: prepare every track and collect natural heights ---------
    struct Prepared {
        std::vector<double> heights;
        std::vector<double> flex;
        double label_width{};
        double title_height{};
        double natural_height{};
        double plot_left{};
        double plot_right{};
        double spacing{};
    };
    std::vector<Prepared> prepared(panels_.size());

    for (std::size_t p = 0; p < panels_.size(); ++p) {
        Panel& panel = *panels_[p];
        if (panel.region_.chrom.empty()) {
            throw Error(ErrorCode::invalid_argument,
                        "panel " + std::to_string(p) + " has no region; call set_region()");
        }
        Prepared& state = prepared[p];
        state.label_width = panel.label_width_ >= 0.0 ? panel.label_width_ : theme_.label_width;
        state.spacing =
            panel.track_spacing_ >= 0.0 ? panel.track_spacing_ : theme_.track_spacing;
        state.plot_left = left + panel.padding_.left + state.label_width;
        state.plot_right = right - panel.padding_.right - theme_.right_gutter;
        if (state.plot_right <= state.plot_left) {
            throw Error(ErrorCode::invalid_argument,
                        "figure is too narrow for the label gutter; widen it or lower "
                        "Theme::label_width");
        }
        if (!panel.title_.empty()) {
            state.title_height =
                font.line_height(theme_.panel_title_font_size) + kPanelTitleGap;
        }

        ViewContext context;
        context.x_region = panel.region();
        context.y_region = panel.region_y();
        context.device_scale = device_scale;
        context.theme = &theme_;

        state.heights.resize(panel.tracks_.size());
        state.flex.resize(panel.tracks_.size());
        double stack = 0.0;
        for (std::size_t t = 0; t < panel.tracks_.size(); ++t) {
            Track& track = *panel.tracks_[t];
            const double plot_left =
                track.wants_label_gutter() ? state.plot_left : left + panel.padding_.left;
            // The width is final here; the height is only a hint, so tracks
            // that size themselves (heatmaps, gene models) work it out during
            // prepare().
            context.content =
                Rect{plot_left, 0.0, state.plot_right - plot_left, track.preferred_height()};
            track.prepare(context);

            state.heights[t] = std::max(0.0, track.preferred_height());
            state.flex[t] = std::max(0.0, track.flex());
            stack += state.heights[t] + track.margins().vertical();
        }
        if (!panel.tracks_.empty()) {
            stack += state.spacing * static_cast<double>(panel.tracks_.size() - 1);
        }

        const double natural =
            stack + panel.padding_.vertical() + state.title_height;
        state.natural_height = panel.height_ > 0.0 ? panel.height_ : natural;

        // A panel with an explicit height hands the difference to its flexible
        // tracks.
        if (panel.height_ > 0.0) {
            double total_flex = 0.0;
            for (double f : state.flex) total_flex += f;
            if (total_flex > 0.0) {
                const double slack = panel.height_ - natural;
                for (std::size_t t = 0; t < state.heights.size(); ++t) {
                    state.heights[t] =
                        std::max(0.0, state.heights[t] + slack * state.flex[t] / total_flex);
                }
            }
        }
    }

    double natural_total = cursor;
    for (std::size_t p = 0; p < prepared.size(); ++p) {
        natural_total += prepared[p].natural_height;
        if (p + 1 < prepared.size()) natural_total += panel_spacing;
    }
    natural_total += margins.bottom;

    // ---- pass two: honour an explicit page height -------------------------
    if (size_.height > 0.0) {
        double total_flex = 0.0;
        for (std::size_t p = 0; p < prepared.size(); ++p) {
            if (panels_[p]->height_ > 0.0) continue;  // already resolved
            for (double f : prepared[p].flex) total_flex += f;
        }
        const double slack = size_.height - natural_total;
        if (total_flex > 0.0 && slack != 0.0) {
            for (std::size_t p = 0; p < prepared.size(); ++p) {
                if (panels_[p]->height_ > 0.0) continue;
                Prepared& state = prepared[p];
                for (std::size_t t = 0; t < state.heights.size(); ++t) {
                    if (state.flex[t] <= 0.0) continue;
                    const double share = slack * state.flex[t] / total_flex;
                    state.heights[t] = std::max(0.0, state.heights[t] + share);
                    state.natural_height += share;
                }
            }
        }
        layout.size.height = size_.height;
    } else {
        layout.size.height = std::max(natural_total, 1.0);
    }

    // ---- pass three: place everything -------------------------------------
    for (std::size_t p = 0; p < panels_.size(); ++p) {
        Panel& panel = *panels_[p];
        Prepared& state = prepared[p];

        PanelLayout placed;
        placed.panel = &panel;
        placed.rect = Rect{left, cursor, right - left, state.natural_height};

        double y = cursor + panel.padding_.top + state.title_height;
        double content_top = y;
        double content_bottom = y;

        for (std::size_t t = 0; t < panel.tracks_.size(); ++t) {
            Track& track = *panel.tracks_[t];
            const Insets& track_margins = track.margins();
            y += track_margins.top;

            const bool gutter = track.wants_label_gutter();
            const double plot_left = gutter ? state.plot_left : left + panel.padding_.left;
            const double reserve = std::min(track.label_reserve(), state.label_width);

            TrackLayout entry;
            entry.track = &track;
            entry.rect.full = Rect{left + panel.padding_.left, y,
                                   state.plot_right - left - panel.padding_.left,
                                   state.heights[t]};
            entry.rect.content =
                Rect{plot_left + track_margins.left, y,
                     state.plot_right - plot_left - track_margins.horizontal(),
                     state.heights[t]};
            entry.rect.label =
                gutter ? Rect{state.plot_left - reserve, y, reserve, state.heights[t]}
                       : Rect{plot_left, y, 0.0, state.heights[t]};
            entry.rect.x = GenomicTransform{panel.region(), entry.rect.content.left(),
                                            entry.rect.content.right()};
            entry.rect.y = GenomicTransform{panel.region_y(), entry.rect.content.top(),
                                            entry.rect.content.bottom()};
            placed.tracks.push_back(entry);

            if (t == 0) content_top = y;
            content_bottom = y + state.heights[t];
            y += state.heights[t] + track_margins.bottom;
            if (t + 1 < panel.tracks_.size()) y += state.spacing;
        }

        placed.content = Rect::from_edges(state.plot_left, content_top, state.plot_right,
                                          content_bottom);
        layout.panels.push_back(std::move(placed));
        cursor += state.natural_height + panel_spacing;
    }

    return layout;
}

void Figure::draw_layout(Canvas& canvas, const Layout& layout) const {
    const Color background = background_set_ ? background_ : theme_.background;
    if (!background.transparent()) {
        canvas.fill_rect(Rect{0.0, 0.0, layout.size.width, layout.size.height}, background);
    }

    if (!title_.empty()) {
        TextStyle style;
        style.font = theme_.font;
        style.size = theme_.title_font_size;
        style.color = theme_.foreground;
        style.valign = VerticalAlign::top;
        canvas.draw_text(Point{layout.title_rect.left(), layout.title_rect.top()}, title_, style);
    }
    if (!subtitle_.empty()) {
        TextStyle style;
        style.font = theme_.font;
        style.size = theme_.font_size;
        style.color = theme_.muted;
        style.valign = VerticalAlign::top;
        canvas.draw_text(Point{layout.subtitle_rect.left(), layout.subtitle_rect.top()},
                         subtitle_, style);
    }

    for (const PanelLayout& panel_layout : layout.panels) {
        const Panel& panel = *panel_layout.panel;

        if (panel.has_background_ && !panel.background_.transparent()) {
            canvas.fill_rect(panel_layout.rect, panel.background_);
        }

        if (!panel.title_.empty()) {
            TextStyle style;
            style.font = theme_.font;
            style.size = theme_.panel_title_font_size;
            style.color = theme_.foreground;
            style.valign = VerticalAlign::top;
            canvas.draw_text(
                Point{panel_layout.rect.left() + panel.padding_.left,
                      panel_layout.rect.top() + panel.padding_.top},
                panel.title_, style);
        }

        // Grid lines run behind every track in the panel, so the eye can carry
        // a coordinate down the whole stack.
        if (panel.show_grid_ && !panel_layout.content.empty() &&
            !panel_layout.tracks.empty()) {
            const GenomicTransform& transform = panel_layout.tracks.front().rect.x;
            StrokeStyle grid;
            grid.color = theme_.grid;
            grid.width = theme_.grid_line_width;
            for (long long tick :
                 genomic_ticks(panel.region().start, panel.region().end, 10)) {
                const double x = transform.x(static_cast<std::int64_t>(tick));
                canvas.stroke_line(Point{x, panel_layout.content.top()},
                                   Point{x, panel_layout.content.bottom()}, grid);
            }
        }

        for (const TrackLayout& entry : panel_layout.tracks) {
            const Track& track = *entry.track;

            // The name goes in whatever the track left of the gutter.
            if (track.show_name() && !track.name().empty() && track.wants_label_gutter()) {
                TextStyle style;
                style.font = theme_.font;
                style.size = theme_.font_size;
                style.color = theme_.foreground;
                style.align = TextAlign::right;
                style.valign = VerticalAlign::middle;
                canvas.draw_text(Point{entry.rect.label.left() - 4.0, entry.rect.full.center_y()},
                                 track.name(), style);
            }

            if (track.clipped()) {
                // A generous vertical margin lets glyph descenders and axis
                // ticks sit just outside the content box without being cut.
                ClipGuard guard(canvas,
                                Rect{entry.rect.full.left() - 2.0, entry.rect.full.top() - 24.0,
                                     entry.rect.full.width + 4.0,
                                     entry.rect.full.height + 48.0});
                track.draw(canvas, entry.rect);
            } else {
                track.draw(canvas, entry.rect);
            }
        }

        if (panel.has_border_ && !panel.border_color_.transparent()) {
            StrokeStyle border;
            border.color = panel.border_color_;
            border.width = panel.border_width_;
            canvas.stroke_rect(panel_layout.content, border);
        }
    }
}

Size Figure::computed_size(double device_scale) { return build_layout(device_scale).size; }

void Figure::render(Canvas& canvas) {
    const Layout layout = build_layout(canvas.device_scale());
    draw_layout(canvas, layout);
}

Image Figure::render_image(double dpi) {
    const Layout layout = build_layout(dpi / kPointsPerInch);
    RasterCanvas canvas(layout.size, dpi, colors::transparent);
    draw_layout(canvas, layout);
    return canvas.take_image();
}

void Figure::save_png(const std::string& path, double dpi) {
    const Image image = render_image(dpi);
    png::Options options;
    options.dpi = static_cast<int>(std::lround(dpi));
    png::write(path, image, options);
}

void Figure::save_pdf(const std::string& path, PdfOptions options) {
    if (options.title.empty()) options.title = title_;
    const Layout layout = build_layout(1.0);
    PdfCanvas canvas(layout.size, std::move(options));
    draw_layout(canvas, layout);
    canvas.save(path);
}

void Figure::save_svg(const std::string& path, SvgOptions options) {
    const Layout layout = build_layout(1.0);
    SvgCanvas canvas(layout.size, std::move(options));
    draw_layout(canvas, layout);
    canvas.save(path);
}

}  // namespace gre
