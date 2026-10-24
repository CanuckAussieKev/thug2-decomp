# HD menu UI (experiment) and THPS3 at x4

Branch worktree-agent-a50da320bc50ca388. The user (2026-10-24): "upscale all of the UI elements used in menus. Make
it toggleable at build time, I just want to see how it would look ... maybe 4x? You can also redo THPS3 textures with
4x as well, for a test."

## What was done

- Tools: `tools/hd_textures/ui.ts` (+ `ui.test.ts`): the menus' `.img.xbx` sprites and `.fnt.xbx` glyph pages through
  the existing HD pass (tools/level_port/hd_textures.ts: Real-ESRGAN, cache, back-projection), model
  `realesrgan-x4plus-anime`, clamped padding, cap 2048. `make_menu_level.ts --hd-ui 2|4 [--hd-ui-model M]` (default
  off) writes them into the overlay last (so the overlay's own level select signs are upscaled too).
- DLL: CMake option `THUG2_HD_UI` (default OFF; off = the code compiled out). `src/game/gfx/hd_ui.h/.cpp`;
  NOT ORIGINAL blocks in `texture_file.cpp` (LoadTextureFile / LoadTextureMemory 0x4E0790 / 0x4E0A90) and `text.cpp`
  (SFont::Load 0x4CF0F0). Self-test `TestHdUiMarkers` (src/hook/selftests_hd_ui.cpp).
- Shared files touched (small, additive): CMakeLists.txt (option, 2 sources), tools/menu_level/make_menu_level.ts
  (flag; "images", "fonts" in its own-folders list), tools/hd_textures/README.md.

## How the on-screen size is kept

A sprite element's size is its texture's size: CSpriteElement::SetTexture (0x4AF130) takes the platform texture's
width / height (vtable thunks 0x49F3C0 / 0x49F3D0 -> 0x4C7FB0 / 0x4C7FE0), which read the sTexture's image size
(+8 / +0xA). SSprite::Draw's texture coordinates are ratios (image size / texture size). A glyph's size is its
rectangle in the font file; its texture coordinates are the rectangle / the page size. So:
- `.img.xbx`: header +0x14 (0 in all 1730 of the game's images) = 0x48440000 | scale; the file is 32-bit (format 0;
  the game makes every image A8R8G8B8 anyway), all sizes x scale, picture at the same corner, the rest the edge
  repeated. After loading, the DLL divides the sTexture's four sizes by the scale: the D3D texture stays full size.
- `.fnt.xbx`: glyph page header +8 = "HDUI", +0xC = scale, page x scale as 32-bit BGRA (the doubled PS2 alpha
  applied), no palette; glyph rectangles / height / base unchanged. The DLL reads the texels as they are and makes the
  texture coordinates from page size / scale.
Without THUG2_HD_UI the markers are ignored (upscaled files would show 2-4x as large): both toggles go together.

- The upscaled images' read buffer (LoadTextureFile / LoadTextureMemory, up to 16 MB) and the HD fonts' texel buffer
  come from the C runtime's heap (malloc), not the game's: the first try used Mem::New and the game died on its first
  HD image (white3, 128x512: Mem::New returned null while the front end loaded its sprites, File::Read into null ->
  FatalFileError -> crash in ShowDiscError's text). With the option off the game's own paths are unchanged.

## Results (workspace THUG2-3, 2026-10-24; tested in the game there, not the main install)

- [x] HD-UI-1: `--hd-ui 4`: 502 of 505 images (437 from the six UI archives + 68 of the overlay's own level select
  signs) and all 8 fonts upscaled; 11 left as they were (smaller than 8 or one colour - drawn as before, unmarked).
  Files 2.8 MB -> 172.6 MB. Texture memory if everything were loaded at once: 9.8 MB -> ~157 MB (mainmenusprites
  3.5, multiplayersprites 2.0, panelsprites 1.9, level select 1.0, fonts 1.6 MB before; x16 each). Cold run 27 s
  (upscaler 21 s), cached 5 s; make_menu_level with it ~60 s instead of ~12 s.
  - In the game (THUG2-3, ON build): the log's `hd ui:` lines - 6 fonts and 200+ images loaded in the front end; the
    data overlay serves them from the prx packs' names. Main menu, Game Options, level select, Foundry's HUD and the
    pause menu look the same size and place as before, sharper (text edges, the level select's sign strip, the
    roundbar). Nothing misaligned seen in those five screens; no errors in the log; self-tests pass.
  - Memory (Foundry in free skate, the log's `memory in foun` line): game heaps unchanged (the HD buffers are
    malloc'd); process private: original 212 MB (peak 217) -> HD UI 258 MB (peak 342, the front end's sprites) ->
    HD UI + THPS3 x4 439 MB (peak 439). Address space free 1430 -> 1311 -> 1195 MB.
  - Load time: main menu ready / Foundry up 67 s (original) vs 67-77 s (HD) from start - within the noise of a
    machine busy with other builds.
  - Comparisons (worktree out\hd_ui_compare, not committed): <screen>_orig.png / _hd4.png / _orig_vs_hd4.png for
    main, options, levelselect, pause, thps3; zooms options_zoom.png, levelselect_zoom.png, thps3_zoom.png.
  - Look: the anime model keeps flat colours and outlines clean; a few single light specks inside some letters of the
    dialog font's page (back-projection); not seen at normal size.
- [x] THPS3-X4: `thps3_levels.ts convert --hd-textures 4` (all 14 levels, into THUG2-3 only): 3178 of 3193 textures
  upscaled, texture data 97.7 MB -> 1556.5 MB, 236 s (upscaler 145 s; 549 from the cache). Foundry loads and runs
  (thps3_* screenshots: before = its original textures swapped into the overlay for that run).
- Checks: npm typecheck / lint / test (356) pass; the build ON and OFF without warnings; thug2_check on THUG2PM.exe and
  the stock THUG2.exe, ON and OFF: 0 MISMATCHED, all 816 early self-tests pass, crash handling 0 FAILED.

## Run-time switch (2026-10-24, the user: "merge into main, set it to on in ini, off by default")

The CMake option is gone: the code is always built. `thug2-decomp.ini [features] hd_ui=` (default 0) switches it.
`make_menu_level.ts --hd-ui N` writes the upscaled files into `thug2-decomp-data\hd_ui\<same relative names>` (the
folder is not made / is removed without --hd-ui); `Front::DataOverlay::OverlayPath` consults it before everything else
only while hd_ui=1 (also for the names inside .prx packs, which the overlay serves by name), so with 0 nothing HD is ever
read. The marker code in texture_file.cpp / text.cpp is unconditional (the game's own files carry no marker).

## Switching it on / off (old text, build-time; superseded by the above)

- On: `out\build\hdui_ON\dinput8.dll` (cmake -DTHUG2_HD_UI=ON) in the game folder +
  `node tools/menu_level/make_menu_level.ts --game C:\Games\THUG2-3 --hd-ui 4`.
- Off: a DLL built without the option (the default; e.g. the user's normal build) + make_menu_level without --hd-ui.
  Mixed: an OFF DLL with the HD files shows menus 4x too large; an ON DLL without them is the original.
- THPS3 back to its own textures: `thps3_levels.ts convert --thug2 C:\Games\THUG2-3` (no --hd-textures), then
  make_menu_level.

## Not done / next

- [ ] The loading screens (loose Data\images\loadscrn_*.img.xbx, 512x512 - 4x would be 16 MB of texture each), the
  front end's 3D backdrop (THPS4 skate shop .tex.xbx - the thps4 converter has no HD pass yet), CAG pieces, the
  level editor's ImGui (vector fonts already).
- [ ] x2 not tried in the game (`--hd-ui 2`, same code path).
