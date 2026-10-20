# Translator fixes: what the level converters think THUG2 has (agent translator-fixes, 2026-10-20)

Follows the read-only audit of the converters' "not in THUG2" assumptions (about 1,000 statements left out that THUG2
can run). Shared code first (tools/level_port/script_port.ts); every converter takes it. **NOT run in the game yet**
(no game runs in this task): the checklist at the end is for the user.

## What changed

1. **A generated member table** - `tools/extract_members.ts` (new) reads THUG2PM.exe / THUG2.exe and writes
   `tools/level_port/thug2_members.ts` (committed, so the converters need no data/derived; header says how to
   regenerate). From the exe alone:
   - the 81 component types and their create functions (RegisterComponentTypes 0x004E4860), each one's vtable (the one
     its constructor stores with its type, the most like CBaseComponent's 0x00647618) and slot 12, CallMemberFunction
     (name);
   - the **table members** (RegisterMemberFunctionTable 0x0046F060, 33 call sites `push 0x18; push table; push type`):
     483 - identical to the old `membersOf` byte scan (it had one bogus zero entry);
   - the **by-name members** (RegisterMemberFunctions 0x0046F020, called once at 0x005A5F62 with the 250 names at
     0x00681AD0, the count from 0x005A5670) and, for each, the components / objects whose CallMemberFunction compares
     its checksum (cmp / sub imm32 up to the function's end: int3 padding or the next known function start). Object
     classes: vtables whose slot 2 is 0x0045D850; CObject's 0x004608F0 is `object` (SetProps, Die), CSkater's (vtable
     0x0064EF48) `skater`. 245 of 250 attributed; 5 (AnimLoaded, AssetLoaded, Set/ClearTargetObject,
     EditorCam_SetOnlyVerticalCollisionChecks) to none - not on any converted object.
   - Why the split matters (src/game/script/cscript_exec.cpp): a table member runs on `GetComponent(type)` - a null and
     a crash without the component; a by-name member goes through the object's CallMemberFunction (each component, then
     CObject's, which returns true) - no crash, nothing done (and true as a condition).
   - **Obj_AllowSkitching (0x0051B9D0)**: the skitch component's slot 12 (type 0x3506CE64, vtable 0x0064C218) - cars
     have it (car_add_components unless NoSkitch). Enabled for VEHICLE.
   - `Thug2.members` is now {member: the components that run it}; `membersOf` and the hand-made `BY_NAME` entries are
     gone (BY_NAME stays as an empty per-converter extension point).
2. **One skater context** (`SKATER_COMPONENTS`): CSkater's constructor (0x00582D90: AddBaseComponents and its 28
   components) and Init (0x00581850: AddMotionComponents, animblender, skeleton, model, shadow, vibration, stance panel,
   skater sound), plus `object`, `compositeobject`, `skater`. THAW's `thaw_skater` is now exactly this (its hand list,
   including the THAW agent's `sound`, removed). Made objects: OBJECT gets lockobj and the base component 0xB87C8E43
   (CreateGameObject 0x00468CB0 adds both to every game object) and model (by name: nothing without one); PED / VEHICLE
   get AddMotionComponents' five, the base component, model (+ shadow / skitch). Stream isn't added to OBJECT (only
   with SupportStreams, and it has a table).
3. **thug2ScriptOk** uses `this.safe` (a converter's `components:` count). **scriptNeeds** now follows only commands:
   names in command position (a statement's first name, after if / not / else / a random's branch - not parameter
   names or values, not `object:` calls, not `name = value`), plus scripts it goes to / spawns on the same object; and
   an `if GotParam X` branch counts only when the calling statement passes X (`<...>` - not known: every branch).
   THUG2's ShatterAndDie, BloodSplat, trigger_jump_boost, TeleportSkaterToNode / SK3_TeleportToNode, Car_Generic01,
   GetTriggerArray, GetBonus, SlowSkaterToStop, PlayStyleAnim, createfastparticlesystem, VibrateOff now pass. The
   hand whitelists that worked round the over-count are removed: THAW's SKATER_SCRIPTS_CHECKED (trigger_jump_boost,
   ShatterAndDie, BloodSplat) and THUG1's `shatteranddie` (TRUSTED keeps ped_ / car_).
   - Side effect found and fixed: the over-count had been hiding THUG2 front-end / goal scripts the source games' level
     scripts call by the same names - level_select_change_level (THUG1, 10), maybe_auto_load_from_memory_card,
     restore_start_key_binding, create_speech_box, GoalManager_HidePoints (THPS4), OldSkool_Icon (THUG1). Added to the
     shared LEFT_OUT (with GoalManager_ShowPoints), and a LEFT_OUT THUG2 script handed out as a script value
     (`scr=`, spawn) is left out too.
