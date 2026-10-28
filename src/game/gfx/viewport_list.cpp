// The engine's viewports seen as a list (see viewport_list.h).

#include "game/gfx/viewport_list.h"

#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

// The ten viewports (0x00701350).
CViewport* Viewport(int index) { return (&GameVariable<CViewport*>(0x00701350))[index]; }

} // namespace

// 0x004A06D0
CViewport* __cdecl GetActiveViewport(int n)
{
    const int* map = GameVariable<const int*>(0x0067E540);
    int shown = 0;
    for (int view = 0; view < 10; ++view) {
        if (map[view] < 0)
            continue;
        if (shown == n)
            return Viewport(map[view]);
        ++shown;
    }
    return nullptr;
}

// 0x004A0710
int __cdecl GetScreenMode() { return GameVariable<int>(0x0070134C); }

// 0x004A0720
bool __cdecl MarkCameraForDeletion(Gfx::Camera* camera)
{
    for (int i = 0; i < 10; ++i) {
        CViewport* viewport = Viewport(i);
        // (CViewport::MarkCameraForDeletion)
        if (viewport && GameFunction<bool(__thiscall*)(CViewport*, Gfx::Camera*)>(0x004A0D50)(viewport, camera))
            return true;
    }
    return false;
}

} // namespace Nx

REPLACE(0x004A06D0, Nx::GetActiveViewport);
REPLACE(0x004A0710, Nx::GetScreenMode);
REPLACE(0x004A0720, Nx::MarkCameraForDeletion);
