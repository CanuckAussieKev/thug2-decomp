#pragma once

#include <cstdint>

#include "editor/level_editor.h"

// NOT ORIGINAL - the gizmos' lines drawn in the 3D view against the world's depth (ours; engine/gfx/depth_lines.h):
// rails, ladders and paths, rail points and trigger volumes. Where a line is hidden by what the world drew in front of
// it, it is drawn as the View menu's Gizmos Behind Geometry says ([editor] gizmos_behind= in thug2-decomp.ini):
//   - X-Ray: as where it shows - every gizmo through everything (the editor's look before this);
//   - Faded (the default): a quarter as strong and dashed;
//   - Hidden: not at all.
// Per pixel: a rail half behind a wall fades only where the wall hides it. The selected and the hovered gizmo are always
// drawn at full strength over everything. Lines wholly off the view are dropped; rails, ladders and paths fade out
// toward the Rail Distance preference ([editor] rail_range=).
//
// The lines are gathered while the frame's overlay is made (after the frame was drawn) and drawn in the next frame's
// 3D view with its camera: the world-space lines stay on their rails while the camera moves.
namespace frontend::level_editor::gizmo_lines {

enum class Behind : uint8_t { kXRay = 0, kFaded = 1, kHidden = 2 };
constexpr int kBehindCount = 3;
constexpr int kDefaultBehind = 1; // Faded
// Its name in the menu.
const char* behind_name(Behind behind);
// The setting now, and changed (prefs: kept in the ini).
Behind behind_now();
void set_behind(Behind behind);
// The View menu's Gizmos Behind Geometry submenu (its three choices).
void behind_menu();

// The view the frame's lines are culled and dashed in.
struct View {
    V3 camera{};
    TickCycle frame{};
    Lens lens{};
    float viewport_width{};
};

// The frame's lines: begun (none yet), added, handed to the renderer.
void begin_frame(const View& view);
void end_frame();

struct Stroke {
    V3 a{};
    V3 b{};
    uint32_t argb{};
    float width{}; // pixels
    bool emphasised = false;  // selected / hovered: at full strength over everything
    bool dash_behind = true;  // where hidden: dashed (false: solid)
};
void add(const Stroke& stroke);
// A rail's point: a dot `radius` pixels across the view at `at`.
void add_dot(V3 at, float radius, uint32_t argb);
// The split view (level_editor_split.h): the lines are gathered once, in the view under the mouse, and the renderer draws
// them in every view - so none are dropped as off that view (`all_views`), and the other views' overlays add none
// (`muted` while they are made).
void set_all_views(bool all_views);
void set_muted(bool muted);

// The pieces (pure).
// What a line keeps of its opacity at `distance` with draw distance `range`: all of it to 60% of the range, then less to
// none at the range.
float distance_fade(float distance, float range);
// `argb` with its alpha times `factor` (0-1).
uint32_t scale_alpha(uint32_t argb, float factor);
// A view's sides (half its width / height at depth 1) and its near plane.
struct ViewSides {
    float tan_x{};
    float tan_y{};
    float near_z{};
};
// Whether a line from `a` to `b` (view space: x right, y up, z the depth) can be seen through `sides`: false when both
// ends are behind the near plane or past the same side.
bool in_view(V3 a, V3 b, const ViewSides& sides);

} // namespace frontend::level_editor::gizmo_lines
