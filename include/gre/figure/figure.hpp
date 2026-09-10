#pragma once

#include <memory>
#include <string>
#include <vector>

#include "gre/figure/panel.hpp"
#include "gre/figure/theme.hpp"
#include "gre/render/image.hpp"
#include "gre/render/pdf_canvas.hpp"
#include "gre/render/svg_canvas.hpp"

namespace gre {

// The whole figure: a page, some margins and one or more panels stacked
// vertically.
//
//   Figure fig;
//   auto& panel = fig.add_panel();
//   panel.set_region("chr8", 127'000'000, 129'000'000);
//   panel.add_track(SignalTrack{atac}.height(50));
//   panel.add_track(HeatmapTrack{hic}.height(300));
//   fig.save_png("figure.png", 300);
//   fig.save_pdf("figure.pdf");
class Figure {
public:
    Figure();
    explicit Figure(Size size);

    // Height 0 (the default) sizes the page from the tracks.
    Figure& set_size(double width, double height);
    Figure& set_size(Size size);
    Figure& set_width(double width);
    Figure& set_margins(Insets margins);
    Figure& set_theme(Theme theme);
    Figure& set_title(std::string title);
    Figure& set_subtitle(std::string subtitle);
    Figure& set_background(Color color);
    Figure& set_panel_spacing(double spacing);

    [[nodiscard]] const Theme& theme() const noexcept { return theme_; }
    [[nodiscard]] Theme& theme() noexcept { return theme_; }

    Panel& add_panel();
    [[nodiscard]] std::size_t panel_count() const noexcept { return panels_.size(); }
    [[nodiscard]] Panel& panel(std::size_t index) { return *panels_.at(index); }
    [[nodiscard]] const Panel& panel(std::size_t index) const { return *panels_.at(index); }

    // Resolves automatic height by running layout at the given output scale.
    [[nodiscard]] Size computed_size(double device_scale = 300.0 / kPointsPerInch);

    // Lays out, prepares every track and draws.  The same call serves every
    // backend.
    void render(Canvas& canvas);

    [[nodiscard]] Image render_image(double dpi = 300.0);
    void save_png(const std::string& path, double dpi = 300.0);
    void save_pdf(const std::string& path, PdfOptions options = {});
    void save_svg(const std::string& path, SvgOptions options = {});

private:
    struct TrackLayout {
        Track* track{nullptr};
        // Page coordinates for ordinary tracks; the track's own local frame
        // for quarter-turned side tracks.
        TrackRect rect;
        bool side{false};
        double rotation{0.0};
        Point origin{};   // page position of local (0, 0), side tracks only
        Rect clip{};      // page coordinates
        bool draw_name{false};
        Point name_anchor{};
        TextAlign name_align{TextAlign::right};
        VerticalAlign name_valign{VerticalAlign::middle};
    };
    struct PanelLayout {
        Panel* panel{nullptr};
        Rect rect;
        Rect content;  // plotting area shared by the panel's tracks
        std::vector<TrackLayout> tracks;
    };
    struct Layout {
        Size size;
        std::vector<PanelLayout> panels;
        Rect title_rect;
        Rect subtitle_rect;
    };

    [[nodiscard]] Layout build_layout(double device_scale);
    void draw_layout(Canvas& canvas, const Layout& layout) const;

    Size size_{Size{540.0, 0.0}};
    Insets margins_{};
    bool margins_set_{false};
    Theme theme_;
    std::string title_;
    std::string subtitle_;
    Color background_{colors::white};
    bool background_set_{false};
    double panel_spacing_{-1.0};
    std::vector<std::unique_ptr<Panel>> panels_;
};

}  // namespace gre
