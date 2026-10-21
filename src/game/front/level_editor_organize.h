#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_nodes.h"

// NOT ORIGINAL - the level editor's organising (ours; see level_editor.h): what the Outliner adds over the level's own
// nodes, the selection tools, and the layout snapshots.
//   - Per row (Blender's outliner): the eye - hidden in the editor only (an object or scenery through
//     level_editor_view.h's Hide, a node's gizmo here; never saved, as H); the lock - locked against changes (moving,
//     turning, scaling, dropping, deleting, Home: refused while it's selected); selectable - an object or scenery is
//     "locked" as level_editor_picking.h's L lock (clicks pass it), a node's gizmo isn't picked.
//   - User groups: a named group of the selection (Ctrl+G), selected by a click on it in the Outliner; labels: an
//     editor-only name shown beside a row's name (right-click Rename label).
//   - Type filters in the Outliner's search: "rail:", "spawn:", "object:"... (any family, singular or plural), "group:"
//     <name>, "label:" (rows with a label), "locked:", "hidden:"; then words as before.
//   - Box select (a drag from empty space, or Ctrl+B and any drag): what's inside the box (dragged to the right) or what
//     it crosses (dragged to the left) - objects with a model, else scenery pieces; Shift adds, Ctrl removes.
//   - Select similar (Shift+G): same mesh (objects: model file; scenery: a copy of the same piece or the same size),
//     same class, same node family, same type; Select all of this family; Invert; Select all.
//   - Layout snapshots: named versions of the edited copy's layout, thug2-decomp-layouts\<scene>.<name>.txt; switching
//     to one keeps the current layout in its own snapshot first, puts the chosen one in place (one undo step) and saves
//     it as the edited copy's layout (<scene>.txt) - the one the level select's (EDITED) entry loads: the active one.
// Records (editor-only - an older reader skips them; the Changes panel doesn't list them):
//   group <name> o:<id>,s:<checksum>,n:<node>...   a user group (its name %-escaped)
//   label <checksum> <text>                         a row's label (%-escaped)
//   edlock <checksum>                               locked against changes
//   nopick <node>                                   a node's gizmo not selectable
//   snapshot <name>                                 the snapshot this layout is (the active one)
namespace Front::LevelEditor::Organize {

// --- the pure parts (self-tested: src/hook/selftests_editor_ux3.cpp) -------------------------------------------------

struct Member {
    char kind = 'n'; // 'o' an object, 's' a scene sector, 'n' a node by itself
    uint32_t id = 0;
};
inline bool operator==(const Member& a, const Member& b) { return a.kind == b.kind && a.id == b.id; }
struct Group {
    std::string name;
    std::vector<Member> members;
};
struct State {
    std::vector<Group> groups;
    std::vector<std::pair<uint32_t, std::string>> labels;
    std::vector<uint32_t> locked;     // edlock
    std::vector<uint32_t> unpickable; // nopick
    std::string snapshot;
};
bool SameState(const State& a, const State& b);
// The records' lines (each ending '\n') and one read (false: not one of these, or bad).
std::string WriteRecords(const State& state);
bool IsRecord(const std::string& keyword);
bool ReadRecord(const std::string& line, State* state);
// A group / label / snapshot name: printable, 1-40 characters after trimming (`out`).
bool CleanName(const std::string& text, std::string* out);
// The label of `id` ("" none); set / cleared ("" clears).
std::string LabelOf(const State& state, uint32_t id);
void SetLabel(State* state, uint32_t id, const std::string& text);
// A flag set in a list of ids, switched.
bool Has(const std::vector<uint32_t>& list, uint32_t id);
void Switch(std::vector<uint32_t>* list, uint32_t id, bool on);

// The Outliner's search: type filters, then the words.
struct Filter {
    uint32_t families = 0; // Nodes::Bit()s named (0: any family)
    std::string group;     // "group:<name>" (only its members)
    bool labeled = false, locked = false, hidden = false;
    std::string text;      // the rest (as the search matched before)
};
Filter ParseFilter(const std::string& search);
// The family a filter word names ("rail", "Rails", "spawn"...; false: none).
bool FamilyOfWord(const std::string& word, Nodes::Family* family);
// Whether a row passes: its family, its text (the name and the label: the words, any case), its flags.
bool RowPasses(const Filter& filter, Nodes::Family family, const std::string& name, const std::string& label,
               bool inGroup, bool locked, bool hidden);

// Box select: the box (back buffer pixels) dragged from (x0, y0) to (x1, y1); inside when dragged to the right, crossing
// when to the left. A thing's screen box (its projected corners' bounds) taken: wholly inside, or (crossing) touching.
struct Rect {
    float x0, y0, x1, y1; // x0 <= x1, y0 <= y1
};
Rect RectOf(float ax, float ay, float bx, float by);
bool CrossingDrag(float startX, float endX);
bool BoxTakes(const Rect& box, const Rect& thing, bool crossing);
// The selection after a box: `found` replacing `current`, added to it, or taken from it (`op`: 0 / 1 add / 2 remove),
// in order, no repeats.
std::vector<uint32_t> Combine(const std::vector<uint32_t>& current, const std::vector<uint32_t>& found, int op);

// Select similar: what a thing is, and whether two are alike by a criterion.
enum class Similar : uint8_t { Mesh, Class, Family, Type };
struct Thing {
    uint32_t id = 0;
    Nodes::Family family = Nodes::Family::Other;
    uint32_t cls = 0, type = 0;
    std::string model; // objects: the model file
    uint32_t source = 0; // scenery: what it's a copy of (itself if not a copy)
    V3 size = {};       // scenery: its box's size
};
bool Alike(const Thing& a, const Thing& b, Similar by);

// Snapshots' files: "<8 hex digits>.<name>.txt" (a name of letters, digits, '_' and '-', 1-32; not "autosave"), and
// one recognised.
bool ValidSnapshotName(const std::string& name);
std::string SnapshotFileName(uint32_t scene, const std::string& name);
bool SnapshotOf(const std::string& fileName, uint32_t scene, std::string* name);

// --- the game side ------------------------------------------------------------------------------------------------

// Registers the layout's records (Layout::AddExtension) and the actions' callbacks; once.
void Init();
const State& Current();
// Changed as one undo step (`label`).
void Change(const State& next, const char* label);

// Per row (a node's name: an object's id, a scenery piece's checksum, a node's own name).
bool HiddenInEditor(const Nodes::Node& node);
void SetHiddenInEditor(const Nodes::Node& node, bool hidden);
bool Locked(uint32_t id);
void SetLocked(uint32_t id, bool locked);
bool Selectable(const Nodes::Node& node);
void SetSelectable(const Nodes::Node& node, bool selectable);
// The editor's hooks: a node's gizmo not drawn (hidden in the editor) / not picked; the selection locked against changes
// (said on the message line when `say`).
bool GizmoHidden(uint32_t name);
bool GizmoPickable(uint32_t name);
bool SelectionLocked(bool say);

// Groups.
void MakeGroup(const std::string& name); // of the selection
void SelectGroup(int index);
void DeleteGroup(int index);
void RenameGroup(int index, const std::string& name);
void AddToGroup(int index); // the selection added
bool InGroup(const std::string& group, const Nodes::Node& node);

// Box select (the editor's click handling): armed (Ctrl+B: the next press starts one anywhere); a press where nothing is
// (`x`, `y`: back buffer pixels; `mods`: Actions' bits); the drag; let go - applied when it moved.
void ArmBox();
bool BoxArmed();
void BoxPress(float x, float y, uint8_t mods);
void BoxTrack(bool down, float x, float y);
bool BoxActive(); // (a drag drawing a box)
// The box's outline for the overlay (back buffer pixels): false when there is none.
bool BoxOutline(Rect* rect, bool* crossing);

// Select similar / all / invert (the Shift+G menu, the command search).
void SelectSimilar(Similar by);
void SelectFamily();
void SelectAll();
void InvertSelection();
// What Select similar on a node asked the Outliner's search to show (its family: "rails:"), taken once ("": nothing).
std::string TakeOutlinerSearch();

// Snapshots.
std::vector<std::string> Snapshots(); // this level's, by name
std::string ActiveSnapshot();
bool SaveSnapshot(const std::string& name, std::string* message);
bool SwitchSnapshot(const std::string& name, std::string* message);
bool DeleteSnapshot(const std::string& name, std::string* message);

} // namespace Front::LevelEditor::Organize
