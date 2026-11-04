// NOT ORIGINAL - the park map's readers and writers. See park_map_format.h.

#include "formats/park_map_format.h"

#include "engine/core/checksums.h"
#include "formats/byte_stream.h"
#include "engine/core/checked_index.h"
#include "engine/core/raw_view.h"

#include <cstddef>
#include <cstdint>
#include <gsl/assert>
#include <array>
#include <span>

namespace park_editing {

// A piece is two words: the name index (low 10 bits), x (the next 8) and z; then the height (biased), the quarter turns.
constexpr uint32_t kNameIndexMask = 0x3FF;
constexpr int kPieceXShift = 10;
constexpr int kPieceZShift = 18;
constexpr int kPieceZLowShift = 8;   // writing: z above x, the pair shifted past the name index
constexpr uint32_t kHeightMask = 0x1F;
// A gap's two ends: their heights are biased, their quarter turns packed a nibble each in one byte, their reaches a
// nibble each; the text follows at the byte after.
constexpr int kGapHeightBias = 8;
constexpr int kGapQuarterTurnsAt = 8;
constexpr int kGapReachAt = 6;
constexpr unsigned kNibbleMask = 0xF;
constexpr std::size_t kGapTextOffset = 0xA;

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
    reader.fixed_string(header->name.data(), sizeof(header->name), kMapNameBytes);
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
    writer.fixed_string(header.name.data(), kMapNameBytes);
    return writer.ok();
}

bool read_park_map_piece(util::ByteReader& reader, ParkMapPiece* piece)
{
    const uint32_t where = reader.u32();
    const uint32_t how = reader.u32();
    piece->name_index = where & kNameIndexMask;
    piece->x = static_cast<uint8_t>(where >> kPieceXShift);
    piece->z = static_cast<uint8_t>(where >> kPieceZShift);
    piece->y = static_cast<int8_t>(((how >> 2) & kHeightMask) - kFloorLevelDepth);
    piece->quarter_turns = static_cast<uint8_t>(how & 3);
    return reader.ok();
}

bool write_park_map_piece(util::ByteWriter& writer, const ParkMapPiece& piece)
{
    writer.u32(((static_cast<uint32_t>(piece.z) << kPieceZLowShift | piece.x) << kPieceXShift) | (piece.name_index & kNameIndexMask));
    writer.u32(((static_cast<uint32_t>(static_cast<uint8_t>(piece.y)) - kFloorLevelDepth) & kHeightMask) << 2 |
               (static_cast<uint32_t>(piece.quarter_turns) & 3));
    return writer.ok();
}

bool read_park_map_gap(util::ByteReader& reader, ParkMapGap* gap, bool current)
{
    std::array<uint8_t, kParkMapGapSize> bytes = {};
    reader.bytes(bytes.data(), current ? kParkMapGapSize : kOldParkMapGapSize);
    for (int end = 0; end < 2; ++end) {
        ParkMapGap::End& end_record = core::at(gap->ends, end);
        end_record.x = core::at(bytes, end);
        end_record.y = static_cast<int8_t>(core::at(bytes, 2 + end) - kGapHeightBias);
        end_record.z = core::at(bytes, 4 + end);
        end_record.quarter_turns =
            static_cast<uint8_t>((core::at(bytes, kGapQuarterTurnsAt) >> (end * 4)) & kNibbleMask);
        end_record.reach_left = static_cast<uint8_t>(core::at(bytes, kGapReachAt + end) >> 4);
        end_record.reach_right = static_cast<uint8_t>(core::at(bytes, kGapReachAt + end) & kNibbleMask);
    }
    util::ByteReader fields(std::span<const uint8_t>(bytes).subspan(kGapTextOffset));
    fields.fixed_string(gap->text.data(), sizeof(gap->text), kParkMapGapTextSize);
    gap->points = fields.u16();
    gap->cancel_flags = current ? fields.u32() : 0;
    return reader.ok();
}

