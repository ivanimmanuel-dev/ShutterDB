#include "internal.hpp"
#include <algorithm>

namespace shutter::detail {
namespace {
class ScanBuffer {
    const Reader &reader_;
    std::uint64_t size_, start_ = 0;
    Bytes bytes_;

  public:
    ScanBuffer(const Reader &reader, std::uint64_t size) : reader_(reader), size_(size) {}
    std::span<const std::byte> view(std::uint64_t offset, std::size_t count) {
        if (offset > size_ || count > size_ - offset)
            throw Error(ErrorCode::corruption, "scan exceeds database size", offset);
        if (offset < start_ || offset - start_ > bytes_.size() ||
            count > bytes_.size() - static_cast<std::size_t>(offset - start_)) {
            const auto length =
                std::min<std::uint64_t>(size_ - offset, std::max<std::size_t>(1024 * 1024, count));
            bytes_.resize(static_cast<std::size_t>(length));
            reader_.read(offset, bytes_);
            start_ = offset;
        }
        return std::span(bytes_).subspan(static_cast<std::size_t>(offset - start_), count);
    }
};
} // namespace
std::span<const std::byte> payload(std::span<const std::byte> record, const RecordHeader &header,
                                   std::uint64_t offset) {
    if (header.total_size > record.size())
        throw Error(ErrorCode::corruption, "truncated record payload", offset);
    const auto bytes = record.subspan(record_header_size, header.total_size - record_header_size);
    const auto actual = crc32c(bytes);
    if (actual != header.payload_crc)
        throw Error(ErrorCode::corruption, "payload CRC32C mismatch", offset, header.payload_crc, actual);
    return bytes;
}
Scan scan(const Reader &reader, const Options &options) {
    Scan result;
    auto &report = result.report;
    auto &stats = report.stats;
    stats.sync_writes = options.sync_writes;
    stats.database_bytes = reader.size();
    ScanBuffer buffer(reader, stats.database_bytes);
    try {
        const auto header = buffer.view(
            0, static_cast<std::size_t>(std::min<std::uint64_t>(file_header_size, stats.database_bytes)));
        stats.last_sequence = decode_file_header(header);
        report.header_valid = true;
        report.valid_bytes = file_header_size;
        std::uint64_t previous_sequence = 0;
        while (report.valid_bytes < stats.database_bytes) {
            const auto offset = report.valid_bytes;
            const auto remaining = stats.database_bytes - offset;
            const auto bytes = buffer.view(
                offset, static_cast<std::size_t>(std::min<std::uint64_t>(record_header_size, remaining)));
            if (remaining < record_header_size) {
                if (!plausible_partial_header(bytes))
                    throw Error(ErrorCode::corruption, "unexpected bytes after final record", offset);
                if (bytes.size() >= 16 && read_le(bytes, 8, 8) <= previous_sequence)
                    throw Error(ErrorCode::corruption, "non-increasing sequence in partial header", offset);
                report.truncated_tail = true;
                break;
            }
            const auto h = decode_header(bytes, offset);
            if (h.sequence <= previous_sequence)
                throw Error(ErrorCode::corruption, "non-increasing sequence number", offset);
            if (h.total_size > remaining) {
                report.truncated_tail = true;
                break;
            }
            const auto data = payload(buffer.view(offset, h.total_size), h, offset);
            const std::string_view key(reinterpret_cast<const char *>(data.data()), h.key_size);
            auto entry = result.index.find(key);
            if (h.kind == Kind::put) {
                const Entry next{offset, h.sequence, h.total_size, h.value_size};
                if (entry == result.index.end()) {
                    const auto cost = key.size() + 128;
                    if (result.index.size() >= options.max_live_keys || cost > options.max_index_bytes ||
                        result.index_bytes > options.max_index_bytes - cost)
                        throw Error(ErrorCode::resource_limit, "in-memory index budget exceeded", offset);
                    result.index.emplace(std::string(key), next);
                    result.index_bytes += cost;
                } else {
                    stats.live_bytes -= entry->second.total_size;
                    entry->second = next;
                }
                stats.live_bytes += h.total_size;
                ++stats.put_records;
            } else {
                if (entry != result.index.end()) {
                    stats.live_bytes -= entry->second.total_size;
                    result.index_bytes -= key.size() + 128;
                    result.index.erase(entry);
                }
                ++stats.tombstones;
            }
            ++stats.records;
            stats.last_sequence = std::max(stats.last_sequence, h.sequence);
            previous_sequence = h.sequence;
            report.valid_bytes += h.total_size;
        }
    } catch (const Error &e) {
        report.issue = Diagnostic{e.code(), e.offset(), e.what(), e.expected_crc(), e.actual_crc()};
    }
    stats.keys = result.index.size();
    if (report.header_valid)
        stats.reclaimable_bytes = stats.database_bytes - file_header_size - stats.live_bytes;
    return result;
}
void require_valid(const VerifyReport &report, bool allow_tail) {
    if (report.issue) {
        const auto &e = *report.issue;
        throw Error(e.code, e.message, e.offset, e.expected_crc, e.actual_crc);
    }
    if (!report.header_valid || (report.truncated_tail && !allow_tail))
        throw Error(ErrorCode::corruption, "truncated final record", report.valid_bytes);
}
} // namespace shutter::detail

namespace shutter {
VerifyReport DB::inspect(const std::filesystem::path &path, const Options &options) {
    const auto p = detail::normalized(path);
    if (!detail::exists(p))
        throw Error(ErrorCode::not_found, "database does not exist");
    detail::File lock(detail::sibling(p, ".lock"), detail::File::Mode::read_write, true);
    lock.lock(false);
    detail::File file(p, detail::File::Mode::read_only);
    return detail::scan(file, options).report;
}
VerifyReport DB::verify() const {
    std::lock_guard guard(impl_->mutex);
    impl_->ensure_usable();
    return detail::scan(*impl_->file, impl_->options).report;
}
} // namespace shutter
