#pragma once

#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"

namespace Obj {
class CObject;
class CCompositeObject;
} // namespace Obj

namespace Front::MenuBackdrop {
struct View;
}

// NOT ORIGINAL - part of the menu_backdrop feature (ours, added at the user's request; not the game's and not
// PartyMod's). See menu_backdrop.h for the backdrop itself.
//
// While the front end shows the backdrop (Los Angeles) through a held view, the skate shop's scene isn't drawn -
// but the main menu's 3D objects live there: the spinning "wheel of loogies" (48 level objects of mainmenu.qb,
// wol_inner_ring ... wol_lightbulb_28, turned by spin_menu_spin_geometry's Obj_Rotate) and the user's skater, who
// spins it (SpinMenuSkaterIdle: Obj_MoveToNode trg_playerrestart, PausePhysics, then animations only). This moves
// those objects into the backdrop: each one keeps its place relative to the camera - the placement is the rigid
// transform that takes the front end's own (menu) camera to the backdrop's view - so the menu looks as it did, in
// Los Angeles.
//
// - Which objects: a table (kObjects in the .cpp: name checksums, an optional offset each). A skater is only moved
//   while its physics is paused (the main menu's), never while it skates (the options menu's tricks).
// - When: the setting is on, the career's level is 0 (the front end), the backdrop's scene is loaded, a menu camera
//   plays (the held view, MenuBackdrop::Holding), and the menu camera's own pose is known: it is read from the
//   camera's object (the owner of the viewport camera's camera component - its position and orientation before
//   the held view replaces them) while nothing is moved, once it stayed the same for two updates in a row (a camera
//   still moving after 30 is taken as it is then), and kept per camera animation for as long as the front end and
//   its backdrop stay loaded.
// - How: position, old position, velocity, orientation and display matrix written, then the object's
//   SetTeleported(true) (its components' Teleport: the model, shadow and collision follow at once). Everything
//   the object was is kept, and put back exactly when it is restored.
// - What fights it: whatever the game writes is noticed (at the start of every object update walk and every
//   render, and inside each moved object's update: after its script, after its components): a value written near
//   where the object was (Obj_MoveToNode to its node, a component keeping its own copy) is taken as the object's
//   real place and moved again; a value near where we put it (Obj_Rotate turning the wheel about its own axis,
//   anything moving it a little) is kept, and its real place is worked back from it.
// - Restored (each object back to its real place, then forgotten): when any of the conditions fails (another
//   camera - Create-A-Skater, the skater select... -, leaving the front end), when the menu camera changes (then
//   moved again with that camera's placement), when a skater's physics is unpaused, before the script functions
//   Cleanup, UnloadAllLevelGeometry and UnloadScene, and at the start of every LoadScene - and nothing is moved
//   again until the backdrop is loaded again or the front end is gone. An object destroyed meanwhile (no longer
//   found under its id, or another object found instead) is forgotten without being touched.
// With the setting off nothing is ever moved and every hook returns at once.
namespace Front::FrontEndObjects {

// A rigid transform (in double): p' = p * rot + trans (the game's row vectors), directions r' = r * rot.
struct Placement {
    double rot[3][3];
    double trans[3];
};
// A camera frame from its backward (row 2) and up (row 1) directions as the views are made (row 0 = up x back),
// made orthonormal: row 2 = back normalised, row 0 = up x back normalised, row 1 = row 2 x row 0.
void CameraFrame(const float back[3], const float up[3], double frame[3][3]);
// The placement taking a camera at `cameraPos` (back / up) to `view` - its position to the view's, its frame to the
// view's - then moved by `offset` in the view's frame (right, up, back).
Placement MakePlacement(const float cameraPos[3], const float cameraBack[3], const float cameraUp[3],
                        const MenuBackdrop::View& view, const float offset[3]);
// A point / a direction through the placement or back (x, y, z; w kept), rounded once to float.
Mth::Vector MapPoint(const Placement& placement, const Mth::Vector& point);
Mth::Vector MapDirection(const Placement& placement, const Mth::Vector& direction);
Mth::Vector UnmapPoint(const Placement& placement, const Mth::Vector& point);
Mth::Vector UnmapDirection(const Placement& placement, const Mth::Vector& direction);

// What a table object is: a skater is moved only while its physics is paused.
enum class Kind { Object, Skater };

// The state of the game the decisions depend on (read by Update; self-tests make their own).
struct Inputs {
    const MenuBackdrop::View* view;   // the setting's view (null: off)
    bool frontEnd;                    // the career's level is 0
    bool backdropLoaded;              // the backdrop's scene is loaded
    uint32_t menuCamera;              // the menu camera animation playing (0: none, or not a menu camera)
    Obj::CCompositeObject* camera;    // the object of the viewport's camera (null: none)
    // Else the pose the game last gave the viewport camera (its setters, before the held view replaced it).
    const Mth::Vector* cameraPos;
    const Mth::Matrix* cameraMatrix;
};
// An object by id (Obj::FindObject in the game).
using Lookup = Obj::CObject* (*)(uint32_t id);

// One step: restores, captures the menu camera's pose (`sample`: a sample may be taken), moves objects. Returns
// whether a sample was taken.
bool Step(const Inputs& inputs, bool sample, Lookup lookup);
// Step with the game's state (`logic`: from the composite object manager's update, else from the renderer). The
// camera is sampled once a frame, by whichever step comes first (the frames counted by the renders).
void Update(bool logic);

// The hooks in CCompositeObject::Update (0x0045D920): after the object's script and own update, and after its
// components. Nothing unless the object is moved.
void BeforeComponents(Obj::CCompositeObject* object);
void AfterComponents(Obj::CCompositeObject* object);
// The hook before a script calls a C function: Cleanup, UnloadAllLevelGeometry, UnloadScene restore and hold off.
void BeforeCFunction(const void* function);
// A level is being loaded (LoadScene) or cleaned up: everything restored, nothing moved until the backdrop is
// loaded again or the front end is gone.
void OnLevelChange();
// The backdrop has just been loaded (MenuBackdrop::OnSceneLoaded): objects may be moved again, the camera poses
// are captured anew.
void OnBackdropLoaded();
// Everything restored now (`why`: logged with debug_backdrop).
void RestoreAll();
void RestoreAll(const char* why);
// thug2-decomp.ini [features] debug_backdrop (default 0): with 1, every change of the decision's inputs, each
// object moved, restored or forgotten, and every restore's reason are logged. Self-tests set it for their run.
int& DebugBackdrop();

// For self-tests: the state, and forgetting it all without restoring anything.
bool Applied();
bool HoldingOff();
int MovedCount();
int CameraPoseCount();
bool IsMoved(const Obj::CCompositeObject* object);
void Reset();
// The table: its object ids (expanded names) and kinds; count returned.
int TableIds(uint32_t* ids, Kind* kinds, int capacity);

} // namespace Front::FrontEndObjects
