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
