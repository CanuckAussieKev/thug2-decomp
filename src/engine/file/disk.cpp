// NOT ORIGINAL - the installed disk (disk.h) and the owners and whole-file helpers over it (disk_files.h).

#include "engine/file/disk.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "engine/file/disk_files.h"

namespace fs {

namespace {

constexpr size_t kReadPieceBytes = 4096; // a whole file is read in pieces of this many bytes

// The disk app/ installed (a constant-initialised pointer: no guard, nothing to destroy).
Disk*& installed()
{
    static Disk* disk = nullptr;
    return disk;
}

} // namespace

Disk& disk() { return *installed(); }

void install_disk(Disk& disk) { installed() = &disk; }

void OwnedFile::reset(DiskFile* file) noexcept
{
    if (valid()) {
        disk().close(file_);
    }
    file_ = file;
}

int OwnedStream::reset(DiskStream* stream) noexcept
{
    int result = 0;
    if (stream_) {
        result = disk().stream_close(stream_);
    }
    stream_ = stream;
    return result;
}

OwnedStream open_stream(const char* path, const char* mode, StreamSharing sharing)
{
    return OwnedStream(disk().open_stream(path, mode, sharing));
}

bool read_text(const std::string& path, std::string* text)
{
    const OwnedStream stream = open_stream(path.c_str(), "rb");
    if (!stream) {
        return false;
    }
    text->clear();
    std::array<char, kReadPieceBytes> piece{};
    for (size_t bytes = 0; (bytes = disk().stream_read(stream.get(), piece.data(), 1, piece.size())) > 0;) {
        text->append(piece.data(), bytes);
    }
    return true;
}

Written write_text(const std::string& path, std::string_view text)
{
    OwnedStream stream = open_stream(path.c_str(), "wb");
    if (!stream) {
        return {};
    }
    Written result;
    result.written = disk().stream_write(stream.get(), text.data(), 1, text.size()) == text.size();
    result.closed = stream.reset() == 0;
    return result;
}

} // namespace fs
