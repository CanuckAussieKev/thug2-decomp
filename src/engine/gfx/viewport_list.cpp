// The engine's viewports seen as a list (see viewport_list.h).

#include "engine/gfx/viewport_list.h"
#include "engine/gfx/viewport.h"

namespace Engine3D {

namespace {

// The ten viewports (0x00701350).
CViewRegion* Viewport(int index) { return g_viewports[index]; }

} // namespace

// 0x004A06D0
CViewRegion* __cdecl FetchCurrentView(int n)
{
    const int* map = g_modeMap;
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
int __cdecl GetViewLayout() { return g_screenMode; }

// 0x004A0720
bool __cdecl FlagCameraForRemoval(Gfx2D::ViewCam* camera)
{
    for (int i = 0; i < 10; ++i) {
        CViewRegion* viewport = Viewport(i);
        // (CViewRegion::FlagCameraForRemoval)
        if (viewport && viewport->FlagCameraForRemoval(camera))
            return true;
    }
    return false;
}

} // namespace Nx
