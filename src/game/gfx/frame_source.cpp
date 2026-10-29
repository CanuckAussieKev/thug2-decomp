// An animation frame source: see frame_source.h.

#include "game/gfx/frame_source.h"

#include <cstring>

#include "game/ass/assman.h"
#include "game/obj/cutscene.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kFrameSourceVtable = 0x00648A68;

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

} // namespace Nx

REPLACE(0x0048CF50, Nx::GetAnimData);
REPLACE(0x0048CF70, &Nx::CFrameSource::Construct);
REPLACE(0x0048D000, &Nx::CFrameSource::Destroy);
REPLACE(0x0048CFA0, &Nx::CFrameSource::SetDone);
REPLACE(0x0048CFD0, &Nx::CFrameSource::SetAnim);
