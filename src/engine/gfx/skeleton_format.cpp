#include "engine/gfx/skeleton_format.h"

namespace skeleton_format {

bool read_skeleton(util::ByteReader& reader, Skeleton* out)
{
    out->version = reader.u32();
    out->flags = reader.u32();
    const uint32_t count = reader.u32();
    if (!reader.ok() || count > static_cast<uint32_t>(kMaxBones))
        return false;
    out->count = static_cast<int>(count);
    for (uint32_t i = 0; i < count; ++i)
        out->names[i] = reader.u32();
    for (uint32_t i = 0; i < count; ++i)
        out->parents[i] = reader.u32();
    for (uint32_t i = 0; i < count; ++i)
        out->mirrors[i] = reader.u32();
    for (uint32_t i = 0; i < count; ++i) {
        for (float& value : out->poses[i].rotation)
            value = reader.f32();
        for (float& value : out->poses[i].translation)
            value = reader.f32();
    }
    return reader.ok();
}

} // namespace skeleton_format
