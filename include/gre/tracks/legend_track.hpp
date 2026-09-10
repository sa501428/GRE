#pragma once

#include <string>
#include <utility>
#include <vector>

#include "gre/core/color.hpp"
#include "gre/core/scale.hpp"
#include "gre/figure/track.hpp"
#include "gre/render/image.hpp"

namespace gre {

class HeatmapTrack;

enum class BarOrientation { horizontal, vertical };
enum class BarAlign { left, center, right };

// A continuous colour bar for a heatmap, with ticks read from the same value
// scale so the legend cannot drift from the data.
class ColorBarTrack : public TrackBase<ColorBarTrack> {
public:
    ColorBarTrack(ColorMap map, ValueScale scale);
    // Takes the colour map and the fitted scale from a prepared heatmap track.
    // Add the heatmap to the panel first so its scale is resolved.
    explicit ColorBarTrack(const HeatmapTrack& heatmap);

    ColorBarTrack& orientation(BarOrientation value);
    ColorBarTrack& align(BarAlign value);
    ColorBarTrack& title(std::string value);
    ColorBarTrack& bar_length(double value);
    ColorBarTrack& bar_thickness(double value);
    ColorBarTrack& max_ticks(int value);
    ColorBarTrack& border(Color color, double width = 0.4);

    void prepare(const ViewContext& context) override;
    void draw(Canvas& canvas, const TrackRect& rect) const override;

    [[nodiscard]] bool wants_label_gutter() const override { return false; }

protected:
    [[nodiscard]] double default_height() const override;

private:
    ColorMap colors_;
    ValueScale scale_;
    const HeatmapTrack* linked_{nullptr};

    BarOrientation orientation_{BarOrientation::horizontal};
    BarAlign align_{BarAlign::left};
    std::string title_;
    double bar_length_{110.0};
    double bar_thickness_{7.0};
    int max_ticks_{4};
    bool has_border_{true};
    Color border_color_{rgb(120, 120, 120)};
    double border_width_{0.4};

    Image gradient_;
};

// Discrete swatches with labels: track keys, sample groups, state colours.
class LegendTrack : public TrackBase<LegendTrack> {
public:
    LegendTrack();

    LegendTrack& add(std::string label, Color color);
    LegendTrack& columns(int value);
    LegendTrack& swatch_size(double value);
    LegendTrack& entry_spacing(double value);
    LegendTrack& align(BarAlign value);

    void prepare(const ViewContext& context) override;
    void draw(Canvas& canvas, const TrackRect& rect) const override;

    [[nodiscard]] bool wants_label_gutter() const override { return false; }

protected:
    [[nodiscard]] double default_height() const override;

private:
    std::vector<std::pair<std::string, Color>> entries_;
    int columns_{0};  // 0: fit as many as the width allows
    double swatch_size_{8.0};
    double entry_spacing_{14.0};
    BarAlign align_{BarAlign::left};

    int resolved_columns_{1};
    int resolved_rows_{1};
    double column_width_{0.0};
};

}  // namespace gre
