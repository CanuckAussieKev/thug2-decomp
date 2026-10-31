#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game/front/level_editor_actions.h"

// NOT ORIGINAL - the level editor's status bar hints (ours; see level_editor.h, level_editor_ux8.h): what the mouse
// buttons and the main keys do right now, Blender-style, from the action registry (level_editor_actions.h - the keys as
// bound now, so a rebinding shows at once) and the editor's state (flying, the tool, a drag, the selection's kind, a
// palette or the ruler armed, a pie open, walking, a game controller...). Holding Ctrl / Shift / Alt lists what that
// modifier does (the registry's actions bound with exactly those modifiers, and the mouse gestures it changes).
//
// A hint's keys are tokens joined by '+': a key cap ("Ctrl", "G", "WASD"), a mouse glyph ("LMB", "RMB", "MMB", "Wheel",
// "Mouse", "2xLMB" a double click) or a controller glyph ("PadA", "PadB", "PadX", "PadY", "LS", "RS", "LT", "RT", "LB",
// "RB", "DPad", "Back", "R3", "L3"). The UI (level_editor_ux8_ui.cpp) draws each as its glyph.
namespace Front::LevelEditor::Hints {

// --- the pieces (pure) ---------------------------------------------------------------------------------------------

enum class Selection : uint8_t { None, Objects, Scenery, PointNode, OtherNode, Wall };
enum class Drag : uint8_t { None, Move, MoveAxis, Rotate, Scale };

struct Context {
    bool rmb = false;       // the right button held (looking / flying)
    bool flyAlways = false; // [editor] wasd_flies=1
    uint8_t mods = 0;       // Actions::kCtrl | kShift | kAlt held
    Selection selection = Selection::None;
    bool freeCamera = false; // (with a selection: the camera flew off it - F frames it again)
    int tool = 0;            // the transform tool: 0 move, 1 rotate, 2 scale (level_editor_transform.h)
    Drag drag = Drag::None;
    bool modal = false;      // Alt+G's transform (no button held)
    std::string typed;       // a typed value's description ("" none)
    int carried = 0;         // what the moving scenery carries
    bool browser = false, placeArmed = false;
    bool chainDrawing = false, ladderDrawing = false, chainPick = false;
    std::string chainWaiting; // (Chains::Waiting)
    bool rulerArmed = false, facesMode = false, boxArmed = false;
    int ux9Tool = 0;          // (UX9 - level_editor_ux9.h) 1 the gap wizard picking, 2 the ground tool marking a rectangle
    bool chainPoint = false;  // a rail / ladder / path point selected
    bool following = false;   // the camera follows the selection (Shift+F)
    bool walking = false;     // walk mode
    bool pieOpen = false;     // a pie menu up
    bool pieSticky = false;   // (opened by a tap: stays until a click)
    bool pad = false;         // controller mode with a controller attached
    bool simulating = false;  // UX6's Simulate (F6)
    // What the held modifiers do (ModifierHints from the registry), filled by the caller when `mods` isn't 0.
    std::vector<std::pair<std::string, std::string>> modded;
};
struct Hint {
    std::string keys, what;
};
// An action's keys as bound now ("" unbound) - Actions::KeysText by id, in the game.
using KeyText = std::function<std::string(const char* id)>;

// The hints for this state (at most kMaxHints).
constexpr size_t kMaxHints = 16;
std::vector<Hint> Build(const Context& context, const KeyText& keyOf);
// The first of a KeysText's alternatives ("Ctrl+Y / Ctrl+Shift+Z" -> "Ctrl+Y"; an Info's text as it is).
std::string FirstKey(const std::string& keysText);
// Single plain keys run together for a cap ("W", "A", "S", "D" -> "WASD"; a key with modifiers or a long name: joined
// with a space).
std::string JoinKeys(const std::vector<std::string>& keys);
// The registry's actions bound with exactly `mods` held (their first such chord), as (keys, name): Press and Toggle,
// in the registry's order, at most `limit`.
std::vector<std::pair<std::string, std::string>> ModifierHints(const std::vector<Actions::Action>& actions, uint8_t mods,
                                                               size_t limit);

// A hint's keys split into tokens ('+' between them; a key ending in '+' - "Keypad+" - kept whole).
std::vector<std::string> Tokens(const std::string& keys);
enum class Glyph : uint8_t { Key, Mouse, MouseLeft, MouseRight, MouseMiddle, Wheel, DoubleClick, PadA, PadB, PadX, PadY,
                             PadOther };
Glyph GlyphOf(const std::string& token);

} // namespace Front::LevelEditor::Hints
