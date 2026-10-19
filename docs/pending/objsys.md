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

- [ ] Next: OS-7 path follower (path_follower.{h,cpp}, selftests_objsys7.cpp); OS-8 sound effects manager part 1
  0x47AC10-0x47B7C0 (src/game/sfx/sfx.{h,cpp}, selftests_objsys8.cpp written), OS-9 part 2 0x47B8E0-0x47D390
  (sfx_voices.cpp written, test to write); then the animation blending 0x47D420-0x47DC31 (+ 0x47D580 / 0x47D660,
  code Ghidra missed), the cutscene system 0x463AA0-0x4680B0 (+ 0x464340).
