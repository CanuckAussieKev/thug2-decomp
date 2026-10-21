# The ports' own ped models - phase 1: the writer and THUG1's people (2026-10-21)

Branch `worktree-agent-ac5e4cfffdeb75865`. Tools only (no src changes). Plan: docs/ped-models-plan.md. The user's
decisions (2026-10-21): peds from their own game; our overlay may ADD entries to THUG2's profile_ped.qb /
cas_ped_m.qb / cas_ped_f.qb (never change THUG2's); the authentic model where one exists, else THUG2's (pros
included); animals and crowds later.

## 1. The skinned-sector writer (shared: tools/level_port/thug2_scene.ts)
A THUG2 skin's mesh vertex, measured on the shipped peds (Data\models\Peds, Ped_Male, Ped_Female - 247 files, 1,066
meshes): stride 40 (+8 per further uv set): 3f position, u32 weights (11 / 11 / 10-bit fields, n / 1023), 4 x u16 bone
indices **x 3** (the bone's matrix rows in the shader constants), u32 normal (x, y 11 bits signed / 1023, z 10 bits
signed / 511 - decoded lengths 0.9977-1.0), BGRA colour, uvs. Vertex shader = how many of the three weights the mesh's
vertices use (1-3; 5 files use variants 4 / 6), offsets [24, 28, 32], pixel shader as a static mesh's
(geometry.pixelShader). The first index list (`lods`) is the strip again - in the exporter's whole-sector numbering
when a sector has several meshes (Ped_M_AU_Torso05).
- `readSkinVertices` / `skinnedMesh` / `skinVertexBuffer` / `pack|unpackWeights` / `pack|unpackNormal` /
  `rewriteSkin`.
- Self-check (`thug1_levels.ts selfcheck`): THUG2's own Ped_M_AU_Head01.skin.xbx and Ped_M_AU_Torso05.skin.xbx
  written again from what the reader makes of them: **byte-identical**. Over all 247 shipped ped skins: 242
  byte-identical, the 5 others differ only by the rare vertex-shader variants. Also: all 198 THUG1 ped skins (every
  `*ped.pre`) convert on a 55-bone skeleton and read back / write again the same.
- Tests: tools/level_port/ped_models.test.ts (packing exact both ways, the mesh layout, THUG2's head byte-identical).

## 2. THUG1's own people (tools/thug1_levels/thug1_peds.ts)
- **Skeletons.** THUG1's men are skinned to `THPS5_human` (55 bones), its women to `Ped_Female`. THUG2 ships
  THPS5_human.ske.xbx byte-identical to THUG1's (never loaded) and THUG1's Ped_Female.ske.xbx is THUG2's byte for byte.
  The men's skeleton is loaded by the level's own animation script (`LoadSkeleton name="skeletons/THPS5_human.ske"
  THPS5_human`). No retargeting.
- **How THUG2 finds an animation** (measured in the exe, 0x00408870 / 0x00480410): an animation asset's key is its
  descChecksum + the SetReferenceChecksum current while loading; an object's animation channel looks up name + its
  "anim set" = the node's `ReferenceChecksum` (0x52A864B0) if given, else its `SkeletonName`. So THUG1's men's
  animations load under `THPS5_human` (THUG2 loads nothing there); THUG1's women's under `THUG1_Ped_Female` with
  `ReferenceChecksum=THUG1_Ped_Female` on their nodes - THUG2's own Ped_Female animations have the same names (and
  differ from THUG1's: every one of THUG1's NY Ped_Female files whose name THUG2 also has differs in its bytes). THUG2's animload_ped_female is loaded for them too (a
  lookup without the reference would find it). ped_add_components passes the node's params to the animblender (`<...>`).
