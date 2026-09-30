# GRE ENCODE gallery

These figures are generated entirely from released public ENCODE files. The
large `.hic` is queried in place with HTTP byte ranges; indexed bigWig and
bigBed files are also queried remotely. Compressed BEDPE and GTF files are
streamed directly and held in memory for the duration of a render.
No ENCODE input needs to be downloaded or checked into this repository.

The committed PNGs were generated at 300 DPI with:

```bash
./examples/gallery/generate.sh
```

Set `GRE_PLOT` to use a different executable or `OUT_DIR` to write elsewhere:

```bash
GRE_PLOT=/path/to/gre_plot OUT_DIR=/tmp/gre-gallery \
  ./examples/gallery/generate.sh
```

Remote rendering requires ordinary HTTPS access and servers that honor byte
range requests for `.hic`, bigWig, and bigBed. ENCODE satisfies those
requirements. Runtime depends mostly on network latency; the 20.7 GB `.hic`
is not transferred in full.

## Public inputs

All coordinates use GRCh38.

| accession | role | format | ENCODE record | direct URL |
|---|---|---|---|---|
| `ENCFF080DPJ` | K562 mapQ30 contact matrix | hic | [record](https://www.encodeproject.org/files/ENCFF080DPJ/) | [download/range source](https://www.encodeproject.org/files/ENCFF080DPJ/@@download/ENCFF080DPJ.hic) |
| `ENCFF699RSL` | K562 5 kb compartment eigenvector | bigWig | [record](https://www.encodeproject.org/files/ENCFF699RSL/) | [download/range source](https://www.encodeproject.org/files/ENCFF699RSL/@@download/ENCFF699RSL.bigWig) |
| `ENCFF134HIZ` | K562 mapQ30 loops | BEDPE.gz | [record](https://www.encodeproject.org/files/ENCFF134HIZ/) | [stream source](https://www.encodeproject.org/files/ENCFF134HIZ/@@download/ENCFF134HIZ.bedpe.gz) |
| `ENCFF173VDJ` | K562 mapQ30 5 kb contact domains | BEDPE.gz | [record](https://www.encodeproject.org/files/ENCFF173VDJ/) | [stream source](https://www.encodeproject.org/files/ENCFF173VDJ/@@download/ENCFF173VDJ.bedpe.gz) |
| `ENCFF045OHM` | K562 replicated H3K27ac peaks | bigBed | [record](https://www.encodeproject.org/files/ENCFF045OHM/) | [download/range source](https://www.encodeproject.org/files/ENCFF045OHM/@@download/ENCFF045OHM.bigBed) |
| `ENCFF005MUK` | GENCODE v47 promoter reference | GTF.gz | [record](https://www.encodeproject.org/files/ENCFF005MUK/) | [stream source](https://www.encodeproject.org/files/ENCFF005MUK/@@download/ENCFF005MUK.gtf.gz) |
| `ENCFF094XCU` | K562 H3K27ac signal p-value | bigWig | [record](https://www.encodeproject.org/files/ENCFF094XCU/) | [download/range source](https://www.encodeproject.org/files/ENCFF094XCU/@@download/ENCFF094XCU.bigWig) |
| `ENCFF357GNC` | K562 ATAC-seq signal p-value | bigWig | [record](https://www.encodeproject.org/files/ENCFF357GNC/) | [download/range source](https://www.encodeproject.org/files/ENCFF357GNC/@@download/ENCFF357GNC.bigWig) |
| `ENCFF336UPT` | K562 CTCF ChIP-seq signal p-value | bigWig | [record](https://www.encodeproject.org/files/ENCFF336UPT/) | [download/range source](https://www.encodeproject.org/files/ENCFF336UPT/@@download/ENCFF336UPT.bigWig) |
| `ENCFF688NNQ` | K562 transcript models, GENCODE V29-based | GTF.gz | [record](https://www.encodeproject.org/files/ENCFF688NNQ/) | [stream source](https://www.encodeproject.org/files/ENCFF688NNQ/@@download/ENCFF688NNQ.gtf.gz) |

The Hi-C, compartment, loop, and domain files belong to released K562 in situ
Hi-C experiment [ENCSR545YBD](https://www.encodeproject.org/experiments/ENCSR545YBD/).
The H3K27ac peaks belong to released K562 Histone ChIP-seq experiment
[ENCSR000AKP](https://www.encodeproject.org/experiments/ENCSR000AKP/).
The additional H3K27ac signal is from that experiment; ATAC-seq is from
[ENCSR868FGK](https://www.encodeproject.org/experiments/ENCSR868FGK/), CTCF
ChIP-seq is from
[ENCSR000EGM](https://www.encodeproject.org/experiments/ENCSR000EGM/), and the
K562 transcript models are from
[ENCSR589FUJ](https://www.encodeproject.org/experiments/ENCSR589FUJ/).

The commands below assume these shell variables, exactly as the generator
does:

```bash
HIC='https://www.encodeproject.org/files/ENCFF080DPJ/@@download/ENCFF080DPJ.hic'
COMPARTMENTS='https://www.encodeproject.org/files/ENCFF699RSL/@@download/ENCFF699RSL.bigWig'
LOOPS='https://www.encodeproject.org/files/ENCFF134HIZ/@@download/ENCFF134HIZ.bedpe.gz'
DOMAINS='https://www.encodeproject.org/files/ENCFF173VDJ/@@download/ENCFF173VDJ.bedpe.gz'
H3K27AC_PEAKS='https://www.encodeproject.org/files/ENCFF045OHM/@@download/ENCFF045OHM.bigBed'
PROMOTERS='https://www.encodeproject.org/files/ENCFF005MUK/@@download/ENCFF005MUK.gtf.gz'
H3K27AC_SIGNAL='https://www.encodeproject.org/files/ENCFF094XCU/@@download/ENCFF094XCU.bigWig'
ATAC_SIGNAL='https://www.encodeproject.org/files/ENCFF357GNC/@@download/ENCFF357GNC.bigWig'
CTCF_SIGNAL='https://www.encodeproject.org/files/ENCFF336UPT/@@download/ENCFF336UPT.bigWig'
TRANSCRIPTS='https://www.encodeproject.org/files/ENCFF688NNQ/@@download/ENCFF688NNQ.gtf.gz'
GRE_PLOT='./build/tools/gre_plot'
```

## 1. Pyramid plus supplied 1D and 2D annotations

This example combines a distance-limited triangular map with a signed
compartment signal, H3K27ac peak intervals, promoter annotations, supplied
contact domains, and supplied loops. The loop `observed` field from the ENCODE
Juicer BEDPE is used as its score and mapped to colour, opacity, line width,
and marker size.

```bash
"$GRE_PLOT" "$HIC" chr8:126500000-130000000 \
  --layout pyramid --norm SCALE --resolution 25000 --max-distance 1200000 \
  --width 7 --dpi 300 --formats png --theme publication \
  --title 'K562 — MYC neighborhood' \
  "$COMPARTMENTS" --name 'A/B compartment' --height 28 --style area \
    --color '#B2182B' --neg-color '#2166AC' \
  "$H3K27AC_PEAKS" --type interval --name 'H3K27ac peaks' --height 24 \
    --color '#D95F02' --no-labels \
  "$PROMOTERS" --name 'GENCODE v47 promoters' --height 36 --no-labels \
  "$DOMAINS" --style domain --side above --color '#6A3D9A' \
    --fill '#998EC330' --dashed \
  "$LOOPS" --style loop --side above --fill '#FFFFFF55' \
    --score-filter-min 40 --colormap viridis --score-opacity 0.4,1 \
    --score-line-width 0.6,2.2 --score-size 0.8,1.5 \
  --out examples/gallery/generated/01_pyramid_tracks
```

![Pyramid contact map with ENCODE compartment, H3K27ac, promoter, domain, and loop tracks](generated/01_pyramid_tracks.png)

## 2. Rich square map with both-axis signal and virtual 4C

The same compartment bigWig appears above and to the left of the square map.
A direct virtual 4C profile is extracted from the `.hic`, and its viewpoint is
marked by a translucent vertical band. Domains are restricted to the upper
triangle; score-styled loops are mirrored into both triangles.

```bash
"$GRE_PLOT" "$HIC" chr8:126500000-130000000 \
  --layout square --norm SCALE --resolution 25000 --map-percentile 0.995 \
  --width 7 --dpi 300 --formats png --theme publication \
  --title 'K562 — square map with 1D and 2D annotation' \
  "$COMPARTMENTS" --name 'PC1' --axis both --height 28 \
    --style area --color '#B2182B' --neg-color '#2166AC' \
  --virtual4c chr8:127700000-127750000 --name 'virtual 4C' --axis x \
    --style line --height 34 --color '#008837' --line-width 1.4 \
  "$DOMAINS" --style domain --side above --color '#542788' \
    --fill '#998EC330' --line-width 1 \
  "$LOOPS" --style loop --side both --fill '#FFFFFF55' \
    --score-filter-min 40 --colormap viridis --score-opacity 0.45,1 \
    --score-line-width 0.7,2.2 --score-size 0.8,1.5 \
  --v-highlight chr8:127700000-127750000 \
    --highlight-color '#00A06020' --highlight-border '#008837' \
  --out examples/gallery/generated/02_square_rich_annotations
```

![Square Hi-C map with both-axis PC1, virtual 4C, domains, loops, and a viewpoint highlight](generated/02_square_rich_annotations.png)

## 3. VS mode: observed above, observed/expected below

VS mode does not have to compare two samples. Here the same remote matrix is
opened with two display modes: raw observed contacts above the diagonal and
observed/expected below. Each half has its own colour map, scale, and linked
legend. The BEDPE loop layer is drawn in both halves.

```bash
"$GRE_PLOT" "$HIC" chr8:126500000-130000000 \
  --layout square --norm SCALE --resolution 25000 \
  --map-colors reds --map-percentile 0.995 \
  --vs "$HIC" --vs-oe --vs-side below --vs-map-colors rd_bu \
    --vs-map-min 0.25 --vs-map-max 4 \
  --width 7 --dpi 300 --formats png --theme publication \
  --title 'K562 — observed contacts vs observed/expected' \
  "$LOOPS" --style loop --side both --color '#202020' \
    --fill '#FFFFFF40' --line-width 0.9 \
  --diagonal --out examples/gallery/generated/03_vs_observed_expected
```

![Split square map with observed contacts above and observed over expected below](generated/03_vs_observed_expected.png)

## 4. Three square panels with one fitted contact scale

The vertically stacked square panels show different chromosomes and spans.
`--shared-map-scale` pools
their displayed finite contact values before fitting, so all three colour bars
resolve to the same limits and the same red means the same contact count.
Tracks and loops are repeated after each `--panel` because annotations are
panel-scoped.

```bash
"$GRE_PLOT" "$HIC" chr8:126500000-130000000 \
  --layout square --norm SCALE --resolution 25000 \
  --shared-map-scale --panel-spacing 16 \
  --width 7 --dpi 300 --formats png --theme publication \
  --title 'K562 — three square maps on one shared contact scale' \
  --panel-title 'MYC neighborhood' \
  "$COMPARTMENTS" --name 'A/B compartment' --height 24 \
    --color '#B2182B' --neg-color '#2166AC' \
  "$LOOPS" --style loop --side both --color '#303030' --line-width 0.8 \
  --panel chr10:15500000-18300000 --panel-title 'chr10 loop-rich locus A' \
  "$COMPARTMENTS" --name 'A/B compartment' --height 24 \
    --color '#B2182B' --neg-color '#2166AC' \
  "$LOOPS" --style loop --side both --color '#303030' --line-width 0.8 \
  --panel chr10:50500000-52500000 --panel-title 'chr10 loop-rich locus B' \
  "$COMPARTMENTS" --name 'A/B compartment' --height 24 \
    --color '#B2182B' --neg-color '#2166AC' \
  "$LOOPS" --style loop --side both --color '#303030' --line-width 0.8 \
  --out examples/gallery/generated/04_multi_panel_shared_scale
```

![Three vertically stacked square contact maps sharing one fitted contact scale](generated/04_multi_panel_shared_scale.png)

## 5. Dark off-diagonal rectangle

Rectangle mode treats x and y as independent regions. This example displays a
block away from the main diagonal, overlays oriented loop boxes, and projects
separate supplied genomic intervals across x and y as cyan and yellow bands.

```bash
"$GRE_PLOT" "$HIC" chr8:126500000-128300000 \
  --layout rectangle --region-y chr8:128000000-130000000 \
  --norm SCALE --resolution 10000 --map-height 260 --map-linear \
  --map-colors magma --map-percentile 0.995 \
  --width 7 --dpi 300 --formats png --theme dark \
  --title 'K562 — off-diagonal contact block' \
  "$LOOPS" --style box --fill '#FFFFFF18' --colormap plasma \
    --score-filter-min 40 --score-opacity 0.45,1 \
    --score-line-width 0.6,2 --score-size 0.8,1.3 \
  --v-highlight chr8:127700000-127750000 \
    --highlight-color '#00E5FF20' --highlight-border '#00E5FF' \
  --h-highlight chr8:129650000-129725000 \
    --highlight-color '#FFEA0020' --highlight-border '#FFEA00' \
  --out examples/gallery/generated/05_off_diagonal_rectangle
```

![Dark off-diagonal rectangular contact block with loop boxes and orthogonal highlights](generated/05_off_diagonal_rectangle.png)

## 6. Supplied loops as arcs, transcript models, and a blue map

The same ENCODE BEDPE loops can be represented as a separate one-dimensional
arc track instead of markers on the matrix. Arc endpoint positions come
directly from the supplied anchors; separation controls the base height, while
the BEDPE observed score controls colour, opacity, stroke width, and a height
multiplier. Longer arcs are painted first so short local loops remain visible.

This example also demonstrates a true transcript-model row and two independent
quantitative assays. H3K27ac is an orange filled area, ATAC-seq is a teal line,
and the Hi-C matrix uses the sequential `blues` palette. Supplied domains remain
a dashed 2D overlay on the contact map.

```bash
"$GRE_PLOT" "$HIC" chr8:126500000-130000000 \
  --layout pyramid --norm SCALE --resolution 25000 --max-distance 700000 \
  --map-colors blues --map-percentile 0.995 \
  --width 7 --label-width 110 --dpi 300 --formats png --theme publication \
  --title 'K562 — loops as arcs above a blue contact map' \
  "$TRANSCRIPTS" --name 'K562 transcript models' --height 70 \
    --row-height 13 --color '#1B7837' --no-labels \
  "$H3K27AC_SIGNAL" --name 'H3K27ac –log10(p)' --height 34 \
    --style area --color '#D95F0E' --percentile 0.995 \
  "$ATAC_SIGNAL" --name 'ATAC –log10(p)' --height 32 \
    --style line --color '#008B8B' --line-width 1.1 --percentile 0.995 \
  "$LOOPS" --style arc --name 'loops (observed)' --height 180 \
    --score-filter-min 40 --colormap plasma --score-opacity 0.45,1 \
    --score-line-width 0.7,2.4 --fill '#7A017720' \
  "$DOMAINS" --style domain --side above --color '#4D4D4D' --dashed \
  --out examples/gallery/generated/06_arc_loops_and_genes
```

![Blue pyramid map with transcript models, H3K27ac, ATAC-seq, and score-styled BEDPE arcs](generated/06_arc_loops_and_genes.png)

## 7. Tracks-only figure with area, bars, points, and intervals

GRE can compose 1D figures without drawing a contact matrix. This dark-theme
example uses `--no-map` and intentionally gives every layer a different visual
grammar: H3K27ac as an orange area with a proper y axis, ATAC-seq as turquoise
bars using maximum aggregation, CTCF as magenta points, scored H3K27ac peaks
with `viridis`, and GENCODE promoters as yellow intervals.

```bash
"$GRE_PLOT" "$HIC" chr8:127200000-128200000 \
  --no-map --norm SCALE --width 7 --label-width 110 --dpi 300 --formats png --theme dark \
  --title 'K562 — 1D track styles at MYC' \
  --subtitle 'area, bars, points, and scored intervals from public ENCODE URLs' \
  "$H3K27AC_SIGNAL" --name 'H3K27ac area' --height 46 \
    --style area --color '#FF9F1C' --percentile 0.995 --y-axis \
  "$ATAC_SIGNAL" --name 'ATAC bars' --height 44 \
    --style bars --color '#2EC4B6' --percentile 0.995 --aggregate max \
  "$CTCF_SIGNAL" --name 'CTCF points' --height 42 \
    --style points --color '#E056FD' --line-width 1.4 --percentile 0.995 \
  "$H3K27AC_PEAKS" --type interval --name 'H3K27ac peaks' --height 24 \
    --colormap viridis --no-labels \
  "$PROMOTERS" --type interval --name 'GENCODE promoters' --height 24 \
    --color '#F4D35E' --no-labels \
  --out examples/gallery/generated/07_one_dimensional_tracks
```

![Dark tracks-only figure with area, bars, points, scored peaks, and promoter intervals](generated/07_one_dimensional_tracks.png)

## Reproducibility notes

- These examples use the file's resolution-specific `SCALE` vectors. This
  legacy version-9 `.hic` advertises only `NONE` in its top-level metadata,
  but direct `SCALE` queries succeed at the 10 kb and 25 kb resolutions used
  here. Normalization availability can be resolution-specific, so test the
  requested normalization and resolution together for other public files.
- URLs are quoted because `@` and other URL characters should reach GRE
  unchanged. The ENCODE URLs shown here do not currently contain shell `&`
  characters, but quoting remains the safe default.
- HTTP errors, range-request failures, or portal maintenance can interrupt a
  remote render. Re-running the script is safe; each output PNG is replaced
  only after its command completes successfully.
- The 44.9 MB transcript GTF used by example 6 is streamed because GTF is not
  indexed for genomic range requests. The `.hic` and bigWig/bigBed inputs are
  range-queried and are not downloaded in full.
- BEDPE column 8 is empty in these Juicer outputs. GRE recognizes the common
  ENCODE/Juicer layout and uses the quantitative column after itemRgb (column
  12: observed loop count or domain score) as a fallback score.
- The PNGs are committed so the README remains viewable when offline. Rebuild
  them when renderer defaults, data adapters, or gallery commands change.
