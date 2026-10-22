# Park editor & graffiti, first part (0x588180-0x58CD40) - PE1 agent

Branch worktree-agent-a8cf5d2fe14b8f721, workspace C:\Games\THUG2-1. Every function of the range has source now: 59 new
here (52 Ghidra functions and 7 it never made - the viewer object's input handler and PC-tool message handlers, found in
the padding), the other 45 were done on main (batches 320-328). Source: src/game/sys/viewer_object.cpp / .h (extended),
src/game/ed/park_rails.cpp / .h, src/game/ed/clipboard.cpp, src/game/ed/gap_slots.cpp (new). Tests:
src/hook/selftests_viewer_object.cpp (extended), selftests_park_rails.cpp, selftests_park_clipboard.cpp,
selftests_park_gaps.cpp (new), on the viewer test kit (src/hook/viewer_testkit.h). Shared files touched: CMakeLists.txt
(new files listed); src/game/ed/park.h - CMetaPiece::IsRestartPiece (0x005902F0, a GameFunction wrapper) added; a new
struct CClipboardEntry; CClipboard's GameFunction wrappers became declarations (implemented in clipboard.cpp), its
mp_metas typed CClipboardEntry*, ShowAt returns bool (the original's AL; the one caller ignores it); GapInfo got
Construct / GetLength; CGapManager's wrappers for 0x0058C990-0x0058CCC0 became declarations (gap_slots.cpp) plus
sInstance (0x0058CBB0) and GetGapTriggerName (0x0058CA60); Construct takes the park manager (PE2's mp_manager, +0xA4).
gap_slots.cpp uses PE2's layout of the class (CGapManager::Gap, m_startedFirst...; merged from main). The rail classes
are the one layout of the park generator's rails: src/game/ed/park_scene.h's RailSet / Rail / RailPoint (PE4's views)
are now aliases of CParkRailSet / CParkRailString / CParkRailPoint, with PE4's field and method names (Init, Clear,
FreePools, FindRail, NewRail, AddRail, FreeRail, CountSegments; m_class, m_name, mp_points, m_numPoints, m_angles,
m_cluster...), so the generator calls ours directly (its tests stand in for ours through OursAt).
Switch off: `[replacements] disable=viewer_object.cpp,park_rails.cpp,clipboard.cpp,gap_slots.cpp` (any of them alone;
viewer_object.cpp also switches off batches 327-328's viewer object code).

- [x] Batch PE1-1 (NOT run in the game yet; the model viewer's object: its animation controls, update, info panel, wheels,
  input handler, PC-tool messages and constructor, 15 functions): src/game/sys/viewer_object.cpp / .h.
  - 0x00589FD0 ReloadAnimation(file, anim): with an animation component, the asset (anim set + anim, or Crc(file) for
    anim 0) unloaded if loaded, LoadAnim(file, anim, anim set), PlayAnimation(anim).
  - 0x0058A2E0 SetRotation(z, x): identity, then m * RotZ(z), m * RotX(x) (D3DXMatrixMultiply stays a call).
  - 0x0058A360 SetAnimSpeed(speed): +0x118 = speed clamped to 1 then 0 (NaN kept); with an animation component its first
    channel's 0x0047E850(speed, 0, 0, 0) and its member function 0xBD4EDD44 (Speed) with {speed = speed} - through the
    component's vtable +0x30 (not CAnimBlenderComponent::CallMemberFunction directly: the test caught that); not paused.
  - 0x0058A4A0 StepAnimation(forward): paused (speed saved in +0x11C, SetAnimSpeed(0)) unless it is; forward: 1/60 s to
    every channel, VisitChannels, the step member function 0x6AAEB76F with {0x06C9F278 = 1/60}; back: the same with
    -1/60 between two reverses (0xF5E2B871) and 0x0047E810(0) calls; then 0x0047EFF0; +0x124 = paused.
  - 0x0058A720 SpinWheels: the board's wheels - the angle 0x007CE590 += max_wheel_speed x speed, wrapped into 0..360 in
    double precision (exactly 360 stays); with more than 5 bones the skeleton's matrices from the model's hierarchy,
    bones 2-5 turned about x, bones 1-5 times bone 0. Without a skeleton component the counter 0x0067F384 moves on
    (mod 32).
  - 0x0058A8B0 UpdatePanel: with the panel shown and a known animation, draw_viewer_object_panel with line1-4 ("name
    Unknown", "speed %f" (0 when paused), "time %f of %f" (time, length), "frame %d of %d" (x60, truncated)); then
    SpinWheels if the model has bones.
  - 0x0058AB30 OwnUpdate (vtable slot 10): the commands (+0x110) - panel toggle (+ kill_viewer_object_panel), speed
    +-0.25 (from the saved speed when paused), frame steps, and five commands passed to the viewer (0x4000, 2, 4,
    0x10000, 0x20000) - then UpdatePanel, commands cleared.
  - 0x0058A060 (no Ghidra function) the input handler's code: ViewerAutoRepeatTime / ViewerAutoRepeatSpeed, the
    function-static repeat count (0x007CE588, guard bit 0x007CE58C - the first call's delay), Left / Right steps with
    auto-repeat, the rest of the buttons as commands (roll only without an animation component).
  - The PC tool's messages (no Ghidra functions): 0x00589840 (0x25) PlayAnimation, 0x0058ACA0 (0x26) SetAnimSpeed,
    0x0058ACC0 (0x27) StepAnimation(the first byte as it is), 0x0058A1F0 (0x28) ReloadAnimation(msg, +0x80),
    0x0058ACE0 (0x29) ReloadSkaterCamAnim (0x005ADEC0) with the message and the checksum at +0x80 unnamed,
    0x0058A210 (0x2C) scripts\animview\<msg>.qb loaded and <msg> spawned on the object.
  - 0x0058ADB0 the constructor: the composite object, vtable 0x0064F138, base components, OBJECT_TYPE_COMPOSITE, the
    input handler (Inp::Handler, vtable 0x0064D7D8) on controller 0, speed 1, panel shown, identity, the six message
    handlers when there's a net handler. Fields named in viewer_object.h (m_commands, m_app, m_speed, m_savedSpeed,
    m_paused, m_panelShown); m_task (+0x108) is the input handler (ViewerObjectInputHandler).
  - Tests TestViewerObject4-8 (reload / rotation / speed / steps; wheels and panel with NaN, wrap, exactly 360, float vs
    double wrap; own update; the input handler over 23 frames and the messages; the constructor with and without a net
    handler). 51 deliberate bugs, 48 caught, 50 after strengthening; 1 equivalent (SetRotation's first product
    pre-multiplied: identity times R either way).

- [x] Batch PE1-2 (NOT run in the game yet; the park generator's rail sets, strings and points, 15 functions):
  src/game/ed/park_rails.cpp / .h (Ed::CParkRailSet / CParkRailString / CParkRailPoint = park_scene.h's RailSet /
  Rail / RailPoint; the strings "In set" / "Out set", "%s RailPoint" / "%s RailString" are the game's).
  - 0x0058AFC0 AddPoint, 0x0058AFF0 the set's constructor (the last rail isn't cleared), 0x0058B010 AddRail,
    0x0058B040 CountSegments (a loop's points, else points - 1), 0x0058B070 CountPoints, 0x0058B090 FindRail(name,
    index), 0x0058B0C0 Init(points, rails, in), 0x0058B280 FreePools, 0x0058B2D0 NewPoint / 0x0058B370 NewRail (the
    set's pool lent to the class's pool pair - 0x007CE5A4 / 0x007CE594 - for the allocation, then taken back; the
    "owned" byte per pool at +4 of the pair), 0x0058B220 the rails' CPoolable delete, 0x0058B420 CopyFrom (the points
    turned 0-3 quarter turns about y and moved; ClimbingNode rails' headings + pi/2, pi, 3pi/2), 0x0058B5B0 FreeRail,
    0x0058B620 DeletePoints, 0x0058B700 Clear.
  - Test TestParkRails (real item pools, the memory manager stood in: three source strings, copies at every turn, every
    delete path, a heap string freed, pairs with current pool 1). 42 deliberate bugs, 38 caught, 40 after
    strengthening; 2 equivalent (GetSegmentCount's "> 1" as ">= 1": 1 - 1 = 0; the copy also copying the link: every
    link but the last is overwritten, the last is the source's null).

- [x] Batch PE1-3 (NOT run in the game yet; the park editor's clipboards, 19 functions): src/game/ed/clipboard.cpp,
  park.h (CClipboardEntry, CClipboard).
  - CClipboardEntry (0x18 bytes, pool pair 0x007CE5C4): 0x0058B800 GetArea (turned with the cursor: offset rotated,
    the far corner moved for turns 1-3, width / length swapped for odd turns), 0x0058B940 CanPlace (CanChangeFloorHere
    uneven), 0x0058B9C0 Paste (the floor raised to its height + the cursor's clipboard height, at most 15, up to 41 steps,
    a flat floor at the height stops it; a new metapiece turned and added unless it's of the inner shell), 0x0058BB20
    GetHeight, 0x0058BBA0 HighlightIntersectingMetas, 0x0058BCA0 ShowPreview (x87: the sine stored as a float, the cosine
    and the turned z kept in double - as the original; the original's offset w is stack garbage: ours keeps the
    position's w).
  - CClipboard: 0x0058B7E0 IsEmpty, 0x0058BDF0 Construct, 0x0058BE30 GetHeight (-1000 = none -> 0), 0x0058BEB0
    HighlightIntersectingMetas, 0x0058BF10 GetArea, 0x0058C050 GetCentre, 0x0058C130 ShowMetas, 0x0058C200
    DestroyMetas, 0x0058C270 AddEntry (duplicates skipped; false when the pool is full - the pool isn't null-checked),
    0x0058C430 Destroy (+ 0x0058C980, its 5-byte jump: REPLACE_SOURCE_ONLY), 0x0058C590 CopySelectionToClipboard (the
    lowest piece's height: riser_below +15, riser_floor +1; restart pieces, risers below and pieces not wholly inside left
    out; the rails over the area copied by the rail editor), 0x0058C780 ShowAt = paste (park memory 0x00590160 /
    0x0058D6D0 / 0x0058D290, every entry's CanPlace, free rail points 0x004F6BF0, then the entries pasted and the rails
    copied, placed and transformed). The rail editor's calls (0x004F....) and the park memory's stay GameFunction calls.
  - Test TestParkClipboard (8 placed metapieces over a 3x2 area with every kind left out, a duplicate, the cursor at
    every turn, previews at 4 angles, every ShowAt failure and 3 pastes, a copy that runs out of entries). 61 deliberate
    bugs, 53 caught, 60 after strengthening (pieces that differ from a duplicate only in height or turn, floor heights
    that differ per entry, the risers as the lowest pieces, IsEmpty with only rails, GetHeight with no entries); 1
    equivalent (Paste not clearing its "flat" flag before each GetFloorHeight, which always writes it).

- [x] Batch PE1-4 (NOT run in the game yet; the gap manager's slots and a gap's info, 10 functions):
  src/game/ed/gap_slots.cpp, park.h.
  - 0x0058C990 GetFreeGapIndex, 0x0058C9B0 EndGap (the second half in a free slot, the gap's id from the counter
    starting at 2000), 0x0058CA10 GetGapIndex, 0x0058CA60 GetGapTriggerName (Crc("EditorTrgGap<slot>"); "PrintGaps"
    prints an odd slot's gap), 0x0058CB20 GetGapInfo (the record's name copied into the info's text, unbounded),
    0x0058CB70 SetCurrentGap (both trigger pieces highlighted, 0x005902B0), 0x0058CBB0 sInstance, 0x0058CCC0 Construct;
    GapInfo 0x0058CBC0 Construct, 0x0058CBF0 GetLength (extents + the piece's length / width x 120; 0 for a bad turn).
  - Test TestParkGaps. 23 deliberate bugs, 21 caught, 23 after strengthening (the test's metapiece / piece addresses
    were off by two words).

Older tests changed (PE1-3 / PE1-4): selftests_parkedA.cpp, selftests_lfxc2.cpp, selftests_lfxc3.cpp stood in for the
clipboard / gap manager functions at their game addresses only - the cursor code now calls ours, so each stand-in also
takes our function's address (TestParkManagerAccessors crashed the whole run before that).

Checks (all four batches): thug2_check on THUG2PM.exe and the stock THUG2.exe: 0 MISMATCHED, all early self-tests
pass; check_code_writes: no overlaps; scan_register_assumptions: only the 5 known RISK lines, every EAX-read function
returns a value; audit_game_function_calls: no MISMATCH; find_gaps: nothing in the range.

What to try by hand (park editor): copy an area with the clipboard tool (CREATE-A-PARK, area selection, copy) including
risers, a restart point and rails; turn the paste with the cursor through all four turns and paste it (on raised floor
too); check the preview follows the cursor smoothly and the rails come along; make a gap (both halves), look at its name
/ score, remove it. Model viewer: only with the PC tool (not available) - the pad's viewer buttons in a viewer object
mode (frame steps, speed, the info panel) if the viewer can be launched.

- [x] Game run (workspace C:\Games\THUG2-1, under the lock; the verify build after merging PE4): every replacement
  installed, `-level BO` into free skate and `-level Sk5Ed` into the park editor (the piece menu and cursor up, the cursor
  moved with the d-pad), no EXCEPTION / RUN-TIME CHECK FAILURE / DEBUG CRT REPORT, closed cleanly. Not driven: the
  clipboard (area select, copy, paste), gaps, the model viewer's object - for the user (above).
