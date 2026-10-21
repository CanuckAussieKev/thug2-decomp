# Engine script commands, first third (0x445B50-0x4489B0) - engine-cmds-1 agent

Branch worktree-agent-a63dbf50be9690ef3, workspace C:\Games\THUG2-1. Source in src/game/script/cfuncs_*.cpp (one
header, cfuncs_engine.h); tests in src/hook/selftests_engcmd*.cpp (kit: src/hook/cfunc_testkit.h - hand-built params,
the stand-in calling script). Shared files touched: CMakeLists.txt (new files listed, additive).
Switch off: `[replacements] disable=cfuncs_lighting.cpp` (and cfuncs_render.cpp, cfuncs_polys.cpp, cfuncs_memory.cpp).

- [x] Batch EC1-1 (NOT run in the game yet; lens flare, terrain, sun, gamma, fog, dynamic lighting, UV wibble and
  texture animation commands, 21 functions): src/game/script/cfuncs_lighting.cpp (new).
  - 0x00445B50 HideLensFlare / 0x00445B70 UnhideLensFlare (0x00442DF0 / 0x00443030 with the same arguments),
    0x00445B90 GetTerrainTypeParam (param=<name>, terrain=<checksum> -> 0x004442C0, or a number (name unknown,
    0x75D274BE) taken as a byte; 0x00444480 fills a structure added to the script's params by AddStructure; false
    if there's none), 0x00445C50 SetSunProps (theta / phi / radius / size floats, texture / corona_texture
    checksums, enable / disable: a 0x44-byte block and the mask of those given to 0x004426D0 - mask 0x10, a colour
    at +0x18, is never set by the command), 0x00445D50 SetProgressive (on > 0 -> 0x005B6090), 0x00445D90
    SetColorBufferClear (0x0048C1B0 is a jump to the empty 0x005480A0: an empty inline SetColorBufferClear(bool)).
  - Gamma: 0x00445DD0 WriteGamma (RegCreateKeyExA Settings\Gamma with KEY_READ|KEY_WRITE, R / G / B written as
    4-byte REG_DWORD values holding the floats, nothing when the key can't be created) and 0x00445E60
    ApplyChangeGamma (color=red|green|blue change=<f>: the channel changes only upwards below 1 / downwards above 0;
    SetGamma 0x0048C130, then WriteGamma TWICE as the original does).
  - 0x00446000 SetDynamicLightModulationFactor (nothing without value; ambient -> 0x0048E970(value, value2);
    directional=<i> -> 0x0048E990(i, value, value2)), 0x004460B0 EnableFog / 0x004460D0 DisableFog (0x00490DE0;
    nothing while distance clipping 0x007D643A is on), 0x004460F0 SetFogExponent (false with distance clipping).
  - Sectors by name (0x0048C510): 0x00446130 VCWibblePlayFromKeyframe (false without sector=; true if the scene has
    none; time in ms), 0x004461D0 SetUVWibbleParams (eight floats, u_vel..v_phase), 0x004462D0 / 0x00446300
    Enable / DisableExplicitUVWibble, 0x00446330 SetUVWibbleOffsets, 0x004463A0 ResetTextureAnim, 0x004463D0
    StartTextureAnim (reset flag), 0x00446420 StopTextureAnim, 0x00446450 ToggleRenderMode (0x0048C0D0). The sector
    methods (0x0049E820-0x0049E880, thiscall, forwarding to the sector's +0x14) are called on a null sector too, as
    the original does (they read +0x14 of it).
  - Quirks kept / noted: values a command reads without a default are uninitialised in the original (our zeros
    are only seen when the script leaves them out).
  - Test (selftests_engcmd1.cpp TestEngineCommandsLighting): every command, original vs ours, all called game
    functions and RegCreateKeyExA / RegSetValueExA / RegCloseKey stood in for by recording code (the registry is never
    touched); each branch: terrain by checksum / number (a number over 255) / found or not, the sun fields one at a
    time and both flags, the gamma over 5 start triples (including -0.0 and NaN) x 4 colours x 3 changes with the
    key failing for some, fog with distance clipping off / on, sectors missing from the scene. 40 deliberate bugs
    caught, 1 (change > 0 vs >= 0) after adding the -0.0 gamma start.
- [x] Batch EC1-2 (NOT run in the game yet; render debugging, the animation system, the light manager, the screen,
  texture splats, gamma / light values, clipping and fog, render modes, bone positions, 26 functions):
  src/game/script/cfuncs_render.cpp (new); a fix in src/game/mth/vector.cpp.
  - 0x00446460 DebugRenderIgnore (0x0048C110(ignore_1, ignore_0)), 0x004464C0 InitAnimCompressTable ("<file>"
    copied with "%s"; t48 -> 0x00482A30, q48 -> 0x004829F0, other types nothing), 0x00446570 InitAnimSystem
    (AnimHeapSize, a second size (name unknown), DefCacheType -> 2 standard / 3 / 4 (two unnamed types) / 0 else for
    0x00480D10; two counts (one unnamed, MaxAnimSubsets) for 0x0047EB30), 0x00446650 UpdateAnimCache (enable /
    disable do nothing - compiled out; clear -> cache 0x00480DB0(0), defrag -> 0x00480F20, neither -> the cache's
    getter and a compiled-out report), 0x004466F0 FinishRendering and 0x00446810 AllowBoneLOD (0x0048C1B0 = the
    empty function: empty inlines), 0x00446700 InitLightManager (max_lights, an unnamed count), 0x00446760
    UninitLightManager, 0x00446770 PushWorldLights, 0x00446780 PopWorldLights (a 5-byte jump to 0x0048EA40: its
    bool), 0x00446790 SetScreen (aspect -> Nx::SetScreenAspectRatio, angle -> 0x004A0B70, letterbox ->
    Nx::SetLetterbox(n != 0)), 0x00446850 DebugLine (compiled out: Gfx::AddDebugLine), 0x004468E0
    GetPausePosByResolution (pos=(50 at a 640-wide back buffer / 20, 240)).
  - 0x00446930 KillAllTextureSplats: box={height dist width offset} - the box's corners at -/+ half of each size in
    inches around the BOX's offset (read from the box structure, not the params; x/y/z of a missing offset are
    uninitialised in the original); the low corner not above 0, the high not below 0; x87: the low half from the
    unrounded product, the high one from the product rounded to a float (they differ past FLT_MAX); w = offset.w + 1
    (1 without an offset); 0x00491630. texture= -> 0x004916F0, nothing -> 0x004915A0.
  - 0x00446C20 GetGammaValues (red / green / blue removed then added as truncated percentages - the product in
    double: 0.29 gives 28), 0x00446CD0 GetDynamicLightModulationFactor (ambient: 0x0048E9B0(1), (0); directional:
    0x0048E9D0(i, 1), (i, 0) - both read the same array in the original; mod_value_lo / mod_value_hi),
    0x00446D70 SetClippingDistances (both viewport cameras' +0xB8 / +0xBC; the defaults are swapped in the original:
    Near defaults to 0x006487D4 = 12000 and Far to 0x006487D0 = 12), 0x00446DF0 SetFogDistance (false with distance
    clipping; both cameras looked up and dropped), 0x00446E50 SetFogColor (r g b a 255/255/255/128; with distance
    clipping: 0x0078733C = 0xAARRGGBB and FALSE; else 0x00490E20(bytes r g b a)), 0x00446F40 SetRenderMode
    (0x006FA734), 0x00446F70 SetWireframeMode (0x006FA730), 0x00446FA0 SetLightAmbientColor and 0x004470A0
    SetLightDiffuseColor (alpha 0x80; they return the callee's AL), 0x00447010 GetLightDirection (0x0048E920's
    vector in degrees: heading = y, pitch = x), 0x00447130 GetLightCurrentColor (ambient_*, red_0.., red_1..),
    0x00447220 GetBonePosition (the skeleton component's 0x00432050; x / y / z added even without the object -
    uninitialised in the original).
  - Found and fixed: Vector::RadiansToDegrees (0x00405F20, rewritten earlier) used 57.2957795f = 0x42652EE1; the
    exe's constant (0x00645468) is 0x42652EE0 - one ulp lower. Its old self-test compares with a tolerance; this
    batch's test (GetLightDirection) compares bits.
  - Test (selftests_engcmd2.cpp TestEngineCommandsRender): every command, original vs ours, every callee stood in
    (the modulation getters run as the original over stand-in arrays; RadiansToDegrees / FeetToInches / the BBox
    constructor run as themselves); all 16 UpdateAnimCache flag combinations, each cache type, the splat box with
    positive / negative / -0 / NaN / 3e37 sizes and an offset with z -0, gamma fractions that round differently in
    float, distance clipping off / on, cameras missing, callee results passed back, bone found / not / no skeleton /
    no object. 45 deliberate bugs caught (6 after fixing the hand-built params' structure / array types, 3 more after
    an offset with -0, 1 after a fog alpha without bit 0, 1 after letterbox=1); 1 equivalent (0x0048E9D0's flag:
    both branches of the original read the same array).
- [x] Batch EC1-3 (NOT run in the game yet; spawned polygons, the scenes' vertex colours, the light direction, metrics,
  8 functions): src/game/script/cfuncs_polys.cpp (new).
  - 0x004472D0 FreeSpawnedPolyDetails (the pool pair 0x00700500's Free - the details' operator delete),
    0x00447330 SpawnPoly (0xB8-byte details from the pool pair, constructed by 0x00491990 - zeroed, blend mode
    0x54628ED7; pos into +0, two vectors (names unknown; (0,0,1,1) / (0,1,0,1)), start_dims / end_dims pairs,
    lifetime, start_colors / end_colors arrays of 4 [r g b a] (per corner) or start_color / end_color (one for all
    corners) as bytes, an unnamed flag -> flags |= 2, just=[right|left|center, bottom|center|top] -> +0xB0 / +0xB4
    of 1 / -1 / 0, BlendMode, texture -> the texture dictionary 0x00700F20's 0x0049F540; then 0x00492450(details,
    &vector1, &vector2)). A failed allocation writes through null as in the original.
  - 0x00447710 CompressSceneVertexColours(scene, target, factor, lightGroup) / 0x00447980 CompressVC (target,
    percent -> factor (100 - percent) * 0.01 rounded once, LightGroup): each scene's sectors (the hash table at
    +0xD0, the sector's light group at +0xC, its geometry +0x14: 0x0048DF70 count, 0x0048E150 colours into a
    Mem::New buffer, 0x0048DFA0 back) - channels (c - target) * factor + target in double, limited to 0..255 and
    truncated, alpha kept, nothing when target and factor are both 0; skies (scene +0x8D) skipped in the list walk,
    then the first sky (0x0048C7B0) done. A chain item without a value ends a scene's walk (as in the original).
  - 0x00447A60 NudgeSceneVertexColours(scene, amount) / 0x00447D90 NudgeVC (percent * 0.01, every scene, skies
    too): per vertex a hash ((int(x*16) * int(y*16)) ^ int(z*16)) & 0x7FFF (positions 0x0048DF80) gives the scale
    float(h / 32768 * amount * 2 + 1 - amount); red and green products rounded to floats, blue's kept in double (as
    the x87 code does), each limited to 0..255 and truncated. The positions are freed before the colours.
  - 0x00447E00 SetLightDirection (index, direction=<vector> with w 0, or heading and pitch (both) turning (0, 0, 1):
    about x by pitch then about y by heading, in the x87's own FSIN / FCOS with the products against the
    extended-precision sine / cosine as the original keeps them (X87SinTimes / X87CosTimes) and the heading's
    sine / cosine stored as floats; returns 0x0048ED70's AL), 0x00447F30 GetMetrics (a new CStruct (struct pool,
    Init) filled by 0x0048CB00, added as Metrics, cleared and freed).
  - Test (selftests_engcmd3.cpp TestEngineCommandsPolys): stand-in heap (Manager::Allocate / Free), stand-in scenes
    (a ring through a scene -1 sentinel; two skies), hash tables with chains, an empty bucket with a chain, a chain
    item without a value, a sector without geometry, light groups; geometry stand-ins; SpawnPoly with per-corner /
    single colours / neither, each `just` value, texture or not; CompressVC over light groups, NaN target, percent
    above 100, a factor that differs when rounded twice; NudgeVC with a vertex hashing to 0 whose channels show the
    float / double rounding; SetLightDirection over angles and missing heading / pitch, both callee results;
    GetMetrics with the allocation failing; the details freed to the heap and into a stand-in pool. 41 deliberate
    bugs caught (7 after making the scene list reachable, 1 after adding a pool, 4 after the rounding cases);
    1 equivalent (IterateNext never returns true with no item).
