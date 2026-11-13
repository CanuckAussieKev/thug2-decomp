# A Vulkan renderer, and ray tracing after it

Status: Vulkan is the default renderer since V11 (`[Graphics] renderer=d3d9` chooses Direct3D 9; Direct3D 9 also runs
when Vulkan can't start, with a log line). History: V1 (the libraries), V2 (the context), V3 (the fixed-function emulation, "V3"
below), V4 (the choice at start-up) and V5a (`render::Gpu` and the render-state cache through it, "V5a" below) are
done. Written against the `stage11` branch (2026-11-05); the numbers come from a scan of `src/` (see the
end).

## The goal

1. **A native Vulkan backend** at parity with the Direct3D 9 one: the same frames, effects and UI, chosen at start-up,
   with Direct3D 9 kept as the reference. Translating Direct3D 9 at run time (DXVK) is not the goal: the point is a
   renderer the project owns, so it can grow.
2. **Ray-traced features after it**: shadows first, then reflections / ambient occlusion, as options over the
   original's look. They need the 64-bit build (docs/design/x64-port.md).
3. A renderer that builds with GCC later: only standard Vulkan, SDL2's Vulkan surface and portable libraries.

## Where the seam is today

The engine reaches the graphics backend only through two interfaces (docs/architecture.md, "The graphics backend"):

- **`render::Device`** (`engine/gfx/render_device.h`, 90 methods). Each method stands for one of the backend's own
  functions:
  - the frame: `start_engine`, `begin_frame_draw`, `draw_world`, `finish_rendering`;
  - a lost device's reset in the main loop's steps: `can_reset`, `release_device_objects`, `reset`,
    `restore_device_objects`;
  - resources: scenes from memory or a file, meshes, models, geoms, sprites, textured quads, render targets, model
    lights, primitive drawers, texture dictionaries, viewports, texture projections, flexible particle systems,
    fonts, the text and window pools;
  - state blocks: fog, gamma, the 2D clip, the primitives' states and depth test, the world's lights;
  - effects a batch at a time: flash, shatter, texture splats, texture shadow casters, the sun's occlusion pixels;
  - what is shown: screenshots, the back buffer's copy, the disc error and loading screens, the loading bar;
  - what the level editor asks: view modes, HUD lines, a geom's meshes and instance, its ground tool's buffers, the
    Add palette's preview.
- **`render::UiBackend`** (`engine/gfx/ui_backend.h`, 17 methods): what the editor's and the new front end's Dear
  ImGui layers ask, a frame at a time. It covers start, process an event, new frame, draw the draw data, invalidate,
  make and release images (`UiTextureId`, already 64 bits), and two targets the renderer draws into (the preview and
  the thumbnails' atlas).

Both are installed by `app/main.cpp` before any of the original's code runs. A second backend is a second
implementation chosen there, and nothing below `app/` links a backend.

The grain is coarse: about 52 device calls a frame in steady play (measured in stage 11). The traversal (which
sectors, meshes and objects are drawn, in which passes and order) happens inside the backend's `draw_world`. The
backend is the original's PC platform layer, and its objects are `WorldScene`, `SubMesh`, `Geom`... derived from the
engine's types.

## What the Direct3D 9 backend actually uses

`src/render_d3d9`: 63 `.cpp` files, 23,607 lines. 42 files call the Direct3D device, in 458 lines. The other 21 files
(4,817 lines: geoms, mesh loading, sub-meshes, mesh lists, lights, shatter, vertex buffers' memory copies, grass,
scene load, material load, fonts, 2D, models, sprites...) never call it. Much of the files that do call it is also
API-neutral logic (sorting, CPU skinning, particle simulation, weather, culling).

### Calls (call sites in `src/`, tests excluded)

| call | sites | call | sites |
|---|---:|---|---:|
| SetRenderState | 94 | SetRenderTarget / SetDepthStencilSurface | 11 / 11 |
| SetTextureStageState | 61 | GetSurfaceLevel | 11 |
| SetSamplerState | 35 | BeginScene / EndScene / Present | 6 each |
| SetTransform | 22 | SetFVF | 5 |
| DrawPrimitiveUP | 21 | DrawPrimitive | 5 |
| SetStreamSource | 20 | CreateVertexBuffer / IndexBuffer | 5 / 3 |
| CreateTexture | 20 | SetPixelShader / CreatePixelShader | 4 / 3 |
| LockRect / Lock | 19 / 14 | SetPixelShaderConstantF | 4 |
| SetIndices | 15 | CreateDepthStencilSurface / CreateRenderTarget | 4 / 3 |
| DrawIndexedPrimitive | 15 | TestCooperativeLevel / Reset | 3 / 1 |
| Clear | 14 | SetLight / LightEnable / SetMaterial | 2 / 4 / 2 |
| StretchRect / GetRenderTargetData / GetFrontBufferData | 2 / 1 / 1 | CreateQuery / Issue / GetData | 1 / 2 / 1 |
| SetGammaRamp | 1 | CheckDeviceMultiSampleType | 2 |
| SetVertexShader / SetVertexShaderConstantF | 2 / 2 (saved and put back around the billboards; the game makes no vertex shader) | D3DXAssembleShader | 1 |

### Fixed-function state

- **Texture stages** (`D3DTSS_`):
  - `COLOROP` / `ALPHAOP` with `MODULATE`, `MODULATE2X`, `MODULATE4X`, `SELECTARG1`, `SELECTARG2`, `DISABLE`;
  - the arguments `TEXTURE`, `DIFFUSE`, `CURRENT`, `TFACTOR`, `ALPHAREPLICATE`;
  - `TEXCOORDINDEX` with `D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR` (environment maps);
  - `TEXTURETRANSFORMFLAGS` `COUNT2` / `DISABLE` (scrolling and projected UVs);
  - 8 calls pass the stage state through a variable (the material passes' tables).
- **Render states** (`D3DRS_`, 94 sites, about 40 states):
  - depth (`ZENABLE`, `ZWRITEENABLE`, `ZFUNC` `LESSEQUAL` / `GREATEREQUAL`, `DEPTHBIAS`);
  - blending (`ALPHABLENDENABLE`, `SRCBLEND` / `DESTBLEND`, `BLENDOP` `ADD` / `REVSUBTRACT`, the factors `ZERO`,
    `ONE`, `SRCALPHA`, `INVSRCALPHA`, `SRCCOLOR`, `DESTCOLOR`, `BLENDFACTOR`, `INVBLENDFACTOR`);
  - alpha test (`GREATEREQUAL` with a reference);
  - `COLORWRITEENABLE`, `CULLMODE`, `FILLMODE`, `STENCILENABLE`, `SCISSORTESTENABLE`, `TEXTUREFACTOR`;
  - fog (`FOGENABLE`, `FOGCOLOR`, `FOGSTART` / `FOGEND`, `FOGVERTEXMODE` `LINEAR`, `FOGTABLEMODE` `NONE`,
    `RANGEFOGENABLE`);
  - fixed-function lighting (`LIGHTING`, `AMBIENT`, `COLORVERTEX`, `DIFFUSE` / `SPECULAR` / `AMBIENT` /
    `EMISSIVEMATERIALSOURCE`, `SPECULARENABLE`, `LOCALVIEWER`; directional lights only);
  - point sprites (`POINTSPRITEENABLE`, `POINTSCALEENABLE`, `POINTSIZE`, `POINTSCALE_A/B/C`);
  - `MULTISAMPLEANTIALIAS`, `DITHERENABLE`.
- **Samplers**: `MIN` / `MAG` / `MIPFILTER` (`POINT`, `LINEAR`, `ANISOTROPIC`, `NONE`), `MAXANISOTROPY`,
  `ADDRESSU` / `V` (`WRAP`, `CLAMP`, `BORDER`), `BORDERCOLOR`.

### Shaders

The game uses no vertex shaders: transform, lighting and fog are fixed-function, and skinning runs on the CPU (one to
three bones a vertex, written into the vertex buffer: `pc_mesh_render.cpp`). Pixel shaders are `ps.1.1`, assembled by
D3DX at start-up (`pixel_shaders.cpp`):
- **6 fixed shaders**:
  - texture times vertex colour, x4;
  - vertex colour times a factor;
  - the bloom's cut-off;
  - vertex alpha times texture alpha;
  - constant alpha times texture alpha;
  - texture times a constant alpha;
- **one generated per material**: a key built from the pass count (up to 4 passes), each pass's 5-bit blend mode and
  which passes take their alpha from the texture alone, cached by that key. Each pass samples `t<n>` and combines it
  by its blend mode.
- Without pixel-shader support the same materials are drawn pass by pass through the texture stages, and stand-in
  numbers tell the fixed-function paths apart.

### Vertex formats

All are FVF, with no vertex declarations:
- meshes: `XYZ | NORMAL | DIFFUSE` plus 0-4 texture coordinate sets, from a table by set count (`pc_mesh_load.cpp`);
  skinned meshes unpack from a 0x4C-byte record;
- `XYZ | DIFFUSE` (14 places), `XYZ | DIFFUSE | TEX1` (9), `XYZ | TEX1` (2);
- pre-transformed screen vertices `XYZRHW | DIFFUSE [| TEX1]` (5: 2D, movies, the loading screen and console, the
  editor's lines).

Primitives: triangle lists (23) and strips (9), line lists (4), point lists (2). Indices are 16-bit.
`DrawPrimitiveUP` (21 sites) draws from memory each time.

### Textures and targets

- **Formats**:
  - `A8R8G8B8`, `X8R8G8B8`;
  - `DXT1`, `DXT5`;
  - `P8` (palettised, with a palette in place);
  - `A1R5G5B5`, `X1R5G5B5`, `R5G6B5` (screenshots and old textures);
  - depth `D24S8`, `D24X8`, `D16`; the UI backend also tries `D24X4S4`, `D15S1`, `D24FS8`.
- **Pools**: `MANAGED` (15), `DEFAULT` (13), `SYSTEMMEM` / `SCRATCH` (1 each). Usage: `DYNAMIC`, `WRITEONLY`,
  `RENDERTARGET`.
- **Render targets and screen copies**:
  - the frame drawn into a screen copy instead of the back buffer, then downsampled, blurred (motion blur) and bloomed
    back (`screen_fx`, `draw_frame`);
  - texture shadows: a caster's model drawn into a texture from the light's view, then projected on the meshes around
    it (`shadow_base`);
  - stencil shadow volumes;
  - texture projections;
  - the editor's preview and atlas targets;
  - the screenshot's resolve (`StretchRect` + `GetRenderTargetData`) and the front buffer's JPEG
    (`GetFrontBufferData` + D3DX).
- **Occlusion queries**: one kind (`D3DQUERYTYPE_OCCLUSION`). The light glows keep up to 4 outstanding queries each,
  and the sun's lens flare reads its pixels.
- **MSAA**: 2 samples or none (the present parameters; `[Graphics] msaa=` asks for more where the device has them).
- **Gamma**: `SetGammaRamp` (the gamma option).
- **Device lost**: `TestCooperativeLevel` / `Reset` in the main loop's steps, with the `DEFAULT`-pool objects dropped
  and made again.

## A Vulkan backend: the shape

**Proposal: split the backend before writing the second one.** A Vulkan backend that implements `render::Device`
directly would have to repeat the whole PC platform layer: 23,600 lines, most of them not about Direct3D. Instead:

```
engine  --render::Device-->  render_pc/        the PC platform layer, API-neutral: scenes, meshes, materials and their
                                               passes, CPU skinning, sorting, particles, weather, shadows' logic, 2D
                             --render::Gpu-->  gpu_d3d9/    Direct3D 9: the 458 device-call lines, as they are
                                               gpu_vulkan/  Vulkan (its Gpu and GpuContext: V6; draws: V7)
engine  --render::UiBackend-->  d3d9 / vulkan UI backends (ImGui's dx9 renderer backend / the Vulkan Gpu)
```

`render::Gpu` is the lower interface. It is still coarse, never per vertex:
- textures and render targets made, locked or filled, released;
- vertex and index buffers (static, dynamic, and a ring for the `UP` draws);
- a **fixed-function state block** as one value: the texture stages, the blend / depth / alpha-test / cull /
  colour-write / stencil states, fog, lights and material sources, texgen and texture transforms, point sprites;
- the transforms;
- a draw (indexed or not, from a buffer or the ring);
- clears, copies and resolves, an occlusion query, present, and lost / reset.

Direct3D 9's implementation of it makes the same calls in the same order, so the D3D9 build's frames don't change.
The engine-facing `render::Device` doesn't change at all.

**Fixed-function emulation in Vulkan.** The state block's shader-relevant part (the texture stages' ops and args, alpha
test, fog mode, lighting on / off and the number of directional lights, texgen, texture transform, point sprite,
vertex format) is a key. Each key gets a generated GLSL vertex / fragment pair compiled to SPIR-V, the way the
original generated `ps.1.1` per material. The blend, depth, cull and colour-write parts become the pipeline's fixed
state. Pipelines are cached by (key, fixed state, render pass) and kept in a Vulkan pipeline cache on disk. The 6
fixed `ps.1.1` shaders and the per-material generated ones are translated to GLSL by hand and by the same generator.

### V3: the fixed-function emulation (done)

`src/gpu_vulkan`: `ff_key.h` (the key), `ff_shaders.h` (key -> GLSL), `ff_uniforms.h` (the uniform blocks,
bindings and attribute locations), `glsl_compiler.h` (GLSL -> SPIR-V, and a library of compiled shaders by key);
`src/tests/ff_shader_check.cpp` (the check).

**What the game draws with, measured from `src/render_d3d9` (now `src/render_pc`).** With pixel shader support (every Vulkan device), the
backend's draws use:
- the six fixed `ps.1.1` shaders (`0x964`, `0x95C`, `0x948`, `0x944`, `0x940`, `0x960`) and the per-material shader for
  two to four passes. Every other pixel shader slot (`0x94C`, `0x950`, `0x954`... in `shader_slots.h`) is empty with
  shaders, so the draws that ask for one (glows, occlusion queries, screen copies, shadow volumes) draw with the
  texture stages as they were left;
- the texture stages at about fifteen places (2D, text, movies, weather, glows, primitives, shadows, loading screens),
  which set some stage states and leave the others as the draws before left them. `submit_pass` (particles) leaves
  stage 0's COLORARG2 at TFACTOR and stage 1 enabled; environment-mapped passes leave COUNT2 and the reflection
  vector on a stage's coordinates. So the state a draw sees depends on the order of the draws before it: the key is
  derived at draw time from the tracked Direct3D 9-style state, not chosen per call site;
- lighting with two directional lights, the material sources COLOR1 / COLOR2 / COLOR1 / MATERIAL, specular and the
  local viewer (all sticky); vertex fog LINEAR by range, never table fog; alpha test GREATEREQUAL only; point sprites
  with point scale (snow); vertex formats `XYZ | NORMAL | DIFFUSE | TEX0-4` (meshes), `XYZ [| DIFFUSE] [| TEX1]`,
  `XYZRHW | DIFFUSE [| TEX1]`, `XYZRHW | TEX1`; never PROJECTED, never more than two stages enabled.

The check's model of those draws (161 sites; the closure over every order of the sites' stage settings reaches 13,149
stage states) makes 3,972 vertex keys and 23,304 fragment keys: 400 texture-stage combinations, 4 x 6 fixed shaders
(fog, alpha test), and the materials' shapes (every mode at every pass for 2 and 3 passes; for 4 passes every mode at
each pass plus every combination of the modes that read r1's or the pass before's alpha; `--all-materials` makes them
all).

**The compiler: glslang at run time.** The keys a session draws come from its scenes' materials (any of 14 shader
modes per pass, four passes, alpha flags: 43,904 four-pass shapes alone) and from the order of the draws, so they can't
be listed when the game is built. glslang (the Vulkan SDK 1.4.304.0 release, built from source by CMake: no Python, no
SDK; its GLSL front end and SPIR-V back end only) is linked into `gpu_vulkan` and compiles each key the first time
it is drawn (`ShaderLibrary`); V7 adds the pipeline cache on disk and can warm it from a scene's materials. SPIRV-Tools'
validator (same release; its build needs Python 3) is linked by the check alone. Shaders target Vulkan 1.0 / SPIR-V 1.0.

**The key.** `ShaderKey` = `VertexKey` (40 bytes) + `FragmentKey` (48 bytes): plain bytes with no padding, compared and
hashed as bytes, every enumerator with Direct3D 9's own number. `normalised()` clears what a half doesn't read (stages
after the first disabled one, arguments an operation doesn't read, CURRENT at stage 0 as DIFFUSE, lighting fields
with lighting off, coordinates of stages not sampled, material modes without shader code...), so the halves are
compiled separately and shared by every pipeline that uses them. It lives in `gpu_vulkan`: `render::Gpu` (V5a)
carries the Direct3D 9-style state block, and the Vulkan side derives the key from it. Matrices, colours, fog
distances, the alpha reference, the texture factor and the pixel shader constants are uniforms (`ff_uniforms.h`).

**The formulas** (each written beside its code in `ff_shaders.cpp`):

| part | formula |
|---|---|
| position | `clip = P (WV v)`, then y negated and half a pixel added (`x += w / width`, `y += w / height`: Direct3D 9's pixel centres are at whole pixels, Vulkan's at halves). Pre-transformed: `ndc = (xy + 0.5 - viewport origin) * 2 / size - 1`, `gl_Position = (ndc, z, 1) / rhw` |
| lighting | `diffuse = Ce + Ca (Ga + sum La) + Cd sum(Ld max(N.L, 0))`, alpha `Cd.a`; `specular = Cs sum(Ls max(N.H, 0)^P)` for `N.L > 0`, `H = normalize(L + V)`; clamped to 0-1; unlit: the vertex colours (missing diffuse white, missing specular black) |
| vertex fog | linear `(end - d) / (end - start)`, exp `e^-(d density)`, exp2 `e^-((d density)^2)`, clamped; `d` the camera-space depth or, with range fog, the distance |
| table fog | the same per pixel from the camera-space depth |
| texture coordinates | a set padded to `(u, v, 1, 0)` (the component after the last 1), or the camera-space normal / position / reflection `I - 2 (N.I) N`, through the texture matrix for COUNTn, divided per pixel for PROJECTED |
| stages | every D3DTOP but PREMODULATE and the bump maps: `MODULATE a1 a2`, `MODULATE2X 2 a1 a2`, `MODULATE4X 4 a1 a2`, `ADD`, `ADDSIGNED a1 + a2 - .5`, `ADDSIGNED2X`, `SUBTRACT`, `ADDSMOOTH a1 + a2 - a1 a2`, the BLEND*ALPHA lerps, `DOTPRODUCT3 4 ((a1 - .5).(a2 - .5))` in every channel, `MULTIPLYADD`, `LERP`; COMPLEMENT and ALPHAREPLICATE on arguments; each stage's result clamped to 0-1; then the specular colour added |
| ps.1.1 | line by line (`_x2` / `_x4` scale, `_sat`, co-issue), constants clamped to -1..1 as ps.1.x reads them, the result clamped to 0-1 |
| alpha test | `round(alpha * 255) <op> ALPHAREF` |

**Uncertain, to settle by V11's comparison:** the non-local viewer's V (Direct3D 9's documentation writes `(0, 0, 1)`;
`(0, 0, -1)`, towards the camera, is used); whether the reflection vector's normal is normalised; a missing
coordinate set (zeros); the fog factor of pre-transformed vertices without a specular colour (0, all fog); w-based
table fog; how drivers quantise the alpha for the test (rounded); ps.1.x's intermediate range (unclamped here; the
first ps.1.1 cards clamped every result to -1..1); the alpha operations of the colour-only MODULATE*_ADD* operations
(taken as SELECTARG1). Two things of the original's own: mode 0xB on a pass after the first writes `v1`, which ps.1.1
can't (D3DX would refuse it; ported as a write nothing reads), and the original's shader cache key keeps the fourth
pass's texture-alpha flag in the same bit as the first pass's present bit (`pixel_shaders.cpp`), so two four-pass
materials differing only in that flag share the first one's shader there; the key here keeps the flags apart.

**The check** (`thug2_ff_shader_check [--all-materials] [--no-gpu] [--device N] [--pipelines N]`): the model's keys,
each half generated, compiled (8 threads) and validated by SPIRV-Tools; then each module handed to the driver and a
pipeline made for each sampled pair (the vertex format's attributes, the descriptor layout, a colour + depth render
pass) on the chosen device. It draws nothing: comparing pixels against Direct3D 9 is V11's.

**Method by method** (`render::Device` and `UiBackend`, against Vulkan):

| group | Vulkan |
|---|---|
| the frame (`start_engine`, `begin_frame_draw`, `draw_world`, `finish_rendering`) | instance, device, swapchain through `SDL_Vulkan_CreateSurface` (the window made with `SDL_WINDOW_VULKAN`); per-frame command buffers, fences and semaphores; `finish_rendering` submits and presents (FIFO for the default present interval) |
| a lost device (`can_reset`, `release_device_objects`, `reset`, `restore_device_objects`) | Vulkan has no "lost on Alt-Tab": these become swapchain re-creation on `VK_ERROR_OUT_OF_DATE_KHR` / a resize / a mode change, with the size-dependent targets (screen copies, depth) made again. A real `VK_ERROR_DEVICE_LOST` is fatal (logged, as the fatal exits are) |
| scenes, meshes, models, geoms (the resources) | made by `render_pc` (unchanged); their buffers through `Gpu`: device-local buffers filled through staging (VMA). `MANAGED`'s CPU copies are already kept (`vertex_buffers`) |
| texture dictionaries | `P8` expanded to RGBA8 on load; `DXT1` / `DXT5` as BC1 / BC3; the 16-bit formats as Vulkan's 16-bit formats where supported, else expanded; mips as loaded |
| render targets, texture projections, shadow casters | images with colour / depth attachments and sampled usage; render passes (or dynamic rendering) per target |
| state blocks (fog, gamma, 2D clip, primitive states, lights) | the fixed-function key and push constants / a per-draw uniform ring; the 2D clip as a scissor; **gamma as a final full-screen pass** (no gamma ramp in Vulkan) |
| effects (flash, shatter, splats, shadows) | logic in `render_pc`; the draws through `Gpu`; stencil volumes need a stencil format (`D24S8` isn't available everywhere: `D32_SFLOAT_S8_UINT` as the fallback) |
| `occlusion_pixels`, the glows' queries | a query pool per frame in flight, results read a frame or more later as the original does (it keeps 4 outstanding) |
| screen copies, blur, bloom | blits / copies between images and full-screen passes; Direct3D 9's half-texel offset is removed in the Vulkan path (the pixel centres differ) |
| `screen_shot`, `capture_back_buffer` | the swapchain image (made with `TRANSFER_SRC`) copied to a host buffer; the JPEG written by stb's JPEG writer instead of D3DX |
| loading screen, bar, console panel, disc error, movies | screen-space draws through `Gpu`; a movie's frames uploaded to a dynamic texture each frame |
| the editor's methods | the same records (`engine/gfx/mesh_records.h`), which the editor reads directly: `render_pc` keeps them, so the Vulkan side only provides the buffers' upload and the preview's target |
| `UiBackend` | ImGui's SDL2 backend, its draw lists drawn by the Vulkan `Gpu` in its own frame (not `imgui_impl_vulkan`: V9); `UiTextureId` = a `Gpu` texture's handle (64 bits); the preview and atlas as render-target textures with their own depth; `invalidate_device_objects` = drop the font image |
| MSAA | sample counts 2/4/8 as the device has them; resolve before the screen copies and present |

**Libraries** (fetched and pinned like the others, licences to confirm before adding):
- Vulkan-Headers (Apache-2.0);
- volk, the loader (MIT);
- Vulkan Memory Allocator (MIT);
- a GLSL -> SPIR-V compiler at build time: glslang (BSD-3 / Apache-2.0) or shaderc (Apache-2.0);
- (ImGui's Vulkan backend: not used - V9 draws ImGui's lists through the Vulkan `Gpu`.)

The validation layers come from the Vulkan SDK, debug builds only.

**32-bit.** The raster backend doesn't need x64. Windows Vulkan drivers ship 32-bit ICDs (to confirm per vendor for
current drivers), so the Vulkan backend can be brought to parity on the 32-bit build, next to the Direct3D 9 backend
and the same frames.

### V5a: `render::Gpu` (done)

`engine/gfx/gpu.h` declares it (beside the handles, `engine/gfx/gpu_handles.h`, which gained `GpuPixelShader` and
`GpuQuery`); `gpu_d3d9/gpu_d3d9.cpp` is Direct3D 9's, installed by `app::install_renderer` with the device.

- **Numbers**: the states (`GpuRenderState`, `GpuStageState`, `GpuSamplerState`, `GpuTransform`, `GpuPrimitive`)
  and their values (`gpu_value::`) are Direct3D 9's own numbers, named; formats, pools, usages and lock flags pass
  as Direct3D 9's numbers. `gpu_d3d9.cpp` checks every name and every structure's layout (material, light, viewport,
  rectangles, locked rectangle, gamma ramp, matrix) against `<d3d9.h>` at compile time.
- **Operations** (one per device call, never per vertex): the state setters; the `get_*` reads (Direct3D 9 asks the
  device; another API can answer from the block); textures, vertex / index buffers, render targets, depth and
  offscreen surfaces, occlusion queries made, locked, set, copied, read back and released; stream source and
  indices; draws from buffers (indexed or not) and from memory (the `UP` draws); begin / end scene, clear, present,
  the gamma ramp, the cooperative level.
- **The state block** (`GpuStateBlock`): the setters are non-virtual - they record into the block, then make the one
  virtual call. It holds every render state (0-209), 8 stages x 33 stage states, 16 samplers x 14 sampler states,
  the 16 textures, the FVF, the pixel shader and c0-c7, the material, 8 lights and their enable bits, world / view /
  projection / 8 texture transforms, the viewport. It starts from (and `reset_state_block` returns to) Direct3D 9's
  documented defaults, at device creation (`reset_render_state`) and after a reset. Everything V3 lists for the
  ShaderKey is in it; like the device, it is sticky.
- **Moved behind it**: the render-state cache (`render_state.cpp`: `change_phase`, the vertex format, the stage
  textures, the blend modes, the stand-in texture stages, the pixel shader and its constants, `apply_render_state`),
  `primitive_states.cpp`, and the device's `set_render_state` / `clear_device` wrappers (which carry about 40 more
  render-state sites in the meshes, the frame, the shadows and the wireframe view). The cache's own decisions - what
  it skips as unchanged, in the renderer's own encodings - are unchanged; both files include no Direct3D header now.
  In steady play about 2,300 of the frame's ~7,300 device calls go through the Gpu.
- **Left for V5b-e**: the other files still call the device (`d3d9::state().d3d_device()`); routing one is replacing
  each call by the Gpu's (the arguments are the same numbers). Not in the interface yet: pixel shader creation (the
  generated `ps.1.1` and its Vulkan counterpart need a description, not tokens), the vertex shader save / restore
  around the billboards, device creation and `Reset` with the present parameters, the front buffer's copy, state
  blocks and the other rarely used calls.

### V5b: meshes, materials, instances through `Gpu` (done)

