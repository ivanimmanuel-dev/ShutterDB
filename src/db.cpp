#include "internal.hpp"
#include <algorithm>
#include <limits>

namespace shutter {
using namespace detail;
const char *error_name(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::invalid_argument:
        return "INVALID_ARGUMENT";
    case ErrorCode::io_error:
        return "IO_ERROR";
    case ErrorCode::corruption:
        return "CORRUPTION";
    case ErrorCode::unsupported_format:
        return "UNSUPPORTED_FORMAT";
    case ErrorCode::lock_conflict:
        return "LOCK_CONFLICT";
    case ErrorCode::permission_denied:
        return "PERMISSION_DENIED";
    case ErrorCode::not_found:
        return "NOT_FOUND";
    case ErrorCode::resource_limit:
        return "RESOURCE_LIMIT";
    case ErrorCode::needs_reopen:
        return "NEEDS_REOPEN";
    }
    return "UNKNOWN";
}
DB::Impl::Impl(const std::filesystem::path &input, const Options &opts)
    : path(normalized(input)), options(opts) {
    lock_file = std::make_unique<File>(sibling(path, ".lock"), File::Mode::read_write, true);
    lock_file->lock(true);
    recover_compaction();
    if (!detail::exists(path)) {
        if (!options.create_if_missing)
            throw Error(ErrorCode::not_found, "database does not exist");
        const auto tmp = sibling(path, ".init");
        remove_file(tmp);
        {
            File initial(tmp, File::Mode::create_exclusive);
            initial.write(0, file_header());
            initial.sync();
        }
        replace(tmp, path);
        sync_directory(path);
    }
    file = std::make_unique<File>(path, File::Mode::read_write);
    state = scan(*file, options);
    require_valid(state.report, options.recover_truncated_tail);
    if (state.report.truncated_tail) {
        const auto removed = state.report.stats.database_bytes - state.report.valid_bytes;
        file->truncate(state.report.valid_bytes);
        file->sync();
        state.report.stats.database_bytes -= removed;
        state.report.stats.reclaimable_bytes -= removed;
        state.report.stats.recovered_tail_bytes = removed;
        state.report.truncated_tail = false;
    }
}
void DB::Impl::ensure_usable() const {
    if (poisoned)
        throw Error(ErrorCode::needs_reopen, "operation outcome is uncertain; close and reopen the database");
}
void DB::Impl::append(Kind kind, std::string_view key, std::span<const std::byte> value) {
    ensure_usable();
    auto &stats = state.report.stats;
    if (stats.last_sequence == std::numeric_limits<std::uint64_t>::max())
        throw Error(ErrorCode::resource_limit, "sequence number exhausted");
    auto bytes = encode(kind, stats.last_sequence + 1, key, value);
    auto found = state.index.find(key);
    const auto old_size = found == state.index.end() ? 0U : found->second.total_size;
    const auto cost = key.size() + 128;
    const bool is_new = kind == Kind::put && found == state.index.end();
    // Allocate before append so publishing the new index entry cannot allocate.
    Index staged;
    if (is_new) {
        if (state.index.size() >= options.max_live_keys || cost > options.max_index_bytes ||
            state.index_bytes > options.max_index_bytes - cost)
            throw Error(ErrorCode::resource_limit, "in-memory index budget exceeded");
        staged.emplace(std::string(key), Entry{});
    }
    hit(Fault::before_append);
    try {
        if (file->size() != stats.database_bytes)
            throw Error(ErrorCode::corruption, "database changed outside this handle");
        const auto split = std::min<std::size_t>(record_header_size, bytes.size() - 1);
        file->write(stats.database_bytes, std::span(bytes).first(split));
        hit(Fault::during_append);
        file->write(stats.database_bytes + split, std::span(bytes).subspan(split));
        hit(Fault::after_append);
        if (options.sync_writes) {
            hit(Fault::before_flush);
            file->sync();
            hit(Fault::after_flush);
        }
    } catch (...) {
        poisoned = true;
        throw;
    }
    const Entry next{stats.database_bytes, stats.last_sequence + 1, static_cast<std::uint32_t>(bytes.size()),
                     static_cast<std::uint32_t>(value.size())};
    if (kind == Kind::put) {
        if (is_new) {
            auto node = staged.extract(staged.begin());
            node.mapped() = next;
            state.index.insert(std::move(node));
            state.index_bytes += cost;
        } else
            found->second = next;
        stats.live_bytes += bytes.size();
        ++stats.put_records;
    } else {
        state.index.erase(found);
        state.index_bytes -= cost;
        ++stats.tombstones;
    }
    stats.live_bytes -= old_size;
    ++stats.records;
    ++stats.last_sequence;
    stats.database_bytes += bytes.size();
    stats.keys = state.index.size();
    stats.reclaimable_bytes = stats.database_bytes - file_header_size - stats.live_bytes;
    state.report.valid_bytes = stats.database_bytes;
}
DB::DB(const std::filesystem::path &path, const Options &options)
    : impl_(std::make_unique<Impl>(path, options)) {}
