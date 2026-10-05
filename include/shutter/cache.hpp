#pragma once
#include <shutter/db.hpp>

namespace shutter {
struct CacheOptions {
    std::uint64_t max_bytes = 1024ULL * 1024 * 1024;
    Options storage{.sync_writes = false};
};
struct CacheStats {
    Stats storage;
    std::uint64_t max_bytes = 0;
    std::uint64_t hits = 0, misses = 0, evictions = 0;
};
class Cache {
  public:
    explicit Cache(const std::filesystem::path &path, const CacheOptions &options = {});
    ~Cache();
    Cache(const Cache &) = delete;
    Cache &operator=(const Cache &) = delete;
    // Returns false when the entry cannot fit, leaving any existing value intact.
    bool put(std::string_view key, std::span<const std::byte> value);
    bool put(std::string_view key, std::string_view value);
    std::optional<std::vector<std::byte>> get(std::string_view key);
    std::optional<std::string> get_string(std::string_view key);
    bool remove(std::string_view key);
    CacheStats stats() const;
    void sync();
    void compact();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace shutter
