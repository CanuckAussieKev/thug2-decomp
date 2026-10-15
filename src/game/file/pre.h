#pragma once

#include <cstdint>

namespace Lst {
class HashTable;
}

// .pre archives ("pre files", .prx on PC): many data files packed into one, loaded into memory in one go.
// Files inside are found by the checksum of their name and read through the same File:: calls as files on
// disk (see game/file/file.h), which offer every call to the pre manager first.
namespace Pre {

// Decompress `size` bytes of LZSS data (the archive variant: 4 KB window in a global ring buffer) from `in`
// into `out`. A negative or absurd size (32 MB or more) hangs, as the original's assertion does.
void Decode(const uint8_t* in, uint8_t* out, int size);

// One loaded archive.
class PreFile {
public:
    // One file inside the archive. Also the "file handle" the File:: calls get for it.
    struct FileInfo {
        int compressedSize;   // 0 if stored uncompressed
        uint8_t* data;        // inside the archive's block (compressed, or the file itself)
        uint8_t* unpacked;    // decompressed copy while the file is open, else null
        int position;         // read position while open
        int size;             // uncompressed size
    };

    // `block` is the whole archive as loaded (the PreFile owns it from now on). `bottom_up` picks the heap
    // that file information and decompressed files are allocated from.
    PreFile(uint8_t* block, bool bottom_up);
    // Frees the block and all file information.
    ~PreFile();

    // Copy (decompressing if needed) the file called `name` to `out`, or to a new Mem::Malloc block if `out`
    // is null. Sets `*size` to its size. Returns the buffer, or null if the file isn't in this archive.
    uint8_t* LoadContainedFile(const char* name, int* size, uint8_t* out);
    // Make `handle` the active file and return its data, or null if it isn't one of this archive's files.
    uint8_t* GetContainedFileByHandle(FileInfo* handle);
    // Open the file called `name`: make it active and decompress it if needed. Returns its handle or null.
    FileInfo* GetContainedFile(const char* name);
    // Read from / seek in the active file. Read returns the number of bytes copied; Seek returns 0, or -1
    // (position unchanged) if the new position would be outside the file. Origins: 0 start, 1 current, 2 end.
    int Read(void* out, unsigned int count);
    int Seek(int offset, int origin);

    uint8_t* m_block;
    int m_numFiles;
    Lst::HashTable* m_files; // FileInfo by name checksum
    FileInfo* m_activeFile;
    int m_openCount;
    bool m_bottomUpHeap;
};

} // namespace Pre
