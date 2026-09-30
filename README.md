# GRE — genomics rendering engine

A small C++20 library that turns genomic data and a concise figure
specification into publication-quality static PNG, PDF and SVG.

It is not a genome browser: no interaction, no viewport state, no streaming.
It reads through the adapters in `gre/data/`, lays out a stack of tracks, and
draws.

```cpp
#include <gre/gre.hpp>
using namespace gre;

auto hic  = StrawMatrixSource::open("sample.hic");
auto atac = IgvSignalSource::open("atac.bigWig");
auto genes = IgvFeatureSource::open("genes.bed");

Figure fig;
fig.set_width(inches(6.5)).set_theme(Theme::publication());

Panel& panel = fig.add_panel();
panel.set_region("chr8", 127'000'000, 129'000'000);
panel.add_track(AxisTrack{});
panel.add_track(SignalTrack{atac}.name("ATAC").height(40));
panel.add_track(GeneTrack{genes}.name("Genes"));
panel.add_track(HeatmapTrack{hic}.mode(HeatmapMode::triangle).log_scale(true));

fig.save_png("figure.png", 300);
fig.save_pdf("figure.pdf");
```

## What it does

- **2D contact maps** in three arrangements: `square` (Juicebox-style),
  `triangle` (the rotated pyramid that sits above 1D tracks), and `rectangle`
  for off-diagonal and inter-chromosomal blocks.
- **1D signal tracks** — line, filled area, bars, points — requested at roughly
  one bin per output pixel rather than at the file's native resolution.
- **Gene models** with exons, coding ranges, strand arrows and automatic row
  packing; **interval tracks** for peaks, domains and states.
- **Split/VS maps** with independent matrices and colour scales above and below
  the diagonal; **BEDPE overlays** for loops, boxes and TAD/domain outlines.
- **Map highlights** projected as vertical and/or horizontal genomic bands,
  with optional translucent fill, borders and dash patterns.
- **Axes, colour bars, legends, grids, themes**, multiple panels.
- **PNG, PDF and SVG** from one figure definition, at any resolution.

## ENCODE example gallery

The gallery below is rendered from public ENCODE URLs, using K562 intact Hi-C.
GRE range-queries the 33.8 GB `.hic` and indexed bigWig/bigBed inputs in place; it does not download
those files in full. Each figure's exact command, input accession table, direct
URLs, interpretation, and regeneration instructions are in the
[complete gallery](examples/gallery/README.md). Run all ten with
`./examples/gallery/generate.sh`.

### Pyramid with 1D tracks, domains, and score-styled loops

![K562 pyramid with ENCODE compartment, H3K27ac, promoter, domain, and loop annotations](examples/gallery/generated/01_pyramid_tracks.png)

### Square map with both-axis PC1, virtual 4C, and rich 2D overlays

![K562 square map with both-axis PC1, virtual 4C, domains, loops, and viewpoint highlight](examples/gallery/generated/02_square_rich_annotations.png)

### VS mode: mapQ30 above and all contacts below

![K562 intact Hi-C split map with mapQ30 and all contacts on linear scales](examples/gallery/generated/03_intact_mapq30_vs_all.png)

### Three square panels on a jointly fitted contact scale

![K562 vertically stacked square maps with a shared map scale](examples/gallery/generated/04_multi_panel_shared_scale.png)

### Dark off-diagonal rectangular block

![K562 dark off-diagonal contact block with loop boxes and highlights](examples/gallery/generated/05_off_diagonal_rectangle.png)

### BEDPE loops as 1D arcs with genes and quantitative tracks

![K562 loop arcs, transcript models, H3K27ac and ATAC above a blue pyramid map](examples/gallery/generated/06_arc_loops_and_genes.png)

### Dark tracks-only composition with alternate 1D styles

![K562 H3K27ac area, ATAC bars, CTCF points, peaks, and promoters](examples/gallery/generated/07_one_dimensional_tracks.png)

### Intact Hi-C at 5 kb in viridis

![K562 intact Hi-C 5 kb square contact map in viridis](examples/gallery/generated/08_intact_5kb_viridis.png)

### Intact Hi-C at 2 kb in blues

![K562 intact Hi-C 2 kb square contact map in blues](examples/gallery/generated/09_intact_2kb_blues.png)

### Intact Hi-C at 50 kb in reds

![K562 intact Hi-C 50 kb pyramid contact map in reds](examples/gallery/generated/10_intact_50kb_reds.png)

## Square (Juicebox-style) layout

Setting a matrix track switches a panel to a square arrangement with tracks on
both axes. Horizontal tracks stack above the map; `add_y_track` tracks are
quarter-turned and run down its left side over the y region. The same source
can feed both.

```
[ x tracks, full map width        ]
[ y ][ y ][   square contact map  ]
[ bottom tracks: axis, colour bar ]
```

```cpp
Panel& panel = fig.add_panel();
panel.set_region("chr1", 20'000'000, 22'000'000);

panel.add_track(AxisTrack{});
panel.add_track(SignalTrack{atac}.name("ATAC").height(30));

// In a y track, height() is the column width.
panel.add_y_track(AxisTrack{}.position(AxisPosition::bottom).height(26));
panel.add_y_track(SignalTrack{atac}.name("ATAC").height(30));

HeatmapTrack& map = panel.set_matrix(
    HeatmapTrack{hic}.mode(HeatmapMode::square).log_scale(true).colors("fall"));

panel.add_bottom_track(AxisTrack{}.position(AxisPosition::bottom));
panel.add_bottom_track(ColorBarTrack{map}.title("contacts (log)"));
```

## Split/VS maps and 2D annotations

A square map can combine two independent matrix sources. The primary source is
drawn in one triangle and the comparison source in the other. The sources may
use different normalization, matrix type, resolution, colour map and value
scale.

```cpp
auto control = StrawMatrixSource::open("control.hic");
auto treated = StrawMatrixSource::open("treated.hic");
auto loops = BedpeSource::open("loops.bedpe.gz");

HeatmapTrack map{control};
map.mode(HeatmapMode::square)
   .colors("reds")
   .compare_with(treated, MatrixHalf::below)
   .comparison_colors("blues");

map.add_annotation(PairAnnotationLayer{loops}
    .style(PairAnnotationStyle::loop)
    .side(AnnotationSide::both)
    .color(color_from_hex("#202020"))
    .dashed()
    .expand(5'000));

map.add_highlight(MapHighlight{{"chr1", 21'000'000, 21'200'000},
                               HighlightAxis::vertical}
    .fill(color_from_hex("#FFD70030"))
    .border(color_from_hex("#A07000")));

panel.set_matrix(std::move(map));
```

`PairAnnotationStyle::loop` draws a circle at each anchor intersection,
`box` draws a rectangle, and `domain` draws a triangular domain outline against
the diagonal. `AnnotationSide::above`, `below`, and `both` control which half
of a square map receives intra-chromosomal annotations. In pyramid layout the
single visible upper triangle is used; a `below`-only layer is therefore not
drawn.

