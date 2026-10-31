#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "editor/level_editor.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_transform.h"

// NOT ORIGINAL - the level editor's "adjust last action" (ours, Blender's Adjust Last Operation - F9 there). After a move (a drag, the arrow keys, typed values), a turn (the ring, Z / X), a scale
// (the cubes, [ / ]), a duplicate (Ctrl+D, Ctrl+Shift+D, Ctrl+Alt+D), an align / distribute (level_editor_arrange.h) or a
// place (the Add / Place palettes), a small panel at the view's bottom left shows its numbers - the offset, the angle,
// the factor, how many copies, the axis and edge, where it was placed - and changing one makes the action again with
// them, as the same undo step: the step it made is put back (without a redo) and the action made again from where it
// started (the copies from their source; a placed thing just moved / turned - its step is its existence). Only while
// that action's step is still the newest on the undo stack (another action, an undo or a redo: the panel says so).
// F4 hides / shows the panel.
namespace frontend::level_editor::adjust {

enum class Kind : uint8_t { kNone, kShiftBy, kTurn, scale_tool, kDuplicate, kJustify, kDistribute, kPlace };

struct ActStep {
    Kind kind = Kind::kNone;
    V3 offset = {};        // Move: from where it was; Duplicate: each copy from the one before (before its turn)
    float degrees = 0.0f;  // Turn; Duplicate: each copy turned; Place: turned since it was placed
    bool quarters = false; // (scenery: turns are whole quarter turns)
    V3 factor = {1.0f, 1.0f, 1.0f}; // Scale (along the world's axes)
    bool uniform = true;
    int count = 1;                         // Duplicate: the copies
    bool grid = false;                     // Move / Duplicate: the offset on the grid (the toolbar's size)
    int axis = 0, edge = 0, reference = 0; // Align / Distribute (Arrange::Edge, Arrange::Reference)
    bool local = false, gaps = false;
    V3 pos = {}; // Place: where it is
};

// --- the pure parts ---------------------------------------------------------------------------------------------

const char* get_title(Kind kind);
// The numbers as they'll be used: a count within 1..kMaxCopies, a factor within 0.05..20 (uniform: all three the x),
// a turn of scenery the nearest whole quarter turns (degrees multiples of 90), an offset on `grid` (when asked; 0: as
// it is).
constexpr int kMaxCopies = 64;
ActStep normalised(const ActStep& action, float grid);
bool get_same(const ActStep& a, const ActStep& b);
// A duplicate's step for the copies (xform::RepeatStep: its offset, turn - or quarter turns for scenery).
xform::RepeatStep step_of(const ActStep& action);

// --- the game side ------------------------------------------------------------------------------------------------

// The editor made an adjustable action (its undo step just pushed - except Place's, whose step may be older): it
// becomes the last action. Duplicate: `source` the selection it copied. Ignored while an adjustment is being made.
void get_note(const ActStep& action);
void note_duplicate(const edit::Selected& source, const ActStep& action);
bool applying();
// The panel's numbers changed (the UI): made again in the next Tick.
void get_request(const ActStep& action, bool now);
// Once a frame while the editor runs (after the editor's keys: F4).
void tick();
// What the panel shows: the last action (kind None: nothing), whether it can still be adjusted, shown or hidden (F4).
const ActStep& final_one();
bool get_adjustable();
bool& get_shown();
// The panel (level_editor_arrange_ui.cpp): at the view's bottom left (`x`, `y`: its corner; back buffer pixels as the
// UI's layout gives them).
void get_panel(float x, float y, float ui_scale);
// The editor starting / a new level: forgotten.
void get_forget();

} // namespace frontend::level_editor::adjust
