#pragma once

// Mouse control of the menus - the mouse_menus feature (thug2-decomp.ini [features] mouse_menus, default 1; 0 = no
// mouse handling at all, as the original). Not in the original game, not from PartyMod: our own addition.
//
// The mouse drives the menus by sending the same pad events a controller does (pad_up / pad_down / pad_left /
// pad_right / pad_choose / pad_back, launched like modules::FrontEnd::input_logic launches them):
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
// The screen elements are read through engine/ui/menu_reading.h (where an item is, which one is under the cursor, the
// selection stepped item by item); the pad events are sent as the front end sends them (game/front_end/menu_pad_event.h).
// Stage 2 - once the screen element classes are decompiled - should replace those reads with the classes' fields, the
// "takes pad events" test with the menu's own (0x004A8100's start), and the stepping with a direct "focus this item".

namespace ext {
class Extension;
}

namespace mouse_menus {

// [features] mouse_menus (read once).
bool& is_switched_on();

// Start listening to SDL's mouse events (os_layer::init_input; nothing when the feature is off).
void prepare_all();
// The mouse menus as an extension (engine/ext/extension.h; app/ registers it first): its event handler, and once a
// frame from the front end's first input handler (modules::FrontEnd::input_logic) the cursor shown or hidden, then
// the mouse's movement, clicks and wheel turned into menu events - after every other extension's frame; while an
// extension owns the game (the level editor), the mouse's events dropped before the others' frames.
ext::Extension& extension();

// [features] debug_mouse (default 0): each click logged - the cursor on the layout, the item found (or the
// elements under the cursor when none), its menu's state, and which events were sent or why none were.
bool& debug_enabled();

} // namespace mouse_menus
