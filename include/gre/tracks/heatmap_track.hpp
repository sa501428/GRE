#pragma once

#include "gre/core/color.hpp"
#include "gre/core/scale.hpp"
#include "gre/data/matrix_source.hpp"
#include "gre/figure/track.hpp"
#include "gre/render/image.hpp"

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

    void prepare(const ViewContext& context) override;
    void draw(Canvas& canvas, const TrackRect& rect) const override;

    [[nodiscard]] const ValueScale& value_scale() const noexcept { return scale_; }
    [[nodiscard]] const ColorMap& color_map() const noexcept { return colors_; }
    [[nodiscard]] const MatrixData& data() const noexcept { return data_; }

protected:
    [[nodiscard]] double default_height() const override;

private:
    void build_square_image();
    void build_triangle_image(double device_scale);

    MatrixSourcePtr source_;
    MatrixData data_;
    bool have_data_{false};

    HeatmapMode mode_{HeatmapMode::square};
    ColorMap colors_;
    bool colors_set_{false};
    ValueScale scale_;
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
};

}  // namespace gre
