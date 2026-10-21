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
- [ ] Next: the pass in the other converters (THPS2 / THPS4 / THUG1 / THAW); maybe drop isolated cut-out texels
  before upscaling; a per-texture opt-out list for textures the model spoils.
