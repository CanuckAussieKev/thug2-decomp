#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game/front/level_editor_ux8.h"

// NOT ORIGINAL - the level editor's tool strip and status bar (ours; see level_editor_ui.h). The top of the screen is a
// menu bar (File, Edit, View, Tools, Help - level_editor_ui_menus.cpp) over one slim strip of the core controls: the
// mode, the transform tool, World / Local, Snap and its grid, the pick filter, the edited copy's name, the budget meter
// and Problems. The strip never draws one control over another: each has a width measured with the fonts and style in
// use, the strip places them (Fit) and what doesn't fit goes into a "»" overflow menu, the least important first. The
// status bar elides its hints and its right-hand text with "…" the same way.
//
// Only ImGui's own API and plain state here: the UI fills a State from the editor and its modules and applies the
// Events; the self-test (src/hook/selftests_editor_ui.cpp) lays both out headless at several window sizes and scales.
namespace Front::LevelEditor::UiStrip {

// --- the pure placement (self-tested) --------------------------------------------------------------------------------

// A control on the strip: its width, the space before it, whether it sits at the strip's right end, and how important
// it is (the lowest goes into the overflow menu first; ties: the later one).
struct Slot {
    float width = 0.0f, gap = 0.0f;
    bool right = false;
    int priority = 0;
};
struct Plan {
    std::vector<bool> shown;
    std::vector<float> x; // (each shown slot's left edge)
    bool overflow = false;
    float overflowX = 0.0f;
};
// The slots placed between `left` and `right`: left-hand ones from the left, right-hand ones against the right end (the
// overflow button, when anything is hidden, at the very end). Nothing overlaps; whatever doesn't fit is hidden.
Plan Fit(const std::vector<Slot>& slots, float left, float right, float overflowWidth, float overflowGap);

// `text` cut to fit `maxWidth` pixels in the current font, ending in "…" when cut ("" when not even that fits).
std::string Elide(const std::string& text, float maxWidth);

// --- the self-test's view of what was drawn -------------------------------------------------------------------------

struct Box {
    float x0, y0, x1, y1;
    std::string what;
};
// While set, every strip control, menu bar title and status bar part drawn is noted here (its item rectangle).
void SetRecorder(std::vector<Box>* boxes);
void Note(const char* what); // (the last item's rectangle)
void NoteRect(float x0, float y0, float x1, float y1, const char* what);

// --- the tool strip -------------------------------------------------------------------------------------------------

struct State {
    bool selection = false;       // something selected (Select lit; else Fly)
    int tool = 0;                 // 0 move, 1 rotate, 2 scale (level_editor_transform.h)
    bool local = false;           // World / Local
    bool snap = false;            // Snap on
    float grid = 25.0f;           // the grid's size
    std::vector<float> grids;     // the sizes offered
    uint32_t pickFilter = 7;      // Picking::kNodes | kObjects | kScenery
    std::string level;            // the edited copy's title
    bool saved = false;           // its layout saved
    bool unsaved = false;         // unsaved changes
    bool budget = false;          // the meter shown (a level up)
    float budgetFill = 0.0f;      // 0..1
    uint32_t budgetArgb = 0;      // its level's colour
    int errors = 0, warnings = 0; // the Problems button's count
    int hidden = 0;               // pieces hidden while editing (Show All)
    bool simulating = false;      // Simulate running (Stop)
    std::function<std::string(const char* id)> keyOf; // an action's keys as bound ("" none)
};

struct Event {
    enum Kind {
        kDeselect,  // Fly
        kTool,      // value: 0 move, 1 rotate, 2 scale
        kSpace,     // World / Local switched
        kSnap,      // value: 0 / 1
        kGrid,      // value: the grid's size
        kPick,      // value: the pick filter's new bits
        kProblems,  // the Problems window
        kBudget,    // the Problems window's Budget tab
        kShowAll,   // everything hidden while editing shown
        kSimulate,  // Simulate stopped
    } kind;
    float value = 0.0f;
};
struct Output {
    std::vector<Event> events;
    bool budgetHovered = false; // (the UI draws the meter's tooltip)
    bool levelHovered = false;
};

// The strip's controls placed and drawn in the current window, across [x, x + width] at y (screen pixels). `scale`: the
// UI's. Returns what was clicked.
void Draw(const State& state, float x, float y, float width, float scale, Output* out);

// --- the status bar ---------------------------------------------------------------------------------------------------

// One row across [x, x + width] at y: the message (or else the key hints - level_editor_hints.h) on the left, `right`
// against the right end; both elided, never overlapping.
void StatusRow(const std::string& message, const std::vector<std::pair<std::string, std::string>>& hints,
               const std::string& right, float x, float y, float width, const Ux8::Fonts& fonts);

} // namespace Front::LevelEditor::UiStrip
