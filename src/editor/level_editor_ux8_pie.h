#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// NOT ORIGINAL - the level editor's pie menus: their slices (ours; see level_editor_ux8.h). Each slice runs a registry
// action (level_editor_actions.h: Actions::Run - the editor's own key code does it, under its own conditions) or sets
// a tool / a view mode directly; its keys are shown beside its name.
namespace frontend::level_editor::ux8::pie {

enum class Kind : uint8_t { kCtxInfo, kView };
struct Item {
    std::string label;
    std::string keys;                // as bound now ("" none)
    std::function<void()> run;
    std::function<bool()> enabled;   // (greyed when false)
    std::function<int()> checked;    // 1 on, 0 off, -1 not a switch
    bool edits = false;              // changes the level (a simulation stops first)
};
struct State {
    bool open = false;
    Kind kind = Kind::kCtxInfo;
    std::vector<Item> items;
    float x = 0, y = 0; // the centre (window pixels)
    int key = -1;       // the registry action that opened it (-1: a controller button)
    bool pad_button_ = false;
    bool sticky = false; // (tapped open: stays until a click)
    uint32_t opened_at_ = 0;
    int hovered = -1;
};
// Pixels from the centre before a slice is pointed at.
constexpr float kDeadzone = 28.0f;
// The slices: with a selection - Move, Rotate, Snap, Drop, Scale, Delete, Duplicate, Frame, Duplicate linked, Arrange
// (UX7's); without - Add, Place, Box
// select, Ruler, Walk, Simulate, Play, Trails. The view's: Lit, Unlit, Wireframe, Collision only, Collision, Trigger
// volumes, Jump arcs, Top / perspective.
std::vector<Item> context_items(bool have_selection);
std::vector<Item> view_items();

} // namespace MenuSystem::level_editor::ux8::Pie
