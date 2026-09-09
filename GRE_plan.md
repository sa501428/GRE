# C++ Genomics Figure Rendering Architecture

## Goal

Build a lightweight C++ library for generating static genomics figures programmatically.

Primary outputs:

- PNG
- PDF

Primary visualization types:

- 2D heatmaps / contact maps
- 1D signal tracks
- gene / interval annotations
- axes, labels, legends, and simple overlays

The system is **not a genome browser**. It does not need interactive navigation, UI state, asynchronous streaming, or complex application infrastructure.

The main priorities are:

- precise control over figure layout
- high-resolution raster output
- publication-quality PDF output
- efficient rendering of large heatmaps
- simple composition of multiple genomic tracks
- minimal abstractions and dependencies

---

## Assumptions

Existing C++ libraries already provide:

- Hi-C / 2D matrix data access
- IGV-style 1D genomic data access

This project should consume those APIs rather than reimplement genomic file parsing.

---

## Overall Approach

Use a simple rendering pipeline:

```text
data
  ↓
track preparation
  ↓
figure layout
  ↓
drawing commands
  ↓
render backend
  ├── PNG
  └── PDF
```

The same figure definition should support both PNG and PDF export.

Dense data such as heatmaps should be treated primarily as raster data.

Sparse elements such as:

- labels
- axes
- gene models
- lines
- borders
- annotations

should remain vector-like where practical, especially for PDF.

---

## Core Architecture

Keep the system divided into four small layers.

```text
Figure
  ↓
Tracks
  ↓
Canvas
  ↓
Backend
```

### Figure

A `Figure` owns:

- size
- margins
- one or more panels
- track ordering
- output settings

Example:

```cpp
Figure fig;

auto& panel = fig.add_panel();
panel.set_region("chr8", 127000000, 129000000);

panel.add_track(GeneTrack{genes});
panel.add_track(SignalTrack{atac});
panel.add_track(HeatmapTrack{hic});

fig.save_png("figure.png", 300);
fig.save_pdf("figure.pdf");
```

The public API should stay close to this level of simplicity.

---

## Tracks

Each visualization type is implemented as a track.

Examples:

```text
HeatmapTrack
SignalTrack
GeneTrack
IntervalTrack
AxisTrack
```

A minimal interface is sufficient:

```cpp
class Track {
public:
    virtual ~Track() = default;

    virtual double preferred_height() const = 0;

    virtual void prepare(const ViewContext&) = 0;

    virtual void draw(Canvas&, const TrackRect&) const = 0;
};
```

`prepare()` handles data transformation.

`draw()` only performs rendering.

Avoid building a large scene graph or widget hierarchy.

---

## Coordinate Model

Use three coordinate spaces:

```text
genomic coordinates
        ↓
figure coordinates
        ↓
device coordinates
```

### Genomic coordinates

Examples:

```text
chr1:100000-200000
Hi-C bin coordinates
```

### Figure coordinates

Backend-independent positions measured in logical units.

Using PDF points is convenient:

```text
72 units = 1 inch
```

### Device coordinates

For PNG:

```text
figure units × DPI / 72 = pixels
```

For PDF:

```text
figure units map directly to PDF points
```

This allows the same figure to render cleanly at different resolutions.

---

## Genomic Transforms

Centralize genomic-to-figure conversion.

```cpp
struct GenomicTransform {
    int64_t start;
    int64_t end;
    double x0;
    double x1;

    double x(int64_t position) const;
};
```

Do not duplicate coordinate formulas inside individual tracks.

For heatmaps, use separate X and Y transforms when needed.

---

## Layout

Keep layout intentionally simple.

A panel is mostly a vertical stack:

```text
┌─────────────────────────┐
│ axis                    │
├─────────────────────────┤
│ genes                   │
├─────────────────────────┤
│ signal                  │
├─────────────────────────┤
│ signal                  │
├─────────────────────────┤
│ Hi-C heatmap            │
└─────────────────────────┘
```

Tracks can have:

- fixed height
- auto height
- small margins

Example:

```cpp
panel.add_track(
    SignalTrack{signal}
        .height(80)
);

panel.add_track(
    HeatmapTrack{hic}
        .height(300)
);
```

No general-purpose layout engine is necessary initially.

---

## Canvas API

Tracks should draw through a small internal `Canvas` abstraction.

```cpp
class Canvas {
public:
    virtual void fill_rect(Rect, Color) = 0;

    virtual void stroke_line(
        Point,
        Point,
        StrokeStyle
    ) = 0;

    virtual void draw_text(
        Point,
        std::string_view,
        TextStyle
    ) = 0;

    virtual void draw_image(
        Rect,
        const ImageView&
    ) = 0;
};
```

Potential implementations:

```text
RasterCanvas
PdfCanvas
```

Do not expose the underlying graphics library directly to tracks.

---

## Heatmap Rendering

Heatmaps deserve a dedicated rendering path.

Do not represent every cell as a general rectangle object.

Instead:

```text
matrix values
    ↓
normalization
    ↓
color lookup
    ↓
RGBA image
```

Example internal representation:

```cpp
struct HeatmapImage {
    int width;
    int height;
    std::vector<uint8_t> rgba;
};
```

Color mapping should use a precomputed lookup table where practical:

```cpp
std::array<RGBA, 1024> color_lut;
```

Rendering becomes essentially:

```cpp
for each value:
    normalize
    lookup color
    write pixel
```

For very large matrices, process in tiles or row blocks.

Do not over-engineer tiling unless memory or performance requires it.

---

## 1D Signal Tracks

A 1D track should request data appropriate for the final image width.

