#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
GRE_PLOT="${GRE_PLOT:-$REPO_DIR/build/tools/gre_plot}"
OUT_DIR="${OUT_DIR:-$SCRIPT_DIR/generated}"

mkdir -p "$OUT_DIR"

HIC='https://www.encodeproject.org/files/ENCFF080DPJ/@@download/ENCFF080DPJ.hic'
COMPARTMENTS='https://www.encodeproject.org/files/ENCFF699RSL/@@download/ENCFF699RSL.bigWig'
LOOPS='https://www.encodeproject.org/files/ENCFF134HIZ/@@download/ENCFF134HIZ.bedpe.gz'
DOMAINS='https://www.encodeproject.org/files/ENCFF173VDJ/@@download/ENCFF173VDJ.bedpe.gz'
H3K27AC_PEAKS='https://www.encodeproject.org/files/ENCFF045OHM/@@download/ENCFF045OHM.bigBed'
PROMOTERS='https://www.encodeproject.org/files/ENCFF005MUK/@@download/ENCFF005MUK.gtf.gz'

"$GRE_PLOT" "$HIC" chr8:126500000-130000000 \
  --layout pyramid --norm SCALE --resolution 25000 --max-distance 1200000 \
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
  --out "$OUT_DIR/01_pyramid_tracks"

"$GRE_PLOT" "$HIC" chr8:126500000-130000000 \
  --layout square --norm SCALE --resolution 25000 --map-percentile 0.995 \
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
  --out "$OUT_DIR/02_square_rich_annotations"

"$GRE_PLOT" "$HIC" chr8:126500000-130000000 \
  --layout square --norm SCALE --resolution 25000 \
  --map-colors reds --map-percentile 0.995 \
  --vs "$HIC" --vs-oe --vs-side below --vs-map-colors rd_bu \
    --vs-map-min 0.25 --vs-map-max 4 \
  --width 7 --dpi 150 --formats png --theme publication \
  --title 'K562 — observed contacts vs observed/expected' \
  "$LOOPS" --style loop --side both --color '#202020' \
    --fill '#FFFFFF40' --line-width 0.9 \
  --diagonal --out "$OUT_DIR/03_vs_observed_expected"

"$GRE_PLOT" "$HIC" chr8:126500000-130000000 \
  --layout square --norm SCALE --resolution 25000 \
  --shared-map-scale --panel-spacing 16 \
  --width 7 --dpi 150 --formats png --theme publication \
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
  --out "$OUT_DIR/04_multi_panel_shared_scale"

"$GRE_PLOT" "$HIC" chr8:126500000-128300000 \
  --layout rectangle --region-y chr8:128000000-130000000 \
  --norm SCALE --resolution 10000 --map-height 260 --map-linear \
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
  --out "$OUT_DIR/05_off_diagonal_rectangle"

printf 'Generated gallery PNGs in %s\n' "$OUT_DIR"
