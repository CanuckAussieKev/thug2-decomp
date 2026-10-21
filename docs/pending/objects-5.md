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
- [x] Batch OC5-3 (NOT run in the game yet; the sun and its lens flare, 14 functions): src/game/gfx/lens_flare.cpp +
  .h (new, namespace Sun); Obj::CSmtPtr::IsNull added to smtptr.h (additive). The original's file was compiled without
  optimisation (frame pointers, every small helper called) - the rewrite calls our math directly.
  - 0x00442670 CSmtPtr::IsNull, 0x00442680 ClampToRange (min checked first, NaN passes - unlike Mth::Clamp),
    0x004426B0 the camera's +0xB4 factor (REPLACE of a thiscall float getter), 0x004426C0 QueryRequest::Init.
  - 0x004426D0 SetSun (the parts by mask; a new texture deletes the old sun texture through its vtable - for a new
    corona too: the original deletes the SUN texture when there's a corona, kept), 0x004429D0 Init (InitSun's work:
    the lens_flare_data array's elements - pos_scale, scale, rgba packed r-low, id - each with creation and morph
    params; LensFlare_Morph_In / Out and SunBurst_Out; colour 255 255 255 128).
  - The flare: 0x004428F0 PlaceFlareElements (each element morphed to centre + pos_scale * (sun - centre)),
    0x00442D70 UpdateFlare, 0x00442DF0 HideFlare (Lens_Flare_Container's alpha to 0 over morph_time; with params,
    the elements sent to sun_pos), 0x00443030 UnhideFlare (alpha 1 over 0.2), 0x00443780 ShowFlare (CreateLensFlare
    run when there's no container; nothing unless DoUpdateLensFlare).
  - Drawing: 0x00443180 BuildSunQuad (a quad facing the camera, size camera+0xB4 * size, its corners through
    0x00434200 then moved to the sun; the primitive drawer's blend modes), 0x00442810 QuerySunVisibility (the
    occlusion query callback: a 10 x 10 query round the sun's screen place), 0x00443870 Render (each frame: the sun
    theta / phi / radius from the camera; hidden behind the camera or off the projection; the sun drawn; a query;
    not seen: the corona fading over SunBurst_Out, MaybeHideLensFlare (with morph_time unless it's been hidden past
    LensFlare_Morph_Out); seen: the corona at half colour, ShowFlare (morphed in unless hidden past Morph_In). The
    visible pixel count's brightness is worked out and never used - kept as a comment).
  - Test: TestSunAndLensFlare (src/hook/selftests_obj_lens_flare.cpp): a stand-in camera, textures (with a recording
    vtable), screen element manager and container (real smart pointers), script structures; the viewport camera and
    the primitive drawer's virtual calls by naked dispatchers (the shared vtable-call helpers 0x004A1090 / 0x004A10A0
    pick by ECX), Script::GetFloat by a float-returning stand-in. Every function and case above, 12 sun-drawing cases.
    The original's Matrix::Rotate is pointed at ours for the test (its x87 sine / cosine differ from ours by an ulp -
    in the game the hook does the same). 48 deliberate bugs: 47 caught (2 after strengthening - a sun just behind the
    camera, a fade of exactly 1), 1 equivalent (the quad corners' w: the facing matrix's last row has no xyz).
  - Found on the way: the primitive drawer the original keeps on its stack is 0x150-0x15C bytes (its frame's gaps).
- [x] Batch OC5-4 (NOT run in the game yet; the walk component's hang helpers, part 1, 14 functions):
  src/game/obj/walk_hang.cpp + .h (new: Walk::Component's member functions - the walk component itself, 0x00524xxx-
  0x0052Axxx, isn't rewritten: its fields reached by offset, named in walk_hang.cpp).
  - 0x0043D930 CreateWaitAndDieComponent (component type 0x03AC8065, vtable 0x00646898; its methods 0x0043D850-
    0x0043D8E0 belong to the neighbouring range), 0x0043D990 Lerp (a + (b - a) * t, each component rounded as the
    x87 code did - y kept unrounded, z and w rounded early), 0x0043DA20 SkaterTurn::TurnedRound (the skater core's
    +0x228 within 45 degrees of a half turn), 0x0043DA70 Param (walk_parameters' floats), 0x0043DAA0 RunSpeed,
    0x0043DC00 HangHeightOffset, 0x0043DC60 CriticalPointUp (all three return the x87 value unrounded, as double).
  - Animation-end callbacks (stored at +0x1948 as the game's addresses - in the game, jumps to ours): 0x0043DB00
    EndPullUp (walking; the pull-up feeler's hit to the triggers and the movable contact), 0x0043DB90
    EndDropFromHang (in the air), 0x0043DCF0 EndDropToHang (hanging: the hang offset, the skater core's rail node /
    manager, the rail script, tagging allowed again).
  - 0x0043DDD0 SideClear (the hang's side obstruction feelers, out and back), 0x0043DFC0 FaceHangRail (the place
    along the rail, the facing across it - to the side the direction says; past 60 degrees +0x1760 is told
    (0x005250A0, x87 FCOS of the double at 0x006468F8 kept through inline asm)), 0x0043E230 NextHangRail (the segment
    before / after, else a joining rail; "BAD ANGLE ON COINCIDENT RAIL" / "FOUND COINCIDENT RAIL" debug output kept),
    0x0043EB80 MovableContact::StartRiding (a ride tracker for the object; the old one freed, its object at +0x6C).
  - Found by the test: going forward, NextHangRail checks the segment's flags on `end` (the segment end -> next's
    first node), not on the next node.
  - Test: TestWalkHang (src/hook/selftests_obj_walk_hang.cpp): the skater core kit (skater_core_testkit.h) over a
    walk arena - the component, 6 rail nodes in a chain, the rail manager, the skater core with its flags, the
    contact and a ride tracker; every callee recorded (the walk component's own code, rail manager, feelers, events,
    scripts, memory); walk_parameters a hand-built structure; Normalize pointed at ours. Random arenas per variant
    (12-240). 39 deliberate bugs: 38 caught (4 after strengthening: short rails, many more FaceHangRail variants, t
    of 0 / 0.25, the pull-up's offset recorded before the drop overwrites it), 1 equivalent (StartRiding's null
    store before the new tracker).
