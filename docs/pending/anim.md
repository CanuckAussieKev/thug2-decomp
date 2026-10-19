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
- [ ] Next: AN-9 - 0x004164F0 / 0x00416730 (the geometry turned / scaled), 0x00414990, 0x00414BE0, 0x00414D30,
  0x00414FE0, the forwarders 0x0040AE80-0x0040AEC0, then the face / line tests 0x0040B420 onwards, 0x00410C90,
  0x004110B0, 0x004114E0, 0x004128B0, 0x004131F0, 0x004137B0, 0x00413B50, 0x00413FF0, 0x00414460. Hidden code
  to give source to later (found by scratchpad hidden.py - runs between Ghidra's functions that aren't padding):
  0x00412730, 0x00417110, 0x00419690, 0x00419750, 0x004198D0, 0x00419AD0, 0x00419BF0, 0x00419E90,
  0x0041A1A0-0x0041A21C.
