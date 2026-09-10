#pragma once

#include <cstdint>
#include <string>

namespace gre {

// Zero-based, half-open [start, end), matching igv::GenomicInterval.
struct GenomicRegion {
    std::string chrom;
    std::int64_t start{};
    std::int64_t end{};

    [[nodiscard]] std::int64_t span() const noexcept { return end > start ? end - start : 0; }
    [[nodiscard]] bool empty() const noexcept { return end <= start; }

    friend bool operator==(const GenomicRegion& a, const GenomicRegion& b) noexcept {
        return a.chrom == b.chrom && a.start == b.start && a.end == b.end;
    }
};

// The single place where genomic coordinates become figure coordinates.
// Tracks must not re-derive this mapping.
struct GenomicTransform {
    std::int64_t start{0};
    std::int64_t end{1};
    double x0{0.0};
    double x1{1.0};

    GenomicTransform() = default;
    GenomicTransform(std::int64_t region_start, std::int64_t region_end, double from, double to)
        : start(region_start), end(region_end), x0(from), x1(to) {}
    GenomicTransform(const GenomicRegion& region, double from, double to)
        : start(region.start), end(region.end), x0(from), x1(to) {}

    [[nodiscard]] std::int64_t span() const noexcept { return end > start ? end - start : 1; }
    [[nodiscard]] double extent() const noexcept { return x1 - x0; }

    // Figure position of a genomic coordinate.  Not clamped: callers that need
    // clamping should clip, so that partially visible features keep their slope.
    [[nodiscard]] double x(std::int64_t position) const noexcept {
        return x0 + (static_cast<double>(position - start) / static_cast<double>(span())) *
                        (x1 - x0);
    }

    [[nodiscard]] double x(double position) const noexcept {
        return x0 + ((position - static_cast<double>(start)) / static_cast<double>(span())) *
                        (x1 - x0);
    }

    // Figure width of a genomic span.
    [[nodiscard]] double width_of(std::int64_t bases) const noexcept {
        return (static_cast<double>(bases) / static_cast<double>(span())) * (x1 - x0);
    }

    // Inverse mapping, for tick placement and hit testing.
    [[nodiscard]] std::int64_t position(double figure_x) const noexcept {
        const double extent_value = x1 - x0;
        if (extent_value == 0.0) return start;
        return start + static_cast<std::int64_t>((figure_x - x0) / extent_value *
                                                 static_cast<double>(span()));
    }

    [[nodiscard]] double bases_per_unit() const noexcept {
        const double extent_value = x1 - x0;
        if (extent_value == 0.0) return static_cast<double>(span());
        return static_cast<double>(span()) / extent_value;
    }
};

}  // namespace gre
