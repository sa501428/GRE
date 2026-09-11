#include "gre/tracks/axis_track.hpp"

#include <algorithm>

#include "gre/core/scale.hpp"

namespace gre {

AxisTrack::AxisTrack() { show_name_ = false; }

AxisTrack& AxisTrack::position(AxisPosition value) {
    position_ = value;
    return *this;
}

AxisTrack& AxisTrack::max_ticks(int value) {
    max_ticks_ = std::max(1, value);
    return *this;
}

AxisTrack& AxisTrack::color(Color value) {
    color_ = value;
    return *this;
}

AxisTrack& AxisTrack::font_size(double value) {
    font_size_ = value;
    return *this;
}

AxisTrack& AxisTrack::show_line(bool value) {
    show_line_ = value;
    return *this;
}

AxisTrack& AxisTrack::show_region(bool value) {
    show_region_ = value;
    return *this;
}

double AxisTrack::default_height() const {
    const double size = font_size_ > 0.0 ? font_size_ : theme().font_size;
    const Font& font = fonts().get(theme().font);
    return theme().tick_length + 2.0 + font.line_height(size);
}

void AxisTrack::prepare(const ViewContext& context) {
    capture_view(context);
    // Fewer ticks on a narrow track, so labels never collide.
    const double per_tick = 62.0;
    const int fitting = std::max(2, static_cast<int>(context.content.width / per_tick));
    ticks_ = genomic_ticks(context.x_region.start, context.x_region.end,
                           std::min(max_ticks_, fitting));
    step_ = ticks_.size() >= 2 ? ticks_[1] - ticks_[0] : context.x_region.span();
}

void AxisTrack::draw(Canvas& canvas, const TrackRect& rect) const {
    const Theme& theme_ref = theme();
    const Color ink = color_.transparent() ? theme_ref.axis : color_;
    const double size = font_size_ > 0.0 ? font_size_ : theme_ref.font_size;
    const bool above = position_ == AxisPosition::top;
    const double line_y = above ? rect.content.bottom() : rect.content.top();
    const double tick_end = above ? line_y - theme_ref.tick_length : line_y + theme_ref.tick_length;

    StrokeStyle stroke;
    stroke.color = ink;
    stroke.width = theme_ref.axis_line_width;

    if (show_line_) {
        canvas.stroke_line(Point{rect.content.left(), line_y}, Point{rect.content.right(), line_y},
                           stroke);
    }

    TextStyle label;
    label.font = theme_ref.font;
    label.size = size;
    label.color = ink;
    label.align = TextAlign::center;
    label.valign = above ? VerticalAlign::bottom : VerticalAlign::top;

    // The region label owns the right-hand end of the rule; tick labels that
    // would run into it are dropped rather than overprinted.
    std::string region_text;
    double region_left = rect.content.right() + 1.0;
    TextStyle region_style = label;
    if (show_region_) {
        region_style.align = TextAlign::right;
        region_style.color = theme_ref.muted;
        region_style.size = theme_ref.small_font_size;
        region_text = format_region(view().x_region.chrom, view().x_region.start,
                                    view().x_region.end);
        region_left = rect.content.right() -
                      canvas.measure_text(region_text, region_style).width - 6.0;
    }

    for (long long tick : ticks_) {
        const double x = rect.x.x(static_cast<std::int64_t>(tick));
        if (x < rect.content.left() - 0.5 || x > rect.content.right() + 0.5) continue;
        canvas.stroke_line(Point{x, line_y}, Point{x, tick_end}, stroke);

        const std::string text = format_position(tick, step_);
        const double half = canvas.measure_text(text, label).width / 2.0;
        if (x + half > region_left) continue;

        // A tick sitting on the left edge would otherwise hang its label off
        // the plotting area and have it clipped away.
        TextStyle placed = label;
        double label_x = x;
        if (x - half < rect.content.left()) {
            placed.align = TextAlign::left;
            label_x = rect.content.left();
        }
        canvas.draw_text(Point{label_x, above ? tick_end - 1.0 : tick_end + 1.0}, text, placed);
    }

    if (show_region_) {
        canvas.draw_text(Point{rect.content.right(), above ? tick_end - 1.0 : tick_end + 1.0},
                         region_text, region_style);
    }
}

}  // namespace gre
