#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>
#include <array>

#include "engine/maths/vec2.h"
#include "editor/level_editor.h"

// NOT ORIGINAL - the level editor's transform tools (ours; see level_editor.h):
// Move / Rotate / Scale gizmos, World / Local space, the pivot, typed values while dragging, the snapping settings
// (level_editor_snap.h does the snapping itself) and the repeat of the last duplicate. What's here is the tools' state
// and their pure parts; level_editor.cpp applies them to the
// selection (its drag code calls these).
//
// The keys (the ini's [editor] wasd_flies= picks the scheme - the UX research: Unity, Unreal and Godot fly only while the right
// mouse button is held, and their letter keys pick tools):
//   wasd_flies=0 (the default): W A S D / Q E fly only while the right button is held (Shift faster); without it W / E / R
//     pick Move / Rotate / Scale (Unreal's keys), Space cycles them (Unreal), the arrow keys nudge the selection (Up /
//     Down along the view, Left / Right across; Page Up / Down up / down) and F frames the selection again (flying with
//     a selection frees the camera from it).
//   wasd_flies=1 (the editor as it was): W A S D / Q E fly or nudge whatever the mouse does, so the modes are on Space
//     (cycles, with a selection - without one Space still flies up) and the toolbar's buttons.
//   Both: ` (the key under Escape) switches World / Local (Unreal's Ctrl+`), Alt+G starts the mode's transform without
//   holding the mouse (Blender's G / R / S - plain G stays Ground): the mouse moves it, a click or Enter applies it,
//   Escape puts it back.
// While dragging (or in that modal transform): X / Y / Z constrain it to an axis (again: free), digits with - and .
// type an exact amount (Move: units along the axis - X when none; Rotate: degrees of heading; Scale: a factor - on the
// axis, else all three), Backspace erases, Enter applies, Escape cancels. The status bar shows what's typed.
namespace frontend::level_editor::xform {

enum class Mode : uint8_t { kShiftBy, kSpinBy, kScale };
enum class Space : uint8_t { kWorld, kLocal };
// Base: as the editor always did (objects turn about the first one's position, scenery about its pivot; scenery scales
// about the middle of its bottom, objects each about its position). Centre: about the middle of the selection's box.
// Own: each object about its own position (scenery's group keeps its pivot - one group moves as one).
enum class Pivot : uint8_t { kBase, kCentre, kOwn };
// What snapping snaps to (the toolbar's Snap to - used while Snap is on; the held keys pick one for a drag).
enum class SnapTarget : uint8_t { kGrid, kWorldGrid, kSurface, kVertex };

struct Settings {
    Mode mode = Mode::kShiftBy;
    Space space = Space::kWorld;
    Pivot pivot = Pivot::kBase;
    SnapTarget target = SnapTarget::kGrid;
    bool align_to_slope_ = false; // (surface snapping: an object stands square to the surface)
};
Settings& at_hand();
// UX1's preferences (level_editor_prefs.h) keep these: wasd_flies (1 - W A S D fly or nudge whatever the mouse does,
// as the editor was; 0, the default - only while the right mouse button is held), snap_move (the grid, units) and
// snap_turn (the rotation snap, degrees - Z / X turn objects and nodes by it too). Set: written to the ini.
bool fly_always();
void set_fly_always(bool always);
float get_grid();
void set_grid(float grid);
float angle_step();
void set_angle_step(float degrees);

// The choices offered (the toolbar's lists).
const std::vector<float>& grid_sizes();  // 1, 5, 10, 25, 50, 100
const std::vector<float>& angle_steps(); // 5, 10, 15, 30, 45, 90
const char* mode_name(Mode mode);        // "Move", "Rotate", "Scale"
const char* pivot_name(Pivot pivot);     // "Base", "Centre", "Own"
const char* target_name(SnapTarget target);
Mode next_mode(Mode mode); // (Space: Move -> Rotate -> Scale -> Move)

// --- the pure parts ---------------------------------------------------------------------------------------------

// The axes of a frame turned by `yaw` about up (heading; positive +z towards +x, as turn_about_up): x (cos, 0, -sin), y
// up, z (sin, 0, cos) - a node's local axes, an object's when upright.
void heading_axes(float yaw, std::span<V3, 3> out);
// The heading of a direction (its x / z): atan2(x, z) - an object's from its at row.
float get_heading_of(V3 at);
// Rows made unit (an object's matrix: its local axes; a zero row becomes the world's).
void unit_axes(std::span<const V3, 3> rows, std::span<V3, 3> out);
// The angle of `point` about the vertical line through `pivot` (as get_heading_of).
float turn_around_axis(V3 pivot, V3 point);
// An angle brought into (-pi, pi].
float get_wrap(float radians);
// An angle (radians) and the step it snaps to (degrees).
struct AngleSnap {
    float radians{};
    float step_degrees_{};
};

// On a step of `step_degrees` (0: as it is).
float snap_angle(AngleSnap angle_snap);
// The nearest whole number of quarter turns.
int get_quarters(float radians);
// A distance on a grid relative to where a drag started: the nearest multiple (0: as it is). Absolute snapping is
// level_editor::snap_to_grid on the coordinate.
float snap_relative(float distance, float grid);
// A scale factor from a drag along a handle: the distance from the pivot now over the distance when grabbed (`s0`), kept
// within 0.05..20 (1 when grabbed at the pivot); and from the cursor's distance in pixels from the pivot's (the centre
// handle, the modal transform). On steps of a tenth with snapping.
float factor_along(float s0, float scale);
float factor_from_pixels(float d0, float distance);
float snap_factor(float factor);

// The gizmo: where (`pivot`), its axes (World: the world's; Local: the selection's) and its size in world units.
struct Gizmo {
    Mode mode = Mode::kShiftBy;
    V3 pivot = {};
    std::array<V3, 3> axes = {
        {{.x = 1.0F, .y = 0.0F, .z = 0.0F}, {.x = 0.0F, .y = 1.0F, .z = 0.0F}, {.x = 0.0F, .y = 0.0F, .z = 1.0F}}};
    float length = 100.0F;
    bool rotates = true; // (Rotate: false - nothing selected turns: a chain's point)
};
// Its handles: 0 / 1 / 2 the axes (Move: arrows; Scale: a cube at each end; Rotate: 1 the heading ring - the only one),
// 3 Scale's centre cube; -1 none.
constexpr int kCentre = 3;
// A world point to back buffer pixels (false: behind the camera).
using Projector = std::function<bool(V3 point, float* px, float* py)>;
// What's drawn: segments (`arrow`: a head at b), each with the handle it belongs to.
struct Segment {
    V3 a, b;
    int handle;
    bool arrow;
};
std::vector<Segment> gizmo_segments(const Gizmo& gizmo);
// A ring's radius and its point count.
struct RingShape {
    float radius{};
    int count{};
};

// The ring's points (Rotate): `count` around the pivot, at the gizmo's length, in the horizontal plane.
std::vector<V3> get_ring(V3 pivot, RingShape ring);
// The handle within `grab` pixels of (x, y) (the nearest), -1 for none.
int handle_at(const Gizmo& gizmo, const Projector& project, maths::Vec2 at, float grab);
// A handle's colour: x red, y green, z blue, the centre white; lit (hovered or dragged) yellow.
uint32_t handle_colour(int handle, bool lit);

// Typed values while dragging (Blender): an axis and the text typed.
struct Typed {
    int axis = -1;              // 0 / 1 / 2: X / Y / Z typed; -1: none
    std::string text;           // digits, '.', a leading '-'
    bool axis_changed_ = false; // (the axis was typed since the drag code last looked: TakeAxisChange)
};
// A key typed (a character: '0'-'9', '.', '-', 'x' / 'y' / 'z', '\b' for Backspace): whether it was taken. '-' turns the
// sign over, the same axis again drops it, a second '.' is refused.
bool type_key(Typed* typed, char character);
// The number typed (false: none yet - nothing, "-", ".").
bool typed_value(const Typed& typed, float* value);
// "Move along X: 12.5" for the status bar (`default_axis`: the arrow dragged, else -1).
std::string describe(Mode mode, const Typed& typed, int default_axis);

// The repeat of the last duplicate (Ctrl+Shift+D, Blender's Shift+R): what the last Ctrl+D (or repeat) copied from and
// made - so the copies' move and turn since can be done again from them.
enum class Kind : uint8_t { kNone, kObject, kScenery, kElem };
struct Repeat {
    Kind kind = Kind::kNone;
    std::vector<uint32_t> made; // (object ids, sector checksums, a node's name)
    V3 source_pos_ = {};
    float source_heading_ = 0.0F;
};
struct RepeatStep {
    V3 offset = {};
    float turn = 0.0F; // radians (scenery: whole quarter turns)
    int quarters = 0;
};
// After a duplicate: the source's place and heading, and the copies made.
void note_duplicate(Repeat* memory, Kind kind, const std::vector<uint32_t>& made, V3 source_pos, float source_heading);
// The step to repeat when the selection (`kind`, `selected`) is still those copies: their move and turn since they were
// made (`pos`, `heading`: the selection's now). False: nothing to repeat (another selection).
bool repeat_for(const Repeat& memory, Kind kind, const std::vector<uint32_t>& selected, V3 pos, float heading,
                RepeatStep* step);
// Where the next copy goes from a copy at `from` (the offset turned by the turn: a turned row makes an arc).
V3 repeat_target(V3 from, const RepeatStep& step);
Repeat& last_duplicate();

// The Tools menu's transform options (level_editor_transform_ui.cpp, in level_editor_ui_menus.cpp's menu bar): the
// pivot, the rotation snap, what Snap snaps to, Align to slope, the fly keys. (The tool, World / Local, Snap and the
// grid are on the tool strip: level_editor_ui_strip.h.)
void options_menu_items();

} // namespace frontend::level_editor::xform
