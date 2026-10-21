# apply_layout.ts: stretched scenery, object scale, exit triggers baked (the editor plan's bake, part 3)

Branch `worktree-agent-a8ae15258c5121c7d` (off main dcfce4d). TypeScript only - no DLL change. **NOT run in the game.**
Checked offline: `npm run typecheck / lint / test` (127 tests pass, 7 new), and a dry run plus a real bake of Training
from `C:\Games\THUG2\Game` (read only; output in the worktree's `out\baked_test`): the node .qb decompiles with ns.exe,
the collision file reads back.

What the DLL now writes (editor-scale2: `scale=x,y,z` on sector / rail / clone records, a trailing `scale=` on object /
copy records; editor-polish: `exit=1` on trigger records) is read and baked by `tools/game/layout_file.ts` and
`tools/game/apply_layout.ts`.

## 1. Per-axis scale on sector / rail / clone records

- `layout_file.ts`: `Placement.scale` is now a `V3` (as level_editor.h's). `readScaleToken` is Scale::ReadToken ported:
  `scale=s` (uniform) or exactly `scale=x,y,z`, each a float above 0; anything else (`scale=1,2`, `0`, `-1`, four
  numbers, text) refuses the file with its line, as the DLL. Other key=value words after the numbers are skipped (a later
  version's), as ReadScaleFields.
- `apply_layout.ts`: `applyPlacement` = `QuarterTurn(PerAxis(x, scale), quarters) + offset` - render vertices, collision
  vertices (box, BSP tree and reaches made again, fixed-point kept only while it fits), shared-collision companions, rail
  points (their `rail` records carry their own placement), clones, all through it.
- Normals: `scaledNormal` = level_editor_scale.cpp's ScaledNormal (the inverse transpose `n / scale`, made unit length,
  then turned; unscaled: only turned - not normalised again; a zero normal stays zero), at `normalOffset` = NormalOffset
  ported (an FVF below 0x10000 with plain XYZ positions (`fvf & 0x400E == 2`) and NORMAL, stride >= 24 -> +12). Before,
  any FVF with the XYZ bit and NORMAL had its +12 turned (an XYZB format's would have been wrong).
- A sector without vertices: its sphere's radius times the largest axis (as Rebuild).
- Test: a triangle whose vertex normals are its face normal, stretched (2, 3, 4) and turned: the written normals are the
  placed triangle's own normal (1e-6); collision, companion, rail point, a stretched clone checked.

## 2. Object / copy `scale=` - bakeable for the model, not for the collision

**The game reads a node's `scale`.** The model component's InitFromStructure (0x004214F0, vtable slot at 0x00646130) ends
with InitModel (0x00420D10; also the member `Obj_InitModel` 0x4D8E11AB via 0x00421AE0), which calls 0x00487B00 on the
structure: `scale` as a float (uniform: x = y = z, w 1), else as a vector, else `x=` / `y=` / `z=` floats - and if any
is there, `CModel::SetScale` (0x00492EE0) on the component's model (+0x208): the same call the editor scales objects'
models with. (The member `Obj_ApplyScaling` 0x657FF1ED uses the same reader.) The structure is the node's fields:
`gameobj_add_components` (ped.qb) runs on the node's params and creates the model component with
`CreateComponentFromStructure component=model <...>` (every LevelObject; other game objects with a model=) and
`bouncyobj_add_components` (skutils.qb) the same way; CCompositeObject::CreateComponentFromStructure merges them in.
No shipped THUG2 level node has a `scale` (checked: every loose Data\levels\*\*.qb node array).

So the bake writes the record's scale into the object's node as **`scale`** - a float when uniform, a vector
`(x, y, z)` when stretched - each axis clamped to 0.25 .. 4 (the editor's Clamp; its compressed-collision limit can't
be known offline). 1, 1, 1: nothing written (the editor leaves an object it never scaled as loaded). A copy gets its
record's scale; a copy without one takes its source's fields **as loaded** (a scale baked into the source this run is
taken back out of the copy - the DLL's copy node is made from the loaded node). Baked twice: replaced, not doubled.
`scale` goes into the name table.

**Not bakeable: the object's collision.** The collision component (0x0041C7B0 InitFromStructure / 0x0041C890
InitCollision, decompiled in collision_component.cpp) reads no scale: its triangles are the model's asset collision
(shared by every object of that model) or a LevelObject's sector's, used at their own size. The editor clones and scales
them at run time (Scale::SetObjectScale); nothing in a level file does that. Each scaled object / copy gets the warning
"its collision isn't - ... it collides at its loaded size in the baked level". (A bouncy object's rigid body's
`contacts=` aren't scaled either.)

## 3. Trigger `exit=1` - bakeable; editor-bake2's in-and-out claim was wrong

editor-bake2.md (and triggers.ts's header) said a baked static box fires on the way in **and out**. The decompiled code
says otherwise: the trigger feeler's static test, `CCollStatic::CollideWithLine` (0x00413B50), takes a face only through
`RayHitsTriangle` (0x0040B420), which refuses a determinant below 0.03 - i.e. a line crossing a face from behind (along
its normal `(v1 - v0) x (v2 - v0)`). The baked box's faces (THUG2's own trigger boxes' winding, Boston's
TRGP_ShoppingCart) point **outwards**: a line entering meets one face, a line leaving meets none. So a plain baked
trigger fires **on the way in only - as the DLL's movable box** (exit=0 needs nothing; it was already right).

`exit=1` (the DLL fires the node's TriggerScript again on leaving, emulated by a per-frame test in Triggers::Play) is now
baked: the trigger's collision object gets the 12 faces **again, wound the other way** (facing in, same flags 0x1150):
a line leaving the box meets one of those, and the feeler's callback runs the same node's TriggerScript - entering and
leaving each fire once. `readTriggerRecord` reads `exit=` as the DLL (0 / 1, once; a bad or repeated one refuses the
record). The report says "also on leaving it". A test ports RayHitsTriangle's sidedness and checks five lines in and
out of a turned box: plain - in 1 face, out 0; exit - in 1, out 1.

Differences from the DLL's emulation that remain: the DLL only fires on leaving for the local skater, not in a network
session / career-type modes, not while editing; a baked box's faces fire for any object with a trigger component
(what fires for the level's own triggers), in any mode (as every baked trigger).

## 4. Every record and key the DLL writes

Checked the writers (level_editor_layout.cpp Write, LevelProps::WriteRecord, Triggers::Record, Fields::Token,
Chains::WriteRecords, Walls' records): the only keys the reader refused were per-axis `scale=` (sector / rail / clone)
and nothing else - object / copy `scale=` was silently ignored (now read), trigger `exit=` skipped (now read). A new test
feeds one line of every record kind with every key the DLL writes (scene, level, sector x2, rail, clone, object, copy x2,
node (light / restart keys), order crown, hide, lock object / sector, trigger x3 (all keys, exit, water), set (every
field value type and del), point (rail / ladder keys), links (- and a list), drop, terrain, facing, grindflat, wall),
with the `# name` comments: no unknown records, every value read.

## Files (tools only)
- `tools/game/layout_file.ts`: Placement.scale V3, `UNIT_SCALE` / `isUnitScale` / `isUniformScale`, `readScaleToken`,
  objects / copies `scale`.
- `tools/game/apply_layout.ts`: per-axis `applyPlacement`, `scaledNormal`, `normalOffset`, `objectScaleValue` /
  object & copy scale baked, exit report, describeValue of vectors / floats, header comments.
- `tools/game/triggers.ts`: `exit` in TriggerSpec / readTriggerRecord, the reversed faces, the corrected header comment.
- Tests: apply_layout.test.ts (5 new), triggers.test.ts (1 new); existing ones adapted to V3 scales.

## Still not bakeable
- An object's / copy's **collision scale** (above): warned per object.
- The level's start restart (`level start=`) - as editor-bake2.
- The DLL's exit emulation's limits (local skater, offline, not editing) - the baked faces fire for whatever triggers
  the level's own boxes.

## Checklist for the user (NOT run in the game)
Workspace with the overlay made; a saved layout on a level (e.g. Training) with: a piece stretched with Alt+] (or the
Inspector, Uniform off), an object scaled with ] and another stretched, a "Run a script" trigger with **Also on leaving
it** ticked, and one without. Move the layout file away after baking (as editor-bake2).
1. `node tools/game/apply_layout.ts --level TR --game C:\Games\THUG2-n --dry-run`: no "bad scale=" refusal; lines
   "object <name>: scale=(x, y, z) (the model drawn scaled)", the collision warning per object, "trigger ...: ... -
   also on leaving it".
2. `--install`, load Training as shipped: the stretched piece has the editor's shape (skate it: collision matches) and
   its lighting looks like the editor's (no dark / bright faces from wrong normals).
3. The scaled objects are drawn at their scale; bump into them: they collide at their original size (expected - the
   warning).
4. The exit trigger: its script runs on entering **and** on leaving the box (once each); the plain one only on
   entering.
