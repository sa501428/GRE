#include "gre/data/memory_sources.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "gre/core/error.hpp"

namespace gre {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

}  // namespace

bool SignalData::range(double& low, double& high) const noexcept {
    bool any = false;
    for (double value : values) {
        if (!std::isfinite(value)) continue;
        if (!any) {
            low = high = value;
            any = true;
        } else {
            low = std::min(low, value);
            high = std::max(high, value);
        }
    }
    return any;
}

MemorySignalSource::MemorySignalSource(std::string chrom, std::vector<Interval> intervals)
    : chrom_(std::move(chrom)), intervals_(std::move(intervals)) {
    std::sort(intervals_.begin(), intervals_.end(),
              [](const Interval& a, const Interval& b) { return a.start < b.start; });
}

MemorySignalSource::MemorySignalSource(GenomicRegion region, std::vector<double> values)
    : chrom_(std::move(region.chrom)) {
    if (values.empty()) return;
    const double bin =
        static_cast<double>(region.span()) / static_cast<double>(values.size());
    intervals_.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        intervals_.push_back(
            Interval{region.start + static_cast<std::int64_t>(std::llround(i * bin)),
                     region.start + static_cast<std::int64_t>(std::llround((i + 1) * bin)),
                     values[i]});
    }
}

std::shared_ptr<MemorySignalSource> MemorySignalSource::make(std::string chrom,
                                                             std::vector<Interval> intervals) {
    return std::make_shared<MemorySignalSource>(std::move(chrom), std::move(intervals));
}

SignalData MemorySignalSource::query(const GenomicRegion& region, std::size_t target_bins) {
    SignalData out;
    out.region = region;
    const std::size_t bins = std::max<std::size_t>(1, target_bins);
    out.values.assign(bins, kNaN);
    out.bin_size = static_cast<double>(region.span()) / static_cast<double>(bins);
    if (!chrom_.empty() && !region.chrom.empty() && chrom_ != region.chrom) return out;
    if (out.bin_size <= 0.0) return out;

    // Overlap-weighted mean, so narrow features are not lost when many source
    // intervals fall into one output bin.
    std::vector<double> weight(bins, 0.0);
    std::vector<double> total(bins, 0.0);
    for (const Interval& interval : intervals_) {
        if (interval.end <= region.start || interval.start >= region.end) continue;
        if (!std::isfinite(interval.value)) continue;
        const double start = std::max<double>(interval.start, region.start);
        const double end = std::min<double>(interval.end, region.end);
        auto first = static_cast<std::ptrdiff_t>((start - region.start) / out.bin_size);
        auto last = static_cast<std::ptrdiff_t>((end - region.start) / out.bin_size);
        first = std::clamp<std::ptrdiff_t>(first, 0, static_cast<std::ptrdiff_t>(bins) - 1);
        last = std::clamp<std::ptrdiff_t>(last, 0, static_cast<std::ptrdiff_t>(bins) - 1);
        for (std::ptrdiff_t b = first; b <= last; ++b) {
            const double bin_start = region.start + static_cast<double>(b) * out.bin_size;
            const double bin_end = bin_start + out.bin_size;
            const double overlap = std::min(end, bin_end) - std::max(start, bin_start);
            if (overlap <= 0.0) continue;
            total[static_cast<std::size_t>(b)] += interval.value * overlap;
            weight[static_cast<std::size_t>(b)] += overlap;
        }
    }
    for (std::size_t i = 0; i < bins; ++i) {
        if (weight[i] > 0.0) out.values[i] = total[i] / weight[i];
    }
    return out;
}

MemoryFeatureSource::MemoryFeatureSource(std::vector<Feature> features)
    : features_(std::move(features)) {}

std::shared_ptr<MemoryFeatureSource> MemoryFeatureSource::make(std::vector<Feature> features) {
    return std::make_shared<MemoryFeatureSource>(std::move(features));
}

std::vector<Feature> MemoryFeatureSource::query(const GenomicRegion& region) {
    std::vector<Feature> out;
    for (const Feature& feature : features_) {
        if (!feature.chrom.empty() && !region.chrom.empty() && feature.chrom != region.chrom) {
            continue;
        }
        if (feature.end <= region.start || feature.start >= region.end) continue;
        out.push_back(feature);
    }
    return out;
}

