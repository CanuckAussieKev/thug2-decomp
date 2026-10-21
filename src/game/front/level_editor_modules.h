#pragma once

#include <cstdint>
#include <memory>

#include "game/front/level_editor.h"

// NOT ORIGINAL - what the level editor (level_editor.cpp) offers the modules that keep their own state and draw their
// own Inspector sections (ours; see level_editor.h): collision face flags (level_editor_faces.h), scenery trick-object
// clusters (level_editor_clusters.h) and camera nodes (level_editor_cameras.h). Each of those modules queues what its
// UI asks for and does it in its own Update, which the editor calls once a frame while it runs.
namespace Front::LevelEditor {

// An undo step of a module's own: what to put back (Restore), and the same things as they are now (Now - what the
// other stack gets). The editor keeps it on its undo / redo stacks like its own steps (64 steps, this session).
struct ModuleStep {
    virtual ~ModuleStep() = default;
    virtual void Restore() = 0;
    virtual std::shared_ptr<ModuleStep> Now() const = 0;
};
// A change is about to be made: `before` (as things are now) goes on the undo stack; the redo stack is dropped and the
// level marked unsaved (the autosave).
void PushModuleUndo(std::shared_ptr<ModuleStep> before);

// The editor's camera now: where it is, which way it looks (yaw / pitch as FrameFromYawPitch takes them), the lens and
// the viewport (back buffer pixels) of the last frame drawn. False while the editor isn't running.
struct ViewNow {
    V3 pos;
    float yaw, pitch;
    Lens lens;
    uint32_t viewport[4];
    float nearZ;
};
bool CurrentView(ViewNow* view);
// The editor's camera put there, looking that way (the selection dropped first: the camera would orbit it).
void LookFrom(V3 pos, float yaw, float pitch);

// --- for the arrange tools (UX7: level_editor_arrange.h, level_editor_adjust.h, level_editor_links.h) --------------
//
// The selection and the editor's own operations on it, each as the keys / drags do it (undo steps included where
// said). Only while the editor runs.
namespace Edit {

enum class Kind : uint8_t { None, Objects, Scenery, Node, Wall };
struct Selected {
    Kind kind = Kind::None;
    std::vector<uint32_t> ids; // objects' ids, sectors' checksums, the node's name, the wall's checksum
};
Selected Selection();
// Exactly that selected again (objects by id - those still there -, sectors, a node by name).
void Select(const Selected& selected);
// The selection's place (as the drags go by: the first object's position, a node's place, the scenery's pivot), its
// base and the middle of its box.
V3 Pos();
float Base();
V3 Centre();
// A change of the selection's places made by the caller (Begin: its undo step's snapshot, what scenery carries armed;
// End: the step pushed - always - and the scenery's collision brought up to date).
void Begin();
void End();
// The selection put at `pos` (Pos), turned by `radians` about up (objects about the toolbar's pivot, a node's heading,
// scenery and walls the nearest whole quarter turns), scaled by `factor` along the world's axes ([ / ]: its own undo
// step; false when nothing changed).
void PlaceAt(V3 pos);
void TurnBy(float radians);
bool ScaleBy(V3 factor, bool uniform);
// Ctrl+D (`repeat`: Ctrl+Shift+D's step; null: beside it) - its undo step; the copies selected. The sources and copies
// it made, in pairs (objects' ids, sectors' checksums) - empty when nothing was copied.
std::vector<std::pair<uint32_t, uint32_t>> Duplicate(const void* repeat);
// The undo stack: a number for its top step (changes with every step pushed or taken), its depth; the steps pushed
// since `depth` folded into one (one Ctrl+Z takes them all); the top step put back and dropped (no redo - the last action
// about to be made again). False: nothing to take.
uint64_t TopSerial();
size_t Depth();
void Fold(size_t depth);
bool TakeBack();
// Copies made (objects' ids - Objects' copies -, sectors' checksums): one undo step that takes them away.
void PushMade(const std::vector<uint32_t>& objects, const std::vector<uint32_t>& sectors);
// The selection's state now as an undo step (before a change made by the caller).
void PushSnapshot();
// Copies of objects removed (as Delete removes them): one undo step that makes them again.
void RemoveCopies(const std::vector<uint32_t>& objects);
// The scenery's collision lookups brought up to date after the caller moved it (no undo step).
void Settle();
// A node field set as the Inspector sets it (one undo step; `kind`: Fields::Kind). False: refused (said).
bool SetField(uint32_t node, uint32_t key, int kind, const std::string& text);
// A chain change made since `before` (Chains::State): its undo step, `select` (a node; 0: none) selected, `message` shown.
void ChainChanged(const void* before, uint32_t select, const std::string& message);
// The ground below a point (the game's collision ray: false - none within reach).
bool Ground(V3 from, V3* ground);
// What the editor is doing: dragging (and with which tool), the cursor (back buffer pixels), the ray under it (false:
// none - over a panel, looking about), the ground marker (the selection's base and the ground below it).
bool Dragging();
int DragMode(); // (Transform::Mode)
void Cursor(float* x, float* y);
bool Ray(V3* origin, V3* dir);
bool GroundMark(V3* base, V3* ground);
// The camera's yaw (radians; the heading new things face).
float Yaw();
// The Add palette (B): open, its tab (0 scenery, 1 objects) and highlighted item's id; a copy of it placed at `at`
// (one undo step, selected; `keepOpen`: the palette stays open).
bool BrowserItem(int* tab, uint32_t* id);
void PlaceBrowserItem(V3 at, bool keepOpen);
// The Place palette (N): its armed entry (-1: none), the entry's colour.
int PlaceArmed(uint32_t* colour);
// Where a click places (the browser's Enter, the Place palette): the ground below what the cursor's ray meets.
V3 CursorPoint();

} // namespace Edit

} // namespace Front::LevelEditor
