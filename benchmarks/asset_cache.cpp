#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <shutter/db.hpp>
#include <sqlite3.h>
#ifdef SHUTTER_HAVE_ROCKSDB
#include <rocksdb/db.h>
#include <rocksdb/version.h>
#include <rocksdb/write_batch.h>
#endif
#ifndef _WIN32
#include <sys/resource.h>
#endif

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
std::uint64_t rss_kib() {
#ifdef __linux__
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line))
        if (line.starts_with("VmHWM:"))
            return std::stoull(line.substr(6));
    return 0;
#elif !defined(_WIN32)
    rusage usage{};
    if (::getrusage(RUSAGE_SELF, &usage) != 0)
        return 0;
#ifdef __APPLE__
    return static_cast<std::uint64_t>(usage.ru_maxrss) / 1024;
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#endif
#else
    return 0;
#endif
}
struct Store {
    virtual ~Store() = default;
    virtual void begin() = 0;
    virtual void finish() = 0;
    virtual void put(std::string_view key, std::span<const std::byte> value) = 0;
    virtual std::optional<Bytes> get(std::string_view key) = 0;
    virtual void remove(std::string_view key) = 0;
    virtual void compact() = 0;
    virtual void close() {}
};
class Shutter final : public Store {
    shutter::DB db_;
    bool batch_;

  public:
    Shutter(const std::filesystem::path &path, bool batch, bool create)
        : db_(path, {.sync_writes = !batch, .create_if_missing = create}), batch_(batch) {}
    void begin() override {}
    void finish() override {
        if (batch_)
            db_.sync();
    }
    void put(std::string_view key, std::span<const std::byte> value) override { db_.put(key, value); }
    std::optional<Bytes> get(std::string_view key) override { return db_.get(key); }
    void remove(std::string_view key) override {
        if (!db_.remove(key))
            throw std::runtime_error("missing key during delete");
    }
    void compact() override { db_.compact(); }
};
class SQLite final : public Store {
    struct Close {
        void operator()(sqlite3 *db) const { sqlite3_close(db); }
    };
    struct Finalize {
        void operator()(sqlite3_stmt *statement) const { sqlite3_finalize(statement); }
    };
    using Statement = std::unique_ptr<sqlite3_stmt, Finalize>;
    std::unique_ptr<sqlite3, Close> db_;
    Statement put_, get_, remove_;
    bool batch_;
    void check(int code) {
        if (code != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db_.get()));
    }
    void exec(const char *sql) { check(sqlite3_exec(db_.get(), sql, nullptr, nullptr, nullptr)); }
    Statement prepare(const char *sql) {
        sqlite3_stmt *statement = nullptr;
        check(sqlite3_prepare_v2(db_.get(), sql, -1, &statement, nullptr));
        return Statement(statement);
    }
    void bind(sqlite3_stmt *statement, std::string_view key) {
        check(sqlite3_reset(statement));
        check(sqlite3_bind_text(statement, 1, key.data(), static_cast<int>(key.size()), SQLITE_STATIC));
    }
    void write(sqlite3_stmt *statement) {
        if (sqlite3_step(statement) != SQLITE_DONE)
            throw std::runtime_error(sqlite3_errmsg(db_.get()));
        check(sqlite3_reset(statement));
    }

  public:
    SQLite(const std::filesystem::path &path, bool batch, bool create, bool tuned) : batch_(batch) {
        sqlite3 *raw = nullptr;
        const auto code = sqlite3_open_v2(
            path.string().c_str(), &raw,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX | (create ? SQLITE_OPEN_CREATE : 0), nullptr);
        db_.reset(raw);
        check(code);
        if (tuned)
            exec("PRAGMA locking_mode=EXCLUSIVE;");
        exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA cache_size=-2048;");
        exec(tuned ? "PRAGMA mmap_size=268435456;" : "PRAGMA mmap_size=0;");
        if (create)
            exec("CREATE TABLE cache (key TEXT PRIMARY KEY, value BLOB NOT NULL);");
        put_ =
            prepare("INSERT INTO cache VALUES(?1, ?2) ON CONFLICT(key) DO UPDATE SET value=excluded.value;");
        get_ = prepare("SELECT value FROM cache WHERE key=?1;");
        remove_ = prepare("DELETE FROM cache WHERE key=?1;");
    }
    void begin() override {
        if (batch_)
            exec("BEGIN IMMEDIATE;");
    }
    void finish() override {
        if (batch_)
            exec("COMMIT;");
    }
    void put(std::string_view key, std::span<const std::byte> value) override {
        bind(put_.get(), key);
        check(sqlite3_bind_blob(put_.get(), 2, value.data(), static_cast<int>(value.size()), SQLITE_STATIC));
        write(put_.get());
    }
    std::optional<Bytes> get(std::string_view key) override {
        bind(get_.get(), key);
        const auto result = sqlite3_step(get_.get());
        if (result == SQLITE_DONE) {
            check(sqlite3_reset(get_.get()));
            return std::nullopt;
        }
        if (result != SQLITE_ROW)
            throw std::runtime_error(sqlite3_errmsg(db_.get()));
        const auto size = sqlite3_column_bytes(get_.get(), 0);
        const auto *data = static_cast<const std::byte *>(sqlite3_column_blob(get_.get(), 0));
        Bytes owned(static_cast<std::size_t>(size));
        if (size)
            std::memcpy(owned.data(), data, owned.size());
        check(sqlite3_reset(get_.get()));
        return owned;
    }
    void remove(std::string_view key) override {
        bind(remove_.get(), key);
        write(remove_.get());
        if (sqlite3_changes(db_.get()) != 1)
            throw std::runtime_error("missing key during delete");
    }
    void compact() override {
        exec("VACUUM;");
        check(sqlite3_wal_checkpoint_v2(db_.get(), nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr));
    }
};
#ifdef SHUTTER_HAVE_ROCKSDB
class Rocks final : public Store {
    std::unique_ptr<rocksdb::DB> db_;
    rocksdb::WriteBatch pending_;
    rocksdb::WriteOptions writes_;
    bool batch_;
    static void check(const rocksdb::Status &status) {
        if (!status.ok())
            throw std::runtime_error(status.ToString());
    }
    static rocksdb::Slice slice(std::string_view key) { return {key.data(), key.size()}; }

