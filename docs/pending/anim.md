# Animation & skeleton / skinned model data (0x406460-0x41A6D0) - anim agent

Branch worktree-agent-a51ba86b0cf626a18. Tests in src/hook/selftests_anim*.cpp (kit: src/hook/anim_testkit.h).
Shared files touched: CMakeLists.txt (new files listed), src/game/mth/vector.cpp / vector.h (two functions added,
ProjectToPlane made exact).

- [x] Batch AN-1 (NOT run in the game yet; the task lists and two angle helpers):
  src/game/core/task.h / task.cpp (new), src/game/mth/vector.cpp.
  - Mth: 0x00406460 GetAngle (degrees between two vectors; x87: lengths, unit vectors and the dot product in
    double, the cosine rounded to a float and limited to [-1, 1]; the result in ST0 unrounded - returned as a
    double), 0x00406630 GetAngleAbout (radians from a to b seen along an axis: both projected onto the plane
    across it, signed by (axis x a) . b). Vector::ProjectToPlane (0x00406080, rewritten earlier) made exact: the
    original keeps the dot product and x's part along the normal in double - the rounded version gave visibly
    different angles for vectors nearly along the axis.
  - Tsk::BaseTask: 0x00406890 constructor (node allocated with the priority), 0x00406930 destructor, 0x00406980
    scalar deleting, 0x004068F0 Remove, 0x00406850 InList. Tsk::List: 0x00406A50 constructor, 0x00406A20
    destructor, 0x004069B0 MarkChanged, 0x00406A80 AddTask (the task's stamp set to the list's - 1: it runs in a
    pass going on), 0x00406AC0 Process (the first argument unused; a pass restarts from the top when tasks are
    added / removed, skipping those already run by their stamp; disabled tasks and those sharing a mask bit
    skipped; nothing while locked), 0x00406BB0 RemoveAllTasks. Tsk::StackList (List + its node): 0x00406D60,
    0x00406C30. Tsk::Stack: 0x00406E80 constructor (the mask at +0x04 left unset), 0x004070E0 destructor,
    0x00406DE0 Push, 0x00406FB0 Pop (a list popped while the stack runs waits in +0x1C until the pass ends; an
    empty stack / a failed allocation crash as in the original), 0x00406C90 AddTask (the current list),
    0x00406CB0 RemoveAllTasks, 0x004070A0 Process, 0x00406F10 DeletePoppedLists. The nodes', heads' and local
    searches' destructors: 0x00406A00 / 0x00406860 (Lst::Node<BaseTask>), 0x00406D00 / 0x00406D20
    (Lst::Node<StackList>), 0x00406E50 (Lst::Head<StackList>), 0x004069C0 / 0x004069D0, 0x00406CA0 / 0x00406CD0
    (the Lst::Search vtables).
  - Tests (selftests_anim.cpp): TestAnimAngles - 3000 random triples (zero, tiny (< 1e-30), huge, parallel,
    opposite, along the axis), compared to 1e-13 (the C runtime's acos differs from the game's in the last bit);
    TestAnimTasks - a stack of 10 tasks with recording callbacks that remove the next / a later task / themselves,
    add tasks, push and pop lists while the stack runs, over 11 passes; a lone list with masks and a lock; the
    destructors in and out of lists; a second stack popped unlocked and locked and destroyed with lists waiting;
    the node / head / search destructors on their own. Allocation and frees stood in for; in our run the game's
    vtables point at ours (as with the hooks in). Every call, allocation, free and the whole arena compared.
  - Mutations: 46 deliberate bugs; 45 caught (39 at first: the stack's state right after construction, a lone
    removal outside a pass, the next task removed during a pass, a node destructor's vtable, freeing with flag 1
    and vectors shorter than 1e-30 added for 6). The other one is equivalent: RemoveAllTasks marking the list
    changed (each Remove marks it already).
