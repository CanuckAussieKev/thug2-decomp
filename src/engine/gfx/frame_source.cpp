// An animation frame source: see frame_source.h.

#include "engine/gfx/frame_source.h"

#include <cstdint>
#include <cstring>

#include "engine/assets/resource_registry.h"
#include "engine/objects/cutscene.h"
#include "engine/core/code_address.h"

namespace render {

namespace {

extern void** const kFrameSourceVtable;

} // namespace

// 0x0048CF50
void* __cdecl get_anim_data(uint32_t checksum, int unused)
{
    if (!checksum)
        return nullptr;
    return assets::asset_manager()->get_asset_data(checksum, unused);
}

// 0x0048CF70
FrameSource* FrameSource::make_new()
{
    vtable = reinterpret_cast<void**>(kFrameSourceVtable);
    std::memset(reinterpret_cast<uint8_t*>(this) + 8, 0, sizeof(FrameSource) - 8);
    is_finished_ = false;
    anim_ = 0;
    return this;
}

// 0x0048D000
FrameSource* FrameSource::teardown(unsigned int flags)
{
    vtable = reinterpret_cast<void**>(kFrameSourceVtable);
    if (flags & 1)
        objects::anim_source_free(this);
    return this;
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

namespace {
static VtableSlot g_frame_source_vtable_slots[] = {&render::FrameSource::teardown};
void** const kFrameSourceVtable = reinterpret_cast<void**>(g_frame_source_vtable_slots);
} // namespace

} // namespace render
