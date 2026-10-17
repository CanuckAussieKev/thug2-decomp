#pragma once

// Mouse control of the menus - the mouse_menus feature (thug2-decomp.ini [features] mouse_menus, default 1; 0 = no
// mouse handling at all, as the original). Not in the original game, not from PartyMod: our own addition.
//
// The mouse drives the menus by sending the same pad events a controller does (pad_up / pad_down / pad_left /
// pad_right / pad_choose / pad_back, launched like Mdl::FrontEnd::InputLogic launches them):
// - moving the cursor over an item of a menu that takes pad events selects it (the menu's pad_up / pad_down, or
//   pad_left / pad_right for a horizontal one, until its selected index is the item's);
// - a left click selects the item under the cursor and chooses it (pad_choose) - on an item with pad_left /
//   pad_right handlers of its own (an option's arrows), a click in its left or right 30% sends pad_left /
//   pad_right instead;
// - a right click is pad_back, the wheel pad_up / pad_down (while a menu takes pad events).
// An item's area is what is drawn for it (itself and everything below it, turned elements as turned boxes). Menus
// that don't move on every pad event get their selection stepped over the next frames (the pending target), and
// a click's event is sent once the item is selected. A turned menu (the main menu's spin menu) isn't moved by
// hovering; a click turns it to the item and chooses it. [features] debug_mouse=1 logs every click.
// The OS cursor is shown in the front end and while the game is paused in a level (the pause menu is the same kind
// of menu), hidden otherwise; the park editor is left alone. Nothing happens until the mouse has moved or clicked,
// and hovering only acts when the item under the cursor changes, so a resting cursor doesn't fight the pad.
//
// Stage 1: the screen elements are read by their offsets here (absolute position, size, menu fields), from what
// the layout code (0x004A9D30, 0x004A8C30) and the menus (0x004A8100, 0x004A7850) do. Stage 2 - once the screen
// element classes are decompiled - should replace those reads with the classes' fields, the "takes pad events"
// test with the menu's own (0x004A8100's start), and the stepping with a direct "focus this item".

#include <cstdint>

namespace Front {
class CScreenElement;
}
namespace Mdl {
struct FrontEnd;
}

