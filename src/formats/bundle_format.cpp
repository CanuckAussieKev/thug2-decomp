#include "engine/core/checked_index.h"
#include "formats/bundle_format.h"
#include "engine/core/ints.h"

namespace bundle_format {

bool read_bundle_table(util::ByteReader& reader, Entry* out_entries, int capacity, int* count)
{
    const std::span<Entry> out(out_entries, static_cast<size_t>(capacity));
    *count = 0;
    reader.u32(); // the version
    const uint32_t entries = reader.u32();
    if (!reader.ok() || entries > ints::as_unsigned(capacity))
        return false;
    const size_t file_size = reader.size();
    for (uint32_t i = 0; i < entries; ++i) {
        Entry& entry = core::at(out, i);
        entry.offset = reader.u32();
        entry.size = reader.u32();
        entry.name = reader.u32();
        entry.type = reader.u32();
        if (!reader.ok() || entry.offset > file_size || entry.size > file_size - entry.offset)
            return false;
        *count = static_cast<int>(i) + 1;
    }
    return true;
}

bool read_skin_colours(util::ByteReader& reader, SkinColours* out)
{
    reader.u32();
    out->count = reader.i32();
    out->entries = reader.cursor();
    return reader.ok() && out->count >= 0 && reader.skip(static_cast<size_t>(out->count) * kSkinColourSize);
}

} // namespace bundle_format
