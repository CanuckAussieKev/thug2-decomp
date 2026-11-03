#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "editor/level_editor.h"
#include "platform/keyboard.h"

// NOT ORIGINAL - the level editor's camera tools (ours; see level_editor.h), after Unreal's and Blender's:
//   - F frames the selection: the camera flies (a smooth move, a third of a second) to see all of it from the side it
//     looks from now. [editor] speed_by_selection=1 also sets the flying speed from its size (Unreal's).
//   - Alt+left drag orbits: round the selection, or round the point of the surface under the cursor where the drag
//     started (TrenchBroom's) - the camera turns rigidly about it, so that point stays where it is on the screen; the
//     wheel while orbiting brings the camera nearer / further. Alt+click without moving is still Alt+click.
//   - The wheel with the right button held changes the flying speed (Unreal's), shown in the status bar.
//   - Camera bookmarks: Ctrl+1..9 stores the camera (place, heading, ortho view), 1..9 flies back to it - per level,
//     kept in thug2-decomp-layouts\<scene>.cameras.txt (a file of their own: the layout's format is unchanged).
//   - Orthographic views (Blender's numpad): 7 top, 1 front (looking along +z), 3 right (looking along -x); Ctrl+7 /
//     1 / 3 the opposite ones; 5 switches between perspective and orthographic for the view as it is. Looking about (the
//     right button, Alt+drag) goes back to perspective. In an ortho view the wheel and W / S zoom, A / D and Q / E pan.
//     The view is near-orthographic: the renderer's own perspective with a long lens from far back (the camera at
//     eye_distance along the view, the lens narrowed to keep the picture's size - adjust_view_angle - and the near plane brought
//     up to keep the depth buffer's precision - get_near_plane), so the game's culling, the picking rays and every gizmo
//     stay right without a projection of our own (the renderer-independence rules: nothing Direct3D here).
// Flying's acceleration ([editor] fly_accel_ms) is fly_ramp's.
namespace frontend::level_editor::view_cam {

// --- the pieces (pure) ------------------------------------------------------------------------------------------

// A camera pose: the editor's place, yaw and pitch (frame_from_yaw_pitch's).
struct Pose {
    V3 pos;
    float yaw, pitch;
};
// The smooth move from one pose to another: `t` 0..1 eased (smoothstep), the yaw the shorter way round.
Pose get_blend(const Pose& from, const Pose& to, float t);
float get_ease(float t);
// The angle `to` brought within half a turn of `from` (the yaw's shorter way).
float nearest_angle(float from, float to);

// What a view is fitted to: the radius, the half angle's tangent and the least distance.
struct FrameFit {
    float radius{};
    float tan_half_{};
    float minimum{};
};

// Framing: how far from a sphere of `radius` the camera must be to see all of it through a lens of half-angle tangent
// `tan_half` (the narrower of the two), with a margin (1.15), never nearer than `minimum`.
float frame_distance(FrameFit frame_fit);
// Speed from the selection's size ([editor] speed_by_selection): 4 times the radius a second, 100..20000.
float speed_for_radius(float radius);

// Orbiting: the camera turned rigidly about `pivot` - from `before` to `after` (frames) - keeping where the pivot is in
// the view: the offset from the pivot in the old frame's terms put back in the new frame's.
V3 orbit_about(V3 pos, V3 pivot, const TickCycle& before, const TickCycle& after);

// Flying's ramp ([editor] fly_accel_ms): the fraction of full speed after `held_seconds` of holding the keys (0 ms:
// always 1); a smoothstep up to 1 over the time.
float fly_ramp(float held_seconds, int accel_ms);

// Ortho views. `equivalent`: the distance the picture's size matches in perspective (what the zoom changes); the eye
// is eye_distance(equivalent) back along the view, the lens narrowed by equivalent / eye.
enum class View : uint8_t { kNone, kTop, kBottom, kMenuSystem, kBack, kRight, kLeft, kDealloc };
float eye_distance(float equivalent);
// An orthographic view's base angle (degrees) and its equivalent.
struct OrthoAngle {
    float base_degrees_{};
    float equivalent{};
};

float ortho_angle(OrthoAngle ortho);
// The near plane for that eye (the default 2 when not ortho): 40% of the way to the pivot.
float ortho_near(float equivalent);
// An axis view's heading: yaw and pitch (top / bottom at +-89 degrees, the most the editor's frame takes).
void axis_yaw_pitch(View view, YawPitchOut out_view);
const char* view_name(View view);

// Camera bookmarks: nine a level, as their file's text (one line each: `<n> x y z yaw pitch view equivalent`, n 1..9;
// anything else on a line is skipped; floats kept exactly) - Parse is false for a file that isn't one.
struct Bookmark {
    bool set = false;
    Pose pose = {};
    View view = View::kNone;
    float equivalent = 0.0f;
};
constexpr int kBookmarks = 9;
std::string format_bookmarks(std::span<const Bookmark, kBookmarks> marks);
bool parse_bookmarks(const std::string& text, std::span<Bookmark, kBookmarks> marks);

// --- the editor's state (level_editor.cpp calls these) --------------------------------------------------------------

// What the tools work on: the editor's camera (its variables) and the selection it orbits (level_editor.cpp's rig_now).
struct Rig {
    V3* pos;
    float* yaw;
    float* pitch;
    float* orbit_distance_; // (with a selection the editor puts the camera this far back from `target` each frame)
    float* speed;           // the flying speed
    bool have_selection_;
    V3 target;                             // the selection's centre (what the camera orbits)
    float radius;                          // the selection's size (a sphere round its boxes)
    bool dragging;                         // a move under way (the wheel pushes it)
    Lens lens;                             // the last frame's (perspective: the game's angle)
    bool (*cast)(V3 from, V3 to, V3* hit); // the game's collision ray
    void (*focus)();                       // the camera made to look at the selection (focus_on_selection)
    void (*deselect)();
};

// The editor started (in the level `scene`: its bookmarks read) / stopped (a flight and an ortho view dropped).
void kickoff(uint32_t scene);
void halt();
// The bookmarks' file for a scene: "<8 hex digits>.cameras.txt" (pure).
std::string bookmarks_file_name(uint32_t scene);

// The mouse this frame, before the editor uses it: Alt+left press held back (an orbit if it moves 4 pixels, else
// given back as an Alt+click when let go), an orbit's movement and wheel taken, the wheel with the right button held
// taken as the flying speed; looking about ends an ortho view. `last_ray`: the previous frame's ray under the cursor
// (where an orbit's pivot is looked for).
struct MouseIn {
    int* left_presses_;
    int* left_releases_;
    bool left, right;
    float* rel_x_;
    float* rel_y_;
    int* wheel;
};
void filter_mouse(const Rig& rig, MouseIn mouse, bool alt, bool have_last_ray, V3 last_ray);
// Alt+drag orbiting now (the editor keeps the mouse captured meanwhile).
bool get_orbiting();
// After the editor's own look: the orbit's turn applied (about the pivot; with a selection only the heading - the
// editor puts the camera round it).
void apply_orbit(const Rig& rig);
// The keys: F, 1..9 / Ctrl+1..9, the numpad's 7 1 3 5 (`keys`: SDL's keyboard state; zeroed while the UI types).
void get_keys(const Rig& rig, platform::KeyState keys, bool ctrl, bool browser_open);
// At the end of the editor's frame: a flight moved on (`interrupted`: the user flew, looked or clicked), an ortho view's
// camera kept on its axis (the wheel's and W / S's moves along the view taken as its zoom, the rest as panning).
void after_update(const Rig& rig, int wheel, bool interrupted);
// Flying's ramp now: called each frame with whether a fly key is held.
float fly_factor(bool held, double seconds);
// The editor's camera put back where it was (play from here's return): a flight and an ortho view dropped.
void get_cancel();
// Whether an ortho view is on, and its pieces (for the editor's distance-based sizes and cut-offs).
bool ortho();
View current_view();
// A distance from the camera as the picture shows it: in an ortho view the far eye's distances scaled down to the
// view's equivalent (the move arrows' length, the gizmos' cut-offs); the distance itself otherwise.
float apparent(float distance);
// The renderer's hooks (render::draw_viewport, for the editor's camera only): the field of view and the near plane.
float adjust_view_angle(float base_degrees);
float get_near_plane();
// For the status bar: "Top (ortho)", "flying to the selection"... (empty: nothing to say).
std::string get_standing();

} // namespace frontend::level_editor::view_cam
