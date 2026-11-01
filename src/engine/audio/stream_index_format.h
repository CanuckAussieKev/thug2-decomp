#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/util/byte_stream.h"

// NOT ORIGINAL - the streamed sounds' index ("data\streams\pcm\pcm.dat", of pcm.wad's Bink streams), read through
// util::ByteReader.
//
// An index (little-endian): u32 entry count, then each entry {u32 name checksum, u32 offset in pcm.wad, u32 size}.
// Read by audio::init_platform (engine/audio/stream.cpp) into its WadEntry table.
namespace stream_index_format {

constexpr size_t kEntrySize = 12;

struct Entry {
    uint32_t checksum = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
};

// The entry count, false if cut short or more entries than the index holds.
inline bool read_count(util::ByteReader& reader, uint32_t* count)
{
    *count = reader.u32();
    return reader.ok() && *count <= reader.remaining() / kEntrySize;
}

// The next entry; false if cut short. `within`: whether it lies inside a pcm.wad of `wad_size` bytes.
inline bool read_entry(util::ByteReader& reader, Entry* out)
{
    out->checksum = reader.u32();
    out->offset = reader.u32();
    out->size = reader.u32();
    return reader.ok();
}
inline bool within(const Entry& entry, uint64_t wad_size)
{
    return entry.offset <= wad_size && entry.size <= wad_size - entry.offset;
}

} // namespace stream_index_format
