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
// (UX5: the Problems window's and the notes' Go to) The camera taken to a node, selected (false: no such node now); to a
// scenery piece, selected (false: none, or not movable); to look at a point from `distance` away (nothing selected).
// Call from a module's Update (not while the UI draws).
bool GoToNodeNamed(uint32_t name);
bool GoToScenery(uint32_t sector);
void GoToPoint(V3 pos, float distance);

// --- for the history, changes, selection and clipboard modules (level_editor_ux3.h) ---------------------------------

// The undo / redo stacks by their steps' labels (each step's short label where it was made, and what was selected then):
// undo oldest first (the last is the next undone), redo next first. JumpHistory: `steps` undone (negative) or redone.
struct HistoryEntry {
    std::string label, detail;
};
std::vector<HistoryEntry> UndoHistory();
std::vector<HistoryEntry> RedoHistory();
void JumpHistory(int steps);
// A module's undo step with a label (PushModuleUndo's steps are labelled by their module otherwise).
void PushModuleUndoLabelled(std::shared_ptr<ModuleStep> before, const char* label);
// The locks (level_editor_picking.h) as they are now, as an undo step - before a change of them.
void PushLocksUndoLabelled(const char* label);
// A layout's text (Layout::Write's) put in place of the level's changes as one undo step `label` (the layout as it is now
// kept for Ctrl+Z): the Changes panel's Revert, a paste, a snapshot switched to. False (`message`): not in a level, or
// the text isn't a layout of this level.
bool ApplyLayoutStep(const std::string& text, const char* label, std::string* message);

// The selection by ids: the objects (the first is the primary), else the scenery pieces, else a node by itself.
struct SelectionIds {
    std::vector<uint32_t> objects, sectors;
    uint32_t node = 0;
};
SelectionIds CurrentSelection();
void SetSelection(const SelectionIds& ids);
// A thing by its name / checksum - a node (its object or scenery, as the Outliner does), else an object, else a scene
// sector - selected and the camera taken to it. False: nothing of that name.
bool GoToThing(uint32_t id);
// A world point to back buffer pixels (false: behind the camera, or no view yet); the window's mouse pixels to back
// buffer pixels.
bool ProjectToBack(V3 point, float* x, float* y);
// The pick filter (level_editor_picking.h's bits) and whether the editor is moving something now.
uint32_t PickFilterNow();
bool ChangeUnderWay();

// The Inspector's multi-selection and Paste fields: fields set (typed `text` of Fields::Kind `kind`) or removed on each
// of `nodes` - one undo step for all (`label`). How many fields were set (`message`: the first refusal).
struct FieldSet {
    uint32_t key = 0;
    int kind = 0;
    std::string text;
    bool removed = false;
};
int SetFieldsOnNodes(const std::vector<uint32_t>& nodes, const std::vector<FieldSet>& fields, const char* label,
                     std::string* message);
// The selection's transform (its position, heading in radians - objects and point nodes - and scale), and one put on it
// (one undo step; what the selection can't take is left: scenery turns by quarters).
struct TransformValues {
    V3 pos = {};
    bool hasHeading = false;
    float heading = 0.0f;
    bool hasScale = false;
    V3 scale = {1.0f, 1.0f, 1.0f};
};
bool SelectionTransform(TransformValues* transform);
bool SetSelectionTransform(const TransformValues& transform);
// The status bar's message line.
void ShowMessage(const std::string& text, double seconds = 4.0);

} // namespace Front::LevelEditor
