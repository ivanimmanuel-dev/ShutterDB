#include "internal.hpp"
#include <algorithm>
#include <cassert>
#include <list>
#include <shutter/cache.hpp>
#include <unordered_map>

namespace shutter {
namespace {
Options storage_options(const CacheOptions &options) {
    if (options.max_bytes < detail::file_header_size)
        throw Error(ErrorCode::invalid_argument, "cache budget must hold the 32-byte file header");
    return options.storage;
}
} // namespace
struct Cache::Impl {
    using Order = std::list<std::string>;
    struct Item {
        Order::iterator position;
        std::uint64_t bytes;
    };
    DB db;
    std::uint64_t limit, hits = 0, misses = 0, evictions = 0;
    Order order;
    std::unordered_map<std::string_view, Item> items;
    mutable std::mutex mutex;

    Impl(const std::filesystem::path &path, const CacheOptions &options)
        : db(path, storage_options(options)), limit(options.max_bytes) {
        std::vector<const detail::Index::value_type *> entries;
        for (const auto &entry : db.impl_->state.index)
            entries.push_back(&entry);
        std::sort(entries.begin(), entries.end(),
                  [](const auto *a, const auto *b) { return a->second.sequence < b->second.sequence; });
        items.reserve(entries.size());
        for (const auto *entry : entries) {
            order.push_front(entry->first);
            items.emplace(order.front(), Item{order.begin(), entry->second.total_size});
        }
        while (db.stats().live_bytes > limit - detail::file_header_size)
            evict({});
        reclaim();
    }
    void erase(std::string_view key) {
        const auto found = items.find(key);
        const auto position = found->second.position;
        items.erase(found);
        order.erase(position);
    }
    void evict(std::string_view protected_key) {
        auto victim = std::prev(order.end());
        if (*victim == protected_key) {
            assert(victim != order.begin());
            --victim;
        }
        const auto key = std::string_view(*victim);
        db.remove(key);
        erase(key);
        ++evictions;
    }
    void reclaim(std::string_view protected_key = {}) {
        auto stats = db.stats();
        if (stats.database_bytes <= limit)
            return;
        const auto usable = limit - detail::file_header_size;
        const auto target = usable - usable / 8;
        // Leave room for subsequent appends instead of compacting a full cache on every write.
        while (stats.live_bytes > target) {
            if (order.size() == 1 && order.front() == protected_key)
                break;
            evict(protected_key);
            stats = db.stats();
        }
        db.compact();
    }
};
Cache::Cache(const std::filesystem::path &path, const CacheOptions &options)
    : impl_(std::make_unique<Impl>(path, options)) {}
Cache::~Cache() = default;
bool Cache::put(std::string_view key, std::span<const std::byte> value) {
    detail::validate_key(key);
    if (value.size() > max_value_size)
        throw Error(ErrorCode::invalid_argument, "value exceeds 16 MiB");
    std::lock_guard guard(impl_->mutex);
    auto stats = impl_->db.stats();
    const auto bytes = detail::record_header_size + key.size() + value.size();
    const auto usable = impl_->limit - detail::file_header_size;
    if (bytes > usable)
        return false;
    const auto found = impl_->items.find(key);
    const bool added = found == impl_->items.end();
    const auto old_bytes = added ? 0 : found->second.bytes;
    if (added) {
        impl_->order.emplace_front(key);
        try {
            impl_->items.emplace(impl_->order.front(), Impl::Item{impl_->order.begin(), bytes});
        } catch (...) {
            impl_->order.pop_front();
            throw;
        }
    }
    bool stored = false;
    try {
        while (stats.live_bytes - old_bytes > usable - bytes) {
            impl_->evict(key);
            stats = impl_->db.stats();
        }
        impl_->db.put(key, value);
        stored = true;
        auto &item = impl_->items.at(key);
        item.bytes = bytes;
        impl_->order.splice(impl_->order.begin(), impl_->order, item.position);
        impl_->reclaim(key);
    } catch (...) {
        if (added && !stored)
            impl_->erase(key);
        throw;
    }
    return true;
}
bool Cache::put(std::string_view key, std::string_view value) {
    return put(key, std::as_bytes(std::span(value.data(), value.size())));
}
std::optional<std::vector<std::byte>> Cache::get(std::string_view key) {
    std::lock_guard guard(impl_->mutex);
    auto value = impl_->db.get(key);
    if (value) {
        ++impl_->hits;
        const auto &item = impl_->items.at(key);
        impl_->order.splice(impl_->order.begin(), impl_->order, item.position);
    } else
        ++impl_->misses;
    return value;
}
std::optional<std::string> Cache::get_string(std::string_view key) {
    auto value = get(key);
    if (!value)
        return std::nullopt;
    if (value->empty())
        return std::string{};
    return std::string(reinterpret_cast<const char *>(value->data()), value->size());
}
bool Cache::remove(std::string_view key) {
    std::lock_guard guard(impl_->mutex);
    if (!impl_->db.remove(key))
        return false;
    impl_->erase(key);
    impl_->reclaim();
    return true;
}
CacheStats Cache::stats() const {
    std::lock_guard guard(impl_->mutex);
    return {impl_->db.stats(), impl_->limit, impl_->hits, impl_->misses, impl_->evictions};
}
void Cache::sync() {
    std::lock_guard guard(impl_->mutex);
    impl_->db.sync();
}
void Cache::compact() {
    std::lock_guard guard(impl_->mutex);
    impl_->db.compact();
}
} // namespace shutter
