#include "internal.hpp"

namespace shutter::detail {
namespace {
constexpr auto table = [] {
    std::array<std::uint32_t, 256> values{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        auto crc = i;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0x82f63b78U : 0U);
        values[i] = crc;
    }
    return values;
}();
} // namespace
std::uint32_t crc32c(std::span<const std::byte> data) noexcept {
    std::uint32_t crc = 0xffffffffU;
    for (auto byte : data)
        crc = table[(crc ^ std::to_integer<std::uint8_t>(byte)) & 0xffU] ^ (crc >> 8);
    return ~crc;
}
} // namespace shutter::detail