- [x] Batch EC1-4 (NOT run in the game yet; the memory manager's commands, the pool-usage dump, DumpHeaps, 15
  functions): src/game/script/cfuncs_memory.cpp (new).
  - Compiled-out commands with their arguments kept (empty inlines / Mem::PushMemProfile): 0x00447FF0
    PushMemProfile, 0x00448020 PopMemProfile, 0x00448030 SetMemThreadSafe (off), 0x00448060 DumpFragments (<size>
    0x3C00, K), 0x00448270 AnalyzeHeap and 0x004482C0 PrintMemInfo (a heap checksum, BottomUpHeap 0x0C7800B0 by
    default; params may be null; the heap looked up, PrintMemInfo copies its name). 0x00448090 MemViewToggle
    (Nx::ToggleMemView), 0x004480A0 GotExtraMemory (0x007CEFAC != 0).
  - 0x004480B0 MemPushContext (an integer: 0 = the bottom-up heap, any other number does nothing; else a heap
    name -> Manager::GetHeap), 0x00448130 MemPopContext, 0x00448140 MemInitHeap (name="<text>" size= ->
    InitNamedHeap(crc, size, text, 0)), 0x004481A0 MemDeleteHeap / 0x00448200 MemHeapExists (name as a string or a
    checksum; they return DeleteNamedHeap's result / GetNamedHeap != null).
  - 0x00448330 DumpPoolUsage (naked DumpPoolUsageStub: "to the dump file" in BL, "print" on the stack, caller pops):
    each line sprintf'd from the exe's own format texts, to Mem::DumpText and / or Dbg::Printf (the line as the
    format, as the original); the current pool of the pairs CStruct 0x006F3614, CComponent 0x006F2338,
    CSymbolTableEntry 0x006F362C, CVector 0x006F3654, CPair 0x006F3644, CArray 0x006F22B4, CScript 0x006F3548, the
    hash items (PoolManager::GetPool(0)), CAnimCachePredictor 0x006F92B8, CBlendChannel 0x006F92C8,
    CBonedAnimController 0x00701430, CAnimLookup 0x006FA7F4, sSpawnedPolyDetails 0x00700500,
    Obj::CEventReceiverList 0x006F0FC8 (used, count, peak; the pools aren't null-checked, as in the original).
  - 0x00448840 DumpHeaps (heap.cpp's DumpHeaps calls it with null params): current heap, then per heap (FirstHeap /
    NextHeap) name, used size, free size ("Frag"), region MemAvailable ("Free"), the region's minimum (read before
    MemAvailable, which can lower it), used blocks - in K (signed / 1024) or in bytes with an unnamed flag
    (0xD65B9AC7); the sound manager's free memory (0x0047AE10 on 0x006F85A8 - 0 in this build - called twice, the
    second divided); then the pool dump (print only).
  - Test (selftests_engcmd4.cpp TestEngineCommandsMemory): a stand-in manager (its methods stood in for, ours and
    the game's), three heaps with negative / odd sizes for the K rounding, a MemAvailable that lowers the minimum,
    13 pool pairs with the current pool alternating (the other one a different pool), every command with null / given
    params, named heaps there or not, the dump through its register convention with all four flag pairs (BL's upper
    bits dirty), DumpHeaps in K / bytes / null params / no heaps. 29 deliberate bugs caught (1 after giving the pairs'
    other pool).

## Game runs (C:\Games\THUG2-1, under the lock)
- 2026-10-21, after EC1-4 (217c18a): `run_game.ps1 -Level foun -FullTests` - 507 early, 17 late, 2 engine-started
  self-tests passed in the game, 3300 replacements installed, Foundry loaded and ran (lit as usual); then
  `-Level BO` (Boston) loaded and ran. No EXCEPTION / RUN-TIME CHECK / DEBUG CRT in thug2-decomp.log.
- For the user to check by hand: gamma changes in the options (ApplyChangeGamma writes the registry twice, as
  the original), fog / clipping commands in levels that use them, CompressVC / NudgeVC (debug scripts), DumpHeaps
  output (debug), SpawnPoly users.
