#include "gre/tracks/arc_track.hpp"

#include <algorithm>
#include <cmath>

namespace gre {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::int64_t midpoint(const GenomicRegion& region) {
    return region.start + region.span() / 2;
}

GenomicRegion expanded(GenomicRegion region, std::int64_t bases) {
    region.start = std::max<std::int64_t>(0, region.start - bases);
    region.end += bases;
    return region;
}

}  // namespace

ArcTrack::ArcTrack(PairFeatureSourcePtr source) : layer_(std::move(source)) {}

ArcTrack& ArcTrack::color(Color value) {
    layer_.color(value);
    return *this;
}
ArcTrack& ArcTrack::fill(Color value) {
    layer_.fill(value);
    return *this;
}
ArcTrack& ArcTrack::line_width(double value) {
    layer_.line_width(value);
    return *this;
}
ArcTrack& ArcTrack::dash(std::vector<double> value) {
    layer_.dash(std::move(value));
    return *this;
}
ArcTrack& ArcTrack::dashed(bool value) {
    layer_.dashed(value);
    return *this;
}
ArcTrack& ArcTrack::expand(std::int64_t bases) {
    layer_.expand(bases);
    return *this;
}
ArcTrack& ArcTrack::show_labels(bool value) {
    layer_.show_labels(value);
    return *this;
}
ArcTrack& ArcTrack::label_font_size(double value) {
    layer_.label_font_size(value);
    return *this;
}
ArcTrack& ArcTrack::score_filter(std::optional<double> minimum,
                                 std::optional<double> maximum) {
    layer_.score_filter(minimum, maximum);
    return *this;
}
ArcTrack& ArcTrack::color_by_score(ColorMap colors, ValueScale scale) {
    layer_.color_by_score(std::move(colors), std::move(scale));
    return *this;
}
ArcTrack& ArcTrack::color_by_score(const std::string& colors, ValueScale scale) {
    layer_.color_by_score(colors, std::move(scale));
    return *this;
}
ArcTrack& ArcTrack::opacity_by_score(double minimum, double maximum) {
    layer_.opacity_by_score(minimum, maximum);
    return *this;
}
ArcTrack& ArcTrack::line_width_by_score(double minimum, double maximum) {
    layer_.line_width_by_score(minimum, maximum);
    return *this;
}

void ArcTrack::prepare(const ViewContext& context) {
    capture_view(context);
    layer_.prepare(context.x_region, context.x_region);
}

void ArcTrack::draw(Canvas& canvas, const TrackRect& rect) const {
    if (layer_.features().empty() || rect.content.empty()) return;

    std::vector<const PairFeature*> ordered;
    ordered.reserve(layer_.features().size());
    for (const PairFeature& feature : layer_.features()) ordered.push_back(&feature);
    // Long arcs first keeps short, local interactions visible in dense tracks.
    std::sort(ordered.begin(), ordered.end(), [](const PairFeature* a, const PairFeature* b) {
        return std::llabs(midpoint(a->second) - midpoint(a->first)) >
               std::llabs(midpoint(b->second) - midpoint(b->first));
    });

    TextStyle label_style;
    label_style.font = theme().font;
    label_style.size = layer_.label_font_size() > 0.0 ? layer_.label_font_size()
                                                      : theme().small_font_size;
    label_style.align = TextAlign::center;
    label_style.valign = VerticalAlign::bottom;

    const double baseline = rect.content.bottom() - 1.0;
    for (const PairFeature* feature_ptr : ordered) {
        const PairFeature& feature = *feature_ptr;
        const GenomicRegion first = expanded(feature.first, layer_.expansion());
        const GenomicRegion second = expanded(feature.second, layer_.expansion());
        double x0 = rect.x.x(midpoint(first));
        double x1 = rect.x.x(midpoint(second));
        if (x1 < x0) std::swap(x0, x1);
        const double span = std::fabs(x1 - x0);
        if (!(span > 0.0)) continue;

        // The endpoints fix the diameter. Keep the same radius in x and y;
        // clipping a tall arc preserves its circular shape.
        const double radius = span / 2.0;
        const double center_x = (x0 + x1) / 2.0;
        const int segments = std::clamp(static_cast<int>(std::ceil(span / 4.0)), 16, 160);
        std::vector<Point> arc;
        arc.reserve(static_cast<std::size_t>(segments) + 1);
        for (int i = 0; i <= segments; ++i) {
            const double angle = kPi * static_cast<double>(i) / segments;
            arc.push_back(Point{center_x - radius * std::cos(angle),
                                baseline - radius * std::sin(angle)});
        }

        const Color fill = layer_.fill_for(feature);
        if (!fill.transparent()) {
            std::vector<Point> dome = arc;
            dome.push_back(Point{x1, baseline});
            dome.push_back(Point{x0, baseline});
            canvas.fill_polygon(dome, fill);
        }
        StrokeStyle stroke;
        stroke.color = layer_.color_for(feature);
        stroke.width = layer_.line_width_for(feature);
        stroke.dash = layer_.dash();
        stroke.cap = LineCap::round;
        stroke.join = LineJoin::round;
        canvas.stroke_polyline(arc, stroke);
        // Short anchor ticks make the BEDPE endpoints unambiguous.
        canvas.stroke_line(Point{x0, baseline - 2.5}, Point{x0, baseline}, stroke);
        canvas.stroke_line(Point{x1, baseline - 2.5}, Point{x1, baseline}, stroke);

        if (layer_.labels_visible() && !feature.name.empty()) {
            label_style.color = stroke.color;
            canvas.draw_text(Point{center_x, baseline - radius - 1.5},
                             feature.name, label_style);
        }
    }
}

}  // namespace gre
