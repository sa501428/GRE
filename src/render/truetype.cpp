#include "render/truetype.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "gre/core/error.hpp"

namespace gre::ttf {
namespace {

[[noreturn]] void fail(const std::string& what) {
    throw Error(ErrorCode::invalid_argument, "font: " + what);
}

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    [[nodiscard]] std::uint8_t u8(std::size_t offset) const {
        require(offset, 1);
        return data_[offset];
    }
    [[nodiscard]] std::uint16_t u16(std::size_t offset) const {
        require(offset, 2);
        return static_cast<std::uint16_t>((data_[offset] << 8) | data_[offset + 1]);
    }
    [[nodiscard]] std::int16_t i16(std::size_t offset) const {
        return static_cast<std::int16_t>(u16(offset));
    }
    [[nodiscard]] std::uint32_t u32(std::size_t offset) const {
        require(offset, 4);
        return (static_cast<std::uint32_t>(data_[offset]) << 24) |
               (static_cast<std::uint32_t>(data_[offset + 1]) << 16) |
               (static_cast<std::uint32_t>(data_[offset + 2]) << 8) |
               static_cast<std::uint32_t>(data_[offset + 3]);
    }
    // F2Dot14 fixed point, used by composite glyph transforms.
    [[nodiscard]] double f2dot14(std::size_t offset) const {
        return static_cast<double>(i16(offset)) / 16384.0;
    }
    [[nodiscard]] bool has(std::size_t offset, std::size_t length) const {
        return offset <= size_ && length <= size_ - offset;
    }

private:
    void require(std::size_t offset, std::size_t length) const {
        if (!has(offset, length)) fail("truncated table (read past end of file)");
    }

    const std::uint8_t* data_;
    std::size_t size_;
};

std::string tag_string(std::uint32_t tag) {
    std::string out(4, '\0');
    out[0] = static_cast<char>((tag >> 24) & 0xFF);
    out[1] = static_cast<char>((tag >> 16) & 0xFF);
    out[2] = static_cast<char>((tag >> 8) & 0xFF);
    out[3] = static_cast<char>(tag & 0xFF);
    return out;
}

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void write_u32_at(std::vector<std::uint8_t>& out, std::size_t offset, std::uint32_t value) {
    out[offset] = static_cast<std::uint8_t>(value >> 24);
    out[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    out[offset + 2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    out[offset + 3] = static_cast<std::uint8_t>(value & 0xFF);
}

// sfnt table checksum: the sum of the table's big-endian 32-bit words, with
// the tail zero padded.
std::uint32_t table_checksum(const std::uint8_t* data, std::size_t length) {
    std::uint32_t sum = 0;
    std::size_t i = 0;
    for (; i + 4 <= length; i += 4) {
        sum += (static_cast<std::uint32_t>(data[i]) << 24) |
               (static_cast<std::uint32_t>(data[i + 1]) << 16) |
               (static_cast<std::uint32_t>(data[i + 2]) << 8) |
               static_cast<std::uint32_t>(data[i + 3]);
    }
    if (i < length) {
        std::uint32_t tail = 0;
        for (std::size_t k = 0; k < 4; ++k) {
            tail <<= 8;
            if (i + k < length) tail |= data[i + k];
        }
        sum += tail;
    }
    return sum;
}

constexpr int kMaxCompositeDepth = 8;

// Composite glyph flags.
constexpr std::uint16_t kArg1And2AreWords = 0x0001;
constexpr std::uint16_t kArgsAreXyValues = 0x0002;
constexpr std::uint16_t kWeHaveAScale = 0x0008;
constexpr std::uint16_t kMoreComponents = 0x0020;
constexpr std::uint16_t kWeHaveAnXAndYScale = 0x0040;
constexpr std::uint16_t kWeHaveATwoByTwo = 0x0080;

}  // namespace

Affine concat(const Affine& outer, const Affine& inner) {
    Affine out;
    out.a = outer.a * inner.a + outer.c * inner.b;
    out.b = outer.b * inner.a + outer.d * inner.b;
    out.c = outer.a * inner.c + outer.c * inner.d;
    out.d = outer.b * inner.c + outer.d * inner.d;
    out.e = outer.a * inner.e + outer.c * inner.f + outer.e;
    out.f = outer.b * inner.e + outer.d * inner.f + outer.f;
    return out;
}

std::shared_ptr<Face> Face::from_file(const std::string& path, int face_index) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw Error(ErrorCode::io, "cannot open font file: " + path);
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    if (size <= 0) throw Error(ErrorCode::io, "empty font file: " + path);
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(data.data()), size);
    if (!stream) throw Error(ErrorCode::io, "short read on font file: " + path);
    return from_memory(std::move(data), face_index);
}

