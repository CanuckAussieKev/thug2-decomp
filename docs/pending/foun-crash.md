# Foundry (foun) load crash - 2026-10-21

**Symptom:** the main install (DLL main 20ebef7) crashed loading THPS3 Foundry from the level select: 0xC0000005
reading 00000004 in `Obj::CRailManager::AddFromNodeArray` (rail_manager.cpp:854, `nodes->m_size` with nodes null),
called from the original's 0x0042B880 (a car's railmanager component init) <- CreateComponentFromStructure <-
car_add_components <- `Obj::InitCar` <- `Obj::CreateCar`.

**Cause: the converted data, not our rewrite.**
- The original 0x0057E960 has no null check either (`mov edi,[esp+0x34]; mov [ebp+0x44],edi; ... cmp [edi+4],ebx`):
  with a null array it faults at the same instruction. Our rewrite (batch VW-6, 652b6ea) is equivalent; nothing to fix.
- 0x0042B880 gets the array from the object's nodearray component (type 0xC472ECC5, vtable 0x0064628C, +0x34 =
  0x005480F0 `return this->+0x18`). That component's InitFromStructure (0x00427F20) takes the `model` string, puts
  `.qb` at its first `.`, prefixes `models\` and loads it with LoadOrGetAsset (0x004087B0) as a node array; a missing
  file gives null.
- THUG2's `car_add_components` (ped.qb) gives every car a nodearray and (unless NoRail) a railmanager component. Every
  THUG2 vehicle model ships its node array (models\veh\Veh_Focus\Veh_Focus.qb ...).
- Foundry's Obj_Bucket is made a Vehicle with model `Obj_Bucket\Obj_Bucket.mdl` by the THUGPRO-new register
  (TPN-THPS3-3 "set"); its model files come from THUGPRO's FOUN_scripts.prx (TPN-THPS3-6). The extractor
  (tools/thps3_levels/thugpro_new_extract.ts) took every file of the model folder **except `.qb`** - so
  models\Obj_Bucket\Obj_Bucket.qb (THUGPRO's node array for it) was never installed, and the bucket's rail manager
  got a null array.
- The same gap affected Subway (icecreamtruck, light_airplane01, CarPlantNode), Tokyo (bullet_train, plane_jet) and
  Rio (CarPlantNode, CarPlantNode_Bus): all would crash the same way when their Vehicle is created.

**Fix (converter / register):**
- thugpro_new_extract.ts: a model entry takes the model folder's `.qb` too; a THUGPRO `set` that makes a THPS3 object
  a Vehicle is now left out (listed in `_left_out`) when neither THUGPRO's archive nor THUG2 has the model's node array
  (the guard against this crash for any future entry).
- tools/compat/thugpro/thps3_levels.json regenerated (`thugpro_compare.ts --extract-thugpro ... --thug2
  C:\Games\THUG2-1`): the only change is the 8 model entries gaining their `.qb` (Obj_Bucket, CarPlantNode x2,
  CarPlantNode_Bus, icecreamtruck, light_airplane01, bullet_train, plane_jet).
- Some of those THUGPRO model .qb files have small trigger scripts calling THUGPRO-only names (e.g. the bucket's
  kill-skater trigger): THUG2's runtime prints "script ... not found" and goes on (docs/script-runtime.md), harmless.

**The lead must reconvert main:** `node tools/thps3_levels/thps3_levels.ts convert Foun Sub Tok Rio` (or all) and
`node tools/menu_level/make_menu_level.ts` for C:\Games\THUG2.

**Verified:** typecheck, lint, npm test (149 pass); build (no C++ change); thug2_check on THUG2PM.exe and
C:\Games\THUG2\Game\THUG2.exe: 503 early self-tests pass; check_code_writes: no overlaps. One load check in
C:\Games\THUG2-1 (under the lock; Foun reconverted, overlay rebuilt, this branch's DLL): `-level foun` reached free
skate in Foundry (status where=level level=foun), `models\obj_bucket\obj_bucket.qb` read from the overlay, no
EXCEPTION in the log. Sub / Tok / Rio not run in the game.
