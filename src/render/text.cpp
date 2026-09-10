#include "gre/render/text.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <numbers>

#include "gre/core/error.hpp"
#include "render/truetype.hpp"

namespace gre {
namespace {

namespace fs = std::filesystem;

std::vector<std::string> default_search_paths() {
    std::vector<std::string> paths;
    const auto add = [&paths](const std::string& dir) {
        std::error_code ec;
        if (fs::is_directory(dir, ec)) paths.push_back(dir);
    };
    if (const char* env = std::getenv("GRE_FONT_DIR"); env != nullptr) add(env);
#if defined(__APPLE__)
    add("/System/Library/Fonts/Supplemental");
    add("/Library/Fonts");
    add("/System/Library/Fonts");
    if (const char* home = std::getenv("HOME"); home != nullptr) {
        add(std::string(home) + "/Library/Fonts");
    }
#elif defined(_WIN32)
    add("C:/Windows/Fonts");
#else
    add("/usr/share/fonts");
    add("/usr/local/share/fonts");
    add("/usr/share/fonts/truetype");
    if (const char* home = std::getenv("HOME"); home != nullptr) {
        add(std::string(home) + "/.local/share/fonts");
        add(std::string(home) + "/.fonts");
    }
#endif
    return paths;
}

struct FamilyCandidates {
    std::vector<std::string> regular;
    std::vector<std::string> bold;
    std::vector<std::string> italic;
    std::vector<std::string> bold_italic;
};

// File names are tried in order.  Arial first, then the metric-compatible
// substitutes that ship on Linux, then anything else with the same shape.
FamilyCandidates sans_candidates() {
    return {
        {"Arial.ttf", "arial.ttf", "LiberationSans-Regular.ttf", "Helvetica.ttc", "Helvetica.ttf",
         "DejaVuSans.ttf", "FreeSans.ttf", "Verdana.ttf"},
        {"Arial Bold.ttf", "arialbd.ttf", "LiberationSans-Bold.ttf", "Helvetica-Bold.ttf",
         "DejaVuSans-Bold.ttf", "FreeSansBold.ttf"},
        {"Arial Italic.ttf", "ariali.ttf", "LiberationSans-Italic.ttf", "DejaVuSans-Oblique.ttf",
         "FreeSansOblique.ttf"},
        {"Arial Bold Italic.ttf", "arialbi.ttf", "LiberationSans-BoldItalic.ttf",
         "DejaVuSans-BoldOblique.ttf", "FreeSansBoldOblique.ttf"},
    };
}

FamilyCandidates serif_candidates() {
    return {
        {"Times New Roman.ttf", "times.ttf", "LiberationSerif-Regular.ttf", "DejaVuSerif.ttf",
         "FreeSerif.ttf", "Georgia.ttf"},
        {"Times New Roman Bold.ttf", "timesbd.ttf", "LiberationSerif-Bold.ttf",
         "DejaVuSerif-Bold.ttf", "FreeSerifBold.ttf"},
        {"Times New Roman Italic.ttf", "timesi.ttf", "LiberationSerif-Italic.ttf",
         "FreeSerifItalic.ttf"},
        {"Times New Roman Bold Italic.ttf", "timesbi.ttf", "LiberationSerif-BoldItalic.ttf",
         "FreeSerifBoldItalic.ttf"},
    };
}

FamilyCandidates mono_candidates() {
    return {
        {"Courier New.ttf", "cour.ttf", "LiberationMono-Regular.ttf", "DejaVuSansMono.ttf",
         "Menlo.ttc", "FreeMono.ttf"},
        {"Courier New Bold.ttf", "courbd.ttf", "LiberationMono-Bold.ttf",
         "DejaVuSansMono-Bold.ttf", "FreeMonoBold.ttf"},
        {"Courier New Italic.ttf", "couri.ttf", "LiberationMono-Italic.ttf",
         "DejaVuSansMono-Oblique.ttf"},
        {"Courier New Bold Italic.ttf", "courbi.ttf", "LiberationMono-BoldItalic.ttf"},
    };
}

std::string lower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool is_font_file(const fs::path& path) {
    const std::string extension = lower(path.extension().string());
    return extension == ".ttf" || extension == ".ttc";
}

}  // namespace

char32_t decode_utf8(std::string_view text, std::size_t& pos) {
    if (pos >= text.size()) return 0;
    const auto byte = static_cast<unsigned char>(text[pos]);
    const auto continuation = [&](std::size_t index) -> bool {
        return index < text.size() && (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80;
    };
    if (byte < 0x80) {
        ++pos;
        return byte;
    }
    if ((byte & 0xE0) == 0xC0 && continuation(pos + 1)) {
        const char32_t cp = static_cast<char32_t>(byte & 0x1F) << 6 |
                            static_cast<char32_t>(text[pos + 1] & 0x3F);
        pos += 2;
        return cp;
    }
    if ((byte & 0xF0) == 0xE0 && continuation(pos + 1) && continuation(pos + 2)) {
        const char32_t cp = static_cast<char32_t>(byte & 0x0F) << 12 |
                            static_cast<char32_t>(text[pos + 1] & 0x3F) << 6 |
                            static_cast<char32_t>(text[pos + 2] & 0x3F);
        pos += 3;
        return cp;
    }
    if ((byte & 0xF8) == 0xF0 && continuation(pos + 1) && continuation(pos + 2) &&
        continuation(pos + 3)) {
        const char32_t cp = static_cast<char32_t>(byte & 0x07) << 18 |
                            static_cast<char32_t>(text[pos + 1] & 0x3F) << 12 |
                            static_cast<char32_t>(text[pos + 2] & 0x3F) << 6 |
                            static_cast<char32_t>(text[pos + 3] & 0x3F);
        pos += 4;
        return cp;
    }
    ++pos;
    return 0xFFFD;
}

std::vector<PositionedGlyph> Font::shape(std::string_view utf8, double size) const {
    std::vector<PositionedGlyph> glyphs;
    glyphs.reserve(utf8.size());
    const double scale = size / static_cast<double>(face_->metrics().units_per_em);
    double pen = 0.0;
    std::size_t pos = 0;
    while (pos < utf8.size()) {
        const char32_t codepoint = decode_utf8(utf8, pos);
        if (codepoint == 0) break;
        std::uint16_t gid = face_->glyph_index(codepoint);
        if (gid == 0 && codepoint != U' ') {
            // Fall back to the replacement character so missing glyphs stay
            // visible rather than silently collapsing the run.
            gid = face_->glyph_index(0xFFFD);
        }
        glyphs.push_back(PositionedGlyph{gid, codepoint, pen});
        pen += face_->advance_units(gid) * scale;
    }
    return glyphs;
}

double Font::width(std::string_view utf8, double size) const {
    const double scale = size / static_cast<double>(face_->metrics().units_per_em);
    double pen = 0.0;
    std::size_t pos = 0;
    while (pos < utf8.size()) {
        const char32_t codepoint = decode_utf8(utf8, pos);
        if (codepoint == 0) break;
        std::uint16_t gid = face_->glyph_index(codepoint);
        if (gid == 0 && codepoint != U' ') gid = face_->glyph_index(0xFFFD);
        pen += face_->advance_units(gid) * scale;
    }
    return pen;
}

double Font::ascent(double size) const {
    return face_->metrics().ascender * size / face_->metrics().units_per_em;
}

double Font::descent(double size) const {
    return -face_->metrics().descender * size / face_->metrics().units_per_em;
}

double Font::line_height(double size) const {
    const auto& m = face_->metrics();
    return (m.ascender - m.descender + m.line_gap) * size / m.units_per_em;
}

double Font::cap_height(double size) const {
    return face_->metrics().cap_height * size / face_->metrics().units_per_em;
}

TextMetrics Font::measure(std::string_view utf8, double size) const {
    return TextMetrics{width(utf8, size), ascent(size), descent(size), line_height(size)};
}

FontRegistry& FontRegistry::instance() {
    static FontRegistry registry;
    return registry;
}

FontRegistry::FontRegistry() : search_paths_(default_search_paths()) {}

void FontRegistry::add_search_path(std::string directory) {
    search_paths_.insert(search_paths_.begin(), std::move(directory));
}

FontId FontRegistry::load_file(const std::string& path, int face_index) {
    for (const auto& [known, index] : by_path_) {
        if (known == path) return FontId{index};
    }
    auto face = ttf::Face::from_file(path, face_index);
    auto font = std::unique_ptr<Font>(new Font());
    font->ps_name_ = face->postscript_name();
    font->family_ = face->family_name().empty() ? face->postscript_name() : face->family_name();
    font->path_ = path;
    font->face_ = std::move(face);
    fonts_.push_back(std::move(font));
    // Handles are one-based so that FontId{} keeps its "use the default" meaning.
    const int id = static_cast<int>(fonts_.size());
    by_path_.emplace_back(path, id);
    return FontId{id};
}

std::string FontRegistry::locate(std::string_view family, bool bold, bool italic) const {
    const std::string key = lower(family);
    FamilyCandidates candidates;
    if (key == "times" || key == "times new roman" || key == "serif") {
        candidates = serif_candidates();
    } else if (key == "courier" || key == "courier new" || key == "mono" || key == "monospace") {
        candidates = mono_candidates();
    } else if (key == "arial" || key == "helvetica" || key == "sans" || key == "sans-serif" ||
               key.empty()) {
        candidates = sans_candidates();
    } else {
        // An unknown family: try the name itself in the usual style spellings,
        // then fall back to the sans list so text still renders.
        const std::string base(family);
        candidates.regular = {base + ".ttf", base + ".ttc"};
        candidates.bold = {base + " Bold.ttf", base + "-Bold.ttf", base + "bd.ttf"};
        candidates.italic = {base + " Italic.ttf", base + "-Italic.ttf", base + "i.ttf"};
        candidates.bold_italic = {base + " Bold Italic.ttf", base + "-BoldItalic.ttf"};
        const FamilyCandidates fallback = sans_candidates();
        candidates.regular.insert(candidates.regular.end(), fallback.regular.begin(),
                                  fallback.regular.end());
        candidates.bold.insert(candidates.bold.end(), fallback.bold.begin(), fallback.bold.end());
        candidates.italic.insert(candidates.italic.end(), fallback.italic.begin(),
                                 fallback.italic.end());
        candidates.bold_italic.insert(candidates.bold_italic.end(), fallback.bold_italic.begin(),
                                      fallback.bold_italic.end());
    }

    // Preferred style first, then degrade rather than fail.
    std::vector<std::string> ordered;
    const auto append = [&ordered](const std::vector<std::string>& names) {
        ordered.insert(ordered.end(), names.begin(), names.end());
    };
    if (bold && italic) {
        append(candidates.bold_italic);
        append(candidates.bold);
        append(candidates.italic);
    } else if (bold) {
        append(candidates.bold);
    } else if (italic) {
        append(candidates.italic);
    }
    append(candidates.regular);

    std::error_code ec;
    for (const std::string& name : ordered) {
        for (const std::string& directory : search_paths_) {
            const fs::path candidate = fs::path(directory) / name;
            if (fs::is_regular_file(candidate, ec)) return candidate.string();
        }
    }

    // Last resort: any TrueType file in the search path, so a headless machine
    // with unusual font packaging still renders text.
    for (const std::string& directory : search_paths_) {
        for (fs::recursive_directory_iterator it(
                 directory, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (it->is_regular_file(ec) && is_font_file(it->path())) return it->path().string();
        }
    }
    return {};
}

FontId FontRegistry::find(std::string_view family, bool bold, bool italic) {
    const std::string path = locate(family, bold, italic);
    if (path.empty()) {
        throw Error(ErrorCode::not_found,
                    "no usable TrueType font found for family '" + std::string(family) +
                        "'; set GRE_FONT to a .ttf path or call "
                        "FontRegistry::add_search_path()");
    }
    try {
        return load_file(path);
    } catch (const Error&) {
        // The located file may be an OpenType/CFF font we cannot parse; try the
        // remaining generic candidates before giving up.
        if (family != "sans") return find("sans", false, false);
        throw;
    }
}

FontId FontRegistry::default_font() {
    if (default_ > 0) return FontId{default_};
    if (const char* env = std::getenv("GRE_FONT"); env != nullptr && *env != '\0') {
        const FontId id = load_file(env);
        default_ = id.value;
        return id;
    }
    const FontId id = find("Arial");
    default_ = id.value;
    return id;
}

void FontRegistry::set_default_font(FontId id) {
    if (id.value <= 0 || static_cast<std::size_t>(id.value) > fonts_.size()) {
        throw Error(ErrorCode::invalid_argument, "invalid font handle");
    }
    default_ = id.value;
}

const Font& FontRegistry::get(FontId id) const {
    int handle = id.value;
    if (handle == 0) {
        // Resolving the default font loads it on first use.  `get` is logically
        // const; the registry caches by design.
        handle = const_cast<FontRegistry*>(this)->default_font().value;
    }
    if (handle <= 0 || static_cast<std::size_t>(handle) > fonts_.size()) {
        throw Error(ErrorCode::invalid_argument, "invalid font handle");
    }
    return *fonts_[static_cast<std::size_t>(handle) - 1];
}

Point rotate_ccw(double dx, double dy, double degrees) {
    if (degrees == 0.0) return Point{dx, dy};
    const double radians = degrees * std::numbers::pi_v<double> / 180.0;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    return Point{dx * c + dy * s, -dx * s + dy * c};
}

Point text_origin(const Font& font, std::string_view text, const TextStyle& style, Point anchor) {
    const double width = font.width(text, style.size);
    const double ascent = font.ascent(style.size);
    const double descent = font.descent(style.size);

    double dx = 0.0;
    switch (style.align) {
        case TextAlign::left:
            dx = 0.0;
            break;
        case TextAlign::center:
            dx = -width / 2.0;
            break;
        case TextAlign::right:
            dx = -width;
            break;
    }
    double dy = 0.0;
    switch (style.valign) {
        case VerticalAlign::baseline:
            dy = 0.0;
            break;
        case VerticalAlign::top:
            dy = ascent;
            break;
        case VerticalAlign::middle:
            dy = (ascent - descent) / 2.0;
            break;
        case VerticalAlign::bottom:
            dy = -descent;
            break;
    }
    const Point offset = rotate_ccw(dx, dy, style.rotation);
    return Point{anchor.x + offset.x, anchor.y + offset.y};
}

Rect text_bounds(const Font& font, std::string_view text, const TextStyle& style, Point anchor) {
    const double width = font.width(text, style.size);
    const double ascent = font.ascent(style.size);
    const double descent = font.descent(style.size);
    const Point origin = text_origin(font, text, style, anchor);

    // Corners of the un-rotated box relative to the baseline origin.
    const Point corners[4] = {{0.0, -ascent}, {width, -ascent}, {width, descent}, {0.0, descent}};
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    for (int i = 0; i < 4; ++i) {
        const Point p = rotate_ccw(corners[i].x, corners[i].y, style.rotation);
        if (i == 0) {
            x0 = x1 = p.x;
            y0 = y1 = p.y;
        } else {
            x0 = std::min(x0, p.x);
            x1 = std::max(x1, p.x);
            y0 = std::min(y0, p.y);
            y1 = std::max(y1, p.y);
        }
    }
    return Rect::from_edges(origin.x + x0, origin.y + y0, origin.x + x1, origin.y + y1);
}

}  // namespace gre
