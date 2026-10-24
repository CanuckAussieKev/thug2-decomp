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
    named 0xEB17151B, its object flag 2), 0x005A5610 AddSkaterEarly (no skater: profile 0's skater added, local),
    0x005A5680 WeatherUpdateGrid (weather vtable +4 through 0x004A1090, true), 0x005A5690-0x005A58D0 the 19 weather
    setters and 0x005A58F0 / 0x005A5910 / 0x005A5930 the screen effects (each the object's own method on (params,
    script), its result returned), 0x005A5950 IsPal (0x007CEFA0 != 2).
  - Test TestWorldCFuncs: original vs ours with every call stood in for (node dispatcher, GetSkater, the skaters'
    vtables, Hide, IsObserving, the profile / AddSkater, the viewer's allocation, constructor and module calls, the
    struct pool / heap, AddChecksum, GetArray, CreateCompositeObjectFromNode, Clear, DeleteStruct, every weather /
    screen-effects method): 0-3 skaters, missing ones, +0xBC clear, ready / observing combinations, LaunchViewer
    from the pool, the heap and without viewer memory. 23 of 23 deliberate bugs caught (1 after adding a one-skater
    case for SkatersAreReady's `> 0`).
