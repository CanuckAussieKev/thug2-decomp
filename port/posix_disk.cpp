// NOT ORIGINAL - the macOS (arm64) port's file backend: fs::Disk (engine/file/disk.h) over POSIX. It stands in for
// src/fs_win32/win32_disk.cpp, which is the same interface over Win32's file calls; the methods here are the POSIX
// calls with the same meaning, and the results keep the Win32 shapes the game reads (INVALID_HANDLE_VALUE's all-ones
// pointer, the Win32 error numbers in last_error, FILETIME's 100 ns since 1601).
//
// The overlapped reads are done at once, at the OVERLAPPED block's offset (pread): nothing is left pending, so a poll
// of one always finds it done - the game's asynchronous file layer is correct with that, only slower to fill.

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include "engine/file/disk.h"
#include "port/posix_disk.h"
#include "port/win32_shim/windows.h"

namespace fs {

// The disk: POSIX's own calls behind fs::Disk.
class PosixDisk final : public Disk {
public:
    DiskFile* open(const char* path, OpenSpec spec) override;
    bool close(DiskFile* file) override;
    bool read(DiskFile* file, void* buffer, uint32_t length, uint32_t* bytes, void* overlapped) override;
    bool write(DiskFile* file, const void* data, uint32_t length, uint32_t* written) override;
    uint32_t seek(DiskFile* file, int32_t offset, Origin origin) override;
    bool seek_to(DiskFile* file, uint64_t offset) override;
    uint32_t size(DiskFile* file) override;
    bool size64(DiskFile* file, uint64_t* size) override;
    bool flush(DiskFile* file) override;
    bool overlapped_result(DiskFile* file, void* overlapped, uint32_t* bytes, bool wait) override;
    uint32_t last_error() override;
    DiskStream* open_stream(const char* path, const char* mode, StreamSharing sharing) override;
    size_t stream_read(DiskStream* stream, void* buffer, size_t size, size_t count) override;
    size_t stream_write(DiskStream* stream, const void* data, size_t size, size_t count) override;
    int stream_seek(DiskStream* stream, long offset, int origin) override;
    long stream_tell(DiskStream* stream) override;
    int stream_flush(DiskStream* stream) override;
    int stream_close(DiskStream* stream) override;
    PathKind kind(const char* path) override;
    bool stat(const char* path, FileStat* stat) override;
    bool local_time(uint64_t write_time, LocalTime* local) override;
    bool make_folder(const char* path) override;
    bool make_folders(const char* path) override;
    bool remove(const char* path) override;
    bool replace(const char* from, const char* to, bool write_through) override;
    bool list(const char* pattern, FoundVisitor& visitor) override;
    bool free_space(uint64_t* available) override;
};

// A file opened by path: the descriptor and the access it was opened with.
struct DiskFile {
    int fd_ = -1;
    Access access_ = Access::kRead;
};

namespace {

int g_last_error = 0;

// A path as this platform spells it: the game's paths are Win32's - "Data\pre\qb_scripts.prx" - and a backslash is
// not a separator here. The same length every time (a backslash is one byte and a slash is one), so a thread's own
// buffer is enough and nothing is allocated on the open path.
struct PosixPath {
    char text[4096];
    explicit PosixPath(const char* path)
    {
        std::size_t at = 0;
        // (a relative path is the game folder's: Win32 games open "Data\pre\..." against their working directory, and
        // a file manager starts this program in "/" - the game's folder is the one the exe is in.)
        if (path != nullptr && path[0] != '/' && path[0] != '\0') {
            const char* folder = port_game_directory();
            for (std::size_t i = 0; folder[i] != '\0' && at + 2 < sizeof(text); ++i) {
                text[at++] = folder[i];
            }
            if (at > 0 && text[at - 1] != '/') {
                text[at++] = '/';
            }
        }
        if (path != nullptr) {
            for (std::size_t i = 0; path[i] != '\0' && at + 1 < sizeof(text); ++i) {
                text[at++] = path[i] == '\\' ? '/' : path[i];
            }
        }
        text[at] = '\0';
    }
    [[nodiscard]] const char* c_str() const { return text; }
};

// POSIX's errno in Win32's numbers (the game tests them).
int win32_error(int error)
{
    switch (error) {
    case 0:
        return 0;
    case ENOENT:
        return 2; // ERROR_FILE_NOT_FOUND
    case EACCES:
    case EPERM:
        return 5; // ERROR_ACCESS_DENIED
    case EBADF:
        return 6; // ERROR_INVALID_HANDLE
    case EEXIST:
        return 183; // ERROR_ALREADY_EXISTS
    case ENOSPC:
        return 112; // ERROR_DISK_FULL
    case ENOTEMPTY:
        return 145; // ERROR_DIR_NOT_EMPTY
    case EISDIR:
        return 5;
    case ENOTDIR:
        return 267; // ERROR_DIRECTORY
    default:
        return 1; // ERROR_INVALID_FUNCTION
    }
}

// A Win32 wildcard match: '*' any run, '?' one character, the rest literal, without case.
bool matches(const char* mask, const char* name)
{
    const char* m = mask;
    const char* n = name;
    const char* star = nullptr;
    const char* retry = nullptr;
    while (*n != '\0') {
        const unsigned char a = static_cast<unsigned char>(*m);
        const unsigned char b = static_cast<unsigned char>(*n);
        if (*m == '?' || (*m != '\0' && *m != '*' &&
                          static_cast<unsigned char>(tolower(a)) == static_cast<unsigned char>(tolower(b)))) {
            ++m;
            ++n;
        } else if (*m == '*') {
            star = m++;
            retry = n;
        } else if (star != nullptr) {
            m = star + 1;
            n = ++retry;
        } else {
            return false;
        }
    }
    while (*m == '*') {
        ++m;
    }
    return *m == '\0';
}

// FILETIME's own unit: 100 ns since 1601.
uint64_t file_time(std::int64_t seconds)
{
    constexpr uint64_t kWindowsEpoch = 11644473600ULL; // 1601 to 1970, in seconds
    constexpr uint64_t kTicksPerSecond = 10000000ULL;
    if (seconds <= 0) {
        return 0;
    }
    return (static_cast<uint64_t>(seconds) + kWindowsEpoch) * kTicksPerSecond;
}

// The directory and the mask of a Win32 pattern ("folder\*.qb" / "folder/*.qb").
void split_pattern(const char* pattern, char* directory, size_t directory_size, char* mask, size_t mask_size)
{
    const char* slash = std::strrchr(pattern, '/');
    const char* backslash = std::strrchr(pattern, '\\');
    if (backslash != nullptr && (slash == nullptr || backslash > slash)) {
        slash = backslash;
    }
    if (slash == nullptr) {
        std::snprintf(directory, directory_size, ".");
        std::snprintf(mask, mask_size, "%s", pattern);
        return;
    }
    const size_t count = static_cast<size_t>(slash - pattern);
    std::snprintf(directory, directory_size, "%.*s", static_cast<int>(count == 0 ? 1 : count), pattern);
    std::snprintf(mask, mask_size, "%s", slash + 1);
    if (mask[0] == '\0') {
        std::snprintf(mask, mask_size, "*");
    }
}

} // namespace

DiskFile* PosixDisk::open(const char* path, OpenSpec spec)
{
    int flags = 0;
    switch (spec.access) {
    case Access::kRead:
        flags = O_RDONLY;
        break;
    case Access::kWrite:
        flags = O_WRONLY;
        break;
    case Access::kReadWrite:
        flags = O_RDWR;
        break;
    }
    switch (spec.disposition) {
    case Disposition::kOpenExisting:
        break;
    case Disposition::kCreateNew:
        flags |= O_CREAT | O_EXCL;
        break;
    case Disposition::kCreateAlways:
        flags |= O_CREAT | O_TRUNC;
        break;
    case Disposition::kOpenAlways:
        flags |= O_CREAT;
        break;
    }
    const int fd = ::open(PosixPath(path).c_str(), flags, 0644);
    if (fd < 0) {
        g_last_error = win32_error(errno);
        return no_file();
    }
    auto* file = new DiskFile();
    file->fd_ = fd;
    file->access_ = spec.access;
    return file;
}

bool PosixDisk::close(DiskFile* file)
{
    if (file == nullptr || file == no_file()) {
        return false;
    }
    const bool closed = ::close(file->fd_) == 0;
    delete file;
    return closed;
}

bool PosixDisk::read(DiskFile* file, void* buffer, uint32_t length, uint32_t* bytes, void* overlapped)
{
    if (file == nullptr || file == no_file()) {
        g_last_error = 6;
        return false;
    }
    auto* block = static_cast<OVERLAPPED*>(overlapped);
    ssize_t got = 0;
    if (block != nullptr) {
        const off_t offset =
            static_cast<off_t>(static_cast<uint64_t>(block->Offset) | (static_cast<uint64_t>(block->OffsetHigh) << 32));
        got = pread(file->fd_, buffer, length, offset);
        if (got >= 0) {
            block->Internal = 0; // STATUS_SUCCESS
            block->InternalHigh = static_cast<ULONG_PTR>(got);
        }
    } else {
        got = ::read(file->fd_, buffer, length);
    }
    if (got < 0) {
        g_last_error = win32_error(errno);
        return false;
    }
    if (bytes != nullptr) {
        *bytes = static_cast<uint32_t>(got);
    }
    return true;
}

bool PosixDisk::write(DiskFile* file, const void* data, uint32_t length, uint32_t* written)
{
    if (file == nullptr || file == no_file()) {
        g_last_error = 6;
        return false;
    }
    const ssize_t put = ::write(file->fd_, data, length);
    if (put < 0) {
        g_last_error = win32_error(errno);
        return false;
    }
    if (written != nullptr) {
        *written = static_cast<uint32_t>(put);
    }
    return true;
}

uint32_t PosixDisk::seek(DiskFile* file, int32_t offset, Origin origin)
{
    if (file == nullptr || file == no_file()) {
        return 0xFFFFFFFFU; // INVALID_SET_FILE_POINTER
    }
    const int whence = origin == Origin::kBegin ? SEEK_SET : (origin == Origin::kCurrent ? SEEK_CUR : SEEK_END);
    const off_t at = lseek(file->fd_, offset, whence);
    return at < 0 ? 0xFFFFFFFFU : static_cast<uint32_t>(at);
}

bool PosixDisk::seek_to(DiskFile* file, uint64_t offset)
{
    if (file == nullptr || file == no_file()) {
        return false;
    }
    return lseek(file->fd_, static_cast<off_t>(offset), SEEK_SET) >= 0;
}

uint32_t PosixDisk::size(DiskFile* file)
{
    uint64_t bytes = 0;
    return size64(file, &bytes) ? static_cast<uint32_t>(bytes) : 0;
}

bool PosixDisk::size64(DiskFile* file, uint64_t* size)
{
    if (file == nullptr || file == no_file() || size == nullptr) {
        return false;
    }
    struct stat info;
    if (fstat(file->fd_, &info) != 0) {
        g_last_error = win32_error(errno);
        return false;
    }
    *size = static_cast<uint64_t>(info.st_size);
    return true;
}

bool PosixDisk::flush(DiskFile* file) { return file != nullptr && file != no_file() && fsync(file->fd_) == 0; }

bool PosixDisk::overlapped_result(DiskFile* /*file*/, void* overlapped, uint32_t* bytes, bool /*wait*/)
{
    auto* block = static_cast<OVERLAPPED*>(overlapped);
    if (block == nullptr) {
        return false;
    }
    if (bytes != nullptr) {
        *bytes = static_cast<uint32_t>(block->InternalHigh);
    }
    return true;
}

uint32_t PosixDisk::last_error() { return static_cast<uint32_t>(g_last_error); }

DiskStream* PosixDisk::open_stream(const char* path, const char* mode, StreamSharing /*sharing*/)
{
    std::FILE* file = std::fopen(PosixPath(path).c_str(), mode);
    if (file == nullptr) {
        g_last_error = win32_error(errno);
    }
    return reinterpret_cast<DiskStream*>(file);
}

size_t PosixDisk::stream_read(DiskStream* stream, void* buffer, size_t size, size_t count)
{
    return std::fread(buffer, size, count, reinterpret_cast<std::FILE*>(stream));
}

size_t PosixDisk::stream_write(DiskStream* stream, const void* data, size_t size, size_t count)
{
    return std::fwrite(data, size, count, reinterpret_cast<std::FILE*>(stream));
}

int PosixDisk::stream_seek(DiskStream* stream, long offset, int origin)
{
    return std::fseek(reinterpret_cast<std::FILE*>(stream), offset, origin);
}

long PosixDisk::stream_tell(DiskStream* stream) { return std::ftell(reinterpret_cast<std::FILE*>(stream)); }

int PosixDisk::stream_flush(DiskStream* stream) { return std::fflush(reinterpret_cast<std::FILE*>(stream)); }

int PosixDisk::stream_close(DiskStream* stream) { return std::fclose(reinterpret_cast<std::FILE*>(stream)); }

PathKind PosixDisk::kind(const char* path)
{
    struct stat info;
    if (path == nullptr || ::stat(PosixPath(path).c_str(), &info) != 0) {
        return PathKind::kNone;
    }
    return S_ISDIR(info.st_mode) ? PathKind::kFolder : PathKind::kFile;
}

bool PosixDisk::stat(const char* path, FileStat* out)
{
    struct stat info;
    if (path == nullptr || out == nullptr || ::stat(PosixPath(path).c_str(), &info) != 0) {
        g_last_error = win32_error(errno);
        return false;
    }
    out->folder = S_ISDIR(info.st_mode);
    out->size = static_cast<uint64_t>(info.st_size);
    out->write_time = file_time(info.st_mtime);
    return true;
}

bool PosixDisk::local_time(uint64_t write_time, LocalTime* local)
{
    if (local == nullptr) {
        return false;
    }
    constexpr uint64_t kWindowsEpoch = 11644473600ULL;
    constexpr uint64_t kTicksPerSecond = 10000000ULL;
    const time_t seconds = static_cast<time_t>(write_time / kTicksPerSecond) - static_cast<time_t>(kWindowsEpoch);
    struct tm broken;
    if (localtime_r(&seconds, &broken) == nullptr) {
        return false;
    }
    local->year = static_cast<uint16_t>(broken.tm_year + 1900);
    local->month = static_cast<uint16_t>(broken.tm_mon + 1);
    local->day = static_cast<uint16_t>(broken.tm_mday);
    local->hour = static_cast<uint16_t>(broken.tm_hour);
    local->minute = static_cast<uint16_t>(broken.tm_min);
    local->second = static_cast<uint16_t>(broken.tm_sec);
    return true;
}

bool PosixDisk::make_folder(const char* path) { return path != nullptr && ::mkdir(PosixPath(path).c_str(), 0755) == 0; }

bool PosixDisk::make_folders(const char* path)
{
    if (path == nullptr) {
        return false;
    }
    // (each part in turn, as Win32's SHCreateDirectory does: an existing one is not an error)
    char buffer[1024];
    std::snprintf(buffer, sizeof(buffer), "%s", PosixPath(path).c_str());
    for (char* at = buffer + 1; *at != '\0'; ++at) {
        if (*at == '/') {
            *at = '\0';
            ::mkdir(buffer, 0755);
            *at = '/';
        }
    }
    return ::mkdir(buffer, 0755) == 0 || kind(buffer) == PathKind::kFolder;
}

bool PosixDisk::remove(const char* path)
{
    if (path == nullptr) {
        return false;
    }
    const bool removed = ::unlink(PosixPath(path).c_str()) == 0;
    if (!removed) {
        g_last_error = win32_error(errno);
    }
    return removed;
}

bool PosixDisk::replace(const char* from, const char* to, bool /*write_through*/)
{
    if (from == nullptr || to == nullptr) {
        return false;
    }
    const bool moved = std::rename(PosixPath(from).c_str(), PosixPath(to).c_str()) == 0;
    if (!moved) {
        g_last_error = win32_error(errno);
    }
    return moved;
}

bool PosixDisk::list(const char* pattern, FoundVisitor& visitor)
{
    if (pattern == nullptr) {
        return false;
    }
    char directory[1024];
    char mask[256];
    split_pattern(PosixPath(pattern).c_str(), directory, sizeof(directory), mask, sizeof(mask));
    DIR* dir = opendir(directory);
    if (dir == nullptr) {
        g_last_error = win32_error(errno);
        return false;
    }
    bool any = false;
    while (struct dirent* entry = readdir(dir)) {
        if (!matches(mask, entry->d_name)) {
            continue;
        }
        char path[1400];
        std::snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        struct stat info;
        if (::stat(path, &info) != 0) {
            continue;
        }
        Found found;
        found.name = entry->d_name;
        found.folder = S_ISDIR(info.st_mode);
        found.size_low = static_cast<uint32_t>(static_cast<uint64_t>(info.st_size) & 0xFFFFFFFFU);
        found.write_time = file_time(info.st_mtime);
        any = true;
        if (!visitor.found(found)) {
            break;
        }
    }
    closedir(dir);
    return any;
}

bool PosixDisk::free_space(uint64_t* available)
{
    struct statvfs info;
    if (available == nullptr || statvfs(".", &info) != 0) {
        return false;
    }
    *available = static_cast<uint64_t>(info.f_bavail) * static_cast<uint64_t>(info.f_frsize);
    return true;
}

} // namespace fs

namespace fs::posix {

Disk& disk()
{
    static fs::PosixDisk the_disk;
    return the_disk;
}

} // namespace fs::posix
