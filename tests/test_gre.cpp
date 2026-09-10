#include <zlib.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "gre/gre.hpp"
#include "render/truetype.hpp"  // internal: exercises the font subsetter directly

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* expression, int line) {
    ++checks;
    if (!condition) {
        std::cerr << "failure at line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)
#define CHECK_NEAR(a, b, tolerance) \
    check(std::fabs((a) - (b)) <= (tolerance), #a " ~= " #b, __LINE__)

using namespace gre;

std::filesystem::path scratch(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

// ---------------------------------------------------------------------------

void test_geometry() {
    const Rect rect{10.0, 20.0, 100.0, 50.0};
    CHECK(rect.right() == 110.0);
    CHECK(rect.bottom() == 70.0);
    CHECK(rect.center_x() == 60.0);

    const Rect inner = rect.inset(Insets{5.0});
    CHECK(inner.x == 15.0 && inner.y == 25.0);
    CHECK(inner.width == 90.0 && inner.height == 40.0);

    const Rect clipped = rect.intersect(Rect{60.0, 0.0, 100.0, 100.0});
    CHECK(clipped.x == 60.0 && clipped.right() == 110.0);
    CHECK(rect.intersect(Rect{500.0, 0.0, 10.0, 10.0}).empty());
    CHECK(rect.contains(Point{10.0, 20.0}));
    CHECK(!rect.contains(Point{110.0, 20.0}));

    CHECK(inches(1.0) == 72.0);
    CHECK_NEAR(points_to_pixels(72.0, 300.0), 300.0, 1e-9);
}

void test_genomic_transform() {
    const GenomicTransform transform{1000, 2000, 0.0, 100.0};
    CHECK_NEAR(transform.x(static_cast<std::int64_t>(1000)), 0.0, 1e-9);
    CHECK_NEAR(transform.x(static_cast<std::int64_t>(2000)), 100.0, 1e-9);
    CHECK_NEAR(transform.x(static_cast<std::int64_t>(1500)), 50.0, 1e-9);
    CHECK_NEAR(transform.width_of(100), 10.0, 1e-9);
    CHECK(transform.position(50.0) == 1500);
    CHECK_NEAR(transform.bases_per_unit(), 10.0, 1e-9);

    // Positions outside the region keep their slope rather than clamping.
    CHECK_NEAR(transform.x(static_cast<std::int64_t>(500)), -50.0, 1e-9);
}

void test_color() {
    CHECK(color_from_hex("#ff0000") == rgb(255, 0, 0));
    CHECK(color_from_hex("0f0") == rgb(0, 255, 0));
    CHECK(color_from_hex("#0000ff80").a == 128);
    Color parsed;
    CHECK(!try_color_from_hex("nope", parsed));
    CHECK(color_to_hex(rgb(18, 52, 86)) == "#123456");

    // Opaque source replaces; transparent source leaves the backdrop alone.
    CHECK(composite(colors::white, rgb(1, 2, 3)) == rgb(1, 2, 3));
    CHECK(composite(colors::white, colors::transparent) == colors::white);
    const Color half = composite(colors::black, rgba(255, 255, 255, 128));
    CHECK(half.r > 120 && half.r < 135);

    const ColorMap map = ColorMap::named("viridis");
    CHECK(map.at(0.0) == color_from_hex("#440154"));
    CHECK(map.at(1.0) == color_from_hex("#FDE725"));
    CHECK(map.lut().size() == ColorMap::kLutSize);
    CHECK(map.reversed().at(0.0) == map.at(1.0));
    // NaN is the "missing data" path used by heatmaps.
    CHECK(map.at(std::nan("")) == map.bad());
    CHECK(ColorMap::has("fall"));
    CHECK(!ColorMap::has("no-such-map"));
}

void test_scale() {
    ValueScale linear(0.0, 100.0);
    CHECK_NEAR(linear.normalize(50.0), 0.5, 1e-9);
    CHECK_NEAR(linear.normalize(-10.0), 0.0, 1e-9);  // clipped
    CHECK_NEAR(linear.denormalize(0.25), 25.0, 1e-9);

    ValueScale unclipped(0.0, 100.0);
    unclipped.clip(false);
    CHECK(std::isnan(unclipped.normalize(200.0)));

    ValueScale logarithmic(0.0, 999.0, ScaleType::log1p);
    CHECK_NEAR(logarithmic.normalize(999.0), 1.0, 1e-9);
    CHECK_NEAR(logarithmic.normalize(9.0), 1.0 / 3.0, 1e-9);

    // Automatic limits come from a percentile, not the extreme value.
    ValueScale automatic;
    automatic.upper_percentile(0.9);
    std::vector<double> values;
    for (int i = 0; i < 100; ++i) values.push_back(i);
    values.push_back(100000.0);
    CHECK(automatic.fit(values));
    CHECK(automatic.max() < 200.0);

    const std::vector<double> ticks = nice_ticks(0.0, 10.0, 5);
    CHECK(!ticks.empty());
    CHECK(ticks.front() >= 0.0 && ticks.back() <= 10.0);

    const std::vector<long long> positions = genomic_ticks(1'000'000, 2'000'000, 5);
    CHECK(!positions.empty());
    for (long long position : positions) {
        CHECK(position >= 1'000'000 && position < 2'000'000);
    }
    CHECK(format_position(1'500'000, 500'000) == "1.5 Mb");
    CHECK(format_position(12'000, 1'000) == "12 kb");
    CHECK(format_region("chr8", 127'000'000, 129'000'000) ==
          "chr8:127,000,000-129,000,000");
}

void test_text() {
    const Font& font = fonts().get(fonts().default_font());
    CHECK(!font.postscript_name().empty());
    CHECK(font.ascent(12.0) > 0.0);
    CHECK(font.descent(12.0) > 0.0);

    const double width = font.width("Hello", 12.0);
    CHECK(width > 0.0);
    // Text width scales linearly with point size.
    CHECK_NEAR(font.width("Hello", 24.0), width * 2.0, 1e-6);
    // A space advances the pen.
    CHECK(font.width("a b", 12.0) > font.width("ab", 12.0));

    const auto glyphs = font.shape("AB", 10.0);
    CHECK(glyphs.size() == 2);
    CHECK(glyphs[0].x == 0.0);
    CHECK(glyphs[1].x > 0.0);
    CHECK(glyphs[0].codepoint == U'A');

    // Alignment resolves against the run's measured width.
    TextStyle style;
    style.size = 10.0;
    style.align = TextAlign::center;
    const Point centred = text_origin(font, "Hello", style, Point{100.0, 50.0});
    CHECK_NEAR(centred.x, 100.0 - font.width("Hello", 10.0) / 2.0, 1e-9);

    style.align = TextAlign::right;
    style.valign = VerticalAlign::top;
    const Point corner = text_origin(font, "Hello", style, Point{100.0, 50.0});
    CHECK_NEAR(corner.x, 100.0 - font.width("Hello", 10.0), 1e-9);
    CHECK_NEAR(corner.y, 50.0 + font.ascent(10.0), 1e-9);

    // 90 degrees anti-clockwise on a y-down page points up.
    const Point rotated = rotate_ccw(1.0, 0.0, 90.0);
    CHECK_NEAR(rotated.x, 0.0, 1e-9);
    CHECK_NEAR(rotated.y, -1.0, 1e-9);

    std::size_t position = 0;
    CHECK(decode_utf8("A", position) == U'A');
    position = 0;
    CHECK(decode_utf8("\xC2\xB5", position) == 0xB5);  // micro sign
    CHECK(position == 2);
}

// Inflates a PNG's IDAT and rebuilds the pixels, so the encoder is checked
// against something other than itself.
bool decode_png(const std::vector<std::uint8_t>& bytes, int& width, int& height, int& channels,
                std::vector<std::uint8_t>& pixels) {
    const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), signature, 8) != 0) return false;

    const auto read_u32 = [&bytes](std::size_t offset) {
        return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
               (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
               (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
               static_cast<std::uint32_t>(bytes[offset + 3]);
    };

    std::vector<std::uint8_t> compressed;
    std::size_t offset = 8;
    int colour_type = 0;
    bool saw_header = false;
    while (offset + 8 <= bytes.size()) {
        const std::uint32_t length = read_u32(offset);
        const std::string type(reinterpret_cast<const char*>(&bytes[offset + 4]), 4);
        const std::size_t data = offset + 8;
        if (data + length > bytes.size()) return false;
        if (type == "IHDR") {
            width = static_cast<int>(read_u32(data));
            height = static_cast<int>(read_u32(data + 4));
            colour_type = bytes[data + 9];
            saw_header = true;
        } else if (type == "IDAT") {
            compressed.insert(compressed.end(), bytes.begin() + static_cast<std::ptrdiff_t>(data),
                              bytes.begin() + static_cast<std::ptrdiff_t>(data + length));
        } else if (type == "IEND") {
            break;
        }
        offset = data + length + 4;
    }
    if (!saw_header || compressed.empty()) return false;
    channels = colour_type == 2 ? 3 : 4;

    const std::size_t stride = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
    std::vector<std::uint8_t> raw((stride + 1) * static_cast<std::size_t>(height));
    uLongf raw_size = static_cast<uLongf>(raw.size());
    if (::uncompress(raw.data(), &raw_size, compressed.data(),
                     static_cast<uLong>(compressed.size())) != Z_OK) {
        return false;
    }

    pixels.assign(stride * static_cast<std::size_t>(height), 0);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t filter = raw[static_cast<std::size_t>(y) * (stride + 1)];
        const std::uint8_t* in = &raw[static_cast<std::size_t>(y) * (stride + 1) + 1];
        std::uint8_t* out = &pixels[static_cast<std::size_t>(y) * stride];
        const std::uint8_t* previous =
            y > 0 ? &pixels[static_cast<std::size_t>(y - 1) * stride] : nullptr;
        for (std::size_t i = 0; i < stride; ++i) {
            const int a = i >= static_cast<std::size_t>(channels)
                              ? out[i - static_cast<std::size_t>(channels)]
                              : 0;
            const int b = previous != nullptr ? previous[i] : 0;
            const int c = (previous != nullptr && i >= static_cast<std::size_t>(channels))
                              ? previous[i - static_cast<std::size_t>(channels)]
                              : 0;
            int value = in[i];
            switch (filter) {
                case 0:
                    break;
                case 1:
                    value += a;
                    break;
                case 2:
                    value += b;
                    break;
                case 3:
                    value += (a + b) / 2;
                    break;
                case 4: {
                    const int p = a + b - c;
                    const int pa = std::abs(p - a);
                    const int pb = std::abs(p - b);
                    const int pc = std::abs(p - c);
                    value += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                    break;
                }
                default:
                    return false;
            }
            out[i] = static_cast<std::uint8_t>(value & 0xFF);
        }
    }
    return true;
}

void test_png_roundtrip() {
    Image image(7, 5, colors::white);
    image.set(0, 0, rgb(255, 0, 0));
    image.set(6, 4, rgb(0, 0, 255));
    image.set(3, 2, rgb(10, 200, 30));

    const std::vector<std::uint8_t> encoded = png::encode(image, png::Options{300, 6, true});
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<std::uint8_t> pixels;
    CHECK(decode_png(encoded, width, height, channels, pixels));
    CHECK(width == 7 && height == 5);
    CHECK(channels == 3);  // fully opaque, so the alpha channel is dropped
    CHECK(pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 0);
    const std::size_t last = (4 * 7 + 6) * 3;
    CHECK(pixels[last] == 0 && pixels[last + 1] == 0 && pixels[last + 2] == 255);
    const std::size_t middle = (2 * 7 + 3) * 3;
    CHECK(pixels[middle] == 10 && pixels[middle + 1] == 200 && pixels[middle + 2] == 30);

    // Translucency forces RGBA.
    Image alpha(4, 4, rgba(0, 0, 0, 128));
    const std::vector<std::uint8_t> rgba_bytes = png::encode(alpha);
    CHECK(decode_png(rgba_bytes, width, height, channels, pixels));
    CHECK(channels == 4);
    CHECK(pixels[3] == 128);
}

void test_raster_canvas() {
    RasterCanvas canvas(Size{100.0, 50.0}, 72.0, colors::white);
    CHECK(canvas.image().width() == 100);
    CHECK(canvas.image().height() == 50);
    CHECK(canvas.device_scale() == 1.0);

    canvas.fill_rect(Rect{10.0, 10.0, 20.0, 20.0}, rgb(255, 0, 0));
    CHECK(canvas.image().get(15, 15) == rgb(255, 0, 0));
    CHECK(canvas.image().get(5, 5) == colors::white);
    // Edges land exactly on pixel boundaries, so the pixel outside is clean.
    CHECK(canvas.image().get(30, 15) == colors::white);
    CHECK(canvas.image().get(29, 15) == rgb(255, 0, 0));

    // Clipping keeps a track inside its own rectangle.
    canvas.push_clip(Rect{0.0, 0.0, 40.0, 50.0});
    canvas.fill_rect(Rect{35.0, 35.0, 20.0, 5.0}, rgb(0, 0, 255));
    canvas.pop_clip();
    CHECK(canvas.image().get(37, 37) == rgb(0, 0, 255));
    CHECK(canvas.image().get(42, 37) == colors::white);

    // Half-covered pixels blend rather than snapping.
    RasterCanvas soft(Size{10.0, 10.0}, 72.0, colors::white);
    soft.fill_rect(Rect{0.0, 0.0, 1.5, 10.0}, colors::black);
    const Color partial = soft.image().get(1, 5);
    CHECK(partial.r > 100 && partial.r < 160);

    // A device scale of 2 doubles the pixel dimensions but not the geometry.
    RasterCanvas scaled(Size{100.0, 50.0}, 144.0, colors::white);
    CHECK(scaled.image().width() == 200);
    scaled.fill_rect(Rect{10.0, 10.0, 20.0, 20.0}, rgb(255, 0, 0));
    CHECK(scaled.image().get(30, 30) == rgb(255, 0, 0));
    CHECK(scaled.image().get(10, 10) == colors::white);
}

void test_draw_image() {
    Image source(2, 2);
    source.set(0, 0, rgb(255, 0, 0));
    source.set(1, 0, rgb(0, 255, 0));
    source.set(0, 1, rgb(0, 0, 255));
    source.set(1, 1, rgb(255, 255, 0));

    RasterCanvas canvas(Size{40.0, 40.0}, 72.0, colors::white);
    canvas.draw_image(Rect{0.0, 0.0, 40.0, 40.0}, source.view(ImageScaling::nearest));
    // Row 0 of the image must land at the top of the rectangle.
    CHECK(canvas.image().get(5, 5) == rgb(255, 0, 0));
    CHECK(canvas.image().get(35, 5) == rgb(0, 255, 0));
    CHECK(canvas.image().get(5, 35) == rgb(0, 0, 255));
    CHECK(canvas.image().get(35, 35) == rgb(255, 255, 0));
}

void test_layout() {
    auto signal = MemorySignalSource::make(
        "chr1", {{1000, 2000, 5.0}, {2000, 3000, 10.0}, {3000, 4000, 2.0}});

    Figure figure;
    figure.set_width(400.0).set_margins(Insets{10.0});
    Panel& panel = figure.add_panel();
    panel.set_region("chr1", 1000, 4000).set_label_width(50.0);
    panel.add_track(SignalTrack{signal}.name("a").height(30.0));
    panel.add_track(SignalTrack{signal}.name("b").height(40.0));

    const Size size = figure.computed_size();
    CHECK(size.width == 400.0);
    // Automatic height covers both tracks plus margins and spacing.
    CHECK(size.height > 70.0 && size.height < 130.0);

    // An explicit height is honoured exactly.
    figure.set_size(400.0, 300.0);
    CHECK(figure.computed_size().height == 300.0);

    // Flexible tracks absorb the surplus.
    Figure flexible;
    flexible.set_size(400.0, 300.0).set_margins(Insets{0.0});
    Panel& stretch = flexible.add_panel();
    stretch.set_region("chr1", 1000, 4000).set_track_spacing(0.0);
    SignalTrack& fixed = stretch.add_track(SignalTrack{signal}.height(50.0).margins(Insets{0.0}));
    SignalTrack& growing =
        stretch.add_track(SignalTrack{signal}.height(50.0).flex(1.0).margins(Insets{0.0}));
    CHECK(flexible.computed_size().height == 300.0);
    CHECK(fixed.preferred_height() == 50.0);
    CHECK_NEAR(growing.preferred_height(), 250.0, 0.5);
}

void test_signal_source() {
    auto source = MemorySignalSource::make(
        "chr1", {{0, 100, 4.0}, {100, 200, 8.0}, {300, 400, 1.0}});
    const SignalData data = source->query(GenomicRegion{"chr1", 0, 400}, 4);
    CHECK(data.size() == 4);
    CHECK_NEAR(data.bin_size, 100.0, 1e-9);
    CHECK_NEAR(data.values[0], 4.0, 1e-9);
    CHECK_NEAR(data.values[1], 8.0, 1e-9);
    CHECK(std::isnan(data.values[2]));  // no record covers 200-300
    CHECK_NEAR(data.values[3], 1.0, 1e-9);

    // Coarser bins average by overlap.
    const SignalData coarse = source->query(GenomicRegion{"chr1", 0, 400}, 2);
    CHECK(coarse.size() == 2);
    CHECK_NEAR(coarse.values[0], 6.0, 1e-9);

    double low = 0.0;
    double high = 0.0;
    CHECK(data.range(low, high));
    CHECK_NEAR(low, 1.0, 1e-9);
    CHECK_NEAR(high, 8.0, 1e-9);
}

void test_matrix_source() {
    std::vector<float> values(16);
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = static_cast<float>(i);
    auto source = MemoryMatrixSource::make(
        MatrixRegion::square(GenomicRegion{"chr1", 0, 400}), 100, 4, 4, values);

    const MatrixData full =
        source->query(MatrixRegion::square(GenomicRegion{"chr1", 0, 400}), 4, 4);
    CHECK(full.width == 4 && full.height == 4);
    CHECK(full.at(0, 0) == 0.0F);
    CHECK(full.at(3, 3) == 15.0F);

    // A sub-region selects the right block.
    const MatrixData part =
        source->query(MatrixRegion::square(GenomicRegion{"chr1", 200, 400}), 2, 2);
    CHECK(part.width == 2 && part.height == 2);
    CHECK(part.at(0, 0) == 10.0F);

    // Asking for fewer bins aggregates instead of dropping data.
    const MatrixData coarse =
        source->query(MatrixRegion::square(GenomicRegion{"chr1", 0, 400}), 2, 2);
    CHECK(coarse.width == 2 && coarse.height == 2);
    CHECK_NEAR(coarse.at(0, 0), 2.5, 1e-5);  // mean of 0, 1, 4, 5
}

void test_gene_packing() {
    std::vector<Feature> features;
    for (int i = 0; i < 3; ++i) {
        Feature feature;
        feature.chrom = "chr1";
        feature.start = 1000;
        feature.end = 2000;
        feature.name = "gene" + std::to_string(i);
        features.push_back(feature);
    }

    GeneTrack track{features};
    track.show_labels(false);
    ViewContext context;
    context.x_region = GenomicRegion{"chr1", 0, 3000};
    context.content = Rect{0.0, 0.0, 300.0, 40.0};
    const Theme theme = Theme::light();
    context.theme = &theme;
    track.prepare(context);
    // Three fully overlapping genes need three rows.
    CHECK(track.rows() == 3);
    CHECK(track.preferred_height() > 0.0);

    GeneTrack collapsed{features};
    collapsed.collapsed(true).show_labels(false);
    collapsed.prepare(context);
    CHECK(collapsed.rows() == 1);
}

void test_full_render() {
    auto signal = MemorySignalSource::make("chr1", {{0, 500, 3.0}, {500, 1000, 9.0}});
    std::vector<float> values(64);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = static_cast<float>((i % 8) + (i / 8));
    }
    auto matrix = MemoryMatrixSource::make(
        MatrixRegion::square(GenomicRegion{"chr1", 0, 1000}), 125, 8, 8, values);

    Figure figure;
    figure.set_width(300.0).set_title("test");
    Panel& panel = figure.add_panel();
    panel.set_region("chr1", 0, 1000);
    panel.add_track(AxisTrack{});
    panel.add_track(SignalTrack{signal}.name("signal").height(30.0));
    HeatmapTrack& heatmap =
        panel.add_track(HeatmapTrack{matrix}.name("hic").height(80.0).square_aspect(false));
    panel.add_track(ColorBarTrack{heatmap});

    const Image image = figure.render_image(96.0);
    CHECK(image.width() == 400);  // 300 pt at 96 dpi
    CHECK(!image.empty());
    // Something was drawn: not every pixel is the background.
    bool has_ink = false;
    for (int y = 0; y < image.height() && !has_ink; ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.get(x, y) != colors::white) {
                has_ink = true;
                break;
            }
        }
    }
    CHECK(has_ink);

    const auto png_path = scratch("gre_test_figure.png");
    const auto pdf_path = scratch("gre_test_figure.pdf");
    const auto svg_path = scratch("gre_test_figure.svg");
    figure.save_png(png_path.string(), 96.0);
    figure.save_pdf(pdf_path.string());
    figure.save_svg(svg_path.string());
    CHECK(std::filesystem::file_size(png_path) > 200);
    CHECK(std::filesystem::file_size(pdf_path) > 400);
    CHECK(std::filesystem::file_size(svg_path) > 200);

    // The PDF is structurally sound and carries an embedded font.
    std::vector<char> pdf(std::filesystem::file_size(pdf_path));
    std::FILE* file = std::fopen(pdf_path.string().c_str(), "rb");
    CHECK(file != nullptr);
    if (file != nullptr) {
        CHECK(std::fread(pdf.data(), 1, pdf.size(), file) == pdf.size());
        std::fclose(file);
    }
    const std::string text(pdf.begin(), pdf.end());
    CHECK(text.rfind("%PDF-1.7", 0) == 0);
    CHECK(text.find("%%EOF") != std::string::npos);
    CHECK(text.find("/Type /Catalog") != std::string::npos);
    CHECK(text.find("startxref") != std::string::npos);
    CHECK(text.find("/FontFile2") != std::string::npos);
    CHECK(text.find("/Identity-H") != std::string::npos);
    CHECK(text.find("/ToUnicode") != std::string::npos);
    // The heatmap went out as an image, not as thousands of rectangles.
    CHECK(text.find("/Subtype /Image") != std::string::npos);

    std::filesystem::remove(png_path);
    std::filesystem::remove(pdf_path);
    std::filesystem::remove(svg_path);
}

void test_font_subset() {
    // A subset must still be a parseable TrueType font containing the glyphs
    // that were asked for.
    const Font& font = fonts().get(fonts().default_font());
    const auto glyphs = font.shape("Hi-C", 10.0);
    std::vector<std::uint16_t> gids;
    for (const PositionedGlyph& glyph : glyphs) gids.push_back(glyph.gid);

    const std::vector<std::uint8_t> subset = font.face().subset(gids);
    CHECK(subset.size() > 200);
    CHECK(subset.size() < font.face().data().size());

    auto reparsed = ttf::Face::from_memory(subset);
    CHECK(reparsed->num_glyphs() == font.face().num_glyphs());
    for (std::uint16_t gid : gids) {
        CHECK(reparsed->advance_units(gid) == font.face().advance_units(gid));
    }
}

void test_dashes() {
    const Point points[2] = {{0.0, 0.0}, {10.0, 0.0}};
    const auto runs = apply_dash(std::span<const Point>(points, 2), {2.0, 2.0}, 0.0);
    CHECK(runs.size() == 3);  // on at 0-2, 4-6, 8-10
    CHECK_NEAR(runs[0][0].x, 0.0, 1e-9);
    CHECK_NEAR(runs[0][1].x, 2.0, 1e-9);
    CHECK_NEAR(runs[2][0].x, 8.0, 1e-9);
}

void test_errors() {
    Figure empty;
    bool threw = false;
    try {
        empty.computed_size();
    } catch (const Error& error) {
        threw = error.code() == ErrorCode::invalid_argument;
    }
    CHECK(threw);

    Panel panel;
    threw = false;
    try {
        panel.set_region("chr1", 100, 100);
    } catch (const Error&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        ColorMap::named("definitely-not-a-colour-map");
    } catch (const Error& error) {
        threw = error.code() == ErrorCode::not_found;
    }
    CHECK(threw);
}

}  // namespace

int main() {
    test_geometry();
    test_genomic_transform();
    test_color();
    test_scale();
    test_text();
    test_png_roundtrip();
    test_raster_canvas();
    test_draw_image();
    test_layout();
    test_signal_source();
    test_matrix_source();
    test_gene_packing();
    test_full_render();
    test_font_subset();
    test_dashes();
    test_errors();

    std::cout << checks - failures << "/" << checks << " checks passed\n";
    if (failures > 0) std::cerr << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
