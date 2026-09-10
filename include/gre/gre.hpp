#pragma once

// Umbrella header: genomic data in, publication-quality PNG/PDF/SVG out.
//
//   #include <gre/gre.hpp>
//
//   gre::Figure fig;
//   auto& panel = fig.add_panel();
//   panel.set_region("chr8", 127'000'000, 129'000'000);
//   panel.add_track(gre::AxisTrack{});
//   panel.add_track(gre::GeneTrack{genes});
//   panel.add_track(gre::SignalTrack{atac}.height(50));
//   panel.add_track(gre::HeatmapTrack{hic}.mode(gre::HeatmapMode::triangle));
//   fig.save_png("figure.png", 300);
//   fig.save_pdf("figure.pdf");

#include "gre/core/color.hpp"
#include "gre/core/error.hpp"
#include "gre/core/genomic_transform.hpp"
#include "gre/core/geometry.hpp"
#include "gre/core/scale.hpp"

#include "gre/data/feature_source.hpp"
#include "gre/data/igv_sources.hpp"
#include "gre/data/matrix_source.hpp"
#include "gre/data/memory_sources.hpp"
#include "gre/data/signal_source.hpp"
#include "gre/data/straw_matrix_source.hpp"

#include "gre/figure/figure.hpp"
#include "gre/figure/panel.hpp"
#include "gre/figure/theme.hpp"
#include "gre/figure/track.hpp"

#include "gre/render/canvas.hpp"
#include "gre/render/image.hpp"
#include "gre/render/pdf_canvas.hpp"
#include "gre/render/raster_canvas.hpp"
#include "gre/render/svg_canvas.hpp"
#include "gre/render/text.hpp"

#include "gre/tracks/axis_track.hpp"
#include "gre/tracks/gene_track.hpp"
#include "gre/tracks/heatmap_track.hpp"
#include "gre/tracks/interval_track.hpp"
#include "gre/tracks/legend_track.hpp"
#include "gre/tracks/signal_track.hpp"

#include "gre/export/png.hpp"
