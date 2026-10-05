#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "internal.hpp"
#include <algorithm>
#include <chrono>
#include <doctest/doctest.h>
#include <fstream>
#include <random>
#include <thread>
#ifndef _WIN32
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace shutter;
using namespace shutter::detail;
namespace {
struct Temp {
    std::filesystem::path dir, path;
    Temp() {
        static std::uint64_t counter = 0;
        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            dir = std::filesystem::temp_directory_path() /
                  ("shutter-test-" + std::to_string(seed) + "-" + std::to_string(++counter));
            if (std::filesystem::create_directory(dir))
                break;
        }
        path = dir / "data.shdb";
    }
    ~Temp() {
        fault_hook = {};
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};
Options buffered() {
    Options o;
    o.sync_writes = false;
    return o;
}
Bytes read_all(const std::filesystem::path &p) {
    File f(p, File::Mode::read_only);
    Bytes b(static_cast<std::size_t>(f.size()));
    f.read(0, b);
    return b;
}
void write_all(const std::filesystem::path &p, std::span<const std::byte> b) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(b.data()), static_cast<std::streamsize>(b.size()));
    REQUIRE(out.good());
}
void expect_error(const std::function<void()> &action, ErrorCode code) {
    bool caught = false;
    try {
        action();
    } catch (const Error &e) {
        caught = true;
        CHECK(e.code() == code);
    }
    CHECK(caught);
}
void seed(const std::filesystem::path &p) {
    DB db(p, buffered());
    db.put("a", "one");
    db.put("b", "two");
    db.put("a", "three");
    db.remove("b");
    db.sync();
}
void check_seed(DB &db) {
    CHECK(db.get_string("a") == "three");
    CHECK_FALSE(db.contains("b"));
    CHECK(db.verify().ok());
}
void inject(Fault target) {
    fault_hook = [target](Fault p) {
        if (p == target)
            throw Error(ErrorCode::io_error, "injected failure");
    };
}
} // namespace

