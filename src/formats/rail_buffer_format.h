#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "formats/byte_stream.h"

// NOT ORIGINAL - the created park's packed rails (the rail editor's buffer: network message 0x7A "rail data", sent
// whole), read and checked through util::ByteReader.
//
// The buffer (little-endian, 0x1132 bytes): u16 rail count (at most kMaxRails), kMaxRails u16 point counts (each
// rail's), then kMaxPoints point records of 5 u16 {x, y, z (signed, 1/8 units), the level's height (1/16), has a
// post} - each rail's points from its first laid, the rails from the first laid; the points' total at most
// kMaxPoints.
//
// The rail editor keeps the buffer's bytes (park_editing::RailAuthoring's +0x34, sent as they are); RailBuffer is the
// buffer decoded, the record the packer fills and the unpacker reads.
namespace park_editing {

constexpr std::size_t kRailPointCounts = 200;
constexpr std::size_t kRailRecordFields = 5;
constexpr std::size_t kRailRecords = 400;

struct RailBuffer {
    uint16_t rail_count_;
    std::array<uint16_t, kRailPointCounts> point_counts_;
    std::array<std::array<uint16_t, kRailRecordFields>, kRailRecords> records_;
};

namespace rail_buffer_format {

constexpr int kMaxRails = 200;
constexpr int kMaxPoints = 400;
constexpr size_t kSize = 0x1132;
// The buffer's bytes.
using Bytes = std::array<uint8_t, kSize>;

// The buffer from `reader` into `out`. False (out cleared: no rails) if cut short, more than kMaxRails rails or the
// rails' points more than kMaxPoints.
bool read_rail_buffer(util::ByteReader& reader, RailBuffer* out);
// `buffer` into `writer` (kSize bytes); false if there isn't room.
bool write_rail_buffer(util::ByteWriter& writer, const RailBuffer& buffer);
// Whether a buffer's counts are within its arrays (what read_rail_buffer checks).
bool counts_fit(const RailBuffer& buffer);

} // namespace rail_buffer_format
} // namespace park_editing
