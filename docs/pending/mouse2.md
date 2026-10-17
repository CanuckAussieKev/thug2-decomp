- [x] Batch 92: Mouse menus, stage 1b - the main menu's spin menu, item areas from what is drawn, selections that take
      several frames, `[features] debug_mouse` - NOT run in the game yet. Record:
      Follows batch 90 (`src/game/front/mouse_menus.{h,cpp}`, tests `src/hook/selftests_mouse.cpp`). The user reported
      that left click didn't choose on the main menu (the rotating menu) nor on the network lobby's ACTIONS box (Host
      Game / Join Game / Refresh next to the GAMES list). Read from the scripts (qb_scripts.prx / mainmenu_scripts.prx
      unpacked; mainmenu_scripts.qb, mainmenu_net.qb, themes.qb) and the menu code (0x004A8100 CBaseMenu's event handler,
      0x004A7B80 its layout, 0x004A76F0 its SetProperties, 0x004A9D30 the layout pass).

      ## Why the main menu didn't click
      - It isn't element type 0x118F6383 (built by 0x004AED90 in CreateScreenElement 0x004ACB70; no script creates that
        type - the scripts use VMenu, hmenu, ContainerElement, SpriteElement, TextElement, textBlockElement,
        WindowElement, vscrollingmenu, HScrollingMenu, Element3D). The main menu is `make_spin_menu`: an ordinary
        **VMenu** `main_vmenu` (alias current_menu), **1 x 1** (`dims = (1, 1)`, `padding_scale = 0`: every item at the
        same point, 0x004A7B80 sizes it from its children), at `spin_menu_pos` (46, 214). Its items
        (`make_spin_menu_item`) are 1 x 1 ContainerElements, each turned by `rot_angle = spin_menu_angles[i]` (0, 22.5,
        ... 337.0 degrees), holding a TextElement at (0, 0) `just [left top]` ("         Story Mode" - leading spaces
        put the words out on the wheel). pad_up / pad_down run `spin_the_menu`, which turns the whole vmenu
        (`DoScreenElementMorph rot_angle = spin_menu_angle`, +-22.5 per step) and the 3D wheel. 8 items are listed twice
        (16 positions, "copy" ids) with `blank` filler items (tag `blank`; their focus script steps on) and disabled
        ones (`active = 0`, also stepped over).
      - So stage 1's item rectangle (the item's own +0x134 + dims * scale) was a 1 x 1 dot at the wheel's centre: the
        visible words weren't part of any item; rotation was ignored too.
      - And the selection only moves one step per 0.15 s: after each step `spin_menu_input_timer` sets
        `disable_pad_handling` (0x004A76F0: clears the menu's +0x1A0, "movement allowed") for spin_menu_spin_time,
        so stage 1's same-frame stepping stopped after one step and the click's "is it selected now" test failed.
      - The front item's own text is faded to alpha 0 (`do_roundbar_highlight`) under the separate "roundbar" label.

      ## Why the network lobby's ACTIONS box didn't click
      - `make_server_list_menu` (mainmenu_net.qb): `actions_menu` is a plain VMenu; its items come from
        `theme_menu_add_item ... no_bg centered`: each item is a ContainerElement `dims = (200, 20)` whose TextElement is
        at `text_pos = (85, -5)` with `just [center center]` (and the highlight bar at (90, -7), centred): the text's
        centre is 5 pixels **above the top** of its own container, so ~3/4 of the visible label lies outside the item's
        rectangle - over the item above it. Stage 1 hit-tested the container only: clicking the first item's label
        ("Host Game") mostly hit nothing (nothing sent), and clicking a later label mostly selected / chose the item
        above it. Every theme_menu_add_item menu has the same offset (text_pos (95, -5) by default).
      - Ruled out: the menu takes pad events normally (focus by `FireEvent focus target = actions_menu`, no pad-event
        priorities), pad_choose is handled by the focused item's own handlers (the menu's handler 0x004A8100 has no
        pad_choose case), no clipping window above it, and Host Game is `not_focusable` only online without
        CanHostGame (then the pad can't choose it either). The lobby's `maybe_launch_lobby_keyboard` (online) only
        reacts to typed text.

      ## Fixes (general)
      - **Item areas from what is drawn** (`ItemContains`): an item's area is itself plus every descendant that isn't
        hidden (whatever its alpha - so the faded front spin item and invisible highlight bars count; parts with no
        area count only if they are the item itself). The unturned parts make one rectangle, widened to the menu's
        width / height as before unless the menu is turned. Overlaps are settled as before (later in the tree wins), which
        gives each theme item exactly its visible label's band.
      - **Rotation** (`ElementBox`, `BoxContains`): an element's contents lie along its absolute angle (+0x14C,
        radians: 0x004A9D30 adds the parent's +0x14C to the local +0xB4 and turns each child's scaled local top left by
        the parent's angle) from its top left. Turned parts are hit-tested as turned boxes.
      - **Selections that take several frames** (`StartTarget` / `AdvanceTarget`, the `Pending` state in Update): a
        hover or click aims at the item; each frame (`Update`, before new input) the selection is stepped again until it
        is there, then a click's event (pad_choose, or pad_left / pad_right on an option's ends) is sent. Gives up after
        300 frames, after 45 frames in a row without the selection changing, or when the menu / item is gone or moved;
        sends nothing on frames the menu doesn't take pad events or sits on an item it passes over by itself (blank /
        active = 0 - its focus script moves on). A right click or the wheel cancels it.
      - **Spin (turned) menus** (`IsRotatingMenu`: the menu or one of its items turned): hovering does **not** move the
        selection - the wheel would turn the hovered item away from under the cursor and the next small movement would
        select another, chasing round. A left click on a visible item turns the wheel to it (one step per 0.15 s, as
        holding the pad) and then chooses it; clicking the front item (its label - the faded text under the roundbar)
        chooses it at once. The wheel turns it like pad_up / pad_down. Items tagged `blank` or `active = 0`
        (`ItemPassedOver`) are never targets.
      - **`[features] debug_mouse=1`** (default 0): on each left click the log gets the cursor in layout coordinates, the
        item found (id, type, rect, angle, alpha, flags, index, its whole area) and its menu (id, type, selected index of
        count, focus, takes pad events, movement allowed +0x1A0, wraps, turned) - or, when no item is found, every
        element under the cursor (by its own box) with the nearest menu above it; then each event sent, or "NOT sent"
        with the reason (pad mapping inactive / menu input blocked / no device), "on its way" for a selection still
        moving and its outcome (selected / given up). Right clicks are logged too.
      - mouse_menus=0 unchanged (none of this runs).

      ## Tests
      `TestMouseItemShapes`: a stand-in theme menu laid out like the ACTIONS box (labels above their containers: each
      label's band selects its own item, across the menu's width; an alpha-0 sprite counts; a hidden sprite and its
      child, and an empty container far away, don't), a stand-in spin menu (1 x 1 vmenu turned -22.5 degrees, six 1 x 1
      items turned 0..112.5 degrees with 150 x 20 texts; the front text at alpha 0): points along each item's label, beside
      it, past its end, and inside the turned texts' surrounding rectangles but outside the texts; the disabled and blank
      items aren't found; a turned menu isn't widened; `ItemBounds` (theme item; a turned text's corners);
      `IsRotatingMenu`, `Rotated` (whole turns, epsilon), `BoxContains` (negative sizes, a quarter turn),
      `ItemPassedOver`. `TestMouseTargets`: frame by frame against a stand-in menu that ignores events for 9 frames
      after each move (the spin menu: 3 steps in 19 frames, 21 events; the short way round a wrapping 16), one that never
      moves (gives up at frame 47), one too slow for the frame limit (gives up at frame 301 at item 34), waiting while the
      menu moves by itself (no events), the menu destroyed, no selection; `StartTarget`. The batch 90 tests still pass
      (the hit-test layout helper now turns children by the parent's angle).
      Mutations (`tools/mutate.py`, 35 deliberate bugs in mouse_menus.cpp: box maths, angle field, Rotated, item parts,
      hidden/empty parts, turned hit vs bounds, widening, passed-over tags, rotating-menu test, every target branch):
      all 35 CAUGHT - 32 at once, 3 after strengthening (a named tag holding `blank`; "already there" / "no selection"
      count no frame). `passed_blank` and `visit_passed` are caught by a crash, not a failed check: a blank item then
      reaches ItemFocusable, whose ContainsTagFlag looks the unnamed `blank` up as a global (LookUpSymbol), which crashes
      offline (no symbol table) - the reason FindItemAt tests ItemPassedOver first.
      Not covered by tests (game side, needs the game): the pending state's bookkeeping in Update / AimAt / Click, the
      debug log, ClearPending on right click / wheel.

      ## What to try in the game
      1. Main menu: hover the wheel's items - nothing should turn. Click an item below or above the front one: the
         wheel turns to it step by step (as holding down / up) and then opens it. Click the front item's label (the
         roundbar): it opens at once. Click a dim filler line between groups: nothing. Wheel: turns it.
      2. Network: Multiplayer -> LAN (or Internet) -> the lobby: hover Host Game / Join Game / Refresh - the highlight
         must follow the label under the cursor (not the one above it); click Host Game: the host options open. Try the
         other theme_menu_add_item menus (game options, pause menu...): hover / click the labels.
      3. Earlier batch 90 checks (options rows' left/right ends, scrolling lists, dialogs, keyboard) still behave.
      4. If something still doesn't click: set `[features] debug_mouse=1` in thug2-decomp.ini, click it, and read the
         "mouse:" lines in thug2-decomp.log (what was under the cursor, the menu's state, what was sent or why not).
      Turn the mouse off with `[features] mouse_menus=0`.
      Checks: thug2_check 0 MISMATCHED, all 167 early self-tests pass; check_code_writes "No overlaps"; call audit no
      MISMATCH; register scan only the known 0x004EFF92 false alarm. No game function added or replaced (our own feature).
