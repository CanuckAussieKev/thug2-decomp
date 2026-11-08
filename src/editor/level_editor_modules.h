#pragma once

#include <span>
#include <cstdint>
#include <memory>
#include <array>

#include "base/maths/vec2.h"
#include "editor/level_editor.h"

// NOT ORIGINAL - what the level editor (level_editor.cpp) offers the modules that keep their own state and draw their
// own Inspector sections (ours; see level_editor.h): collision face flags (level_editor_faces.h), scenery trick-object
// clusters (level_editor_clusters.h) and camera nodes (level_editor_cameras.h). Each of those modules queues what its
// UI asks for and does it in its own Update, which the editor calls once a frame while it runs.
namespace frontend::level_editor {

constexpr double kDefaultShowSeconds = 4.0;

// An undo step of a module's own: what to put back (Restore), and the same things as they are now (Now - what the
// other stack gets). The editor keeps it on its undo / redo stacks like its own steps (64 steps, this session).
struct ModuleStep {
    virtual ~ModuleStep() = default;
    virtual void get_restore() = 0;
    [[nodiscard]] virtual std::shared_ptr<ModuleStep> get_now() const = 0;
};
// A change is about to be made: `before` (as things are now) goes on the undo stack; the redo stack is dropped and the
// level marked unsaved (the autosave).
void push_module_undo(std::shared_ptr<ModuleStep> before);

// The editor's camera now: where it is, which way it looks (yaw / pitch as frame_from_yaw_pitch takes them), the lens and
// the viewport (back buffer pixels) of the last frame drawn. False while the editor isn't running.
struct ViewNow {
    V3 pos;
    float yaw, pitch;
    Lens lens;
    std::array<uint32_t, 4> viewport;
    float near_z_;
};
bool current_view(ViewNow* view);
// The editor's camera put there, looking that way (the selection dropped first: the camera would orbit it).
void look_from(V3 pos, YawPitch view);
// (UX5: the Problems window's and the notes' Go to) The camera taken to a node, selected (false: no such node now); to a
// scenery piece, selected (false: none, or not movable); to look at a point from `distance` away (nothing selected).
// Call from a module's Update (not while the UI draws).
bool go_to_node_named(uint32_t name);
bool go_to_scenery(uint32_t sector);
void go_to_point(V3 pos, float distance);

// --- for the history, changes, selection and clipboard modules (level_editor_ux3.h) ---------------------------------

// The undo / redo stacks by their steps' labels (each step's short label where it was made, and what was selected then):
// undo oldest first (the last is the next undone), redo next first. jump_history: `steps` undone (negative) or redone.
struct EditorUndoRecord {
    std::string label, detail;
};
std::vector<EditorUndoRecord> undo_history();
std::vector<EditorUndoRecord> redo_history();
void jump_history(int steps);
// A module's undo step with a label (push_module_undo's steps are labelled by their module otherwise).
void push_module_undo_labelled(std::shared_ptr<ModuleStep> before, const char* label);
// The locks (level_editor_picking.h) as they are now, as an undo step - before a change of them.
void push_locks_undo_labelled(const char* label);
// A layout's text (Layout::emit_out's) put in place of the level's changes as one undo step `label` (the layout as it is now
// kept for Ctrl+Z): the Changes panel's Revert, a paste, a snapshot switched to. False (`message`): not in a level, or
// the text isn't a layout of this level.
bool apply_layout_step(const std::string& text, const char* label, std::string* message);

// The selection by ids: the objects (the first is the primary), else the scenery pieces, else a node by itself.
struct SelectionIds {
    std::vector<uint32_t> objects, sectors;
    uint32_t node = 0;
};
SelectionIds current_selection();
void set_selection(const SelectionIds& ids);
// A thing by its name / checksum - a node (its object or scenery, as the Outliner does), else an object, else a scene
// sector - selected and the camera taken to it. False: nothing of that name.
bool go_to_thing(uint32_t id);
// A world point to back buffer pixels (false: behind the camera, or no view yet); the window's mouse pixels to back
// buffer pixels.
bool project_to_back(V3 point, float* x, float* y);
// The pick filter (level_editor_picking.h's bits) and whether the editor is moving something now.
uint32_t pick_filter_now();
bool change_under_way();

// The Inspector's multi-selection and Paste fields: fields set (typed `text` of Fields::Kind `kind`) or removed on each
// of `nodes` - one undo step for all (`label`). How many fields were set (`message`: the first refusal).
struct FieldSet {
    uint32_t key = 0;
    int kind = 0;
    std::string text;
    bool removed = false;
};
int set_fields_on_nodes(const std::vector<uint32_t>& nodes, const std::vector<FieldSet>& fields, const char* label,
                        std::string* message);
// The selection's transform (its position, heading in radians - objects and point nodes - and scale), and one put on it
// (one undo step; what the selection can't take is left: scenery turns by quarters).
struct TransformValues {
    V3 pos = {};
    bool has_heading_ = false;
    float heading = 0.0F;
    bool has_scale_ = false;
    V3 scale = {.x = 1.0F, .y = 1.0F, .z = 1.0F};
};
bool selection_transform(TransformValues* transform);
bool set_selection_transform(const TransformValues& transform);
// The status bar's message line.
void show_message(const std::string& text, double seconds = kDefaultShowSeconds);

// --- for the arrange tools (UX7: level_editor_arrange.h, level_editor_adjust.h, level_editor_links.h) --------------
//
// The selection and the editor's own operations on it, each as the keys / drags do it (undo steps included where
// said). Only while the editor runs.
namespace edit {

enum class Kind : uint8_t { kNone, kObject, kScenery, kElem, kWall };
struct Selected {
    Kind kind = Kind::kNone;
    std::vector<uint32_t> ids; // objects' ids, sectors' checksums, the node's name, the wall's checksum
};
Selected get_selection();
// Exactly that selected again (objects by id - those still there -, sectors, a node by name).
void choose_one(const Selected& selected);
// The selection's place (as the drags go by: the first object's position, a node's place, the scenery's pivot), its
// base and the middle of its box.
V3 get_pos();
float current_base();
V3 current_centre();
// A change of the selection's places made by the caller (Begin: its undo step's snapshot, what scenery carries armed;
// End: the step pushed - always - and the scenery's collision brought up to date).
void get_begin();
void get_end();
// The selection put at `pos` (Pos), turned by `radians` about up (objects about the toolbar's pivot, a node's heading,
// scenery and walls the nearest whole quarter turns), scaled by `factor` along the world's axes ([ / ]: its own undo
// step; false when nothing changed).
void place_at(V3 pos);
void turn_by(float radians);
bool scale_by(V3 factor, bool uniform);
// Ctrl+D (`repeat`: Ctrl+Shift+D's step; null: beside it) - its undo step; the copies selected. The sources and copies
// it made, in pairs (objects' ids, sectors' checksums) - empty when nothing was copied.
std::vector<std::pair<uint32_t, uint32_t>> get_duplicate(const void* repeat);
// The undo stack: a number for its top step (changes with every step pushed or taken), its depth; the steps pushed
// since `depth` folded into one (one Ctrl+Z takes them all); the top step put back and dropped (no redo - the last action
// about to be made again). False: nothing to take.
uint64_t top_serial();
size_t deepness();
void fold(size_t depth);
bool take_back();
// What an action made: objects and sectors (checksums).
struct MadeThings {
    std::span<const uint32_t> objects;
    std::span<const uint32_t> sectors;
};

// Copies made (objects' ids - Objects' copies -, sectors' checksums): one undo step that takes them away.
void push_made(MadeThings made);
// The selection's state now as an undo step (before a change made by the caller).
void push_snapshot();
// Copies of objects removed (as Delete removes them): one undo step that makes them again.
void remove_copies(const std::vector<uint32_t>& ids);
// The scenery's collision lookups brought up to date after the caller moved it (no undo step).
void get_settle();
// A node's field: the node, the field's key and its kind.
struct FieldKey {
    uint32_t node{};
    uint32_t key{};
    int kind{};
};

// A node field set as the Inspector sets it (one undo step; `kind`: Fields::Kind). False: refused (said).
bool set_field(FieldKey field_key, const std::string& text);
// A chain change made since `before` (Chains::State): its undo step, `select` (a node; 0: none) selected, `message` shown.
void chain_changed(const void* before, uint32_t select, const std::string& message);
// The ground below a point (the game's collision ray: false - none within reach).
bool get_ground(V3 from, V3* ground);
// What the editor is doing: dragging (and with which tool), the cursor (back buffer pixels), the ray under it (false:
// none - over a panel, looking about), the ground marker (the selection's base and the ground below it).
bool get_dragging();
int drag_mode(); // (xform::Mode)
maths::Vec2 get_cursor();
bool get_ray(RayOut ray);
// A mark on the ground: the point it stands on and where the ground is.
struct GroundMark {
    V3* base{};
    V3* ground{};
};

bool ground_mark(GroundMark mark);
// The camera's yaw (radians; the heading new things face).
float get_yaw();
// The Add palette (B): open, its tab (0 scenery, 1 objects) and highlighted item's id; a copy of it placed at `at`
// (one undo step, selected; `keep_open`: the palette stays open).
bool browser_item(int* tab, uint32_t* id);
void place_browser_item(V3 at, bool keep_open);
// The Place palette (N): its armed entry (-1: none), the entry's colour.
int place_armed(uint32_t* colour);
// Where a click places (the browser's Enter, the Place palette): the ground below what the cursor's ray meets.
V3 cursor_point();
// The same for a point of the view other than the editor's cursor (back buffer pixels: the Content Browser's drag,
// whose mouse the UI holds). False: not in the view.
bool cursor_point_at(float back_x, float back_y, V3* point);

} // namespace edit

} // namespace frontend::level_editor