  public:
    Rocks(const std::filesystem::path &path, bool batch, bool create) : batch_(batch) {
        rocksdb::Options options;
        options.create_if_missing = create;
        options.compression = rocksdb::kNoCompression;
#if ROCKSDB_MAJOR >= 11
        check(rocksdb::DB::Open(options, path.string(), &db_));
#else
        rocksdb::DB *raw = nullptr;
        check(rocksdb::DB::Open(options, path.string(), &raw));
        db_.reset(raw);
#endif
        writes_.sync = true;
    }
    void begin() override { pending_.Clear(); }
    void finish() override {
        if (batch_)
            check(db_->Write(writes_, &pending_));
    }
    void put(std::string_view key, std::span<const std::byte> value) override {
        const rocksdb::Slice bytes(reinterpret_cast<const char *>(value.data()), value.size());
        if (batch_)
            check(pending_.Put(slice(key), bytes));
        else
            check(db_->Put(writes_, slice(key), bytes));
    }
    std::optional<Bytes> get(std::string_view key) override {
        rocksdb::PinnableSlice value;
        const auto status = db_->Get(rocksdb::ReadOptions{}, db_->DefaultColumnFamily(), slice(key), &value);
        if (status.IsNotFound())
            return std::nullopt;
        check(status);
        Bytes owned(value.size());
        std::memcpy(owned.data(), value.data(), value.size());
        return owned;
    }
    void remove(std::string_view key) override {
        if (batch_)
            check(pending_.Delete(slice(key)));
        else
            check(db_->Delete(writes_, slice(key)));
    }
    void compact() override {
        rocksdb::FlushOptions flush;
        flush.wait = true;
        check(db_->Flush(flush));
        check(db_->CompactRange(rocksdb::CompactRangeOptions{}, nullptr, nullptr));
    }
    void close() override { check(db_->Close()); }
};
#endif
std::uint64_t disk_bytes(const std::filesystem::path &directory) {
    std::uint64_t size = 0;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(directory))
        if (entry.is_regular_file())
            size += entry.file_size();
    return size;
}
void stamp(Bytes &value, std::size_t id, std::size_t generation) {
    for (unsigned i = 0; i < 8; ++i)
        value[i] = std::byte((static_cast<std::uint64_t>(id) >> (i * 8)) & 255);
    value[8] = std::byte(generation);
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 8)
            throw std::runtime_error(
                "usage: shutter_compare ENGINE MODE PHASE DIRECTORY COUNT VALUE_BYTES ROUND");
        const std::string engine = argv[1], mode = argv[2], phase = argv[3];
        const std::filesystem::path directory = argv[4];
        const auto count = number(argv[5]), value_size = number(argv[6]);
        const auto round = number(argv[7]);
        if ((engine != "shutter" && engine != "sqlite" && engine != "sqlite-tuned" && engine != "rocksdb") ||
            (mode != "sync" && mode != "batch") ||
            (phase != "populate" && phase != "read" && phase != "churn" && phase != "reopen" &&
             phase != "compact" && phase != "compacted-read") ||
            count < 4 || count > 100000 || count % 4 || value_size < 16 ||
            value_size > shutter::max_value_size || round > 255 ||
            ((phase == "populate" || phase == "read") != (round == 0)))
            throw std::runtime_error("invalid comparison configuration");
        const auto path = directory / "cache.db";
        const bool create = phase == "populate", batch = mode == "batch";
        if (create && std::filesystem::exists(path))
            throw std::runtime_error("populate requires a new database");
        std::mt19937 random(20261005);
        std::vector<std::string> keys;
        for (std::size_t i = 0; i < 2 * count; ++i) {
            const auto suffix = std::to_string(i);
            std::string key(64 - suffix.size(), '0');
            for (auto &digit : key)
                digit = "0123456789abcdef"[random() % 16];
            keys.push_back(key + suffix);
        }
        std::vector<std::size_t> order(count);
        std::iota(order.begin(), order.end(), 0);
        std::shuffle(order.begin(), order.end(), random);
        Bytes value(value_size);
        for (auto &byte : value)
            byte = std::byte(random() & 255);
        const auto start = Clock::now();
        std::unique_ptr<Store> db;
        if (engine == "shutter")
            db = std::make_unique<Shutter>(path, batch, create);
