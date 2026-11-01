#include "engine/file/pre_format.h"

#include <cstring>

namespace pre_format {

bool read_archive_header(util::ByteReader& reader, ArchiveHeader* out)
{
    out->total_size = reader.u32();
    out->version = reader.u32();
    out->file_count = reader.i32();
    return reader.ok() && out->file_count >= 0;
}

bool read_file_record(util::ByteReader& reader, FileRecord* out)
{
    out->offset = reader.position();
    out->size = reader.i32();
    out->compressed_size = reader.i32();
    out->name_size = reader.u16();
    out->use_count = reader.u16();
    out->checksum = reader.u32();
    if (!reader.ok() || out->size < 0 || out->compressed_size < 0 || out->name_size == 0)
        return false;
    out->name = reinterpret_cast<const char*>(reader.cursor());
    if (!reader.skip(out->name_size) || std::memchr(out->name, 0, out->name_size) == nullptr)
        return false;
    out->data = reader.cursor();
    uint32_t stored = out->stored_size();
    if (!reader.skip(stored))
        return false;
    // (the padding: the last file's may be missing at the end of the archive)
    size_t pad = ((stored + 3) & ~3u) - stored;
    return reader.skip(pad < reader.remaining() ? pad : reader.remaining());
}

bool read_patch_header(util::ByteReader& reader, PatchHeader* out)
{
    out->patched_size = reader.u32();
    out->record_count = reader.u32();
    return reader.ok();
}

bool apply_patch(uint8_t* archive, size_t capacity, const uint8_t* patch, size_t patch_size)
{
    util::ByteReader reader(patch, patch_size);
    PatchHeader header;
    if (!read_patch_header(reader, &header))
        return false;
    for (uint32_t i = 0; i < header.record_count; ++i) {
        uint32_t offset = reader.u32();
        uint32_t length = reader.u32();
        const uint8_t* bytes = reader.cursor();
        if (!reader.skip(length) || offset > capacity || length > capacity - offset)
            return false;
        std::memcpy(archive + offset, bytes, length);
    }
    return true;
}

} // namespace pre_format
