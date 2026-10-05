#include "internal.hpp"
#include <algorithm>
#include <cerrno>
#include <limits>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace shutter::detail {
namespace {
[[noreturn]] void io_error(const char *operation) {
#ifdef _WIN32
    const auto code = GetLastError();
    auto category = code == ERROR_ACCESS_DENIED ? ErrorCode::permission_denied
                    : code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND ? ErrorCode::not_found
                                                                                   : ErrorCode::io_error;
    throw Error(category,
                std::string(operation) + ": " + std::system_category().message(static_cast<int>(code)));
#else
    const auto code = errno;
    auto category = code == EACCES || code == EPERM ? ErrorCode::permission_denied
                    : code == ENOENT                ? ErrorCode::not_found
                                                    : ErrorCode::io_error;
    throw Error(category, std::string(operation) + ": " + std::generic_category().message(code));
#endif
}
void check_offset(std::uint64_t offset, std::size_t length = 0) {
    constexpr auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (offset > limit || length > limit - offset)
        throw Error(ErrorCode::resource_limit, "file offset exceeds signed 64-bit range", offset);
}
} // namespace
File::File(const std::filesystem::path &path, Mode mode, bool lock_file) {
#ifdef _WIN32
    auto access = GENERIC_READ | (mode == Mode::read_only ? 0 : GENERIC_WRITE);
    auto creation = lock_file ? OPEN_ALWAYS : mode == Mode::create_exclusive ? CREATE_NEW : OPEN_EXISTING;
    handle_ = CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE)
        io_error("open");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle_, &info)) {
        auto error = GetLastError();
        CloseHandle(handle_);
        SetLastError(error);
        io_error("file info");
    }
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || info.nNumberOfLinks > 1) {
        CloseHandle(handle_);
        throw Error(ErrorCode::invalid_argument,
                    "database and sidecars must be regular files without hard links");
    }
#else
    int flags = mode == Mode::read_only ? O_RDONLY : O_RDWR;
    // Opening a FIFO for inspection must not block before the regular-file check.
    // O_NONBLOCK has no effect on the regular files we accept.
    flags |= O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
    if (lock_file)
        flags |= O_CREAT;
    else if (mode == Mode::create_exclusive)
        flags |= O_CREAT | O_EXCL;
    fd_ = ::open(path.c_str(), flags, 0600);
    if (fd_ < 0)
        io_error("open");
    struct stat info{};
    if (::fstat(fd_, &info) < 0) {
        const auto error = errno;
        ::close(fd_);
        errno = error;
        io_error("fstat");
    }
    if (!S_ISREG(info.st_mode) || info.st_nlink > 1) {
        ::close(fd_);
        throw Error(ErrorCode::invalid_argument,
                    "database and sidecars must be regular files without hard links");
    }
#endif
}
File::~File() {
#ifdef _WIN32
    if (handle_ && handle_ != INVALID_HANDLE_VALUE)
        CloseHandle(handle_);
#else
    if (fd_ >= 0)
        ::close(fd_);
#endif
}
std::uint64_t File::size() const {
#ifdef _WIN32
    LARGE_INTEGER n{};
    if (!GetFileSizeEx(handle_, &n))
        io_error("file size");
    return static_cast<std::uint64_t>(n.QuadPart);
#else
    struct stat info{};
    if (::fstat(fd_, &info) < 0)
        io_error("fstat");
    return static_cast<std::uint64_t>(info.st_size);
#endif
}
void File::read(std::uint64_t offset, std::span<std::byte> into) const {
    check_offset(offset, into.size());
    while (!into.empty()) {
        const auto chunk = std::min<std::size_t>(into.size(), 1024 * 1024);
#ifdef _WIN32
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN))
            io_error("seek");
        DWORD n = 0;
        if (!ReadFile(handle_, into.data(), static_cast<DWORD>(chunk), &n, nullptr))
            io_error("read");
#else
        auto n = ::pread(fd_, into.data(), chunk, static_cast<off_t>(offset));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            io_error("pread");
        }
