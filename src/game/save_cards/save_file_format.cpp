// NOT ORIGINAL - the save file header's reader and writer (the original cast the bytes onto a struct). See
// save_file_format.h.

#include "game/save_cards/save_file_format.h"

#include "engine/core/checksums.h"
#include "engine/util/byte_stream.h"

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
    constexpr uint8_t kZero[4] = {};
    const uint32_t crc = checksums::extend_crc_with_buffer(kZero, 4, 0xFFFFFFFFu);
    return checksums::extend_crc_with_buffer(file + 4, static_cast<int>(size - 4), crc);
}

} // namespace save_cards
