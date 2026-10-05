#include <algorithm>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <shutter/db.hpp>

namespace {
using Clock = std::chrono::steady_clock;
struct Result {
    std::string name;
    std::size_t operations;
    double seconds;
};
template <class F> Result measure(std::string name, std::size_t n, F action) {
    const auto begin = Clock::now();
    action();
    return {std::move(name), n, std::chrono::duration<double>(Clock::now() - begin).count()};
}
std::size_t number(const char *text) {
    std::size_t value = 0;
    std::string_view input(text);
    auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), value);
    if (error != std::errc{} || end != input.data() + input.size())
        throw std::runtime_error("invalid numeric argument");
    return value;
}
struct Temp {
    std::filesystem::path path;
    explicit Temp(const std::filesystem::path &root) {
        for (std::uint64_t i = 0;; ++i) {
            path = root / ("shutter-bench-" + std::to_string(Clock::now().time_since_epoch().count()) + "-" +
                           std::to_string(i));
            if (std::filesystem::create_directory(path))
                break;
        }
    }
    ~Temp() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
} // namespace
int main(int argc, char **argv) {
    try {
        std::size_t count = 10000, value_size = 128, key_size = 16;
        bool sync = false;
        auto directory = std::filesystem::temp_directory_path();
        for (int i = 1; i < argc; ++i) {
            const std::string arg(argv[i]);
            if (arg == "--sync")
                sync = true;
            else if (arg == "--count" || arg == "--value-size" || arg == "--key-size" ||
                     arg == "--directory") {
                if (++i == argc)
                    throw std::runtime_error("missing argument");
                if (arg == "--count")
                    count = number(argv[i]);
                else if (arg == "--value-size")
                    value_size = number(argv[i]);
                else if (arg == "--key-size")
                    key_size = number(argv[i]);
                else
                    directory = argv[i];
            } else
                throw std::runtime_error("usage: shutter_bench [--count N] [--key-size N] [--value-size N] "
                                         "[--sync] [--directory PATH]");
        }
        if (count == 0 || count > 1000000 || key_size < 8 || key_size > shutter::max_key_size ||
            value_size > shutter::max_value_size)
            throw std::runtime_error("out of range: count 1..1000000, key 8..65536, value 0..16777216");
        auto key = [key_size](std::size_t i) {
            auto s = std::to_string(i);
            return std::string(key_size - s.size(), 'k') + s;
        };
        std::vector<std::size_t> order(count);
        std::iota(order.begin(), order.end(), 0);
        std::mt19937 random(20261005);
        std::shuffle(order.begin(), order.end(), random);
        const std::string value(value_size, 'v');
        Temp temp(directory);
        const auto path = temp.path / "bench.shdb";
        shutter::Options options;
        options.sync_writes = sync;
        options.max_live_keys = 2 * count;
        options.max_index_bytes = static_cast<std::uint64_t>(2 * count) * (key_size + 128);
        std::vector<Result> results;
        auto db = std::make_unique<shutter::DB>(path, options);
        results.push_back(measure("sequential_put", count, [&] {
            for (std::size_t i = 0; i < count; ++i)
                db->put(key(i), value);
        }));
        results.push_back(measure("random_put", count, [&] {
            for (auto i : order)
                db->put(key(count + i), value);
        }));
        std::uint64_t consumed = 0;
        results.push_back(measure("random_get", count, [&] {
            for (auto i : order) {
                auto v = db->get(key(i));
                if (!v)
                    throw std::runtime_error("missing benchmark key");
                consumed += v->size() + 1;
            }
        }));
        results.push_back(measure("missing_get", count, [&] {
            for (auto i : order) {
                if (db->get(key(2 * count + i)))
                    throw std::runtime_error("unexpected benchmark key");
                ++consumed;
            }
        }));
        results.push_back(measure("overwrite", count, [&] {
            for (auto i : order)
                db->put(key(i), value);
        }));
        results.push_back(measure("delete", count, [&] {
            for (auto i : order)
                if (!db->remove(key(count + i)))
                    throw std::runtime_error("delete failed");
        }));
        db->sync();
        db.reset();
        results.push_back(
            measure("startup_recovery", 1, [&] { db = std::make_unique<shutter::DB>(path, options); }));
        const auto before = db->stats().database_bytes;
        results.push_back(measure("verification", 1, [&] {
            if (!db->verify().ok())
                throw std::runtime_error("benchmark verification failed");
        }));
        results.push_back(measure("compaction", 1, [&] { db->compact(); }));
        const auto after = db->stats().database_bytes;
        if (!db->verify().ok())
            throw std::runtime_error("benchmark verification failed");
        db.reset();
        std::cout << std::setprecision(10) << "{\"version\":\"" << shutter::version
                  << "\",\"seed\":20261005,\"count\":" << count << ",\"key_bytes\":" << key_size
                  << ",\"value_bytes\":" << value_size << ",\"sync_writes\":" << (sync ? "true" : "false")
                  << ",\"database_bytes_before_compaction\":" << before
                  << ",\"database_bytes_after_compaction\":" << after << ",\"consumed\":" << consumed
                  << ",\"results\":[";
        for (std::size_t i = 0; i < results.size(); ++i) {
            const auto &r = results[i];
            if (i)
                std::cout << ',';
            std::cout << "{\"workload\":\"" << r.name << "\",\"operations\":" << r.operations
                      << ",\"seconds\":" << r.seconds
                      << ",\"operations_per_second\":" << static_cast<double>(r.operations) / r.seconds
                      << '}';
        }
        std::cout << "]}\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
