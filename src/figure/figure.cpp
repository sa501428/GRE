#include "gre/figure/figure.hpp"

#include <algorithm>
#include <cmath>

#include "gre/core/error.hpp"
#include "gre/core/scale.hpp"
#include "gre/export/png.hpp"
#include "gre/render/raster_canvas.hpp"
#include "render/rotated_canvas.hpp"

namespace gre {
namespace {

constexpr double kTitleGap = 5.0;
constexpr double kPanelTitleGap = 3.0;
constexpr double kNameGap = 4.0;

// Tracks are clipped a little loosely: glyph descenders, axis ticks and range
// labels legitimately sit just outside the plotting box.
Rect clip_for(const Rect& rect) {
    return Rect{rect.left() - 2.0, rect.top() - 24.0, rect.width + 4.0, rect.height + 48.0};
}

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
        std::vector<double> heights;  // x tracks, above the map
        std::vector<double> flex;
        std::vector<double> bottom_heights;  // x tracks, below the map
        std::vector<double> bottom_flex;
        std::vector<double> y_widths;  // side tracks, square panels only
        double label_width{};
        double title_height{};
        double natural_height{};
        double plot_left{};
        double plot_right{};
        double spacing{};
        // Square panels only.
        double map_left{};
        double map_side{};
        double y_label_height{};

        [[nodiscard]] double total_flex() const {
            double total = 0.0;
            for (double f : flex) total += f;
            for (double f : bottom_flex) total += f;
            return total;
        }

        // Hands `slack` to the flexible tracks in either band, in proportion to
        // `total` (which may span several panels).  Returns how much was used.
        double distribute(double slack, double total) {
            if (!(total > 0.0)) return 0.0;
            double used = 0.0;
            const auto share_out = [&](std::vector<double>& sizes,
                                       const std::vector<double>& weights) {
                for (std::size_t i = 0; i < sizes.size(); ++i) {
                    if (weights[i] <= 0.0) continue;
                    const double share = slack * weights[i] / total;
                    sizes[i] = std::max(0.0, sizes[i] + share);
                    used += share;
                }
            };
            share_out(heights, flex);
            share_out(bottom_heights, bottom_flex);
            return used;
        }
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

        // Square panels reserve columns on the left for the quarter-turned
        // tracks, and the map takes whatever square fits in what is left.
        state.map_left = state.plot_left;
        state.map_side = state.plot_right - state.plot_left;
        if (panel.square_layout()) {
            const double available = state.plot_right - state.plot_left;
            state.y_widths.resize(panel.y_tracks_.size());

            const auto columns_total = [&]() {
                double total = 0.0;
                for (std::size_t i = 0; i < state.y_widths.size(); ++i) {
                    total += state.y_widths[i] + panel.y_tracks_[i]->margins().vertical();
                }
                if (!panel.y_tracks_.empty()) {
                    total += state.spacing * static_cast<double>(panel.y_tracks_.size() - 1);
                }
                return total;
            };

            for (std::size_t i = 0; i < panel.y_tracks_.size(); ++i) {
                state.y_widths[i] = std::max(0.0, panel.y_tracks_[i]->preferred_height());
            }
            double side = available - columns_total();

            // Side tracks run over the y region, in their own local frame:
            // local x is genomic and spans the map, local y is the column.
            const auto prepare_side_tracks = [&](double map_side) {
                ViewContext side_context = context;
                side_context.x_region = panel.region_y();
                side_context.y_region = panel.region_y();
                for (std::size_t i = 0; i < panel.y_tracks_.size(); ++i) {
                    side_context.content = Rect{0.0, 0.0, std::max(map_side, 1.0),
                                                state.y_widths[i]};
                    panel.y_tracks_[i]->prepare(side_context);
                    state.y_widths[i] = std::max(0.0, panel.y_tracks_[i]->preferred_height());
                }
            };
            prepare_side_tracks(side);

            // A track that sized itself during prepare (gene rows, say) changes
            // the column total, so settle the square once more.
            const double refined = available - columns_total();
            if (std::fabs(refined - side) > 0.5) {
                side = refined;
                prepare_side_tracks(side);
            }
            state.map_side = std::max(available - columns_total(), 1.0);
            state.map_left = state.plot_right - state.map_side;

            bool any_side_name = false;
            for (const auto& track : panel.y_tracks_) {
                if (track->show_name() && !track->name().empty()) any_side_name = true;
            }
            if (any_side_name) {
                state.y_label_height = panel.y_label_height_ >= 0.0
                                           ? panel.y_label_height_
                                           : font.line_height(theme_.font_size) + 2.0;
            }
        }

