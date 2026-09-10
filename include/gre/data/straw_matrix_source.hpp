#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gre/data/matrix_source.hpp"

namespace gre {

struct HicContig {
    std::string name;
    std::int64_t length{};
    int index{};
};

struct StrawOptions {
    // NONE, VC, VC_SQRT, KR, SCALE -- whatever the file carries.
    std::string normalization{"NONE"};
    // observed, oe, expected.
    std::string matrix_type{"observed"};
    std::string unit{"BP"};
    // 0 asks the source to pick the coarsest resolution that still fills the
    // requested pixel width.
    std::int32_t resolution{0};
};

// Hi-C contact matrices, read through straw.  The straw headers are not
// exposed here: this translation unit is the only place that knows about them.
class StrawMatrixSource : public MatrixSource {
public:
    explicit StrawMatrixSource(std::string path, StrawOptions options = {});
    ~StrawMatrixSource() override;

    StrawMatrixSource(const StrawMatrixSource&) = delete;
    StrawMatrixSource& operator=(const StrawMatrixSource&) = delete;

    [[nodiscard]] static std::shared_ptr<StrawMatrixSource> open(std::string path,
                                                                 StrawOptions options = {});

    MatrixData query(const MatrixRegion& region, std::size_t target_width,
                     std::size_t target_height) override;

    [[nodiscard]] const std::vector<HicContig>& contigs() const;
    [[nodiscard]] const std::vector<std::int32_t>& resolutions() const;
    [[nodiscard]] const std::vector<std::string>& normalizations() const;
    [[nodiscard]] const std::string& genome() const;
    [[nodiscard]] int version() const;

    // Matches "chr1" against "1" and vice versa, case-insensitively.  Returns
    // an empty string when the file has no such contig.
    [[nodiscard]] std::string resolve_contig(std::string_view name) const;
    [[nodiscard]] std::int64_t contig_length(std::string_view name) const;

    // Resolution used by the most recent query, in base pairs.
    [[nodiscard]] std::int32_t last_resolution() const;

    // Convenience: the whole of one chromosome.
    [[nodiscard]] MatrixRegion whole_contig(std::string_view name) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gre
