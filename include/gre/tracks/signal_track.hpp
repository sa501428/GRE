#pragma once

#include <string>
#include <vector>

#include "gre/core/scale.hpp"
#include "gre/data/signal_source.hpp"
#include "gre/figure/track.hpp"

namespace gre {

enum class SignalStyle {
    line,    // polyline through bin centres
    area,    // filled step profile down to the baseline
    bars,    // one bar per bin
    points,  // a dot per bin
};

// A 1D quantitative track.  Data is requested at the resolution the output can
// display -- roughly one bin per device pixel -- rather than at the file's
// native resolution.
class SignalTrack : public TrackBase<SignalTrack> {
public:
    explicit SignalTrack(SignalSourcePtr source);
    // Pre-binned data, for callers that already have the values they want.
    explicit SignalTrack(SignalData data);

    SignalTrack& style(SignalStyle value);
    SignalTrack& color(Color value);
    // Defaults to `color` at reduced alpha for area and bar styles.
    SignalTrack& fill_color(Color value);
    // Colour for values below the baseline; defaults to `color`.
    SignalTrack& negative_color(Color value);
    SignalTrack& line_width(double value);

    SignalTrack& scale(ValueScale value);
    SignalTrack& limits(double low, double high);
    SignalTrack& log_scale(bool value);
    // Where the area and bar styles are anchored.  Defaults to 0, or to the
    // scale minimum when that is above 0.
    SignalTrack& baseline(double value);

    // Small "[0 - 25]" annotation in the top-left of the plot area.
    SignalTrack& show_range_label(bool value);
    // A proper y axis with ticks in the left gutter.
    SignalTrack& y_axis(bool value);
    SignalTrack& bar_gap(double value);

    void prepare(const ViewContext& context) override;
    void draw(Canvas& canvas, const TrackRect& rect) const override;

    [[nodiscard]] double label_reserve() const override;

    [[nodiscard]] const SignalData& data() const noexcept { return data_; }
    [[nodiscard]] const ValueScale& value_scale() const noexcept { return scale_; }

protected:
    [[nodiscard]] double default_height() const override { return 44.0; }

private:
    [[nodiscard]] double y_of(double value, const Rect& content) const;

    SignalSourcePtr source_;
    SignalData data_;
    bool have_data_{false};

    SignalStyle style_{SignalStyle::area};
    Color color_{colors::transparent};
    Color fill_color_{colors::transparent};
    Color negative_color_{colors::transparent};
    double line_width_{0.8};
    ValueScale scale_;
    bool baseline_set_{false};
    double baseline_{0.0};
    bool show_range_label_{true};
    bool y_axis_{false};
    double bar_gap_{0.0};
};

}  // namespace gre