bool write_park_map_gap(util::ByteWriter& writer, const ParkMapGap& gap)
{
    const auto& end_record = gap.ends;
    writer.u8(core::at(end_record, 0).x);
    writer.u8(core::at(end_record, 1).x);
    writer.u8(static_cast<uint8_t>(core::at(end_record, 0).y + kGapHeightBias));
    writer.u8(static_cast<uint8_t>(core::at(end_record, 1).y + kGapHeightBias));
    writer.u8(core::at(end_record, 0).z);
    writer.u8(core::at(end_record, 1).z);
    writer.u8(static_cast<uint8_t>((core::at(end_record, 0).reach_right & kNibbleMask) |
                                   (core::at(end_record, 0).reach_left << 4)));
    writer.u8(static_cast<uint8_t>((core::at(end_record, 1).reach_right & kNibbleMask) |
                                   (core::at(end_record, 1).reach_left << 4)));
    writer.u8(
        static_cast<uint8_t>(core::at(end_record, 0).quarter_turns | (core::at(end_record, 1).quarter_turns << 4)));
    writer.u8(0);
    writer.fixed_string(gap.text.data(), kParkMapGapTextSize);
    writer.u16(gap.points);
    writer.u32(gap.cancel_flags);
    return writer.ok();
}

void park_map_to_words(std::span<const uint8_t, kParkMapBytes> map, std::span<uint32_t> words)
{
    util::ByteReader reader(map);
    for (uint32_t i = 0; i < kParkMapWords; ++i) {
        core::at(words, i) = reader.u32();
    }
}

void park_map_from_words(std::span<uint8_t, kParkMapBytes> map, std::span<const uint32_t> words)
{
    util::ByteWriter writer(map);
    for (uint32_t i = 0; i < words.size() && i < kParkMapWords; ++i) {
        writer.u32(core::at(words, i));
    }
    writer.fill(0, writer.remaining());
}

void ParkMap::set_area(MapArea area)
{
    const auto x = area.x;
    const auto z = area.z;
    const auto width = area.width;
    const auto length = area.length;
    core::at(bytes_, kAreaXOffset) = x;
    core::at(bytes_, kAreaZOffset) = z;
    core::at(bytes_, kAreaWidthOffset) = width;
    core::at(bytes_, kAreaLengthOffset) = length;
}

std::span<char, kMapNameBytes> ParkMap::name() { return std::span<char, kMapNameBytes>(raw::chars(&core::at(bytes_, kNameOffset)), kMapNameBytes); }
std::span<const char, kMapNameBytes> ParkMap::name() const
{
    return std::span<const char, kMapNameBytes>(raw::chars(&core::at(bytes_, kNameOffset)), kMapNameBytes);
}

int8_t ParkMap::floor_height(size_t cell) const
{
    return static_cast<int8_t>(core::at(bytes_, kParkMapHeaderSize + cell));
}
void ParkMap::set_floor_height(size_t cell, int8_t height)
{
    Expects(cell < kParkMapFloorCells);
    core::at(bytes_, kParkMapHeaderSize + cell) = static_cast<uint8_t>(height);
}

uint16_t ParkMap::half_at(size_t offset) const
{
    util::ByteReader reader(std::span<const uint8_t>(bytes_).subspan(offset, 2));
    return reader.u16();
}
uint32_t ParkMap::word_at(size_t offset) const
{
    util::ByteReader reader(std::span<const uint8_t>(bytes_).subspan(offset, 4));
    return reader.u32();
}
void ParkMap::put_half(HalfAt half)
{
    const auto offset = half.offset;
    const auto value = half.value;
    util::ByteWriter writer(std::span<uint8_t>(bytes_).subspan(offset, 2));
    writer.u16(value);
}
void ParkMap::put_word(WordAt word)
{
    const auto offset = word.offset;
    const auto value = word.value;
    util::ByteWriter writer(std::span<uint8_t>(bytes_).subspan(offset, 4));
    writer.u32(value);
}

uint32_t park_map_checksum(std::span<const uint8_t> map)
{
    return checksums::generate_crc_from_buffer(map.first(kParkMapBytes).subspan(4).data(), static_cast<int>(kParkMapBytes - 4));
}

} // namespace park_editing
