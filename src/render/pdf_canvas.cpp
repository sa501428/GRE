#include "gre/render/pdf_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <sstream>

#include "export/pdf_writer.hpp"
#include "gre/core/error.hpp"
#include "render/truetype.hpp"

namespace gre {
namespace {

using pdf::format_number;

std::string colour_operands(Color color) {
    return format_number(color.r / 255.0) + " " + format_number(color.g / 255.0) + " " +
           format_number(color.b / 255.0);
}

// A deterministic six-letter tag, as PDF requires for subsetted fonts.
std::string subset_tag(std::string_view name, std::size_t glyph_count) {
    std::uint32_t hash = 2166136261u;
    for (char c : name) {
        hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619u;
    }
    hash = (hash ^ static_cast<std::uint32_t>(glyph_count)) * 16777619u;
    std::string tag(6, 'A');
    for (int i = 0; i < 6; ++i) {
        tag[static_cast<std::size_t>(i)] = static_cast<char>('A' + (hash % 26));
        hash /= 26;
    }
    return tag;
}

std::string utf16be_hex(char32_t codepoint) {
    char buffer[16];
    if (codepoint > 0xFFFF) {
        const char32_t value = codepoint - 0x10000;
        const auto high = static_cast<unsigned>(0xD800 + (value >> 10));
        const auto low = static_cast<unsigned>(0xDC00 + (value & 0x3FF));
        std::snprintf(buffer, sizeof buffer, "%04X%04X", high, low);
    } else {
        std::snprintf(buffer, sizeof buffer, "%04X", static_cast<unsigned>(codepoint));
    }
    return buffer;
}

// Latin-1 approximation for the non-embedded base-14 fallback.
std::string to_winansi(std::string_view utf8) {
    std::string out;
    std::size_t pos = 0;
    while (pos < utf8.size()) {
        const char32_t codepoint = decode_utf8(utf8, pos);
        if (codepoint == 0) break;
        out.push_back(codepoint < 256 ? static_cast<char>(codepoint) : '?');
    }
    return out;
}

}  // namespace

struct PdfCanvas::Impl {
    explicit Impl(PdfOptions opts) : options(std::move(opts)) {}

    struct FontUse {
        std::string resource;         // "F1"
        std::set<std::uint16_t> gids;
        std::map<std::uint16_t, char32_t> to_unicode;
        bool embed{true};
    };

    struct ImageUse {
        std::string resource;  // "Im1"
        int width{};
        int height{};
        std::vector<std::uint8_t> rgb;
        std::vector<std::uint8_t> alpha;  // empty when fully opaque
    };

    PdfOptions options;
    std::string content;
    std::map<int, FontUse> font_uses;      // keyed by resolved FontId value
    std::vector<ImageUse> images;
    std::map<int, std::string> alpha_states;  // alpha in 0-255 -> resource name
    int clip_depth{0};
    bool finished{false};

    void emit(std::string_view text) { content.append(text); }

    void emit_line(const std::string& text) {
        content.append(text);
        content.push_back('\n');
    }

    // Registers an ExtGState for a constant alpha and returns its resource
    // name, or an empty string when the colour is opaque.
    std::string alpha_resource(std::uint8_t alpha) {
        if (alpha == 255) return {};
        auto it = alpha_states.find(alpha);
        if (it != alpha_states.end()) return it->second;
        const std::string name = "GS" + std::to_string(alpha_states.size() + 1);
        alpha_states.emplace(alpha, name);
        return name;
    }

    void begin_state(std::uint8_t alpha) {
        emit_line("q");
        const std::string gs = alpha_resource(alpha);
        if (!gs.empty()) emit_line("/" + gs + " gs");
    }

    void end_state() { emit_line("Q"); }

    void emit_stroke_style(const StrokeStyle& style) {
        emit_line(colour_operands(style.color) + " RG");
        emit_line(format_number(std::max(style.width, 0.05)) + " w");
        emit_line(std::to_string(static_cast<int>(style.cap)) + " J");
        emit_line(std::to_string(static_cast<int>(style.join)) + " j");
        if (style.dashed()) {
            std::string pattern = "[";
            for (std::size_t i = 0; i < style.dash.size(); ++i) {
                if (i > 0) pattern += " ";
                pattern += format_number(style.dash[i]);
            }
            pattern += "] " + format_number(style.dash_phase) + " d";
            emit_line(pattern);
        }
    }

