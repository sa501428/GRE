// Exercises the pieces added in the polish phase: multiple panels, shared
// styling through themes, every signal style, colour bars and legends.
//
//   gre_example_gallery [output-prefix]

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "gre/gre.hpp"

namespace {

using namespace gre;

constexpr const char* kChrom = "chr1";
constexpr std::int64_t kStart = 1'000'000;
constexpr std::int64_t kEnd = 3'000'000;

std::shared_ptr<MemorySignalSource> wave(unsigned seed, double amplitude, double period,
                                         bool signed_values) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> jitter(0.0, amplitude * 0.08);
    constexpr std::int64_t kStep = 2'000;
    std::vector<MemorySignalSource::Interval> intervals;
    for (std::int64_t x = kStart; x < kEnd; x += kStep) {
        const double phase = 2.0 * 3.14159265358979 * static_cast<double>(x - kStart) / period;
        double value = amplitude * std::sin(phase) + jitter(rng);
        if (!signed_values) value = std::fabs(value);
        intervals.push_back({x, x + kStep, value});
    }
    return MemorySignalSource::make(kChrom, std::move(intervals));
}

std::shared_ptr<MemoryMatrixSource> checkerboard(std::int64_t bin) {
    const auto bins = static_cast<std::size_t>((kEnd - kStart) / bin);
    std::vector<float> values(bins * bins);
    for (std::size_t i = 0; i < bins; ++i) {
        for (std::size_t j = 0; j < bins; ++j) {
            const auto distance = static_cast<double>(i > j ? i - j : j - i);
            const double compartment =
                std::sin(static_cast<double>(i) / 9.0) * std::sin(static_cast<double>(j) / 9.0);
            values[i * bins + j] =
                static_cast<float>((1.0 + 0.8 * compartment) * 900.0 / std::pow(distance + 2.0, 1.05));
        }
    }
    return MemoryMatrixSource::make(MatrixRegion::square(GenomicRegion{kChrom, kStart, kEnd}), bin,
                                    bins, bins, std::move(values));
}

std::vector<Feature> states() {
    const struct {
        const char* name;
        Color color;
    } palette[] = {
        {"Enhancer", rgb(240, 180, 40)},
        {"Promoter", rgb(200, 50, 50)},
        {"Transcribed", rgb(60, 150, 80)},
        {"Quiescent", rgb(200, 200, 200)},
    };

    std::mt19937 rng(7);
    std::uniform_int_distribution<int> pick(0, 3);
    std::uniform_int_distribution<std::int64_t> width(6'000, 40'000);

    std::vector<Feature> features;
    std::int64_t cursor = kStart;
    while (cursor < kEnd) {
        const std::int64_t length = width(rng);
        const int index = pick(rng);
        Feature feature;
        feature.chrom = kChrom;
        feature.start = cursor;
        feature.end = std::min(cursor + length, kEnd);
        feature.name = palette[index].name;
        feature.color = palette[index].color;
        features.push_back(std::move(feature));
        cursor += length;
    }
    return features;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string prefix = argc > 1 ? argv[1] : "gallery";

    try {
        Figure figure;
        figure.set_width(inches(7.5))
            .set_title("GRE track gallery")
            .set_subtitle("signal styles, colour maps, multiple panels, legends");

        // ---- panel one: signal styles -------------------------------------
        Panel& styles = figure.add_panel();
        styles.set_region(kChrom, kStart, kEnd)
            .set_title("Signal styles")
            .set_show_grid(true)
            .set_border(rgb(210, 210, 210));
        styles.add_track(AxisTrack{});
        styles.add_track(
            SignalTrack{wave(1, 20.0, 260'000.0, false)}.name("area").style(SignalStyle::area));
        styles.add_track(SignalTrack{wave(2, 20.0, 190'000.0, false)}
                             .name("line")
                             .style(SignalStyle::line)
                             .color(rgb(200, 70, 60))
                             .y_axis(true));
        styles.add_track(SignalTrack{wave(3, 20.0, 420'000.0, false)}
                             .name("bars")
                             .style(SignalStyle::bars)
                             .color(rgb(80, 150, 90)));
        styles.add_track(SignalTrack{wave(4, 15.0, 150'000.0, true)}
                             .name("signed")
                             .style(SignalStyle::bars)
                             .color(rgb(60, 110, 190))
                             .negative_color(rgb(200, 90, 70)));

        // ---- panel two: annotations ---------------------------------------
        Panel& annotations = figure.add_panel();
        annotations.set_region(kChrom, kStart, kEnd).set_title("Annotations");
        annotations.add_track(
            IntervalTrack{states()}.name("ChromHMM").box_height(8.0).row_height(11.0));
        annotations.add_track(LegendTrack{}
                                  .add("Enhancer", rgb(240, 180, 40))
                                  .add("Promoter", rgb(200, 50, 50))
                                  .add("Transcribed", rgb(60, 150, 80))
                                  .add("Quiescent", rgb(200, 200, 200))
                                  .align(BarAlign::left));

        // ---- panel three: heatmap modes and colour maps --------------------
        Panel& maps = figure.add_panel();
        maps.set_region(kChrom, kStart, kEnd).set_title("Contact map, square and rotated");
        auto matrix = checkerboard(20'000);

        HeatmapTrack& square = maps.add_track(HeatmapTrack{matrix}
                                                  .name("viridis")
                                                  .mode(HeatmapMode::square)
                                                  .height(150.0)
                                                  .square_aspect(false)
                                                  .log_scale(true)
                                                  .colors("viridis")
                                                  .show_diagonal(true));
        maps.add_track(ColorBarTrack{square}.title("log contacts").align(BarAlign::left));
        maps.add_track(HeatmapTrack{matrix}
                           .name("fall, rotated")
                           .mode(HeatmapMode::triangle)
                           .max_distance(600'000)
                           .log_scale(true)
                           .colors("fall"));
        maps.add_track(AxisTrack{}.position(AxisPosition::bottom));

        figure.save_png(prefix + ".png", 200.0);
        figure.save_pdf(prefix + ".pdf");
        figure.save_svg(prefix + ".svg");

        const Size size = figure.computed_size();
        std::printf("wrote %s.{png,pdf,svg}  (%.0f x %.0f pt)\n", prefix.c_str(), size.width,
                    size.height);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
    return 0;
}
