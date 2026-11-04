#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// NOT ORIGINAL - the file system seam: where the game's data files come from. A source is one place a data file
// ("levels\foun\foun.qb", "data/...") can be: the data overlay's folder (engine/file/data_overlay.h - ours, before
// everything), the loaded .pre archives (engine/file/archive.h), the Data folder on disk (engine/file/file_load.h).
// app/ adds them at start-up in that order, the order the game looks in: a file of the overlay is the overlay's, then
// an archive's copy, then the Data folder's loose file (the original's: archives first, then the data folder; the
// overlay went before both). file:: (engine/file/file_load.h) is the façade the game calls: each of its calls goes
// down the chain until a source handles it.
//
// A source handles a call or passes it on, as the original's archive manager did through its "handled" flag
// (file::archive_handled()): every call on an open file is offered to the sources in order too, since a handle doesn't
// say whose it is - an archive's entry, or a disk file's (the overlay's and the Data folder's are both disk files: the
// overlay opens its file and leaves the calls on it to the Data folder's disk calls, which the archives are asked
// before, as for any disk file). The names are the game's: matched without regard to case where the source does
// (the overlay lower-cases them, the archives use checksums of the name), '/' read as '\'.
//
// The grain is a call per source per open, read, seek, size or close - never per byte: a read is one call with the
// caller's whole buffer, as before.
namespace fs {

using Handle = void*; // an open file as its source gave it (a disk file's handle, an archive's entry)

constexpr size_t kLoosePathSize = 260; // a loose copy's path (file::kOverlayPathSize)

// A seek: the offset and where it is from (0 the start, 1 the current position, anything else the end).
struct SeekTo {
    int offset = 0;
    int origin = 0;
};

// A source's answer: whether the call was its, and its result.
template <typename T>
struct Answer {
    bool handled = false;
    T value{};
};

class Source {
public:
    Source() = default;
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;
    Source(Source&&) = delete;
    Source& operator=(Source&&) = delete;

    // Whether the source keeps its files in memory (the archives): the loose copies of the sources before the first
    // such source go before it (FileSystem::loose_override).
    [[nodiscard]] virtual bool in_memory() const { return false; }

    // --- a data file by name ---
    virtual Answer<bool> exists(const char* /*name*/) { return {}; }
    // `mode` is fopen's ("rb").
    virtual Answer<Handle> open(const char* /*name*/, const char* /*mode*/) { return {}; }
    // The path of the source's loose copy of a file - for the readers that open it themselves (the asynchronous
    // reads, the sound waves): false if the source has none.
    virtual bool loose_copy(const char* /*name*/, std::span<char, kLoosePathSize> /*path*/) { return false; }

    // --- an open file ---
    virtual Answer<int> close(Handle /*file*/) { return {}; }
    virtual Answer<int> read(void* /*buffer*/, int /*size*/, int /*count*/, Handle /*file*/) { return {}; }
    virtual Answer<int> write(const void* /*buffer*/, int /*size*/, int /*count*/, Handle /*file*/) { return {}; }
    virtual Answer<uint32_t> seek(Handle /*file*/, SeekTo /*seek*/) { return {}; }
    virtual Answer<uint32_t> size(Handle /*file*/) { return {}; }

protected:
    ~Source() = default;
};

// The ordered sources and the calls down them.
class FileSystem {
public:
    static constexpr int kMaxSources = 4;

    // app/'s start-up: a source searched after the others.
    void add(Source& source);

    // Each asks the sources in order and returns the first one's answer that handles it (a source last in the chain
    // handles everything: the Data folder). Unhandled by all: false / null / 0.
    bool exists(const char* name);
    Handle open(const char* name, const char* mode);
    int close(Handle file);
    int read(void* buffer, int size, int count, Handle file);
    int write(const void* buffer, int size, int count, Handle file);
    uint32_t seek(Handle file, SeekTo seek);
    uint32_t size(Handle file);

    // A loose copy from a source searched before the archives (the overlay's): the readers that take a file from an
    // archive unless such a copy exists (packs::read_in, the asynchronous open, the sound waves).
    bool loose_override(const char* name, std::span<char, kLoosePathSize> path);
    // The loose file a name means: the first source's loose copy (the overlay's, else the Data folder's path).
    bool loose_path(const char* name, std::span<char, kLoosePathSize> path);

private:
    std::array<Source*, kMaxSources> sources_{};
    int count_ = 0;
};

FileSystem& file_system();

} // namespace fs
