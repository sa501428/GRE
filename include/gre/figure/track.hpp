#pragma once

#include <string>
#include <utility>

#include "gre/core/genomic_transform.hpp"
#include "gre/figure/theme.hpp"
#include "gre/render/canvas.hpp"

namespace gre {

// What a track is told before it prepares its data.  The content width is
// final at this point; the height is provisional, because tracks such as gene
// models decide their own height from the data they load.
struct ViewContext {
    GenomicRegion x_region;
    GenomicRegion y_region;
    Rect content;
    // Device pixels per figure unit, so tracks can ask for data at the
    // resolution the output can actually show.
    double device_scale{1.0};
    const Theme* theme{nullptr};

    // Number of bins that fit across the content at output resolution.
    [[nodiscard]] std::size_t target_bins() const noexcept {
        const double pixels = content.width * device_scale;
        return pixels > 1.0 ? static_cast<std::size_t>(pixels) : std::size_t{1};
    }
};

// Where a track draws.  `content` is the plotting area whose x range matches
// the transform; `label` is the left gutter.
struct TrackRect {
    Rect full;
    Rect label;
    Rect content;
    GenomicTransform x;
    GenomicTransform y;
};

class Track {
public:
    virtual ~Track() = default;

    // Height in figure units.  Called after prepare(), so it may depend on the
    // data that was loaded.
    [[nodiscard]] virtual double preferred_height() const = 0;

    // Data transformation only; no drawing state and no absolute positions.
    virtual void prepare(const ViewContext&) {}

    virtual void draw(Canvas& canvas, const TrackRect& rect) const = 0;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const Insets& margins() const noexcept { return margins_; }
    [[nodiscard]] double flex() const noexcept { return flex_; }
    [[nodiscard]] bool clipped() const noexcept { return clip_; }
    [[nodiscard]] bool show_name() const noexcept { return show_name_; }

    // How much of the right-hand side of the label gutter the track draws into
    // itself, for example a y-axis.  The figure puts the track name in what is
    // left.
    [[nodiscard]] virtual double label_reserve() const { return 0.0; }

    // Tracks that own the full panel width (a colour bar, say) can opt out of
    // the label gutter.
    [[nodiscard]] virtual bool wants_label_gutter() const { return true; }

protected:
    // draw() receives only a canvas and a rectangle, so anything else a track
    // needs at draw time is captured here during prepare().
    void capture_view(const ViewContext& context) { view_ = context; }
    [[nodiscard]] const ViewContext& view() const noexcept { return view_; }
    [[nodiscard]] const Theme& theme() const noexcept;

    ViewContext view_{};
    std::string name_;
    double height_{0.0};  // 0 means "use the track's own preferred height"
    Insets margins_{1.0, 0.0, 1.0, 0.0};
    double flex_{0.0};
    bool clip_{true};
    bool show_name_{true};
};

// Fluent configuration that keeps the derived type, so
// `SignalTrack{data}.height(80).color(...)` works as the plan describes.
template <typename Derived>
class TrackBase : public Track {
public:
    using Track::margins;
    using Track::name;

    Derived& name(std::string value) {
        name_ = std::move(value);
        return self();
    }
    Derived& height(double value) {
        height_ = value;
        return self();
    }
    Derived& margins(Insets value) {
        margins_ = value;
        return self();
    }
    // Non-zero weight lets the track absorb leftover space when the figure has
    // a fixed height.
    Derived& flex(double weight) {
        flex_ = weight;
        return self();
    }
    Derived& clip(bool value) {
        clip_ = value;
        return self();
    }
    Derived& show_name(bool value) {
        show_name_ = value;
        return self();
    }

    [[nodiscard]] double preferred_height() const override {
        return height_ > 0.0 ? height_ : default_height();
    }

protected:
    [[nodiscard]] virtual double default_height() const { return 60.0; }
    [[nodiscard]] bool has_explicit_height() const noexcept { return height_ > 0.0; }

    Derived& self() noexcept { return static_cast<Derived&>(*this); }
};

}  // namespace gre
