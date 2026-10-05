#include "internal.hpp"
#include <bit>
#include <cstring>
#if defined(__x86_64__) || defined(_M_X64)
#include <nmmintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#endif

namespace shutter::detail {
namespace {
constexpr auto tables = [] {
    std::array<std::array<std::uint32_t, 256>, 8> values{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        auto crc = i;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0x82f63b78U : 0U);
        values[0][i] = crc;
    }
    for (std::size_t slice = 1; slice < values.size(); ++slice)
        for (std::size_t i = 0; i < 256; ++i) {
            const auto crc = values[slice - 1][i];
            values[slice][i] = (crc >> 8) ^ values[0][crc & 255];
        }
    return values;
}();
#if defined(__x86_64__) || defined(_M_X64)
bool has_crc_instruction() noexcept {
#ifdef _MSC_VER
    int registers[4]{};
    __cpuid(registers, 1);
    return (registers[2] & (1 << 20)) != 0;
#else
    return __builtin_cpu_supports("sse4.2") != 0;
#endif
}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("sse4.2")))
#endif
std::uint32_t crc32c_x86(std::span<const std::byte> data) noexcept {
    std::uint64_t crc = 0xffffffffU;
    while (data.size() >= 8) {
        std::uint64_t word;
        std::memcpy(&word, data.data(), sizeof(word));
        crc = _mm_crc32_u64(crc, word);
        data = data.subspan(8);
    }
    for (const auto byte : data)
        crc = _mm_crc32_u8(static_cast<std::uint32_t>(crc), std::to_integer<unsigned char>(byte));
    return ~static_cast<std::uint32_t>(crc);
}
#endif
} // namespace
std::uint32_t crc32c_portable(std::span<const std::byte> data) noexcept {
    std::uint32_t crc = 0xffffffffU;
    const auto *bytes = reinterpret_cast<const unsigned char *>(data.data());
    auto remaining = data.size();
    while (remaining >= 8) {
        std::uint32_t word;
        if constexpr (std::endian::native == std::endian::little)
            std::memcpy(&word, bytes, sizeof(word));
        else
            word = std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
                   (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
        word ^= crc;
        crc = tables[7][word & 255] ^ tables[6][(word >> 8) & 255] ^ tables[5][(word >> 16) & 255] ^
              tables[4][word >> 24] ^ tables[3][bytes[4]] ^ tables[2][bytes[5]] ^ tables[1][bytes[6]] ^
              tables[0][bytes[7]];
        bytes += 8;
        remaining -= 8;
    }
    for (std::size_t i = 0; i < remaining; ++i)
        crc = (crc >> 8) ^ tables[0][(crc ^ bytes[i]) & 255];
    return ~crc;
}
std::uint32_t crc32c(std::span<const std::byte> data) noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    static const auto implementation = has_crc_instruction() ? crc32c_x86 : crc32c_portable;
    return implementation(data);
#else
    return crc32c_portable(data);
#endif
}
} // namespace shutter::detail
