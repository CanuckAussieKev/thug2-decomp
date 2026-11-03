// NOT ORIGINAL - the save file header's reader and writer (the original cast the bytes onto a struct). See
// save_file_format.h.

#include "formats/save_file_format.h"

#include "engine/core/checksums.h"
#include "formats/byte_stream.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace save_cards {

bool read_save_file_header(util::ByteReader& reader, SaveFileHeader* header)
{
    header->checksum = reader.u32();
    header->summary_crc = reader.u32();
    header->summary_size = reader.i32();
    header->size = reader.i32();
    header->version = reader.i32();
    return reader.ok();
}

bool write_save_file_header(util::ByteWriter& writer, const SaveFileHeader& header)
{
    writer.u32(header.checksum);
    writer.u32(header.summary_crc);
    writer.i32(header.summary_size);
    writer.i32(header.size);
    writer.i32(header.version);
    return writer.ok();
}

uint32_t save_file_checksum(const uint8_t* file, size_t size)
{
    if (size < 4)
        return 0;
    constexpr std::array<uint8_t, 4> kZero = {};
    const uint32_t crc = checksums::extend_crc_with_buffer(kZero, 0xFFFFFFFFU);
    return checksums::extend_crc_with_buffer(std::span(file, size).subspan(4), crc);
}

} // namespace save_cards
