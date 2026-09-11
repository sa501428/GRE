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

enum class TrackKind { automatic, signal, gene, interval };
enum class TrackAxis { x, y, both };

struct TrackSpec {
    std::string path;
    TrackKind kind{TrackKind::automatic};
    TrackAxis axis{TrackAxis::x};

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
    std::optional<SignalStyle> style;
    std::optional<std::string> colormap;
    std::optional<double> row_height;
    std::optional<double> line_width;
    std::optional<double> baseline;
    std::optional<std::string> aggregate;
    bool log{false};
    bool y_axis{false};
};

struct Options {
    std::string hic_path;
    GenomicRegion region;
    GenomicRegion region_y;
    bool has_region_y{false};

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

    std::vector<TrackSpec> tracks;
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

bool parse_region(const std::string& text, GenomicRegion& out) {
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
        ends_with(name, ".gff3") || ends_with(name, ".gtf") || ends_with(name, ".bb") ||
        ends_with(name, ".bigbed") || ends_with(name, ".genepred")) {
        return TrackKind::gene;
    }
    if (ends_with(name, ".narrowpeak") || ends_with(name, ".broadpeak") ||
        ends_with(name, ".peak")) {
        return TrackKind::interval;
    }
    return TrackKind::automatic;
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
        "BED/GFF/GTF/bigBed gene tracks, narrowPeak/broadPeak interval tracks.\n"
        "\n"
        "Global:\n"
        "  --region CHR:START-END   region to plot (also accepted bare)\n"
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
        "  --no-map                 tracks only, no contact map\n"
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
        "  --style line|area|bars|points       default area\n"
        "  --aggregate mean|max|min|sum        binning, default mean\n"
        "  --line-width PT\n"
        "  --y-axis                 ticked y axis instead of a range label\n"
        "  --no-range-label\n"
        "  --colormap NAME          colour intervals by score\n"
        "  --no-labels              hide gene names\n"
        "  --row-height PT          gene track row pitch\n"
        "  --type signal|gene|interval         override the extension guess\n"
        "  --axis x|y|both          square layout: which axis (default x)\n",
        program);
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
                out.region = region;
                continue;
            }
            TrackSpec spec;
            spec.path = argument;
            spec.kind = kind_from_extension(argument);
            if (spec.kind == TrackKind::automatic) {
                throw Error(ErrorCode::invalid_argument,
                            "cannot tell what '" + argument +
                                "' is from its extension; add --type signal|gene|interval");
            }
            out.tracks.push_back(std::move(spec));
            current = &out.tracks.back();
            continue;
        }

        // ---- global ----
        if (argument == "--region") {
            if (!parse_region(value("--region"), out.region)) {
                throw Error(ErrorCode::invalid_argument, "--region wants CHR:START-END");
            }
        } else if (argument == "--region-y") {
            if (!parse_region(value("--region-y"), out.region_y)) {
                throw Error(ErrorCode::invalid_argument, "--region-y wants CHR:START-END");
            }
            out.has_region_y = true;
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
        } else if (argument == "--no-map") {
            out.no_map = true;

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
            track_option("--style").style = parse_style(value("--style"));
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
        } else if (argument == "--row-height") {
            track_option("--row-height").row_height = std::stod(value("--row-height"));
        } else if (argument == "--type") {
            const std::string kind = lower(value("--type"));
            TrackSpec& spec = track_option("--type");
            if (kind == "signal") {
                spec.kind = TrackKind::signal;
            } else if (kind == "gene") {
                spec.kind = TrackKind::gene;
            } else if (kind == "interval") {
                spec.kind = TrackKind::interval;
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
};

LoadedSource load(const TrackSpec& spec) {
    LoadedSource loaded;
    if (spec.kind == TrackKind::signal) {
        auto source = IgvSignalSource::open(spec.path);
        source->fill_empty(0.0);
        if (spec.aggregate.has_value()) source->aggregation(parse_aggregation(*spec.aggregate));
        loaded.signal = std::make_shared<CachingSignalSource>(source);
    } else {
        loaded.features = std::make_shared<CachingFeatureSource>(IgvFeatureSource::open(spec.path));
    }
    return loaded;
}

// Builds one track from a spec.  `vertical` tracks are quarter-turned by the
// layout, so they drop the bits that only make sense across a wide row.
std::unique_ptr<Track> build_track(const TrackSpec& spec, const LoadedSource& source,
                                   bool vertical) {
    const std::string name = spec.name.value_or(stem(spec.path));

    if (spec.kind == TrackKind::signal) {
        auto track = std::make_unique<SignalTrack>(source.signal);
        track->name(name);
        track->style(spec.style.value_or(SignalStyle::area));
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

std::unique_ptr<HeatmapTrack> build_map(const Options& options, const MatrixSourcePtr& source,
                                        HeatmapMode mode) {
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

        if (options.region.chrom.empty()) options.region = default_region(*matrix);
        std::printf("region: %s\n", format_region(options.region.chrom, options.region.start,
                                                  options.region.end)
                                        .c_str());

        const bool square = options.layout == "square";
        const bool rectangle = options.layout == "rectangle" || options.layout == "rect";
        if (!square && !rectangle && options.layout != "pyramid" &&
            options.layout != "triangle") {
            throw Error(ErrorCode::invalid_argument,
                        "--layout wants square, pyramid or rectangle");
        }

        Figure figure;
        figure.set_theme(Theme::named(options.theme));
        const double width = options.width_inches > 0.0 ? inches(options.width_inches)
                                                        : inches(square ? 7.5 : 7.0);
        figure.set_size(width, options.height_inches > 0.0 ? inches(options.height_inches) : 0.0);
        figure.set_title(options.has_title
                             ? options.title
                             : format_region(options.region.chrom, options.region.start,
                                             options.region.end));
        figure.set_subtitle(options.has_subtitle
                                ? options.subtitle
                                : basename(options.hic_path) + "   " + straw.matrix_type + " / " +
                                      straw.normalization);

        Panel& panel = figure.add_panel();
        panel.set_region(options.region);
        if (options.has_region_y) panel.set_region_y(options.region_y);
        if (options.label_width >= 0.0) panel.set_label_width(options.label_width);
        panel.set_show_grid(options.grid.value_or(!square));

        // One reader per file, shared by the horizontal and vertical view of it.
        std::vector<LoadedSource> sources;
        sources.reserve(options.tracks.size());
        for (const TrackSpec& spec : options.tracks) sources.push_back(load(spec));

        panel.add_track(AxisTrack{}.show_region(!square));
        for (std::size_t i = 0; i < options.tracks.size(); ++i) {
            const TrackSpec& spec = options.tracks[i];
            const bool on_x = spec.axis != TrackAxis::y || !square;
            if (on_x) panel.add_track(build_track(spec, sources[i], /*vertical=*/false));
        }

        HeatmapTrack* map = nullptr;
        if (square) {
            panel.add_y_track(AxisTrack{}.position(AxisPosition::bottom).height(26.0));
            for (std::size_t i = 0; i < options.tracks.size(); ++i) {
                const TrackSpec& spec = options.tracks[i];
                if (spec.axis == TrackAxis::x) continue;
                panel.add_y_track(build_track(spec, sources[i], /*vertical=*/true));
            }
            if (!options.no_map) {
                map = static_cast<HeatmapTrack*>(
                    &panel.set_matrix(build_map(options, matrix, HeatmapMode::square)));
            }
            panel.add_bottom_track(AxisTrack{}.position(AxisPosition::bottom));
        } else if (!options.no_map) {
            const HeatmapMode mode =
                rectangle ? HeatmapMode::rectangle : HeatmapMode::triangle;
            map = static_cast<HeatmapTrack*>(
                &panel.add_track(build_map(options, matrix, mode)));
        }

        if (map != nullptr) {
            panel.add_bottom_track(ColorBarTrack{*map}
                                       .title(options.oe ? "obs/exp"
                                              : options.map_log.value_or(true)
                                                  ? "contacts (log)"
                                                  : "contacts")
                                       .align(BarAlign::right)
                                       .bar_length(110.0));
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

        if (map != nullptr) {
            const MatrixData& data = map->data();
            std::size_t populated = 0;
            for (float v : data.values) {
                if (std::isfinite(v) && v != 0.0F) ++populated;
            }
            std::printf("map: %zu x %zu bins at %lld bp, %zu non-empty\n", data.width,
                        data.height, static_cast<long long>(data.bin_size), populated);
            if (populated == 0) {
                std::fprintf(stderr, "warning: no contacts in this region\n");
            }
        }
        const Size size = figure.computed_size();
        std::printf("page: %.0f x %.0f pt\n", size.width, size.height);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
    return 0;
}
