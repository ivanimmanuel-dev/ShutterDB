#pragma once
#include <array>
#include <functional>
#include <map>
#include <mutex>
#include <shutter/db.hpp>

namespace shutter::detail {
constexpr std::size_t file_header_size = 32, record_header_size = 40;
using Bytes = std::vector<std::byte>;
std::uint32_t crc32c(std::span<const std::byte> data) noexcept;
std::uint64_t read_le(std::span<const std::byte> bytes, std::size_t offset, std::size_t count);
void write_le(std::span<std::byte> bytes, std::size_t offset, std::uint64_t value, std::size_t count);
Bytes file_header(std::uint64_t sequence = 0);
std::uint64_t decode_file_header(std::span<const std::byte> bytes);
enum class Kind : std::uint8_t { put = 1, remove = 2 };
struct RecordHeader {
    Kind kind;
    std::uint64_t sequence;
    std::uint32_t key_size, value_size, payload_crc, total_size;
};
RecordHeader decode_header(std::span<const std::byte> bytes, std::uint64_t offset);
bool plausible_partial_header(std::span<const std::byte> bytes);
Bytes encode(Kind kind, std::uint64_t sequence, std::string_view key, std::span<const std::byte> value);
struct Reader {
    virtual ~Reader() = default;
    virtual std::uint64_t size() const = 0;
    virtual void read(std::uint64_t offset, std::span<std::byte> into) const = 0;
};
class File final : public Reader {
  public:
    enum class Mode { read_only, read_write, create_exclusive };
    File(const std::filesystem::path &path, Mode mode, bool lock_file = false);
    ~File() override;
    File(const File &) = delete;
    File &operator=(const File &) = delete;
    std::uint64_t size() const override;
    void read(std::uint64_t offset, std::span<std::byte> into) const override;
    void write(std::uint64_t offset, std::span<const std::byte> bytes);
    void truncate(std::uint64_t size);
    void sync();
    void lock(bool exclusive);

  private:
#ifdef _WIN32
    void *handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};
std::filesystem::path normalized(const std::filesystem::path &path);
std::filesystem::path sibling(const std::filesystem::path &path, const char *suffix);
void sync_directory(const std::filesystem::path &path);
void replace(const std::filesystem::path &source, const std::filesystem::path &target);
void remove_file(const std::filesystem::path &path);
bool exists(const std::filesystem::path &path);
void copy_durable(const File &source, const std::filesystem::path &target);
struct Entry {
    std::uint64_t offset, sequence;
    std::uint32_t total_size, value_size;
};
using Index = std::map<std::string, Entry, std::less<>>;
struct Scan {
    VerifyReport report;
    Index index;
    std::uint64_t index_bytes = 0;
};
Scan scan(const Reader &reader, const Options &options);
Bytes payload(const Reader &reader, const RecordHeader &header, std::uint64_t offset);
void require_valid(const VerifyReport &report, bool allow_tail = false);
void validate_key(std::string_view key);
enum class Fault {
    before_append,
    during_append,
    after_append,
    before_flush,
    after_flush,
    compaction_start,
    temporary_write,
    temporary_validation,
    before_replacement,
    after_replacement,
    after_directory_sync
};
#ifdef SHUTTER_TESTING
extern thread_local std::function<void(Fault)> fault_hook;
void hit(Fault point);
#else
inline void hit(Fault) {}
#endif
} // namespace shutter::detail

namespace shutter {
struct DB::Impl {
    std::filesystem::path path;
    Options options;
    std::unique_ptr<detail::File> lock_file, file;
    detail::Scan state;
    bool poisoned = false;
    mutable std::mutex mutex;
    Impl(const std::filesystem::path &path, const Options &options);
    void ensure_usable() const;
    void append(detail::Kind kind, std::string_view key, std::span<const std::byte> value);
    void compact();
    void recover_compaction();
};
} // namespace shutter
