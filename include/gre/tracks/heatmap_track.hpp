#pragma once

#include <deque>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "gre/core/color.hpp"
#include "gre/core/scale.hpp"
#include "gre/data/matrix_source.hpp"
#include "gre/figure/track.hpp"
#include "gre/render/image.hpp"
#include "gre/tracks/map_annotation.hpp"

namespace gre {

enum class HeatmapMode {
    // The full square block, diagonal running top-left to bottom-right.
    square,
    // The upper triangle rotated 45 degrees: the diagonal becomes the top
    // edge and contact distance runs downwards.  This is the view that sits
    // above 1D tracks.
    triangle,
    // An arbitrary x-by-y block, for off-diagonal or inter-chromosomal views.
    rectangle,
};

enum class MatrixHalf { above, below };

// Fits one value scale over several heatmaps. Attach the same group to maps in
// different panels to make their colours quantitatively comparable.
class HeatmapScaleGroup {
public:
    HeatmapScaleGroup() = default;
    explicit HeatmapScaleGroup(ValueScale scale)
        : template_scale_(scale), scale_(std::move(scale)), have_scale_(true) {}

    void submit(std::uint64_t preparation_id, const ValueScale& requested,
                const float* values, std::size_t count);
    void finalize(std::uint64_t preparation_id);
    [[nodiscard]] const ValueScale& scale() const noexcept { return scale_; }

private:
    std::uint64_t preparation_id_{0};
    std::optional<ValueScale> template_scale_;
    ValueScale scale_;
    bool have_scale_{false};
    bool finalized_{false};
    std::vector<float> values_;
};

using HeatmapScaleGroupPtr = std::shared_ptr<HeatmapScaleGroup>;

// Dense contact data.  Values are normalised, looked up in a colour table and
// written straight into an RGBA image, which the backends then draw as one
// raster -- never as per-cell rectangles.
class HeatmapTrack : public TrackBase<HeatmapTrack> {
public:
    explicit HeatmapTrack(MatrixSourcePtr source);
    explicit HeatmapTrack(MatrixData data);

    HeatmapTrack& mode(HeatmapMode value);
    HeatmapTrack& colors(ColorMap value);
    HeatmapTrack& colors(const std::string& name);
    HeatmapTrack& scale(ValueScale value);
    HeatmapTrack& limits(double low, double high);
    HeatmapTrack& log_scale(bool value);
    // Clip the colour range at a percentile of the data instead of its
    // maximum; contact matrices have a long tail that otherwise washes the
    // whole map out.  Defaults to 0.99.
    HeatmapTrack& upper_percentile(double value);
    HeatmapTrack& shared_scale(HeatmapScaleGroupPtr group);

    // Square mode: draw a second matrix in one half of the map. The primary
    // source occupies the other half and each source has its own colour scale.
    HeatmapTrack& compare_with(MatrixSourcePtr source, MatrixHalf half = MatrixHalf::below);
    HeatmapTrack& comparison_colors(ColorMap value);
    HeatmapTrack& comparison_colors(const std::string& name);
    HeatmapTrack& comparison_scale(ValueScale value);
    HeatmapTrack& comparison_limits(double low, double high);
    HeatmapTrack& comparison_log_scale(bool value);
    HeatmapTrack& comparison_upper_percentile(double value);
    HeatmapTrack& shared_comparison_scale(HeatmapScaleGroupPtr group);

    // Triangle mode: how far from the diagonal to show.  0 means the whole
    // region.
    HeatmapTrack& max_distance(std::int64_t bases);
    // Square mode: force height to equal width.  On by default.
    HeatmapTrack& square_aspect(bool value);
    HeatmapTrack& show_diagonal(bool value);
    HeatmapTrack& border(Color color, double width = 0.6);
    // Smooth the raster instead of keeping bin edges crisp.
    HeatmapTrack& interpolate(bool value);
    HeatmapTrack& background(Color color);

    PairAnnotationLayer& add_annotation(PairAnnotationLayer layer);
    MapHighlight& add_highlight(MapHighlight highlight);

    void prepare(const ViewContext& context) override;
    void finalize_prepare() override;
    void draw(Canvas& canvas, const TrackRect& rect) const override;

    [[nodiscard]] const ValueScale& value_scale() const noexcept { return scale_; }
    [[nodiscard]] const ColorMap& color_map() const noexcept { return colors_; }
    [[nodiscard]] const MatrixData& data() const noexcept { return data_; }
    [[nodiscard]] bool has_comparison() const noexcept { return comparison_source_ != nullptr; }
    [[nodiscard]] const ValueScale& comparison_value_scale() const noexcept {
        return comparison_scale_;
    }
    [[nodiscard]] const ColorMap& comparison_color_map() const noexcept {
        return comparison_colors_;
    }
    [[nodiscard]] const MatrixData& comparison_data() const noexcept {
        return comparison_data_;
    }

protected:
    [[nodiscard]] double default_height() const override;

private:
    void build_square_image();
    void build_triangle_image(double device_scale);
    void draw_highlights(Canvas& canvas, const TrackRect& rect) const;
    void draw_annotations(Canvas& canvas, const TrackRect& rect) const;

    MatrixSourcePtr source_;
    MatrixData data_;
    bool have_data_{false};

    HeatmapMode mode_{HeatmapMode::square};
    ColorMap colors_;
    bool colors_set_{false};
    ValueScale scale_;
    ValueScale scale_policy_;
    HeatmapScaleGroupPtr shared_scale_;
    MatrixSourcePtr comparison_source_;
    MatrixData comparison_data_;
    MatrixHalf comparison_half_{MatrixHalf::below};
    ColorMap comparison_colors_;
    bool comparison_colors_set_{false};
    ValueScale comparison_scale_;
    ValueScale comparison_scale_policy_;
    HeatmapScaleGroupPtr shared_comparison_scale_;
    std::int64_t max_distance_{0};
    bool square_aspect_{true};
    bool show_diagonal_{false};
    bool has_border_{false};
    Color border_color_{colors::transparent};
    double border_width_{0.6};
    bool interpolate_{false};
    Color background_{colors::transparent};

    Image image_;
    double resolved_height_{0.0};
    std::int64_t shown_distance_{0};
    std::deque<PairAnnotationLayer> annotations_;
    std::deque<MapHighlight> highlights_;
};

}  // namespace gre
