// An animation frame source: see frame_source.h.

#include "engine/gfx/frame_source.h"

#include <cstdint>
#include <cstring>

#include "engine/assets/resource_registry.h"
#include "engine/objects/cutscene.h"
#include "engine/gfx/scene_load.h"

namespace render {

// 0x0048CF50
void* __cdecl get_anim_data(uint32_t checksum, int unused)
{
    if (!checksum)
        return nullptr;
    return assets::asset_manager()->get_asset_data(checksum, unused);
}

// (0x004B3990 / the cutscene's 0x00464560: the pools' carve; 0x004645E0 frees)
void* FrameSource::operator new(std::size_t size) noexcept
{
    return g_frame_source_pools.carve(static_cast<unsigned int>(size));
}
void FrameSource::operator delete(void* source)
{
    if (source)
        objects::anim_source_free(source);
}

// 0x0048CF70
FrameSource::FrameSource()
{
    std::memset(reinterpret_cast<uint8_t*>(this) + 8, 0, sizeof(FrameSource) - 8);
    is_finished_ = false;
    anim_ = 0;
}

// 0x0048CFA0
void* FrameSource::set_done(bool done)
{
    is_finished_ = done;
    if (done)
        return nullptr;
    return get_anim_data(anim_, 1);
}

// 0x0048CFD0
void* FrameSource::play_clip(uint32_t anim)
{
    anim_ = anim;
    is_finished_ = false;
    get_anim_data(anim, 1); // (looked up twice, the first result not used)
    return get_anim_data(anim_, 1);
}

} // namespace render
