#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "gre/core/genomic_transform.hpp"

namespace gre {

// A uniformly binned signal over one region.  NaN marks bins with no data,
// which tracks draw as gaps rather than zeros.
struct SignalData {
    GenomicRegion region;
    std::vector<double> values;
    double bin_size{0.0};

    [[nodiscard]] std::size_t size() const noexcept { return values.size(); }
    [[nodiscard]] bool empty() const noexcept { return values.empty(); }
    [[nodiscard]] double bin_start(std::size_t index) const noexcept {
        return static_cast<double>(region.start) + static_cast<double>(index) * bin_size;
    }
    [[nodiscard]] double bin_end(std::size_t index) const noexcept {
        return bin_start(index) + bin_size;
    }
    // Extremes over the finite values; returns false when everything is NaN.
    [[nodiscard]] bool range(double& low, double& high) const noexcept;
};

// The adapter genomic readers implement.  Keeping it this small is what stops
// file-format details from reaching the drawing code.
//
// `target_bins` is the number of bins the figure can actually show, derived
// from the track's pixel width.  Implementations should not return
// substantially more resolution than that.
class SignalSource {
public:
    virtual ~SignalSource() = default;
    virtual SignalData query(const GenomicRegion& region, std::size_t target_bins) = 0;
};

using SignalSourcePtr = std::shared_ptr<SignalSource>;

}  // namespace gre
