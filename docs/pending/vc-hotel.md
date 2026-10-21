# THUG1 Vancouver: the hotel drawn from inside (agent vc-hotel, 2026-10-21)

The user's report (main install, after the day's reconvert; screenshot thug2_2026-10-21_22-49-06): "I'm in the hotel,
yet I can see the ice rink below." Skater (2093.6, 0.6, 1180.5), camera (1952.4, 78.0, 1168.5) looking +x: the hotel's
interior (lobby floor, walls) isn't drawn, the underground ice rink, the plaza and the city show through.

## Cause

THUG1's VC shows and hides whole parts of the level by where the camera is: 20 ProximNodes (`TRG_proxim_vh`,
`TRG_proxim_vh_interior`, `TRG_proxim_vh_p1/p2`, `TRG_proxim_sup*`, `TRG_proxim_in_*`, `TRG_proxim_mus*`; class
ProximNode, `Type=Camera`, `Shape=BoundingBox` - the box of the collision object of the node's name, which our
vc.col.xbx has with THUG1's boxes) whose scripts (VC_scripts.qb) go

    script VC_proxim_vh {
        if inside { ... visible prefix="g_vh_int_A" / B / C / top }      // the hotel's interior
        else      { ... invisible prefix="g_vh_int_A" / B / C / top }
    }
    script VC_proxim_sup_st1_ugoff { if inside { invisible prefix="g_ug" } else { visible prefix="g_ug" } } // the rink

`inside` is the engine's `Inside` command (THUG2 0x00449950, src/game/script/debug_cfuncs.cpp CFunc_Inside: whether
the proximity script running now was started by entering). Our script translator (tools/level_port/script_port.ts
`condition`) rewrote every `if inside` to `if GotParam inside` - written for THPS3, whose proximity nodes we replace with
ghost objects that pass an `inside` parameter. THUG1's ProximNodes are kept as THUG2's own (thug1_port.ts), and THUG2's
proximity code (Obj::Proxim_Update / RunNodeScript, src/game/obj/proxim.cpp) passes no parameters, so the test was
always false: every VC proximity script ran its "outside" branch on entering as well as leaving. A node starts as
holding the camera (m_outside 0), so at the first update every node the camera isn't in runs its exit script - the
hotel's interior went invisible from the start and entering it ran the exit branch again; the underground (rink) was
never hidden inside the hotel. Exactly the screenshot.

Not from today's change: a conversion with the tools from before 0d1eb27 (PC-THUG1-21 / 22) has the same 17
`if GotParam inside` as today's (only VC_startup and the peds differ). VC_startup (now run) only loads sounds and creates the limo /
leaf blower - no visibility. THUGPRO's VC (VC_scripts.prx, read only) keeps THUG1's `if inside` - THUG2's command.

## Fix

- `TranslatorOptions.proximInside` (script_port.ts; default false - THPS3's ghosts and THPS4's brought proximity
  scripts unchanged): `if inside` is kept as THUG2's Inside command. thug1_port.ts sets it (THUG1's ProximNodes are
  THUG2's). Shared file: additive option.
- vc.qb after: 0 `GotParam inside` (17 before), 21 `if inside` (4 - VC_proxim_mus, _mus_st1, _in_p1_st1, _in_ug_p1 - already were); the visible / invisible / Kill / Create
  statements unchanged (5275 before and after). Other THUG1 levels' `if inside` (proximity scripts) are fixed the same way.
- Test: script_port.test.ts "condition: `if inside` ..." (both forms).

## Checks

- npm typecheck / lint / test (243 / 0); thug1_levels selfcheck 0 failures.
- Game (C:\Games\THUG2-4, this worktree's verify DLL - no C++ changes, main's -, all 16 THUG1 levels converted with
  `--thug2 C:\Games\THUG2-4` + make_menu_level --game; `-level vc`, automation teleport / camera / capture):
  - the user's spot and camera (teleport 2093.6 0.6 1180.5 85.1; camera 1952.4 78.0 1168.5 0.980 -0.179 0.083): the
    hotel lobby drawn - red carpet, marble floor, columns, the front desk with its clerks; no rink, no city.
  - out (skater at (1150, 0, 1180), camera from (1000, 150, 1180) looking +x): the Goodmount Vancouver's front drawn;
    back in at the user's spot: the lobby again (the proximity scripts switch both ways).
  - the limo parked by the SJ restart, as before.
  - log: no run-time check failures / errors. Lock taken and released (DLL and ini put back).
- For the user: walk through the hotel (lobby, upper floors, the pagoda / Superior Court ends) and the underground -
  every part THUG1 hides by camera position now comes and goes as in THUG1; say if anything pops in late.