- **Models.** For each ped node of the port: its THUG1 profile (profile_ped.qb) -> its CAS parts (cas_ped_m / _f) ->
  the skins in THUG1's `<LV>ped.pre` archives, converted (`thug1.toThug2Skin`: per-mesh buffers, bone x3, packed
  normals; THUG1's weights are THUG2's packing) to `models\THUG1_Peds\<THUG1's path>.skin.xbx`, the .tex.xbx (THUG2's
  format already) and .cas.xbx as they are, an empty .col.xbx (THUG2's peds have one; THUG1's none).
  Names: profiles `THUG1_<profile>`, parts `THUG1_<desc_id>` (spaces -> `_`), all `no_random` (THUG2's random peds
  never pick a THUG1 part: different skeleton). THUG1's `ped_board` parts (skateboard, leaf blower, jackhammer...) go in
  `ped_m_accessories` / `ped_f_accessories` (THUG2's `board` list is the skaters', in cas_skater_m.qb - not touched).
  `scale=` kept; `body_shape=` kept when THUG2 has the scaling global.
- **Random people** (random_male_profile, randomized_desc_id, MultiStage's random_index): THUG2 picks random parts in
  its engine from every non-no_random part (THUG2's own on THPS6_human among them), so the port picks: up to 6
  variants per THUG1 profile and level (`THUG1_random_male_profile_NJ_3`), the node's variant from its name, each a
  head / torso / legs of one THUG1 skin tone (random_set) and THUG1's level filter (level_specific /
  exclude_from_levels with `load_<level>`).
- **Overlay additions** (tools/level_port/ped_models.ts): the converter writes `<staging>\peds.json` ({cas: {list:
  [entries]}, profiles: [globals]}); make_menu_level reads every staging's, decompiles THUG2's cas_ped_m.qb /
  cas_ped_f.qb / profile_ped.qb, appends (entries at the end of their list, profiles before the
  `__register_checksums__` line), recompiles into the overlay - and the same edits on the Anteara Mod's copies. The
  three files round-trip byte-identical through NeverScript unedited. `installFiles` now also copies `<staging>\anims`.
- **Animations.** Files to `anims\THUG1\<THUG1's path>.ska.xbx` (THUG1's .ska.xbx are THUG2's format). Per level, in
  its .qb: `animload_THUG1_THPS5_human` (men), `animload_THUG1_Ped_Female` (women), `animload_THUG1_<set>` for every
  THUG1 set a ped names in Extra_Anims or the level's own load_level_anims lists (Ped_Bum, Ped_Drunk, Ped_Dealer,
  Ped_Crackhead, Ped_Sec, Ped_Video...), all called from the level's load_level_anims (THUGPRO's TPN-THUG1-3
  load_level_anims, which loads THPS6_human's, is left out: reported). The base sets hold THUG1's ped-folder base
  animations, `default`, and every animation THUG2's ped AI may play (its game\ped scripts' names, followed through
  the globals they name: generic_ped_anims_wait, default_M_anim_assets...; the skating peds' trick lists only when the
  level has THUG1 skaters) plus every animation the level's ped scripts reach (standing_anims=Skater2_goal_wait...):
  THUG1's file of that name, else a THUG1 animation of the same role (`ROLES`: falldown, getup, jump, wave, walk... ->
  Ped_M_* / Ped_F_*, else the idle) - an animation the set hasn't crashes the game (0x00482BA0). THUG1's animload
  lists name some files its PC archives lack (anims\Ped_Bitches\...): skipped.
  Sizes (NJ / NY / VC together): 93 models, 731 animation files, 6.2 MB; LoadAnim lines per level: NJ 735 (men 665 -
  with skaters' tricks), NY 878, VC 934.
- **THUG1's animation lists back.** THUG1's `Ped_InitAI WalkAnims=default_drunk_anim_assets` (/ Walking_Anims= /
  Walk_Anims=, a pro_anims.qb list or one animation) becomes THUG2's mechanism - a level global
  `THUG1_default_drunk_anim_assets_anim_types = {Anim_Types={IdleToWalk=Ped_Drunk_WalkFromIdle ... WalkAnims=[...]}}`
  passed unnamed (`Ped_InitAI THUG1_..._anim_types Fall`, as THUG2's `Ped_InitAI avoid default_Fat_anim_assets`):
  SetTags makes it the ped's Anim_Types, which Ped_Utils_GetAnimName reads first. Only where every ped of the level
  is THUG1's own (a THUG2 ped can't play THUG1's). NJ: drunk / thug (Ped_Mafia_*) / crackhead lists.
- **Node pass** (thug1_port.ts NodePass.ownPed): every ped whose THUG1 profile converts gets `profile=THUG1_...`,
  its rig through `pedRigs.fixPedRigs(..., own)` (THUG1_MALE: THPS5_human + animload_THUG1_THPS5_human; THUG1_FEMALE:
  Ped_Female + animload_THUG1_Ped_Female + ReferenceChecksum), Extra_Anims -> our set. Others keep the old path
  (THUG2 profile on THPS6_human / Ped_Female). NJ 175, NY 69, VC 67 peds: all THUG1's own; 0 profiles failed.
- `--thug2-peds` (thug1_levels convert): THUG2's people as before.

## Shared files changed (small, additive)
- tools/level_port/thug2_scene.ts: the skin reader / writer (new functions only).
- tools/level_port/ped_rigs.ts: `Rig.reference`, `THUG1_MALE` / `THUG1_FEMALE` / `OWN_RIGS`, `fixPedRigs`'s optional
  4th parameter `own(profile, node)` (sets / removes ReferenceChecksum); `otherAnims` skips the own rigs' sets.
- tools/level_port/ped_rig_check.ts: knows THUG1_ profiles' rigs (`ownRig`) and checks their sets are loaded.
- tools/level_port/ped_models.ts (new), tools/level_port/ported_levels.ts (`installFiles` copies anims\ too),
  tools/menu_level/make_menu_level.ts (the additions; `anims` in the overlay's own folders).
- tools/thug1_levels: thug1.ts (`toThug2Skin`), thug1_peds.ts (new), thug1_port.ts, thug1_levels.ts (selfcheck,
  `--thug2-peds`, TPN-THUG1-3 load_level_anims left out with own peds).

## Checks
- (after the animation fix) `thug1_levels selfcheck`: 2,190 files, 0 failures.
- npm run typecheck / lint / test: 202 tests pass.
- `node tools/thug1_levels/thug1_levels.ts selfcheck`: 533 files, 0 failures (incl. the skins above).
- `node tools/level_port/ped_rig_check.ts <staging>`: NJ / NY / VC ok.

## In the game (workspace C:\Games\THUG2-4, NJ / NY / VC converted, own DLL build)
- Run 1: crash loading NJ - `Ass::CAsset::AddDependent` on a null group (CAssMan::LoadAsset's JoinGroup): the model's
  .tex.xbx wasn't found, so the skin's group asset was missing. Cause: the files were written as `<name>.skin.skin.xbx`
  (the mesh name's `.skin` not taken off). Fixed (thug1_peds.write). A missing ped texture crashes THUG2 - no fallback.
- Run 2: crash at 0x00482BA2 (FrameIndex on a null frame): `default` missing from the THPS5_human set (logged with a
  temporary line in CStandardAnimController's PlayAnim: "missing animation 1CA1FF20 in set AE0C6C7F" = default in
  THPS5_human). Cause: **NeverScript's `#xxxxxxxx` is the checksum's bytes in file order** (little-endian), but
  `qb.nameText` wrote the number's hex digits - `#1CA1FF20` compiled to 0x20FFA11C. Every name without a usable text
  (here `default`, a keyword) came out byte-swapped. Fixed in the shared `qb.nameText` (tests updated: `#C5787EE3` for
  `script`); tools/anteara's keyOf already used file order.
- Run 3: NJ loads and runs (no exception, no pool warnings); THUG1's models and animations are read from the overlay
  (log: models\thug1_peds\..., anims\thug1\...), but the people were **crumpled**: up close, Dealer02 was a heap of
  his shirt on the ground (from 60+ units it looked like a shadow only). Experiment: every THUG1 animation file
  replaced by THUG2's Ped_F_Idle1 (55 tracks, THUG2's encoding) -> full bodies standing. So the skins, skeleton,
  textures and profiles were right and THUG1's animation data was decoded wrongly.
- Cause: a .ska key may be an index into a **"standard key" table** the game loads at start-up (anims\standardkeyQ.bin /
  standardkeyT.bin in Bits.pre / Bits.prx: 256 x 8 bytes; read by THUG2's key decoders 0x00482C20 / 0x00482E70 from
  0x006F9AD8 / 0x006F92D8). THUG1's and THUG2's tables differ, so THUG1's table keys pick THUG2's values. Fix
  (`thug1.untableAnim`): every table key written out explicitly with THUG1's table values (rotation key: header
  without 0x4000 + 3 x u16; translation key: byte without 0x80 + 3 x u16), the per-bone stream sizes and the totals
  updated, partial animations' (flag 0x80000) bone bits kept. The format otherwise is THUG2's (both decoders read
  THUG1's flag set). Self-check: all 1,657 THUG1 object animations (anims\..., every .pre) convert and have no table
  keys left (the levels' camera paths, flags 0x1E000000, are another format - not converted).
- Run 4 (after the fix) - **THUG1's people on screen and animating** (captures, all at frame 1 unless noted; copies in
  the worktree's out\scripts\peds\captures):
  - NJ `nj_dealers_fix`: the three drug dealers by the porch (Dealer02 in the long coat, Dealer09 in the leaf shirt,
    Dealer10 hooded) - THUG1's models; frames 1 and 60 differ (the leaf-shirt dealer's arm goes from a wave to his
    head; 3.2% of pixels change over 60 frames with the camera still).
  - NY `ny_parking3`: THUG1's female NYPD parking officer (Ped_Female rig, THUG1_Ped_Female animations: hands on hips)
    next to the bum; `ny_guard3`: a THUG1 security guard.
  - VC `vc_clerk`: the female desk clerk (Ped_Female); `vc_doorman`: the doorman by the rink.
  - No exceptions, no `pool ... full` / `80% full` lines in NJ, NY or VC.
  - The peds stand at their nodes (Ped_InitAI Stand); the camera has to be within ~80 units to frame them well.
- The workspace's own THUG1 staging and overlay were moved aside during the runs and put back afterwards; THUG2-4's
  lock released (its DLL and ini put back).

## What's left
- THUG1: animals (dogs, the pigeons, Hawaii's shark) and SJ's MonsterPed crowd (later phase, the user's decision);
  levels other than NJ / NY / VC not run in the game.
- THPS3: done in phase 2 (below). THPS4 / THAW: not started (plan sections 4.2, 4.4).

## After merging main (0d1eb27: Vancouver limo / driving PC-THUG1-21 / 22, TP-42)
- Conflicts in make_menu_level.ts (the Anteara edits: the ped files' and car_scripts'), thug1_levels.ts / thug1_port.ts
  (portScripts gets both `wonGoals` and `made`): both sides kept. typecheck / lint / test (242 pass), thug1_levels
  selfcheck (2,190 files, 0 failures).
- VC re-converted into C:\Games\THUG2-3 (own build of the merged branch): the limo is there with its "Press ... to
  drive." prompt, and THUG1's female desk clerk stands animating - both survive the merge; no exceptions, no pool
  warnings. The workspace's own staging / overlay put back, lock released.

# Phase 2: THPS3's own people in the THPS3 port (2026-10-21)

Continued from a stopped agent's uncommitted work (its readers and converter draft, brought over and finished).
Tools only - no src changes (the temporary log lines used during the game runs were taken out again).

## The converter
- **Readers** (tools/thps3_levels/thps3_skin.ts): THPS3's `.SKN` (a RenderWare 3.1 clump: each frame's Neversoft bone
  chunk 0x108 gives its bone number, RenderWare's skin chunk 0x116 the per-vertex 4 bone numbers + 4 weights and per
  bone the inverse bind matrix; vertices in bone 0's z-up space) and `.ska` (u32, 0x80000000, duration, rotation /
  translation key counts, keys {quaternion or translation, time, the previous key's offset}; each track's first key
  at time 0, in bone order). THPS3's and THUG2's stored quaternions turn points the same way (checked against THUG2's
  own rest poses and THPS3's walk). All 176 men's (pedestrian_a) and 50 women's (Pedfem_a) animations parse.
  Writers: THUG2's `.ske.xbx` (ids, parents, mirrors, rest pose) and `.ska.xbx` in its plain key forms (flags
  0x06810000 - 15 of THUG2's own files use them; rotation: u16 frame at 60/s | 0x8000 when w < 0, 3 x i16 / 16384;
  translation: u8 0, u16 frame, 3 x i16 / 32 - checked against the decoders 0x00482C20 / 0x00482E70 and the loader
  0x00484200: no table keys, no custom keys).
- **Rig: THPS3's own, no retarget.** 14 bones for the men's set, 15 for the women's (no hands, no neck - on
  THPS6_human's 50 most bones would get nothing and THPS3's animations couldn't be used). One skeleton per set,
  `THPS3_<set>_skeleton`: the set's base model's bind pose, bone 0 turned y-up and raised by the set's ground height
  (the idle's frame 0 puts the base model's feet at 0: 37.85 men, 39.29 women); each model's vertices refitted bone by
  bone into that pose (the models of one set have different bind poses). The self-check poses every model by the
  written skeleton + animation the way THUG2 does and compares with THPS3's own skinning: within 0.046 units (30 men's
  models), 0.040 (6 women's).
- **Faces.** THPS3 draws its people double-sided and their strips wind both ways (about half of the triangles face
  inwards); THUG2 culls a skin's back faces (its own skins: all but a handful of triangles wound along their normals),
  so the first game run showed the guards with holes in their shirts and trousers. Every triangle is turned to face
  along its vertex normals and the strip joined again with degenerate-only joins (`outwardStrip`, `joinTriangles`);
  the self-check fails any written skin triangle wound against its normals.
- **Models / profiles**: `models\THPS3_Peds\<model>\<model>.skin/.tex/.col/.cas.xbx` (textures from the model's
  .tdx); profiles `THPS3_<model> = {ped_body={desc_id=THPS3_<model>}}`, parts `no_random`, added through peds.json
  (THUG2's entries untouched). A ped node whose THPS3 `model=` belongs to the men's or women's set gets that profile,
  `skeletonName=THPS3_<set>_skeleton`, `AnimName=animload_THPS3_<set>` and Ped_From_Profile / Ped_F_From_Profile;
  fixPedRigs leaves it alone (not on a THUG2 human skeleton); ped_rig_check knows `THPS3_` profiles and checks their
  set is loaded. Pros (PedPro_*: built from skater CAS parts on the skater rig) and crowds keep THUG2's stand-ins
  (THUG2's own PedPro_ models where THUG2 has the pro). `--thug2-peds`: THUG2's people everywhere, no peds.json /
  skeletons / animations (checked on Foundry and Suburbia).
- **Animations**: the level's `animload_THPS3_<set>` (called from load_level_anims): `LoadSkeleton` of the set's
  skeleton, then under `SetReferenceChecksum THPS3_<set>_skeleton` every animation THUG2's ped AI may play
  (thug1_peds.pedAiAnims) from THPS3's file of that name or role (ROLES: getup, fall, jump, push, clap, cheer, wave,
  disgust, talk, turn, idle / walk transitions, run, walk; else the idle), and `default` (the idle). Files:
  `anims\THPS3\<set>\<THPS3's name>.ska.xbx`; installFiles / make_menu_level carry `skeletons\` too.
  Foundry + Rio + Suburbia + Airport: 25 profiles, 25 models, 51 animation files, 2.6 MB.
- **The crash on the way**: the first run with own peds crashed at 0x00482BA2 (`default` missing from the set). The
  skeleton / reference was first named `THPS3_pedestrian_a` - the name of the set's base model's profile
  (`THPS3_Pedestrian_A`, a global structure), so the script's `SetReferenceChecksum THPS3_pedestrian_a` passed the
  global's contents instead of the name (the reference became 0x03CFCBA8 - seen with a temporary log line in
  CAssMan::LoadAnim) and every animation went in under the wrong key. Now `THPS3_<set>_skeleton` (a test keeps rig
  and profile names apart).

## Checks
- `node tools/thps3_levels/thps3_levels.ts selfcheck`: 329 files, 0 failures (pedestrian_a: 30 models, 176
  animations; pedfem_a: 6 models, 50 animations; no inward triangles). thug1 / thps4 / thaw selfchecks: 0 failures.
- npm run typecheck / lint / test: 260 tests pass (thps3_peds.test.ts: quaternions, the skeleton / animation writers
  read back, mirrors, joinTriangles, outwardStrip, rig names, the construction worker walking posed as THPS3 poses him).
- `node tools/level_port/ped_rig_check.ts <staging>`: Foundry, Rio, Suburbia, Airport ok.

## In the game (C:\Games\THUG2-3, own DLL build of this branch, Foundry / Rio / Suburbia / Airport converted)
- No exceptions, no pool warnings in the four levels (after the reference fix).
- Captures (copies in the worktree's out\captures):
  - Foundry `p3b_foun_booth_1/3`: the two security guards (Ped_Security_B) in the booth, front and back, solid
    (before the face fix: holes); `p3b_foun_welder_1/2`: the welder (Ped_Welder_A) with his mask and torch.
  - Suburbia `p3_sub_thinman_w1/w3`, `p3b_sub_thinman_1`: the Thin Man in his suit (waving, then hands behind his
    back); `p3b_sub_bbq_1..4`: the BBQ guy at his grill (chef's hat, apron), moving between frames.
  - Rio `p3b_rio_spect_1..3`: the spectators (Pedestrian_A-D) made with `run Create prefix=Ped_End_Spectator` (Rio's
    own people are all made by its competition scripts, none in free skate; its free-skate crowd is THUG2's
    stand-ins), standing and clapping.
  - Airport `p3b_ap_start_1/2`: three walkers at the start (Pedestrian_A, Pedestrian_B and a woman, Pedfem_F) walking
    with THPS3's walk; `p3b_ap_ticket_1/2`: the ticket agent (Pedfem_E, the women's rig) standing.
- Test positions: a ped stands at (x, y, -z) of its node's Position as the .qb holds it (found with a temporary
  automation command logging named objects' positions) - the first captures aimed at (x, y, z) and showed nothing.
- The workspace's own THPS3 staging and overlay (moved aside by the stopped agent into `.thps3peds-backup`) put back;
  THUG2-3's lock released (DLL and ini put back).

## What's left
- For a person to judge: the people in motion (the walk / idle mix THUG2's ped AI picks from THPS3's animations), the
  BBQ guy's apron flap; LOD (THPS3's `_LOD01-03.SKN` not used; the models are 480-640 triangles).
- THPS3's pros (PedPro_*), crowds (Crowd_*), animals: later (the user's decision).
- Shared bug seen, not changed: `thps3_levels.trisToStrip` (and THPS2's copy) doubles only the last vertex at a join,
  so between every two joined triangles it draws a stray one (c, a, b). Only THPS3 / THPS2 sectors stored as triangle
  lists use it; worth a fix with a game check of those levels.
- Useful for later ped tests: a permanent automation command logging where named objects are (the temporary one was
  `echo obj <names>`).

## Shared files changed (small, additive)
- tools/level_port/ported_levels.ts (`installFiles` copies `<staging>\skeletons` too), tools/menu_level/
  make_menu_level.ts (`skeletons` one of the overlay's own folders), tools/level_port/ped_rig_check.ts (THPS3_
  profiles' rigs), docs/level-port-compromises.md (THPS3's peds rows).
- tools/thps3_levels: thps3_skin.ts, thps3_peds.ts (+ test) new; thps3_levels.ts (`Port.peds`, the ped node's rig,
  `loadAnimsNs`'s own sets, `--thug2-peds`, the selfcheck).
