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

## Examples

```bash
# Rotated pyramid map under 1D tracks.
./build/examples/gre_example_hic sample.hic chr8:127000000-129000000 \
    atac.bigWig genes.bed --norm KR

# Juicebox-style square map with tracks on both axes.
./build/examples/gre_example_square sample.hic chr1:20000000-22000000 \
    atac.bigWig genes.bed --norm KR --out square
```

Both accept `--norm`, `--oe`, `--out PREFIX` and `--dpi N`, recognise input
files by extension, and print what the `.hic` file contains. With no region
they pick a window a quarter of the way along the first contig, and they report
how many bins actually carry contacts so a blank map is never a mystery.

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