        // Both horizontal bands are prepared the same way; the width is final
        // here, while the height is only a hint, so tracks that size
        // themselves (heatmaps, gene models) work it out during prepare().
        const auto prepare_band = [&](const std::vector<std::unique_ptr<Track>>& band,
                                      std::vector<double>& heights, std::vector<double>& flex) {
            heights.assign(band.size(), 0.0);
            flex.assign(band.size(), 0.0);
            double stack = 0.0;
            for (std::size_t t = 0; t < band.size(); ++t) {
                Track& track = *band[t];
                const double plot_left =
                    track.wants_label_gutter() ? state.map_left : left + panel.padding_.left;
                context.content =
                    Rect{plot_left, 0.0, state.plot_right - plot_left, track.preferred_height()};
                track.prepare(context);

                heights[t] = std::max(0.0, track.preferred_height());
                flex[t] = std::max(0.0, track.flex());
                stack += heights[t] + track.margins().vertical();
            }
            if (!band.empty()) stack += state.spacing * static_cast<double>(band.size() - 1);
            return stack;
        };

        const double top_stack = prepare_band(panel.tracks_, state.heights, state.flex);
        const double bottom_stack =
            prepare_band(panel.bottom_tracks_, state.bottom_heights, state.bottom_flex);

        // The panel is up to three horizontal bands: tracks, then the square
        // map (when there is one), then the bottom tracks.
        double content_height = top_stack;
        if (panel.square_layout()) {
            ViewContext matrix_context = context;
            matrix_context.content = Rect{state.map_left, 0.0, state.map_side, state.map_side};
            panel.matrix_->prepare(matrix_context);
            if (!panel.tracks_.empty()) content_height += state.spacing;
            content_height += state.map_side + panel.matrix_->margins().vertical() +
                              state.y_label_height;
        }
        if (!panel.bottom_tracks_.empty()) {
            if (!panel.tracks_.empty() || panel.square_layout()) content_height += state.spacing;
            content_height += bottom_stack;
        }

        const double natural =
            content_height + panel.padding_.vertical() + state.title_height;
        state.natural_height = panel.height_ > 0.0 ? panel.height_ : natural;

