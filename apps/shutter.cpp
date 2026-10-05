#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <shutter/db.hpp>
#include <sstream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {
std::string json(std::string_view text) {
    std::ostringstream out;
    out << '"';
    for (char raw_char : text) {
        const auto c = static_cast<unsigned char>(raw_char);
        if (c == '"' || c == '\\')
            out << '\\' << static_cast<char>(c);
        else if (c < 0x20 || c >= 0x7f)
            out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
        else
            out << static_cast<char>(c);
    }
    out << '"';
    return out.str();
}
std::string hex(std::span<const std::byte> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (auto b : bytes) {
        const auto c = std::to_integer<unsigned>(b);
        result += digits[c >> 4];
        result += digits[c & 15];
    }
    return result;
}
void stats_json(const shutter::Stats &s) {
    std::cout << "\"keys\":" << s.keys << ",\"records\":" << s.records << ",\"put_records\":" << s.put_records
              << ",\"tombstones\":" << s.tombstones << ",\"database_bytes\":" << s.database_bytes
              << ",\"live_bytes\":" << s.live_bytes << ",\"reclaimable_bytes\":" << s.reclaimable_bytes
              << ",\"last_sequence\":" << s.last_sequence << ",\"format_version\":" << s.format_version;
}
void stats_text(const shutter::Stats &s) {
    std::cout << "ShutterDB\n\nKeys                " << s.keys << "\nRecords             " << s.records
              << "\nPUT records         " << s.put_records << "\nTombstones          " << s.tombstones
              << "\nDatabase bytes      " << s.database_bytes << "\nLive record bytes   " << s.live_bytes
              << "\nReclaimable bytes   " << s.reclaimable_bytes << "\nLast sequence       "
              << s.last_sequence << "\nFormat              v1\n";
}
void usage() {
    std::cout << "ShutterDB " << shutter::version << "\n\n"
              << "Usage:\n"
                 "  shutter init FILE\n"
                 "  shutter set KEY VALUE [--db FILE]\n"
                 "  shutter set KEY --value-file INPUT [--db FILE]\n"
                 "  shutter get KEY [--db FILE]\n"
                 "  shutter delete KEY [--db FILE]\n"
                 "  shutter stats [--db FILE]\n"
                 "  shutter verify [--db FILE]\n"
                 "  shutter compact [--db FILE]\n"
                 "  shutter recover [--db FILE]\n"
                 "  shutter version\n\n"
                 "Options:\n"
                 "  --db FILE     Database path (default: app.shdb)\n"
                 "  --buffered    Skip per-write synchronization for set or delete\n"
                 "  --raw         Write value bytes without a newline (get)\n"
                 "  --json        Print JSON results\n"
                 "  --            Treat remaining arguments as literal keys or values\n\n"
                 "verify scans the file. recover truncates an incomplete final record.\n"
                 "Exit codes: 0 success, 1 missing key, 2 usage, 3 runtime, 4 corruption, 5 locked.\n";
}
} // namespace
int main(int argc, char **argv) {
    bool as_json = false;
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--json")
            as_json = true;
    try {
        if (argc < 2) {
            usage();
            return 2;
        }
        std::string command = argv[1];
        if (command == "--help" || command == "help") {
            usage();
            return 0;
        }
        if (command == "version") {
            std::cout << "ShutterDB " << shutter::version << '\n';
            return 0;
        }
        std::filesystem::path path = "app.shdb", value_file;
        bool raw = false, buffered = false, literal = false;
        std::vector<std::string> positional;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (!literal && arg == "--") {
                literal = true;
                continue;
            }
            if (!literal && (arg == "--db" || arg == "--value-file")) {
                if (++i == argc)
                    throw shutter::Error(shutter::ErrorCode::invalid_argument, "missing option value");
                if (arg == "--db")
                    path = argv[i];
                else
                    value_file = argv[i];
            } else if (!literal && arg == "--json")
                as_json = true;
            else if (!literal && arg == "--raw")
                raw = true;
            else if (!literal && arg == "--buffered")
                buffered = true;
            else if (!literal && arg.starts_with("--"))
                throw shutter::Error(shutter::ErrorCode::invalid_argument, "unknown option: " + arg);
            else
                positional.push_back(arg);
        }
        auto arity = [&](std::size_t n) {
            if (positional.size() != n)
                throw shutter::Error(shutter::ErrorCode::invalid_argument,
                                     "wrong number of arguments; run shutter --help");
        };
        if (raw && (as_json || command != "get"))
            throw shutter::Error(shutter::ErrorCode::invalid_argument, "--raw requires get without --json");
        if (!value_file.empty() && command != "set")
            throw shutter::Error(shutter::ErrorCode::invalid_argument, "--value-file requires set");
        if (buffered && command != "set" && command != "delete")
            throw shutter::Error(shutter::ErrorCode::invalid_argument, "--buffered requires set or delete");
        if (command == "init") {
            arity(1);
            path = positional[0];
        } else if (command == "set")
            arity(value_file.empty() ? 2 : 1);
        else if (command == "get" || command == "delete")
            arity(1);
        else if (command == "stats" || command == "verify" || command == "compact" || command == "recover")
            arity(0);
        else
            throw shutter::Error(shutter::ErrorCode::invalid_argument, "unknown command: " + command);
        if (command == "verify" || command == "stats") {
            const auto report = shutter::DB::inspect(path);
            if (as_json) {
                std::cout << "{\"ok\":" << (report.ok() ? "true" : "false")
                          << ",\"header_valid\":" << (report.header_valid ? "true" : "false")
                          << ",\"truncated_tail\":" << (report.truncated_tail ? "true" : "false")
                          << ",\"valid_bytes\":" << report.valid_bytes << ',';
                stats_json(report.stats);
                if (report.issue) {
                    const auto &issue = *report.issue;
                    std::cout << ",\"error\":" << json(shutter::error_name(issue.code))
                              << ",\"message\":" << json(issue.message) << ",\"offset\":" << issue.offset;
                    if (issue.expected_crc)
                        std::cout << ",\"expected_crc32c\":" << *issue.expected_crc
                                  << ",\"actual_crc32c\":" << *issue.actual_crc;
                } else if (report.truncated_tail) {
                    std::cout
                        << ",\"error\":\"CORRUPTION\",\"message\":\"incomplete final record\",\"offset\":"
                        << report.valid_bytes;
                }
                std::cout << "}\n";
            } else {
                stats_text(report.stats);
                std::cout << '\n' << (report.ok() ? "Verification passed\n" : "Verification failed\n");
                if (report.truncated_tail)
                    std::cout << "Incomplete tail at byte " << report.valid_bytes
                              << ". Use shutter recover after making a copy.\n";
                if (report.issue) {
                    const auto &issue = *report.issue;
                    std::cout << shutter::error_name(issue.code) << " at byte " << issue.offset << ": "
                              << issue.message << '\n';
                    if (issue.expected_crc)
                        std::cout << "Expected CRC32C: " << *issue.expected_crc
                                  << "; actual: " << *issue.actual_crc << '\n';
                }
            }
            if (report.issue && report.issue->code != shutter::ErrorCode::corruption &&
                report.issue->code != shutter::ErrorCode::unsupported_format)
                return 3;
            return report.ok() ? 0 : 4;
        }
        shutter::Options options;
        options.create_if_missing = command == "init";
        options.recover_truncated_tail = command == "recover";
        options.sync_writes = !buffered;
        shutter::DB db(path, options);
        if (command == "set") {
            std::string value;
            if (value_file.empty())
                value = positional[1];
            else {
                std::ifstream in(value_file, std::ios::binary);
                if (!in)
                    throw shutter::Error(shutter::ErrorCode::io_error, "cannot open value file");
                std::array<char, 65536> block{};
                while (in) {
                    in.read(block.data(), block.size());
                    value.append(block.data(), static_cast<std::size_t>(in.gcount()));
                    if (value.size() > shutter::max_value_size)
                        throw shutter::Error(shutter::ErrorCode::invalid_argument, "value exceeds 16 MiB");
                }
                if (!in.eof())
                    throw shutter::Error(shutter::ErrorCode::io_error, "value file read failed");
            }
            db.put(positional[0], value);
        } else if (command == "get") {
            auto value = db.get(positional[0]);
            if (!value) {
                if (as_json)
                    std::cout << "{\"found\":false}\n";
                return 1;
            }
            if (as_json)
                std::cout << "{\"found\":true,\"encoding\":\"hex\",\"value\":" << json(hex(*value)) << "}\n";
            else {
#ifdef _WIN32
                if (raw)
                    _setmode(_fileno(stdout), _O_BINARY);
#endif
                if (!value->empty())
                    std::cout.write(reinterpret_cast<const char *>(value->data()),
                                    static_cast<std::streamsize>(value->size()));
                if (!raw)
                    std::cout << '\n';
            }
            return std::cout ? 0 : 3;
        } else if (command == "delete") {
            if (!db.remove(positional[0])) {
                if (as_json)
                    std::cout << "{\"removed\":false}\n";
                return 1;
            }
        } else if (command == "compact")
            db.compact();
        if (as_json)
            std::cout << "{\"ok\":true,\"recovered_tail_bytes\":" << db.stats().recovered_tail_bytes << "}\n";
        else if (command == "recover")
            std::cout << "Removed " << db.stats().recovered_tail_bytes << " bytes from incomplete tail\n";
        return 0;
    } catch (const shutter::Error &e) {
        if (as_json)
            std::cerr << "{\"error\":" << json(shutter::error_name(e.code()))
                      << ",\"message\":" << json(e.what()) << ",\"offset\":" << e.offset() << "}\n";
        else
            std::cerr << shutter::error_name(e.code()) << ": " << e.what() << " (offset " << e.offset()
                      << ")\n";
        switch (e.code()) {
        case shutter::ErrorCode::invalid_argument:
            return 2;
        case shutter::ErrorCode::corruption:
        case shutter::ErrorCode::unsupported_format:
            return 4;
        case shutter::ErrorCode::lock_conflict:
            return 5;
        default:
            return 3;
        }
    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 3;
    }
}