- [x] Batch AN-2 (NOT run in the game yet; the asset base class and the asset manager):
  src/game/ass/asset.h / asset.cpp, assman.h / assman.cpp (new).
  - Ass::CAsset (0x4C bytes, vtable 0x00645568; the slots are listed in asset.h): 0x00407910 constructor,
    0x00407810 destructor, 0x004093E0 (a jump to it, the types' body; source only), 0x00407950 / 0x00407AF0 the
    scalar deleting ones, 0x004075F0 the base Load / LoadFromBuffer (loading; 0), 0x00407600 Unload, 0x00407610
    IsLoaded, 0x00407620 ProcessBuffer, 0x00407630 FinishLoading, 0x00407640 Abort (the read cancelled and closed),
    0x004076D0 GetName, the setters 0x004076E0 / 0x004072F0 / 0x004076F0 / 0x00407700 / 0x004075D0, the state
    setters 0x00407670-0x004076C0 (0 unloaded, 1 loading, 2 read, 3 processing, 4 loaded, 5 aborted),
    0x00407840 AddDependent, 0x00407890 RemoveDependent, 0x00407710 the async read's callback (not a function in
    Ghidra: found from its references), the dependents list's destructors 0x004077B0 / 0x004077D0 / 0x004078E0.
    The types' constructors: 0x004071A0 animation, 0x00409220 scene, 0x004095F0 scene collision, 0x00409970
    skeleton, 0x00409C40 skinned model, 0x0040A210 skin collision, 0x0040A580 texture dictionary, 0x00407A30
    cutscene, 0x00408BB0 node array, 0x00408BF0 (a node array made the node manager's owner). The type numbers
    from the extension table 0x0067C650 (BIN 1, SCN/GEOM 2, TEX 3, COL 4, SKA/FAM 5, SKE 6, MDL/SKIN 7, 8 skin
    collision, CUT 9, QB 10, 11 an owned node array).
  - Ass::CAssMan (0x3C bytes, a module; vtable 0x00645658): 0x00407F80 constructor, 0x00408930 / 0x00408A10
    destructors, 0x00407E90 / 0x00407AE0 start / stop (its task into the main loop's logic tasks), 0x00408060
    LoadAssetFromStream, 0x00408440 LoadAsset, 0x004087B0 LoadOrGetAsset, 0x00408870 LoadAnim, 0x00407EC0
    ReloadAsset, 0x00407B20 UnloadAsset, 0x00407CF0 / 0x00407D40 / 0x00407F20 / 0x00407F50 the unload-alls,
    0x00407B90 GetAsset (waits; IsLoaded asked twice first), 0x00407C00 / 0x00407C70 GetAssetData (by name),
    0x00407CA0 AssetExists, 0x00407A50 / 0x00407A60 (source only, 4 bytes) / 0x00407A70, 0x00407DD0
    ProcessPendingAssets and 0x00407EB0 its task callback (both not functions in Ghidra), 0x00407A80
    RemoveFromList, 0x004079C0 GetTypeFromExtension, 0x004088E0 CreateAssetManager, 0x00407980
    ReleaseAssetManager.
  - Findings: the script integer 0x25DC7904 set keeps a failed asset (not deleted; a debug leftover, LoadOrGet
    looks it up for nothing). LoadAssetFromStream has no cutscene case and a type that isn't one crashes (a null
    asset). The asset lists are circular, but ProcessPendingAssets' walk ends only at a null link and
    RemoveFromList's search only at null too: with an asset pending, or one in the other list, neither would end -
    async loading looks unused (every caller in the exe loads synchronously as far as seen). LoadAnim loads with
    the manager's +0x14 "permanent" flag.
  - Tests (selftests_anim2.cpp): TestAnimAssetBase - every constructor on dirty memory, the base methods through
    the vtable, abort with and without a file, the read callback, dependents added / removed (first, middle, last,
    missing, empty), the destructors; TestAnimAssetManager - made / started / stopped, loads of every type from files
    and memory (by extension and type, groups, async, failing with and without the debug integer, the one-shot and
    kept callbacks), lookups (waiting, a key-0 item behind a flushed bucket head), reload, LoadOrGet, LoadAnim,
    the pending list processed (a null-ended chain: loaded / read / processing that fails / loading / waiting on its
    group), the unload-alls, RemoveFromList, the extension table, construction on dirty memory, destroyed through
    the references. The types' Load / LoadFromBuffer / Unload / Reload / FinishLoading, the cutscene's IsLoaded,
    the memory manager, pool, async file calls, _stricmp and the script integer stood in for; in our run the asset
    vtables point at ours. Every call and the arena compared.
  - Mutations: 64 deliberate bugs; 63 caught (56 at first: a base destructor on a typed asset, the key-0 lookup,
    construction on dirty memory, the manager really destroyed (three references), a cutscene's IsLoaded counted,
    a name whose only "extension" is its first letters added for 7). The other one can't be tested: a cutscene
    from LoadAssetFromStream (the original crashes).
- [x] Batch AN-3 (NOT run in the game yet; the asset types): src/game/ass/asset_types.cpp (new), asset.h.
  - The load parameters' pool: 0x00407730 AllocLoadParams (4 blocks of 0x60 at 0x00691758; the size ignored),
    0x00407770 FreeLoadParams. 0x00408A70 the types' Reload (Unload, then Load(name, 0, 0, 0, 0) == 0).
  - Animation: 0x004072E0 / 0x004072D0, 0x004071C0 LoadFromBuffer, 0x00407300 Load ("<name>.<platform
    extension>", from a file or through Pip - then the lowercased name's checksum kept at +0x4C), 0x004074F0
    FinishLoading (Gfx's anim frame data, 0x3C bytes: 0x00482AF0 / 0x004856C0; Load is called on a failed
    allocation too), 0x004074A0 Unload, 0x00407290 FreeBuffer (also the node arrays').
  - Cutscene: 0x00408AC0 / 0x00408AB0, 0x00408AD0 Load (a CCutsceneData loaded; no StartLoading; the data leaks
    if Load fails), 0x00408A40 Unload (only the data deleted), 0x00408AA0 IsLoaded (the data's).
  - Node array: 0x00408BE0 GetType (10, or 11 when the node manager's owner) / 0x00408BD0, 0x00408EA0 /
    0x00408D10 (the file read on the top-down heap; a missing file returns with the heap context still pushed, as in
    the original), 0x00408C10 FinishLoading (0x005A5990 parses the script, the node array added to the node manager,
    the script array kept), 0x00408F00 Unload (each node's object found by its name and told - vtable slot 6 -, the
    node array removed, the script unloaded by 0x005A59B0), 0x004090D0 CreateNodeArray (a new array of empty
    structures made a global symbol).
  - Scene: 0x004093D0, 0x004092D0 SetUpParams (textures checksum 0xDC8116B2, four flags), 0x00409520 / 0x004093F0
    (the file name from 0x0048C390), 0x00409240 FinishLoading (0x0048CD80 with the group's textures), 0x00409380
    Unload (0x0048C690). Scene collision: 0x00409750, 0x00409690, 0x00409890 / 0x00409760 (0x0049CA20),
    0x00409610 (added to the group's scene: 0x0049D590 / 0x0049C9F0), 0x00409700 (0x0049CFB0).
  - Skeleton: 0x00409A40 / 0x00409A30, 0x00409990 LoadFromBuffer (never copies), 0x00409A50 Load (the buffer on
    the top-down heap), 0x00409B80 FinishLoading (the skeleton data, 0x598 bytes: 0x004A2C20 / 0x004A39C0; false and
    a leak if it fails), 0x004099E0 Unload, 0x00409A10 FreeBuffer.
  - Skinned model (0x60: +0x4C archive, +0x50 parameters, +0x54 / +0x58 the .cas file and its contents, +0x5C the
    files still being read): 0x00409E00 / 0x00409DF0, 0x00409E10 SetUpParams, 0x00409E90 the parameters' Clear,
    0x0040A140 / 0x00409EB0 Load ("<base>.skin|mdl.<ext>" - 0x0048BFE0 says whether the name has a .cas file -
    through Pip when it has one and the load isn't async; then "<base>.cas.<ext>"), 0x00409C70 FinishLoading
    (0x0048C030), 0x00409D30 Unload, 0x00409D70 Abort (the .cas read too), 0x00409DA0 FreeBuffer, 0x00409C60
    SetRead (the async read's: read only once both files are; not a function in Ghidra).
  - Skin collision: 0x0040A290 / 0x0040A280, 0x0040A400 / 0x0040A2A0 (0x00490AD0), 0x0040A510 (added to the
    group's model: 0x00490B80; the data its +0x8C), 0x0040A230 (0x00490A90). Texture dictionary: 0x0040A710,
    0x0040A620 SetUpParams, 0x0040A840 / 0x0040A720 (0x0049EF90), 0x0040A5A0 (0x0049F050), 0x0040A6C0 (0x0049EFD0).
  - Tests (selftests_anim3.cpp): TestAnimAssetTypes - for every type: 10 Loads (blocking / async, file / archive,
    parameters / a structure / neither, the file - or the .cas file - missing, a mixed-case name), each then unloaded;
    4 LoadFromBuffers (copied or not, parameters with the skin's heap flag set); FinishLoading on its own with the
    engine's loader failing and working, then Unload with a buffer still there; FreeBuffer with an archive, a buffer,
    nothing; the names and types. Also the owned node array's type, CreateNodeArray (an old symbol and none), the
    skin's Abort / SetRead / Clear, Reload, the parameter pool exhausted. Every engine, file, Pip, script, node
    manager and cutscene call stood in for (recorded); in our run the asset vtables point at ours; the asset, the
    parameter pool and the arena compared after each step.
  - Mutations: 70 deliberate bugs; 69 caught (62 at first: dirty file handles, distinct object vtable slots, a
    hashed flag answer, a clean parameter pool per case, FinishLoading's result and Unload with a buffer, the
    skin's copy with its heap flag added for 7). The other is equivalent: the archive name's lowercasing (the
    checksum ignores case).
- [x] Game run (2026-10-20, C:\Games\THUG2-1, stock THUG2.exe, AN-1..AN-3): free skate in Boston (-level BO) for
  90 s - running and responding, the skater in the level; 2953 of 2953 replacements installed, late and
  engine-started self-tests passed, shadow checks no mismatches; no EXCEPTION / RUN-TIME CHECK FAILURE / DEBUG CRT
  REPORT. Two early self-tests failed that aren't this branch's: "anim controllers: partial" (fe_anim) and "text
  elements: updates" - the fe test kit's half-pointer mapping: the arenas landed near 0x00FC0000 in the game, where
  plain values 0x00FF0006 / 0x01000000 look like halves of arena pointers in our run only (also seen once offline).
- [x] Batch AN-4 (NOT run in the game yet; math helpers and the collision caches): src/game/nx/coll_cache.h /
  coll_cache.cpp (new), src/game/mth/vector.cpp / matrix.cpp / geometry.cpp (+ headers).
  - Mth: 0x00406560 IsAngleBeyond (not a function in Ghidra; called by the skater code: whether two vectors are
    more than `degrees` apart - false if either is shorter than 0.5; x87 FCOS of the limit, the rest in double),
    0x0040AED0 Vector::operator==, 0x0040AF20 operator/(Vector, float) (the reciprocal rounded to a float for x, y, z,
    w multiplied by the unrounded one), Matrix 0x0040AF60 Transform / 0x0040B000 TransformAsPos / 0x0040B090
    RotateVector (each sum in the original's order, rounded once), 0x0040B100 ScaleRows, BBox 0x0040AB00
    operator=, 0x0040B170 Intersects (x, z, y; NaN doesn't rule it out), 0x0040B1D0 Contains (NaN does).
  - Nx::CCollCache (0xFCC bytes: a box, 500 (object, geometry) entries - the static ones first -, the counts at
    +0xFC0 / +0xFC4 / +0xFC8): 0x0040AB50 constructor (registered), 0x0040AB40 destructor, 0x0040ABA0 Update (the
    grid lists along the box's diagonal - 0x0048C980 -, each list's objects - 0x004188C0 - and the movable list
    0x00691984 (objects with flags 2 / 4 skipped) tested against the box grown by 0.5 - vtable slot 13; the
    geometry from slot 21 / 20), 0x0040A910 RemoveStatic (shifts), 0x0040A990 RemoveMovable (the last moved in);
    the registry (8 caches at 0x006918EC, bits 0x0069190C): 0x0040AA00 / 0x0040AA40 / 0x0040AA80 / 0x0040AAC0;
    0x0040ADD0 ShapeTypeFromChecksum (box and two others 1, sector 4).
  - Tests (selftests_anim4.cpp): TestAnimMathHelpers - 4000 random cases (zeros, -0, NaN, small integers, +-3e19,
    equal vectors, scaled copies; vectors 0.25-0.6 long; b exactly 0..179 degrees from a with limits on and around
    it; 81 matrices of cancelling columns), each helper's result bit for bit, in place too; TestAnimCollCaches -
    three caches and six more registered (the ninth refused), updated with no grid lists and with lists and
    movables (flags 2 / 4 / null data), removals from each part and from all caches, a double registration and
    unregistrations, the shape types; the grid / list queries and the objects' slots stood in for (recorded).
  - Mutations: 37 deliberate bugs; 36 caught (24 at first: lengths at the 0.5 limit, angles at their limit, the
    cancelling matrices, the whole grown box recorded, removals of the other part's objects, the stale registry
    pointer added for 12). The other is equivalent: the grown box's w (AddPoint ignores it).
- [x] Batch AN-5 (NOT run in the game yet; the collision object base and the multi-object): src/game/nx/coll_obj.h /
  coll_obj.cpp (new).
  - Nx::CCollObj (0x10 bytes, vtable 0x00645A30: +0x04 checksum, +0x08 flags - 0x8000 owns its geometry, 0x4000
    shares it -, +0x0A terrain, +0x0C geometry; the vtable slots as far as known are listed in coll_obj.h):
    0x0040B370 SetGeometry (the geometry's checksum kept), 0x00413390 the scalar deleting destructor (the base's
    body: an owned, unshared geometry destroyed - 0x00416970 - and freed).
  - The lists of collision objects: 0x004131D0 / 0x00411880 Lst::Node<CCollObj> (vtable 0x00645AA0), 0x00413180 /
    0x004131A0 Lst::Head<CCollObj> (0x00645D34).
  - Nx::CCollMulti (0x3C bytes, vtable 0x00645AA8: +0x10 its children's list, +0x14 their box, +0x34 dirty, +0x35
    the box valid, +0x38 a value): 0x004133E0 constructor, 0x00411D10 copy constructor, 0x00411B10 destructor
    (each child and its node deleted, the head, out of the caches, the base's body), 0x00413460 scalar deleting,
    0x00413490 Clone (the copy flagged 0x8000, a clone of each child), 0x00411BE0 AddChild, and the slots
    0x00411D90 (1: every child's, stopping at the first false), 0x00411CC0 / 0x00411C30 / 0x00411C80 (4, 5, 6:
    passed to the children; 4 and 5 mark the box dirty), 0x00411F60 / 0x00411F80 (7, 8: the first child's - a
    tail jump; no children crashes, as in the original), 0x00410960 (9: a zero vector), 0x0040AE20 / 0x0040AE10
    (10, 11: +0x38; the getter source only, 4 bytes), 0x0040AE30 (3: terrain 0; source only), 0x00411E80 (13:
    false only if the box is known and misses), 0x00411EC0 (14: every child asked, any hit), 0x00411F20 (19),
    0x00411E50 (20: the box, recomputed when dirty; null if a child has none), 0x00411DD0 RecomputeBBox.
  - Tests (selftests_anim5.cpp): TestAnimCollMulti - three multis on dirty memory, stand-in children (every slot
    recorded with its arguments), a nested multi, a child that answers false, one without a box; each slot (the
    dirty flag after each pass-down), the box tests far and near (also with no box), Clone, the copy, deletion with
    an owned / shared / unowned geometry and a registered cache holding them, the base's deleting destructor
    freeing and not, the list node / head functions; allocation and frees stood in for, our vtables in our run.
  - Mutations: 35 deliberate bugs, all caught (28 at first; the unowned geometry, freeing with flag 1, a registered
    cache, the clone's flags, the dirty flag after each call and the box test with no box added for 7).
- [x] Batch AN-6 (NOT run in the game yet; the movable collision objects and raw vertices): src/game/nx/
  coll_movable.h / .cpp, coll_tri.h / .cpp (new).
  - Nx::CCollObjTriData (the geometry; only what's used so far): 0x0040B230 / 0x0040B2D0 GetRawVertexPos (16-bit
    vertices in sixteenths from the box's minimum, or floats - the second copies those as raw words; w = 0).
  - Nx::CCollMovable (0xA4 bytes, vtable 0x00645B00: +0x10 position, +0x20 orientation, +0x60 its transpose,
    +0xA0 a value): 0x00411FA0 constructor, 0x00412660 copy, 0x004121A0 destructor (out of the caches' movables),
    0x004135B0 scalar deleting, 0x00412070 SetOrientation (rows 0-2, the transpose of the whole matrix; 0x00412FF0
    jumps to it - source only), 0x00412050 / 0x00412060 (source only: 4 bytes, shared with the scaled object),
    0x00412230 GetVertexPos, 0x004122C0 BoxToLocal, 0x0040AE50 / 0x0040AE40. 0x004120D0 / 0x00412150 the matrix
    transpose (from another / in place).
  - Nx::CCollMovBBox (0xD0 bytes, vtable 0x00645BC0: a box, one set of face flags and terrain): 0x00412550
    constructor (the default box (-50, -20, -90)..(30, 36, 90)), 0x00413700 copy, 0x004125F0 / 0x004136D0
    destructors, 0x00412600 SetGeometry (also its box; a null geometry crashes, as in the original), 0x0040AE60 /
    0x0040AE70 face flags / terrain, 0x004126E0 DebugRender (the box through 0x0048D090).
  - Nx::CCollScaled (0xA0 bytes, vtable 0x00645B58: scale, matrix, inverse): 0x004123C0 constructor, 0x00412460 /
    0x004135E0 destructors, 0x00413610 / 0x00413640 / 0x004136A0 set position / orientation / scale,
    0x00412490 UpdateInverse (each row's 1 / scale unrounded for its three products), 0x004109C0 (source only),
    0x004109D0 GetVertexPos (scaled, w the scale's w, through Matrix::Transform - row 3 counts by that w).
  - Tests (selftests_anim6.cpp): TestAnimCollMovables - every function on objects built on dirty memory with random
    transforms and compressed / plain geometry (each object recorded after its constructor), copies of objects
    with distinct fields, transposes in place and not, the debug box, the destructors with owned and shared
    geometry and a registered cache holding the movables.
  - Mutations: 35 deliberate bugs; 33 caught (26 at first: each object recorded right after construction, distinct
    terrain and +0xCA bytes for the copies added for 7). Equivalent: the movable vertex's w (always 0 + the
    position's), the box destructor's own vtable write (overwritten at once by the base's).
- [x] Batch AN-7 (NOT run in the game yet; the triangle collision objects - a sector's, an instance, a movable
  one; 37 functions): src/game/nx/coll_static.h / .cpp (new), coll_movable.cpp (CCollScaled copy), coll_tri.h.
  - Faces: 0x00410BF0 / 0x00411750 the face flags (u16 at face +0, zero-extended) / terrain (u16 at +2), faces of
    8 bytes when the geometry's +0x0A byte is set, else 10 (slots 2, 3 of all three objects).
  - Nx::CCollStatic (0x18 bytes, vtable 0x00645C18; +0x10 the "face-rejection optimization" buffer, a byte per
    face): 0x00412760 destructor (the buffer freed only if owned, flag 0x8000), 0x004139A0 Clone (hidden code:
    an instance - a CCollStaticInstance sharing the geometry - or a copy with the geometry copied by 0x00416BF0
    and the buffer copied on the bottom-up heap; null if the geometry copy fails), 0x00410C20 SetWorldPosition
    (the geometry moved by the difference from slot 7's, 0x00416400), 0x004123A0 / 0x004123B0 (the shared origin
    0x00691914 / orientation 0x00691928), 0x00410980 GetVertexPos (w = 1), 0x00410C70 / 0x00412750 the box.
  - Nx::CCollStaticInstance (0xC8 bytes, vtable 0x00645C78: a CCollScaled with a world box): 0x004127C0
    constructor, 0x00414C30 copy, 0x00412840 / 0x00413ED0 destructors, 0x00414CD0 Clone (shares the geometry),
    0x00411480 SetSharedGeometry (flags |= 0xC000, the geometry's box), 0x00413F00 / 0x00413F30 (hidden) /
    0x00413F50 set position / orientation / scale (the inverse and world box made again), 0x004113B0
    UpdateWorldBBox, 0x00411460, 0x00412830, 0x00411330 RescaleIfAt (only at its exact position), 0x00412F50
    DebugRender (the scaled matrix to 0x00414D60). CCollScaled's copy constructor 0x00413F80.
  - Nx::CCollMovTri (0xD4 bytes, vtable 0x00645CE0: radius, dirty flag, box): 0x00413000 constructor, 0x00413080
    copy, 0x00413070 / 0x004143D0 destructors, 0x00414400 Clone (geometry copied), 0x00411780 SetGeometry (the
    bounding radius from the raw vertices), 0x00412F90 SetWorldPosition (hidden code), 0x00411810 / 0x00412FC0
    (the box made again when dirty), 0x0040B390 UpdateBBox, 0x00413130 DebugRender. 0x00414D60 the geometry's
    debug render (empty; source only).
  - Tests (selftests_anim7.cpp): TestAnimCollTriObjects - a sector object built by hand on compressed and plain
    stand-in geometry: faces small and not, the slots, the vertex, the clones (instance, copy with and without
    its buffer, geometry copy failing); the instance's setters (recorded after each), world box, rescale at /
    not at / off only in y, clone, render (0x00414D60 stood in for, recording the matrix); the movable's geometry,
    radius, position, box (recorded after each step), clone, render; destructors with owned and unowned buffers.
  - Mutations: 42 deliberate bugs; 38 caught (28 at first: the origin's w, owned / unowned buffers in the
    destructors, the buffer before SetSharedGeometry, recording after each setter, a position off only in y and
    a stand-in for the debug render added for 10). Equivalent: the scaled vertex's w in UpdateWorldBBox (unused by
    the box), the constructor's dirty flag (SetGeometry sets it), the radius compare's > vs >= and its sum order
    (the same float after the square root).
- [x] Batch AN-8 (NOT run in the game yet; the collision geometry and its BSP tree; 27 functions): src/game/nx/
  coll_tri.h / .cpp, mth/matrix.cpp; coll_obj.cpp / coll_static.cpp now call the geometry's methods directly.
  - Nx::CCollBSPNode (8 bytes, new[]'d: +0 the axis in the low 2 bits - 3 a leaf - and the split in sixteenths
    above; a leaf's face count at +2 and face index list at +4; a split node's two children at +4, bit 0 swapping
    which is the "less" side): 0x00414D70 constructor (hidden code, zeroed), 0x00415C90 / 0x00415CE0 node /
    face counts, 0x00415D30 the first face list (the lowest pointer: the shared array), 0x00415D80 Translate,
    0x00415E00 RotateY (quarter turns about Y around a centre: x and z splits swap axis, some negated with their
    sides swapped), 0x00415F30 Scale, 0x00416000 Relocate (a file's offsets to pointers; cdecl), 0x00416A10
    Clone (nodes and face lists copied, pointers moved; the argument unused).
  - Nx::CCollObjTriData: +0x34 the tree, +0x38 one byte per vertex. 0x00414EE0 SetRawVertexPos (compressed:
    sixteenths toward zero, x's offset not rounded to a float first), 0x00414F90 GetRawVertices (0x00415C00
    jumps to it: source only), 0x00415C10 SetRawVertices (the box first), 0x00414D80 GetFaceNormal ((v1 - v0) x
    (v2 - v0); one edge's z unrounded in the x term, as the original's x87 code), 0x00415B70 CopyFrom, 0x00416BF0
    Clone (vertices, faces, vertex bytes, tree; null without vertices when asked, or when an array isn't
    allocated), 0x00416970 destructor, 0x00416080 DeleteBSP (delete[]'s element destructor 0x005480A0 is
    empty), 0x004169C0 Relocate (first argument unused), 0x00416400 Translate (plain vertices, box with w, tree),
    0x00416320 GetFacesInBox / 0x004160E0 its walk (globals 0x00691968 buffer, 0x0069196C count - -1 on
    overflow, then every face: the index table 0x0067C6B8 - and 0x00691970 the buffer size, only stored),
    0x004163B0 IsPointBehindFaces / 0x004161D0 its walk (down the "less" sides only; a leaf's faces counted until
    the point is in front of one; true when that's every face of the geometry).
  - Mth::Matrix::operator== 0x00416D80 (all 16 values; NaN unequal).
  - Tests (selftests_anim8.cpp): TestAnimCollGeometry - two hand-made geometries (plain with 10-byte faces,
    compressed with 8-byte faces) and trees with swapped sides: counts, first lists, every face's normal (one
    degenerate), raw vertices got and set (one offset not exact in a float), boxes reaching some leaves, all
    (overflowing at 2, 4, 8 faces; exactly 9 of 9), none, no tree; points down each path and on a face's plane;
    moves, each quarter turn with and without the centre, scaling (a split needing more than a float's bits), a
    file's geometry fixed up, copies with / without vertex bytes and vertices, the tree's copy and a leaf's, a
    16-bit leaf count, deletion; Matrix == with one value changed, NaN, -0 / +0. Our run's stand-ins also cover
    our own Destruct / Clone / Translate in the AN-5..AN-7 tests.
  - Mutations: 72 deliberate bugs; 68 caught (57 at first: the leaf count's high byte, an offset not exact in a
    float, vertex bytes on an empty copy, overflow early and exactly at the face count, a point on a face's plane,
    a big split added for 11). Not caught: the squared length's sum order (below a float), the buffer size
    global (stored and cleared, never read), the point's x offset kept unrounded (x87 detail too small for the
    test data), the nodes' constructor before the copy overwrites them.
- [x] Game run (2026-10-20, C:\Games\THUG2-1, stock THUG2.exe, AN-1..AN-8): free skate in Boston (-level BO), 90 s
  standing and 45 s skating (keys posted: forward, turns; the skater rode along the plaza and up a wall) - running
  and responding; 3087 of 3087 replacements installed (69 source only), early (314), late (17) and engine-started
  (2) self-tests passed; no EXCEPTION / RUN-TIME CHECK FAILURE / DEBUG CRT REPORT.
- [x] Batch AN-9 (NOT run in the game yet; the geometry turned and scaled, the forwarding slots, the movable
  collision factory; 11 functions): src/game/nx/coll_tri.cpp, coll_movable.cpp, coll_static.h / .cpp.
  - Nx::CCollObjTriData: 0x004164F0 RotateY (quarter turns about Y around a centre: moved there, the tree turned,
    compressed vertices through a temporary array on the top-down heap, the box rebuilt from its turned corners,
    moved back, +0x3C cleared; a failed allocation of the array crashes, as in the original), 0x00416730 Scale
    (the same; the box's corners scaled - w kept, not re-sorted), 0x00414D30 SetUpAllFaces (the shared index
    table 0x0067C6B8 filled with 0..2047 while its first entry isn't 0; whether there's a tree).
  - Nx::CCollMovBBox::Clone 0x00414BE0 (slot 12: a copy, flag 0x8000, no geometry).
  - The slots that pass on to the geometry (Nx::CollGeometryForwarders): 0x0040AE80 / 0x0040AE90 (a sector's slots
    22, 23: turned / scaled), 0x0040AEA0 (its slot 19: the geometry's debug render 0x0048DCD0, compiled out),
    0x0040AEB0 (0x00414FE0 - not rewritten yet), 0x0040AEC0 (slot 18 of every collision class:
    IsPointBehindFaces).
  - Nx::CreateMovableCollision 0x00414990 (cdecl): type 1 a box around all the geometries' boxes, type 4 a movable
    triangle object (several: a multi-object of them; none: null), other types null; the value set through slot
    11 and the object registered (0x004177B0, not rewritten yet).
  - Tests (selftests_anim9.cpp): TestAnimCollTurnScale - plain and compressed geometry with and without a tree,
    each quarter turn 0-4 and a scale with a negative factor, around centres; the face table filled (first entry
    0xFFFF, another non-zero value) and not (0); the box's clone; each forwarding slot on a sector object (the
    debug render, 0x00414FE0 and the registration stood in for); the factory for types 1, 4, 2, 0 with 0, 1 and 3
    geometries.
  - Mutations: 36 deliberate bugs; 35 caught (33 at first: +0x3C set before the scale, a first table entry that's
    neither 0 nor 0xFFFF added for 2). Equivalent: the turn's move-there w (the box is rebuilt with w 1 before the
    move back).
- [x] Batch AN-10 (NOT run in the game yet; the geometry's occlusion polygons; 1 function, 2.9 KB):
  src/game/nx/coll_tri.cpp, coll_static.cpp.
  - Nx::CCollObjTriData::AddOcclusionPolys 0x00414FE0 (0x0040AEB0 passes on to it): each face paired with the first
    later face sharing exactly two corners (compared as 16 bits, each of face i's corners matched with the first
    equal corner of face j) whose normal ((v1 - v0) x (v2 - v0), normalised unless zero, rounded where the x87 code
    stores) is within 0.05 on each axis; the quad - face i's corners with face j's other corner between the shared
    ones - to Nx::AddDebugShape(4, ...) (0x0048BF80's jump target: an occlusion polygon, 0x004D8D70) with the
    geometry's checksum. Each face's flags are read and tested for a sign they can't have (no effect).
  - Tests (selftests_anim9.cpp): TestAnimCollOcclusion - a 4x4 grid split into triangle pairs (diagonals, corner
    orders and some windings random; vertices lifted within, near and beyond the tolerance), stray, repeated and
    degenerate triangles, shuffled; then in order a collinear and a degenerate face sharing two corners, a tilted
    quad's triangles with face i's shared corners at 0-1, 1-2, 0-2, corner indices above 255 differing only above
    the low byte, a last adjacent pair; plain (10-byte faces) and compressed (8-byte faces), and with 0-2 faces.
    AddDebugShape stood in for (each quad's 16 floats and the id recorded): 25 quads.
  - Mutations: 24 deliberate bugs; 22 caught (15 at first: the ordered cases above and the 0.16 lift added for 7).
    Not caught: one edge's z rounded to a float (x87 detail, below the tolerance), < for <= at the tolerance.
- [x] Batch AN-11 (NOT run in the game yet; the collision intersection tests; 3 functions): src/game/nx/
  coll_tests.h / .cpp (new), mth/vector.cpp.
  - Nx::RayHitsTriangle 0x0040B420 (cdecl: Moller-Trumbore from the front only - a determinant below 0.03
    misses -, the barycentric bounds allowing the determinant times 1.00001, t in 0..1 written once it's reached),
    Nx::LineHitsSphere 0x0040B610 (the line's distance from the centre against the radius, then the nearer crossing
    against a limit), Nx::SegmentsCross2D 0x0040B750 (2D segments crossing for s and u in 0..1; s = 0 below a
    1e-11 denominator; the first sign test uses the denominator before it's rounded to a float). The arithmetic
    rounds to floats only where the x87 code stores. LineHitsSphere and SegmentsCross2D are registered through
    naked adapters that keep EDX: their callers 0x0040EC90 / 0x0040D370 (not rewritten yet) keep a value in it,
    which the originals don't touch (scan_register_assumptions still lists those 6 sites as RISK until the callers
    are rewritten - it can't see the adapters; TestAnimCollIntersectionsKeepEdx proves it).
  - Mth::CrossProduct 0x00402550 made exact: each component's products kept unrounded and the difference rounded
    once, as the original (ours rounded each product to a float - last-bit differences; its old test only checked
    within a tolerance).
  - Tests (selftests_anim10.cpp): TestAnimCollIntersections - 4000 random rays at triangles (from either side,
    near edges and corners, along the plane, too short, going away, degenerate triangles, NaN / infinite / huge /
    tiny values) plus exact cases (t = 1, u = 0, inside and beyond the 1.00001 slack, from behind and below), 4000
    lines at spheres (each called again with the limit at the crossing found) and 4000 segment pairs (parallel,
    collinear, touching, nearly parallel, through the middle; a crossing with a 1e-12 denominator): results and
    outputs bit for bit (any NaN counted as one: x87 and SSE pass different NaNs on). TestAnimCollIntersections-
    KeepEdx: both adapters called with a marker in EDX.
  - Mutations: 35 deliberate bugs; 30 caught (24 at first: the exact ray cases, the second sphere call and the tiny
    denominator added for 6). Not caught: the determinant's sum order, the bound from the unrounded determinant, u
    kept unrounded (rounding below what the comparisons see), >= for > against the bound and the unrounded
    denominator's sign (exact equality / underflow only).
- [x] Batch AN-12 (NOT run in the game yet; the point / triangle distance; 1 function, 1.4 KB of x87):
  src/game/nx/coll_tests.h / .cpp.
  - Nx::PointTriangleDistanceSquared 0x0040B860 (cdecl; 16 callers in 0x0040BDC0-0x0040FAE1): Eberly's squared
    distance with its seven regions, the nearest point's s and t written when their pointers aren't null. Returned
    as a double: the original leaves the unrounded value in ST0 and some callers compare it before storing it. One
    region keeps the original's slip - below the first edge's start (s < 0, t >= 0) e is compared with c without
    its sign flipped (a missing FCHS), so it always clamps along the second edge. Most of its tests read the flags
    so that NaN counts as "less"; the C++ follows that ("below").
  - Tests (selftests_anim10.cpp): TestAnimCollPointTriangle - 12000 points over and beside random triangles (the
    parameters spread over -1..2, lifted off the plane; corners and edges; collinear, repeated and zero triangles;
    NaN / infinite / huge / tiny values; each output pointer left null at times) and every integer point of a
    9 x 9 x 3 grid around five integer triangles (exact ties); the distance's 64 bits, s and t compared.
  - Mutations: 34 deliberate bugs; 29 caught (28 at first: a triangle where d = 0 below both edges, found by a
    brute-force search over integer cases, added for 1). Not caught: the NaN-as-less reading in two places a NaN
    can't reach (it would have made s or t NaN first), s * (1 / det) for s / det (rounding below a float), >= for >
    and < for <= in region 2 (the same results either way - checked over all small integer cases).
- [x] Batch AN-13 (NOT run in the game yet; the line tests' hit record and the movable box's line test; 2
  functions): src/game/nx/coll_line.h / .cpp (new), coll_movable.h, mth/geometry.cpp.
  - Nx::CollisionHit (our name; 0x50 bytes: normal, point, face, distance, ignore / require flags, the face's flags
    and terrain, the object and its checksum, a callback (cdecl, given the record) and its data, hit, the face's
    flag 0x40; +0x38 / +0x44 / +0x4C not known) and its copy 0x004F8160 (fieldwise, the padding left; outside
    this range - written for ReportHit, not registered).
  - Nx::ReportHit 0x004131F0 (cdecl; the slot-14 line tests' report): a face hit at a distance along the line is
    taken unless its flags include an ignored one or lack a required one - the record filled (the point
    recomputed along the line, w = 1) and the callback called; but unless it's nearer than the record's distance
    (not nearer while 0x00691910 asks for the farthest), the record is copied back as it was and false returned,
    the callback having run.
  - Nx::CCollMovBBox::CollideWithLine 0x004137B0 (slot 14: the line, its direction - unused -, the record, the
    line's box - unused): the line taken into the box's space, its box against the object's, BBox::LineIntersect,
    the point and normal turned back (RotateVector) and placed, the distance |hit - start| / |end - start|. The
    face index handed on is left unset by the original (stack garbage); 0 here - the test leaves that word out.
  - Mth::BBox::LineIntersect 0x00403550 made exact: the crossing fraction and the crossing point in double,
    rounded to floats where the original stores them (ours did them in floats - last-bit differences).
  - Tests (selftests_anim11.cpp): TestAnimCollLineReport - 3000 lines at a movable box placed and turned at random
    (through it, beside it, starting / ending in it, zero length), records with distances to beat below / at /
    above the hit, ignored / required flags, a callback (which also changes the record), nearest and farthest
    wanted, each hit tried again at its own distance; then 400 ReportHit calls on a sector object's faces (each
    face's flags and terrain). Records (all but padding), callback calls and results compared.
  - Mutations: 35 deliberate bugs; 33 caught (26 + 4 at first: the callback changing more of the record added for
    the copy's fields). Equivalent: the box pre-check (LineIntersect rejects those lines anyway) and the placed
    point's w (ReportHit recomputes the point).
- [x] Batch AN-14 (NOT run in the game yet; the triangle objects' line tests; 4 functions): src/game/nx/coll_line.cpp,
  coll_static.h, coll_movable.h / .cpp, coll_tri.h.
  - Slot 14 of the three triangle classes: Nx::CCollStatic::CollideWithLine 0x00413B50 (the faces of the line's box
    - GetFacesInBox -, each skipped when its rejection byte - the sector's buffer for 10-byte faces, the face's last
    byte for 8-byte ones - is below a threshold: the line's distance from the geometry's minimum corner times the
    geometry's +0x3C read as a float, truncated; past 255 nothing is tested; each face tried with RayHitsTriangle,
    its normal (v1 - v0) x (v2 - v0) normalised, offered to ReportHit with the corner), CCollStaticInstance::
    CollideWithLine 0x00413FF0 (the same in the instance's space, no rejection; the normal normalised inline and
    turned by the matrix), CCollMovTri::CollideWithLine 0x00414460 (in the object's space; rejection only for 8-byte
    faces; the normal's length and inverse rounded to floats, its x kept unrounded for the turn). Each rounds where
    its x87 code stores - they differ. The corner each passes on as the hit point is dead (ReportHit recomputes the
    point from the line).
  - Nx::CCollScaled::LineToLocal 0x00410A30: both ends through the inverse (Transform, w included).
  - Tests (selftests_anim12.cpp): TestAnimCollTriangleLines - a 60-face random mesh, plain (10-byte faces) and
    compressed (8-byte faces), with rejection bytes (some 255) and scale; a sector over it, an instance (scaled,
    turned, placed) and a movable object (turned, placed), re-placed every 100 lines; 1500 lines per mesh through
    and around them, half with a BSP tree (a split on x: the line's box decides the faces), the record's distance
    and flags varied, nearest / farthest, scales rejecting nothing / everything; 200 lines at exactly threshold 255
    (only the 255 faces tested); LineToLocal on random lines. 559 hits of 3000.
  - Mutations: 32 deliberate bugs; 25 caught (22 at first: the threshold-255 lines, the tree and the movable's box
    added for 3). Not caught: the corner's scale, transform and placing (dead: see above), and four roundings in
    the rejection distance (below the threshold's integer steps).
- [x] Batch AN-15 (NOT run in the game yet; the animation blender component; 47 functions, 0x00419690-0x0041A6D0
  with the hidden runs 0x00419690, 0x00419750, 0x004198D0, 0x00419AD0, 0x00419BF0, 0x00419E90, 0x0041A1A0):
  src/game/obj/anim_blender.h / .cpp (new). Started by the previous session, finished here.
  - Obj::CAnimBlenderComponent (type 0x7149B753, 0x44 bytes, vtable 0x00645D8C): Create 0x0041A5B0, Construct
    0x0041A410, DestroyBody 0x0041A4C0 (the observer list emptied twice, as the original), Destroy 0x0041A610;
    member functions registered 0x00419690 (table 0x00682350); the vtable: Update 0x0041A260 (channels after the
    first below weight 0.00001 only reset; several channels: Gfx::BlendPosesById set by set up to the highest bone
    set; observers told; the skeleton given the pose), InitFromStructure 0x00419DD0, Finalize 0x004196D0,
    PassToChannels 0x00419980, CallMemberFunction 0x00419950; the script member functions AddChannelFromScript
    0x0041A1A0, WaitOnChannel 0x00419E90, RemoveChannel 0x00419750, BlendChannel 0x004197C0, ChannelExists
    0x00419F10, PassToChannel 0x004198D0; the channel walks FindChannel 0x00419D40, AddChannel 0x00419FD0,
    NotifyObservers 0x0041A220, VisitChannels 0x004199C0, PassToEachChannel 0x004199F0, ResetChannels 0x00419A20,
    AnyChannel 0x00419C10, AllChannelsFlag29 0x00419C70, ModelReady 0x00419CA0; the first channel's calls
    0x00419730, 0x00419A90, 0x00419B50, 0x00419B90, 0x00419BB0, 0x00419BD0, 0x00419BF0, SetFlag4C 0x00419B30,
    GetValue / HasValue 0x00419AF0 / 0x00419B10 (Gfx::CBlendChannel's, by their established names), GetAnimSet
    0x00419AB0, 0x00419AD0, IsAnimComplete 0x00419CD0, 0x00419F80, 0x0041A0A0, GetTimes 0x0041A0E0; the channel
    links' destructors 0x00419D70 / 0x00419D90 / 0x0041A170 and a channel's GetTimes 0x00419CF0; LargerOf
    0x004196B0; VectorAt1C::Get 0x0041A680. The original's crashes on missing channels kept (commented).
  - Script structures through CStruct's own methods (GetChecksum / GetFloat / AddChecksum / ContainsFlag), the pose
    blend through Gfx::BlendPosesById.
  - Tests (selftests_anim13.cpp): TestAnimBlenderComponent - stand-in channels (a fake vtable), observers, script
    structures and channel methods (ours and the originals' alike); every function with and without channels, ids
    found and not, channels at the front / back, the update with no change, one and several channels (weights
    below the threshold after the first, bone sets), the observers, destroyed. Calls out, results and the arena.
  - Mutations: 60 deliberate bugs, all caught.
  - The self-test runner got a watchdog (src/hook/selftest_runner.cpp, shared): a test still running after 45 s
    is named in the log ("SELFTEST HANG in ...") and the process ends (exit 3) - an earlier anim test once hung
    thug2_check. tools/mutate.py lists those lines too.
- [x] Batch AN-16 (NOT run in the game yet; the world's line queries, the movables' list and the collision feeler;
  20 functions): src/game/nx/coll_world.h / .cpp, feeler.h / .cpp (new), gfx/debug_lines.h (new), coll_line.h /
  .cpp (two fields named), skater/debug_collision.cpp / .h.
  - Nx::RegisterMovable 0x004177B0 / UnregisterMovable 0x00417800 (the movable objects' list at 0x00691978).
  - Nx::CollideWithStatics 0x004106C0 (the hit's distance to beat 1.01 / -0.01; through the cache when it holds
    the line's box - its statics whose boxes touch the line's box grown by 0.5 - else the scenes' grids along
    the line, each object's slot 13 first, the hit's scene noted), CollideWithStaticsFarthest 0x00410930,
    CollideWithMovables 0x004118C0 (the distance only reset when the hit has nothing; the cache's movables with a
    box, or the list's without flags 2 / 4; the object's owner on the hit while it's tested; the owner of the
    last object hit returned), CollideWithMovablesFarthest 0x00411AE0.
  - Nx::CFeeler (0xB0 bytes): Construct 0x00417760 and with a line 0x00417250, SetIgnore 0x004170F0, SetStart /
    SetEnd / SetLine 0x00417190 / 0x004171B0 / 0x004171D0, GetCollision 0x004172F0 (static then movables, the
    feeler's cache or the current one 0x00691974; the object hit in the smart pointer, its id; distance, point
    and normal copied; 1 / 0 in EAX), GetCollisionAlong 0x00417710, IsHitTriangleCollision 0x00417120,
    GetTestedOwner 0x00417140, HitObjectAlive 0x00417210, GetHitObject 0x00417240, DebugDraw 0x00417150
    (compiled out: Gfx::AddDebugLine), CallbackRelay 0x00417110 (hidden code: the feeler's callback given the
    feeler).
  - CollisionHit's +0x44 is the tested movable's owner (`owner`), +0x4C the scene of the static object hit
    (`scene`).
  - debug_collision: its REPLACE_FALLBACK wrapper on 0x004172F0 is gone; GetCollision calls
    DebugCollision::NoteFeelerQuery (NOT ORIGINAL, logs only with [features] debug_collision).
  - Other code still reaches the feeler through its addresses (local wrappers in park_floor, rail_manager,
    camera_util, editor_camera, avoid, composite, level_editor): left as they are.
  - Tests (selftests_anim14.cpp): TestAnimFeelerQueries - 12 stand-in collision objects (fake vtable answering
    slots 1, 10, 13, 14 from a per-query script: touching, hitting at random distances, owners; ReportHit's
    nearer / farther rule emulated, the callback called), the scenes' grids (none, 0-3, an object in two), a
    cache holding or not holding the line's box (entries without a box), the movables registered and
    unregistered (twice-registered, the first, an unknown one, an empty list); 300 feeler queries (callback or
    not, its cache / the current one / none, ignore flags, movables, nearest / farthest, the three ways of setting
    the line, the object gone afterwards) and 100 direct queries on a record with and without a hit.
  - Mutations: 57 deliberate bugs, all caught (55 at first: unregistering the list's first node and the line's
    w added for 2).
- [x] Batch AN-17 (NOT run in the game yet; a scene's collision grid; 11 functions): src/game/nx/coll_grid.h / .cpp
  (new); coll_world.cpp and coll_cache.cpp call the grid directly; CCollObj's +0x0A renamed m_visitStamp (it's the
  grid queries' mark, not a terrain; coll_obj.h / .cpp, coll_movable.cpp, coll_static.cpp, two tests).
  - Nx::CCollGrid (our name; 0x4B30 bytes: the box, the cell width / depth, 20 x 20, the cells [x][z]) and
    Nx::CollGridCell (0x30: box, +0x24 count, +0x28 a block, +0x2C the list): CollGridCell::Construct 0x004183B0
    / Destruct 0x00418410; CCollGrid::Init 0x00418440 (the corners summed in double precision across the cells),
    Clear 0x004183D0, Fill 0x00418BD0 (two passes over the statics with faces - counts, Malloc'd lists, a
    top-down scratch block of fill counts; a float inverse cell width but a double inverse depth; indices clamped
    to 19 only), Update 0x00418F60 (cell by cell: removed / added / moved lists; gaps filled from the end while
    objects were taken out, extras waiting on a pending list - Lst nodes 0x00645D7C - one replacing each taken out
    later; ReallocateShrink / ReallocateUp else Realloc; then every list moved to a fresh Malloc block),
    ObjectsInBox 0x00418710 (box grown 0.5 on x / z; double inverse width, float inverse depth; each object once
    by the stamp 0x00693998; no limit), ObjectsAlongLine 0x004188C0 (the line made 0.5 longer each way along its
    unit direction, rounded as the x87 code does; one cell: its objects as they are; more: each once, 2047 at
    most; the count out); the pending list's node / head destructors 0x00418ED0 / 0x00418EF0 / 0x00418F30.
  - Quirk kept: in Update a removal that finds nothing in a cell still counts, and a later addition then writes
    before the cell's list (the game's lists are consistent, so it doesn't happen there).
  - Tests (selftests_anim15.cpp): TestAnimCollGrid - 40 random bounds (sizes not exact in floats) through Init;
    40 stand-in statics (a fake slot 21, flags 2 / 4, some without faces, some boxes at cell edges, some where the
    float and double inverse cell widths truncate differently - found by trying the edges' neighbouring floats)
    through Fill; 600 box and line queries (clamped, at edges +- 0.5 and nudges, shorter than a unit, zero length,
    single cell, the count wanted or not); 16 updates with consistent added / removed / moved lists (in-place
    resizing working or not); a line over 2400 objects in 7 cells (the 2047 limit); Clear; the cell and node
    pieces. A bump allocator records every allocation; the cells, lists, calls and the arena compared.
  - Mutations: 45 deliberate bugs; 43 caught (33 at first: aligned boxes and lines, more rounds and additions,
    the limit reached, distinct heaps, rounding-sensitive values added for 10). Not caught: two roundings of the
    line's unit direction in ObjectsAlongLine (float vs double inverse length, the x part rounded) - they change
    the 0.5-unit extension below a float's resolution at the line's ends.
  - Also: the self-test runner's watchdog; a flaky "vector insert iterator out-of-range" DEBUG CRT REPORT seen
    once in a full offline run (THUG2PM.exe) and not again in four more - not traced to a test.
- [x] Batch AN-18 (NOT run in the game yet; the rect queries and the rect feeler; 14 functions): src/game/nx/coll_tests.h /
  .cpp (RectHitsTriangle), coll_rect.h / .cpp (new), coll_static.h, coll_movable.h, coll_obj.h.
  - Nx::RectHitsTriangle 0x0040D370 (a parallelogram - corner and two sides, Nx::CollRect - against a triangle:
    the triangle's edges cut by the rect's plane give P Q (each on an edge kind 1-3, crossing points rounded in the
    original's two different ways); in the plane the normal is largest across, each end inside all four sides a
    point, then each rect side crossing P Q (kind 0); true only with two points - one may be written and false
    returned, as in the original).
  - Nx::RectHit (0x60: two ends - point, the triangle edge, on-edge flag -, normal, checksum, flag 0x40) and its
    copies 0x00417AD0 / 0x00417B60; Nx::RectHits (0xC08: 32 hits, count, ignore / require flags).
  - CCollStatic::CollideWithRect 0x00410C90 (slot 15) and CCollStaticInstance::CollideWithRect 0x004128B0 (in the
    instance's space, its own box; points scaled and placed, normals turned back), CCollScaled::RectToLocal
    0x00410AA0, the in-place vector turn 0x00411840; CollideRectWithStatics 0x0040FEE0 (cache or the scenes' grids
    along the rect's diagonal; the grid's ObjectsInBox called and its answer dropped, as the original).
  - Nx::CRectFeeler (0x1844): Construct 0x004182D0, Query 0x00417840 (its cache or 0x0069198C), JoinHits 0x00417C00
    (duplicates dropped, pieces lying along each other joined end to end), DebugDraw 0x00417860 (compiled out).
  - Small ones next to them: CCollObj::SetChecksum 0x0040FEC0, CCollStatic::TakeBuffer 0x00410BC0.
  - Tests (selftests_anim16.cpp): TestAnimCollRectTriangle - 20000 random rect / triangle pairs (axis planes,
    degenerate rects, corners on / near the plane, NaN) and 6000 exact ones (a unit rect in z = 0 with corners at
    0, +-1e-5, ...; rects whose normal has two equal largest parts); TestAnimCollRectQueries - a 60-face mesh plain
    and compressed (with and without a tree), sector and instance (scaled, turned, placed), 1600 rects with flags
    and a nearly full / full list; rects into the instance's space; the world query through a cache and through
    stand-in grids; the rect feeler built, queried and 400 crafted hit lists joined; the copies and small pieces.
  - Mutations: 80 deliberate bugs; 72 caught (66 at first: exact plane distances, normal ties, a tree, the
    rejection scale added for 6). Not caught: rounding changes below a float's resolution in RectHitsTriangle
    (the order of a plane distance's sum, Q's side values as double or float, the second side direction test as
    float or double - rounding can't change its sign), the w part of a scaled point (TransformAsPos ignores it),
    of a kind-1 edge (raw vertices have w 0) and of a hit's direction (never read).
- [x] Batch AN-19 (NOT run in the game yet; the sphere queries and the sphere feeler; 9 functions): src/game/nx/coll_sphere.h /
  .cpp (new), coll_static.h.
  - Nx::SegmentCrossesSphere 0x0040E280 (0-2 crossings along a segment), SweptSphereHitsEdge 0x0040E560 (a moving
    sphere against a segment - touching already, else the capsule round it against the centre's path; the edge's
    nearest point then; the edge's own move is (0, 0, 0, 1) in practice: the point's w counts the time),
    SweptSphereHitsCorner 0x0040E940 (a corner and the edge from / to it the path heads for; a corner inside the
    sphere sets *t 0 and misses), SweptSphereHitsTriangle 0x0040EC90 (the face's unit normal out; a sphere apart
    from the plane classified by its centre against the three edges - the face if the move's capsule holds its
    nearest point, an edge, a corner; one within reach by where its front meets the plane - the face up to the
    limit, an edge, a corner's line test), SphereHitsTriangle 0x0040F9E0 (standing, from the front only).
  - Nx::SphereHit (0x2C) / SphereHits (0x5A4: 32 hits, the move's direction and sweep, count, flags, mode);
    CCollStatic::CollideWithSphere 0x004110B0 (slot 16 of sectors and instances alike: an instance's faces taken
    unplaced, as the original); CollideSphereWithStatics 0x00410240 (the sphere's box and the moved sphere's; cache
    or grids along the grown box's diagonal); Nx::CSphereFeeler (0xB60): Construct 0x00418330, Query 0x00418300 (mode
    1, its cache or 0x00691990).
  - The original's Line::DistanceSqrToLine / DistanceSqrToPoint (0x00402640 / 0x00402D60, an earlier batch's
    rewrites, through Capsule::IntersectsSegment / ContainsPoint) keep doubles where ours round to floats - the
    sphere tests let both runs use the original's there. Worth making exact some time (outside this range).
  - Tests (selftests_anim17.cpp): TestAnimCollSphereTests - 6000 random cases (segments through / grazing / missing
    spheres, moving spheres against still and moving edges, corners with their edges, triangles from both sides,
    touching or not, moving or not, limits; standing spheres; NaN) and exact ones (ends and grazes on the surface,
    a corner on the surface, a zero divisor deciding the edge, a flat triangle touched exactly / reached at the
    limit, no area); TestAnimCollSphereQueries - a mesh's sector (plain / compressed; flags; full lists; modes; sweeps
    0, positive, negative), the world query through a cache and stand-in grids, the feeler.
  - Mutations: 74 deliberate bugs; 67 caught (57 at first: the exact cases, moving edges, negative sweeps added for
    10). Not caught: equivalent ones (the capsule test always gives the edge parameter within 0..1; a triangle with
    no area misses either way), NaN-only differences (2), and roundings below a float's resolution (3).
- [ ] Next: AN-15 - slots 15-17 of the sector / instance: 0x00410C90 / 0x004128B0, 0x004110B0, 0x004114E0; then
  0x0040BDC0, 0x0040D370, 0x0040E280, 0x0040E560, 0x0040E940, 0x0040EC90, 0x0040F9E0, 0x0040FC00, 0x0040FEC0,
  0x0040FEE0, 0x00410240, 0x004106C0, 0x00410930, 0x00410AA0, 0x00410BC0, the 0x00417xxx feelers / registration
  (0x004177B0, 0x00417800...). Hidden code to give source to later (found by scratchpad hidden.py - runs between
  Ghidra's functions that aren't padding): 0x00412730, 0x00417110, 0x00419690, 0x00419750, 0x004198D0,
  0x00419AD0, 0x00419BF0, 0x00419E90, 0x0041A1A0-0x0041A21C.
