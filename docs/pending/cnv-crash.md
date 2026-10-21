# THPS4 Carnival (cnv) load crash, and London / Chicago / Alcatraz / San Francisco - 2026-10-21

**Symptom:** in C:\Games\THUG2-3, with main's converted cnv.qb, Carnival crashed while loading at THUG2.exe+0x8DE30
(0x0048DE30). Still the same with main's DLL (c41cd3c, the bigger script pools): 0xC0000005 reading 00000000, ECX 0,
return address 0x00421D7C; symbolized: `Obj::CBaseComponent::CallMemberFunction` <- `CCompositeObject::CallMemberFunction`
<- `ObjectCallMemberFunction` <- `CScript::ExecuteCommand` <- `CScript::Update` <- `CCompositeObject::Update` (an
object's script running a member function). load_check over every THPS4 level (main's converter + this DLL) found
four more crashes: hof and lon at 0x00482BA2, alc and sf2 in `ComponentCallMemberFunction` (cscript_exec.cpp:82).

## 1. Carnival: model lights (0x0048DE30)
- 0x0048DE30 is a 2-instruction thunk, `mov eax,[ecx]; jmp [eax+0x24]` (a virtual call). Its caller 0x00421D77 is in the
  model component's member function 0x00421AE0: case 0xA14D6372 (Obj_EnableAmbientLight) jumps to 0x00421D68:
  `push 1; ecx = model (+0x208)->0x00532EB0()` (= `mov eax,[ecx+0x2C]`: the model's lights) `-> 0x0048DE30`. With no
  lights, ECX is 0. Obj_SetLightAmbientColor (0x3CF63049), Obj_SetLightDiffuseColor, Obj_Enable/DisableDiffuseLight,
  Obj_SetLightDirection all call through +0x2C unchecked; only Obj_HasModelLights tests it.
- The lights are made by the model component's init 0x004214F0 only with `UseModelLights` in the object's parameters
  (0x10079F2D: 0x00493BD0 makes them; without it 0x00493C10 takes them away). THUG1's NY nodes carry UseModelLights;
  the skater adds it itself (skater.cpp).
- Carnival's rides (TRG_FerrisCar*, TRG_HatterRide_Teacup*, TRG_Veh_Gravitron, TRG_BusRide_Bus*, the coasters' bodies
  through CNV_BigCoasterSection) run THPS4's `Mdl_NoDynLighting` (Obj_EnableAmbientLight, Obj_EnableDiffuseLight
  index=0/1, Obj_SetLightAmbientColor 128, Obj_SetLightDiffuseColor 0): THPS4's models had lights, THUG2's need the
  flag. London has the same script (11 objects). No other converted level (THPS3, THUG1, THAW) uses the light members.
- Fix (converter): `tools/level_port/nodes.ts` `useModelLights(text, scripts, more)` (shared): UseModelLights after
  the Class of every node whose TriggerScript reaches a light member (`MODEL_LIGHT_COMMANDS`; reachability:
  `scriptsUsing`, factored out of absentInNetGames - same output). thps4_levels.ts runs it after netGameNodes:
  PC-THPS4-12 `model_lights` (required; tools/compat/thugpro/thps4_levels.json + README). cnv: 28 objects, lon: 11.

## 2. London and Chicago (hof): pigeon animations (0x00482BA2)
- `CStandardAnimController::CallMemberFunction` (anim_controller.cpp:225, PlayAnim) -> `FrameIndex` 0x00482BA0 on a
  null frame: an animation of a set that isn't loaded. Both levels have THUG2 pigeons (lon 9, hof 5; Anl_Pigeon,
  AnimName=animload_anl_pigeon) and, since the ped-rigs merge (7a67779), a `load_level_anims` loading only
  animload_THPS6_human / animload_ped_female. animload_anl_pigeon (scripts\allanims.qb) is loaded by nothing else -
  THUG2's own levels' load_level_anims names it (THPS3's converter's loadAnimsNs already adds its animals).
- Proof: lon.qb with `animload_anl_pigeon` added to load_level_anims by hand ran 130 s (crashed at 56 s without).
- Fix (converter): `tools/level_port/ped_rigs.ts` `otherAnims(nodes, names)`: the other AnimName sets of the level's
  Pedestrian nodes, passed to loadLevelAnimsNs as `extra`, and load_level_anims written when there are only animals
  too. Used by thps4_levels.ts and thaw_levels.ts (THAW has no animals: its output is unchanged, checked by a full
  convert to scratch). Not a port change (THUG2's own levels do the same).

## 3. Alcatraz and San Francisco: a tram's stream (cscript_exec.cpp:82)
- A temporary log (not committed) in `Script::CallMemberFunction` named it: member 0xDB05A233 (Obj_PlayStream,
  component 0x0F1641E3 stream) on object tev_TramB01 (a LevelObject), script Auto_SFX_Car_Tire_Squeal.
- THPS4's alc_Tram runs THUG2's Car_Generic01 (SK4_Scripts.qb); when it brakes for the skater it runs `SpawnSound
  Auto_SFX_Car_Tire_Squeal` - SpawnSound (0x005A0460) spawns the script on the calling script's object (+0xBC) - and
  that plays a stream. A LevelObject has a stream component only with SupportStreams (THUG2's
  gameobj_add_components); peds and cars always have one (AddMotionComponents 0x0057B2F0).
- Fix (converter): `nodes.supportStreams(text, scripts, more)`: SupportStreams on every LevelObject / GameObject whose
  TriggerScript reaches a stream member (thug2_members' "stream" table), through the level's scripts and THUG2's own
  (`more` = script_port.Thug2.bodies - every name a THUG2 script uses, so SpawnSound's target counts). PC-THPS4-13
  `support_streams` (required). alc 3, hof 8, lon 15, sf2 11, sch 1, zoo 1 objects.
- Not changed: script_port's SAME_OBJECT_SCRIPTS still lacks SpawnSound, so Thug2.scriptNeeds doesn't see that
  Car_Generic01 needs a stream; adding it would make the translator drop Car_Generic01 from every LevelObject tram
  (they'd stop moving) unless OBJECT's safe components gained "stream" in every converter. The node flag keeps the
  trams and fixes the crash.

## Verified
- npm run typecheck / lint / test (186 pass; new: useModelLights, supportStreams, otherAnims); selfcheck thps4 (139
  files), thps3 (323), thug1 (333), thaw (735): 0 failures. No C++ change (the diagnostic log was removed).
- C:\Games\THUG2-3 under its lock: THPS4 converted from this branch + make_menu_level, this branch's DLL (= main's
  source): load_check 120-180 s each - alc, cnv, hof, jnk, kon, lon, sf2, sch, zoo, motox, practice (the skate shop):
  all running and responding, no EXCEPTION. (Two runs closed by themselves with exit code 0 in the front end before
  loading - sf2 once, the skate shop once as `thps4_skateshop`, a name -level doesn't know; sf2 ran on the retry.)

## The lead must
Reconvert THPS4 (`node tools/thps4_levels/thps4_levels.ts convert` - all levels: cnv, lon, hof, alc, sf2, sch, zoo
change) and run `node tools/menu_level/make_menu_level.ts` for the main install. THAW unchanged (no reconvert needed
for this). No DLL change.

## For the user to check
1. Carnival: the ferris wheel, teacups, gravitron, buses and coasters look evenly lit (THPS4's Mdl_NoDynLighting:
   ambient 128, no diffuse) - not black or flickering.
2. Alcatraz / San Francisco: skate in front of a tram until it brakes: the tire squeal plays, no crash.
3. London / Chicago / San Francisco: the pigeons peck, walk and fly up when the skater comes near.