namespace MouseMenus {

// [features] mouse_menus (read once; self-tests may set it).
bool& Enabled();

// Start listening to SDL's mouse events (Sio::InitSdlInput; nothing when the feature is off).
void Init();
// Once a frame, from the front end's first input handler (Mdl::FrontEnd::InputLogic): the cursor shown or hidden,
// then the mouse's movement, clicks and wheel turned into menu events.
void Update(Mdl::FrontEnd* frontEnd);

// --- the pieces (pure, self-tested) -----------------------------------------------------------------------------

// Window pixels -> the 640x480 menu layout: the window's size, the back buffer's (0x00786D6C / 0x00786D70) and
// the layout -> back buffer conversion Nx::SetLetterbox set (scale 0x00786D80 / 84, offset 0x00786D88 / 8C:
// back buffer = layout * scale + offset).
struct ScreenMapping {
    int windowWidth, windowHeight;
    uint32_t backBufferWidth, backBufferHeight;
    float scaleX, scaleY;
    int32_t offsetX, offsetY;
};
// The game's current conversion for a window of this size.
ScreenMapping CurrentMapping(int windowWidth, int windowHeight);
// False (nothing written) when a size or scale is 0.
bool WindowToLayout(const ScreenMapping& mapping, float windowX, float windowY, float* layoutX, float* layoutY);

// An element's rectangle on the layout: its absolute top left (+0x134, after its justification and its parents'
// positions and scales) and its size (+0x160 / +0x164, the base dims, times its absolute scale +0x13C / +0x140).
// Rotation is ignored.
struct Rect {
    float left, top, right, bottom;
    bool Contains(float x, float y) const { return x >= left && x < right && y >= top && y < bottom; }
};
Rect ElementRect(const Front::CScreenElement* element);

// An element's area with its rotation: from its absolute top left (+0x134 / +0x138) along its absolute angle
// (+0x14C, radians: its own and its parents' - the layout pass 0x004A9D30 places a child's top left at the
// parent's top left plus the parent's angle turning the scaled local offset, so an element's contents lie along
// its angle from its top left), `width` x `height` (base dims times absolute scale; either may be negative).
struct Box {
    float x, y, width, height, angle;
};
Box ElementBox(const Front::CScreenElement* element);
bool BoxContains(const Box& box, float x, float y);
// Whether an angle turns anything (not a whole number of turns).
bool Rotated(float angle);

// A menu (a vmenu or an hmenu - CBaseMenu, vtable 0x0064A048) and its item.
bool IsMenu(const Front::CScreenElement* element);
bool IsVerticalMenu(const Front::CScreenElement* menu); // +0x190
int SelectedIndex(const Front::CScreenElement* menu);   // +0x178 (-1: none)
bool MenuHasFocus(const Front::CScreenElement* menu);   // +0x180
bool MenuWraps(const Front::CScreenElement* menu);      // +0x1A1
int ChildCount(const Front::CScreenElement* element);
// What the menu's move (0x004A7850) skips: an item tagged tag_not_focusable, or one with block_events (flag
// 0x200000) on it or a parent.
bool ItemFocusable(const Front::CScreenElement* item);

// Items a menu passes over by script: the main menu's spin menu (make_spin_menu_item) gives its filler items the
// tag `blank` and its disabled ones `active = 0`; their focus script moves the selection on at once. They are
// never a mouse target.
bool ItemPassedOver(const Front::CScreenElement* item);

// Where an item is drawn: itself and everything below it that isn't hidden (whatever their alpha - the spin
// menu fades out the front item's text under its "roundbar" label; theme_menu_add_item's text and highlight bar
// are centred 5-7 pixels above the top of the item's own 20-pixel-high container, so mostly outside it). Parts
// with no area count only if they are the item itself. The unrotated parts make one rectangle,
// widened to the menu's width (a vertical menu) or height (a horizontal one) unless the menu is rotated; rotated
// parts count each as its own turned box.
bool ItemContains(const Front::CScreenElement* item, const Front::CScreenElement* menu, float x, float y);
// The rectangle around all of it (for the debug log).
Rect ItemBounds(const Front::CScreenElement* item, const Front::CScreenElement* menu);
// A menu whose items are turned (the main menu's spin menu: its vmenu and items have rot_angle): hovering
// doesn't select (the items would turn away under the cursor and the next one would be selected, and so on);
// a click turns it to the item and chooses it.
bool IsRotatingMenu(const Front::CScreenElement* menu);

struct Hit {
    Front::CScreenElement* menu;
    Front::CScreenElement* item;
    int index; // the item's place in the menu's children
};
// Which menus count (the game's test: MenuTakesPadEvents).
using MenuFilter = bool (*)(Front::CScreenElement* menu);
// The item under (x, y): of the elements below `root` that are focusable items of a menu `accepts` (and not passed
// over), not hidden and visible (alpha), inside every window element above them (their clip region; scrolling
// menus too), the deepest - and of overlapping ones the last in the tree. An item's area: ItemContains. False if
// none.
bool FindItemAt(Front::CScreenElement* root, float x, float y, MenuFilter accepts, Hit* out);
// Whether there is a menu `accepts` below `root` (vertical only: `vertical`).
bool AnyMenu(Front::CScreenElement* root, MenuFilter accepts, bool verticalOnly);

// Move a menu's selection to `target`, one pad event (`send`, direction -1 or +1) at a time, re-reading the
// selected index (`read`, -1 when the menu is gone) after each: the shorter way round when the menu wraps; stops
// when the index is the target, didn't move, moved the wrong way or past the target (an item that can't be
// focused), or after `count` events. Returns the events sent.
int StepSelection(int target, int count, bool wraps, int (*read)(void* context), void (*send)(void* context, int direction),
                  void* context);

// Moving a selection to an item over several frames - for menus that don't move on every pad event (the spin
// menu takes pad_up / pad_down only while its 0.15 s turn isn't running: spin_menu_input_timer turns
// disable_pad_handling on, clearing the menu's +0x1A0 - and TemporarilyDisableInput blocks the menus for a
// moment). Once a frame: StepSelection again, until the selection is the target; gives up after `kTargetFrames`
// frames or `kMaxStuckFrames` frames in a row without the selection changing, or when it has none (-1). `wait`:
// send nothing this frame (the current item is one the menu passes over by itself).
enum class TargetState { Reached, Moving, GaveUp };
struct Target {
    int index;       // the item's place in its menu
    int count;       // the menu's children
    bool wraps;
    int framesLeft;
    int stuckFrames; // frames in a row ending with the selection where the last one ended
    int lastSeen;    // the selection at the end of the last frame (-2: none yet)
};
constexpr int kTargetFrames = 300;
constexpr int kMaxStuckFrames = 45;
Target StartTarget(int index, int count, bool wraps);
TargetState AdvanceTarget(Target& target, bool wait, int (*read)(void* context),
                          void (*send)(void* context, int direction), void* context);

// [features] debug_mouse (default 0): each click logged - the cursor on the layout, the item found (or the
// elements under the cursor when none), its menu's state, and which events were sent or why none were.
bool& DebugEnabled();

// A click on an item with its own pad_left / pad_right handlers: -1 in its left 30%, +1 in its right 30%, else 0
// (pad_choose).
int OptionSide(const Rect& item, float x);

} // namespace MouseMenus
