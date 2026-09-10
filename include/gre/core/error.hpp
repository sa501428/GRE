#pragma once

#include <stdexcept>
#include <string>

namespace gre {

enum class ErrorCode {
    invalid_argument,
    io,
    not_found,
    unsupported,
    internal,
};

class Error : public std::runtime_error {
public:
    Error(ErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}

    [[nodiscard]] ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};

}  // namespace gre
