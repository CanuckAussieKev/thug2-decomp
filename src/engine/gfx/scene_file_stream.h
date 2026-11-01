#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/file/file_load.h"

// NOT ORIGINAL - a scene file read through the file system with scene_format's reads (scene_format::MemoryStream's
// interface): its size taken when opened, every read and skip counted against it. A failed read zero-fills its
// output; after one, nothing more is read.
namespace scene_format {

class FileStream {
public:
    explicit FileStream(file::Hdl file) : file_(file), size_(file::get_file_size(file)) {}

    bool take(void* out, size_t bytes)
    {
        if (!fits(bytes)) {
            ok_ = false;
            std::memset(out, 0, bytes);
            return false;
        }
        const int read = file::get_take_in(out, static_cast<int>(bytes), 1, file_);
        if (read != static_cast<int>(bytes)) {
            ok_ = false;
            if (read >= 0 && static_cast<size_t>(read) < bytes)
                std::memset(static_cast<uint8_t*>(out) + read, 0, bytes - static_cast<size_t>(read));
            return false;
        }
        position_ += bytes;
        return true;
    }
    bool skip(size_t bytes)
    {
        if (!fits(bytes)) {
            ok_ = false;
            return false;
        }
        file::seek_to(file_, static_cast<int>(bytes), 1);
        position_ += bytes;
        return true;
    }
    bool fits(uint64_t bytes) const { return ok_ && bytes <= size_ - position_; }
    bool ok() const { return ok_; }
    void fail() { ok_ = false; }
    size_t position() const { return position_; }
    file::Hdl file() const { return file_; }

private:
    file::Hdl file_;
    size_t size_;
    size_t position_ = 0;
    bool ok_ = true;
};

} // namespace scene_format
