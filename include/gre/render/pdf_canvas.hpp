#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "gre/render/canvas.hpp"

namespace gre {

struct PdfOptions {
    // Flate-compress content and image streams.
    bool compress{true};
    // Embed a subset of the TrueType program.  With this off, or when the
    // font's licence forbids embedding, text falls back to the base-14
    // Helvetica family.
    bool embed_fonts{true};
    bool subset_fonts{true};

    std::string title;
    std::string author;
    std::string subject;
    std::string creator{"GRE"};
};

// Writes a one-page PDF.  Text, lines, gene models and annotations stay
// vector; dense rasters (heatmaps) are embedded as images, as the plan
// requires -- a large contact map must never become a million rectangles.
class PdfCanvas : public Canvas {
public:
    explicit PdfCanvas(Size page_size, PdfOptions options = {});
    ~PdfCanvas() override;

    PdfCanvas(const PdfCanvas&) = delete;
    PdfCanvas& operator=(const PdfCanvas&) = delete;

    [[nodiscard]] Size size() const override { return size_; }
    [[nodiscard]] double device_scale() const override { return 1.0; }

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

    // Serializes the document.  Further drawing after this is not allowed.
    [[nodiscard]] std::vector<std::uint8_t> finish();
    void save(const std::string& path);

private:
    struct Impl;

    Size size_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gre
