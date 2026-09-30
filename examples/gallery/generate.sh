#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
GRE_PLOT="${GRE_PLOT:-$REPO_DIR/build/tools/gre_plot}"
OUT_DIR="${OUT_DIR:-$SCRIPT_DIR/generated}"

mkdir -p "$OUT_DIR"

# Independent figures can fetch and render concurrently. Keep the default
# modest so the ENCODE range server and local memory are not overloaded.
GALLERY_JOBS="${GALLERY_JOBS:-2}"
pids=()
run_plot() {
  local output_path="${@: -1}"
  local filename="${output_path##*/}"
  local number="${filename%%_*}"
  if [[ -n "${GALLERY_ONLY:-}" && ",${GALLERY_ONLY}," != *",${number},"* ]]; then
    return
  fi
  "$GRE_PLOT" "$@" &
  pids+=("$!")
  if ((${#pids[@]} >= GALLERY_JOBS)); then
    wait "${pids[0]}"
    pids=("${pids[@]:1}")
  fi
}

HIC='https://www.encodeproject.org/files/ENCFF621AIY/@@download/ENCFF621AIY.hic'
GM12878_HIC='https://www.encodeproject.org/files/ENCFF070CHZ/@@download/ENCFF070CHZ.hic'
COMPARTMENTS='https://www.encodeproject.org/files/ENCFF944MHS/@@download/ENCFF944MHS.bigWig'
LOOPS='https://www.encodeproject.org/files/ENCFF256ZMD/@@download/ENCFF256ZMD.bedpe.gz'
DOMAINS='https://www.encodeproject.org/files/ENCFF126GED/@@download/ENCFF126GED.bedpe.gz'
H3K27AC_PEAKS='https://www.encodeproject.org/files/ENCFF045OHM/@@download/ENCFF045OHM.bigBed'
PROMOTERS='https://www.encodeproject.org/files/ENCFF005MUK/@@download/ENCFF005MUK.gtf.gz'
H3K27AC_SIGNAL='https://www.encodeproject.org/files/ENCFF094XCU/@@download/ENCFF094XCU.bigWig'
ATAC_SIGNAL='https://www.encodeproject.org/files/ENCFF357GNC/@@download/ENCFF357GNC.bigWig'
CTCF_SIGNAL='https://www.encodeproject.org/files/ENCFF336UPT/@@download/ENCFF336UPT.bigWig'
TRANSCRIPTS='https://www.encodeproject.org/files/ENCFF688NNQ/@@download/ENCFF688NNQ.gtf.gz'

run_plot "$HIC" chr8:126500000-130000000 \
  --layout pyramid --norm SCALE --resolution 25000 --max-distance 1200000 \
  --map-linear --map-colors fall --map-max 100 \
  --width 7 --dpi 300 --formats png --theme publication \
  --title 'K562 — MYC neighborhood' \
  "$COMPARTMENTS" --name 'A/B compartment' --height 28 --style area \
    --color '#B2182B' --neg-color '#2166AC' \
  "$H3K27AC_PEAKS" --type interval --name 'H3K27ac peaks' --height 24 \
    --color '#D95F02' --no-labels \
  "$PROMOTERS" --name 'GENCODE v47 promoters' --height 36 --no-labels \
  "$DOMAINS" --style domain --side above --color '#00BFD8' \
    --fill '#00BFD814' \
  "$LOOPS" --style loop --side above --color '#4D5563' \
    --score-filter-min 50 --line-width 0.65 --score-size 0.55,0.55 \
  --out "$OUT_DIR/01_pyramid_tracks"

run_plot "$HIC" chr8:126500000-130000000 \
  --layout square --norm SCALE --resolution 25000 --map-linear \
  --map-colors reds --map-percentile 0.90 \
  --width 7 --dpi 300 --formats png --theme publication \
  --title 'K562 — square map with 1D and 2D annotation' \
  "$COMPARTMENTS" --name 'PC1' --axis both --height 28 \
    --style area --color '#B2182B' --neg-color '#2166AC' \
  "$DOMAINS" --style domain --side above --color '#F5D547' \
    --fill '#F5D54714' --line-width 0.9 \
  "$LOOPS" --style loop --side above --color '#00BFD8' \
    --line-width 0.8 --score-size 0.5,0.5 \
  --v-highlight chr8:127700000-127750000 \
    --highlight-color '#00A06020' --highlight-border '#008837' \
  --out "$OUT_DIR/02_square_rich_annotations"

run_plot "$HIC" chr8:126500000-130000000 \
  --layout square --norm SCALE --resolution 25000 \
  --map-colors reds --map-linear --map-percentile 0.90 \
  --vs "$GM12878_HIC" --vs-norm SCALE --vs-side below \
    --vs-map-colors blues --vs-map-linear --vs-map-percentile 0.90 \
  --width 7 --dpi 300 --formats png --theme publication \
  --title 'Intact Hi-C — K562 above, GM12878 below' \
  --diagonal --out "$OUT_DIR/03_k562_vs_gm12878"

run_plot "$HIC" chr8:126500000-130000000 \
  --layout square --norm SCALE --resolution 25000 \
  --shared-map-scale --panel-spacing 16 --map-linear \
  --map-colors magma --map-percentile 0.90 \
  --width 7 --dpi 300 --formats png --theme publication \
  --title 'K562 — three square maps on one shared contact scale' \
  --panel-title 'MYC neighborhood' \
  "$COMPARTMENTS" --name 'A/B compartment' --height 24 \
    --color '#B2182B' --neg-color '#2166AC' \
  --panel chr10:15500000-18300000 --panel-title 'chr10 loop-rich locus A' \
  "$COMPARTMENTS" --name 'A/B compartment' --height 24 \
    --color '#B2182B' --neg-color '#2166AC' \
  --panel chr10:50500000-52500000 --panel-title 'chr10 loop-rich locus B' \
  "$COMPARTMENTS" --name 'A/B compartment' --height 24 \
    --color '#B2182B' --neg-color '#2166AC' \
  --out "$OUT_DIR/04_multi_panel_shared_scale"

run_plot "$HIC" chr8:126500000-128300000 \
  --layout rectangle --region-y chr8:128000000-130000000 \
  --norm SCALE --resolution 10000 --map-height 390 --map-linear \
  --map-colors magma --map-percentile 0.97 \
  --width 7 --dpi 500 --formats png --theme dark \
  --title 'K562 — off-diagonal contact block' \
  "$LOOPS" --style box --fill '#FFFFFF12' --color '#B4BFC9' \
    --score-filter-min 60 --line-width 0.6 --score-size 0.7,0.7 \
  --v-highlight chr8:127700000-127750000 \
    --highlight-color '#00E5FF20' --highlight-border '#00E5FF' \
  --h-highlight chr8:129650000-129725000 \
    --highlight-color '#FFEA0020' --highlight-border '#FFEA00' \
  --out "$OUT_DIR/05_off_diagonal_rectangle"

run_plot "$HIC" chr8:126500000-130000000 \
  --layout pyramid --norm SCALE --resolution 25000 \
  --map-colors blues --map-linear --map-percentile 0.90 \
  --width 7 --label-width 110 --dpi 300 --formats png --theme publication \
  --title 'K562 — loops as arcs above a blue contact map' \
  "$TRANSCRIPTS" --name 'K562 transcript models' --height 90 \
    --row-height 16 --representative-transcripts --color '#1B7837' \
  "$H3K27AC_SIGNAL" --name 'H3K27ac –log10(p)' --height 34 \
    --style area --color '#D95F0E' --percentile 0.995 \
  "$ATAC_SIGNAL" --name 'ATAC –log10(p)' --height 32 \
    --style line --color '#008B8B' --line-width 1.1 --percentile 0.995 \
  "$LOOPS" --style arc --name 'loops (observed)' --height 175 \
    --score-filter-min 20 --color '#753A8A' --line-width 1.0 \
  "$DOMAINS" --style domain --side above --color '#4D4D4D' --dashed \
  --out "$OUT_DIR/06_arc_loops_and_genes"

run_plot "$HIC" chr8:127200000-128200000 \
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
  --out "$OUT_DIR/07_one_dimensional_tracks"

run_plot "$HIC" chr8:127000000-128000000 \
  --layout square --norm SCALE --resolution 5000 \
  --map-linear --map-colors viridis --map-percentile 0.93 \
  --width 7 --dpi 300 --formats png --theme publication \
  --title 'K562 intact Hi-C — MYC at 5 kb' \
  "$LOOPS" --style loop --side above --color '#59636F' \
    --score-filter-min 50 --line-width 0.6 --score-size 0.5,0.5 \
  --out "$OUT_DIR/08_intact_5kb_viridis"

run_plot "$HIC" chr8:127450000-127950000 \
  --layout square --norm SCALE --resolution 2000 \
  --map-linear --map-colors blues --map-percentile 0.93 \
  --width 7 --dpi 500 --formats png --theme publication \
  --title 'K562 intact Hi-C — local contacts at 2 kb' \
  "$LOOPS" --style loop --side above --color '#E31A1C' \
    --line-width 0.8 --score-size 0.5,0.5 \
  --out "$OUT_DIR/09_intact_2kb_blues"

run_plot "$HIC" chr8:115000000-135000000 \
  --layout pyramid --norm SCALE --resolution 50000 --max-distance 8000000 \
  --map-linear --map-colors reds --map-max 75 \
  --width 7 --dpi 300 --formats png --theme publication \
  --title 'K562 intact Hi-C — chr8 overview at 50 kb' \
  "$COMPARTMENTS" --name 'A/B compartment' --height 30 --style area \
    --color '#B2182B' --neg-color '#2166AC' \
  --out "$OUT_DIR/10_intact_50kb_reds"

for pid in "${pids[@]}"; do
  wait "$pid"
done

printf 'Generated gallery PNGs in %s\n' "$OUT_DIR"
