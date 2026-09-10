#include "gre/core/scale.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace gre {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

double forward(double value, ScaleType type) noexcept {
    switch (type) {
        case ScaleType::linear:
            return value;
        case ScaleType::log:
            return value > 0.0 ? std::log10(value) : kNaN;
        case ScaleType::log1p:
            return value > -1.0 ? std::log10(1.0 + value) : kNaN;
        case ScaleType::symlog:
            return std::copysign(std::log10(1.0 + std::fabs(value)), value);
    }
    return value;
}

double inverse(double value, ScaleType type) noexcept {
    switch (type) {
        case ScaleType::linear:
            return value;
        case ScaleType::log:
            return std::pow(10.0, value);
        case ScaleType::log1p:
            return std::pow(10.0, value) - 1.0;
        case ScaleType::symlog:
            return std::copysign(std::pow(10.0, std::fabs(value)) - 1.0, value);
    }
    return value;
}

// Percentile of a scratch copy; `p` in [0, 1].  Uses nth_element so that large
// matrices are not fully sorted twice.
double percentile_of(std::vector<double>& scratch, double p) {
    if (scratch.empty()) return kNaN;
    if (p <= 0.0) return *std::min_element(scratch.begin(), scratch.end());
    if (p >= 1.0) return *std::max_element(scratch.begin(), scratch.end());
    const auto index = static_cast<std::size_t>(p * static_cast<double>(scratch.size() - 1));
    auto nth = scratch.begin() + static_cast<std::ptrdiff_t>(index);
    std::nth_element(scratch.begin(), nth, scratch.end());
    return *nth;
}

bool fit_from(std::vector<double>&& finite, ValueScale& scale, bool auto_min, bool auto_max,
              double lower_p, double upper_p) {
    if (finite.empty()) return false;
    double lo = scale.min();
    double hi = scale.max();
    if (auto_min) lo = percentile_of(finite, lower_p);
    if (auto_max) hi = percentile_of(finite, upper_p);
    if (!(hi > lo)) {
        // A constant field still has to produce a usable range.
        const double magnitude = std::max(std::fabs(hi), 1.0);
        lo = hi - magnitude * 0.5;
        hi = hi + magnitude * 0.5;
    }
    if (auto_min) scale.min(lo);
    if (auto_max) scale.max(hi);
    return true;
}

std::string group_digits(long long value) {
    const bool negative = value < 0;
    unsigned long long magnitude =
        negative ? static_cast<unsigned long long>(-(value + 1)) + 1ULL
                 : static_cast<unsigned long long>(value);
    std::string digits = std::to_string(magnitude);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3 + 1);
    const std::size_t lead = digits.size() % 3 == 0 ? 3 : digits.size() % 3;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i == lead || (i > lead && (i - lead) % 3 == 0)) out.push_back(',');
        out.push_back(digits[i]);
    }
    return negative ? "-" + out : out;
}

double nice_step(double rough) {
    if (!(rough > 0.0)) return 1.0;
    const double magnitude = std::pow(10.0, std::floor(std::log10(rough)));
    const double residual = rough / magnitude;
    if (residual <= 1.0) return magnitude;
    if (residual <= 2.0) return 2.0 * magnitude;
    if (residual <= 5.0) return 5.0 * magnitude;
    return 10.0 * magnitude;
}

}  // namespace

bool ValueScale::fit(const std::vector<double>& values) {
    std::vector<double> finite;
    finite.reserve(values.size());
    for (double v : values) {
        if (std::isfinite(v)) finite.push_back(v);
    }
    return fit_from(std::move(finite), *this, auto_min_, auto_max_, lower_percentile_,
                    upper_percentile_);
}

bool ValueScale::fit(const float* values, std::size_t count) {
    std::vector<double> finite;
    finite.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (std::isfinite(values[i])) finite.push_back(static_cast<double>(values[i]));
    }
    return fit_from(std::move(finite), *this, auto_min_, auto_max_, lower_percentile_,
                    upper_percentile_);
}

double ValueScale::normalize(double value) const noexcept {
    if (!std::isfinite(value)) return kNaN;
    const double t = forward(value, type_);
    const double lo = forward(min_, type_);
    const double hi = forward(max_, type_);
    if (!std::isfinite(t) || !std::isfinite(lo) || !std::isfinite(hi) || hi == lo) return kNaN;
    const double unit = (t - lo) / (hi - lo);
    if (unit < 0.0 || unit > 1.0) {
        if (!clip_) return kNaN;
        return std::clamp(unit, 0.0, 1.0);
    }
    return unit;
}

double ValueScale::denormalize(double unit) const noexcept {
    const double lo = forward(min_, type_);
    const double hi = forward(max_, type_);
    return inverse(lo + unit * (hi - lo), type_);
}

std::vector<float> ValueScale::build_lut(std::size_t size) const {
    std::vector<float> table(size);
    if (size == 0) return table;
    for (std::size_t i = 0; i < size; ++i) {
        const double value =
            min_ + (max_ - min_) * static_cast<double>(i) / static_cast<double>(size - 1);
        table[i] = static_cast<float>(normalize(value));
    }
    return table;
}