Highlights are vector overlays. Vertical highlights work in every layout;
horizontal genomic highlights work in square and rectangle layouts. A pyramid's
vertical coordinate is contact distance rather than a genomic y axis, so GRE
does not draw a misleading horizontal genomic band there.

## Building

Requires CMake ≥ 3.24, a C++20 compiler, zlib, and the two sibling data
libraries — [straw](../straw) (curl, zstd) and [igv-cpp](../igv-cpp) (htslib).
Both are consumed from their source trees, so point CMake at them if they are
not next to this directory.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build
```

`-DGRE_STRAW_DIR=` and `-DGRE_IGV_DIR=` override the sibling paths.

To use it from another CMake project:

```cmake
add_subdirectory(path/to/GRE)
target_link_libraries(my_target PRIVATE gre::gre)
```

`add_subdirectory` is the supported integration. An installed `libgre` would
not be self-contained, because GRE does not install straw or igv-cpp.

## gre_plot

`gre_plot` composes a figure from a local or HTTP(S) `.hic` file, any number of 1D tracks,
matrix-derived virtual 4C profiles, BEDPE overlays, and multiple vertically
stacked genomic panels.
Options that follow a track file apply to that track; everything else is
global. `--help` lists them all.

```bash
./build/tools/gre_plot sample.hic chr2:40000000-44000000 \
    --layout square --norm SCALE --out figure \
    compartment.bedGraph --name ECHO --height 34 \
    eigen.wig --name POSSUM2 --height 34 --percentile 0.999 \
    genes.bed --name Genes --no-labels
```

A split map with loops, a TAD layer, and a highlighted interval can be produced
directly from the CLI:

```bash
./build/tools/gre_plot control.hic chr2:40000000-44000000 \
    --layout square --norm SCALE \
    --vs treated.hic --vs-norm SCALE --vs-side below \
    --map-colors reds --vs-map-colors blues \
    loops.bedpe --style loop --side both --dashed --expand 5kb \
    domains.bedpe --style domain --side above --color '#333333' \
    --v-highlight chr2:41500000-41750000 \
    --highlight-color '#FFD70030' --highlight-dashed
```

### How command lines are interpreted

The first positional argument is always the primary `.hic` file. A later bare
`CHR:START-END` token sets the x region; other bare paths create tracks or
BEDPE layers according to their extension.

Global options may appear anywhere. Per-track options apply to the most
recently named track file:

```bash
# --name and --color apply only to atac.bigWig. --no-labels applies only to
# genes.gtf. --map-colors remains global even though it appears last.
./build/tools/gre_plot sample.hic chr8:127Mb-129Mb \
    atac.bigWig --name ATAC --color '#CC0000' \
    genes.gtf --name Genes --no-labels \
    --map-colors fall
```

BEDPE files are not allocated a separate row. They become vector layers inside
the contact-map rectangle. Likewise, `--highlight-*` styling applies to the
most recently declared `--v-highlight`, `--h-highlight`, or `--xy-highlight`:

```bash
./build/tools/gre_plot sample.hic chr1:10Mb-14Mb --layout square \
    --v-highlight chr1:11Mb-11.2Mb \
        --highlight-color '#FFD70030' --highlight-border '#A07000' \
    --h-highlight chr1:12.5Mb-12.7Mb \
        --highlight-color '#00A0FF28' --highlight-dash 5,2
```

`--panel REGION` begins a new panel scope. Tracks, BEDPE layers, virtual 4C
profiles, and highlights declared after it belong only to that panel. Global
map, theme, export, and VS options continue to apply to every panel. The first
bare region or `--region` describes panel 1:

```bash
./build/tools/gre_plot sample.hic chr1:20Mb-24Mb \
    peaks.bigWig --name ATAC \
    --panel chr8:127Mb-129Mb --panel-title MYC \
    genes.gtf --name Genes \
    --panel chr12:10Mb-12.8Mb --panel-title CDK2 \
    loops.bedpe --style loop
```

`--virtual4c VIEWPOINT` creates a signal track and becomes the current track,
just as if a bigWig path had been supplied. Therefore `--name`, `--style`,
`--color`, `--height`, `--aggregate`, `--min`, and the other signal options
immediately following it style that profile.

Lengths accept raw bases or `bp`, `kb`, `k`, `Mb`, and `m` suffixes. Genomic
coordinates may contain commas or underscores. Colours accept `#rgb`,
`#rrggbb`, or `#rrggbbaa`; the last form includes alpha, so quote it in a shell
when it begins with `#`.

### Common CLI recipes

#### Rotated pyramid with a limited contact distance

The pyramid's top edge is the diagonal and distance from the diagonal runs
downward. `--max-distance` reduces its depth and therefore its page height.

```bash
./build/tools/gre_plot sample.hic chr8:127Mb-129Mb \
    --layout pyramid --norm KR --max-distance 500kb \
    atac.bigWig --name ATAC --style area --height 36 \
    genes.gtf --name Genes --row-height 13 \
    --formats pdf,png --dpi 450 --out chr8_pyramid
```

#### Square map with symmetric x/y tracks

`--axis both` reads a file once and puts it above and to the left of a square
map. `--axis x` and `--axis y` restrict it to one side.

```bash
./build/tools/gre_plot sample.hic chr1:20Mb-24Mb \
    --layout square --norm SCALE --diagonal \
    eigenvector.bedGraph --name 'PC1' --axis both --height 30 \
        --style area --color '#CC0000' --neg-color '#0047AB' \
    insulation.bigWig --name Insulation --axis x --height 34 \
    genes.gtf --name Genes --axis x --no-labels \
    --width 7.5 --formats pdf,svg --out square_tracks
```

#### Off-diagonal and inter-chromosomal blocks

The bare region is x; `--region-y` supplies y. Rectangle mode does not assume a
symmetric matrix and therefore also works across chromosomes.

```bash
./build/tools/gre_plot sample.hic chr1:10Mb-15Mb \
    --layout rectangle --region-y chr2:30Mb-35Mb \
    --norm KR --map-height 260 --map-linear \
    --title 'chr1–chr2 contacts' --out trans_block
```

For an off-diagonal block on one chromosome:

```bash
./build/tools/gre_plot sample.hic chr4:20Mb-25Mb \
    --layout rectangle --region-y chr4:60Mb-65Mb \
    --resolution 25kb --map-colors magma
```

#### Observed/expected maps

Observed/expected defaults to a logarithmic ratio scale from 0.25 to 4 and a
diverging colour map centred on 1. Override those limits when a figure series
must use an identical scale.

```bash
./build/tools/gre_plot sample.hic chr3:40Mb-50Mb \
    --oe --norm KR --map-colors rd_bu \
    --map-min 0.5 --map-max 2 \
    --title 'Observed / expected' --out chr3_oe
```

