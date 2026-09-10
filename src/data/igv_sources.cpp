#include "gre/data/igv_sources.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

#include "gre/core/error.hpp"
#include "igv/igv.hpp"

namespace gre {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

std::string lower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Annotation files and .hic files disagree about the "chr" prefix often
// enough that every query tries both spellings.
std::vector<std::string> contig_spellings(const std::string& name) {
    std::vector<std::string> names{name};
    if (lower(name).rfind("chr", 0) == 0) {
        names.push_back(name.substr(3));
    } else {
        names.push_back("chr" + name);
    }
    return names;
}

Strand convert_strand(igv::Strand strand) {
    switch (strand) {
        case igv::Strand::forward:
            return Strand::forward;
        case igv::Strand::reverse:
            return Strand::reverse;
        case igv::Strand::unknown:
            break;
    }
    return Strand::unknown;
}

// BED itemRgb is "r,g,b"; some writers emit a hex colour instead.
std::optional<Color> parse_color(const std::string& text) {
    if (text.empty() || text == "0" || text == ".") return std::nullopt;
    Color parsed;
    if (try_color_from_hex(text, parsed)) return parsed;
    int channels[3] = {0, 0, 0};
    int index = 0;
    std::size_t position = 0;
    while (index < 3 && position <= text.size()) {
        const std::size_t comma = text.find(',', position);
        const std::string part =
            text.substr(position, comma == std::string::npos ? std::string::npos : comma - position);
        if (part.empty()) return std::nullopt;
        try {
            channels[index] = std::clamp(std::stoi(part), 0, 255);
        } catch (const std::exception&) {
            return std::nullopt;
        }
        ++index;
        if (comma == std::string::npos) break;
        position = comma + 1;
    }
    if (index != 3) return std::nullopt;
    return rgb(channels[0], channels[1], channels[2]);
}

}  // namespace

