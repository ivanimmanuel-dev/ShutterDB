#pragma once
#include <cstddef>
#include <cstdint>

namespace shutter {
inline constexpr std::uint32_t max_key_size = 64 * 1024;
inline constexpr std::uint32_t max_value_size = 16 * 1024 * 1024;
struct Options {
    bool sync_writes = true;
    bool create_if_missing = true;
    bool recover_truncated_tail = true;
    std::size_t max_live_keys = 1'000'000;
    // Index budget charges key length plus 128 bytes per entry.
    std::uint64_t max_index_bytes = 256ULL * 1024 * 1024;
};
} // namespace shutter
