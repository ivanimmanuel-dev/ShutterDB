#include "internal.hpp"
#include <algorithm>
#include <cstring>

namespace shutter::detail {
std::uint64_t read_le(std::span<const std::byte> b, std::size_t o, std::size_t n) {
    if (n > 8 || o > b.size() || n > b.size() - o)
        throw Error(ErrorCode::corruption, "integer outside header", o);
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < n; ++i)
        value |= std::uint64_t(std::to_integer<unsigned char>(b[o + i])) << (i * 8);
    return value;
}
void write_le(std::span<std::byte> b, std::size_t o, std::uint64_t v, std::size_t n) {
    if (n > 8 || o > b.size() || n > b.size() - o)
        throw Error(ErrorCode::invalid_argument, "integer outside buffer");
    for (std::size_t i = 0; i < n; ++i)
        b[o + i] = std::byte((v >> (i * 8)) & 0xffU);
}
Bytes file_header(std::uint64_t sequence) {
    Bytes b(file_header_size);
    std::memcpy(b.data(), "SHUTDB\r\n", 8);
    write_le(b, 8, 1, 2);
    write_le(b, 10, file_header_size, 2);
    write_le(b, 16, sequence, 8);
    write_le(b, 28, crc32c(std::span(b).first(28)), 4);
    return b;
}
std::uint64_t decode_file_header(std::span<const std::byte> b) {
    if (b.size() != file_header_size)
        throw Error(ErrorCode::corruption, "truncated file header");
    if (std::memcmp(b.data(), "SHUTDB\r\n", 8) != 0)
        throw Error(ErrorCode::corruption, "bad database magic");
    if (read_le(b, 8, 2) != 1)
        throw Error(ErrorCode::unsupported_format, "unsupported database version", 8);
    auto expected = static_cast<std::uint32_t>(read_le(b, 28, 4));
    auto actual = crc32c(b.first(28));
    if (expected != actual)
        throw Error(ErrorCode::corruption, "file header CRC32C mismatch", 0, expected, actual);
    if (read_le(b, 10, 2) != file_header_size || read_le(b, 12, 4) || read_le(b, 24, 4))
        throw Error(ErrorCode::corruption, "invalid file header fields");
    return read_le(b, 16, 8);
}
RecordHeader decode_header(std::span<const std::byte> b, std::uint64_t offset) {
    if (b.size() != record_header_size)
        throw Error(ErrorCode::corruption, "truncated record header", offset);
    if (std::memcmp(b.data(), "SHDR", 4) != 0)
        throw Error(ErrorCode::corruption, "bad record magic", offset);
    if (read_le(b, 4, 2) != 1)
        throw Error(ErrorCode::unsupported_format, "unsupported record version", offset);
    auto expected = static_cast<std::uint32_t>(read_le(b, 36, 4));
    auto actual = crc32c(b.first(36));
    if (expected != actual)
        throw Error(ErrorCode::corruption, "record header CRC32C mismatch", offset, expected, actual);
    auto type = read_le(b, 6, 1);
    auto key = read_le(b, 16, 4), value = read_le(b, 20, 4), total = read_le(b, 28, 4);
    if ((type != 1 && type != 2) || read_le(b, 7, 1) || read_le(b, 32, 4))
        throw Error(ErrorCode::corruption, "invalid record type, flags or reserved bytes", offset);
    if (!key || key > max_key_size || value > max_value_size || total != record_header_size + key + value ||
        (type == 2 && value != 0) || read_le(b, 8, 8) == 0)
        throw Error(ErrorCode::corruption, "invalid record lengths or sequence", offset);
    return {static_cast<Kind>(type),
            read_le(b, 8, 8),
            static_cast<std::uint32_t>(key),
            static_cast<std::uint32_t>(value),
            static_cast<std::uint32_t>(read_le(b, 24, 4)),
            static_cast<std::uint32_t>(total)};
}
bool plausible_partial_header(std::span<const std::byte> b) {
    const auto prefix = encode(Kind::put, 1, "x", {});
    for (std::size_t i = 0; i < std::min<std::size_t>(b.size(), 6); ++i)
        if (b[i] != prefix[i])
            return false;
    if (b.size() > 6 && b[6] != std::byte{1} && b[6] != std::byte{2})
        return false;
    if (b.size() > 7 && b[7] != std::byte{0})
        return false;
    if (b.size() >= 16 && read_le(b, 8, 8) == 0)
        return false;
    if (b.size() >= 20 && (!read_le(b, 16, 4) || read_le(b, 16, 4) > max_key_size))
        return false;
    if (b.size() >= 24 && (read_le(b, 20, 4) > max_value_size || (b[6] == std::byte{2} && read_le(b, 20, 4))))
        return false;
    if (b.size() >= 32 && read_le(b, 28, 4) != record_header_size + read_le(b, 16, 4) + read_le(b, 20, 4))
        return false;
    for (std::size_t i = 32; i < std::min<std::size_t>(b.size(), 36); ++i)
        if (b[i] != std::byte{0})
            return false;
    return true;
}
void validate_key(std::string_view key) {
    if (key.empty() || key.size() > max_key_size)
        throw Error(ErrorCode::invalid_argument, "key must contain 1..65536 bytes");
}
Bytes encode(Kind kind, std::uint64_t sequence, std::string_view key, std::span<const std::byte> value) {
    validate_key(key);
    if (value.size() > max_value_size)
        throw Error(ErrorCode::invalid_argument, "value exceeds 16 MiB");
    Bytes b(record_header_size + key.size() + value.size());
    std::memcpy(b.data(), "SHDR", 4);
    write_le(b, 4, 1, 2);
    write_le(b, 6, static_cast<unsigned>(kind), 1);
    write_le(b, 8, sequence, 8);
    write_le(b, 16, key.size(), 4);
    write_le(b, 20, value.size(), 4);
    std::memcpy(b.data() + record_header_size, key.data(), key.size());
    std::copy(value.begin(), value.end(),
              b.begin() + static_cast<std::ptrdiff_t>(record_header_size + key.size()));
    write_le(b, 24, crc32c(std::span(b).subspan(record_header_size)), 4);
    write_le(b, 28, b.size(), 4);
    write_le(b, 36, crc32c(std::span(b).first(36)), 4);
    return b;
}
} // namespace shutter::detail
