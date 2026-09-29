# GRE ENCODE gallery

These figures are generated entirely from released public ENCODE files. The
large `.hic` is queried in place with HTTP byte ranges; indexed bigWig and
bigBed files are also queried remotely. The small compressed BEDPE and GTF
files are streamed directly and held in memory for the duration of a render.
No ENCODE input needs to be downloaded or checked into this repository.

The committed PNGs were generated at 150 DPI with:

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

The Hi-C, compartment, loop, and domain files belong to released K562 in situ
Hi-C experiment [ENCSR545YBD](https://www.encodeproject.org/experiments/ENCSR545YBD/).
The H3K27ac peaks belong to released K562 Histone ChIP-seq experiment
[ENCSR000AKP](https://www.encodeproject.org/experiments/ENCSR000AKP/).

The commands below assume these shell variables, exactly as the generator
does:

```bash
HIC='https://www.encodeproject.org/files/ENCFF080DPJ/@@download/ENCFF080DPJ.hic'
COMPARTMENTS='https://www.encodeproject.org/files/ENCFF699RSL/@@download/ENCFF699RSL.bigWig'
LOOPS='https://www.encodeproject.org/files/ENCFF134HIZ/@@download/ENCFF134HIZ.bedpe.gz'
DOMAINS='https://www.encodeproject.org/files/ENCFF173VDJ/@@download/ENCFF173VDJ.bedpe.gz'
H3K27AC_PEAKS='https://www.encodeproject.org/files/ENCFF045OHM/@@download/ENCFF045OHM.bigBed'
PROMOTERS='https://www.encodeproject.org/files/ENCFF005MUK/@@download/ENCFF005MUK.gtf.gz'
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
  --layout pyramid --norm NONE --resolution 25000 --max-distance 1200000 \
  --width 7 --dpi 150 --formats png --theme publication \
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
  --layout square --norm NONE --resolution 25000 --map-percentile 0.995 \
  --width 7 --dpi 150 --formats png --theme publication \
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
  --layout square --norm NONE --resolution 25000 \
  --map-colors reds --map-percentile 0.995 \
  --vs "$HIC" --vs-oe --vs-side below --vs-map-colors rd_bu \
    --vs-map-min 0.25 --vs-map-max 4 \
  --width 7 --dpi 150 --formats png --theme publication \
  --title 'K562 — observed contacts vs observed/expected' \
  "$LOOPS" --style loop --side both --color '#202020' \
    --fill '#FFFFFF40' --line-width 0.9 \
  --diagonal --out examples/gallery/generated/03_vs_observed_expected
```

![Split square map with observed contacts above and observed over expected below](generated/03_vs_observed_expected.png)

## 4. Three panels with one fitted contact scale

The panels show different chromosomes and spans. `--shared-map-scale` pools
their displayed finite contact values before fitting, so all three colour bars
resolve to the same limits and the same red means the same contact count.
Tracks and loops are repeated after each `--panel` because annotations are
panel-scoped.

```bash
"$GRE_PLOT" "$HIC" chr8:126500000-130000000 \
  --layout pyramid --norm NONE --resolution 25000 --max-distance 1000000 \
  --shared-map-scale --panel-spacing 16 \
  --width 7 --dpi 150 --formats png --theme publication \
  --title 'K562 — three loci on one shared contact scale' \
  --panel-title 'MYC neighborhood' \
  "$COMPARTMENTS" --name 'A/B compartment' --height 24 \
    --color '#B2182B' --neg-color '#2166AC' \
  "$LOOPS" --style loop --side above --color '#303030' --line-width 0.8 \
  --panel chr10:15500000-18300000 --panel-title 'chr10 loop-rich locus A' \
  "$COMPARTMENTS" --name 'A/B compartment' --height 24 \
    --color '#B2182B' --neg-color '#2166AC' \
  "$LOOPS" --style loop --side above --color '#303030' --line-width 0.8 \
  --panel chr10:50500000-52500000 --panel-title 'chr10 loop-rich locus B' \
  "$COMPARTMENTS" --name 'A/B compartment' --height 24 \
    --color '#B2182B' --neg-color '#2166AC' \
  "$LOOPS" --style loop --side above --color '#303030' --line-width 0.8 \
  --out examples/gallery/generated/04_multi_panel_shared_scale
```

![Three vertically stacked loci sharing one fitted contact scale](generated/04_multi_panel_shared_scale.png)

## 5. Dark off-diagonal rectangle

Rectangle mode treats x and y as independent regions. This example displays a
block away from the main diagonal, overlays oriented loop boxes, and projects
separate supplied genomic intervals across x and y as cyan and yellow bands.

```bash
"$GRE_PLOT" "$HIC" chr8:126500000-128300000 \
  --layout rectangle --region-y chr8:128000000-130000000 \
  --norm NONE --resolution 10000 --map-height 260 --map-linear \
  --map-colors magma --map-percentile 0.995 \
  --width 7 --dpi 150 --formats png --theme dark \
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

## Reproducibility notes

- These examples intentionally use `--norm NONE` because `ENCFF080DPJ`
  advertises `NONE` through its `.hic` metadata. GRE reports available
  normalizations before rendering; do not assume `KR` or `SCALE` exists in
  every public file.
- URLs are quoted because `@` and other URL characters should reach GRE
  unchanged. The ENCODE URLs shown here do not currently contain shell `&`
  characters, but quoting remains the safe default.
- HTTP errors, range-request failures, or portal maintenance can interrupt a
  remote render. Re-running the script is safe; each output PNG is replaced
  only after its command completes successfully.
- BEDPE column 8 is empty in these Juicer outputs. GRE recognizes the common
  ENCODE/Juicer layout and uses the quantitative column after itemRgb (column
  12: observed loop count or domain score) as a fallback score.
- The PNGs are committed so the README remains viewable when offline. Rebuild
  them when renderer defaults, data adapters, or gallery commands change.
