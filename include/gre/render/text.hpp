#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gre/core/color.hpp"
#include "gre/core/geometry.hpp"

namespace gre {

namespace ttf {
class Face;
}

enum class TextAlign { left, center, right };

// Where the anchor point sits relative to the text box.
enum class VerticalAlign { baseline, top, middle, bottom };

// Handle into the process-wide FontRegistry.  A default-constructed FontId
// means "the default font".
struct FontId {
    int value{0};

    friend bool operator==(FontId a, FontId b) noexcept { return a.value == b.value; }
};

struct TextMetrics {
    double width{};
    double ascent{};      // above the baseline, positive
    double descent{};     // below the baseline, positive
    double line_height{};
};

struct TextStyle {
    FontId font{};
    double size{9.0};
    Color color{colors::black};
    TextAlign align{TextAlign::left};
    VerticalAlign valign{VerticalAlign::baseline};
    // Degrees, counter-clockwise as seen on the page.  90 gives a y-axis label.
    double rotation{0.0};
};

// One glyph placed along the baseline of a run.
struct PositionedGlyph {
    std::uint16_t gid{};
    char32_t codepoint{};
    double x{};  // offset from the run origin, in figure units
};

class Font {
public:
    [[nodiscard]] TextMetrics measure(std::string_view utf8, double size) const;
    [[nodiscard]] double width(std::string_view utf8, double size) const;
    [[nodiscard]] double ascent(double size) const;
    [[nodiscard]] double descent(double size) const;
    [[nodiscard]] double line_height(double size) const;
    [[nodiscard]] double cap_height(double size) const;

    // Glyph ids and offsets for a run; shared by every backend so that raster
    // and vector output place text identically.
    [[nodiscard]] std::vector<PositionedGlyph> shape(std::string_view utf8, double size) const;

    [[nodiscard]] const std::string& postscript_name() const noexcept { return ps_name_; }
    [[nodiscard]] const std::string& family() const noexcept { return family_; }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    // Internal: backends need the underlying face to draw or embed glyphs.
    [[nodiscard]] const ttf::Face& face() const noexcept { return *face_; }

private:
    friend class FontRegistry;
    Font() = default;

    std::shared_ptr<ttf::Face> face_;
    std::string ps_name_;
    std::string family_;
    std::string path_;
};

// Loads and caches fonts.  Fonts are immutable once loaded and shared between
// figures; the registry is process-wide because TextStyle carries only a
// handle.
class FontRegistry {
public:
    [[nodiscard]] static FontRegistry& instance();

    // The font used by TextStyle{}.  Resolved on first use by searching for
    // Arial and then its metric-compatible substitutes.  Override with the
    // GRE_FONT environment variable (a path to a .ttf/.ttc).
    [[nodiscard]] FontId default_font();
    void set_default_font(FontId id);

    [[nodiscard]] FontId load_file(const std::string& path, int face_index = 0);

    // family: "Arial"/"Helvetica"/"sans", "Times"/"serif", "Courier"/"mono",
    // or any family name present in the search path.  Throws when nothing
    // usable is found.
    [[nodiscard]] FontId find(std::string_view family, bool bold = false, bool italic = false);

    [[nodiscard]] const Font& get(FontId id) const;

    void add_search_path(std::string directory);
    [[nodiscard]] const std::vector<std::string>& search_paths() const noexcept {
        return search_paths_;
    }

private:
    FontRegistry();

    [[nodiscard]] std::string locate(std::string_view family, bool bold, bool italic) const;

    std::vector<std::unique_ptr<Font>> fonts_;
    std::vector<std::string> search_paths_;
    std::vector<std::pair<std::string, int>> by_path_;
    int default_{0};  // 0 until the default font has been resolved
};

[[nodiscard]] inline FontRegistry& fonts() { return FontRegistry::instance(); }

// Baseline origin for `text` drawn with `style` anchored at `anchor`.  All
// backends route through this so alignment is identical everywhere.
[[nodiscard]] Point text_origin(const Font& font, std::string_view text, const TextStyle& style,
                                Point anchor);

// Bounding box of the run in figure coordinates, accounting for rotation.
[[nodiscard]] Rect text_bounds(const Font& font, std::string_view text, const TextStyle& style,
                               Point anchor);

// Rotates (dx, dy) counter-clockwise on the page (y grows downwards).
[[nodiscard]] Point rotate_ccw(double dx, double dy, double degrees);

// Decodes one UTF-8 code point, advancing `pos`.  Invalid bytes yield U+FFFD.
[[nodiscard]] char32_t decode_utf8(std::string_view text, std::size_t& pos);

}  // namespace gre
