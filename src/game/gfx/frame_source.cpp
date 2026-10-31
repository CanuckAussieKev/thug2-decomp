// An animation frame source: see frame_source.h.

#include "game/gfx/frame_source.h"

#include <cstdint>
#include <cstring>

#include "game/ass/assman.h"
#include "game/obj/cutscene.h"
#include "game/core/code_address.h"

namespace Nx {

namespace {

extern void** const kFrameSourceVtable;

} // namespace

// 0x0048CF50
void* __cdecl GetAnimData(uint32_t checksum, int unused)
{
    if (!checksum)
        return nullptr;
    return Ass::AssetManager()->GetAssetData(checksum, unused);
}

// 0x0048CF70
CFrameSource* CFrameSource::Construct()
{
    vtable = reinterpret_cast<void**>(kFrameSourceVtable);
    std::memset(reinterpret_cast<uint8_t*>(this) + 8, 0, sizeof(CFrameSource) - 8);
    m_done = false;
    m_anim = 0;
    return this;
}

// 0x0048D000
CFrameSource* CFrameSource::Destroy(unsigned int flags)
{
    vtable = reinterpret_cast<void**>(kFrameSourceVtable);
    if (flags & 1)
        Obj::AnimSourceFree(this);
    return this;
}

// 0x0048CFA0
void* CFrameSource::SetDone(bool done)
{
    m_done = done;
    if (done)
        return nullptr;
    return GetAnimData(m_anim, 1);
}

// 0x0048CFD0
void* CFrameSource::SetAnim(uint32_t anim)
{
    m_anim = anim;
    m_done = false;
    GetAnimData(anim, 1); // (looked up twice, the first result not used)
    return GetAnimData(m_anim, 1);
}

namespace {
static VtableSlot kFrameSourceVtable_slots[] = {&Nx::CFrameSource::Destroy};
void** const kFrameSourceVtable = reinterpret_cast<void**>(kFrameSourceVtable_slots);
} // namespace

} // namespace Nx
