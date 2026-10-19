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
- [x] Batch AM-2 (NOT run in the game yet; occlusion polygons and view culling): src/game/gfx/occlusion.{h,cpp} (new).
  - Occlusion polygons (0x787360, 0x58 bytes: in use, enabled, checksum, four corners, normal) and occluders
    (0x792360, eight of 0x5C: the polygon, five planes, what each hid in the last two frames):
    0x004D8D70 `AddOcclusionPoly(p0, p1, p2, p3, checksum)` (normal (p1 - p0) x (p3 - p0) with p3.x - p0.x kept in
    double precision, then Mth::Vector::Normalize), 0x004D8C90 `EnableOcclusionPoly`, 0x004D8CC0
    `RemoveAllOcclusionPolys`, 0x004D8BB0 `CompareOccluders` (qsort's comparison - not a Ghidra function; disabled
    polygons' counts cleared, more hidden first), 0x004D8C10 `SortOccluders` (the game's qsort; occluders that hid
    nothing in two frames dropped from the end, this frame's count cleared), 0x004D8CE0 `AddOccluders` (up to four
    more enabled polygons a frame - counted even when the eight are full - carrying on from the last),
    0x004D8ED0 `BuildOccluders(eye, frame)` (which side the eye is on - not a number counts as behind - picks the
    corner order; ten inlined cross-product / normalise / distance sequences, each kept as the original rounds it:
    SetPlane's length order (x,z,y / z,y,x), one plane with the unrounded inverse for x, the distance products in
    z,y,x order for the polygon's own plane), 0x004D9AC0 `SphereOccluded(centre, radius, weight)` (behind all five
    planes of an occluder: a distance that isn't a number counts as behind; the occluder credited; the object
    transform's position added unrounded).
  - Culling against the camera (Engine glue's "SphereVisible" 0x4B2310 jumps to the first): 0x004DB570
    `SphereOutsideView` (int 1 / 0), 0x004DB790 `SphereInView` (bool; the object transform's position added and
    rounded; leaves -z - radius in 0x7CA978), 0x004DB6D0 `BeyondClipDistance` (with distance clipping 0x7D643A on:
    |view position| - radius past the far clip). Near plane 0x786CA8, far 0x786CAC, the side planes' factors
    0x786E20-2C. D3DXVec3Transform (0x5BF19F, a dispatch thunk) stays a library call.
  - Note for engine_glue.cpp (another range): its AddDebugShape / FlagDebugLines / ClearDebugLines are really
    AddOcclusionPoly (type 4 = four corners) / EnableOcclusionPoly / RemoveAllOcclusionPolys.
  Test (src/hook/selftests_appmain2.cpp, Early): TestOcclusion - four seeds of 15-18 polygons (quads at random
  places and turns; degenerate ones; reversed; one with a corner that isn't a number; one whose differences don't fit
  a float), 60 frames each of enabling by checksum, eyes moving (also on a polygon's plane, behind them all), 40
  spheres a frame (some not a number, weights -2..4, the object transform on / off), SortOccluders / AddOccluders
  called directly, RemoveAll part way; all the memory compared. TestViewCulling - exact edges first (radius - z ==
  near, radius == a + b, == c - d, and one float either side) then 3 x 3000 random spheres through all three tests
  with a stand-in D3DXVec3Transform, random view / clip values, the transform on / off.
  Mutations: 34 deliberate bugs; 29 caught (24 at first, 5 after adding the NaN polygon / NaN spheres, the
  wide-range polygon and the exact edges). The other 5 only change the order of a sum of squares or of products
  before a float rounding (length orders, distance orders) - below float precision for any input the test could
  find; they're kept as the original orders them.
- [x] Batch AM-3 (NOT run in the game yet; shadows): src/game/gfx/shadow.{h,cpp} (new).
  - Shadow casters (0x10C bytes: render target, model, view, projection (near 5), the receivers' projection (near 80),
    the texture matrix) in the hash table at 0x7CA970, keyed by the target's address: 0x004DDC90 `AddShadowCaster`,
    0x004DB0A0 `RemoveShadowCaster`, 0x004DB100 `AimShadowCaster` (up is z when looking straight along y - x and z
    both equal - else y; D3DX LookAt / Ortho 120 x 120). The casters are walked with the table's own iterator: an
    item with no value ends the walk (as the original).
  - 0x004DBBD0 `RenderShadowTextures` (each caster with a model into its texture: fog off for it, the model's
    active meshes drawn with flag 8 and marked 0x200; casters without a model skipped), 0x004DBF50 `DrawShadows`
    (each caster's texture projected onto the receivers in view: 0x004DA0F0 `ProjectShadowVertices` (the clip
    flags from the float bits as the original), 0x004DA190 `ClipShadowTriangles` (dropped when all three are off
    one side, or facing > -1e-4 worked out in double), DrawPrimitiveUP, the draw / triangle counters 0x786EBC /
    0x786EC0), 0x004DB880 `ShadowTextureMatrix` (the view's inverse is worked out and not used), 0x004DB9D0
    `DrawShadowVolumes` (the scene's flagged meshes in view; two-sided stencil, or the two sides apart with
    blend modes 0x10000002 / 0x10000004; the vertex-colour pixel shader locked meanwhile; ZFUNC GREATEREQUAL
    without two-sided stencil).
  Test (src/hook/selftests_appmain3.cpp, Early): TestShadowProjection (random positions / matrices so every clip
  bit and both facings happen, degenerate and repeated indices, and a triangle facing exactly -1e-4);
  TestShadowPasses (6 seeds over a stand-in caster table: a model-less caster with more after it, an empty table,
  render skipped, fog / depth bias / border colour on and off, a render target without a depth surface, receivers
  hidden / in view / flagged; stand-in D3DX in double precision); TestShadowVolumes (two-sided or not, the blend
  override cleared, the pixel shader lock recorded during each draw); TestShadowCasters (add three, aim along every
  case incl. x equal with z not and -0 / 0, remove in buckets / chained / behind an empty value / missing).
  Mutations: 31 deliberate bugs, all caught (24 at first; the exact-limit triangle, the blend override cleared,
  the lock recorded in the draw, the model-less caster moved before others, a missing depth surface, the x-only
  aim case and near-identity caster matrices - so triangles are really drawn - were added for the other 7).
- [x] Batch AM-4 (NOT run in the game yet; a scene's mesh list): src/game/gfx/mesh_list.{h,cpp} (new; Nx::sScene).
  - 0x004DDD50 `CompareMeshes` (qsort's: the material's sort key - not a number counts as different, greater
    later -, its +0xC, the same material, its flag 0x10, the mesh's +0x44, the material's address), 0x004DE300
    `sScene::Construct`, 0x004DE490 `DestroyBody` (the material table and its buckets, each mesh's first vertex
    buffer, the list, the records array, the COM object), 0x004DE340 `UpdateSphere` (x and y halves in double,
    z's size rounded first, as the original), 0x004DE3E0 `AllocateMeshList`, 0x004DDDF0 `CountMeshes`,
    0x004DDE20 `AddMeshes`, 0x004DDE60 `RemoveMeshes` (each gap closed; a null already in the list is taken for
    one - with none to be found the original never ends, and neither does this), 0x004DDFA0 `SortMeshes` (the
    game's qsort, then the transparent / flagged ranges recounted), 0x004DE020 `FindMesh` (by load index),
    0x004DE420 `FindMaterial` (by checksum, through the table's iterator), 0x004DE060 `BreakStrips` (records
    that match the mask mark a strip break in their mesh's indices; each mesh with breaks gets a rebuilt index
    buffer with the break's index repeated twice more; the original's 64-entry break table is overrun by load
    indices of 64 and up - here those are skipped), 0x004DE560 `DeleteScene` (each material destroyed and
    freed, then the scene).
  Test (src/hook/selftests_appmain4.cpp, Early): TestMeshList - 4 seeds of 24 meshes over 8 materials (sort keys
  equal / different / not a number, flags), CompareMeshes on every pair, then construct (state recorded), count
  (recorded), allocate, add, sort, find (also missing), find materials (also a checksum of 0, no table), the
  sphere from 300 bounds of every size, strip breaks (masks, records found at the first place looked or later,
  some not matching, none), remove (also absent and repeated), destroy or delete; the buffers / allocation /
  vertex buffers stood in; all memory compared. Load indices kept under 64 (see above).
  Mutations: 26 deliberate bugs, all caught (20 at first; the states recorded after construct / count, more
  bounds, a material checksum of 0 and records matching at once were added for the other 6).
- [x] Batch AM-5 (NOT run in the game yet; pixel shaders and one-pass materials): src/game/gfx/pixel_shaders.{h,cpp},
  simple_material.{h,cpp} (new).
  - 0x004DD7F0 `CreatePixelShaders` (once: the handles cleared; without pixel shaders the stand-in numbers 1-10 the
    fixed-function stages tell apart, else the six fixed shaders assembled - their sources as literals - and
    created; the shadow caster table made), 0x004DD2C0 `GeneratedPixelShader` (a material's passes: the key from
    the pass count, modes, passes and per-pass alpha flags 0x004D1D90; the cache 0x7CA9C0 - the first item with
    the key and a shader -; else the ps.1.1 text written as the original sprintfs it, assembled, created and
    cached; the "texture only" branches are unreachable since every pass counts as present, as in the original),
    0x004DDC00 `MaterialPixelShader` (one pass: the fixed shaders by texture / brighten / alpha flags; more: the
    generated one).
  - 0x004D9B80 `sSimpleMaterial::Construct` (the texture dictionary's entry, direct or indirect; a 0x110-byte
    material: sort key rand() * rand() with the game's rand, one pass, blend mode by name with the fixed alpha in
    the top byte, colour 0.5, alpha fixed / 128; the shader by mode), 0x004D9CD0 `DestroyBody`, 0x004D9CF0
    `BlendModeFromName`, 0x004DA6D0 `SkipFileChunk`, 0x004DA710 `SkipMemoryChunk`.
  Test (src/hook/selftests_appmain5.cpp, Early): TestFixedPixelShaders (with / without pixel shaders, once only,
  stand-in D3DXAssembleShader recording the text), TestGeneratedPixelShaders (3 x 160 materials of 0-4 passes,
  every mode, alpha flags, failing assembly, cached keys later left without a shader), TestSimpleMaterial (every
  blend name and others, textures found / indirect / missing, fixed alphas incl. out of range, destroy, the chunk
  skippers).
  Mutations: 30 deliberate bugs; 29 caught (27 at first; cache items left without a shader and all 18 names in
  the constructor were added for 2). The other (gen_reg) changes the unreachable "texture only" branch.
