#pragma once

// NOT ORIGINAL - what level_editor.cpp's pieces share (the editor's state, its picking / drag records and the helpers
// more than one piece uses). They were the one file's anonymous namespace; here they are in `internal`, with a
// using-directive, so every piece sees them as it saw them there. Only the level_editor_*.cpp pieces of the
// editor's core include this.

#include "editor/level_editor.h"
#include "engine/gfx/render_device.h"
#include "engine/pad_input/input_source.h"
#include <SDL2/SDL.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <array>
#include "editor/level_editor_fields.h"
#include "editor/level_editor_level_props.h"
#include "editor/level_editor_arrange.h"
#include "base/maths/vec2.h"
#include "base/core/format.h"
#include "editor/level_editor_camera.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_preview.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_play.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ux3.h"
#include "editor/level_editor_ux8.h"
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

constexpr std::size_t kCaptionTextSize = 96;

constexpr std::size_t kStatusTextSize = 160;

constexpr std::size_t kPickTextSize = 64;

constexpr float kHalf = 0.5F;               // half

constexpr float kQuarterTurnDegrees = 90.0F; // degrees in a quarter turn

constexpr float kNearZ = 2.0F; // (render::choose_camera's near plane)

constexpr float kLookRadiansPerPixel = 0.0035F;

constexpr float kMaxPickDistance = 30000.0F;

constexpr float kMinOrbit = 20.0F, kMaxOrbit = 20000.0F;

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

std::string describe_selection();

int inspected_node();

std::vector<picking::Lock> selection_locks();

void follow_scenery();

view_cam::Rig rig_now();

perform::Resume resume_now();

void apply_resume(const perform::Resume& resume);

template <typename... Args>
void note_action(text::FormatFor<Args...> format, Args... args)
{
    text::format_to(level_editor_state().last_action_, format, args...);
}

void mark_unsaved();

// The mouse, from the events since the last Update.
struct Mouse {
    float x = 0.0F, y = 0.0F; // window pixels
    int window_width_ = 0, window_height_ = 0;
    float rel_x_ = 0.0F, rel_y_ = 0.0F;
    int wheel = 0;
    int left_presses_ = 0, left_releases_ = 0;
    int middle_presses_ = 0; // (as Alt+left click: one scenery piece in / out)
    bool left = false, right = false;
};

Mouse& stored_mouse();

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

struct Browser {
    bool open = false;
    int tab = 0; // 0: scenery pieces, 1: objects
    int index = 0;
    int top = 0;
    int preview = -1; // (the entry the preview shows when not the highlighted one: the one hovered in the list)
    struct Item {
        uint32_t id;
        std::string label;
    };
    std::vector<Item> items;
};

Browser& stored_browser();

constexpr int kBrowserRows = 22;

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

// NOT ORIGINAL grouping - the undo and redo steps and the pending one (were globals). Made on first use, never
// destroyed (undo_steps()).
struct UndoSteps {
    std::vector<UndoStep> undo_;
    std::vector<UndoStep> redo_;
    uint64_t undo_serial_{};
    UndoStep pending_{};
    bool has_pending_{};
};

UndoSteps& undo_steps();

UndoStep get_snapshot();

void push_undo(UndoStep step, const char* label = nullptr);

void push_locks_undo(const char* label = "Lock / unlock");

bool scale_selection_by(V3 factor, bool uniform);

V3 selection_scale_now();

void scale_selection_to(V3 scale);

V3& scale_at_field_start();

void begin_change();

void end_change();

void get_restore(UndoStep& step);

UndoStep at_hand(const UndoStep& step);

void step_history(std::vector<UndoStep>* from, std::vector<UndoStep>* to, const char* what);

bool get_to_pixel(const TickCycle& frame, V3 point, float* px, float* py);

xform::Mode gizmo_mode();

float selection_heading();

xform::Gizmo gizmo_now();

void start_drag(V3 origin, V3 dir, bool vertical, const TickCycle& frame, float mouse_x, float mouse_y);

// What a drag snaps to (level_editor_snap.h): held keys, or the toolbar's Snap and its Snap to.
struct Snapping {
    bool on = false;
    xform::SnapTarget target = xform::SnapTarget::kGrid;
};

Snapping snapping_now(bool ctrl, bool shift, bool alt);

void get_drag(V3 origin, V3 dir, const TickCycle& frame, float mouse_x, float mouse_y, Snapping snap);

void note_for_adjust(xform::Mode mode, V3 moved, bool uniform);

void end_drag();

void set_relative(bool on);

int arrow_at(const TickCycle& frame, maths::Vec2 at);

// --- drawing ---

// A line's end in screen pixels (the device draws the old HUD's lines: render::Device::draw_screen_lines).
using LineVertex = render::ScreenLineVertex;

void add_line(V3 a, V3 b, const TickCycle& frame, uint32_t color, std::vector<LineVertex>* out, int thickness = 1);

float pixel_scale();

void update_highlights();

// NOT ORIGINAL grouping - whether occlusion is off, in the editor and in play (were globals). Made on first use, never
// destroyed (occlusion_switch()).
struct OcclusionSwitch {
    // Occlusion culling switched off while editing (O; render::occlusion_off).
    bool occlusion_off_ = true;
    bool occlusion_in_play_{}; // (render::occlusion_off as it was when the editor opened)
};

OcclusionSwitch& occlusion_switch();

bool get_cast(V3 from, V3 to, V3* hit);

bool ground_below(V3 from, V3* ground);

const char* mode_name();

void update_hud();

// --- adding and deleting nodes (level_editor_node_create.h) ---