        // A panel with an explicit height hands the difference to its flexible
        // tracks.
        if (panel.height_ > 0.0) {
            state.distribute(panel.height_ - natural, state.total_flex());
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
            total_flex += prepared[p].total_flex();
        }
        const double slack = size_.height - natural_total;
        if (total_flex > 0.0 && slack != 0.0) {
            for (std::size_t p = 0; p < prepared.size(); ++p) {
                if (panels_[p]->height_ > 0.0) continue;
                prepared[p].natural_height += prepared[p].distribute(slack, total_flex);
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

        bool placed_anything = false;
        const auto place_band = [&](const std::vector<std::unique_ptr<Track>>& band,
                                    const std::vector<double>& heights) {
            if (band.empty()) return;
            if (placed_anything) y += state.spacing;
            for (std::size_t t = 0; t < band.size(); ++t) {
                Track& track = *band[t];
                const Insets& track_margins = track.margins();
                y += track_margins.top;

                const bool gutter = track.wants_label_gutter();
                const double plot_left = gutter ? state.map_left : left + panel.padding_.left;
                const double reserve = std::min(track.label_reserve(), state.label_width);

                TrackLayout entry;
                entry.track = &track;
                entry.rect.full = Rect{plot_left, y, state.plot_right - plot_left, heights[t]};
                entry.rect.content = Rect{plot_left + track_margins.left, y,
                                          state.plot_right - plot_left -
                                              track_margins.horizontal(),
                                          heights[t]};
                entry.rect.label = gutter ? Rect{plot_left - reserve, y, reserve, heights[t]}
                                          : Rect{plot_left, y, 0.0, heights[t]};
                entry.rect.x = GenomicTransform{panel.region(), entry.rect.content.left(),
                                                entry.rect.content.right()};
                entry.rect.y = GenomicTransform{panel.region_y(), entry.rect.content.top(),
                                                entry.rect.content.bottom()};
                entry.clip = clip_for(entry.rect.full);
                entry.draw_name = gutter && track.show_name() && !track.name().empty();
                entry.name_anchor =
                    Point{entry.rect.label.left() - kNameGap, entry.rect.full.center_y()};
                placed.tracks.push_back(entry);

                if (!placed_anything) content_top = y;
                placed_anything = true;
                content_bottom = y + heights[t];
                y += heights[t] + track_margins.bottom;
                if (t + 1 < band.size()) y += state.spacing;
            }
        };

        place_band(panel.tracks_, state.heights);

        if (panel.square_layout()) {
            if (placed_anything) y += state.spacing;
            const Insets& matrix_margins = panel.matrix_->margins();
            y += matrix_margins.top;
            const double map_top = y;
            const Rect map{state.map_left, map_top, state.map_side, state.map_side};

            TrackLayout matrix_entry;
            matrix_entry.track = panel.matrix_.get();
            matrix_entry.rect.full = map;
            matrix_entry.rect.content = map;
            matrix_entry.rect.label = Rect{map.left(), map_top, 0.0, state.map_side};
            matrix_entry.rect.x = GenomicTransform{panel.region(), map.left(), map.right()};
            matrix_entry.rect.y = GenomicTransform{panel.region_y(), map.top(), map.bottom()};
            matrix_entry.clip = clip_for(map);
            matrix_entry.draw_name =
                panel.matrix_->show_name() && !panel.matrix_->name().empty();
            // The map's own name goes in the gutter, left of the side columns.
            matrix_entry.name_anchor = Point{state.plot_left - kNameGap, map.center_y()};
            placed.tracks.push_back(matrix_entry);

            // Side columns, first added furthest from the map -- the mirror of
            // how x tracks stack downwards towards it.
            double column_x = state.plot_left;
            for (std::size_t i = 0; i < panel.y_tracks_.size(); ++i) {
                Track& track = *panel.y_tracks_[i];
                const Insets& side_margins = track.margins();
                // Local +y points page-left, so the local "bottom" margin is
                // the gap on the left of the column.
                const double column_left = column_x + side_margins.bottom;
                const double column_right = column_left + state.y_widths[i];

                TrackLayout entry;
                entry.track = &track;
                entry.side = true;
                entry.rotation = -90.0;
                entry.origin = Point{column_right, map_top};
                entry.rect.full = Rect{0.0, 0.0, state.map_side, state.y_widths[i]};
                entry.rect.content = entry.rect.full;
                entry.rect.label = Rect{0.0, 0.0, 0.0, state.y_widths[i]};
                entry.rect.x = GenomicTransform{panel.region_y(), 0.0, state.map_side};
                entry.rect.y = entry.rect.x;
                entry.clip = Rect{column_left - 1.0, map_top - 1.0, state.y_widths[i] + 2.0,
                                  state.map_side + 2.0};
                entry.draw_name = track.show_name() && !track.name().empty();
                entry.name_anchor =
                    Point{(column_left + column_right) / 2.0, map.bottom() + 2.0};
                entry.name_align = TextAlign::center;
                entry.name_valign = VerticalAlign::top;
                placed.tracks.push_back(entry);

                column_x += side_margins.vertical() + state.y_widths[i] + state.spacing;
            }

            if (!placed_anything) content_top = map_top;
            placed_anything = true;
            content_bottom = map.bottom();
            y = map.bottom() + matrix_margins.bottom + state.y_label_height;
        }

        // Grid lines and the panel border cover the data, not the legends that
        // follow it.
        const double data_bottom = content_bottom;
        place_band(panel.bottom_tracks_, state.bottom_heights);

        placed.content =
            Rect::from_edges(state.map_left, content_top, state.plot_right, data_bottom);
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
        if (panel.show_grid_ && !panel_layout.content.empty()) {
            const GenomicTransform transform{panel.region(), panel_layout.content.left(),
                                             panel_layout.content.right()};
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

            if (entry.draw_name) {
                TextStyle style;
                style.font = theme_.font;
                style.size = theme_.font_size;
                style.color = theme_.foreground;
                style.align = entry.name_align;
                style.valign = entry.name_valign;
                canvas.draw_text(entry.name_anchor, track.name(), style);
            }

            if (entry.side) {
                // Side tracks draw in their own frame; the wrapper maps every
                // primitive onto the page.
                if (track.clipped()) {
                    ClipGuard guard(canvas, entry.clip);
                    RotatedCanvas rotated(canvas, entry.origin, entry.rotation);
                    track.draw(rotated, entry.rect);
                } else {
                    RotatedCanvas rotated(canvas, entry.origin, entry.rotation);
                    track.draw(rotated, entry.rect);
                }
            } else if (track.clipped()) {
                ClipGuard guard(canvas, entry.clip);
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
