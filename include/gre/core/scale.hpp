#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace gre {

enum class ScaleType {
    linear,
    log,     // log10 of the value; non-positive values become NaN
    log1p,   // log10(1 + value); keeps zeros, common for raw contact counts
    symlog,  // signed log, for observed/expected style ratios
};

// Maps data values onto the unit interval used by colour maps and by the
// vertical axis of signal tracks.
class ValueScale {
public:
    ValueScale() = default;
    ValueScale(double min_value, double max_value, ScaleType type = ScaleType::linear)
        : min_(min_value), max_(max_value), type_(type), auto_min_(false), auto_max_(false) {}

    ValueScale& type(ScaleType value) noexcept {
        type_ = value;
        return *this;
    }
    [[nodiscard]] ScaleType type() const noexcept { return type_; }

    ValueScale& min(double value) noexcept {
        min_ = value;
        auto_min_ = false;
        return *this;
    }
    ValueScale& max(double value) noexcept {
        max_ = value;
        auto_max_ = false;
        return *this;
    }
    ValueScale& limits(double lo, double hi) noexcept { return min(lo).max(hi); }

    [[nodiscard]] double min() const noexcept { return min_; }
    [[nodiscard]] double max() const noexcept { return max_; }
    [[nodiscard]] bool auto_min() const noexcept { return auto_min_; }
    [[nodiscard]] bool auto_max() const noexcept { return auto_max_; }

    // When either limit is automatic, it is taken from a percentile of the
    // data rather than the extremes.  Hi-C matrices in particular have a long
    // tail that would otherwise wash the whole map out.
    ValueScale& upper_percentile(double p) noexcept {
        upper_percentile_ = p;
        return *this;
    }
    ValueScale& lower_percentile(double p) noexcept {
        lower_percentile_ = p;
        return *this;
    }
    [[nodiscard]] double upper_percentile() const noexcept { return upper_percentile_; }
    [[nodiscard]] double lower_percentile() const noexcept { return lower_percentile_; }

    // Values below `min` clamp to 0 and above `max` clamp to 1 unless
    // clipping is disabled, in which case out-of-range values return NaN and
    // are drawn as "missing".
    ValueScale& clip(bool value) noexcept {
        clip_ = value;
        return *this;
    }
    [[nodiscard]] bool clip() const noexcept { return clip_; }

    // Fills automatic limits from the supplied values.  NaNs are ignored.
    // Returns false when the data contained nothing usable.
    bool fit(const std::vector<double>& values);
    bool fit(const float* values, std::size_t count);

    // Data value -> [0, 1].  Returns NaN for missing or (unclipped)
    // out-of-range input.
    [[nodiscard]] double normalize(double value) const noexcept;

    // Inverse of `normalize`, used to label axes and colour bars.
    [[nodiscard]] double denormalize(double unit) const noexcept;

    // Builds a 1024-entry table mapping a linear value ramp onto normalized
    // positions, so heatmap rendering is a multiply and an index.
    [[nodiscard]] std::vector<float> build_lut(std::size_t size) const;

private:
    double min_{0.0};
    double max_{1.0};
    ScaleType type_{ScaleType::linear};
    bool auto_min_{true};
    bool auto_max_{true};
    bool clip_{true};
    double lower_percentile_{0.0};
    double upper_percentile_{1.0};
};

// "Nice" tick positions for a linear axis: at most `max_ticks` values at
// 1/2/5 x 10^k spacing covering [lo, hi].
[[nodiscard]] std::vector<double> nice_ticks(double lo, double hi, int max_ticks);

// Formats a value with the fewest digits that still distinguishes neighbouring
// ticks spaced `step` apart.
[[nodiscard]] std::string format_tick(double value, double step);

// Genomic tick spacing in base pairs (1/2/5 x 10^k), plus a formatter that
// picks bp / kb / Mb automatically.
[[nodiscard]] std::vector<long long> genomic_ticks(long long start, long long end, int max_ticks);
[[nodiscard]] std::string format_position(long long position, long long step);
// Short form used for axis labels, e.g. "chr8:127,000,000-129,000,000".
[[nodiscard]] std::string format_region(const std::string& chrom, long long start, long long end);

}  // namespace gre
