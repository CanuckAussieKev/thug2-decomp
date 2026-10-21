# THPS3 Warehouse: half the level going black (2026-10-21)

User report (main install, DLL from b0e6b3b, THPS3 reconverted with main's converter, OcclusionCulling=0): "some of the
map is flickering" - screenshots thug2_2026-10-21_19-46-31 / -32: skater (-83.1, -267.2, -536.1) heading 135.6, camera
(-180.3, -178.0, -436.8) looking along (0.675, -0.261, -0.690). Both sidecars have the same skater and camera pose; in
-31 the room around the skater (floor, walls, the toilets' windows) isn't drawn - black, with the main room's ramps
seen through where its walls should be; 83 frames later (-32) it is drawn. Branch `worktree-agent-a8bed1e73cc3bfa14`;
workspace C:\Games\THUG2-1.

## Cause: a THPS3 light-flicker script hiding the lit set, with the dark set it switches to left out

- THPS3's `JKU_Ware_RoomFlicker` (Data\Scripts\jku_scripts.qb) flickers the room's lights by swapping two copies of
  the same surfaces: `visible prefix=SkWare_Light / invisible prefix=SkWare_Dark`, then in a loop: wait 10 / 30 / 60 /
  120 frames (random), `invisible SkWare_Light + visible SkWare_Dark`, wait 10-120 frames, back to lit.
- PC-THPS3-1 (tools/compat/thugpro/thps3_levels.json) leaves the dark set out (SkWare_Dark*: both sets drawn
  z-fought). The translator then drops the dark statements (withoutUndrawn: "prefix matching geometry without a scene
  sector: skware_dark x3") but kept `invisible prefix="SkWare_Light"`: every cycle the lit set (SkWare_Light_Floor06,
  _Wall01/04/05, _QP05, the windows, signs...) went away for 10-120 frames with nothing in its place - black.
- Why now: THPS3 itself spawns the script from its restart nodes' scripts, which our port doesn't bring along, so it
  never ran - until THUGPRO's new content (TPN-THPS3-2 node `Ware_RoomFlicker` + TPN-THPS3-4 `TRG_Ware_RoomFlicker`
  -> `Obj_SpawnScript JKU_Ware_RoomFlicker`, included since 56c5b11, 2026-10-20) started it at level load. Converted with
  `--no-thugpro-new` the level's .qb has no RoomFlicker at all (52 scripts instead of 53). Not the DLL: not the EC3
  commands, the occlusion / frustum culling, the env-pass materials or render state - the series below were taken with
  main's own DLL (4d49723) and only the conversion changed.

## Fix (converter)

- PC-THPS3-1 gets `"shown": ["skware_light"]`: the copy kept in place of the left-out one stays drawn.
- tools/level_port/script_port.ts: TranslatorOptions.shownPrefixes / Translator.hidesShown - an Invisible or Kill
  whose `prefix=` overlaps a shown prefix (the set, a part of it, or a broader prefix covering it) is left out (noted
  "hides sectors kept shown"); `name=` of a single sector is kept (the lit glass panes' Kill before their Shatter).
- tools/thps3_levels/thps3_levels.ts: shownPrefixes(pc, base) passed through portScripts. The translated
  JKU_Ware_RoomFlicker is now `visible prefix=SkWare_Light` + waits - harmless.
- Test: script_port.test.ts "hidesShown". npm typecheck / lint / test (189) pass. No C++ change; thug2_check (built from
  this branch = main's code) on THUG2PM.exe and C:\Games\THUG2\Game\THUG2.exe: 519 early self-tests pass;
  check_code_writes: no overlaps.
- Docs: tools/compat/thugpro/README.md (PC-THPS3-1 row), docs/level-port-compromises.md (THPS3 Warehouse line).

## Numbers

Workspace C:\Games\THUG2-1 under the lock, 1280 x 720, OcclusionCulling=0 as the user, main's DLL (4d49723 built in
this worktree), Warehouse converted there: `run_game.ps1 -Level ware -Commands "waitfor level ware 150;teleport -83.1
-267.2 -536.1 135.6;camera -180.3 -178.0 -436.8 0.675 -0.261 -0.690;wait 60;capture 300 ware_<x>"`. Frame 150 of
the "before" series is the user's -31 screenshot exactly. Regions: right 720,100 560x460; floor 0,580 1280x140; left
0,250 540x250.

| series (300 frames)            | frames with the floor > 50% black | frame_diff t=24: flickering px (frame / right / floor / left) | mean abs d (frame) |
|--------------------------------|---------------------------------|------------------------------------------------------------------|---------------|
| before (main's conversion)     | 70 (3 runs: 10, 30, 30 frames)  | 95.74% / 100% / 100% / 96.65%                                    | 3.83          |
| after (this branch)            | 0                               | 1.34% / 0.05% / 0.48% / 0.00%                                    | 0.09          |

What's left after is the skater's idle animation and its shadow (heat map C:\Games\THUG2-1\Screenshots\
ware_after_heat.png). Series kept in C:\Games\THUG2-1\Screenshots\ware_{before,after}.

## Other THPS3 levels

Only Warehouse has a drawn set left out by a port change (PC-THPS3-1 is the only `prefixes` entry). The other levels'
dropped sector statements (converted all 17 into a scratch folder) are collision-only geometry (no scene sector:
Foundry's secret generator collision, Airport's lowarea / startarea..., Tokyo's smashers...) - nothing drawn goes
missing. Their looping scripts that hide things (Foundry's valve / DJ booth lights, Canada's log loader) swap pairs that
are both converted. THUG2-1's THPS3 staging now holds only Warehouse (this branch's); its overlay was rebuilt.

## For the lead / the user

- Reconvert Warehouse (`node tools/thps3_levels/thps3_levels.ts convert Ware --thug2 C:\Games\THUG2\Game`, or every
  THPS3 level as usual) and run make_menu_level.ts; no DLL change needed.
- The user: stand in Warehouse's toilets / the room by the halfpipe (the screenshot spot) for half a minute - the
  floor and walls should never go black. The room's lights no longer flicker at all (THPS3's flicker needs the dark
  set, which z-fights when drawn with the lit one).
