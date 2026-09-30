// gre_plot -- compose a Hi-C figure from a .hic file and any number of 1D
// tracks, with per-track overrides.
//
//   gre_plot FILE.hic [chr:start-end] [global options]
//            FILE [track options] FILE [track options] ...
//
// Options that follow a track file apply to that track; everything else is
// global.  Run with --help for the full list.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gre/gre.hpp"

using namespace gre;

namespace {

// ---------------------------------------------------------------------------
// Specifications built from the command line
// ---------------------------------------------------------------------------

enum class TrackKind { automatic, signal, gene, interval, bedpe, virtual4c };
enum class TrackAxis { x, y, both };

struct PanelSpec {
    GenomicRegion region;
    GenomicRegion region_y;
    bool has_region_y{false};
    std::string title;
    bool has_title{false};
};

struct TrackSpec {
    std::string path;
    TrackKind kind{TrackKind::automatic};
    TrackAxis axis{TrackAxis::x};
    std::size_t panel_index{0};
    std::optional<GenomicRegion> viewpoint;

    std::optional<std::string> name;
    std::optional<Color> color;
    std::optional<Color> negative_color;
    std::optional<double> height;
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<double> percentile;
    std::optional<bool> symmetric;
    std::optional<bool> range_label;
    std::optional<bool> labels;
    std::optional<std::string> style;
    std::optional<std::string> colormap;
    std::optional<double> row_height;
    std::optional<double> line_width;
    std::optional<double> baseline;
    std::optional<std::string> aggregate;
    bool log{false};
    bool y_axis{false};
    bool representative_transcripts{false};

    AnnotationSide annotation_side{AnnotationSide::both};
    std::optional<Color> fill;
    std::vector<double> dash;
    std::int64_t expansion{0};
    std::optional<double> score_filter_min;
    std::optional<double> score_filter_max;
    std::optional<std::pair<double, double>> score_opacity;
    std::optional<std::pair<double, double>> score_line_width;
    std::optional<std::pair<double, double>> score_size;
};

struct HighlightSpec {
    std::size_t panel_index{0};
    GenomicRegion region;
    HighlightAxis axis{HighlightAxis::vertical};
    Color fill{rgba(255, 215, 0, 48)};
    Color border{rgba(190, 130, 0, 180)};
    double line_width{0.8};
    std::vector<double> dash;
    std::int64_t expansion{0};
};

struct Options {
    std::string hic_path;
    std::vector<PanelSpec> panels{PanelSpec{}};

    std::string layout{"pyramid"};
    std::string output{"figure"};
    std::vector<std::string> formats{"png", "pdf"};
    double dpi{300.0};
    double width_inches{0.0};
    double height_inches{0.0};
    std::string theme{"publication"};
    std::string title;
    std::string subtitle;
    bool has_title{false};
    bool has_subtitle{false};
    std::optional<bool> grid;
    double label_width{-1.0};
    double panel_spacing{-1.0};

    // Contact map
    bool no_map{false};
    std::string normalization{"NONE"};
    bool oe{false};
    std::int32_t resolution{0};
    std::optional<std::string> map_colors;
    std::optional<double> map_min;
    std::optional<double> map_max;
    std::optional<double> map_percentile;
    std::optional<double> map_height;
    std::optional<bool> map_log;
    std::int64_t max_distance{0};
    bool diagonal{false};
    std::optional<Color> map_border;
    bool shared_map_scale{false};

    // Split/VS map. The second source occupies one triangle of a square map.
    std::string comparison_path;
    std::optional<std::string> comparison_normalization;
    bool comparison_oe{false};
    std::int32_t comparison_resolution{0};
    std::optional<std::string> comparison_colors;
    std::optional<double> comparison_min;
    std::optional<double> comparison_max;
    std::optional<double> comparison_percentile;
    std::optional<bool> comparison_log;
    MatrixHalf comparison_half{MatrixHalf::below};

