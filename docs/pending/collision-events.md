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
  - `Checkpoint` (velocity kept at named points): Update (update-start, state-physics, after-4FF200, try-grind,
    free-revert, update-rest), DoGroundPhysics / MoveOnGround (gravity, friction-side-feelers, wall-ahead-check,
    ground-check, moved, turned, along-facing-4FF590, side-collisions, turn-round-4FF920), CheckForGround's off-ground
    (set-air-4FDED0, SkaterOffEdge-event, GroundGone-event, after-4FEBA0).
- Screenshot: the event's own frame, as the game shows it. The reversal is detected the frame it happens, while the
  skater camera still looks the way the skater was going (it follows the turn over the next frames) - so the
  picture is the view along the old travel direction from behind the skater, without moving a camera.
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

- The uber frig (found with this tool): SkaterAdjustPhysics's update 0x004F8420 (not rewritten) runs after the core
  physics. When the skater isn't grinding / on a lip in a created park, it casts the component's feeler (0x005003A0,
  ignore 0x10) from just above the skater down uber_frig_current_height x 12 inches; if that hits NOTHING it puts the
  skater back at the core's +0x98 (where the frame began, nudged by one of five small offsets in turn) and negates the
  velocity (state -> air unless ground / air). That's the user's "exact inversion": no collision below the next step.
  Its walking twin is CWalkComponent::UpdateGroundBelow (walk_collision.cpp). The detector tells it from the other
  causes by checkpoints (`flipped=update-rest>update-start` and the velocity exactly negated) -> reason `uber-frig`.

## Checks
- thug2_check (out\build\colev): THUG2PM.exe and C:\Games\THUG2\Game\THUG2.exe: 0 MISMATCHED, 792 early self-tests
  passed - the first build and the final one.
- check_code_writes: No overlaps. npm test (341) / typecheck / lint pass. The build has no warnings.

