#pragma once

#include <memory>
#include <string>

#include "gre/render/canvas.hpp"

namespace gre {

struct SvgOptions {
    // Font stack written into the output.  The default keeps Arial first so
    // that SVG matches the PNG and PDF backends where Arial is installed.
    std::string font_family{"Arial, Helvetica, sans-serif"};
    // Embed rasters as base64 PNG data URIs.  Heatmaps stay images here too.
    bool embed_images{true};
    // Render heatmap bins crisply rather than smoothing them.
    bool pixelated_images{true};
};

// SVG output.  Text is emitted as real <text>, so it stays editable, but the
// viewer supplies the font: exact glyph widths can differ slightly from the
// PNG and PDF backends, which measure the actual font file.
class SvgCanvas : public Canvas {
public:
    explicit SvgCanvas(Size page_size, SvgOptions options = {});
    ~SvgCanvas() override;

    SvgCanvas(const SvgCanvas&) = delete;
    SvgCanvas& operator=(const SvgCanvas&) = delete;

    [[nodiscard]] Size size() const override { return size_; }

    void fill_rect(const Rect& rect, Color color) override;
    void stroke_rect(const Rect& rect, const StrokeStyle& style) override;
    void stroke_line(Point from, Point to, const StrokeStyle& style) override;
    void stroke_polyline(std::span<const Point> points, const StrokeStyle& style) override;
    void fill_polygon(std::span<const Point> points, Color color,
                      FillRule rule = FillRule::nonzero) override;
    void draw_text(Point anchor, std::string_view text, const TextStyle& style) override;
    void draw_image(const Rect& rect, const ImageView& image) override;
    void push_clip(const Rect& rect) override;
    void pop_clip() override;

    [[nodiscard]] std::string finish();
    void save(const std::string& path);

private:
    struct Impl;

    Size size_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gre