std::shared_ptr<Face> Face::from_memory(std::vector<std::uint8_t> data, int face_index) {
    auto face = std::shared_ptr<Face>(new Face());
    face->data_ = std::move(data);
    const Reader reader(face->data_.data(), face->data_.size());

    std::uint32_t base = 0;
    const std::uint32_t sfnt = reader.u32(0);
    if (sfnt == 0x74746366u) {  // 'ttcf': a collection, take the requested face.
        const std::uint32_t count = reader.u32(8);
        if (face_index < 0 || static_cast<std::uint32_t>(face_index) >= count) {
            fail("face index out of range in font collection");
        }
        base = reader.u32(12 + 4 * static_cast<std::size_t>(face_index));
    } else if (sfnt != 0x00010000u && sfnt != 0x74727565u /* 'true' */) {
        if (sfnt == 0x4F54544Fu) fail("OpenType/CFF fonts are not supported; use a TrueType font");
        fail("unrecognised font format");
    }
    face->base_ = base;
    face->parse();
    return face;
}

void Face::parse() {
    const Reader reader(data_.data(), data_.size());
    const std::uint16_t num_tables = reader.u16(base_ + 4);
    for (std::uint16_t i = 0; i < num_tables; ++i) {
        const std::size_t record = base_ + 12 + 16 * static_cast<std::size_t>(i);
        Table entry;
        const std::string tag = tag_string(reader.u32(record));
        entry.offset = reader.u32(record + 8);
        entry.length = reader.u32(record + 12);
        if (!reader.has(entry.offset, entry.length)) {
            // Some fonts pad the last table's recorded length; clamp instead of
            // rejecting an otherwise usable file.
            if (entry.offset >= data_.size()) continue;
            entry.length = static_cast<std::uint32_t>(data_.size() - entry.offset);
        }
        tables_.emplace(tag, entry);
    }
    if (!table("head") || !table("hhea") || !table("maxp") || !table("hmtx")) {
        fail("missing a required table (head/hhea/maxp/hmtx)");
    }
    if (!table("glyf") || !table("loca")) {
        fail("no glyf/loca table; only TrueType outlines are supported");
    }
    parse_head();
    parse_hhea_maxp();
    parse_os2_post();
    parse_names();
    parse_cmap();
}

const Face::Table* Face::table(const char* tag) const {
    const auto it = tables_.find(tag);
    return it == tables_.end() ? nullptr : &it->second;
}

void Face::parse_head() {
    const Reader reader(data_.data(), data_.size());
    const std::uint32_t head = table("head")->offset;
    metrics_.units_per_em = reader.u16(head + 18);
    if (metrics_.units_per_em == 0) metrics_.units_per_em = 1000;
    metrics_.x_min = reader.i16(head + 36);
    metrics_.y_min = reader.i16(head + 38);
    metrics_.x_max = reader.i16(head + 40);
    metrics_.y_max = reader.i16(head + 42);
    long_loca_ = reader.i16(head + 50) != 0;
}

void Face::parse_hhea_maxp() {
    const Reader reader(data_.data(), data_.size());
    const std::uint32_t hhea = table("hhea")->offset;
    metrics_.ascender = reader.i16(hhea + 4);
    metrics_.descender = reader.i16(hhea + 6);
    metrics_.line_gap = reader.i16(hhea + 8);
    num_hmetrics_ = reader.u16(hhea + 34);
    num_glyphs_ = reader.u16(table("maxp")->offset + 4);
    if (num_hmetrics_ <= 0) fail("hhea declares no horizontal metrics");
}

