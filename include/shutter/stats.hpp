#pragma once
#include <cstdint>
#include <optional>
#include <shutter/result.hpp>
#include <string>

namespace shutter {
struct Stats {
    std::uint64_t keys = 0, records = 0, put_records = 0, tombstones = 0;
    std::uint64_t database_bytes = 0, live_bytes = 0, reclaimable_bytes = 0;
    std::uint64_t last_sequence = 0, recovered_tail_bytes = 0;
    std::uint16_t format_version = 1;
    bool sync_writes = true;
};
struct Diagnostic {
    ErrorCode code = ErrorCode::corruption;
    std::uint64_t offset = 0;
    std::string message;
    std::optional<std::uint32_t> expected_crc, actual_crc;
};
struct VerifyReport {
    bool header_valid = false, truncated_tail = false;
    std::uint64_t valid_bytes = 0;
    Stats stats;
    std::optional<Diagnostic> issue;
    bool ok() const noexcept { return header_valid && !truncated_tail && !issue; }
};
} // namespace shutter
