// The engine's viewports seen as a list (see viewport_list.h).

#include "engine/gfx/viewport_list.h"
#include "engine/gfx/viewport.h"
#include "engine/core/checked_index.h"

namespace render {

namespace {

// The ten viewports (0x00701350).
ViewRegion* get_viewport(int index) { return core::at(g_viewports, index); }

} // namespace

// 0x004A06D0
ViewRegion* fetch_current_view(int n)
{
    const int* map = g_mode_map;
    int shown = 0;
    for (int view = 0; view < 10; ++view) {
        if (map[view] < 0)
            continue;
        if (shown == n)
            return get_viewport(map[view]);
        ++shown;
    }
    return nullptr;
}

// 0x004A0710
int get_view_layout() { return g_screen_mode; }

// 0x004A0720
bool flag_camera_for_removal(gfx::ViewCam* camera)
{
    for (int i = 0; i < 10; ++i) {
        ViewRegion* viewport = get_viewport(i);
        // (ViewRegion::flag_camera_for_removal)
        if (viewport && viewport->flag_camera_for_removal(camera))
            return true;
    }
    return false;
}

} // namespace render
