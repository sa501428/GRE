// Browser-style figure: 1D tracks over a rotated (pyramid) contact map.
//
//   gre_example_hic <file.hic> [chr:start-end] [file.bigWig ...] [genes.bed ...]
//                   [--norm KR] [--oe] [--out PREFIX] [--dpi N]
//
// With no region it picks a window in the middle of the first contig, and
// prints what the .hic file contains either way.

#include <cstdio>

#include "example_args.hpp"

using namespace gre;

int main(int argc, char** argv) {
    example::Arguments arguments;
    if (!example::parse(argc, argv, arguments)) return 2;

    try {
        StrawOptions options;
        options.normalization = arguments.normalization;
        options.matrix_type = arguments.oe ? "oe" : "observed";
        auto matrix = StrawMatrixSource::open(arguments.hic_path, options);
        example::describe(*matrix);

        if (arguments.region.chrom.empty()) {
            arguments.region = example::default_region(*matrix);
        }
        const GenomicRegion& region = arguments.region;
        std::printf("region: %s\n",
                    format_region(region.chrom, region.start, region.end).c_str());

        Figure figure;
        figure.set_width(inches(7.0))
            .set_theme(Theme::publication())
            .set_title(format_region(region.chrom, region.start, region.end))
            .set_subtitle(example::basename(arguments.hic_path) + "   " +
                          options.matrix_type + " / " + options.normalization);

        Panel& panel = figure.add_panel();
        panel.set_region(region).set_show_grid(true);
        panel.add_track(AxisTrack{}.show_region(true));

        for (const std::string& path : arguments.signal_paths) {
            auto source = IgvSignalSource::open(path);
            source->fill_empty(0.0);
            panel.add_track(
                SignalTrack{source}.name(example::basename(path)).height(34.0));
        }
        for (const std::string& path : arguments.feature_paths) {
            panel.add_track(
                GeneTrack{IgvFeatureSource::open(path)}.name(example::basename(path)));
        }

        // The rotated view puts the diagonal along the top, so contact distance
        // reads downwards and lines up with the tracks above.
        HeatmapTrack& heatmap = panel.add_track(HeatmapTrack{matrix}
                                                    .name("Hi-C")
                                                    .mode(HeatmapMode::triangle)
                                                    .log_scale(!arguments.oe)
                                                    .colors(arguments.oe ? "rd_bu" : "fall"));
        if (arguments.oe) heatmap.scale(ValueScale{0.25, 4.0, ScaleType::log});

        panel.add_bottom_track(ColorBarTrack{heatmap}
                                   .title(arguments.oe ? "obs/exp" : "contacts (log)")
                                   .align(BarAlign::right));

        figure.save_png(arguments.output + ".png", arguments.dpi);
        figure.save_pdf(arguments.output + ".pdf");

        const Size size = figure.computed_size();
        std::printf("resolution used: %d bp\n", matrix->last_resolution());
        example::report_matrix(heatmap);
        std::printf("wrote %s.{png,pdf}  (%.0f x %.0f pt)\n", arguments.output.c_str(),
                    size.width, size.height);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
    return 0;
}
