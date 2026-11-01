// NOT ORIGINAL - the park map's readers and writers. See park_map_format.h.

#include "game/park_editing/park_map_format.h"

#include "engine/core/checksums.h"
#include "engine/util/byte_stream.h"

namespace park_editing {

bool read_park_map_header(util::ByteReader& reader, ParkMapHeader* header)
{
    header->checksum = reader.u32();
    header->extent = reader.u16();
    header->version = reader.u16();
    header->shell_size = reader.u16();
    reader.skip(2);
    header->x = reader.u8();
    header->z = reader.u8();
    header->width = reader.u8();
    header->length = reader.u8();
    header->piece_count = reader.u16();
    header->gap_count = reader.u16();
    header->max_players = reader.u8();
    reader.skip(3);
    header->theme = reader.u32();
    reader.fixed_string(header->name, sizeof(header->name), 0x40);
    return reader.ok();
}

bool write_park_map_header(util::ByteWriter& writer, const ParkMapHeader& header)
{
    writer.u32(header.checksum);
    writer.u16(header.extent);
    writer.u16(header.version);
    writer.u16(header.shell_size);
    writer.u16(0);
    writer.u8(header.x);
    writer.u8(header.z);
    writer.u8(header.width);
    writer.u8(header.length);
    writer.u16(header.piece_count);
    writer.u16(header.gap_count);
    writer.u8(header.max_players);
    writer.fill(0, 3);
    writer.u32(header.theme);
    writer.fixed_string(header.name, 0x40);
    return writer.ok();
}

bool read_park_map_piece(util::ByteReader& reader, ParkMapPiece* piece)
{
    uint32_t where = reader.u32();
    uint32_t how = reader.u32();
    piece->name_index = where & 0x3FF;
    piece->x = static_cast<uint8_t>(where >> 10);
    piece->z = static_cast<uint8_t>(where >> 18);
    piece->y = static_cast<int8_t>(((how >> 2) & 0x1F) - 16);
    piece->quarter_turns = static_cast<uint8_t>(how & 3);
    return reader.ok();
}

bool write_park_map_piece(util::ByteWriter& writer, const ParkMapPiece& piece)
{
    writer.u32(((static_cast<uint32_t>(piece.z) << 8 | piece.x) << 10) | (piece.name_index & 0x3FF));
    writer.u32(((static_cast<uint32_t>(static_cast<uint8_t>(piece.y)) - 0x10) & 0x1F) << 2 |
               (static_cast<uint32_t>(piece.quarter_turns) & 3));
    return writer.ok();
}

bool read_park_map_gap(util::ByteReader& reader, ParkMapGap* gap, bool current)
{
    uint8_t bytes[kParkMapGapSize] = {};
    reader.bytes(bytes, current ? kParkMapGapSize : kOldParkMapGapSize);
    for (int end = 0; end < 2; ++end) {
        ParkMapGap::End& e = gap->ends[end];
        e.x = bytes[end];
        e.y = static_cast<int8_t>(bytes[2 + end] - 8);
        e.z = bytes[4 + end];
        e.quarter_turns = static_cast<uint8_t>((bytes[8] >> (end * 4)) & 0xF);
        e.reach_left = static_cast<uint8_t>(bytes[6 + end] >> 4);
        e.reach_right = static_cast<uint8_t>(bytes[6 + end] & 0xF);
    }
    util::ByteReader fields(bytes + 0xA, kParkMapGapSize - 0xA);
    fields.fixed_string(gap->text, sizeof(gap->text), kParkMapGapTextSize);
    gap->points = fields.u16();
    gap->cancel_flags = current ? fields.u32() : 0;
    return reader.ok();
}

bool write_park_map_gap(util::ByteWriter& writer, const ParkMapGap& gap)
{
    const ParkMapGap::End* e = gap.ends;
    writer.u8(e[0].x);
    writer.u8(e[1].x);
    writer.u8(static_cast<uint8_t>(e[0].y + 8));
    writer.u8(static_cast<uint8_t>(e[1].y + 8));
    writer.u8(e[0].z);
    writer.u8(e[1].z);
    writer.u8(static_cast<uint8_t>((e[0].reach_right & 0xF) | (e[0].reach_left << 4)));
    writer.u8(static_cast<uint8_t>((e[1].reach_right & 0xF) | (e[1].reach_left << 4)));
    writer.u8(static_cast<uint8_t>(e[0].quarter_turns | (e[1].quarter_turns << 4)));
    writer.u8(0);
    writer.fixed_string(gap.text, kParkMapGapTextSize);
    writer.u16(gap.points);
    writer.u32(gap.cancel_flags);
    return writer.ok();
}

void park_map_to_words(const void* map, uint32_t* words)
{
    util::ByteReader reader(map, kParkMapBytes);
    for (uint32_t i = 0; i < kParkMapWords; ++i)
        words[i] = reader.u32();
}

void park_map_from_words(void* map, const uint32_t* words, uint32_t count)
{
    util::ByteWriter writer(map, kParkMapBytes);
    for (uint32_t i = 0; i < count && i < kParkMapWords; ++i)
        writer.u32(words[i]);
    writer.fill(0, writer.remaining());
}

uint32_t park_map_checksum(const uint8_t* map)
{
    return checksums::generate_crc_from_buffer(map + 4, static_cast<int>(kParkMapBytes - 4));
}

} // namespace park_editing
