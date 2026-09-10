#include "gre/tracks/signal_track.hpp"

#include <algorithm>
#include <cmath>

#include "gre/core/error.hpp"

namespace gre {
namespace {

// Below this bin width, individual bars stop being distinguishable and a
// single stepped polygon is both faster and cleaner.
constexpr double kMinBarWidth = 1.5;

}  // namespace

SignalTrack::SignalTrack(SignalSourcePtr source) : source_(std::move(source)) {
    if (source_ == nullptr) {
        throw Error(ErrorCode::invalid_argument, "SignalTrack needs a signal source");
    }
}

SignalTrack::SignalTrack(SignalData data) : data_(std::move(data)), have_data_(true) {}

SignalTrack& SignalTrack::style(SignalStyle value) {
    style_ = value;
    return *this;
}

SignalTrack& SignalTrack::color(Color value) {
    color_ = value;
    return *this;
}

SignalTrack& SignalTrack::fill_color(Color value) {
    fill_color_ = value;
    return *this;
}

SignalTrack& SignalTrack::negative_color(Color value) {
    negative_color_ = value;
    return *this;
}

SignalTrack& SignalTrack::line_width(double value) {
    line_width_ = value;
    return *this;
}

SignalTrack& SignalTrack::scale(ValueScale value) {
    scale_ = std::move(value);
    return *this;
}

SignalTrack& SignalTrack::limits(double low, double high) {
    scale_.limits(low, high);
    return *this;
}

SignalTrack& SignalTrack::log_scale(bool value) {
    scale_.type(value ? ScaleType::log1p : ScaleType::linear);
    return *this;
}

SignalTrack& SignalTrack::baseline(double value) {
    baseline_ = value;
    baseline_set_ = true;
    return *this;
}

SignalTrack& SignalTrack::show_range_label(bool value) {
    show_range_label_ = value;
    return *this;
}

SignalTrack& SignalTrack::y_axis(bool value) {
    y_axis_ = value;
    return *this;
}

SignalTrack& SignalTrack::bar_gap(double value) {
    bar_gap_ = std::max(0.0, value);
    return *this;
}

double SignalTrack::label_reserve() const { return y_axis_ ? 26.0 : 0.0; }

void SignalTrack::prepare(const ViewContext& context) {
    capture_view(context);
    if (source_ != nullptr) {
        data_ = source_->query(context.x_region, context.target_bins());
        have_data_ = true;
    }
    if (!have_data_) return;

    if (scale_.auto_min() || scale_.auto_max()) {
        ValueScale fitted = scale_;
        if (fitted.fit(data_.values)) {
            // Anchoring at zero is almost always what a coverage track wants.
            if (scale_.auto_min() && fitted.min() > 0.0 && fitted.max() > 0.0) {
                fitted.min(0.0);
            }
            if (scale_.auto_max() && fitted.max() < 0.0) fitted.max(0.0);
            scale_ = fitted;
        } else {
            scale_.limits(0.0, 1.0);
        }
    }
    if (!baseline_set_) {
        baseline_ = std::clamp(0.0, scale_.min(), scale_.max());
    }
}

double SignalTrack::y_of(double value, const Rect& content) const {
    const double unit = scale_.normalize(value);
    if (!std::isfinite(unit)) return content.bottom();
    return content.bottom() - unit * content.height;
}

void SignalTrack::draw(Canvas& canvas, const TrackRect& rect) const {
    if (!have_data_ || data_.empty() || rect.content.empty()) return;
    const Theme& theme_ref = theme();
    const Color ink = color_.transparent() ? theme_ref.signal_color : color_;
    const Color negative_ink = negative_color_.transparent() ? ink : negative_color_;
    const Color fill = fill_color_.transparent() ? ink : fill_color_;
    const Rect& content = rect.content;
    const double base_y = y_of(baseline_, content);

    const auto x_left = [&](std::size_t i) { return rect.x.x(data_.bin_start(i)); };
    const auto x_right = [&](std::size_t i) { return rect.x.x(data_.bin_end(i)); };

    switch (style_) {
        case SignalStyle::bars: {
            const double bin_width = std::max(x_right(0) - x_left(0), 0.0);
            if (bin_width >= kMinBarWidth) {
                for (std::size_t i = 0; i < data_.size(); ++i) {
                    const double value = data_.values[i];
                    if (!std::isfinite(value)) continue;
                    const double y = y_of(value, content);
                    const double top = std::min(y, base_y);
                    const double bottom = std::max(y, base_y);
                    if (bottom - top <= 0.0) continue;
                    const double gap = std::min(bar_gap_, bin_width * 0.4);
                    canvas.fill_rect(
                        Rect{x_left(i) + gap / 2.0, top, bin_width - gap, bottom - top},
                        value < baseline_ ? negative_ink : fill);
                }
                break;
            }
            [[fallthrough]];  // too narrow to separate: draw as a filled profile
        }
        case SignalStyle::area: {
            // Runs of finite bins become one stepped polygon each, so gaps in
            // the data stay gaps instead of being bridged.
            std::size_t i = 0;
            while (i < data_.size()) {
                while (i < data_.size() && !std::isfinite(data_.values[i])) ++i;
                if (i >= data_.size()) break;
                std::size_t end = i;
                while (end < data_.size() && std::isfinite(data_.values[end])) ++end;

                std::vector<Point> polygon;
                polygon.reserve((end - i) * 2 + 2);
                polygon.push_back(Point{x_left(i), base_y});
                for (std::size_t k = i; k < end; ++k) {
                    const double y = y_of(data_.values[k], content);
                    polygon.push_back(Point{x_left(k), y});
                    polygon.push_back(Point{x_right(k), y});
                }
                polygon.push_back(Point{x_right(end - 1), base_y});
                canvas.fill_polygon(polygon, fill);
                i = end;
            }
            break;
        }
        case SignalStyle::line: {
            StrokeStyle stroke;
            stroke.color = ink;
            stroke.width = line_width_;
            stroke.join = LineJoin::round;
            std::size_t i = 0;
            while (i < data_.size()) {
                while (i < data_.size() && !std::isfinite(data_.values[i])) ++i;
                if (i >= data_.size()) break;
                std::vector<Point> run;
                while (i < data_.size() && std::isfinite(data_.values[i])) {
                    run.push_back(Point{(x_left(i) + x_right(i)) / 2.0,
                                        y_of(data_.values[i], content)});
                    ++i;
                }
                if (run.size() >= 2) canvas.stroke_polyline(run, stroke);
            }
            break;
        }
        case SignalStyle::points: {
            const double radius = std::max(line_width_, 0.9);
            for (std::size_t i = 0; i < data_.size(); ++i) {
                if (!std::isfinite(data_.values[i])) continue;
                canvas.fill_circle(
                    Point{(x_left(i) + x_right(i)) / 2.0, y_of(data_.values[i], content)}, radius,
                    ink);
            }
            break;
        }
    }

    // A baseline rule, when the data crosses it.
    if (scale_.min() < baseline_ && baseline_ < scale_.max()) {
        StrokeStyle zero;
        zero.color = theme_ref.muted;
        zero.width = theme_ref.grid_line_width;
        canvas.stroke_line(Point{content.left(), base_y}, Point{content.right(), base_y}, zero);
    }

    if (y_axis_ && rect.label.width > 0.0) {
        StrokeStyle stroke;
        stroke.color = theme_ref.axis;
        stroke.width = theme_ref.axis_line_width;
        canvas.stroke_line(Point{content.left(), content.top()},
                           Point{content.left(), content.bottom()}, stroke);

        TextStyle style;
        style.font = theme_ref.font;
        style.size = theme_ref.small_font_size;
        style.color = theme_ref.muted;
        style.align = TextAlign::right;
        style.valign = VerticalAlign::middle;

        const std::vector<double> ticks = scale_ticks(scale_, 3);
        const double step = ticks.size() >= 2 ? tick_step(ticks) : scale_.max();
        for (double tick : ticks) {
            const double y = y_of(tick, content);
            if (y < content.top() - 0.5 || y > content.bottom() + 0.5) continue;
            canvas.stroke_line(Point{content.left(), y},
                               Point{content.left() - theme_ref.tick_length, y}, stroke);
            canvas.draw_text(Point{content.left() - theme_ref.tick_length - 1.5, y},
                             format_tick(tick, step), style);
        }
    } else if (show_range_label_) {
        TextStyle style;
        style.font = theme_ref.font;
        style.size = theme_ref.small_font_size;
        style.color = theme_ref.muted;
        style.valign = VerticalAlign::top;
        const double step = std::fabs(scale_.max() - scale_.min());
        canvas.draw_text(Point{content.left() + 2.0, content.top() + 1.0},
                         "[" + format_tick(scale_.min(), step) + " - " +
                             format_tick(scale_.max(), step) + "]",
                         style);
    }
}

}  // namespace gre
