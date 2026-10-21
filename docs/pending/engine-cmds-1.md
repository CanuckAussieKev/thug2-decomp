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