#### Two samples above and below the diagonal

VS mode is square-only and requires identical x/y regions. By default the
primary file occupies the upper triangle and `--vs` occupies the lower. Each
half is queried and fitted independently, and different stored resolutions are
registered by genomic coordinate.

```bash
./build/tools/gre_plot control.hic chr2:40Mb-44Mb \
    --layout square --norm SCALE --resolution 10kb \
    --map-colors reds --map-percentile 0.995 \
    --vs treated.hic --vs-side below --vs-norm SCALE --vs-resolution 25kb \
    --vs-map-colors blues --vs-map-percentile 0.995 \
    --diagonal --out control_vs_treated
```

To place the comparison above the diagonal:

```bash
./build/tools/gre_plot control.hic chr2:40Mb-44Mb --layout square \
    --vs treated.hic --vs-side above
```

The same file can be opened twice to compare observed contacts with
observed/expected. This is a split display, not an arithmetic difference map.

```bash
./build/tools/gre_plot sample.hic chr6:20Mb-24Mb --layout square \
    --norm KR --map-colors reds \
    --vs sample.hic --vs-norm KR --vs-oe --vs-map-colors rd_bu \
    --vs-map-min 0.5 --vs-map-max 2
```

#### Several loci with one jointly fitted colour scale

Each `--panel` starts another vertically stacked genomic view. Map settings are
global, while tracks and annotations are panel-local. `--shared-map-scale`
collects finite matrix values from every panel and fits one percentile range,
so equal colours mean equal values across loci. In VS mode, all primary halves
share one scale and all comparison halves share a second scale; primary and VS
are not pooled together.

```bash
./build/tools/gre_plot sample.hic chr1:20Mb-24Mb \
    --layout square --norm SCALE --map-percentile 0.995 \
    --shared-map-scale --panel-title 'Locus A' \
    loops_a.bedpe --style loop --side above \
    --panel chr8:127Mb-129Mb --panel-title 'Locus B' \
    loops_b.bedpe --style loop --side above \
    --panel chr12:10Mb-13Mb --panel-title 'Locus C' \
    --panel-spacing 18 --formats pdf,png --out three_loci
```

Tracks must be repeated if they should appear in more than one panel. This is
intentional: each occurrence can have a panel-specific label, range, or style.

```bash
./build/tools/gre_plot sample.hic chr1:20Mb-24Mb \
    genes.gtf --name Genes --no-labels \
    --panel chr8:127Mb-129Mb \
    genes.gtf --name Genes --no-labels \
    --shared-map-scale
```

Without `--shared-map-scale`, each panel fits its own range. Explicit
`--map-min`/`--map-max` still work and are naturally identical across panels;
the shared flag is most useful when one or both limits remain automatic.

#### Virtual 4C directly from the displayed matrix source

Virtual 4C is a direct viewpoint extraction, not a peak caller or aggregate
analysis. GRE queries the rectangle formed by the display region and the
viewpoint, then reduces only the matrix rows covered by the supplied viewpoint
interval. It uses the primary `.hic` source with the same `--norm`, `--oe`, and
`--resolution` settings as the map.

```bash
./build/tools/gre_plot sample.hic chr3:45Mb-49Mb \
    --norm KR --resolution 10kb \
    --virtual4c chr3:46.20Mb-46.25Mb \
        --name 'Viewpoint: promoter' --style line --color '#2C7FB8' \
        --height 42 --line-width 1.4 \
    --v-highlight chr3:46.20Mb-46.25Mb \
        --highlight-color '#2C7FB820' --highlight-border '#2C7FB8'
```

The default reduction is the mean across viewpoint bins. `--aggregate sum`
gives total contacts and `--aggregate max` gives the strongest viewpoint bin:

```bash
./build/tools/gre_plot sample.hic chr11:62Mb-66Mb --layout square \
    --virtual4c chr11:64.05Mb-64.15Mb --aggregate sum \
        --name '100 kb viewpoint (sum)' --style area --axis x \
        --color '#7A0177' --height 36
```

In square layout a virtual 4C track also accepts `--axis x|y|both`. A y-axis
profile is queried over the panel's y region after the track is rotated.

#### Loops and TADs from BEDPE

Layers are drawn in input order after highlights, so later BEDPE files appear
over earlier ones. `--side` applies to intra-chromosomal square maps. Rectangle
mode instead orients each pair against the x and y regions; pyramid mode has a
single upper-triangle projection and suppresses a `below`-only layer.

```bash
./build/tools/gre_plot sample.hic chr2:40Mb-44Mb --layout square \
    loops.bedpe.gz --style loop --side both \
        --color '#111111' --fill '#FFFFFF60' --line-width 1.2 \
    domains.bedpe --style domain --side above \
        --color '#6A3D9A' --dashed --dash 5,2 --expand 5kb \
    selected_loops.bedpe --style box --side below \
        --color '#FF7F00' --labels
```

For a pyramid:

```bash
./build/tools/gre_plot sample.hic chr10:90Mb-94Mb --layout pyramid \
    loops.bedpe --style loop --color '#202020' \
    domains.bedpe --style domain --color '#7B3294' --dashed
```

BEDPE column 8 scores can drive filtering and several visual channels. Here,
scores below 5 are removed; the remaining score range is fitted over visible
records and mapped simultaneously to viridis colour, 20–100% opacity, 0.5–2.5
pt outlines, and 0.7–1.6× marker size:

```bash
./build/tools/gre_plot sample.hic chr2:40Mb-44Mb --layout square \
    loops.bedpe --style loop --side both --fill '#FFFFFF70' \
        --score-filter-min 5 --colormap viridis --percentile 0.99 \
        --score-opacity 0.2,1 --score-line-width 0.5,2.5 \
        --score-size 0.7,1.6
```

Use `--min` and `--max` after a BEDPE file to fix the score mapping across
panels or samples. A recognizable per-record itemRgb is used when neither
`--color` nor `--colormap` is supplied. An explicit `--color` gives the layer
one colour; `--colormap` maps scores to colours.
Records without a finite score keep the ordinary layer colour and dimensions,
unless any score filter is supplied, in which case unscored records are
excluded because they cannot satisfy the filter.

#### Highlighting loci, rows, and columns

Alpha colours preserve the heatmap beneath a highlight. A custom dash pattern
is a comma-separated sequence of lengths in PDF points.

```bash
./build/tools/gre_plot sample.hic chr5:70Mb-75Mb --layout square \
    --v-highlight chr5:71Mb-71.2Mb \
        --highlight-color '#FFD70030' --highlight-border '#A07000' \
        --highlight-width 1.0 \
    --h-highlight chr5:73Mb-73.1Mb \
        --highlight-color '#00A0FF28' --highlight-dash 6,3 \
        --highlight-expand 10kb \
    --xy-highlight chr5:74Mb-74.05Mb \
        --highlight-color '#FF000020'
```

