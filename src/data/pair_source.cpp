#include "gre/data/pair_source.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string_view>

#include "gre/core/error.hpp"

namespace gre {
namespace {

bool overlaps(const GenomicRegion& feature, const GenomicRegion& view) {
    return (feature.chrom.empty() || view.chrom.empty() || feature.chrom == view.chrom) &&
           feature.end > view.start && feature.start < view.end;
}

bool relevant(const PairFeature& feature, const GenomicRegion& x_region,
              const GenomicRegion& y_region) {
    return (overlaps(feature.first, x_region) && overlaps(feature.second, y_region)) ||
           (overlaps(feature.second, x_region) && overlaps(feature.first, y_region));
}

std::vector<std::string> fields_of(const std::string& line) {
    std::istringstream input(line);
    std::vector<std::string> fields;
    for (std::string field; input >> field;) fields.push_back(std::move(field));
    return fields;
}

PairFeature parse_record(const std::vector<std::string>& fields, std::size_t line_number,
                         const std::string& path) {
    if (fields.size() < 6) {
        throw Error(ErrorCode::invalid_argument,
                    path + ":" + std::to_string(line_number) +
                        ": BEDPE record has fewer than six columns");
    }

    PairFeature feature;
    try {
        feature.first = GenomicRegion{fields[0], std::stoll(fields[1]), std::stoll(fields[2])};
        feature.second = GenomicRegion{fields[3], std::stoll(fields[4]), std::stoll(fields[5])};
        if (fields.size() > 6 && fields[6] != ".") feature.name = fields[6];
        if (fields.size() > 7 && fields[7] != ".") feature.score = std::stod(fields[7]);
    } catch (const std::exception&) {
        throw Error(ErrorCode::invalid_argument,
                    path + ":" + std::to_string(line_number) +
                        ": invalid BEDPE coordinate or score");
    }
    if (feature.first.start < 0 || feature.second.start < 0 || feature.first.empty() ||
        feature.second.empty()) {
        throw Error(ErrorCode::invalid_argument,
                    path + ":" + std::to_string(line_number) +
                        ": BEDPE intervals must be non-negative and non-empty");
    }

    // itemRgb is not part of the BEDPE core, but several producers append it.
    // Accept the first colour-looking optional field without assigning a fixed
    // producer-specific column number.
    for (std::size_t i = 8; i < fields.size(); ++i) {
        Color parsed;
        int red = 0;
        int green = 0;
        int blue = 0;
        const bool rgb_triplet =
            std::sscanf(fields[i].c_str(), "%d,%d,%d", &red, &green, &blue) == 3 &&
            red >= 0 && red <= 255 && green >= 0 && green <= 255 && blue >= 0 && blue <= 255;
        if (rgb_triplet) {
            feature.color = rgb(red, green, blue);
            break;
        }
        if (try_color_from_hex(fields[i], parsed)) {
            feature.color = parsed;
            break;
        }
    }
    return feature;
}

template <typename NextLine>
std::vector<PairFeature> read_records(NextLine next_line, const std::string& path) {
    std::vector<PairFeature> out;
    std::string line;
    std::size_t line_number = 0;
    while (next_line(line)) {
        ++line_number;
        const std::size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') continue;
        const std::string_view view(line.data() + first, line.size() - first);
        if (view.starts_with("track") || view.starts_with("browser")) continue;
        out.push_back(parse_record(fields_of(line), line_number, path));
    }
    return out;
}

bool gzip_path(const std::string& path) {
    return path.size() >= 3 && path.compare(path.size() - 3, 3, ".gz") == 0;
}

}  // namespace

BedpeSource::BedpeSource(std::string path) : path_(std::move(path)) {
    if (gzip_path(path_)) {
        gzFile file = gzopen(path_.c_str(), "rb");
        if (file == nullptr) throw Error(ErrorCode::io, "cannot open BEDPE file '" + path_ + "'");
        constexpr int kChunk = 8192;
        const auto next = [file](std::string& line) {
            line.clear();
            char buffer[kChunk];
            while (gzgets(file, buffer, kChunk) != nullptr) {
                line += buffer;
                if (!line.empty() && line.back() == '\n') return true;
            }
            return !line.empty();
        };
        try {
            features_ = read_records(next, path_);
        } catch (...) {
            gzclose(file);
            throw;
        }
        gzclose(file);
    } else {
        std::ifstream file(path_);
        if (!file) throw Error(ErrorCode::io, "cannot open BEDPE file '" + path_ + "'");
        const auto next = [&file](std::string& line) {
            return static_cast<bool>(std::getline(file, line));
        };
        features_ = read_records(next, path_);
    }
}

std::shared_ptr<BedpeSource> BedpeSource::open(std::string path) {
    return std::make_shared<BedpeSource>(std::move(path));
}

std::vector<PairFeature> BedpeSource::query(const GenomicRegion& x_region,
                                            const GenomicRegion& y_region) {
    std::vector<PairFeature> out;
    for (const PairFeature& feature : features_) {
        if (relevant(feature, x_region, y_region)) out.push_back(feature);
    }
    return out;
}

MemoryPairFeatureSource::MemoryPairFeatureSource(std::vector<PairFeature> features)
    : features_(std::move(features)) {}

std::shared_ptr<MemoryPairFeatureSource> MemoryPairFeatureSource::make(
    std::vector<PairFeature> features) {
    return std::make_shared<MemoryPairFeatureSource>(std::move(features));
}

std::vector<PairFeature> MemoryPairFeatureSource::query(const GenomicRegion& x_region,
                                                        const GenomicRegion& y_region) {
    std::vector<PairFeature> out;
    for (const PairFeature& feature : features_) {
        if (relevant(feature, x_region, y_region)) out.push_back(feature);
    }
    return out;
}

}  // namespace gre