std::vector<double> nice_ticks(double lo, double hi, int max_ticks) {
    std::vector<double> ticks;
    if (!std::isfinite(lo) || !std::isfinite(hi) || max_ticks < 1) return ticks;
    if (hi < lo) std::swap(lo, hi);
    if (hi == lo) {
        ticks.push_back(lo);
        return ticks;
    }
    const double step = nice_step((hi - lo) / max_ticks);
    const double first = std::ceil(lo / step) * step;
    // Guard against a step so small that the loop would not terminate.
    if (!(step > 0.0)) return ticks;
    for (double v = first; v <= hi + step * 1e-9; v += step) {
        // Snap values that are a rounding error away from zero.
        ticks.push_back(std::fabs(v) < step * 1e-9 ? 0.0 : v);
        if (ticks.size() > static_cast<std::size_t>(max_ticks) * 4) break;
    }
    return ticks;
}

std::vector<double> scale_ticks(const ValueScale& scale, int max_ticks) {
    if (scale.type() == ScaleType::linear) {
        return nice_ticks(scale.min(), scale.max(), max_ticks);
    }
    // Round to a readable value near each evenly spaced position.
    const auto round_nice = [](double value) {
        if (value == 0.0 || !std::isfinite(value)) return 0.0;
        const double magnitude = std::pow(10.0, std::floor(std::log10(std::fabs(value))));
        const double candidates[] = {1.0, 1.5, 2.0, 3.0, 5.0, 7.0, 10.0};
        double best = magnitude;
        double best_error = std::fabs(value) / magnitude;
        for (double candidate : candidates) {
            const double error = std::fabs(std::fabs(value) / magnitude - candidate);
            if (error < best_error) {
                best_error = error;
                best = candidate * magnitude;
            }
        }
        return std::copysign(best, value);
    };

    std::vector<double> ticks;
    const int count = std::max(2, max_ticks);
    for (int i = 0; i < count; ++i) {
        const double unit = static_cast<double>(i) / (count - 1);
        const double value = round_nice(scale.denormalize(unit));
        if (!std::isfinite(value)) continue;
        if (value < scale.min() || value > scale.max()) continue;
        if (!ticks.empty() && std::fabs(value - ticks.back()) <=
                                  std::max(std::fabs(value), 1.0) * 1e-9) {
            continue;
        }
        ticks.push_back(value);
    }
    if (ticks.empty()) ticks.push_back(scale.min());
    return ticks;
}

double tick_step(const std::vector<double>& ticks) {
    if (ticks.size() < 2) return 0.0;
    double step = std::fabs(ticks[1] - ticks[0]);
    for (std::size_t i = 2; i < ticks.size(); ++i) {
        step = std::min(step, std::fabs(ticks[i] - ticks[i - 1]));
    }
    return step;
}

std::string format_tick(double value, double step) {
    if (!std::isfinite(value)) return "";
    const double magnitude = std::max(std::fabs(value), std::fabs(step));
    if (magnitude != 0.0 && (magnitude >= 1e6 || magnitude < 1e-4)) {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%.3g", value);
        return std::string(buffer);
    }
    int decimals = 0;
    if (step > 0.0 && step < 1.0) {
        decimals = static_cast<int>(std::ceil(-std::log10(step)));
        decimals = std::clamp(decimals, 0, 6);
    }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    std::string out(buffer);
    if (out == "-0") out = "0";
    return out;
}

std::vector<long long> genomic_ticks(long long start, long long end, int max_ticks) {
    std::vector<long long> ticks;
    if (end <= start || max_ticks < 1) return ticks;
    const double rough = static_cast<double>(end - start) / max_ticks;
    auto step = static_cast<long long>(std::llround(nice_step(rough)));
    if (step < 1) step = 1;
    long long first = (start + step - 1) / step * step;
    if (first < start) first += step;
    for (long long v = first; v < end; v += step) ticks.push_back(v);
    return ticks;
}

std::string format_position(long long position, long long step) {
    const auto render = [](double value, long long divisor, long long tick_step,
                           const char* suffix) {
        int decimals = 0;
        // Enough decimals to keep neighbouring ticks distinct in the chosen unit.
        const double step_in_unit = static_cast<double>(tick_step) / static_cast<double>(divisor);
        if (step_in_unit < 1.0 && step_in_unit > 0.0) {
            decimals = static_cast<int>(std::ceil(-std::log10(step_in_unit)));
            decimals = std::clamp(decimals, 0, 3);
        }
        char buffer[48];
        std::snprintf(buffer, sizeof buffer, "%.*f %s", decimals, value, suffix);
        return std::string(buffer);
    };

    // Switch units well before the step reaches a whole unit: at 500 kb
    // spacing "1.5 Mb" reads better than "1500 kb".
    if (step >= 100'000) {
        return render(static_cast<double>(position) / 1e6, 1'000'000, step, "Mb");
    }
    if (step >= 100) {
        return render(static_cast<double>(position) / 1e3, 1'000, step, "kb");
    }
    return group_digits(position) + " bp";
}

std::string format_region(const std::string& chrom, long long start, long long end) {
    return chrom + ":" + group_digits(start) + "-" + group_digits(end);
}

}  // namespace gre
