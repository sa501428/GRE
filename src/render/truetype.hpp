#pragma once

// A small TrueType reader: enough to measure text, walk glyph outlines, and
// emit a subsetted font program for PDF embedding.  Deliberately not a general
// font library -- no hinting, no shaping, no OpenType layout tables.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gre::ttf {

// (x, y) -> (a*x + c*y + e, b*x + d*y + f)
struct Affine {
    double a{1.0}, b{0.0}, c{0.0}, d{1.0}, e{0.0}, f{0.0};

    [[nodiscard]] static Affine scaling(double sx, double sy) { return Affine{sx, 0, 0, sy, 0, 0}; }
    [[nodiscard]] static Affine translation(double dx, double dy) {
        return Affine{1, 0, 0, 1, dx, dy};
    }
    [[nodiscard]] double map_x(double x, double y) const { return a * x + c * y + e; }
    [[nodiscard]] double map_y(double x, double y) const { return b * x + d * y + f; }
};

// outer applied after inner.
[[nodiscard]] Affine concat(const Affine& outer, const Affine& inner);

// Receives glyph outlines in the coordinate space produced by the transform
// handed to Face::outline.  Contours are always closed.
class OutlineSink {
public:
    virtual ~OutlineSink() = default;
    virtual void move_to(double x, double y) = 0;
    virtual void line_to(double x, double y) = 0;
    virtual void quad_to(double cx, double cy, double x, double y) = 0;
    virtual void close() = 0;
};

struct FaceMetrics {
    int units_per_em{1000};
    int ascender{800};
    int descender{-200};
    int line_gap{0};
    int x_min{0}, y_min{0}, x_max{1000}, y_max{1000};
    int cap_height{700};
    int x_height{500};
    int weight_class{400};
    double italic_angle{0.0};
    bool fixed_pitch{false};
    // fsType bit 1 set means the vendor forbids embedding outright.
    bool embedding_restricted{false};
};

class Face {
public:
    // `data` is retained: the PDF backend embeds a subset of these bytes.
    [[nodiscard]] static std::shared_ptr<Face> from_memory(std::vector<std::uint8_t> data,
                                                           int face_index = 0);
    [[nodiscard]] static std::shared_ptr<Face> from_file(const std::string& path,
                                                         int face_index = 0);

    [[nodiscard]] std::uint16_t glyph_index(char32_t codepoint) const;
    [[nodiscard]] int advance_units(std::uint16_t gid) const;
    [[nodiscard]] int num_glyphs() const noexcept { return num_glyphs_; }
    [[nodiscard]] const FaceMetrics& metrics() const noexcept { return metrics_; }
    [[nodiscard]] const std::string& postscript_name() const noexcept { return ps_name_; }
    [[nodiscard]] const std::string& family_name() const noexcept { return family_name_; }
    [[nodiscard]] const std::vector<std::uint8_t>& data() const noexcept { return data_; }

    // Walks the glyph, mapping font units through `transform`.  Returns false
    // for an empty glyph (a space, say).
    bool outline(std::uint16_t gid, const Affine& transform, OutlineSink& sink) const;

    // A valid TrueType font containing only `gids` (plus everything composite
    // glyphs among them reference, plus .notdef).  Glyph ids are preserved, so
    // the result can be used with /CIDToGIDMap /Identity.
    [[nodiscard]] std::vector<std::uint8_t> subset(const std::vector<std::uint16_t>& gids) const;

private:
    struct Table {
        std::uint32_t offset{};
        std::uint32_t length{};
    };

    void parse();
    void parse_head();
    void parse_hhea_maxp();
    void parse_os2_post();
    void parse_names();
    void parse_cmap();
    [[nodiscard]] const Table* table(const char* tag) const;
    [[nodiscard]] std::uint32_t glyph_offset(std::uint16_t gid) const;
    [[nodiscard]] std::uint32_t glyph_length(std::uint16_t gid) const;
    void outline_impl(std::uint16_t gid, const Affine& transform, OutlineSink& sink,
                      int depth) const;
    void collect_components(std::uint16_t gid, std::vector<std::uint8_t>& seen, int depth) const;

    std::vector<std::uint8_t> data_;
    std::uint32_t base_{0};
    std::map<std::string, Table, std::less<>> tables_;
    FaceMetrics metrics_{};
    std::string ps_name_;
    std::string family_name_;
    int num_glyphs_{0};
    int num_hmetrics_{0};
    bool long_loca_{false};
    std::uint32_t cmap_subtable_{0};
    int cmap_format_{-1};
    bool cmap_symbol_{false};
};

}  // namespace gre::ttf
