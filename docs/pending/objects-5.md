# Object components & level / scene loading, fifth part (0x43D929-0x445B50) - objects-5 agent (OC5)

Worktree branch worktree-agent-aa48f6d0092e5bd2c. The range turned out to hold: the walk component's hang / ladder /
climbing helpers (WalkHangUtil.cpp, 0x43DA20-0x4425B0), the sun and its lens flare (0x4426D0-0x443870), the terrain
manager (terrain types and their sounds, 0x4442C0-0x444EB0) and the engine's scene / asset / configuration script
commands (0x445000-0x445B30). Shared files touched: CMakeLists.txt (new files listed, additive).

- [x] Batch OC5-1 (NOT run in the game yet; the engine's scene, asset and configuration commands, 19 functions):
  src/game/script/cfuncs_scene.cpp + .h (new). LoadScene (0x00445130) was done already (gfx/scene.cpp).
  - 0x00445000 LoadTerrainSounds (Terrain=<checksum> -> 0x00444EB0; false without), 0x00445030 PreloadModel
    (name -> <name>.mdl through 0x004879D0; a model made (0x0048BFA0), its file loaded (0x004935E0 with
    AllowReplaceTex as its 5th argument), destroyed (0x0048C3C0)), 0x00445120 UnloadAllLevelGeometry (0x0048C810),
    0x00445320 LoadCollision (scene= [is_net]: the scene by name (0x0048C750) - not checked for null, as the
    original - 0x0049DF70), 0x00445380 AddCollision (scene= Add= -> 0x0049DFD0), 0x004453F0 QuickReload
    (0x0048CEF0), 0x00445720 AddScene (0x0048CDD0 with the memory manager's debug heap pushed), 0x004457B0
    UnloadScene (found by name: true; a name not loaded unloads the FIRST scene of the engine's table (0x0048C770)
    and returns false - the original's; the texture dictionary at +0x88 unloaded after the scene).
  - Assets: 0x00445430 UnloadAnim (descChecksum + refChecksum, or + the asset manager's +0x18 - which is the
    reference checksum SetReferenceChecksum sets (CAssMan::m_unknown18) - or the name's CRC), 0x004454E0 LoadAsset
    (name or an unnamed string, parent, async, desc; LoadOrGetAsset gets the script's params pointer as its `extra`;
    on failure the AssertOnMissingAssets integer is read for a compiled-out assert), 0x004455A0 LoadAnim (returns
    CAssMan::LoadAnim's result - no `mov al,1`), 0x00445640 LoadSkeleton, 0x004456A0 SetReferenceChecksum
    (checksum or integer), 0x004456F0 SetDefaultPermanent.
  - Configuration: 0x00445820 SetConfig (language: English 2, French and Spanish 3, German and Italian 5; CD (first
    time: File::AsyncInit, passed "DATA") / NotCD (0x00684FA8), GotExtraMemory / NoExtraMemory (0x007CEFAC), Pal /
    NTSC (0x00684FB0), framerate (0x00684FB4)), 0x00445980 PrintConfig (the original passes no number for "%s, %d
    fps": the %d reads the frame rate still on the stack from the name function's call - passed explicitly here),
    0x00445A30 Bootstrap (0x007CEFAD).
  - The sun: 0x00445A40 InitSun (theta phi radius size texture corona_texture lens_flare_data -> 0x004429D0),
    0x00445B30 UpdateLensFlare (0x00442D70).
  - Checksum names found from the game's .qb name tables (AllowReplaceTex, refChecksum, language, NoExtraMemory, NTSC,
    framerate, lens_flare_data, walk_parameters...).
  - Test: TestSceneCommands (src/hook/selftests_obj_scene_cmds.cpp): every command, original vs ours, on hand-built
    params (cfunc_testkit.h), every game function called stood in for by recording code (the asset manager's and the
    memory manager's methods at both the game's and our addresses, with stand-in manager objects; Dbg::Printf's
    strings compared by content); the configuration globals recorded after each call. 40 deliberate bugs caught (1
    after strengthening: Italian's case needed a language order that changes the setting).
- [x] Batch OC5-2 (NOT run in the game yet; the terrain manager, 17 functions): src/game/sfx/terrain.cpp + .h (new,
  namespace Terrain).
  - Two Lst::HashTables filled by 0x004444A0 LoadTerrainTypes from terrain.qb: Terrain_Types (0x006968D0: a terrain's
    checksum -> its index byte, 8-byte items) and Terrain_Actions (0x006968E8: a sound table's index -> TABLE_* type
    and TA_* action, 12-byte items); 0x00444460 empties both (items not freed).
  - Look-ups: 0x004442C0 GetTerrainIndex (an unknown terrain reads its index through null - a crash, as the original;
    the level editor already guards against it), 0x00444310 GetTableAction (likewise through null + 8), 0x00444360
    GetTerrainChecksum / 0x004443B0 GetTableIndex (table walks that stop at an empty item), 0x00444400 / 0x00444480
    GetTerrainStructure(Into) (the terrain's global structure, else STANDARD_TERRAIN_DEFAULT's).
  - Sounds: 0x004445B0 GetSoundInfo (a 0x84-byte SoundInfo: name, the checksum of the name without folders, pitch /
    volume ranges, load pitch / volume; defaults 100 except minVol 0), 0x00444710 GetScaledVolume (returns the
    unrounded x87 value as double; maxVol compared with 100 by its bits), 0x00444760 PickSoundAction (a soundAction
    structure, or a weighted random pick by chance, walking the array from its end - Mth::Rnd), 0x00444830
    FindSoundAction (the action's structure, or the first array entry whose useUpTo isn't below the value; a NaN
    value fits only entries without useUpTo), 0x004448E0 LoadSoundAction / 0x00444950 LoadTableSounds (load type 1,
    the load pitch and volume; TERRAIN_DEFAULT's structure for an action counts only if it names the action again
    inside - otherwise its array is read, and with no array the original reads an uninitialised pointer),
    0x00444B10 GetTerrainSound, 0x00444BB0 PlayTerrainSound (volume / pitch scaled into the sound's ranges;
    propagate: message 0x35 of 12 bytes - the position truncated to int16s, table, terrain, the three percentages
    truncated to bytes - to the server through client 0), 0x00444EB0 LoadTerrainSounds (TERRAIN_DEFAULT's
    SoundActions with the terrain's appended over them, every table loaded).
  - Test: TestTerrainManager (src/hook/selftests_obj_terrain.cpp): stand-in tables (16 buckets), hand-built script
    data in terrain.qb's shapes, an arena behind the memory manager's Allocate; the sound manager (the dropoff
    distance by a float-returning thiscall stand-in), the network client, Mth::Rnd and the global symbol look-ups
    recorded. Every function, every branch above; arena, tables and trace compared. 42 deliberate bugs: 41 caught (2
    after strengthening - a sound with minVol 0 but maxVol not 100, a missing STANDARD_TERRAIN_DEFAULT), 1
    equivalent (maxVol's bit compare vs a float compare with 100).
  - Register scan: 0x004442C0 / 0x004443B0 callers push the whole EAX into byte parameters (the original's upper
    bytes were a pointer's) - only the byte is read.
