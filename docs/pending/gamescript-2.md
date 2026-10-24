# Game script commands 0x005A5360-0x005AAEF0 (weather, script start-up, memory card / save, textures, profiles) - gamescript-2 agent

Branch worktree-agent-ad2bfb852cf16e83b. Ghidra snapshot copy C:\Games\THUG2-ghidra-snapshot-gs2. Tests in
src/hook/selftests_gs2_*.cpp. Shared files touched: CMakeLists.txt (new files listed).

- [x] Batch GS2-1 (NOT run in the game yet; light groups, the skaters' pause, the viewer, the weather, the screen
  effects - 31 CFunctions): src/game/script/world_cfuncs.h / .cpp (new).
  - 0x005A5360 SetLightGroup / 0x005A5380 IsAlive (the node command dispatcher 0x005A4EF0 - the neighbouring range,
    via GameFunction - with command 7 / 5), 0x005A53A0 PauseSkaters (hide flag; each skater with +0xBC set: vtable
    +0x2C, then Hide(hide); the count read once, unsigned loop), 0x005A5420 UnPauseSkaters (vtable +0x30, shown),
    0x005A5480 SkatersAreReady (GameNet m_readyToPlay; a skater, else IsObserving 0x005A0D90), 0x005A54E0
    LaunchViewer (once: Mdl::Viewer made, registered, started; viewercam_composite_structure as a composite object
    named viewer_cam, its object flag 2), 0x005A5610 AddSkaterEarly (no skater: profile 0's skater added, local),
    0x005A5680 WeatherUpdateGrid (weather vtable +4 through 0x004A1090, true), 0x005A5690-0x005A58D0 the 19 weather
    setters and 0x005A58F0 / 0x005A5910 / 0x005A5930 the screen effects (each the object's own method on (params,
    script), its result returned), 0x005A5950 IsPal (0x007CEFA0 != 2).
  - Test TestWorldCFuncs: original vs ours with every call stood in for (node dispatcher, GetSkater, the skaters'
    vtables, Hide, IsObserving, the profile / AddSkater, the viewer's allocation, constructor and module calls, the
    struct pool / heap, AddChecksum, GetArray, CreateCompositeObjectFromNode, Clear, DeleteStruct, every weather /
    screen-effects method): 0-3 skaters, missing ones, +0xBC clear, ready / observing combinations, LaunchViewer
    from the pool, the heap and without viewer memory. 23 of 23 deliberate bugs caught (1 after adding a one-skater
    case for SkatersAreReady's `> 0`).
- [x] Batch GS2-2 (NOT run in the game yet; the script system's start-up - 14 functions): src/game/script/
  script_startup.h / .cpp (new).
  - 0x005A5660 GetNumCFunctions (1254) / 0x005A5670 GetNumMemberFunctions (250), 0x005A5960 LoadQBFile (LoadQB, not
    temporary), 0x005A5990 LoadQBFileData, 0x005A59B0 UnloadQBFile (UnloadQB, then nodearray looked up - the result
    unused), 0x005A59D0 LoadStartupQBFiles (qb_scripts.pre loaded on the top-down heap; each line of
    scripts\qdir.txt - CR / LF ends, empty lines skipped, the path from "data\" on, no length check on the 200-byte
    line - unloaded and loaded with the permanent string cache on; then debugger_names removed), 0x005A5B20 (the same
    in a memory profile), 0x005A5B40-0x005A5DF0 the pool creators of CAnimCachePredictor (0x1E4 bytes),
    CBlendChannel (0x74), CBonedAnimController (0x28), CAnimLookup (0x214), sSpawnedPolyDetails (0xB8),
    Obj::CEventReceiverList (0x14) - Script::CreatePool's code, 0x005A5E70 AllocatePools (the script pools, those six
    with 200/150/150/175/75/175 items, the CFunctions and member functions registered).
  - Found: a stock THUG2.exe names "qb_scripts.prx" at 0x00657698 where THUG2PM.exe has ".pre" - ours keeps .pre (the
    file layer opens a .pre as .prx anyway); the test writes THUG2PM.exe's byte there while it runs.
  - Test TestScriptStartup: original vs ours with LoadQB / LoadQBData / UnloadQB / GetArray / the heap stack / Pip /
    the permanent strings / FindSymbol / CleanUpAndRemoveSymbol / CreateScriptPools / the registrations stood in for,
    allocations from one arena per run (the pools' blocks and contents compared): a qdir.txt with CR LF, LF, empty
    lines, "data\" in and out of the path, a last line without its end, a size ending mid-line, none; pool pairs
    with current pool 0 and 1. 23 of 23 deliberate bugs caught (one by hanging, as the original would on that bug).
- [x] Batch GS2-3 (NOT run in the game yet; the save types, the saves' summaries and the card state commands - 33
  functions): src/game/mc/save_types.h / .cpp (new), src/game/mc/memcard_cfuncs.h / .cpp (extended).
  - Save types by checksum (OptionsAndPros .SKA a, Cas b, Park .PRK c, NetworkSettings .NWS e, Cat .CAT f,
    CreatedGoals .GOA g, CreatedGraphic .GRF h, Replay .REP): 0x005A5F70 MakeFileName (EAX/ESI/EDI: 8 base-26
    letters of the name's CRC, the last the type's letter), 0x005A60C0 TypeFromLetter (AL; keeps ECX/EDX -
    DeleteMemCardFile relies on EDX, found by scan_register_assumptions), 0x005A6180 IconSpaceRequired (EAX),
    0x005A6210 TypeVersion (EAX: VERSION_*, -1), 0x005A62A0 TypeFileName (EAX/ECX: "Save/%s.EXT" in 0x007CECE8),
    0x005A6390 MakeFolderFileName (EDI + stack), 0x005A63E0 IsLater (EDI/ESI: year..seconds), 0x005A6CD0
    RoundUpToCardBlock (ESI: platform 1-3/7 1 KB, 4 16 KB, 5 8 KB), 0x005A7340 TypeFileSize (ECX: SaveSize_* +
    0x14 rounded), 0x005A8470 SaveBlocks (cdecl: + icon space + 4 on platforms 1-3, + 3 on the PC), 0x005A6120
    ApplyGoalManagerParams (fastcall), 0x005A6C80 ApplyCatAndCustomSkater (EDI + stack), 0x005A8EB0
    KeepLoadedContents, 0x005A6610 WriteSummary (EAX/ESI + stack: per type from a save or the live state - the
    park map info, the goal editor's count, CAT name, network_id, career totals / is_male / name), 0x005A7520 /
    0x005A8E80 two list types' scalar deleting destructors (vtables 0x0065797C / 0x00658A60).
  - CFunctions: 0x005A6550 SaveFailedDueToInsufficientSpace, 0x005A6560 SetSectionsToApplyWhenLoading
    (ApplyCustomSkater 8, ApplyStorySkater 4, ApplyStory 2, 0xB39D7CC4 1, all), 0x005A6600 LoadedCustomSkater,
    0x005A6D30 GetMostRecentSave (no type asked = any type; BadVersion / Corrupt ones count only on platforms 4 and
    5 - our first version had the type test the wrong way round, caught by the test), 0x005A7210 FormatCard (the
    card's always-false stub), 0x005A7260 CardIsInSlot, 0x005A7280 CardIsFormatted, 0x005A72A0 GetSummaryInfo
    (VaultData: the kept contents with their own type), 0x005A7430 GetMemCardDataForUpload, 0x005A7450
    GetMaxTHPS4FilesAllowed (75), 0x005A7470 SwitchToTempPoolsIfTheyExist, 0x005A74F0 SwitchToRegularPools.
  - LoadFromMemoryCard's inline-assembly calls to these helpers renamed Game* (still through the game addresses,
    which the existing LoadFromMemoryCard self-test stands in for).
  - Tests TestSaveTypes / TestSaveSummaries: original vs ours (register entry points through inline assembly) for
    every name / type / letter (all 256 bytes, ECX/EDX checked) / platform -1..8 / size edge, the dates field by
    field, the summaries from hand-built saves (each OptionsAndPros career case, missing GlobalInfo, parks with
    and without goals) and the live state (stand-in skater, career, park editor, goal editor, preferences), the
    most recent save over 7 entries x 3 platforms x 4 types, the card commands with and without a card, the
    temporary pools (each missing, back from each). 48 deliberate bugs: 47 caught (3 after strengthening: a career
    without the first field, only one pool pair not on its first pool, ECX/EDX in TypeFromLetter), 1 equivalent
    (IsLater's > as >= at a differing field).
- [x] Batch GS2-4 (NOT run in the game yet; the save file commands - 6 CFunctions): src/game/mc/save_files_cfuncs.h /
  .cpp (new).
  - 0x005A6EA0 DeleteMemCardFile (PC / platform 7: RemoveFile(CardFileName), none = false; consoles: the file, its
    icon.sys and the type's icon - from the last letter of the name - deleted, then the folder; platform 5 the
    "%s%s%s%s" name; others false; music and streams paused except on the PC), 0x005A8500 GetMemCardSpaceRequired,
    0x005A8550 MemCardFileExists (the name made per platform; total_file_size in blocks; on the PC a file that
    won't open is removed by its type name), 0x005A8BB0 ClearMemCardDataForUpload, 0x005A8DA0
    CreateTemporaryMemCardPools (only the component reserve checked; 50000 / 50000 / 10000 on the top-down heap),
    0x005A9700 RemoveTemporaryMemCardPools (each reserve pool: its block and itself freed if the pair owns it).
  - Test TestSaveFileCFuncs: original vs ours with the card (GetCard, delete, RemoveFile, ConvertFilename, Open,
    Seek, Flush, Close, the file's destructor), Pip / music / sprintf / pool creation and freeing stood in for - no
    file touched: every platform 0-8 x card / no card / failing deletes (the file, icon.sys) / failing removal, every
    type's icon, names with and without a folder, MemCardFileExists' missing name / type / conversion / file, the
    reserve pools present or not, owned or not. 29 deliberate bugs: 28 caught (1 after adding a failing first
    delete), 1 equivalent (the PC path without a card file name setting the result before the removal check that
    clears it anyway).
- [x] Batch GS2-5 (NOT run in the game yet; what a save holds and how it is put back - 5 functions):
  src/game/mc/save_contents.h / .cpp (new), GetSaveInfo in save_files_cfuncs.cpp.
  - 0x005A7560 WriteGlobalInfo (Options: SplitScreenPreferences copy, SoundOptions - MainVolume, MusicVolume from
    MusicVolOutsideBox, the two 64-bit forbidden-track sets as four integers high/low, current_soundtrack on the PC,
    RandomMode -, ControllerPreferences - 8 structures of AutokickOn / SpinTapsOn / VibrationOn from the skate
    module's +0x394 bytes -, Taunts; the career, the records, Pros), 0x005A7CF0 ReadGlobalInfo (the reverse: the
    Change script with MusicVolOutsideBox, set_loaded_soundtrack with trackchecksum, the soundtrack symbol's value,
    the 8 controllers' setters 0x00567C30 / 0x00567C00 / 0x00567BD0, LastLevelLoadScript / LastGameMode into the
    script's parameters, CASFileName for a non-pro first profile), 0x005A79A0 WriteSaveContents (EBX + stack; quirk
    kept: OptionsAndPros runs on into CreatedGraphic's layer_infos), 0x005A81B0 ApplyLoadedContents (EAX / ESI +
    stack: per type; OptionsAndPros by SetSectionsToApplyWhenLoading's bits with the APPLY_* messages, layer_infos
    into the caller's params, the sections cleared), 0x005A8BF0 GetSaveInfo.
  - Test TestSaveContents: original vs ours with every call out stood in for (floats returned through `fld`
    stand-ins), new structures and arrays from one arena per run: all types on the PC and a console, RandomMode on /
    off, full / partial / career-less global info on two platforms, pro or not, the soundtrack symbol there or not,
    every type applied, OptionsAndPros with each section and both profile indexes, GetSaveInfo for the types whose
    summary of empty contents is safe. 34 of 34 deliberate bugs caught.
