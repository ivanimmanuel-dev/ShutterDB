#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace shutter {
enum class ErrorCode {
    invalid_argument,
    io_error,
    corruption,
    unsupported_format,
    lock_conflict,
    permission_denied,
    not_found,
    resource_limit,
    needs_reopen
};
const char *error_name(ErrorCode code) noexcept;
class Error : public std::runtime_error {
  public:
    Error(ErrorCode code, std::string message, std::uint64_t offset = 0,
          std::optional<std::uint32_t> expected = {}, std::optional<std::uint32_t> actual = {})
        : std::runtime_error(std::move(message)), code_(code), offset_(offset), expected_(expected),
          actual_(actual) {}
    ErrorCode code() const noexcept { return code_; }
    std::uint64_t offset() const noexcept { return offset_; }
    std::optional<std::uint32_t> expected_crc() const noexcept { return expected_; }
    std::optional<std::uint32_t> actual_crc() const noexcept { return actual_; }

  private:
    ErrorCode code_;
    std::uint64_t offset_;
    std::optional<std::uint32_t> expected_, actual_;
};
} // namespace shutter
