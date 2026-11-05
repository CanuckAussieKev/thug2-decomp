#pragma once

#include <cstddef>
#include <cstdint>

#include "base/core/all_ones.h"

// NOT ORIGINAL - fs::Disk: the operating system's files, by path - the one place the game's code reaches them. Under
// it are the file system's loose sources (the data overlay and the Data folder, engine/file/file_system.h), the
// asynchronous reads, the sound streams and waves, the save files, and the files our tools keep in the game folder
// (the level editor's layouts, the new front end's state, screenshots, the test-automation channel's files). app/
// installs Win32's (fs_win32/win32_disk.h) at start-up, before any of the original's code runs; a std::filesystem /
// POSIX one could stand in for a GCC build. Not installed, a call dereferences null.
//
// The methods are the operating system's calls the game made, one for one, with the same arguments: a file opened by
// path with the access, sharing and creation the caller asked for, read, written, moved and sized by its handle, then
// closed; a buffered stream (the C runtime's: what fopen gave) read and written in the caller's pieces; a folder made,
// a file removed or moved over another, a folder listed. The grain is an open, a read or write of a caller's buffer, a
// close - never a byte: whatever buffering the caller had (a stream's, a whole file in one read), it keeps.
//
// Logs and crash reports stay in the platform layer (platform/run_log, the CRT's reports): they must work before
// anything is installed and while the game is failing.
namespace fs {

// A file opened by path: the operating system's own handle (Win32's HANDLE - the same value the original kept), so a
// handle costs what the original's did. no_file() is the failed open (Win32's INVALID_HANDLE_VALUE).
struct DiskFile;
// A buffered stream (the C runtime's FILE*; null when it failed).
struct DiskStream;

inline DiskFile* no_file() noexcept { return platform::all_ones_pointer<DiskFile>(); }

enum class Access : uint8_t {
    kRead,
    kWrite,
    kReadWrite,
};
// What others (another handle, another program) may do with the file while it's open.
enum class Sharing : uint8_t {
    kNone,
    kRead,
    kReadWriteDelete,
};
enum class Disposition : uint8_t {
    kOpenExisting,
    kCreateNew,    // fails if it exists
    kCreateAlways, // emptied if it exists
    kOpenAlways,   // made if it doesn't exist
};
// How the file is read (Win32's flags): none, normal, sequential, or overlapped without the system's buffering (the
// asynchronous reads: whole sectors).
enum class Hint : uint8_t {
    kNone,
    kNormal,
    kSequential,
    kOverlappedUnbuffered,
};
struct OpenSpec {
    Access access = Access::kRead;
    Sharing sharing = Sharing::kNone;
    Disposition disposition = Disposition::kOpenExisting;
    Hint hint = Hint::kNormal;
};
enum class Origin : uint8_t {
    kBegin,
    kCurrent,
    kEnd,
};
// A stream's sharing: as fopen / _fsopen(_SH_DENYNO) / std::fstream share it (all), as _fsopen(_SH_DENYWR), or as
// fopen_s (shared reading, no writing by others).
enum class StreamSharing : uint8_t {
    kAll,
    kDenyWrite,
    kSecure,
};

// What a path is.
enum class PathKind : uint8_t {
    kNone,
    kFile,
    kFolder,
};
// A file's or folder's size and last write (the system's file time: 100 ns ticks since 1601, UTC).
struct FileStat {
    bool folder = false;
    uint64_t size = 0;
    uint64_t write_time = 0;
};
// A file time in local time (the system's calendar fields).
struct LocalTime {
    uint16_t year = 0;
    uint16_t month = 0;
    uint16_t day = 0;
    uint16_t hour = 0;
    uint16_t minute = 0;
    uint16_t second = 0;
};
// A file a folder listing found (`name` is valid during the call only).
struct Found {
    const char* name = nullptr;
    bool folder = false;
    uint32_t size_low = 0; // the size's low 32 bits
    uint64_t write_time = 0;
};
// Told each file a listing finds, in the system's order; false stops the listing (nothing more is asked of the
// system).
class FoundVisitor {
public:
    FoundVisitor() = default;
    FoundVisitor(const FoundVisitor&) = delete;
    FoundVisitor& operator=(const FoundVisitor&) = delete;
    FoundVisitor(FoundVisitor&&) = delete;
    FoundVisitor& operator=(FoundVisitor&&) = delete;
    virtual bool found(const Found& file) = 0;

protected:
    ~FoundVisitor() = default;
};

class Disk {
public:
    Disk() = default;
    Disk(const Disk&) = delete;
    Disk& operator=(const Disk&) = delete;
    Disk(Disk&&) = delete;
    Disk& operator=(Disk&&) = delete;