A pyramid can show vertical genomic highlights, but not horizontal genomic
highlights: its visual y coordinate is contact distance rather than a second
genomic axis.

#### Tracks only

`--no-map` produces a conventional stacked-track figure. BEDPE arc tracks are
allowed; 2D BEDPE overlays and map highlights are rejected because there is no
2D drawing area.

```bash
./build/tools/gre_plot sample.hic chr12:10Mb-12Mb --no-map \
    atac.bigWig --name ATAC --style area --height 40 \
    chip.bigWig --name H3K27ac --style line --height 34 \
    peaks.narrowPeak --name Peaks --type interval --colormap viridis \
    genes.gtf --name Genes --out tracks_only
```

#### Reproducible publication exports

PDF and SVG keep axes, text, genes, highlights, and annotations as vectors; the
dense matrix remains one embedded raster image. DPI affects PNG and the
sampling resolution requested from the matrix source.

```bash
./build/tools/gre_plot sample.hic chr7:50Mb-54Mb \
    --norm SCALE --resolution 10kb --map-percentile 0.99 \
    --formats png,pdf,svg --dpi 600 \
    --width 7.2 --theme publication \
    --title 'MYC locus' --subtitle 'SCALE, 10 kb' \
    --out figure_2a
```

Track files are recognised by extension — bigWig/bedGraph/wig become signal
tracks, BED/GFF/GTF/genePred/bigBed gene tracks, narrowPeak/broadPeak interval
tracks, and `.bedpe`/`.bedpe.gz` files become 2D map overlays by default or 1D
rows with `--style arc`. `--type` overrides the guess.

Public HTTP(S) URLs can be used anywhere a path is accepted. `.hic`, bigWig,
bigBed, and other indexed binary formats require a server that honors byte
range requests; text/gzip BEDPE, BED, GFF/GTF, bedGraph, and wig inputs are
streamed through their readers. Quote URLs in shell commands.

### Figure options

| flag | meaning | default |
|---|---|---|
| `--region CHR:START-END` | region to plot; also accepted as a bare argument | a window ¼ along the first contig |
| `--panel CHR:START-END` | begin another vertically stacked panel; following tracks/annotations are scoped to it | none |
| `--panel-title TEXT` | title of the current panel | region, in a multi-panel figure |
| `--panel-spacing PT` | vertical gap between panels | from the theme |
| `--region-y CHR:START-END` | second axis, for an off-diagonal block | same as `--region` |
| `--layout square\|pyramid\|rectangle` | square is Juicebox-style with tracks on both axes; pyramid is the rotated triangle under 1D tracks; rectangle is an arbitrary x-by-y block | `pyramid` |
| `--out PREFIX` | output name stem | `figure` |
| `--formats png,pdf,svg` | which files to write | `png,pdf` |
| `--dpi N` | raster resolution | `300` |
| `--width INCHES` | page width | `7`, or `7.5` for square |
| `--height INCHES` | page height, before any track file | fit the tracks |
| `--theme light\|dark\|publication` | colours, type sizes and spacing | `publication` |
| `--title TEXT` | headline | the region |
| `--subtitle TEXT` | second line | file, matrix type and normalization |
| `--grid` / `--no-grid` | vertical rules behind the tracks | on except in square layout |
| `--label-width PT` | width of the track-name gutter | from the theme |

### Contact map options

| flag | meaning | default |
|---|---|---|
| `--norm NAME` | `NONE`, `VC`, `VC_SQRT`, `KR`, `SCALE` — whatever the file carries | `NONE` |
| `--oe` | observed/expected instead of observed | off |
| `--resolution BP` | force a stored resolution; accepts `5000`, `5kb`, `2.5Mb` | the coarsest that still fills the width |
| `--map-colors NAME` | colour map (see below) | `juicebox`, or `rd_bu` with `--oe` |
| `--map-min V` / `--map-max V` | fix the colour range | fitted from the data |
| `--map-percentile P` | clip the top of the range; contact matrices have a long tail | `0.99` |
| `--map-log` / `--map-linear` | value scaling | log |
| `--map-height PT` | map height, for rectangle layout | `240` |
| `--max-distance BP` | pyramid: how far from the diagonal to show | the whole region |
| `--diagonal` | draw the diagonal in square layout | off |
| `--map-border HEX` | outline the map | none |
| `--shared-map-scale` | jointly fit automatic map limits across all panels; VS layers form a separate shared group | off |
| `--vs FILE.hic` | use a second matrix in one triangle of a square map | none |
| `--vs-side above\|below` | triangle occupied by the second matrix | `below` |
| `--vs-norm NAME` | normalization for the second matrix | primary `--norm` |
| `--vs-oe` | use observed/expected for the second matrix | off |
| `--vs-resolution BP` | force the second matrix resolution | primary `--resolution` |
| `--vs-map-colors NAME` | second matrix colour map | `juicebox`, or `rd_bu` with `--vs-oe` |
| `--vs-map-min V` / `--vs-map-max V` | second matrix colour range | fitted independently |
| `--vs-map-percentile P` | second matrix upper clipping percentile | `0.99` |
| `--vs-map-log` / `--vs-map-linear` | second matrix scaling | log unless `--vs-oe` |
| `--v-highlight REGION` | add a vertical map highlight | none |
| `--h-highlight REGION` | add a horizontal map highlight | none |
| `--xy-highlight REGION` | add vertical and horizontal highlights | none |
| `--highlight-color HEX` | fill of the preceding highlight; alpha is accepted | `#FFD70030` |
| `--highlight-border HEX` | border of the preceding highlight | translucent ochre |
| `--highlight-width PT` | border width of the preceding highlight | `0.8` |
| `--highlight-dashed` / `--highlight-dash A,B` | dashed highlight border | solid |
| `--highlight-expand BP` | expand the preceding highlighted interval | `0` |
| `--no-map` | tracks only, no contact map | off |
| `--virtual4c REGION` | add a matrix-backed viewpoint profile as the current signal track | none |

### Per-track options

Each applies to the track file it follows.

