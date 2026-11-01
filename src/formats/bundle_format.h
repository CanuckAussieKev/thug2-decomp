#pragma once

#include <cstddef>
#include <cstdint>

#include "formats/byte_stream.h"

// NOT ORIGINAL - the asset bundle format (the cutscenes' .cut files, file::AssetBundle), read through
// util::ByteReader.
//
// A bundle (little-endian): u32 version (1), u32 entry count, then each entry {u32 offset of the asset from the
// start of the file, u32 size, u32 name checksum, u32 type checksum}, then the assets' data.
namespace bundle_format {

constexpr size_t kHeaderSize = 8;
constexpr size_t kEntrySize = 16;

struct Entry {
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t name = 0;
    uint32_t type = 0;
};

// The table: the count (false past `capacity`) and the entries into `out` - each asset within the file (false at
// the first that isn't). `*count` gets the entry count read.
bool read_bundle_table(util::ByteReader& reader, Entry* out, int capacity, int* count);

// A skinned model's colour asset (type 0x2CD4107D): u32 (not read), i32 entry count, entries of kSkinColourSize
// bytes (the skin's colour table: render::set_screen_clip's).
constexpr size_t kSkinColourSize = 12;
struct SkinColours {
    int count = 0;
    const uint8_t* entries = nullptr; // in place
};
// False if cut short or the count is negative.
bool read_skin_colours(util::ByteReader& reader, SkinColours* out);

} // namespace bundle_format
