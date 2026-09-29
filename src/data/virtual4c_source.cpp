#include "gre/data/virtual4c_source.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "gre/core/error.hpp"

namespace gre {

Virtual4CSource::Virtual4CSource(MatrixSourcePtr matrix, GenomicRegion viewpoint)
    : matrix_(std::move(matrix)), viewpoint_(std::move(viewpoint)) {
    if (matrix_ == nullptr) {
        throw Error(ErrorCode::invalid_argument, "Virtual4CSource needs a matrix source");
    }
    if (viewpoint_.empty()) {
        throw Error(ErrorCode::invalid_argument, "virtual 4C viewpoint must be non-empty");
    }
}

std::shared_ptr<Virtual4CSource> Virtual4CSource::make(MatrixSourcePtr matrix,
                                                       GenomicRegion viewpoint) {
    return std::make_shared<Virtual4CSource>(std::move(matrix), std::move(viewpoint));
}

Virtual4CSource& Virtual4CSource::aggregation(Aggregation value) {
    aggregation_ = value;
    return *this;
}

SignalData Virtual4CSource::query(const GenomicRegion& region, std::size_t target_bins) {
    SignalData out;
    out.region = region;
    if (region.empty()) return out;

    const std::size_t width = std::max<std::size_t>(1, target_bins);
    const double relative = static_cast<double>(viewpoint_.span()) /
                            static_cast<double>(std::max<std::int64_t>(region.span(), 1));
    const std::size_t height = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::ceil(relative * width)), 1, 4096);
    const MatrixData matrix =
        matrix_->query(MatrixRegion::of(region, viewpoint_), width, height);
    if (matrix.empty()) return out;

    out.region = matrix.region.x();
    out.bin_size = static_cast<double>(out.region.span()) /
                   static_cast<double>(std::max<std::size_t>(matrix.width, 1));
    out.values.assign(matrix.width, std::numeric_limits<double>::quiet_NaN());

    for (std::size_t column = 0; column < matrix.width; ++column) {
        double total = 0.0;
        double maximum = -std::numeric_limits<double>::infinity();
        std::size_t count = 0;
        for (std::size_t row = 0; row < matrix.height; ++row) {
            const double value = matrix.at(row, column);
            if (!std::isfinite(value)) continue;
            total += value;
            maximum = std::max(maximum, value);
            ++count;
        }
        if (count == 0) continue;
        switch (aggregation_) {
            case Aggregation::mean:
                out.values[column] = total / static_cast<double>(count);
                break;
            case Aggregation::sum:
                out.values[column] = total;
                break;
            case Aggregation::maximum:
                out.values[column] = maximum;
                break;
        }
    }
    return out;
}

}  // namespace gre