| flag | meaning | default |
|---|---|---|
| `--name TEXT` | label in the gutter | the file name stem |
| `--color HEX` | main colour, and the colour above the baseline | `#CC0000` |
| `--neg-color HEX` | colour below the baseline | `#0047AB` |
| `--height PT` | track height; the column width for a y-axis track | `44` signal, data-dependent for genes |
| `--min V` / `--max V` | fix the data range | fitted |
| `--percentile P` | clip the top of an automatic range; on a signed track this applies to the magnitudes | `1.0` |
| `--symmetric` / `--no-symmetric` | centre the range on the baseline | on when the data spans zero |
| `--baseline V` | where area and bar tracks are anchored | `0` |
| `--log` | log value scaling | off |
| `--style line\|area\|bars\|points` | how a signal is drawn | `area` |
| `--style loop\|box\|domain\|arc` | draw BEDPE on the map, or as a separate 1D arc row | `loop` |
| `--aggregate mean\|max\|min\|sum` | how records are combined into a bin | `mean` |
| `--line-width PT` | stroke width for `line` and `points` | `0.8` |
| `--y-axis` | ticked y axis in the gutter instead of a range label | off |
| `--no-range-label` | hide the `[min - max]` annotation | shown |
| `--colormap NAME` | colour intervals by score | none |
| `--labels` / `--no-labels` | show or hide names | genes shown on x; BEDPE hidden |
| `--row-height PT` | gene or interval row pitch | `13` / `11` |
| `--type signal\|gene\|interval\|bedpe` | override the extension guess | from the extension |
| `--axis x\|y\|both` | square layout: which axis the track annotates | `x` |
| `--side above\|below\|both` | square-map placement of a BEDPE layer | `both` |
| `--expand BP` | expand both BEDPE anchors | `0` |
| `--fill HEX` | BEDPE shape fill; alpha is accepted | transparent |
| `--dashed` / `--dash A,B` | BEDPE outline dash pattern in points | solid |
| `--score-filter-min V` / `--score-filter-max V` | retain BEDPE records whose finite column-8 score is in range | no filtering |
| `--score-opacity A,B` | map low/high BEDPE scores to opacity in `[0,1]` | fixed opacity |
| `--score-line-width A,B` | map low/high BEDPE scores to outline width in points | fixed `--line-width` |
| `--score-size A,B` | map low/high BEDPE scores to 2D marker-size multipliers; unavailable for arcs | `1,1` |

### BEDPE arc tracks

`--style arc` changes a BEDPE input from a 2D map overlay into a separate 1D
track. Each supplied pair is projected to the midpoint of its two anchors and
joined by an upward semicircle with radius equal to half the endpoint span. This is a coordinate-only rendering operation: GRE
does not call loops, merge records, calculate significance, or aggregate the
contact matrix.

```bash
./build/tools/gre_plot sample.hic chr8:126.8Mb-128.6Mb \
    --layout pyramid --norm SCALE \
    genes.gtf.gz --name Genes --height 70 \
    loops.bedpe.gz --style arc --name Loops --height 180 \
      --score-filter-min 40 --colormap plasma \
      --score-opacity 0.4,1 --score-line-width 0.7,2.4 \
      --fill '#7A017720' \
    --out arc_loops
```

Each arc has a circular shape fixed by its endpoints. Arcs taller than the
track are clipped; increase `--height` to show their full semicircles. Longer arcs are
painted first so shorter local interactions remain visible. `--fill` adds a
translucent dome under each arc, `--dashed`/`--dash` affect the stroke, and the
same score filtering, colour, opacity, and line-width options used by 2D loop
overlays apply unchanged. `--labels` places the BEDPE name above the apex.

In C++ the corresponding track is `ArcTrack`:

```cpp
panel.add_track(
    ArcTrack{BedpeSource::open("loops.bedpe.gz")}
        .name("Loops")
        .height(180)
        .color_by_score("plasma")
        .opacity_by_score(0.4, 1.0)
        .line_width_by_score(0.7, 2.4));
```

### BEDPE input contract

GRE accepts local paths or public HTTP(S) URLs for plain `.bedpe` and
gzip-compressed `.bedpe.gz`. Coordinates are zero-based and half-open, matching
BED. Blank lines, comments beginning with `#`, and UCSC `track`/`browser` lines
are ignored.

| column | field | use in GRE |
|---:|---|---|
| 1–3 | `chrom1 start1 end1` | first anchor |
| 4–6 | `chrom2 start2 end2` | second anchor |
| 7 | name | optional label used with `--labels` |
| 8 | score | optional filter, colour, opacity, width, and marker-size mapping |
| 9–10 | strands | accepted but not currently rendered |
| later optional field | `R,G,B` or hex colour | per-record stroke colour when recognizable |

Juicer-style ENCODE BEDPE files commonly leave column 8 empty, put itemRgb in
column 11, and put observed/domain score in column 12. GRE uses that column-12
value as the score fallback when column 8 is missing.

For GTF/GFF gene tracks, records sharing a `transcript_id` are assembled into
one transcript model. `exon` rows become exon blocks, `CDS` rows define the
thicker coding range, and transcript/gene names are retained for labels. This
assembly is purely structural parsing of the supplied annotation; GRE does not
infer transcripts or modify their coordinates.

For example:

```text
# chrom1 start1 end1 chrom2 start2 end2 name score strand1 strand2 itemRgb
chr2  40100000 40110000  chr2  41200000 41210000  loop_1  85  +  -  230,40,40
chr2  42000000 42010000  chr2  43500000 43510000  loop_2  42  .  .  30,90,180
```

Anchor order does not matter. GRE detects and swaps anchors as needed for an
x/y rectangle, and orders intra-chromosomal anchors by genomic midpoint for
square and pyramid views. Records that overlap neither displayed axis are not
drawn.

The three styles have distinct geometry:

- `loop` draws a true device-space circle around the 2D anchor intersection.
  Its diameter is derived from both anchor spans, but remains circular when
  the anchors have different widths or the map itself is rectangular. Very
  narrow anchors receive a minimum visible size.
- `box` draws the rectangular product of the two anchor intervals.
- `domain` draws a triangle bounded by the diagonal in square and pyramid
  views. In a non-diagonal rectangle it falls back to a box because there is no
  meaningful diagonal domain triangle.

`--expand 5kb` subtracts 5 kb from each anchor start and adds 5 kb to each
anchor end, clamping starts at zero. It affects geometry, not BEDPE filtering.
`--side above|below|both` controls mirrored placement only when x and y are the
same square genomic region.

Highlights are painted first, then BEDPE layers in command-line order, followed
by the optional diagonal and map border. This keeps loop/TAD outlines legible
over translucent locus highlights.

Score mappings are normalized through one `ValueScale`. With automatic limits,
the scale is fitted after region and score filtering. `--percentile` controls
its upper quantile, while `--min` and `--max` fix either or both ends. The same
normalized score drives every requested visual channel, so a record that is
high-colour is also high-opacity/high-width when those mappings increase from
low to high. Reversing a pair, such as `--score-size 1.8,0.7`, intentionally
makes low scores larger.

Aliases: `--label` for `--name`, `--track-height` and `--height-pt` for a
per-track `--height` where the positional meaning of `--height` would be
ambiguous, and British spellings `--colour`, `--neg-colour`, `--map-colours`.
`--help` prints the same list.

### Colour maps

`juicebox` (white→red, the default), `fall` (white→yellow→red→maroon),
`viridis`, `magma`, `inferno`, `plasma`, `cividis`, `gray`, `gray_r`, `reds`,
`blues`, `rd_bu` and `bwr` (diverging, for observed/expected).

