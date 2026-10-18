# collision-debug (agent record)

NOT ORIGINAL debug feature: `thug2-decomp.ini [features] debug_collision=1` (default 0) logs what collision
feelers hit - for the invisible wall at THAW The Mall's entrance glass (Z_MA_Sk_JOW_Mall_Entrance04).
Source: `src/game/skater/debug_collision.cpp` (the full findings are in its header comment).

- Hook: 0x004172F0 (the feeler's GetCollision(movables, reverse), thiscall, RET 8), wrapped as a
  REPLACE_FALLBACK (not counted as rewritten; steps aside once 0x004172F0 gets source). The original runs, then a
  hit is logged. `debug_collision=1`: hits asked by skater code (callers 0x004F8000-0x00534000) that aren't floors
  (normal y <= 0.7); `=2`: every hit. At most once per 2 s per distinct (collision object/object, face), 8 lines/s.
- Line: `collision: sector <name> (<checksum>) obj flags <f> face <i> flags <face flags> terrain <t> at (point)
  normal (n) line (start) -> (end) [skater (pos)] caller <return address>`; `object <name/id>` for a movable
  (composite object) hit.

## Findings (Ghidra, THUG2PM.exe)
- Face test (0x004131F0, every accepted face): flags from the collision object's vtable +0x08 (CCollStatic:
  0x00410BF0, the u16 straight from the .col face array); skipped if `flags & ignore_1` or `~flags & ignore_0`.
- Feeler constructors (0x00417760, 0x00417250) set ignore_1 = 0x0010, ignore_0 = 0: **0x10 faces are skipped** by
  default. The skater's feeler check 0x005003A0 gets the masks from its callers - all readable ones pass (0x10, 0).
  Other SetIgnore (0x004170F0) users: (0x810,0), (0x90,0), (0x3000,0) - 0x10 not ignored but 0x1000 is -,
  (0,0x20), (0,0x50).
- Trigger faces (0x40) get no special handling in the query; only `flags & 0x40` is noted in the result (+0x49).
  A 0x1150 face can't be hit by any of the skater's feelers.
- Collision objects have flags at +0x08: 2 / 4 (4 = killed). Kill on a LevelGeometry node (0x005A32C0 ->
  0x0049E9B0(false)) sets 4 on the sector's collision object (sector +0x18). The world query (0x004188C0) skips
  2|4 objects, and so does filling the collision cache (0x0040ABA0) - but querying the cache (0x004106C0) doesn't
  re-check them: a sector killed after the cache was filled stays collidable through it until the next refill
  (face flags still apply).
- Shatter (0x005A5090) on LevelGeometry: invisible (0x005A3B80) + the sector's shatter (vtable +0x04); no
  collision change.
- So if the log shows nothing at the glass, the stop isn't a feeler hit (e.g. a movable object would show as
  `object ...`).

Built and checked (verify build, thug2_check). Load check in the game: see the commit / report. The wall itself
NOT tested in the game yet (needs the player).
