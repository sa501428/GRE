#include "gre/export/png.hpp"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#include "gre/core/error.hpp"

namespace gre::png {
namespace {

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void put_chunk(std::vector<std::uint8_t>& out, const char (&type)[5],
               const std::vector<std::uint8_t>& payload) {
    put_u32(out, static_cast<std::uint32_t>(payload.size()));
    const std::size_t crc_start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    const auto crc = static_cast<std::uint32_t>(
        ::crc32(::crc32(0L, Z_NULL, 0), out.data() + crc_start,
                static_cast<uInt>(out.size() - crc_start)));
    put_u32(out, crc);
}

int paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

// Sum of the filtered bytes read as signed values; the PNG spec's heuristic
// for picking a filter per scanline.
long filter_cost(const std::vector<std::uint8_t>& row) {
    long cost = 0;
    for (std::uint8_t byte : row) {
        cost += byte < 128 ? byte : 256 - byte;
    }
    return cost;
}

}  // namespace

std::vector<std::uint8_t> encode(const Image& image, const Options& options) {
    if (image.empty()) {
        throw Error(ErrorCode::invalid_argument, "cannot encode an empty image");
    }
    const bool rgb = options.allow_rgb && image.fully_opaque();
    const int channels = rgb ? 3 : 4;
    const auto width = static_cast<std::size_t>(image.width());
    const auto height = static_cast<std::size_t>(image.height());
    const std::size_t row_bytes = width * static_cast<std::size_t>(channels);

    // Filtered scanlines: one filter byte then the filtered row.
    std::vector<std::uint8_t> raw;
    raw.reserve((row_bytes + 1) * height);

    std::vector<std::uint8_t> current(row_bytes);
    std::vector<std::uint8_t> previous(row_bytes, 0);
    std::vector<std::uint8_t> candidate(row_bytes);
    std::vector<std::uint8_t> best(row_bytes);

    for (std::size_t y = 0; y < height; ++y) {
        const std::uint8_t* source = image.row(static_cast<int>(y));
        if (rgb) {
            for (std::size_t x = 0; x < width; ++x) {
                current[x * 3 + 0] = source[x * 4 + 0];
                current[x * 3 + 1] = source[x * 4 + 1];
                current[x * 3 + 2] = source[x * 4 + 2];
            }
        } else {
            std::memcpy(current.data(), source, row_bytes);
        }

        int best_filter = 0;
        long best_cost = -1;
        for (int filter = 0; filter <= 4; ++filter) {
            for (std::size_t i = 0; i < row_bytes; ++i) {
                const int a = i >= static_cast<std::size_t>(channels)
                                  ? current[i - static_cast<std::size_t>(channels)]
                                  : 0;
                const int b = previous[i];
                const int c = i >= static_cast<std::size_t>(channels)
                                  ? previous[i - static_cast<std::size_t>(channels)]
                                  : 0;
                int value = current[i];
                switch (filter) {
                    case 0:
                        break;
                    case 1:
                        value -= a;
                        break;
                    case 2:
                        value -= b;
                        break;
                    case 3:
                        value -= (a + b) / 2;
                        break;
                    default:
                        value -= paeth(a, b, c);
                        break;
                }
                candidate[i] = static_cast<std::uint8_t>(value & 0xFF);
            }
            const long cost = filter_cost(candidate);
            if (best_cost < 0 || cost < best_cost) {
                best_cost = cost;
                best_filter = filter;
                best.swap(candidate);
            }
        }

        raw.push_back(static_cast<std::uint8_t>(best_filter));
        raw.insert(raw.end(), best.begin(), best.end());
        previous.swap(current);
    }

    uLongf compressed_size = ::compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> compressed(compressed_size);
    const int level = std::clamp(options.compression, 0, 9);
    const int status = ::compress2(compressed.data(), &compressed_size, raw.data(),
                                   static_cast<uLong>(raw.size()), level);
    if (status != Z_OK) throw Error(ErrorCode::internal, "zlib failed to compress PNG data");
    compressed.resize(compressed_size);

    std::vector<std::uint8_t> out;
    out.reserve(compressed.size() + 128);
    const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    out.insert(out.end(), signature, signature + 8);

    std::vector<std::uint8_t> header;
    put_u32(header, static_cast<std::uint32_t>(width));
    put_u32(header, static_cast<std::uint32_t>(height));
    header.push_back(8);                                            // bit depth
    header.push_back(static_cast<std::uint8_t>(rgb ? 2 : 6));       // colour type
    header.push_back(0);                                            // deflate
    header.push_back(0);                                            // adaptive filtering
    header.push_back(0);                                            // no interlace
    put_chunk(out, "IHDR", header);

    // Mark the data as sRGB so viewers do not apply a display transform of
    // their own; figures are authored in sRGB.
    put_chunk(out, "sRGB", std::vector<std::uint8_t>{0});
    std::vector<std::uint8_t> gamma;
    put_u32(gamma, 45455);  // 1/2.2, the companion value to sRGB
    put_chunk(out, "gAMA", gamma);

    if (options.dpi > 0) {
        const auto pixels_per_metre =
            static_cast<std::uint32_t>(std::lround(options.dpi / 0.0254));
        std::vector<std::uint8_t> phys;
        put_u32(phys, pixels_per_metre);
        put_u32(phys, pixels_per_metre);
        phys.push_back(1);  // unit: metre
        put_chunk(out, "pHYs", phys);
    }

    put_chunk(out, "IDAT", compressed);
    put_chunk(out, "IEND", std::vector<std::uint8_t>{});
    return out;
}

void write(const std::string& path, const Image& image, const Options& options) {
    const std::vector<std::uint8_t> bytes = encode(image, options);
    std::ofstream stream(path, std::ios::binary);
    if (!stream) throw Error(ErrorCode::io, "cannot open for writing: " + path);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw Error(ErrorCode::io, "failed while writing: " + path);
}

}  // namespace gre::png
