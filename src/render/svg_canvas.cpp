#include "gre/render/svg_canvas.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "gre/core/error.hpp"
#include "gre/export/png.hpp"

namespace gre {
namespace {

std::string number(double value) {
    if (!std::isfinite(value)) value = 0.0;
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.4f", value);
    std::string text(buffer);
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
    }
    if (text.empty() || text == "-0") text = "0";
    return text;
}

std::string escape_xml(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&apos;";
                break;
            default:
                out.push_back(c);
        }
    }
    return out;
}

std::string base64(const std::vector<std::uint8_t>& data) {
    static const char* table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const std::uint32_t triple = (static_cast<std::uint32_t>(data[i]) << 16) |
                                     (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                                     static_cast<std::uint32_t>(data[i + 2]);
        out.push_back(table[(triple >> 18) & 0x3F]);
        out.push_back(table[(triple >> 12) & 0x3F]);
        out.push_back(table[(triple >> 6) & 0x3F]);
        out.push_back(table[triple & 0x3F]);
    }
    if (i < data.size()) {
        std::uint32_t triple = static_cast<std::uint32_t>(data[i]) << 16;
        const bool two = i + 1 < data.size();
        if (two) triple |= static_cast<std::uint32_t>(data[i + 1]) << 8;
        out.push_back(table[(triple >> 18) & 0x3F]);
        out.push_back(table[(triple >> 12) & 0x3F]);
        out.push_back(two ? table[(triple >> 6) & 0x3F] : '=');
        out.push_back('=');
    }
    return out;
}

std::string paint(Color color) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x", color.r, color.g, color.b);
    return buffer;
}

std::string points_attribute(std::span<const Point> points) {
    std::string out;
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (i > 0) out.push_back(' ');
        out += number(points[i].x) + "," + number(points[i].y);
    }
    return out;
}

}  // namespace

struct SvgCanvas::Impl {
    explicit Impl(SvgOptions opts) : options(std::move(opts)) {}

    SvgOptions options;
    std::ostringstream body;
    std::ostringstream defs;
    int clip_counter{0};
    int clip_depth{0};
    bool finished{false};

    void stroke_attributes(std::ostringstream& out, const StrokeStyle& style) {
        out << " fill=\"none\" stroke=\"" << paint(style.color) << "\" stroke-width=\""
            << number(style.width) << "\"";
        if (!style.color.opaque()) {
            out << " stroke-opacity=\"" << number(style.color.a / 255.0) << "\"";
        }
        switch (style.cap) {
            case LineCap::round:
                out << " stroke-linecap=\"round\"";
                break;
            case LineCap::square:
                out << " stroke-linecap=\"square\"";
                break;
            case LineCap::butt:
                break;
        }
        switch (style.join) {
            case LineJoin::round:
                out << " stroke-linejoin=\"round\"";
                break;
            case LineJoin::bevel:
                out << " stroke-linejoin=\"bevel\"";
                break;
            case LineJoin::miter:
                break;
        }
        if (style.dashed()) {
            out << " stroke-dasharray=\"";
            for (std::size_t i = 0; i < style.dash.size(); ++i) {
                if (i > 0) out << ",";
                out << number(style.dash[i]);
            }
            out << "\"";
            if (style.dash_phase != 0.0) {
                out << " stroke-dashoffset=\"" << number(style.dash_phase) << "\"";
            }
        }
    }
};

SvgCanvas::SvgCanvas(Size page_size, SvgOptions options)
    : size_(page_size), impl_(std::make_unique<Impl>(std::move(options))) {
    if (page_size.empty()) {
        throw Error(ErrorCode::invalid_argument, "SVG page size must be positive");
    }
}

SvgCanvas::~SvgCanvas() = default;

void SvgCanvas::push_clip(const Rect& rect) {
    const int id = ++impl_->clip_counter;
    impl_->defs << "<clipPath id=\"clip" << id << "\"><rect x=\"" << number(rect.x) << "\" y=\""
                << number(rect.y) << "\" width=\"" << number(rect.width) << "\" height=\""
                << number(rect.height) << "\"/></clipPath>\n";
    impl_->body << "<g clip-path=\"url(#clip" << id << ")\">\n";
    ++impl_->clip_depth;
}

void SvgCanvas::pop_clip() {
    if (impl_->clip_depth <= 0) return;
    impl_->body << "</g>\n";
    --impl_->clip_depth;
}

void SvgCanvas::fill_rect(const Rect& rect, Color color) {
    if (color.transparent() || rect.empty()) return;
    impl_->body << "<rect x=\"" << number(rect.x) << "\" y=\"" << number(rect.y) << "\" width=\""
                << number(rect.width) << "\" height=\"" << number(rect.height) << "\" fill=\""
                << paint(color) << "\"";
    if (!color.opaque()) impl_->body << " fill-opacity=\"" << number(color.a / 255.0) << "\"";
    impl_->body << "/>\n";
}

