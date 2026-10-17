# Mouse menus, stage 1 (wt/mouse) - NOT run in the game yet

A new feature, not original game code and not PartyMod's: the mouse drives the front end's menus.
`thug2-decomp.ini` `[features] mouse_menus=1` (the default); **`mouse_menus=0` turns it off completely** (no SDL
mouse handler registered, `FrontEnd::InputLogic` doesn't call it, the cursor stays hidden as before).

Code: `src/game/front/mouse_menus.{h,cpp}` (new), two calls added - `Sio::InitSdlInput` -> `MouseMenus::Init()`
(registers an SDL event handler), and `Mdl::FrontEnd::InputLogic` (0x0054FB40) -> `MouseMenus::Update(frontEnd)` at
its start, with the first controller's handler only (once a frame). Tests: `src/hook/selftests_mouse.cpp`.

## How it works
- **Input**: SDL_MOUSEMOTION (only with a real movement: xrel/yrel non-zero), SDL_MOUSEBUTTONDOWN (left, right),
  SDL_MOUSEWHEEL; the window's size is read from the event's window. Nothing happens until the mouse has moved,
  clicked or scrolled once.
- **When**: in the front end (`Nx::CurrentLevel() == 0`) or while the game is paused in a level
  (`FrontEnd::m_paused` - the pause menu is built from the same screen elements and gets the same broadcast pad
  events, so it should work the same way), and never in the park editor unless paused (its element 0x5999551E
  exists - the same test as Sio's InParkEditor). The OS cursor is shown exactly then (SDL_ShowCursor), hidden
  otherwise.
- **Window -> layout**: window pixels -> back buffer pixels (window size vs 0x00786D6C/70), then the inverse of
  SetLetterbox's `back buffer = layout * scale + offset` (0x00786D80/84, 0x00786D88/8C). Note the original's +16 *
  scaleY vertical offset (the root window is 640 x 448 at layout y 0).
- **The element tree**: the screen element manager (0x00701440) holds the root at +0x44 (SetRootScreenElement
  0x004AC900 -> 0x004AB730); an element's first child is +0x58, the next sibling +0x64, the parent +0x4C (the
  order 0x004A9600 counts and the menus index - an item's index is its place in that list). The layout pass
  (0x004A9D30, run from the root every frame by the manager's update 0x004ABD00) stores each element's absolute
  values: top left +0x134/+0x138 (justification already applied - 0x004A8C30: local top left = pos - (just + 1) / 2
  * dims * scale, then parent's position + parent's scale * that, rotated by the parent's angle +0x14C), absolute
  scale +0x13C/+0x140, alpha +0x114 (parents' included). Base dims are +0x160/+0x164 (GetScreenElementDims
  0x004AD650 returns them times the local scale +0xA4/+0xA8; GetScreenElementPosition 0x004ADA80 returns the
  *local* +0xE0/+0xE4, not useful here). So an element's rectangle = (+0x134, +0x138) + (+0x160 * +0x13C, +0x164 *
  +0x140). Rotation is ignored.
- **Menus**: types vmenu 0x130EF802 / hmenu 0xCCDED1E1 (CBaseMenu, vtable 0x0064A048, 0x1A8 bytes; constructors
  0x004A75C0 / 0x004A7630). Fields: +0x178 selected index (-1 none), +0x17C selected item's id, +0x180 has the focus,
  +0x190 vertical, +0x1A0 movement allowed, +0x1A1 wraps. The menu's event handler (0x004A8100, vtable +0x14) moves
  the selection on pad_up / pad_down (vertical) or pad_left / pad_right (horizontal) when focused and +0x1A0, through
  0x004A7A40 -> 0x004A7850 (index += direction; past an end: wrap, or bounce back; skips items tagged
  `tag_not_focusable` (0xF33A3321, set by `not_focusable`) or with `block_events` (flag 0x200000) on them or a
  parent (0x004A8F00); unfocus/focus events to the old/new item). While the manager has pad-event priorities (+0x4E8
  > 0 and the event among its list +0x5DC/+0x69C) only elements with priority (flag 0x10000000 up the chain,
  0x004A8F20) of the top priority (0x004AB7C0) get pad events. Flags (+0x08): 0x400000 hide, 0x200000 block_events,
  0x10000000 priority > 0 (SetProperties 0x004A96B0).
- **Hit-testing** (`FindItemAt`): a pre-order walk from the root; an element counts if its parent is a menu that
  takes pad events (has the focus + the priority test above), it is focusable (as 0x004A7850 decides), not hidden
  (subtrees of hidden elements are skipped), alpha >= 0.01, and the point is inside its area and inside every
  clipping ancestor (window elements 0xA13ADBB7 - 0x004B2060 makes a viewport - and v/hscrollingmenus, when their
  dims are non-zero). An item's area is its rectangle widened to its menu's width (vertical menu) or height
  (horizontal). Later finds win: the deepest, and of overlapping siblings the last in the tree.
- **Hover**: when the item under the cursor changes (only on frames the mouse moved), the menu's selection is
  stepped to it with the same broadcast events the pad sends (launched like InputLogic: target/source 0x36B2EE74,
  `{device_num = the first controller's device, controller = 0}`, skipped when that mapping is inactive or menu
  input blocked): pad_up/pad_down or pad_left/pad_right, re-reading +0x178 after each event, the shorter way round
  when the menu wraps, stopping when it got there, didn't move, went the wrong way (bounce) or past the target, or
  the menu was destroyed (held through a smart pointer). Up to 4 passes, so selecting an option row can focus its
  inner hmenu and then select the hmenu item under the cursor.
- **Clicks**: left = select the item under the cursor (always, not only on change) then pad_choose if it is now
  selected; on a vertical menu's item that has its own pad_left / pad_right handler (its script's event handler
  table), a click in the item's left 30% sends pad_left, right 30% pad_right, the middle pad_choose. Right click =
  pad_back, wheel = pad_up / pad_down (up to 5 per frame) - both only while some menu takes pad events (wheel: a
  vertical one).

## Not covered / known limits
- Menus of the other element type 0x118F6383 (made by 0x004AED90, 0x180 bytes - name unknown) aren't treated as
  menus; nor are custom script-driven "menus" that read pad events without a vmenu/hmenu.
- Rotated elements (+0x14C) and 3D-anchored ones (flag 0x80000) are hit-tested as unrotated rectangles.
- Scrolling menus: clipping by the scrolling menu's own rect is a guess (its dims are taken as the visible window).
- Right click / wheel do nothing on screens without a focused menu (e.g. "press any button" screens).
- The park editor's and Create-a-Graphic's cursors are not mouse-driven. CAG's tool menus are ordinary menus, so
  hovering them moves their selection like the pad would.
- Each hover step is a real pad event: menus play their focus sounds / animations per step (as holding the pad).

## Stage 2 should replace
- The offset reads (`Field<>` in mouse_menus.cpp: +0x08, +0x4C, +0x58, +0x64, +0x114, +0x134..+0x164, +0x178,
  +0x180, +0x190, +0x1A1; the manager's +0x44, +0x4E8, +0x5DC, +0x69C) with CScreenElement / CBaseMenu /
  CScreenElementManager fields.
- `MenuTakesPadEvents` with the menu's own test (the start of 0x004A8100), `ItemFocusable` with 0x004A7850's.
- The stepping (pad events one by one) with a direct "focus this item" (the menu's `focus` event with
  child_id 0x229D3DE4 only works on an unfocused menu - a decompiled CBaseMenu could set the selection directly and
  send the unfocus/focus events itself).
- Exact z-order (draw priority) for overlapping items instead of tree order.

## What to try in the game
1. Start the game (mouse_menus on by default). The cursor should appear in the front end menus. Without touching
   the mouse, the pad/keyboard should work exactly as before (the cursor at rest does nothing).
2. Move the cursor over main-menu items: each should become selected (with the usual focus sound/highlight). Move
   between items quickly, top to bottom and back; on a wrapping menu, from the first item to the last.
3. Left click an item: it opens as with Enter / A. Right click: goes back as with Escape / B. Wheel: moves the
   selection up/down.
4. An options screen with left/right values (e.g. the game options or the skater/board select): click the left or
   right end of an option row - its value should change; click the middle - as pad_choose.
5. Menus inside scrolling lists (e.g. level select, long lists): hover items near the list's top/bottom edge;
   scrolled-out items must not be selectable.
6. Dialog boxes (e.g. "Quit?"): hovering/clicking should only act on the dialog, not the menu behind it.
7. The on-screen keyboard (naming a skater/park): hover keys, click to type.
8. Start a level: the cursor must disappear. Pause (Esc/Start): the cursor appears; hover/click the pause menu
   items; unpause: the cursor disappears again.
9. Park editor: the cursor stays hidden while editing; in its pause menu the mouse works.
10. With `mouse_menus=0`: no cursor anywhere, mouse does nothing (as before).
11. Read thug2-decomp.log: no RUN-TIME CHECK FAILURE / DEBUG CRT REPORT.
Turn it off with `[features] mouse_menus=0` if anything misbehaves.

## Tests
`TestMouseMapping` (the mapping through our Nx::SetLetterbox for 16:9, 4:3, 5:4 back buffers in the front end,
16:10 / 16:9 in a level, a back buffer shown in a different-size window; widescreen front end on and off; refusing
empty sizes), `TestMouseHitTest` (a stand-in tree of 29 elements laid out with the original's justification
0x004A8C30: overlaps, nesting, hidden / not_focusable / block_events (own and the menu's) / alpha 0 items, a hidden
menu, an unfocused menu over a focused one, a clipping window, uniformly and vertically scaled menus, right/bottom
justification, item areas widened across the menu (both sides, both menu directions); plus
ElementRect, ChildCount, IsMenu, AnyMenu, OptionSide), `TestMouseStepping` (a stand-in menu moving as 0x004A7850:
down / up / wrap both ways / ties / skipped items / a target it can't focus / locked / bounce / no selection / out of
range / destroyed mid-way (both directions) / a menu that wraps when told it doesn't, events recorded).

Mutations (`tools/mutate.py`, 40 deliberate bugs in mouse_menus.cpp/.h: mapping, rectangles, every hit-test
condition, AnyMenu, ChildCount, every stepping branch, OptionSide): all caught - 33 at once, 7 after strengthening
the tests (non-proportional window, vertical-only scale, block_events on the menu, widened areas on the left and
below, destroyed menu going round, unexpected wrap). One more (dropping a `next == current` check) was equivalent
- `moved <= 0` covers it - so the check was removed.
Checks: thug2_check 0 MISMATCHED, all 150 early self-tests pass; call audit clean; check_code_writes no overlaps;
register scan only the known 0x4EFF92 false alarm. No REPLACE registrations added (nothing of the game rewritten).
