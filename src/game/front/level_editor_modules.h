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

} // namespace Front::LevelEditor
