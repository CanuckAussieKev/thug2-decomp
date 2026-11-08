#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "editor/level_editor.h"
#include "editor/level_editor_camera.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ui_layout.h"
#include "engine/ext/extension.h"

struct ImDrawList;

// NOT ORIGINAL - the level editor's split view (ours; see level_editor.h), after Unreal's, 3ds Max's, Blender's and
// Hammer's: the Scene panel shows the level in one, two or four views, each with its own camera, view type and view
// mode.
//   - Layouts (the View menu's Layout, a view's corner menu): Single; Two side by side; Two stacked; Four (Top, Front,
//     Left - orthographic - and Perspective, as 3ds Max's). [editor] split_layout=0..3 in thug2-decomp.ini.
//   - The view under the mouse is the active one (a highlight round it): the editor's camera, picking, gizmos, drags,
//     flying and keys are its own - the editor's camera state is that view's, the others' kept here and swapped in when
//     the mouse goes to them (never while a button is held, a drag, an orbit or a key move is under way). Nothing else
//     in the editor knows of the other views.
//   - Each view's corner menu: its view type (Perspective, Top, Bottom, Front, Back, Left, Right), its view mode (Lit,
//     Unlit, Wireframe, Collision only), Maximise / Restore and the layout. [editor] split_view1..4= keep each view's
//     type and mode (type + 16 x mode); the cameras' places aren't kept (they start from the editor's camera).
//   - Maximise (F11, the corner's button): the active view fills the panel; again: the layout back. [editor]
//     split_maximised=.
//   - Orthographic views of an axis (Top ... Right) in a split layout: the right button pans them (it doesn't turn
//     them, which would end the ortho view), the wheel zooms; every ortho view of an axis has a grid (spacing 1, 2 or
//     5 x 10^n units, at least kGridPixels apart; every fifth line stronger).
// The renderer draws the level once for each view shown (engine/ext/extension.h view_count / begin_view: the first
// viewport again in its part of the frame, its own camera, field of view, near plane and view mode); the active view is
// drawn last, so what the renderer keeps of the frame's camera is its. With one view shown (Single, or maximised) the
// renderer's path is the original's: nothing is drawn twice.
// The overlays (gizmos, labels, collision, the rails' lines...) are made for each view with its camera: the other views'
// first, with the editor's camera state theirs for a moment, then the active one's as before. The rails' and triggers'
// depth-tested lines (level_editor_gizmo_lines.h) are gathered once, in the active view, and the renderer draws them in
// every view.
namespace frontend::level_editor::split_view {

enum class Layout : uint8_t { kSingle, kTwoSideBySide, kTwoStacked, kFour, kCount };
constexpr int kLayouts = static_cast<int>(Layout::kCount);
constexpr int kViews = 4; // the views' state (slots): the four views' of the quad layout
constexpr int kPerspectiveSlot = 3;
constexpr float kGridPixels = 24.0F;

// --- the pieces (pure) ---------------------------------------------------------------------------------------------

const char* layout_name(Layout layout);
// A view's place in the Scene panel: parts of it (0..1).
struct Area {
    float x = 0.0F;
    float y = 0.0F;
    float width = 1.0F;
    float height = 1.0F;
};
// The views a layout shows (Single: one) and, for each place, which slot it shows (Single: the active one) and where.
int shown_count(Layout layout);
int slot_at(Layout layout, int place);
Area area_at(Layout layout, int place);
// The place (in `areas`) holding the point (parts of the panel), or -1.
int place_holding(std::span<const Area> areas, float x, float y);
// A view's type and mode kept in the ini as one number (type + 16 x mode), and back (false: not one).
int pack_view(view_cam::View type, int mode);
bool unpack_view(int value, view_cam::View* type, int* mode);
// The grid's spacing: the smallest of 1, 2, 5 x 10^n units at least `min_pixels` apart at `units_per_pixel`.
float grid_spacing(float units_per_pixel, float min_pixels);
// Whether a view is an ortho view of an axis (Top ... Right; not the numpad's 5 of a view as it is).
bool axis_view(view_cam::View view);

// --- the editor's state --------------------------------------------------------------------------------------------

// The editor started in the level `scene` (the layout and the views' settings read once) / stopped (the active view's
// state kept for the way back - play from here).
void kickoff(uint32_t scene);
void halt();
// Its actions (Maximise View, the layouts) in the keymap.
void prepare_all();

Layout layout();
void set_layout(Layout layout);
bool maximised();
void toggle_maximise();
// More than one view shown now (a split layout, not maximised).
bool several();
int active();
// A slot made the active one (its corner's Maximise: the view the button is on).
void make_active(int slot);

// Once a frame, first (the editor's Update, once its camera is known): the views made ready, the view under the mouse
// made the active one. Then, after the editor's keys were read: F11, and F in an ortho view of an axis.
void tick_frame();
void tick_keys();
// The mouse this frame, before the editor uses it: in an ortho view of an axis (a split layout) the right button's
// drag pans it (the movement taken).
void pan_ortho(float* rel_x, float* rel_y, bool right);

// --- the renderer's side (the editor's extension) -----------------------------------------------------------------

int view_count();
bool begin_view(int index, ext::ViewPass* pass);
void end_views();
// While another view's pass is drawn: its camera (true), its field of view and near plane.
bool pass_pose(V3* pos, TickCycle* frame);
float pass_view_angle(float base_degrees);
float pass_near_plane();
// The view noted for the pass being drawn: another view's kept here (true), the active one's left to the editor.
bool note_pass(const Lens& lens, std::span<const uint32_t, 4> viewport);

// --- the overlays --------------------------------------------------------------------------------------------------

// The other views' overlays made: for each, its camera state swapped in, `overlay` (the editor's) emptied, `gather`
// run, the result kept as that view's; then `overlay` emptied for the active view's (and its Collision only applied).
void gather_others(ui::Overlay* overlay, const std::function<void()>& gather);
// The grid of an ortho view of an axis, into the overlay (the view whose state the editor has now).
void add_grid(ui::Overlay* overlay);

// --- what the Scene panel shows and changes ---------------------------------------------------------------------------

// The layout shown now (maximised: Single) and the slot at a place of it.
Layout shown_layout();
// A slot's overlay (null for the active one: the editor's), its view type and mode (the active one's: the editor's).
const ui::Overlay* overlay_of(int slot);
view_cam::View type_of(int slot);
int mode_of(int slot);
// A slot given another view type (an axis view looks at what it looked at; Perspective looks down at it) or mode.
void set_type(int slot, view_cam::View type);
void set_mode(int slot, int mode);

// The automation channel's `fe editor split <single | side | stacked | four | maximise | active <view> <timeout>>` (the
// timeout: a last number is the channel's, so a view's number needs one after it) and `fe editor pick <view 1-4>
// <x> <y>` (x, y: parts of that view, 0..1): the layout set, or the ray through that point of the view (as the mouse's
// would be) cast - the scenery piece it picks and where, and for an ortho view of an axis where a ray straight along
// its axis through the same point meets the level (they should agree). `words` from "editor" on; the result's text.
std::string automation(const std::vector<std::string>& words);

// --- the Scene panel (level_editor_split_ui.cpp) -------------------------------------------------------------------

// Each view's overlay clipped to it (`active_overlay`: the active view's), the borders, the active view's highlight,
// each view's corner menu and maximise button. True when the mouse is on a corner menu or button (not the view's).
struct PanelDraw {
    ImDrawList* list = nullptr;
    ui_layout::Rect2 rect;
    const ui::Overlay* active_overlay = nullptr;
    std::function<void(const ui::Overlay&)> draw_overlay;
    float scale = 1.0F;
};
bool draw_panel(const PanelDraw& panel);
// The View menu's Layout submenu.
void layout_menu();

} // namespace frontend::level_editor::split_view
