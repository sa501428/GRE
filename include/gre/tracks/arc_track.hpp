#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gre/figure/track.hpp"
#include "gre/tracks/map_annotation.hpp"

namespace gre {

// A one-dimensional interaction track. Each supplied BEDPE pair is projected
// onto the x axis and joined by an arc; no loop calling or aggregation is
// performed. Score styling intentionally matches PairAnnotationLayer.
class ArcTrack : public TrackBase<ArcTrack> {
public:
    explicit ArcTrack(PairFeatureSourcePtr source);

    ArcTrack& color(Color value);
    ArcTrack& fill(Color value);
    ArcTrack& line_width(double value);
    ArcTrack& dash(std::vector<double> value);
    ArcTrack& dashed(bool value = true);
    ArcTrack& expand(std::int64_t bases);
    ArcTrack& show_labels(bool value);
    ArcTrack& label_font_size(double value);
    ArcTrack& score_filter(std::optional<double> minimum,
                           std::optional<double> maximum = std::nullopt);
    ArcTrack& color_by_score(ColorMap colors, ValueScale scale = {});
    ArcTrack& color_by_score(const std::string& colors, ValueScale scale = {});
    ArcTrack& opacity_by_score(double minimum, double maximum);
    ArcTrack& line_width_by_score(double minimum, double maximum);

    void prepare(const ViewContext& context) override;
    void draw(Canvas& canvas, const TrackRect& rect) const override;

    [[nodiscard]] const std::vector<PairFeature>& features() const noexcept {
        return layer_.features();
    }

protected:
    [[nodiscard]] double default_height() const override { return 80.0; }

private:
    PairAnnotationLayer layer_;
};

}  // namespace gre