    void emit_polyline(std::span<const Point> points, bool close) {
        if (points.empty()) return;
        emit_line(format_number(points[0].x) + " " + format_number(points[0].y) + " m");
        for (std::size_t i = 1; i < points.size(); ++i) {
            emit_line(format_number(points[i].x) + " " + format_number(points[i].y) + " l");
        }
        if (close) emit_line("h");
    }
};

PdfCanvas::PdfCanvas(Size page_size, PdfOptions options)
    : size_(page_size), impl_(std::make_unique<Impl>(std::move(options))) {
    if (page_size.empty()) {
        throw Error(ErrorCode::invalid_argument, "PDF page size must be positive");
    }
    // Work in figure coordinates: origin top-left, y downwards.  Every
    // subsequent operator is emitted in that space.
    impl_->emit_line("1 0 0 -1 0 " + format_number(page_size.height) + " cm");
}

PdfCanvas::~PdfCanvas() = default;

void PdfCanvas::push_clip(const Rect& rect) {
    impl_->emit_line("q");
    impl_->emit_line(format_number(rect.x) + " " + format_number(rect.y) + " " +
                     format_number(rect.width) + " " + format_number(rect.height) + " re W n");
    ++impl_->clip_depth;
}

void PdfCanvas::pop_clip() {
    if (impl_->clip_depth <= 0) return;
    impl_->emit_line("Q");
    --impl_->clip_depth;
}

void PdfCanvas::fill_rect(const Rect& rect, Color color) {
    if (color.transparent() || rect.empty()) return;
    impl_->begin_state(color.a);
    impl_->emit_line(colour_operands(color) + " rg");
    impl_->emit_line(format_number(rect.x) + " " + format_number(rect.y) + " " +
                     format_number(rect.width) + " " + format_number(rect.height) + " re f");
    impl_->end_state();
}

void PdfCanvas::stroke_rect(const Rect& rect, const StrokeStyle& style) {
    if (style.color.transparent() || style.width <= 0.0) return;
    impl_->begin_state(style.color.a);
    impl_->emit_stroke_style(style);
    impl_->emit_line(format_number(rect.x) + " " + format_number(rect.y) + " " +
                     format_number(rect.width) + " " + format_number(rect.height) + " re S");
    impl_->end_state();
}

void PdfCanvas::stroke_line(Point from, Point to, const StrokeStyle& style) {
    const Point points[2] = {from, to};
    stroke_polyline(std::span<const Point>(points, 2), style);
}

void PdfCanvas::stroke_polyline(std::span<const Point> points, const StrokeStyle& style) {
    if (points.size() < 2 || style.color.transparent() || style.width <= 0.0) return;
    impl_->begin_state(style.color.a);
    impl_->emit_stroke_style(style);
    impl_->emit_polyline(points, /*close=*/false);
    impl_->emit_line("S");
    impl_->end_state();
}

void PdfCanvas::fill_polygon(std::span<const Point> points, Color color, FillRule rule) {
    if (points.size() < 3 || color.transparent()) return;
    impl_->begin_state(color.a);
    impl_->emit_line(colour_operands(color) + " rg");
    impl_->emit_polyline(points, /*close=*/true);
    impl_->emit_line(rule == FillRule::even_odd ? "f*" : "f");
    impl_->end_state();
}

void PdfCanvas::draw_text(Point anchor, std::string_view text, const TextStyle& style) {
    if (text.empty() || style.color.transparent() || style.size <= 0.0) return;
    const Font& font = fonts().get(style.font);
    const Point origin = text_origin(font, text, style, anchor);

    // Resolve the handle so that TextStyle{} and an explicit default id share
    // one PDF font resource.
    int key = style.font.value;
    if (key == 0) key = fonts().default_font().value;

    auto [it, inserted] = impl_->font_uses.try_emplace(key);
    if (inserted) {
        it->second.resource = "F" + std::to_string(impl_->font_uses.size());
        it->second.embed =
            impl_->options.embed_fonts && !font.face().metrics().embedding_restricted;
    }
    Impl::FontUse& use = it->second;

    const double radians = style.rotation * std::numbers::pi_v<double> / 180.0;
    const double c = std::cos(radians);
    const double s = std::sin(radians);

    impl_->begin_state(style.color.a);
    impl_->emit_line("BT");
    impl_->emit_line("/" + use.resource + " " + format_number(style.size) + " Tf");
    impl_->emit_line(colour_operands(style.color) + " rg");
    impl_->emit_line(format_number(c) + " " + format_number(-s) + " " + format_number(-s) + " " +
                     format_number(-c) + " " + format_number(origin.x) + " " +
                     format_number(origin.y) + " Tm");

    if (use.embed) {
        // Identity-H: two-byte codes that are glyph ids.
        std::string hex = "<";
        for (const PositionedGlyph& glyph : font.shape(text, style.size)) {
            use.gids.insert(glyph.gid);
            use.to_unicode.emplace(glyph.gid, glyph.codepoint);
            char buffer[8];
            std::snprintf(buffer, sizeof buffer, "%04X", glyph.gid);
            hex += buffer;
        }
        hex += "> Tj";
        impl_->emit_line(hex);
    } else {
        impl_->emit_line("(" + pdf::escape_literal(to_winansi(text)) + ") Tj");
    }
    impl_->emit_line("ET");
    impl_->end_state();
}

void PdfCanvas::draw_image(const Rect& rect, const ImageView& source) {
    if (source.empty() || rect.empty()) return;

    Impl::ImageUse image;
    image.resource = "Im" + std::to_string(impl_->images.size() + 1);
    image.width = source.width;
    image.height = source.height;
    image.rgb.resize(static_cast<std::size_t>(source.width) *
                     static_cast<std::size_t>(source.height) * 3);

    bool has_alpha = false;
    for (int y = 0; y < source.height; ++y) {
        const std::uint8_t* row = source.row(y);
        for (int x = 0; x < source.width; ++x) {
            if (row[static_cast<std::ptrdiff_t>(x) * 4 + 3] != 255) {
                has_alpha = true;
                break;
            }
        }
        if (has_alpha) break;
    }
    if (has_alpha) {
        image.alpha.resize(static_cast<std::size_t>(source.width) *
                           static_cast<std::size_t>(source.height));
    }

    for (int y = 0; y < source.height; ++y) {
        const std::uint8_t* row = source.row(y);
        for (int x = 0; x < source.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(source.width) +
                static_cast<std::size_t>(x);
            const std::uint8_t* pixel = row + static_cast<std::ptrdiff_t>(x) * 4;
            image.rgb[index * 3 + 0] = pixel[0];
            image.rgb[index * 3 + 1] = pixel[1];
            image.rgb[index * 3 + 2] = pixel[2];
            if (has_alpha) image.alpha[index] = pixel[3];
        }
    }

    const std::string resource = image.resource;
    impl_->images.push_back(std::move(image));

    // Map the image's unit square onto the rect with the first row at the top
    // of the rect, which is what a y-down figure space expects.
    impl_->emit_line("q");
    impl_->emit_line(format_number(rect.width) + " 0 0 " + format_number(-rect.height) + " " +
                     format_number(rect.x) + " " + format_number(rect.bottom()) + " cm");
    impl_->emit_line("/" + resource + " Do");
    impl_->emit_line("Q");
}

std::vector<std::uint8_t> PdfCanvas::finish() {
    if (impl_->finished) {
        throw Error(ErrorCode::internal, "PdfCanvas::finish() called twice");
    }
    impl_->finished = true;
    while (impl_->clip_depth > 0) pop_clip();

    pdf::Writer writer;
    const int catalogue = writer.allocate();
    const int pages = writer.allocate();
    const int page = writer.allocate();
    const int contents = writer.allocate();
    const int resources = writer.allocate();

    writer.define_stream(contents, "",
                         std::vector<std::uint8_t>(impl_->content.begin(), impl_->content.end()),
                         impl_->options.compress);

    // ---- fonts ------------------------------------------------------------
    std::string font_resources;
    for (auto& [handle, use] : impl_->font_uses) {
        const Font& font = fonts().get(FontId{handle});
        const ttf::Face& face = font.face();
        const double units = static_cast<double>(face.metrics().units_per_em);
        const auto to_glyph_space = [units](double value) {
            return static_cast<int>(std::lround(value * 1000.0 / units));
        };

        if (!use.embed) {
            const int simple = writer.add(
                "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding "
                ">>");
            font_resources += "/" + use.resource + " " + std::to_string(simple) + " 0 R ";
            continue;
        }

        std::vector<std::uint16_t> gids(use.gids.begin(), use.gids.end());
        const std::string tag = subset_tag(font.postscript_name(), gids.size());
        const std::string base_name = tag + "+" + font.postscript_name();

        std::vector<std::uint8_t> program =
            impl_->options.subset_fonts ? face.subset(gids)
                                        : std::vector<std::uint8_t>(face.data());
        const std::size_t program_length = program.size();
        const int font_file = writer.add_stream("/Length1 " + std::to_string(program_length),
                                                std::move(program), impl_->options.compress);

        int flags = 4;  // symbolic: the encoding is the font's own glyph order
        if (face.metrics().fixed_pitch) flags |= 1;
        if (face.metrics().italic_angle != 0.0) flags |= 64;

        std::ostringstream descriptor;
        descriptor << "<< /Type /FontDescriptor /FontName /" << pdf::escape_name(base_name)
                   << " /Flags " << flags << " /FontBBox [" << to_glyph_space(face.metrics().x_min)
                   << " " << to_glyph_space(face.metrics().y_min) << " "
                   << to_glyph_space(face.metrics().x_max) << " "
                   << to_glyph_space(face.metrics().y_max) << "]"
                   << " /ItalicAngle " << format_number(face.metrics().italic_angle)
                   << " /Ascent " << to_glyph_space(face.metrics().ascender) << " /Descent "
                   << to_glyph_space(face.metrics().descender) << " /CapHeight "
                   << to_glyph_space(face.metrics().cap_height) << " /StemV "
                   << (face.metrics().weight_class >= 600 ? 140 : 80) << " /FontFile2 "
                   << font_file << " 0 R >>";
        const int descriptor_id = writer.add(descriptor.str());

        // /W as runs of consecutive glyph ids, which is much shorter than one
        // entry per glyph for the alphabetic ranges figures actually use.
        std::string widths = "[";
        for (std::size_t i = 0; i < gids.size();) {
            std::size_t j = i;
            while (j + 1 < gids.size() && gids[j + 1] == gids[j] + 1) ++j;
            widths += " " + std::to_string(gids[i]) + " [";
            for (std::size_t k = i; k <= j; ++k) {
                widths += (k == i ? "" : " ") + std::to_string(to_glyph_space(
                                                   face.advance_units(gids[k])));
            }
            widths += "]";
            i = j + 1;
        }
        widths += " ]";

        std::string cmap =
            "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
            "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
            "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
            "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
        std::vector<std::pair<std::uint16_t, char32_t>> mappings(use.to_unicode.begin(),
                                                                 use.to_unicode.end());
        for (std::size_t i = 0; i < mappings.size(); i += 100) {
            const std::size_t count = std::min<std::size_t>(100, mappings.size() - i);
            cmap += std::to_string(count) + " beginbfchar\n";
            for (std::size_t k = i; k < i + count; ++k) {
                char buffer[16];
                std::snprintf(buffer, sizeof buffer, "%04X", mappings[k].first);
                cmap += "<";
                cmap += buffer;
                cmap += "> <" + utf16be_hex(mappings[k].second) + ">\n";
            }
            cmap += "endbfchar\n";
        }
        cmap += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
        const int to_unicode = writer.add_stream(
            "", std::vector<std::uint8_t>(cmap.begin(), cmap.end()), impl_->options.compress);

        std::ostringstream descendant;
        descendant << "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /"
                   << pdf::escape_name(base_name)
                   << " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >>"
                   << " /FontDescriptor " << descriptor_id << " 0 R /DW 1000 /W " << widths
                   << " /CIDToGIDMap /Identity >>";
        const int descendant_id = writer.add(descendant.str());

        std::ostringstream composite;
        composite << "<< /Type /Font /Subtype /Type0 /BaseFont /" << pdf::escape_name(base_name)
                  << " /Encoding /Identity-H /DescendantFonts [" << descendant_id
                  << " 0 R] /ToUnicode " << to_unicode << " 0 R >>";
        const int font_id = writer.add(composite.str());
        font_resources += "/" + use.resource + " " + std::to_string(font_id) + " 0 R ";
    }

    // ---- images -----------------------------------------------------------
    std::string xobject_resources;
    for (Impl::ImageUse& image : impl_->images) {
        int smask = 0;
        if (!image.alpha.empty()) {
            smask = writer.add_stream(
                "/Type /XObject /Subtype /Image /Width " + std::to_string(image.width) +
                    " /Height " + std::to_string(image.height) +
                    " /ColorSpace /DeviceGray /BitsPerComponent 8",
                std::move(image.alpha), impl_->options.compress);
        }
        std::string dict = "/Type /XObject /Subtype /Image /Width " +
                           std::to_string(image.width) + " /Height " +
                           std::to_string(image.height) +
                           " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Interpolate false";
        if (smask != 0) dict += " /SMask " + std::to_string(smask) + " 0 R";
        const int image_id = writer.add_stream(dict, std::move(image.rgb), impl_->options.compress);
        xobject_resources += "/" + image.resource + " " + std::to_string(image_id) + " 0 R ";
    }

    // ---- graphics states --------------------------------------------------
    std::string gs_resources;
    for (const auto& [alpha, name] : impl_->alpha_states) {
        const std::string value = format_number(alpha / 255.0);
        const int id =
            writer.add("<< /Type /ExtGState /ca " + value + " /CA " + value + " >>");
        gs_resources += "/" + name + " " + std::to_string(id) + " 0 R ";
    }

    std::string resource_dict = "<< /ProcSet [/PDF /Text /ImageC]";
    if (!font_resources.empty()) resource_dict += " /Font << " + font_resources + ">>";
    if (!xobject_resources.empty()) {
        resource_dict += " /XObject << " + xobject_resources + ">>";
    }
    if (!gs_resources.empty()) resource_dict += " /ExtGState << " + gs_resources + ">>";
    resource_dict += " >>";
    writer.define(resources, resource_dict);

    writer.define(page, "<< /Type /Page /Parent " + std::to_string(pages) + " 0 R /MediaBox [0 0 " +
                            format_number(size_.width) + " " + format_number(size_.height) +
                            "] /Resources " + std::to_string(resources) + " 0 R /Contents " +
                            std::to_string(contents) + " 0 R >>");
    writer.define(pages, "<< /Type /Pages /Kids [" + std::to_string(page) + " 0 R] /Count 1 >>");
    writer.define(catalogue, "<< /Type /Catalog /Pages " + std::to_string(pages) + " 0 R >>");
    writer.set_root(catalogue);

    std::string info = "<< /Producer (" + pdf::escape_literal(impl_->options.creator) + ")";
    if (!impl_->options.title.empty()) {
        info += " /Title (" + pdf::escape_literal(impl_->options.title) + ")";
    }
    if (!impl_->options.author.empty()) {
        info += " /Author (" + pdf::escape_literal(impl_->options.author) + ")";
    }
    if (!impl_->options.subject.empty()) {
        info += " /Subject (" + pdf::escape_literal(impl_->options.subject) + ")";
    }
    info += " >>";
    writer.set_info(writer.add(info));

    return writer.serialize();
}

void PdfCanvas::save(const std::string& path) {
    const std::vector<std::uint8_t> bytes = finish();
    std::ofstream stream(path, std::ios::binary);
    if (!stream) throw Error(ErrorCode::io, "cannot open for writing: " + path);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw Error(ErrorCode::io, "failed while writing: " + path);
}

}  // namespace gre
