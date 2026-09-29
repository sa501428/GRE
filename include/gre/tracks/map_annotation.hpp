#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gre/core/color.hpp"
#include "gre/core/genomic_transform.hpp"
#include "gre/core/scale.hpp"
#include "gre/data/pair_source.hpp"

namespace gre {

enum class AnnotationSide { above, below, both };

enum class PairAnnotationStyle {
    loop,
    box,
    domain,
};

// Styling and data for one pairwise map-overlay layer.
class PairAnnotationLayer {
public:
    explicit PairAnnotationLayer(PairFeatureSourcePtr source);

    PairAnnotationLayer& style(PairAnnotationStyle value);
    PairAnnotationLayer& side(AnnotationSide value);
    PairAnnotationLayer& color(Color value);
    PairAnnotationLayer& fill(Color value);
    PairAnnotationLayer& line_width(double value);
    PairAnnotationLayer& dash(std::vector<double> value);
    PairAnnotationLayer& dashed(bool value = true);
    PairAnnotationLayer& expand(std::int64_t bases);
    PairAnnotationLayer& show_labels(bool value);
    PairAnnotationLayer& label_font_size(double value);
    // Score-aware styling. Automatic score limits are fitted to the visible,
    // filtered records. Records without scores retain the ordinary style.
    PairAnnotationLayer& score_filter(std::optional<double> minimum,
                                      std::optional<double> maximum = std::nullopt);
    PairAnnotationLayer& color_by_score(ColorMap colors, ValueScale scale = {});
    PairAnnotationLayer& color_by_score(const std::string& colors, ValueScale scale = {});
    PairAnnotationLayer& opacity_by_score(double minimum, double maximum);
    PairAnnotationLayer& line_width_by_score(double minimum, double maximum);
    PairAnnotationLayer& size_by_score(double minimum, double maximum);

    [[nodiscard]] PairAnnotationStyle style() const noexcept { return style_; }
    [[nodiscard]] AnnotationSide side() const noexcept { return side_; }
    [[nodiscard]] Color color() const noexcept { return color_; }
    [[nodiscard]] Color fill() const noexcept { return fill_; }
    [[nodiscard]] double line_width() const noexcept { return line_width_; }
    [[nodiscard]] const std::vector<double>& dash() const noexcept { return dash_; }
    [[nodiscard]] std::int64_t expansion() const noexcept { return expansion_; }
    [[nodiscard]] bool labels_visible() const noexcept { return show_labels_; }
    [[nodiscard]] double label_font_size() const noexcept { return label_font_size_; }
    [[nodiscard]] const std::vector<PairFeature>& features() const noexcept { return features_; }
    [[nodiscard]] const ValueScale& fitted_score_scale() const noexcept {
        return fitted_score_scale_;
    }
    [[nodiscard]] Color color_for(const PairFeature& feature) const noexcept;
    [[nodiscard]] Color fill_for(const PairFeature& feature) const noexcept;
    [[nodiscard]] double line_width_for(const PairFeature& feature) const noexcept;
    [[nodiscard]] double size_for(const PairFeature& feature) const noexcept;

    void prepare(const GenomicRegion& x_region, const GenomicRegion& y_region);

private:
    PairFeatureSourcePtr source_;
    std::vector<PairFeature> features_;
    PairAnnotationStyle style_{PairAnnotationStyle::loop};
    AnnotationSide side_{AnnotationSide::both};
    Color color_{rgb(30, 30, 30)};
    Color fill_{colors::transparent};
    double line_width_{1.2};
    std::vector<double> dash_;
    std::int64_t expansion_{0};
    bool show_labels_{false};
    double label_font_size_{0.0};
    std::optional<double> score_minimum_;
    std::optional<double> score_maximum_;
    std::optional<ColorMap> score_colors_;
    ValueScale score_scale_;
    ValueScale fitted_score_scale_;
    std::optional<std::pair<double, double>> score_opacity_;
    std::optional<std::pair<double, double>> score_line_width_;
    std::optional<std::pair<double, double>> score_size_;
};

enum class HighlightAxis { vertical, horizontal, both };

// A genomic interval projected across a map as a translucent band.
class MapHighlight {
public:
    explicit MapHighlight(GenomicRegion region,
                          HighlightAxis axis = HighlightAxis::vertical);

    MapHighlight& axis(HighlightAxis value);
    MapHighlight& fill(Color value);
    MapHighlight& border(Color value, double width = 0.8);
    MapHighlight& dash(std::vector<double> value);
    MapHighlight& dashed(bool value = true);
    MapHighlight& expand(std::int64_t bases);

    [[nodiscard]] const GenomicRegion& region() const noexcept { return region_; }
    [[nodiscard]] HighlightAxis axis() const noexcept { return axis_; }
    [[nodiscard]] Color fill() const noexcept { return fill_; }
    [[nodiscard]] Color border_color() const noexcept { return border_color_; }
    [[nodiscard]] double border_width() const noexcept { return border_width_; }
    [[nodiscard]] const std::vector<double>& dash() const noexcept { return dash_; }
    [[nodiscard]] std::int64_t expansion() const noexcept { return expansion_; }

private:
    GenomicRegion region_;
    HighlightAxis axis_{HighlightAxis::vertical};
    Color fill_{rgba(255, 215, 0, 48)};
    Color border_color_{rgba(190, 130, 0, 180)};
    double border_width_{0.8};
    std::vector<double> dash_;
    std::int64_t expansion_{0};
};

}  // namespace gre
