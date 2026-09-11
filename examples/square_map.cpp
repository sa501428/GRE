// Juicebox-style square contact map with IGV-style tracks along both axes.
//
//   gre_example_square <file.hic> [chr:start-end] [file.bigWig ...] [genes.bed]
//                      [--norm KR] [--oe] [--out PREFIX] [--dpi N]
//
// The same 1D sources feed the horizontal tracks above the map and the
// quarter-turned tracks down its left side, so both axes are annotated from
// one set of files.

#include <cstdio>
#include <memory>
#include <vector>

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

        // One reader per file, shared by the horizontal and vertical track that
        // display it.
        std::vector<SignalSourcePtr> signals;
        for (const std::string& path : arguments.signal_paths) {
            auto source = IgvSignalSource::open(path);
            source->fill_empty(0.0);
            signals.push_back(source);
        }
        std::vector<FeatureSourcePtr> features;
        for (const std::string& path : arguments.feature_paths) {
            features.push_back(IgvFeatureSource::open(path));
        }

        Figure figure;
        figure.set_width(inches(7.5))
            .set_theme(Theme::publication())
            .set_title(format_region(region.chrom, region.start, region.end))
            .set_subtitle(example::basename(arguments.hic_path) + "   " +
                          options.matrix_type + " / " + options.normalization);

        Panel& panel = figure.add_panel();
        panel.set_region(region).set_label_width(66.0);

        // ---- horizontal tracks, above the map ----------------------------
        panel.add_track(AxisTrack{});
        for (std::size_t i = 0; i < signals.size(); ++i) {
            panel.add_track(SignalTrack{signals[i]}
                                .name(example::basename(arguments.signal_paths[i]))
                                .height(30.0));
        }
        for (std::size_t i = 0; i < features.size(); ++i) {
            panel.add_track(GeneTrack{features[i]}
                                .name(example::basename(arguments.feature_paths[i])));
        }

        // ---- vertical tracks, down the map's left side -------------------
        //
        // In a y track, height() is the column width.  They are listed in the
        // same order as the horizontal ones, and laid out from the outside in,
        // so the two axes read as mirror images.
        panel.add_y_track(AxisTrack{}.position(AxisPosition::bottom).height(26.0));
        for (std::size_t i = 0; i < signals.size(); ++i) {
            panel.add_y_track(SignalTrack{signals[i]}
                                  .name(example::basename(arguments.signal_paths[i]))
                                  .height(30.0)
                                  .show_range_label(false));
        }
        for (std::size_t i = 0; i < features.size(); ++i) {
            panel.add_y_track(GeneTrack{features[i]}
                                  .name(example::basename(arguments.feature_paths[i]))
                                  .height(34.0)
                                  .show_labels(false));
        }

        // ---- the map itself ----------------------------------------------
        HeatmapTrack& map = panel.set_matrix(HeatmapTrack{matrix}
                                                 .name("Hi-C")
                                                 .mode(HeatmapMode::square)
                                                 .log_scale(!arguments.oe)
                                                 .colors(arguments.oe ? "rd_bu" : "fall")
                                                 .show_diagonal(false)
                                                 .border(rgb(140, 140, 140)));
        if (arguments.oe) {
            // Observed/expected is a ratio: centre the diverging map on 1.
            map.scale(ValueScale{0.25, 4.0, ScaleType::log}).colors("rd_bu");
        }

        // Below the map: the colour bar, and a second coordinate rule.
        panel.add_bottom_track(AxisTrack{}.position(AxisPosition::bottom));
        panel.add_bottom_track(ColorBarTrack{map}
                                   .title(arguments.oe ? "obs/exp" : "contacts (log)")
                                   .align(BarAlign::right)
                                   .bar_length(110.0));

        figure.save_png(arguments.output + ".png", arguments.dpi);
        figure.save_pdf(arguments.output + ".pdf");

        const Size size = figure.computed_size();
        std::printf("resolution used: %d bp\n", matrix->last_resolution());
        example::report_matrix(map);
        std::printf("wrote %s.{png,pdf}  (%.0f x %.0f pt)\n", arguments.output.c_str(),
                    size.width, size.height);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
    return 0;
}
