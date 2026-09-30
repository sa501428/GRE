#include "gre/tracks/heatmap_track.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "gre/core/error.hpp"

namespace gre {
namespace {

// Guards against a pathological device scale turning a track into gigabytes of
// intermediate raster.
constexpr int kMaxImageDimension = 12000;

int pixels_for(double figure_units, double device_scale) {
    const double pixels = figure_units * device_scale;
    if (!(pixels > 1.0)) return 1;
    return std::min(kMaxImageDimension, static_cast<int>(std::lround(pixels)));
}

double midpoint(const GenomicRegion& region) {
    return (static_cast<double>(region.start) + static_cast<double>(region.end)) / 2.0;
}

bool chrom_matches(const GenomicRegion& a, const GenomicRegion& b) {
    return a.chrom.empty() || b.chrom.empty() || a.chrom == b.chrom;
}

GenomicRegion expanded(GenomicRegion region, std::int64_t amount) {
    region.start = std::max<std::int64_t>(0, region.start - amount);
    region.end += amount;
    return region;
}

float sample_matrix(const MatrixData& data, double x, double y) {
    if (data.empty()) return std::numeric_limits<float>::quiet_NaN();
    const double x_span = static_cast<double>(data.region.x_end - data.region.x_start);
    const double y_span = static_cast<double>(data.region.y_end - data.region.y_start);
    if (!(x_span > 0.0) || !(y_span > 0.0)) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const auto column = static_cast<std::ptrdiff_t>(std::floor(
        (x - static_cast<double>(data.region.x_start)) / x_span * data.width));
    const auto row = static_cast<std::ptrdiff_t>(std::floor(
        (y - static_cast<double>(data.region.y_start)) / y_span * data.height));
    if (row < 0 || column < 0 || static_cast<std::size_t>(row) >= data.height ||
        static_cast<std::size_t>(column) >= data.width) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return data.at(static_cast<std::size_t>(row), static_cast<std::size_t>(column));
}

Rect visible_box(const GenomicTransform& x, const GenomicTransform& y, const GenomicRegion& xr,
                 const GenomicRegion& yr, double minimum = 4.0) {
    double left = x.x(xr.start);
    double right = x.x(xr.end);
    double top = y.x(yr.start);
    double bottom = y.x(yr.end);
    if (right < left) std::swap(left, right);
    if (bottom < top) std::swap(top, bottom);
    if (right - left < minimum) {
        const double centre = (left + right) / 2.0;
        left = centre - minimum / 2.0;
        right = centre + minimum / 2.0;
    }
    if (bottom - top < minimum) {
        const double centre = (top + bottom) / 2.0;
        top = centre - minimum / 2.0;
        bottom = centre + minimum / 2.0;
    }
    return Rect::from_edges(left, top, right, bottom);
}

std::vector<Point> ellipse_points(const Rect& rect) {
    constexpr int kSegments = 32;
    constexpr double kPi = 3.14159265358979323846;
    std::vector<Point> points;
    points.reserve(kSegments + 1);
    for (int i = 0; i <= kSegments; ++i) {
        const double angle = 2.0 * kPi * static_cast<double>(i) / kSegments;
        points.push_back(Point{rect.center_x() + std::cos(angle) * rect.width / 2.0,
                               rect.center_y() + std::sin(angle) * rect.height / 2.0});
    }
    return points;
}

Rect square_around(double x, double y, double diameter) {
    diameter = std::max(1.5, diameter);
    return Rect{x - diameter / 2.0, y - diameter / 2.0, diameter, diameter};
}

void paint_shape(Canvas& canvas, const std::vector<Point>& points, Color fill,
                 const StrokeStyle& stroke, bool closed = true) {
    if (!fill.transparent() && points.size() >= 3) canvas.fill_polygon(points, fill);
    if (!stroke.color.transparent()) {
        std::vector<Point> outline = points;
        if (closed && !outline.empty() &&
            (outline.front().x != outline.back().x || outline.front().y != outline.back().y)) {
            outline.push_back(outline.front());
        }
        canvas.stroke_polyline(outline, stroke);
    }
}

}  // namespace

void HeatmapScaleGroup::submit(std::uint64_t preparation_id, const ValueScale& requested,
                               const float* values, std::size_t count) {
    if (preparation_id_ != preparation_id) {
        preparation_id_ = preparation_id;
        values_.clear();
        finalized_ = false;
        have_scale_ = template_scale_.has_value();
        if (template_scale_.has_value()) scale_ = *template_scale_;
    }
    if (!have_scale_) {
        scale_ = requested;
        have_scale_ = true;
    }
    if (values == nullptr) return;
    values_.reserve(values_.size() + count);
    for (std::size_t i = 0; i < count; ++i) {
        if (std::isfinite(values[i])) values_.push_back(values[i]);
    }
}

void HeatmapScaleGroup::finalize(std::uint64_t preparation_id) {
    if (preparation_id_ != preparation_id || finalized_) return;
    finalized_ = true;
    if (!have_scale_) return;
    ValueScale fitted = scale_;
    if ((fitted.auto_min() || fitted.auto_max()) &&
        !fitted.fit(values_.data(), values_.size())) {
        fitted.limits(0.0, 1.0);
    } else if (scale_.auto_min() && fitted.min() > 0.0 &&
               fitted.type() != ScaleType::symlog) {
        fitted.min(0.0);
    }
    scale_ = fitted;
}

HeatmapTrack::HeatmapTrack(MatrixSourcePtr source) : source_(std::move(source)) {
    if (source_ == nullptr) {
        throw Error(ErrorCode::invalid_argument, "HeatmapTrack needs a matrix source");
    }
    scale_.upper_percentile(0.99);
    scale_policy_.upper_percentile(0.99);
    comparison_scale_.upper_percentile(0.99);
    comparison_scale_policy_.upper_percentile(0.99);
}

HeatmapTrack::HeatmapTrack(MatrixData data) : data_(std::move(data)), have_data_(true) {
    scale_.upper_percentile(0.99);
    scale_policy_.upper_percentile(0.99);
    comparison_scale_.upper_percentile(0.99);
    comparison_scale_policy_.upper_percentile(0.99);
}

HeatmapTrack& HeatmapTrack::mode(HeatmapMode value) {
    mode_ = value;
    return *this;
}

HeatmapTrack& HeatmapTrack::colors(ColorMap value) {
    colors_ = std::move(value);
    colors_set_ = true;
    return *this;
}

HeatmapTrack& HeatmapTrack::colors(const std::string& name) {
    return colors(ColorMap::named(name));
}

HeatmapTrack& HeatmapTrack::scale(ValueScale value) {
    scale_policy_ = value;
    scale_ = std::move(value);
    return *this;
}

HeatmapTrack& HeatmapTrack::limits(double low, double high) {
    scale_policy_.limits(low, high);
    scale_.limits(low, high);
    return *this;
}

HeatmapTrack& HeatmapTrack::log_scale(bool value) {
    scale_policy_.type(value ? ScaleType::log1p : ScaleType::linear);
    scale_.type(value ? ScaleType::log1p : ScaleType::linear);
    return *this;
}

HeatmapTrack& HeatmapTrack::upper_percentile(double value) {
    scale_policy_.upper_percentile(std::clamp(value, 0.0, 1.0));
    scale_.upper_percentile(std::clamp(value, 0.0, 1.0));
    return *this;
}

HeatmapTrack& HeatmapTrack::shared_scale(HeatmapScaleGroupPtr group) {
    shared_scale_ = std::move(group);
    return *this;
}

HeatmapTrack& HeatmapTrack::compare_with(MatrixSourcePtr source, MatrixHalf half) {
    if (source == nullptr) {
        throw Error(ErrorCode::invalid_argument, "comparison matrix source cannot be null");
    }
    comparison_source_ = std::move(source);
    comparison_half_ = half;
    return *this;
}

HeatmapTrack& HeatmapTrack::comparison_colors(ColorMap value) {
    comparison_colors_ = std::move(value);
    comparison_colors_set_ = true;
    return *this;
}

HeatmapTrack& HeatmapTrack::comparison_colors(const std::string& name) {
    return comparison_colors(ColorMap::named(name));
}

HeatmapTrack& HeatmapTrack::comparison_scale(ValueScale value) {
    comparison_scale_policy_ = value;
    comparison_scale_ = std::move(value);
    return *this;
}

HeatmapTrack& HeatmapTrack::comparison_limits(double low, double high) {
    comparison_scale_policy_.limits(low, high);
    comparison_scale_.limits(low, high);
    return *this;
}

HeatmapTrack& HeatmapTrack::comparison_log_scale(bool value) {
    comparison_scale_policy_.type(value ? ScaleType::log1p : ScaleType::linear);
    comparison_scale_.type(value ? ScaleType::log1p : ScaleType::linear);
    return *this;
}

HeatmapTrack& HeatmapTrack::comparison_upper_percentile(double value) {
    comparison_scale_policy_.upper_percentile(std::clamp(value, 0.0, 1.0));
    comparison_scale_.upper_percentile(std::clamp(value, 0.0, 1.0));
    return *this;
}

HeatmapTrack& HeatmapTrack::shared_comparison_scale(HeatmapScaleGroupPtr group) {
    shared_comparison_scale_ = std::move(group);
    return *this;
}

HeatmapTrack& HeatmapTrack::max_distance(std::int64_t bases) {
    max_distance_ = std::max<std::int64_t>(0, bases);
    return *this;
}

HeatmapTrack& HeatmapTrack::square_aspect(bool value) {
    square_aspect_ = value;
    return *this;
}

HeatmapTrack& HeatmapTrack::show_diagonal(bool value) {
    show_diagonal_ = value;
    return *this;
}

HeatmapTrack& HeatmapTrack::border(Color color, double width) {
    border_color_ = color;
    border_width_ = width;
    has_border_ = true;
    return *this;
}

HeatmapTrack& HeatmapTrack::interpolate(bool value) {
    interpolate_ = value;
    return *this;
}

HeatmapTrack& HeatmapTrack::background(Color color) {
    background_ = color;
    return *this;
}

PairAnnotationLayer& HeatmapTrack::add_annotation(PairAnnotationLayer layer) {
    annotations_.push_back(std::move(layer));
    return annotations_.back();
}

MapHighlight& HeatmapTrack::add_highlight(MapHighlight highlight) {
    highlights_.push_back(std::move(highlight));
    return highlights_.back();
}

double HeatmapTrack::default_height() const {
    if (resolved_height_ > 0.0) return resolved_height_;
    // Before prepare() runs this is only a hint for the provisional layout.
    return 240.0;
}

void HeatmapTrack::prepare(const ViewContext& context) {
    capture_view(context);
    image_ = Image{};
    scale_ = scale_policy_;
    comparison_scale_ = comparison_scale_policy_;
    if (!colors_set_ && context.theme != nullptr) colors_ = context.theme->heatmap_colors;
    if (!comparison_colors_set_) comparison_colors_ = colors_;
    if (comparison_source_ != nullptr && mode_ != HeatmapMode::square) {
        throw Error(ErrorCode::invalid_argument,
                    "split comparison matrices are supported only in square mode");
    }
    if (comparison_source_ != nullptr && !(context.x_region == context.y_region)) {
        throw Error(ErrorCode::invalid_argument,
                    "split comparison matrices require identical x and y regions");
    }

    const GenomicRegion& x_region = context.x_region;
    const GenomicRegion& y_region =
        mode_ == HeatmapMode::rectangle ? context.y_region : context.x_region;
    const std::int64_t span = x_region.span();
    shown_distance_ = max_distance_ > 0 ? std::min(max_distance_, span) : span;

    // Height first: the data request depends on it.
    if (has_explicit_height()) {
        resolved_height_ = height_;
    } else {
        switch (mode_) {
            case HeatmapMode::square:
                resolved_height_ = square_aspect_ ? context.content.width : 240.0;
                break;
            case HeatmapMode::triangle:
                resolved_height_ = context.content.width *
                                   (static_cast<double>(shown_distance_) /
                                    static_cast<double>(std::max<std::int64_t>(span, 1))) /
                                   2.0;
                break;
            case HeatmapMode::rectangle:
                resolved_height_ = 240.0;
                break;
        }
    }
    resolved_height_ = std::max(resolved_height_, 1.0);

    const std::size_t target_width =
        static_cast<std::size_t>(pixels_for(context.content.width, context.device_scale));
    std::size_t target_height =
        static_cast<std::size_t>(pixels_for(resolved_height_, context.device_scale));
    if (mode_ == HeatmapMode::triangle) {
        // The rotated view samples both axes at the x resolution.
        target_height = target_width;
    }

    if (source_ != nullptr) {
        const MatrixRegion region = mode_ == HeatmapMode::rectangle
                                        ? MatrixRegion::of(x_region, y_region)
                                        : MatrixRegion::square(x_region);
        data_ = source_->query(region, target_width, target_height);
        have_data_ = true;
    }
    if (!have_data_ || data_.empty()) return;

    if (comparison_source_ != nullptr) {
        comparison_data_ = comparison_source_->query(MatrixRegion::square(x_region),
                                                     target_width, target_height);
    }

    if (shared_scale_ != nullptr) {
        shared_scale_->submit(context.preparation_id, scale_, data_.values.data(),
                              data_.values.size());
    } else if (scale_.auto_min() || scale_.auto_max()) {
        ValueScale fitted = scale_;
        if (fitted.fit(data_.values.data(), data_.values.size())) {
            if (scale_.auto_min() && fitted.min() > 0.0 &&
                scale_.type() != ScaleType::symlog) {
                // Contact maps read best anchored at zero.
                fitted.min(0.0);
            }
            scale_ = fitted;
        } else {
            scale_.limits(0.0, 1.0);
        }
    }

    if (shared_comparison_scale_ != nullptr && !comparison_data_.empty()) {
        shared_comparison_scale_->submit(context.preparation_id, comparison_scale_,
                                         comparison_data_.values.data(),
                                         comparison_data_.values.size());
    } else if (comparison_source_ != nullptr && comparison_data_.empty()) {
        comparison_scale_.limits(0.0, 1.0);
    } else if (!comparison_data_.empty() &&
               (comparison_scale_.auto_min() || comparison_scale_.auto_max())) {
        ValueScale fitted = comparison_scale_;
        if (fitted.fit(comparison_data_.values.data(), comparison_data_.values.size())) {
            if (comparison_scale_.auto_min() && fitted.min() > 0.0 &&
                comparison_scale_.type() != ScaleType::symlog) {
                fitted.min(0.0);
            }
            comparison_scale_ = fitted;
        } else {
            comparison_scale_.limits(0.0, 1.0);
        }
    }

    for (PairAnnotationLayer& annotation : annotations_) {
        annotation.prepare(x_region, y_region);
    }

    if (shared_scale_ == nullptr && shared_comparison_scale_ == nullptr) {
        if (mode_ == HeatmapMode::triangle) {
            build_triangle_image(context.device_scale);
        } else {
            build_square_image();
        }
    }
}

void HeatmapTrack::finalize_prepare() {
    if (!have_data_ || data_.empty()) return;
    if (shared_scale_ != nullptr) {
        shared_scale_->finalize(view().preparation_id);
        scale_ = shared_scale_->scale();
    }
    if (shared_comparison_scale_ != nullptr && !comparison_data_.empty()) {
        shared_comparison_scale_->finalize(view().preparation_id);
        comparison_scale_ = shared_comparison_scale_->scale();
    }
    if (shared_scale_ != nullptr || shared_comparison_scale_ != nullptr) {
        if (mode_ == HeatmapMode::triangle) {
            build_triangle_image(view().device_scale);
        } else {
            build_square_image();
        }
    }
}

void HeatmapTrack::build_square_image() {
    if (comparison_source_ != nullptr) {
        const std::size_t width = std::max(data_.width, comparison_data_.width);
        const std::size_t height = std::max(data_.height, comparison_data_.height);
        image_ = Image(static_cast<int>(width), static_cast<int>(height));
        const auto& primary_lut = colors_.lut();
        const auto& comparison_lut = comparison_colors_.lut();
        const double x_start = static_cast<double>(view().x_region.start);
        const double y_start = static_cast<double>(view().y_region.start);
        const double x_span = static_cast<double>(view().x_region.span());
        const double y_span = static_cast<double>(view().y_region.span());
        for (std::size_t row = 0; row < height; ++row) {
            std::uint8_t* out = image_.row(static_cast<int>(row));
            const double y = y_start + (row + 0.5) / height * y_span;
            for (std::size_t column = 0; column < width; ++column) {
                const double x = x_start + (column + 0.5) / width * x_span;
                const bool below = y > x;
                const bool use_comparison = comparison_half_ == MatrixHalf::below ? below : !below;
                const MatrixData& selected = use_comparison ? comparison_data_ : data_;
                const ValueScale& selected_scale =
                    use_comparison ? comparison_scale_ : scale_;
                const auto& lut = use_comparison ? comparison_lut : primary_lut;
                const Color bad =
                    use_comparison ? comparison_colors_.bad() : colors_.bad();
                const double unit = selected_scale.normalize(sample_matrix(selected, x, y));
                const Color color =
                    std::isnan(unit)
                        ? bad
                        : lut[static_cast<std::size_t>(
                              unit * static_cast<double>(ColorMap::kLutSize - 1))];
                out[column * 4 + 0] = color.r;
                out[column * 4 + 1] = color.g;
                out[column * 4 + 2] = color.b;
                out[column * 4 + 3] = color.a;
            }
        }
        return;
    }

    // One image pixel per matrix bin: the backends scale it, so nothing is
    // resampled twice and the PDF embeds exactly the data that was loaded.
    image_ = Image(static_cast<int>(data_.width), static_cast<int>(data_.height));
    const auto& lut = colors_.lut();
    const Color bad = colors_.bad();
    for (std::size_t row = 0; row < data_.height; ++row) {
        std::uint8_t* out = image_.row(static_cast<int>(row));
        for (std::size_t column = 0; column < data_.width; ++column) {
            const double unit = scale_.normalize(data_.at(row, column));
            const Color color =
                std::isnan(unit)
                    ? bad
                    : lut[static_cast<std::size_t>(
                          unit * static_cast<double>(ColorMap::kLutSize - 1))];
            out[column * 4 + 0] = color.r;
            out[column * 4 + 1] = color.g;
            out[column * 4 + 2] = color.b;
            out[column * 4 + 3] = color.a;
        }
    }
}

void HeatmapTrack::build_triangle_image(double device_scale) {
    const int width = pixels_for(view().content.width, device_scale);
    const int height = pixels_for(resolved_height_, device_scale);
    image_ = Image(width, height);

    const auto& lut = colors_.lut();
    const Color bad = colors_.bad();
    const double region_start = static_cast<double>(data_.region.x_start);
    const double region_end = static_cast<double>(data_.region.x_end);
    const double bin = static_cast<double>(std::max<std::int64_t>(data_.bin_size, 1));
    const double y_origin = static_cast<double>(data_.region.y_start);
    const double view_start = static_cast<double>(view().x_region.start);
    const double view_span = static_cast<double>(std::max<std::int64_t>(view().x_region.span(), 1));
    const double distance_span = static_cast<double>(std::max<std::int64_t>(shown_distance_, 1));

    // 2x2 supersampling keeps the slanted edges of the pyramid smooth.
    constexpr int kSamples = 2;
    const double sample_weight = 1.0 / (kSamples * kSamples);

    for (int py = 0; py < height; ++py) {
        std::uint8_t* out = image_.row(py);
        for (int px = 0; px < width; ++px) {
            double accumulated[4] = {0.0, 0.0, 0.0, 0.0};
            for (int sy = 0; sy < kSamples; ++sy) {
                for (int sx = 0; sx < kSamples; ++sx) {
                    const double fx = (px + (sx + 0.5) / kSamples) / width;
                    const double fy = (py + (sy + 0.5) / kSamples) / height;
                    const double centre = view_start + fx * view_span;
                    const double distance = fy * distance_span;
                    const double low = centre - distance / 2.0;
                    const double high = centre + distance / 2.0;
                    if (low < region_start || high >= region_end) continue;

                    const auto column = static_cast<std::ptrdiff_t>((high - region_start) / bin);
                    const auto row = static_cast<std::ptrdiff_t>((low - y_origin) / bin);
                    if (row < 0 || column < 0 ||
                        static_cast<std::size_t>(row) >= data_.height ||
                        static_cast<std::size_t>(column) >= data_.width) {
                        continue;
                    }
                    const double unit = scale_.normalize(
                        data_.at(static_cast<std::size_t>(row), static_cast<std::size_t>(column)));
                    const Color color =
                        std::isnan(unit)
                            ? bad
                            : lut[static_cast<std::size_t>(
                                  unit * static_cast<double>(ColorMap::kLutSize - 1))];
                    const double alpha = color.a / 255.0;
                    accumulated[0] += color.r * alpha * sample_weight;
                    accumulated[1] += color.g * alpha * sample_weight;
                    accumulated[2] += color.b * alpha * sample_weight;
                    accumulated[3] += alpha * sample_weight;
                }
            }
            const double alpha = accumulated[3];
            if (alpha <= 0.0) {
                out[px * 4 + 0] = out[px * 4 + 1] = out[px * 4 + 2] = out[px * 4 + 3] = 0;
                continue;
            }
            out[px * 4 + 0] =
                static_cast<std::uint8_t>(std::lround(std::clamp(accumulated[0] / alpha, 0.0, 255.0)));
            out[px * 4 + 1] =
                static_cast<std::uint8_t>(std::lround(std::clamp(accumulated[1] / alpha, 0.0, 255.0)));
            out[px * 4 + 2] =
                static_cast<std::uint8_t>(std::lround(std::clamp(accumulated[2] / alpha, 0.0, 255.0)));
            out[px * 4 + 3] =
                static_cast<std::uint8_t>(std::lround(std::clamp(alpha * 255.0, 0.0, 255.0)));
        }
    }
}

void HeatmapTrack::draw_highlights(Canvas& canvas, const TrackRect& rect) const {
    for (const MapHighlight& highlight : highlights_) {
        const GenomicRegion region = expanded(highlight.region(), highlight.expansion());
        StrokeStyle stroke;
        stroke.color = highlight.border_color();
        stroke.width = highlight.border_width();
        stroke.dash = highlight.dash();

        const auto paint = [&](const Rect& band) {
            if (!highlight.fill().transparent()) canvas.fill_rect(band, highlight.fill());
            if (!stroke.color.transparent()) canvas.stroke_rect(band, stroke);
        };

        if ((highlight.axis() == HighlightAxis::vertical ||
             highlight.axis() == HighlightAxis::both) &&
            chrom_matches(region, view().x_region)) {
            const double x0 = rect.x.x(region.start);
            const double x1 = rect.x.x(region.end);
            paint(Rect::from_edges(std::min(x0, x1), rect.content.top(), std::max(x0, x1),
                                   rect.content.bottom()));
        }

        // A triangle map's vertical coordinate is contact distance rather than
        // a genomic axis, so a horizontal genomic highlight has no honest
        // projection there.
        if (mode_ != HeatmapMode::triangle &&
            (highlight.axis() == HighlightAxis::horizontal ||
             highlight.axis() == HighlightAxis::both) &&
            chrom_matches(region, view().y_region)) {
            const double y0 = rect.y.x(region.start);
            const double y1 = rect.y.x(region.end);
            paint(Rect::from_edges(rect.content.left(), std::min(y0, y1),
                                   rect.content.right(), std::max(y0, y1)));
        }
    }
}

void HeatmapTrack::draw_annotations(Canvas& canvas, const TrackRect& rect) const {
    for (const PairAnnotationLayer& layer : annotations_) {
        StrokeStyle base_stroke;
        base_stroke.color = layer.color();
        base_stroke.width = layer.line_width();
        base_stroke.dash = layer.dash();
        base_stroke.join = LineJoin::round;

        TextStyle label_style;
        label_style.font = theme().font;
        label_style.size = layer.label_font_size() > 0.0 ? layer.label_font_size()
                                                        : theme().small_font_size;
        label_style.color = layer.color();
        label_style.valign = VerticalAlign::bottom;

        for (const PairFeature& feature : layer.features()) {
            GenomicRegion first = expanded(feature.first, layer.expansion());
            GenomicRegion second = expanded(feature.second, layer.expansion());
            StrokeStyle stroke = base_stroke;
            stroke.color = layer.color_for(feature);
            stroke.width = layer.line_width_for(feature);
            const Color feature_fill = layer.fill_for(feature);
            const double feature_size = layer.size_for(feature);
            TextStyle feature_label_style = label_style;
            feature_label_style.color = stroke.color;

            if (mode_ == HeatmapMode::triangle) {
                if (layer.side() == AnnotationSide::below ||
                    !chrom_matches(first, view().x_region) ||
                    !chrom_matches(second, view().x_region)) {
                    continue;
                }
                if (midpoint(first) > midpoint(second)) std::swap(first, second);
                const double start = std::min<double>(first.start, second.start);
                const double end = std::max<double>(first.end, second.end);
                const double centre = (midpoint(first) + midpoint(second)) / 2.0;
                const double distance = std::max(0.0, midpoint(second) - midpoint(first));
                const double x = rect.x.x(centre);
                const double y = rect.content.top() +
                                 std::min(distance / std::max<double>(shown_distance_, 1.0), 1.0) *
                                     rect.content.height;

                Rect label_box;
                if (layer.style() == PairAnnotationStyle::domain) {
                    const Point left{rect.x.x(start), rect.content.top()};
                    const Point right{rect.x.x(end), rect.content.top()};
                    const Point apex{rect.x.x((start + end) / 2.0),
                                     rect.content.top() +
                                         (end - start) /
                                             std::max<double>(shown_distance_, 1.0) *
                                             rect.content.height};
                    // The top edge follows the main Hi-C diagonal. Only draw
                    // the two domain legs, even for clipped large domains.
                    if (!feature_fill.transparent()) {
                        const Point fill_points[] = {left, right, apex};
                        canvas.fill_polygon(fill_points, feature_fill);
                    }
                    if (!stroke.color.transparent()) {
                        const Point leg_points[] = {left, apex, right};
                        canvas.stroke_polyline(leg_points, stroke);
                    }
                    label_box = Rect::from_edges(rect.x.x(start), rect.content.top(),
                                                 rect.x.x(end), apex.y);
                } else {
                    const double width = feature_size * std::max(
                        4.0, std::fabs(rect.x.width_of(first.span() + second.span()) / 2.0));
                    if (layer.style() == PairAnnotationStyle::loop) {
                        label_box = square_around(x, y, width);
                        paint_shape(canvas, ellipse_points(label_box), feature_fill, stroke);
                    } else {
                        const double height = std::max(4.0, width * 0.65);
                        label_box = Rect{x - width / 2.0, y - height / 2.0, width, height};
                        if (!feature_fill.transparent()) canvas.fill_rect(label_box, feature_fill);
                        canvas.stroke_rect(label_box, stroke);
                    }
                }
                if (layer.labels_visible() && !feature.name.empty()) {
                    canvas.draw_text(Point{label_box.right() + 2.0, label_box.top()}, feature.name,
                                     feature_label_style);
                }
                continue;
            }

            const bool same_axis = mode_ == HeatmapMode::square &&
                                   view().x_region == view().y_region &&
                                   first.chrom == second.chrom &&
                                   chrom_matches(first, view().x_region);

            const auto draw_at = [&](const GenomicRegion& xr, const GenomicRegion& yr,
                                     AnnotationSide side) {
                Rect label_box;
                if (layer.style() == PairAnnotationStyle::domain && same_axis) {
                    const double start = std::min<double>(first.start, second.start);
                    const double end = std::max<double>(first.end, second.end);
                    std::vector<Point> triangle;
                    if (side == AnnotationSide::above) {
                        triangle = {{rect.x.x(start), rect.y.x(start)},
                                    {rect.x.x(end), rect.y.x(start)},
                                    {rect.x.x(end), rect.y.x(end)}};
                    } else {
                        triangle = {{rect.x.x(start), rect.y.x(start)},
                                    {rect.x.x(start), rect.y.x(end)},
                                    {rect.x.x(end), rect.y.x(end)}};
                    }
                    paint_shape(canvas, triangle, feature_fill, stroke, false);
                    label_box = visible_box(rect.x, rect.y,
                                            GenomicRegion{first.chrom,
                                                          static_cast<std::int64_t>(start),
                                                          static_cast<std::int64_t>(end)},
                                            GenomicRegion{first.chrom,
                                                          static_cast<std::int64_t>(start),
                                                          static_cast<std::int64_t>(end)});
                } else {
                    label_box = visible_box(rect.x, rect.y, xr, yr);
                    if (layer.style() == PairAnnotationStyle::loop) {
                        // Loop anchors are commonly different widths on x and y.  Use one
                        // device-space diameter so the glyph remains circular regardless
                        // of anchor span or rectangular map dimensions.
                        const double diameter = feature_size * std::max(
                            4.0, std::sqrt(label_box.width * label_box.height));
                        label_box = square_around(label_box.center_x(), label_box.center_y(),
                                                  diameter);
                    } else if (feature_size != 1.0) {
                        const double width = label_box.width * feature_size;
                        const double height = label_box.height * feature_size;
                        label_box = Rect{label_box.center_x() - width / 2.0,
                                         label_box.center_y() - height / 2.0, width, height};
                    }
                    if (layer.style() == PairAnnotationStyle::loop) {
                        paint_shape(canvas, ellipse_points(label_box), feature_fill, stroke);
                    } else {
                        if (!feature_fill.transparent()) canvas.fill_rect(label_box, feature_fill);
                        canvas.stroke_rect(label_box, stroke);
                    }
                }
                if (layer.labels_visible() && !feature.name.empty()) {
                    canvas.draw_text(Point{label_box.right() + 2.0, label_box.top()}, feature.name,
                                     feature_label_style);
                }
            };

            if (same_axis) {
                if (midpoint(first) > midpoint(second)) std::swap(first, second);
                if (layer.side() == AnnotationSide::above ||
                    layer.side() == AnnotationSide::both) {
                    draw_at(second, first, AnnotationSide::above);
                }
                if (layer.side() == AnnotationSide::below ||
                    layer.side() == AnnotationSide::both) {
                    draw_at(first, second, AnnotationSide::below);
                }
            } else if (chrom_matches(first, view().x_region) &&
                       chrom_matches(second, view().y_region)) {
                draw_at(first, second, AnnotationSide::above);
            } else if (chrom_matches(second, view().x_region) &&
                       chrom_matches(first, view().y_region)) {
                draw_at(second, first, AnnotationSide::above);
            }
        }
    }
}

void HeatmapTrack::draw(Canvas& canvas, const TrackRect& rect) const {
    if (rect.content.empty()) return;
    if (!background_.transparent()) canvas.fill_rect(rect.content, background_);
    if (image_.empty()) return;

    const ImageScaling scaling =
        interpolate_ ? ImageScaling::bilinear : ImageScaling::automatic;
    canvas.draw_image(rect.content, image_.view(scaling));

    draw_highlights(canvas, rect);
    draw_annotations(canvas, rect);

    if (show_diagonal_ && mode_ == HeatmapMode::square) {
        StrokeStyle stroke;
        stroke.color = theme().muted;
        stroke.width = theme().grid_line_width;
        canvas.stroke_line(Point{rect.content.left(), rect.content.top()},
                           Point{rect.content.right(), rect.content.bottom()}, stroke);
    }

    if (has_border_ && !border_color_.transparent()) {
        StrokeStyle stroke;
        stroke.color = border_color_;
        stroke.width = border_width_;
        if (mode_ == HeatmapMode::triangle) {
            // Trace the pyramid rather than a box around it.
            const Point outline[3] = {
                {rect.content.left(), rect.content.top()},
                {rect.content.right(), rect.content.top()},
                {rect.content.center_x(), rect.content.bottom()},
            };
            std::vector<Point> loop(outline, outline + 3);
            loop.push_back(outline[0]);
            canvas.stroke_polyline(loop, stroke);
        } else {
            canvas.stroke_rect(rect.content, stroke);
        }
    }
}

}  // namespace gre