### Notes on the defaults

- **Signed data centres on zero** and is drawn red above the baseline, blue
  below — the A/B convention for compartment eigenvectors. All-positive data
  anchors at zero instead.
- `--percentile` on a signed track applies to the *magnitudes*, so one large
  spike of either sign stops flattening everything else.
- `--height` is the page height in inches before any track file, and the track
  height in points after one. In a y-axis track it is the column width.
- `--axis both` puts the same file on both axes of a square figure, reading it
  once.
- `--panel` resets the current track. A per-track option immediately after a
  panel boundary is therefore rejected until a file or `--virtual4c` creates a
  new current track.
- Shared map fitting pools finite displayed cells, applies the configured
  percentile once, and anchors non-symlog contact maps at zero just like
  single-map fitting.
- The dark theme uses `magma` for the map, since a white-to-red ramp reads
  poorly on a dark ground.

With no region the tool picks a window a quarter of the way along the first
contig, and it always reports the file's resolutions and normalizations plus
how many bins actually carry contacts, so a blank map is never a mystery.

### Troubleshooting and interpretation

- **“Cannot tell what this file is from its extension.”** Add
  `--type signal`, `--type gene`, `--type interval`, or `--type bedpe` after
  that file. This is especially useful for extensionless generated files.
- **The contact map is blank.** Read the reported non-empty-bin count. Check
  chromosome spelling, region bounds, normalization availability, and whether
  the forced resolution exists. GRE automatically reconciles `chr1` and `1`,
  but cannot invent a missing normalization vector.
- **A BEDPE layer is missing.** Confirm that both anchors overlap the displayed
  square region, or that one anchor overlaps x and the other overlaps y in
  rectangle mode. A `--side below` layer is intentionally invisible in a
  pyramid because a pyramid contains only the upper-triangle projection.
- **Loops are too small to see.** Use `--expand 5kb` or a larger value, increase
  `--line-width`, add a translucent `--fill`, or switch to `--style box`.
- **Annotations obscure the matrix.** Use an alpha fill such as `#FFFFFF40`,
  omit `--fill`, reduce line width, or use a dashed outline. Pair annotations
  remain vector objects in PDF/SVG regardless of style.
- **The two VS halves look incomparable.** By default each half is fitted
  independently. Set explicit primary and comparison limits with
  `--map-min/--map-max` and `--vs-map-min/--vs-map-max` when quantitative visual
  comparison is intended.
- **Two panels use misleadingly similar colours for different values.** Add
  `--shared-map-scale`, or set explicit map limits. The shared option pools
  primary panels together and VS panels together; it deliberately does not
  pool primary and VS layers because they may use different matrix types or
  colour maps.
- **A track appeared in the wrong panel.** Files and highlights belong to the
  panel active when they are declared. Put panel-1 tracks before the first
  `--panel`; put each later panel's tracks after its `--panel REGION` token.
- **Virtual 4C is empty.** The viewpoint must overlap a matrix chromosome and
  use the same chromosome naming conventions accepted by the `.hic` source.
  Check normalization and resolution just as for an empty map. The viewpoint
  may be outside the displayed x interval, but it must exist on the matrix's y
  axis.
- **Score styling did nothing.** BEDPE score is column 8, not column 7. Use a
  numeric finite value. `--colormap`, `--score-opacity`, `--score-line-width`,
  and `--score-size` all normalize through the visible layer's score range.
- **The comparison half is empty.** GRE leaves it transparent when its query
  has no data. Check the second file's contigs, normalization, and resolution;
  it does not silently substitute primary values.
- **A horizontal highlight does not appear in pyramid mode.** This is by
  design: the pyramid y coordinate is genomic separation. Use a square map for
  row/column highlighting or a vertical-only pyramid highlight.
- **A PDF looks sharp but the heatmap is still pixelated.** The matrix is
  intentionally rasterized as one image while semantic annotations remain
  vector. Increase figure width or select a finer resolution; DPI controls PNG
  sampling but does not turn matrix cells into PDF vector rectangles.
- **SVG text shifts slightly.** SVG uses the viewer's installed font, whereas
  PNG and PDF are measured from GRE's selected font file. Set `GRE_FONT` and
  ensure the same font is available to the SVG viewer when exact matching is
  required.

## Advanced C++ usage

The CLI covers vertically stacked panels with one global map configuration.
The library API additionally supports independently configured panels,
in-memory matrices/features, custom `Track` subclasses, and programmatic
construction of pair annotations.

### Shared scales across independently configured panels

Attach the same `HeatmapScaleGroup` to every map that should be quantitatively
comparable. Figure preparation loads every panel first, fits the group once,
then rasterizes each map and refreshes linked colour bars.

```cpp
auto shared = std::make_shared<HeatmapScaleGroup>();

Panel& locus_a = figure.add_panel();
locus_a.set_region("chr1", 20'000'000, 24'000'000).set_title("locus A");
HeatmapTrack& map_a = locus_a.add_track(
    HeatmapTrack{hic}
        .mode(HeatmapMode::triangle)
        .colors("juicebox")
        .shared_scale(shared));
locus_a.add_bottom_track(ColorBarTrack{map_a}.title("shared contacts"));

Panel& locus_b = figure.add_panel();
locus_b.set_region("chr8", 127'000'000, 129'000'000).set_title("locus B");
HeatmapTrack& map_b = locus_b.add_track(
    HeatmapTrack{hic}
        .mode(HeatmapMode::triangle)
        .colors("juicebox")
        .shared_scale(shared));
```

Construct a group with a scale to make its fitting policy explicit:

```cpp
ValueScale policy;
policy.type(ScaleType::log1p).upper_percentile(0.995);
auto shared = std::make_shared<HeatmapScaleGroup>(policy);
```

For split maps, use a second group with
`shared_comparison_scale(comparison_group)`. This keeps all primary layers
together and all comparison layers together without accidentally combining
two differently normalized quantities.

### Independent primary and comparison scales

```cpp
auto control = StrawMatrixSource::open(
    "control.hic", StrawOptions{.normalization = "SCALE", .resolution = 10'000});
auto treated = StrawMatrixSource::open(
    "treated.hic", StrawOptions{.normalization = "KR", .resolution = 25'000});

ValueScale control_scale;
control_scale.type(ScaleType::log1p).upper_percentile(0.995);

ValueScale treated_scale;
treated_scale.type(ScaleType::log1p).upper_percentile(0.99);

Panel& panel = figure.add_panel();
panel.set_region("chr2", 40'000'000, 44'000'000);

HeatmapTrack& map = panel.set_matrix(
    HeatmapTrack{control}
        .mode(HeatmapMode::square)
        .colors("reds")
        .scale(control_scale)
        .compare_with(treated, MatrixHalf::below)
        .comparison_colors("blues")
        .comparison_scale(treated_scale)
        .show_diagonal(true));

panel.add_bottom_track(
    ColorBarTrack{map, HeatmapLayer::primary}.title("control").align(BarAlign::left));
panel.add_bottom_track(
    ColorBarTrack{map, HeatmapLayer::comparison}.title("treated").align(BarAlign::right));
```

