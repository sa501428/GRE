#pragma once

// Argument handling shared by the examples: a .hic file, an optional region,
// optional signal and annotation files, and a couple of switches.  Files are
// recognised by extension so the command line stays short.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "gre/gre.hpp"

namespace example {

struct Arguments {
    std::string hic_path;
    gre::GenomicRegion region;
    std::vector<std::string> signal_paths;
    std::vector<std::string> feature_paths;
    std::string normalization{"NONE"};
    std::string output{"figure"};
    double dpi{300.0};
    bool oe{false};
};

inline std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

inline bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::string basename(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// chr:start-end, tolerating commas and underscores in the numbers.
inline bool parse_region(const std::string& text, gre::GenomicRegion& out) {
    const std::size_t colon = text.rfind(':');
    if (colon == std::string::npos) return false;
    const std::size_t dash = text.find('-', colon);
    if (dash == std::string::npos) return false;
    const auto to_number = [](std::string value) {
        value.erase(std::remove(value.begin(), value.end(), ','), value.end());
        value.erase(std::remove(value.begin(), value.end(), '_'), value.end());
        return std::stoll(value);
    };
    try {
        out.chrom = text.substr(0, colon);
        out.start = to_number(text.substr(colon + 1, dash - colon - 1));
        out.end = to_number(text.substr(dash + 1));
    } catch (const std::exception&) {
        return false;
    }
    return out.end > out.start;
}

inline bool is_signal(const std::string& path) {
    const std::string extension = lower(path);
    return ends_with(extension, ".bigwig") || ends_with(extension, ".bw") ||
           ends_with(extension, ".bedgraph") || ends_with(extension, ".bg") ||
           ends_with(extension, ".wig");
}

inline bool is_features(const std::string& path) {
    const std::string extension = lower(path);
    return ends_with(extension, ".bed") || ends_with(extension, ".bed.gz") ||
           ends_with(extension, ".gff") || ends_with(extension, ".gff3") ||
           ends_with(extension, ".gtf") || ends_with(extension, ".bb") ||
           ends_with(extension, ".bigbed") || ends_with(extension, ".narrowpeak") ||
           ends_with(extension, ".broadpeak");
}

inline void print_usage(const char* program) {
    std::fprintf(stderr,
                 "usage: %s <file.hic> [chr:start-end] [file.bigWig ...] [genes.bed ...]\n"
                 "           [--norm NONE|VC|VC_SQRT|KR|SCALE] [--oe] [--out PREFIX]\n"
                 "           [--dpi N]\n",
                 program);
}

// Returns false when the arguments are unusable; the caller should exit.
inline bool parse(int argc, char** argv, Arguments& out) {
    if (argc < 2) {
        print_usage(argv[0]);
        return false;
    }
    out.hic_path = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        const auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", name);
                return {};
            }
            return argv[++i];
        };
        if (argument == "--norm") {
            out.normalization = next("--norm");
        } else if (argument == "--out") {
            out.output = next("--out");
        } else if (argument == "--dpi") {
            out.dpi = std::stod(next("--dpi"));
        } else if (argument == "--oe") {
            out.oe = true;
        } else if (parse_region(argument, out.region)) {
            continue;
        } else if (is_signal(argument)) {
            out.signal_paths.push_back(argument);
        } else if (is_features(argument)) {
            out.feature_paths.push_back(argument);
        } else {
            std::fprintf(stderr, "unrecognised argument: %s\n", argument.c_str());
            print_usage(argv[0]);
            return false;
        }
    }
    return true;
}

inline void describe(const gre::StrawMatrixSource& source) {
    std::printf("genome %s, version %d\n", source.genome().c_str(), source.version());
    std::printf("resolutions:");
    for (std::int32_t resolution : source.resolutions()) std::printf(" %d", resolution);
    std::printf("\nnormalizations:");
    for (const std::string& norm : source.normalizations()) std::printf(" %s", norm.c_str());
    std::printf("\ncontigs (%zu):", source.contigs().size());
    std::size_t shown = 0;
    for (const gre::HicContig& contig : source.contigs()) {
        if (shown++ >= 10) {
            std::printf(" ...");
            break;
        }
        std::printf(" %s(%lld)", contig.name.c_str(), static_cast<long long>(contig.length));
    }
    std::printf("\n");
}

// A window in the middle of the first contig, wide enough that the finest
// stored resolution gives a few hundred bins.
inline gre::GenomicRegion default_region(const gre::StrawMatrixSource& source) {
    if (source.contigs().empty()) {
        throw gre::Error(gre::ErrorCode::not_found, "the .hic file lists no contigs");
    }
    const gre::HicContig& contig = source.contigs().front();
    const std::int64_t finest = source.resolutions().front();
    const std::int64_t span =
        std::min<std::int64_t>(contig.length, std::max<std::int64_t>(finest * 400, 1));
    const std::int64_t start = std::max<std::int64_t>(0, contig.length / 2 - span / 2);
    return gre::GenomicRegion{contig.name, start, std::min(start + span, contig.length)};
}

}  // namespace example