    std::vector<TrackSpec> tracks;
    std::vector<HighlightSpec> highlights;
};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string basename(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// File name without directory or extension: a better default track label than
// the full name, which is often long and shared across a directory tree.
std::string stem(const std::string& path) {
    std::string name = basename(path);
    const std::size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    return name;
}

std::int64_t parse_bases(const std::string& text);

bool parse_region(const std::string& text, GenomicRegion& out) {
    const std::size_t colon = text.rfind(':');
    if (colon == std::string::npos) return false;
    const std::size_t dash = text.find('-', colon);
    if (dash == std::string::npos) return false;
    try {
        out.chrom = text.substr(0, colon);
        out.start = parse_bases(text.substr(colon + 1, dash - colon - 1));
        out.end = parse_bases(text.substr(dash + 1));
    } catch (const std::exception&) {
        return false;
    }
    return out.end > out.start;
}

// Accepts plain numbers as well as 10kb / 2.5Mb / 1_000_000.
std::int64_t parse_bases(const std::string& text) {
    std::string value = text;
    value.erase(std::remove(value.begin(), value.end(), ','), value.end());
    value.erase(std::remove(value.begin(), value.end(), '_'), value.end());
    double multiplier = 1.0;
    const std::string suffix = lower(value);
    if (ends_with(suffix, "kb") || ends_with(suffix, "k")) {
        multiplier = 1e3;
        value.resize(value.size() - (ends_with(suffix, "kb") ? 2 : 1));
    } else if (ends_with(suffix, "mb") || ends_with(suffix, "m")) {
        multiplier = 1e6;
        value.resize(value.size() - (ends_with(suffix, "mb") ? 2 : 1));
    } else if (ends_with(suffix, "bp")) {
        value.resize(value.size() - 2);
    }
    return static_cast<std::int64_t>(std::llround(std::stod(value) * multiplier));
}

TrackKind kind_from_extension(const std::string& path) {
    const std::string name = lower(path);
    if (ends_with(name, ".bigwig") || ends_with(name, ".bw") || ends_with(name, ".bedgraph") ||
        ends_with(name, ".bg") || ends_with(name, ".wig")) {
        return TrackKind::signal;
    }
    if (ends_with(name, ".bed") || ends_with(name, ".bed.gz") || ends_with(name, ".gff") ||
        ends_with(name, ".gff.gz") || ends_with(name, ".gff3") ||
        ends_with(name, ".gff3.gz") || ends_with(name, ".gtf") ||
        ends_with(name, ".gtf.gz") || ends_with(name, ".bb") ||
        ends_with(name, ".bigbed") || ends_with(name, ".genepred")) {
        return TrackKind::gene;
    }
    if (ends_with(name, ".narrowpeak") || ends_with(name, ".broadpeak") ||
        ends_with(name, ".peak")) {
        return TrackKind::interval;
    }
    if (ends_with(name, ".bedpe") || ends_with(name, ".bedpe.gz")) {
        return TrackKind::bedpe;
    }
    return TrackKind::automatic;
}

std::vector<double> parse_dash(const std::string& text) {
    std::vector<double> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::string part =
            text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!part.empty()) {
            const double value = std::stod(part);
            if (!(value > 0.0)) {
                throw Error(ErrorCode::invalid_argument, "dash lengths must be positive");
            }
            out.push_back(value);
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    if (out.empty()) throw Error(ErrorCode::invalid_argument, "dash pattern is empty");
    return out;
}

std::pair<double, double> parse_pair(const std::string& text, const char* name) {
    const std::size_t comma = text.find(',');
    if (comma == std::string::npos || text.find(',', comma + 1) != std::string::npos) {
        throw Error(ErrorCode::invalid_argument,
                    std::string(name) + " wants two comma-separated numbers");
    }
    return {std::stod(text.substr(0, comma)), std::stod(text.substr(comma + 1))};
}

PairAnnotationStyle parse_annotation_style(const std::string& text) {
    const std::string value = lower(text);
    if (value == "loop" || value == "loops") return PairAnnotationStyle::loop;
    if (value == "box" || value == "boxes") return PairAnnotationStyle::box;
    if (value == "domain" || value == "tad" || value == "triangle") {
        return PairAnnotationStyle::domain;
    }
    throw Error(ErrorCode::invalid_argument,
                "unknown BEDPE style '" + text + "' (use loop, box, or domain)");
}

bool is_arc_track(const TrackSpec& spec) {
    if (spec.kind != TrackKind::bedpe || !spec.style.has_value()) return false;
    const std::string value = lower(*spec.style);
    return value == "arc" || value == "arcs" || value == "link" || value == "links";
}

SignalStyle parse_style(const std::string& text) {
    const std::string value = lower(text);
    if (value == "line") return SignalStyle::line;
    if (value == "area" || value == "fill") return SignalStyle::area;
    if (value == "bars" || value == "bar" || value == "histogram") return SignalStyle::bars;
    if (value == "points" || value == "dots") return SignalStyle::points;
    throw Error(ErrorCode::invalid_argument, "unknown style '" + text + "'");
}

IgvSignalSource::Aggregation parse_aggregation(const std::string& text) {
    const std::string value = lower(text);
    if (value == "mean" || value == "avg") return IgvSignalSource::Aggregation::mean;
    if (value == "max") return IgvSignalSource::Aggregation::max;
    if (value == "min") return IgvSignalSource::Aggregation::min;
    if (value == "sum") return IgvSignalSource::Aggregation::sum;
    throw Error(ErrorCode::invalid_argument, "unknown aggregation '" + text + "'");
}

// Compartment and eigenvector tracks are the common case here, and the
// convention is A red, B blue.
const Color kPositiveDefault = rgb(204, 0, 0);
const Color kNegativeDefault = rgb(0, 71, 171);

// ---------------------------------------------------------------------------
// Caching adapters
// ---------------------------------------------------------------------------
//
// In a square figure the same file feeds a horizontal and a vertical track over
// the same region, and unindexed bedGraph/wig files are read by scanning.  One
// slot of memoisation turns two scans into one.

class CachingSignalSource : public SignalSource {
public:
    explicit CachingSignalSource(SignalSourcePtr inner) : inner_(std::move(inner)) {}

    SignalData query(const GenomicRegion& region, std::size_t target_bins) override {
        if (cached_.has_value() && region == key_region_ && target_bins == key_bins_) {
            return *cached_;
        }
        SignalData data = inner_->query(region, target_bins);
        key_region_ = region;
        key_bins_ = target_bins;
        cached_ = data;
        return data;
    }

private:
    SignalSourcePtr inner_;
    GenomicRegion key_region_;
    std::size_t key_bins_{0};
    std::optional<SignalData> cached_;
};

class CachingFeatureSource : public FeatureSource {
public:
    explicit CachingFeatureSource(FeatureSourcePtr inner) : inner_(std::move(inner)) {}

    std::vector<Feature> query(const GenomicRegion& region) override {
        if (cached_.has_value() && region == key_region_) return *cached_;
        std::vector<Feature> features = inner_->query(region);
        key_region_ = region;
        cached_ = features;
        return features;
    }

private:
    FeatureSourcePtr inner_;
    GenomicRegion key_region_;
    std::optional<std::vector<Feature>> cached_;
};

// ---------------------------------------------------------------------------
// Usage
// ---------------------------------------------------------------------------

void print_usage(const char* program) {
    std::printf(
        "usage: %s FILE.hic [chr:start-end] [global options]\n"
        "       FILE [track options] FILE [track options] ...\n"
        "\n"
        "Options after a track file apply to that track.  Track files are\n"
        "recognised by extension: bigWig/bedGraph/wig become signal tracks,\n"
        "BED/GFF/GTF/bigBed gene tracks, narrowPeak/broadPeak interval tracks,\n"
        "and BEDPE map overlays or 1D arc tracks.\n"
        "\n"
        "Global:\n"
        "  --region CHR:START-END   region to plot (also accepted bare)\n"
        "  --panel CHR:START-END    start another vertically stacked panel\n"
        "  --panel-title TEXT       title for the current panel\n"
        "  --panel-spacing PT       gap between panels\n"
        "  --region-y CHR:START-END second axis, for an off-diagonal block\n"
        "  --layout square|pyramid|rectangle   default pyramid\n"
        "  --out PREFIX             output name stem (default figure)\n"
        "  --formats png,pdf,svg    which files to write (default png,pdf)\n"
        "  --dpi N                  raster resolution (default 300)\n"
        "  --width INCHES           page width (default 7, or 7.5 square)\n"
        "  --height INCHES          page height (default: fit the tracks)\n"
        "  --theme light|dark|publication      default publication\n"
        "  --title TEXT / --subtitle TEXT      default: region and file\n"
        "  --grid / --no-grid       vertical rules behind the tracks\n"
        "  --label-width PT         width of the track-name gutter\n"
        "\n"
        "Contact map:\n"
        "  --norm NAME              NONE, VC, VC_SQRT, KR, SCALE (default NONE)\n"
        "  --oe                     observed/expected, diverging colours\n"
        "  --resolution BP          force a resolution (default: fit the width)\n"
        "  --map-colors NAME        colour map (default juicebox, rd_bu for --oe)\n"
        "  --map-min V --map-max V  fix the colour range\n"
        "  --map-percentile P       clip the top of the range (default 0.99)\n"
        "  --map-log / --map-linear value scaling (default log)\n"
        "  --map-height PT          height, for rectangle layout\n"
        "  --max-distance BP        pyramid: how far from the diagonal to show\n"
        "  --diagonal               draw the diagonal in square layout\n"
        "  --map-border HEX         outline the map\n"
        "  --shared-map-scale       jointly fit map colours across panels\n"
        "  --vs FILE.hic            split square map with a second Hi-C file\n"
        "  --vs-side above|below    half occupied by --vs (default below)\n"
        "  --vs-norm NAME           normalization for --vs (default --norm)\n"
        "  --vs-oe                  observed/expected for --vs\n"
        "  --vs-resolution BP       resolution for --vs (default --resolution)\n"
        "  --vs-map-colors NAME     independent comparison colour map\n"
        "  --vs-map-min/max V       independent comparison limits\n"
        "  --vs-map-percentile P    independent comparison clipping\n"
        "  --vs-map-log/linear      independent comparison value scaling\n"
        "  --v-highlight REGION     translucent vertical map highlight\n"
        "  --h-highlight REGION     translucent horizontal map highlight\n"
        "  --xy-highlight REGION    vertical and horizontal highlight\n"
        "  --highlight-color HEX    fill for the preceding highlight\n"
        "  --highlight-border HEX   border for the preceding highlight\n"
        "  --highlight-width PT / --highlight-dashed / --highlight-dash A,B\n"
        "  --highlight-expand BP    expand the preceding highlight\n"
        "  --no-map                 tracks only, no contact map\n"
        "  --virtual4c REGION       matrix viewpoint as a new signal track\n"
        "\n"
        "Per track:\n"
        "  --name TEXT              label (default: the file name stem)\n"
        "  --color HEX              positive/main colour\n"
        "  --neg-color HEX          colour below the baseline\n"
        "  --height PT              track height, or column width on the y axis\n"
        "  --min V --max V          fix the data range\n"
        "  --percentile P           clip the top of an automatic range\n"
        "  --symmetric / --no-symmetric   centre the range on the baseline\n"
        "                           (default: on when the data spans zero)\n"
        "  --baseline V             where area and bar tracks are anchored\n"
        "  --log                    log value scaling\n"
        "  --style line|area|bars|points       signal style, default area\n"
        "          loop|box|domain|arc         BEDPE map/1D style\n"
        "  --aggregate mean|max|min|sum        binning, default mean\n"
        "  --line-width PT\n"
        "  --y-axis                 ticked y axis instead of a range label\n"
        "  --no-range-label\n"
        "  --colormap NAME          colour intervals by score\n"
        "  --labels / --no-labels   show or hide names\n"
        "  --row-height PT          gene track row pitch\n"
        "  --representative-transcripts  longest transcript per gene name\n"
        "  --type signal|gene|interval|bedpe   override the extension guess\n"
        "  --axis x|y|both          square layout: which axis (default x)\n"
        "  --side above|below|both  BEDPE placement (default both)\n"
        "  --expand BP              expand BEDPE anchors\n"
        "  --fill HEX               BEDPE overlay fill (alpha accepted)\n"
        "  --dashed / --dash A,B    BEDPE outline dash pattern\n"
        "  --score-filter-min/max V BEDPE score filtering\n"
        "  --score-opacity A,B      map low/high BEDPE scores to opacity\n"
        "  --score-line-width A,B   map scores to outline width in points\n"
        "  --score-size A,B         map scores to marker-size multipliers\n"
        "                           (not available for --style arc)\n"
        "\n"
        "Scoping rules:\n"
        "  Global options may appear anywhere. Per-track options apply to the\n"
        "  most recently named track file. BEDPE files are map overlays except\n"
        "  with --style arc, which creates a 1D row. Highlight styling applies\n"
        "  to the most recently declared --v-highlight, --h-highlight or\n"
        "  --xy-highlight.\n"
        "  --panel starts a new scope: following tracks and highlights belong\n"
        "  to that panel. --virtual4c creates a track, so track options that\n"
        "  follow it style that profile.\n"
        "\n"
        "Examples:\n"
        "  # Rotated pyramid with genes and a signal track\n"
        "  %s sample.hic chr8:127000000-129000000 atac.bigWig --name ATAC genes.bed\n"
        "\n"
        "  # Square map with the same compartment track on both axes\n"
        "  %s sample.hic chr1:20Mb-24Mb --layout square eigen.bedGraph --axis both\n"
        "\n"
        "  # Off-diagonal or inter-chromosomal rectangle\n"
        "  %s sample.hic chr1:10Mb-15Mb --layout rectangle --region-y chr2:30Mb-35Mb\n"
        "\n"
        "  # Observed/expected map with a fixed diverging range\n"
        "  %s sample.hic chr3:40Mb-50Mb --oe --map-min 0.5 --map-max 2 --map-colors rd_bu\n"
        "\n"
        "  # Two samples split across the diagonal\n"
        "  %s control.hic chr2:40Mb-44Mb --layout square --norm SCALE \\\n"
        "      --vs treated.hic --vs-norm SCALE --vs-side below \\\n"
        "      --map-colors reds --vs-map-colors blues\n"
        "\n"
        "  # Loops in both halves and dashed TADs above the diagonal\n"
        "  %s sample.hic chr2:40Mb-44Mb --layout square \\\n"
        "      loops.bedpe.gz --style loop --side both --color '#202020' \\\n"
        "      domains.bedpe --style domain --side above --dashed --expand 5kb\n"
        "\n"
        "  # Translucent vertical and horizontal highlighted intervals\n"
        "  %s sample.hic chr5:70Mb-75Mb --layout square \\\n"
        "      --v-highlight chr5:71Mb-71.2Mb --highlight-color '#FFD70030' \\\n"
        "      --h-highlight chr5:73Mb-73.1Mb --highlight-dashed\n"
        "\n"
        "  # Score-driven loop styling\n"
        "  %s sample.hic chr2:40Mb-44Mb --layout square loops.bedpe \\\n"
        "      --score-filter-min 5 --colormap viridis \\\n"
        "      --score-opacity 0.2,1 --score-line-width 0.5,2.5 --score-size 0.7,1.6\n"
        "\n"
        "  # Direct virtual 4C profile plus its viewpoint marker\n"
        "  %s sample.hic chr3:45Mb-49Mb --virtual4c chr3:46.2Mb-46.25Mb \\\n"
        "      --name Promoter --style line --aggregate mean \\\n"
        "      --v-highlight chr3:46.2Mb-46.25Mb\n"
        "\n"
        "  # Three panels with one jointly fitted primary map scale\n"
        "  %s sample.hic chr1:20Mb-24Mb --shared-map-scale \\\n"
        "      --panel chr8:127Mb-129Mb --panel-title MYC \\\n"
        "      --panel chr12:10Mb-13Mb --panel-title CDK2\n"
        "\n"
        "  # Publication outputs at explicit raster resolution\n"
        "  %s sample.hic chr7:50Mb-54Mb --formats png,pdf,svg --dpi 600 \\\n"
        "      --width 7.2 --theme publication --out figure\n",
        program, program, program, program, program, program, program, program, program,
        program, program, program);
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

bool parse_command_line(int argc, char** argv, Options& out) {
    if (argc < 2) {
        print_usage(argv[0]);
        return false;
    }
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            print_usage(argv[0]);
            return false;
        }
    }
    out.hic_path = argv[1];

    TrackSpec* current = nullptr;
    std::size_t current_panel = 0;
    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        const auto value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                throw Error(ErrorCode::invalid_argument, std::string(name) + " needs a value");
            }
            return argv[++i];
        };
        const auto track_option = [&](const char* name) -> TrackSpec& {
            if (current == nullptr) {
                throw Error(ErrorCode::invalid_argument,
                            std::string(name) + " applies to a track, but no track file has "
                                                "been named yet");
            }
            return *current;
        };

        if (argument.rfind("--", 0) != 0) {
            // A bare word is either the region or a track file.
            GenomicRegion region;
            if (parse_region(argument, region)) {
                out.panels[current_panel].region = region;
                continue;
            }
            TrackSpec spec;
            spec.path = argument;
            spec.kind = kind_from_extension(argument);
            spec.panel_index = current_panel;
            if (spec.kind == TrackKind::automatic) {
                throw Error(ErrorCode::invalid_argument,
                            "cannot tell what '" + argument +
                                "' is from its extension; add --type signal|gene|interval|bedpe");
            }
            out.tracks.push_back(std::move(spec));
            current = &out.tracks.back();
            continue;
        }

        // ---- global ----
        if (argument == "--region") {
            if (!parse_region(value("--region"), out.panels[current_panel].region)) {
                throw Error(ErrorCode::invalid_argument, "--region wants CHR:START-END");
            }
        } else if (argument == "--panel") {
            PanelSpec panel;
            if (!parse_region(value("--panel"), panel.region)) {
                throw Error(ErrorCode::invalid_argument, "--panel wants CHR:START-END");
            }
            out.panels.push_back(std::move(panel));
            current_panel = out.panels.size() - 1;
            current = nullptr;
        } else if (argument == "--panel-title") {
            out.panels[current_panel].title = value("--panel-title");
            out.panels[current_panel].has_title = true;
        } else if (argument == "--region-y") {
            if (!parse_region(value("--region-y"), out.panels[current_panel].region_y)) {
                throw Error(ErrorCode::invalid_argument, "--region-y wants CHR:START-END");
            }
            out.panels[current_panel].has_region_y = true;
        } else if (argument == "--layout") {
            out.layout = lower(value("--layout"));
        } else if (argument == "--out") {
            out.output = value("--out");
        } else if (argument == "--formats") {
            out.formats.clear();
            std::string list = value("--formats");
            std::size_t start = 0;
            while (start <= list.size()) {
                const std::size_t comma = list.find(',', start);
                const std::string item =
                    list.substr(start, comma == std::string::npos ? std::string::npos
                                                                  : comma - start);
                if (!item.empty()) out.formats.push_back(lower(item));
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        } else if (argument == "--dpi") {
            out.dpi = std::stod(value("--dpi"));
        } else if (argument == "--width") {
            out.width_inches = std::stod(value("--width"));
        } else if (argument == "--height") {
            // Page height in inches before any track file, track height in
            // points after one -- which is how each reads at its own position.
            if (current != nullptr) {
                current->height = std::stod(value("--height"));
            } else {
                out.height_inches = std::stod(value("--height"));
            }
        } else if (argument == "--theme") {
            out.theme = lower(value("--theme"));
        } else if (argument == "--title") {
            out.title = value("--title");
            out.has_title = true;
        } else if (argument == "--subtitle") {
            out.subtitle = value("--subtitle");
            out.has_subtitle = true;
        } else if (argument == "--grid") {
            out.grid = true;
        } else if (argument == "--no-grid") {
            out.grid = false;
        } else if (argument == "--label-width") {
            out.label_width = std::stod(value("--label-width"));
        } else if (argument == "--panel-spacing") {
            out.panel_spacing = std::stod(value("--panel-spacing"));

            // ---- contact map ----
        } else if (argument == "--norm") {
            out.normalization = value("--norm");
        } else if (argument == "--oe") {
            out.oe = true;
        } else if (argument == "--resolution") {
            out.resolution = static_cast<std::int32_t>(parse_bases(value("--resolution")));
        } else if (argument == "--map-colors" || argument == "--map-colours") {
            out.map_colors = value("--map-colors");
        } else if (argument == "--map-min") {
            out.map_min = std::stod(value("--map-min"));
        } else if (argument == "--map-max") {
            out.map_max = std::stod(value("--map-max"));
        } else if (argument == "--map-percentile") {
            out.map_percentile = std::stod(value("--map-percentile"));
        } else if (argument == "--map-height") {
            out.map_height = std::stod(value("--map-height"));
        } else if (argument == "--map-log") {
            out.map_log = true;
        } else if (argument == "--map-linear") {
            out.map_log = false;
        } else if (argument == "--max-distance") {
            out.max_distance = parse_bases(value("--max-distance"));
        } else if (argument == "--diagonal") {
            out.diagonal = true;
        } else if (argument == "--map-border") {
            out.map_border = color_from_hex(value("--map-border"));
        } else if (argument == "--shared-map-scale") {
            out.shared_map_scale = true;
        } else if (argument == "--vs" || argument == "--compare-hic") {
            out.comparison_path = value("--vs");
        } else if (argument == "--vs-side") {
            const std::string side = lower(value("--vs-side"));
            if (side == "above") {
                out.comparison_half = MatrixHalf::above;
            } else if (side == "below") {
                out.comparison_half = MatrixHalf::below;
            } else {
                throw Error(ErrorCode::invalid_argument, "--vs-side wants above or below");
            }
        } else if (argument == "--vs-norm") {
            out.comparison_normalization = value("--vs-norm");
        } else if (argument == "--vs-oe") {
            out.comparison_oe = true;
        } else if (argument == "--vs-resolution") {
            out.comparison_resolution =
                static_cast<std::int32_t>(parse_bases(value("--vs-resolution")));
        } else if (argument == "--vs-map-colors" || argument == "--vs-map-colours") {
            out.comparison_colors = value("--vs-map-colors");
        } else if (argument == "--vs-map-min") {
            out.comparison_min = std::stod(value("--vs-map-min"));
        } else if (argument == "--vs-map-max") {
            out.comparison_max = std::stod(value("--vs-map-max"));
        } else if (argument == "--vs-map-percentile") {
            out.comparison_percentile = std::stod(value("--vs-map-percentile"));
        } else if (argument == "--vs-map-log") {
            out.comparison_log = true;
        } else if (argument == "--vs-map-linear") {
            out.comparison_log = false;
        } else if (argument == "--v-highlight" || argument == "--h-highlight" ||
                   argument == "--xy-highlight") {
            GenomicRegion region;
            if (!parse_region(value(argument.c_str()), region)) {
                throw Error(ErrorCode::invalid_argument,
                            argument + " wants CHR:START-END");
            }
            HighlightSpec highlight;
            highlight.panel_index = current_panel;
            highlight.region = std::move(region);
            highlight.axis = argument == "--v-highlight"   ? HighlightAxis::vertical
                             : argument == "--h-highlight" ? HighlightAxis::horizontal
                                                             : HighlightAxis::both;
            out.highlights.push_back(std::move(highlight));
        } else if (argument == "--highlight-color" || argument == "--highlight-colour") {
            if (out.highlights.empty()) {
                throw Error(ErrorCode::invalid_argument,
                            "--highlight-color needs a preceding highlight");
            }
            out.highlights.back().fill = color_from_hex(value("--highlight-color"));
        } else if (argument == "--highlight-border") {
            if (out.highlights.empty()) {
                throw Error(ErrorCode::invalid_argument,
                            "--highlight-border needs a preceding highlight");
            }
            out.highlights.back().border = color_from_hex(value("--highlight-border"));
        } else if (argument == "--highlight-width") {
            if (out.highlights.empty()) {
                throw Error(ErrorCode::invalid_argument,
                            "--highlight-width needs a preceding highlight");
            }
            out.highlights.back().line_width = std::stod(value("--highlight-width"));
        } else if (argument == "--highlight-dashed") {
            if (out.highlights.empty()) {
                throw Error(ErrorCode::invalid_argument,
                            "--highlight-dashed needs a preceding highlight");
            }
            out.highlights.back().dash = {4.0, 2.5};
        } else if (argument == "--highlight-dash") {
            if (out.highlights.empty()) {
                throw Error(ErrorCode::invalid_argument,
                            "--highlight-dash needs a preceding highlight");
            }
            out.highlights.back().dash = parse_dash(value("--highlight-dash"));
        } else if (argument == "--highlight-expand") {
            if (out.highlights.empty()) {
                throw Error(ErrorCode::invalid_argument,
                            "--highlight-expand needs a preceding highlight");
            }
            out.highlights.back().expansion = parse_bases(value("--highlight-expand"));
        } else if (argument == "--no-map") {
            out.no_map = true;

        } else if (argument == "--virtual4c") {
            GenomicRegion viewpoint;
            if (!parse_region(value("--virtual4c"), viewpoint)) {
                throw Error(ErrorCode::invalid_argument,
                            "--virtual4c wants CHR:START-END");
            }
            TrackSpec spec;
            spec.path = "virtual4c";
            spec.kind = TrackKind::virtual4c;
            spec.panel_index = current_panel;
            spec.viewpoint = std::move(viewpoint);
            spec.style = "line";
            out.tracks.push_back(std::move(spec));
            current = &out.tracks.back();

            // ---- per track ----
        } else if (argument == "--name" || argument == "--label") {
            track_option("--name").name = value("--name");
        } else if (argument == "--color" || argument == "--colour") {
            track_option("--color").color = color_from_hex(value("--color"));
        } else if (argument == "--neg-color" || argument == "--neg-colour") {
            track_option("--neg-color").negative_color = color_from_hex(value("--neg-color"));
        } else if (argument == "--height-pt") {
            track_option("--height-pt").height = std::stod(value("--height-pt"));
        } else if (argument == "--min") {
            track_option("--min").minimum = std::stod(value("--min"));
        } else if (argument == "--max") {
            track_option("--max").maximum = std::stod(value("--max"));
        } else if (argument == "--percentile") {
            track_option("--percentile").percentile = std::stod(value("--percentile"));
        } else if (argument == "--symmetric") {
            track_option("--symmetric").symmetric = true;
        } else if (argument == "--no-symmetric") {
            track_option("--no-symmetric").symmetric = false;
        } else if (argument == "--baseline") {
            track_option("--baseline").baseline = std::stod(value("--baseline"));
        } else if (argument == "--log") {
            track_option("--log").log = true;
        } else if (argument == "--style") {
            track_option("--style").style = lower(value("--style"));
        } else if (argument == "--aggregate") {
            track_option("--aggregate").aggregate = value("--aggregate");
        } else if (argument == "--line-width") {
            track_option("--line-width").line_width = std::stod(value("--line-width"));
        } else if (argument == "--y-axis") {
            track_option("--y-axis").y_axis = true;
        } else if (argument == "--no-range-label") {
            track_option("--no-range-label").range_label = false;
        } else if (argument == "--colormap") {
            track_option("--colormap").colormap = value("--colormap");
        } else if (argument == "--no-labels") {
            track_option("--no-labels").labels = false;
        } else if (argument == "--labels") {
            track_option("--labels").labels = true;
        } else if (argument == "--row-height") {
            track_option("--row-height").row_height = std::stod(value("--row-height"));
        } else if (argument == "--representative-transcripts") {
            track_option("--representative-transcripts").representative_transcripts = true;
        } else if (argument == "--type") {
            const std::string kind = lower(value("--type"));
            TrackSpec& spec = track_option("--type");
            if (kind == "signal") {
                spec.kind = TrackKind::signal;
            } else if (kind == "gene") {
                spec.kind = TrackKind::gene;
            } else if (kind == "interval") {
                spec.kind = TrackKind::interval;
            } else if (kind == "bedpe" || kind == "pair") {
                spec.kind = TrackKind::bedpe;
            } else {
                throw Error(ErrorCode::invalid_argument, "unknown --type '" + kind + "'");
            }
        } else if (argument == "--axis") {
            const std::string axis = lower(value("--axis"));
            TrackSpec& spec = track_option("--axis");
            if (axis == "x") {
                spec.axis = TrackAxis::x;
            } else if (axis == "y") {
                spec.axis = TrackAxis::y;
            } else if (axis == "both") {
                spec.axis = TrackAxis::both;
            } else {
                throw Error(ErrorCode::invalid_argument, "--axis wants x, y or both");
            }
        } else if (argument == "--side") {
            const std::string side = lower(value("--side"));
            TrackSpec& spec = track_option("--side");
            if (side == "above") {
                spec.annotation_side = AnnotationSide::above;
            } else if (side == "below") {
                spec.annotation_side = AnnotationSide::below;
            } else if (side == "both") {
                spec.annotation_side = AnnotationSide::both;
            } else {
                throw Error(ErrorCode::invalid_argument, "--side wants above, below or both");
            }
        } else if (argument == "--expand") {
            track_option("--expand").expansion = parse_bases(value("--expand"));
        } else if (argument == "--fill") {
            track_option("--fill").fill = color_from_hex(value("--fill"));
        } else if (argument == "--dashed") {
            track_option("--dashed").dash = {4.0, 2.5};
        } else if (argument == "--dash") {
            track_option("--dash").dash = parse_dash(value("--dash"));
        } else if (argument == "--score-filter-min") {
            track_option("--score-filter-min").score_filter_min =
                std::stod(value("--score-filter-min"));
        } else if (argument == "--score-filter-max") {
            track_option("--score-filter-max").score_filter_max =
                std::stod(value("--score-filter-max"));
        } else if (argument == "--score-opacity") {
            track_option("--score-opacity").score_opacity =
                parse_pair(value("--score-opacity"), "--score-opacity");
        } else if (argument == "--score-line-width") {
            track_option("--score-line-width").score_line_width =
                parse_pair(value("--score-line-width"), "--score-line-width");
        } else if (argument == "--score-size") {
            track_option("--score-size").score_size =
                parse_pair(value("--score-size"), "--score-size");
        } else if (argument == "--track-height") {
            track_option("--track-height").height = std::stod(value("--track-height"));
        } else {
            throw Error(ErrorCode::invalid_argument, "unknown option '" + argument + "'");
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Building tracks
// ---------------------------------------------------------------------------

struct LoadedSource {
    SignalSourcePtr signal;
    FeatureSourcePtr features;
    PairFeatureSourcePtr pairs;
};

LoadedSource load(const TrackSpec& spec, const MatrixSourcePtr& matrix) {
    LoadedSource loaded;
    if (spec.kind == TrackKind::signal) {
        auto source = IgvSignalSource::open(spec.path);
        source->fill_empty(0.0);
        if (spec.aggregate.has_value()) source->aggregation(parse_aggregation(*spec.aggregate));
        loaded.signal = std::make_shared<CachingSignalSource>(source);
    } else if (spec.kind == TrackKind::virtual4c) {
        auto source = Virtual4CSource::make(matrix, *spec.viewpoint);
        if (spec.aggregate.has_value()) {
            const std::string aggregation = lower(*spec.aggregate);
            if (aggregation == "mean" || aggregation == "avg") {
                source->aggregation(Virtual4CSource::Aggregation::mean);
            } else if (aggregation == "sum") {
                source->aggregation(Virtual4CSource::Aggregation::sum);
            } else if (aggregation == "max") {
                source->aggregation(Virtual4CSource::Aggregation::maximum);
            } else {
                throw Error(ErrorCode::invalid_argument,
                            "virtual 4C aggregation wants mean, sum, or max");
            }
        }
        loaded.signal = source;
    } else if (spec.kind == TrackKind::bedpe) {
        loaded.pairs = BedpeSource::open(spec.path);
    } else {
        loaded.features = std::make_shared<CachingFeatureSource>(IgvFeatureSource::open(spec.path));
    }
    return loaded;
}

// Builds one track from a spec.  `vertical` tracks are quarter-turned by the
// layout, so they drop the bits that only make sense across a wide row.
std::unique_ptr<Track> build_track(const TrackSpec& spec, const LoadedSource& source,
                                   bool vertical) {
    const std::string default_name =
        spec.kind == TrackKind::virtual4c
            ? std::string("virtual 4C (") + format_region(spec.viewpoint->chrom,
                                                          spec.viewpoint->start,
                                                          spec.viewpoint->end) + ")"
            : stem(spec.path);
    const std::string name = spec.name.value_or(default_name);

    if (is_arc_track(spec)) {
        auto track = std::make_unique<ArcTrack>(source.pairs);
        track->name(name).show_labels(spec.labels.value_or(false));
        if (spec.color.has_value()) track->color(*spec.color);
        if (spec.height.has_value()) track->height(*spec.height);
        if (spec.fill.has_value()) track->fill(*spec.fill);
        if (spec.line_width.has_value()) track->line_width(*spec.line_width);
        if (!spec.dash.empty()) track->dash(spec.dash);
        if (spec.expansion > 0) track->expand(spec.expansion);
        if (spec.score_size.has_value()) {
            throw Error(ErrorCode::invalid_argument,
                        "--score-size cannot change the radius of a semicircular arc");
        }
        if (spec.score_filter_min.has_value() || spec.score_filter_max.has_value()) {
            track->score_filter(spec.score_filter_min, spec.score_filter_max);
        }
        if (spec.colormap.has_value()) {
            ValueScale score_scale;
            if (spec.minimum.has_value()) score_scale.min(*spec.minimum);
            if (spec.maximum.has_value()) score_scale.max(*spec.maximum);
            if (spec.percentile.has_value()) {
                score_scale.upper_percentile(*spec.percentile);
            }
            track->color_by_score(*spec.colormap, score_scale);
        }
        if (spec.score_opacity.has_value()) {
            track->opacity_by_score(spec.score_opacity->first,
                                    spec.score_opacity->second);
        }
        if (spec.score_line_width.has_value()) {
            track->line_width_by_score(spec.score_line_width->first,
                                       spec.score_line_width->second);
        }
        return track;
    }

    if (spec.kind == TrackKind::signal || spec.kind == TrackKind::virtual4c) {
        auto track = std::make_unique<SignalTrack>(source.signal);
        track->name(name);
        track->style(spec.style.has_value() ? parse_style(*spec.style) : SignalStyle::area);
        track->color(spec.color.value_or(kPositiveDefault));
        track->negative_color(spec.negative_color.value_or(kNegativeDefault));
        if (spec.height.has_value()) track->height(*spec.height);
        if (spec.log) track->log_scale(true);
        if (spec.symmetric.has_value()) track->symmetric(*spec.symmetric);
        if (spec.baseline.has_value()) track->baseline(*spec.baseline);
        if (spec.line_width.has_value()) track->line_width(*spec.line_width);

        ValueScale scale;
        if (spec.log) scale.type(ScaleType::log1p);
        if (spec.minimum.has_value()) scale.min(*spec.minimum);
        if (spec.maximum.has_value()) scale.max(*spec.maximum);
        if (spec.percentile.has_value()) scale.upper_percentile(*spec.percentile);
        track->scale(scale);

        if (spec.y_axis && !vertical) track->y_axis(true);
        track->show_range_label(spec.range_label.value_or(!vertical));
        return track;
    }

    if (spec.kind == TrackKind::gene) {
        auto track = std::make_unique<GeneTrack>(source.features);
        track->name(name);
        if (spec.color.has_value()) track->color(*spec.color);
        if (spec.height.has_value()) track->height(*spec.height);
        if (spec.row_height.has_value()) track->row_height(*spec.row_height);
        track->representative_transcripts(spec.representative_transcripts);
        // Gene names need a wide row; in a narrow turned column they only
        // collide, so they are off there unless asked for.
        track->show_labels(spec.labels.value_or(!vertical));
        return track;
    }

    auto track = std::make_unique<IntervalTrack>(source.features);
    track->name(name);
    if (spec.color.has_value()) track->color(*spec.color);
    if (spec.height.has_value()) track->height(*spec.height);
    if (spec.row_height.has_value()) track->row_height(*spec.row_height);
    if (spec.colormap.has_value()) {
        track->color_by_score(ColorMap::named(*spec.colormap));
        ValueScale scale;
        if (spec.minimum.has_value()) scale.min(*spec.minimum);
        if (spec.maximum.has_value()) scale.max(*spec.maximum);
        track->score_scale(scale);
    }
    track->show_labels(spec.labels.value_or(!vertical));
    return track;
}

PairAnnotationLayer build_annotation(const TrackSpec& spec, const LoadedSource& source) {
    PairAnnotationLayer layer{source.pairs};
    layer.style(spec.style.has_value() ? parse_annotation_style(*spec.style)
                                       : PairAnnotationStyle::loop);
    layer.side(spec.annotation_side);
    if (spec.color.has_value()) layer.color(*spec.color);
    if (spec.fill.has_value()) layer.fill(*spec.fill);
    if (spec.line_width.has_value()) layer.line_width(*spec.line_width);
    if (!spec.dash.empty()) layer.dash(spec.dash);
    if (spec.expansion > 0) layer.expand(spec.expansion);
    layer.show_labels(spec.labels.value_or(false));
    if (spec.score_filter_min.has_value() || spec.score_filter_max.has_value()) {
        layer.score_filter(spec.score_filter_min, spec.score_filter_max);
    }
    if (spec.colormap.has_value()) {
        ValueScale score_scale;
        if (spec.minimum.has_value()) score_scale.min(*spec.minimum);
        if (spec.maximum.has_value()) score_scale.max(*spec.maximum);
        if (spec.percentile.has_value()) score_scale.upper_percentile(*spec.percentile);
        layer.color_by_score(*spec.colormap, score_scale);
    }
    if (spec.score_opacity.has_value()) {
        layer.opacity_by_score(spec.score_opacity->first, spec.score_opacity->second);
    }
    if (spec.score_line_width.has_value()) {
        layer.line_width_by_score(spec.score_line_width->first,
                                  spec.score_line_width->second);
    }
    if (spec.score_size.has_value()) {
        layer.size_by_score(spec.score_size->first, spec.score_size->second);
    }
    return layer;
}

std::unique_ptr<HeatmapTrack> build_map(const Options& options, const MatrixSourcePtr& source,
                                        const MatrixSourcePtr& comparison, HeatmapMode mode,
                                        const HeatmapScaleGroupPtr& shared_scale = nullptr,
                                        const HeatmapScaleGroupPtr& shared_comparison_scale = nullptr) {
    auto map = std::make_unique<HeatmapTrack>(source);
    map->name("Hi-C");
    map->mode(mode);
    map->colors(ColorMap::named(
        options.map_colors.value_or(options.oe ? std::string("rd_bu")
                                           : std::string("juicebox"))));
    if (options.map_border.has_value()) map->border(*options.map_border);
    if (options.diagonal) map->show_diagonal(true);
    if (options.map_height.has_value()) map->height(*options.map_height);
    if (options.max_distance > 0) map->max_distance(options.max_distance);

    const bool logarithmic = options.map_log.value_or(!options.oe);
    ValueScale scale;
    if (options.oe) {
        // A ratio: centre a diverging map on 1 and work in log space.
        scale = ValueScale{options.map_min.value_or(0.25), options.map_max.value_or(4.0),
                           ScaleType::log};
    } else {
        if (logarithmic) scale.type(ScaleType::log1p);
        if (options.map_min.has_value()) scale.min(*options.map_min);
        if (options.map_max.has_value()) scale.max(*options.map_max);
        scale.upper_percentile(options.map_percentile.value_or(0.99));
    }
    map->scale(scale);
    if (shared_scale != nullptr) map->shared_scale(shared_scale);

    if (comparison != nullptr) {
        map->compare_with(comparison, options.comparison_half);
        map->comparison_colors(ColorMap::named(options.comparison_colors.value_or(
            options.comparison_oe ? std::string("rd_bu")
                                  : options.map_colors.value_or(std::string("juicebox")))));
        const bool comparison_log = options.comparison_log.value_or(!options.comparison_oe);
        ValueScale comparison_scale;
        if (options.comparison_oe) {
            comparison_scale = ValueScale{options.comparison_min.value_or(0.25),
                                          options.comparison_max.value_or(4.0), ScaleType::log};
        } else {
            if (comparison_log) comparison_scale.type(ScaleType::log1p);
            if (options.comparison_min.has_value()) comparison_scale.min(*options.comparison_min);
            if (options.comparison_max.has_value()) comparison_scale.max(*options.comparison_max);
            comparison_scale.upper_percentile(options.comparison_percentile.value_or(0.99));
        }
        map->comparison_scale(comparison_scale);
        if (shared_comparison_scale != nullptr) {
            map->shared_comparison_scale(shared_comparison_scale);
        }
    }
    return map;
}

void describe(const StrawMatrixSource& source) {
    std::printf("genome %s, version %d\n", source.genome().c_str(), source.version());
    std::printf("resolutions:");
    for (std::int32_t resolution : source.resolutions()) std::printf(" %d", resolution);
    std::printf("\nnormalizations:");
    for (const std::string& norm : source.normalizations()) std::printf(" %s", norm.c_str());
    std::printf("\n");
}

GenomicRegion default_region(const StrawMatrixSource& source) {
    if (source.contigs().empty()) {
        throw Error(ErrorCode::not_found, "the .hic file lists no contigs");
    }
    const HicContig& contig = source.contigs().front();
    const std::int64_t finest = source.resolutions().front();
    const std::int64_t span =
        std::min<std::int64_t>(contig.length, std::max<std::int64_t>(finest * 400, 1));
    const std::int64_t start = std::max<std::int64_t>(0, contig.length / 4 - span / 2);
    return GenomicRegion{contig.name, start, std::min(start + span, contig.length)};
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    try {
        if (!parse_command_line(argc, argv, options)) return argc < 2 ? 2 : 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 2;
    }

    try {
        StrawOptions straw;
        straw.normalization = options.normalization;
        straw.matrix_type = options.oe ? "oe" : "observed";
        straw.resolution = options.resolution;
        auto matrix = StrawMatrixSource::open(options.hic_path, straw);
        describe(*matrix);

        MatrixSourcePtr comparison;
        std::shared_ptr<StrawMatrixSource> comparison_straw;
        if (!options.comparison_path.empty()) {
            StrawOptions comparison_options;
            comparison_options.normalization =
                options.comparison_normalization.value_or(options.normalization);
            comparison_options.matrix_type = options.comparison_oe ? "oe" : "observed";
            comparison_options.resolution = options.comparison_resolution > 0
                                                ? options.comparison_resolution
                                                : options.resolution;
            comparison_straw =
                StrawMatrixSource::open(options.comparison_path, comparison_options);
            comparison = comparison_straw;
        }

        if (options.panels.front().region.chrom.empty()) {
            options.panels.front().region = default_region(*matrix);
        }
        for (std::size_t p = 0; p < options.panels.size(); ++p) {
            const GenomicRegion& region = options.panels[p].region;
            if (region.chrom.empty()) {
                throw Error(ErrorCode::invalid_argument,
                            "panel " + std::to_string(p + 1) + " has no region");
            }
            std::printf("panel %zu: %s\n", p + 1,
                        format_region(region.chrom, region.start, region.end).c_str());
        }

        const bool square = options.layout == "square";
        const bool rectangle = options.layout == "rectangle" || options.layout == "rect";
        if (!square && !rectangle && options.layout != "pyramid" &&
            options.layout != "triangle") {
            throw Error(ErrorCode::invalid_argument,
                        "--layout wants square, pyramid or rectangle");
        }
        if (comparison != nullptr && !square) {
            throw Error(ErrorCode::invalid_argument, "--vs requires --layout square");
        }
        if (comparison != nullptr && options.no_map) {
            throw Error(ErrorCode::invalid_argument, "--vs cannot be combined with --no-map");
        }

        Figure figure;
        figure.set_theme(Theme::named(options.theme));
        const double width = options.width_inches > 0.0 ? inches(options.width_inches)
                                                        : inches(square ? 7.5 : 7.0);
        figure.set_size(width, options.height_inches > 0.0 ? inches(options.height_inches) : 0.0);
        if (options.panel_spacing >= 0.0) figure.set_panel_spacing(options.panel_spacing);
        figure.set_title(options.has_title
                             ? options.title
                             : options.panels.size() == 1
                                   ? format_region(options.panels.front().region.chrom,
                                                   options.panels.front().region.start,
                                                   options.panels.front().region.end)
                                   : basename(options.hic_path));
        figure.set_subtitle(options.has_subtitle
                                ? options.subtitle
                                : basename(options.hic_path) + "   " + straw.matrix_type + " / " +
                                      straw.normalization);

        // One reader per file, shared by the horizontal and vertical view of it.
        std::vector<LoadedSource> sources;
        sources.reserve(options.tracks.size());
        for (const TrackSpec& spec : options.tracks) sources.push_back(load(spec, matrix));

        HeatmapScaleGroupPtr shared_primary;
        HeatmapScaleGroupPtr shared_comparison;
        if (options.shared_map_scale) {
            shared_primary = std::make_shared<HeatmapScaleGroup>();
            if (comparison != nullptr) {
                shared_comparison = std::make_shared<HeatmapScaleGroup>();
            }
        }

        std::vector<HeatmapTrack*> maps;
        maps.reserve(options.panels.size());
        for (std::size_t p = 0; p < options.panels.size(); ++p) {
            const PanelSpec& panel_spec = options.panels[p];
            Panel& panel = figure.add_panel();
            panel.set_region(panel_spec.region);
            if (panel_spec.has_region_y) panel.set_region_y(panel_spec.region_y);
            if (options.label_width >= 0.0) panel.set_label_width(options.label_width);
            panel.set_show_grid(options.grid.value_or(!square));
            if (panel_spec.has_title) {
                panel.set_title(panel_spec.title);
            } else if (options.panels.size() > 1) {
                panel.set_title(format_region(panel_spec.region.chrom, panel_spec.region.start,
                                              panel_spec.region.end));
            }

            panel.add_track(AxisTrack{}.show_region(!square));
            for (std::size_t i = 0; i < options.tracks.size(); ++i) {
                const TrackSpec& spec = options.tracks[i];
                if (spec.panel_index != p ||
                    (spec.kind == TrackKind::bedpe && !is_arc_track(spec))) {
                    continue;
                }
                const bool on_x = spec.axis != TrackAxis::y || !(square || rectangle);
                if (on_x) panel.add_track(build_track(spec, sources[i], /*vertical=*/false));
            }

            HeatmapTrack* map = nullptr;
            if (square || rectangle) {
                if (rectangle) panel.set_matrix_height(options.map_height.value_or(240.0));
                panel.add_y_track(AxisTrack{}.position(AxisPosition::bottom).height(26.0));
                for (std::size_t i = 0; i < options.tracks.size(); ++i) {
                    const TrackSpec& spec = options.tracks[i];
                    if (spec.panel_index != p ||
                        (spec.kind == TrackKind::bedpe && !is_arc_track(spec)) ||
                        spec.axis == TrackAxis::x) {
                        continue;
                    }
                    panel.add_y_track(build_track(spec, sources[i], /*vertical=*/true));
                }
                if (!options.no_map) {
                    map = static_cast<HeatmapTrack*>(&panel.set_matrix(build_map(
                        options, matrix, comparison,
                        rectangle ? HeatmapMode::rectangle : HeatmapMode::square, shared_primary,
                        shared_comparison)));
                }
                panel.add_bottom_track(AxisTrack{}.position(AxisPosition::bottom));
            } else if (!options.no_map) {
                map = static_cast<HeatmapTrack*>(&panel.add_track(build_map(
                    options, matrix, comparison, HeatmapMode::triangle, shared_primary,
                    shared_comparison)));
            }

            if (map != nullptr) {
                maps.push_back(map);
                for (std::size_t i = 0; i < options.tracks.size(); ++i) {
                    if (options.tracks[i].panel_index == p &&
                        options.tracks[i].kind == TrackKind::bedpe &&
                        !is_arc_track(options.tracks[i])) {
                        map->add_annotation(build_annotation(options.tracks[i], sources[i]));
                    }
                }
                for (const HighlightSpec& spec : options.highlights) {
                    if (spec.panel_index != p) continue;
                    MapHighlight highlight{spec.region, spec.axis};
                    highlight.fill(spec.fill)
                        .border(spec.border, spec.line_width)
                        .dash(spec.dash)
                        .expand(spec.expansion);
                    map->add_highlight(std::move(highlight));
                }

                const std::string primary_title = options.oe ? "obs/exp"
                                                  : options.map_log.value_or(true)
                                                      ? "contacts (log)"
                                                      : "contacts";
                if (comparison != nullptr) {
                    panel.add_bottom_track(
                        ColorBarTrack{*map, HeatmapLayer::primary}
                            .title(basename(options.hic_path) + " — " + primary_title)
                            .align(BarAlign::left)
                            .bar_length(110.0));
                    const std::string comparison_title =
                        options.comparison_oe ? "obs/exp"
                        : options.comparison_log.value_or(true) ? "contacts (log)"
                                                                : "contacts";
                    panel.add_bottom_track(
                        ColorBarTrack{*map, HeatmapLayer::comparison}
                            .title(basename(options.comparison_path) + " — " + comparison_title)
                            .align(BarAlign::left)
                            .bar_length(110.0));
                } else {
                    panel.add_bottom_track(ColorBarTrack{*map}
                                               .title(primary_title)
                                               .align(BarAlign::right)
                                               .bar_length(110.0));
                }
            } else {
                const bool has_overlay = std::any_of(
                    options.tracks.begin(), options.tracks.end(), [p](const TrackSpec& spec) {
                        return spec.panel_index == p && spec.kind == TrackKind::bedpe &&
                               !is_arc_track(spec);
                    });
                const bool has_highlight = std::any_of(
                    options.highlights.begin(), options.highlights.end(),
                    [p](const HighlightSpec& spec) { return spec.panel_index == p; });
                if (has_overlay || has_highlight) {
                    throw Error(ErrorCode::invalid_argument,
                                "BEDPE overlays and highlights require a contact map");
                }
            }
        }

        for (const std::string& format : options.formats) {
            const std::string path = options.output + "." + format;
            if (format == "png") {
                figure.save_png(path, options.dpi);
            } else if (format == "pdf") {
                figure.save_pdf(path);
            } else if (format == "svg") {
                figure.save_svg(path);
            } else {
                throw Error(ErrorCode::invalid_argument, "unknown output format '" + format + "'");
            }
            std::printf("wrote %s\n", path.c_str());
        }

        for (std::size_t p = 0; p < maps.size(); ++p) {
            const MatrixData& data = maps[p]->data();
            std::size_t populated = 0;
            for (float v : data.values) {
                if (std::isfinite(v) && v != 0.0F) ++populated;
            }
            std::printf("map %zu: %zu x %zu bins at %lld bp, %zu non-empty\n", p + 1,
                        data.width, data.height, static_cast<long long>(data.bin_size), populated);
            if (maps[p]->has_comparison()) {
                const MatrixData& other = maps[p]->comparison_data();
                std::size_t other_populated = 0;
                for (float v : other.values) {
                    if (std::isfinite(v) && v != 0.0F) ++other_populated;
                }
                std::printf("comparison map %zu: %zu x %zu bins at %lld bp, %zu non-empty\n",
                            p + 1, other.width, other.height,
                            static_cast<long long>(other.bin_size), other_populated);
            }
            if (populated == 0) {
                std::fprintf(stderr, "warning: no contacts in this region\n");
            }
        }
        const Size size = figure.last_rendered_size().has_value()
                              ? *figure.last_rendered_size()
                              : figure.computed_size();
        std::printf("page: %.0f x %.0f pt\n", size.width, size.height);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
    return 0;
}
