#include <charconv>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <shutter/db.hpp>
#ifndef _WIN32
#include <sys/resource.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
struct Expected {
    std::uint64_t generation = 0;
    bool live = false;
};
std::uint64_t mix(std::uint64_t n) {
    n += 0x9e3779b97f4a7c15ULL;
    n = (n ^ (n >> 30)) * 0xbf58476d1ce4e5b9ULL;
    n = (n ^ (n >> 27)) * 0x94d049bb133111ebULL;
    return n ^ (n >> 31);
}
std::string key(std::uint64_t n) { return "key-" + std::to_string(n); }
std::string value(std::uint64_t generation, std::size_t bytes = 128) {
    if (generation % 31 == 0)
        return {};
    std::string result(bytes, '\0');
    for (std::size_t i = 0; i < bytes; ++i)
        result[i] = static_cast<char>(mix(generation + i) & 255);
    return result;
}
std::uint64_t number(const char *text) {
    std::string_view input(text);
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size())
        throw std::runtime_error("bad number");
    return result;
}
std::uint64_t rss_kib() {
#ifndef _WIN32
    rusage info{};
    if (::getrusage(RUSAGE_SELF, &info) != 0)
        return 0;
#ifdef __APPLE__
    return static_cast<std::uint64_t>(info.ru_maxrss) / 1024;
#else
    return static_cast<std::uint64_t>(info.ru_maxrss);
#endif
#else
    return 0; // RSS measurement is unavailable on Windows.
#endif
}
void ensure(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Counts {
    std::uint64_t puts = 0, deletes = 0, reads = 0, misses = 0;
};
void operation(std::uint64_t i, std::vector<Expected> &oracle, shutter::DB *db, Counts &counts) {
    const auto keys = oracle.size();
    const auto random = mix(i ^ 20261005ULL);
    const auto id = i < keys ? i : random % keys;
    const auto kind = i < keys ? 0 : (random >> 32) % 10;
    auto &expected = oracle[static_cast<std::size_t>(id)];
    if (kind < 4) {
        if (db)
            db->put(key(id), value(i));
        expected = {i, true};
        ++counts.puts;
    } else if (kind < 6) {
        if (db)
            ensure(db->remove(key(id)) == expected.live, "delete disagrees with oracle");
        expected.live = false;
        ++counts.deletes;
    } else if (kind < 9) {
        if (db) {
            const auto actual = db->get_string(key(id));
            ensure(actual.has_value() == expected.live, "get presence disagrees with oracle");
            if (actual)
                ensure(*actual == value(expected.generation), "get bytes disagree with oracle");
        }
        ++counts.reads;
    } else {
        if (db)
            ensure(!db->get(key(keys + id)), "missing key was found");
        ++counts.misses;
    }
}
void compare(shutter::DB &db, const std::vector<Expected> &oracle) {
    std::uint64_t live = 0;
    for (std::size_t i = 0; i < oracle.size(); ++i) {
        const auto actual = db.get_string(key(i));
        ensure(actual.has_value() == oracle[i].live, "final presence disagrees with oracle");
        if (actual) {
            ensure(*actual == value(oracle[i].generation), "final bytes disagree with oracle");
            ++live;
        }
    }
    ensure(db.stats().keys == live, "live count disagrees with oracle");
    ensure(db.verify().ok(), "full verification failed");
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 7)
            throw std::runtime_error("usage: shutter_stress PHASE DB_PATH KEYS OPERATIONS BATCH BATCHES");
        const std::string phase = argv[1];
        const std::filesystem::path path = argv[2];
        const auto keys = number(argv[3]), operations = number(argv[4]), batch = number(argv[5]),
                   batches = number(argv[6]);
        if (!keys || keys > 1000000 || operations < keys || operations > 10000000 || !batches ||
            batches > 100 || batch >= batches || operations % batches)
            throw std::runtime_error("invalid stress configuration");
        shutter::Options options;
        options.sync_writes = false;
        if (phase == "memory") {
            const auto baseline = rss_kib();
            const std::string bytes(64 * 1024, 'v');
            shutter::DB db(path, options);
            for (std::size_t i = 0; i < 4096; ++i)
                db.put(key(i), bytes);
            db.sync();
            ensure(db.get_string(key(4095)) == bytes, "memory probe value mismatch");
            const auto peak = rss_kib();
            // The values total 256 MiB; cap RSS growth at half that size.
            if (baseline && peak)
                ensure(peak - baseline < 128 * 1024, "RSS growth suggests resident values");
            std::cout << "{\"phase\":\"memory\",\"logical_value_bytes\":268435456,\"baseline_peak_rss_kib\":"
                      << baseline << ",\"peak_rss_kib\":" << peak
                      << ",\"database_bytes\":" << db.stats().database_bytes << "}\n";
            return 0;
        }
        std::vector<Expected> oracle(static_cast<std::size_t>(keys));
        const auto chunk = operations / batches;
        const auto previous = phase == "mutate" ? batch * chunk : operations;
        Counts ignored;
        for (std::uint64_t i = 0; i < previous; ++i)
            operation(i, oracle, nullptr, ignored);
        options.create_if_missing = phase == "mutate" && batch == 0;
        const auto open_begin = Clock::now();
        shutter::DB db(path, options);
        const double open_seconds = std::chrono::duration<double>(Clock::now() - open_begin).count();
        Counts counts;
        double compact_seconds = 0, verify_seconds = 0;
        const auto before = db.stats().database_bytes;
        const auto begin = Clock::now();
        if (phase == "mutate") {
            for (std::uint64_t i = previous; i < previous + chunk; ++i) {
                operation(i, oracle, &db, counts);
                if ((i + 1) % 10000 == 0)
                    db.sync();
            }
            db.sync();
        } else if (phase != "compact" && phase != "check")
            throw std::runtime_error("unknown phase");
        const auto operation_seconds = std::chrono::duration<double>(Clock::now() - begin).count();
        const auto verify_begin = Clock::now();
        compare(db, oracle);
        verify_seconds = std::chrono::duration<double>(Clock::now() - verify_begin).count();
        if (phase == "compact") {
            const auto compact_begin = Clock::now();
            db.compact();
            compact_seconds = std::chrono::duration<double>(Clock::now() - compact_begin).count();
            compare(db, oracle);
        }
        const auto stats = db.stats();
        std::cout << std::setprecision(10) << "{\"phase\":\"" << phase << "\",\"batch\":" << batch
                  << ",\"operations\":" << (phase == "mutate" ? chunk : 0) << ",\"puts\":" << counts.puts
                  << ",\"deletes\":" << counts.deletes << ",\"reads\":" << counts.reads
                  << ",\"misses\":" << counts.misses << ",\"open_seconds\":" << open_seconds
                  << ",\"operation_seconds\":" << operation_seconds
                  << ",\"oracle_and_verify_seconds\":" << verify_seconds
                  << ",\"compact_seconds\":" << compact_seconds << ",\"bytes_before\":" << before
                  << ",\"database_bytes\":" << stats.database_bytes << ",\"live_keys\":" << stats.keys
                  << ",\"records\":" << stats.records << ",\"peak_rss_kib\":" << rss_kib() << "}\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
