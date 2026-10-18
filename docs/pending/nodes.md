# Agent "nodes": Level nodes & composite objects, 0x45BDA0-0x45EFD0

Range finished: the 41 functions not yet rewritten, all with REPLACE (none source-only).

- [x] Batch nodes-1 (NOT run in the game yet when committed; see below): SkateScript's node functions, the node
  name hash, the prefix tables, the scan of scripts for calls, Obj_GetCollision.
  - **src/game/script/nodes.cpp / nodes.h** (new, 25): a node's name / links / position / angles / orientation by
    its structure (0x45BE40 GetNodeName, 0x45C020 GetLinkCount, 0x45BE10 GetLink, 0x45C420 GetLinkedNode,
    0x45C4B0 IsLinkedTo, 0x45C050 GetPosition, 0x45C180 GetAngles, 0x45C0B0 GetOrientation) and by name
    (0x45C1B0 GetNode, 0x45BE70 NodeExists, 0x45C3A0, 0x45C3E0, 0x45C460, 0x45C560, 0x45C590, 0x45C520);
    0x45C2D0 GetNearestNodeByPrefix; the instance wrappers 0x45C1D0-0x45C230 (walk, last node array); 0x45C950
    ForEachScriptCall and 0x45CCA0 ForEachNodeScriptCall (the gap list, 0x578AC0, finds every EndGap call in the
    nodes' TriggerScript= this way).
  - **src/game/script/node_prefixes.cpp / .h** (new, 12): the nodes by name prefix: PrefixNodes::Add 0x45BDA0,
    PrefixTable (Construct 0x45C5D0, Destruct 0x45CD30, Add 0x45C650, Compact 0x45C240), PrefixTables (instance
    0x6F0D80: AddNodeArray 0x45C700, RemoveNodeArray 0x45CDF0, GetNodes 0x45BE90), 0x45C900 AddNodeArrayPrefixes
    (on the script heap), 0x45CE40 RemoveNodeArrayPrefixes, 0x45BF90 / 0x45BFB0 GetPrefixedNodes (checksum / text).
  - **node_manager.cpp**: CNodeManager::HashNode 0x45CE60, UnhashNodeArray 0x45CEF0, GetNode 0x45CFC0; the
    manager's own calls to these and to the node / prefix functions are now direct.
  - **composite.cpp**: CCompositeObject::Obj_GetCollision 0x45E9E0 (member function 0x0168B09C).
  - Findings: nodes of class=link (0xC953660E) aren't hashed. A node name's prefixes are the text before each '_'
    (except "TRG", case-insensitive) and the name without trailing digits; "X_" and "X" both become known checksum
    names. GetNodes' static buffer (0x6EFDE0, 1000 names) and the 100- / 1000-byte name buffers aren't bounds
    checked (kept). GetLink reads a null links array if a node has none (kept). GetNearestNodeByPrefix doesn't reset
    its position between nodes: a node without pos= is measured where the previous one was (the first such node:
    ours uses the origin, the original stack garbage). The scan follows script calls, SetEventHandler's scr= and
    EndGap's gapscript= to depth 5; names that aren't calls are passed over alone, so their arguments are scanned
    as statements. Obj_GetCollision: a feeler from pos + up*height + at*distance + right*shift (feet, height and
    length default 3) along right (side) / down (down) / at; returns whether it hit (several sums kept at 53 bits:
    written out in double).
  - Names recovered from the .qb files' checksum tables: link, EndGap, gapscript, shift, objectDistance.
  - Test: src/hook/selftests_nodes.cpp (TestNodes, Early): four original-vs-ours runs on stand-ins (hash with
    collisions and wrap-round, lookups, links, positions, quaternions incl. clamping; prefix tables over three node
    arrays, growth, removal from the middle, a chained bucket; the scan over a script exercising every token case,
    the depth limit, the node walk; Obj_GetCollision's seven parameter sets). 71 deliberate bugs: 65 caught at
    first, 5 more after strengthening the test data; 1 equivalent (an empty name text vs a null one in
    AddNodeArray: the loop doesn't run either way). selftests_cobj.cpp's stand-ins now also cover our versions of the
    functions the node manager calls directly.
  - Checks: 0 MISMATCHED argument sizes, all early self-tests pass, register scan (every function whose result is
    read returns one), audit of GameFunction calls (no MISMATCH), check_code_writes (no overlaps), find_gaps
    (nothing left in the range, 0 thunks).

## Classes defined
- SkateScript::PrefixNodes, PrefixTable, PrefixTables (node_prefixes.h; all their code is in this range).
- No new layout for Nx's collision feeler: composite.cpp uses a 0xB0-byte buffer with offset accessors
  (CollisionFeeler, anonymous namespace), as park_floor.cpp / level_editor.cpp do.
- Extended (additively): SkateScript::CNodeManager (three methods, NodeManagerInstance()), Obj::CCompositeObject
  (Obj_GetCollision).
