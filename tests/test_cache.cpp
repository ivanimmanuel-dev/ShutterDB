#include "internal.hpp"
#include <chrono>
#include <doctest/doctest.h>
#include <fstream>
#include <random>
#include <shutter/cache.hpp>
#include <thread>

using namespace shutter;
namespace {
struct CacheFile {
    std::filesystem::path directory, path;
    CacheFile() {
        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = std::filesystem::temp_directory_path() / ("shutter-cache-" + std::to_string(seed));
        REQUIRE(std::filesystem::create_directory(directory));
        path = directory / "cache.shdb";
    }
    ~CacheFile() {
        detail::fault_hook = {};
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};
} // namespace
TEST_CASE("cache evicts cold entries, protects the new entry and bounds its file") {
    CacheFile file;
    const std::string value(100, 'x');
    Cache cache(file.path, {.max_bytes = 32 + 4 * 141});
    for (const auto key : {"a", "b", "c", "d"})
        REQUIRE(cache.put(key, value));
    REQUIRE(cache.get("a"));
    REQUIRE(cache.put("e", value));
    CHECK_FALSE(cache.get("b"));
    CHECK(cache.get("a"));
    CHECK(cache.get("e"));
    CHECK(cache.stats().evictions >= 1);
    CHECK(std::filesystem::file_size(file.path) <= cache.stats().max_bytes);
    cache.sync();
}
TEST_CASE("oversized cache insert does not evict or replace existing data") {
    CacheFile file;
    Cache cache(file.path, {.max_bytes = 100});
    REQUIRE(cache.put("key", "value"));
    const auto before = cache.stats().storage;
    CHECK_FALSE(cache.put("key", std::string(100, 'x')));
    CHECK_FALSE(cache.put("new", std::string(100, 'x')));
    CHECK(cache.get_string("key") == "value");
    CHECK(cache.stats().storage.database_bytes == before.database_bytes);
    CHECK(cache.stats().evictions == 0);
}
TEST_CASE("cache recovers write order after restart and applies a smaller budget") {
    CacheFile file;
    {
        Cache cache(file.path, {.max_bytes = 4096});
        for (const auto key : {"a", "b", "c", "d"})
            REQUIRE(cache.put(key, std::string(100, *key)));
        REQUIRE(cache.put("a", std::string(100, 'A')));
        cache.sync();
    }
    {
        Cache cache(file.path, {.max_bytes = 32 + 3 * 141});
        CHECK_FALSE(cache.get("b"));
        CHECK(cache.get_string("a") == std::string(100, 'A'));
        CHECK(cache.get("d"));
        CHECK(cache.stats().storage.database_bytes <= cache.stats().max_bytes);
        cache.sync();
    }
    CHECK(DB::inspect(file.path).ok());
}
TEST_CASE("cache overwrites and deletes reclaim space without exceeding the budget") {
    CacheFile file;
    Cache cache(file.path, {.max_bytes = 8192});
    std::mt19937 random(42);
    for (int i = 0; i < 500; ++i) {
        const auto key = std::to_string(random() % 20);
        const std::string value(random() % 600, static_cast<char>('a' + i % 26));
        REQUIRE(cache.put(key, value));
        REQUIRE(cache.get_string(key) == value);
        if (i % 3 == 0)
            CHECK(cache.remove(key));
        CHECK_FALSE(cache.remove("absent"));
        CHECK(cache.stats().storage.database_bytes <= 8192);
        CHECK(std::filesystem::file_size(file.path) <= 8192);
    }
    cache.sync();
}
TEST_CASE("header-only cache rejects entries and invalid budgets do not create files") {
    CacheFile file;
    CHECK_THROWS_AS(Cache(file.path, {.max_bytes = 31}), Error);
    CHECK_FALSE(std::filesystem::exists(file.path));
    Cache cache(file.path, {.max_bytes = 32});
    CHECK_FALSE(cache.put("a", ""));
    CHECK_FALSE(cache.get("a"));
    CHECK(cache.stats().storage.database_bytes == 32);
}
TEST_CASE("cache supports binary keys, empty values and concurrent callers") {
    CacheFile file;
    Cache cache(file.path, {.max_bytes = 65536});
    const std::string binary("a\0b", 3);
    REQUIRE(cache.put(binary, ""));
    CHECK(cache.get_string(binary) == "");
    auto worker = [&](int thread) {
        for (int i = 0; i < 100; ++i) {
            const auto key = std::to_string(thread) + ":" + std::to_string(i);
            cache.put(key, "value");
            (void)cache.get(key);
        }
    };
    std::thread a(worker, 1), b(worker, 2);
    a.join();
    b.join();
    CHECK(cache.stats().storage.keys == 201);
    CHECK(cache.stats().hits == 201);
    cache.sync();
}
TEST_CASE("cache append and automatic compaction failures require recovery") {
    for (const auto fault :
         {detail::Fault::after_append, detail::Fault::before_replacement, detail::Fault::after_replacement}) {
        CacheFile file;
        {
            Cache cache(file.path, {.max_bytes = 200});
            REQUIRE(cache.put("a", std::string(100, 'a')));
            cache.sync();
            detail::fault_hook = [fault](detail::Fault point) {
                if (point == fault)
                    throw Error(ErrorCode::io_error, "injected cache failure");
            };
            CHECK_THROWS_AS(cache.put("b", std::string(100, 'b')), Error);
            detail::fault_hook = {};
            CHECK_THROWS_AS(cache.get("b"), Error);
        }
        Cache reopened(file.path, {.max_bytes = 200});
        if (const auto value = reopened.get_string("b"))
            CHECK(*value == std::string(100, 'b'));
        CHECK(reopened.stats().storage.database_bytes <= 200);
        reopened.sync();
    }
}
