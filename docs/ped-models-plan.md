# Plan: the ports' own ped models (and animations)

Question (user): why do the ports put THUG2's people on the original games' ped positions, and why not import each game's
own models? Today (docs/level-port-compromises.md, "Peds: model - Stand-in") the converters pick a THUG2 profile per ped
(`tools/level_port/peds_cars.ts`, `ped_rigs.ts`, thug1_port.ts, thaw_scripts.ts, thps3_levels.ts). The reason is only that
nobody has written the skinned-model and skeleton/animation converters yet: all four converters read and write
*static* geometry, and `geometry.convertScene` drops every skinned sector ("skinned - left out"). Nothing in the ped
pipeline is blocked by the engine - THUG2 already builds a ped from a data profile, and THUGPRO has shipped converted
THPS3 peds for years (below). This is research only (read-only; nothing run in the game, nothing written outside this file).

Facts below were measured on the installed files (THUG1, THAW, THPS4, THPS3, THUG2, THUGPRO) unless marked "unverified".

## 1. How THUG2 builds a ped (what a new ped has to plug into)

- A level node is `Class=Pedestrian skeletonName=THPS6_human AnimName=animload_THPS6_human Type=Ped_From_Profile
  profile=<name>` (women: `Ped_F_From_Profile`, `Ped_Female`, `animload_ped_female`; ped_rigs.ts).
- `profile_ped.qb` (global structs) lists the profile's CAS parts: `{ ped_body={desc_id=...} }` or
  `{ ped_m_head / ped_m_torso / ped_m_legs / ped_m_accessories = {desc_id=...} }` (women: `ped_f_*`).
- `cas_ped_m.qb` / `cas_ped_f.qb` map each `desc_id` to a file: `{ desc_id=Ped_B_Head mesh="models/THUGPRO_Peds/Ped_B/Ped_B_Head.skin" no_random hidden not_in_multiplayer }`.
  The loader then reads `<mesh>.skin.xbx` + `.tex.xbx` + `.col.xbx` + `.cas.xbx` (the last a 12-byte stub in every shipped ped).
- The rig (skeleton + animation set) is chosen by the node, not by the model: only `THPS6_human` (50 bones) and `Ped_Female`
  (55) are `LoadSkeleton`ed for people (`scripts\game\skeleton.qb`). The skeleton files `THPS5_human.ske.xbx` and `Ped_F.ske.xbx`
  are shipped in THUG2's Data\skeletons but never loaded.

So a **new ped = new files (skin/tex/col/cas) + two QB additions (a `cas_ped_*` desc_id entry and a `profile_ped` global)** -
data only, delivered through the data overlay (`thug2-decomp-data`, tools/menu_level/make_menu_level.ts), which already merges
QB files. No decompiled code changes. Whether the overlay may replace `cas_ped_m.qb`/`profile_ped.qb` as whole files (as
THUGPRO's `thugpro_qb.prx` does) or only add to them is the first thing to confirm (it is the same question for any added
profile; the Anteara overlay already replaces scripts).

The vertex format a THUG2 skin needs (measured on `Ped_M_AU_Head01.skin.xbx`): scene header `1,1,1`; one sector with
`SEC_WEIGHTS` (0x10) set, one mesh, built-in vertex shader 3 (skinned), stride 40: position 3f, packed weights (u32,
11/11/10 bits), 4 x u16 bone indices, packed normal at +24, colour at +28, uv 2f at +32. `thug1.ts` already reads exactly
this weight/bone data from THUG1 sectors (`weights` u32 packed, `bones` 4H); `thps4.ts` reads THPS4's (4f weights, 4H bones).
**Missing everywhere: the writer for a skinned sector in `thug2_scene.ts`'s mesh form** (shared by all four games, item A).

## 2. What THUGPRO does (evidence)

