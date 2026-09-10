#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gre/core/color.hpp"
#include "gre/core/genomic_transform.hpp"

namespace gre {

enum class Strand : char { unknown = '.', forward = '+', reverse = '-' };

struct Exon {
    std::int64_t start{};
    std::int64_t end{};
};

// One drawable interval.  Gene tracks use `exons` and the thick (coding)
// range; interval tracks ignore both.
struct Feature {
    std::string name;
    std::string chrom;
    std::int64_t start{};
    std::int64_t end{};
    Strand strand{Strand::unknown};
    std::vector<Exon> exons;
    // Coding range.  thick_end <= thick_start means "nothing thick".
    std::int64_t thick_start{0};
    std::int64_t thick_end{0};
    std::optional<Color> color;
    std::optional<double> score;

    [[nodiscard]] std::int64_t length() const noexcept { return end > start ? end - start : 0; }
    [[nodiscard]] bool has_thick() const noexcept { return thick_end > thick_start; }
};

class FeatureSource {
public:
    virtual ~FeatureSource() = default;
    virtual std::vector<Feature> query(const GenomicRegion& region) = 0;
};

using FeatureSourcePtr = std::shared_ptr<FeatureSource>;

}  // namespace gre
