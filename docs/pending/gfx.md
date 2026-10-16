- [x] PartyMod gfx, part 1 (NOT run in the game yet; branch pm/gfx): the screen's shape and blur, with
  PartyMod's replacements built in. New src/game/gfx/screen.cpp (namespace Nx), all `REPLACE_SOURCE_ONLY`
  because PartyMod's jumps or byte writes sit on their first bytes:
  - 0x004A0780 SetScreenAspectRatio: PartyMod's `setAspectRatio` (stores the aspect it wants - 4:3 in the front
    end, the window's shape in a level - and ignores the argument). The original stored the argument in
    0x00701340 (kept as a comment).
  - 0x004A07C0 GetScreenAngleFactor: PartyMod's `getScreenAngleFactor` (aspect / (4/3) for screens wider than
    4:3, unless the constant 0x0067E544 isn't 1; also updates 0x00701340). The original returned that constant.
  - 0x004B2330 SetScreenBlur: **depends on PartyMod's `DisableBlur` setting** (on by default): on, the blur is
    forced to 0 (`setBlurWrapper`); off, the original (store the amount in 0x00787328 if 0x00786D40, the device
    can blur). gfx.c now records the setting in `blur_disabled` (a THUG2_DECOMP-marked edit).
  - 0x004B2440 SetLetterbox: PartyMod's `setLetterbox` (fits the 640x480 layout to the aspect it wants, pillar-
    or letterboxed). The original's squash to 3/4 height is given as a comment.
  - 0x0048C170 CFunc_SetScreenBlur: the original (unnamed integer -> 0x006FA72C and SetScreenBlur). PartyMod's
    `restoreBlur` (DisableBlur off) writes this function's first five bytes with the bytes already there in
    THUG2PM.exe (checked: 51 8B 4C 24 08) - so no change in behaviour, but it would wipe a hook's jump, hence
    source only. Note its SetScreenBlur call reaches PartyMod's blur override when DisableBlur is on.
  - All nine patch addresses are in tools/check_partymod_overlap.py's HANDLED list.
  - Tests (src/hook/selftests_gfx.cpp, Early): TestGfxScreen - the originals as PartyMod leaves them (its jumps
    written over their starts for the test) vs ours over front end / level / no skater (stand-in skater and
    career), four window and back-buffer shapes (wider and narrower than wanted), the angle-factor constant 1
    and not 1, letterbox on/off from both previous states; SetScreenBlur with DisableBlur on (patched) and off
    (the unpatched original), the device able to blur or not, four amounts. TestGfxScriptSetScreenBlur - the
    script command over stand-in parameter structures (the amount, none, only a named integer, an unnamed
    float), both settings, device able or not: return value, 0x006FA72C and 0x00787328. 11 deliberate bugs
    caught (each by the test meant to catch it). Not tested: the in-game path where PartyMod's real patches are
    applied (same functions, so only the setting plumbing differs).
  - Left undone (budget): the five large rendering functions PartyMod patches inside. They're hookable
    (`REPLACE`) and their disassembly is read; notes for whoever picks them up:
    - 0x004B2D80 (the per-frame world render: per viewport, viewport rect, set_camera, scene passes, sky
      pass with fog 20/21, then 2D): PartyMod NOPs both letterbox viewport adjustments (0x004B2E65, 58 bytes;
      0x004B33CA, 35 bytes) and calls `setDisplayRegion` there (still behind the `if (letterbox active)` test),
      and calls `draw2DWrapper` instead of 0x004DFB90. ~30 callees, all cdecl except 0x4A0D20, 0x485B10,
      0x485A00/10, 0x4D6530, 0x48E370, 0x49AB50 (RET 4), 0x496640, 0x4A10A0 (thiscall). Passes a dword with
      garbage upper bytes to 0x490DE0 (only AL matters there - check 0x4B8E40).
    - 0x004DF390 (the blur pass): PartyMod turns the JZ at 0x004DF63A into JMP - the letterbox adjustment of
      the blur quad never runs. Mind the precisions: v1.u = (float)(1 - unrounded t/W) but 1 - tH uses the
      float-rounded tH.
    - 0x004DB200 (set_camera(matrix, position, screen angle, aspect, scale, render at infinity), 6 cdecl
      args): PartyMod's far plane default is 96000 (patch at 0x004DB35D, original 32000). Uses FPTAN/FPATAN
      and D3DX through dispatch pointers (0x5C036B, 0x5BFA8C, 0x5C0509).
    - 0x004B9030 (screen flash quad): the DrawPrimitiveUP call at 0x004B91E7 goes through PartyMod's
      `reorder_flash_vertices` (rotates the first three vertices); u/v are uninitialised without a texture.
    - 0x0044FA80 (movie frame quad): `movie_setTexture` at 0x0044FB8C, `movie_drawPrim` (stdcall) at
      0x0044FC88 + NOP; also sprintf's an unused "e:\movietest\movie%04d.bmp" with counter 0x00696D9C.
    Testing them offline needs a stand-in D3D device (a vtable of recording stubs) and detours over every
    callee; all five are plain D3D state/draw sequences, so that's feasible.