`vertex_buffers`, `pc_buffers`, `mesh_list`, `sub_mesh`, `pc_mesh_life` (the buffers made, locked, retained, released,
unbound and made again after a reset), `pc_mesh_render` (the skinned mesh's fog, world transform, lock and indexed
draw), `pc_material` (the stage states, the texture matrices, the texture factor, the specular material),
`pc_instance` (the two lights, the world transform, the vertex colour state), `pixel_shaders` (the shaders made) and
`pc_mesh_load` (the buffers' usage, format and lock numbers) include no Direct3D header any more
(`tools/d3d_headers.test.ts`). The same calls with the same numbers in the same order; the lights and the
specular material are `GpuLight` / `GpuMaterial` (Direct3D 9's layouts), the matrices plain floats.

- **Added to the interface**: `retain_vertex_buffer` (a share of an arena's holds a reference), `get_stream_source`
  (a mesh's destruction asks which buffer the stream holds, to unbind it), `create_pixel_shader` (the ps.1.1 token
  stream the assembler made: Direct3D 9 passes it on, another API reads the tokens or the program they stand for -
  the text is still assembled by the D3DX wrapper, which is V5e's to move), `gpu_value::kUsageWriteOnly` /
  `kUsageDynamic` / `kPoolDefault` / `kFormatIndex16` / `kLockReadOnly` / `kLockDiscard`.
- The meshes' world draws now go through the Gpu in steady play: about 6,900 of the frame's ~7,300 device calls.

### V5c: particles, billboards, weather, grass, glow, splats, shatter through `Gpu` (done)

`pc_flexible_particles`, `pc_particles` (the dynamic vertex buffer's batches: stream source, quads' indices, indexed and
plain draws), `pc_billboards` and `billboard_quad` (the camera-facing meshes and the 2D sprites' quads), `pc_weather`
(rain lines, snow points, splash quads), `pc_grass` (a lock flag only), `glow` (the occlusion-tested quads),
`texture_splats` and `shatter` (draws from memory) include no Direct3D header any more
(`tools/d3d_headers.test.ts`). The same calls with the same numbers in the same order; one `Gpu` call where a device call
was, nothing per particle. The part of the platform layer these files use - the dynamic vertex buffer's lock / unlock,
the quads' index buffer, the occlusion answer - is `render_pc/dynamic_vertices.h`, in the handles' terms
(`pc_d3d_resources.cpp` converts at the edge; V5d moved its creation behind the `Gpu`).

- **Added to the interface**: `set_vertex_shader` / `set_vertex_constants` (recorded in the state block beside the pixel
  shader's: `vertex_shader`, `vertex_constants` c0-c7), `get_vertex_format`, `get_pixel_shader`, `get_vertex_shader`,
  `get_indices` (the billboards save and restore what the device holds around their draw), `GpuVertexShader`, and the
  vertex format bits (`kFvf*`, `engine/gfx/mesh_formats.h`) checked against `<d3d9.h>`.
- **Design choice (flagged)**: the saved shaders and index buffer come back as Direct3D 9 gives them - with a reference
  the original never released for the shaders (it leaked them) - so `get_*` is the device's own read, one call each.
  A Vulkan `Gpu` keeps its pipeline in the state block already, so `get_pixel_shader` / `get_vertex_shader` /
  `get_vertex_format` / `get_indices` read the block and take no reference (the caller's release of the buffers is a
  no-op there), and the billboard's vertex shader is its own vertex stage, selected by `set_vertex_shader`.
- State blocks, stream-source frequency, the scissor rectangle and clip planes are not used by any of these files; the
  calls the logs show (`SetStreamSourceFreq`, `SetClipPlane`, `CreateStateBlock`...) are Dear ImGui's Direct3D 9 backend,
  which is not meant to go through the `Gpu`.

### V5d: screen effects, the frame, shadows, texture projections, viewports through `Gpu` (done)

`screen_fx` (the screen copies' blur and bloom), `draw_frame` (transforms, black bars, the motion blur, viewport, fog
states), `shadow_base` (the shadow textures and their projection onto the receivers; the stencil volumes' z function and
culling), `pc_texture_projection`, `pc_viewport` (the sun's occlusion queries), `flash`, `wireframe`, `pc_fog`,
`pc_gamma` and `view_mode` include no Direct3D header any more (`tools/d3d_headers.test.ts`). The same calls with the
same numbers in the same order. `pc_d3d_resources.cpp` makes, locks and releases the dynamic vertex buffer, the quads'
index buffer and the occlusion queries through the Gpu (`dynamic_vertices.h` gained begin / end of a query, in plain
types); it and `graphics_options_d3d.cpp` stayed Direct3D files for the device and its multisample / present parameters
until V5e, which moved them behind the `GpuContext`.

- **Added to the interface**: `texture_level_size` (GetLevelDesc: the blur reads the copies' sizes), `release_pixel_shader`
  (the editor's unlit twins), `gpu_value::kLockNoOverwrite`, `kResultOk`, `kResultNotReady`, `kFvfXyzRhw`. `device.h`
  hands out the frame's surfaces as handles: `screen_copy_texture(i)`, `scene_depth_surface()`,
  `back_buffer_surface()`, `back_depth_surface()`, `presented_height()`, `device_made()`.
- **Render targets and screen copies (flagged)**: there is no "begin / end of a pass" operation, because Direct3D 9's
  calls already carry it. `set_render_target` + `set_depth_surface` name the attachments (the pair is always set
  together, as the original does: the screen copies, the shadow textures with a null depth, the MSAA scene surface,
  the back buffer); a Vulkan `Gpu` opens a pass lazily at the first draw or clear and ends it at the next
  `set_render_target` / `set_depth_surface` / `copy_surface` / `present`, or when `set_texture` binds an image that is an
  attachment of the open pass (then the image goes to shader-read layout, and back to attachment at its next
  `set_render_target`). A screen copy is a texture the passes sample and, through `texture_surface(level 0)`, the surface
  set as the target; the surface reference between `texture_surface` and `release_surface` is a name for that image.
  The MSAA scene surface is resolved with `copy_surface` (the resolve of a multisampled attachment, which Vulkan can do as
  the pass's own resolve attachment or as `vkCmdResolveImage`). `clear` first in a pass is the load-op clear.
  The shadow textures (`render_shadow_textures`) are one pass per caster with no depth attachment; the targets are put back
  afterwards from the `render_target` / `depth_surface` reads, which a Vulkan `Gpu` answers from its block.
- **Occlusion queries (flagged)**: made, begun, the quad drawn, ended, read a frame or more later (the sun's flare asks
  with `query_result` every frame and takes 1, "not ready", for no answer yet): the Vulkan `Gpu` takes a slot of the
  frame's query pool at `create_occlusion_query`, brackets the draws with `begin_query` / `end_query` inside one pass
  (colour writes are off, the quad is depth-tested only), and returns the slot at `release_query`; results come back with
  the frame's fence.
- **Stencil (flagged)**: `draw_shadow_volumes` sets only the z function (`kZFunc`), the culling and the blend modes of its
  passes; no stencil render state is set anywhere in the game's code, and the device's depth format is `D24X8` (no stencil
  bits). The volumes' draws are therefore stencil-free in this build, and a Vulkan `Gpu` needs no stencil aspect for
  parity; the numbers for it (`kStencilEnable`, the other stencil states) are recorded in the block if a build ever sets
  them, and the stencil format question in the table above (`D32_SFLOAT_S8_UINT`) stays open for then.

### V5e: 2D, text, textures, loading screens, movies, screenshots, device creation through `Gpu` and `GpuContext` (done)

`text`, `pc_scene`, `scene_render`, `pc_primitive`, `editor_support`, `loading_bar`, `loading_screen`,
`loading_console_panel`, `bitmap_texture`, `pc_texture`, `texture_file`, `movie`, `screenshot_capture`, `pc_engine`,
`device`, `d3d9_device`, `graphics_options_d3d` and `pc_d3d_resources` include no Direct3D header any more
(`tools/d3d_headers.test.ts`). The same calls with the same numbers in the same order; the text and the 2D already made
one Gpu call per device call (`draw_indexed_from_memory` per text batch, `draw_from_memory` per quad), nothing per glyph.

- **The creation interface (`engine/gfx/gpu_context.h`, `render::GpuContext`)**, installed by `app/` beside the `Gpu`;
  Direct3D 9's is `gpu_d3d9/d3d9_context.cpp`, the one place with `Direct3DCreate9`, `CreateDevice`, `Reset`, the
  `D3DPRESENT_PARAMETERS`, the adapter's identifier, the caps and the multisample support:
  `create_instance`, `adapter_ready`, `sample_quality_levels(samples, colour, depth, windowed)`, `configure(GpuSettings)`,
  `create_device(GpuDeviceRequest)`, `settings()`, `adapter()`, `depth_textures_supported()`, `caps()`, `device_made()`,
  `mark_ready()` / `ready()`, `reset()`, `shutdown()`.
  - **Present parameters as settings (flagged)**: `GpuSettings` is the back buffer's size, windowed, vsync, the
    multisample count and quality, and the colour and depth formats as Direct3D 9's format numbers
    (`gpu_value::kFormat*`). Direct3D 9 fills its `D3DPRESENT_PARAMETERS` from them (vsync: the driver's default
    interval; off: immediate); Vulkan builds the swap chain from them (size, the surface format picked to match the colour
    format, vsync: FIFO present mode, off: MAILBOX or IMMEDIATE; windowed / full screen: a window mode of the platform's
    windowing, there is no exclusive mode needed) and the depth and multisampled images from the depth format and sample
    count. The window it presents to is the platform's (`sys::game_window()`), not a setting.
  - **Device lost and reset (flagged)**: a Direct3D 9 device is lost (alt-tab from full screen) and brought back by
    `GpuContext::reset()` with the stored settings, after the engine's loop released the default-pool objects
    (`release_device_objects`) and before it makes them again (`restore_device_objects`): `Gpu::test_cooperative_level`
    answers `gpu_value::kResultDeviceLost` while it is lost. In Vulkan nothing is lost like that: `reset()` is the swap
    chain's recreation (`vkDeviceWaitIdle`, the old swap chain passed as `oldSwapchain`, the frame's images made again at
    the stored size), and `test_cooperative_level` answers the lost value after a present or acquire returned
    `VK_ERROR_OUT_OF_DATE_KHR` (a resize, a minimise, a display mode change) and 0 once the recreation worked; a
    `VK_SUBOPTIMAL_KHR` is treated as out of date at the next frame. The release / restore steps are the same code: the
    screen copies, the depth surface, the multisampled scene target, the render-target textures and the dynamic buffers
    are Vulkan images and buffers that must be sized again.
  - **Caps**: `GpuCaps` is the six answers the renderer reads (ps 1.1, stream offsets, max anisotropy, anisotropic
    minification, border addressing, blend factor); a Vulkan `Gpu` answers them from the physical device's features and
    limits (`samplerAnisotropy` / `maxSamplerAnisotropy`; border addressing is `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER`;
    blend factors are always available; ps 1.1 is true because the pixel shaders are made from V3's keys; stream offsets
    are `vkCmdBindVertexBuffers` offsets, always available). `adapter()` is the physical device's name, vendor and device
    id (the Radeon 8500 and Intel 915G checks of the original).
  - **MSAA**: `sample_quality_levels` is Direct3D 9's `CheckDeviceMultiSampleType` on both formats; a Vulkan
    implementation answers 1 (quality levels do not exist) for every sample count the physical device's
    `framebufferColorSampleCounts & framebufferDepthSampleCounts` has.
- **The frame's objects are handles in `device.cpp`** (the screen copies, their level 0, the depth surface, the back
  buffer and its depth), not in `d3d9_state.h`: made and released through the `Gpu` in the original's order.
- **Textures (flagged)**: made, locked, read, copied and released through `create_texture` / `lock_texture` /
  `unlock_texture` / `texture_surface` / `texture_level_desc` / `texture_level_count` / `release_texture` with Direct3D 9's
  format, pool and usage numbers (`gpu_value::kFormat*`, `kPool*`, `kUsageRenderTarget`). The formats the game makes:
  A8R8G8B8, X8R8G8B8, X1R5G5B5, A1R5G5B5, DXT1, DXT5 and P8. **P8 (paletted) has no Vulkan format**: a Vulkan `Gpu`
  expands it to RGBA8 when the texture is made (`create_texture` with `kFormatP8` allocates `VK_FORMAT_R8G8B8A8_UNORM`;
  the caller locks and writes 8-bit indices, and `unlock_texture` expands them). The game never sets a Direct3D palette (no `SetPaletteEntries` / `SetCurrentTexturePalette`), so what a P8
  texture shows in the original is the driver's default palette; which levels draw one at all is for V6 to find out (log
  the P8 creations) before choosing what the expansion's palette is. Palettised images in the game's files already make
  A8R8G8B8 and expand with `expand_palette` on the CPU; P8 is made by the texture dictionaries' 8-bit textures with no
  palette block (`pixel_layout`) and `PcTexture::create_image`. X1R5G5B5 and
  A1R5G5B5 are converted the same way (`VK_FORMAT_A1R5G5B5_UNORM_PACK16` exists where supported, else expand), DXT1 / DXT5
  are `VK_FORMAT_BC1_RGBA_UNORM_BLOCK` / `BC3_UNORM_BLOCK` (the block-compressed levels uploaded as they are; level sizes
  are in blocks, as `copy_from` and `construct_copy` already count rows). The managed pool (a system-memory copy kept
  for the device to be reset) is a staging buffer in Vulkan; a texture made in the default pool with render-target usage
  is an image with colour-attachment and sampled usage.
- **Text and 2D**: `text` sets the stage and cull states and draws the batch with `draw_indexed_from_memory` (the
  caller's indices and vertices, copied by the Gpu for this draw: a ring in Vulkan); the font page texture is one
  `create_texture`, one `lock_texture`, one `unlock_texture`.
- **Loading screens**: drawn by a timer thread that runs on the Gpu while the main thread loads (as the Direct3D device is
  `D3DCREATE_MULTITHREADED`). The Gpu's state block is not locked; a Vulkan `Gpu` must serialise these calls with the main
  thread's (a mutex on the Gpu's command recording) or the loading screen must be drawn from the main thread between
  loading steps - decided by V10 (the mutex: "V10" below). `loading_console_panel` saves the device's whole state (`Gpu::save_state`, a state
  block in Direct3D 9, a copy of the Gpu's state block in Vulkan: `release_saved_state` frees it), sets its own states,
  draws, and restores (`restore_state`).
- **Movies**: the Bink frame's decode (`copy_to_buffer`) writes straight into the locked X8R8G8B8 texture
  (`lock_texture` / `unlock_texture`): Vulkan returns a staging buffer pointer and copies it to the image at `unlock`.
- **Screenshots**: `capture_back_buffer` reads the back buffer through `back_buffer`, `surface_desc`,
  `create_render_target` + `copy_surface` (the multisample resolve), `create_offscreen_surface` (system memory),
  `read_render_target`, `lock_surface` / `unlock_surface`; `screen_shot` (the script command's JPEG) uses `display_size`,
  `create_offscreen_surface` (scratch), `read_front_buffer` and `d3dx::save_surface_to_jpg` - the one D3DX call left in it:
  a Vulkan backend reads the last presented image back with `lock_surface` and writes the JPEG itself.
- **Added to the interface**: `texture_level_desc`, `texture_level_count`, `surface_desc`, `lock_surface`,
  `unlock_surface`, `back_buffer`, `display_size`, `read_front_buffer`, `save_state` / `restore_state` /
  `release_saved_state` (`GpuSavedState`), `gpu_value::kFormat*`, `kPoolManaged`, `kPoolSystemMemory`, `kPoolScratch`,
  `kUsageRenderTarget`, `kResultDeviceLost`, `kResultNotFound`.
- **What included a Direct3D header** (V5f's input; V5f moved these to `src/gpu_d3d9`, the only folder that includes one): `gpu_d3d9.cpp` (the Gpu), `d3d9_context.cpp` (the creation),
  `d3d9_state.h` / `.cpp` (the device and instance pointers), `d3d9_handles.h` (the handle conversions), `d3dx.cpp` (D3DX9:
  the shader assembly and the JPEG, `d3dx.h`'s declarations are Direct3D-free), `d3d9_ui_backend.cpp` and
  `imgui_texture_id.h` (Dear ImGui's backend, `render::UiBackend`; V9 made the Vulkan one) and `d3d_matrix.h` (unused
  since V5d; V5f deleted it).

### V6: the Vulkan `Gpu` and `GpuContext` for the resources (done)

`src/gpu_vulkan` (the folder `render_vulkan` renamed, library `thug2_gpu_vulkan`): `gpu_vulkan.h` (`render::vulkan::gpu()`,
`gpu_context()`; no Vulkan header seen through it), `vulkan_gpu.cpp`, `vulkan_gpu_context.cpp`, and their pieces
`vulkan_formats`, `vulkan_memory`, `vulkan_samplers`, `vulkan_ring`, `vulkan_jpeg`, `vulkan_objects.h`, `vulkan_backend.h`;
`src/tests/vulkan_gpu_check.cpp` (the check). Nothing draws yet and app/ doesn't install it: `renderer=vulkan` still runs
Direct3D 9 (V7).

- **The link edge first**: `render_pc` called D3DX's shader assembly (`pixel_shaders`, `view_mode`) and its JPEG writer
  (`pc_engine`'s `screen_shot`), which `gpu_d3d9` defined. Both are `Gpu` operations now: `create_pixel_shader(source,
  length, shader)` (Direct3D 9: `D3DXAssembleShader` then `CreatePixelShader` from its tokens, the buffers released; the
  Vulkan `Gpu` keeps the text for V7) and `save_surface_jpeg(file, surface)` (Direct3D 9: `D3DXSaveSurfaceToFile`; Vulkan:
  the surface's pixels made RGB and encoded with stb_image_write). `gpu_d3d9` no longer includes or links `render_pc`.
  Direct3D 9's results are the same: the same token streams (the 22 shaders of a foun run, hashed where they are made) and
  the same JPEG bytes (a screen shot of a fixed picture).
- **The context**: `create_instance` opens the instance, the window's surface, the physical device (discrete first, or
  the one asked for) and the logical device with anisotropic filtering, BC textures and wireframe where offered;
  `create_device` makes the swapchain, the memory, the back buffer (an image of its own, B8G8R8A8: V7 presents it) and the
  depth surface the settings name. `sample_quality_levels` answers 1 for a sample count both attachments have; `caps()`
  comes from the features and limits; `reset()` makes the swapchain again.
- **Formats**: A8R8G8B8 / X8R8G8B8 are B8G8R8A8 (the same bytes; X's alpha is read as 1 through the sampled view's
  swizzle); R5G6B5, A1R5G5B5 and X1R5G5B5 are Vulkan's own 16-bit formats (the same words) where the device samples and
  filters them, else widened to B8G8R8A8 (each channel to its nearest 8-bit value); DXT1 / DXT3 / DXT5 are BC1 (RGBA,
  DXT1's one-bit alpha) / BC2 / BC3, the blocks uploaded as they are (a device without BC refuses them); **P8** is widened
  through a grey palette (index n: n, n, n, opaque) - the game sets no Direct3D palette, and the Direct3D 9 drivers of
  today don't make P8 textures, so this is a choice, in one place (`p8_palette`); depth D24S8 / D24X8 are
  D24_UNORM_S8_UINT, else **D32_SFLOAT_S8_UINT** (the AMD iGPU here has no D24S8), D16 is D16_UNORM.
- **Textures**: a level's lock hands out Direct3D 9's layout (rows of 4x4 blocks for BC) from a system-memory copy of the
  level (the managed pool's own), a partial lock the area's first byte; the unlock converts the level and uploads it; a
  read-only lock uploads nothing. A level's surface shares its texture's references, as Direct3D 9's do.
- **Buffers** (VMA): static ones in device memory with a system-memory copy, the locked part uploaded at the unlock; dynamic
  ones in memory the processor writes, locked in place - a discard moves to a version the device is done with (V7's
  frames record when a version was last drawn from; until then it is the same memory), a no-overwrite keeps it. The draws
  from memory copy their vertices and indices into a ring (a buffer per frame in flight, doubled when outgrown, the old
  one kept until its frame comes round).
- **Surfaces**: render targets (lockable ones read back at the lock, uploaded at the unlock; multisampled ones resolved
  whole by `copy_surface`), depth surfaces, render-target and depth textures, system-memory surfaces; `copy_surface` is a
  blit (or a resolve); `read_render_target` and `read_front_buffer` read the image back (the front buffer: the back
  buffer's image, the last frame V7 will present, in the top left of the display-sized surface).
- **Queries**: pools of 64 occlusion slots; until V7 records the draws between `begin_query` and `end_query`, the end runs
  the query alone (0 pixels), and `query_result` answers from the pool.
- **Samplers**: made once per combination of a stage's states (`sampler_key` from the state block): filters, addressing,
  the border colour as the nearest of Vulkan's three (the game uses only 0, transparent black), LOD bias, the most detailed
  level, anisotropy clamped to the device's.
- **References**: every object counts them as Direct3D 9's does; the device holds a reference to the render targets, the
  depth surface, the streams and the index buffer it is given; textures and shaders set in the state block are not held
  (the block keeps their addresses - V7 must not draw with one that was released).
- **The state block** is the interface's, recorded by its setters; the Vulkan `Gpu`'s `send_*` only answer, the reads
  (`get_*`) answer from the block, and `save_state` / `restore_state` are the block's copy alone.
- **Threads**: the loading screen's timer thread and the console panel draw through the `Gpu` while the main thread loads.
  Every `Gpu` and `GpuContext` operation of the Vulkan backend takes one lock for its whole length, so their calls run one
  at a time; the state block is recorded before the lock is taken (as with Direct3D 9: it is a record).
- **Transfers** (uploads, read-backs, blits, resolves, the queries for now) run at once in a command buffer of their own,
  waited for: an unlock is finished when it returns. V7 moves the draws' work into the frame's command buffers.
- **The check** (`thug2_vulkan_gpu_check [--device N]`): every kind made, filled, read back from the device's memory,
  compared byte for byte with what was written (the expected texels written out from the formats' definitions), and
  released, on both GPUs here; no validation layer is installed on this machine, so the Khronos validation was not run.

### V7: the Vulkan `Gpu`'s draws and the frame (done)

`renderer=vulkan` now runs the game on Vulkan: app/ installs render_pc's device over `render::vulkan::gpu()` and
`gpu_context()`. New in `src/gpu_vulkan`: `vulkan_pixel_program`, `vulkan_draw_state`, `vulkan_pipelines`,
`vulkan_frames`, `vulkan_draws`, `vulkan_ui_backend`.

- **What it needs**: Vulkan 1.3's dynamic rendering (no render pass or framebuffer objects), `VK_KHR_push_descriptor`,
  and 1.3's dynamic states. app/ asks before the window is made (`render::vulkan::usable`: the loader, an instance and
  the device the context would pick); without them Direct3D 9 runs and the log says why. `[Graphics] vulkan_device=N`
  picks the physical device (Vulkan's numbering; -1, the default, the best - a discrete one first).
- **The pipeline key** (`PipelineKey`): V3's `ShaderKey` (normalised), the colour and depth attachments' formats and
  samples, the primitive's class (points, lines, triangles), the fill mode, blending (on, the two factors and the
  operation - both used for alpha too, as Direct3D 9 does without separate alpha blending) and the colour write mask.
  Dynamic, so not in the key: the viewport and scissor, the cull mode (Direct3D 9's clockwise front faces are Vulkan's
  `VK_FRONT_FACE_CLOCKWISE` in the same framebuffer coordinates), the depth test, writes and comparison, the depth bias
  (Direct3D 9's depth units as 2^24 steps; the slope-scaled bias as it is), the blend factor, the primitive within its
  class and the vertex stride. Pipelines are made on first use and kept; the GLSL's SPIR-V is kept on disk by its hash
  (`thug2-decomp-vulkan-shaders.bin`) and the driver's pipeline cache beside it (`thug2-decomp-vulkan-pipelines.bin`),
  written every 10 s when something new was made (the 64-bit build's two files end in `-x64`: a 32-bit driver's cache
  is never given to the 64-bit one). foun makes 52 pipelines from 40 shaders (0.9 s of glslang in the
  Debug build the first time, none after).
- **The key from the state block** (`draw_shader_key`): the render states, the four stages' operations, arguments,
  coordinate indices and transform flags, and the bound pixel shader's program. A pixel shader is recognised from the
  text the PC layer assembles (`classify_pixel_shader`): the six fixed shaders by their text, a material's generated
  shader by its lines (passes, blend modes, alpha flags); text it doesn't know draws with the stages and is logged.
- **Uniforms**: both blocks filled from the block per draw (world x view, the normal matrix - the inverse transpose of
  its 3x3 -, the projection, the stages' texture matrices, the viewport for the pixel offset, the lights' directions
  into camera space by the view, the material, fog, points, the pixel shader constants, the texture factor, the alpha
  reference) and copied into the draws' ring only when they changed. Textures and samplers are push descriptors (one
  set layout for every pipeline: the two blocks, four textures); a stage with no texture, or with one released while
  still set, samples a 1x1 opaque white texture.
- **Passes** (V5d's inference, with dynamic rendering): a draw or clear opens one on render target 0 and the depth
  surface (a depth surface of another sample count is left out); it stays open while they stay and ends at a draw or
  clear into other targets, a copy, a transfer into something the stream used, a read-back or the present. A clear of
  the whole target with no pass open on it opens the pass clearing it (load op); other clears are
  `vkCmdClearAttachments` within the viewport. As a pass opens its attachments move to their attachment layouts (a
  barrier after their earlier use); as it ends, textures drawn into move back to be sampled. Nothing is recorded per draw
  but the draw's own state: no barrier per draw.
- **The frame** (`vulkan_frames`): two frames in flight, each an uploads and a stream command buffer, a fence and the
  swapchain acquire's semaphore. A submission is both, uploads first; submissions are numbered and resources remember
  the last one that used them. The present blits the back buffer (resolved first when multisampled) into the acquired
  swapchain image, submits, presents, and waits for the frame in flight that comes round (its retired objects
  destroyed, its rings started again). A swapchain out of date or suboptimal is made again by the next present; the back
  buffer is the Gpu's own image, so the device is never lost (`test_cooperative_level` answers ok) and
  `GpuContext::reset` remakes the swapchain after finishing the frame's work.
- **Uploads**: texture levels, static buffers' locked bytes, a new image's first layout and query resets are staged in a
  ring of their own and recorded into the uploads command buffer - before the stream - unless the stream already used
  what they write, when they go into the stream (the pass ended, buffer barriers around them). A level loads in one
  submission; while loading, the frame's work is submitted and waited for when the staging ring passes 32 MB. Read-backs
  (lockable render targets, `read_render_target`, the front buffer, the checks' views) finish the frame's work first.
- **Lifetimes**: released images and buffers go to the frame's retired list and are destroyed when that frame's slot
  comes round; a discard of a dynamic buffer moves to a version no unfinished submission drew from.
- **Occlusion queries**: begun in the pass the draws go into (opened for it) and ended there (or when the pass ends
  first); a slot is reset before the stream of a submission that hasn't used it; a result is given once its submission
  is known to have finished (a slot used before may still hold an earlier result).
- **Not yet** (V8-V10): the UI backend refuses to start on Vulkan (logged once): the editor uses its old HUD and the new
  front end isn't drawn, which is safe - both check that the backend started. The gamma ramp is kept, not applied
  (applied since V8).
  Movies and the loading screens draw through the same `Gpu` and run (the loading screen's timer thread under the
  backend's lock), as they are (V10 checked them).
- **Measured** (foun, the Debug build, the screenshot spot): about 370 draws and 12 passes a frame, 30-33 ms a frame
  with under 0.1 ms of it waiting for the device or the swapchain (the processor's work: the game, the PC layer, the
  draws' recording); the same scripted 1,921-frame run took 42.7 s on Vulkan and 63.2 s on Direct3D 9 on this machine.
  Loading foun uploads 8,499 times (40 MB, 0.03 s of the processor's time), recorded into the frames that the loading
  screen presents.
- **Compared with Direct3D 9** at the screenshot spot: the world, its textures and lighting, the skater and the HUD
  draw the same; the Vulkan picture is brighter overall (Direct3D 9's pixels are about 0.88
  of Vulkan's, a uniform scale - a full-screen pass Vulkan doesn't draw the same yet, to be found in V8) and the
  skater's texture shadow is missing (texture shadows: V8). (Both found and fixed in V8, below.) Both GPUs here (NVIDIA RTX 5070 Ti, AMD Radeon iGPU) draw it;
  they differ from each other by 0.6 of 255 on average.
- **The check**: `thug2_vulkan_gpu_check` draws end to end too - a clear that opens the back buffer's pass, a
  pre-transformed strip and an indexed draw from memory read back pixel for pixel, an occlusion query's count: 109
  checks on both GPUs. The Khronos validation layer still can't run: the SDK installed here has only a 64-bit layer, and
  the game is a 32-bit process (the context leaves a layer that can't be loaded out).

### V8: the effects at parity (done)

- **The brighter picture was the bloom.** The screen effects draw from the target they drew into just before (the
  quarter-size copy into the cut-off, the cut-off into the blur, each blur pass into the next). The pass on the earlier
  target was still open when the next draw chose its textures, so the texture was taken for one sampled while drawn into
  and the blank white one used instead: the blur passes wrote their vertex colours, and the bloom added an even grey over
  the whole frame (about 13%). A draw on other targets now ends the open pass before its textures are chosen. On both
  backends the bloom at the screenshot spot in foun now adds nothing (its cut-off, 0.94, is above every texel of the
  half-brightness copy), as Direct3D 9's always did.
- **Texture shadows** were drawn into a viewport of the screen's size and clipped away: Direct3D 9's `SetRenderTarget`
  resets the viewport to the whole of the new target 0 and the game relies on it (the shadow textures, the screen copies
  and the back buffer get no viewport of their own); the Vulkan `Gpu` now does the same. The game's shadow textures are
  colour render targets drawn with the casters' colour, not depth textures, so no comparison sampling is needed.
- **The gamma ramp**: Direct3D 9 gives a swap chain's ramp to the display hardware in full screen only (in a window it is
  ignored without an error); each channel's 8-bit value is looked up in its 256 entries and the entry's top 8 bits shown.
  In full screen, and only when the ramp changes something (the game's default doesn't), the present puts the frame
  through a lookup pass first: copied into a sampled image, drawn through the ramp into another, copied into the
  swapchain's image. Full-screen passes have their own pipelines (`Pipelines::screen_pass`: a triangle from three
  vertices without a vertex buffer, the draws' pipeline layout).
- **The swapchain**: each present compares the surface's current extent with the swapchain's (not every driver reports a
  minimised or resized window as out of date) and makes it again when they differ - none while minimised.
- **Compared with Direct3D 9** (the Debug build, NVIDIA RTX 5070 Ti, `msaa=8`, five spots: foun's screenshot spot and
  the free-skate starts of z_ho, BO, sch and z_dt): mean channel differences 0.29-0.51 of 255, 0.6-1.1% of the pixels by
  more than 8; no uniform difference left (mean signed differences 0.07-0.26). What is left: the edges of the
  multisampled scene (the two APIs' 8-sample patterns and resolves differ by a pixel's share along every edge) and
  whatever moves with time (animated machinery, traffic lights, screens, pedestrians and the skater's idle pose differ
  between runs on Direct3D 9 too). The skater's shadow, the bloom, the sun's glow and the lights' glows draw on both.
- **The checks**: `thug2_vulkan_gpu_check` adds the viewport after `set_render_target`, a target drawn from straight
  after its pass, and the gamma pass (the identity ramp makes none; a halving ramp halves a target's pixels).

### V9: `render::UiBackend` on Vulkan (done)

- **The choice: the Vulkan `Gpu` draws ImGui's lists itself; ImGui's own Vulkan renderer backend is not used.** Its
  backend records into a command buffer it is handed inside a render pass (or dynamic rendering) it is told the format
  of, and keeps its own descriptor pool, pipeline, buffer rotation and font upload (a command pool and a queue submit
  of its own, then a wait for the queue). The `Gpu`'s frame is different: passes are inferred, not opened by the
  caller; one stream command buffer per frame; two frames in flight with submission serials and deferred deletion;
  push descriptors and one pipeline layout; and a second thread (the loading screen's) presents through the same
  queue under the backend's lock. Fitting ImGui's backend in would mean exposing the stream and the open pass, keeping
  its buffers in step with our frames, and freeing its descriptor sets only once no frame in flight uses them. Drawing
  the lists through the `Gpu` is less code and fits by construction: ImGui's SDL2 platform backend stays as it is.
- **The draws** (`vulkan_ui_backend.cpp`, `record_ui_draws` in `vulkan_draws.cpp`): `render_draw_data` gathers the
  lists' vertices (ImGui's 20-byte vertex) and 16-bit indices into one batch, each command's clip rectangle, texture
  and base vertex; the batch goes into the draws' ring (per frame in flight) and is drawn in the stream, in the pass on
  the device's target 0 - the back buffer, multisampled when the game's `msaa` is on - so it is part of the frame the
  present copies into the swapchain. As a game draw does, the pass on other targets is ended first and each texture
  made ready to be sampled (the preview drawn just before is sampled from its own finished pass). `Pipelines::ui_pass`
  makes one pipeline per attachment formats and sample count: ImGui's vertex layout, the colour times the texture,
  alpha blending as ImGui's Direct3D 9 backend does it (colour: source alpha / one minus it; alpha: one / one minus
  source alpha), no depth test, the scissor per draw. The stream's bound state is forgotten after (the game's next
  draw binds its own pipeline and sets its states again). `start` makes the pipeline for the back buffer at once and
  refuses (`kDeviceRefused`) when the driver won't, so the editor's old HUD and the original menus stand in then.
- **Images**: `create_texture` makes a `Gpu` texture in the managed pool (A8R8G8B8 or DXT1, one level), filled through
  a lock; its id is its handle as 64 bits. Released through the `Gpu`, an image is destroyed only once the frames that
  may draw it have finished. The font atlas is one of them, made by `new_frame` when the context has none (ImGui's
  RGBA texels as BGRA); `invalidate_device_objects` releases it - a reset, or the fonts built again at another UI scale
  (the editor's and the new front end's scales are theirs: the draw data arrives scaled to the back buffer).
- **The preview and the thumbnails' atlas**: render-target textures (X8R8G8B8) with depth surfaces of their own in the
  game depth's format, made on first use. `begin_offscreen` / `begin_atlas_cell` save the target, the depth surface,
  the viewport and the world / view / projection transforms and switch to the picture through the `Gpu`'s own calls
  (so its state block stays true), and clear it (the atlas once whole when new, then the cell); `end_*` put them back.
- **A device reset**: the Vulkan `Gpu` loses nothing (`GpuContext::reset` makes the swapchain again); the editor and
  the front end still drop the font atlas and the two pictures around it, as on Direct3D 9, and they are made again on
  next use. The engine's log line for a refusing renderer backend now names the backend in use.
- **The checks**: `thug2_vulkan_gpu_check` adds the UI backend: an image from texels, the preview begun and ended (the
  target and viewport put back), an atlas cell, and the UI's draws read back - a textured rectangle, the preview's
  picture clipped to its top half, a half-transparent rectangle blended over another.

### V10: movies, loading screens, screenshots and the device's methods on Vulkan (done)

- **The viewport a device starts with**: Direct3D 9's device has one from its creation and after a reset - the whole of
  render target 0 - and the start-up movies draw before anything sets one. The Vulkan `Gpu`'s state block holds none
  then (zero width and height), and its draws skipped every draw with an empty viewport: the movies were black. An
  unset viewport is now taken as the whole of target 0 at the first draw, clear or `get_viewport`.
- **A reset** (`GpuContext::reset`, after the focus comes back; the PC layer's default-pool objects released around
  it and the state block reset after it, as for Direct3D 9) makes the swapchain again and leaves the bindings as
  Direct3D 9's `Reset` does: target 0 the back buffer, the others none, the device's depth surface, no vertex streams
  or indices. When the settings' size or samples changed, the back buffer and its depth surface are made again at the
  new ones (the game itself never changes them after start-up: its resolution is the ini's).
- **A stage reading its texture with none set**: Direct3D 9 draws as if the stage were disabled (the loading bar:
  stage 0 still selects the texture the loading picture left, none is set, and the bar is drawn in its vertices'
  colours). The Vulkan `Gpu` sampled its blank white texture there; `draw_shader_key` now disables the first texture
  stage that samples with no texture set (and so the stages after it). A pixel shader reads its textures itself.
- **Movies**: each frame is decoded into the locked texture's own bytes and copied at `unlock_texture` through the
  staging ring, recorded before the frame's draws (one upload a frame, no wait for the device: the frames in flight
  and the image's barrier order it after the frame before that sampled it).
- **The loading screens' threads**: the loading bar's timer and the loading console's draw whole frames (scene, the
  picture, the bar, the panel, present) from winmm's timer thread while the main thread loads - textures, buffers,
  shaders made and filled, now and then a read-back. Every `Gpu` and `GpuContext` operation takes the backend's one
  mutex for its whole length, and everything Vulkan wants externally synchronised is touched only inside it: the
  queue (submissions, `vkQueueWaitIdle`), the swapchain (acquire, present, its recreation), the command pools and
  buffers, the rings, the frames' slots and the deferred deletions. Whole frames are not atomic: an operation of the
  main thread can come between two of the timer thread's - as with Direct3D 9's multithreaded device. That is safe
  here: a read-back or a full staging ring ends the open pass and submits what was recorded, and the timer thread's
  next draw opens a new pass on the same targets (loading, not clearing). The game's own screen mutex keeps the timer
  threads' frames apart from `show_loading_screen`'s. The state block is recorded outside the lock, as before.
- **Screenshots**: `capture_back_buffer` (the screenshot key) resolves the multisampled back buffer with `copy_surface`
  and reads it back with `read_render_target`, both after the frame's work has been submitted and waited for. The
  script command's JPEG reads the last presented frame (the resolved copy the present made) into a surface of the
  window's display's size, the frame at its top left and the rest black. Direct3D 9's `GetFrontBufferData` copies the
  primary display's whole desktop instead, which Vulkan can't read (and nothing else draws there).
- **The device's methods**: every `render::Device` method runs over the Vulkan `Gpu` as over Direct3D 9's; what the
  Vulkan `Gpu` does differently by design: `test_cooperative_level` never reports a lost device (the present makes the
  swapchain again), the `send_*` setters do nothing (the draws read the state block), the vertex shader is never made
  (fixed function), the disc error's screen draws through the same calls, untested (it needs a failing read).
- **The checks**: `thug2_vulkan_gpu_check` adds a new device's viewport, a reset at another size and back, a thread
  drawing whole frames while the main thread loads with read-backs between, and the unset texture stage.

### V11: the parity check against Direct3D 9 (done)

**The tool**: `tools/game/renderer_parity.ts` (tools/game/README.md) runs a plan (`renderer_parity.json`) in a
workspace under its lock: for each run, renderer and group the game is started with `[Graphics] renderer=` written,
the skater stood still with `teleport`, the camera held with `camera`, and the presented frame captured with the
automation channel's `capture 1` (the screenshot key's capture). Each renderer's own noise is the set of pixels that
differ by more than 8 between any two of its runs (grown by a pixel); run 1 of each is compared outside the union of
both renderers' noise. The level editor, where the channel stops, is pictured with PrintWindow; movie and loading-screen
frames, and the view modes, need a measuring build that dumps them (never committed).

**The spots** (Debug build, NVIDIA RTX 5070 Ti, 1280x720, the workspace's settings: anisotropy 16, draw and LOD distance
x3; three runs a renderer; "over": pixels outside the noise differing by more than 8 in a channel):

| spot | noise D3D9 / Vulkan (% of pixels) | msaa=8: mean / largest / % over | msaa=0: mean / largest / % over |
|---|---|---|---|
| foun-shot (the screenshot spot, the shadow, the bloom chain) | 0.57 / 1.08 | 0.38 / 74 / 0.36 | 0.31 / 99 / 0.15 |
| foun-sky | 0.24 / 0.51 | 0.43 / 43 / 0.27 | 0.37 / 201 / 0.20 |
| foun-deck | 0.19 / 0.02 | 0.23 / 42 / 0.14 | 0.17 / 42 / 0.02 |
| z_dt-street | 0.16 / 0.16 | 0.30 / 128 / 0.78 | 0.15 / 128 / 0.25 |
| z_dt-back | 0.05 / 0.08 | 0.37 / 148 / 0.90 | 0.23 / 153 / 0.37 |
| BO-yard | 0.02 / 0.00 | 0.42 / 96 / 0.89 (2.60 / 155 / 9.70 before the fix below) | 0.37 / 97 / 0.62 |
| BO-up (the sky) | 0.00 / 0.01 | 0.09 / 55 / 0.31 | 0.03 / 55 / 0.05 |
| BO-far | 0.00 / 0.04 | 0.08 / 42 / 0.27 | 0.02 / 61 / 0.01 |
| z_ho-start (the sun's glow, peds) | 0.00 / 1.59 | 0.16 / 61 / 0.30 | 0.09 / 65 / 0.07 |
| z_ho-turn | 0.00 / 0.41 | 0.37 / 111 / 0.82 | 0.23 / 127 / 0.36 |
| sch-court (the skater's shadow) | 0.31 / 0.47 | 0.49 / 63 / 0.66 | 0.38 / 60 / 0.32 |
| sch-trees (alpha-tested leaves) | 0.08 / 0.18 | 0.27 / 153 / 0.57 | 0.15 / 153 / 0.23 |
| new front end: main / options / create / skater / mode / levels | 0 / 0-0.19 | 0.01-0.13 / 22-66 / 0.03-0.26 | - |
| level editor in foun (window picture) | 0 / 0 | 0.23 / 52 / 0.57 | - |
| movies (six start-up movies, frames 30 / 120 / 300) | 0 / 0 | 0.00-0.23 / 0-188 / 0-0.26 (the quad's edge columns) | - |
| loading screens (foun -> z_dt -> sch, every 6th timer frame) | 0-1.5 / 0-1.5 | 0.04-1.2 / 55-252 / 0.2-1.4 where both caught the same picture | - |
| loading console panel | 0 / 0 | 0.01 / 55 / 0.02 | - |
| Unlit view mode (foun-shot) | 0.86 / 1.52 | 0.70 / 180 / 1.16 (the animated machinery) | - |
| collision-only view mode | 0 / 0 | 0.01 / 42 / 0.00 | - |
| wireframe view mode | 1.85 / 0 | 1.74 / 161 / 8.25 | - |

Mean signed differences (Vulkan - Direct3D 9) are -0.2 to +0.25 of 255 everywhere: no uniform shift is left.

**Fixed this round**: a material pass without an alpha line (mode 0xC, "blend by the pass before's alpha") was read as
mode 5 after pass 1 - the two write the same `lrp` line there - so the pass took the vertex alpha instead of the pass
before's; a four-pass material in BO drew its wood almost black. Found by skipping the frame's draws one at a time on
Vulkan (draw 106 of 380) and showing each pass's value on both renderers.

**What is left, and why**:
- **Multisampled edges.** With `msaa=0` the pixels over 8 fall from 0.27-0.90% to 0.01-0.62% (BO-far 0.27 -> 0.01,
  z_ho-start 0.30 -> 0.07): most of what is left at `msaa=8` is the edges. Direct3D 9 here covers the samples of an edge
  on whole pixels as if it weren't moved by the half pixel that its pixel centres need (seen exactly on the movie
  quad's edge columns); Vulkan moves the geometry. Matching it would mean rasterising unmoved and interpolating every
  input at (-0.5, -0.5) on multisampled targets (`interpolateAtOffset`, the sampleRateShading feature): not done - the
  difference is a fraction of an edge pixel's coverage. (What forced `msaa=8` in round 12's pair: the workspace's own
  `[Graphics] msaa=8`; this tool writes the key in place, so `--set Graphics.msaa=0` works.)
- **The texture shadow's edge** (sch-court): the inside of the shadow is the same colour (it multiplies by the shadow
  colour; its shape comes from the alpha test on the shadow texture's filtered alpha), but its outline is a little
  smoother and a pixel off on Vulkan - D3D9's is stair-stepped at the shadow texture's texels. The shadow texture's
  sampling or the alpha test's rounding near the threshold; not settled.
- **Wireframe** (a debug view mode): the two APIs rasterise lines differently (Direct3D 9's diamond rule against
  Vulkan's rectangular lines, through 8x MSAA) - some lines one pixel apart or missing.
- **Animation timing**: z_ho's pedestrians and the idle skater are timed by the clock, and Vulkan's frames take longer in
  the Debug build, so with fog on in one pair a pedestrian stood in another pose (1.56% over); the machinery in foun.
- **Loading screens**: the bar's fill is timed, so frames pair up only where both renderers caught the same moment.
- **Fog**: `[Graphics] Fog=1` changed nothing at these spots on either renderer (the levels' fog isn't on there), so the
  fog formulas are still unchecked against Direct3D 9 in the game.

**V3's open questions**: the alpha test (rounded) and the ps.1.x intermediates (unclamped: this NVIDIA driver reports
`PixelShader1xMaxValue` 65504) agree with Direct3D 9 wherever the spots show them (foliage, the materials); the four-pass
material above was the only shader difference found. The non-local viewer, the reflection vector's normalisation,
missing coordinate sets, fog without a specular colour, table fog and the MODULATE*_ADD* alpha forms are not used at any
spot compared (the game never sets table fog, round 2's scan), so they stay as written.

**The unset-stage rule**: no other draw in these captures samples a stage with no texture set; the rule stays as the
loading bar showed it.

**Frame times** (Debug build; the presents timed by a measuring build, ms a frame over the last 300 presents of each
level's run, which includes its captures): Direct3D 9 foun 35.4, z_dt 58.2, BO 26.2, z_ho 51.9, sch 37.5; Vulkan 50.1,
63.0, 33.3, 59.6, 53.0 - Vulkan 10-40% slower in Debug, processor-bound (under 0.2 ms a frame waiting for the device).
The game's own frames run at 60 a second on both (the status file: 17.0-17.2 ms).

**Validation**: the Khronos layer (32-bit) on: core checks in the five levels, the front end and the editor, 0
messages; synchronization checks in foun's tricks run, 9 write-after-write hazards between copies into one static buffer
(in one command buffer and across submissions) - the copies now wait for the ones before them - then 0. Then core and
synchronization checks (submit-time validation on) over the start-up movies, four loads in a row (the loading
screens' timer threads), the level editor in its Unlit view mode, a minimise / restore and full screen with alt-tab, on
the NVIDIA and the AMD GPU: 0 messages. GPU-assisted validation (foun): only its own notes on the features it turned
on. Best practices (with the NVIDIA and AMD checks): performance advice only (dynamic states, barrier and pipeline-bind
counts, memory priorities, fences), and images moved from an undefined layout straight to sampling - textures and
targets sampled before anything wrote them, which Direct3D 9 leaves undefined too.

**Memory** (32-bit, windowed 1280x720): Vulkan 477-500 MB private and 886-916 MB of address space free in the five
levels and the editor; Direct3D 9 237 MB / 1,370 MB in foun. Full screen remains the tightest (V10: 567 MB, 804 MB free).


## Ray tracing: what it needs on top

`VK_KHR_acceleration_structure`, `VK_KHR_ray_query` (hybrid: ray queries from raster fragment shaders are the simplest
first step) or `VK_KHR_ray_tracing_pipeline`, `VK_KHR_buffer_device_address` and descriptor indexing.

| needs | today | what must be added |
|---|---|---|
| **the frame as data**: the instances drawn this frame (which mesh, its world transform, its material, flags), the lights (the world's ambient and directional lights, the 256 dynamic-light slots), the camera | implicit: `draw_world` walks the backend's own lists and draws as it goes | `render_pc` fills a per-frame record as it walks: instances with a stable mesh id, transform and material id; lights; cameras per viewport. API-neutral; the D3D9 path ignores it. `render::Device` doesn't change (the engine already hands over whole scenes, models and geoms) |
| **persistent geometry for acceleration structures** | the sector meshes are made at scene load and kept (good); skinned meshes are rewritten each frame on the CPU; particles, billboards, grass layers, text and 2D are drawn from memory | a BLAS per static mesh at scene load (compacted); skinned / CPU-written meshes refitted or rebuilt per frame from the buffer the CPU skinning writes (later: skinning in a compute shader); particles and billboards left out of the BLAS at first; the TLAS rebuilt per frame from the instance list |
| **materials readable in shaders** | a material is passes with blend modes, textures and UV sets, applied by state blocks and generated pixel shaders | a material table in a GPU buffer (per pass: texture index, UV set, blend mode, alpha flags, colour) and bindless textures (descriptor indexing), so a hit shader can shade any surface |
| **the 64-bit address space** | 32-bit build | acceleration structures, scratch buffers and the bindless tables need address space, and ray-tracing driver support in 32-bit processes is uncertain (to confirm). Ray tracing ships on the x64 build only |
| **a denoiser** | - | for soft shadows / reflections: a choice to make (licences to confirm) |

The first feature proposed is **ray-traced sun / directional-light shadows** as a screen-space mask that modulates
the original's lighting, behind an ini option. It needs the instance list, static BLAS and the camera, but not the
material table (alpha-tested foliage aside). Reflections and ambient occlusion come after the material table.

### The frame record (R1)

The contract is `engine/gfx/rt_scene.h`; `render_pc/rt_record.h` fills it, the same for Direct3D 9 and Vulkan, and does
nothing but test one pointer while no `RayTracing` is installed.

- **When:** `draw_world` gathers the frame's instances and lights before anything is drawn (`record_frame`);
  `draw_viewport` adds each viewport's camera once the world's camera is chosen, and the frame's first calls
  `begin_frame` there - before the world's passes, so a backend builds its TLAS before any draw can trace a ray.
- **Instances** are the scene, not only what passes the view's tests: every mesh of the loaded scenes but the sky's
  (`kWorld`, the identity transform) and every active instance's meshes (`kObject`; per bone where the scene's bones are
  drawn one draw each, the bone times the instance's transform; a one-bone mesh with its bone of the palette before it),
  shown and not hidden. Off-screen geometry is there, so it can cast shadows into the view. Flags come from the
  material's first pass: `kTransparent` (the pass's transparent flag), else `kAlphaTested` (an alpha reference - the
  game's materials nearly all have one of 1), else `kOpaque`; `kCastsShadow` unless transparent; `kSkinned` for meshes
  skinned on the CPU.
- **Meshes** are registered the first frame they appear (their first level of detail: the vertex buffer they were last
  drawn from, its share's offset in an arena, the strip or list, the indices' copy in memory), removed when destroyed;
  `update_mesh` (an addition to the contract) when the buffers they are in change - a device reset forgets them all
  instead, and they come back under new ids; `mesh_changed` the frame after the CPU rewrote their vertices (skinning,
  the editor), colour-only writes not counted. A skinned mesh's geometry is therefore the pose it was drawn with the
  frame before, and one mesh shared by several skinned instances holds the last one's pose.
- **Materials:** `set_material` when first used and when their first pass changes (texture animations): its texture,
  UV set 0, blend mode, alpha reference (0-1), colour (the pass's doubled: 1 is the texture unchanged; the fourth value
  the pass's fixed alpha), flags, and R3's sampler states: the pass's address modes (wrap, clamp, border where the
  device has it) and linear filtering.
- **Lights:** the level's ambient colour, its directional lights (the way the light travels; colour times intensity /
  128) and the dynamic point lights that are on.
- **Checking it:** `[debug] rt_contract_check=1` puts a checking `RayTracing` in front of whatever is installed
  (`devtools/rt_check.h`): every call validated, every mesh drawn in the world's and objects' passes looked up in the
  frame's record with the transform it was drawn with (`wants_draws` / `mesh_drawn`, contract additions), the lights
  compared with the light manager's. `node tools/game/rt_contract.ts run --game C:\Games\THUG2-<n> --build <folder>`
  plays BO (riding), LA, SE, foun and Anteara Mod's BO with each renderer and fails on any violation; the console's
  `rt dump` writes the frame record as JSON (the tool keeps one per renderer).


### The sun and where the shadows fall (R4)

The ray-traced shadows (`gpu_vulkan/vulkan_rt_shadows`) darken a pixel where the sun is blocked. Which sun, and where
the original's own lighting already says otherwise, is decided so:

- **The baked sun** (`engine/gfx/rt_sun`; today one of `rt_sun_source`'s choices - "One sun for every effect"
  below). The frame record's
  directional lights are the light manager's two (toward (0.75, 0.5, 0.43) and (-0.79, 0.5, -0.37)) in every level -
  they light the skater and the objects, not the level - so tracing toward the first one put the shadows where the
  level's bake has none (BO, sch: 65-95% of each picture darkened, the sunlit paving with it). The level's own sun is in
  its world meshes' vertex colours: in a new world's first frame, before it is drawn ("The first frame after a load"
  below), `render_pc/rt_record` samples its
  upward-facing triangles (the colour's luma at each) and `fit_baked_sun` traces every sample toward 240 directions
  over the sky (then 60 within 10 degrees of the best, 40 within 3): the direction whose "not blocked" correlates best
  with the brightness is the sun (BO heading -75 / elevation 52, z_dt 44 / 53, z_ho 45 / 58, sch -45 / 36; about 1.5-2.7
  million rays, 150-370 ms once a level). Its score is the confidence: under 0.3 the bake shows no sun (the Foundry:
  0.07 - indoors, lit by its lamps) and the level gets no shadow pass. (`rt_shadow_sun=light | x,y,z` once chose the
  frame record's first light or a fixed direction; retired with it.) The console's `rt sun <x y z> | off` overrides
  every effect's sun (a check aid).
- **Where the bake agrees** (`rt_shadow_match_bake=1`). With the sun found, about 100,000 points of the upward faces are
  traced toward it once more; over a grid of the level seen from above (as many cells as give each about 8 blocked
  points, at most 64 a side: 150-170 units in the levels tried) each cell gets the share of its blocked points whose
  baked colour is dark - 1: the bake has that shadow, a traced one is welcome; 0: the bake lit it anyway (lamps, a
  roof or a caster it left out) and the traced shadow is dropped there. The pass reads the grid (a storage buffer,
  bilinear) at each pixel's x and z. `rt agree <x> <z>` prints its value.
- **The sky test** (`rt_shadow_covered=0`). A pixel whose sun ray is blocked traces `rt_shadow_sky_rays` (5) more rays
  up - straight up and 31 degrees out round it; if none reaches the sky it is under a roof and keeps the level's own
  light; each one that does lets in a share of the shadow, all of it from half of them on.
- **What it still does:** the traced shadow multiplies the colour where the bake already darkened it (the pass's design
  since R4: the bake's shadows get sharper edges and darker), and the skater and objects receive the world's shadows.
- **Measured** (`tools/game/rt_shadows_check.ts`, its five levels): "the sun against the bake" - the pixels the
  original lit darker (under the `off` picture's Otsu threshold) darkened 0.93 times as often as the brighter ones with
  the frame record's light (no better than chance), 1.5-1.6 times with the baked sun; the Foundry's spots unchanged.

### HDR and tone mapping (`rt_hdr`)

`gpu_vulkan/vulkan_hdr` (`[Graphics] rt_hdr`, off by default; no ray tracing needed). The light the effects add - the
ray-traced passes at `opaque_drawn`, one-bounce light, fog and light shafts at `world_drawn` - must not clip at the 8-bit
targets' white, so with `rt_hdr=1` the world is drawn into 16-bit float targets and mapped back to the display's 0 - 1
by one pass, the last listener at `rt::world_view_drawn` (`ViewOrder::kToneMapping`); the 2D is drawn after, unchanged.

- **Which targets:** the back buffer is made `VK_FORMAT_R16G16B16A16_SFLOAT` (its surface still reports the settings'
  8-bit format to the game); render_pc asks `rt::world_target_format` for the targets the world is drawn into instead of
  the back buffer - the two full-size screen copies (the bloom's and the motion blur's), `[Graphics] msaa`'s scene
  surface and the game view's picture (the editor's Scene panel) - and gets `kFormatA16B16G16R16F` (D3DFMT 113) or our
  `kFormatX16B16G16R16F` (read with alpha 1, as X8R8G8B8). `rt::hdr_colour()` says whether it is on (set when the device
  is made). The quarter-size and smaller screen copies, textures, the UI's previews stay 8-bit: they hold what is
  already mapped, or nothing of the world.
- **Values:** the game's own colours, display-referred (not linear): 1 is the original's white, as before. Draws are no
  longer clipped at 1 (additive glows, doubled vertex colours keep their range). Values above 1 are welcome in the
  colour target bound at `opaque_drawn` and `world_drawn` when `rt::hdr_colour()` is true (GI, fog: add light there, in
  this same space - or linearise with gamma 2.2 for the maths and go back); with it false the target is 8-bit and
  everything is clipped at 1 per draw as the original's. Negative values and NaN are read as 0 by the pass.
- **The pass:** the bound target's viewport copied out (`vkCmdCopyImage`, any sample count), then a full-screen
  triangle writes `rgb` back (alpha kept): exposure (`rt_exposure` stops of linear light: a factor 2^(stops/2.2) on
  these values), saturation (Rec. 709 luma), contrast (a power about 0.5), the curve - `rt_tonemap=0`: unchanged up to a
  knee, then 1 - d exp(-(c - k)/d), the knee chosen so that `rt_white_point` (default 1.25: knee ~0.911, the original's
  white shown at ~247) is within half a step of white, `rt_white_point=1` an exact clip; `rt_tonemap=1`: Narkowicz's
  ACES fit on the linearised colour, scaled so the white point is white - then an optional `.cube` 3D LUT
  (`rt_grade_lut`, trilinear, from a storage buffer) and a clamp to 0 - 1. A multisampled target: each sample mapped,
  their mean written to every sample (no sample-rate shading needed). A view drawn into an 8-bit target isn't mapped.
- **What else knows:** the present blits the float back buffer into the swapchain (the conversion clamps); the gamma
  pass works in the frame's format; `read_front_buffer` and the screenshot's `read_render_target` convert to B8G8R8A8
  (`read_hdr_as_bgra8`: clamped, rounded); `copy_surface` of a multisampled float target into an 8-bit one resolves in its
  own format first, then blits (`resolve_for_copy`).
- **Checked:** `thug2_vulkan_hdr_check` (80 checks, both GPUs here, validation and synchronization validation 0): the
  pass against `tone_map_reference` (its processor twin) on every 16-bit float 0 - 16, random ones up to 8, NaN, both
  infinities, negatives and denormals under eight settings (defaults, a clip, exposure +2 / -1.5, ACES, saturation and
  contrast, white point 4, a 17^3 LUT) - within 1/255 (the largest difference 0.12 of a step: the target's rounding),
  alpha untouched; every value up to the knee unchanged bit for bit; only the viewport changed; 8-bit targets and the
  setting off: no pass; four samples: values above 1 through the resolve, each pixel the mean of its mapped samples (from
  its coverage), the copy into an 8-bit target within a step; the back buffer: float, reported X8R8G8B8, its shot and
  front-buffer reads the mapped bytes. The pass costs 0.04 ms at 1920 x 1080 (0.24 ms with four samples) on the
  RTX 5070 Ti, 1.1 ms (6.4 ms) on the integrated Radeon. In the game: `tools/game/rt_shadows_check.ts --effect hdr`.
- **Not done:** auto-exposure; HDR output to an HDR display (the swapchain stays 8-bit sRGB-less UNORM).

### Volumetric fog and light shafts

`gpu_vulkan/vulkan_rt_fog` (`[Graphics] rt_fog`, off by default; `rt_fog_mode`, `rt_fog_density`, `rt_fog_height`,
`rt_fog_anisotropy`, `rt_fog_steps`) listens at `rt::world_view_drawn` (`ViewOrder::kFog`, 500: after the transparent
passes, particles and weather, before tone mapping and the 2D). `rt::ViewDrawn::fog` (added to the contract) carries
the level's fog as `render_pc` told the device - the game draws linear range fog, f = (end - d) / (end - start) over
the distance from the eye, its end 7200 past its start with distance clipping (pc_fog.cpp).

- **The medium.** The game's fog read as a medium whose extinction is 1 / (end - d) between start and end: its
  transmittance is exactly the game's f (to the power `rt_fog_density`), integrated per segment in closed form, so the
  step count changes only how the light is sampled, never how thick the fog is. `rt_fog_height` H thins it by
  exp(-(y - camera y) / H) (0: even).
- **The light.** Each segment's light is sampled at one jittered point (interleaved gradient noise + the frame's
  golden-ratio offset): a ray query toward the level's **baked sun** (`rt_sun`, as R4 uses it; the console's
  `rt sun` overrides it), masks 0x01 + 0x02. L = fog colour x (0.5 + 0.5 x lit x phase), phase Henyey-Greenstein
  (`rt_fog_anisotropy`) x 4 pi - 1 on average, so an unshadowed fog keeps the game's look; shaded fog is half as
  bright, fog looking into the sun brighter. No colour for the sun (the bake has none): the fog's colour keeps the hue.
  No sun (the fit not run yet, or a level whose bake shows none - the Foundry, indoors): L = the fog colour - the
  game's fog exactly, no shafts. (Windows into a sunless interior would need a light the level doesn't have.)
- **The passes.** Half resolution (each texel marches one pixel of its 2 x 2 block, a different one each frame), a
  temporal pass (the texel's point reprojected through the viewport's previous camera, bilinear over the history
  texels whose distance agrees, clamped to this frame's 3 x 3 neighbourhood, up to 12 frames), then a full-screen
  blend colour x T + light, upsampled with weights that fall off with the distance difference (depth-aware). Per
  viewport histories (split screen), up to 4.
- **Modes.** `rt_fog_mode=0` (the default) replaces the level's fog: the Vulkan Gpu's fixed-function emulation leaves
  the device's fog out of every draw while the pass draws (`rt_fog_replaces_level_fog`), and the pass draws it -
  the same colour and distances. Its limit: transparent passes and particles don't write depth, so they get the fog
  of the surface behind them. `rt_fog_mode=1` keeps the game's fog as drawn and adds only the sunlit part
  (x 0.5): shafts over the original, nothing taken away.
- **The sky** (the depth's far value: the sky dome, or the fog-coloured clear with distance clipping) is never dimmed;
  it gets the sunlit light only, x 0.1 - the shafts against the sky.
- **Which fog.** THUG2's levels draw fog only where a script turns it on, or with `[Graphics] DistanceClipping=1` and
  `Fog=1` - then every mesh fogs over the last quarter before the far plane (pc_mesh_render.cpp), which `ViewDrawn::fog`
  reports. None of the check's five levels has fog by default. So a level without fog gets, with `rt_fog_haze=1` (the
  default) and a baked sun, a thin haze in shafts mode: light grey, a linear "fog" from the camera to 4000 units whose
  sunlit light alone is added - shafts, a glow toward the sun, nothing dimmed. `rt_fog_haze=0` leaves such levels as
  they are.
- **Cost** (device timestamps around the passes, logged every 240 as "rt_fog: the passes take"): the check's scene at
  1920 x 1080 - RTX 5070 Ti 0.48 / 0.99 / 2.03 ms at 16 / 32 / 64 steps, the AMD Radeon integrated GPU 14 / 28 / 52 ms
  (the ray queries dominate: linear in the steps). In the game: see below.
- **Checks.** `thug2_vulkan_rt_fog_check` (65 checks, both GPUs, validation with synchronization 0): a wall with a
  window, the sun through it into an even fog - every low-resolution texel's distance, its transmittance against
  exp(-integral) = (end - d) / (end - start) (largest error 5e-4, 16-bit floats) and its light against the
  processor's march of the same pixel with the same jitter (98% within 2e-4); the shaft's texels lighter than the
  others by what the exact integral says (0.065 against 0.073); after 40 still frames every history full and within
  0.001 mean of the exact integral (2048 steps); the composite = surface x T + light (mean 0.8 levels); a moved
  camera's histories kept (15k) or restarted (0.4k) with no ghost; shafts mode leaving pixels in no sunlit fog
  unchanged; the sky never dimmed; no pass without fog, a scene or the setting. `--perf` times the passes.
  `tools/game/rt_shadows_check.ts --effect fog` checks it in the game.

### One-bounce indirect light (`rt_gi`)

`gpu_vulkan/vulkan_rt_effects` (`[Graphics] rt_gi`, `rt_gi_strength` / `_rays` / `_distance` / `_denoise`, off by
default) - the occlusion's passes and denoiser run a second time over a colour (`rt::ViewOrder::kIndirect`, after the
shadows and the occlusion):

- **The rays:** per pixel `rt_gi_rays` cosine-weighted ray queries (masks 0x01 + 0x02 + 0x04, alpha tested) up to
  `rt_gi_distance`, the occlusion's directions; a hit brings its albedo x the sun's light x N.L where a ray toward the
  sun (0x01 + 0x02) is clear; a miss nothing. The hit's albedo: R3's material at the texture coordinates R2's geometry
  table gives, at a level for the ray's spread, x the material's colour, at most 1 - the vertex colours (the bake) left
  out. The sun: the console's `rt sun`, else the level's baked sun (above), white; none where the bake shows none.
- **Why the sun only:** THUG2's vertex colours already hold the artists' sky and ambient light; what they lack is light
  bounced off sunlit surfaces. A first version also lit the hits by the frame record's ambient and let misses bring it
  as the sky, subtracting the sky afterwards: the ambient is the skater's (the same in every level), and BO's shaded
  courtyard got no bounce at all while doubled material colours brightened the Foundry. The shader keeps both terms
  (and the apply a sky to subtract), all 0.
- **The pixel's own albedo** (what the bounce lights): a ray from the camera finds the pixel's triangle (kept where it
  ends at the depth's point, as the reflections do) and reads its material the same way at the pixel's footprint. The
  filters run on the light alone (E, demodulated): the texture's detail isn't blurred.
- **Adding it to a baked frame:** the frame colour is albedo x baked light, and neither is known apart. The pass adds
  strength x albedo x E with the blend's `ONE_MINUS_DST_COLOR, ONE` - a screen: what is added is scaled by what the
  colour leaves below white, so a lit pixel gains little and none passes white, a dark one gains most; nothing is ever
  darkened. A float target (HDR) gets `ONE, ONE`: values above 1 kept.
- **Denoiser:** R5's temporal and a-trous shaders compiled with `VALUE_RGB` (the value a colour, the history's length in
  alpha; the noise floors scaled by the most one ray brings) - the occlusion's are the same shaders as before, its check
  unchanged (38/38).
- **Checked:** `thug2_vulkan_rt_gi_check` - a white floor, a red wall, a grey pillar, the sun from the pillar's side:
  the raw light at ~39.7k clear-cut pixels exactly the processor's trace of the same rays (one sample and four), the
  albedo found, the colour formula at every pixel, a reddish bounce beside the wall, an open floor untouched, the
  denoised light against a 256-ray reference (mean 0.007, 99th percentile 0.023), NVIDIA and AMD, validation and
  synchronization validation 0. In the game `rt_shadows_check.ts --effect gi` (five levels): off identical to main's
  build (at most 0.06%, the noise); on: nothing darker or black, no runaway, the Foundry (no baked sun) unchanged,
  1.7-30% of a picture brightened, the baked-dark pixels brightened 1.7 times as often as the baked-bright ones.
- **Cost** (`rt_gi: <x> ms a pass on the device`, timestamps around all its passes; 1920x1080, z_dt's street, RTX 5070 Ti,
  denoised): 1 ray 1.45-1.49 ms, 2 rays (the default) 1.89-2.07 ms, 4 rays 2.44 ms - the camera ray, the albedo and the
  denoiser are about 1 ms of it. Not measured on the AMD (integrated) device in the game.

### The sun, its glare and the sky (`rt_sun`, `rt_shafts`, `rt_bloom`, `rt_sky_model`)

What read as dated beside the ray-traced light was mostly the original's own sun: a white halo sprite and a corona at
`sun::render`'s place (engine/gfx/lens_flare: theta / phi from SetSun, drawn after the particles) and the lens flare's
screen elements over the picture; and the fog's haze was a flat grey veil with no sun colour (the bake gives none) that
only ever added light. Four settings, all off by default, change that; each works the same with `rt_hdr=0` and
`rt_hdr=1` (rt_hdr is only the internal range: the output is 8-bit either way).

- **The sky model** (`engine/gfx/rt_sky`, `gpu_vulkan/vulkan_rt_sky`). Single scattering in a spherical atmosphere:
  Rayleigh air (5.8 / 13.6 / 33.1 x 10^-6 per metre, scale height 8 km), Mie haze (8 x 10^-6, 1.2 km, Cornette-Shanks
  g 0.8), the viewer 100 m up. From the baked sun's elevation it gives the sun's transmittance - its colour relative to
  the sun overhead, as the game's display-referred colours (^1/2.2, the brightest channel 1): (1, 0.61, 0.18) at 2
  degrees, (1, 0.88, 0.64) at 10, (1, 0.95, 0.83) at 20, (1, 0.99, 0.96) at 45 - and the sky's radiance in any
  direction, exposed so the zenith with the sun 45 degrees up has luminance 0.3 (linear). The device keeps a table of
  the sky (64 azimuths from the sun's x 32 elevations, finer near the horizon - 32 segments per ray, 16 toward the sun -
  + a row: the transmittance, the colour, the zenith), made again only when the sun moves. `rt_sun_colour=r,g,b`
  overrides the colour. `rt::set_sun_light` (a contract addition) publishes it while `rt_sky_model=1` or a colour is
  set: the one-bounce light (`rt_gi`) tints its sun by it (its luma kept - nothing else about it changes); the shadows
  don't use it (they darken the bake, which has no colour to replace). It is published by the fog's or the sun's pass,
  so the bounce takes the colour from the frame after one of them runs.
- **Aerial perspective** (`vulkan_rt_fog`, `rt_sky_model=1`, a level without fog, `rt_fog_haze=1`). The haze becomes a
  medium of its own: extinction `kFogAerialExtinction` (1/12000 a unit) x `rt_fog_haze_density` x `rt_fog_density` at
  the level's ground, thinning by e every `rt_fog_haze_height` (600) above it and as thick as there below it - the
  ground is the 10th percentile of the bake samples' heights (`rt::BakedSun::ground`, a contract addition; the camera's
  height when unknown; logged with the first pass) - integrated in closed form along each segment, followed to 40000
  units. Its light at a sample: sky x (0.45 + 0.55 x lit), with sky the table's colour toward the ray (no lower than
  0.03 rad above the horizon) and lit the same ray query toward the sun as before: a long unshaded path ends as the
  sky's colour - the far world fades into the sky, never brighter or whiter than it (the glow toward the sun is the
  table's own aureole); shaded haze is the sky's share alone, so the shadows of buildings show as beams. Composited as
  the replaced fog (colour x T + light). The sky's pixels are hazed too, as a surface at the far plane along the ray
  (or 40000, the nearer): the sky dome paints the world past the meshes - AU's open sea, hills - and a mesh that reaches
  that far meets it with the same haze. (Fixed after the user's AU screenshot: the first version left the sky
  undimmed and lit the haze with sky x 0.45 + the sun's colour x a phase - up to ~1.2 x the sky toward the sun -, so
  AU's far sea, cut where its mesh ends, went flat white with stepped edges against the dome's dark painted sea.) A
  level with its own (scripted or distance) fog keeps its look: only its sunlight takes the sun's colour (luma kept).
- **Which sun is drawn.** The sun the player sees is the game's own: render_pc's `draw_viewport` tells rt_scene where
  `sun::render` would put its sprite every viewport (`rt::set_game_sun`, a contract addition - render's theta / phi
  from the level's SetSun / InitSun), and the disc, its glare and the rays are drawn there (`rt_sun_place=game`, the
  default then). It needn't be the baked sun the level was lit with: z_dt's and BO's game sun is 20 degrees up toward
  +z, their baked ones 53 and 52 degrees up elsewhere - the original game had the same split (its sprite against its
  bake). (Changed by "One sun for every effect" below: the disc is drawn at the scene's sun - the one the shadows, the
  bounce, the fog's shafts and the sky table light by -, the game's shown sun only where there is none; `rt_sun_place`
  is retired.) The disc's colour is the model's for the drawn sun's elevation (or `rt_sun_colour`).
- **The pass** (`gpu_vulkan/vulkan_rt_sun`, a listener at `world_drawn`, `rt::ViewOrder::kSun` = 600 - after the fog,
  before tone mapping): the viewport's colour copied out; (1) the rays (`rt_shafts`, half resolution): from each texel
  toward the sun's vanishing point on the screen, 48 points along the whole way (the start jittered by interleaved
  gradient noise), each a source where the depth is the sky's, exp(-angle to the sun / 0.12) bright, weighted 0.975^i,
  averaged; x `rt_shaft_strength` (0.8), the sun's colour, a fade - none with the sun behind the camera, fading out as its
  place leaves the screen by half the screen's size; (2) the source (full resolution, two 16-bit float images - so the
  sun's light keeps its range with an 8-bit target): the frame's colour (a multisampled target's mean) and the light
  added - on the sky's pixels (a multisampled pixel's share of sky samples) the disc (`rt_sun`: radius 0.0093 rad x
  `rt_sun_size` - twice the real sun's -, 24 x its colour, limb-darkened, its edge one pixel soft) and its aureole
  (0.5 e^(-angle/0.04) + 0.15 e^(-angle/0.25)), and the rays upsampled; (3) the bloom: X = 0.25 x the added light +
  `rt_bloom_strength` (0.05) x the frame's colour with `rt_bloom` - the frame's part left out while the game's own bloom
  is on (`rt::ViewDrawn::game_bloom`, a contract addition: z_dt and BO turn it on), so the two don't double up - down a
  chain of half-size images (Jimenez's 13 taps, weights summing to 1) to 7 levels or a shorter side of 8, then up (a
  3 x 3 tent, each level's own added at weight 1/levels): B, a blur of X holding the same light; (4) the composite (a
  full-screen pass, alpha kept): frame + light + B - X - light moved, never made. An 8-bit target clips the result as
  the game's draws do; with `rt_hdr=1` the tone mapping maps it. `rt_lens_dirt=1` adds faint procedural spots lit by B
  (light added: off by default, and off in the energy checks).
- **The original's sun.** While the pass draws a sun (`rt_sun=1` and a sun to draw), `rt::set_sun_glare_replaced`
  tells `draw_viewport` to skip `sun::render` - no sprite, no corona, no occlusion query - and
  `sun::hide_for_replacement` fades the lens flare's container out (as the original does when the sun is behind the
  camera); "sun: the original's sun and lens flare left out" is logged. With no sun at all the original's is kept.
- **Cost** (device timestamps between the parts, logged every 240 as "rt_sun: the passes take"): the check's scene at
  1920 x 1080, everything on (`--perf`): RTX 5070 Ti 0.22 ms - rays 0.11, source 0.02, bloom 0.07, composite 0.03; the
  AMD Radeon integrated GPU 10.3 ms - rays 6.2, source 1.0, bloom 1.6, composite 1.5 (`rt_shafts=0` there). In the game
  at 1280 x 720 with 8x MSAA (z_dt, BO; NVIDIA): 0.18 - 0.25 ms with rt_hdr=0, 0.32 - 0.40 with rt_hdr=1 and every
  other effect on. The sky table is remade only when the sun moves (a 64 x 33 dispatch).
- **Checked:** `thug2_vulkan_rt_sun_check` (76 checks, the RTX 5070 Ti and the AMD Radeon, validation and
  synchronization validation 0): the device's sky table against rt_sky.h's integrals (256 / 128 segments) at 2, 5, 10,
  20, 45 and 80 degrees - 0.1-0.4% mean, 0.4-1.7% at the 99th percentile -, the transmittance and colour within 0.002
  of the 1024 / 512-segment reference, the colour warmer the lower it is, the zenith bluer than the horizon; the sun's
  place on the screen against the camera's lens (exact), a pixel's angle, the light added at ~75,000 clear-cut pixels
  against the formula (worst 0.13% relative, none on surfaces), the disc's centre white (8-bit) / above 20 (float), no
  pass and the original's sun kept with no sun, still left out with the sun behind the camera; every ray texel against
  the processor's march over its own ray-cast sky mask (97th percentile 2e-4), the wall beside the window lit by the rays
  5 times as much as far from it; the bloom: X (with the disc at 20 x white) through the first level and the chain's top
  within 0.3%, the composite into a float target keeping the picture's sum (0.6787 -> 0.6782) with the disc kept above
  1, into an 8-bit target clipped at 1, the frame's own bloom alone keeping an 8-bit frame's mean to 0.00 levels; the
  aerial haze's transmittance at ~14,000 texels against the closed form of its height profile (within 5e-4) and its
  light at least the sky's share of the model's sky colour and at most that colour; ~5,300 sky texels hazed as a surface
  at the far plane (within 5e-4); everything off: no pass, the picture
  unchanged. The other checks (rt 20, shadows 45, effects 38, reflections 39, materials 34, fog 71, gi 29, hdr 80) pass
  unchanged on both GPUs. In the game, `tools/game/rt_shadows_check.ts --effect sun` (z_dt and BO, five spots, two runs,
  main's build as the baseline): `off` against main's build 0.00-0.01% changed; `on` bounded (mean luma change 0.7-25,
  nothing black, at most 3.8% white - the disc and its glare with the sun in open sky).

### Water, glass and the skater's shadow (`rt_reflection_fresnel`, `rt_glass_roughness`, the haze, `rt_shadow_objects`)

After the user's screenshots - Australia's sea "too bright", Barcelona's harbour milky and whited out looking into the
sun, Australia's glass building "a bit too much like mirrors", the skater's blocky shadow beside the ray-traced ones -
each effect was switched off in turn at the same spots (`tools/game/rt_shadows.json` AU-water, BA;
`rt_shadows_check.ts`): the reflections changed AU's and BA's water by nothing (their sea materials have no
environment pass - they aren't reflective to R5 - and BA's water is transparent, which the pass never sees); the sky
model's aerial haze made both seas milky and the harbour white (with `rt_sky_model=0` the sea is the original's deep
blue); the glass was the reflections'.

- **The haze** (`vulkan_rt_fog`): `rt_fog_haze_density` defaults to 0.3 (was 1): an extinction of 1/40000 a unit at the
  level's ground - the near sea keeps its own colour, the horizon still softens, and AU's sea still meets its sky
  dome's painted sea without a band (both hazed to the same distance). Its light toward a low sun is capped: no
  brighter than the model's sky at the same height a quarter turn from the sun, x (1 + `kFogAerialGlow`, 0.35) - the
  table's aureole lit all the haze toward the sun white. (The sun check's aerial reference applies the same cap.)
- **Which reflective surfaces are what** (`render_pc/rt_record.cpp` classify_reflective, logged per material with
  `[debug] rt_reflection_materials=1`): `rt::Material::reflection_class` - water: a pass with a UV wibble or a texture
  animation (moving water textures); glass: the environment pass lerped in by the first pass's alpha (blend modes 12 /
  13 - the reflection painted into the clear parts of a texture: the windows in AU's and BA's wall textures, e.g.
  8A6663D9 on the user's building), or under a later pass blended over it by its alpha (a wall texture whose clear
  texels are its windows, the environment map drawn first: BO's brick buildings), or a first pass that is seen through
  (transparent, alpha or fixed blend); shiny: the rest (an opaque surface with its reflection added or blended over:
  metal, paint, polished floors). And `reflection_mask`: modes 13 / 12 - the environment lerped in by the pass before's
  alpha, the glass's own colour put back (below). Both are in R3's material record (56 bytes a row then; 100 now, with the surface maps' `surface_texture` and `f0` - "One F0" below - and the mask passes' nine words).
- **Where a surface reflects: its environment pass's share** (`environment_share` in rt_record.cpp; the user's Boston
  street, `rt_shadows.json` BO-facade: a whole brick facade turned mirror-grey, not just its windows). The game's
  generated pixel shader (`render_pc/pixel_shaders.cpp`) puts the first pass in r0 and blends each later pass into it by
  its mode, a pass's alpha being its texel's (pass flag 0x1000) or the vertex alpha x the texel's, doubled. So the
  environment colour E reaches the screen scaled by its own mode (after the first pass: 1 add, 5 blend, 2 / 6 by the
  fixed alpha, 12 / 13 by the pass before's alpha; 3 / 4 subtract it, 7 - 10 multiply or brighten by its alpha alone,
  0 and 11 do nothing - those draw no environment colour: not reflective) and then by every later pass's: 5 x (1 - a)
  (the wall drawn over its windows), 6 x (1 - fixed alpha), 7 x a, 8 x fixed alpha, 12 / 13 x (1 - a) / a of the pass
  before; adds, subtracts and brightens leave it. The constant factors go into the reflectivity, the per-pixel ones
  into `rt::Material::mask_passes` (up to three: a pass's texture, its texture coordinate set - pass n reads set n,
  `rt::Geometry::uv_sets` -, addressing, kMaskClear 1 - a / kMaskOpaque a, whether the vertex alpha counts); R3's record
  carries each one's texture slot, sampler and mode (100 bytes a row with the surface maps' two fields before them), and the reflection pass multiplies the weight by
  every factor at the pixel (each texture at the level of its own coordinates' footprint; Fresnel on or off). BO's
  facades are an environment pass first (a 32 x 32 sky picture) under the wall texture by mode 5: before, the
  reflectivity of the environment pass covered the whole wall; now only where the wall texture is clear.
- **Fresnel** (`rt_reflection_fresnel=1`, the default; 0 the first version's look): the pixel's weight from Schlick's
  curve at the cosine c between the view and the triangle's normal - water F0 0.02, glass `rt_glass_reflectance` (0.2; real glass's 0.04 left AU's panes - bright textures, not dark rooms - with a mean change of 1.4 levels against the first version's 12: no reflection to see): weight = reflectivity x
  min(1, 2 x strength) x (F0 + (1 - F0)(1 - c)^5) (the default strength 0.5 is the physical amount); shiny surfaces
  keep reflectivity x strength face-on and rise along the same curve to reflectivity x min(1, 2 x strength) at grazing
  angles. x the mask at the pixel (the mask passes' factors, above). Schlick's curve is within
  0.06 of the exact reflectance of water (n 1.333) and glass (n 1.5) at every angle (the check).
- **Masked glass's own colour.** Where the game lerped its environment map in by the texture's alpha it drew the sky
  picture *in place of* the texture - there is no glass colour in the frame to show face-on. The pass puts it back: the
  texel x the pass's colour x the vertex colour doubled (objects: the frame's lights) - what the game's shader would have
  drawn without the environment pass - into a second image (the base layer, premultiplied by mask x (1 - F)), and the
  apply blends `ONE, ONE_MINUS_SRC_ALPHA`: colour x (1 - mask(1 - F) - mask F h) + mask(1 - F) x own + mask F h x
  reflection (h: the share of the rays that hit something). Face-on a pane is mostly its own colour, at grazing angles
  the reflection; where the rays miss, F of the game's environment map stays (the sky it painted). Other classes keep
  the first version's blend (colour toward the reflection by the weight; the environment map under it).
- **Rough glass** (`rt_glass_roughness`, 0.05 radians; 0: a mirror): glass traces 4 rays over a cone round the mirror
  direction (a golden-angle disc, turned by a quarter of its step in each pixel of a 2 x 2 block), averages their hits
  and stores its weight negative; the apply blurs such pixels with their rough neighbours (a 3 x 3 tent by weight) - 16
  directions seen per neighbourhood, no noise, no history. Water and shiny surfaces stay one deterministic mirror ray.
- **The skater's shadow** (`rt_shadow_objects`, now 1 by default): the skater, peds and vehicles cast ray-traced shadows,
  and while a shadow pass casts them `rt::set_object_shadows_replaced` (a contract addition) tells the shadow component
  (`engine/objects/shadow_component.cpp` refresh_shadow) to hide each object's own shadow - the detailed one (the model
  in a 256 x 256 texture projected on the ground - the blocky one), the simple blob, the volume. A view with no pass (no
  baked sun yet, a level whose bake shows none - the Foundry -, `rt_shadows=0`, objects off) gives them back. Objects
  receive only the world's shadows: a pixel whose surface is an object (a ray from the camera against the objects alone
  ends at the depth) traces without the objects' mask - the low-poly body traced against itself drew blotches.
- **No faceted shading on characters** (the user's NJ screenshot: dark triangles over the skater's shirt and arms).
  Switched off one at a time at that spot (`tools/game/rt_shadows.json` nj-skater): only `rt_shadows` drew them, and
  only for a sun the skater's back faced away from. The pass shaded a pixel facing away from the light as in shadow by
  the normal rebuilt from the depth - a triangle's, flat - so the skater's smooth body went dark triangle by triangle.
  An object's pixel now skips that test (the game's own lighting already darkens its far side) and takes the world's
  shadows only; the occlusion's and bounce light's rays likewise leave out the objects from an object's own pixels.
  The skater's shadow itself was checked against his pose: a grid of `rtprobe` rays along the sun through him gives his
  whole silhouette (head, arms, the board held across), a sun from the side casts it whole on the road, and while
  riding (push, ollie) the shadow follows each pose. `update_mesh` with the same triangles in another buffer (a skinned
  mesh's buffers taking turns) now refits as `mesh_changed` does - before, it kept the pose the structure was built
  with (`thug2_vulkan_rt_check`: a mesh's two buffers taking turns, update_mesh only). `[debug] rt_objects=1` logs each
  object mesh's flags as ray tracing sees them (the skater's six alpha-blended parts cast no shadow, as before).
- **No faceted occlusion on characters** (the user's AU screenshot: the skater in the shade beside a wall, his shirt,
  face and arms in hard light and dark triangles). Switched off one at a time at that spot with the user's settings:
  only `rt_ao` drew them (`rt_gi` the same, fainter; shadows, the relit mode, per-pixel lighting and Ray Reconstruction
  not). Leaving the objects out of an object pixel's rays wasn't enough: the rays' hemisphere was still turned by the
  depth's normal - its triangle's, flat - so the ground and the wall took a different share of each triangle. The
  occlusion's and the bounce's rays now leave an object's pixel around the normal the game lights it with: the camera
  ray that finds the object (`rays_mask`) reads its triangle's vertex normals from the geometry table, blended at the
  hit and taken to the world by the inverse transpose of the instance's matrix (turned to the side the depth's normal
  faces). The table gained them: `rt::Geometry::normal_offset` / `normal_format` (`kNormalFloat3`; render_pc from the
  vertex format - the mesh's own `normal_offset()` is not where the drawn vertex has it: the skater's skinned meshes,
  format 152, give 24 there, their colour's place, with the normal at 12), the record's `normal_offset` and
  `kGeometryNormal`, `hit_normal()` in its GLSL (the record 52 bytes). The occlusion binds the table too (binding 7) -
  the TLAS's own when it traces the scene builder's, else one record with nothing live (a caller's own TLAS: the
  depth's normals, as before). The world's pixels are untouched; the guide's normal (the denoiser's) is the smooth one on
  objects. `thug2_vulkan_rt_gi_check` (39 checks, both GPUs, validation 0): a 12 x 8 sphere with its vertex normals as
  an object beside the red wall, both effects' raw rays averaged over 48 frames x 16 rays - across its triangles' edges
  the occlusion steps 0.001 beyond the steps within a triangle (at most 0.006), the bounce 0.0003; the same sphere
  without normals 0.021 and 0.0086 (the measure sees the facets: at least 0.012).
- **Checked:** `thug2_vulkan_rt_reflections_check` (115 checks): Schlick against the exact Fresnel; the mirror as water,
  glass (four samples), glass at real glass's 0.04, shiny and both masks, and the floor under a cover pass (stripes of
  clear and opaque texels through its second texture coordinate set, with Fresnel and without: the opaque stripes
  exactly as without the pass) - every clear-cut reflected pixel's colour and the pass's own weight (its
  image's alpha, within 1.5e-3) against the processor's for the angle it is seen at, the masked glass's colour put back
  (hits and misses); rough glass against the same glass as a mirror (the light added 0.96 - 0.98 of it, softer, no pixel
  black, no NaN, every weight at most the mirror's); the first version's cases with Fresnel off unchanged. The shadow
  check (45), the sun check (aerial haze with the cap) and the others pass on both GPUs, validation with synchronization
  0. In the game: `rt_shadows_check.ts --effect reflections --groups AU-water,BA` (the user's three views and AU-sea, the
  user's settings with `--set`).

### The sky: the atmosphere and volumetric clouds (`rt_sky`, `rt_clouds`)

THUG2's sky is a textured dome per level - the `<level>_Sky` scenes (`is_sky`: a scene whose name holds "_sky"), drawn
by `draw_viewport` after the world's opaque passes from a camera at the origin with an infinite far plane, depth test on
and no depth writes, so it fills only the pixels the world left at the depth's far value. What the skies hold (the
fifteen main levels' scenes read offline): an opaque dome of 4-5 photographic textures (BO, CA, SE, TR share one; AU's
paints its sea below the horizon, BE's adds an opaque brown ring at -5 degrees), and in seven of them transparent layers
drawn after it: AP, DJ, NO, PH and SC put cloud cards (alpha-blended, draw order 1200-1210) 40-65 km out over a plain
grey dome; LA adds a sun glow (additive), a smog band and haze strips just above the horizon. No sky scene holds
mountains or skyline geometry - the distant hills, cities and SE2's storm overhead are the levels' own meshes (they keep
their depth and stay in front of any sky).

`[Graphics] rt_sky=1` (Vulkan, no ray tracing needed; off by default - `rt_sky=0` draws exactly what main draws):

- **The split** (contract additions in `rt_scene.h`: `SkyReplacement`, `ViewListener::sky_drawn`, `sky_view_drawn`,
  `ViewOrder::kSky`). `draw_viewport` draws the sky scene's opaque meshes (the backdrop), tells the listeners, then its
  transparent meshes only with `keep_layers` - true without clouds (LA keeps its smog bands, DJ its cloud cards over the
  new atmosphere), false with `rt_clouds=1` (the volumetric clouds replace the painted cards; a double layer of clouds
  read wrong). The listener draws over every pixel still at the far plane - the backdrop with it.
- **The level's own sky** (`SkyCapture`, `rt_sky_match.h`). Once per sky scene `render_pc` draws all its meshes from the
  origin into six 90-degree faces (a 384 x 256 texture, cleared to the background colour) and hands it over; the pass
  reads it back once and analyses it: per band of elevation - just below the horizon (-8 to -0.5 degrees: a painted sea,
  the far ground - logged, never matched), the horizon (0.5 - 10), half way (15 - 45), overhead (55 - 90) - the clear sky's colour (texels weighted
  by 1 - cloudiness, cloudiness from saturation: 0.32 and more clear, 0.10 and less cloud) and everything's mean; the
  clouds' share between 3 and 35 degrees up (the sky a player sees) and their colour. Logged: "rt_sky: the level's sky".
- **The atmosphere** (`rt_sky.h`, `vulkan_rt_sky`): the existing model - Rayleigh, Mie, the same radii, scale heights and
  phases, so the sun's colour, the fog's haze and the drawn sky are one model - with the light scattered more than once
  added after Hillaire (EGSR 2020): Psi(altitude, sun) = L2 / (1 - f) from 8 x 8 directions round each point (the second
  order with an even phase and the ground's light, albedo 0.3), added as scattering x Psi along each view ray. Three
  tables, 16-bit floats: the transmittance (256 x 64, Bruneton's mapping, made once), Psi (32 x 32, once), and the sky
  from the viewer (192 x 108: across sqrt(azimuth / pi) from the sun, down the elevation's signed square root - fine near
  the sun and the horizon, below it too), made again when the sun or the match changes. Exposure as before (the zenith
  0.3 with the sun 45 degrees up): the multiple scattering lifts the sky 1.15 x (sun 10 degrees up) to 1.5 x (45).
- **The match** (`rt_sky_match`, 0.8 by default). Gains per channel for the horizon, half way and overhead bands -
  the level's colour (its clear sky with clouds on, everything's without: an overcast level stays grey) over the model's
  band mean, linear, clamped to 0.05 - 20, to the power `rt_sky_match` - interpolated between the bands' centres (4, 30,
  72 degrees). Below the horizon the dome is not asked (its painted ground - BE's brown ring, BO's khaki far ground -
  came back as a warm band right under the horizon): the sky-view table shows the horizon's haze there (the sky 0.6
  degrees up in the same azimuth) giving way to the model's own far ground by up to 60% over 20 degrees down - the far
  world's aerial haze, which reads this table, and the sky meet without a band. The sky keeps the model's shape -
  brighter toward the sun, a gradient to the horizon - with each level's colours. Logged: "rt_sky: matched - gains ...".
- **The sun** it is drawn for: the one the player sees (`visible_sun`: the game's SetSun, the baked sun without one; a
  fixed 50 degrees up when neither exists - logged). The fog's table follows the drawn sky (`set_table_follows_sky_view`:
  its rows read from the sky-view table, the drawn sun in the last row's entry 3 - `sky_display` measures azimuths from
  it), so the aerial haze fades the far world - and, since the haze fix, the sky's own pixels at the far plane - into
  this sky's colour: AU's sea meets the horizon without a band. Its last row (the lighting sun's transmittance and colour,
  the bounce light's tint) is the model's for the baked sun, as before.
- **The clouds** (`rt_clouds=1`; `rt_clouds.h` is the model and its processor twin, `vulkan_rt_clouds` the device's).
  A shell 1.5 - 4 km above the ground round the planet. Noise made at start-up by three compute passes from an integer
  hash (no assets; the processor computes the same bytes): a 128^3 shape (Perlin-Worley; Worley fractals at 4, 8, 16
  cells), a 32^3 detail (Worley at 4, 8, 16), a 256^2 weather map (patches; the clouds' kind), all tiling (9 km, 1.1 km,
  42 km; the weather's b a clump field - Worley at 16 cells, 2.6 km). The density: the cover - the weather's patches
  x its clumps, so a patch is a field of separate clouds with sky between them, not a sheet (the first version's flat
  grey ceiling seen from 1500 units up: whole patches of cover, the shape's threshold opened to 0.3, read as one
  overcast base); a height profile per kind whose top falls toward the cover's edge (domes, not the clumps' vertical
  walls); the shape thresholded by the cover (never past 0.65 - 0.9 of its range short of overcast), the detail eroding
  the edges. The model turns from none to overcast over a narrow range of its own coverage, so the settings' coverage
  goes through `rt::cloud_amount` (kCloudAmounts, measured by the check's sweep): a coverage c leaves about 1 - c of
  the sky above 12 degrees clear (0.25: 75%, 0.5: 49%, 0.75: 24%, 1: none). The light: the sun's through the cloud
  (six growing steps), Wrenninge's three octaves (Beer's law, multiple scattering), two Henyey-Greenstein lobes (0.8
  forward - the silver lining toward the sun - and -0.3), a powder term, the sky's light (the model's horizon colour at
  the base x0.6, its zenith's at the top, each as bright as the matched sky - not tinted twice); Hillaire's
  energy-conserving step. Coverage auto from the level (0.2 + 0.7 x its share, at most 0.75: a painted sky's grey cards
  and haze count as cloud, and a clear one should stay mostly clear), or `rt_cloud_coverage`; the clouds' colour x the
  level's cloud hue and brightness (dark storm clouds stay dark). The layer is a shell round the planet
  (`cloud_layer_span`): a level ray meets its base 134 km off and the far clouds sink toward the horizon (d^2 / 2R: 197 m
  at 50 km), foreshortened; the march stops 40 km into the layer and the composite fades the clouds into the sky by e per
  20 km, so toward the horizon they melt into the haze instead of running out to it as a sheet.
  They drift with the game clock (`ViewDrawn::game_seconds`, a contract addition: a demo replays the same sky) at 12 m/s
  x `rt_cloud_speed`, and the camera's place moves through them.
- **The passes** (`vulkan_rt_sky_pass`, at `sky_drawn`): the march at a quarter, half (the default) or full resolution
  (`rt_cloud_quality` 0 / 1 / 2: 32 / 48 / 96 steps), only texels whose block has sky, each texel's samples jittered by
  interleaved gradient noise + the frame's golden-ratio offset; a temporal pass (the texel's direction reprojected through
  the viewport's last camera - the clouds are far, a direction is enough -, the history bilinear there, clamped to this
  frame's 3 x 3 neighbourhood, 0.1 of this frame); the composite: a full-screen triangle at the far plane, depth-tested
  (less or equal) against the world's depth - every sample of the sky, no other - writing the sky-view table toward the
  pixel x the clouds' transmittance + their light (the history upsampled, texels without sky left out), the clouds faded
  into the sky with distance (e per 20 km), display-referred. An 8-bit target clips at 1; with `rt_hdr=1` values above 1
  stay for the tone mapping. Split screen: a history per viewport (up to 4). A history that starts again - a new
  viewport, a new level's capture - is seeded in its first frame (the march and the temporal pass 12 times with the next
  frames' jitter, the composite after the last), so the clouds show converged.
- **A level's own sky meshes** (SE2's storm overhead): level geometry, not part of the sky scene - `rtprobe` from the
  sky spot (1500 units over the first restart) meets it 1,030 - 1,360 units up in every direction from 50 degrees to
  overhead (one instance, one material): a low ceiling about 2,500 units over the island, its edges faded by its own
  alpha and drawn over whatever sky is behind it. The edge the first version showed was its dark grey against the flat
  grey-white cloud base (the grey ceiling above); against separate cumulus with blue between them it reads as a storm
  front over fair-weather clouds (out/sky2's SE2 pictures). A blend toward the dome's colour near far, high geometry was
  tried and left out: the storm is near (1 - 1.4 thousand units), so depth can't tell it from a building's top or a
  palm's leaves without haloing those.
- **Level start**: with ray tracing, `RayTracing::prepare_frame` (the first frame after a load's work) also matches the
  level's sky and makes the atmosphere's tables before any view is drawn (`prepare_rt_sky`), so the fog's table and the
  bounce light read the new level's sky from its first frame; without ray tracing the first view's pass does it, still
  before that frame is presented. The capture is drawn earlier in the same frame (`capture_sky`), the read-back stall
  once per level falls in the frame shown over the loading screen's last picture.
- **Cloud shadows** (`rt_cloud_shadows=1`, off by default; at `opaque_drawn`): each surface pixel's way to the sun
  through the layer, the density at four heights (no detail), the colour x mix(1, transmittance, 0.65) - drifting shadows
  on the level; no ray tracing.
- **The reflections' misses** (`rt_sky_for_rays`; `vulkan_rt_reflections`' SKY variant, bindings 9 - 12): with
  `rt_sky=1` a reflection ray that leaves the world sees the sky drawn - the sky-view table toward it and, where its
  direction falls on the viewport, this frame's clouds (their history, read as the composite reads it, faded out over
  the viewport's last 8%; beyond it the sky alone) - as the game's colour; with `rt_sky=0` the misses keep the game's
  environment map as before.
- **Not done**: no time of day (the sun stays where the level puts it); clouds in reflections only where the reflected
  direction is on screen.
- **Checked**: `thug2_vulkan_rt_sky_check` (60 checks; the RTX 5070 Ti and the AMD Radeon integrated GPU, validation and
  synchronization validation 0, no ray tracing asked for): the transmittance table against 2048 segments, Psi against
  the processor's integral, the sky-view table against `sky_radiance_ms` (below the horizon: the same haze-to-ground
  blend on the processor) at 10 and 45 degrees, the multiple scattering brightening the sky, the zenith bluer than the
  horizon; a painted capture analysed on the device exactly as on the processor, `rt_sky_match=1` putting each band's
  centre at the model x the capture's colour over the model's band, 0 leaving the gains 1; below the horizon the
  horizon's haze, not the capture's dark sea (across the horizon luma within 2%, chromaticity within 0.01); a khaki ring
  painted under the horizon seen by the ground band and changing nothing in the match; the composite in an 8-bit and a
  float target; the noise the processor's to a step; every marched texel against the processor's march; the clear share
  of the sky above 12 degrees by coverage (0.25: 75%, 0.5: 49%, 0.75: 24%, 1: 0%); the layer's curvature (a level ray
  meets its base 133.5 km off, 50 km across it lies 196 m under a flat layer: d^2 / 2R); after 30 still frames the
  history steadier than the march; the first frame seeded (its history 3 x nearer the settled one than a march); a
  turned camera without ghosts; no NaN. `thug2_vulkan_rt_reflections_check` (99): with rt_sky every clear-cut miss shows
  the sky-view table toward its reflection (29,338 pixels, 0 off by more than 3 / 255), and with no sky drawn the misses
  are unchanged. The other checks on both GPUs: sun 77, fog 76, hdr 80, gi 32, rt 20, shadows 45, effects 51,
  materials 34, rt_scene, format. In the game `tools/game/rt_shadows_check.ts --effect sky` (tools/game/rt_sky.json, the
  user's [Graphics] settings - every RT effect, 8 x MSAA, tessellation): drawn everywhere; bounded but one picture -
  TR-horizon, looking into the low sun from 1500 units up, has 12.7% turned white (the bound is 8%: the original is
  near-white there too, 230 - 245; the drawn sky's glare reaches 255); mean luma change at most 55.8 (NO-up), at most
  0.34% turned black.
- **Cost** (device timestamps, logged every 240 passes as "rt_sky: the passes take"): `--perf` at 1920 x 1080 (the
  check's scene, half the screen sky) - the sky alone 0.015 ms on the RTX 5070 Ti, 0.85 ms on the AMD integrated GPU;
  with clouds at half resolution 0.13 ms (march 0.09, temporal 0.01, composite 0.03) and 7.4 ms (4.7, 1.1, 1.6); a
  seeded first frame 12 x the march and temporal pass once. In the game at 1280 x 720 with 8 x MSAA and every other
  effect on: 0.04 - 0.12 ms on the RTX 5070 Ti.
- **Dark levels**: the sun on the clouds x a smoothstep of the dome's brightness (0.18 - 0.30 of its mean luma): NO's
  night sky (0.23) keeps its clouds dim grey; the day and dusk levels are unchanged.

### The first frame after a load

With the effects on, the first frame the player sees after a load is as complete as one a second later - nothing
fades in. Before, four things were deferred: the baked sun was fitted only once the world had stood still for 60
frames (shadows, the bounce light, the fog's shafts and the haze's sun skipped until then, then ~0.35 s of tracing
mid-play); the BLASes' compaction ran a few frames later (memory only - every BLAS was already built in the first
frame); the temporal effects (AO, GI, fog) started from empty histories and faded in over 12 - 32 frames; and the sky
model's table / sun colour was made by the sun's pass, after the bounce light had read last frame's colour. Now:

- **A new world's work in its first frame** (`render_pc/rt_record`): the loaded scenes' key (each non-sky scene, its
  meshes and their count - not which are shown, so a sector turned on or off keeps the fit) differs from the one last
  settled - a level's first frame, the editor's load. Right after that frame's `begin_frame` and before any view is
  drawn: `RayTracing::settle` (added to the contract; the Vulkan backend finishes the frame's work, then builds the
  frame's scene again - which collects the compacted sizes, copies each static BLAS into its smaller structure and
  rebuilds the TLAS over the new addresses - in rounds until no size is waited for), then the baked sun's fit when it is
  wanted. The frame is presented over the loading screen's last picture, so this is loading time. Log: "rt: a new
  world's first frame: ... ms in all before it is drawn" and "rt: the new world's first frame took ... ms".
- **Every history starts again** with a new world (`settle` resets the AO's, GI's and fog's): a camera held at the
  same place across the load would otherwise find the last world's history.
- **Meshes not skinned yet are traced by nothing**: a mesh skinned on the processor is recorded as a transparent
  instance (mask 0x80) until one of its vertex buffers has been written after it was first recorded - before that its
  buffers hold the unskinned data, which in a level's first frame cast large wrong shadows and occlusion (AU: 40% of the
  first picture darkened, the seeded histories carrying it for ~30 frames). The same held for a ped appearing mid-play.
- **The frame's sun before the views**: `RayTracing::prepare_frame` (added; every frame after `begin_frame`) - the
  Vulkan backend brings the sky model's table and `rt::set_sun_light` up to date for the frame's sun while the sun's
  effects or the sky model are on.
- **Seeded histories** (`gpu_vulkan/vulkan_rt_history.h`): a history that starts again - a new viewport or target, a
  load, a viewport not drawn the frame before, the settings changed, or a **cut** (the camera moved more than 250 units
  or turned more than 45 degrees between two frames: a teleport, a restart, looking behind) - runs the trace and the
  temporal pass as many times in that frame as the history holds, each with the next frame's directions / jitter from
  the same camera: AO and GI 32 times (then the spatial filter once), the fog 12 marches. The frame shows what a still
  camera shows after that many frames; pixels disoccluded in a continuing shot still start again on their own. The cost
  is that many passes in the one frame (logged once per effect: "a history started again ... seeded").
- **Pipelines**: every effect's pipelines are made the first time its pass runs - now all in that first frame, before
  it is presented (the fog's and the shadows' no longer wait for the sun). The RT shaders are compiled by glslang each
  run (no SPIR-V cache yet - the front end's first frame pays for most of it: 0.3 - 2 s measured, the driver's own cache
  warm or not).
- **Off**: with no ray-tracing backend `rt_record` does nothing; with ray tracing but no effect wanting the sun, only
  the compaction (milliseconds).
- **Checked**: `thug2_vulkan_rt_effects_check` (the first denoised frame and a camera cut against the 256-ray reference,
  every history 32 long), `thug2_vulkan_rt_gi_check` (the first frame against its reference), `thug2_vulkan_rt_fog_check`
  (the first frame's history full and converged; the raw march compared at the seeded frame index). In the game,
  `tools/game/first_frame_check.ts` (tools/game/README.md): the automation channel's `capture 61 <folder> afterload`
  takes a level's first frame and the 60 after it from a camera held through the load, with the user's `[Graphics]`
  (`on`) and with ray tracing off (`off`: the scene's own motion); frame 1 against frame 60 outside both noise masks.
  Measured (RTX 5070 Ti, 1280 x 720, the user's settings: every effect, msaa 8): BO 0.000% of the compared pixels
  changed, AU 0.007%, z_dt 0.081%, foun 0.015% (at most 0.5%); the work before the first frame is drawn 255 / 361 / 186
  / 265 ms (the structures 13 - 55 ms of it, the rest the sun's fit), the first frame 389 / 1557 / 330 / 365 ms in all
  (the seeded histories and the pipelines first used there; the front end's first frame ~330 ms). Before, the first
  frame showed no shadows / bounce / haze sun and AO / GI faded in; the fit's ~0.35 s came a second into play.

### Smoothed curves (`rt_tessellation`)

THUG2's curves are facets: a quarter pipe is 6-12 flat strips, a rail 6-8 sides, a cannon wheel or a car's wing a few
dozen triangles, and their silhouettes and shading show it. `[Graphics] rt_tessellation=1` (off by default;
`rt_tessellation=0` loads and draws exactly what main does - one test of a flag per mesh at load) rounds them off.

- **Where it is done: once, at load, on the processor** (`engine/gfx/smooth_tess`, `render_pc/mesh_smoothing`). A
  scene mesh's body (`read_mesh`, before its buffers are made) gets its curved triangles split: the new vertices after
  its own, a new strip for its first level of detail. Nothing in the draw path changes - the fixed-function emulation,
  the pipelines, the Direct3D 9 renderer and the ray tracing all get more triangles - so it costs no shader variant, no
  tessellation stage (absent from the AMD integrated GPU's and MoltenVK's portability subsets anyway) and no mesh
  shaders, and **what rays see is what is drawn**: `rt_record` registers each mesh's first level of detail from its
  buffers, so its BLAS is built from the smoothed geometry (the shadows, occlusion and reflections fall on the rounded
  surface; `rtprobe` hits it - below). The other levels of detail keep the original vertices (still first) and strips.
  One reader keeps the original triangles: the baked sun's samples (R4: `fit_baked_sun` and the bake's agreement walk
  the world's triangles, each counted the same) - split triangles would weigh 9 - 16 times and move the fitted sun (sch:
  elevation 36 -> 31, the agreement's samples 195,000 -> 673,000: shadows changed level-wide, the first check run caught
  it); `mesh_smoothing` keeps each smoothed mesh's original strip by its index buffer (shared copies share it, copies
  copy it) and `rt_record` samples that.
  Distance-adaptive factors would mean a second strip per mesh or a tessellation stage; instead each mesh gets the level
  its curves need (below), which keeps it crack-free with a fixed vertex buffer.
- **Which meshes**: every scene mesh (levels, objects, cars - models loaded from memory too) with a plain vertex format
  (a position, optionally a normal, a colour and texture coordinate pairs), not skinned (skaters and peds are left
  out), no per-vertex colour animation, no bone-scaled positions, not dynamic, its material's first pass neither
  transparent (decals, glass, water) nor cut out (alpha reference above 8: foliage, fences' meshes). Within a mesh only
  triangles with a **curved edge** are split: the corners' normals come from the geometry (the file's normals are
  lighting, and most level meshes have none) - faces joined across edges where they meet at under 50 degrees (smoothing
  groups, the faces' normals averaged by their corner angles) - and an edge is curved when exactly two faces of the mesh
  share it, wound alike, at under 50 degrees, and its curve leaves the straight edge by more than 0.02 units. Flat walls
  and floors (parallel normals: no curve), hard edges (creases), open edges and the meshes' boundaries stay straight.
  Measured: BO 585 of 2173 meshes (11,577 triangles split; indices 39,250 -> 294,202), sch 961 of 1711 (24,380;
  117,492 -> 650,697); 52 / 104 ms at load.
- **The shape**: Phong tessellation (Boubekeur and Alexa 2008: a point of the flat triangle pulled toward its corners'
  tangent planes) with shape factor 0.5, raised per edge by 2 (1 / cos h - 1) / sin^2 h (h half the angle between the
  edge's two normals) so an edge's middle lies on the circular arc its normals describe; inside a triangle the edges'
  factors blend toward their own edge. On a 16-sided cylinder of radius 100 the new points lie within 0.02 of the circle
  (the facets: 1.92); on an 8-sided rail of radius 3, 0.008 (0.23). A mesh's level (2 - `rt_tess_level`, default 3) is
  the one that follows its largest curve to about 0.05 units (the error falls as 1 / level^2).
- **Crack-free**: one level a mesh, so two split triangles on an edge cut it alike; a curved edge's points are made from
  its two ends' positions and normals only, in one order (the lower welded position first), so the triangles on it - with
  the same vertex indices or across a texture seam (positions repeated) - get the same points to the bit; a straight edge
  of a split triangle is not cut: its points collapse onto its two ends (degenerate slivers, a fan), so meshes and
  triangles beside it meet it at its original two vertices - no T-junctions anywhere.
- **Collision is the original's** (nothing in physics changes; the game's collision is its own `.col` data), so the
  drawn surface must stay near it: each mesh's largest distance of a new point from its original flat triangle is
  measured, and past `rt_tess_max_deviation` (default 2 units) the shape factor is scaled down until it is exactly that.
  Measured per level ([debug] `tess_log=1` logs every mesh): sch's median 0.69, BO's 1.36; 270 meshes a level reach the
  cap (big low-poly shapes - mounds, domes, statues - whose uncapped bulge would be 2 - 99 units). At sch's quarter pipe
  `tools/game/tess_gap.ts` (a fan of rays cast both through the collision - `colprobe` - and the ray-traced scene -
  `rtprobe` - with rt_tessellation 0 and 1, the gap along the slope's normal) finds the drawn surface moved by at most
  0.77 (mean 0.16) and only behind the collision (the arc lies behind its chords): the wheels can float that much over
  the drawn curve between its original vertices, never sink (0.04 at most in front); the original's own drawn meshes
  already differ from its collision by up to 2.5 there.
- **Displacement** (`rt_displacement` > 0: units for the full height range): a new point moves along the smoothed normal
  by scale x (height - 0.5), inside the cap left by the curve; only new points shared outright by both triangles of an
  edge, or inside a triangle (a seam's two sides would read different texels and open); never on thin meshes (bounds'
  smallest side under 24 units: rails, poles). The heights come from `mesh_smoothing::set_height_lookup` - the seam for
  generated height maps (a material's lookup on the processor, at load); none is installed yet, and
  `rt_tess_test_heights=1` gives every material a test pattern (ridges along u) to see it work.
- **Checked**: `thug2_tess_check` (processor only - the tessellation runs there, so it is the same on every GPU): a
  16-sided cylinder with a seam down its side and one round its middle (every inner edge shared by two triangles wound
  opposite ways after welding by position; the seam's new points made on both sides at the same positions; texture
  coordinates continuous; every triangle still facing out; the original vertices first and unchanged; nothing NaN),
  capped to 0.5 (the deviation exactly the cap, every point between the facets and the circle), displaced by a ridged
  height (within the cap, still crack-free); an 8-sided rail (smoothed, not displaced); a quarter pipe with floor and
  deck in two meshes side by side (on the arc within 0.0014 between its end segments - 0.26 at the floor's and the
  coping's corners, which have one side's normals only -, the deck behind the coping's crease untouched, the two meshes
  meeting at their original vertices only); a flat floor, a box and `rt_tess_level=1` unchanged; a mesh too big for 16
  bits left as it is. In the game `tools/game/rt_shadows_check.ts --effect tess` (the plan's `qpsch` / `qpBO` groups:
  sch's quarter pipe still and with the skater riding up it, fences, barriers, a taxi; BO's start, cannons, statues):
  off against main's build changed 0.00 - 0.01% (identical but for animation), on against off bounded (mean luma change
  at most 0.8, nothing turned black or white - no holes, no sky through a crack).
- **Cost**: the draws' triangles - the smoothed meshes' indices x 5.5 - 7.5 (BO 39,250 -> 294,202; the split triangles'
  rows are strips of their own, joined by degenerate triangles); in sch at the start with the user's settings (every
  ray-traced effect on, 8 x MSAA, 1280 x 720, RTX 5070 Ti) the frame lines read 21.9 / 24.5 ms against 22.0 / 25.1 off -
  within the run-to-run noise (the frame is bound by the processor; the device waits 0.3 ms); memory: sch +66,780
  vertices (~2.5 MB) and +533,205 indices (~1 MB); 50 - 180 ms more at load.
- **Not done**: no distance-adaptive or screen-space factors (one level a mesh, made at load: changing it needs the level
  reloaded); skinned meshes; the generated height maps (the seam is there).

### Relit: the bake taken apart (`rt_relit`)

`engine/gfx/rt_relit` (the analysis, API-neutral) and `render_pc/rt_relit_world` (the world's meshes, the cache, the
writes) - `[Graphics] rt_relit`, off by default. THUG2's world light is baked into its vertex colours: what a vertex
shows is the artists' colour (its tint) times the light that reached it, and only the product is stored. The relit
mode separates them and lights the level again.

- **Where it acts: the vertex buffers.** The relit colours are written into the buffers the game draws from (each
  vertex's colour word, as `MeshRef::apply_vertex_colors` writes the game's colour animations; `rt_record::ColourWrite`,
  so the acceleration structures aren't rebuilt), not into the fixed-function shaders: nothing in `ff_key` /
  `ff_shaders` changes, so the per-pixel lighting work there is untouched, and Direct3D 9's draws would show the same
  colours. The ray-traced effects (shadows, occlusion, one bounce, reflections, fog) run over the relit colours as
  they ran over the bake. Each buffer's own colours are copied before the first write and written back when the mode
  is turned off (the console's `rt relit off`), the level opts out or its bake shows no sun; the baked sun's fit reads
  the copies (`rt_relit_world::original_colours`). A vertex whose colour the game animates is left to the animation.
- **When.** At the baked sun's fit (rt_record, once the world has stood still): the same moment, the same world - so
  whatever moves the fit into the loading screen moves this with it. Meshes shown later are taken apart when they first
  stand in a still world, with the level's model as it is.
- **The light at each vertex** (`trace_relit_light`, RayTracing::probe_many, masks 0x01): its normal (the mesh's, or
  its triangles' area-weighted, turned by the world's winding - the sign that turns most of its horizontal area up);
  S, the sun's share - 4 rays toward the baked sun jittered over a disc 1.5 degrees across (none for a vertex facing
  away), times N.L; K, the sky's share - 24 cosine-weighted rays over its hemisphere (a golden-angle set turned by a
  hash of the vertex), those that escape; B, the bounce - the same rays' hits, each bringing the baked colour where it
  lands (the hit's barycentrics from the hit point), averaged over every ray. The rays start 2 units off the surface.
- **The model** (`fit_relit_model`): E = a S + b K + g B per channel. Two vertices joined by an edge share their
  surface's colour, so y_i E_j = y_j E_i - a condition on (a, b, g) that no tint enters; every edge of every mesh gives
  one, and (a, b, g) is the non-negative direction minimising them (each subset of the three tried; reweighted three
  times toward the median residual, so edges across a change of paint count little), its scale per channel the median
  of y / E (the median tint 1). The hue the colours can't tell from the tints (a bluer light over yellower tints shows
  the same) is the median's: the tints are grey on average.
- **Tints and the residual** (`decompose_relit`): each vertex's own tint y / E (E no lower than 5% of the level's mean),
  and its neighbourhood's - a normalised convolution over the mesh's edges, 4 rings, each vertex weighted by its
  confidence (its light over 0.35 x the level's median, at most 1). A vertex brighter than its neighbourhood's tint x E
  by 1.2 times or more is lamp-lit: left out of the neighbourhoods (four rounds, as a wide pool shows its edges first),
  and the share of its light over that prediction (smoothstep 1.2 - 1.8) is its residual - the lamps', glows' and
  artists' own light, kept as it was. The rest gives its tint, kept by `rt_relit_detail` x its confidence (the
  neighbourhood's otherwise); tints clamp at 2 (what a clamped tint can't carry goes to the residual too). One factor
  (the level's `scale`, 0.7 - 1.4) keeps the level's mean brightness the bake's.
- **Relit:** scale x tint x E' + residual (`rt_relit_residual=0` leaves the residual out), blended with the bake by
  `rt_relit_strength`. At the bake's own sun it is the bake again (detail 1: within a byte at 99.6% of the check's
  vertices). E' takes the sun where it is now - the scene's sun (`rt_sun_source`, "One sun for every effect" below;
  `rt_relit_sun` before it) - its rays traced again when it moves (150,000 vertices a
  frame), then its sky rays for the bounce of the new light (the hits' colours the relit ones; 40,000 vertices a
  frame); and the sun's colour: with `rt_sky_model=1` the model's colour at its elevation over the colour at the bake's
  (the bake's own colour kept at the bake's sun).
- **The skater and the objects** keep the game's two directional lights; while the world is relit by another sun or a
  coloured one (`rt_relit_world::object_lighting`), pc_lights turns them by the rotation that takes the bake's sun to
  the relit one and tints the first (the key light) by the sun's colour. At the bake's own sun they are the original's.
- **No sun.** A level whose bake shows none (rt_sun.h's fit: the Foundry, indoors - lit by its lamps) keeps its bake:
  relit from the sky and bounce alone it would go dark. So does one whose fit finds no sunlight (the sun's term under
  0.05), and one listed off in `src/engine/gfx/rt_relit_levels.json` (built in) or `thug2-decomp-relit.json` beside
  the exe (the same layout: `{"levels": {"<scene>": {"relit": false, "strength": 0.5}}}`).
- **The cache:** `thug2-decomp-relit\<the level's scene checksum>.bin` beside the exe - the model and each mesh's
  vertices (normal, shares, bounce, tint, residual), each mesh by a hash of its vertices and indices (and the settings
  that change the results: the ray counts, detail, the format's version). A mesh found there isn't traced.
- **Checked:** `thug2_vulkan_rt_relit_check` (25 checks, the RTX 5070 Ti and the AMD Radeon, validation 0): a level
  (a ground with a red-painted square, a building, a pillar, a degenerate triangle and an unused vertex) whose bake is
  made on the processor from known tints, sun, sky and bounce and a lamp in the building's shadow - the analysis's own
  rays traced by a processor's ray tracer, iterated until the bounce holds still - then taken apart on the device: the
  traced shares and bounce the processor's at every vertex; each channel's sky and bounce against its sun within 1-3%
  of the bake's; every tint within 6% (over the factor per channel the colours can't tell); the red square's contrast
  against the ground exact (2.00); the lamp's pool's middle back in full as the residual, 42% of its light where it is
  a quarter of the bake or more (its soft edge stays in the tints), no residual elsewhere; relit at the bake's sun within
  a byte at 1111 of 1115 vertices, strength 0 exact; relit by another sun within 4 bytes of the processor's bake for it
  at 97% of the vertices (90% before the bounce is traced again); the bake's sun found by rt_sun.h's fit within 10
  degrees; no model: the bake kept; no NaN. `--perf` (484,000 vertices): 6.8 million rays in 1.4 s (RTX 5070 Ti) / 2.0
  s (AMD), the fit and the tints 0.8 / 1.0 s, the sun again 0.2 s.
- **In the game** (`tools/game/rt_shadows_check.ts --effect relit`, foun / z_dt / BO / AU / BA, every other effect on as
  the user plays - rt_shadows, ao, gi, reflections, fog, sun, sky model, HDR, 8x MSAA -, two runs): the analysis
  104,000 - 132,000 vertices, 1.7 - 2.2 million rays, 0.52 - 0.69 s once (a frame's stall when the world first stands
  still), 13 - 35 ms from the cache after; nothing a frame after that (the colours are in the buffers). `rt_relit=0`
  against main's build: 0.00 - 0.05% of the pixels changed (the noise). At the bake's sun every level relit is its bake
  ("mean brightness 68.2 against the bake's 68.2, 0.0% of the vertices changed by more than 2"; at most 0.3% of a
  picture past the noise); the Foundry (no baked sun) unchanged. `rt_relit_sun=game` (the sun the game shows: 20
  degrees up toward +z in z_dt, BO, BA, toward +x in AU, against the bakes' 50 - 53): mean vertex brightness 3 - 7%
  lower, 47 - 72% of the vertices changed, the sun warmer by the sky model's (1, 0.95, 0.85) - z_dt's street 25 luma
  darker in the picture (its stairs and pavement turned away from the low sun), BO's yard 5, AU 4, BA 2.
- **Not done / limits:** (with `rt_relit_sun=game` the ray-traced shadows, bounce and fog kept the baked sun - fixed by
  `rt_sun_source`, below); the soft edge of a lamp's pool partly stays in the tints (and so dims with a
  lower sun); walls whose vertices start their rays on the floor's plane see half their hemisphere as the GPU rounds it;
  the analysis runs at the baked sun's fit, after the loading screen, not during it.

### One sun for every effect (`rt_sun_source`)

Before this, each effect picked its own sun: the shadows `rt_shadow_sun` (the baked one), the drawn disc `rt_sun_place`
(the game's), relit `rt_relit_sun`, and the sky model the game's. With mixed choices the lighting, the shadows and
the disc pointed different ways. Now `engine/gfx/rt_scene.h` `scene_sun()` is the only accessor, and every effect
asks it: the shadows, the bounce (`rt_gi`), the fog's shafts and haze, the drawn disc with its glare, rays and bloom,
the sky table, the sky model and clouds, relit, and through relit the skater's and objects' turned lights. The rules
are the pure `rt_sun.h` `resolve_scene_sun`, in this order:

1. The console's `rt sun x y z`.
2. `rt_sun_source=x,y,z` or `heading,elevation`: a fixed direction, for experiments.
3. No baked-sun fit yet: no sun. The bake shows no sun (indoors): none, whatever the setting, and the level keeps its
   bake.
4. `level` (the default): the baked sun.
5. `game`: the game's shown sun (`rt::game_sun`), but only when relit relights the level, its entry in
   `rt_relit_levels.json` doesn't say `"sun": "level"`, and the sun is at least 10 degrees up. Otherwise the baked sun.

A counter changes whenever the direction or the origin changes. `rt_shadow_sun`, `rt_sun_place` and `rt_relit_sun`
are retired; the log names them when an ini still has them. The Options screen has one "Sun" row (Sky | Level) at the
top of the Lighting tab, and changing it is live: the shadows, bounce, fog, sun, sky and relit settings are read
again, so their histories reset and relit traces again. The console's `rt sun` prints the scene's sun, the game's,
the baked one and the sun each effect used last; `rt sun source <...>` switches for one run.

**Game against the bake.** A sun other than the baked one can't light the bake: its shadows point the other way. So:

- `rt_sun_source` other than `level` turns `rt_relit` on (logged).
- The game's sun is used only once relit has the level's model. Until then the scene's sun is the baked one, so there
  is never a mismatch.
- The shadows' agreement grid (`rt_shadow_match_bake`) is made again for the new sun once relit has written every mesh
  for it (`rt_relit_world::settled`): `render_pc/rt_record` `other_sun_work` takes the same samples, with the relit
  colours (tint x the new light + the residual), and traces them toward the scene's sun. The relit colours hold the
  bake's shadows turned to that sun, and lamps and glows (the residual) keep the traced shadow off where they lit the
  bake. `rt::agreement_for(direction)` gives the shadow pass the grid for its sun (the baked one or this one); until it
  exists the traced shadow applies everywhere. The roof test is traced at run time and needs no change.

**Checked:**
- `thug2_rt_scene_check` covers the rules and `rt_sun_source`'s text.
- `thug2_vulkan_rt_sun_check` adds 9 checks (RTX 5070 Ti and AMD Radeon). In each mode - fixed, switched, level, game
  relit, game not yet relit, entry keeps the bake, game sun too low, none shown, indoors - the shadows, fog and drawn
  sun all receive the scene's sun, and the counter moves exactly when the sun changes.
- In the game, `tools/game/sun_source_check.ts` ran the 15 main levels with the user's look (every effect, 8x MSAA,
  DLSS), each with `level` and with `game`, four spots a level. The Level | Game pictures are in its `side\` folder.

**Results in the game:**
- **Every effect on one sun.** In every level and both modes, all six effects that asked held the scene's sun. The one
  exception is relit with `level`: it lights by its cached model's own fit, kept so the bake comes back exactly, which a
  new load's fit puts within 1 - 3 degrees.
- **No sun in 5 levels.** AP, CA, NO, SE and SE2 have a bake that shows no sun. They get no sun in either mode, and their
  pictures are the same in both.
- **The game's sun is a default.** It is the same in 12 of the 15 levels: heading 0, 20 degrees up. AU's is heading 65,
  SE's lies on the horizon and NO shows none. The baked suns are the artists' real ones: 32 - 62 degrees up, every
  heading.
- **Relit by the game's sun:**
  - The shadows, the disc and the lit faces agree, as BA's banner shadow cast toward the disc shows.
  - The levels are mostly darker and flatter, since a 20-degree sun leaves the bake's sunlit ground in shade. DJ's
    pictures fall to 0.55 - 0.83 of Level's brightness, AU's to 0.64 - 0.85, SC's to 0.60 - 0.84. AU's road shows
    blotchy relit shadows. TR, already dark, gets 1.3% more black.
  - Others brighten where the low sun faces the camera: LA 1.14 - 1.33x, PH up to 1.38x, ST up to 1.30x.
  - No NaN or black holes: black share within 0.2% of Level everywhere but TR.
- **Hence `level` is the default.** DJ, AU and SC are listed `"sun": "level"` in `rt_relit_levels.json` for those who
  pick `game`.

### Upscaling: DLSS and FSR (`upscaler`)

`gpu_vulkan/vulkan_upscale` (+ `vulkan_motion`, `vulkan_upscale_fsr`, `vulkan_upscale_dlss`; `[Graphics] upscaler=off|dlss|fsr|auto`,
`upscale_quality=dlaa|quality|balanced|performance|ultra`, `ray_reconstruction`, `upscale_sharpness`, `fsr_version`;
off by default - nothing is drawn differently then: no listener, no motion bit, the scale never published).

- **The world drawn smaller** (contract additions in `engine/gfx/rt_scene.h`: `ViewScale`, `view_begins`,
  `ViewDrawn::output_rect` / `jitter`, `ViewOrder::kUpscale` = 950). The upscaler publishes a scale (1, 0.667, 0.588,
  0.5, 0.333) and the frame's jitter; render_pc's `draw_viewport` then draws the window's one viewport (not a split view,
  not the editor's game-view picture) into the top-left part of its rectangle, its projection moved by the jitter
  (`rt::jitter_projection`: Halton (2, 3), ceil(8 x ratio^2) phases - FSR's and DLSS's advice). Everything up to
  `world_view_drawn` - the opaque passes, the ray-traced effects at `opaque_drawn`, the transparent passes, particles,
  fog, the sun, the tone mapping - works on that smaller rectangle as on a smaller window (the effects' histories
  reproject through the jittered cameras: sub-pixel noise their clamps take). The listener at `kUpscale` fills the
  whole rectangle; the game's own screen effects and the 2D come after, at the full size. The editor's mouse ray keeps
  the full rectangle (`ext::ViewNote::rect`).
- **Motion vectors** (`vulkan_motion.h`). From `view_begins` to the upscale every pass on the view's target has a
  second colour attachment (RGBA16F: x, y the move to the pixel's place the frame before in 0 - 1 of the picture, z the
  largest alpha a blended draw left - the reactive mask). Every 3D draw gets the fixed-function emulation's motion bit
  (`VertexKey::motion` / `FragmentKey::motion`, `PipelineKey::motion`; a separate addition to the vertex stage: two
  varyings, the previous position at location 12 from binding 1, `VertexUniforms::previous_clip` / `motion_jitter`):
  the position this frame without its jitter, and the frame before's through that frame's world (the draw's
  `Gpu::MotionSource::key` - render_pc's mesh and instance; key 0: the level, which doesn't move), view (the camera's
  the frame before; its rotation alone for the sky) and unjittered projection. Skinned meshes: render_pc's buffers take
  turns, so the frame before's pose is the buffer before (`MotionSource::previous_vertices`). Opaque draws write x, y;
  blended ones only z (blend MAX); pre-transformed ones nothing. The device gets `independentBlend`.
- **The preparation pass** (compute, at `kUpscale` after the tone mapping): where no draw wrote a motion (the
  attachment's clear, x = 1000: the sky, cleared pixels) the camera's motion of the point at the pixel's depth; the
  depth as R32F; the reactive mask (z, at most 0.9); the colour blitted to RGBA16F. Then the provider, then its output
  blitted over `output_rect`. Direct3D 9's pixel centres (the Vulkan Gpu moves every draw half a pixel) are told to the
  providers through the jitter offset: each provider's convention (`UpscaleJitterTerms`) was measured, see the check.
- **FSR**: AMD's FSR SDK 2.4.0's C API (MIT headers) and its signed `amd_fidelityfx_loader_vk.dll` /
  `amd_fidelityfx_upscaler_vk.dll` (FSR 3.1.6; FSR 4 on the GPUs that have it, `fsr_version=3` keeps 3.1), loaded at run
  time; non-linear (display-referred) colour, the reactive mask, optional sharpening. Its passes need 16-bit floats and
  integers and compute-shader derivatives: the device gets them when an upscaler is set (`UpscaleAdditions`).
- **DLSS**: NVIDIA Streamline 2.14.16 (MIT) with DLSS 310.9.16, manual hooking: `sl.interposer.dll` (signature checked)
  initialised before the instance; its instance / device extensions, Vulkan 1.2 / 1.3 features and queues through
  `ContextAdditions` (`vulkan_context.h`); `slSetVulkanInfo` when the device is made; its mandatory present / swapchain /
  wait-idle hooks by putting its proxies into volk's function pointers. Per frame: a frame token, the constants (the
  unjittered matrices, clip to the frame before's clip, the jitter, `mvecScale` 1, LDR colour), the tags (colour in and
  out, depth, motion, the reactive mask as "bias current colour"), one evaluation. DLAA is its `dlaa` mode.
- **Ray Reconstruction** (`ray_reconstruction=1`, sl.dlss_d): fed the same frame plus guides - the "albedo" is the
  opaque colour before any ray-traced effect (copied first at `opaque_drawn`), so what the effects add or take away is
  the lighting it denoises; normals from the depth (view space, roughness 1 packed); no specular albedo. While it runs,
  the occlusion's and the bounce light's own denoisers are skipped (their raw rays reach the frame). Fits: AO and GI
  (modulations of a baked frame). Doesn't fit: reflections (a mirror ray, no noise: left as is), the shadows (no
  denoiser of their own), fog / sun (screen-space, after the frame). Not measured for quality yet (needs the game).
- **DLSS 5 / neural rendering**: no public SDK - nothing done.
- **MSAA**: off while upscaling (`sample_quality_levels` answers 0 for more than one sample, `create_device` drops the
  request) - the world's target must have one sample.
- **Licences**: THIRD_PARTY_NOTICES.md - FSR's DLLs under AMD's binary licence (redistributable with its notice), the
  DLSS DLLs under NVIDIA's RTX SDKs licence (with an application, not alone; attribution in its credits), Streamline
  MIT; the files are fetched at configure time (`cmake/upscalers.cmake`, SHA-256 checked, 64-bit Windows, `THUG2_FSR` /
  `THUG2_DLSS`), never committed; their licence files are copied beside them.
- **Checked:** `thug2_vulkan_upscale_check` (validation with synchronization; the RTX 5070 Ti and the AMD Radeon
  integrated GPU): the jitter (Halton values, phases 8 / 18 / 32 / 72, centred); every clear-cut pixel's motion of a
  still ground, a moving and turning quad (its key), a skinned quad (the previous buffer) and the sky (from the depth)
  against the processor's reprojection - worst 0.002 px; FSR and DLSS on a lattice of thin bars after 48 still frames
  against a 4 x 4 supersampled reference (PSNR, the full size without AA 17.40 dB): FSR 67% 19.44 / 19.50 (NVIDIA /
  AMD), 59% 18.83, 50% 17.33 / 17.20, 100% 19.63; DLSS 67% 19.93, 50% 20.01, 33% 19.10, DLAA 19.99; each provider's
  offset beats the jitter's sign reversed and no / reversed pixel-centre alignment; output size, no NaN; FSR the same
  bytes twice from a fresh start (not at 100% on the AMD GPU). Validation 0 of ours; inside the vendor libraries'
  own passes FSR's shaders draw format warnings (32-bit declared on images it makes 16-bit) and NGX
  synchronization notes on its own buffers - counted apart. The other Vulkan checks pass unchanged on both GPUs.
  In the game: `tools/game/rt_shadows_check.ts --effect upscale --upscaler dlss:quality` (z_dt, BO, sch - seven spots,
  every ray-traced effect on, HDR, `off` with 8x MSAA, 1920 x 1080, the RTX 5070 Ti): `on` against `off` mean PSNR /
  SSIM - DLSS quality 34.65 dB / 0.944, performance 34.52 / 0.951, DLAA 35.16 / 0.945; FSR quality 35.61 / 0.958,
  performance 34.39 / 0.940; worst spot z_dt-back (30.7 dB); nothing black; the logs say every view was upscaled.

### Per-pixel lighting and surface maps (`rt_pbr`)

`gpu_vulkan/vulkan_surface` + the fixed-function generator (`ff_key.h` `kSurface*`, `ff_shaders.cpp` "the per-pixel
lighting"), `[Graphics] rt_pbr` (off by default), `rt_pbr_specular`, `rt_pbr_normal_strength`, `rt_pbr_objects`. No ray
tracing needed; with it the bake's sun (R4's fit) is the light the maps answer to.

- **The maps** are made offline from the player's own textures (`tools/pbr_maps/pbr_maps.ts make --game <folder>`, or
  `unpack_game.ts --pbr-maps classical`, recorded so `--restore` removes them): for each level dictionary
  `levels\<x>\<x>.tex.xbx` the game reads (skies left out) - the data overlay's copy (`thug2-decomp-data`: the ported
  levels), else an archive's in `Data\pre`, else the loose one - a file `<x>.tex.xbx.pbr` beside that copy (in the
  overlay, or in `Data\levels\<x>` for an archive's) (`formats/surface_map_format.h`:
  "T2SM" v1, entries of checksum, the texture's content hash - FNV-1a 64 of its first level as stored -, size, class,
  generator, F0, then RGBA8 texels: normal x along +u, y along +v as 0 - 255 for -1 - 1, roughness, height 0.5 = the
  surface). A map is used only for the texels it was made from: an upscaled or modded texture keeps none until the
  maps are made again (the log says so per dictionary).
- **Which generator.** The classical one (`classical.ts`): height = the band-passed luma (scales 1-8 texels x size /
  256, dark is low), normals by a Sobel filter x 3 x the class's bump, roughness = the class's base ± half its range by
  the local grain. CHORD (Ubisoft La Forge, SIGGRAPH Asia 2025) was the model candidate: its weights
  (Hugging Face `Ubisoft/ubisoft-laforge-chord`, `chord_v1.safetensors`) are gated behind accepting Ubisoft's terms and
  licensed for academic or research use only (the Ubisoft Machine Learning License, Research-Only - Copyleft; outputs
  unclaimed) - so it is not fetched, shipped or run here: `--generator chord --chord <folder>` runs a player's own
  set-up (`chord.ts`: its `test.py`, its maps read back, OpenGL normals turned to +v, the roughness clamped into the
  class) and falls back to the classical maps per texture. "PBRnxt" (MIT) was not found. The classical generator is the
  default and the only one measured. CHORD also expects 1024 x 1024 generated, tileable textures; THUG2's are 64 - 256
  texels of DXT-compressed photo art.
- **Which textures** (`classes.ts`, in order): skipped - smaller than 16 texels, environment maps (the sky pictures
  sphere-mapped on), textures used only as a later pass (overlays, light maps), decal passes, blended passes whose
  texture is see-through (> 10% of it under 0.9 alpha, the game's doubled alpha read: 128 is opaque), cut-outs (> 20%
  under half alpha) unless green (foliage), flat art and text (< 24 distinct 5-bit colours). The six levels of the
  in-game check (foun, z_dt, BO, z_ho, sch, AU with their `_net` dictionaries): 2,697 maps of 5,222 textures in 12 s -
  1,240 skipped as see-through, 444 overlays, 300 cut-outs, 251 flat art / text, 224 decals; classed concrete 987,
  default 793, stone 449, wood 196, foliage 136, ground 45, tile 26, water 25, metal 23, asphalt 15, glass 2. (A first
  look in BO had the default class at 0.45 - 0.8 and the normals at 4 x: plastered walls sparkled - raised to 0.55 -
  0.85, 3 x.)
- **Roughness** - AI or classical, a generator's roughness is clamped into its texture's class's range; the class from
  the level's materials (a material with an environment pass is shiny: grey - metal, see-through - glass, else glazed
  tile) and the texture's colour statistics (hue, saturation, luma, grain, streaks):

  | class | roughness range | base | F0 | bump |
  |---|---|---|---|---|
  | default (painted) | 0.55 - 0.85 | 0.72 | 0.04 | 0.5 |
  | asphalt | 0.75 - 0.95 | 0.88 | 0.04 | 1.0 |
  | concrete | 0.7 - 0.95 | 0.85 | 0.04 | 0.8 |
  | stone, brick | 0.6 - 0.95 | 0.8 | 0.04 | 1.3 |
  | metal | 0.25 - 0.6 | 0.4 | 0.6 | 0.4 |
  | wood | 0.55 - 0.85 | 0.7 | 0.04 | 0.7 |
  | glass | 0.05 - 0.2 | 0.1 | 0.04 | 0 |
  | water | 0.03 - 0.12 | 0.06 | 0.02 | 0.2 |
  | foliage | 0.6 - 0.85 | 0.75 | 0.04 | 0.6 |
  | ground (dirt, sand) | 0.8 - 1.0 | 0.92 | 0.04 | 1.0 |
  | fabric, skin | 0.7 - 0.95 | 0.85 | 0.04 | 0.5 |
  | tile (glazed) | 0.2 - 0.5 | 0.35 | 0.04 | 0.6 |

- **In the game.** With `rt_pbr=1` the Vulkan Gpu installs `engine/gfx/surface_maps.h`'s `SurfaceMaps`; render_pc's
  dictionary loader (`render_pc/surface_map_load`) reads `<file>.pbr` and attaches each map to its texture (matched by
  checksum and content hash). The map becomes an A8R8G8B8 texture with its mips made on the processor (normals averaged
  and renormalised, roughness widened where they spread - Toksvig: alpha^2 + (1 - |n|) / |n|) and is let go of with its
  texture. The dictionaries the game loads by name - levels' and models' (`load_tex_dict`, from an archive or a
  file) - are looked up; those handed over as memory (`load_tex_dict_from_memory`: the asset loader's) get none.
- **Which draws.** A draw whose program ends by multiplying with the vertex colour - a material's generated shader, the
  single-texture `kTextureVertexX4` / `kTextureConstantAlpha` - not pre-transformed, not points: with a map on its first
  texture it gets `kSurfaceLighting | kSurfaceMap` (+ `kSurfaceLit` when lit by lights with normals); without one, a
  lit draw with normals gets `kSurfaceLighting | kSurfaceLit` (objects lit per pixel; `rt_pbr_objects=0` leaves them).
  Everything else - and every draw with `rt_pbr=0` - keeps `surface` 0: its key's bytes, and so its shaders, are the
  same as without this work (all 50,180 shaders the ff check generates are byte-identical to main's).
- **The shader.** The vertex shader adds the camera-space normal and position and the lighting's parts that don't
  depend on the normal (Ce + Ca (Ga + sum La), and Cd). The fragment shader: the normal - the vertex's, or the
  triangle's own from the position's derivatives - bent by the map through a tangent frame made per pixel: the
  gradients of u and v along the surface from the screen derivatives of the position and stage 0's coordinates,
  (du/dx (dP/dy x N) + du/dy (N x dP/dx)) x sign(N . (dP/dx x dP/dy)), scaled by the longer of the two; no frame where
  the coordinates don't change (beyond rounding). Lit draws: Direct3D 9's diffuse formula per pixel with the draw's
  lights. The world: the bake kept, scaled by (a + N'.L) / (a + N.L) toward the sun where the bake is sunlit (its light
  level, luma of 2 x v0, from 0.35 to 0.8) and by (1.5 + N'.up) / (1.5 + N.up) at 0.5 elsewhere (the sky), the scale held to
  0.55 - 1.5; a = 0.45. The sun: the console's `rt sun`, else the bake's (R4's fit, with ray tracing), else the game's
  shown sun at half weight, else none (the sky's part alone). Then the program runs with that in place of v0, and GGX
  specular (Smith-Schlick, Schlick's Fresnel with the class's F0, pi x the BRDF x N.L - the convention in which the
  diffuse term is the light x N.L) is added x `rt_pbr_specular`, per light or toward the sun where sunlit.
- **Why per pixel and not per-mesh tangents.** The game's meshes have no tangents, share arenas, and the CPU rewrites
  the skinned ones each frame; a tangent stream would need a second vertex binding per draw kept in step with all of
  that. The derivative frame is the per-triangle (Lengyel) tangent frame exactly - what a MikkTSpace pass gives a flat
  triangle before averaging at the vertices - follows the skinning by itself and turns with a mirrored mapping; the
  check below compares it with the quad's own u / v gradients. The price: the frame is per triangle (a curved surface's
  map is lit facet by facet along its tangents; its normal stays the interpolated one).
- **How the effects meet it.** R4's shadows, R5's occlusion and the bounce are full-screen passes after the opaque
  world: they act per pixel on what the lighting wrote (the traced shadow darkens the bent highlights with the rest).
  R3's material table carries each material's map (`MaterialRecord::surface_texture`, its slot in the bindless array)
  with `material_has_surface()` / `material_surface_lod()` in `material_glsl()` - for hit shading, a normal-mapped
  bounce and rough reflections; GI doesn't read it yet. The record is 64 bytes a row: the water / glass work's
  `reflection_class` and `reflection_mask`, then `surface_texture` and `f0`.
- **One F0.** `MaterialRecord::f0` is the material's Fresnel reflectance face-on, decided in one place
  (`record_f0()`, vulkan_rt_materials): a material the reflections class as water (`rt::kReflectWater`) gets
  `rt::kReflectWaterF0` (0.02, engine/gfx/rt_scene.h) whatever its map says; any other its surface map's class's F0
  (0 without a map). The reflections pass reads water's F0 from the record (it had its own 0.02 constant); glass keeps
  `rt_glass_reflectance` (an art setting - the game's windows are reflective sheets, real glass's 0.04 showed nothing)
  and shiny surfaces their reflectivity-based curve. The maps' classes give water 0.02 too (tools/pbr_maps/classes.ts),
  so the per-pixel highlight (SurfaceUniforms' F0, from the map) and the reflections agree on a water texture.
- **The key's bytes with the upscaler.** `FragmentKey::surface` and `motion` took its two spare bytes; `VertexKey`
  had one spare, so it grew from 36 to 40 bytes (`motion`, `surface`, three spare - no padding). The varyings: the
  per-pixel lighting's at locations 7 - 10, the motion vectors' at 11 and 12; a draw can have both (its fragment shader
  writes `o_motion` at location 1 after the lit colour). With `rt_pbr=0` and `upscaler=off` the generated shaders are
  byte-identical to before.
- **Hooks for the other work streams.** The relit mode: `ff_key.h` `kSurfaceRelit` (reserved) and
  `SurfaceUniforms::relit` - a key with it keeps the per-pixel normal (`surface_n`, `surface_geo`), view (`surface_v`),
  roughness and specular, and replaces the bake's term where `surface_lighting()` builds `surface_v0`. Tessellation /
  displacement: the height is the map's alpha (0.5 the surface), bound at `kSurfaceMapBinding` (7) for a key with
  `kSurfaceMap` and in the material table (`surface_texture`); `surface_map_for(colour)` finds a texture's map.
- **Checked.** `thug2_vulkan_pbr_check` (28 checks; the RTX 5070 Ti and the AMD Radeon, validation and synchronization
  validation 0): a quad drawn with the game's material shader and a synthetic map (an 8 x 8 grid of tilted normals and
  stepped roughness, point-sampled) into a float target, each of ~12,000 - 15,500 clear-cut pixels against the
  processor's ray-cast of the same formulas with the quad's own tangent frame - lit facing the camera, turned 35 / 20
  degrees, u mirrored, the mapping turned 90 degrees, u and v mirrored, baked sunlit and in the shade, no normals in the
  vertices, constant coordinates (no frame), no map (lights per pixel): 100% within 0.003, the worst 0.0005 (16-bit
  floats); `rt_pbr` off: the original's shader against Direct3D 9's vertex lighting the same way; every pixel finite;
  the map changes a baked picture. `thug2_vulkan_rt_materials_check` (+2): a map in its own bindless slot, read through
  the material texel for texel, cleared when its texture goes. `thug2_ff_shader_check --surface`: every key that can
  take the bits (45,792 fragment keys more: 99,042 modules, 153,962 pipelines) generated, compiled, validated and
  made into pipelines - none refused, no validation message, on both GPUs. `thug2_format_check`: the file's reader and
  the TypeScript writer on one sample (the same bytes). `tools/pbr_maps/*.test.ts`: the hash, the file, the classical
  maps' orientation (+u, +v), the classes. In the game: `tools/game/rt_shadows_check.ts --effect pbr` - with the
  user's settings (every ray-traced effect on, 8x MSAA) 13 spots in foun, z_dt, BO, z_ho, sch and AU: mean luma change
  1.0 - 7.0 of 255, at most 0.05% turned black and 0.01% white, maps attached in every level; with a plain ini and
  main's build as the baseline, `rt_pbr=0` against main 0.00 - 0.01% (the noise). The sky's ratio was (1 + N'.up) /
  (1 + N.up) in those runs: foun's indoor bake (no sun) was darkened at the clamp (8.9% of foun-deck, median 0.55);
  with (1.5 + ...) foun, BO and z_dt again: foun-deck 0.9% changed (median 0.86), mean luma change 0.4 - 6.9.
- **Cost** (`thug2_vulkan_pbr_check --perf`: a full-screen 1920 x 1080 layer of the material shader with a map):
  RTX 5070 Ti 0.041 ms off, 0.056 lit, 0.052 baked; the AMD Radeon integrated GPU 0.21 ms off, 2.1 lit, 2.2 baked - the
  shader's arithmetic (frame, two normalisations, GGX) is ten times the original's there. The game's frame lines were
  too few to separate the difference from the noise (35 - 40 ms either way with everything on).

### Art style: the toon filter (`rt_style`)

`gpu_vulkan/vulkan_style` (`[Graphics] rt_style`, off by default; no ray tracing or HDR needed). A post-process that
makes the world look like a cartoon - painted colour fields, a few shades of light, dark ink lines - drawn by the last
listener at `rt::world_view_drawn` (`ViewOrder::kStyle`, 975: after the tone mapping, so it works on the display's 0 - 1,
and after the upscaler, so with DLSS / FSR it runs at the output's resolution); the 2D (HUD, menus, text) is drawn after
it and never filtered. Each viewport separately (split screen: nothing is read across its edge).

- **The passes** (vulkan_style.h has the formulas): the viewport's colour copied out (a multisampled target resolved
  first; the result written to every sample); the depth made linear (compute: the projection's w, R32F; the far value
  - nothing drawn, the sky - marked); the colour at half resolution (compute: 2 x 2 means), smoothed by a generalized
  Kuwahara filter (compute: the four overlapping quadrants of radius r, each's mean weighted by 1 / (1 + (255^2
  variance)^4) - texture detail flattens, edges between fields stay); then one full-screen triangle per pixel: the
  smoothed colour brought back up edge-aware (a joint bilateral upsample guided by the pixel's own colour), its OKLab
  lightness put in `rt_style_bands` soft-edged bands (30% of it kept as it was, so the darkest shades aren't crushed; hue kept, chroma x `rt_style_saturation`), the outline mixed in,
  the result mixed over the original by `rt_style_strength`.
- **The outlines** come from the linear depth alone: per axis, with the neighbours h pixels away, s = 1 - z (1/z_a +
  1/z_b) / 2 - zero on any plane however steeply it is seen (1/z is affine across a flat surface on the screen), positive
  where the pixel stands out toward the camera (the near side of a silhouette, a ridge), negative where it is recessed
  (a valley, the far side of a silhouette). |s| x f / h (f the focal length in pixels) is a crease's change of slope,
  independent of the resolution and the distance; smoothstepped, cut on the far side of a step (the line stays on the
  object in front - a silhouette's line is one-sided and h wide), creases at 75%, faded out from 2,500 to 9,000 units
  (distant geometry would turn into a mesh of lines), none on the sky. The ink: the toon colour's OKLab lightness x 0.2,
  chroma x 1.3 - dark and tinted by what it outlines, never pure black. h and r scale with the picture's height (given
  for 720 lines).
- **Depth surfaces** are made samplable whenever the format allows (`style_depth_usage`), so `rt_style` can be turned on
  from the Options screen with no restart; without a samplable depth there are no outlines (logged once).
- **Settings** (all live - `kLiveKeys` `rt_style*`): `rt_style` off / toon (a name since the other looks - below; the old
  0 / 1 still read), `rt_style_strength` (1), `rt_style_smooth` (4:
  the Kuwahara radius in 720-line pixels; 0 none), `rt_style_bands` (5; 0 none), `rt_style_outlines` (1; 0 none),
  `rt_style_outline_width` (1), `rt_style_saturation` (1.15). The Options screen's Colour tab: Art style (its nine
  names) and the knobs.
- **Checked:** `thug2_vulkan_style_check` (72 checks, the RTX 5070 Ti and the integrated Radeon, validation with
  synchronization validation 0): a 256 x 128 view drawn with screen-space quads at known depths (a box against the sky,
  a tilted floor, a ridge) and filled with noisy fields, a grey gradient and NaN / infinities. The Kuwahara pass's
  output (read back) within a step of `style_kuwahara`'s (largest 0.0024); the composite against `style_reference` fed
  the pass's own smoothed colour and linear depth: every pixel within 2.5 steps (largest 0.12 - 0.56); against the twin
  alone at most 2% past (the smoothing's 16-bit rounding crossing a band's soft edge: 0 - 1.2% here); finite, 0 - 1,
  alpha kept; the linear depth (the box at 200.00, the sky marked); the smoothing calms the noise (spread 0.087 ->
  0.020), the bands make plateaus (226 of 255 neighbours equal), the outline lies along the box's edges on the box,
  none on the sky beside it, down the ridge's crease, none inside the planes; a viewport changes nothing outside it bit
  for bit; `rt_style=0` and strength 0 record no pass; an 8-bit target; four samples (the resolve filtered, written to
  every sample). Cost (2 compute passes at half resolution, 1 at the depth's, a full-screen triangle): 0.15 ms at
  1920 x 1080, 0.34 ms at 2560 x 1440 on the RTX 5070 Ti; 6.2 / 15.4 ms on the integrated Radeon (the Kuwahara loop).
- **In the game** (the user's settings - ray tracing, HDR, DLSS Ray Reconstruction at 67%, 2560 x 1440 - with
  `rt_style=1`): BO, AU, BA, z_dt and the front end's main menu and Options screen before / after; the HUD, the menus'
  text and the score stay sharp, the world reads as painted fields with soft-edged shade bands and dark ink round
  objects, rails, steps and the skater. The passes cost 0.48 - 0.51 ms a frame on the RTX 5070 Ti (the log's
  "rt_style: the passes take"; 0.82 ms with smoothing 6 and lines 2). Known: alpha-tested decals a little above the
  ground (shadow and stain decals) get outlines of their own (a real depth step); a low-poly hill's facets show faint
  crease lines.

### Art styles: comic, sketch, noir, thermal

`rt_style` is a name: `off | toon | cel | comic | sketch | vhs | retro | watercolour | noir | thermal` (`ArtStyle` in
gpu_vulkan.h; `parse_art_style` takes any case and the old 0 / 1). `cel` is no post-process: it is cel shading, the
rendering mode ("Cel shading" below - `style_has_pass` is false for it and off, so the listener isn't added). `vhs`,
`retro` and `watercolour` are "Art styles: vhs, retro, watercolour" below. The Options screen has them all in its Style
tab: `rt_style` first, then every style's knobs (`rt_style_*`, `rt_cel_*`), each listed only while its style is picked
(`shown_table`). The four looks here (`gpu_vulkan/vulkan_style_looks`, formulas in its header) reuse the toon filter's passes and
listener - the viewport's colour copied out (resolved), the linear depth, the half-resolution colour and its Kuwahara
smoothing - and add a composite of their own (a fragment pipeline per look, format and samples), so they share its
place (`ViewOrder::kStyle`, after the tone mapping and the upscaler, before the 2D), its per-viewport handling and its
independence from ray tracing, HDR and DLSS / FSR. Patterns (halftone, hatching, paper fibre, grain) are anchored to the
viewport's pixels and scale with its height (a "cell" is height / 720 pixels); none of them follows the camera, so
nothing shimmers - the paper and the hatching are the same every frame, the noir grain and thermal noise change with
the frame's seed (the frame serial).

- **Ink** (comic, sketch): an edge pass (compute, R32F at the depth's size) writes the toon outline's edge per depth
  pixel (`style_edges` is its twin); the composite widens it: the largest e x clamp(e (k + 1) - d, 0, 1) over the depth
  pixels within k + 0.5 (a table of offsets sorted by distance, `kInkCount[k]` of them) - a strong edge reaches k
  pixels, a weak crease fewer, so lines taper where an edge fades and are never darker than their own edge. k =
  rt_style_outline_width x 2 (comic) or x 0.5 (sketch) at 720 lines, scaled with the depth's height, at most 4.
- **comic**: the smoothed colour's OKLab lightness in `rt_style_bands` hard-edged bands, lifted (0.12 + 0.88 L: printed
  colour is never black), chroma x `rt_style_saturation` x 1.2; a halftone screen at 45 degrees (`rt_style_comic_halftone`
  spacing) whose dots grow with the darkness below L 0.62, in a darker shade of the fill; paper tint and fibre
  (`rt_style_paper`, `rt_style_grain`); near-black ink.
- **sketch**: the smoothed lightness as a tone; four hatching layers (55, 145, 10, 100 degrees) fade in below 0.72, 0.52,
  0.34, 0.18, strokes `rt_style_sketch_hatch` apart, thicker where darker, with pressure and gaps along them; each layer
  bent by a slow noise (the strokes shifted across by up to a spacing over ~100-pixel regions - a turn of the whole frame
  about the corner would swirl); grainy graphite outlines; cream paper with fibre; `rt_style_sketch_wash` of a pale copy
  of the colour under the pencil (0: grey).
- **noir**: the lightness through a tone curve - `rt_style_noir_contrast` about a middle moved 60% of the way from 0.5
  to the mean lightness around the pixel (the half-resolution colour at it and on a ring of 40 720-line pixels: a dark
  corner keeps its mid-tones, a sunlit square its own - a local dodge and burn), then smoothstep, crushing the blacks;
  film grain per cell and frame, strongest in the mid-tones (`rt_style_grain`); a vignette (`rt_style_vignette`); the
  result mixes `rt_style_noir_dark` to `rt_style_noir_light` (default black to warm paper; Teal + Orange in Options is
  the orange / teal pair).
- **thermal**: heat = 0.1 + 0.38 brightness (the half-resolution colour brought up bilinearly - the slight blur) + 0.15
  nearness (150 - 3,000 units) + 0.6 x `rt_style_thermal_focus` x "stands in front"; the sky 0.04 + 0.12 brightness.
  "Stands in front" needs no draw ids: four pairs of opposite depth samples on a ring of 24 720-line pixels and one three
  times as wide, per pair the lesser of 1 - z / z_sample (both sides farther; the sky counts 1), the most of them,
  smoothstepped 0.05 - 0.3. On any plane one side of each pair is nearer (0: floors, walls, ramps), beside a ledge's edge
  the ground is on one side (0), while a skater, a ped, a car, a rail or a pole has background on both sides (a figure up
  to ~70 pixels wide at 720 lines glows all over through the wide ring). Sensor noise per cell, per line and per frame;
  a mild vignette; the palette black - blue - purple - red - orange - yellow - white.
- **Settings** (all live): `rt_style_comic_halftone` (5), `rt_style_sketch_hatch` (5), `rt_style_sketch_wash` (0.35),
  `rt_style_paper` (0.5, comic and sketch), `rt_style_grain` (0.5: noir, thermal, the papers), `rt_style_vignette` (0.5:
  noir, thermal), `rt_style_noir_contrast` (1.8), `rt_style_noir_dark` (0,0,0), `rt_style_noir_light` (0.95,0.92,0.84),
  `rt_style_thermal_focus` (0.6); the toon's smooth / bands / outlines / width / saturation / strength apply where they
  fit (README). Options: the Colour tab's rows for each, each listed only under the looks that use it (the
  shown-when rules in frontend/new_frontend_options.cpp). The push constants grew to 192 bytes (four look vectors; the device's limit checked).
- **Checked:** `thug2_vulkan_style_check` (133 checks, the RTX 5070 Ti and the integrated Radeon, validation with
  synchronization validation 0). Each look against its processor twin (`style_look_reference`, fed the pass's own
  smoothed colour and linear depth, at the frame's seed - `style_last_parameters`): every pixel within 2.5 steps (0
  past; the largest 0.12 - 0.22 steps), finite, 0 - 1, alpha kept; the edge pass as `style_edges` (exact). What each
  does: comic and sketch ink along the box's edges and down the ridge, none on the sky beside it or inside a plane;
  halftone dots over a third of the mid-dark floor and box, paper between them; comic's fills flattened (spread 0.087 ->
  0.020); sketch's strokes over the gradient's dark end (spread 0.12), bare paper at its light end, pencil grey with no
  wash; both identical frame to frame. Noir: every pixel on the duotone's line, the dark quarter crushed (0.008), the
  light kept (0.72), the grain moving with the frame (each frame as its seed's twin), teal shadows / orange lights with
  that pair. Thermal: a figure in front of a wall warmer all over (the twin on a depth of its own), the wall and a
  floor not; the sky cold; the noise moving with the frame. Each look in a viewport changes nothing outside it; vhs /
  retro / watercolour record no pass; the names read back. Cost at 2560 x 1440 on the RTX 5070 Ti (the fastest of 16
  short rounds - other programs share the device): toon 0.46 - 0.48 ms, comic 0.63 - 1.2, sketch 0.57 - 1.0, noir 0.07,
  thermal 0.14 - 0.26 (all under the 1.5 ms the check holds a discrete device to); 1920 x 1080: 0.04 - 0.3.
- **In the game** (THUG2-6, the user's [Graphics] - ray tracing, HDR, DLSS Ray Reconstruction - in a 1280 x 720 window;
  BO's yard and its upper deck, AU's beach steps and glass front, z_dt's street and the skater on the steps, the front
  end's main menu and Options, the pause menu over z_dt): each reads as its own look - comic as a coloured comic panel
  (bold ink round the skater, rails and ramps, dots in the shade), sketch as a pencil drawing (cross-hatched shade,
  graphite outlines, the sunlit beach left as paper), noir as a hard black-and-white still (the skater kept out of the
  dark by the local middle), thermal as a heat camera (the skater, rails, lamp posts and peds glowing yellow-orange over
  purple ground, the sky dark blue). The HUD, the menus and their text are never filtered. The log's "rt_style: the
  passes take" at 1280 x 720: comic 0.19 - 0.49 ms, sketch 0.39 - 0.50, noir 0.07 - 0.17, thermal 0.11 - 0.21. Known:
  thermal's "stands in front" is at the depth's resolution (with DLSS the edges of a glowing figure are stepped), and a
  rooftop's thin structures against the sky can glow; noir's local middle leaves a faint halo where a very bright and a
  very dark area meet; the Options screen shows Off for an ini that still says rt_style=1 (the game reads it as toon).

### Grass (`grass`)

`engine/gfx/grass.h` (the field and the `Sink` contract), `render_pc/grass_world` (the grass found), `gpu_vulkan/vulkan_grass`
(the pass) - `[Graphics] grass` (off by default), `grass_density`, `grass_height`, `grass_distance`, all live. No ray
tracing needed; Direct3D 9 installs no `Sink` and draws none.

- **Where the grass is.** The collision's terrain types - the ones the skater's wheels sound on
  (`engine/sound/surface_kind.h`): a face whose terrain is `TERRAIN_GRASS` or `TERRAIN_GRASSDRIED` (their indices looked
  up in the loaded terrain table, the shipped order 20 / 21 when it isn't loaded), facing up (normal y at least 0.7),
  collidable and not a trigger. The shipped levels' upward grass in square metres: ST 9,055 (+1,033 dried), PH 4,218,
  SC 3,951, AU 2,265, TR 2,056 dried, BE 1,561, LA 1,141, BO 914 (the common's three lawns - their render textures are
  the olive grass ones), BA 716; NO and SE have none. No texture heuristic is used: in BO, AU and BA the lawns the
  player sees are the grass terrain (a top-down map of BO's collision by terrain beside one of its render textures'
  mean colours), so the terrain alone decides. A level with none logs "no grass in this world's collision".
- **The field** (`grass::make_field`, made on the processor once a world - the loaded scenes' key changed - or when the
  density changes; Boston 30 - 50 ms, Skatopia 0.6 s, once at the load): each grass face gets area x 0.155 blades a square unit x density
  (about 240 a square metre), placed by hashes of its corners' bits and the blade's number (a uniform point on the
  triangle) - the same field every run, whatever order the sectors load in. A root is dropped when any other face covers
  it seen from above from 0.5 to 120 units over it (a ramp, a box, a bench, a rail, a path's kerb laid over the lawn):
  grass never grows through them; coplanar ground doesn't count. Roots are bucketed into 256-unit tiles, each tile's in
  the order of a rank hash. A blade is 16 bytes (its root; rank, height, facing and shade hashes, a dried flag).
- **The pass** at `opaque_drawn`, `ViewOrder::kGrass` (450: after the opaque world, the objects and the sky, and after
  the ray-traced shadows, occlusion, bounce and reflections have shaded the ground; before the fog and the tone mapping
  at `world_drawn`, which take the blades with the rest; never in the BLAS). The blades take the shaded ground's colour
  at their roots - each is in the shadow (and the occlusion) its root is in. The first try ran the pass before the
  shadows (90), so they would shade the blades from the depth the blades write: with the user's settings in BO every
  blade came out near black - a blade is a few pixels wide, and the normals the screen-space passes rebuild from its
  depth are noise (occlusion at radius 192 and strength 1 darkened them most). Per view (vulkan_grass.h has the formulas): the tiles nearer than the
  distance and in the view's frustum chosen on the processor, each drawing its first blades - those whose rank is below
  the share kept at its nearest point (1 out to a fifth of the distance, then (near / d)^1.5), with 3, 2 or 1 segments;
  at most 1.5 million blades a view (the share lowered for all beyond). The viewport's colour is copied out (resolved
  when multisampled); a compute pass puts every chosen blade's root on the screen and, where the depth there is the
  root's own, keeps the colour there in a buffer of the blades' colours (so a root hidden this frame - under the skater -
  keeps the last colour seen; a blade never seen isn't drawn and grows in over three sightings). Then the blades are
  drawn as triangle strips, one instance each, depth-tested into the colour and depth (and the upscaler's motion
  attachment when the view is upscaled - the camera's motion; the wind's sway isn't in it).
- **A blade** (`grass_blade_shape` is the vertex shader's twin): 6 x grass_height x its own 0.55 - 1.45 units tall,
  times the fade (it sinks into the lawn from 70% of the distance to all of it, where its colour is the lawn's), its
  growth through the rank band, and how well its colour is known; bent by its own lean, the wind (two slow waves over
  the ground, downwind) and each pusher within its reach - the skaters and peds (render_pc's instances drawn with bones,
  the eight nearest the camera; 30 units, within 72 units of height), which bend it straight away - the tip as high as
  its length allows; turned 60% of the way to face the camera; 0.45 units wide at the root (never under 0.9 pixels:
  distant blades get wider as they get fewer), to nothing at the tip, along a quadratic curve. Its colour is the
  ground's under it - the texture, the bake's light and shadow (dark corners stay dark), the per-pixel lighting when
  `rt_pbr`, the traced shadow, occlusion and bounce - times 0.75 at the root (the contact darkening; 0.62 read as dark
  specks from above) to 1.12 at the tip, +-10% per blade.
- **Checked.** `thug2_vulkan_grass_check` (26 checks; the RTX 5070 Ti and the integrated AMD Radeon, validation with
  synchronization validation 0): a 400 x 400 lawn of grass faces on a coplanar ground face, a box standing on it, a steep
  grass bank and a dried patch - the field: the same 25,361 blades byte for byte from the same faces, the count within
  3% of the area x the density (25,361 for 25,358), the box's 989 roots dropped (992 expected), every root on a lawn face,
  none under the box or on the bank, the dried ones flagged, each tile in rank order; the pass over a ground drawn in the
  view's depth (its left half green, its right half straw): only pixels whose ray meets the lawn below the tallest blade
  and outside the box's footprint change (0 stray), 98% of the lawn's inside covered after three frames, the blades green
  over green (0.21 0.53 0.16) and straw over straw (0.53 0.46 0.11), the same picture from a new field, every pixel and
  colour finite with not-a-number and infinities poured into the ground under the roots; every one of the 25,361 blades'
  tip and height on the device within 0.0001 units of the twin; the share kept and the height falling with the distance
  to nothing at it, no blade tall and nothing drawn from beyond it, every blade full height near the camera, fewer blades
  drawn from further away (19,140 against 5,235); the 237 blades within half a skater's reach bent straight away from
  him, further than without him; four samples; off: no pass, no sink. Cost (copy, gather and draw, the defaults, a lawn
  filling a 2560 x 1440 view from a skater's eye height - 161,107 blades in 64 draws): 0.18 - 0.26 ms on the RTX 5070 Ti,
  3.7 - 4.7 ms on the integrated Radeon. `thug2_options_check` has the four keys.
- **In the game** (the user's [Graphics]: ray tracing with every effect, HDR, DLSS Ray Reconstruction at 67%, the
  relit levels and per-pixel lighting, 2560 x 1440): BO's common - 80 grass faces, 914 square metres, 204,707 blades in
  42 tiles (4.7 MB on the device, made in 30 - 50 ms when the level loads, 14,782 roots under something dropped) -
  reads as a short lawn at the skater's height and from above, the trees' traced shadows lying across it, none on the
  paths, the steps or the statue's plinth; ST (Skatopia): 9,922 square metres of grass and dried grass, 2,066,269 blades
  (47 MB, 582 ms at the load), a meadow with the wooden steps standing in it bare. The pass's own timing in the log:
  0.05 - 0.27 ms on the device (40,000 - 77,000 blades a pass at 1707 x 960, DLSS's input); `[debug] frame_profile`'s
  `grass` phase (the processor: the pushers each frame) 0.02 - 0.03 ms. Frame-time runs on a machine shared with other
  workspaces' games were too noisy to see the pass in the frame (BO 34.9 -> 26.6 fps, ST 20.5 -> 23.7 fps, one run
  each). Known: the blades' motion vectors carry the camera's motion, not the wind's sway (DLSS sees the sway as
  change); the grass is only where the collision says grass - a lawn painted on a face of another terrain gets none.

### Cel shading (`rt_cel`)

`engine/gfx/rt_cel` (the settings, the stream's layout, the bands' formulas and their twins on the processor),
`gpu_vulkan/vulkan_cel` (which draws are banded, their uniforms, the world's outlines), the fixed-function generator
(`ff_key.h` `kSurfaceCel*`, `ff_shaders.cpp` "the light in bands"), `render_pc/rt_relit_world` (the stream);
`[Graphics] rt_style=cel` (an art style in the Options screen's list; any other value turns it off - an older ini's
`rt_cel=1` still turns it on while `rt_style` is off), `rt_cel_bands`, `rt_cel_shadow_colour`, `rt_cel_rim`,
`rt_cel_outline_width`, `rt_cel_flatten`. A rendering mode, not a filter: the light is put in bands in the draws' own
fragment shaders, where it is worked out; as the art style `cel` it has no post-process of its own.

- **The relit world** (`rt_relit=1`): relit already knows each vertex's parts. With `rt_cel` on it also writes a second
  vertex stream per world mesh (`cel_stream_words`: three D3DCOLOR words a vertex - the sky's and bounce's light and
  N.L toward the relit sun; the tint x the level's scale and the vertex's share of the sun's rays; the lamps' residual
  and a flag), a GpuVertexBuffer render_pc names per draw (`Gpu::CelSource`, set in `MeshRef`'s draw beside the motion
  source); the Vulkan Gpu binds it at binding 2 (`kCelLocation` 13 - 15). Each pixel:
  `tint x (ambient_q x mix(shadow colour, 1, q) + sun x q) + residual_q` - q the sun's term `N.L x visibility` in
  `rt_cel_bands - 1` steps (the step a little early: + 0.15), ambient_q the sky's and bounce's light in whole stops,
  residual_q the lamps' light in steps of 0.25 (hue kept in both), `sun` the relit model's sun light (a x the sun's
  colour, published by render_pc: `rt::set_cel_world`). A vertex the game animates carries the flag 0: its pixels are
  banded as the bake.
- **Hard shadows.** With ray tracing the visibility is a ray query in the fragment shader (`kSurfaceCelShadow`; GLSL 4.60,
  SPIR-V 1.4; the frame's TLAS at `kCelSceneBinding` 9, only in a ray-tracing device's layout - the vulkan_rt_scene
  builder hands it over as it does the shadows'): from the pixel's world point (`eye_to_world`, the view's inverse) 2
  units off the surface along its face normal and 1 toward the sun, the level's opaque geometry (mask 0x01), terminated
  on the first hit - no penumbra. Rays only where the sun's term could reach the first band. The stream's N.L excludes
  the vertex's own share then (which is the fallback without ray tracing), and the ray-traced shadows' pass steps aside
  (`cel_owns_shadows`): the shadow band is the shadow. The skater's own projected shadow comes back with it.
- **Without relit** (or a level that keeps its bake): the vertex colour's luminance in steps of `2 / (bands - 1)` stops,
  its hue kept, the shadow colour over the steps below 0.3 - 0.55 of the bake's light.
- **Objects** (lit by lights with normals: the skater, peds, cars): each light's N.L per pixel in bands (the key light's
  - the first - times the traced visibility), the shadow colour on the ambient part where the key light is off, and a
  rim band where `1 - N.V` passes 0.62 (`rt_cel_rim` x (the key light + 0.3), dimmer in the shadow band).
- **Every band edge** is anti-aliased over about a pixel: the band's position's screen derivative (`fwidth`) x 0.75 as
  the smoothstep's half width; every derivative is taken in uniform control flow (before any branch on a pixel's
  value).
- **Outlines.** Objects: an inverted hull - each lit draw that doesn't blend (or blends with an alpha test: the skater's and peds' meshes) is drawn again (`kSurfaceCelHull`: the
  vertices pushed out along their normals by 2.5 x `rt_cel_outline_width` pixels of a 720-line picture, measured on the
  screen; the faces toward the camera culled; the program's colour turned into ink). The world: a pass after the
  upscaler (`ViewOrder::kCel` = 960, world_drawn, before the toon filter): the toon filter's measure of a bend or step
  in the linear depth with the neighbours `rt_cel_outline_width` depth pixels away (one-sided: the line on the object
  in front), creases at 60%, faded out from 2,500 to 9,000 units, a depth step under 0.6 units + 0.1% of the distance
  ignored (decals just off their walls). Ink: the surface's own colour, luminance x 0.28,
  chroma x 1.25 (`rt::cel_ink`).
- **Flatter textures** (`rt_cel_flatten`): each sampled texture's colour mixed by the setting toward the same texture
  2.5 mips blurrier; its alpha untouched (cut-outs keep their edges). The edge-preserving filter done once per texture
  was not needed for the look and isn't done.
- **The sky** (`rt_sky`): the composite's `CEL` variant - the atmosphere in 6 flat bands of elevation (narrower low
  down; the colour at each band's middle, its azimuth kept) and its brightness in half stops (the sun's glow in rings);
  the clouds' cover with a crisp edge at 0.3 and their own colour in two-thirds-of-a-stop steps.
- **Steps aside:** the ambient occlusion and the one-bounce light passes while the mode is on (their soft gradients
  are what it takes away; relit's own bounce is in the ambient), the ray-traced shadows' pass while the draws trace.
- **Settings** (all live - `kLiveKeys` `rt_cel*`; the stream is rewritten when they change): `rt_cel` 0 / 1,
  `rt_cel_bands` (3; 2 - 4), `rt_cel_shadow_colour` (0.62,0.66,0.86 - `r,g,b` or `#rrggbb`), `rt_cel_rim` (0.5),
  `rt_cel_outline_width` (1; 0 none), `rt_cel_flatten` (0.5). The Options screen's Lighting tab: Cel shading and its
  five knobs.
- **rt_cel=0:** no key gets the bits (`cel_draw` returns none), so every generated shader is byte-identical to before
  (`thug2_ff_shader_check --surface --motion --dump` against main's: all 197,992 generated shaders byte-identical); the descriptor layout gained the
  cel block's binding (8) and, on a ray-tracing device, the scene's (9), pushed only for cel draws.
- **Checked:** `thug2_vulkan_cel_check` (24 checks; the RTX 5070 Ti and the AMD Radeon, validation with synchronization
  validation 0): the twins (the colour's text, `bands` levels, the stream's words); the stream's bands of a lit
  gradient for 2, 3 and 4 bands - every pixel away from an edge the processor's twin (worst 0.0004), as many plateaus
  as bands, each edge at most two pixels, the shadow band's blue / red the colour's (1.388 / 1.387); the bake banded
  (100% the twin's); an object lit by a light (100%, 3 plateaus) and its rim (every pixel past the threshold brighter,
  the rest unchanged); hard shadows - a ground whose vertices say "no sun" lit only by the traced visibility under a
  box in this check's own acceleration structures: 22,024 pixels lit or in the shadow band exactly as the processor's
  ray says, none between, the edge's two pixels left out; a lit cube's hull (97% of the ring two pixels round its
  silhouette ink, nothing further out or on the cube changed); the world's lines (on a box's edge on the box, none
  inside, on the wall or the sky); flatten 0.8 (a checkerboard's spread 0.30 -> 0.11); `rt_cel=0` the same picture as
  before the mode was ever on and the original's formula; every pixel finite. `thug2_ff_shader_check --cel`: 68,680
  fragment keys in bands (25,950 with the stream) - 127,977 modules compiled, validated and accepted by the driver,
  4,000 sampled pipelines made, none refused, validation 0.
  Cost (`--perf`, device timestamps, a full-screen layer at 2560 x 1440): RTX 5070 Ti - the bands +0.07 ms, the traced
  visibility +0.68 ms (one ray a pixel), objects with the rim and the hull 0.52 ms, the world's lines +0.20 ms; the AMD
  integrated GPU 2.5 / 2.2 / 4.9 / 3.4 ms. In the game the world's lines measured 0.42 ms at 2560 x 1440 (the log's
  "the outlines' pass takes"), and the occlusion and bounce passes it switches off cost more than that.
- **In the game** (THUG2-5, the user's `[Graphics]` - every ray-traced effect, relit by the game's sun, HDR, DLSS Ray
  Reconstruction, 2560 x 1440 - with `rt_cel` 0 / 1): BO, AU, BA, z_dt. Hard traced shadows with the cool shadow band,
  flat lit faces, ink round the skater and along the level's silhouettes, the HUD untouched. Known: a low-poly hill's
  triangles show as faint crease lines and their per-vertex N.L as faceted band edges; alpha-tested foliage gets ink
  round its cut-outs; the per-vertex N.L of a large flat-shaded wall makes its terminator follow the triangles.

### Water (`rt_water`)

`gpu_vulkan/vulkan_water` and `water_shader` (`[Graphics] rt_water`, off by default; no ray tracing or HDR needed;
Direct3D 9 ignores it). The game's water is flat textured sheets - scrolling coordinates, opaque or alpha-blended,
sometimes over a mirrored copy of the level drawn under it. With `rt_water=1` the water surfaces are shaded as water in
their own draw: waves, a reflection, refraction, colour by depth, shoreline foam and the sun's glitter.

- **Which surfaces are water.** render_pc marks the material it hands in (`MaterialRef::hand_in` ->
  `render_pc/water_materials.cpp`, through `engine/gfx/water_surfaces.h`): a material whose passes move - a UV wibble
  (pass flag 0x1, or a pass's oscillator) or a texture animation (0x800) - and whose first pass has a texture and is
  drawn as itself or blended by alpha (blend modes 0, 5, 6, 11; not added, subtracted or multiplied: glows, shadows,
  light pools). Every later draw with that texture on stage 0 is water until the next material. The shader narrows it
  per pixel: only where the surface faces up (the triangle's normal from the position's derivatives, y above
  0.82 - 0.94: not waterfalls, flags, scrolling signs), and only where the texture's mean colour (its smallest level)
  looks like water's - not warm (red over blue by 0.04 - 0.12: lava, foliage, sunlight pools), not near white (luma
  above 0.58 - 0.68: foam and caustic sheets drawn over the water) nor near black (below 0.06 - 0.12: shadows).
  `node tools/game/water_survey.ts` applies the same rules to the levels' files: of the moving materials, AP 6 of 22,
  AU 10 / 21 (the sea E51F4A45, pools), BA 30 / 75 (the sea CDC12099 / BAC6100F, the harbour 53A5B53A / B3705C24,
  fountains), BE 36 / 63 (the canal E06BBC98, fountains), BO 9 / 50 (the harbour 669D49A1), NO 21 / 58 (the river
  6228C216, F5DFC91E), SE 22 / 113, ST 20 / 65, TR 5 / 16... `[debug] rt_water_materials=1` logs each moving material
  the game draws, water or not, with its passes.
- **The key.** A water draw's fragment key gets `FragmentKey::water` = 1 (ff_key.h: a byte of its own, cel shading
  took the surface byte's last bits; on the programs the per-pixel lighting takes: kMaterial, kTextureVertexX4,
  kTextureConstantAlpha) and its vertex half
  `surface` = 1 (the camera-space position handed on). The water's GLSL runs after the program's colour is clamped:
  it returns the shaded colour, keeps the program's alpha for the alpha test and sets the written alpha afterwards. A
  key without it is generated exactly as before (`rt_water=0`: no key has it; `thug2_ff_shader_check` compiles
  the same 50,180 shaders). Five push-descriptor bindings (10 - 14, after cel shading's 8 and 9: the block, the copy's colour and linear depth, the
  sky capture, the ripples) are added to the shared set layout and pushed only for water draws.
- **The copy behind the water.** At a view's first water draw the colour target (resolved when multisampled) and its
  depth made linear (a compute pass: the projection's w, 1e9 where nothing was drawn) are copied over the view's
  rectangle; every water draw of the view reads them. An opaque water draw before the view's opaque world is finished
  (the sea is often among the level's first meshes) is kept back - its state block and buffers live for the frame - and
  drawn at `rt::opaque_view_drawn` (a listener at 450, after the ray-traced effects), so the copy has the whole opaque
  world without the water itself: its bed and what it reflects. (The ray-traced shadows, occlusion and bounce light at
  that point see the bed, not the water.) Transparent water draws after it use the same copy, or make it.
- **1. Waves.** Four Gerstner waves about the wind's heading (the swell: wavelengths 1, 0.63, 0.41, 0.27 x the
  level's longest, steepness 0.12 - 0.07 x the level's height x `rt_water_waves`, crests sharpened with Q = 0.6, deep
  water's speed sqrt(g / k) with g = 386 units / s^2 - the units are inches), each faded out where a pixel spans 5 -
  20% of its wavelength; and two layers of a tiling ripple texture drifting with the wind (`rt_water_wind`: their size
  and speed). The texture (256 x 256, RGBA16F: slope x, slope z, height, the foam's pattern) is made on the device at
  the first water draw, each of its 9 levels by a compute pass: 64 sine waves of whole cycles across the tile (2 - 24,
  within 63 degrees of the wind's axis, amplitude |k|^-1.7, phases from a PCG hash) with each wave faded out between
  half and all of the level's Nyquist limit - band-limited mip levels, so far water flattens instead of shimmering;
  the slope scaled to an RMS of 0.22. Everything moves with `ViewDrawn::game_seconds`: it pauses with the game, and a
  replay or an export draws the same frames.
- **2. The reflection** along the wave normal - with the waves' tilt lessened at grazing angles (x 0.3 - 1 by how much
  the surface faces the eye: a pixel there spans many facets, so the reflection stretches along the water instead of
  breaking up) - weighted by Fresnel (Schlick, water's F0 0.02; its grazing value held below 1 by the waves' slope: rough
  water never mirrors fully): a ray marched over the copy's depth in camera space (22 steps growing x 1.32 from 1.5% of
  the surface's distance, the crossing halved 5 times, faded toward the view's edges; none where it leaves the view or
  passes behind something), else the sky - as the copy shows it toward that direction (where nothing was drawn), else as
  the level's sky was captured (`rt::sky_capture`: the sky scene drawn into 3 x 2 faces once per sky scene, asked for
  while `rt_water` is on), else the fog's colour.
- **3. Refraction.** The copy read at the pixel moved by the waves' tilt (in camera space) x the water behind it (up to
  300 units) x the focal length / the distance x 0.12 - none at the shore; not moved onto something in front of the
  water.
- **4. Colour by depth.** The light's path through the water (from the surface to what is behind, along the view) and
  down to it absorbed per channel: exp(-sigma d), sigma = 3 / visibility x (0.2 + 1.6 (1 - hue)), the hue the tint over
  its largest channel (`rt_water_tint`: auto - the game's own colour at the pixel without the texture's detail: the
  texture's mean through what the program did to its texel, so a level's painted shallows keep their colour - or r,g,b) - red absorbed first, so
  shallow water shows its bed nearly clear and slightly green-blue; refracted = bed x through x down + deep x (1 -
  through), deep = hue^2 scaled to 0.65 x the tint's brightness at the light the game gave the surface (the program's
  colour over its texel: the bake, the pass colours). Visibility is the level's x `rt_water_clarity`. The texture's own
  detail is kept faintly (x its texel over its mean, 35%).
- **5. Foam** where the water is shallow - within 24 units of its bed below (the screen-space depth difference: a
  beach, and the waterline round anything standing in the water) - in the ripples' drifting foam pattern,
  x `rt_water_foam`; white at the surface's light.
- **6. The sun's glitter.** Normalised Blinn-Phong (power 1500, x 190) of the scene's sun (`rt::scene_sun`, else the
  sun the game shows) on the wave normals, Fresnel at the half vector, where the bake says the surface is sunlit (the
  vertex colour's luma 0.3 - 0.6); at most 6 with `rt_hdr` (the bloom takes it up), 1 without.
- **Alpha.** Where the game drew any water (alpha above a quarter) the written alpha is 1 - what is behind is in the
  colour already; where it drew none it stays as it was. Stacked water sheets of different materials each replace what
  is under them, so the upper one's edge shows (a seam in NO's river).
- **Levels** (water_materials.cpp's table, by the scene's name; `[Graphics] rt_water_wind_heading` overrides the wind's
  way): AU the open sea (waves 1, swell 900 units, visibility 900), BA a harbour (0.4, 520, 600), BE a canal (0.15, 300,
  220), NO a river (0.22, 350, 180), SE (0.5, 600, 450); other levels 0.4, 500, 450.
- **Settings:** `rt_water`, `rt_water_waves`, `rt_water_wind`, `rt_water_wind_heading`, `rt_water_clarity`,
  `rt_water_foam`, `rt_water_tint`, `rt_water_screen_reflections` - all live (the Options screen's Surfaces tab, the
  knobs shown only while `rt_water` is on). `[debug] rt_water_view=1` colours what the reflection found (red the
  march, green the sky on the screen, blue the captured sky).
- **Cost:** `thug2_vulkan_water_check --perf`, a frame of full-screen water at 2560 x 1440 (the sand and the water):
  0.25 - 0.36 ms off, 0.61 - 0.90 ms on - 0.36 - 0.54 ms for the copy and the shading of every pixel (another game ran on
  the device in the higher run). In the game (`water_check.ts`, the user's settings at 2560 x 1440 - DLSS quality, every
  ray-traced effect -, the frame profile's device time over 20 s held on the level's first spot, two runs each): BA off
  31.7 / 26.1 ms, on 31.5 / 24.1; NO off 15.1 / 11.4, on 12.2 / 12.8 - the water's cost is inside the runs' own spread
  (5 ms between two runs of the same setting).
- **Checked:** `thug2_vulkan_water_check` (22 checks, both GPUs here, validation with synchronization 0): a synthetic
  shore drawn through `render::Gpu` with the game's single-texture material shader - calm water against the processor at
  33,600 pixels (refraction, absorption, the deep colour, Fresnel toward the sky; 99.9% within 0.004); a red wall
  standing on the water mirrored where its mirror image is on the screen (2,800 pixels, the sky at 20,800; all within
  0.01 of the processor); `rt_water=0` and an unmarked draw the same bytes as each other and the water the program's
  colour; the sand in front of the water the same bytes with it on; foam only where shallow (and brighter there); waves
  that move with the clock and repeat with it; the sun's glitter above 1 at the mirror direction and nowhere on the
  sand; the ripple texture's levels 0 and 4 against `water_waves_reference` (0.0005); opaque water kept back and drawn
  after the opaque world; no NaN. In the game `node tools/game/water_check.ts run` (five levels, `rt_water` off and on
  over the user's settings, pictures and the frame profile's device time).
- **Not done:** ray-traced reflection rays from the water (with ray tracing the reflection is still the screen's and the
  sky's - a ray query in the water's shader would need the TLAS, the geometry table and the material set bound to the
  draws); the water's own geometry doesn't move (the waves are normals); stacked sheets are each shaded; the copy is
  made once per view, so transparent things drawn between it and a later water draw aren't seen through that water.

### Art styles: vhs, retro, watercolour

`gpu_vulkan/vulkan_style_film` (`[Graphics] rt_style=vhs | retro | watercolour` - `ArtStyle::kVhs`, `kRetro`, `kWatercolour`
in rt_style's name list `off | toon | cel | comic | sketch | vhs | retro | watercolour | noir | thermal`; their knobs `rt_style_<style>_*`, all
live; `rt_style_strength` mixes each over the original as it does the toon's). Each is a post-process at the toon's
place (`ViewOrder::kStyle`: after the tone mapping and the upscaler, before the 2D - the HUD is never filtered), per
viewport (split screen: nothing read across a viewport's edge), alpha kept, NaN-free. `vulkan_style.cpp`'s pass plans
the view (the colour's rectangle, upscaled or not) and hands it to `record_look_pass` when rt_style is one of these
(one marked hook). Shared: `vulkan_style_kit` (one pushed-descriptor layout, 128 bytes of push constants - each
style's `LookConstants` -, a linear sampler, the full-screen triangle, the viewport's colour copied out / resolved,
timing logged every 300 passes as `rt_style <style>: the passes take`). Each style has a processor twin
(`vhs_reference`, `retro_reference`, `watercolour_paint` / `watercolour_reference`) fed the same constants.

- **vhs** (`vulkan_style_vhs.cpp`, one full-screen pass): a tape line s = height / 480. Per pixel: the line shifted
  sideways by a per-line jitter (a hash of the line and the frame - 30 a second of game time), a slow bend down the
  picture and, inside a tracking band that rolls down the picture in the first 2.4 s of nine 9-second slots in ten, a
  tear; the fisheye (barrel distortion: the source at c x (1 + k r^2) / (1 + k r_edge^2), k = 0.5 x
  `rt_style_vhs_fisheye`, so the left / right edges' middles stay put, the centre is magnified and the corners go dark -
  a vignette and a soft frame edge); the source read along the line - luma through a 5-tap soft kernel, YIQ's chroma
  through a wide 7-tap one moved right (`rt_style_vhs_bleed`) and x `rt_style_vhs_saturation`; the tape's levels (black
  lifted to 0.05, white at 0.95) and grain per tape cell; the camcorder's display (`rt_style_vhs_overlay`): a blinking
  red dot + REC top left, SP and an h:mm:ss tape counter at the bottom, a 5 x 7 dot font of our own (white over a dark
  shadow; it wobbles with the tape, not with the lens); scanlines between the tape's lines (faded out below 2 pixels a
  line). Everything that moves follows `rt::ViewDrawn::game_seconds` (the game clock): a paused game holds the picture,
  a demo video export renders each frame as the run plays (the clock is the run's).
- **retro**: the geometry and a picture pass.
  - *Geometry* - `VertexKey::retro` (one of the key's spare bytes; ff_key.h): set by `record_draw` on every 3D draw (not
    pre-transformed) while `retro_geometry()` - the style is retro and a view's world is being drawn, from its
    `view_begins` to its `world_drawn` (a listener of the style's own) - so the 2D after it and every other shader are
    untouched: with the style off the generated GLSL is byte for byte as before (`thug2_ff_shader_check --dump`, the
    whole key set with `--surface --motion`, against main's build: identical). The vertex shader (ff_shaders.cpp) moves
    x / w, y / w toward the low-resolution grid's nearest corner (`VertexUniforms::retro`: the cells, snap 0 - 1) and
    then scales the whole position by mix(1, 1 / w, affine). A positive factor leaves the point where it is on the
    screen and its depth (x / w, y / w, z / w unchanged - the triangle covers the same pixels and clips the same way),
    but the rasteriser's perspective correction divides by the new w: at 1 every attribute - texture coordinates,
    colours, fog - is interpolated linearly on the screen (the affine "swimming" textures). The depth stays exact, so
    the ray-traced effects (they read the depth) keep working under retro.
  - *Picture* (`vulkan_style_retro.cpp`, one full-screen pass, integer arithmetic): `rt_style_retro_lines` rows of
    square cells (the columns rounded to the aspect), each cell the source's pixel at its middle (point-sampled), each
    channel brought to `rt_style_retro_bits` bits through a 4 x 4 ordered dither per cell (`rt_style_retro_dither`).
- **watercolour** (`vulkan_style_watercolour.cpp`): (compute) the colour at half resolution; (compute) the paint - a
  5 x 5 bilateral blur of it (taps `rt_style_watercolour_bleed` x 2 half pixels apart at 720 lines; colour weight
  exp(-30 |d|^2)); (full-screen) the paint read at a place moved by a flow field (`_flow`: value noise fixed to the
  screen, 48-pixel cells - the paper doesn't move with the camera, so nothing shimmers), its pigment density raised where
  its lightness changes (the wet edge, `_edges`) and by the paper's grain (value noise at 2.5 and 14 pixels,
  `_granulation`) - C' = C - (C - C^2)(d - 1) -, the highlights washed toward the paper's colour (`_paper`), and a
  light graphite line along the paint's edges read from a place moved the other way (`_outlines`: drawn by hand, not
  traced).
- **Checked:** `thug2_vulkan_style_film_check` (138 checks; the RTX 5070 Ti and the integrated Radeon (`--device 1`),
  validation with synchronization validation 0): a 256 x 128 picture (noisy fields, a gradient, NaN / infinities) - each
  style at its defaults, every knob up, every knob off at strength 0.6, an 8-bit target: every pixel within 3 steps of
  its twin (largest 0.1 - 1.3 steps; the bad values' corner left out of the comparison - the device's filter over an
  infinity - but every channel finite and 0 - 1 everywhere), alpha kept; the watercolour's paint within a step of its
  twin; the vhs picture the same at the same game time and changed a second later, its REC blinking; retro flat 2 x 2
  cells with at most 32 shades a channel; the watercolour calming the noise; a viewport changes nothing outside it bit
  for bit; strength 0 records no pass; four samples (the resolve filtered, every sample written). The retro geometry:
  a 3D floor quad (near z 2, far z 20, vertex colours red 0 near / 1 far) drawn through the fixed-function shaders -
  off: rows and columns where the projection puts it, red half way down the screen 0.091 (perspective-correct, expected
  0.091); retro: its corners on the 128 x 64 grid, red 0.487 (affine, ~0.5); retro with snap 0 and affine 0: as off.
  `thug2_ff_shader_check --retro`: every non-pre-transformed vertex key again with the retro bit (3,948 added) - all
  54,128 shaders compiled and validated by SPIRV-Tools, every module and 3,000 sampled pipelines accepted by the driver.
  Cost on the RTX 5070 Ti at 2560 x 1440: vhs 0.20 - 0.31 ms, retro 0.08 ms, watercolour 0.20 - 0.31 ms (the check's
  runs vary); the integrated Radeon 8 - 10 / 3.7 - 4.9 / 14 - 18 ms (the budget is checked on a discrete card only).
- **In the game** (THUG2-2, the user's settings - ray tracing with every effect, HDR, DLSS Ray Reconstruction, 8x MSAA,
  2560 x 1440 -, `rt_style` 4 / 5 / 6 in this branch's numbering): BO, AU, z_dt and the main menu each. The HUD stays
  sharp in all three; the new front end's main menu draws no world view, so it is untouched. vhs: dark rounded fisheye
  corners, soft luma, colour smeared right of the rails and signs, the counter running with the game clock, a tracking
  band tearing a strip of lines; retro: 427 x 240 cells, dithered 15-bit colour, textures swimming on the big road and
  stair polygons, the ray-traced shadows and reflections still on; watercolour: painted fields with dark rims, paper
  grain in the sky, washed-out highlights, light pencil lines. The log's device times: vhs 0.18 - 0.48 ms, retro 0.20 ms
  (the picture pass), watercolour 0.51 ms. The BO demo (perf_BO, 22 s) saved as a video with vhs on: 659 frames, the run
  identical to the recording (1316 check points), the counter and band advancing frame by frame with the run's clock.

### Changing the settings while the game runs (the Options screen)

`app/renderer_choice.cpp` reads each effect's settings in a function of its own (`read_rt_shadows`, `read_rt_ao`,
`read_rt_gi`, `read_rt_reflections`, `read_hdr`, `read_rt_fog`, `read_rt_sun`, `read_rt_sky`, `read_rt_relit`,
`read_upscale`, `read_rt_pbr`, `read_style`, `read_grass`, `read_rt_cel`, `read_water`) and installs `reload_live_key` through `render::graphics_options::set_live_reload`. When the new front
end's Options screen writes a `[Graphics]` key it calls `render::graphics_options::reload_live(key)`: a key of an
effect (`kLiveKeys`: the shadows', occlusion's, the bounce's, the reflections', the tone mapping's numbers, the fog's,
the sun / shafts / bloom / sky model's, the sky and clouds', relit's, the per-pixel lighting's - `rt_pbr`, `rt_pbr_*` -,
the art style's, the grass's - `grass`, `grass_*` -, cel shading's - `rt_cel`, `rt_cel_*` - and the water's - `rt_water`,
`rt_water_*`)
has that effect's settings read again and set
through its `set_rt_*_settings` - each takes the backend's lock and adds or removes its view listener, as at
start-up; the shadows are followed by the bounce (their `off` clears the baked sun it wants). Effects' histories start
again, and the sky is matched again, so a change shows at once. What the device or the level's load was made for
waits for the restart: `renderer`, `ray_tracing` (the device's extensions), `rt_hdr` (the colour targets' format - a
live reload keeps the start-up value for the HDR pass, the fog, the sun and the sky), `upscaler` / `upscale_quality` /
`ray_reconstruction` (the targets and the provider), `rt_tessellation` (read once, when meshes load) and
`anisotropy` / `msaa` / the distance scales. On Direct3D 9 nothing is installed: the key is written and the screen says
"Restart to apply".
`rt_pbr` switched on while a level is loaded lights its objects per pixel at once; its surface maps are attached as
the texture dictionaries load, so the level's own maps come with the next level loaded (switched off, nothing reads
them at once).

## Order of work

| # | task | depends on | start |
|---|---|---|---|
| V1 | `src/gpu_vulkan` skeleton: CMake option `THUG2_VULKAN`, the fetched libraries (Vulkan-Headers, volk, VMA, glslang / shaderc), `tools/layers.json` / `cmake_layers.ts` entries, a header test (Vulkan headers only in `gpu_vulkan`) | - | now, against `stage11` |
| V2 | A Vulkan context test program (`src/tests`): instance, device selection, an SDL window with a Vulkan surface, swapchain, a cleared frame, resize / minimise / out-of-date handling, validation in debug | V1 | now, against `stage11` |
| V3 | The fixed-function emulation generator: the state key -> GLSL -> SPIR-V, the 6 fixed `ps.1.1` shaders and the per-material generator in GLSL, compiled offline in a test program and checked by reading the key's terms back | V1 | now, against `stage11` |
| V4 | Choose the renderer at start-up: `[Graphics] renderer=d3d9\|vulkan`; `app/` installs the matching `Device` / `UiBackend`; the SDL window flag through `sys::create_sdl_window` | V1 | after stage 11 - done: `app/renderer_choice`, falls back to Direct3D 9 until the device exists |
| V5a | `render::Gpu`: the interface, and `render_state`'s cache and `primitive_states` through it (D3D9 implementation = today's calls) | - | after stage 11 - done: `engine/gfx/gpu.h`, `gpu_d3d9/gpu_d3d9.cpp` |
| V5b | Meshes, materials, skinning, mesh lists, instances through `Gpu` | V5a | after stage 11 - done |
| V5c | Particles, billboards, weather, grass, glow, texture splats, shatter through `Gpu` | V5a | after stage 11 - done |
| V5d | Screen effects, draw frame, shadows (texture and stencil), texture projection, viewports through `Gpu` | V5a | after stage 11 - done |
| V5e | 2D, text, fonts, sprites, loading screen / bar / console, movie, screenshots, device creation and reset through `Gpu` / `GpuContext` | V5a | after stage 11 - done |
| V5f | The folder split: `render_d3d9` -> `render_pc` (API-neutral, no `<d3d9.h>`) + `gpu_d3d9`; `tools/d3d_headers.test.ts` extended | V5b-e | after stage 11 - done: `render_pc` (118 files, `thug2_render_pc`), `gpu_d3d9` (11 files, `thug2_gpu_d3d9`); `render_vulkan` kept its name until V6 put the Vulkan `Gpu` in it (renamed `gpu_vulkan` then) |
| V6 | `gpu_vulkan` resources: textures (format conversions), buffers (static, dynamic, the `UP` ring), render targets, depth formats, samplers | V2, V5a | after stage 11 - done: the Vulkan `Gpu` and `GpuContext` (nothing drawn), `thug2_vulkan_gpu_check`; the pixel shaders' assembly and the screen shot's JPEG became `Gpu` operations |
| V7 | `gpu_vulkan` draws: pipelines from V3's keys, the state block, transforms, indexed / ring draws; a level's world drawn | V3, V6, V5b | after stage 11 - done: foun drawn and skated with `renderer=vulkan` on both GPUs here; passes, frames in flight, batched uploads, deferred deletion, the present; `[Graphics] vulkan_device` |
| V8 | `gpu_vulkan` effects: screen copies, blur, bloom, occlusion queries, texture shadows, stencil volumes, flash, MSAA resolve, gamma pass | V7, V5c, V5d | after stage 11 - done: the bloom's blank textures (the brighter picture) and the shadow textures' viewport fixed, the gamma pass in full screen, the swapchain following the surface; five spots compared with Direct3D 9 |
| V9 | `UiBackend` for Vulkan: ImGui's Vulkan backend, the preview and atlas targets, images | V6 | after stage 11 - done: ImGui's lists drawn by the Vulkan `Gpu` (ImGui's SDL2 backend kept), images as `Gpu` textures, the preview and atlas through the `Gpu`; the new front end and the level editor (in the game and the editor exe) draw on Vulkan |
| V10 | Movies, loading screens, screenshots and the editor's methods on Vulkan | V7, V5e | after stage 11 - done: the start-up viewport (the movies were black) and the unset texture stage (the loading bar was white) fixed, a reset leaves the bindings as Direct3D 9's; the loading threads, screenshots, full screen and alt-tab checked on Vulkan |
| V11 | Parity: a screenshot comparison between the two backends at the same spot and frame (the automation channel's teleport and capture), with each backend's run-to-run noise as the baseline; the front end, the editor, five levels; frame times | V8-V10 | after stage 11 - done: `tools/game/renderer_parity.ts`; a material pass misread fixed; what is left is MSAA edges, a shadow outline and line rasterisation (V11 above) |
| R1 | The frame as data: `render_pc` fills the per-frame instance / light / camera record; stable mesh and material ids | V5f | after V11 and the x64 port - done: `render_pc/rt_record` (below, "The frame record"), the contract check `devtools/rt_check` ([debug] rt_contract_check=1) and `tools/game/rt_contract.ts`: 0 violations in BO, LA, SE, foun and Anteara Mod's BO with both renderers |
| R2 | Acceleration structures: a BLAS per static mesh at scene load, refit / rebuild for CPU-skinned meshes, the TLAS per frame | R1, V7, x64 | done (the receiving side; the meshes arrive with R1): `gpu_vulkan/vulkan_rt_scene` - a BLAS per registered mesh from the Gpu's own buffers (16 / 32-bit indices, offsets, strips listed), static ones compacted, dynamic ones refitted (rebuilt every 32 refits); the TLAS per frame in flight from the record (custom index = material; mask 0x01 world / 0x02 objects / 0x04 alpha-tested / 0x80 no shadow, an alpha test against 1/255 counting as opaque), handed to R4's shadow pass; materials forwarded to R3's table; `current_scene()` for the effects; in the game (with R1) foun's 1,239 meshes take 3.7 MB of BLAS (11.5 MB before compaction) and `rtprobe` meets `colprobe` (90.002 / 90.000); `thug2_vulkan_rt_check` traces thousands of rays against a CPU reference on both GPUs here, 0 validation messages (core + sync); automation `rtprobe` / `colprobe` |
| R3 | The material table and bindless textures | R1, V6 | after V11 and the x64 port - done: `gpu_vulkan/vulkan_rt_materials` (`MaterialTable`, made with `ray_tracing=1`): a record per material id in a storage buffer (scalar layout, grown with a new set), textures as one partially bound, update-after-bind array of sampled images (a slot per texture, white once released, reused after the frames in flight), 32 samplers (address u x v x nearest / linear) picked per material, and the GLSL hit shaders include (`material_glsl()`: `material()`, `material_sample()`, the alpha test); `thug2_vulkan_rt_materials_check` reads it back through a compute shader on both GPUs here |
| R4 | Ray-traced directional shadows (ray queries in the lighting pass), an ini option, off by default | R2 | after R2 - done (waits for R1's light and R2's TLAS): `gpu_vulkan/vulkan_rt_shadows` (`[Graphics] rt_shadows`, `rt_shadow_strength` / `_softness` / `_rays` / `_objects`): `draw_viewport` tells `rt_scene`'s view listeners when a viewport's opaque world, objects and sky are drawn (`rt::opaque_view_drawn`, before the transparent passes); one full-screen pass then reads the depth (each sample of a multisampled target when the device has sample-rate shading, else sample 0), rebuilds the world position with the view's inverse view x projection and the normal from the nearer neighbours' depths, traces a ray query toward the frame record's directional light (instance mask 0x01 world, + 0x02 objects; opaque, first hit) and multiplies the colour by 1 - strength where it is blocked; soft shadows trace several rays over the sun's disc (no denoiser). The TLAS is handed over with `rt_shadows_set_scene()`; without it or a directional light the pass is skipped (logged once). `thug2_vulkan_rt_shadow_check` compares the pass with a processor ray-cast pixel by pixel (one sample, four, objects, soft, a split view, the sky test) on both GPUs here; `tools/game/rt_shadows_check.ts` checks it in the game. The sun is the level's baked one (`engine/gfx/rt_sun`: fitted to the world's vertex colours; none indoors), shadows are dropped where the bake disagrees and under roofs - above, "The sun and where the shadows fall" |
| R5 | Reflections / ambient occlusion and a denoiser | R2, R3 | done - ambient occlusion and the denoiser: `gpu_vulkan/vulkan_rt_effects` (`[Graphics] rt_ao`, `rt_ao_strength` / `_radius` / `_rays` / `_denoise`, off by default) listens at `rt::opaque_view_drawn` after R4 and runs compute passes over images of the viewport's size - the rays (world position and normal rebuilt from the depth as R4 does - sample 0's own position in a multisampled target -, `rt_ao_rays` cosine-weighted ray queries up to `rt_ao_radius` world units, masks 0x01 + 0x02, directions from a 2D R2 sequence advanced each frame and turned per pixel by a hash), the temporal accumulation (the world position reprojected through the previous frame's camera of that viewport, bilinear over the texels whose guide agrees - on the pixel's plane, normal within ~25 degrees -, the history clamped to the 3x3 spread of this frame's rays plus a noise floor, up to 32 frames; nothing agrees: it starts again), three a-trous passes (5x5 B3 kernel at steps 1, 2, 4, weighted by normal, plane distance and the noise the history's length leaves) - then one full-screen multiplying pass, colour x (1 - strength x occlusion). Histories per viewport (rectangle + target size, up to 4). The TLAS comes from R2 through `rt_effects_set_scene_locked()`. `thug2_vulkan_rt_effects_check` (38 checks, both GPUs here, validation with synchronization 0): the raw rays against the same directions traced on the processor (50k clear-cut pixels exact, one sample and four), the denoised still camera against a 256-ray reference (mean error 0.003, 99th percentile 0.035), a moved camera's histories kept where the surface was seen (47k pixels) and restarted where it was disoccluded (1.5k), no NaN; `tools/game/rt_shadows_check.ts --effect ao` checks it in the game. Reflections: `gpu_vulkan/vulkan_rt_reflections` (`[Graphics] rt_reflections`, `rt_reflection_strength`, off by default), a listener after R4's and the AO's. Which surfaces: the game marks shiny ones with an environment-mapped material pass (pass flag 0x8, a sky picture sphere-mapped on); R1's registration gives `rt::Material::reflectivity` (that pass's brightest colour channel, doubled as drawn, times its fixed alpha where its blend mode takes one) and `environment_pass`, carried into R3's record (48 bytes a row). Hit shading: `rt::Geometry` has the texture coordinates' and colour's offsets and formats (R1 from the mesh's offsets and vertex format), R2 writes a geometry table per frame (`GeometryRecord`: the vertex / index buffers' device addresses as the frame uses them, the layout, strips as its own 32-bit lists) bound beside the TLAS, and `scene_geometry_glsl()` fetches a hit triangle's vertices through `GL_EXT_buffer_reference`. The pass: a primary ray per pixel (toward the depth's world point; kept when it ends there) identifies the surface and its material - no per-pixel material target, and nothing at all while the frame has no reflective instance; a reflective one gets the triangle's flat normal and one mirror ray (masks 0x01 + 0x02 + 0x04, alpha tested along the way), its hit shaded with the material's texture at the interpolated texture coordinates (the level from the ray's footprint: the pixel's spread over both legs), times the vertex colour doubled as the game's pixel shaders do (objects: the frame's ambient and directional lights); a miss leaves the pixel to the game's own environment map. A perfect mirror is one deterministic ray: no noise, so no denoiser. A full-screen pass mixes the colour toward it by reflectivity x strength (alpha kept; the pixels without a reflection discarded). `thug2_vulkan_rt_reflections_check` (39 checks, both GPUs, validation and synchronization validation 0): texture coordinates, colours and positions at 2100 hits through the table in every mesh layout (16- / 32-bit indices, a 16-bit strip from an odd index, unindexed lists and strips, offsets, an instance scaled, one with neither attribute) against the meshes' own; a mirror floor (a 32-bit strip) reflecting a box with an 8 x 8 texture, ~9000 clear-cut pixels each the processor's texel x vertex colour, one sample and four, strength 1 and 0.4; the box, the sky and the floor that mirrors nothing exactly as without the pass; nothing reflective or the setting off: no pass. `tools/game/rt_shadows_check.ts --effect reflections` checks it in the game. Not done: fog on the reflection, back faces treated as surfaces, rough (glossy) reflections beyond glass's (see "Water, glass and the skater's shadow": Fresnel, masks, rough glass) |
| H1 | HDR targets, tone mapping and colour grading (`rt_hdr`): the base the added light (GI, fog) needs | V7, V8 | done - `gpu_vulkan/vulkan_hdr` (above, "HDR and tone mapping"): float back buffer and world targets, the tone-mapping pass last at `world_drawn`, `thug2_vulkan_hdr_check`, `rt_shadows_check.ts --effect hdr` |
| R5 fog | Volumetric fog and light shafts | R2, R4's baked sun | done - `gpu_vulkan/vulkan_rt_fog` (`[Graphics] rt_fog`, off by default): the level's own linear fog as a medium of the same transmittance, lit by the baked sun through ray queries at half resolution with temporal accumulation and a depth-aware upsample; replaces the game's fog (`rt_fog_mode=0`) or adds the shafts over it (1); a level without fog gets a haze's shafts (`rt_fog_haze`) - "Volumetric fog and light shafts" above |
| GI | One-bounce diffuse indirect light ("secondary lighting") with ray queries | R2, R3, R5, the baked sun | done: `gpu_vulkan/vulkan_rt_effects` (`[Graphics] rt_gi`, `rt_gi_strength` / `_rays` / `_distance` / `_denoise`, off by default), a view listener at `kIndirect`: the sun's light bounced once off what is around a pixel (hits shaded with R3's material at R2's texture coordinates, a shadow ray toward the level's baked sun), R5's denoiser over the colour, added to the frame as strength x the pixel's albedo x the light through a screen blend (see "One-bounce indirect light"). `thug2_vulkan_rt_gi_check` (39 checks, both GPUs, validation and synchronization 0); `tools/game/rt_shadows_check.ts --effect gi`; 1080p on the RTX 5070 Ti: 1.5 / 2.0 / 2.4 ms for 1 / 2 / 4 rays |
| Sun | The sun, its glare, screen-space rays, an energy-conserving bloom and a sky model (aerial perspective) | R2, R4's baked sun, R5 fog, H1 | done: `gpu_vulkan/vulkan_rt_sun` + `vulkan_rt_sky`, `engine/gfx/rt_sky` (`[Graphics] rt_sun`, `rt_shafts`, `rt_bloom`, `rt_sky_model`, off by default): the original's sun sprite and lens flare replaced by a disc and aureole where the game shows its sun, radial rays over the depth's sky mask, a 13-tap / tent mip-chain bloom over float intermediates (the same with `rt_hdr` 0 and 1), the haze as aerial perspective coloured by a single-scattering sky (see "The sun, its glare and the sky"). `thug2_vulkan_rt_sun_check` (both GPUs, validation and synchronization 0); `tools/game/rt_shadows_check.ts --effect sun` |
| PBR | Per-pixel lighting and surface maps (normal, roughness, height) made from the original textures | V3, V7 (R3, R4's sun to use them fully) | done: `gpu_vulkan/vulkan_surface`, `ff_key` `kSurface*`, `tools/pbr_maps` (`[Graphics] rt_pbr`, off by default): the classical generator, the bake relit per pixel toward its sun and the sky, objects' lights per pixel, GGX highlights, the maps in R3's table (see "Per-pixel lighting and surface maps"). `thug2_vulkan_pbr_check` (both GPUs, validation and synchronization 0); `tools/game/rt_shadows_check.ts --effect pbr` |


```
V1 -> V2 -> V6 ----------> V7 -> V8 -> V11 -> R1 -> R2 -> R4 -> R5
V1 -> V3 ---------------/        V9  /               R3 -/
V5a -> V5b..e -> V5f   (V5b feeds V7; V5c, V5d feed V8; V5e feeds V10)
V4 (needs V1) before the first in-game Vulkan frame (V7)
```

## Conflicts and where PRs go

- **Stage 11** is still restructuring the code on branch `stage11`. Its remaining rounds include the renderer's
  remaining class splits, the editor's and front end's files and API, and the docs. When stage 11 is finished it is
  merged straight into main and pushed. A PR against main before then would conflict at once.
- **V1-V3 are pure additions** (a new library, a test program, a generator) and can start now. They target the
  `stage11` branch until it merges, then main. Their shared edits (the root `CMakeLists.txt`, `tools/layers.json`, the
  header tests) are small and additive.
- **Everything that edits `render_d3d9`, `app/`, `platform/` or the engine (V4-V11) starts after stage 11 is merged
  to main** and targets main.
- **Between the tasks:**
  - V5a-e each touch their own files, but all of them touch `render_state` / the device's state record. Land V5a
    first; V5b-e can then run in parallel with care.
  - V5f moves every file, so nothing else is open in `render_d3d9` while it runs.
  - The x64 port's X11 (`render_d3d9` on x64) lands before V5 starts, or after V5f.
  - R1-R5 wait for both V11 and the x64 port (x64-port.md X15).

## Risks

1. **The split (V5)** must keep the Direct3D 9 frames identical: the same calls in the same order. It is checked by
   the screenshot noise method used in stage 11 and by a call log diff in a measuring build.
2. **Fixed-function fidelity**: fog (vertex, range-based), the `MODULATE2X` / `4X` scales, alpha test against
   8-bit references, texgen and the texture transforms, point sprite sizes, and D3D9's pixel centres. A wrong term
   shows as a slightly different picture, so V11's comparison is the gate.
3. **Pipeline count**: too many keys mean stutters while pipelines are made. Mitigations: the disk cache, warming the
   cache at scene load from the scene's materials, and later dynamic state (`VK_EXT_extended_dynamic_state`).
4. **Device-lost semantics** differ (swapchain re-creation, not reset). The engine's reset steps stay, with their
   meaning mapped.
5. **Ray tracing on this content**: low-poly geometry with baked vertex lighting means ray-traced effects have to sit
   on top of the original's look (masks, options), not replace it. Skinned meshes rebuilt per frame cost time.
   Driver support for ray tracing and 32-bit Vulkan is to confirm.

## Estimate

In this project's rounds (30-90 minutes of an agent's work): V1-V3 3-4, V4 1, V5a-f 6-8, V6 2, V7 3, V8 3, V9 1,
V10 2, V11 2. That is about **23-26 rounds to raster parity**. Ray tracing (R1-R5): a first version of each done.

## How the numbers were measured

A Python scan over `src/` (comments stripped) counts the Direct3D calls by name, the states, values, formats, pools,
FVF combinations and primitive types named beside them, and the pixel shader sources. Values passed through
variables (material tables, the renderer's cache) are counted as such: the scan lists what is named, not every value
used at run time.