void Face::parse_os2_post() {
    const Reader reader(data_.data(), data_.size());
    if (const Table* os2 = table("OS/2"); os2 != nullptr && os2->length >= 78) {
        const std::uint16_t version = reader.u16(os2->offset);
        metrics_.weight_class = reader.u16(os2->offset + 4);
        const std::uint16_t fs_type = reader.u16(os2->offset + 8);
        metrics_.embedding_restricted = (fs_type & 0x0002) != 0;
        if (version >= 2 && os2->length >= 90) {
            metrics_.x_height = reader.i16(os2->offset + 86);
            metrics_.cap_height = reader.i16(os2->offset + 88);
        }
        if (os2->length >= 72) {
            const int typo_ascender = reader.i16(os2->offset + 68);
            const int typo_descender = reader.i16(os2->offset + 70);
            // hhea values are what browsers and PDF viewers line-break with, but
            // some fonts leave them at zero.
            if (metrics_.ascender == 0 && typo_ascender != 0) metrics_.ascender = typo_ascender;
            if (metrics_.descender == 0 && typo_descender != 0) {
                metrics_.descender = typo_descender;
            }
        }
    }
    if (metrics_.cap_height <= 0) {
        metrics_.cap_height = static_cast<int>(metrics_.ascender * 0.7);
    }
    if (metrics_.x_height <= 0) {
        metrics_.x_height = static_cast<int>(metrics_.ascender * 0.5);
    }
    if (const Table* post = table("post"); post != nullptr && post->length >= 16) {
        // italicAngle is a 16.16 signed fixed point value.
        const auto fixed = static_cast<std::int32_t>(reader.u32(post->offset + 4));
        metrics_.italic_angle = static_cast<double>(fixed) / 65536.0;
        metrics_.fixed_pitch = reader.u32(post->offset + 12) != 0;
    }
}

void Face::parse_names() {
    const Table* name = table("name");
    if (name == nullptr || name->length < 6) {
        ps_name_ = "UnknownFont";
        return;
    }
    const Reader reader(data_.data(), data_.size());
    const std::uint16_t count = reader.u16(name->offset + 2);
    const std::uint16_t storage = reader.u16(name->offset + 4);

    const auto read_name = [&](std::uint16_t wanted_id) -> std::string {
        std::string best;
        int best_score = -1;
        for (std::uint16_t i = 0; i < count; ++i) {
            const std::size_t record = name->offset + 6 + 12 * static_cast<std::size_t>(i);
            if (!reader.has(record, 12)) break;
            const std::uint16_t platform = reader.u16(record);
            const std::uint16_t encoding = reader.u16(record + 2);
            const std::uint16_t name_id = reader.u16(record + 6);
            if (name_id != wanted_id) continue;
            const std::uint16_t length = reader.u16(record + 8);
            const std::uint16_t offset = reader.u16(record + 10);
            const std::size_t start = name->offset + storage + offset;
            if (!reader.has(start, length)) continue;

            // Windows/Unicode names are UTF-16BE; Macintosh Roman is close
            // enough to ASCII for the names we care about.
            const bool utf16 = platform == 3 || platform == 0;
            const int score = platform == 3 && encoding == 1 ? 2 : (platform == 1 ? 1 : 0);
            if (score <= best_score) continue;
            std::string value;
            if (utf16) {
                for (std::size_t k = 0; k + 1 < length; k += 2) {
                    const std::uint16_t unit = reader.u16(start + k);
                    value.push_back(unit < 0x80 ? static_cast<char>(unit) : '?');
                }
            } else {
                for (std::size_t k = 0; k < length; ++k) {
                    value.push_back(static_cast<char>(reader.u8(start + k)));
                }
            }
            best = value;
            best_score = score;
        }
        return best;
    };

    ps_name_ = read_name(6);
    family_name_ = read_name(1);
    if (ps_name_.empty()) ps_name_ = family_name_.empty() ? "UnknownFont" : family_name_;
    // PostScript names may not contain spaces or PDF delimiters.
    std::string cleaned;
    for (char c : ps_name_) {
        if (c > 32 && c < 127 && c != '(' && c != ')' && c != '<' && c != '>' && c != '[' &&
            c != ']' && c != '{' && c != '}' && c != '/' && c != '%' && c != '#') {
            cleaned.push_back(c);
        }
    }
    ps_name_ = cleaned.empty() ? "UnknownFont" : cleaned;
}

