// Linux test-only syscall interposer. Loaded only into disposable CLI subprocesses.
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
enum class Action { normal, fail, short_io };
Action action(const char *operation, unsigned &calls) {
    ++calls;
    const char *selected = std::getenv("SHUTTER_IO_OPERATION");
    const char *ordinal = std::getenv("SHUTTER_IO_CALL");
    if (!selected || !ordinal || std::strcmp(selected, operation) ||
        calls != std::strtoul(ordinal, nullptr, 10))
        return Action::normal;
    const char *mode = std::getenv("SHUTTER_IO_MODE");
    if (mode && !std::strcmp(mode, "short"))
        return Action::short_io;
    errno = mode && !std::strcmp(mode, "eintr") ? EINTR : mode && !std::strcmp(mode, "enospc") ? ENOSPC : EIO;
    return Action::fail;
}
template <class Function> Function next(const char *name) {
    auto fn = reinterpret_cast<Function>(::dlsym(RTLD_NEXT, name));
    if (!fn)
        std::_Exit(96);
    return fn;
}
} // namespace
extern "C" ssize_t pwrite(int fd, const void *data, size_t count, off_t offset) {
    static auto real = next<ssize_t (*)(int, const void *, size_t, off_t)>("pwrite");
    static unsigned calls = 0;
    const auto effect = action("pwrite", calls);
    if (effect == Action::fail)
        return -1;
    if (effect == Action::short_io && count > 3)
        count = 3;
    return real(fd, data, count, offset);
}
extern "C" ssize_t pread(int fd, void *data, size_t count, off_t offset) {
    static auto real = next<ssize_t (*)(int, void *, size_t, off_t)>("pread");
    static unsigned calls = 0;
    const auto effect = action("pread", calls);
    if (effect == Action::fail)
        return -1;
    if (effect == Action::short_io && count > 3)
        count = 3;
    return real(fd, data, count, offset);
}
extern "C" int fsync(int fd) {
    static auto real = next<int (*)(int)>("fsync");
    static unsigned calls = 0;
    if (action("fsync", calls) == Action::fail)
        return -1;
    return real(fd);
}
extern "C" int ftruncate(int fd, off_t size) {
    static auto real = next<int (*)(int, off_t)>("ftruncate");
    static unsigned calls = 0;
    if (action("ftruncate", calls) == Action::fail)
        return -1;
    return real(fd, size);
}
extern "C" int rename(const char *source, const char *target) {
    static auto real = next<int (*)(const char *, const char *)>("rename");
    static unsigned calls = 0;
    if (action("rename", calls) == Action::fail)
        return -1;
    return real(source, target);
}
