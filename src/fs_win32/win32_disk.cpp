// NOT ORIGINAL - Win32's fs::Disk (win32_disk.h): each method is the Win32 or C runtime call the game made at that
// place, unchanged - the same call with the same arguments (the original's file layer 0x005B9640-0x005BA010, its save
// files 0x005BC740-0x005BCAE0, its sound streams 0x00453670 / 0x00454370, its waves 0x0047C870, the face pictures
// 0x005A4730, and the files our tools keep in the game folder).

#include "fs_win32/win32_disk.h"

#include <windows.h>

#include <share.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <system_error>

#include "base/core/no_destroy.h"
#include "engine/file/disk.h"
#include "engine/memory/mem.h"
#include <gsl/pointers>

namespace fs::win32 {

namespace {

constexpr int kHighWordShift = 32;

// The same address as a pointer to To (a handle and the system's object: never dereferenced as the other's type).
template <typename To, typename From>
To* same_address(From* pointer) noexcept
{
    void* const address = static_cast<void*>(pointer);
    return static_cast<To*>(address);
}
// A handle's Win32 / C runtime value and back: the same pointer (a handle is the system's own value).
HANDLE native(DiskFile* file) noexcept { return static_cast<HANDLE>(file); }
DiskFile* handle(HANDLE native) noexcept { return static_cast<DiskFile*>(native); }
std::FILE* native(DiskStream* stream) noexcept { return same_address<std::FILE>(stream); }
gsl::owner<std::FILE*> owned(DiskStream* stream) noexcept { return same_address<std::FILE>(stream); }
DiskStream* handle(gsl::owner<std::FILE*> native) noexcept { return same_address<DiskStream>(native); }

uint64_t ticks(const FILETIME& time)
{
    return (static_cast<uint64_t>(time.dwHighDateTime) << kHighWordShift) | time.dwLowDateTime;
}
FILETIME file_time(uint64_t ticks)
{
    return {.dwLowDateTime = static_cast<DWORD>(ticks), .dwHighDateTime = static_cast<DWORD>(ticks >> kHighWordShift)};
}

DWORD access_of(Access access)
{
    switch (access) {
    case Access::kRead:
        return GENERIC_READ;
    case Access::kWrite:
        return GENERIC_WRITE;
    case Access::kReadWrite:
        return GENERIC_READ | GENERIC_WRITE;
    }
    return 0;
}
DWORD sharing_of(Sharing sharing)
{
    switch (sharing) {
    case Sharing::kNone:
        return 0;
    case Sharing::kRead:
        return FILE_SHARE_READ;
    case Sharing::kReadWriteDelete:
        return FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    }
    return 0;
}
DWORD disposition_of(Disposition disposition)
{
    switch (disposition) {
    case Disposition::kOpenExisting:
        return OPEN_EXISTING;
    case Disposition::kCreateNew:
        return CREATE_NEW;
    case Disposition::kCreateAlways:
        return CREATE_ALWAYS;
    case Disposition::kOpenAlways:
        return OPEN_ALWAYS;
    }
    return OPEN_EXISTING;
}
DWORD flags_of(Hint hint)
{
    switch (hint) {
    case Hint::kNone:
        return 0;
    case Hint::kNormal:
        return FILE_ATTRIBUTE_NORMAL;
    case Hint::kSequential:
        return FILE_FLAG_SEQUENTIAL_SCAN;
    case Hint::kOverlappedUnbuffered:
        return FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING;
    }
    return FILE_ATTRIBUTE_NORMAL;
}
DWORD method_of(Origin origin)
{
    switch (origin) {
    case Origin::kBegin:
        return FILE_BEGIN;
    case Origin::kCurrent:
        return FILE_CURRENT;
    case Origin::kEnd:
        return FILE_END;
    }
    return FILE_BEGIN;
}

class Win32Disk final : public Disk {
public:
    DiskFile* open(const char* path, OpenSpec spec) override
    {
        return handle(CreateFileA(path, access_of(spec.access), sharing_of(spec.sharing), nullptr,
                                  disposition_of(spec.disposition), flags_of(spec.hint), nullptr));
    }
    bool close(DiskFile* file) override { return CloseHandle(native(file)) != FALSE; }
    bool read(DiskFile* file, void* buffer, uint32_t length, uint32_t* bytes, void* overlapped) override
    {
        DWORD count = 0;
        const BOOL done = ReadFile(native(file), buffer, length, bytes ? &count : nullptr, static_cast<OVERLAPPED*>(overlapped));
        if (bytes) {
            *bytes = count;
        }
        return done != FALSE;
    }
    bool write(DiskFile* file, const void* data, uint32_t length, uint32_t* written) override
    {
        DWORD count = 0;
        const BOOL done = WriteFile(native(file), data, length, &count, nullptr);
        if (written) {
            *written = count;
        }
        return done != FALSE;
    }
    uint32_t seek(DiskFile* file, int32_t offset, Origin origin) override
    {
        return SetFilePointer(native(file), offset, nullptr, method_of(origin));
    }
    bool seek_to(DiskFile* file, uint64_t offset) override
    {
        LARGE_INTEGER at;
        at.QuadPart = static_cast<LONGLONG>(offset);
        return SetFilePointerEx(native(file), at, nullptr, FILE_BEGIN) != FALSE;
    }
    uint32_t size(DiskFile* file) override
    {
        DWORD high = 0;
        return ::GetFileSize(native(file), &high);
    }
    bool size64(DiskFile* file, uint64_t* size) override
    {
        LARGE_INTEGER bytes{};
        const bool known = GetFileSizeEx(native(file), &bytes) != FALSE;
        *size = known ? static_cast<uint64_t>(bytes.QuadPart) : 0;
        return known;
    }
    bool flush(DiskFile* file) override { return FlushFileBuffers(native(file)) != FALSE; }
    bool overlapped_result(DiskFile* file, void* overlapped, uint32_t* bytes, bool wait) override
    {
        DWORD count = 0;
        const BOOL done = GetOverlappedResult(native(file), static_cast<OVERLAPPED*>(overlapped), &count, wait ? TRUE : FALSE);
        *bytes = count;
        return done != FALSE;
    }
    uint32_t last_error() override { return GetLastError(); }

