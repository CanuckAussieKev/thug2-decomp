# appmain: 0x4D6D40-0x4E6B80 ("Application main & game object rendering glue")

Agent "appmain". Branch worktree-agent-ac70ca06307ace3fc, game workspace C:\Games\THUG2-4.

## The range

202 Ghidra functions (56 rewritten before: WinMain, Sys::Main, ParseNetworkParams, InitDirect3D, SetCamera, the
blur pass, SSprite::Draw, the SingletonPtr holders, the flip-rotate component). Most of the range is the PC Nx
renderer's device side: the cached render state, the device's objects, occlusion and view culling, shadows, a
scene's mesh list, the pixel shaders, the screen effects (bloom, blur, the 2D pass); then the language start-up
(0x4E0DA0 / 0x4E0FE0), and after WinMain some game object components (0x4E2D20-0x4E6B80).

Shared files touched (small, additive): CMakeLists.txt (new sources and selftests_appmain*.cpp);
src/hook/selftests_text.cpp (TestInitDirect3D also stands in for our CreateDevice / ResetRenderState /
CreateRenderTargets / LoadGamma, which InitDirect3D now calls directly); src/game/gfx/device.{h,cpp} (InitDirect3D's
callees rewritten next to it).

Test kit: src/hook/appmain_testkit.h - recording stand-in COM objects (device, Direct3D, textures, surfaces: every
call traced with its arguments), GameMemory (save / fill / dump game memory), Lcg, CallEscapable (run a function
that never returns and end it from a stand-in).

## Batches

