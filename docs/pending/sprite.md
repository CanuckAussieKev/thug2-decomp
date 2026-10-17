# Sprite draw and the widescreen front end's edges (agent "sprite", branch wt/sprite) - NOT run in the game yet

- [x] Sprite draw (NOT run in the game yet): **Nx::SSprite::Draw 0x004DFCF0** (src/game/gfx/sprite.*). SSprite is the
      2D sprite (vtable 0x0064AD28: [0] destructor 0x4DFCC0, [1] BeginDraw 0x4DF990, [2] Draw, [3] EndDraw 0x4DFA40),
      built on the same priority-ordered drawable base as SText (vtable 0x0064AD18). thiscall, no arguments, plain
      RET, no return value; only reached through the vtable. Fields: +0x14 texture (+4/+6 the texture's size, +8/+0xA
      the image's, 16-bit), +0x18/+0x1C position, +0x20/+0x22 size, +0x24/+0x28 scale, +0x2C/+0x30 hot spot, +0x34
      rotation (radians), +0x38 RGBA, +0x3C flipV, +0x3D flipU. Draws one indexed quad (DrawIndexedPrimitiveUP, vtable
      +0x150; indices 0x67EFA8 = 0 1 2 1 3 2; FVF 0x144 through 0x4D9DB0; stage 0 colour/alpha MODULATE2X); counters
      0x786EBC / 0x786EC0; skipped with 0x7D1EC0. Findings / quirks kept: scale 0 draws nothing, negative scale
      mirrors (u or v swapped); the flips swap the coordinates but pass one through a 16-bit integer (_ftol: values
      between 0 and 1 become 0); alpha 0 draws nothing; upright sprites: as wide as the layout or wider (x0 <= 0 and
      x1 >= 640) -> the whole back buffer's width, the same for heights (<= 0, >= 480); otherwise mapped through
      0x786D80/88 (x) and 0x786D84/8C (y) - but a y edge exactly at 0 isn't mapped (stays 0, so it ignores
      PartyMod's 36-pixel offset at 1080p); the bottom edge is kept at double precision until stored. Rotated
      sprites: corners rotated by 0x4882D0 (a Vector RotateZ, thiscall, RET 4, x87 fsin/fcos - not a function in
      Ghidra's database, so audit_game_function_calls shows "?"; not rewritten), mapped and truncated to whole
      pixels, no full-screen case.
- [x] Widescreen front end edges (not in the original; only when Nx::FrontEndWidescreen()): after the original's
      mapping, an upright sprite whose left edge in layout space is <= 0 is drawn from screen x 0, one whose right
      edge is >= 640 to the back buffer's width (each edge on its own; y unchanged). **Beyond the brief: rotated
      sprites too when the rotation leaves them upright or on their side** (each side's two rotated corners share an
      x within 0.01 layout units; per corner then: layout x <= 0 -> 0, >= 640 -> width). Reason: the menus' bottom
      band is 10 strips with `rot_angle = 180` (build_top_and_bottom_blocks, scripts/game/menu/thememenu.qb, in
      qb_scripts.prx), so an "unrotated only" rule would have left it ending at x 67 / 1780 as in screenshot 1. A
      sprite turned by any other angle is unchanged (e.g. pause_menu_gradient's 179.9 - only in levels anyway).
      Side effect to know: a front-end sprite sliding in from off the layout (fully left of 0) gets its left edge
      pinned at screen 0 - stretched between 0 and its mapped right edge while it moves.
- Text (SText::Draw 0x4CFD00) and the loading bar: not changed. Text stays inside the layout (menus centre it);
  the loading bar's positions are computed from the screen width directly. Only the text and the sprite draw read
  0x786D88.
- Tests (src/hook/selftests_sprite.cpp): TestSpriteDraw - original vs ours, widescreen_menus off, 1792 sprites (7
  places incl. exactly 0 / y1 exactly 0 / full-screen, 8 shapes incl. mirrored, scale 0, exactly the layout's size and bigger,
  2 hot spots, 4 rotations: 0, 0.3, 180 and -90 degrees, flips, alpha 0, textures none / part / whole, rendering
  skipped) x 4 back buffers / layout conversions; every device call and vertex recorded. TestSpriteWidescreenEdges -
  worked-out corner x values on 1920x1080 (scale 2.25, offset 240/36): across / at the left edge, across the right
  edge, inside, setting off, in a level, turned 180 and -90 degrees (truncated pixels), and turned 0.3 rad
  (identical to the setting off). 48 deliberate bugs (39 in the draw, 9 in the rule): all caught, 3 after
  strengthening (x1 exactly 640 for the full-width case; an upright and a 180-degree sprite ending exactly at 640).
- Network menu (screenshot 1, "the menu is offset a bit too"): the backdrop panel (dark purple with the city,
  x 240..1697 at 1080p = layout ~0..648) is **not a sprite**: make_server_list_menu (levels/mainmenu/mainmenu_net.qb
  in mainmenu_scripts.prx) calls make_mainmenu_3d_plane, an **Element3d** screen element (model "UI_bg\UI_bg.mdl",
  pos (320, 225), scale (2.1, 2.2, 2.1), CameraZ -500) drawn through the 3D path, so the sprite rule doesn't touch
  it. In PartyMod's pillarboxed front end its right ~8 layout units (1680..1697) were cropped by the 4:3 viewport;
  now it shows whole, ending 17 px past the 4:3 area on the right while its left end sits exactly at the 4:3
  area's left edge (x 240) - which is what looks "offset". Not worked out: why its left end is exactly at 240 (a
  viewport/clip at the layout's left edge, or the model's own edge) and how Element3d projects its model with the
  wide 3D view. That needs the Element3d draw code (not decompiled). With this branch the bands, the top/bottom
  streak lines and the globe change; the backdrop panel doesn't.
- What to look at in the game (1920x1080 or any wide window, widescreen_menus=1): the main menu and the network
  "ACTIONS" menu - the top band (strips at layout x 0..) should now start at the screen's left edge, the bottom
  band (180-degree strips) should reach both screen edges, the dark streak lines under/over the bands likewise; the
  menu text, highlight bars and boxes should be exactly where they were. widescreen_menus=0: identical to before
  (PartyMod's pillarbox). In a level: HUD sprites unchanged. Disable with `[replacements] disable=sprite.cpp`.