    // --- a file by its handle (Win32's CreateFile / ReadFile / WriteFile / SetFilePointer...) ---
    // The file at `path` (no_file() if it can't be opened: last_error() says why).
    virtual DiskFile* open(const char* path, OpenSpec spec) = 0;
    virtual bool close(DiskFile* file) = 0;
    // `length` bytes into `buffer`; `bytes` (may be null) the count read. With `overlapped` (the caller's OVERLAPPED
    // block: a file opened kOverlappedUnbuffered) the read may go on after the call (false, last_error() pending).
    virtual bool read(DiskFile* file, void* buffer, uint32_t length, uint32_t* bytes, void* overlapped) = 0;
    virtual bool write(DiskFile* file, const void* data, uint32_t length, uint32_t* written) = 0;
    // The position moved (a 32-bit offset): the new position's low 32 bits.
    virtual uint32_t seek(DiskFile* file, int32_t offset, Origin origin) = 0;
    // The position set to a 64-bit offset from the start: whether it was.
    virtual bool seek_to(DiskFile* file, uint64_t offset) = 0;
    // The size's low 32 bits (all ones if it can't be read), or the whole size.
    virtual uint32_t size(DiskFile* file) = 0;
    virtual bool size64(DiskFile* file, uint64_t* size) = 0;
    virtual bool flush(DiskFile* file) = 0;
    // An overlapped read's count once it has finished (`wait`: until it has).
    virtual bool overlapped_result(DiskFile* file, void* overlapped, uint32_t* bytes, bool wait) = 0;
    // The last call's error (Win32's GetLastError).
    virtual uint32_t last_error() = 0;

    // --- a buffered stream (the C runtime's fopen / fread / fwrite...) ---
    // `mode` as fopen takes it ("rb", "wb", "w"); null if it can't be opened.
    virtual DiskStream* open_stream(const char* path, const char* mode, StreamSharing sharing) = 0;
    virtual size_t stream_read(DiskStream* stream, void* buffer, size_t size, size_t count) = 0;
    virtual size_t stream_write(DiskStream* stream, const void* data, size_t size, size_t count) = 0;
    // fseek's origins: SEEK_SET, SEEK_CUR, SEEK_END.
    virtual int stream_seek(DiskStream* stream, long offset, int origin) = 0;
    virtual long stream_tell(DiskStream* stream) = 0;
    virtual int stream_flush(DiskStream* stream) = 0;
    virtual int stream_close(DiskStream* stream) = 0;

    // --- paths and folders ---
    virtual PathKind kind(const char* path) = 0;
    // A path's size and times (false: it isn't there).
    virtual bool stat(const char* path, FileStat* stat) = 0;
    // A file time in local time (false: it can't be converted).
    virtual bool local_time(uint64_t write_time, LocalTime* local) = 0;
    // One folder made (its parent must exist); every missing folder of the path made.
    virtual bool make_folder(const char* path) = 0;
    virtual bool make_folders(const char* path) = 0;
    virtual bool remove(const char* path) = 0;
    // `from` moved over `to` (replacing it); `write_through`: the move on disk before it returns.
    virtual bool replace(const char* from, const char* to, bool write_through) = 0;
    // The files and folders `pattern` ("folder\*.txt") matches, each told to `visitor`: false if none matched.
    virtual bool list(const char* pattern, FoundVisitor& visitor) = 0;
    // The bytes free to the user on the current drive (false: unknown).
    virtual bool free_space(uint64_t* available) = 0;

protected:
    ~Disk() = default;
};

// The disk app/ installed.
Disk& disk();
// app/'s start-up, before any of the original's code runs.
void install_disk(Disk& disk);

} // namespace fs