Feature feature_from_igv(const igv::Feature& feature) {
    Feature out;
    out.name = feature.name;
    out.chrom = feature.interval.contig;
    out.start = feature.interval.start;
    out.end = feature.interval.end;
    out.strand = convert_strand(feature.strand);
    out.score = feature.score;
    if (feature.color.has_value()) out.color = parse_color(*feature.color);

    out.exons.reserve(feature.blocks.size());
    for (const igv::GenomicInterval& block : feature.blocks) {
        out.exons.push_back(Exon{block.start, block.end});
    }
    if (feature.thick_start.has_value() && feature.thick_end.has_value()) {
        out.thick_start = *feature.thick_start;
        out.thick_end = *feature.thick_end;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Signal
// ---------------------------------------------------------------------------

struct IgvSignalSource::Impl {
    std::string path;
    std::unique_ptr<igv::SignalReader> reader;
    Aggregation aggregation{Aggregation::mean};
    bool fill_empty{false};
    double empty_value{0.0};
};

IgvSignalSource::IgvSignalSource(std::string path) : impl_(std::make_unique<Impl>()) {
    impl_->path = std::move(path);
    try {
        igv::AnyReader opened = igv::open_reader(igv::Resource{impl_->path});
        auto* signal = std::get_if<std::unique_ptr<igv::SignalReader>>(&opened);
        if (signal == nullptr) {
            throw Error(ErrorCode::invalid_argument,
                        "'" + impl_->path + "' is not a signal file (expected bigWig/bedGraph/wig)");
        }
        impl_->reader = std::move(*signal);
    } catch (const igv::Error& error) {
        throw Error(ErrorCode::io, "cannot open '" + impl_->path + "': " + error.what());
    }
}

IgvSignalSource::~IgvSignalSource() = default;

std::shared_ptr<IgvSignalSource> IgvSignalSource::open(std::string path) {
    return std::make_shared<IgvSignalSource>(std::move(path));
}

IgvSignalSource& IgvSignalSource::aggregation(Aggregation value) {
    impl_->aggregation = value;
    return *this;
}

IgvSignalSource& IgvSignalSource::fill_empty(double value) {
    impl_->fill_empty = true;
    impl_->empty_value = value;
    return *this;
}

const std::string& IgvSignalSource::path() const noexcept { return impl_->path; }

SignalData IgvSignalSource::query(const GenomicRegion& region, std::size_t target_bins) {
    SignalData out;
    out.region = region;
    const std::size_t bins = std::max<std::size_t>(1, target_bins);
    out.bin_size = static_cast<double>(region.span()) / static_cast<double>(bins);
    out.values.assign(bins, impl_->fill_empty ? impl_->empty_value : kNaN);
    if (out.bin_size <= 0.0) return out;

    std::vector<igv::SignalValue> records;
    for (const std::string& contig : contig_spellings(region.chrom)) {
        try {
            records = impl_->reader->get(igv::GenomicInterval{contig, region.start, region.end});
        } catch (const igv::Error&) {
            records.clear();
        }
        if (!records.empty()) break;
    }
    if (records.empty()) return out;

    std::vector<double> accumulator(bins, 0.0);
    std::vector<double> weights(bins, 0.0);

    for (const igv::SignalValue& record : records) {
        if (!std::isfinite(record.value)) continue;
        const double start = std::max<double>(record.interval.start, region.start);
        const double end = std::min<double>(record.interval.end, region.end);
        if (end <= start) continue;
        auto first = static_cast<std::ptrdiff_t>((start - region.start) / out.bin_size);
        auto last = static_cast<std::ptrdiff_t>((end - region.start) / out.bin_size);
        first = std::clamp<std::ptrdiff_t>(first, 0, static_cast<std::ptrdiff_t>(bins) - 1);
        last = std::clamp<std::ptrdiff_t>(last, 0, static_cast<std::ptrdiff_t>(bins) - 1);

        for (std::ptrdiff_t b = first; b <= last; ++b) {
            const auto index = static_cast<std::size_t>(b);
            const double bin_start = region.start + static_cast<double>(b) * out.bin_size;
            const double overlap =
                std::min(end, bin_start + out.bin_size) - std::max(start, bin_start);
            if (overlap <= 0.0) continue;
            switch (impl_->aggregation) {
                case Aggregation::mean:
                    accumulator[index] += record.value * overlap;
                    weights[index] += overlap;
                    break;
                case Aggregation::sum:
                    accumulator[index] += record.value * overlap / out.bin_size;
                    weights[index] += overlap;
                    break;
                case Aggregation::max:
                    accumulator[index] = weights[index] > 0.0
                                             ? std::max(accumulator[index], record.value)
                                             : record.value;
                    weights[index] += overlap;
                    break;
                case Aggregation::min:
                    accumulator[index] = weights[index] > 0.0
                                             ? std::min(accumulator[index], record.value)
                                             : record.value;
                    weights[index] += overlap;
                    break;
            }
        }
    }

    for (std::size_t i = 0; i < bins; ++i) {
        if (weights[i] <= 0.0) continue;
        out.values[i] = impl_->aggregation == Aggregation::mean
                            ? accumulator[i] / weights[i]
                            : accumulator[i];
    }
    return out;
}

// ---------------------------------------------------------------------------
// Features
// ---------------------------------------------------------------------------

struct IgvFeatureSource::Impl {
    std::string path;
    std::unique_ptr<igv::FeatureReader> reader;
    std::string type_filter;
    std::size_t max_features{0};
};

IgvFeatureSource::IgvFeatureSource(std::string path) : impl_(std::make_unique<Impl>()) {
    impl_->path = std::move(path);
    try {
        igv::AnyReader opened = igv::open_reader(igv::Resource{impl_->path});
        auto* features = std::get_if<std::unique_ptr<igv::FeatureReader>>(&opened);
        if (features == nullptr) {
            throw Error(ErrorCode::invalid_argument,
                        "'" + impl_->path + "' is not a feature file (expected BED/GFF/bigBed)");
        }
        impl_->reader = std::move(*features);
    } catch (const igv::Error& error) {
        throw Error(ErrorCode::io, "cannot open '" + impl_->path + "': " + error.what());
    }
}

IgvFeatureSource::~IgvFeatureSource() = default;

std::shared_ptr<IgvFeatureSource> IgvFeatureSource::open(std::string path) {
    return std::make_shared<IgvFeatureSource>(std::move(path));
}

IgvFeatureSource& IgvFeatureSource::feature_type(std::string type) {
    impl_->type_filter = std::move(type);
    return *this;
}

IgvFeatureSource& IgvFeatureSource::max_features(std::size_t limit) {
    impl_->max_features = limit;
    return *this;
}

const std::string& IgvFeatureSource::path() const noexcept { return impl_->path; }

std::vector<Feature> IgvFeatureSource::query(const GenomicRegion& region) {
    std::vector<igv::Feature> records;
    for (const std::string& contig : contig_spellings(region.chrom)) {
        try {
            records = impl_->reader->get(igv::GenomicInterval{contig, region.start, region.end});
        } catch (const igv::Error&) {
            records.clear();
        }
        if (!records.empty()) break;
    }

    std::vector<Feature> out;
    out.reserve(records.size());
    for (const igv::Feature& record : records) {
        if (!impl_->type_filter.empty() && record.type != impl_->type_filter) continue;
        out.push_back(feature_from_igv(record));
        if (impl_->max_features > 0 && out.size() >= impl_->max_features) break;
    }
    return out;
}

}  // namespace gre