The two sources may return different bin sizes. GRE samples each half by
genomic coordinate into one output image. The diagonal belongs to the primary
source. If the comparison query is empty, its half remains transparent.

### Multiple BEDPE layers

```cpp
auto loops = BedpeSource::open("loops.bedpe.gz");
auto domains = BedpeSource::open("domains.bedpe");

map.add_annotation(PairAnnotationLayer{loops}
    .style(PairAnnotationStyle::loop)
    .side(AnnotationSide::both)
    .color(color_from_hex("#202020"))
    .fill(color_from_hex("#FFFFFF50"))
    .line_width(1.2)
    .show_labels(false));

map.add_annotation(PairAnnotationLayer{domains}
    .style(PairAnnotationStyle::domain)
    .side(AnnotationSide::above)
    .color(color_from_hex("#6A3D9A"))
    .dash({5.0, 2.0})
    .expand(5'000));
```

### Score-driven BEDPE styling

```cpp
ValueScale loop_scores;
loop_scores.min(5.0).upper_percentile(0.99);

map.add_annotation(
    PairAnnotationLayer{loops}
        .style(PairAnnotationStyle::loop)
        .side(AnnotationSide::both)
        .fill(color_from_hex("#FFFFFF70"))
        .score_filter(5.0, std::nullopt)
        .color_by_score("viridis", loop_scores)
        .opacity_by_score(0.25, 1.0)
        .line_width_by_score(0.5, 2.5)
        .size_by_score(0.7, 1.6));
```

The styling methods are independent: call only the channels that encode
something meaningful. `color_for`, `fill_for`, `line_width_for`, and
`size_for` expose resolved per-feature styles for custom renderers or legends.

### Virtual 4C as an ordinary signal source

```cpp
auto viewpoint = Virtual4CSource::make(
    hic, GenomicRegion{"chr3", 46'200'000, 46'250'000});
viewpoint->aggregation(Virtual4CSource::Aggregation::mean);

panel.add_track(
    SignalTrack{viewpoint}
        .name("promoter viewpoint")
        .style(SignalStyle::line)
        .color(color_from_hex("#2C7FB8"))
        .line_width(1.4)
        .height(42.0));

map.add_highlight(
    MapHighlight{viewpoint->viewpoint(), HighlightAxis::vertical}
        .fill(color_from_hex("#2C7FB820"))
        .border(color_from_hex("#2C7FB8")));
```

`Virtual4CSource` performs no calling, smoothing, expected-model fitting, or
cross-locus aggregation. Each output bin is the mean, sum, or maximum of the
supplied matrix cells intersecting that bin and the fixed viewpoint interval.

### In-memory loops and domains

Pair annotations do not have to originate in BEDPE:

```cpp
std::vector<PairFeature> calls = {
    PairFeature{
        "loop A",
        GenomicRegion{"chr1", 10'100'000, 10'110'000},
        GenomicRegion{"chr1", 11'300'000, 11'310'000},
        color_from_hex("#E31A1C"),
        92.0,
    },
};

auto pairs = MemoryPairFeatureSource::make(std::move(calls));
map.add_annotation(PairAnnotationLayer{pairs}
    .style(PairAnnotationStyle::loop)
    .side(AnnotationSide::both)
    .show_labels(true));
```

Custom sources implement `PairFeatureSource::query(x_region, y_region)`, making
it possible to connect a database, caller output, or an application-owned data
structure without converting it to a file.

### Programmatic highlights

```cpp
map.add_highlight(
    MapHighlight{GenomicRegion{"chr1", 10'800'000, 10'950'000},
                 HighlightAxis::vertical}
        .fill(color_from_hex("#FFD70030"))
        .border(color_from_hex("#A07000"), 0.8));

map.add_highlight(
    MapHighlight{GenomicRegion{"chr1", 11'500'000, 11'600'000},
                 HighlightAxis::both}
        .fill(color_from_hex("#00A0FF20"))
        .dashed()
        .expand(10'000));
```

For full control, a custom `Track` can use `TrackRect::x`, `TrackRect::y`, and
the vector primitives on `Canvas`. Built-in map overlays should generally be
preferred because they share the heatmap's clipping and triangle projection.

## Publication feature coverage and roadmap

GRE deliberately separates analysis from rendering. A computed result can
often already be shown even when GRE does not calculate it: insulation,
directionality index, eigenvectors, ChIP-seq, accessibility, replication
timing, and similar one-dimensional values can be supplied as bigWig,
bedGraph, or wig; called genes/intervals can be supplied as BED/GFF/GTF; loops
and domains can be supplied as BEDPE.

GRE's boundary is intentionally strict: it can display supplied values,
re-express matrix cells visually, or extract a direct viewpoint profile. It
does not call biological structures or perform aggregate/statistical analyses.
Therefore APA/pileups, aggregate domains, saddle plots, insulation,
directionality index, PC1/eigenvector inference, boundary calling, loop
significance, replicate aggregation, and P(s) estimation are out of scope.
Their outputs remain valid GRE inputs when supplied as matrices, signals,
intervals, or pairs.

The table below focuses on common publication geometry and composition that is
still missing without crossing that boundary.