TEST_CASE("empty database, first write, overwrite, missing and tombstones") {
    Temp t;
    {
        DB db(t.path);
        CHECK(db.stats().database_bytes == file_header_size);
        CHECK(db.stats().keys == 0);
        CHECK_FALSE(db.get("missing"));
        CHECK_FALSE(db.remove("missing"));
        db.put("foo", "one");
        db.put("foo", "two");
        CHECK(db.get_string("foo") == "two");
        CHECK(db.remove("foo"));
        CHECK_FALSE(db.remove("foo"));
        CHECK(db.stats().records == 3);
        CHECK(db.stats().tombstones == 1);
        CHECK(db.verify().ok());
    }
    DB reopened(t.path);
    CHECK_FALSE(reopened.get("foo"));
    CHECK(reopened.stats().tombstones == 1);
}
TEST_CASE("binary keys, binary values, empty values and persistence") {
    Temp t;
    const std::string key("a\0b", 3), value("\0\xff\n\r", 4);
    {
        DB db(t.path);
        db.put(key, value);
        db.put("empty", "");
    }
    DB db(t.path);
    CHECK(db.get_string(key) == value);
    REQUIRE(db.get("empty"));
    CHECK(db.get("empty")->empty());
}
TEST_CASE("thousands of random operations match a reference map across restarts") {
    Temp t;
    std::map<std::string, std::string> oracle;
    std::mt19937 rng(23017);
    for (int batch = 0; batch < 6; ++batch) {
        DB db(t.path, buffered());
        for (int n = 0; n < 700; ++n) {
            auto key = "k" + std::to_string(rng() % 251);
            if (rng() % 4 == 0)
                CHECK(db.remove(key) == (oracle.erase(key) != 0));
            else {
                auto value = std::to_string(rng());
                db.put(key, value);
                oracle[key] = value;
            }
        }
        if (batch % 2 == 0)
            db.compact();
        CHECK(db.stats().keys == oracle.size());
        for (const auto &[key, value] : oracle)
            CHECK(db.get_string(key) == value);
        CHECK(db.verify().ok());
    }
}
TEST_CASE("limits are validated before append and index budget is bounded") {
    Temp t;
    Options opts = buffered();
    opts.max_live_keys = 1;
    DB db(t.path, opts);
    expect_error([&] { db.put("", "value"); }, ErrorCode::invalid_argument);
    expect_error([&] { db.put(std::string(max_key_size + 1, 'k'), "value"); }, ErrorCode::invalid_argument);
    expect_error([&] { db.put("key", std::string(max_value_size + 1, 'v')); }, ErrorCode::invalid_argument);
    db.put(std::string(max_key_size, 'k'), "");
    expect_error([&] { db.put("second", "v"); }, ErrorCode::resource_limit);
    CHECK(db.stats().records == 1);
    db.remove(std::string(max_key_size, 'k'));
    db.put("second", "v");
    CHECK(db.verify().ok());
}
TEST_CASE("maximum value and bounded index bytes") {
    Temp t;
    {
        DB db(t.path, buffered());
        const std::string value(max_value_size, 'x');
        db.put("large", value);
        CHECK(db.get_string("large") == value);
    }
    Options o;
    o.max_index_bytes = 1;
    expect_error([&] { DB db(t.path, o); }, ErrorCode::resource_limit);
    CHECK(DB::inspect(t.path).ok());
}
TEST_CASE("CRC32C standard check vector") {
    std::string text = "123456789";
    CHECK(crc32c(std::as_bytes(std::span(text))) == 0xe3069283U);
    CHECK(crc32c({}) == 0U);
}
TEST_CASE("buffered scanning handles crossing headers, large records and incomplete tails") {
    struct Memory final : Reader {
        std::span<const std::byte> bytes;
        mutable std::size_t reads = 0, sizes = 0;
        explicit Memory(std::span<const std::byte> input) : bytes(input) {}
        std::uint64_t size() const override {
            ++sizes;
            return bytes.size();
        }
        void read(std::uint64_t offset, std::span<std::byte> into) const override {
            REQUIRE(offset <= bytes.size());
            REQUIRE(into.size() <= bytes.size() - offset);
            ++reads;
            std::copy_n(bytes.data() + offset, into.size(), into.data());
        }
    };
    Bytes bytes = file_header();
    const Bytes padding(1024 * 1024 - file_header_size - record_header_size - 3 - 16, std::byte{42});
    auto first = encode(Kind::put, 1, "pad", padding);
    bytes.insert(bytes.end(), first.begin(), first.end());
    const auto crossing = bytes.size();
    const Bytes large(1024 * 1024 + 37, std::byte{17});
    auto second = encode(Kind::put, 2, "large", large);
    bytes.insert(bytes.end(), second.begin(), second.end());
    for (std::uint64_t i = 3; i < 1003; ++i) {
        auto record = encode(Kind::put, i, "small", std::as_bytes(std::span("value", 5)));
        bytes.insert(bytes.end(), record.begin(), record.end());
    }
    Memory reader(bytes);
    auto state = scan(reader, {});
    REQUIRE(state.report.ok());
    CHECK(state.index.size() == 3);
    CHECK(state.index.at("large").offset == crossing);
    CHECK(reader.reads < 16);
    CHECK(reader.sizes == 1);
    bytes[crossing + record_header_size + 5 + 1024 * 1024] ^= std::byte{1};
    Memory damaged(bytes);
    auto report = scan(damaged, {}).report;
    REQUIRE(report.issue);
    CHECK(report.issue->offset == crossing);
    bytes[crossing + record_header_size + 5 + 1024 * 1024] ^= std::byte{1};
    bytes.resize(crossing + second.size() - 1);
    Memory truncated(bytes);
    report = scan(truncated, {}).report;
    CHECK(report.truncated_tail);
    CHECK_FALSE(report.issue);
    CHECK(report.valid_bytes == crossing);
}
TEST_CASE("CRC32C implementations match an independent bitwise reference at every alignment") {
    auto reference = [](std::span<const std::byte> bytes) {
        std::uint32_t crc = 0xffffffffU;
        for (const auto byte : bytes) {
            crc ^= std::to_integer<unsigned char>(byte);
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ ((crc & 1) ? 0x82f63b78U : 0U);
        }
        return ~crc;
    };
    Bytes data(1024 * 1024 + 16);
    std::mt19937 random(20261005);
    for (auto &byte : data)
        byte = std::byte(random() & 255);
    for (std::size_t offset = 0; offset < 16; ++offset) {
        for (std::size_t length = 0; length <= 256; ++length) {
            const auto bytes = std::span(data).subspan(offset, length);
            const auto expected = reference(bytes);
            CHECK(crc32c_portable(bytes) == expected);
            CHECK(crc32c(bytes) == expected);
        }
        for (const std::size_t length : {4095U, 4096U, 4097U, 65535U, 65536U, 1048576U}) {
            const auto bytes = std::span(data).subspan(offset, length);
            const auto expected = reference(bytes);
            CHECK(crc32c_portable(bytes) == expected);
            CHECK(crc32c(bytes) == expected);
        }
    }
}
TEST_CASE("all single-byte mutations of a complete file are rejected") {
    Temp t;
    seed(t.path);
    const auto original = read_all(t.path);
    for (std::size_t i = 0; i < original.size(); ++i) {
        CAPTURE(i);
        auto bytes = original;
        bytes[i] ^= std::byte{0x01};
        write_all(t.path, bytes);
        const auto before = read_all(t.path);
        CHECK_FALSE(DB::inspect(t.path).ok());
        CHECK(read_all(t.path) == before);
        CHECK_THROWS_AS(DB(t.path), Error);
        CHECK(read_all(t.path) == before);
    }
}
TEST_CASE("every truncation boundary is diagnosed and only incomplete records recover") {
    Temp t;
    {
        DB db(t.path, buffered());
        db.put("first", "safe");
        db.put("last", "payload");
    }
    const auto original = read_all(t.path);
    const auto first_end = file_header_size + record_header_size + 5 + 4;
    for (std::size_t cut = 0; cut <= original.size(); ++cut) {
        CAPTURE(cut);
        write_all(t.path, std::span(original).first(cut));
        const auto before = read_all(t.path);
        const auto report = DB::inspect(t.path);
        CHECK(read_all(t.path) == before);
        if (cut < file_header_size) {
            CHECK_FALSE(report.ok());
            CHECK_THROWS_AS(DB(t.path), Error);
            continue;
        }
        DB recovered(t.path);
        CHECK(recovered.verify().ok());
        CHECK(recovered.contains("first") == (cut >= first_end));
        CHECK(recovered.contains("last") == (cut == original.size()));
        recovered.put("after", "recovery");
        CHECK(recovered.verify().ok());
    }
}
TEST_CASE("strict recovery mode and junk tails never change the file") {
    Temp t;
    seed(t.path);
    auto bytes = read_all(t.path);
    auto partial = encode(Kind::put, 5, "new", {});
    bytes.insert(bytes.end(), partial.begin(), partial.begin() + 12);
    write_all(t.path, bytes);
    Options o;
    o.recover_truncated_tail = false;
    expect_error([&] { DB db(t.path, o); }, ErrorCode::corruption);
    CHECK(read_all(t.path) == bytes);
    bytes.back() = std::byte{1};
    bytes[bytes.size() - 12] = std::byte{'X'};
    write_all(t.path, bytes);
    expect_error([&] { DB db(t.path); }, ErrorCode::corruption);
    CHECK(read_all(t.path) == bytes);
}
TEST_CASE("malformed lengths, type, flags, sequence, versions and reserved fields") {
    Temp t;
    seed(t.path);
    const auto original = read_all(t.path);
    for (const auto &[field, value, count] :
         std::vector<std::tuple<std::size_t, std::uint64_t, std::size_t>>{{16, 0xffffffff, 4},
                                                                          {20, 0xffffffff, 4},
                                                                          {6, 9, 1},
                                                                          {7, 1, 1},
                                                                          {8, 0, 8},
                                                                          {28, 7, 4},
                                                                          {32, 1, 4},
                                                                          {4, 99, 2}}) {
        auto bytes = original;
        auto header = std::span(bytes).subspan(file_header_size, record_header_size);
        write_le(header, field, value, count);
        write_le(header, 36, crc32c(header.first(36)), 4);
        write_all(t.path, bytes);
        CHECK_FALSE(DB::inspect(t.path).ok());
        CHECK_THROWS_AS(DB(t.path), Error);
    }
}
TEST_CASE("a corrupted length in the middle cannot masquerade as a truncated tail") {
    Temp t;
    seed(t.path);
    auto bytes = read_all(t.path);
    bytes[file_header_size + 20] ^= std::byte{0x40};
    write_all(t.path, bytes);
    const auto report = DB::inspect(t.path);
    REQUIRE(report.issue);
    CHECK_FALSE(report.truncated_tail);
    CHECK(report.issue->offset == file_header_size);
    CHECK(report.issue->expected_crc.has_value());
    CHECK_THROWS_AS(DB(t.path), Error);
    CHECK(read_all(t.path) == bytes);
}
TEST_CASE("complete final checksum failure is corruption, not recoverable truncation") {
    Temp t;
    seed(t.path);
    auto bytes = read_all(t.path);
    bytes.back() ^= std::byte{8};
    write_all(t.path, bytes);
    CHECK_FALSE(DB::inspect(t.path).truncated_tail);
    CHECK_THROWS_AS(DB(t.path), Error);
    CHECK(read_all(t.path) == bytes);
}
TEST_CASE("compaction preserves latest values, drops dead records, and preserves sequence watermark") {
    Temp t;
    seed(t.path);
    std::uint64_t sequence = 0;
    {
        DB db(t.path);
        check_seed(db);
        const auto before = db.stats();
        db.compact();
        const auto after = db.stats();
        CHECK(after.database_bytes < before.database_bytes);
        CHECK(after.tombstones == 0);
        CHECK(after.records == 1);
        CHECK(after.reclaimable_bytes == 0);
        check_seed(db);
        CHECK(after.last_sequence == before.last_sequence);
        db.remove("a");
        sequence = db.stats().last_sequence;
        db.compact();
        CHECK(db.stats().database_bytes == file_header_size);
    }
    DB db(t.path);
    db.put("new", "value");
    CHECK(db.stats().last_sequence == sequence + 1);
    CHECK(db.verify().ok());
}
TEST_CASE("append failure recovery reflects the last completed write stage") {
    for (const auto point : {Fault::before_append, Fault::during_append, Fault::after_append,
                             Fault::before_flush, Fault::after_flush}) {
        Temp t;
        seed(t.path);
        {
            DB db(t.path);
            inject(point);
            CHECK_THROWS_AS(db.put("new", "value"), Error);
            fault_hook = {};
            if (point != Fault::before_append)
                expect_error([&] { db.put("bad", "continue"); }, ErrorCode::needs_reopen);
        }
        DB db(t.path);
        check_seed(db);
        CHECK(db.contains("new") == (point != Fault::before_append && point != Fault::during_append));
        db.put("next", "ok");
        CHECK(db.verify().ok());
    }
}
TEST_CASE("injected compaction failures preserve logical contents at every boundary") {
    for (const auto point :
         {Fault::compaction_start, Fault::temporary_write, Fault::temporary_validation,
          Fault::before_replacement, Fault::after_replacement, Fault::after_directory_sync}) {
        Temp t;
        seed(t.path);
        {
            DB db(t.path);
            inject(point);
            CHECK_THROWS_AS(db.compact(), Error);
            fault_hook = {};
        }
        {
            DB db(t.path);
            check_seed(db);
            db.put("new", "safe");
        }
        DB again(t.path);
        check_seed(again);
        CHECK(again.get_string("new") == "safe");
    }
}
TEST_CASE("missing or damaged primary recovers from a durable compaction backup") {
    for (bool missing : {false, true}) {
        Temp t;
        seed(t.path);
        {
            DB db(t.path);
            inject(Fault::after_replacement);
            CHECK_THROWS_AS(db.compact(), Error);
            fault_hook = {};
        }
        if (missing)
            std::filesystem::remove(t.path);
        else {
            auto b = read_all(t.path);
            b[0] ^= std::byte{1};
            write_all(t.path, b);
        }
        DB db(t.path);
        check_seed(db);
    }
}
TEST_CASE("a valid primary wins over an incomplete backup and stale temporary file") {
    Temp t;
    seed(t.path);
    const Bytes junk(5, std::byte{8});
    write_all(sibling(t.path, ".backup"), junk);
    write_all(sibling(t.path, ".compact"), junk);
    DB db(t.path);
    check_seed(db);
    CHECK_FALSE(detail::exists(sibling(t.path, ".backup")));
}
TEST_CASE("compaction preserves original after temporary corruption") {
    Temp t;
    seed(t.path);
    const auto original = read_all(t.path);
    {
        DB db(t.path);
        fault_hook = [&](Fault p) {
            if (p == Fault::temporary_validation) {
                File file(sibling(t.path, ".compact"), File::Mode::read_write);
                const std::array b{std::byte{'X'}};
                file.write(0, b);
            }
        };
        CHECK_THROWS_AS(db.compact(), Error);
        fault_hook = {};
    }
    CHECK(read_all(t.path) == original);
    DB db(t.path);
    check_seed(db);
}
TEST_CASE("locking rejects another handle, inspector, and normalized aliases") {
    Temp t;
    {
        DB db(t.path);
        expect_error([&] { DB other(t.path); }, ErrorCode::lock_conflict);
        expect_error([&] { DB other(t.dir / "." / "data.shdb"); }, ErrorCode::lock_conflict);
        expect_error([&] { (void)DB::inspect(t.path); }, ErrorCode::lock_conflict);
        db.compact();
        expect_error([&] { DB other(t.path); }, ErrorCode::lock_conflict);
    }
    CHECK(DB::inspect(t.path).ok());
}
TEST_CASE("missing and zero length files do not become empty valid databases") {
    Temp t;
    Options o;
    o.create_if_missing = false;
    expect_error([&] { DB db(t.path, o); }, ErrorCode::not_found);
    CHECK_FALSE(detail::exists(t.path));
    write_all(t.path, {});
    CHECK_THROWS_AS(DB(t.path), Error);
    CHECK(read_all(t.path).empty());
}
TEST_CASE("read detects changed payload and externally truncated database") {
    Temp t;
    DB db(t.path);
    db.put("key", "value");
    {
        File f(t.path, File::Mode::read_write);
        const std::array b{std::byte{0}};
        f.write(f.size() - 1, b);
    }
    expect_error([&] { (void)db.get("key"); }, ErrorCode::corruption);
    {
        File f(t.path, File::Mode::read_write);
        f.truncate(file_header_size);
    }
    expect_error([&] { (void)db.get("key"); }, ErrorCode::corruption);
    CHECK_THROWS_AS(db.put("another", "value"), Error);
}
TEST_CASE("reads reject valid replacement records that disagree with the open index") {
    for (const auto replacement : {0, 1, 2, 3}) {
        Temp t;
        DB db(t.path);
        db.put("key", "value");
        auto record = encode(
            Kind::put, replacement == 0 ? 2 : 1, replacement == 1 ? "bad" : "key",
            std::as_bytes(std::span(replacement == 2 ? "longer value" : "value", replacement == 2 ? 12 : 5)));
        auto bytes = file_header();
        bytes.insert(bytes.end(), record.begin(), record.end());
        if (replacement == 3)
            bytes.resize(bytes.size() - 1);
        write_all(t.path, bytes);
        expect_error([&] { (void)db.get("key"); }, ErrorCode::corruption);
    }
}
TEST_CASE("one DB serializes operations from multiple threads") {
    Temp t;
    DB db(t.path, buffered());
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back([&, i] {
            for (int n = 0; n < 100; ++n)
                db.put(std::to_string(i) + ":" + std::to_string(n), "value");
        });
    for (auto &thread : threads)
        thread.join();
    CHECK(db.stats().keys == 400);
    CHECK(db.verify().ok());
}
TEST_CASE("an unsupported primary is never overwritten by a compaction backup") {
    Temp t;
    seed(t.path);
    const auto backup = read_all(t.path);
    write_all(sibling(t.path, ".backup"), backup);
    auto future = backup;
    write_le(future, 8, 2, 2);
    write_le(future, 28, crc32c(std::span(future).first(28)), 4);
    write_all(t.path, future);
    expect_error([&] { DB db(t.path); }, ErrorCode::unsupported_format);
    CHECK(read_all(t.path) == future);
    CHECK(read_all(sibling(t.path, ".backup")) == backup);
}
TEST_CASE("a partial header with an old sequence is corruption and is never truncated") {
    Temp t;
    seed(t.path);
    auto bytes = read_all(t.path);
    auto old = encode(Kind::put, 2, "bad", {});
    bytes.insert(bytes.end(), old.begin(), old.begin() + 20);
    write_all(t.path, bytes);
    auto report = DB::inspect(t.path);
    REQUIRE(report.issue);
    CHECK_FALSE(report.truncated_tail);
    expect_error([&] { DB db(t.path); }, ErrorCode::corruption);
    CHECK(read_all(t.path) == bytes);
}
TEST_CASE("repeated compaction preserves binary and maximum-size keys and values") {
    Temp t;
    const std::string binary_key("binary\0key", 10);
    const std::string binary_value("\0\xff\n\r", 4);
    const std::string large_key(max_key_size, 'k');
    const std::string large_value(max_value_size, 'v');
    std::uint64_t before = 0;
    {
        DB db(t.path, buffered());
        for (int i = 0; i < 5000; ++i)
            db.put("hot", std::to_string(i));
        for (int i = 0; i < 2000; ++i) {
            const auto key = "dead" + std::to_string(i);
            db.put(key, "discard");
            CHECK(db.remove(key));
        }
        db.put(binary_key, binary_value);
        db.put(large_key, large_value);
        db.put("empty", "");
        before = db.stats().database_bytes;
        db.sync();
    }
    for (int iteration = 0; iteration < 3; ++iteration) {
        DB db(t.path);
        db.compact();
        CHECK(db.get_string("hot") == "4999");
        CHECK(db.get_string(binary_key) == binary_value);
        CHECK(db.get_string(large_key) == large_value);
        CHECK(db.get_string("empty") == "");
        CHECK_FALSE(db.contains("dead1999"));
        CHECK(db.stats().keys == 4);
        CHECK(db.stats().records == 4);
        CHECK(db.stats().tombstones == 0);
        CHECK(db.stats().database_bytes < before);
        CHECK(db.verify().ok());
    }
}
TEST_CASE("sequence exhaustion rejects writes and duplicate sequences reject opens") {
    Temp t;
    auto bytes = file_header(UINT64_MAX);
    write_all(t.path, bytes);
    {
        DB db(t.path);
        expect_error([&] { db.put("k", "v"); }, ErrorCode::resource_limit);
    }
    CHECK(read_all(t.path) == bytes);
    bytes = file_header();
    const auto record = encode(Kind::put, 1, "key", {});
    bytes.insert(bytes.end(), record.begin(), record.end());
    bytes.insert(bytes.end(), record.begin(), record.end());
    write_all(t.path, bytes);
    const auto report = DB::inspect(t.path);
    REQUIRE(report.issue);
    CHECK(report.issue->offset == file_header_size + record.size());
    CHECK_THROWS_AS(DB(t.path), Error);
}
#ifndef _WIN32
TEST_CASE("inspect rejects a FIFO without waiting for a producer") {
    Temp t;
    REQUIRE(::mkfifo(t.path.c_str(), 0600) == 0);
    const auto pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        ::alarm(2);
        try {
            (void)DB::inspect(t.path);
        } catch (const Error &e) {
            std::_Exit(e.code() == ErrorCode::invalid_argument ? 0 : 2);
        }
        std::_Exit(1);
    }
    int status = 0;
    REQUIRE(::waitpid(pid, &status, 0) == pid);
    REQUIRE(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
}
TEST_CASE("process termination at append and compaction boundaries recovers on POSIX") {
    for (const auto point :
         {Fault::before_append, Fault::during_append, Fault::after_append, Fault::before_flush,
          Fault::after_flush, Fault::compaction_start, Fault::temporary_write, Fault::temporary_validation,
          Fault::before_replacement, Fault::after_replacement, Fault::after_directory_sync}) {
        Temp t;
        seed(t.path);
        const auto pid = ::fork();
        REQUIRE(pid >= 0);
        if (pid == 0) {
            DB db(t.path);
            fault_hook = [point](Fault p) {
                if (p == point)
                    std::_Exit(77);
            };
            if (point <= Fault::after_flush)
                db.put("new", "value");
            else
                db.compact();
            std::_Exit(78);
        }
        int status = 0;
        REQUIRE(::waitpid(pid, &status, 0) == pid);
        REQUIRE(WIFEXITED(status));
        CHECK(WEXITSTATUS(status) == 77);
        DB db(t.path);
        check_seed(db);
        if (point == Fault::after_flush)
            CHECK(db.get_string("new") == "value");
        db.put("after-crash", "ok");
        CHECK(db.verify().ok());
    }
}
TEST_CASE("process lock and symlink aliases reject a second writer") {
    Temp t;
    DB db(t.path);
    auto alias = t.dir / "alias.shdb";
    std::filesystem::create_symlink(t.path, alias);
    expect_error([&] { DB other(alias); }, ErrorCode::lock_conflict);
    auto pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        try {
            DB other(t.path);
        } catch (const Error &e) {
            std::_Exit(e.code() == ErrorCode::lock_conflict ? 0 : 2);
        }
        std::_Exit(1);
    }
    int status = 0;
    REQUIRE(::waitpid(pid, &status, 0) == pid);
    CHECK(WEXITSTATUS(status) == 0);
}
TEST_CASE("hardlinked databases are rejected") {
    Temp t;
    seed(t.path);
    auto alias = t.dir / "hardlink.shdb";
    std::filesystem::create_hard_link(t.path, alias);
    expect_error([&] { DB db(t.path); }, ErrorCode::invalid_argument);
    expect_error([&] { DB db(alias); }, ErrorCode::invalid_argument);
}
#endif
