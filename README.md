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
- **Axes, colour bars, legends, grids, themes**, multiple panels.
- **PNG, PDF and SVG** from one figure definition, at any resolution.

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

`gre_plot` composes a figure from a `.hic` file and any number of 1D tracks.
Options that follow a track file apply to that track; everything else is
global. `--help` lists them all.

```bash
./build/tools/gre_plot sample.hic chr2:40000000-44000000 \
    --layout square --norm SCALE --out figure \
    compartment.bedGraph --name ECHO --height 34 \
    eigen.wig --name POSSUM2 --height 34 --percentile 0.999 \
    genes.bed --name Genes --no-labels
```

Track files are recognised by extension — bigWig/bedGraph/wig become signal
tracks, BED/GFF/GTF/genePred/bigBed gene tracks, narrowPeak/broadPeak interval
tracks. `--type` overrides the guess.

### Figure options

| flag | meaning | default |
|---|---|---|
| `--region CHR:START-END` | region to plot; also accepted as a bare argument | a window ¼ along the first contig |
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
| `--no-map` | tracks only, no contact map | off |

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
| `--aggregate mean\|max\|min\|sum` | how records are combined into a bin | `mean` |
| `--line-width PT` | stroke width for `line` and `points` | `0.8` |
| `--y-axis` | ticked y axis in the gutter instead of a range label | off |
| `--no-range-label` | hide the `[min - max]` annotation | shown |
| `--colormap NAME` | colour intervals by score | none |
| `--no-labels` | hide gene names | shown on x, hidden on y |
| `--row-height PT` | gene or interval row pitch | `13` / `11` |
| `--type signal\|gene\|interval` | override the extension guess | from the extension |
| `--axis x\|y\|both` | square layout: which axis the track annotates | `x` |

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
- The dark theme uses `magma` for the map, since a white-to-red ramp reads
  poorly on a dark ground.

With no region the tool picks a window a quarter of the way along the first
contig, and it always reports the file's resolutions and normalizations plus
how many bins actually carry contacts, so a blank map is never a mystery.

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
- `data/` — `SignalSource`, `FeatureSource`, `MatrixSource` and the straw /
  igv-cpp / in-memory adapters.
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
