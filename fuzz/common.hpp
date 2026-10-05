#pragma once
#include "internal.hpp"
#include <algorithm>
#include <cstdlib>

namespace shutter::fuzz {
struct MemoryReader final : detail::Reader {
    std::span<const std::byte> bytes;
    explicit MemoryReader(std::span<const std::byte> input) : bytes(input) {}
    std::uint64_t size() const override { return bytes.size(); }
    void read(std::uint64_t offset, std::span<std::byte> into) const override {
        if (offset > bytes.size() || into.size() > bytes.size() - offset)
            throw Error(ErrorCode::corruption, "fuzz reader out of range", offset);
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), into.size(), into.begin());
    }
};
inline void exercise(std::span<const std::byte> bytes) {
    if (bytes.size() > 1024 * 1024)
        return;
    MemoryReader reader(bytes);
    Options options;
    options.max_live_keys = 128;
    options.max_index_bytes = 1024 * 1024;
    const auto state = detail::scan(reader, options);
    if (state.report.valid_bytes > bytes.size())
        std::abort();
    if (state.report.ok() && state.report.valid_bytes != bytes.size())
        std::abort();
    if (state.report.truncated_tail && !state.report.issue) {
        MemoryReader prefix(bytes.first(static_cast<std::size_t>(state.report.valid_bytes)));
        const auto recovered = detail::scan(prefix, options);
        if (!recovered.report.ok() || recovered.index.size() != state.index.size())
            std::abort();
    }
    try {
        detail::require_valid(state.report, true);
    } catch (const Error &) {
    }
    try {
        detail::require_valid(state.report, false);
    } catch (const Error &) {
    }
    if (bytes.size() >= detail::record_header_size) {
        try {
            const auto h = detail::decode_header(bytes.first(detail::record_header_size), 0);
            if (h.total_size <= bytes.size())
                (void)detail::payload(bytes, h, 0);
        } catch (const Error &) {
        }
    } else
        (void)detail::plausible_partial_header(bytes);
}
} // namespace shutter::fuzz