#ifdef SHUTTER_HAVE_ROCKSDB
        else if (engine == "rocksdb")
            db = std::make_unique<Rocks>(path, batch, create);
#else
        else if (engine == "rocksdb")
            throw std::runtime_error("configure with SHUTTER_COMPARE_ROCKSDB=ON to enable RocksDB");
#endif
        else
            db = std::make_unique<SQLite>(path, batch, create, engine == "sqlite-tuned");
        const auto open_seconds = elapsed(start);
        double write_seconds = 0, first_pass_seconds = 0, warm_read_seconds = 0, miss_seconds = 0;
        double compact_seconds = 0;
        std::size_t writes = 0, hits = 0, misses = 0;
        std::uint64_t consumed = 0, before_compact = 0, after_compact = 0;
        const bool changed = round != 0;
        if (create || phase == "churn") {
            const auto begin = Clock::now();
            auto write = [&](auto operation) {
                if (writes % 128 == 0)
                    db->begin();
                operation();
                if (++writes % 128 == 0)
                    db->finish();
            };
            for (const auto id : order) {
                if (!create && id % 4 == 3)
                    continue;
                if (!create && id % 4 == 1) {
                    if (round > 1) {
                        stamp(value, id, round);
                        write([&] { db->put(keys[id], value); });
                    }
                    write([&] { db->remove(keys[id]); });
                } else {
                    stamp(value, id, round);
                    write([&] { db->put(keys[id], value); });
                }
            }
            if (writes % 128)
                db->finish();
            write_seconds = elapsed(begin);
        } else if (phase != "compact") {
            for (int pass = 0; pass < 4; ++pass) {
                const auto begin = Clock::now();
                for (const auto id : order) {
                    if (changed && id % 4 == 1)
                        continue;
                    auto found = db->get(keys[id]);
                    if (!found || found->size() != value_size)
                        throw std::runtime_error("read mismatch");
                    consumed += std::to_integer<unsigned>(found->front()) +
                                std::to_integer<unsigned>(found->back()) + found->size();
                    ++hits;
                }
                if (pass == 0)
                    first_pass_seconds = elapsed(begin);
                else
                    warm_read_seconds += elapsed(begin);
            }
            const auto begin = Clock::now();
            for (int pass = 0; pass < 4; ++pass)
                for (const auto id : order) {
                    if (db->get(keys[count + id]))
                        throw std::runtime_error("unexpected missing key");
                    ++misses;
                }
            miss_seconds = elapsed(begin);
        }
        auto verify = [&] {
            for (std::size_t id = 0; id < count; ++id) {
                auto actual = db->get(keys[id]);
                const bool present = !changed || id % 4 != 1;
                if (actual.has_value() != present)
                    throw std::runtime_error("presence check failed");
                if (present) {
                    stamp(value, id, changed && id % 2 == 0 ? round : 0);
                    if (*actual != value)
                        throw std::runtime_error("full value check failed");
                }
            }
        };
        verify();
        if (phase == "compact") {
            before_compact = disk_bytes(directory);
            const auto begin = Clock::now();
            db->compact();
            compact_seconds = elapsed(begin);
            after_compact = disk_bytes(directory);
            verify();
        }
        const auto active_bytes = disk_bytes(directory);
        const auto close_begin = Clock::now();
        db->close();
        db.reset();
        const auto close_seconds = elapsed(close_begin);
        std::cout << std::setprecision(10) << "{\"engine\":\"" << engine << "\",\"mode\":\"" << mode
                  << "\",\"phase\":\"" << phase << "\",\"round\":" << round << ",\"count\":" << count
                  << ",\"value_bytes\":" << value_size << ",\"sqlite_version\":\"" << sqlite3_libversion()
#ifdef SHUTTER_HAVE_ROCKSDB
                  << "\",\"rocksdb_version\":\"" << ROCKSDB_MAJOR << '.' << ROCKSDB_MINOR << '.'
                  << ROCKSDB_PATCH
#endif
                  << "\",\"open_seconds\":" << open_seconds << ",\"write_seconds\":" << write_seconds
                  << ",\"writes\":" << writes << ",\"first_pass_seconds\":" << first_pass_seconds
                  << ",\"warm_read_seconds\":" << warm_read_seconds << ",\"hits\":" << hits
                  << ",\"miss_seconds\":" << miss_seconds << ",\"misses\":" << misses
                  << ",\"compact_seconds\":" << compact_seconds << ",\"close_seconds\":" << close_seconds
                  << ",\"bytes_before_compaction\":" << before_compact
                  << ",\"bytes_after_compaction\":" << after_compact << ",\"active_bytes\":" << active_bytes
                  << ",\"closed_bytes\":" << disk_bytes(directory) << ",\"peak_rss_kib\":" << rss_kib()
                  << ",\"consumed\":" << consumed << ",\"verified\":true}\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
