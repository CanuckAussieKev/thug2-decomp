# Ported peds' rigs: women on Ped_Female (2026-10-21)

Branch `worktree-agent-afafbf233027f0ecc`. Tools only (no src changes). NOT played in the game by the user yet.

## The bug
The user's screenshots (main install, THAW Beverly Hills `z_bh`, 2026-10-21 18:45-59 / 18:46-02): women with their
arms and torsos twisted through their bodies.

## Cause
THUG2 skins its people on two skeletons (scripts\game\skeleton.qb): `THPS6_human` (50 bones) and `Ped_Female`
(55). Read from Data\skeletons\*.ske.xbx: their first 15 bone checksums are the same, from index 15 on (the
shoulders / arms / neck) they differ - Ped_Female has 5 more bones and its arm bones at other indices. A model rigged
to Ped_Female posed by THPS6_human comes out exactly as in the screenshots.

THUG2's own levels pair them: `Type=Ped_F_From_Profile skeletonName=Ped_Female AnimName=animload_ped_female` with
`random_female_profile` (12 peds in LA, AP, AU...), `Ped_Sun_Bather_01` (AU, SE2), `Ped_Nurse_01` (BE, BO); all
other profiles on THPS6_human (Ped_ZombieFem01 too). Its model viewer (scripts\game\menu\models.qb) agrees (Nurse, Sun
Bather, BostonChick, Spanish Chick on Ped_Female; Zombie Female 01 / Female 01 on THPS6_human); THUG1's NY / VC put
`Ped_BusinessWoman_01` on Ped_Female. And load_level (scripts\levels.qb) loads the Ped_Female animations only through
the level's `load_level_anims` script (THPS6_human's are loaded at start-up).

- **THAW** (`thaw_scripts.ts` ped()): every ped `Ped_From_Profile` + `THPS6_human` + `animload_THPS6_human`, whatever
  the profile - and `profileFor` gives `random_female_profile` to women (FEMALE words: "lady", "fem"...). z_bh's 24
  `Z_BH_TRG_Ped_OldLady_*` were women on THPS6_human. No load_level_anims either.
- **THPS4**: the same in thps4_scripts.ts (standing peds: female / girl / skank types -> random_female_profile);
  peds_cars.ts's walkers / pros likewise (no women there so far).
- **THUG1**: kept its own skeletons, but an unknown profile -> random_female_profile left THUG1's THPS5_human ->
  THPS6_human: Vancouver's female security guards (shared structure `vc_inc_152`, Type=Ped_F_Security).
- **THPS3**: already right (Ped_F_From_Profile / Ped_Female / its load_level_anims) - unchanged output.
- Not THUGPRO-related: THUGPRO's z_bh (inspected, read only) uses THAW's own ped models and profiles (Ped_Dave,
  Ped_Mindy...), all on THPS6_human (THAW's skeleton); we use THUG2's stand-in people, so need THUG2's pairing.
- Not fixProfiles: it already chose random_female_profile only for Ped_F_From_Profile nodes; the problem is the
  skeleton next to the profile.

## Fix (shared: tools/level_port)
- `tools/level_port/ped_rigs.ts`: `femaleRigProfiles` (from THUG2's profile_ped.qb: profiles made of `ped_f_*` parts,
  or whose `ped_body` is Nurse 01 / Sun Bather 01 / Businesswoman 01 - 6 of THUG2's 154 profiles: random_female_profile,
  Ped_Hint_Skulls, Ped_Hint_Blimp, Ped_Nurse_01, Ped_Sun_Bather_01, Ped_Businesswoman_01), `fixPedRigs` (every
  Class=Pedestrian on a human skeleton with a profile gets that profile's skeletonName / AnimName, and the generic Type
  Ped_From_Profile <-> Ped_F_From_Profile; a level's own Type kept; animals untouched), `loadLevelAnimsNs`.
- `peds_cars.Thug2Assets`: `femaleRigs`, `rig(profile)`.
- THAW: ped() writes the profile's rig; convertLevelNodes runs fixPedRigs on all nodes (THUGPRO's added ones too) and
  adds `load_level_anims` (animload_THPS6_human + animload_ped_female when any woman).
- THPS4: standing peds use the rig; fixPedRigs over converted + peds_cars nodes; load_level_anims added.
- THUG1: fixPedRigs per ped before its animations go into the level's load_level_anims (the existing mechanism).
- THPS3: fixPedRigs after fixProfiles (a no-op on today's data; covers THUGPRO-added peds).
- `node tools/level_port/ped_rig_check.ts [--thug2 <game>] <overlay folders...>`: read-only audit of converted levels
  (peds on the wrong rig, Ped_Female animations not loaded). Unit tests in level_port.test.ts.

## Affected (ped_rig_check on the main install's overlays, before)
- THAW: z_bh (24 women), z_ho (2), kyoto (4).
- THPS4: cnv (4), hof (2), lon (2), sch (4), sf2 (1).
- THUG1: vc (vc_inc_152, the female security guards).
- THPS3: none.
After: every level of all four converters' output checks ok (converted to C:\Games\THUG2-3 / scratch).

## Verified
- npm run typecheck / lint / test (183 pass), after merging main (12155c3, resolveInstall).
- THAW and THPS4 converted into C:\Games\THUG2-3, make_menu_level --game C:\Games\THUG2-3; THPS3 / THUG1 converted to
  a scratch folder and checked.
- Game runs (THUG2-3, under the lock; the DLL from the main install - same src as this branch): z_bh loads, peds stand;
  at restart Z_BH_TRG_Restart_BikeGoal10 the same woman (red hair, green trousers) before (main's z_bh.qb): wide
  stance, arm bent out, waist twisted; after: standing naturally, arms at her sides. Log clean.
- THPS4 Carnival (cnv) crashes while loading in THUG2-3 (THUG2.exe+0x8DE30) - with main's cnv.qb too, so not this
  change (main's later script pool sizes may matter); not investigated.
- Teleporting the skater onto a ped node (ResetSkaters node_name=<ped>) crashed - a test-driving mistake, not the levels.

## For the lead
Reconvert and install THAW and THPS4 (all levels) and THUG1 (at least VC); THPS3 unchanged. Then make_menu_level.

## For the user to check
1. THAW Beverly Hills: the women along the streets (City Hall, Rodeo-style shops) stand normally, idle animations play.
2. THAW Hollywood (2 women), Kyoto (4), THPS4 Carnival / Hoffman / London / School / San Francisco, THUG1 Vancouver's
   security women.