DB::~DB() = default;
void DB::put(std::string_view key, std::span<const std::byte> value) {
    std::lock_guard guard(impl_->mutex);
    impl_->append(Kind::put, key, value);
}
void DB::put(std::string_view key, std::string_view value) {
    put(key, std::as_bytes(std::span(value.data(), value.size())));
}
std::optional<Bytes> DB::get(std::string_view key) const {
    validate_key(key);
    std::lock_guard guard(impl_->mutex);
    impl_->ensure_usable();
    auto found = impl_->state.index.find(key);
    if (found == impl_->state.index.end())
        return std::nullopt;
    const auto &entry = found->second;
    // The index supplies the validated record size, so one read can fetch the whole record.
    Bytes bytes(entry.total_size);
    impl_->file->read(entry.offset, bytes);
    const auto h = decode_header(std::span(bytes).first(record_header_size), entry.offset);
    if (h.kind != Kind::put || h.sequence != entry.sequence || h.total_size != entry.total_size ||
        h.key_size != key.size())
        throw Error(ErrorCode::corruption, "record no longer matches the index", entry.offset);
    const auto data = payload(bytes, h, entry.offset);
    if (std::string_view(reinterpret_cast<const char *>(data.data()), h.key_size) != key)
        throw Error(ErrorCode::corruption, "record no longer matches the index", entry.offset);
    bytes.erase(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(record_header_size + h.key_size));
    return bytes;
}
std::optional<std::string> DB::get_string(std::string_view key) const {
    auto data = get(key);
    if (!data)
        return std::nullopt;
    if (data->empty())
        return std::string{};
    return std::string(reinterpret_cast<const char *>(data->data()), data->size());
}
bool DB::remove(std::string_view key) {
    validate_key(key);
    std::lock_guard guard(impl_->mutex);
    impl_->ensure_usable();
    if (!impl_->state.index.contains(key))
        return false;
    impl_->append(Kind::remove, key, {});
    return true;
}
bool DB::contains(std::string_view key) const {
    validate_key(key);
    std::lock_guard guard(impl_->mutex);
    impl_->ensure_usable();
    return impl_->state.index.contains(key);
}
Stats DB::stats() const {
    std::lock_guard guard(impl_->mutex);
    impl_->ensure_usable();
    return impl_->state.report.stats;
}
void DB::sync() {
    std::lock_guard guard(impl_->mutex);
    impl_->ensure_usable();
    try {
        hit(Fault::before_flush);
        impl_->file->sync();
        hit(Fault::after_flush);
    } catch (...) {
        impl_->poisoned = true;
        throw;
    }
}
void DB::compact() {
    std::lock_guard guard(impl_->mutex);
    impl_->ensure_usable();
    impl_->compact();
}
} // namespace shutter

#ifdef SHUTTER_TESTING
namespace shutter::detail {
thread_local std::function<void(Fault)> fault_hook;
void hit(Fault point) {
    if (fault_hook)
        fault_hook(point);
}
} // namespace shutter::detail
#endif
