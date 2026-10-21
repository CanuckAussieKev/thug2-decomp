# THPS3 Rio (rio) crash after loading - 2026-10-21

**Symptom:** with main's DLL (out\build\lead, C:\Games\THUG2-2) Rio crashed ~76 s after start: 0xC0000005 reading
CCCCCCD0 in `Script::CStruct::ExpandInto` (struct.cpp:1223, `this` = 0xCCCCCCCC), called from 0x00488F82 (inside
0x00488F40, the profile / appearance loader) <- 0x0042270C. Reproduced once, same registers as in ported-movers.md.

**Cause: the converted data (a THUGPRO-new ped), not our rewrite.**
- 0x0042270C is in the model component's member function `Obj_InitModelFromProfile` (0x98ED5C8B): it calls
  GetStructure(`struct` 0x456D28D1, &local, 1) at 0x004226D6 **without initialising the local**, then passes it to
  0x00488F40, which calls ExpandInto on it. GetStructure (0x004767C0, original and ours alike) writes nothing when the
  parameter isn't a structure. The 0xCC fill is our debug build's stack; the original reads whatever is there - it
  crashes or corrupts the same way. Our rewrites are equivalent; nothing to fix in C++.
- THUG2's `ped_init_model` (ped.qb) runs `Obj_InitModelFromProfile struct=<profile> ...` for a ped with `profile`.
- Rio's `TRG_Ped_2084` is THUGPRO's own ped (TPN-THPS3-2 node, from THUGPRO's RIO_scripts.prx) with
  `profile=random_sk3_male_profile` - a THUGPRO profile THUG2's scripts\profile_ped.qb doesn't define, so `struct=`
  carries a plain name and the ped's creation crashes. Every other converted level in the workspace (THPS3, THPS4,
  THUG1, THAW overlays) was scanned for profiles THUG2 hasn't: this is the only one.

**Fix (converter):**
- `tools/level_port/nodes.ts` `fixProfiles(nodes, names, known)` (shared, new): a node whose `profile` THUG2 doesn't
  define becomes THUG2's `random_female_profile` (Type Ped_F_From_Profile) or `random_male_profile`; returns what it
  changed.
- `tools/thps3_levels/thps3_levels.ts` convertNodeArray: after THUGPRO's nodes are applied (and PC-THPS3-15), with a
  Port, runs fixProfiles against Thug2Assets' profiles; the report says
  `ped profiles THUG2 hasn't, made THUG2's: TRG_Ped_2084: random_sk3_male_profile -> random_male_profile`.
  The register's CRC stays valid (it's of THUGPRO's bytes, checked before the change), so no re-extraction.
- Test: level_port.test.ts "fixProfiles" (unknown male / female, a known pro kept, a node without a profile).
- typecheck, lint, npm test (153 pass), thps3_levels selfcheck (323 files, 0 failures). No C++ change.

**Verified** (C:\Games\THUG2-2 under its lock, main's DLL out\build\lead\dinput8.dll, all of THPS3 reconverted from
this branch + make_menu_level): load_check rio (150 s: free skate in Rio, picture taken, running and responding, no
EXCEPTION), foun / tok / sub (120 s each: running, responding, loaded, no errors).

**The lead must reconvert THPS3** (`node tools/thps3_levels/thps3_levels.ts convert` - all, so levels.json stays
whole; only Rio's output changes) and run `node tools/menu_level/make_menu_level.ts` for the main install.
