#pragma once

#include <cstddef>
#include <cstdint>

namespace util {
class ByteReader;
class ByteWriter;
} // namespace util

// The save file's own bytes: a 0x14-byte header, then the summary and the contents, each a script structure as the
// structure writer stores it (engine/script/struct_buffer.h); on the PC the file is padded with 0x69 up to its type's
// fixed size. The NetworkSettings contents are stored with 0x69 added to each byte.
//
//   +0x00 u32  CRC of the header, summary and contents (generate_crc_from_buffer) with this field 0
//   +0x04 u32  CRC of the summary
//   +0x08 i32  the summary's size (at most kMaxSummarySize)
//   +0x0C i32  the header's, summary's and contents' size (the padding not counted)
//   +0x10 i32  the save type's version (VERSION_<type>)
namespace save_cards {

constexpr size_t kSaveFileHeaderSize = 0x14;
constexpr int kMaxSummarySize = 100;
// What a directory listing reads of each file: the header and the start of the summary.
constexpr size_t kListedFileBytes = 0x78;
// The added to / taken off each byte of the NetworkSettings contents, and the padding's bytes.
constexpr uint8_t kSaveFileMask = 0x69;

struct SaveFileHeader {
    uint32_t checksum = 0;
    uint32_t summary_crc = 0;
    int32_t summary_size = 0;
    int32_t size = 0;
    int32_t version = 0;
};

bool read_save_file_header(util::ByteReader& reader, SaveFileHeader* header);
bool write_save_file_header(util::ByteWriter& writer, const SaveFileHeader& header);

// A save file's checksum: the CRC of its first `size` bytes with the checksum field taken as 0.
uint32_t save_file_checksum(const uint8_t* file, size_t size);

} // namespace save_cards