| feature | common publication use | current GRE status | in-scope addition |
|---|---|---|---|
| Difference, ratio, and log-ratio display | direct condition A vs B comparison | split VS display only | optional cell-wise display transforms over already aligned matrices, with explicit zero/missing policies; no statistical testing |
| Directional/anchored loops | promoter–enhancer direction or motif orientation | strands are retained but not rendered | arrowheads, anchor glyphs, asymmetric anchor colours, and strand-aware orientation |
| Arc/link tracks | compact interactions above a 1D locus | implemented: `ArcTrack` / `--style arc`, semicircular geometry, fill/dashes/labels, and score-driven colour, opacity, and width | optional directional arrowheads and separate anchor styling |
| Stripes and extrusion trails | vertical/horizontal structures called elsewhere | rectangular highlights approximate bands | supplied stripe polygons/anchors, tapered ends, gradients, and score styling |
| Nested TAD presentation | visualize supplied domain hierarchy | several BEDPE layers work manually | level-aware packing, side assignment, labels, and a domain legend without domain calling |
| Diagonal distance guides | label 100 kb, 500 kb, or 1 Mb separation | diagonal and pyramid depth only | parallel contours, labels, and shaded distance bands computed only from coordinates |
| Masks and unreliable regions | explain assembly gaps or low mappability | generic highlights and intervals | hatched row/column/diagonal masks from supplied BED intervals |
| Ideograms/cytobands | chromosome and viewport context | generic feature rendering only | cytoband stain palette, centromere geometry, chromosome extent, viewport marker |
| Callouts | arrows, brackets, text, numbered loci | custom C++ track required | reusable callout overlay with anchors and collision-aware labels |
| Panel grids | sample × locus or sample × resolution figures | CLI stacks panels vertically | row/column panel grids, aligned map boxes, shared outer axes, panel letters |
| Declarative figure specification | complex reproducible figures | detailed CLI and C++ API | YAML/JSON schema for per-panel sources/styles and reusable presets |
| Insets and zoom connectors | overview plus local detail | multiple stacked panels only | inset rectangles and vector connectors between supplied regions |
| Whole-genome display | chromosome-block overview | one x/y block per panel | supplied chromosome concatenation order, boundaries, labels, and trans blocks |
| Categorical matrix overlays | compartment/state blocks supplied by callers | highlights and BEDPE shapes | generic 2D interval/block source with categorical fill, hatch, border, and legend |
| Signal uncertainty ribbons | display externally calculated confidence bounds | one value per signal track | multi-column signal source and ribbon/error-band rendering, without calculating uncertainty |
| Rich legends | decode score size/opacity/width and categorical layers | continuous map bars and discrete colour swatches | size, line-width, opacity, hatch, and composite BEDPE legends |
| Provenance | reproducible methods and supplements | title/subtitle and console report | embedded command, source metadata, regions, scale limits, version, and sidecar JSON |
| Accessibility patterns | survive grayscale and colour-vision differences | colour and dashes are configurable | hatch/pattern fills, tested palettes, contrast warnings, and monochrome presets |
| Scale bars and bin grids | communicate resolution directly on the map | genomic axes only | map-bin grid, genomic scale bar, and explicit resolution badge |

### Recommended implementation order

Shared map fitting, score-driven BEDPE styling, stacked CLI panels, and direct
virtual 4C extraction are now implemented. The highest-value next additions
within the visualization-only scope are:

1. **Arc/link tracks and directional anchor glyphs.** They reuse the pair
   source and score styling model while covering a very common compact figure
   form.
2. **Panel grids and a declarative figure specification.** Stacked CLI panels
   solve the linear case; sample-by-locus layouts need aligned rows/columns and
   per-panel map sources that are clearer in YAML/JSON than positional flags.
3. **Diagonal-distance guides, masks, and supplied stripe geometry.** These
   are coordinate projections of provided annotations, not new analyses.
4. **Rich legends and accessibility patterns.** Score-size/opacity mappings
   need publication-ready keys, and hatch/monochrome styles improve print and
   colour-vision accessibility.
5. **Callouts, insets, zoom connectors, and panel lettering.** These remove
   the last common reasons to post-process a GRE figure in Illustrator.
6. **Optional cell-wise difference/ratio display transforms.** This remains
   in scope only as transparent arithmetic over compatible supplied matrices;
   significance estimation and replicate modeling remain outside GRE.

Interactive pan/zoom, browser state, and live streaming remain intentional
non-goals. They are characteristic of genome browsers rather than static
publication rendering.

## Architecture

```
Figure → Panel → Track → Canvas → backend
```

- `core/` — geometry, colour and colour maps, value scales, the single
  genomic→figure transform.
- `figure/` — figure, panel, track interface, themes, layout.
- `tracks/` — heatmap, signal, gene, interval, axis, colour bar, legend.
- `render/` — the `Canvas` interface plus raster, PDF and SVG backends, text
  and font handling.
- `data/` — `SignalSource`, `FeatureSource`, `PairFeatureSource`, `MatrixSource`
  and the straw / igv-cpp / BEDPE / in-memory adapters.
- `export/` — PNG and PDF writers.

Figure coordinates are PDF points (72 per inch) with the origin at the
top-left. The raster backend scales by `dpi/72`; the PDF backend maps them
directly with a y-flip. Nothing else in the codebase re-derives that mapping,
and no track computes a genomic position itself — `GenomicTransform` is the one
place that happens.

### Dense vs. vector

Contact matrices are normalised, looked up in a 1024-entry colour table and
written straight into an RGBA image, which the backends draw as a single
raster. A 400×400 map is one embedded image in the PDF, not 160,000
rectangles. Everything else — text, axes, gene models, annotations, borders —
stays vector, so a typical figure PDF is tens of kilobytes with selectable,
searchable text.

### Dependencies

zlib, and the two sibling data libraries. PNG chunks, the PDF object and xref
writer, the TrueType parser, the glyph rasteriser and the anti-aliased scanline
rasteriser are all part of this codebase — there is no vendored graphics or
font library.

### Text

Text defaults to Arial, found by searching the platform's font directories and
falling back through the metric-compatible substitutes (Liberation Sans,
Helvetica, DejaVu Sans). `GRE_FONT` names a `.ttf`/`.ttc` explicitly and
`GRE_FONT_DIR` adds a search directory. Every backend measures the same font
file and routes through the same `text_origin`, so alignment is identical
across PNG, PDF and SVG. PDFs embed a subset of the font program as a
CIDFontType2 with Identity-H encoding and a `/ToUnicode` map, so text copies
and searches correctly.

## Limits worth knowing

- Split/VS comparison is intentionally limited to a square with identical x
  and y regions. It displays two sources; it does not calculate a difference,
  ratio, or statistical comparison.
- BEDPE files and URLs are read into memory and filtered per view. This is
  appropriate for sparse loop/domain call sets but is not an indexed solution
  for files containing tens of millions of pairs.
- BEDPE score drives filtering, colour, opacity, line width, and marker size.
  Strand columns are accepted but strand-aware glyphs are not implemented yet.
- The CLI multi-panel layout is one vertical stack with global map settings.
  A sample-by-locus grid or different `.hic` source per panel still requires
  the C++ API.
- Virtual 4C is a direct row reduction over one viewpoint interval. It does
  not smooth the trace, fit an expected model, call peaks, or combine
  replicates.
- Pyramid y coordinates represent contact distance. Horizontal genomic
  highlights and below-only annotations therefore have no pyramid projection.
- Only TrueType outlines are supported. An OpenType/CFF font (`.otf`) is
  rejected, and the search falls through to the next candidate.
- `RotatedCanvas` handles quarter turns only; arbitrary rotation of a whole
  track is not supported. Rotated *text* works at any angle.
- SVG emits real `<text>`, so the viewer supplies the font; glyph widths can
  differ slightly from PNG and PDF, which measure the file itself.
- A `.hic` query whose stored resolutions are far finer than the requested
  pixel width aggregates whole numbers of bins rather than allocating the
  natural grid, capped at 8192 cells per axis.
- Rows and columns with no normalization vector come back as zero rather than
  as missing, so they read as empty rather than as a gap.