struct PlaceEntry {
    node_create::Kind kind;
    node_create::Team team;
    std::string label;
    uint32_t colour;
    bool lobby = false; // (a team lobby flag, TRG_Flag_<Colour>: node_create::lobby_flag_name)
};

const std::vector<PlaceEntry>& place_entries();

void place_node(int entry, V3 at);

void after_chain_change(const chains::State& before, uint32_t select, const std::string& message);

uint32_t chain_target();

void run_chain_op(chains::ChainOp op, uint32_t arg);

void field_request(const ui::Request& request);

chains::Blit chain_kind_of(int entry);

void draw_chain_point(V3 dir, bool alt);

void stop_chain_drawing(const char* message);

void unlink_list_node(bool confirmed);

bool show_hidden_node();

void move_selected_crown(int delta);

ui::ShapeMesh assemble_model();

void show_help();

void fill_browser();

void show_browser();

V3 cursor_point(V3 dir, bool have_ray, const TickCycle& frame);

void place_from_browser(V3 at, bool keep_open = false);

std::vector<std::pair<uint32_t, uint32_t>>& duplicate_pairs();

void get_duplicate(const TickCycle& frame, const xform::RepeatStep* repeat = nullptr);

void repeat_duplicate(const TickCycle& frame);

void switch_wall(uint32_t wall, bool off);

void switch_scenery_collision(bool off);

void erase_one();

void get_reset();

void focus_on_selection();

nodes::Occluder gizmo_occluder(const TickCycle& frame, V3 dir, const scenery::Hit& hit, bool everything);

float trigger_reach(V3 dir);

bool cycle_holds(float x, float y, bool shift, bool everything);

int hover_cycle_index(const objects::GameEntity* object);

void note_click_for_cycle(maths::Vec2 at, V3 dir, bool plain);

bool drop_under_cursor(V3 dir);

void view_hide(bool isolate);

void view_show_all();

void show_collision(bool on);

void toggle_lock();

void get_look_at(V3 target, float distance);

bool squeezed(input::KeyState keys, SDL_Scancode key, bool* was);

void select_node(int index);

int flag_node_of(objects::GameEntity* object);

void go_to_node(int index);

void drop_selection();

void save_layout();

void answer_autosave(bool discard);

void reset_level_now();

void ask_reset_level();

void toggle_help();

void toggle_browser();

void focus_browser_item();

void go_to_selection();

void apply_ui_requests(V3 dir, bool have_ray, const TickCycle& frame);

// --- UX8 (level_editor_ux8.h): wheel drill-through, click-to-teleport, walk mode ---

// The controller's right stick at full tilt: as a mouse moved this many pixels a second.
constexpr float kPadLookPixelsPerSecond = 700.0F;

bool& stored_drilled();

void drill_through(int steps, maths::Vec2 point, V3 dir);

void teleport_to(V3 dir);

void walk_frame(const Mouse& input, input::KeyState keys, const ux8::Pad& pad, double seconds);

void transform_keys(input::KeyState keys, bool ctrl, bool shift, bool alt, const Mouse& input, V3 dir, bool have_ray,
                    const TickCycle& frame);

// One editor frame's input and view, as tick_frame's steps share them.
struct EditorFrame {
    double seconds = 0.0;
    Mouse input;
    input::KeyState keys;
    ux8::Pad pad;
    bool pad_cursor_ = false; // the crosshair is the cursor (a game controller in controller mode)
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    double boost = 1.0;      // Shift: kFastFlyFactor
    bool no_change_ = false; // a drag, a key move, a field edit or a resize under way: no undo / save / delete keys
    TickCycle frame{};       // the camera's frame after this frame's looking
    V3 dir{};                // the mouse ray (the view's direction without one)
    bool have_ray_ = false;
    float back_x_ = 0.0F; // the cursor in back-buffer pixels
    float back_y_ = 0.0F;
};

void tick_level_systems();

void tick_hud_and_autosave();

double take_frame_seconds();

Mouse take_mouse();

void read_keys_and_pad(EditorFrame& editor_frame);

bool global_keys_and_walk(EditorFrame& editor_frame);

void handle_escape();

void tick_panels(EditorFrame& editor_frame);

void edit_keys(const EditorFrame& editor_frame);

void view_keys();

void tick_triggers_and_tools(const EditorFrame& editor_frame);

void command_keys(const EditorFrame& editor_frame);

void finish_chain_drawing(const Mouse& input);

void look_and_fly(EditorFrame& editor_frame);

void cast_mouse_ray(EditorFrame& editor_frame);

void apply_ray_tools(EditorFrame& editor_frame);

void browser_keys(const EditorFrame& editor_frame);

void move_with_keys(const EditorFrame& editor_frame);

void log_mouse_debug(const EditorFrame& editor_frame);

void update_tool_hovers(const EditorFrame& editor_frame);

void update_drag(EditorFrame& editor_frame);

void update_hover(const EditorFrame& editor_frame);

void handle_clicks(EditorFrame& editor_frame);

void finish_frame(const EditorFrame& editor_frame);

void add_hover_boxes(const TickCycle& frame, objects::GameEntity* hover, std::vector<LineVertex>& lines);

void add_selected_boxes(const TickCycle& frame, const std::vector<objects::GameEntity*>& objects,
                        std::vector<LineVertex>& lines);

void add_gizmo_lines(const TickCycle& frame, std::vector<LineVertex>& lines);

void draw_with_ui(const TickCycle& frame, const std::vector<objects::GameEntity*>& objects,
                  const std::vector<LineVertex>& lines);

} // namespace internal

// What the source file's anonymous namespace held that its pieces share: visible as it was.
using namespace internal;

} // namespace frontend::level_editor