Example:

```text
genomic span = 10 Mb
track width  = 2000 px

≈ 5 kb / pixel
```

The existing data layer should ideally provide resolution-aware data.

The visualization layer should avoid loading much higher-resolution data than the output can display.

A signal track can then convert the resulting bins to:

- line
- filled area
- histogram-like bars

depending on style.

---

## PNG Backend

The PNG backend renders everything to an RGBA buffer.

```text
Figure
  ↓
RasterCanvas
  ↓
RGBA buffer
  ↓
PNG encoder
```

Example:

```cpp
struct Image {
    int width;
    int height;
    int stride;
    std::vector<uint8_t> pixels;
};
```

PNG encoding should remain separate from drawing.

The renderer simply produces pixels.

---

## PDF Backend

PDF output should be hybrid.

Use native PDF/vector drawing for:

- text
- axes
- lines
- gene models
- annotations
- borders

Use embedded raster images for:

- Hi-C heatmaps
- other extremely dense visualizations

Example:

```text
PDF page
├── vector title
├── vector axes
├── vector gene annotations
├── vector signal lines
└── embedded heatmap image
```

Do not emit millions of individual PDF rectangles for large heatmaps.

---

## Text

Keep text support small initially.

Required functionality:

- font family
- size
- alignment
- basic measurement
- draw text

Example:

```cpp
struct TextStyle {
    FontId font;
    double size;
    TextAlign align;
};
```

A small `FontManager` or wrapper around the chosen graphics backend is sufficient.

Complex text shaping can be added later if actually needed.

---

## Data Interfaces

Keep genomic readers outside the renderer.

Define minimal adapters such as:

```cpp
class SignalSource {
public:
    virtual SignalData query(
        ChromId chromosome,
        int64_t start,
        int64_t end,
        size_t target_bins
    ) = 0;
};
```

and:

```cpp
class MatrixSource {
public:
    virtual MatrixData query(
        const MatrixRegion&,
        size_t target_width,
        size_t target_height
    ) = 0;
};
```

Existing IGV/Hi-C libraries can implement these adapters.

This prevents file-format details from leaking into visualization code.

---

## Suggested Module Structure

```text
src/
├── core/
│   ├── geometry.hpp
│   ├── color.hpp
│   └── genomic_transform.hpp
│
├── figure/
│   ├── figure.hpp
│   ├── panel.hpp
│   └── track.hpp
│
├── tracks/
│   ├── heatmap_track.*
│   ├── signal_track.*
│   ├── gene_track.*
│   ├── interval_track.*
│   └── axis_track.*
│
├── render/
│   ├── canvas.hpp
│   ├── raster_canvas.*
│   ├── pdf_canvas.*
│   └── text.*
│
├── data/
│   ├── signal_source.hpp
│   └── matrix_source.hpp
│
└── export/
    ├── png.*
    └── pdf.*
```

This can remain a relatively small codebase.

---

## Rendering Flow

For a typical figure:

```text
create Figure
    ↓
define genomic region
    ↓
add tracks
    ↓
calculate track rectangles
    ↓
query required data
    ↓
prepare track data
    ↓
create backend
    ↓
draw tracks in order
    ↓
write PNG or PDF
```

For example:

```text
Figure
├── GeneTrack
├── ATAC SignalTrack
├── ChIP SignalTrack
└── Hi-C HeatmapTrack
```

No persistent rendering state or interactive viewport is required.

---

## Performance Strategy

Start simple.

The main performance rules should be:

1. Do not load substantially more genomic data than can be displayed.
2. Rasterize dense heatmaps directly rather than generating geometric objects.
3. Keep numerical arrays contiguous.
4. Avoid per-cell allocations.
5. Precompute color lookup tables.
6. Parallelize large heatmaps only if profiling shows it is useful.
7. Add caching only where repeated figure generation benefits from it.

Avoid prematurely implementing:

- elaborate tile caches
- GPU rendering
- SIMD intrinsics
- asynchronous pipelines
- generic scene graphs
- browser-style level-of-detail systems

Static figure generation should remain straightforward.

---

## Initial Implementation Plan

### Phase 1 — Core

Implement:

- geometry types
- colors
- genomic transforms
- figure and panel
- simple vertical track layout
- `Canvas` interface

### Phase 2 — Raster Output

Implement:

- RGBA framebuffer
- rectangle and line drawing
- text
- image compositing
- PNG export

At this stage, support:

- signal tracks
- axes
- basic annotations
- heatmaps

### Phase 3 — PDF Output

Implement a PDF canvas supporting:

- paths
- rectangles
- lines
- text
- embedded raster images

Render heatmaps as embedded images.

### Phase 4 — Polish

Add only as needed:

- legends
- better label placement
- shared axes
- multiple panels
- more track styles
- reusable themes
- SVG export if useful

---

## Non-Goals

Initially, do not build:

- an interactive genome browser
- pan/zoom infrastructure
- web rendering
- GUI components
- live data streaming
- plugin systems
- generalized plotting grammar
- complex layout constraints
- GPU rendering
- elaborate caching infrastructure

The library should remain focused on one task:

> Convert genomic data and a concise figure specification into high-quality static PNG and PDF figures.

---

## Design Principle

Prefer simple data structures and direct rendering.

The intended architecture is:

```text
Figure specification
        ↓
Genomic data
        ↓
Track preparation
        ↓
Simple layout
        ↓
Canvas drawing
        ↓
PNG / PDF
```

Dense numerical data is rasterized.

Semantic annotations remain vector where useful.

Everything else should stay as lightweight as possible.