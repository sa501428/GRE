#include "gre/tracks/interval_track.hpp"

#include <algorithm>
#include <cmath>

#include "gre/core/error.hpp"

namespace gre {
namespace {

constexpr double kLabelGap = 3.0;

}  // namespace

IntervalTrack::IntervalTrack(FeatureSourcePtr source) : source_(std::move(source)) {
    if (source_ == nullptr) {
        throw Error(ErrorCode::invalid_argument, "IntervalTrack needs a feature source");
    }
}

IntervalTrack::IntervalTrack(std::vector<Feature> features)
    : features_(std::move(features)), have_data_(true) {}

IntervalTrack& IntervalTrack::color(Color value) {
    color_ = value;
    return *this;
}

IntervalTrack& IntervalTrack::color_by_score(ColorMap map) {
    score_colors_ = std::move(map);
    color_by_score_ = true;
    return *this;
}

IntervalTrack& IntervalTrack::score_scale(ValueScale value) {
    score_scale_ = std::move(value);
    return *this;
}

IntervalTrack& IntervalTrack::box_height(double value) {
    box_height_ = std::max(0.5, value);
    return *this;
}

IntervalTrack& IntervalTrack::row_height(double value) {
    row_height_ = std::max(1.0, value);
    return *this;
}

IntervalTrack& IntervalTrack::packed(bool value) {
    packed_ = value;
    return *this;
}

IntervalTrack& IntervalTrack::max_rows(int value) {
    max_rows_ = std::max(1, value);
    return *this;
}

IntervalTrack& IntervalTrack::show_labels(bool value) {
    show_labels_ = value;
    return *this;
}

IntervalTrack& IntervalTrack::border(Color color, double width) {
    border_color_ = color;
    border_width_ = width;
    has_border_ = true;
    return *this;
}

IntervalTrack& IntervalTrack::rounded(bool value) {
    rounded_ = value;
    return *this;
}

double IntervalTrack::default_height() const {
    return packed_ ? std::max(1, rows_) * row_height_ : row_height_;
}

void IntervalTrack::prepare(const ViewContext& context) {
    capture_view(context);
    if (source_ != nullptr) {
        features_ = source_->query(context.x_region);
        have_data_ = true;
    }
    rows_of_.assign(features_.size(), 0);
    rows_ = 1;
    if (features_.empty()) return;

    std::sort(features_.begin(), features_.end(),
              [](const Feature& a, const Feature& b) { return a.start < b.start; });

    if (color_by_score_ && (score_scale_.auto_min() || score_scale_.auto_max())) {
        std::vector<double> scores;
        scores.reserve(features_.size());
        for (const Feature& feature : features_) {
            if (feature.score.has_value()) scores.push_back(*feature.score);
        }
        if (!score_scale_.fit(scores)) score_scale_.limits(0.0, 1.0);
    }

    if (!packed_) return;

    const GenomicTransform transform{context.x_region, context.content.left(),
                                     context.content.right()};
    std::vector<double> row_end;
    for (std::size_t i = 0; i < features_.size(); ++i) {
        const double x0 = transform.x(features_[i].start);
        const double x1 = transform.x(features_[i].end);
        int row = 0;
        while (row < static_cast<int>(row_end.size()) &&
               row_end[static_cast<std::size_t>(row)] > x0) {
            ++row;
        }
        if (row >= max_rows_) row = max_rows_ - 1;
        if (row >= static_cast<int>(row_end.size())) {
            row_end.resize(static_cast<std::size_t>(row) + 1, 0.0);
        }
        row_end[static_cast<std::size_t>(row)] = std::max(x1 + 1.0, row_end[static_cast<std::size_t>(row)]);
        rows_of_[i] = row;
    }
    rows_ = std::max<int>(1, static_cast<int>(row_end.size()));
}

void IntervalTrack::draw(Canvas& canvas, const TrackRect& rect) const {
    if (features_.empty() || rect.content.empty()) return;
    const Theme& theme_ref = theme();
    const Color base = color_.transparent() ? theme_ref.interval_color : color_;
    const double row_height = rect.content.height / std::max(1, rows_);

    TextStyle label_style;
    label_style.font = theme_ref.font;
    label_style.size = theme_ref.small_font_size;
    label_style.color = theme_ref.foreground;
    label_style.valign = VerticalAlign::middle;

    StrokeStyle stroke;
    stroke.color = border_color_;
    stroke.width = border_width_;

    for (std::size_t i = 0; i < features_.size(); ++i) {
        const Feature& feature = features_[i];
        const int row = packed_ ? rows_of_[i] : 0;
        const double centre = rect.content.top() + (row + 0.5) * row_height;
        const double height = std::min(box_height_, row_height);
        const double x0 = rect.x.x(feature.start);
        const double x1 = rect.x.x(feature.end);

        Color ink = feature.color.value_or(base);
        if (color_by_score_ && feature.score.has_value()) {
            ink = score_colors_.at(score_scale_.normalize(*feature.score));
        }

        // Sub-pixel intervals still need to be visible.
        const Rect box{x0, centre - height / 2.0, std::max(x1 - x0, 0.6), height};
        if (rounded_) {
            const double radius = std::min(height / 2.0, box.width / 2.0);
            canvas.fill_rect(Rect{box.x + radius, box.y, std::max(box.width - 2 * radius, 0.0),
                                  box.height},
                             ink);
            canvas.fill_circle(Point{box.x + radius, box.center_y()}, radius, ink);
            canvas.fill_circle(Point{box.right() - radius, box.center_y()}, radius, ink);
        } else {
            canvas.fill_rect(box, ink);
        }
        if (has_border_ && !border_color_.transparent()) canvas.stroke_rect(box, stroke);

        if (show_labels_ && !feature.name.empty()) {
            canvas.draw_text(Point{x1 + kLabelGap, centre}, feature.name, label_style);
        }
    }
}

}  // namespace gre
