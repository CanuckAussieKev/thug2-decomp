#pragma once

#include <cstdint>

namespace gfx {
class ViewCam;
}

// The engine's viewports seen as a list (Nx): the active ones by index, the screen mode, a camera marked in all of
// them. (The viewports themselves - render::ViewRegion, ten at 0x00701350 - are in viewport.h.)
namespace render {

class ViewRegion;

// 0x004A06D0: the n-th active viewport - the n-th view of the screen mode's map (0x0067E540: ten entries, -1 for a
// view not shown) that's shown. Null if there are fewer.
ViewRegion* fetch_current_view(int n);
// 0x004A0710: the screen mode (0x0070134C: one_camera, split_vertical, ...).
int get_view_layout();
// 0x004A0720: `camera` marked for deletion in the first viewport showing it (ViewRegion::flag_camera_for_removal
// 0x004A0D50). Whether one did.
bool flag_camera_for_removal(gfx::ViewCam* camera);

} // namespace render
