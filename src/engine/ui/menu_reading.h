#pragma once

#include <cstdint>
#include "base/maths/vec2.h"

namespace frontend {
class UiWidget;
}

// NOT ORIGINAL - reading the original menus' screen elements (ours, from the mouse_menus feature: frontend/mouse_menus.h):
// where an element and a menu's item are on the 640x480 layout, the window -> layout conversion, which item is under a
// point, a menu's selection, focus and wrapping, and moving a selection item by item with pad events. The mouse menus,
// the new front end's mirror, the test-automation channel and the level editor's cursor use them.
//
// Stage 1: the screen elements are read by their offsets here (absolute position, size, menu fields), from what
// the layout code (0x004A9D30, 0x004A8C30) and the menus (0x004A8100, 0x004A7850) do.
namespace frontend::menu_reading {

// Window pixels -> the 640x480 menu layout: the window's size, the back buffer's (0x00786D6C / 0x00786D70) and
// the layout -> back buffer conversion render::set_letterbox set (scale 0x00786D80 / 84, offset 0x00786D88 / 8C:
// back buffer = layout * scale + offset).
struct ScreenMapping {
    int window_width_, window_height_;
    uint32_t back_buffer_width_, back_buffer_height_;
    float scale_x_, scale_y_;
    int32_t offset_x_, offset_y_;
};
// The game's current conversion for a window of this size.
// A window's size in pixels.
struct WindowSize {
    int width;
    int height;
};
ScreenMapping current_mapping(WindowSize window);
// False (nothing written) when a size or scale is 0.
bool window_to_layout(const ScreenMapping& mapping, float window_x, float window_y, float* layout_x, float* layout_y);

// An element's rectangle on the layout: its absolute top left (+0x134, after its justification and its parents'
// positions and scales) and its size (+0x160 / +0x164, the base dims, times its absolute scale +0x13C / +0x140).
// Rotation is ignored.
struct Rect2 {
    float left, top, right, bottom;
};

[[nodiscard]] inline bool get_holds(const Rect2& rect2, float x, float y)
{
    return x >= rect2.left && x < rect2.right && y >= rect2.top && y < rect2.bottom;
}
Rect2 element_rect(const frontend::UiWidget* element);

// An element's area with its rotation: from its absolute top left (+0x134 / +0x138) along its absolute angle
// (+0x14C, radians: its own and its parents' - the layout pass 0x004A9D30 places a child's top left at the
// parent's top left plus the parent's angle turning the scaled local offset, so an element's contents lie along
// its angle from its top left), `width` x `height` (base dims times absolute scale; either may be negative).
struct Cuboid {
    float x, y, width, height, angle;
};
Cuboid element_box(const frontend::UiWidget* element);
bool box_contains(const Cuboid& box, maths::Vec2 at);
// Whether an angle turns anything (not a whole number of turns).
bool get_rotated(float angle);

// A menu (a vmenu or an hmenu - MenuBase, vtable 0x0064A048) and its item.
bool is_menu(const frontend::UiWidget* element);
bool menu_runs_top_to_bottom(const frontend::UiWidget* menu); // +0x190
int selected_index(const frontend::UiWidget* menu);           // +0x178 (-1: none)
bool menu_has_focus(const frontend::UiWidget* menu);          // +0x180
bool menu_wraps(const frontend::UiWidget* menu);              // +0x1A1
int child_count(const frontend::UiWidget* element);
// What the menu's move (0x004A7850) skips: an item tagged tag_not_focusable, or one with block_events (flag
// 0x200000) on it or a parent.
bool item_focusable(const frontend::UiWidget* item);

// Items a menu passes over by script: the main menu's spin menu (make_spin_menu_item) gives its filler items the
// tag `blank` and its disabled ones `active = 0`; their focus script moves the selection on at once. They are
// never a mouse target.
bool item_passed_over(const frontend::UiWidget* item);

// Where an item is drawn: itself and everything below it that isn't hidden (whatever their alpha - the spin
// menu fades out the front item's text under its "roundbar" label; theme_menu_add_item's text and highlight bar
// are centred 5-7 pixels above the top of the item's own 20-pixel-high container, so mostly outside it). Parts
// with no area count only if they are the item itself. The unrotated parts make one rectangle,
// widened to the menu's width (a vertical menu) or height (a horizontal one) unless the menu is rotated; rotated
// parts count each as its own turned box.
struct Hit;
bool item_contains(const Hit& hit, maths::Vec2 at);
// The rectangle around all of it (for the debug log).
Rect2 item_bounds(const Hit& hit);
// A menu whose items are turned (the main menu's spin menu: its vmenu and items have rot_angle): hovering
// doesn't select (the items would turn away under the cursor and the next one would be selected, and so on);
// a click turns it to the item and chooses it.
bool is_rotating_menu(const frontend::UiWidget* menu);

struct Hit {
    frontend::UiWidget* menu;
    frontend::UiWidget* item;
    int index; // the item's place in the menu's children
};
// Which menus count (the game's test: menu_takes_pad_events).
using MenuFilter = bool (*)(frontend::UiWidget* menu);
// The item under (x, y): of the elements below `root` that are focusable items of a menu `accepts` (and not passed
// over), not hidden and visible (alpha), inside every window element above them (their clip region; scrolling
// menus too), the deepest - and of overlapping ones the last in the tree. An item's area: item_contains. False if
// none.
bool find_item_at(frontend::UiWidget* root, float x, float y, MenuFilter accepts, Hit* out);
// Whether there is a menu `accepts` below `root` (vertical only: `vertical`).
bool any_menu(frontend::UiWidget* root, MenuFilter accepts, bool vertical_only);

// Move a menu's selection to `target`, one pad event (`send`, direction -1 or +1) at a time, re-reading the
// selected index (`read`, -1 when the menu is gone) after each: the shorter way round when the menu wraps; stops
// when the index is the target, didn't move, moved the wrong way or past the target (an item that can't be
// focused), or after `count` events. Returns the events sent.
int step_selection(int target, int count, bool wraps, int (*read)(void* context),
                   void (*send)(void* context, int direction), void* context);

// Moving a selection to an item over several frames - for menus that don't move on every pad event (the spin
// menu takes pad_up / pad_down only while its 0.15 s turn isn't running: spin_menu_input_timer turns
// disable_pad_handling on, clearing the menu's +0x1A0 - and TemporarilyDisableInput blocks the menus for a
// moment). Once a frame: step_selection again, until the selection is the target; gives up after `kTargetFrames`
// frames or `kMaxStuckFrames` frames in a row without the selection changing, or when it has none (-1). `wait`:
// send nothing this frame (the current item is one the menu passes over by itself).
enum class TargetState : uint8_t { kReached, kMoving, kGaveUp };
struct Target {
    int index; // the item's place in its menu
    int count; // the menu's children
    bool wraps;
    int frames_left_;
    int stuck_frames_; // frames in a row ending with the selection where the last one ended
    int last_seen_;    // the selection at the end of the last frame (-2: none yet)
};
constexpr int kTargetFrames = 300;
constexpr int kMaxStuckFrames = 45;
Target start_target(int index, int count, bool wraps);
TargetState advance_target(Target& target, bool wait, int (*read)(void* context),
                           void (*send)(void* context, int direction), void* context);

// Whether a menu takes the pad events now (the game's test, as hovering uses it): it has the focus, and - while the
// element manager gives pad events only to the elements of the highest priority - it is one of those.
bool menu_takes_pad_events(frontend::UiWidget* menu);

// A click on an item with its own pad_left / pad_right handlers: -1 in its left 30%, +1 in its right 30%, else 0
// (pad_choose).
int option_side(const Rect2& item, float x);

} // namespace frontend::menu_reading
