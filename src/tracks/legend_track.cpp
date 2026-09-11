#include "gre/tracks/legend_track.hpp"

#include <algorithm>
#include <cmath>

#include "gre/tracks/heatmap_track.hpp"

namespace gre {
namespace {

constexpr double kTitleGap = 3.0;
constexpr double kLabelGap = 2.0;
constexpr int kGradientSteps = 256;

double aligned_left(BarAlign align, const Rect& area, double width) {
    switch (align) {
        case BarAlign::center:
            return area.center_x() - width / 2.0;
        case BarAlign::right:
            return area.right() - width;
        case BarAlign::left:
            break;
    }
    return area.left();
}

}  // namespace

ColorBarTrack::ColorBarTrack(ColorMap map, ValueScale scale)
    : colors_(std::move(map)), scale_(std::move(scale)) {
    show_name_ = false;
}

ColorBarTrack::ColorBarTrack(const HeatmapTrack& heatmap) : linked_(&heatmap) {
    show_name_ = false;
}

ColorBarTrack& ColorBarTrack::orientation(BarOrientation value) {
    orientation_ = value;
    return *this;
}

ColorBarTrack& ColorBarTrack::align(BarAlign value) {
    align_ = value;
    return *this;
}

ColorBarTrack& ColorBarTrack::title(std::string value) {
    title_ = std::move(value);
    return *this;
}

ColorBarTrack& ColorBarTrack::bar_length(double value) {
    bar_length_ = std::max(4.0, value);
    return *this;
}

ColorBarTrack& ColorBarTrack::bar_thickness(double value) {
    bar_thickness_ = std::max(1.0, value);
    return *this;
}

ColorBarTrack& ColorBarTrack::max_ticks(int value) {
    max_ticks_ = std::max(2, value);
    return *this;
}

ColorBarTrack& ColorBarTrack::border(Color color, double width) {
    border_color_ = color;
    border_width_ = width;
    has_border_ = true;
    return *this;
}

double ColorBarTrack::default_height() const {
    const Font& font = fonts().get(theme().font);
    const double label_height = font.line_height(theme().small_font_size);
    if (orientation_ == BarOrientation::horizontal) {
        double height = bar_thickness_ + kLabelGap + label_height;
        if (!title_.empty()) height += label_height + kTitleGap;
        return height;
    }
    return bar_length_ + (title_.empty() ? 0.0 : font.line_height(theme().small_font_size) +
                                                     kTitleGap);
}

void ColorBarTrack::prepare(const ViewContext& context) {
    capture_view(context);
    if (linked_ != nullptr) {
        // The heatmap has already been prepared, so its scale is final.
        colors_ = linked_->color_map();
        scale_ = linked_->value_scale();
    }

    // A gradient strip drawn as an image stays smooth in PDF as well as PNG.
    if (orientation_ == BarOrientation::horizontal) {
        gradient_ = Image(kGradientSteps, 1);
        for (int i = 0; i < kGradientSteps; ++i) {
            gradient_.set(i, 0, colors_.at(static_cast<double>(i) / (kGradientSteps - 1)));
        }
    } else {
        gradient_ = Image(1, kGradientSteps);
        for (int i = 0; i < kGradientSteps; ++i) {
            // Top of a vertical bar is the high end of the scale.
            gradient_.set(0, i,
                          colors_.at(1.0 - static_cast<double>(i) / (kGradientSteps - 1)));
        }
    }
}

void ColorBarTrack::draw(Canvas& canvas, const TrackRect& rect) const {
    if (gradient_.empty() || rect.content.empty()) return;
    const Theme& theme_ref = theme();
    const Font& font = fonts().get(theme_ref.font);
    const double label_height = font.line_height(theme_ref.small_font_size);

    TextStyle text;
    text.font = theme_ref.font;
    text.size = theme_ref.small_font_size;
    text.color = theme_ref.foreground;

    // The bar's own extent, so the title can sit over it wherever it is placed.
    const double bar_width = orientation_ == BarOrientation::horizontal
                                 ? std::min(bar_length_, rect.content.width)
                                 : bar_thickness_;
    const double bar_x = aligned_left(align_, rect.content, bar_width);

    double top = rect.content.top();
    if (!title_.empty()) {
        TextStyle title_style = text;
        title_style.valign = VerticalAlign::top;
        title_style.align = TextAlign::left;
        // Keep the title with the bar rather than pinned to the track's edge.
        double x = bar_x;
        if (align_ == BarAlign::center) {
            title_style.align = TextAlign::center;
            x = bar_x + bar_width / 2.0;
        } else if (align_ == BarAlign::right) {
            title_style.align = TextAlign::right;
            x = bar_x + bar_width;
        }
        canvas.draw_text(Point{x, top}, title_, title_style);
        top += label_height + kTitleGap;
    }

    // Ticks follow the scale's own spacing, so a log bar does not crowd every
    // label at one end.
    const std::vector<double> ticks = scale_ticks(scale_, max_ticks_);
    const double step = ticks.size() >= 2 ? tick_step(ticks)
                                          : std::fabs(scale_.max() - scale_.min());

    StrokeStyle stroke;
    stroke.color = border_color_;
    stroke.width = border_width_;

    if (orientation_ == BarOrientation::horizontal) {
        const Rect bar{bar_x, top, bar_width, bar_thickness_};
        canvas.draw_image(bar, gradient_.view(ImageScaling::bilinear));
        if (has_border_) canvas.stroke_rect(bar, stroke);

        text.valign = VerticalAlign::top;
        for (double tick : ticks) {
            const double unit = scale_.normalize(tick);
            if (!std::isfinite(unit)) continue;
            // Pull the end labels inside the bar rather than letting them
            // overhang the plotting area.
            text.align = unit <= 0.001   ? TextAlign::left
                         : unit >= 0.999 ? TextAlign::right
                                         : TextAlign::center;
            canvas.draw_text(Point{bar.left() + unit * bar.width, bar.bottom() + kLabelGap},
                             format_tick(tick, step), text);
        }
    } else {
        const double height =
            std::min(bar_length_, rect.content.height - (top - rect.content.top()));
        const Rect bar{bar_x, top, bar_thickness_, height};
        canvas.draw_image(bar, gradient_.view(ImageScaling::bilinear));
        if (has_border_) canvas.stroke_rect(bar, stroke);

        text.align = TextAlign::left;
        for (double tick : ticks) {
            const double unit = scale_.normalize(tick);
            if (!std::isfinite(unit)) continue;
            text.valign = unit <= 0.001   ? VerticalAlign::bottom
                          : unit >= 0.999 ? VerticalAlign::top
                                          : VerticalAlign::middle;
            canvas.draw_text(Point{bar.right() + kLabelGap, bar.bottom() - unit * bar.height},
                             format_tick(tick, step), text);
        }
    }
}

LegendTrack::LegendTrack() { show_name_ = false; }

LegendTrack& LegendTrack::add(std::string label, Color color) {
    entries_.emplace_back(std::move(label), color);
    return *this;
}

LegendTrack& LegendTrack::columns(int value) {
    columns_ = std::max(0, value);
    return *this;
}

LegendTrack& LegendTrack::swatch_size(double value) {
    swatch_size_ = std::max(1.0, value);
    return *this;
}

LegendTrack& LegendTrack::entry_spacing(double value) {
    entry_spacing_ = std::max(0.0, value);
    return *this;
}

LegendTrack& LegendTrack::align(BarAlign value) {
    align_ = value;
    return *this;
}

double LegendTrack::default_height() const {
    const Font& font = fonts().get(theme().font);
    const double row = std::max(swatch_size_, font.line_height(theme().small_font_size));
    return std::max(1, resolved_rows_) * (row + 2.0);
}

void LegendTrack::prepare(const ViewContext& context) {
    capture_view(context);
    if (entries_.empty()) {
        resolved_columns_ = resolved_rows_ = 1;
        return;
    }
    const Theme& theme_ref = context.theme != nullptr ? *context.theme : theme();
    const Font& font = fonts().get(theme_ref.font);

    double widest = 0.0;
    for (const auto& [label, color] : entries_) {
        widest = std::max(widest, font.width(label, theme_ref.small_font_size));
    }
    column_width_ = swatch_size_ + 4.0 + widest + entry_spacing_;

    resolved_columns_ = columns_ > 0
                            ? columns_
                            : std::max(1, static_cast<int>(context.content.width / column_width_));
    resolved_columns_ = std::min<int>(resolved_columns_, static_cast<int>(entries_.size()));
    resolved_rows_ = static_cast<int>((entries_.size() + resolved_columns_ - 1) /
                                      static_cast<std::size_t>(resolved_columns_));
}

void LegendTrack::draw(Canvas& canvas, const TrackRect& rect) const {
    if (entries_.empty() || rect.content.empty()) return;
    const Theme& theme_ref = theme();
    const Font& font = fonts().get(theme_ref.font);
    const double row_height = rect.content.height / std::max(1, resolved_rows_);

    TextStyle text;
    text.font = theme_ref.font;
    text.size = theme_ref.small_font_size;
    text.color = theme_ref.foreground;
    text.valign = VerticalAlign::middle;

    const double block_width = column_width_ * resolved_columns_ - entry_spacing_;
    const double origin = aligned_left(align_, rect.content, block_width);

    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const int column = static_cast<int>(i) % resolved_columns_;
        const int row = static_cast<int>(i) / resolved_columns_;
        const double x = origin + column * column_width_;
        const double centre = rect.content.top() + (row + 0.5) * row_height;

        canvas.fill_rect(Rect{x, centre - swatch_size_ / 2.0, swatch_size_, swatch_size_},
                         entries_[i].second);
        canvas.draw_text(Point{x + swatch_size_ + 4.0, centre}, entries_[i].first, text);
    }
    (void)font;
}

}  // namespace gre
