#pragma once

#include <cstddef>
#include <cstdint>

#include "formats/byte_stream.h"

// NOT ORIGINAL - the created park's packed rails (the rail editor's buffer: network message 0x7A "rail data", sent
// whole), read and checked through util::ByteReader.
//
// The buffer (little-endian, 0x1132 bytes): u16 rail count (at most kMaxRails), kMaxRails u16 point counts (each
// rail's), then kMaxPoints point records of 5 u16 {x, y, z (signed, 1/8 units), the level's height (1/16), has a
// post} - each rail's points from its first laid, the rails from the first laid; the points' total at most
// kMaxPoints.
//
// RailBuffer is the rail editor's own copy of it (park_editing::RailAuthoring's +0x34): the file-format view the
// packer fills in place.
namespace park_editing {

struct RailBuffer {
    uint16_t rail_count_;        // +0x00
    uint16_t point_counts_[200]; // +0x02
    uint16_t records_[400][5];   // +0x192
};
static_assert(sizeof(RailBuffer) == 0x1132, "file format: the rail buffer");

namespace rail_buffer_format {

constexpr int kMaxRails = 200;
constexpr int kMaxPoints = 400;
constexpr size_t kSize = sizeof(RailBuffer);

// The buffer from `reader` into `out`. False (out cleared: no rails) if cut short, more than kMaxRails rails or the
// rails' points more than kMaxPoints.
bool read_rail_buffer(util::ByteReader& reader, RailBuffer* out);
// Whether a buffer's counts are within its arrays (what read_rail_buffer checks).
bool counts_fit(const RailBuffer& buffer);

} // namespace rail_buffer_format
} // namespace park_editing
