#pragma once

// Minimal PDF object serializer: indirect objects, streams, an xref table and
// a trailer.  Enough to write a single-page figure with embedded fonts and
// images, and nothing more.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gre::pdf {

// PDF reals must not use exponent notation, and trailing zeros only bloat the
// file.
[[nodiscard]] std::string format_number(double value);

// Escapes a string for use inside PDF literal-string parentheses.
[[nodiscard]] std::string escape_literal(std::string_view text);

// Escapes a name for use after '/'.
[[nodiscard]] std::string escape_name(std::string_view text);

class Writer {
public:
    Writer();

    // Reserves an object number so objects can refer to each other before
    // being written.
    [[nodiscard]] int allocate();
    void define(int id, std::string body);
    void define_stream(int id, const std::string& dict_entries, std::vector<std::uint8_t> data,
                       bool compress);

    int add(std::string body);
    int add_stream(const std::string& dict_entries, std::vector<std::uint8_t> data, bool compress);

    void set_root(int id) { root_ = id; }
    void set_info(int id) { info_ = id; }

    [[nodiscard]] std::vector<std::uint8_t> serialize() const;

private:
    struct Object {
        std::string body;
        std::vector<std::uint8_t> stream;
        bool has_stream{false};
        bool defined{false};
    };

    std::vector<Object> objects_;
    int root_{0};
    int info_{0};
};

}  // namespace gre::pdf