void Face::parse_cmap() {
    const Table* cmap = table("cmap");
    if (cmap == nullptr) return;
    const Reader reader(data_.data(), data_.size());
    const std::uint16_t count = reader.u16(cmap->offset + 2);

    int best_score = -1;
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::size_t record = cmap->offset + 4 + 8 * static_cast<std::size_t>(i);
        if (!reader.has(record, 8)) break;
        const std::uint16_t platform = reader.u16(record);
        const std::uint16_t encoding = reader.u16(record + 2);
        const std::uint32_t offset = reader.u32(record + 4);
        const std::size_t subtable = cmap->offset + offset;
        if (!reader.has(subtable, 4)) continue;
        const std::uint16_t format = reader.u16(subtable);
        if (format != 0 && format != 4 && format != 6 && format != 12) continue;

        int score = 0;
        bool symbol = false;
        if (platform == 3 && encoding == 10) {
            score = 5;
        } else if (platform == 3 && encoding == 1) {
            score = 4;
        } else if (platform == 0) {
            score = 3;
        } else if (platform == 3 && encoding == 0) {
            score = 2;
            symbol = true;
        } else if (platform == 1 && encoding == 0) {
            score = 1;
        }
        if (score > best_score) {
            best_score = score;
            cmap_subtable_ = static_cast<std::uint32_t>(subtable);
            cmap_format_ = format;
            cmap_symbol_ = symbol;
        }
    }
}