#endif
        if (n == 0)
            throw Error(ErrorCode::corruption, "unexpected end of file", offset);
        offset += static_cast<std::uint64_t>(n);
        into = into.subspan(static_cast<std::size_t>(n));
    }
}
void File::write(std::uint64_t offset, std::span<const std::byte> bytes) {
    check_offset(offset, bytes.size());
    while (!bytes.empty()) {
        const auto chunk = std::min<std::size_t>(bytes.size(), 1024 * 1024);
#ifdef _WIN32
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN))
            io_error("seek");
        DWORD n = 0;
        if (!WriteFile(handle_, bytes.data(), static_cast<DWORD>(chunk), &n, nullptr))
            io_error("write");
#else
        auto n = ::pwrite(fd_, bytes.data(), chunk, static_cast<off_t>(offset));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            io_error("pwrite");
        }
#endif
        if (n == 0)
            throw Error(ErrorCode::io_error, "write made no progress", offset);
        offset += static_cast<std::uint64_t>(n);
        bytes = bytes.subspan(static_cast<std::size_t>(n));
    }
}
void File::truncate(std::uint64_t length) {
    check_offset(length);
#ifdef _WIN32
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(length);
    if (!SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN) || !SetEndOfFile(handle_))
        io_error("truncate");
#else
    if (::ftruncate(fd_, static_cast<off_t>(length)) < 0)
        io_error("ftruncate");
#endif
}
void File::sync() {
#ifdef _WIN32
    if (!FlushFileBuffers(handle_))
        io_error("FlushFileBuffers");
#else
    while (::fsync(fd_) < 0) {
        if (errno != EINTR)
            io_error("fsync");
    }
#endif
}
void File::lock(bool exclusive) {
#ifdef _WIN32
    OVERLAPPED overlap{};
    DWORD flags = LOCKFILE_FAIL_IMMEDIATELY | (exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0);
    if (!LockFileEx(handle_, flags, 0, 1, 0, &overlap)) {
        if (GetLastError() == ERROR_LOCK_VIOLATION)
            throw Error(ErrorCode::lock_conflict, "database is already open");
        io_error("LockFileEx");
    }
#else
    while (::flock(fd_, (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB) < 0) {
        if (errno == EINTR)
            continue;
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            throw Error(ErrorCode::lock_conflict, "database is already open");
        io_error("flock");
    }
#endif
}
std::filesystem::path normalized(const std::filesystem::path &path) {
    if (path.empty())
        throw Error(ErrorCode::invalid_argument, "empty database path");
    std::error_code ec;
    auto result = std::filesystem::weakly_canonical(std::filesystem::absolute(path, ec), ec);
    if (ec)
        throw Error(ErrorCode::io_error, "resolve database path: " + ec.message());
    return result;
}
std::filesystem::path sibling(const std::filesystem::path &path, const char *suffix) {
    auto result = path;
    result += suffix;
    return result;
}
bool exists(const std::filesystem::path &path) {
    std::error_code ec;
    const bool found = std::filesystem::exists(path, ec);
    if (ec)
        throw Error(ErrorCode::io_error, "file status: " + ec.message());
    return found;
}
void remove_file(const std::filesystem::path &path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec)
        throw Error(ErrorCode::io_error, "remove sidecar: " + ec.message());
}
void replace(const std::filesystem::path &source, const std::filesystem::path &target) {
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        io_error("replace");
#else
    if (::rename(source.c_str(), target.c_str()) < 0)
        io_error("rename");
#endif
}
void sync_directory(const std::filesystem::path &path) {
#ifdef _WIN32
    (void)path; // Windows has no equivalent portable directory fsync contract.
#else
    const int fd = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        io_error("open parent directory");
    int status;
    do {
        status = ::fsync(fd);
    } while (status < 0 && errno == EINTR);
    const int error = errno;
    ::close(fd);
    if (status < 0) {
        errno = error;
        io_error("sync parent directory");
    }
#endif
}
void copy_durable(const File &source, const std::filesystem::path &target) {
    File copy(target, File::Mode::create_exclusive);
    const auto n = source.size();
    Bytes buffer(64 * 1024);
    for (std::uint64_t offset = 0; offset < n;) {
        auto chunk = std::span(buffer).first(
            static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), n - offset)));
        source.read(offset, chunk);
        copy.write(offset, chunk);
        offset += chunk.size();
    }
    copy.sync();
}
} // namespace shutter::detail
