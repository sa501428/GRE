#include "gre/data/straw_matrix_source.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

#include "gre/core/error.hpp"
#include "straw.h"

namespace gre {
namespace {

// Never let one query allocate more than this many cells per axis, however
// coarse the file's stored resolutions are relative to the request.
constexpr std::size_t kOversample = 2;
constexpr std::size_t kMaxCells = 8192;

std::string lower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string strip_chr(std::string_view name) {
    const std::string lowered = lower(name);
    if (lowered.rfind("chr", 0) == 0) return std::string(name.substr(3));
    return std::string(name);
}

std::int64_t floor_div(std::int64_t value, std::int64_t divisor) {
    const std::int64_t quotient = value / divisor;
    return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

std::int64_t align_down(std::int64_t value, std::int64_t step) {
    return floor_div(value, step) * step;
}

std::int64_t align_up(std::int64_t value, std::int64_t step) {
    return -align_down(-value, step);
}

}  // namespace

struct StrawMatrixSource::Impl {
    std::string path;
    StrawOptions options;
    std::string genome;
    int version{0};
    std::vector<HicContig> contigs;
    std::vector<std::int32_t> resolutions;
    std::vector<std::string> normalizations;
    std::int32_t last_resolution{0};

    // Largest stored resolution that still gives at least `target` bins across
    // `span`; the finest available when none qualifies.
    [[nodiscard]] std::int32_t choose_resolution(std::int64_t span, std::size_t target) const {
        if (options.resolution > 0) return options.resolution;
        if (resolutions.empty()) {
            throw Error(ErrorCode::not_found, "no resolutions in " + path);
        }
        const double desired =
            static_cast<double>(span) / static_cast<double>(std::max<std::size_t>(target, 1));
        std::int32_t best = *std::min_element(resolutions.begin(), resolutions.end());
        for (std::int32_t resolution : resolutions) {
            if (static_cast<double>(resolution) <= desired && resolution > best) best = resolution;
        }
        return best;
    }
};

StrawMatrixSource::StrawMatrixSource(std::string path, StrawOptions options)
    : impl_(std::make_unique<Impl>()) {
    impl_->path = std::move(path);
    impl_->options = std::move(options);
    try {
        // One open for everything: over HTTP each accessor would otherwise
        // cost its own round trips.
        const StrawFileInfo info = getFileInfo(impl_->path);
        impl_->genome = info.genome;
        impl_->version = info.version;
        impl_->normalizations = info.normalizations;
        impl_->resolutions =
            impl_->options.unit == "FRAG" ? info.fragResolutions : info.bpResolutions;
        impl_->contigs.reserve(info.chromosomes.size());
        for (const chromosome& contig : info.chromosomes) {
            if (lower(contig.name) == "all") continue;
            impl_->contigs.push_back(HicContig{contig.name, contig.length, contig.index});
        }
        std::sort(impl_->resolutions.begin(), impl_->resolutions.end());
    } catch (const std::exception& error) {
        throw Error(ErrorCode::io,
                    "cannot read Hi-C file '" + impl_->path + "': " + error.what());
    }
    if (impl_->resolutions.empty()) {
        throw Error(ErrorCode::not_found,
                    "no " + impl_->options.unit + " resolutions in " + impl_->path);
    }
}

StrawMatrixSource::~StrawMatrixSource() = default;

std::shared_ptr<StrawMatrixSource> StrawMatrixSource::open(std::string path,
                                                           StrawOptions options) {
    return std::make_shared<StrawMatrixSource>(std::move(path), std::move(options));
}

const std::vector<HicContig>& StrawMatrixSource::contigs() const { return impl_->contigs; }
const std::vector<std::int32_t>& StrawMatrixSource::resolutions() const {
    return impl_->resolutions;
}
const std::vector<std::string>& StrawMatrixSource::normalizations() const {
    return impl_->normalizations;
}
const std::string& StrawMatrixSource::genome() const { return impl_->genome; }
int StrawMatrixSource::version() const { return impl_->version; }
std::int32_t StrawMatrixSource::last_resolution() const { return impl_->last_resolution; }

std::string StrawMatrixSource::resolve_contig(std::string_view name) const {
    for (const HicContig& contig : impl_->contigs) {
        if (contig.name == name) return contig.name;
    }
    // .hic files are inconsistent about the "chr" prefix; BED and bigWig files
    // just as much, so match on the bare name.
    const std::string wanted = lower(strip_chr(name));
    for (const HicContig& contig : impl_->contigs) {
        if (lower(strip_chr(contig.name)) == wanted) return contig.name;
    }
    return {};
}

std::int64_t StrawMatrixSource::contig_length(std::string_view name) const {
    const std::string resolved = resolve_contig(name);
    for (const HicContig& contig : impl_->contigs) {
        if (contig.name == resolved) return contig.length;
    }
    return 0;
}

MatrixRegion StrawMatrixSource::whole_contig(std::string_view name) const {
    const std::string resolved = resolve_contig(name);
    const std::int64_t length = contig_length(name);
    return MatrixRegion{resolved, 0, length, resolved, 0, length};
}

MatrixData StrawMatrixSource::query(const MatrixRegion& region, std::size_t target_width,
                                    std::size_t target_height) {
    const std::string chrom_x = resolve_contig(region.chrom_x);
    const std::string chrom_y = resolve_contig(region.chrom_y);
    if (chrom_x.empty() || chrom_y.empty()) {
        throw Error(ErrorCode::not_found, "chromosome '" +
                                              (chrom_x.empty() ? region.chrom_x : region.chrom_y) +
                                              "' is not in " + impl_->path);
    }

    const std::int64_t x_span = std::max<std::int64_t>(region.x_end - region.x_start, 1);
    const std::int64_t y_span = std::max<std::int64_t>(region.y_end - region.y_start, 1);
    const std::int32_t resolution =
        impl_->choose_resolution(std::max(x_span, y_span), std::max(target_width, target_height));
    if (resolution <= 0) throw Error(ErrorCode::internal, "invalid Hi-C resolution");
    impl_->last_resolution = resolution;

    // Aggregate whole numbers of stored bins so the output stays bin-aligned.
    const std::size_t cap_x = std::min(kMaxCells, std::max<std::size_t>(target_width, 1) * kOversample);
    const std::size_t cap_y = std::min(kMaxCells, std::max<std::size_t>(target_height, 1) * kOversample);
    const auto natural_x = static_cast<std::size_t>((x_span + resolution - 1) / resolution);
    const auto natural_y = static_cast<std::size_t>((y_span + resolution - 1) / resolution);
    std::int64_t factor = 1;
    if (natural_x > cap_x) factor = std::max<std::int64_t>(factor, static_cast<std::int64_t>((natural_x + cap_x - 1) / cap_x));
    if (natural_y > cap_y) factor = std::max<std::int64_t>(factor, static_cast<std::int64_t>((natural_y + cap_y - 1) / cap_y));
    const std::int64_t bin = static_cast<std::int64_t>(resolution) * factor;

    const std::int64_t x_start = align_down(region.x_start, bin);
    const std::int64_t x_end = std::max(align_up(region.x_end, bin), x_start + bin);
    const std::int64_t y_start = align_down(region.y_start, bin);
    const std::int64_t y_end = std::max(align_up(region.y_end, bin), y_start + bin);

    MatrixData out;
    out.bin_size = bin;
    out.region = MatrixRegion{chrom_x, x_start, x_end, chrom_y, y_start, y_end};
    const auto width = static_cast<std::size_t>((x_end - x_start) / bin);
    const auto height = static_cast<std::size_t>((y_end - y_start) / bin);
    out.resize(width, height, 0.0F);

    // "observed" counts add up when bins are merged; ratios average instead.
    const bool averaging =
        impl_->options.matrix_type == "oe" || impl_->options.matrix_type == "expected";
    std::vector<double> sums;
    std::vector<double> counts;
    if (averaging) {
        sums.assign(width * height, 0.0);
        counts.assign(width * height, 0.0);
    }

    const auto location = [](const std::string& chrom, std::int64_t start, std::int64_t end) {
        return chrom + ":" + std::to_string(start) + ":" + std::to_string(end);
    };
    const bool intra = chrom_x == chrom_y;

    const auto deposit = [&](std::int64_t row, std::int64_t column, float value) {
        if (row < 0 || column < 0 || static_cast<std::size_t>(row) >= height ||
            static_cast<std::size_t>(column) >= width) {
            return;
        }
        const std::size_t index = static_cast<std::size_t>(row) * width +
                                  static_cast<std::size_t>(column);
        if (averaging) {
            sums[index] += value;
            counts[index] += 1.0;
        } else {
            out.values[index] += value;
        }
    };

    try {
        strawStream(impl_->options.matrix_type, impl_->options.normalization, impl_->path,
                    location(chrom_x, x_start, x_end - 1), location(chrom_y, y_start, y_end - 1),
                    impl_->options.unit, resolution, [&](const contactRecord& record) {
                        if (!std::isfinite(record.counts)) return;
                        // straw reports genomic coordinates, not bin indices.
                        const std::int64_t x = record.binX;
                        const std::int64_t y = record.binY;
                        const std::int64_t column = (x - x_start) / bin;
                        const std::int64_t row = (y - y_start) / bin;
                        deposit(row, column, record.counts);
                        if (intra) {
                            // Only one triangle is stored, so mirror unless the
                            // record is on the diagonal.
                            const std::int64_t mirror_column = (y - x_start) / bin;
                            const std::int64_t mirror_row = (x - y_start) / bin;
                            if (mirror_row != row || mirror_column != column) {
                                deposit(mirror_row, mirror_column, record.counts);
                            }
                        }
                    });
    } catch (const std::exception& error) {
        throw Error(ErrorCode::io, "Hi-C query failed for " + impl_->path + ": " + error.what());
    }

    if (averaging) {
        for (std::size_t i = 0; i < out.values.size(); ++i) {
            out.values[i] = counts[i] > 0.0 ? static_cast<float>(sums[i] / counts[i])
                                            : std::numeric_limits<float>::quiet_NaN();
        }
    }
    return out;
}

}  // namespace gre
