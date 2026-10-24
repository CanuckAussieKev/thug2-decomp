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

## Part 2: the level select's globe, arrows and everything else the first pass skipped (2026-10-24, THUG2-3)

The user: "the spinning globe in the level select, along with the arrows, etc, I think may not have been upscaled."

### What the level select is drawn from (checked in the log's `data overlay:` lines and the scripts)

- The globe is not a 3D model: `level_select_globe_slice` (gamemenu_levelselect.qb) lays out 22 sprites `map0`..`map21`
  (64x64, 16 colours, `LevelSelectSprites`, from Levelselect.prx) in strips and slides them; `level_dot` / `level_arrow`
  (the big white arrow) / `level_highlight` are sprites too. The up/down/left/right arrows are `PanelSprites/SkateSprites/
  left_arrow, right_arrow` and the sign strip's `sign_*`. All of these were in the first pass (the log: every
  `images\levelselectsprites\*` and `images\panelsprites\*` file of a level-select run is read from hd_ui; 426 images
  served, none from outside it). The only one not upscaled is `map0` (one solid ocean colour).
- So the globe WAS upscaled, but the effect is modest: the art is 16-colour 64 px tiles, so the model sharpens the
  continents' edges and the sprites' outlines, it can't add detail (before / after zoom below). What the user sees as
  "not upscaled" is likely that, plus the parts that really were skipped (below).
