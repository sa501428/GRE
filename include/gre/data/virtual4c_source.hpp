#pragma once

#include <memory>

#include "gre/data/matrix_source.hpp"
#include "gre/data/signal_source.hpp"

namespace gre {

// A direct one-dimensional view of contacts between a fixed viewpoint and a
// requested genomic region. This is extraction/rendering, not a caller or an
// aggregate analysis: each output value comes from one matrix column reduced
// only across the bins covered by the supplied viewpoint interval.
class Virtual4CSource : public SignalSource {
public:
    enum class Aggregation { mean, sum, maximum };

    Virtual4CSource(MatrixSourcePtr matrix, GenomicRegion viewpoint);

    [[nodiscard]] static std::shared_ptr<Virtual4CSource> make(
        MatrixSourcePtr matrix, GenomicRegion viewpoint);

    SignalData query(const GenomicRegion& region, std::size_t target_bins) override;

    Virtual4CSource& aggregation(Aggregation value);
    [[nodiscard]] const GenomicRegion& viewpoint() const noexcept { return viewpoint_; }

private:
    MatrixSourcePtr matrix_;
    GenomicRegion viewpoint_;
    Aggregation aggregation_{Aggregation::mean};
};

}  // namespace gre