- [x] Batch AM-1 (NOT run in the game yet; the renderer's cached device state and the device's start-up objects):
  src/game/gfx/render_state.{h,cpp} (new), src/game/gfx/device.{h,cpp}.
  - 0x004DAB60 `Nx::SetState(state, value)` (cdecl; the renderer's own render state numbering, RenderStateId): z write
    / z test (LESSEQUAL or ALWAYS) / alpha ref (0 = alpha test off) / sampler addressing 4-7 (U low 16 bits, V high:
    wrap, clamp, border - clamp without border support 0x786AC0; other values cached, nothing sent) / mip LOD bias 8-11
    / cull / alpha blend / alpha test / specular (with the local viewer) / fog / depth bias (unsigned x the float
    0x64AAA4, x87 double then float) / lighting / filters 0x20-0x23 (bit 16 magnification, bit 0 minification linear)
    / local viewer; 0xC-0xF and 0x17-0x1F nothing. Every send through Nx::SetRenderState (0x44F870, ours), as the
    original's tail jumps. The cache is 0x786E30-0x786F34 / 0x787338-0x787348.
  - 0x004DA760 `SetBlendMode(mode)`: an override (0x786F34) wins; the low 24 bits pick (op, src, dest) from the table
    (switch 0x4DA9BC: 0/0xB, 1-4, 5/0xC, 6, 7/8, 9, 0xA, 0xD, 0xF, 0x11; 0xE, 0x10 and above use the ARGUMENT - not the
    override - for all three), black fog (via 0x44F870) under modes 1-4 and the fog colour (straight to the device)
    otherwise, a fix-up without blend factors (0x786AC1: BLENDFACTOR/INVBLENDFACTOR -> ONE/INVSRCALPHA, a lone
    BLENDFACTOR source -> ZERO, destinations -> SRCALPHA / INVSRCALPHA), and the top byte as the blend factor (doubled,
    0x80 and up saturating, in all four channels).
  - 0x004D9DB0 `SetVertexFormat` (unless locked 0x786F28), 0x004DA730 `SetTexture(stage, texture)`, 0x004D9DE0
    `SetFixedFunctionStages(shader, factor)` (the texture stages standing in for the fixed pixel shaders 0x7CA944-64
    without pixel shader support: tested in the original's order, 0x7CA958 and unknown ones = texture only),
    0x004DAA10 (the same with the material's colour +0x40 x 255 through _ftol, white without a material),
    0x004DAA80 `SetPixelShader(shader, material)` (or the stand-in stages without ps.1.1 0x786AB8; lock 0x786EC4;
    five constants 0x786ECC), 0x004DAAF0 (n constants, then constant 4 again), 0x004DA0E0 (returns 0x7CA978 in ST0),
    0x004DB620 `SetWorldTransform` (the object transform 0x7CA968 -> copy 0x7CA980 with w = 0, 0, 0, 1, or null).
  - 0x004D7D70 `ResetRenderState` (identity transforms 0x786B90/BD0/C10, cache defaults, 0xDEADBABE "no blend mode",
    the default light and ambient as bit patterns, fog -240 / -24600) and 0x004D80D0 `ApplyRenderState` (the cache
    to the device: render states, per sampler magnification / mip linear, anisotropic minification on sampler 0 at
    back buffers up to 480 lines (signed compare; min(anisotropy, 3)), wrap addressing, a zero material, material
    sources, 0x4CD940, 0x4DD7F0 (the shaders), fog, multisample antialias with the setting 0x7D6434).
  - 0x004D7C80 `CreateDevice(width, height, hardwareVertices, multisample)` (HAL, hardware or software vertex
    processing | MULTITHREADED; the adapter identifier zeroed and read; DirectSoundEnumerateA through the exe's import
    0x64502C; 0x786AB5 = "8500" in the adapter description (Radeon 8500); 0x67DA40 = 0 for an Intel 915G (8086:2582)
    with the last sound device "Realtek HD Audio rear output"; returns CreateDevice's HRESULT) and its callback
    0x004D7C20 `SoundDeviceFound` (not a Ghidra function: copies each description to 0x786AC8 - over 200 characters
    only the first 199, unterminated - returns TRUE so the last one stays).
  - 0x004D8420 `CreateRenderTargets` (dynamic buffers 0x5F4620(0x40000) / 0x5F4690(0x1000); nine X8R8G8B8 render
    target textures at 1, 1, 4, 4, 4, 8, 8, 16, 16ths and their level-0 surfaces; a depth surface; the back buffer and
    depth surface pointers kept (released at once); a 256x256 target for depth textures (0x786AB9); the capabilities:
    ps.1.1, stream offset, border addressing (only ever set), max anisotropy, blend factor on both sides; the back
    buffer size / format, depth bits 16 / 32, the first target's format; then ApplyRenderState) and 0x004D7AF0
    `ReleaseRenderTargets` (surface pointers left as they were; 0x5F4650, 0x5F4670, 0x5F52D0).
  - 0x004D7B60 `LoadGamma` (Settings\Gamma R/G/B, 0.25 each without; the key is never closed) -> 0x4D07B0.
  - 0x004D89C0 `ShowDiscError` (never returns: the timer killed, the device made if missing, the two lines of the
    disc error in French (language 3), German (5) or English, drawn forever with Present / Clear and fixed states).
  - InitDirect3D (0x4D8650, earlier batch) now calls these directly.
  Test (src/hook/selftests_appmain.cpp, Early): a recording stand-in device; TestRenderStateCache - 4000 random calls
  of every setter per variant from random caches made of the values used, capability flags and locks toggling, four
  handle variants (distinct, all null, 0x7CA944-58 equal); TestRenderStateDefaults (5 cases: heights 480 / 481 / -5,
  anisotropy, antialiasing, with and without the reset); TestCreateDevice (9 cases: vertex processing, multisampling,
  failing, "8500", Intel ids, Realtek last / not last / with a suffix, a 230- and a 200-character name, none);
  TestRenderTargets (5 cases: every caps flag on / off, D16 / D24, sizes that don't divide, depth textures, no
  surfaces, the first surface missing; then the release); TestLoadGamma (key missing, each value missing); TestShowDiscError
  (each language, timer, device missing; run 0-3 frames and ended from the stand-in Present with CallEscapable).
  Mutations: 55 deliberate bugs, all caught (52 at first; the handles-equal variant, the first-surface-missing case and
  a stand-in that changes the query size were added for the other 3).