    DiskStream* open_stream(const char* path, const char* mode, StreamSharing sharing) override
    {
        switch (sharing) {
        case StreamSharing::kAll: {
            const gsl::owner<std::FILE*> stream = memory::adopt<std::FILE*>(_fsopen(path, mode, _SH_DENYNO));
            return handle(stream);
        }
        case StreamSharing::kDenyWrite: {
            const gsl::owner<std::FILE*> stream = memory::adopt<std::FILE*>(_fsopen(path, mode, _SH_DENYWR));
            return handle(stream);
        }
        case StreamSharing::kSecure: {
            gsl::owner<std::FILE*> stream = nullptr;
            if (fopen_s(&stream, path, mode) != 0) {
                return nullptr;
            }
            return handle(stream);
        }
        }
        return nullptr;
    }
    size_t stream_read(DiskStream* stream, void* buffer, size_t size, size_t count) override
    {
        return std::fread(buffer, size, count, native(stream));
    }
    size_t stream_write(DiskStream* stream, const void* data, size_t size, size_t count) override
    {
        return std::fwrite(data, size, count, native(stream));
    }
    int stream_seek(DiskStream* stream, long offset, int origin) override
    {
        return std::fseek(native(stream), offset, origin);
    }
    long stream_tell(DiskStream* stream) override { return std::ftell(native(stream)); }
    int stream_flush(DiskStream* stream) override { return std::fflush(native(stream)); }
    int stream_close(DiskStream* stream) override
    {
        const gsl::owner<std::FILE*> file = owned(stream);
        return std::fclose(file);
    }

    PathKind kind(const char* path) override
    {
        const DWORD attributes = GetFileAttributesA(path);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            return PathKind::kNone;
        }
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U ? PathKind::kFolder : PathKind::kFile;
    }
    bool stat(const char* path, FileStat* stat) override
    {
        WIN32_FILE_ATTRIBUTE_DATA data = {};
        if (GetFileAttributesExA(path, GetFileExInfoStandard, &data) == FALSE) {
            return false;
        }
        stat->folder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
        stat->size = (static_cast<uint64_t>(data.nFileSizeHigh) << kHighWordShift) | data.nFileSizeLow;
        stat->write_time = ticks(data.ftLastWriteTime);
        return true;
    }
    bool local_time(uint64_t write_time, LocalTime* local) override
    {
        const FILETIME utc = file_time(write_time);
        FILETIME here;
        SYSTEMTIME time{};
        const bool converted = (FileTimeToLocalFileTime(&utc, &here) != FALSE) && (FileTimeToSystemTime(&here, &time) != FALSE);
        *local = {.year = time.wYear, .month = time.wMonth, .day = time.wDay, .hour = time.wHour, .minute = time.wMinute, .second = time.wSecond};
        return converted;
    }
    bool make_folder(const char* path) override { return CreateDirectoryA(path, nullptr) != FALSE; }
    bool make_folders(const char* path) override
    {
        std::error_code error;
        return std::filesystem::create_directories(std::filesystem::path(path), error);
    }
    bool remove(const char* path) override { return DeleteFileA(path) != FALSE; }
    bool replace(const char* from, const char* to, bool write_through) override
    {
        const DWORD flags = MOVEFILE_REPLACE_EXISTING | (write_through ? MOVEFILE_WRITE_THROUGH : 0U);
        return MoveFileExA(from, to, flags) != FALSE;
    }
    bool list(const char* pattern, FoundVisitor& visitor) override
    {
        WIN32_FIND_DATAA found;
        HANDLE search = FindFirstFileA(pattern, &found);
        if (search == INVALID_HANDLE_VALUE) {
            return false;
        }
        for (bool again = true; again;) {
            const Found file{.name = std::data(found.cFileName),
                             .folder = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U,
                             .size_low = found.nFileSizeLow,
                             .write_time = ticks(found.ftLastWriteTime)};
            again = visitor.found(file) && (FindNextFileA(search, &found) != FALSE);
        }
        FindClose(search);
        return true;
    }
    bool free_space(uint64_t* available) override
    {
        ULARGE_INTEGER free_bytes;
        ULARGE_INTEGER total;
        if (GetDiskFreeSpaceExA(nullptr, &free_bytes, &total, nullptr) == FALSE) {
            return false;
        }
        *available = free_bytes.QuadPart;
        return true;
    }
};

} // namespace

Disk& disk()
{
    static core::NoDestroy<Win32Disk> disk;
    return disk.get();
}

} // namespace fs::win32
