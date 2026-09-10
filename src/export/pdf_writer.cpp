#include "export/pdf_writer.hpp"

#include <zlib.h>

#include <cmath>
#include <cstdio>

#include "gre/core/error.hpp"

namespace gre::pdf {
namespace {

std::vector<std::uint8_t> deflate_bytes(const std::vector<std::uint8_t>& data) {
    uLongf size = ::compressBound(static_cast<uLong>(data.size()));
    std::vector<std::uint8_t> out(size);
    const int status =
        ::compress2(out.data(), &size, data.data(), static_cast<uLong>(data.size()), 6);
    if (status != Z_OK) throw Error(ErrorCode::internal, "zlib failed to compress PDF stream");
    out.resize(size);
    return out;
}

void append(std::vector<std::uint8_t>& out, std::string_view text) {
    out.insert(out.end(), text.begin(), text.end());
}

}  // namespace

std::string format_number(double value) {
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

std::string escape_literal(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        if (c == '(' || c == ')' || c == '\\') out.push_back('\\');
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        if (c == '\r') {
            out += "\\r";
            continue;
        }
        out.push_back(c);
    }
    return out;
}

std::string escape_name(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte <= 32 || byte >= 127 || c == '/' || c == '#' || c == '(' || c == ')' ||
            c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '%') {
            char buffer[4];
            std::snprintf(buffer, sizeof buffer, "#%02X", byte);
            out += buffer;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

Writer::Writer() = default;

int Writer::allocate() {
    objects_.emplace_back();
    return static_cast<int>(objects_.size());  // object numbers are one-based
}

void Writer::define(int id, std::string body) {
    if (id < 1 || static_cast<std::size_t>(id) > objects_.size()) {
        throw Error(ErrorCode::internal, "PDF object number out of range");
    }
    Object& object = objects_[static_cast<std::size_t>(id) - 1];
    object.body = std::move(body);
    object.has_stream = false;
    object.defined = true;
}

void Writer::define_stream(int id, const std::string& dict_entries,
                           std::vector<std::uint8_t> data, bool compress) {
    if (id < 1 || static_cast<std::size_t>(id) > objects_.size()) {
        throw Error(ErrorCode::internal, "PDF object number out of range");
    }
    std::string entries = dict_entries;
    if (compress) {
        data = deflate_bytes(data);
        entries += " /Filter /FlateDecode";
    }
    Object& object = objects_[static_cast<std::size_t>(id) - 1];
    object.body = "<< " + entries + " /Length " + std::to_string(data.size()) + " >>";
    object.stream = std::move(data);
    object.has_stream = true;
    object.defined = true;
}

int Writer::add(std::string body) {
    const int id = allocate();
    define(id, std::move(body));
    return id;
}

int Writer::add_stream(const std::string& dict_entries, std::vector<std::uint8_t> data,
                       bool compress) {
    const int id = allocate();
    define_stream(id, dict_entries, std::move(data), compress);
    return id;
}

std::vector<std::uint8_t> Writer::serialize() const {
    if (root_ < 1) throw Error(ErrorCode::internal, "PDF document has no root object");

    std::vector<std::uint8_t> out;
    append(out, "%PDF-1.7\n");
    // A comment with high bytes marks the file as binary for transfer tools.
    const std::uint8_t binary_marker[] = {'%', 0xE2, 0xE3, 0xCF, 0xD3, '\n'};
    out.insert(out.end(), binary_marker, binary_marker + sizeof binary_marker);

    std::vector<std::size_t> offsets(objects_.size(), 0);
    for (std::size_t i = 0; i < objects_.size(); ++i) {
        const Object& object = objects_[i];
        if (!object.defined) {
            throw Error(ErrorCode::internal,
                        "PDF object " + std::to_string(i + 1) + " was allocated but never defined");
        }
        offsets[i] = out.size();
        append(out, std::to_string(i + 1) + " 0 obj\n");
        append(out, object.body);
        if (object.has_stream) {
            append(out, "\nstream\n");
            out.insert(out.end(), object.stream.begin(), object.stream.end());
            append(out, "\nendstream");
        }
        append(out, "\nendobj\n");
    }

    const std::size_t xref_offset = out.size();
    append(out, "xref\n0 " + std::to_string(objects_.size() + 1) + "\n");
    append(out, "0000000000 65535 f \n");
    for (std::size_t offset : offsets) {
        char entry[24];
        std::snprintf(entry, sizeof entry, "%010zu 00000 n \n", offset);
        append(out, entry);
    }

    append(out, "trailer\n<< /Size " + std::to_string(objects_.size() + 1) + " /Root " +
                    std::to_string(root_) + " 0 R");
    if (info_ >= 1) append(out, " /Info " + std::to_string(info_) + " 0 R");
    append(out, " >>\nstartxref\n" + std::to_string(xref_offset) + "\n%%EOF\n");
    return out;
}

}  // namespace gre::pdf