MemoryMatrixSource::MemoryMatrixSource(MatrixRegion region, std::int64_t bin_size,
                                       std::size_t width, std::size_t height,
                                       std::vector<float> values) {
    if (values.size() != width * height) {
        throw Error(ErrorCode::invalid_argument,
                    "matrix value count does not match the given dimensions");
    }
    stored_.region = std::move(region);
    stored_.bin_size = bin_size;
    stored_.width = width;
    stored_.height = height;
    stored_.values = std::move(values);
}

std::shared_ptr<MemoryMatrixSource> MemoryMatrixSource::make(MatrixRegion region,
                                                             std::int64_t bin_size,
                                                             std::size_t width, std::size_t height,
                                                             std::vector<float> values) {
    return std::make_shared<MemoryMatrixSource>(std::move(region), bin_size, width, height,
                                                std::move(values));
}

MatrixData MemoryMatrixSource::query(const MatrixRegion& region, std::size_t target_width,
                                     std::size_t target_height) {
    if (stored_.empty() || stored_.bin_size <= 0) return MatrixData{};

    const auto column_of = [&](std::int64_t position) {
        return (position - stored_.region.x_start) / stored_.bin_size;
    };
    const auto row_of = [&](std::int64_t position) {
        return (position - stored_.region.y_start) / stored_.bin_size;
    };

    const std::int64_t first_column =
        std::clamp<std::int64_t>(column_of(region.x_start), 0,
                                 static_cast<std::int64_t>(stored_.width));
    const std::int64_t last_column =
        std::clamp<std::int64_t>(column_of(region.x_end - 1) + 1, first_column,
                                 static_cast<std::int64_t>(stored_.width));
    const std::int64_t first_row = std::clamp<std::int64_t>(
        row_of(region.y_start), 0, static_cast<std::int64_t>(stored_.height));
    const std::int64_t last_row = std::clamp<std::int64_t>(
        row_of(region.y_end - 1) + 1, first_row, static_cast<std::int64_t>(stored_.height));

    const auto source_width = static_cast<std::size_t>(last_column - first_column);
    const auto source_height = static_cast<std::size_t>(last_row - first_row);
    if (source_width == 0 || source_height == 0) return MatrixData{};

    // Aggregate only when the caller asked for meaningfully less than we hold.
    const std::size_t out_width =
        target_width > 0 ? std::min(source_width, std::max<std::size_t>(target_width, 1))
                         : source_width;
    const std::size_t out_height =
        target_height > 0 ? std::min(source_height, std::max<std::size_t>(target_height, 1))
                          : source_height;

    MatrixData out;
    out.resize(out_width, out_height, 0.0F);
    out.bin_size = stored_.bin_size * static_cast<std::int64_t>(
                                          std::max<std::size_t>(1, source_width / out_width));
    out.region = MatrixRegion{region.chrom_x,
                              stored_.region.x_start + first_column * stored_.bin_size,
                              stored_.region.x_start + last_column * stored_.bin_size,
                              region.chrom_y,
                              stored_.region.y_start + first_row * stored_.bin_size,
                              stored_.region.y_start + last_row * stored_.bin_size};

    std::vector<double> sums(out_width * out_height, 0.0);
    std::vector<double> counts(out_width * out_height, 0.0);
    for (std::size_t r = 0; r < source_height; ++r) {
        const std::size_t out_row = std::min(out_height - 1, r * out_height / source_height);
        for (std::size_t c = 0; c < source_width; ++c) {
            const std::size_t out_column = std::min(out_width - 1, c * out_width / source_width);
            const float value =
                stored_.at(static_cast<std::size_t>(first_row) + r,
                           static_cast<std::size_t>(first_column) + c);
            if (!std::isfinite(value)) continue;
            sums[out_row * out_width + out_column] += value;
            counts[out_row * out_width + out_column] += 1.0;
        }
    }
    for (std::size_t i = 0; i < sums.size(); ++i) {
        out.values[i] = counts[i] > 0.0 ? static_cast<float>(sums[i] / counts[i])
                                        : std::numeric_limits<float>::quiet_NaN();
    }
    return out;
}

}  // namespace gre
