# thps2 - Tony Hawk's Pro Skater 2's ten levels in THUG2 (phase 1)

Agent "thps2", branch worktree-agent-af1bef8bfb5fcc71c (continuing a stopped agent's start: its uncommitted
tools/thps2_levels/pkr.ts and thps2.ts, reviewed - layouts checked against the data, the restart / rail notes and the
transparency fixed). Workspace used: C:\Games\THUG2-3 (under its lock). NOT run by a person yet.

User decisions (2026-10-21): all ten THPS2 levels from THPS2's own data (C:\Games\THPS2\ALL.PKR) - Hangar, School II,
Venice Beach and Philadelphia too, never THUG1's or THUGPRO's remakes - as their own entries (thps2_<name>) in a
"THPS2" level select section, beside THUG1's remakes in the THUG1 section.

## What was made

- `tools/thps2_levels/pkr.ts` - ALL.PKR (PKR2: folders, files, stored or zlib) - reader and a writer for tests.
- `tools/thps2_levels/thps2.ts` - .psx worlds / texture libraries, .trg nodes (restarts, rail points); the formats in
  its header.
- `tools/thps2_levels/thps2_levels.ts` - `convert [levels] --thug2 <game> --thps2 <thps2>`, `selfcheck`, `list`
  (README.md there). Staging `thug2-decomp-thps2`, levels `thps2_hangar` ... `thps2_skateheaven`, level select numbers
  100-109.
- `thps2_levels.test.ts` - 14 tests on synthetic files (PKR round trip, a world with a textured gouraud quad and an
  invisible trigger, a texture library with magenta / 0x0000, a .trg with aligned restart / rail data, the converter's
  space, winding, uvs, colours, collision flags, textures with a BMP, restart kinds, the node array).
- Shared files (small, additive): `tools/lib/install.ts` + `tools/game/game_exe.ps1` (an install kind `thps2`, marker
  ALL.PKR), `tools/level_port/ported_levels.ts` (STAGINGS + thug2-decomp-thps2), `tools/menu_level/level_select.ts`
  (THPS2_LEVELS, a "TONY HAWK'S PRO SKATER 2" section after THPS3's), `tools/menu_level/make_menu_level.ts` (passes
  the THPS2 manifest).

## Formats found (beyond io_thps_scene's notes - documentation only, no licence)

- ALL.PKR: header "PKR2", u32 1, folder / file counts; folders {name[32], first entry offset, count}; files {name[32],
  compression (0xFFFFFFFE stored - all of the PC release), offset, size, stored size}.
- Levels: skhan Hangar, sksl2 School II, skmar Marseille, skny NY City, skven Venice Beach, skss Skatestreet, skph
  Philadelphia, skbul Bullring, skb1 Chopper Drop (its BMPs in newtex\Hawaii), skhvn Skate Heaven. THPS1's levels and
  the park editor's sets are in ALL.PKR too (not converted).
- Object positions 20.12 fixed point (vertex units); vertices i16; no rotations.
- Transparent texels: 0x0000 and magenta 31,0,31 (the cut-outs: 48,219 texels of Hangar's library). newtex BMPs (24-bit,
  twice the size, by the texture name's checksum) are black where the PlayStation texel is transparent: the converter
  takes the alpha from the PlayStation texture.
- Face flags: 0x80 not drawn (collision only - none has the 0x1000-0x4000 field all drawn faces have); 0x40
  semi-transparent (inferred); 0x20 has 4 more bytes.
- Collision ("surface"): 0x20 vert (the steep ramp tops against the 0x40 walls' feet), 0x40 invisible walls on vert
  lips, 0x02 trigger planes (3,711 of 4,332 invisible), bits 9-12 a terrain number. io_thps_scene's 0x10 wallride /
  0x40 vert / 0x100 not skatable don't fit the data (0x10 is on floors).
- Normals: a face's plane normal is -(v1 - v0) x (v2 - v0) (3,188 of 3,209 Hangar faces).
- Quads: corner order 0 1 / 2 3 (2,010 of 2,031 Hangar quads; 21 circular).
- .trg: restart {links, i32 x y z, i16 angles (4096 = a turn), name}; rail point {links, i32 x y z, u16 flags (low
  nibble 0/1/2 - a terrain?, 0x10 / 0x20 / 0x40 more)}; 32-bit data aligned to 4 in the file. Rail links go to rail
  points (and to type 12 nodes - named command nodes).

## The space (worked out in the game)

- THUG2 = (x, -y, -z) / 2.25. The reflection (x, -y, z) (the PlayStation GTE's right-handed space taken literally)
  drew Hangar's MAKE 'EM PAY sign mirrored; the half turn reads it right. 2.25: THPS2's Hangar is 2.26 times THUG1's
  remake across (25,093 units vs 11,100 inches); the skater against the ramps looks right.
- Triangles 0 2 1 / 1 2 3 (THUG2's collision takes (b - a) x (c - a) as the outward normal: THUG1 Hangar's floors 887
  up / 380 down).
- Restart heading: a node's Angles y = h faces the skater h from +z towards +x (a test: h = 90 degrees gave the
  sidecar's heading 90); THPS2's 0 faces THUG2's +z (Hangar's start: down its drop-in, seen in the game), turning
  towards -x: Angles y = -angle * 2 pi / 4096 (16 of the 20 off-axis restarts face open floor that way, 4 the other).

## Game runs (C:\Games\THUG2-3, own build's DLL, make_menu_level --game)

RESULTS

## Not done (phase 2 plan)

PLAN
