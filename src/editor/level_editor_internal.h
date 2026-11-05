#pragma once

// NOT ORIGINAL - what level_editor.cpp's pieces share (the editor's state, its picking / drag records and the helpers
// more than one piece uses). They were the one file's anonymous namespace; here they are in `internal`, with a
// using-directive, so every piece sees them as it saw them there. Only the level_editor_*.cpp pieces of the
// editor's core include this.

#include "editor/level_editor.h"
#include "engine/gfx/render_device.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include "editor/level_editor_fields.h"
#include "editor/level_editor_level_props.h"
#include "editor/level_editor_arrange.h"
#include "base/maths/vec2.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_walls.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/gfx/view_cam.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"

namespace frontend::level_editor {

namespace internal {

constexpr float kDefaultFlySpeed = 800.0F;    // units a second

constexpr float kDefaultOrbitDistance = 300.0F;

constexpr std::size_t kLastActionSize = 128;

constexpr float kScreenSpanFactor = 2.0F;  // the world width of the screen is twice depth times the tangent

constexpr std::size_t kPickTextSize = 64;

constexpr float kHalf = 0.5F;               // half

constexpr float kMaxPickDistance = 30000.0F;

// NOT ORIGINAL grouping - level_editor.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_state()).
struct LevelEditorState {
    bool active_{};
    // The camera we fly (the first viewport's, when the editor started) and the pose the game had given it.
    gfx::ViewCam* camera_{};
    maths::Vec4 saved_pos_{};
    maths::Mat4 saved_matrix_{};
    // Our pose.
    V3 pos_{};
    float yaw_{};
    float pitch_{};
    float speed_ = kDefaultFlySpeed; // units a second (Shift: 4x)
    // With something selected: the camera's distance from it, and whether the keys are moving it.
    float orbit_distance_ = kDefaultOrbitDistance;
    bool key_moving_{};
    // The view as last drawn (note_view).
    bool have_view_{};
    Lens lens_{};
    std::array<uint32_t, 4> viewport_{};
    uint64_t last_counter_{};
    bool pause_key_released_{};
    bool exit_requested_{};
    int saved_cursor_ = -1;
    bool relative_{};
    int moves_{};
    bool unsaved_{}; // (changes since the layout was last saved)
    // The autosave (level_editor_layout.h): which level load the unsaved changes belong to, a count of changes (the
    // snapshot made again when it moves), when it was last written, and the offer of an older one.
    int unsaved_load_ = -1;
    int changes_{};
    int snapshot_changes_{};
    uint32_t last_autosave_{};
    bool offer_show_{}; // (shown on the first frame the HUD is up)
    bool offer_pending_{}; // (Ctrl+R / Ctrl+Shift+R)
    // What the editor last did, for the log if the game crashes (plain memory: the crash hook reads it).
    std::array<char, kLastActionSize> last_action_ = {"none"};
    // The camera tools and play from here (level_editor_camera.h, level_editor_play.h): the last frame's ray under the
    // cursor (an orbit's pivot), the play key (held when the editor starts: it counts once let go).
    bool last_have_ray_{};
    V3 last_ray_{};
};

LevelEditorState& level_editor_state();

std::vector<picking::Lock> selection_locks();

void follow_scenery();

// What is picked: the pointer (only used after checking it's still the object found under its id).
struct Picked {
    objects::GameEntity* object = nullptr;
    uint32_t id = 0;
};

// NOT ORIGINAL grouping - the hover, the drag and its gizmo's axis, handle and plane (were globals). Made on first use,
// never destroyed (drag_state()).
struct DragState {
    Picked hover_{};
    // The selected objects (the first is the one the camera and the arrows go by); or the selected scenery
    // (Scenery::Selection) - never both.
    std::vector<Picked> objects_;
    // The scene sector under the cursor (its checksum; 0: none).
    uint32_t hover_sector_{};
    // Dragging: free (the plane / the view's terms) or along one axis (an arrow).
    bool dragging_{};
    bool drag_vertical_{};
    int axis_ = -1; // (0 / 1 / 2: dragging along x / y / z; -1: free)
    int hover_axis_ = -1; // (the arrow under the cursor)
    int hover_handle_ = -1; // (a selected trigger volume's resize handle under the cursor - level_editor_triggers.h)
    bool cancel_requested_{}; // (Escape: a drag put back, the browser or the help closed - before leaving)
    V3 drag_start_{}; // where the selection was when the drag started
    V3 drag_offset_{};
    V3 drag_plane_normal_{};
    V3 drag_plane_offset_{};
    float press_x_{};
    float press_y_{};
    bool drag_moved_{};
    float drag_reach_{};
    bool drag_screen_{};
    V3 drag_from_{};
    V3 drag_right_{};
    V3 drag_forward_{};
    float drag_mouse_x_{};
    float drag_mouse_y_{};
    float drag_units_{};
    float drag_push_{};
    float axis_t0_{};
    // The transform tools (level_editor_transform.h): what the drag does (Move unless it started on a Rotate / Scale handle
    // or as Alt+G's modal transform - no button held: a click or Enter applies it), what's typed, whether a turn or a scale
    // was made (an undo step even when the position stayed), the turn and scale so far, where the cursor is (back buffer
    // pixels), vertex snapping's corner, and the camera let go of the selection (wasd_flies=0: flying with RMB - F frames
    // it again).
    xform::Mode drag_mode_ = xform::Mode::kShiftBy;
    bool modal_{};
    bool typed_apply_{};
    bool pending_forced_{};
    xform::Typed typed_{};
    V3 transform_pivot_{};
    float rot_total_{};
    float rot_applied_{};
    float rot_last_{};
    float rot_sign_ = 1.0F;
    bool rot_by_plane_{};
    int quarters_applied_{};
    float scale_s0_{};
    V3 scale_start_ = {.x = 1.0F, .y = 1.0F, .z = 1.0F};
    float cursor_x_{};
    float cursor_y_{};
    bool vertex_have_{};
    V3 vertex_corner_{};
    bool free_camera_{};
    // The ground marker: the selection's base and the ground below it.
    bool have_mark_{};
    V3 mark_base_{};
    V3 mark_ground_{};
    // The help panel (F1) and the browser (B).
    bool help_{};
    bool snap_always_{}; // (the toolbar's Snap 25: moves snap without Ctrl)
    // The node layer (level_editor_nodes.h): the families shown ([editor] families in the ini), a node selected by itself
    // (a gizmo or an Outliner row that isn't an object or scenery - a point node moves and turns as objects do,
    // level_editor_node_edit.h; chains and the rest are read-only), the gizmo under the cursor, and what the last frame
    // drew of them (picked by screen distance).
    uint32_t families_ = nodes::kAllFamilies;
    bool node_selected_{};
    int node_index_{};
    uint32_t node_name_{};
    bool field_editing_{}; // (the Inspector's position / heading fields are being changed: one undo step)
    bool online_warned_{}; // (the warning for a spawn, crown or flag moved in a level shared with THUGPRO: once a run)
    bool online_warning_shown_{}; // (its window is up)
    int hover_node_ = -1;
    // Picking (level_editor_picking.h): what a click in the view may take ([editor] pick=), the piece a plain click passes
    // under the cursor (for the status bar: Alt+click takes it), and click-to-cycle - the last click's spot, what was
    // under it in the cycle's order and which of those it took (the hover shows the next one while it holds).
    uint32_t pick_filter_ = picking::kAll;
    uint32_t hover_passed_{};
};

DragState& drag_state();

struct Cycle {
    bool have = false;
    float x = 0.0F, y = 0.0F;
    V3 pos = {};
    int changes = 0;
    uint32_t filter = 0;
    std::vector<picking::TableRow> order;
    int index = -1;
};

// NOT ORIGINAL grouping - the pick cycle, the placing, the confirmations, the pickables and the overlay (were globals).
// Made on first use, never destroyed (pick_cycle()).
struct PickCycle {
    Cycle cycle_{};
    int hover_cycle_ = -1; // (the hover is the cycle's next entry: its index)
    // Phase 3 (level_editor_node_create.h): the Place palette (N) and its armed entry (a click in the view places it), a
    // level's node waiting for the confirm window before it's hidden.
    bool place_open_{};
    int place_armed_ = -1;
    uint32_t confirm_hide_{};
    // Reset level (Ctrl+Shift+Delete, the toolbar): the confirm window is up; the old HUD's second press (its time).
    bool confirm_reset_{};
    uint32_t reset_asked_at_{};
    // Rails (level_editor_node_edit.h): a rail node moves with its whole rail (L toggles: one point).
    bool move_entire_rail_ = true;
    std::vector<nodes::Pickable> pickables_;
    ui::Overlay overlay_{};
    int hud_taken_down_{}; // (the UI runs: frames the HUD's screen elements have been taken down)
};

PickCycle& pick_cycle();

objects::GameEntity* get_live(const Picked& picked);

std::vector<objects::GameEntity*> selected_objects();

objects::GameEntity* get_primary();

int picked_node_index();

bool node_values(node_edit::Values* values);

uint32_t wall_only();

bool have_selection();

void note_online_change(nodes::Family family, const char* what, const char* verb);

void note_online_edit(int index);

void set_selected_node(const node_edit::Values& values);

void select_node_only(int index);

bool is_picked(objects::GameEntity* object);

void select_objects(const std::vector<objects::GameEntity*>& objects);

void unpick();

V3 selection_pos();

float get_selection_base();

V3 target_of();

void place_selection(V3 pos);

V3 selection_centre();

void turn_objects_by(const std::vector<objects::GameEntity*>& objects, float radians, V3 centre);

void turn_selection(int notches);

void get_settle();

// --- undo / redo (Ctrl+Z / Ctrl+Y) ---
//
// A step is how things were before a change: the selected objects' places, or the scenery's state (Scenery::State:
// its pieces' placements, hidden or not, its rails), or - for copies made or deleted - whether those copies exist.
// Undoing puts that back and keeps what it replaced for redo.

struct MadeCopy {
    uint32_t name{}, source{};
    object_edit::State state;
};

struct FieldUndo {
    uint32_t node{}, key{};
    fields::Own own;
};

struct UndoStep {
    enum class Kind : uint8_t {
        kObjects,
        kScenery,
        kMade,
        kNodes,
        kAdded,
        kHidden,
        kOrder,
        kLevel,
        kChains,
        kWalls,
        kTriggers,
        kFields,
        kProps,
        kLocks,
        kEntities,
        kModule,
        kGroup
    } kind = Kind::kObjects;
    // kGroup (UX7 - level_editor_modules.h's Edit::fold): several steps made as one action, put back last first.
    std::vector<UndoStep> group;
    uint64_t serial = 0; // (a number for the step: the adjust-last panel knows its step is still the newest)
    // kEntities: path speeds and emitters' sounds to put back (level_editor_entities.h).
    entities::State entities;
    // kModule: a module's own step (level_editor_modules.h - face flags, clusters, cameras).
    std::shared_ptr<ModuleStep> module;
    // kLocks: the locks to put back (level_editor_picking.h - L, Unlock, Unlock all).
    std::vector<picking::Lock> locks;
    // kScenery: what the scenery carried along (level_editor_carry.h) - their places before the change.
    std::vector<object_edit::State> carried_objects_;
    node_edit::Step carried_nodes_;
    // kFields (phase 6): nodes' own fields to put back (level_editor_fields.h); kProps: the level's properties.
    std::vector<FieldUndo> fields;
    level_props::Props props;
    // kWalls (level_editor_walls.h): collision states to put back - an invisible wall's (wall: selected again) or the
    // selected scenery's switches (sectors: selected again).
    std::vector<walls::WallState> walls;
    uint32_t wall = 0;
    // kChains (phase 4): the chains' state to put back (level_editor_chains.h).
    chains::State chains;
    std::vector<object_edit::State> objects;
    scenery::State scenery;
    node_edit::Step nodes; // (kNodes: the nodes' places - level_editor_node_edit.h)
    // kMade: the scenery copies and object copies, and whether they exist once this step is put back.
    std::vector<uint32_t> sectors;
    std::vector<MadeCopy> copies;
    bool exist = false;
    V3 where = {}; // (the selection's position then: whether a drag moved it)
    // kAdded: nodes the editor added and where in the overlay (exist: whether they're there once put back); kHidden:
    // a level's nodes and whether they're hidden once put back; kOrder: the overlay's order (names) to put back.
    std::vector<node_create::Spec> added;
    std::vector<int> added_at_;
    std::vector<uint32_t> hidden_names_;
    bool hidden = false;
    std::vector<std::string> order;
    // kLevel (Reset level): put back, the level is reset again (`reset`) or the layout `layout` (Write's text) put in
    // place again.
    bool reset = false;
    std::string layout;
    // kTriggers: the trigger volumes' state to put back (level_editor_triggers.h).
    triggers::State triggers;
    // (the undo history panel - level_editor_ux3.h: a short label given where the step is made, and what was selected)
    std::string label, detail;
};

UndoStep at_hand(const UndoStep& step);

float pixel_scale();

nodes::Occluder gizmo_occluder(const TickCycle& frame, V3 dir, const scenery::Hit& hit, bool everything);

float trigger_reach(V3 dir);

bool cycle_holds(float x, float y, bool shift, bool everything);

int hover_cycle_index(const objects::GameEntity* object);

void note_click_for_cycle(maths::Vec2 at, V3 dir, bool plain);

bool drop_under_cursor(V3 dir);

} // namespace internal

// What the source file's anonymous namespace held that its pieces share: visible as it was.
using namespace internal;

} // namespace frontend::level_editor
