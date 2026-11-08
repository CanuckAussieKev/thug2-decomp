#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "editor/level_editor.h"
#include "editor/level_editor_nodes.h"
#include "base/maths/vec2.h"

// NOT ORIGINAL - the level editor's organising (ours; see level_editor.h): what the Outliner adds over the level's own
// nodes, the selection tools, and the layout snapshots.
//   - Per row (Blender's outliner): the eye - hidden in the editor only (an object or scenery through
//     level_editor_view.h's Hide, a node's gizmo here; never saved, as H); the lock - locked against changes (moving,
//     turning, scaling, dropping, deleting, Home: refused while it's selected); selectable - an object or scenery is
//     "locked" as level_editor_picking.h's L lock (clicks pass it), a node's gizmo isn't picked.
//   - User groups: a named group of the selection (Ctrl+G; Ctrl+Shift+G ungroups), selected by a click on it in the
//     Outliner or on one of its scenery pieces in the view (level_editor_scenery.h: a click takes the group, again the
//     piece); labels: an editor-only name shown beside a row's name (right-click Rename label).
//   - Type filters in the Outliner's search: "rail:", "spawn:", "object:"... (any family, singular or plural), "group:"
//     <name>, "label:" (rows with a label), "locked:", "hidden:"; then words as before.
//   - Box select (a drag from empty space, or Ctrl+B and any drag): what's inside the box (dragged to the right) or what
//     it crosses (dragged to the left) - objects with a model, else scenery as the pick mode takes it (Groups: the units
//     inside / crossed, Pieces: the pieces - level_editor_scenery.h's box_units); Shift adds, Ctrl removes.
//   - Select similar (Shift+G): same mesh (objects: model file; scenery: a copy of the same piece or the same size),
//     same class, same node family, same type; Select all of this family; Invert; Select all.
//   - Layout snapshots: named versions of the edited copy's layout, thug2-decomp-layouts\<scene>.<name>.txt; switching
//     to one keeps the current layout in its own snapshot first, puts the chosen one in place (one undo step) and saves
//     it as the edited copy's layout (<scene>.txt) - the one the level select's (EDITED) entry loads: the active one.
// Records (editor-only - an older reader skips them; the Changes panel doesn't list them):
//   group <name> o:<id>,s:<checksum>,n:<node>...   a user group (its name %-escaped); a copy of scenery as c:<n> - the
//                                                   n-th of the layout's `clone` records (a copy's checksum is new
//                                                   each time the layout goes in; an older reader refuses the record)
//   label <checksum> <text>                         a row's label (%-escaped)
//   edlock <checksum>                               locked against changes
//   nopick <node>                                   a node's gizmo not selectable
//   snapshot <name>                                 the snapshot this layout is (the active one)
namespace frontend::level_editor::organize {

// --- the pure parts --------------------------------------------------------------------------------------------------

struct Member {
    char kind = 'n'; // 'o' an object, 's' a scene sector, 'n' a node by itself
    uint32_t id = 0;
};
inline bool operator==(const Member& a, const Member& b) { return a.kind == b.kind && a.id == b.id; }
struct GroupSet {
    std::string name;
    std::vector<Member> members;
};
struct State {
    std::vector<GroupSet> groups;
    std::vector<std::pair<uint32_t, std::string>> labels;
    std::vector<uint32_t> locked;     // edlock
    std::vector<uint32_t> unpickable; // nopick
    std::string snapshot;
};
bool same_state(const State& a, const State& b);
// The records' lines (each ending '\n') and one read (false: not one of these, or bad).
std::string write_records(const State& state);
bool is_record(const std::string& keyword);
bool read_record(const std::string& line, State* state);
// A group / label / snapshot name: printable, 1-40 characters after trimming (`out`).
bool clean_name(const std::string& text, std::string* out);
// The label of `id` ("" none); set / cleared ("" clears).
std::string get_label_of(const State& state, uint32_t id);
void set_label(State* state, uint32_t id, const std::string& text);
// A flag set in a list of ids, switched.
bool get_has(std::span<const uint32_t> list, uint32_t id);
void set_flag(std::vector<uint32_t>* list, uint32_t id, bool on);

// The Outliner's search: type filters, then the words.
struct Filter {
    uint32_t families = 0; // Nodes::Bit()s named (0: any family)
    std::string group;     // "group:<name>" (only its members)
    bool labeled = false, locked = false, hidden = false;
    std::string text; // the rest (as the search matched before)
};
Filter parse_filter(const std::string& search);
// The family a filter word names ("rail", "Rails", "spawn"...; false: none).
bool family_of_word(const std::string& word, nodes::Family* family);
// Whether a row passes: its family, its text (the name and the label: the words, any case), its flags.
bool row_passes(const Filter& filter, nodes::Family family, const std::string& name, const std::string& label,
                bool in_group, bool locked, bool hidden);

// Box select: the box (back buffer pixels) dragged from (x0, y0) to (x1, y1); inside when dragged to the right, crossing
// when to the left. A thing's screen box (its projected corners' bounds) taken: wholly inside, or (crossing) touching.
struct Rect2 {
    float x0, y0, x1, y1; // x0 <= x1, y0 <= y1
};
Rect2 rect_of(float ax, float ay, float bx, float by);
bool crossing_drag(float origin_coord_x, float last_column);
bool box_takes(const Rect2& box, const Rect2& thing, bool crossing);
// The selection after a box: `found` replacing `current`, added to it, or taken from it (`op`: 0 / 1 add / 2 remove),
// in order, no repeats.
std::vector<uint32_t> combine(const std::vector<uint32_t>& current, std::span<const uint32_t> found, int op);

// Select similar: what a thing is, and whether two are alike by a criterion.
enum class Similar : uint8_t { kMesh, kClass, kFamily, kKindTag };
struct Thing {
    uint32_t id = 0;
    nodes::Family family = nodes::Family::kOther;
    uint32_t cls = 0, type = 0;
    std::string model;   // objects: the model file
    uint32_t source = 0; // scenery: what it's a copy of (itself if not a copy)
    V3 size = {};        // scenery: its box's size
};
bool alike(const Thing& a, const Thing& b, Similar by);

// Snapshots' files: "<8 hex digits>.<name>.txt" (a name of letters, digits, '_' and '-', 1-32; not "autosave"), and
// one recognised.
bool valid_snapshot_name(const std::string& name);
std::string snapshot_file_name(uint32_t scene, const std::string& name);
bool snapshot_of(const std::string& file_name, uint32_t scene, std::string* name);

// --- the game side ------------------------------------------------------------------------------------------------

// Registers the layout's records (Layout::add_extension) and the actions' callbacks; once.
void prepare_all();
const State& at_hand();
// Changed as one undo step (`label`).
void get_change(const State& next, const char* label);

// Per row (a node's name: an object's id, a scenery piece's checksum, a node's own name).
bool hidden_in_editor(const nodes::Elem& node);
void set_hidden_in_editor(const nodes::Elem& node, bool hidden);
bool latched(uint32_t id);
void set_locked(uint32_t id, bool locked);
bool get_selectable(const nodes::Elem& node);
void set_selectable(const nodes::Elem& node, bool selectable);
// The editor's hooks: a node's gizmo not drawn (hidden in the editor) / not picked; the selection locked against changes
// (said on the message line when `say`).
bool gizmo_hidden(uint32_t name);
bool gizmo_pickable(uint32_t name);
bool selection_locked(bool say);

// Groups.
void make_group(const std::string& wanted); // of the selection
void select_group(int index);
void delete_group(int index);
void rename_group(int index, const std::string& wanted);
void add_to_group(int index); // the selection added
// Ungroup (Ctrl+Shift+G): every group the selection is in taken away (its members stay as they are; the quick sets
// "Set 1".."Set 9" kept) - one undo step.
void ungroup_selection();
bool in_group(const std::string& group, const nodes::Elem& node);

// Box select (the editor's click handling): armed (Ctrl+B: the next press starts one anywhere); a press where nothing is
// (`x`, `y`: back buffer pixels; `mods`: Actions' bits); the drag; let go - applied when it moved.
void arm_box();
bool box_armed();
void box_press(maths::Vec2 at, uint8_t mods);
void box_track(bool down, float x, float y);
bool box_active(); // (a drag drawing a box)
// The box's outline for the overlay (back buffer pixels): false when there is none.
bool box_outline(Rect2* rect, bool* crossing);

// Select similar / all / invert (the Shift+G menu, the command search).
void select_similar(Similar by);
void select_family();
void select_all();
void invert_selection();
// What Select similar on a node asked the Outliner's search to show (its family: "rails:"), taken once ("": nothing).
std::string take_outliner_search();

// Snapshots.
std::vector<std::string> snapshots(); // this level's, by name
std::string active_snapshot();
bool save_snapshot(const std::string& name, std::string* message);
bool switch_snapshot(const std::string& name, std::string* message);
bool delete_snapshot(const std::string& name, std::string* message);

} // namespace frontend::level_editor::organize