std::uint16_t Face::glyph_index(char32_t codepoint) const {
    if (cmap_format_ < 0) return 0;
    const Reader reader(data_.data(), data_.size());
    auto cp = static_cast<std::uint32_t>(codepoint);
    // Symbol fonts place their glyphs in the private use area.
    if (cmap_symbol_ && cp < 0x100) cp += 0xF000;

    switch (cmap_format_) {
        case 0: {
            if (cp > 255) return 0;
            return reader.u8(cmap_subtable_ + 6 + cp);
        }
        case 4: {
            if (cp > 0xFFFF) return 0;
            const std::uint16_t seg_count = reader.u16(cmap_subtable_ + 6) / 2;
            if (seg_count == 0) return 0;
            const std::size_t end_codes = cmap_subtable_ + 14;
            const std::size_t start_codes = end_codes + 2 * seg_count + 2;
            const std::size_t id_deltas = start_codes + 2 * seg_count;
            const std::size_t id_range_offsets = id_deltas + 2 * seg_count;

            std::uint16_t lo = 0;
            std::uint16_t hi = seg_count;
            while (lo < hi) {
                const std::uint16_t mid = static_cast<std::uint16_t>((lo + hi) / 2);
                if (reader.u16(end_codes + 2u * mid) < cp) {
                    lo = static_cast<std::uint16_t>(mid + 1);
                } else {
                    hi = mid;
                }
            }
            if (lo >= seg_count) return 0;
            const std::uint16_t start = reader.u16(start_codes + 2u * lo);
            if (cp < start) return 0;
            const std::uint16_t range_offset = reader.u16(id_range_offsets + 2u * lo);
            const std::uint16_t delta = reader.u16(id_deltas + 2u * lo);
            if (range_offset == 0) {
                return static_cast<std::uint16_t>((cp + delta) & 0xFFFF);
            }
            const std::size_t address =
                id_range_offsets + 2u * lo + range_offset + 2u * (cp - start);
            if (!reader.has(address, 2)) return 0;
            const std::uint16_t gid = reader.u16(address);
            if (gid == 0) return 0;
            return static_cast<std::uint16_t>((gid + delta) & 0xFFFF);
        }
        case 6: {
            const std::uint16_t first = reader.u16(cmap_subtable_ + 6);
            const std::uint16_t entries = reader.u16(cmap_subtable_ + 8);
            if (cp < first || cp >= static_cast<std::uint32_t>(first) + entries) return 0;
            return reader.u16(cmap_subtable_ + 10 + 2 * (cp - first));
        }
        case 12: {
            const std::uint32_t groups = reader.u32(cmap_subtable_ + 12);
            std::uint32_t lo = 0;
            std::uint32_t hi = groups;
            while (lo < hi) {
                const std::uint32_t mid = lo + (hi - lo) / 2;
                const std::size_t entry = cmap_subtable_ + 16 + 12ull * mid;
                if (!reader.has(entry, 12)) return 0;
                if (reader.u32(entry + 4) < cp) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            if (lo >= groups) return 0;
            const std::size_t entry = cmap_subtable_ + 16 + 12ull * lo;
            const std::uint32_t start = reader.u32(entry);
            if (cp < start) return 0;
            return static_cast<std::uint16_t>(reader.u32(entry + 8) + (cp - start));
        }
        default:
            return 0;
    }
}

int Face::advance_units(std::uint16_t gid) const {
    const Reader reader(data_.data(), data_.size());
    const std::uint32_t hmtx = table("hmtx")->offset;
    const int index = std::min<int>(gid, num_hmetrics_ - 1);
    return reader.u16(hmtx + 4u * static_cast<std::uint32_t>(index));
}

std::uint32_t Face::glyph_offset(std::uint16_t gid) const {
    const Reader reader(data_.data(), data_.size());
    const std::uint32_t loca = table("loca")->offset;
    if (long_loca_) return reader.u32(loca + 4u * gid);
    return static_cast<std::uint32_t>(reader.u16(loca + 2u * gid)) * 2u;
}

std::uint32_t Face::glyph_length(std::uint16_t gid) const {
    if (gid + 1 > num_glyphs_) return 0;
    const std::uint32_t start = glyph_offset(gid);
    const std::uint32_t end = glyph_offset(static_cast<std::uint16_t>(gid + 1));
    return end > start ? end - start : 0;
}

bool Face::outline(std::uint16_t gid, const Affine& transform, OutlineSink& sink) const {
    if (gid >= num_glyphs_) return false;
    if (glyph_length(gid) == 0) return false;
    outline_impl(gid, transform, sink, 0);
    return true;
}

void Face::outline_impl(std::uint16_t gid, const Affine& transform, OutlineSink& sink,
                        int depth) const {
    if (depth > kMaxCompositeDepth || gid >= num_glyphs_) return;
    const std::uint32_t length = glyph_length(gid);
    if (length == 0) return;
    const Reader reader(data_.data(), data_.size());
    const std::size_t glyph = table("glyf")->offset + glyph_offset(gid);
    const std::int16_t contours = reader.i16(glyph);

    if (contours < 0) {
        // Composite glyph: each component is another glyph plus a placement.
        std::size_t cursor = glyph + 10;
        std::uint16_t flags = 0;
        do {
            flags = reader.u16(cursor);
            const std::uint16_t component = reader.u16(cursor + 2);
            cursor += 4;
            double dx = 0.0;
            double dy = 0.0;
            if ((flags & kArg1And2AreWords) != 0) {
                const std::int16_t arg1 = reader.i16(cursor);
                const std::int16_t arg2 = reader.i16(cursor + 2);
                cursor += 4;
                if ((flags & kArgsAreXyValues) != 0) {
                    dx = arg1;
                    dy = arg2;
                }
            } else {
                const auto arg1 = static_cast<std::int8_t>(reader.u8(cursor));
                const auto arg2 = static_cast<std::int8_t>(reader.u8(cursor + 1));
                cursor += 2;
                if ((flags & kArgsAreXyValues) != 0) {
                    dx = arg1;
                    dy = arg2;
                }
            }
            Affine component_transform = Affine::translation(dx, dy);
            if ((flags & kWeHaveAScale) != 0) {
                const double s = reader.f2dot14(cursor);
                cursor += 2;
                component_transform.a = s;
                component_transform.d = s;
            } else if ((flags & kWeHaveAnXAndYScale) != 0) {
                component_transform.a = reader.f2dot14(cursor);
                component_transform.d = reader.f2dot14(cursor + 2);
                cursor += 4;
            } else if ((flags & kWeHaveATwoByTwo) != 0) {
                component_transform.a = reader.f2dot14(cursor);
                component_transform.b = reader.f2dot14(cursor + 2);
                component_transform.c = reader.f2dot14(cursor + 4);
                component_transform.d = reader.f2dot14(cursor + 6);
                cursor += 8;
            }
            outline_impl(component, concat(transform, component_transform), sink, depth + 1);
        } while ((flags & kMoreComponents) != 0);
        return;
    }

    if (contours == 0) return;
    const auto contour_count = static_cast<std::size_t>(contours);
    const std::size_t end_points = glyph + 10;
    const std::size_t point_count = reader.u16(end_points + 2 * (contour_count - 1)) + 1u;
    const std::uint16_t instruction_length = reader.u16(end_points + 2 * contour_count);
    std::size_t cursor = end_points + 2 * contour_count + 2 + instruction_length;

    std::vector<std::uint8_t> flags(point_count);
    for (std::size_t i = 0; i < point_count;) {
        const std::uint8_t flag = reader.u8(cursor++);
        flags[i++] = flag;
        if ((flag & 0x08) != 0) {  // repeat
            std::uint8_t repeats = reader.u8(cursor++);
            while (repeats-- > 0 && i < point_count) flags[i++] = flag;
        }
    }

    std::vector<double> xs(point_count);
    std::vector<double> ys(point_count);
    std::int32_t value = 0;
    for (std::size_t i = 0; i < point_count; ++i) {
        const std::uint8_t flag = flags[i];
        if ((flag & 0x02) != 0) {
            const std::uint8_t delta = reader.u8(cursor++);
            value += (flag & 0x10) != 0 ? delta : -static_cast<std::int32_t>(delta);
        } else if ((flag & 0x10) == 0) {
            value += reader.i16(cursor);
            cursor += 2;
        }
        xs[i] = value;
    }
    value = 0;
    for (std::size_t i = 0; i < point_count; ++i) {
        const std::uint8_t flag = flags[i];
        if ((flag & 0x04) != 0) {
            const std::uint8_t delta = reader.u8(cursor++);
            value += (flag & 0x20) != 0 ? delta : -static_cast<std::int32_t>(delta);
        } else if ((flag & 0x20) == 0) {
            value += reader.i16(cursor);
            cursor += 2;
        }
        ys[i] = value;
    }

    std::size_t first_point = 0;
    for (std::size_t contour = 0; contour < contour_count; ++contour) {
        const std::size_t last_point = reader.u16(end_points + 2 * contour);
        if (last_point < first_point || last_point >= point_count) break;
        const std::size_t n = last_point - first_point + 1;

        const auto on_curve = [&](std::size_t i) {
            return (flags[first_point + i] & 0x01) != 0;
        };
        const auto px = [&](std::size_t i) { return xs[first_point + i]; };
        const auto py = [&](std::size_t i) { return ys[first_point + i]; };
        const auto emit_move = [&](double x, double y) {
            sink.move_to(transform.map_x(x, y), transform.map_y(x, y));
        };
        const auto emit_line = [&](double x, double y) {
            sink.line_to(transform.map_x(x, y), transform.map_y(x, y));
        };
        const auto emit_quad = [&](double cx, double cy, double x, double y) {
            sink.quad_to(transform.map_x(cx, cy), transform.map_y(cx, cy), transform.map_x(x, y),
                         transform.map_y(x, y));
        };

        // Start at the first on-curve point.  A contour made entirely of
        // off-curve points starts at the midpoint of the wrap-around pair.
        std::size_t start_index = 0;
        double start_x = 0.0;
        double start_y = 0.0;
        bool found_on_curve = false;
        for (std::size_t i = 0; i < n; ++i) {
            if (on_curve(i)) {
                start_index = i;
                start_x = px(i);
                start_y = py(i);
                found_on_curve = true;
                break;
            }
        }
        if (!found_on_curve) {
            start_index = n - 1;
            start_x = (px(n - 1) + px(0)) / 2.0;
            start_y = (py(n - 1) + py(0)) / 2.0;
        }

        emit_move(start_x, start_y);
        bool have_control = false;
        double control_x = 0.0;
        double control_y = 0.0;
        for (std::size_t k = 1; k <= n; ++k) {
            const std::size_t i = (start_index + k) % n;
            if (on_curve(i)) {
                if (have_control) {
                    emit_quad(control_x, control_y, px(i), py(i));
                    have_control = false;
                } else {
                    emit_line(px(i), py(i));
                }
            } else {
                if (have_control) {
                    // Two consecutive off-curve points imply an on-curve point
                    // halfway between them.
                    emit_quad(control_x, control_y, (control_x + px(i)) / 2.0,
                              (control_y + py(i)) / 2.0);
                }
                control_x = px(i);
                control_y = py(i);
                have_control = true;
            }
        }
        if (have_control) emit_quad(control_x, control_y, start_x, start_y);
        sink.close();

        first_point = last_point + 1;
    }
}

void Face::collect_components(std::uint16_t gid, std::vector<std::uint8_t>& seen, int depth) const {
    if (depth > kMaxCompositeDepth || gid >= num_glyphs_) return;
    if (seen[gid]) return;
    seen[gid] = 1;
    const std::uint32_t length = glyph_length(gid);
    if (length < 10) return;
    const Reader reader(data_.data(), data_.size());
    const std::size_t glyph = table("glyf")->offset + glyph_offset(gid);
    if (reader.i16(glyph) >= 0) return;

    std::size_t cursor = glyph + 10;
    std::uint16_t flags = 0;
    do {
        flags = reader.u16(cursor);
        const std::uint16_t component = reader.u16(cursor + 2);
        cursor += 4;
        cursor += (flags & kArg1And2AreWords) != 0 ? 4 : 2;
        if ((flags & kWeHaveAScale) != 0) {
            cursor += 2;
        } else if ((flags & kWeHaveAnXAndYScale) != 0) {
            cursor += 4;
        } else if ((flags & kWeHaveATwoByTwo) != 0) {
            cursor += 8;
        }
        collect_components(component, seen, depth + 1);
    } while ((flags & kMoreComponents) != 0);
}

std::vector<std::uint8_t> Face::subset(const std::vector<std::uint16_t>& gids) const {
    std::vector<std::uint8_t> keep(static_cast<std::size_t>(num_glyphs_), 0);
    collect_components(0, keep, 0);  // .notdef is always present
    for (std::uint16_t gid : gids) {
        if (gid < num_glyphs_) collect_components(gid, keep, 0);
    }

    // Glyph ids are preserved so that /CIDToGIDMap /Identity stays valid; the
    // glyphs we dropped simply become zero-length entries in loca.
    const std::uint32_t glyf_base = table("glyf")->offset;
    std::vector<std::uint8_t> glyf;
    std::vector<std::uint32_t> loca;
    loca.reserve(static_cast<std::size_t>(num_glyphs_) + 1);
    for (int gid = 0; gid < num_glyphs_; ++gid) {
        loca.push_back(static_cast<std::uint32_t>(glyf.size()));
        if (!keep[static_cast<std::size_t>(gid)]) continue;
        const std::uint32_t offset = glyph_offset(static_cast<std::uint16_t>(gid));
        const std::uint32_t length = glyph_length(static_cast<std::uint16_t>(gid));
        if (length == 0) continue;
        const std::uint8_t* start = data_.data() + glyf_base + offset;
        glyf.insert(glyf.end(), start, start + length);
        while (glyf.size() % 4 != 0) glyf.push_back(0);  // glyphs are 4-byte aligned
    }
    loca.push_back(static_cast<std::uint32_t>(glyf.size()));

    std::vector<std::uint8_t> loca_bytes;
    loca_bytes.reserve(loca.size() * 4);
    for (std::uint32_t offset : loca) put_u32(loca_bytes, offset);

    // head must advertise the long loca format we just wrote.
    const Table* head_table = table("head");
    std::vector<std::uint8_t> head(data_.data() + head_table->offset,
                                   data_.data() + head_table->offset + head_table->length);
    if (head.size() >= 54) {
        head[50] = 0;
        head[51] = 1;
        // checkSumAdjustment is recomputed once the whole file is laid out.
        head[8] = head[9] = head[10] = head[11] = 0;
    }

    struct OutTable {
        std::string tag;
        std::vector<std::uint8_t> bytes;
    };
    std::vector<OutTable> out_tables;
    const auto copy_table = [&](const char* tag) {
        if (const Table* t = table(tag); t != nullptr && t->length > 0) {
            out_tables.push_back({tag, std::vector<std::uint8_t>(
                                           data_.data() + t->offset,
                                           data_.data() + t->offset + t->length)});
        }
    };
    // A PDF CIDFontType2 needs the outlines and metrics; the hinting programs
    // are kept because the glyph instructions we copied refer to them.
    copy_table("cvt ");
    copy_table("fpgm");
    out_tables.push_back({"glyf", std::move(glyf)});
    out_tables.push_back({"head", std::move(head)});
    copy_table("hhea");
    copy_table("hmtx");
    out_tables.push_back({"loca", std::move(loca_bytes)});
    copy_table("maxp");
    copy_table("prep");

    std::sort(out_tables.begin(), out_tables.end(),
              [](const OutTable& a, const OutTable& b) { return a.tag < b.tag; });

    const auto count = static_cast<std::uint16_t>(out_tables.size());
    std::uint16_t entry_selector = 0;
    while ((1u << (entry_selector + 1)) <= count) ++entry_selector;
    const auto search_range = static_cast<std::uint16_t>((1u << entry_selector) * 16u);
    const auto range_shift = static_cast<std::uint16_t>(count * 16u - search_range);

    std::vector<std::uint8_t> out;
    put_u32(out, 0x00010000u);
    put_u16(out, count);
    put_u16(out, search_range);
    put_u16(out, entry_selector);
    put_u16(out, range_shift);

    const std::size_t directory = out.size();
    out.resize(directory + 16u * count, 0);

    std::size_t head_offset_in_file = 0;
    for (std::size_t i = 0; i < out_tables.size(); ++i) {
        auto& entry = out_tables[i];
        while (out.size() % 4 != 0) out.push_back(0);
        const auto offset = static_cast<std::uint32_t>(out.size());
        const auto length = static_cast<std::uint32_t>(entry.bytes.size());
        const std::uint32_t checksum = table_checksum(entry.bytes.data(), entry.bytes.size());
        if (entry.tag == "head") head_offset_in_file = offset;

        const std::size_t record = directory + 16 * i;
        for (int k = 0; k < 4; ++k) {
            out[record + static_cast<std::size_t>(k)] =
                static_cast<std::uint8_t>(entry.tag[static_cast<std::size_t>(k)]);
        }
        write_u32_at(out, record + 4, checksum);
        write_u32_at(out, record + 8, offset);
        write_u32_at(out, record + 12, length);
        out.insert(out.end(), entry.bytes.begin(), entry.bytes.end());
    }
    while (out.size() % 4 != 0) out.push_back(0);

    if (head_offset_in_file != 0 && head_offset_in_file + 12 <= out.size()) {
        const std::uint32_t total = table_checksum(out.data(), out.size());
        write_u32_at(out, head_offset_in_file + 8, 0xB1B0AFBAu - total);
    }
    return out;
}

}  // namespace gre::ttf
