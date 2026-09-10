#pragma once

#include <string>
#include <vector>

#include "gre/data/feature_source.hpp"
#include "gre/data/matrix_source.hpp"
#include "gre/data/signal_source.hpp"

namespace gre {

// Sources backed by data the caller already has.  Useful for values computed
// in-process, for tests, and as a worked example of the adapter contract.
class MemorySignalSource : public SignalSource {
public:
    struct Interval {
        std::int64_t start{};
        std::int64_t end{};
        double value{};
    };

    // Interval records, bedGraph style.  They need not be sorted.
    MemorySignalSource(std::string chrom, std::vector<Interval> intervals);
    // Values already on a uniform grid across `region`.
    MemorySignalSource(GenomicRegion region, std::vector<double> values);

    SignalData query(const GenomicRegion& region, std::size_t target_bins) override;

    [[nodiscard]] static std::shared_ptr<MemorySignalSource> make(
        std::string chrom, std::vector<Interval> intervals);

private:
    std::string chrom_;
    std::vector<Interval> intervals_;
};

class MemoryFeatureSource : public FeatureSource {
public:
    explicit MemoryFeatureSource(std::vector<Feature> features);

    std::vector<Feature> query(const GenomicRegion& region) override;

    [[nodiscard]] static std::shared_ptr<MemoryFeatureSource> make(std::vector<Feature> features);

private:
    std::vector<Feature> features_;
};

// A dense block held in memory.  Queries that ask for less resolution than is
// stored are aggregated by mean rather than decimated, so nothing disappears.
class MemoryMatrixSource : public MatrixSource {
public:
    MemoryMatrixSource(MatrixRegion region, std::int64_t bin_size, std::size_t width,
                       std::size_t height, std::vector<float> values);

    MatrixData query(const MatrixRegion& region, std::size_t target_width,
                     std::size_t target_height) override;

    [[nodiscard]] static std::shared_ptr<MemoryMatrixSource> make(MatrixRegion region,
                                                                  std::int64_t bin_size,
                                                                  std::size_t width,
                                                                  std::size_t height,
                                                                  std::vector<float> values);

private:
    MatrixData stored_;
};

}  // namespace gre
