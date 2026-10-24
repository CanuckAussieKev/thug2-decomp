# Collision events: the skater's velocity turning round, logged with its cause

NOT ORIGINAL - a debugging aid (the user, 2026-10-24: "detect when we hit a piece where no collision exists ... save
them in the log with a certain tag, so I can skate around a level, and you can then use them in bulk to fix the
level"). Branch `worktree-agent-a0e7d0cb6bd7b3520`.

## What it is
- `[debug] collision_events=1` (default 0): src/game/skater/collision_events.h / .cpp. Each time the skater's velocity
  turns round on the ground, one log line `collision-event: key=value ...` with the cause, the face and six probes.
  `[debug] collision_event_screenshots=1` (default 0): a screenshot of the event's frame too (the screenshot code's new
  `Sys::Screenshot::RequestEventShot`; works with the screenshot key off; the sidecar says "for: collision event <n>";
  no on-screen message).
- Hooks (calls only - nothing changed, and with the switch off NotePath / EndFrame return at once):
  - `CorePhysicsComponent::Update` (0x00510830, core_update.cpp) calls `EndFrame` last;
  - `NotePath` after each path that turns, reflects or stops the skater against collision:
    BounceOffWallAhead 0x005059D0 (wall-ahead, wall-ahead-corner, wall-ahead-push), CheckWallAhead 0x00506E50
    (wall-ahead-climb), CheckSideCollision 0x0050A2C0 (side-wall), CheckAirForwardCollision 0x00508780 (air-bonk,
    air-wall-vert), KeepOffCeiling 0x00504EE0 / KeepOffCeilingAtWall 0x005051B0 (ceiling), CheckForGround 0x00509BD0
    (ground-unskatable, ground-rejected, no-ground).
- The detector, the reasons and the line: collision_events.h's header comment. Rate limit: the same spot (40 inches)
  once in 5 s; only the local skater's component (self-tests' stand-ins are ignored).
- Every path in the skater code that turns the velocity round, found while reading (for the record):
  - 0x005059D0 BounceOffWallAhead: turned by Wall_Bounce_Angle_Multiplier x the wall's angle (BounceOffWall
    0x005039F0), slowed the more head-on, then if the feeler ahead hits again "into a corner": turned by pi less that,
    velocity halved - the only path that turns the velocity right round in one frame;
  - 0x0050A2C0 on the ground: put back to +0x98 and turned off the wall (BounceOffWall, scale 0.2);
  - 0x00508780 in the air: the velocity along the wall plus a tenth of the speed out of it (a bonk);
  - the ground's gravity along a slope (DoGroundPhysics 0x0050EE70): rolls the skater back up a slope it can't climb -
    no collision path at all ("slope");
  - not reversals, but they follow one: 0x004FF590 keeps the velocity along the facing (+-), and 0x004FF920 (not
    rewritten) turns the skater round (the board's facing negated: the 180 the user sees) once the velocity points
    backwards - so the visible 180 is the consequence, not the cause.
- Tool: `node tools/game/collision_events.ts <thug2-decomp.log> [--level <name>] [--col <level | file.col.xbx>]
  [--root <folder>]... [--radius <inches>] [--json]` - tests in collision_events.test.ts (the DLL's self-test line).
- Self-tests (src/hook/selftests_collision_events.cpp, Early): the hooks on a stand-in component write nothing; the
  line's exact text.

## Checks
- thug2_check (out\build\colev): THUG2PM.exe 0 MISMATCHED, 792 early self-tests passed; THUG2.exe: (below).
- check_code_writes: No overlaps. npm test / typecheck / lint pass. The build has no warnings.

## The user's repro (BEAVER VALLEY, (-6605.2, 7.0, 3452.5) heading 89.6, pushing forward)
(below)

## To switch it off
It's off by default. `[replacements] disable=` isn't needed: with `[debug] collision_events=0` the hooks return at once.
