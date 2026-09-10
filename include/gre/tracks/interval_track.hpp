#pragma once

#include <string>
#include <vector>

#include "gre/core/color.hpp"
#include "gre/core/scale.hpp"
#include "gre/data/feature_source.hpp"
#include "gre/figure/track.hpp"

namespace gre {

// Plain interval annotations: peaks, domains, blacklists, ChromHMM states.
// Boxes only -- no exon structure -- optionally coloured by score.
class IntervalTrack : public TrackBase<IntervalTrack> {
public:
    explicit IntervalTrack(FeatureSourcePtr source);
    explicit IntervalTrack(std::vector<Feature> features);

    IntervalTrack& color(Color value);
    // Colour each interval by its score through this map.
    IntervalTrack& color_by_score(ColorMap map);
    IntervalTrack& score_scale(ValueScale value);
    IntervalTrack& box_height(double value);
    IntervalTrack& row_height(double value);
    // Pack overlapping intervals onto separate rows instead of drawing them
    // on top of one another.
    IntervalTrack& packed(bool value);
    IntervalTrack& max_rows(int value);
    IntervalTrack& show_labels(bool value);
    IntervalTrack& border(Color color, double width = 0.4);
    IntervalTrack& rounded(bool value);

    void prepare(const ViewContext& context) override;
    void draw(Canvas& canvas, const TrackRect& rect) const override;

    [[nodiscard]] const std::vector<Feature>& features() const noexcept { return features_; }

protected:
    [[nodiscard]] double default_height() const override;

private:
    FeatureSourcePtr source_;
    std::vector<Feature> features_;
    bool have_data_{false};

    Color color_{colors::transparent};
    ColorMap score_colors_;
    bool color_by_score_{false};
    ValueScale score_scale_;
    double box_height_{9.0};
    double row_height_{11.0};
    bool packed_{false};
    int max_rows_{10};
    bool show_labels_{false};
    bool has_border_{false};
    Color border_color_{colors::transparent};
    double border_width_{0.4};
    bool rounded_{false};

    std::vector<int> rows_of_;
    int rows_{1};
};

}  // namespace gre
