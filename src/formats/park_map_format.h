#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <array>

#include "engine/core/checked_index.h"

namespace util {
class ByteReader;
class ByteWriter;
} // namespace util

// NOT ORIGINAL (the format is the original's) - the park map's bytes (ParkMap below: the park as it is
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

// The map's fixed fields (the bytes before the pieces).
constexpr std::size_t kVersionOffset = 0x06;
constexpr std::size_t kShellSizeOffset = 0x08;
constexpr std::size_t kReserved0aOffset = 0x0A;
constexpr std::size_t kAreaXOffset = 0x0C;
constexpr std::size_t kAreaZOffset = 0x0D;
constexpr std::size_t kAreaWidthOffset = 0x0E;
constexpr std::size_t kAreaLengthOffset = 0x0F;
constexpr std::size_t kPieceCountOffset = 0x10;
constexpr std::size_t kGapCountOffset = 0x12;
constexpr std::size_t kMaxPlayersOffset = 0x14;
constexpr std::size_t kThemeOffset = 0x18;
constexpr std::size_t kNameOffset = 0x1C;
constexpr std::size_t kMapNameBytes = 0x40;      // on disk, possibly unterminated
constexpr std::size_t kMapNameBufferSize = 0x41; // with the terminator

constexpr size_t kParkMapBytes = 0x3A98;
constexpr size_t kParkMapHeaderSize = 0x5C;
// The park's floor: a square grid of cells, each raised or dug in steps (the game's park code uses these too).
constexpr int kFloorSize = 58;       // the grid is 58 x 58 cells (saved x, then z)
constexpr int kFloorLevelMax = 15;   // the highest a floor cell or a piece can be raised, in steps
constexpr int kFloorLevelDepth = 16; // how many steps below zero it can be dug (the lowest is the negative); a saved
                                     // piece's height is stored with this added
constexpr size_t kParkMapFloorCells = size_t{kFloorSize} * kFloorSize;
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
    std::array<char, kMapNameBufferSize> name = {}; // (0x40 bytes on disk, possibly unterminated)
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
    std::array<End, 2> ends;
    std::array<char, kParkMapGapTextSize + 1> text = {};
    uint16_t points = 0;
    uint32_t cancel_flags = 0; // (current version only)
};
// NOLINTEND(readability-identifier-naming)
// `current`: the current version's 0x30-byte gaps (else 0x2C, no cancel flags).
bool read_park_map_gap(util::ByteReader& reader, ParkMapGap* gap, bool current);
bool write_park_map_gap(util::ByteWriter& writer, const ParkMapGap& gap);

// The map's checksum: the CRC of all of it past the checksum field.
uint32_t park_map_checksum(std::span<const uint8_t> map);

// The map as the integer array it is saved as (Park_editor_map: its bytes as little-endian words).
constexpr uint32_t kParkMapWords = kParkMapBytes / 4;
// The map's words into `words` (kParkMapWords of them).
void park_map_to_words(std::span<const uint8_t, kParkMapBytes> map, std::span<uint32_t> words);
// The map from `words` (at most kParkMapWords of them; the rest of the map zeroed).
void park_map_from_words(std::span<uint8_t, kParkMapBytes> map, std::span<const uint32_t> words);

// The park map as the park editor keeps it (park_editing::ParkLayout's +0x13C90): its bytes - saved and sent to the
// other players as they are - and its header's fields and floor heights read and written in place, at their places in
// the bytes (what the editor doesn't set stays as it was, as the original's in-place map kept it). The metapieces and
// gaps are read and written through the records above.
class ParkMap {
public:
    [[nodiscard]] std::span<uint8_t, kParkMapBytes> bytes() { return bytes_; }
    [[nodiscard]] std::span<const uint8_t, kParkMapBytes> bytes() const { return bytes_; }

    [[nodiscard]] uint32_t checksum() const { return word_at(0x00); }
    void set_checksum(uint32_t checksum) { put_word({.offset = 0x00, .value = checksum}); }
    [[nodiscard]] uint16_t extent() const { return half_at(0x04); }
    void set_extent(uint16_t extent) { put_half({.offset = 0x04, .value = extent}); }
    [[nodiscard]] uint16_t version() const { return half_at(kVersionOffset); }
    void set_version(uint16_t version) { put_half({.offset = kVersionOffset, .value = version}); }
    [[nodiscard]] uint16_t shell_size() const { return half_at(kShellSizeOffset); }
    void set_shell_size(uint16_t size) { put_half({.offset = kShellSizeOffset, .value = size}); }
    void set_reserved_0a(uint16_t value) { put_half({.offset = kReserved0aOffset, .value = value}); }
    // The build area: x, z, width, length.
    [[nodiscard]] uint8_t area_x() const { return core::at(bytes_, kAreaXOffset); }
    [[nodiscard]] uint8_t area_z() const { return core::at(bytes_, kAreaZOffset); }
    [[nodiscard]] uint8_t area_width() const { return core::at(bytes_, kAreaWidthOffset); }
    [[nodiscard]] uint8_t area_length() const { return core::at(bytes_, kAreaLengthOffset); }
    // A park's area in cells: its corner (x, z), its width and its length.
    struct MapArea {
        uint8_t x{};
        uint8_t z{};
        uint8_t width{};
        uint8_t length{};
    };

    void set_area(MapArea area);
    [[nodiscard]] uint16_t piece_count() const { return half_at(kPieceCountOffset); }
    void set_piece_count(uint16_t count) { put_half({.offset = kPieceCountOffset, .value = count}); }
    [[nodiscard]] uint16_t gap_count() const { return half_at(kGapCountOffset); }
    void set_gap_count(uint16_t count) { put_half({.offset = kGapCountOffset, .value = count}); }
    [[nodiscard]] uint8_t max_players() const { return core::at(bytes_, kMaxPlayersOffset); }
    void set_max_players(uint8_t players) { core::at(bytes_, kMaxPlayersOffset) = players; }
    [[nodiscard]] uint32_t theme() const { return word_at(kThemeOffset); }
    void set_theme(uint32_t theme) { put_word({.offset = kThemeOffset, .value = theme}); }
    // The park's name in place (0x40 characters, terminated within them when it is shorter).
    [[nodiscard]] std::span<char, kMapNameBytes> name();
    [[nodiscard]] std::span<const char, kMapNameBytes> name() const;
    // The floor heights, cell x * 58 + z.
    [[nodiscard]] int8_t floor_height(size_t cell) const;
    void set_floor_height(size_t cell, int8_t height);

private:
    [[nodiscard]] uint16_t half_at(size_t offset) const;
    [[nodiscard]] uint32_t word_at(size_t offset) const;
    // A 16-bit value and where it is written.
    struct HalfAt {
        size_t offset{};
        uint16_t value{};
    };

    void put_half(HalfAt half);
    // A 32-bit value and where it is written.
    struct WordAt {
        size_t offset{};
        uint32_t value{};
    };

    void put_word(WordAt word);

    std::array<uint8_t, kParkMapBytes> bytes_{};
};
static_assert(sizeof(ParkMap) == kParkMapBytes, "the park editor keeps the map's bytes");

} // namespace park_editing
