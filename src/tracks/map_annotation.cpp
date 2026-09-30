#include "gre/tracks/map_annotation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "gre/core/error.hpp"

namespace gre {

PairAnnotationLayer::PairAnnotationLayer(PairFeatureSourcePtr source)
    : source_(std::move(source)) {
    if (source_ == nullptr) {
        throw Error(ErrorCode::invalid_argument, "PairAnnotationLayer needs a pair source");
    }
}

PairAnnotationLayer& PairAnnotationLayer::style(PairAnnotationStyle value) {
    style_ = value;
    return *this;
}
PairAnnotationLayer& PairAnnotationLayer::side(AnnotationSide value) {
    side_ = value;
    return *this;
}
PairAnnotationLayer& PairAnnotationLayer::color(Color value) {
    color_ = value;
    color_override_ = true;
    return *this;
}
PairAnnotationLayer& PairAnnotationLayer::fill(Color value) {
    fill_ = value;
    return *this;
}
PairAnnotationLayer& PairAnnotationLayer::line_width(double value) {
    line_width_ = std::max(0.1, value);
    return *this;
}
PairAnnotationLayer& PairAnnotationLayer::dash(std::vector<double> value) {
    value.erase(std::remove_if(value.begin(), value.end(),
                               [](double length) { return !(length > 0.0); }),
                value.end());
    if (value.size() % 2 == 1) {
        const std::vector<double> repeated = value;
        value.insert(value.end(), repeated.begin(), repeated.end());
    }
    dash_ = std::move(value);
    return *this;
}
PairAnnotationLayer& PairAnnotationLayer::dashed(bool value) {
    dash_ = value ? std::vector<double>{4.0, 2.5} : std::vector<double>{};
    return *this;
}
PairAnnotationLayer& PairAnnotationLayer::expand(std::int64_t bases) {
    expansion_ = std::max<std::int64_t>(0, bases);
    return *this;
}
PairAnnotationLayer& PairAnnotationLayer::show_labels(bool value) {
    show_labels_ = value;
    return *this;
}
PairAnnotationLayer& PairAnnotationLayer::label_font_size(double value) {
    label_font_size_ = std::max(0.0, value);
    return *this;
}
PairAnnotationLayer& PairAnnotationLayer::score_filter(std::optional<double> minimum,
                                                       std::optional<double> maximum) {
    if (minimum.has_value() && maximum.has_value() && *maximum < *minimum) {
        throw Error(ErrorCode::invalid_argument, "BEDPE score maximum is below minimum");
    }
    score_minimum_ = minimum;
    score_maximum_ = maximum;
    return *this;
}
PairAnnotationLayer& PairAnnotationLayer::color_by_score(ColorMap colors, ValueScale scale) {
    score_colors_ = std::move(colors);
    score_scale_ = std::move(scale);
    return *this;
}
PairAnnotationLayer& PairAnnotationLayer::color_by_score(const std::string& colors,
                                                         ValueScale scale) {
    return color_by_score(ColorMap::named(colors), std::move(scale));
}
PairAnnotationLayer& PairAnnotationLayer::opacity_by_score(double minimum, double maximum) {
    score_opacity_ = std::pair{std::clamp(minimum, 0.0, 1.0),
                               std::clamp(maximum, 0.0, 1.0)};
    return *this;
}
PairAnnotationLayer& PairAnnotationLayer::line_width_by_score(double minimum, double maximum) {
    score_line_width_ = std::pair{std::max(0.1, minimum), std::max(0.1, maximum)};
    return *this;
}
PairAnnotationLayer& PairAnnotationLayer::size_by_score(double minimum, double maximum) {
    score_size_ = std::pair{std::max(0.05, minimum), std::max(0.05, maximum)};
    return *this;
}

namespace {
double score_unit(const PairFeature& feature, const ValueScale& scale) {
    if (!feature.score.has_value()) return std::numeric_limits<double>::quiet_NaN();
    return scale.normalize(*feature.score);
}

double interpolate_range(const std::optional<std::pair<double, double>>& range, double unit,
                         double fallback) {
    if (!range.has_value() || !std::isfinite(unit)) return fallback;
    return range->first + std::clamp(unit, 0.0, 1.0) * (range->second - range->first);
}
}  // namespace

Color PairAnnotationLayer::color_for(const PairFeature& feature) const noexcept {
    const double unit = score_unit(feature, fitted_score_scale_);
    Color result = score_colors_.has_value() && std::isfinite(unit)
                       ? score_colors_->at(unit)
                       : color_override_ ? color_ : feature.color.value_or(color_);
    return with_alpha(result, interpolate_range(score_opacity_, unit, 1.0));
}
Color PairAnnotationLayer::fill_for(const PairFeature& feature) const noexcept {
    const double unit = score_unit(feature, fitted_score_scale_);
    Color result = fill_;
    if (score_colors_.has_value() && std::isfinite(unit) && !fill_.transparent()) {
        const Color score_color = score_colors_->at(unit);
        result.r = score_color.r;
        result.g = score_color.g;
        result.b = score_color.b;
    }
    return with_alpha(result, interpolate_range(score_opacity_, unit, 1.0));
}
double PairAnnotationLayer::line_width_for(const PairFeature& feature) const noexcept {
    return interpolate_range(score_line_width_, score_unit(feature, fitted_score_scale_),
                             line_width_);
}
double PairAnnotationLayer::size_for(const PairFeature& feature) const noexcept {
    return interpolate_range(score_size_, score_unit(feature, fitted_score_scale_), 1.0);
}

void PairAnnotationLayer::prepare(const GenomicRegion& x_region,
                                  const GenomicRegion& y_region) {
    features_ = source_->query(x_region, y_region);
    if (score_minimum_.has_value() || score_maximum_.has_value()) {
        features_.erase(
            std::remove_if(features_.begin(), features_.end(), [&](const PairFeature& feature) {
                if (!feature.score.has_value() || !std::isfinite(*feature.score)) return true;
                return (score_minimum_.has_value() && *feature.score < *score_minimum_) ||
                       (score_maximum_.has_value() && *feature.score > *score_maximum_);
            }),
            features_.end());
    }
    fitted_score_scale_ = score_scale_;
    if (fitted_score_scale_.auto_min() || fitted_score_scale_.auto_max()) {
        std::vector<double> scores;
        scores.reserve(features_.size());
        for (const PairFeature& feature : features_) {
            if (feature.score.has_value() && std::isfinite(*feature.score)) {
                scores.push_back(*feature.score);
            }
        }
        if (!fitted_score_scale_.fit(scores)) fitted_score_scale_.limits(0.0, 1.0);
    }
}

MapHighlight::MapHighlight(GenomicRegion region, HighlightAxis axis)
    : region_(std::move(region)), axis_(axis) {
    if (region_.empty()) {
        throw Error(ErrorCode::invalid_argument, "map highlight region must be non-empty");
    }
}
MapHighlight& MapHighlight::axis(HighlightAxis value) {
    axis_ = value;
    return *this;
}
MapHighlight& MapHighlight::fill(Color value) {
    fill_ = value;
    return *this;
}
MapHighlight& MapHighlight::border(Color value, double width) {
    border_color_ = value;
    border_width_ = std::max(0.1, width);
    return *this;
}
MapHighlight& MapHighlight::dash(std::vector<double> value) {
    value.erase(std::remove_if(value.begin(), value.end(),
                               [](double length) { return !(length > 0.0); }),
                value.end());
    if (value.size() % 2 == 1) {
        const std::vector<double> repeated = value;
        value.insert(value.end(), repeated.begin(), repeated.end());
    }
    dash_ = std::move(value);
    return *this;
}
MapHighlight& MapHighlight::dashed(bool value) {
    dash_ = value ? std::vector<double>{4.0, 2.5} : std::vector<double>{};
    return *this;
}
MapHighlight& MapHighlight::expand(std::int64_t bases) {
    expansion_ = std::max<std::int64_t>(0, bases);
    return *this;
}

}  // namespace gre
