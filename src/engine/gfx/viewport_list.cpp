// The engine's viewports seen as a list (see viewport_list.h).

#include "engine/gfx/viewport_list.h"
#include "engine/gfx/viewport.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/view_setup.h"

#include <cstddef>
#include <span>

namespace render {

constexpr int kViewportCount = 10;

namespace {

// The ten viewports (0x00701350).
ViewRegion* get_viewport(int index) { return core::at(view_setup().viewports(), index); }

} // namespace

// 0x004A06D0
ViewRegion* fetch_current_view(int n)
{
    const std::span<const int> map = view_setup().mode_map();
    int shown = 0;
    for (int view = 0; view < kViewportCount; ++view) {
        // (past the table's end the original read the words after it: those views are not shown)
        const int entry = static_cast<size_t>(view) < map.size() ? core::at(map, view) : -1;
        if (entry < 0)
            continue;
        if (shown == n)
            return get_viewport(entry);
        ++shown;
    }
    return nullptr;
}

// 0x004A0710
int get_view_layout() { return view_setup().screen_mode(); }

// 0x004A0720
bool flag_camera_for_removal(gfx::ViewCam* camera)
{
    for (int i = 0; i < kViewportCount; ++i) {
        ViewRegion* viewport = get_viewport(i);
        // (ViewRegion::flag_camera_for_removal)
        if (viewport && viewport->flag_camera_for_removal(camera))
            return true;
    }
    return false;
}

} // namespace render
