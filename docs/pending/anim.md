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
- [ ] Next: AN-4 - code Ghidra never made functions of: 0x00406560 (called from the skater code, 0x00514320 ...;
  an angle-limit test of two vectors), then 0x0040A910-0x0040AB00 (an 8-slot registry at 0x006918EC) and
  0x0040AB40 onwards. Hidden code to give source to later (found by scratchpad hidden.py - runs between Ghidra's
  functions that aren't padding): 0x00412730, 0x00412F90, 0x004139A0, 0x00413F30, 0x00414D70, 0x00417110,
  0x00419690, 0x00419750, 0x004198D0, 0x00419AD0, 0x00419BF0, 0x00419E90, 0x0041A1A0-0x0041A21C.
