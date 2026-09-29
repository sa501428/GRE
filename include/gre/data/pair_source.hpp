#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gre/core/color.hpp"
#include "gre/core/genomic_transform.hpp"

namespace gre {

// A two-dimensional genomic feature, such as a loop, interaction, or domain
// encoded as BEDPE. Coordinates are zero-based, half-open.
struct PairFeature {
    std::string name;
    GenomicRegion first;
    GenomicRegion second;
    std::optional<Color> color;
    std::optional<double> score;
};

class PairFeatureSource {
public:
    virtual ~PairFeatureSource() = default;
    virtual std::vector<PairFeature> query(const GenomicRegion& x_region,
                                           const GenomicRegion& y_region) = 0;
};

using PairFeatureSourcePtr = std::shared_ptr<PairFeatureSource>;

// A BEDPE reader. Local paths and public HTTP(S) URLs may point to plain text
// or gzip-compressed files. The first six BEDPE columns are required; name and
// score are read when present. Records are kept in memory because loop/domain
// files are normally sparse.
class BedpeSource : public PairFeatureSource {
public:
    explicit BedpeSource(std::string path);

    [[nodiscard]] static std::shared_ptr<BedpeSource> open(std::string path);
    std::vector<PairFeature> query(const GenomicRegion& x_region,
                                   const GenomicRegion& y_region) override;

    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] const std::vector<PairFeature>& features() const noexcept { return features_; }

private:
    std::string path_;
    std::vector<PairFeature> features_;
};

class MemoryPairFeatureSource : public PairFeatureSource {
public:
    explicit MemoryPairFeatureSource(std::vector<PairFeature> features);

    [[nodiscard]] static std::shared_ptr<MemoryPairFeatureSource> make(
        std::vector<PairFeature> features);
    std::vector<PairFeature> query(const GenomicRegion& x_region,
                                   const GenomicRegion& y_region) override;

private:
    std::vector<PairFeature> features_;
};

}  // namespace gre
