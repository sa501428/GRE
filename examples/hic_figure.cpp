// A figure built from real files, through the straw and igv-cpp adapters.
//
//   gre_example_hic <file.hic> [region] [signal.bigWig ...] [genes.bed]
//
// `region` is chr:start-end; with no region the example picks a window in the
// middle of the first contig.  Extra arguments are matched by extension:
// .bigWig/.bw/.bedgraph/.wig become signal tracks, .bed/.gff/.gtf/.bb become a
// gene track.  With no arguments at all it prints what the .hic file contains.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "gre/gre.hpp"

namespace {

using namespace gre;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Accepts chr:start-end with optional commas or underscores in the numbers.
bool parse_region(const std::string& text, GenomicRegion& out) {
    const std::size_t colon = text.rfind(':');
    if (colon == std::string::npos) return false;
    const std::size_t dash = text.find('-', colon);
    if (dash == std::string::npos) return false;

    const auto to_number = [](std::string value) -> std::int64_t {
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

void describe(const StrawMatrixSource& source) {
    std::printf("genome: %s   version: %d\n", source.genome().c_str(), source.version());
    std::printf("resolutions (bp):");
    for (std::int32_t resolution : source.resolutions()) std::printf(" %d", resolution);
    std::printf("\nnormalizations:");
    for (const std::string& norm : source.normalizations()) std::printf(" %s", norm.c_str());
    std::printf("\ncontigs (%zu):", source.contigs().size());
    std::size_t shown = 0;
    for (const HicContig& contig : source.contigs()) {
        if (shown++ >= 8) {
            std::printf(" ...");
            break;
        }
        std::printf(" %s(%lld)", contig.name.c_str(),
                    static_cast<long long>(contig.length));
    }
    std::printf("\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: %s <file.hic> [chr:start-end] [signal.bigWig ...] [genes.bed]\n",
                     argv[0]);
        return 2;
    }

    try {
        StrawOptions options;
        options.normalization = "NONE";
        auto matrix = StrawMatrixSource::open(argv[1], options);
        describe(*matrix);

        GenomicRegion region;
        std::vector<std::string> signal_paths;
        std::string feature_path;

        for (int i = 2; i < argc; ++i) {
            const std::string argument = argv[i];
            const std::string extension = lower(argument);
            if (parse_region(argument, region)) continue;
            if (ends_with(extension, ".bigwig") || ends_with(extension, ".bw") ||
                ends_with(extension, ".bedgraph") || ends_with(extension, ".wig")) {
                signal_paths.push_back(argument);
            } else if (ends_with(extension, ".bed") || ends_with(extension, ".bed.gz") ||
                       ends_with(extension, ".gff") || ends_with(extension, ".gff3") ||
                       ends_with(extension, ".gtf") || ends_with(extension, ".bb") ||
                       ends_with(extension, ".bigbed")) {
                feature_path = argument;
            } else {
                std::fprintf(stderr, "ignoring unrecognised argument: %s\n", argument.c_str());
            }
        }

        if (region.chrom.empty()) {
            if (matrix->contigs().empty()) {
                std::fprintf(stderr, "the file has no contigs\n");
                return 1;
            }
            // A window in the middle of the first contig, sized so the finest
            // stored resolution gives a few hundred bins.
            const HicContig& contig = matrix->contigs().front();
            const std::int64_t finest = matrix->resolutions().front();
            const std::int64_t span =
                std::min<std::int64_t>(contig.length, std::max<std::int64_t>(finest * 400, 1));
            const std::int64_t start = std::max<std::int64_t>(0, contig.length / 2 - span / 2);
            region = GenomicRegion{contig.name, start, std::min(start + span, contig.length)};
        }
        std::printf("region: %s\n",
                    format_region(region.chrom, region.start, region.end).c_str());

        Figure figure;
        figure.set_width(inches(7.0))
            .set_theme(Theme::publication())
            .set_title(format_region(region.chrom, region.start, region.end))
            .set_subtitle(std::string(argv[1]));

        Panel& panel = figure.add_panel();
        panel.set_region(region).set_show_grid(true);
        panel.add_track(AxisTrack{});

        for (const std::string& path : signal_paths) {
            auto source = IgvSignalSource::open(path);
            source->fill_empty(0.0);
            // Trim the directory for the track label.
            const std::size_t slash = path.find_last_of("/\\");
            panel.add_track(SignalTrack{source}
                                .name(slash == std::string::npos ? path : path.substr(slash + 1))
                                .height(38.0));
        }

        if (!feature_path.empty()) {
            panel.add_track(GeneTrack{IgvFeatureSource::open(feature_path)}.name("Genes"));
        }

        HeatmapTrack& heatmap = panel.add_track(HeatmapTrack{matrix}
                                                    .name("Hi-C")
                                                    .mode(HeatmapMode::square)
                                                    .log_scale(true)
                                                    .colors("fall")
                                                    .border(rgb(150, 150, 150)));
        panel.add_track(ColorBarTrack{heatmap}.title("contacts (log)").align(BarAlign::right));

        figure.save_png("hic_figure.png", 300.0);
        figure.save_pdf("hic_figure.pdf");
        std::printf("resolution used: %d bp\n", matrix->last_resolution());
        std::printf("wrote hic_figure.png and hic_figure.pdf\n");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
    return 0;
}