void SvgCanvas::stroke_rect(const Rect& rect, const StrokeStyle& style) {
    if (style.color.transparent() || style.width <= 0.0) return;
    impl_->body << "<rect x=\"" << number(rect.x) << "\" y=\"" << number(rect.y) << "\" width=\""
                << number(rect.width) << "\" height=\"" << number(rect.height) << "\"";
    impl_->stroke_attributes(impl_->body, style);
    impl_->body << "/>\n";
}

void SvgCanvas::stroke_line(Point from, Point to, const StrokeStyle& style) {
    const Point points[2] = {from, to};
    stroke_polyline(std::span<const Point>(points, 2), style);
}

void SvgCanvas::stroke_polyline(std::span<const Point> points, const StrokeStyle& style) {
    if (points.size() < 2 || style.color.transparent() || style.width <= 0.0) return;
    impl_->body << "<polyline points=\"" << points_attribute(points) << "\"";
    impl_->stroke_attributes(impl_->body, style);
    impl_->body << "/>\n";
}

void SvgCanvas::fill_polygon(std::span<const Point> points, Color color, FillRule rule) {
    if (points.size() < 3 || color.transparent()) return;
    impl_->body << "<polygon points=\"" << points_attribute(points) << "\" fill=\"" << paint(color)
                << "\"";
    if (rule == FillRule::even_odd) impl_->body << " fill-rule=\"evenodd\"";
    if (!color.opaque()) impl_->body << " fill-opacity=\"" << number(color.a / 255.0) << "\"";
    impl_->body << "/>\n";
}

void SvgCanvas::draw_text(Point anchor, std::string_view text, const TextStyle& style) {
    if (text.empty() || style.color.transparent() || style.size <= 0.0) return;
    const Font& font = fonts().get(style.font);
    const Point origin = text_origin(font, text, style, anchor);

    impl_->body << "<text x=\"" << number(origin.x) << "\" y=\"" << number(origin.y)
                << "\" font-family=\"" << escape_xml(impl_->options.font_family)
                << "\" font-size=\"" << number(style.size) << "\" fill=\"" << paint(style.color)
                << "\" xml:space=\"preserve\"";
    if (!style.color.opaque()) {
        impl_->body << " fill-opacity=\"" << number(style.color.a / 255.0) << "\"";
    }
    if (style.rotation != 0.0) {
        // SVG angles run clockwise in a y-down system; ours run anti-clockwise.
        impl_->body << " transform=\"rotate(" << number(-style.rotation) << " "
                    << number(origin.x) << " " << number(origin.y) << ")\"";
    }
    impl_->body << ">" << escape_xml(text) << "</text>\n";
}

void SvgCanvas::draw_image(const Rect& rect, const ImageView& source) {
    if (source.empty() || rect.empty()) return;
    if (!impl_->options.embed_images) return;

    Image copy(source.width, source.height);
    for (int y = 0; y < source.height; ++y) {
        std::copy(source.row(y), source.row(y) + static_cast<std::ptrdiff_t>(source.width) * 4,
                  copy.row(y));
    }
    const std::vector<std::uint8_t> encoded = png::encode(copy, png::Options{0, 6, true});

    impl_->body << "<image x=\"" << number(rect.x) << "\" y=\"" << number(rect.y) << "\" width=\""
                << number(rect.width) << "\" height=\"" << number(rect.height)
                << "\" preserveAspectRatio=\"none\"";
    if (impl_->options.pixelated_images) {
        impl_->body << " image-rendering=\"pixelated\"";
    }
    impl_->body << " href=\"data:image/png;base64," << base64(encoded) << "\"/>\n";
}

std::string SvgCanvas::finish() {
    if (impl_->finished) throw Error(ErrorCode::internal, "SvgCanvas::finish() called twice");
    impl_->finished = true;
    while (impl_->clip_depth > 0) pop_clip();

    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\""
        << number(size_.width) << "pt\" height=\"" << number(size_.height) << "pt\" viewBox=\"0 0 "
        << number(size_.width) << " " << number(size_.height) << "\">\n";
    const std::string defs = impl_->defs.str();
    if (!defs.empty()) out << "<defs>\n" << defs << "</defs>\n";
    out << impl_->body.str();
    out << "</svg>\n";
    return out.str();
}

void SvgCanvas::save(const std::string& path) {
    const std::string text = finish();
    std::ofstream stream(path);
    if (!stream) throw Error(ErrorCode::io, "cannot open for writing: " + path);
    stream << text;
    if (!stream) throw Error(ErrorCode::io, "failed while writing: " + path);
}

}  // namespace gre
