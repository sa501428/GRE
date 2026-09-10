#include "gre/tracks/gene_track.hpp"

#include <algorithm>
#include <cmath>

#include "gre/core/error.hpp"

namespace gre {
namespace {

constexpr double kLabelGap = 3.0;
constexpr double kArrowSpacing = 26.0;

}  // namespace

GeneTrack::GeneTrack(FeatureSourcePtr source) : source_(std::move(source)) {
    if (source_ == nullptr) {
        throw Error(ErrorCode::invalid_argument, "GeneTrack needs a feature source");
    }
}

GeneTrack::GeneTrack(std::vector<Feature> features)
    : features_(std::move(features)), have_data_(true) {}

GeneTrack& GeneTrack::color(Color value) {
    color_ = value;
    return *this;
}

GeneTrack& GeneTrack::row_height(double value) {
    row_height_ = std::max(1.0, value);
    return *this;
}

GeneTrack& GeneTrack::exon_height(double value) {
    exon_height_ = std::max(1.0, value);
    return *this;
}

GeneTrack& GeneTrack::utr_height(double value) {
    utr_height_ = std::max(1.0, value);
    return *this;
}

GeneTrack& GeneTrack::max_rows(int value) {
    max_rows_ = std::max(1, value);
    return *this;
}

GeneTrack& GeneTrack::collapsed(bool value) {
    collapsed_ = value;
    return *this;
}

GeneTrack& GeneTrack::show_labels(bool value) {
    show_labels_ = value;
    return *this;
}

GeneTrack& GeneTrack::show_arrows(bool value) {
    show_arrows_ = value;
    return *this;
}

GeneTrack& GeneTrack::label_font_size(double value) {
    label_font_size_ = value;
    return *this;
}

double GeneTrack::default_height() const {
    return std::max(1, rows_) * row_height_;
}

void GeneTrack::prepare(const ViewContext& context) {
    capture_view(context);
    if (source_ != nullptr) {
        features_ = source_->query(context.x_region);
        have_data_ = true;
    }
    placed_.clear();
    rows_ = 1;
    if (features_.empty()) return;

    std::sort(features_.begin(), features_.end(), [](const Feature& a, const Feature& b) {
        if (a.start != b.start) return a.start < b.start;
        return a.end < b.end;
    });

    const GenomicTransform transform{context.x_region, context.content.left(),
                                     context.content.right()};
    const double font_size =
        label_font_size_ > 0.0
            ? label_font_size_
            : (context.theme != nullptr ? context.theme->small_font_size : 6.5);
    const Font& font =
        fonts().get(context.theme != nullptr ? context.theme->font : FontId{});

    // Pack features into rows.  A feature occupies its own span plus the width
    // of its label, so names do not collide with the next gene along.
    std::vector<double> row_end;
    placed_.reserve(features_.size());
    for (std::size_t i = 0; i < features_.size(); ++i) {
        const Feature& feature = features_[i];
        const double x0 = transform.x(feature.start);
        const double x1 = transform.x(feature.end);
        const double label_width =
            show_labels_ && !feature.name.empty() ? font.width(feature.name, font_size) : 0.0;

        Placed entry;
        entry.index = i;
        entry.label_visible = label_width > 0.0;

        // Prefer a label to the right; fall back to the left when that would
        // run off the plot.
        entry.label_after = x1 + kLabelGap + label_width <= context.content.right();
        const double occupied_start =
            entry.label_after ? x0 : x0 - kLabelGap - label_width;
        const double occupied_end = entry.label_after ? x1 + kLabelGap + label_width : x1;

        int row = 0;
        if (collapsed_) {
            row = 0;
        } else {
            while (row < static_cast<int>(row_end.size()) &&
                   row_end[static_cast<std::size_t>(row)] > occupied_start) {
                ++row;
            }
            if (row >= max_rows_) {
                // Out of rows: drop the label and try again on the least
                // crowded row rather than hiding the gene entirely.
                entry.label_visible = false;
                row = 0;
                double best = row_end.empty() ? 0.0 : row_end[0];
                for (std::size_t r = 1; r < row_end.size(); ++r) {
                    if (row_end[r] < best) {
                        best = row_end[r];
                        row = static_cast<int>(r);
                    }
                }
            }
        }
        if (row >= static_cast<int>(row_end.size())) row_end.resize(static_cast<std::size_t>(row) + 1, 0.0);
        row_end[static_cast<std::size_t>(row)] =
            std::max(row_end[static_cast<std::size_t>(row)],
                     entry.label_visible ? occupied_end : x1 + kLabelGap);
        entry.row = row;
        placed_.push_back(entry);
    }
    rows_ = std::max<int>(1, static_cast<int>(row_end.size()));
}

void GeneTrack::draw(Canvas& canvas, const TrackRect& rect) const {
    if (placed_.empty() || rect.content.empty()) return;
    const Theme& theme_ref = theme();
    const Color base = color_.transparent() ? theme_ref.gene_color : color_;
    const double font_size =
        label_font_size_ > 0.0 ? label_font_size_ : theme_ref.small_font_size;
    const double row_height = rect.content.height / std::max(1, rows_);

    TextStyle label_style;
    label_style.font = theme_ref.font;
    label_style.size = font_size;
    label_style.valign = VerticalAlign::middle;

    for (const Placed& entry : placed_) {
        const Feature& feature = features_[entry.index];
        const Color ink = feature.color.value_or(base);
        const double centre = rect.content.top() + (entry.row + 0.5) * row_height;
        const double x0 = rect.x.x(feature.start);
        const double x1 = rect.x.x(feature.end);

        // Intron line spanning the whole gene.
        StrokeStyle line;
        line.color = ink;
        line.width = std::max(0.6, theme_ref.axis_line_width);
        canvas.stroke_line(Point{x0, centre}, Point{x1, centre}, line);

        if (show_arrows_ && feature.strand != Strand::unknown && x1 - x0 > kArrowSpacing) {
            const int direction = feature.strand == Strand::forward ? 1 : -1;
            const double arrow_height = std::min(exon_height_ * 0.55, row_height * 0.4);
            const double start = std::max(x0, rect.content.left());
            const double stop = std::min(x1, rect.content.right());
            for (double x = start + kArrowSpacing / 2.0; x < stop; x += kArrowSpacing) {
                canvas.fill_arrow(Point{x + direction * 2.0, centre}, 3.0, arrow_height,
                                  direction, ink);
            }
        }

        // Exons.  With no exon list the whole feature is one block.
        const auto draw_block = [&](std::int64_t start, std::int64_t end, double height) {
            if (end <= start) return;
            const double left = rect.x.x(start);
            const double right = rect.x.x(end);
            canvas.fill_rect(Rect{left, centre - height / 2.0, std::max(right - left, 0.5), height},
                             ink);
        };

        if (feature.exons.empty()) {
            draw_block(feature.start, feature.end, exon_height_);
        } else {
            for (const Exon& exon : feature.exons) {
                if (!feature.has_thick()) {
                    draw_block(exon.start, exon.end, exon_height_);
                    continue;
                }
                // Split each exon into UTR and coding parts so the coding
                // region reads as the thicker box.
                const std::int64_t coding_start = std::max(exon.start, feature.thick_start);
                const std::int64_t coding_end = std::min(exon.end, feature.thick_end);
                draw_block(exon.start, std::min(exon.end, feature.thick_start), utr_height_);
                draw_block(std::max(exon.start, feature.thick_end), exon.end, utr_height_);
                draw_block(coding_start, coding_end, exon_height_);
            }
        }

        if (show_labels_ && entry.label_visible && !feature.name.empty()) {
            label_style.color = ink;
            label_style.align = entry.label_after ? TextAlign::left : TextAlign::right;
            const double x = entry.label_after ? x1 + kLabelGap : x0 - kLabelGap;
            canvas.draw_text(Point{x, centre}, feature.name, label_style);
        }
    }
}

}  // namespace gre