4. **Shared TriggerType** (`script_port.triggerType`, applied by the Translator to every TriggerType statement; THPS4's
   own copy removed): THUG2's TriggerType (0x00434970) masks the trigger component's events (+0x28, set by
   TriggerNode 0x00434A60) with every non-negative number of its first unnamed array, or of any array in an array of
   arrays; no array: a null read (0x004349B2). Forms:
   - `TRIGGER_*` (THPS3, THPS4, THUG1 - the same ten names and values 0-9 in all three skutils.qb): `[[7 3] ...]`;
   - THAW's flags (`land onto`, `{Lip On}` - its inline structures are plain arguments by then): GetTriggerArray's three
     numbers (walk 14, skate 1, jump 2, onto / on 3, off 4, offedge 5, wallplant 6, lip 7, land 8, bonk 9, climb / hang
     10, graffiti 11, spin 12, grind 13, projectile 14; through: none), in its order, -1 padded;
   - `<trigger_array>`: kept. Smack / Natas / AcidDrop / STALL: no THUG2 event - left out and reported
     (`TriggerType: no THUG2 trigger event: X`).
   - The numbers checked in the exe: the .rdata event triples callers push to 0x00434BF0 / 0x00434A60 - skate onto /
     off / off edge {1,3} {1,4} {1,5}, jump off {2,4} (0x0050391A), wallplant {6,3} {6,4}, land onto {8,3}
     (0x00508575), bonk {9}, hang {10,4} (0x0043DB46), graffiti onto {11,3} (0x0051E658), grind onto {13,3}
     (0x0050AAA3), walk {14,3} {14,4} (0x0052B55B), projectile {14} (0x004F3A34). Lip 7 and spin 12 aren't pushed as
     constants: GetTriggerArray's.
   - THAW: no call site was needed in thaw_scripts.ts for its level scripts (the Translator does it); its own
     RailAccelerate (written as text) now checks `TriggerType [3, 8, -1]` (land onto, from `triggerFlags`), as THAW's.
