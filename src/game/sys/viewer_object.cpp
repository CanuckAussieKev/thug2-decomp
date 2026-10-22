// The model viewer's object (0x005897B0-0x0058ADB0). See viewer_object.h.

#include "game/sys/viewer_object.h"

#include <bit>
#include <cstdint>

#include "game/ass/assman.h"
#include "game/core/manager.h"
#include "game/core/task.h"
#include "game/obj/anim_blender.h"
#include "game/script/cscript.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kViewerObjectVtable = reinterpret_cast<void**>(0x0064F138);
constexpr uint32_t kKillViewerObjectPanel = 0x9E65A71E; // kill_viewer_object_panel

CAnimBlenderComponent* Animation(CCompositeObject* object)
{
    return static_cast<CAnimBlenderComponent*>(object->GetComponent(CAnimBlenderComponent::kType));
}

} // namespace

// 0x005897B0
void CViewerObject::ShutDown() { Script::RunScript(kKillViewerObjectPanel, nullptr, nullptr, false, nullptr); }

// 0x005897D0
void CViewerObject::PlayAnimation(uint32_t anim)
{
    // (The original looks the component up again for each call.)
    if (!Animation(this) || !Animation(this)->HasValue(anim))
        return;
    const float length = Animation(this)->GetValue(anim);
    Animation(this)->PlayAnimationEx(anim, 0, 0, std::bit_cast<uint32_t>(length), 1, std::bit_cast<uint32_t>(0.3f),
                                     std::bit_cast<uint32_t>(1.0f));
}

// 0x00589860
void CViewerObject::DestroyBody()
{
    m_vtable = kViewerObjectVtable;
    m_task->Remove(); // (not null-checked: as the original)
    if (m_asset) {
        Ass::CAssMan* assets = Ass::AssetManager();
        if (Ass::CAsset* asset = assets->GetAsset(m_asset, 0)) {
            assets->UnloadAsset(asset);
            assets->GetAsset(m_asset, 0);
        }
        m_asset = 0;
    }
    if (m_task)
        reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*reinterpret_cast<void***>(m_task))[2])(m_task, 1);
    CCompositeObject::DestroyBody();
}

// 0x0058A2B0
CViewerObject* CViewerObject::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

} // namespace Obj

REPLACE(0x005897B0, &Obj::CViewerObject::ShutDown);
REPLACE(0x005897D0, &Obj::CViewerObject::PlayAnimation);
REPLACE(0x00589860, &Obj::CViewerObject::DestroyBody);
REPLACE(0x0058A2B0, &Obj::CViewerObject::Destroy);
