#include "engine/file/bundle_format.h"

namespace bundle_format {

bool read_bundle_table(util::ByteReader& reader, Entry* out, int capacity, int* count)
{
    *count = 0;
    reader.u32(); // the version
    uint32_t entries = reader.u32();
    if (!reader.ok() || entries > static_cast<uint32_t>(capacity))
        return false;
    size_t file_size = reader.size();
    for (uint32_t i = 0; i < entries; ++i) {
        Entry& entry = out[i];
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

} // namespace bundle_format
