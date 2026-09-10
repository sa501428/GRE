#include "gre/tracks/heatmap_track.hpp"

#include <algorithm>
#include <cmath>

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

}  // namespace

HeatmapTrack::HeatmapTrack(MatrixSourcePtr source) : source_(std::move(source)) {
    if (source_ == nullptr) {
        throw Error(ErrorCode::invalid_argument, "HeatmapTrack needs a matrix source");
    }
    scale_.upper_percentile(0.99);
}

HeatmapTrack::HeatmapTrack(MatrixData data) : data_(std::move(data)), have_data_(true) {
    scale_.upper_percentile(0.99);
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
    scale_ = std::move(value);
    return *this;
}

HeatmapTrack& HeatmapTrack::limits(double low, double high) {
    scale_.limits(low, high);
    return *this;
}

HeatmapTrack& HeatmapTrack::log_scale(bool value) {
    scale_.type(value ? ScaleType::log1p : ScaleType::linear);
    return *this;
}

HeatmapTrack& HeatmapTrack::upper_percentile(double value) {
    scale_.upper_percentile(std::clamp(value, 0.0, 1.0));
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

double HeatmapTrack::default_height() const {
    if (resolved_height_ > 0.0) return resolved_height_;
    // Before prepare() runs this is only a hint for the provisional layout.
    return 240.0;
}

void HeatmapTrack::prepare(const ViewContext& context) {
    capture_view(context);
    if (!colors_set_ && context.theme != nullptr) colors_ = context.theme->heatmap_colors;

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

    if (scale_.auto_min() || scale_.auto_max()) {
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

    if (mode_ == HeatmapMode::triangle) {
        build_triangle_image(context.device_scale);
    } else {
        build_square_image();
    }
}

void HeatmapTrack::build_square_image() {
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

void HeatmapTrack::draw(Canvas& canvas, const TrackRect& rect) const {
    if (rect.content.empty()) return;
    if (!background_.transparent()) canvas.fill_rect(rect.content, background_);
    if (image_.empty()) return;

    const ImageScaling scaling =
        interpolate_ ? ImageScaling::bilinear : ImageScaling::automatic;
    canvas.draw_image(rect.content, image_.view(scaling));

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
