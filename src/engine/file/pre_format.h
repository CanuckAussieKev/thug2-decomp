#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/util/byte_stream.h"

// NOT ORIGINAL - the .pre / .prx archive format and its localised patches, read through util::ByteReader.
//
// An archive (little-endian):
//   header    u32 total size (the file's own), u32 version, i32 file count
//   each file u32 size (uncompressed), u32 compressed size (0: stored as it is), u16 name size, u16 0 (the loaded
//             archive's use count), u32 checksum of the name; the name (name size bytes, zero-terminated and
//             zero-padded); the data (compressed size bytes if compressed, else size), padded to 4 bytes.
// A patch (.ppf / .ppd, French / German): u32 the patched archive's size, u32 record count, then each record
//   {u32 offset, u32 length, `length` bytes written at that offset of the archive}.
//
// The archive readers (engine/file/archive.cpp, packs.cpp) walk an archive through these; packs.cpp's repacked copy
// (every file uncompressed, 16-byte aligned) is its own in-memory view (packs::PreEntry).
namespace pre_format {

constexpr size_t kHeaderSize = 12;
constexpr size_t kFileHeaderSize = 16;
constexpr size_t kPatchHeaderSize = 8;

struct ArchiveHeader {
    uint32_t total_size = 0;
    uint32_t version = 0;
    int32_t file_count = 0;
};

struct FileRecord {
    int32_t size = 0;
    int32_t compressed_size = 0; // 0: stored
    uint16_t name_size = 0;
    uint16_t use_count = 0;
    uint32_t checksum = 0;
    const char* name = nullptr;    // in place, terminated within name_size
    const uint8_t* data = nullptr; // in place: stored_size() bytes
    size_t offset = 0;             // of the file's header in the archive

    uint32_t stored_size() const { return static_cast<uint32_t>(compressed_size ? compressed_size : size); }
};

// The header; false if the archive is shorter or its count negative.
bool read_archive_header(util::ByteReader& reader, ArchiveHeader* out);
// The next file and its data (the reader left at the next file); false if any of it is past the end, a size is
// negative or the name has no terminator within its size.
bool read_file_record(util::ByteReader& reader, FileRecord* out);

struct PatchHeader {
    uint32_t patched_size = 0;
    uint32_t record_count = 0;
};
bool read_patch_header(util::ByteReader& reader, PatchHeader* out);
// Apply a patch's records to `archive` (`capacity` bytes). False at the first record past the patch's end or past
// the archive's capacity (the records before it applied).
bool apply_patch(uint8_t* archive, size_t capacity, const uint8_t* patch, size_t patch_size);

} // namespace pre_format
