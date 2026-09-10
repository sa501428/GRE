#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gre/core/genomic_transform.hpp"

namespace gre {

// A rectangle of the contact matrix.  For an intra-chromosomal square view the
// two axes name the same region.
struct MatrixRegion {
    std::string chrom_x;
    std::int64_t x_start{};
    std::int64_t x_end{};
    std::string chrom_y;
    std::int64_t y_start{};
    std::int64_t y_end{};

    [[nodiscard]] static MatrixRegion square(const GenomicRegion& region) {
        return MatrixRegion{region.chrom, region.start, region.end,
                            region.chrom, region.start, region.end};
    }
    [[nodiscard]] static MatrixRegion of(const GenomicRegion& x, const GenomicRegion& y) {
        return MatrixRegion{x.chrom, x.start, x.end, y.chrom, y.start, y.end};
    }
    [[nodiscard]] bool intra() const noexcept { return chrom_x == chrom_y; }
    [[nodiscard]] GenomicRegion x() const { return GenomicRegion{chrom_x, x_start, x_end}; }
    [[nodiscard]] GenomicRegion y() const { return GenomicRegion{chrom_y, y_start, y_end}; }
};

// A dense row-major block of matrix values.  NaN marks bins with no data.
// `region` is the block the source actually returned, which may be snapped
// outwards to bin boundaries.
struct MatrixData {
    std::size_t width{0};
    std::size_t height{0};
    std::vector<float> values;
    std::int64_t bin_size{0};
    MatrixRegion region;

    [[nodiscard]] bool empty() const noexcept { return width == 0 || height == 0; }
    [[nodiscard]] float at(std::size_t row, std::size_t column) const noexcept {
        return values[row * width + column];
    }
    [[nodiscard]] float& at(std::size_t row, std::size_t column) noexcept {
        return values[row * width + column];
    }
    void resize(std::size_t new_width, std::size_t new_height, float fill = 0.0F) {
        width = new_width;
        height = new_height;
        values.assign(new_width * new_height, fill);
    }
};

// `target_width`/`target_height` are the pixel dimensions the heatmap will
// occupy.  Implementations pick a stored resolution close to that and must not
// return vastly more.
class MatrixSource {
public:
    virtual ~MatrixSource() = default;
    virtual MatrixData query(const MatrixRegion& region, std::size_t target_width,
                             std::size_t target_height) = 0;
};

using MatrixSourcePtr = std::shared_ptr<MatrixSource>;

}  // namespace gre
