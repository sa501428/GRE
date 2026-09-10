// A complete figure from synthetic data: no external files needed.
//
//   gre_example_basic [output-prefix]
//
// Writes <prefix>.png, <prefix>.pdf and <prefix>.svg.

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "gre/gre.hpp"

namespace {

using namespace gre;

constexpr const char* kChrom = "chr8";
constexpr std::int64_t kStart = 127'000'000;
constexpr std::int64_t kEnd = 129'000'000;

// A contact matrix with a couple of TAD-like blocks and a corner peak, so the
// example looks like something a Hi-C figure would actually show.
std::shared_ptr<MemoryMatrixSource> synthetic_matrix(std::int64_t bin) {
    const auto bins = static_cast<std::size_t>((kEnd - kStart) / bin);
    std::vector<float> values(bins * bins, 0.0F);

    const std::size_t domain_a_start = bins / 8;
    const std::size_t domain_a_end = bins / 2;
    const std::size_t domain_b_start = domain_a_end + bins / 20;
    const std::size_t domain_b_end = bins - bins / 10;

    std::mt19937 rng(20240909);
    std::uniform_real_distribution<double> noise(0.85, 1.15);

    for (std::size_t i = 0; i < bins; ++i) {
        for (std::size_t j = 0; j < bins; ++j) {
            const auto distance = static_cast<double>(i > j ? i - j : j - i);
            // Contact probability decays with genomic separation.
            double value = 1200.0 / std::pow(distance + 2.0, 1.1);

            const bool in_a = i >= domain_a_start && i < domain_a_end &&
                              j >= domain_a_start && j < domain_a_end;
            const bool in_b = i >= domain_b_start && i < domain_b_end &&
                              j >= domain_b_start && j < domain_b_end;
            if (in_a) value *= 2.4;
            if (in_b) value *= 3.1;

            // A loop between the two domain boundaries.
            const auto peak_i = static_cast<double>(domain_a_start);
            const auto peak_j = static_cast<double>(domain_b_end - 1);
            const double di = static_cast<double>(i) - peak_i;
            const double dj = static_cast<double>(j) - peak_j;
            value += 55.0 * std::exp(-(di * di + dj * dj) / 12.0);
            const double di2 = static_cast<double>(i) - peak_j;
            const double dj2 = static_cast<double>(j) - peak_i;
            value += 55.0 * std::exp(-(di2 * di2 + dj2 * dj2) / 12.0);

            values[i * bins + j] = static_cast<float>(value * noise(rng));
        }
    }
    return MemoryMatrixSource::make(
        MatrixRegion::square(GenomicRegion{kChrom, kStart, kEnd}), bin, bins, bins,
        std::move(values));
}

std::shared_ptr<MemorySignalSource> synthetic_signal(unsigned seed, double amplitude,
                                                     int peak_count) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> background(0.0, amplitude * 0.12);
    std::uniform_int_distribution<std::int64_t> position(kStart, kEnd - 5'000);
    std::uniform_real_distribution<double> strength(0.35, 1.0);

    constexpr std::int64_t kStep = 2'000;
    std::vector<MemorySignalSource::Interval> intervals;
    for (std::int64_t x = kStart; x < kEnd; x += kStep) {
        intervals.push_back({x, x + kStep, background(rng)});
    }
    for (int p = 0; p < peak_count; ++p) {
        const std::int64_t centre = position(rng);
        const double height = amplitude * strength(rng);
        for (std::int64_t x = centre - 6'000; x < centre + 6'000; x += kStep) {
            if (x < kStart || x >= kEnd) continue;
            const double distance = static_cast<double>(std::llabs(x - centre));
            intervals.push_back({x, x + kStep, height * std::exp(-distance / 3'000.0)});
        }
    }
    return MemorySignalSource::make(kChrom, std::move(intervals));
}

std::vector<Feature> synthetic_genes() {
    struct Definition {
        const char* name;
        std::int64_t start;
        std::int64_t end;
        Strand strand;
        int exons;
    };
    const Definition definitions[] = {
        {"MYC", 127'735'434, 127'742'951, Strand::forward, 3},
        {"PVT1", 127'794'533, 128'101'253, Strand::forward, 12},
        {"CASC8", 127'229'354, 127'508'631, Strand::reverse, 6},
        {"CCAT1", 127'207'382, 127'219'268, Strand::forward, 2},
        {"GSDMC", 128'329'000, 128'375'000, Strand::reverse, 8},
        {"FAM49B", 128'527'000, 128'690'000, Strand::forward, 10},
        {"ASAP1", 130'000'000, 130'200'000, Strand::reverse, 4},  // outside the view
    };

    std::vector<Feature> features;
    for (const Definition& definition : definitions) {
        Feature feature;
        feature.name = definition.name;
        feature.chrom = kChrom;
        feature.start = definition.start;
        feature.end = definition.end;
        feature.strand = definition.strand;

        const std::int64_t span = definition.end - definition.start;
        const std::int64_t stride = span / definition.exons;
        for (int e = 0; e < definition.exons; ++e) {
            const std::int64_t exon_start = definition.start + e * stride;
            feature.exons.push_back(Exon{exon_start, exon_start + std::max<std::int64_t>(stride / 3, 400)});
        }
        feature.thick_start = definition.start + span / 6;
        feature.thick_end = definition.end - span / 6;
        features.push_back(std::move(feature));
    }
    return features;
}

std::vector<Feature> synthetic_domains() {
    std::vector<Feature> features;
    const std::int64_t bounds[][2] = {
        {127'240'000, 128'010'000}, {128'060'000, 128'820'000}, {128'860'000, 128'990'000}};
    int index = 0;
    for (const auto& bound : bounds) {
        Feature feature;
        feature.chrom = kChrom;
        feature.start = bound[0];
        feature.end = bound[1];
        feature.name = "TAD " + std::to_string(++index);
        feature.score = 0.4 + 0.25 * index;
        features.push_back(std::move(feature));
    }
    return features;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string prefix = argc > 1 ? argv[1] : "basic_figure";

    try {
        auto matrix = synthetic_matrix(5'000);
        auto atac = synthetic_signal(11, 30.0, 22);
        auto chip = synthetic_signal(29, 12.0, 14);

        Figure figure;
        figure.set_width(inches(6.5))
            .set_theme(Theme::publication())
            .set_title("Synthetic contact map at the MYC locus")
            .set_subtitle("Generated by the GRE example; no data files required");

        Panel& panel = figure.add_panel();
        panel.set_region(kChrom, kStart, kEnd).set_show_grid(true);

        panel.add_track(AxisTrack{}.show_region(true));

        panel.add_track(SignalTrack{atac}.name("ATAC").height(40).color(rgb(31, 119, 180)));
        panel.add_track(SignalTrack{chip}
                            .name("CTCF")
                            .height(34)
                            .style(SignalStyle::area)
                            .color(rgb(214, 96, 40)));

        panel.add_track(GeneTrack{synthetic_genes()}.name("Genes"));

        panel.add_track(IntervalTrack{synthetic_domains()}
                            .name("TADs")
                            .color_by_score(ColorMap::named("blues"))
                            .box_height(7.0)
                            .rounded(true));

        HeatmapTrack& heatmap =
            panel.add_track(HeatmapTrack{matrix}
                                .name("Hi-C 5 kb")
                                .mode(HeatmapMode::triangle)
                                .max_distance(900'000)
                                .log_scale(true)
                                .colors("fall"));

        panel.add_track(ColorBarTrack{heatmap}
                            .title("contacts (log)")
                            .bar_length(120.0)
                            .align(BarAlign::right));

        figure.save_png(prefix + ".png", 300.0);
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
