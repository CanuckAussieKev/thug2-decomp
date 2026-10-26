#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "game/front/level_editor_nav.h"

struct ImFont;

// NOT ORIGINAL - the level editor's navigation and hints (ours; see level_editor.h):
//   - Context hints: the status bar shows what the mouse buttons and the main keys do now, with key caps and mouse /
//     controller glyphs - from the action registry (keys as bound now) and the editor's state (level_editor_hints.h).
//     Holding Ctrl / Shift / Alt lists what that modifier does.
//   - Quick selection sets (UX3's groups - named, saved in the layout, the Outliner's Groups tree - by the names "Set 1"
//     .. "Set 9"): Ctrl+Alt+1..9 stores the selection as set n, Alt+Shift+1..9 selects it again; the toolbar's Sets menu
//     stores / recalls / adds to / takes from them and the other groups.
//   - Pie menus (Blender's): Q or Tab - the common actions for what's selected (tools, drop, copy, frame, snap,
//     delete...; nothing selected: add, place, box, ruler, walk, simulate, play, search); Alt+Q or Shift+Tab - the view
//     (the four view modes, collision, trigger volumes, jump arcs, the top / perspective views). Held: point and let go;
//     tapped: it stays until a click (Esc or the right button closes it). Q opens no pie while the right button is held
//     (Q flies down then), nor with [editor] wasd_flies=1 while a fly key is held.
//   - Controller mode (Preferences, [editor] pad=1; or the command search's "Controller mode"): the first game
//     controller drives the editor - left stick flies, right stick looks, LT / RT down / up, L3 faster, A selects at the
//     crosshair (held: drags), B backs out (as Escape, never leaving), X the pie, Y the view pie (the left stick points,
//     let go or A runs), D-pad nudges, LB / RB turn, R3 frames, Back undoes; Start still leaves the editor. Walking:
//     left stick walks, A jumps. The mouse moving takes the cursor back from the crosshair.
//   - Wheel drill-through (TrenchBroom's): Alt+wheel over a spot steps what a click would take through everything under
//     the cursor, front to back (as clicking again does), highlighting it, without selecting it - then a click takes it.
//   - The follow camera (Unity's Shift+F): Shift+F frames the selection and keeps the camera with it as it moves - nudged,
//     typed into the Inspector, undone, a moving object's preview - even after flying off it with the right button
//     (keeping the offset); Shift+F again stops. Not during a mouse drag (the drag follows the cursor).
//   - Click-to-teleport: a double click on a surface (on nothing a click would select) takes the camera there (a short
//     glide, kept looking the same way, Nav::TeleportTarget); "Teleport to the cursor" in the command search too.
//   - Walk mode (Shift+`, Blender's walk navigation): the camera at a skater's eye height with gravity and collision (the
//     game's collision ray) - W A S D walk (the fly keys), Space / E jump, Shift runs, the mouse looks; Shift+` or Esc
//     stops (the camera stays). For judging a level's scale without a test run.
// Every key is the registry's (level_editor_actions.h: rebindable; registered here with Actions::Register).
namespace Front::LevelEditor::Ux8 {

// --- the editor's hooks (level_editor.cpp) --------------------------------------------------------------------------

// Once: the actions registered (Actions::Register - also the self-test's), the controller-mode setting read.
void Init();
void RegisterActions();
// The editor started / stopped (a pie closed, walking stopped, the follow camera off).
void Start();
void Stop();

// A game controller this frame (controller mode on and one attached; else `active` false and nothing else set).
struct Pad {
    bool active = false;
    bool used = false;       // a stick past its dead zone or a button down this frame
    float moveX = 0, moveY = 0; // left stick (-1..1; y: up = forward)
    float lookX = 0, lookY = 0; // right stick (y: up = up)
    float upDown = 0;        // RT - LT (0..1 each)
    bool fast = false;       // L3 held
    bool aDown = false;
    int aPresses = 0, aReleases = 0;
    bool b = false, x = false, y = false, back = false, r3 = false, lb = false, rb = false; // pressed this frame
    bool xDown = false, yDown = false;
    bool up = false, down = false, left = false, right = false; // the D-pad held
};
Pad ReadPad();
bool PadMode();
void SetPadMode(bool on); // (written to the ini)
// The keyboard with the controller's held buttons added (the D-pad as the nudge actions' keys, L3 as Shift) - a copy.
const uint8_t* PadKeys(const uint8_t* keys, const Pad& pad);
// Whether the crosshair (the viewport's middle) is the cursor: since the controller was last used, the mouse hasn't
// moved. `x`, `y`: where (window pixels).
bool PadCursor(const Pad& pad, bool mouseMoved, float* x, float* y);
// What the controller asks of the editor this frame, besides the mouse and keys: B (cancel), the turn / frame / undo
// buttons (Actions::Trigger'ed here; a simulation stopped first - level_editor_playtest.h).
bool PadCancel(const Pad& pad);

// Once a frame after the keymap's BeginFrame (Actions): this module's actions and the pies. `leftPresses` /
// `leftReleases` / `rightPress`: the mouse's this frame (a pie takes them: the view doesn't see them).
struct Frame {
    float mouseX = 0, mouseY = 0; // window pixels
    int* leftPresses = nullptr;
    int* leftReleases = nullptr;
    bool right = false;
    bool dragging = false;
    bool browser = false;
    const uint8_t* keys = nullptr;
    Pad pad;
    bool haveSelection = false;
};
void Update(Frame& frame);
// Escape: a pie closed or walking stopped (true: it was taken).
bool Cancel();
bool WantsEscape(); // (a pie or walking: Escape doesn't leave the editor)

// The follow camera.
bool Following();
Nav::Follower& Follower();
// Walk mode (the editor runs it: Update returns early - level_editor.cpp).
bool Walking();
void SetWalking(bool on, V3 camera);
Nav::Walker& WalkerState();
// Asked for by an action this frame (taken once): walk mode switched, a teleport to the cursor.
bool TakeWalkToggle();
bool TakeTeleport();
// A click on nothing (`now` ms, window pixels): true on the second of a double click.
bool NoteEmptyClick(uint32_t now, float x, float y);
// A glide (teleport): the camera's place this frame (false: none under way).
void StartGlide(V3 from, V3 to);
bool Glide(V3* pos, double seconds);
void StopGlide();

// For the status bar.
bool PieOpen();
bool PieSticky();
bool PadOwnsCursor(); // (controller mode, and the crosshair is the cursor)

// Selection sets (UX3's groups; the keys, the toolbar's Sets menu - call from the editor's Update, not while drawing):
// the selection stored as the group `name` (replacing it), the group selected, the selection added to / taken from
// group `index`. Each an undo step (Organize::Change).
void StoreSet(const std::string& name);
void RecallSet(const std::string& name);
void AddToSet(int index);
void TakeFromSet(int index);

// --- the UI (level_editor_ux8_ui.cpp, ImGui) -----------------------------------------------------------------------

struct Fonts {
    ImFont *ui = nullptr, *small = nullptr, *mono = nullptr;
    float scale = 1.0f;
};
// Over the view: the pie, the controller's crosshair, walk mode's crosshair; `viewport` the layout's view rectangle
// (window pixels - the crosshair's place).
void Windows(float vx, float vy, float vw, float vh, const Fonts& fonts);
// The Edit menu's Selection Sets submenu (level_editor_ui_menus.cpp).
void SetsMenu(const Fonts& fonts);
// The status bar's hints (key caps, mouse and controller glyphs); with `maxWidth` (pixels) the hints that don't fit are
// left out, an ellipsis in their place.
void HintRow(const std::vector<std::pair<std::string, std::string>>& hints, const Fonts& fonts, float maxWidth = 0.0f);
// The preferences window's section (controller mode).
void PrefsSection();

} // namespace Front::LevelEditor::Ux8
