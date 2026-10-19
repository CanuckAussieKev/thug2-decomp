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
- [ ] Next: AN-3 - the asset types' methods (Animation 0x004071C0-0x004074F0, Cutscene 0x00408A40-0x00408AD0,
  Node Array 0x00408BD0-0x00408F00, 0x004090D0, Scene 0x00409240-0x00409520, SceneColl 0x00409610-0x00409890,
  Skeleton 0x00409990-0x00409B80, skin 0x00409C70-0x0040A140, SkinColl / TexDict up to 0x0040AB00).
