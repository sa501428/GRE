#pragma once

#include <memory>
#include <string>
#include <vector>

#include "gre/data/feature_source.hpp"
#include "gre/data/signal_source.hpp"
#include "igv/records.hpp"

namespace gre {

// Converts one igv-cpp record into the renderer's representation, including
// exon blocks, the coding range and an itemRgb colour when present.
[[nodiscard]] Feature feature_from_igv(const igv::Feature& feature);

// 1D quantitative data (bigWig, bedGraph, wig) read through igv-cpp.
class IgvSignalSource : public SignalSource {
public:
    enum class Aggregation { mean, max, min, sum };

    explicit IgvSignalSource(std::string path);
    ~IgvSignalSource() override;

    IgvSignalSource(const IgvSignalSource&) = delete;
    IgvSignalSource& operator=(const IgvSignalSource&) = delete;

    [[nodiscard]] static std::shared_ptr<IgvSignalSource> open(std::string path);

    SignalData query(const GenomicRegion& region, std::size_t target_bins) override;

    // How several records falling in one output bin are combined.  Peaks are
    // easiest to read with `max`; coverage with `mean`, the default.
    IgvSignalSource& aggregation(Aggregation value);
    // Bins with no overlapping record become this value instead of NaN.  Use
    // 0 for coverage tracks that should read as zero where the file is silent.
    IgvSignalSource& fill_empty(double value);

    [[nodiscard]] const std::string& path() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Intervals and gene models (BED, GFF/GTF, genePred, PSL, bigBed, cytoband).
class IgvFeatureSource : public FeatureSource {
public:
    explicit IgvFeatureSource(std::string path);
    ~IgvFeatureSource() override;

    IgvFeatureSource(const IgvFeatureSource&) = delete;
    IgvFeatureSource& operator=(const IgvFeatureSource&) = delete;

    [[nodiscard]] static std::shared_ptr<IgvFeatureSource> open(std::string path);

    std::vector<Feature> query(const GenomicRegion& region) override;

    // Keep only features whose `type` matches, which is how a GFF is reduced
    // to genes or transcripts.  Empty keeps everything.
    IgvFeatureSource& feature_type(std::string type);
    // Cap on features returned per query, so a dense annotation over a wide
    // region cannot swamp the figure.  0 means no limit.
    IgvFeatureSource& max_features(std::size_t limit);

    [[nodiscard]] const std::string& path() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gre
