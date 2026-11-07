#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "base/core/api.h"
#include "base/core/format.h"
#include "engine/file/disk.h"

// NOT ORIGINAL - owners and whole-file helpers over fs::Disk (engine/file/disk.h) for the files our tools keep in the
// game folder (the level editor's, the new front end's, the screenshots', the test-automation channel's): a file or a
// stream closed when its owner goes, a file read or written whole through a stream.
namespace fs {

// A file opened by path, closed when it goes. Both "no file" values (no_file() and null) count as empty.
class OwnedFile {
public:
    OwnedFile() = default;
    explicit OwnedFile(DiskFile* file) noexcept : file_(file) {}
    OwnedFile(const OwnedFile&) = delete;
    OwnedFile& operator=(const OwnedFile&) = delete;
    OwnedFile(OwnedFile&& other) noexcept : file_(std::exchange(other.file_, no_file())) {}
    OwnedFile& operator=(OwnedFile&& other) noexcept
    {
        if (this != &other) {
            reset(std::exchange(other.file_, no_file()));
        }
        return *this;
    }
    ~OwnedFile() { reset(); }

    [[nodiscard]] DiskFile* get() const noexcept { return file_; }
    [[nodiscard]] bool valid() const noexcept { return file_ != no_file() && file_ != nullptr; }
    explicit operator bool() const noexcept { return valid(); }
    // Closes the current file (if any) and keeps `file`.
    THUG2_API void reset(DiskFile* file = no_file()) noexcept;

private:
    DiskFile* file_ = no_file();
};

// A stream, closed when it goes.
class OwnedStream {
public:
    OwnedStream() = default;
    explicit OwnedStream(DiskStream* stream) noexcept : stream_(stream) {}
    OwnedStream(const OwnedStream&) = delete;
    OwnedStream& operator=(const OwnedStream&) = delete;
    OwnedStream(OwnedStream&& other) noexcept : stream_(std::exchange(other.stream_, nullptr)) {}
    OwnedStream& operator=(OwnedStream&& other) noexcept
    {
        if (this != &other) {
            reset(std::exchange(other.stream_, nullptr));
        }
        return *this;
    }
    ~OwnedStream() { reset(); }

    [[nodiscard]] DiskStream* get() const noexcept { return stream_; }
    explicit operator bool() const noexcept { return stream_ != nullptr; }
    // Closes the current stream (if any: its close's result, else 0) and keeps `stream`.
    int reset(DiskStream* stream = nullptr) noexcept;

private:
    DiskStream* stream_ = nullptr;
};

// fopen, owned.
OwnedStream open_stream(const char* path, const char* mode, StreamSharing sharing = StreamSharing::kAll);

// The whole file read through a stream opened "rb" (as std::ifstream(path, binary) and `text << in.rdbuf()` read it):
// false if it can't be opened.
THUG2_API bool read_text(const std::string& path, std::string* text);
// `text` written to `path` through a stream opened "wb" (as std::ofstream(path, binary | trunc) and `out << text`):
// whether it was opened and every byte written, and whether it then closed cleanly.
struct Written {
    bool written = false;
    bool closed = false;
};
THUG2_API Written write_text(const std::string& path, std::string_view text);

// fprintf to a stream: the text written whole (a %c of 0 included), as text::print_to writes to a FILE*.
template <typename... Args>
void print_to(DiskStream* stream, text::FormatFor<Args...> format, Args... args)
{
    const std::string made = text::format<Args...>(format, args...);
    disk().stream_write(stream, made.data(), 1, made.size());
}

// A folder listing told to `visit(const Found&) -> bool` (fs::Disk::list).
template <typename Visit>
bool list(const char* pattern, Visit visit)
{
    class Visitor final : public FoundVisitor {
    public:
        explicit Visitor(Visit& visit) : visit_(visit) {}
        Visitor(const Visitor&) = delete;
        Visitor& operator=(const Visitor&) = delete;
        Visitor(Visitor&&) = delete;
        Visitor& operator=(Visitor&&) = delete;
        ~Visitor() = default;
        bool found(const Found& file) override { return visit_(file); }

    private:
        Visit& visit_;
    };
    Visitor visitor(visit);
    return disk().list(pattern, visitor);
}

} // namespace fs
