# HD textures - optional AI upscaling pass for converted levels

Branches worktree-agent-a8ec50daa5baab075 / -a6516fa1a9a16ac7c (stopped by accident), finished in
worktree-agent-a4da161d87af84c60. Tools only (no game code): tools/level_port/hd_textures.ts, dxt.ts,
hd_textures.test.ts, tools/hd_textures/{fetch,compare_tex,compare_shots}.ts + README.md, tools/thps3_levels
(`--hd-textures [2|4]`, `--hd-max/min/model/cache/cache-clear/no-match/tool/gpu`), THIRD_PARTY_NOTICES.md (Real-ESRGAN).

- [x] HD-1 (tested in the workspace THUG2-3, not the main install): `--hd-textures` in the THPS3 converter -
  Real-ESRGAN realesrgan-ncnn-vulkan-20220424 (fetched into out\, never committed); off by default; colour and alpha
  separately (cut-outs: bilinear + threshold, sharp); 16-texel wrap / clamp padding; size cap; back-projection to the
  original's colours; format kept (32-bit / DXT1 / DXT5) with full mips; persistent cache in
  %LOCALAPPDATA%\thug2-decomp\hd-textures keyed by texel hash + every output setting, hits / misses reported,
  `--hd-cache-clear`.
  - Foundry + Canada: 416 textures, 16.4 MB -> 65.8 MB (x2) / 263.3 MB (x4, cap 1024); cold 54-62 s (upscaler 44-49 s),
    cached 9 s. Peak working set 160 MB (original) -> ~200 MB (x2) -> 304-372 MB (x4); GPU dedicated 45-52 -> 77-92 ->
    121-194 MB. Both levels load and run at x4, logs clean; load time within noise.
  - Comparisons (worktree out\hd_compare, not committed): <level>_view<n>_orig_x2_x4.png, _orig_x4.png, zooms
    foun_view1_wall_zoom, foun_view3_floor_zoom, can_view1_fence_zoom, can_view2_cloud_cutout_zoom.
  - Cut-out alpha checked texel by texel: the upscaled alpha equals the bilinear-threshold expectation exactly.
  - Look issues: invented detail (Canada's brushed-metal quarter pipe becomes a woven pattern, concrete gets grain);
    isolated single opaque texels of a cut-out (Canada's cloud billboard) show as specks a bit more than before.
  - Pitfall: the game reads ported levels from the data overlay - run make_menu_level after every convert.
- [x] THPS2 done: see the THPS2 section below.
- [ ] Next: the pass in the other converters (THPS2 / THPS4 / THUG1 / THAW); maybe drop isolated cut-out texels
  before upscaling; a per-texture opt-out list for textures the model spoils.

## THPS2 (branch worktree-agent-a607ee04b116f0ea1; tested in workspace THUG2-4, not the main install)

- [x] HD-2: `--hd-textures [2|4]` and the same `--hd-*` options in `tools/thps2_levels` (convert; off by default; the
  pass is `level_port/hd_textures.ts` itself - `optionsFromCli` / `fillHdTexturesArg` added there for parseCli-based
  converters). Addressing: a texture some face's uvs reach past (the converter's `tiled` set, as its materials' wrap)
  is padded wrapped on both axes, every other one clamped; cut-outs keep their PlayStation alpha (0 / 255 ->
  bilinear + threshold, sharp). The textures are already 32-bit (palettes expanded, newtex BMPs where there are
  any = twice the PlayStation size), so nothing is skipped but one-colour ones. Sky scenes are upscaled too.
  Test: thps2_levels.test.ts (wiring, wrap vs clamp padding with a stand-in model, option parsing).
- Model: realesrgan-x4plus-anime for THPS2. Hangar's 12 largest textures side by side (out\hd_compare\thps2\
  textures_hangar_{x4plus,anime}_sheet.png): both are sharper; x4plus invents rust / grain on soft painted art (the
  corrugated wall) and bold outlines on murals, anime keeps the flat painted look and the stencilled lettering clean
  - THPS2's art is hand-painted, soft and already upscaled once (2x BMPs), so less invention is better.
- All ten levels at x4 (cap 1024): 1439 textures, 1437 upscaled (2 one-colour), texture data 85.4 MB -> 1367.5 MB
  (Hangar 6.6 -> 105.5 MB); convert cold 161 s (upscaler 81 s), cached 38 s; make_menu_level 11 s. Game, Hangar:
  process private 203 MB (peak 217) -> 363 MB, load about the same; log clean. One view before / after:
  out\hd_compare\thps2\hangar_orig_vs_x4.png (+ _zoom). The difference at 640x360 is mild; fine detail only shows
  close up.
- Install: `node tools/thps2_levels/thps2_levels.ts convert --thug2 C:\Games\THUG2\Game --hd-textures 4
  --hd-model realesrgan-x4plus-anime --hd-tool C:\Dev\thug2-decomp\out\hd_textures\realesrgan-ncnn-vulkan-20220424\realesrgan-ncnn-vulkan.exe`
  then `node tools/menu_level/make_menu_level.ts --game C:\Games\THUG2\Game`.

## DXT compression of upscaled textures (branch hd-compress; tested in workspace THUG2-4, not the main install)

- [x] HD-3: `--hd-textures` now writes upscaled textures that were 32-bit as DXT1 (opaque), DXT1 with 1-bit alpha
  (cut-out: .tex.xbx DXT 2; the loader makes both D3DFMT_DXT1, the alpha test still sees 0 / 255 - checked texel by
  texel in tests and on Foundry / Hangar textures) or DXT5 (soft alpha), full mips. On by default; `--hd-no-compress`
  (THPS3 and THPS2 converters) keeps 32-bit. Stays 32-bit when a side of the upscaled size isn't a multiple of 4. A
  texture that already was DXT keeps its format (re-encoded with the better encoder). The HD menu UI never compresses
  (`uiOptions`: compress false). The cache holds the 32-bit result, so compression is not in the cache key (a new
  encoder needs no rebuild; the old caches stay warm). `HdStats.compressed` is in the report line.
- Encoder (dxt.ts): per block the principal axis (power iteration) for the first endpoints, then up to 4 rounds of
  least squares refit to the assigned indices (kept while the error falls); DXT1 3-colour mode for blocks with
  transparent texels; DXT5 alpha picks the 8-value or the 6-value + 0 / 255 ramp by error. Deterministic; 1024x1024
  in ~0.12 s.
- compare_tex.ts: `--per-kind N --height H` (N largest of each alpha kind + mean error of the visible texels).
- Measured, THPS2 all ten at 4x (anime): texture data 85.4 MB -> 170.9 MB (was 1367.5 MB uncompressed; 8x smaller);
  HD pass 74 s with everything cached (was 38 s: the encoding is the difference), make_menu_level 10 s.
  THPS3 all 14 at 4x: 97.7 MB -> 241.2 MB (was ~1.56 GB), pass 99.5 s cached.
- Game (THUG2-4, lead's dinput8.dll, selftests off), process private peak: Hangar 373 MB uncompressed x4 -> 219 MB
  DXT (original 203 MB); Foundry 401 MB -> 232 MB. Working set peak 330 -> 176 and 358 -> 190 MB. Logs clean.
- Quality: mean error of visible texels, DXT vs the 32-bit x4: 1-4 of 255 levels (opaque 1.2-3.7, cut-out 1.4-2.9,
  soft 1.2-1.7; soft alpha 0.1-0.4); cut-out alpha identical texel for texel. In-game pairs look the same.
  Known DXT1 limit: a cut-out's transparent texel is black, so bilinear filtering at a cut-out's edge can darken it
  by a fraction of a texel (at x4 not noticeable).
- Pictures (worktree out\hd_compare\compress\, not committed): hangar_x4raw_vs_dxt(.png, _zoom.png),
  foundry_x4raw_vs_dxt.png, tex_foun\sheet.png and tex_hangar\sheet.png (left 32-bit x4, right DXT; 3 opaque /
  3 cut-out / 3 soft alpha).
- Note: converting a single level overwrites the install's `thug2-decomp-thps2/3` with that one level (the workspace
  has Hangar / Foundry only now).
