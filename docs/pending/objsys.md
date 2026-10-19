# objsys: 0x45EFD0-0x47DC40 ("Object system & script runtime")

Agent "objsys". Branch worktree-agent-ac4285dede37d3675, game workspace C:\Games\THUG2-4.

## The range

685 Ghidra functions, 467 rewritten before this agent; 218 left (~48 KB) in clusters: the cutscene system
(0x463AA0-0x467707), the cutscene-object / path manager code 0x467710-0x46AAA1, the proximity nodes and the control
pad 0x46AAC0-0x46C26C, the preferences class 0x46C270-0x46C6A0, the structure buffer writer / reader 0x479840-
0x47A8D1 and the sound effects manager 0x47AC10-0x47DC31.

Shared files touched (small, additive): CMakeLists.txt (new sources and selftests_objsys*.cpp); tools/mutate.py
(a hanging check is killed by its process id, not by name - other checkouts' checks may be running).

## Batches

- [x] Batch OS-1 (NOT run in the game yet; the controller pad): src/game/obj/control_pad.{h,cpp} (new).
  - The pad as the skater reads it (THPS's CSkaterPad / CSkaterButton; here Obj::CControlPad / CControlButton): 16
    buttons of 0x20 bytes (UP, DOWN, LEFT, RIGHT, L1, L2, L3, R1, R2, R3, CIRCLE, SQUARE, TRIANGLE, X, START,
    SELECT at +0x000-0x1E0), the raw sticks +0x200, the scaled sticks +0x210, each stick as angle + length
    +0x220, two timed holds +0x230.
  - Buttons: 0x0046B7E0 ClearTrigger, 0x0046B7F0 ClearRelease, 0x0046B800 SetName (the checksum), 0x0046B820
    Update(pressure) (down above 0x40; the press / release times are the game clock + its offset 0x696D60; while
    +0x1C (a Tmr::GetTime time as a float, 0 = none) is set, a press only counts once that time has passed - any
    other update clears it; compared unsigned, NaN holds), 0x0046B8B0 SetPressed(byte) (the byte itself is stored;
    a release here isn't timed), 0x0046B900 / 0x0046B920 the time since the release / press.
  - Pad: 0x0046B940 Init (the names; the holds cleared; returns this), 0x0046BB50 Reset (every button released, in
    the original's order; sticks zeroed), 0x0046BC60 Clear (Reset, then every button's events taken - the
    original's slip kept: R2's press is taken twice and its release kept, R3's release twice and its press kept),
    0x0046BDA0 Update(data) (PS2 pad layout: pressures 4-0xF as full 0xFF / 0, L3 / R3 / SELECT from bits 0x200 /
    0x400 / 0x100 of +0x1C; START isn't read; the scaled sticks: data 0-3 - 0x80 moved 50 towards 0 (crossing it:
    no dead band) / 77, -0x80 counts as -0x7F, exactly centred = 0; the raw sticks data 0x12-0x15 - 0x80; the
    angles atan2(x, -y) through the FPU's FPATAN; the second stick under 0.001 long stands in the d-pad's angle at
    length 127; the two holds end once the time has passed or the second stick is centred / leaves the up / down
    quarter), 0x0046C270 GetButton(name) (in the original's search order).
  - 0x0046BA50 DPadDirection and 0x0046BA80 DPadAngle (the d-pad's 16 combinations; the angle table is the
    original's static 0x6F2270, filled on first use - a constant table here), 0x0046C370 StickLength(x, y, angle)
    (__stdcall: x / 127 or 128 by sign, the length times |cos| of the angle brought into the quarter around up /
    down / left / right (FCOS) times 1/0.85, at most 1).
  - Test (selftests_objsys1.cpp): TestControlPad - 40 random pads (buttons pressed / not / holding a 2, locks
    none / 1500 / 1200.5 / NaN / 3e9; pad holds 0 / 1200 / 1500 / past 2^31) driven through 24 random calls each
    (updates from random data - centred sticks, the d-pad standing in, stick pointed up / down -, pressures around
    0x40, SetPressed with 0 / 1 / 2 / 0x55 / 0x100, the times, names, lookups with duplicate names, Reset, Clear),
    the clocks stood in (scripted, unsigned values past 2^31, exactly the lock time), the whole pad compared after
    every call; directed updates at exactly the holds' time with the stick up / down. TestControlPadMath: every
    d-pad combination, StickLength over each quarter / boundary / NaN / both signs, the clamp just over 1.
  - Mutations: 36 deliberate bugs; 32 caught (29 at first; the holds at exactly the time, duplicate names and the
    clamp were added for 3). The other 4 are equivalent: `value >= 0` for `> 0` in the scaling (0 returns
    earlier), `x >= 0` for `x > 0` in StickLength (0 / 127 = 0 / 128), `a >= -pi/4` for `a > -pi/4` (cos is even:
    the same length either way).

- [x] Batch OS-2 (NOT run in the game yet; proximity nodes, restart node lists, preferences):
  src/game/obj/proxim.{h,cpp}, src/game/script/preferences.{h,cpp} (new).
  - Proximity nodes (THUG's Obj::Proxim_*; level nodes of class ProximNode; the list 0x006F2268 = {head, entering,
    in-script}): 0x0046AAC0 ProximShapeFromChecksum (0xE460ABDE box, 0x03F1E5A7 sector box, 0x64FBA415 none,
    0x6AADF154 sector collision, anything else sphere), 0x0046AB10 / 0x0046AB30 the manager made / emptied
    (Proxim_Init), 0x0046AB50 Proxim_Cleanup (every node freed), 0x0046AB90 Proxim_GetNode (no null check, as the
    original), 0x0046ABC0 ProximNode::SetActive (a node going inactive kills its objects (vtable slot 6), or runs its
    script as the exit for each skater it holds - entering flag clear, in-script set around it; everyone counts as
    outside after), 0x0046ACB0 Proxim_SetActive(name), 0x0046ACF0 SetSector (the sector's box; for the sector shapes
    also its world matrix, inverted), 0x0046AD50 IsInside (sphere: z, y, x squares summed unrounded <= radius^2; box;
    sector box: the point into the sector's frame about the box's centre; the sector's collision object's +0x48; the
    "none" shape and unknown ones: no), 0x0046AEC0 GetClosestPoint (the sphere's: w's product unrounded; the boxes'
    surfaces, the sector box through its frame and back by the world matrix; none: `out` left - the original copies
    uninitialised locals), 0x0046B1F0 Proxim_AddNode (name=, shape, sector, position, ProximType 0x7321A8D6
    (0x57525413 -> 2, 0xC4E311FA -> 1, else 0), radius and its square, script; ProximObject nodes keep their name as
    the objects' base id; plain nodes start holding the skaters flagged at +0xBC, end them (SetActive(false)) and
    take `active`), 0x0046B400 Proxim_Update(mask, object, point, type) (entering: plain nodes spawn their script on
    the object (or run it) with the entering flag; ProximObject nodes make a composite object (id = base + the
    skater's id, the node's position, components from proximobj_composite_structure with the sector's name
    0x08470F2E when there is one, finalised, its script switched to the node's); leaving: the exit script / the
    object killed).
  - Restart nodes: 0x0046B680 IsRestartNode (code Ghidra never made a function of), 0x0046B6C0 GetRestartNodes(type,
    out, start) (class Restart; type 0 = any, else those whose restart_types hold it), 0x0046B790 GetRestartNode(type,
    index, start) (wrapped past the count; a negative index reads before the list, as the original).
  - Prefs::Preferences (vtable 0x00647B34: GameNet::Manager's network / taunt preferences): 0x0046C470 Init,
    0x0046C490 Deinit, 0x0046C680 the scalar deleting destructor, 0x0046C4A0 GetPreference, 0x0046C4D0
    GetPreferenceArray, 0x0046C500 SetPreferences, 0x0046C510 / 0x0046C550 / 0x0046C590 GetPreferenceString / Value /
    Checksum (asserting getters: a missing field returns the preference's name - the original reads the value back
    from the argument's slot; the checksum getter asks for the preference with an assert), 0x0046C5D0 SetPreference,
    0x0046C610 SetPreferenceArray, 0x0046C650 Load (a global structure's components).
  - Tests (selftests_objsys2.cpp): TestProxim - 24 seeds of 3-6 nodes from random parameters (every shape name,
    type, radius or none, ProximObject, renamed by name=, sectors or none, skaters flagged or not), 40 random calls
    (updates of skaters / no object at points near and far, each mask, each type; nearest points; inside tests;
    activation by node and by name; lookups), then a directed pass (every node active, entered by two skaters at its
    centre, one leaving far away, the sphere's exact edge, deactivated), clean-up with and without a manager - the
    world stood in for (node parameters, objects and their vtables, sectors and collision, scripts, memory), the box
    / matrix / vector maths routed to ours in both runs, the whole arena and every call compared. TestRestartNodes:
    the shape names, the lists by type from a start node or the first, IsRestartNode, the n-th. TestPreferences:
    every method on stand-in structures whose getters find the field or not, the destructor with flags 0-3.
    objsys_testkit.h: globals saved and restored by the tests' cleanup (also after a crash).
  - Mutations: 47 deliberate bugs; 45 caught (37 at first; the directed pass, the sphere's edge, w values that round
    and the destructor flags were added for 8). The other 2 are equivalent: the manager's in-script flag cleared
    after Calloc (zeroed already), SetActive returning early only when activating (inactive nodes always have
    m_outside -1).
  - Also: src/game/core/singleton.{h,cpp} (shared, additive): 0x00469490 Spt::PointerMember::Get (3 bytes, source
    only; the folded one-pointer getter); tools/mutate.py lists every failing / crashing test of a mutation (an
    unrelated flaky test - "PC async handle" fails now and then on this busy machine - hid which test caught it).

- [x] Batch OS-3 (NOT run in the game yet; script structures to and from byte buffers - the network / save
  serializer): src/game/script/struct_buffer.{h,cpp} (new).
  - 0x00479840 WriteNameAndType (a name as a one-byte index into the global name array 0x35115A20 (type | 0x80), a
    two-byte index into 0x25231F42 (type | 0x40), else the checksum), 0x004798F0 WriteInteger (the smallest form:
    zero, unsigned / signed byte or short, else four bytes; name in ECX, value in EBX - a naked stub), 0x00479A50
    WriteFloat (zero floats as their own type; buffer ECX, name EDX, size ESI - stub), 0x00479AC0 / 0x00479B20
    WriteString / WriteLocalString (buffer EAX, string ECX - stubs), 0x00479B80 WriteArrayToBuffer (type, two-byte
    count, elements; strings, pairs, vectors, nested structures and arrays), 0x00479E20 WriteToBuffer (every
    component; scripts as the placeholder name 0xB9C4F664; the callback per component; 0 when it doesn't fit),
    0x0047A070 CalculateBufferSize (a write into a scratch buffer), 0x0047A0C0 ReadFromBuffer (the structure
    cleared and rebuilt; unknown types: only the name read), 0x0047A440 ReadArrayFromBuffer (arrays made on the
    script heap).
  - Tests (selftests_objsys3.cpp): TestStructBuffer - 30 seeds of random nested structures (every component type,
    names from the two index arrays or not) written in full, measured, into short buffers of every length near the
    end, an array alone into 0-4 byte buffers, the 300-name array read back alone (a two-byte count), read back and
    written again, a crafted buffer of every short form and unknown types - on a stood-in memory manager over an
    arena, the whole arena and every call compared. TestStructBufferStubs: the register entry points against the
    originals into buffers big enough, exactly enough and too small.
  - Mutations: 41 deliberate bugs, all caught (the array-alone and 300-name cases added for 2).

- [x] Batch OS-4 (NOT run in the game yet; sound emitter nodes, the follow trail): src/game/obj/emitter.{h,cpp} (new).
  - Emitter nodes (a Lst::HashTable at 0x006F2260 of EmitterNode, 0x60 bytes): 0x004680E0 Emitter_GetNode, 0x00468120
    IsInside (box: the sector's bounding box; sphere: the squared distance summed unrounded z, x, y against the
    radius squared; other shapes never), 0x004681A0 GetClosestPoint (the point itself inside; the box's closest
    surface point; the sphere's along the direction), 0x004682B0 Emitter_Cleanup (every node freed and flushed; the
    table stays), 0x00468340 Emitter_SetActive (the first node with that name), 0x004683B0 Emitter_Init (the table
    made with 4 bits, or emptied), 0x00468450 Emitter_AddNode (from the node's parameters: name= renames it, the
    shape names 0x03F1E5A7 other / 0xAA069978 sphere / 0xE460ABDE box, the sector of that name - its box for box
    emitters -, position, radius, script).
  - CFollowTrail (64 points spaced over 40 units behind an object): 0x00468AC0 Init, 0x004685D0 Reset, 0x00468610
    Update (the object's position added when it's moved more than 40 from the newest point), 0x00468730
    GetPosition (the point m_distance back along the trail, and the matrix turned to face along it: level or free).
  - Tests (selftests_objsys4.cpp): TestEmitters - 24 seeds of 2-9 nodes from random parameters (each shape name and
    none, renamed - some to the same new name -, sectors or not, radius or not), 30 lookups / activations (by old,
    new and unknown names) / inside tests and nearest points (points inside, outside and on a sphere's exact edge),
    the clean-up or Init on the full table - on a stood-in memory manager, the arena and every call compared.
    TestFollowTrail: 30 seeds of 10-259 updates (missing object, steps under, exactly at and over 40, the ring
    wrapping), positions and facings back along the trail at distances within, across and past it, both facing
    modes; the reset on dirty memory.
  - Mutations: 33 deliberate bugs, all caught (25 at first; the exact sphere edge, shared new names, the reset on
    dirty memory, longer trails and the exact spacing were added for 8).

- [x] Batch OS-5 (NOT run in the game yet; objects made for level nodes, movie details, the class node list):
  src/game/obj/node_objects.{h,cpp} (new).
  - 0x00468B00 CreateParticleObject (moving_particle_composite_structure when the node has HasMotion, else
    particle_composite_structure), 0x00468B60 StartNodeObject (finalised, switched to the node's script 0x2CA8A299
    with its params 0x7031F10C, the creation script 0x413B766D run with the node's name and its integer 0x99A9B716
    if it has one), 0x00468CB0 CreateGameObject (the components of 0x735A20BA, two more around the script
    0xF3B84DA6, finalised, the creation script, component 0x286A8D26's set-up, the node script), 0x00468EB0
    CreateGameObjectFromNode, 0x00468EC0 CreateNodeObject (flag 0x69C257E9: a plain composite object from the
    structure with script 0xA98F5C63; else a game object).
  - CMovieDetails (a list node + settings + a camera): 0x00468FB0 Construct, 0x00469230 DestroyBody (the camera
    deleted unless a viewport uses it), 0x004692D0 / 0x00468F70 the deleting destructors, 0x00469050
    InitFromStructure, 0x00469180 GetSettings, 0x00468F20 CheckDone, 0x00468F40 / 0x00468F50 / 0x00468F60 setters.
  - CClassNodeList: 0x004693C0 Init, 0x00469300 Clear (0x004693D0, a jump to it: source only), 0x00469340
    SetTransform, 0x00469350 Transform, 0x004693E0 AddEntry, 0x00469430 SetNodes (an entry per node of class
    0xD18719D5).
  - Tests (selftests_objsys5.cpp): TestNodeObjects - 40 seeds of nodes with random parameters made into particle,
    game and plain objects (the object manager, components, scripts and structures stood in for; every call
    compared). TestMovieDetails: construct (the node as built), random settings / setters / CheckDone, the three
    destructors with flags 0-3 on a linked node; the class node list from random node arrays, transformed or not,
    cleared through both entry points - the arena compared.
  - Mutations: 35 deliberate bugs, all caught (the constructed node and destructor flags 2-3 added for 2).
  - Also: selftests_objsys3.cpp's arenas at fixed high addresses (a misaligned name in the buffers equal to a low
    arena's address was relocated in one run only) and its names changed to match.

- [x] Batch OS-6 (NOT run in the game yet; the path manager and its object groups): src/game/obj/path_manager.{h,cpp}
  (new).
  - CObjectGroup (smart pointers to objects, new[]'d): 0x004694A0 Destruct (code Ghidra never made a function of:
    every slot released, the array freed), 0x004698C0 Init(size), 0x00469540 ReleaseAll, 0x00469580 Remove,
    0x004695D0 Add (the first free slot; nothing when full).
  - CPathManager (singleton 0x006F2264): 0x004694E0 Instance, 0x00469620 AddToPath (the node's PathNum group),
    0x00469670 AddToExtraGroup, 0x004696C0 NumberPaths (every node walked: classes 0x8E6B02AD / 0x30C19600 and
    0x4C23A77E with type 0xCBA10FFA skipped, nodes without links unless 0x4C23A77E, nodes numbered already; down the
    first links numbering, a numbered node giving its number to the whole chain from the start), 0x00469820 Free,
    0x00469960 Allocate (on the bottom-up heap), 0x00469A80 CFunc_AllocatePathManMemory (defaults 200 groups of 20,
    an extra group of 200).
  - Tests (selftests_objsys6.cpp): TestPathManager - 30 seeds of random node graphs (every skipped class and type,
    numbered nodes, chains running into numbered ones, loops), the manager made (as built) and allocated, 30 random
    adds / removes / releases on groups and the extra group, a group of its own destroyed while holding objects,
    numbering, freeing (twice), the script command with no sizes, some or all given - on a stood-in memory manager,
    the arena after the group operations, after numbering and at the end compared, and every call.
  - Mutations: 30 deliberate bugs; 29 caught (22 at first; the arena images, the manager as made, the defaults and
    the destroyed group added for 7). The other is equivalent: a slot cleared again after Release (which clears it).

- [x] Batch OS-7 (NOT run in the game yet; following a path of linked nodes): src/game/obj/path_follower.{h,cpp}
  (new).
  - CPathFollower (vtable 0x00647A84; used by the path component): 0x0046A1D0 Construct, 0x00469B50 the deleting
    destructor, 0x00469B00 ReadNodePosition, 0x0046A120 NextNode (a random link when the owner's component
    0x0A015E17 says so, else the first), 0x00469F60 UpdatePosition (along the segment), 0x0046A000 CheckCorner (the
    cut short of the node: half the next segment when that's shorter than the corner distance), 0x0046A250
    StartSegment, 0x00469B80 ComputeArc (the arc through the corner: its centre on the bisector, radius, angle and
    frame; 0 = blend instead, 1 = arc, 2 = degenerate), 0x0046A320 ArcStep (the frame turned about Y via
    D3DXMatrixMultiply), 0x0046A550 StartCorner, 0x0046A3F0 Start (nodes within 1 unit skipped), 0x0046A640 Update
    (a distance, optionally weighed by a direction: straight / arc / blend / done; the component told of each node
    passed - at the arc's or blend's half), 0x0046A1B0 GetHeading. The original's x87 comparisons kept (unordered
    counts as nearer where it does).
  - Tests (selftests_objsys7.cpp): TestPathFollower - 60 seeds of random node graphs (close nodes, dead ends, random
    links), corner distances none / small / large, a start on, near or away from the path, 80 updates of random
    distances (some weighed by directions, perpendicular ones too), headings; the follower as constructed and after
    every step compared (a NaN's sign not compared: once a degenerate corner has made the state NaN, the x87's
    choice between two NaN operands differs from SSE's) - the vector maths routed to ours in both runs, acos the
    same for both.
  - Mutations: 36 deliberate bugs; 32 caught (the constructed state added for 1). The other 4 are equivalent: the
    carry added in StartSegment (always 0 there), the zero-distance arc (unreachable: FLAG_CORNER returns first),
    the along value set before StartSegment overwrites it, and the done state after a failed NextNode (a node
    without links is always the last node).

- [x] Batch OS-8 (NOT run in the game yet; the sound effects manager, part 1: tables, volumes, parameters):
  src/game/sfx/sfx.{h,cpp} (new).
  - sVolume (2 channels, 5 for the surround type; 3D ones carry a position and range): 0x0047B030 Init (the
    manager's default type, silent; returns this), 0x0047B070 SetType, 0x0047AC50 PercentageAdjustment,
    0x0047ACF0 IsSilent (never for 3D), 0x0047AD60 SetSilent, 0x0047B080 GetLoudestChannel (signed; first of equals).
  - Free functions: 0x0047AC10 FalloffFromChecksum, 0x0047AFD0 GetMainVolume, 0x0047AFE0 CountPlayingVoices,
    0x0047B010 NoSound (no DirectSound device 0x007D6A38, or platform 2 / 6).
  - CSfxManager: 0x0047B160 Construct, 0x0047AD80 GetPlayingIndex (unique id first, then by name - the name path
    reads two debug script globals), 0x0047AE20 AddPositionalSound (the count raised even when no entry is free,
    as the original), 0x0047AE90 StopGroup / 0x0047AEA0 UnloadGroup / 0x0047AF10 UnloadAllGroups (no groups in this
    build), 0x0047AF40 InitPlaybackProps, 0x0047B1B0 CleanUp (default dropoff back to 1020), 0x0047B210
    SetDefaultDropoff, 0x0047B240 StopAllSounds, 0x0047B2A0 UpdateVoiceVolumes, 0x0047B2C0 GetWaveTableEntry (level
    table first), 0x0047B340 IsPaused, 0x0047B360 LoadSound (the name after the last \ or /; 256 per table),
    0x0047B480 AdjustVolumeAndPitch (the game's 50 % first), 0x0047B4E0 StopSound, 0x0047B520 SetSoundParams,
    0x0047B5D0 SetMainVolume (below 0.1 silent; the user soundtrack too), 0x0047B650 SetPitch (at most 0.01; NaN
    kept), 0x0047B6B0 PrintSounds (compiled out), 0x0047B6F0 IsSoundPlaying, 0x0047B760 GetDropoffDistance,
    0x0047B7C0 SetSoundParamsScaled. The driver's voice calls go through the game's until OS-9.
  - Tests (selftests_objsys8.cpp): TestSfxManager - 60 seeds of random sound state (wave tables with duplicate
    names and every count up to full, playing sounds, the positional pool and list, settings, pause flags, the
    device and platform), 30 random calls each on random ids (mostly ones in the tables), every result and driver
    call recorded and the whole state compared.
  - Mutations: 37 deliberate bugs; 35 caught (33 at first; ids drawn from the playing table added for 2). The other
    2 are equivalent: UnloadGroup's count stored only when positive (unchanged otherwise), and SetPitch's clamp as
    `pitch < 0.01` (differs only at exactly 0.01, which it sets anyway).
- [x] Batch OS-9 (NOT run in the game yet; the sound effects manager, part 2: positional sounds and the PC
  driver's voices): src/game/sfx/sfx_voices.cpp (new); sfx.cpp now calls the voices directly.
  - CSfxManager: 0x0047B8E0 DopplerShift (the pitch moved by how fast the sound nears the camera; nothing past 360
    units a 60th - equal shifts), 0x0047BA00 GetPositionalProps, 0x0047BAB0 IsUniqueIdFree, 0x0047BB20
    GetUniqueId, 0x0047BB50 StopObjectSounds / 0x0047BC30 StopObjectSound, 0x0047BD20 PlaySound, 0x0047BE10
    GetVolume (falloff smooth / linear / squared / root; 5 % past the dropoff still "in range"; 3D; the
    stereo-positional branch is dead code - the type was just set to 3D), 0x0047C140 PlayObjectSound (a silent
    looping sound handed to the object to start later), 0x0047C360 UpdatePositionalSound (480 units past the
    dropoff: stopped, checked every 2 s; NaN dropoffs go on), 0x0047C560 UpdatePositionalSounds; 0x0047BA60
    CreateManager / 0x0047C6C0 ReleaseManager (reference counted).
  - The driver: 0x0047C720 GetFreeVoice, 0x0047C790 GroupCount (source only, 4 bytes), 0x0047C7A0 StopVoice,
    0x0047C7C0 / 0x0047C7D0 the voice volume and pitch, 0x0047C7E0 IsVoicePlaying, 0x0047C810 UpdateVoiceStates
    (0x0047B000, a jump to it: source only), 0x0047C870 PCWave::Load (the .wav chunks: fmt, data, smpl loops;
    the IMA ADPCM conversion 0x005F5A20 stays the game's; the samples buffer is the format's bytes-per-second long,
    as the original), 0x0047CA50 InitVoices (the voice count from the free hardware mixing buffers, 3D and the
    listener), 0x0047CBD0 StopAllVoices, 0x0047CC10 FreePermanentWaves, 0x0047CC70 LoadWave (data\sounds\*.snd),
    0x0047CD70 SetVoiceParameters (3D: the position relative to the camera, turned to the viewport's when another
    camera hears it; stereo: volume and pan in hundredths of a decibel; the frequency clamped 100-200000), 0x0047D130
    PlayVoice (a buffer made when the voice held another wave; locked, filled, played), 0x0047D390 SilenceVoices.
  - Found while testing: the CRT's _ftol is a 64-bit truncation (a frequency over 2^31 wraps instead of clamping to
    100; an infinite pan is 0) - kept; DopplerShift shifted at exactly the limit (the predecessor's `<=` was wrong);
    UpdatePositionalSound goes on for a NaN dropoff.
  - Tests (selftests_objsys9.cpp): TestSfxPositional - 80 seeds of random positional lists (duplicates too),
    playing sounds, wave tables, cameras and objects, 25 random calls each (every list entry's props first), with
    the cameras, the objects' sound parts, the clock, the voices, script globals and memory stood in for.
    TestSfxVoices - 100 seeds over recording DirectSound objects (device, buffers, 3D buffers, listener: creation,
    QueryInterface, Lock, Play, GetStatus and GetCaps answers scripted, failures too), every driver function at
    random. TestSfxWaveLoad - 60 seeds of random .wav files (chunk orders, sizes, loops, files cut short, a missing
    one) in a scratch folder, through LoadWave and PCWave::Load (the original's C runtime routed to ours). (States
    the original can't take - a voice with a wave but no buffer, a negative voice count, a failed primary buffer -
    left out.) The OS-8 test now stands in for our voice functions as well.
  - Mutations: 113 deliberate bugs; 107 caught (100 at first; two mutations rewritten to build; duplicates in the
    list, every entry's props, distances and pitches at the limits added). The other 6 are equivalent: the Doppler
    limit's `<=` (differs only at exactly the limit), the sign of a vector whose length is taken, a failed manager
    allocation (the instance was null already), GetVolume's `<=` on the loudest channel (always 0 there: nothing
    changes), the smpl read limit 0x20 (the loop count still read, the rest skipped alike), LoadWave freeing samples
    on failure (there are none yet).
- [x] Batch OS-10 (NOT run in the game yet; blending animation poses): src/game/gfx/anim_blend.{h,cpp} (new).
  - Gfx::CPose (0x840 bytes: 64 rotations, 64 translations, 64 valid flags). 0x0047D470 InterpolateRotation (a
    normalised lerp of quaternions, the shorter way; as the original, the `to` rotation is overwritten with the
    result - the higher pose's rotations change while blending; x87: two components and the length unrounded),
    0x0047D680 BlendPoses (weights[0] = 1 - the others; from the top down each pose with a weight goes into the
    next lower one with a weight, by 1 - lower / (lower + this + the weight taken so far); bones only the higher
    pose has are copied), 0x0047D8F0 BlendPosesById (the same over the poses with an id; weights under 0.00001
    left out; copied bones marked valid; as the original, the search for a lower pose doesn't stop at the first
    unless its id matches). Both copy `bones` poses' worth (bones * 0x840 bytes) to `out` when it isn't the poses
    - the original's size, kept (its callers pass out == poses).
  - 0x0047D580 AddTimelineInfo (code Ghidra missed, nothing calls it; stdcall: a timeline's anim, speed, loop type
    name from 0x006486BC and start / current / end times from 0x004813B0 into a structure).
  - The blender channel's pieces (Gfx::CBlendChannel, the rest not rewritten): 0x0047D420 SetFlag4C, 0x0047D430
    BlendTo(target, period), 0x0047D620 GetValue / 0x0047D640 HasValue (0x0048CF50 at the channel's key + an
    offset), 0x0047D660 ClearFlag (code Ghidra missed: flag 0x5DB4115F out of the owner's parameters).
  - Tests (selftests_objsys10.cpp): TestAnimBlend - 300 seeds: the rotation blend alone (opposite, zero and
    right-angle rotations, t outside 0-1), both pose blends over 1-7 random poses with weights 0 / small / negative
    / NaN, ids, 0-64 bones, out the poses or another buffer; the weights, every pose and the output compared.
    TestBlendChannel - 60 seeds of random channels: every piece and the timeline info (the structure, the lookup
    and the timeline's times stood in for).
  - Mutations: 36 deliberate bugs; 34 caught (33 at first; right-angle rotations added for 1). The other 2: BlendTo's
    `!(period != 0)` (the same test, equivalent) and the dot product's summing order (changes only a sign that
    rounding decides - not reached by the test data).
- [x] Batch OS-11 (NOT run in the game yet; the cutscene's objects, assets, lights, camera and poses):
  src/game/obj/cutscene_data.cpp (new); cutscene.h (CCutsceneData's layout filled in: the object entries +0x60,
  their count +0x2054, the animation entries +0x2058 / +0x2C58, the asset names +0x2C5C / +0x345C, +0x34B0...).
  - 0x00463AA0 QuatToMatrix (cdecl; the quaternion's x, y, z negated in place, as the original; x87: the squares
    and w*z unrounded, w*x and w*y rounded), 0x00464340 IdentityQuat::Set (code Ghidra missed; nothing calls it:
    source only), 0x00463BA0 SkaterModelName (the profile's model from array 0xCA1DB456, else "Male1" when
    0x3F813177 is 1, "Female1" otherwise), 0x00463DA0 ReadColour (percent; white when the structure is missing
    or 0x5A96985D is set; returns out), 0x00463E80 / 0x00463ED0 Add / RemoveCutsceneLights (the nodes of array
    0x2A79C44E: 0x00490570 makes a light from a node, 0x0048FCA0 removes it by node name), 0x004640D0
    SetUpCutsceneHeaps.
  - CCutsceneData: 0x00463C70 DestroyObjects (the objects that stood in for level objects give those back their
    model settings and the cutscene's colour; every object deleted), 0x00463F20 LoadAsset / 0x00463F70
    LoadAnimation (the asset manager 0x00408060; listed), 0x00464000 CreateObject (a composite object of type
    0xCDEB9229 from scripts 0x1F29C5A5 and 0x2E20EA32), 0x004641A0 ShowHiddenObjects, 0x00464220 UpdateCamera (the
    camera animation's rotation and position at the cutscene's time; the object animations updated unless
    skipped), 0x00464360 UpdateObjectPoses (each object's skeleton posed by its animation; within the first
    twelfth of a second without the bone list).
  - Found while testing: SkaterModelName's integer picks "Male1" when 1 (first written the other way round).
    Values read with the assert flag start at 0 in ours (the original's are uninitialised).
  - Tests (selftests_objsys11.cpp): TestCutsceneObjects - 120 seeds of random cutscene data (object entries,
    animations, times, flags) and a world of stood-in structures / arrays (answers from a hash of what's asked),
    objects, components, models, skeletons, the asset and memory managers, scripts and the camera animation; 10
    random calls each; every call, result, the data, the objects, the camera and the pose buffers compared. (The
    original's unchecked reads - a missing profiles array, objects or components in DestroyObjects - left out.)
  - Mutations: 40 deliberate bugs, all caught (37 at first; more integer values, the made object's memory and
    the twelfth-of-a-second boundary added for 3).
- [x] Game run (C:\Games\THUG2-4, this branch at OS-11 merged with main): BO free skate for 70 s - 2763 of 2763
  replacements installed, the level plays (skater, HUD, peds), late and engine-started self-tests all passed, no
  EXCEPTION / RUN-TIME CHECK FAILURE / DEBUG CRT REPORT. Two early self-tests failed in the game only (not offline):
  "skate net registration" / "skate net handlers" (selftests_tod4.cpp, the time-of-day agent's, from main: a handler
  address the original and ours record differently in the game) - not this range's.
- [ ] Next: OS-12+ the rest of the cutscene system 0x4645A0-0x4680B0 (OS-12: the pools, the data's constructor,
  0x464780, the big loader 0x4648D0 and its neighbours to 0x465AB0; then 0x465C00-0x466DD0; then 0x467710-0x4680B0).
