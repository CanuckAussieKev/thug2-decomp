# Volumetric light, round 4 - distinct shafts (agent volumetric4, branch worktree-agent-adb238c1e05b76e7b) - 2026-11-16

STATUS: WORK IN PROGRESS (WIP commit). Not merged-ready: model numbers still being tuned, docs/README not updated.

## Findings: why the shafts were a veil (round 3, main 970f54953)
Measured with the new `tools/game/shaft_contrast.ts` (pairs: the same pose with rt_fog_air=cinematic and
`console set rt_fog_air clear`; the difference = the cinematic street air's own light, luma 0-255).
Round-3 baseline (tag `_b`, captures C:\Games\THUG2-6\Screenshots\<pose>_b and <pose>_b_off; air pictures
out\captures\air_<pose>_b.png; sheet out\captures\peek_b2.png):
- dusk/dawn the cinematic air adds almost nothing: bo_dusk_toward beam (p90) 8.8 levels, veil (median) 2.9;
  be_dusk_beam_side 4.1 / 0.6; sch_tower_* 7-14 / ~0; night is the real veil: be_night_start veil 45.5, beam 89.9
  (structure 0.03 = an even glow), be_night_street 19.4 / 50.6.
- CONFIRMED causes:
  1. The volume round the eye (60 m) is too short: at a 6-12 degree sun the near air is in the buildings' shade (190 m
     long shadows); the beams that exist are further away, outside the volume -> nothing but the aerial haze's glow.
  2. The street air is too shallow (thins by e every 8 m): a low sun's beams pass over / between the roofs (20-30 m up)
     where it has all but gone.
  3. Night: the lamps' x5 glow in the mist lights it evenly (omni lamps, no fixture shadows) -> an even veil.
- Ruled out as main cause: the quarter resolution / temporal blur - the check's new bar case (a 0.3 m bar's shadow
  sheet seen edge-on in the beam, 1280x720 at 1/4) reproduces the exact integral's line within 0.3-0.8 texels, depth
  0.9-1.2 of exact. The 0.35 shade floor is a minor factor (lit:shade per unit ~40:1 already).
- Model check: shaft contrast per unit of air was never the issue; geometry (where the air is) was.

## Done (in the WIP commit)
1. `rt_volume_light.h`: AirArt `along` (volume stretched toward the light: range x (1 + (along-1) cos^2)), `deep`
   (x the layer's height by day; night mist stays low), `air_stretch`, `air_reach(d, art, cosine)`, `air_map_half`,
   `kAirMapLift`. Cinematic now: density 2.0 (was 2.5, offsets deep), shade 0.2 (was 0.35), along 4, deep 4.
2. `vulkan_rt_fog.cpp`: the air's sun map - step 0 compute pass (kAirMapBody: rays down the light's way over a square
   round the eye, R32F first-hit distance, cut-outs by alpha), binding 13, FogBlock +map_side/map_up/map_centre (512 B),
   `set_air_map`, `ensure_air_map`, `air_map_pipeline`; the march reads it at EVERY step inside the square
   (`map_light_seen`: bilinear compare, penumbra from the light's disc), traced pairs outside; steps: a ray leaving the
   volume marches `steps` in it (t^2) + steps/2 past it in a geometric series (cloud-gap scale); temporal pass
   variance-clipped (kFogHistorySpread 1.5). Settings: RtFogSettings::map, kRtFogQualityMap {512,1024,1536}
   (renderer_choice.cpp).
3. Checks: fog check processor march mirrors the new layout + map square; new bar case (run_bar) - passes at map >=
   500 texels (512 hit a geometric coincidence with the axis-aligned bar: line lost; 1024 is now Medium's). rt_lights
   check passes the light's direction for the stretch. TEMPORARY debug hooks still in the check (remove before final):
   BAR_DEBUG / BAR_MAP env vars in vulkan_rt_fog_check.cpp (run_bar profile print, run() early return).
4. Fog check (before deep/along tuning) 140/140 incl. perf at 2560x1440: Low 0.20-0.27, Medium 0.33-0.44, High
   1.5-2.1 ms. After deep: the air-model veil bounds failed at density 2.5 -> lowered to 2.0 (not yet re-run full).

## Game run n1 (tag _n1) - INVALID for comparison: the user's ini had changed to snow=1 meanwhile (falling snow in
every run). ws_ini.ps1 now forces snow=0. Still visible: sch_tower_high WHITES OUT toward the 12-degree sun (along 4 x
deep 4) - needs taming. Run n2 (snow off, + tower-shadow poses sch_tower_back / sch_tower_col) was in progress.

## Left (in order)
1. Measure n2 (`out\scripts\measure.ps1 -Tag n2`); tame the toward-sun whiteout (lower `along` to ~2.5, or density
   falloff with the stretch so optical depth toward the sun stays ~ the round volume's; maybe deep 3).
2. Night veil: lower the uniform lamp glow (lamp_glow) or concentrate it (lamps' light in the air only near each lamp)
   so cones/halos read; per-step lamp shadows inside the volume if affordable.
3. Full fog check, rt_lights, rt_sky, rt_sun, options_check, daylight_check, soak (frame_bench live_settings_soak),
   sim_check; x86 build (out\scripts\build86.cmd); npm typecheck/lint/test.
4. Remove BAR_DEBUG/BAR_MAP hooks; tighten the bar case asserts; README rows (rt_fog_quality now also the map side),
   docs/design/vulkan.md "Volumetric fog and light shafts" (new bullet "Distinct shafts", cinematic numbers changed),
   vulkan_rt_fog.h step 0 description.
5. Merge main (snow round 3 runs in parallel), final captures + contact sheet, release the game lock.

## Workspace / tools
- Lock HELD: C:\Games\THUG2-6, owner volumetric4 (release: tools\game\game_lock.ps1 -Action release -Owner
  volumetric4 -Game C:\Games\THUG2-6). Deployed: this branch's out\build\vol4-x64.
- Relit data rebaked (BO 34e098ec, BE d43571f2, sch 5bb021ea - same keys as round 3).
- Scripts (worktree out\scripts): build64.cmd, build86.cmd, ws_ini.ps1 (user's [Graphics] read-only + workspace
  window, rt_dof 0, tod held, snow 0), poses.ps1, all.ps1 (-Tag t), measure.ps1, sheet.ps1.
- Sun at College 06:20: toward (0.744, 0.2145, -0.633), 12.4 degrees; the tower ~(-10, ?, -2981).
