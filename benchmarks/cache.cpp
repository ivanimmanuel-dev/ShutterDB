#include <algorithm>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <shutter/cache.hpp>

namespace {
using Clock = std::chrono::steady_clock;
using Bytes = std::vector<std::byte>;
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}
std::size_t number(const char *text) {
    std::size_t value = 0;
    const std::string_view input(text);
    const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), value);
    if (error != std::errc{} || end != input.data() + input.size())
        throw std::runtime_error("invalid numeric argument");
    return value;
}
struct Temp {
    std::filesystem::path path;
    explicit Temp(const std::filesystem::path &root) {
        for (std::uint64_t attempt = 0;; ++attempt) {
            path = root / ("shutter-cache-bench-" + std::to_string(Clock::now().time_since_epoch().count()) +
                           "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(path))
                break;
        }
    }
    ~Temp() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
struct Timings {
    std::vector<double> samples;
    double seconds = 0;
    void add(double duration) {
        samples.push_back(duration);
        seconds += duration;
    }
    void print(std::string_view name) {
        std::sort(samples.begin(), samples.end());
        auto percentile = [&](std::size_t percentage) {
            if (samples.empty())
                return 0.0;
            const auto rank = (samples.size() * percentage + 99) / 100;
            return samples[rank - 1] * 1000;
        };
        std::cout << ",\"" << name << "\":{\"operations\":" << samples.size() << ",\"seconds\":" << seconds
                  << ",\"p50_ms\":" << percentile(50) << ",\"p95_ms\":" << percentile(95)
                  << ",\"p99_ms\":" << percentile(99)
                  << ",\"max_ms\":" << (samples.empty() ? 0.0 : samples.back() * 1000) << '}';
    }
};
void stamp(Bytes &value, std::size_t id) {
    for (unsigned i = 0; i < 8; ++i)
        value[i] = std::byte((static_cast<std::uint64_t>(id) >> (i * 8)) & 255);
}
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main(int argc, char **argv) {
    try {
        std::size_t writes = 16384, value_size = 4096, max_mib = 16, sync_every = 128;
        auto directory = std::filesystem::temp_directory_path();
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument != "--writes" && argument != "--value-size" && argument != "--max-mib" &&
                argument != "--sync-every" && argument != "--directory")
                throw std::runtime_error("usage: shutter_cache_bench [--writes N] [--value-size N] "
                                         "[--max-mib N] [--sync-every N] [--directory PATH]");
            if (++i == argc)
                throw std::runtime_error("missing argument");
            if (argument == "--writes")
                writes = number(argv[i]);
            else if (argument == "--value-size")
                value_size = number(argv[i]);
            else if (argument == "--max-mib")
                max_mib = number(argv[i]);
            else if (argument == "--sync-every")
                sync_every = number(argv[i]);
            else
                directory = argv[i];
        }
        if (writes == 0 || writes > 1000000 || value_size < 8 || value_size > shutter::max_value_size ||
            max_mib == 0 || max_mib > 8192 || sync_every == 0)
            throw std::runtime_error(
                "out of range: writes 1..1000000, value 8..16777216, budget 1..8192 MiB");
        const auto budget = static_cast<std::uint64_t>(max_mib) * 1024 * 1024;
        const auto record_bytes = static_cast<std::uint64_t>(40 + 64 + value_size);
        check(record_bytes <= budget - 32, "value and key must fit within the cache budget");
        std::mt19937 random(20261005);
        std::vector<std::string> keys;
        keys.reserve(writes);
        for (std::size_t id = 0; id < writes; ++id) {
            const auto suffix = std::to_string(id);
            std::string key(64 - suffix.size(), '0');
            for (auto &digit : key)
                digit = "0123456789abcdef"[random() % 16];
            keys.push_back(key + suffix);
        }
        Bytes value(value_size);
        for (auto &byte : value)
            byte = std::byte(random() & 255);
        Temp temp(directory);
        const auto path = temp.path / "assets.shdb";
        const shutter::CacheOptions options{.max_bytes = budget};
        auto cache = std::make_unique<shutter::Cache>(path, options);
        Timings inserts, maintenance, hits, misses;
        inserts.samples.reserve(writes);
        auto previous_bytes = cache->stats().storage.database_bytes;
        for (std::size_t id = 0; id < writes; ++id) {
            stamp(value, id);
            const bool reclaim = previous_bytes + record_bytes > budget;
            const auto start = Clock::now();
            const bool stored = cache->put(keys[id], value);
            if ((id + 1) % sync_every == 0)
                cache->sync();
            const auto duration = elapsed(start);
            check(stored, "cache rejected a value within its budget");
            inserts.add(duration);
            if (reclaim)
                maintenance.add(duration);
            previous_bytes = cache->stats().storage.database_bytes;
            check(previous_bytes <= budget, "write exceeded the cache budget");
        }
        double final_sync_seconds = 0;
        if (writes % sync_every) {
            const auto start = Clock::now();
            cache->sync();
            final_sync_seconds = elapsed(start);
        }
        const auto stored_stats = cache->stats();
        check(std::filesystem::file_size(path) == stored_stats.storage.database_bytes, "file size mismatch");
        std::vector<std::size_t> present, absent;
        for (std::size_t id = 0; id < writes; ++id) {
            const auto actual = cache->get(keys[id]);
            if (actual) {
                stamp(value, id);
                check(*actual == value, "stored value differs from its input");
                present.push_back(id);
            } else
                absent.push_back(id);
        }
        check(!present.empty() && present.back() == writes - 1, "latest entry was evicted");
        check(present.size() == stored_stats.storage.keys, "live-key count mismatch");
        check(absent.size() == stored_stats.evictions, "eviction count mismatch");
        check(stored_stats.storage.live_bytes == present.size() * record_bytes, "live-byte count mismatch");
        std::shuffle(present.begin(), present.end(), random);
        std::shuffle(absent.begin(), absent.end(), random);
        hits.samples.reserve(3 * present.size());
        misses.samples.reserve(absent.size());
        for (unsigned pass = 0; pass < 3; ++pass)
            for (const auto id : present) {
                const auto start = Clock::now();
                const auto actual = cache->get(keys[id]);
                hits.add(elapsed(start));
                stamp(value, id);
                check(actual && *actual == value, "hit returned a missing or changed value");
            }
        for (const auto id : absent) {
            const auto start = Clock::now();
            const auto actual = cache->get(keys[id]);
            misses.add(elapsed(start));
            check(!actual, "evicted entry reappeared");
        }
        cache.reset();
        const auto reopen_start = Clock::now();
        cache = std::make_unique<shutter::Cache>(path, options);
        const auto reopen_seconds = elapsed(reopen_start);
        for (const auto id : present) {
            const auto actual = cache->get(keys[id]);
            stamp(value, id);
            check(actual && *actual == value, "reopening lost a surviving entry");
        }
        for (const auto id : absent)
            check(!cache->get(keys[id]), "reopening restored an evicted entry");
        check(cache->stats().storage.database_bytes <= budget, "reopening exceeded the budget");
        cache.reset();
        check(shutter::DB::inspect(path).ok(), "cache file verification failed");
        std::cout << std::setprecision(10) << "{\"version\":\"" << shutter::version
                  << "\",\"seed\":20261005,\"writes\":" << writes
                  << ",\"key_bytes\":64,\"value_bytes\":" << value_size << ",\"max_bytes\":" << budget
                  << ",\"sync_every\":" << sync_every << ",\"evictions\":" << stored_stats.evictions
                  << ",\"live_keys\":" << present.size()
                  << ",\"database_bytes\":" << stored_stats.storage.database_bytes
                  << ",\"final_sync_seconds\":" << final_sync_seconds
                  << ",\"reopen_seconds\":" << reopen_seconds << ",\"verified\":true";
        inserts.print("insert");
        maintenance.print("maintenance_insert");
        hits.print("hit");
        misses.print("miss");
        std::cout << "}\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
