#pragma once

#include <cstddef>
#include <cstdint>

namespace util {
class ByteReader;
class ByteWriter;
} // namespace util

// NOT ORIGINAL (the format is the original's) - the park map's bytes (CompressedMap in park.h: the park as it is
// saved in a Park save's Park_editor_map integer array, in Data/CustomParks/custom<n>.prk's structure, and sent to
// the other players). 0x3A98 bytes, little-endian:
//
//   +0x00 u32  CRC of the rest (+0x04 to the end)        +0x04 u16  bytes used     +0x06 u16  version (20004)
//   +0x08 u16  the shell size                             +0x0A u16  (0)
//   +0x0C u8 x4  the build area: x, z, width, length      +0x10 u16  the metapieces  +0x12 u16  the gaps
//   +0x14 u8   max players, 3 bytes (0)                   +0x18 u32  the theme (0: "Default")
//   +0x1C char[0x40]  the park's name                     +0x5C i8[58 * 58]  the floor heights (x, then z)
//   then the metapieces (8 bytes each) and the gaps (0x30 bytes each; 0x2C in older versions), zeros to the end.
//
// A metapiece: u32 (z << 18 | x << 10 | the name's index in Ed_Save_Map, 10 bits), u32 ((height + 16) & 0x1F) << 2 |
// its quarter turns. A gap: both ends' x (u8 each), y + 8 (u8 each), z (u8 each), sides (u8 each: right in the low
// 4 bits, left in the high ones), the quarter turns (u8: the first end's in the low 4 bits), (0), its name
// (32 bytes, zero-padded), its points (u16) and (current version only) its cancel flags (u32).
namespace park_editing {

constexpr size_t kParkMapBytes = 0x3A98;
constexpr size_t kParkMapHeaderSize = 0x5C;
constexpr size_t kParkMapFloorCells = 58 * 58; // (park.h's kFloorSize squared)
constexpr size_t kParkMapRecordsStart = kParkMapHeaderSize + kParkMapFloorCells;
constexpr size_t kParkMapPieceSize = 8;
constexpr size_t kParkMapGapSize = 0x30;
constexpr size_t kOldParkMapGapSize = 0x2C;
constexpr size_t kParkMapGapTextSize = 0x20;
constexpr uint16_t kParkMapVersion = 20004;

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ParkMapHeader {
    uint32_t checksum = 0;
    uint16_t extent = 0;
    uint16_t version = 0;
    uint16_t shell_size = 0;
    uint8_t x = 0, z = 0, width = 0, length = 0;
    uint16_t piece_count = 0;
    uint16_t gap_count = 0;
    uint8_t max_players = 0;
    uint32_t theme = 0;
    char name[0x41] = {}; // (0x40 bytes on disk, possibly unterminated)
};
// NOLINTEND(readability-identifier-naming)
bool read_park_map_header(util::ByteReader& reader, ParkMapHeader* header);
bool write_park_map_header(util::ByteWriter& writer, const ParkMapHeader& header);

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ParkMapPiece {
    uint32_t name_index = 0; // (10 bits)
    uint8_t x = 0, z = 0;
    int8_t y = 0;
    uint8_t quarter_turns = 0;
};
// NOLINTEND(readability-identifier-naming)
bool read_park_map_piece(util::ByteReader& reader, ParkMapPiece* piece);
bool write_park_map_piece(util::ByteWriter& writer, const ParkMapPiece& piece);

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ParkMapGap {
    struct End {
        uint8_t x = 0;
        int8_t y = 0;
        uint8_t z = 0;
        uint8_t quarter_turns = 0, reach_left = 0, reach_right = 0; // (4 bits each)
    };
    End ends[2];
    char text[kParkMapGapTextSize + 1] = {};
    uint16_t points = 0;
    uint32_t cancel_flags = 0; // (current version only)
};
// NOLINTEND(readability-identifier-naming)
// `current`: the current version's 0x30-byte gaps (else 0x2C, no cancel flags).
bool read_park_map_gap(util::ByteReader& reader, ParkMapGap* gap, bool current);
bool write_park_map_gap(util::ByteWriter& writer, const ParkMapGap& gap);

// The map's checksum: the CRC of all of it past the checksum field.
uint32_t park_map_checksum(const uint8_t* map);

// The map as the integer array it is saved as (Park_editor_map: its bytes as little-endian words).
constexpr uint32_t kParkMapWords = kParkMapBytes / 4;
// The map's words into `words` (kParkMapWords of them).
void park_map_to_words(const void* map, uint32_t* words);
// The map from `count` words (at most kParkMapWords; the rest of the map zeroed).
void park_map_from_words(void* map, const uint32_t* words, uint32_t count);

} // namespace park_editing
