#pragma once

#include <string>
#include <vector>

#include "gre/figure/track.hpp"

namespace gre {

enum class AxisPosition {
    // Rule along the track's bottom edge, ticks and labels above it.  Use when
    // the axis heads a stack of data tracks.
    top,
    // Rule along the track's top edge, ticks and labels below it.
    bottom,
};

// A genomic coordinate ruler: 1/2/5 x 10^k tick spacing, labelled in bp, kb or
// Mb according to the span.
class AxisTrack : public TrackBase<AxisTrack> {
public:
    AxisTrack();

    AxisTrack& position(AxisPosition value);
    AxisTrack& max_ticks(int value);
    AxisTrack& color(Color value);
    AxisTrack& font_size(double value);
    AxisTrack& show_line(bool value);
    // Draws "chr8:127,000,000-129,000,000" at the right-hand end.
    AxisTrack& show_region(bool value);

    void prepare(const ViewContext& context) override;
    void draw(Canvas& canvas, const TrackRect& rect) const override;

protected:
    [[nodiscard]] double default_height() const override;

private:
    AxisPosition position_{AxisPosition::top};
    int max_ticks_{8};
    Color color_{colors::transparent};  // transparent: take the theme's axis colour
    double font_size_{0.0};             // 0: take the theme's font size
    bool show_line_{true};
    bool show_region_{false};

    std::vector<long long> ticks_;
    long long step_{1};
};

}  // namespace gre