## The user's repro (BEAVER VALLEY, (-6605.2, 7.0, 3452.5) heading 89.6, pushing forward)
Two runs in C:\Games\THUG2-2 (thp8_net_beavervalley converted there with the current converter - the same data as the
main install's 12:58 one), `-Level thp8_net_beavervalley`, `teleport -6605.2 7.0 3452.5 89.6 board; trace; press up
120`. Reproduced both times, frame for frame: the velocity went from +326 to -326.4 (x) in one frame at x = -6519.8,
the skater's position put back, one frame "in the air", then rolling back at the same speed (trace: frame 166
316.73 -> 167 -326.39). Event line (run 2; the reason was `no-ground` then - the uber-frig classification was added
after it, from this very line's `flipped=update-rest>update-start@167`):

    collision-event: n=1 level=thp8_net_beavervalley frame=167 reason=no-ground paths=no-ground frames=26
    spot=-6519.8,8.0,3453.1 ... vel_before=67.4,-0.0,0.5 vel_after=-326.4,0.0,-2.2 heading_before=89.6 heading=89.6
    face=none probe_down_hit=sector:0x5A3B8C53;face:892;flags:0x0000;...;at:-6519.8,7.0,3453.1
    probe_down_beyond=none probe_down_beyond_all=none probe_ahead=sector:Z_Houses_Street_JG_FlowerRing01;face:19;...
    flipped=update-rest>update-start@167 screenshot=requested

**Cause: a hole in the converted collision - the suburb road has NO collision faces at all from x = -6513 to about
x = -4950 (z ~3390-3540, the middle of the road; ~40 m long, ~4 m wide).** The skater rides onto it, CheckForGround
finds nothing (no-ground), and the uber frig sends him back reversed. The drawn road is there (the event's
screenshot shows it ahead). Around it, the only floor is the converter's own `thp8_net_beavervalley_GapFill_32`
(gapFills floored the road's sides, up to 5 cells from the ground either side, leaving the middle open); before
x = -6513 the road is object 0x2FB25506. Every face (any flags) checked on a 6-inch line along z = 3453 with
out\scripts\ground_line.ts (scratch). What the converter fix would take: find which THP8 body should cover that
stretch (it's not there in the .col.xbx at all, not even flagged 0x10) - dropped by convertCollision / seamWalls, or
in a zone not converted (z_world / z_navmesh hold shared pieces?), or a Havok shape type the reader skips - and bring
it back; gapFills shouldn't be the only floor on a road.

Events 2 and 3 of the same run were the skater rolling back afterwards: event 2 another uber frig near
Z_Houses_Street_JG_FlowerRing01 (-6628, 8, 3226) - not looked into; event 3 a wall bounce off the roundabout's
Z_Houses_Street_JG_CenterTree face 40 (its planter: a real one).

Confirmed in the game: the events are logged, the probes and screenshots work (the screenshot shows the road ahead
from behind the skater, the event's frame), checkpoints place the flip between frames. Not run in the game: the
`uber-frig` reason label itself (added after the second run; the tool's tests cover it) - the next run should print
`reason=uber-frig` for this spot.

## To switch it off
It's off by default. `[replacements] disable=` isn't needed: with `[debug] collision_events=0` the hooks return at once.

## Follow-up (2026-10-24, evening): notice, false positives, richer data
- On screen: `collision event <n>: <reason>` (+ " (screenshot)") for 2 s through the screenshot code's message panel
  (`Sys::Screenshot::ShowNotice`, at y 100 so the score panel doesn't cover it; a newer one replaces it; no sound, the
  game carries on). Shown after that frame's event screenshot is taken, so it isn't in the picture.
- A level's first frame logs `collision-event: logging on in <level> (col <size@time>, scn <size@time>)`.
- `collision_event_screenshots` defaults to 1 with the switch on (0 turns them off).
- `slope` only when the ground was steeper than 5 degrees somewhere in the window (ChooseReason, kSlopeNormalY): the
  user's Training event 2 (flat ground, velocity -502 -> +217, no path) was the player turning round and isn't logged
  now. The reversal test is pure now (FindReversal over Motion frames) and self-tested: braking and pushing back is a
  reversal, flat ground with no path isn't logged, 10 degrees is `slope`, a smooth 6-degrees-a-frame turn isn't one.
- Rate limit: the same spot (40 inches) is left out only within 1 s of its last line (was 5 s); the next line there
  carries `repeats=<n>` (the tool adds them to the spot's count).
- Main line: `probe_dir=x,y,z` (the beyond / ahead probes' direction: the travel before the reversal - the probes and
  the screenshot face the way the skater was going, not the way he turned), `repeats=`.
- `collision-event-detail: n=<same> state= physics= speed= trick= balance= input= input_window= stick= ray=a>b
  corners=a;b;c col_box=min>max edge= edge_dist= open_edge= col_file= scn_file= approach= camera=pos>dir`:
  - state: the SkaterState state, physics (skating / walking: +0x1C), trick (+0xE0), the balance trick's kind;
    input: the CControlPad's buttons held at the event and at any frame of the window (up down left right l1 l2 l3 r1
    r2 r3 circle square triangle x start select), the first stick;
  - ray: the feeler that decided it - for a path the component's feeler line when the path ran; for the uber frig its
    own down feeler (the core's feeler line at the next frame's start: the adjust-physics component uses the core's
    feeler); for slope the last ground feeler;
  - the face (for the uber frig: the last ground the skater stood on): its three corners (CCollObj slot 9), its object's
    box, the edge nearest the spot and whether it's open - no other face of the object shares both corners and (for
    ground, normal y > 0.5) a probe 6 inches beyond the edge's middle finds nothing; walls' edges stay "-";
  - col_file / scn_file: the level's .col / .scn (the overlay's .xbx, else Data\pre\<level>col.prx) size@time;
  - approach: the skater's position and state letter (g a w l r p) every 10 frames over the last 50;
  - camera: viewport 0's camera (Sys::Screenshot::LastView).
- Tool: parses the detail lines; a `state` column; per event probe_dir / ray / edge / input / approach; verdicts
  "an open edge ... (a dropped piece?)", "flat ground: the player's own turn" (old logs), "player turn? (down held)" /
  (trick / manual) for slopes; with --col a WARNING when the file's size@time isn't the col_file the game loaded.
- Checked in the game (THUG2-2, THP8 SKATE PARK, a push at a quarter pipe; two short runs): the
  logging-on line with the file stamps, the event and detail lines, the notice on screen (gone after 2 s), the
  screenshot on by default. The open-edge test then also flagged a barrier wall's bottom edge - walls are no longer
  judged (`open_edge=-`), not re-run in the game after that change.