- **THUGPRO ships THPS3's own ped models, not THAW's.** The user's premise (THAW peds on THPS6_human) did not check out: I
  listed every `.prx` in `%LOCALAPPDATA%\THUG Pro\data\pre` and `models\`. No THAW ped skin exists there (THAW levels'
  peds - Kyoto's 10 etc. - carry no ped models in their prx; they use THUG2/THUGPRO profiles). What *is* there:
  `models\THUGPRO_Peds\<ped>\<part>.skin.xbx/.tex.xbx/.col.xbx/.cas.xbx` inside `FOUN_/OIL_/RIO_/SHP_/SI_/SUB_scripts.prx`
  (THPS3 levels): ped_Constr_A/B, ped_welder_A/B (head/torso/legs/mask/hat), ped_security_b, ped_B/C/D (head/torso/legs),
  pedFem_A/B/C/Neversoft, ped_bully, ped_BBQ_Guy, ped_thinman (+axe), ped_richkid (+specs). Names and sizes match THPS3's
  `Data\Models\Ped_Constr_A\ped_Constr_A.SKN` etc. (35 KB RW -> 43 KB THUG2 skin). Many are split into head/torso/legs CAS parts.
- Their nodes (decompiled `levels\sub\sub.qb`): `Class=Pedestrian skeletonName=THPS6_human AnimName=animload_THPS6_human
  Type=Pedestrian_B Profile=Pedestrian_B` - i.e. THPS3 models **rigged to THUG2's 50-bone THPS6_human** and driven by THUG2's
  animations. Profiles are in THUGPRO's `thugpro_qb.prx\qb\profile_ped.qb` (`pedestrian_b = { ped_m_head={desc_id=Ped_B_Head} ped_m_torso=... ped_m_legs=... }`),
  the desc_ids in its `qb\cas_ped_m.qb`.
- THUGPRO also retargets THUG1's animations: `thugpro_anims.prx` `anims\thug1\...\*.ska.xbx` have **50 tracks** (header bone count
  0x32) while THUG1's own skater/ped anims have 55 (0x37, THPS5_human). So it moved 55-bone THUG1 animation to THPS6_human.
- Lesson: the retarget-to-THPS6_human route is proven for THPS3 models and for 55 -> 50 animation. Its cost is quality (bones
  without a counterpart are folded away; local axes/proportions differ), which is why THUGPRO's source rigs were authored for it.
  THUGPRO's code/data is reference only (rewrite, name TP-n only if it is compat data - it is not: this is our own port content).

## 3. Skeleton compatibility (measured)

Bone-name checksums, parent arrays (`.ske` = `u32 2, 0, bone count`, then ids, parent ids, ...; 44 bytes/bone in THUG1/THUG2,
THPS4's older file 12 bytes/bone - no rest pose).

| Skeleton | Bones | Relation |
|---|---|---|
| THUG2 `THPS6_human` | 50 | 30 names known (Bone_*, Cloth_*); head chain 8-12, hands off the shoulders |
| THUG2 `Ped_Female` | 55 | first 15 bones equal THPS6_human, then 5 more bones (Jaw, Brow, PonyTail_2, 2 shoulder-twist) |
| THUG1 `THPS5_human` | 55 | **byte-identical to THUG2's `THPS5_human.ske.xbx`**, and the same 55 names as Ped_Female (only 10 bones in a different index order: Ankle/Toe/leg bones 40-49); 47 of 55 names exist in THPS6_human |
| THUG1 `human`, `Ped_F` (old) | 50 | **same ids and same parent array as THPS4's `human.ske`** - THPS4's rig, names not in THUG2's list; hierarchy differs from THPS6_human (different bone order/branching). THUG1's `Ped_F.ske.xbx` is byte-identical to THUG2's file |
| THPS4 `human` / `Ped_F` | 50 / 50 | the old rig above; `.ske` 608 bytes: ids + parents + 4 B/bone only |
| THAW | ? | `.ske` found only inside the cutscene (`IGC_*`) paks: GH3-family memory image, 1776 B; 7 of THPS6_human's 50 ids occur in it (names partly shared, rig clearly different); the in-game ped skeleton not yet located |
| THPS3 | per model set | no `.ske`: bones live in the RenderWare skin/HAnim chunks of each `.SKN`; `.ska` headers show different per-set track counts (unverified) |

Consequences:
1. **THUG1 needs no retargeting at all**: load THUG2's own `THPS5_human.ske.xbx` as a skeleton (`LoadSkeleton ... THPS5_human animName=animload_THPS5_human`
   in the overlay's skeleton.qb) and ship THUG1's `anims.pre` `.ska.xbx` (already THUG2's format; 55 tracks) under an `animload_THPS5_human` list.
   Skin bone indices are valid as they are. THUG2's ped AI then plays THUG1's animations on THUG1's models; THUG1-only animations
   (drunk, bum, drug dealer...) work because they come with their rig. Fallback (THUGPRO's way): remap 55 -> 50 by name.
2. THPS4's rig is a different family from both: either **ship it as its own skeleton + converted animations** (needs a rest pose,
   not in its `.ske`) or **retarget bone-by-bone onto THPS6_human** (hand-made 50 -> 50 map by anatomy; then the models also work with all THUG2 animations).
3. THAW and THPS3: retargeting onto THPS6_human (THUGPRO's proven route) is the practical path for models; their own
   animations need new converters (compressed GH3 keys / RenderWare `.ska`).

## 4. Per game

Sizes: small = days of a single agent, medium = a week-ish, large = several batches. Order = expected ease.

### 4.1 THUG1 (small to medium) - first
- Files: `Data\models\Peds\<ped>\*.skin/.tex/.col/.cas` (64 folders, 130 skins), `Ped_Male`/`Ped_Female` CAS parts (96 skins);
  also inside the `*ped.pre` level archives (198 distinct skins, `.xbx` names). Animations `anims.pre` (+ per-level), skeletons `skeletons.pre`.
  THUG1's own `profile_ped.qb`/`cas_ped_*.qb` (qb.pre) give profile -> parts.
- Format: scene header and tex/col same family as THUG2; `.skin` sector layout is THUG1's (thug1.ts reads positions, normals, weights, bones, uvs).
- Work: (A) skinned-sector writer; (B) texture/collision files through the existing THUG1 converters; (C) CAS desc + profile
  entries generated from THUG1's profile_ped/cas tables (all 130 profiles, replacing the 80/51/43... random substitutions); (D) skeleton
  load + animation set for THPS5_human; (E) node pass: stop mapping THPS5 -> THPS6 (thug1_port.ts:443); (F) animals/crowds later (MonsterPed, dogs: skeleton files exist in THUG1 and THUG2).
- Risks: ped AI scripts of THUG2 may name animations THUG1 lacks (the existing "animation the ped's set hasn't" crash, 0x00482BA0) - generate
  the set from the intersection; peds with CAS accessory parts; Ped_Female vs THPS5_human index swap (10 bones) if the
  Ped_Female rig is reused instead (not needed with plan 1).

### 4.2 THPS4 (medium)
- Files: `Game\data\models\Peds\Ped_*\Ped_*{skin,tex,col,cas,usg}.dat` (10 folders + 15 PedPros), `Ped_m`/`Ped_f` CAS parts (80 skins),
  `Game\data\skeletons\human.ske`, `Ped_F.ske`, `anims\Ped_M\*ska.dat` (97 files) and per-type folders (Ped_Cop, Ped_Kid, Ped_Mafia...).
- Format: skin.dat = scene family (thps4.readScene reads weights 4f + bones 4H); `ska.dat` header = THUG2's (`1, flags, duration, 0x32 tracks`), compression flags differ.
- Work: (A) writer (weights 4f -> packed 11/11/10); (B) texture/col via the THPS4 converter; (C) rig decision: own skeleton needs `.ske.xbx` with
  rest pose (derive from the first key of an animation or the skin; unverified) - or the 50 -> 50 retarget; (D) `.ska.dat` -> `.ska.xbx` converter
  (flags + bone-order remap + conjugation if local axes differ); (E) profiles from THPS4's ped scripts (Pedestrian_* types); pros already have models in THUG2 (PedPro_*) - keep THUG2's only if the user prefers.
- Risks: unnamed bone ids (names unknown; need the THPS4 name list from its scripts or Morten's dumps for understanding only);
  rest pose; 50 -> 50 semantic mapping. Animals (anl_* skeletons) are separate (compromises table).

### 4.3 THPS3 (medium to large; evidence it works: THUGPRO)
- Files: `Data\Models\<Ped|Pedestrian_A-D|Pedfem_A-F|PedPro_*|Crowd_*>\*.SKN` (+ `_LOD01-03.SKN`, `.tdx`) - 295 skins, ~55 ped folders;
  `Data\Anims\<set>\<set>_<anim>.ska` (pedestrian_a 176, Pedfem_a 50, crowd, terrorist...).
- Format: RenderWare 3.1 clump with skin plugin + HAnim hierarchy + Neversoft extensions; `thps3.ts` already parses RW streams (clumps, geometry, materials, `.tdx`) but not the skin plugin (vertex bone indices/weights, inverse bind matrices) or HAnim.
- Work: (A) writer; (B) RW skin + HAnim reader (bone indices, weights, hierarchy); (C) retarget bones onto THPS6_human / Ped_Female by hierarchy (as THUGPRO did; a map table per source skeleton); (D) `.tdx` textures through existing code; (E) head/torso/legs split optional (THUGPRO did it for some; not needed - use `ped_body`); (F) animations: either retarget THPS3's `.ska` (keys per bone) to 50 tracks or keep THUG2's (crowds/specials like BBQ, Darth Maul dance need their own).
- Risks: scale/orientation of RW skins (Y-up, units), bone-count variance per ped set, LOD files, per-bone local-axis conventions.

### 4.4 THAW (large)
- Files: `Game\data\models\peds\Ped_*\ped_*.skin.wpc/.tex.wpc/.col.wpc/.usg.wpc/.nav.wpc` (59 folders, 167 skins; most also have `.tex.zip.wpc`);
  skeleton(s) in paks (`.ske` seen only in `pak\IGC\*`); animations `.ska` in `perm_anims`, `net_anims`, `unloadable_anims`, `bmx_anims`
  (464 + 816 + 833 + 234 files, names unresolved via dbg), compressed with `data\anims\standardkeyQ.bin` / `standardkeyT.bin`.
- Format: `.skin.wpc` = same memory image family as `.geom` (header `0, 7 x 0xCABAAAFA`, FVF vertex buffers) - `thaw.readGeom` reads `.geom`; extend for skinned FVF. `.ske` and `.ska` are GH3-family images with a new compressed key format; nothing parsed yet.
- Work: (A) writer; (B) skinned `.geom`/`.skin` reader; (C) skeleton reader + name-based retarget table (bone names partly equal to THUG2's) onto THPS6_human / Ped_Female; (D) textures (`readTex`, exists); (E) profiles from THAW's own ped scripts; (F) animations: a GH3 `.ska` key decoder (large, can be deferred - THUG2's ped animations drive the retargeted models).
- Risks: skeleton not located; hat/accessory CAS pieces in `skaterparts`; the `.tex.zip.wpc` variants; bone-count/rig differences larger than 50 -> 50.

## 5. Reusable across games

| Piece | Used by |
|---|---|
| A. `thug2_scene.ts` skinned-sector writer (stride-40 vertex, packed weights, shader 3, `SEC_WEIGHTS`) + a "bone remap" hook (index -> index, weights merged for folded bones) | all four |
| B. Ped asset emitter: write `models/<Game>_Peds/<name>/<name>.skin/.tex/.col/.cas.xbx` into the overlay; `cas_ped_m/f` desc_id + `profile_ped` generation (merge, not replace); a `thps4/thaw/thps3 -> THUG2 profile` table | all four |
| C. Skeleton tooling: `.ske.xbx` reader/writer (ids, parents, 36 B rest data), name-based retarget table builder, bone-map files as data (not constants in code) | THPS4, THAW, THPS3, (THUG1 fallback) |
| D. `.ska.xbx` reader/writer + track remap | THUG1 (none needed), THPS4, THPS3, THAW |
| E. Node pass: `ped_rigs.ts` extended so a profile owned by a ported game keeps its own rig; stop the "random THUG2 profile" fallback | all four |
| F. Verification: `ped_rig_check.ts`-style offline checks + a model viewer pass (viewer exists in the scratch work) | all four |

## 6. Recommended order and first step

1. **A + B + THUG1** (the writer is the shared prerequisite; THUG1 needs it plus the least else: the same skeleton family and animation format as THUG2).
   Order inside: writer + round-trip test against THUG2's own `Ped_M_AU_Head01.skin.xbx` (read -> write byte-identical) -> convert one THUG1 ped
   (e.g. `Ped_Bartender`) -> overlay it with a profile -> one level, one ped, in a workspace.
2. **THPS3** (proven by THUGPRO; once the RW skin reader exists the retarget table is the work), then **THPS4** (own skeleton or 50 -> 50 map), **THAW** last (reader, skeleton and animation work all new).
   THPS4 vs THPS3 can swap if the user prefers: THPS4's reader and header-compatible animations exist; THPS3 has the most peds per level.
3. Animations of the originals (THUG1 immediately, others as retargeted or converted sets) come after the models look right.

**First step (one agent, small):** add `writeSkinnedSector` to `tools/level_port/thug2_scene.ts` (+ Python twin rule: the module has a `.ts` twin policy - check `docs/typescript.md`), with a selfcheck that re-writes
THUG2's `Ped_M_AU_Head01.skin.xbx` from its parsed data and round-trips byte-identical, then a `thug1_levels` subcommand that converts one THUG1 ped to a THUG2 skin and prints its bone indices against THPS5_human.
Open decisions for the user: (a) overlay may replace/merge `cas_ped_m.qb` and `profile_ped.qb` (confirm in the overlay maker); (b) keep THUG2's skater-pro ped models for the pros or use each game's; (c) animals/crowds in a later phase.

## 7. Not verified / to check before the work starts
- Whether THUG2's engine accepts a 55-bone `THPS5_human` skeleton for a *Pedestrian* node end-to-end (Ped_Female proves 55 works; THPS5_human is the same name set).
- THPS4 rest-pose source; THPS3 per-set bone counts; THAW's ped skeleton location and bone names (needs a `.ske` reader on `pak\IGC\*` first).
- The overlay's handling of two models using the same `desc_id` (names are per-game prefixed in THUGPRO: `THUGPRO_Peds`; ours would be `<Game>_Peds`).