5. **Face flags** (comments; FACE_KEEP values unchanged for THPS3 0x1FFF, THPS4 0x15FC, THUG1 0x1FFF):
   - THUG2's 0x2000 is "not taggable" (the graffiti feelers' SetIgnore(0x3000) at 0x0051DF71 / 0x0051E74B). **Not kept**
     for these three: THPS4 sets it on 487,923 of 488,023 faces and THUG1 on all 476,641 - a different meaning there;
     kept, it would make every face untaggable. THPS3: 0x1000-0x8000 each on a few hundred of 580,016 faces with every
     other bit about half the time (stray values; no tagging in THPS3).
   - 0x4000 / 0x8000: **no reader in THUG2's code.** Feelers' masks (SetIgnore's callers) never include them, and a
     linear sweep of .text for every test / and / bt with 0x4000 / 0x8000 (or ah-style 0x40 / 0x80) finds only other
     fields (CCollObj +0x08's "owns / shares geometry" 0x8000 / 0x4000 at 0x004121EE..., pad buttons, park-editor piece
     flags +0x1D0, render flags). Dropping them changes nothing in the game.
   - THPS4's FACE_KEEP comment was wrong: THUG2 also uses 0x01, 0x02, 0x200, 0x800 (1,292 / 1,513 / 1,076 / 1,532
     faces); THPS4's are left out because their THPS4 meaning isn't checked (they're on 15,806 / 13,651 / 17,191 /
     2,986 of THPS4's faces).
6. **ShatterAndDie's texture**: THUG2's defaults `texture_swap=BA_APM_brokenglass01` (OldStyle skips it). The THAW
   agent found that on PC Shatter stores texture_swap (0x005A5090) but the piece builder (0x004BA7A0) never reads it,
   so the default changes nothing: no OldStyle is passed. (THPS4's ShatterAndDie, in CPF_Scripts, and THUG1's are
   THUG2's OldStyle branch without `<...>`; THPS3 has none.)
7. **Name clashes** (`isThps3Script`: THUG2's script of a name wins over the source's): documented there; the clashing
   scripts the conversions actually run, compared by the parameters each body reads (`<x>`, GotParam x): THPS3 2, THPS4
   18, THUG1 47, THAW 14 - in every one that differs the source's parameters are a subset of THUG2's (THUG2's extras:
   Car_Generic01 HPRV / StickToGround / Skitch, TeleportSkaterToNode vertair / CameraId, ShatterAndDie OldStyle /
   texture_swap / Vibrate, Pigeon_Generic perched, SetObjectColor_CurrentTOD tod_params..., maybe_auto_load
   force_autoload, goal_wait_and_show_key_combo_text ScreenElementPos). No caller passes a parameter THUG2's version
   ignores. (THPS4 also still runs THUG2's kill_start_key_binding, GoalManager_HideGoalPoints,
   add_multiplayer_mode_goals, goal_wait_and_show_key_combo_text - as before this change; candidates for its LEFT_OUT.)
8. `thugpro_extract.ts:954` ("THUGPRO's own C++, e.g. TriggerType"): already reworded on main by the THAW agent - not
   touched.

Shared files changed (all additive for callers): tools/level_port/script_port.ts, thug2_members.ts (new),
script_port.test.ts (new), README.md; tools/extract_members.ts (new); tools/parity/level_port/driver.ts (scriptNeeds'
type); thps4_scripts.ts (its TriggerType / COMPONENTS / BY_NAME removed); thaw_scripts.ts (skater context,
SKATER_SCRIPTS_CHECKED, RailAccelerate); thug1_port.ts (TRUSTED); face flag comments in thps3_levels.ts, thps4.ts,
thug1_levels.ts; docs/level-port-compromises.md.

## Left-out statements, before -> after (all levels, from each Translator's report)

Before: main at 6963c91 + the merged THAW-only fixes; after: this branch. `convert --thug2 C:\Games\THUG2-1` (default
compat).

| Game | Before | After | |
|---|---|---|---|
| THPS3 | 2,548 | 2,244 | -304 |
| THPS4 | 3,623 | 3,059 | -564 |
| THUG1 | 2,034 | 1,880 | -154 |
| THAW | 1,931 | 1,773 (1,629 with the re-extracted register data) | -158 (-302) |
| Total | 10,136 | 8,956 (8,812) | -1,180 (-1,324) |

By name (the changes):
- THPS3: TriggerType 112 -> 0, DuplicateTrigger 71 -> 0, Obj_PlaySound 84 -> 0, OnRail / OnGround 13 -> 0,
  Obj_ObjectInRect / InRadius 9 -> 0, StartGapTrick 5 -> 0, Obj_SetColor 4 -> 0, VibrateOff 2 -> 0,
  Obj_AdjustSound 2 -> 0, SparksOff 1, Held 1 -> 0.
- THPS4: ShatterAndDie 146 -> 0, Car_Generic01 82 -> 0, DuplicateTrigger 66 -> 0, Obj_LockToObject 62 -> 0,
  Obj_AdjustSound 56 -> 0, TeleportSkaterToNode / SK3_TeleportToNode 42 -> 0, AnimEquals 17 -> 0, Obj_MoveToNode
  14 -> 0, Held 10 -> 0, Obj_MoveToRelPos 8 -> 0, model light / colour / bounding sphere / shadow members 31 -> 0,
  SparksOff 8 -> 0, AdjustBalance 5 -> 0, Obj_PlaySound 3 -> 0, Obj_ObjectInRadius 3 -> 0, Obj_GetSpeed 4 -> 0,
  CurrentScorePotGreaterThan / SetQueueTricks / Rotate 2 each -> 0, nodes passed on now kept 7 -> 0;
  Obj_AllowSkitching 6 still out (THPS4's cars run as NONE: "member of skitch"); now left out by name:
  create_speech_box 6, restore_start_key_binding 5, GoalManager_HidePoints 2, maybe_auto_load_from_memory_card 2;
  geometry-prefix expansions 6 -> 12 (more statements reach them).
- THUG1: TriggerType 51 -> 0, Obj_LockToObject 17 -> 0, Obj_ObjectInRadius / InRect 13 -> 0, FirstTimeOnThisRail
  12 -> 0, Obj_AdjustSound 9 -> 0 (1 left: a NONE context), Skitching 7 -> 0, teleports 15 -> 0, model / shadow
  members 14 -> 0, IsLatestTrick 3, SlowSkaterToStop 3, PausePhysics / UnPausePhysics 4, IsInBail 2,
  DuplicateTrigger 2, Obj_PlaySound 2, GetSpin 1 -> 0; now left out by name: level_select_change_level 10,
  OldSkool_Icon 3, goal_wait_and_show_key_combo_text 3 (THUG1's list; reached now).
- THAW (code alone): TriggerType 113 -> 0 (Smack 7, AcidDrop 6, Natas 3, STALL 2 left out as no THUG2 event),
  createfastparticlesystem 31 -> 0, GetBonus 12 -> 0, SetSpeed 13 -> 3, Obj_LockToObject 5 -> 0, onRail 3,
  PlayStyleAnim 3, GotSpecial / AdjustBalance / RotateDisplay / GetHeading / Rotate / IsInBail / GetTriggerArray 1
  each -> 0. With the register data: THUGPRO's versions of 95 more gap scripts replace our translations (their own
  left-outs go: printf 588 -> 550, GoalManager_GoalIsActive 197 -> 177...).

## The register data (`--extract-thugpro`, each game, against C:\Games\THUG2-1's conversions)

- THPS3, THPS4, THUG1: unchanged.
- THAW (tools/compat/thugpro/thaw_levels.json): TPN-THAW-4 scripts 37 -> 132 (set 12 -> 102, add 25 -> 30): THAW gap
  scripts THUGPRO gated by GetTriggerArray / TriggerType, refused before because TriggerType wasn't a known command
  (register README updated). `--compat none` output changes too (the translator), as expected.

## Checks (all on THUG2-1's conversions)

- typecheck / lint / test (100 tests; new: tools/level_port/script_port.test.ts - member table lookup, the contexts,
  triggerType's three forms, scriptNeeds' command / GotParam rules); selfcheck of thps3 / thps4 / thug1 / thaw: 0
  failures.
- col_check: 66 level collision files, 0 unreachable faces.
- Links: every node array's Links in range (THPS3 10,854, THPS4 13,933, THUG1 21,415, THAW 20,132).
- ns.exe decompiles every .qb but thps3_skateshop.qb (known), THUG1's seven `<level>_thugpro.qb` and THAW's
  z_el.qb - the same nine fail on main's output.
- make_menu_level --game C:\Games\THUG2-1: fine; THPS4's backdrop: 44 rail nodes, 138 peds / cars / waypoints, 3
  trigger geometry nodes and 6 scripts.

## In the game (for the user)

- THPS3 / THUG1 gaps that test how you landed: THPS3 Roswell, Cruise Ship, Skater Island lip gaps; THUG1 lip / land
  gaps - do they score now (and only when landed / lipped as named)?
- Multi-piece gaps (DuplicateTrigger): THPS3 (71) and THPS4 (66) - a gap whose second / third piece should count.
- THPS4 breakable glass (ShatterAndDie, 146): breaks, with the object's own texture on the shards.
- Teleports: THPS4 (TeleportSkaterToNode / SK3_TeleportToNode, 42), THUG1 (15): the skater is moved.
- Objects riding cranes / vehicles (Obj_LockToObject): THPS4 Kona / Junkyard / London, THUG1 (17), THAW (5).
- Sound fades (Obj_AdjustSound): THPS4 water jets / machines (56), THUG1 (9). THPS3 geometry hit sounds (84
  Obj_PlaySound): glass, metal.
- THAW: rail speed-ups only on land / onto; gaps with TriggerType conditions (East LA, Downtown, Hollywood...).
- THPS4 cars (Car_Generic01 on its vehicles, 82) drive; skater-run THPS4 trigger scripts with Held / AnimEquals /
  AdjustBalance conditions.

To switch off: revert the branch's commits (the shared translator; no ini switch).
