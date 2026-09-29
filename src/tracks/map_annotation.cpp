#include "gre/tracks/map_annotation.hpp"

#include <algorithm>

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
void PairAnnotationLayer::prepare(const GenomicRegion& x_region,
                                  const GenomicRegion& y_region) {
    features_ = source_->query(x_region, y_region);
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