- What was really skipped (survey of what menus draw from in THUG2-3's Data\pre / loose Data):

| group | files | MB | menu-visible? | now |
|---|---|---|---|---|
| loading screens `Data\images\loadscrn*.img.xbx` (640x448 32-bit) | 64 (+64 `fr\`, +64 `gr\` localised) | 70 (+65 +65) | yes | 64 done (x2: 1280x896) |
| menu 3D models' .tex.xbx: UI_bg, mainmenu_bg, options_clouds, logo, gradient_bg, cat_bg, menu3d (+16 per-level signs: the pause menu's sign, graffiti) | 24 dictionaries, 173 textures | 0.3 | yes | 155 of 173 textures done (18 smaller than 8 or one colour) |
| the original front end's scene `levels\mainmenu\mainmenu.tex.xbx` (mainmenuscn.prx; shown only with `menu_backdrop=0`) | 1 | 0.9 | with menu_backdrop=0 | done (in the 24 / 155) |
| Create-A-Graphic pieces `cagpieces.prx` / `CAGR` (663), `cagpremade` (50) | 713 | 2.9 | yes (CAG editor) | NOT: the editor edits them texel by texel; code takes the sTexture's sizes as the D3D texture's |
| Bits.prx / `images\Bits` (208 img + 39 tex: particles, shatter debris, snow, sun, `cag_workspace`) | 247 | 0.7 | no (in-game) | not |
| PermTex.prx (34: skater scuffs / scars, car light glows, CAGR base textures) | 34 | 0.1 | no | not |
| `images\Tags` (76 CAS stickers), `faces` (20) | 96 | 1.6 | CAS only | not (faces / stickers are edited by code) |
| skater / ped / vehicle / object model textures (skaterparts*.prx 309+75+77 tex, Models\*: Skater_Male, Peds, Veh, ...) | ~900 | ~20 | CAS / select-skater shows skaters | not (3D, not UI; could use the level converters' pass) |
| mainmenu_scripts.prx's 5 particle images | 5 | 0 | no | not |

### What was done

- `tools/hd_textures/ui.ts`: `upscaleUi` also takes `.tex.xbx` (each texture of a dictionary through the same pass under
  a number of its own - checksums repeat across files -, copied back, the dictionary written with the level converters'
  `writeTex`; clamped when padding, the clouds wrapped) and loose loading screens (the `.img.xbx` path - the picture is
  drawn full screen from UV 0..1 whatever its size - checked: 0x4B78B0 loads it through `CTexture::Load` 0x4C7F00 ->
  LoadTextureFile 0x4E0790, so the image marker and ApplyHdScale already apply, and 0x4B6CD0 draws UV 0..1). The 2048
  cap makes 640x448 x2, not x4 (a 4x one would be 2560x1792 = 18 MB of texture each). `gameUiFiles(game, {loadingScreens,
  models})` collects them; `install` reports them. No C++ change: `hd_ui\` serves any name (the overlay consults it for
  `models\...tex.xbx` and `levels\...` exactly as for images), model textures need no marker (normalised UVs).
- `make_menu_level.ts --hd-ui 4` now makes all of this (569 images incl. 64 loading screens + 8 fonts + 24 model
  dictionaries; 472 MB in hd_ui, of which loading screens 280 MB; cold run ~80 s with the cache from part 1, 54 s warm).
  No edit to make_menu_level.ts itself this round (its `hdUi.install` call is unchanged).
- `tools/game/window_shot.ps1`: `THUG2_SHOT_FULL=1` saves the full-size picture (default still half size);
  `tools/game/crop_compare.ts <before> <after> x,y,w,h out.png [--zoom N]`: the same rectangle of two pictures side by side.
- ui.test.ts: tests for a model dictionary (kept checksums, sizes, MENU_MODELS names) and a loading screen (x2 + marker).

### Checks (workspace THUG2-3, hd_ui=0 vs 1, the same DLL: no C++ changed, so no rebuild / thug2_check needed)

- Level select (`out\shots\lsa_hd0-1.png` before, `lsb_hd1-1.png` after, 1280x720 full size; `globe_zoom.png`,
  `arrows_zoom.png`: the same rectangle before | after, x2 / x3): same size and place; the globe's continents and the
  big arrow smooth instead of stair-stepped, the left / right arrows and sign strip crisp.
- Pause menu (`pause_pair.png`: hd_ui=0 | 1): the menu3d level sign (graffiti) sharper, same size.
- Loading screen (`lg_hd0_28.png` | `lg_hd1_27.png`, `loadscrn_zoom.png`): the same part of the picture at the same size,
  sharper (note: in these workspace captures the loading screen shows magnified 2.2x - that is how it is drawn with
  hd_ui=0 too, not ours; window capture of a 1280x720 window).
- The log has no errors; `data overlay:` shows `hd_ui\images\loadscrn*.img.xbx`, `hd_ui\models\ui_bg\ui_bg.tex.xbx`,
  `hd_ui\levels\mainmenu\mainmenu.tex.xbx`, `models\menu3d\menu3d.tex.xbx` ... served.
- typecheck / lint / tests (ui.test.ts 5 pass).
- THUG2-3 left with hd_ui=1, its DLL as it was, the overlay made with --hd-ui 4.

### Left

- [ ] Create-A-Graphic pieces and faces / stickers (need the code that edits them to use the real texture size).
- [ ] Localised loading screens (`images\fr`, `images\gr`: 128 files, +130 MB at x2) - add if a French / German game matters.
- [ ] Skater / ped model textures (CAS, select skater): level-converter pass, not UI.
- [ ] A cheaper globe: the 22 map tiles are 16-colour art; if it should look better than "sharp edges", the source art would
  need redrawing (an upscaler can't add the detail).

## Part 3: loading screens at x4, DXT1 (2026-10-25, THUG2-3)

The user: "AI upscale the loading screens to 4x and compress." NOT merged; DLL rebuild needed (below).

- Size cap: the loading screens' pass has its own cap, 4096 (`ui.ts loadingOptions`), so 640x448 -> 2560x1792. D3D9 cards take
  4096+; the screens are not powers of two, which the loader already handled at x2 (header "plain": no swizzling; the
  game's own 640x448 are the same, NPOT textures work on the device).
- Compression: opaque loading screens (all 64) are written as DXT1 (`encodeImg(..., asDxt1)` through tools/level_port/dxt.ts):
  header format = `HdUi::kFormatDxt1` ("DXT1", hd_ui.h), the marker x4 as before, the file is the header + one level of
  blocks, not swizzled, rows bottom-up like the 32-bit plain images (the sprite samples v flipped). Other images / fonts / model
  textures stay uncompressed 32-bit (text and edges show DXT artefacts). A screen with alpha, or sides not a multiple of 4,
  would stay 32-bit.
- C++ (NOT ORIGINAL, small, marked): `texture_file.cpp` LoadTextureFile / LoadTextureMemory: if the header's format is
  kFormatDxt1 and the image carries the HD marker, the Direct3D texture is created D3DFMT_DXT1 (one level) and the blocks are
  read straight into it (`IsHdDxt`, `HdDxtSize`); then the usual Finish / ApplyHdScale (sizes divided by the scale, so the picture
  is still drawn 640x448-logical, UV 0..1). Files without the marker or with the original formats take the original path.
- Mips: none. The screens are drawn full screen at about 1:1 of the 640x448 picture -> at 1280x720 the texture is minified
  exactly 2:1 (bilinear then averages a 2x2 block = a box filter), at 1080p 1.33:1; mips would add 33% memory for nothing.
- Model: `realesrgan-x4plus` for loading screens (not the UI's anime one). Compared on airport / australia / 2player at x4
  (out\lscmp, not committed): the anime model smooths photographic texture (airport's concrete / roof turned into smeared
  plastic patches); x4plus keeps grain and texture (brick, grass, torn paper, paint), at the price of slightly rougher edges
  on flat lettering. They are photo / art collages, so x4plus. `--hd-ui-loading-model M` (make_menu_level) /
  `--loading-model M` (ui.ts) changes it.
- Sizes: loading screens 280 MB (x2, 32-bit, 4.4 MB texture each) -> 140 MB (x4 DXT1, 2.19 MB each, 2560x1792 in video
  memory 2.19 MB); hd_ui folder 472.7 MB -> 332.4 MB (the rest unchanged); an x4 32-bit one would have been 18.4 MB each.
  make_menu_level --hd-ui 4 with warm cache for the rest: 3 min 10 s (61 images made).
- Game check (THUG2-3, hd_ui=1, this build's DLL, lock released and DLL restored): `-Level foun`: the log has `data overlay:
  images\loadscrn_generic read from hd_ui\...`, `hd ui: 1 upscaled image(s) loaded (this one x4, 2560x1792 texels; drawn at
  640x448)`, no errors; the loading screen is up 4.4 s (DXT x4) vs 4.2 s (x2, 32-bit) - no difference in load time. Pictures
  (out\shots in the worktree, not committed): `loadscreen_x4dxt_full.png` (full size capture), `crop_x2_vs_x4dxt.png` and
  `crop2_x2_vs_x4dxt.png` (the same rectangle before | after, x3): same size and place; x4plus + 2560 shows finer grain and no
  banding in the flat yellow; the window is 1280x720, so the gain over x2 is modest at that size - a 1920x1080+ screen shows more.
- Checks: typecheck / lint / tests (395) pass; ui.test.ts has the DXT1 screen (size, format, row order via dxt.decode), the
  32-bit fallback and the model default; build (verify, own PDB endpoint) clean; thug2_check on THUG2PM.exe and the stock
  THUG2.exe: 0 MISMATCHED, 818 early self-tests pass, crash handling 0 FAILED; check_code_writes: No overlaps.
- Install: the DLL must be rebuilt (the old DLL would read the "DXT1" format as a bit depth and break); then
  `node tools/menu_level/make_menu_level.ts --game C:\Games\THUG2 --hd-ui 4` (the overlay's hd_ui loading screens are replaced).
