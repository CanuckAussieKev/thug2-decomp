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
  written every 10 s when something new was made - a shader compiled, or a pipeline the cache didn't have (Vulkan 1.3's
  creation feedback says; a pipeline the cache had marked them too until 2026-11-16, and the 70+ MB written on the
  thread that draws stalled a frame 60 - 70 ms every 10 s while playing - "vulkan: the shader and pipeline caches
  saved ... in N ms" when it takes over 8 ms, "vulkan: a pipeline took N ms to make" likewise; since 2026-11-16 the
  write is a thread of its own - the driver's cache read under the lock pipeline creation takes, each file written
  beside its own and renamed over it, one save at a time: a 128 MB write had taken 9 s on the drawing thread) (the 64-bit build's two files end in `-x64`: a 32-bit driver's cache
  is never given to the 64-bit one). Since 2026-11-16 the files are bounded (they had reached 90 + 44 MB): the driver's
  one big cache can't be pruned, so each pipeline has a cache of its own - made from the blob kept for its key (a hash of
  the pipeline's key), read back when the pipeline wasn't in it - and the pipelines file holds those blobs, each with the
  run (a counter in the file's header) that last used it; the shaders file's modules carry the same. A save keeps the
  newest up to 24 MB of shaders and 40 MB of pipelines and drops the rest ("N entries dropped for the size" in the
  save's log line); a file in the old format (the driver's whole cache) isn't read and is replaced at the first save. foun makes 52 pipelines from 40 shaders (0.9 s of glslang in the
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
- **A reset** (`GpuContext::reset`: a new back-buffer size; the PC layer's back-buffer targets released around it and
  the state block reset after it, as for Direct3D 9) makes the swapchain again and leaves the bindings as
  Direct3D 9's `Reset` does: target 0 the back buffer, the others none, the device's depth surface, no vertex streams
  or indices. When the settings' size or samples changed, the back buffer and its depth surface are made again at the
  new ones (the game itself never changes them after start-up: its resolution is the ini's). Nothing is lost on a focus
  change (`GpuContext::loses_device_objects` false): the window's focus coming back no longer resets the device, and a
  reset leaves the vertex and index buffers alone - until 2026-11-16 each focus change released and made again every
  buffer (each uploaded, every ray-traced structure built again, the relit level relit from buffers holding relit
  colours): a 10-15 s frame each time, a "not responding" College.
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
- **One shadow model** (lighting round A, 2026-11-16 - the user: "all lighting hacks should be removed, and things
  should be handled via physics"). The sun's visibility at every lit pixel - its world casters', the objects' and the
  clouds' share - takes the place of the light the pixel was drawn with. A relit world pixel (its light store, read at
  its triangle) is scaled by its light with the pixel's visibility over its light with the vertex's; an object lit by
  the game's lights loses its key light's share where the sun doesn't reach it (`rt::ObjectLight`); any other lit
  pixel its lighting state's direct share (`rt_light_field.h direct_share`). Retired with the old "darken over the
  bake" model: `rt_shadow_strength` (a blend of two estimates of one visibility), the agreement grid
  (`rt_shadow_match_bake`, `other_sun_work`), the sky test (`rt_shadow_covered`, `rt_shadow_sky_rays`). The passes
  need the level relit, and a relit level holds none of the bake's light - there is nothing left to agree with.

### Lights at night (`rt_lights`)

Night and indoor levels look flat: their lamps, neon and lit windows are bright textures that light nothing (the game
bakes their light into the vertex colours round them - no shadows, nothing on the skater). `rt_lights=1` makes them
lights for a ray-traced pass (`gpu_vulkan/vulkan_rt_lights.h`, the light sources in `engine/gfx/rt_lights.h`).
`rt_lights=auto` (the default since 2026-11-15) sets the pass up but lights only after dark: with the time of day by
its lamps' amount (dusk to dawn), without it only in a dark level (the emitters' median brightness below); 0 never.
- **Sources.** The frame record's point lights (the level's light nodes - `engine/gfx/scene_lights.h` - and any a script
  makes), and the level's **emitters**, found once a world by `render_pc/rt_record.cpp` (after the structures settle):
  *glows* - world meshes whose first pass blends additively (modes 1, 2: lamp halos, neon tubes, light cones, light
  pools), moving ones (UV wibble, texture animation: water, caustics) left out; and, only in a **dark level** (its
  world's median vertex brightness at most 64 of the white 128), *self-lit* triangles - opaque ones whose dimmest corner
  is at least 112 and 2.5 times the level's median (lamp heads, lit windows, signs). Triangles are joined into clusters
  (a triangle joins the nearest cluster of its kind whose first triangle is within 96 units): one light per lamp, a long
  tube one every 96 units or so. A cluster's light: its area-weighted centre, colour (vertex x pass colour; the pass
  multiplies in its texture's mean - the texture's smallest level), intensity (the colour's brightness, at most 2),
  size (half its diagonal: where its shadow rays stop), core (the radius of a disc of its area: its falloff's), reach
  (12 x its core, 200 - 1500 units). A flat cluster facing up is a pool of light: a glow's is lifted by its core, a
  self-lit one dropped (that is baked light on the floor). A point light's core is a tenth of its reach.
- **Per view** the lights whose reach touches the frustum, the `rt_lights_max` most important (intensity x how much of
  its reach the camera is within), go in a uniform block (at most 256).
- **Per pixel** (one full-screen pass after the bounce light, `ViewOrder::kLights`): the position and normal from the
  depth, the albedo from a camera ray and the material table (as `rt_gi`'s), every light's unshadowed light (colour x
  intensity x falloff x N.L; falloff `core^2 / (core^2 + d^2) x (1 - (d / reach)^4)^2` - a disc of the cluster's area and brightness: its
  brightness at it, falling with the square of the distance past its own size), the
  brightest `rt_lights_shadowed` traced (a ray query toward each through the opaque world and objects, stopped the
  light's size short: a lamp's own fixture doesn't shadow it; an object's pixel the world's shadows only), the rest
  added in the share of the traced ones' light that got through; the sum's luminance held under 1 (as it is up to 0.5,
  then L' = 0.5 + 0.5 (L - 0.5) / L: many lamps over one spot light it up to its own colour, never white). Added = strength x albedo
  x light (screened on an 8-bit target, added on HDR). Until 2026-11-15 the core was at least a quarter of the reach (up
  to 375 units) and the intensity grew with a cluster's area: one street-wide glow in New Orleans lit the whole street
  evenly - white at strength 0.35, still like daylight at 0.1. Now the default strength is 1. Deterministic: no noise, no history.
- **Check**: `thug2_vulkan_rt_lights_check` (the clustering and selection on the processor; a lamp beside a box on a dark
  floor against the processor's answer, one sample and four, a red-textured glow, two lamps with one traced; off and a
  lamp out of view leave every pixel as drawn; 49 lamps over the floor: their sum past the knee, as the processor's, no
  pixel white at the default strength; a street-wide glow's light 600 units away an eighth of the light under it). `--perf`: 64 lamps in view, 320x240: 0.087 ms a pass (RTX 5070 Ti) -
  about 2-3 ms at 1920x1080 in that scene; a level's deeper BVH costs more (not measured in the game yet).
- **Lighting round A**: the lamps light by their data alone - no knee on their sum (the HDR tone map compresses),
  nothing subtracted. On the relit world the albedo is the texture's times the light store's tint. One owner: while the
  pass lights, relit lights no lamps per vertex (`rt_lights.h lamps_drawn`, `lamps_on` - one rule for both: the clock's
  lamps, else a dark level's); otherwise relit lights the same lamps per vertex (L, traced at the bake from the same
  list, in the same units). The lamps' list is in the level's relit data (path-tracer input: position, colour x
  intensity, falloff core and reach).

### Relit: the bake taken apart, offline (`rt_relit`)

`engine/gfx/rt_relit` (the analysis, API-neutral), `engine/gfx/relit_data` (the level's data file),
`render_pc/relit_bake` (the bake: the game itself in a batch run) and `render_pc/rt_relit_world` (the world's meshes,
the data read, the real-time half) - `[Graphics] rt_relit`, off by default. THUG2's world light is baked into its vertex
colours: what a vertex shows is the artists' colour (its tint) times the light that reached it, and only the product
is stored. The relit mode separates them and lights the level again.

**Lighting round A (2026-11-16): the data is the surfaces' true colour.** The fit (E = a S + b K + g B + c L) only
describes the bake to take it out: each vertex's tint is its colour over that light where the bake agrees with the
model; where the bake is brighter or darker by `kRelitLampLow` or more (lamps the list doesn't hold, the bake's sun
where the rays saw a caster, the bake's shadows where the rays saw the sun - College's kiosks and court fences - and
its darkened corners), the tint is its surroundings' and that light is dropped; a mesh lit or shaded whole takes its
neighbours' difference across meshes (`relit_light_across`). Kept per vertex: the tint, S toward the bake's sun, K, the
bounce of the relit light at the bake's sun, L; and the lamps' list. Gone: the residual, the missed sun, the level's
scale, the agreement grid. A bake with no sun (night, indoors - New Orleans) is taken apart too: its sky, bounce and
lamps; the clock's daylight is the corpus median sun and sky (`kRelitDaySun`, `kRelitDaySky`). The baked sun's search
reaches 2 degrees up (an evening bake's sun) and judges each sample against its own mesh too. Data v3; older files are
out of date. **In the game** relit never writes the game's vertex buffers: the light stream (rt_cel.h, six words - the
sixth the colour) is the light store every relit world draw reads in place of its vertex colour (VertexKey
`kVertexRelitColour`, attribute `kRelitColourLocation`); the ray-traced passes read the same store at a hit (albedo +
light: what a path tracer's closest-hit would read). The level looks different from the original, even at its own
time: the original look is `rt_relit=0`. The rest of this section is the history before it.

**The decomposition is made once a level, offline** (the user, 2026-11-15: no time limits, no fallbacks). Nothing is
traced or fitted while the game loads a level: the game reads the level's data and lights the level from it.

- **The bake** (`thug2-decomp.exe -relit-bake <level>`; `node tools/game/relit_bake.ts --game <folder>` runs it for
  every installed level - THUG2's own, the overlay's, the ported ones - one game at a time, and writes
  `thug2-decomp-relit\bake-report.txt` and each level's log). The game starts straight in the level, muted, windowed
  and behind the user's windows (`platform::batch_run`), with its own settings over the user's ini
  (`config::set_overrides`: the Vulkan renderer with ray tracing, the level's own time, the relit mode and the lighting
  passes off - the buffers hold the bake). The geometry settings (building relief, smoothed curves, the water's grid) are the user's, so
  the data matches the meshes the user's game makes. Only the level asked for is baked: its load script running (the
  skate module's level - the main menu is a level of its own, `load_mainmenu`, whose world is up and steady long
  before the level's: its 90 steady drawn frames could come before the menu's 60 front-end frames that start the
  level - `max_fps` draws more frames than the 60 Hz ticks - and the menu was baked under the level's name) and its world the scene its level structure names
  (`level=`). Otherwise nothing is written and the game ends with exit code 2 (the level never came up within 5
  minutes, or `start level:` refused it) or 3 (another scene) - FAILED in the report. Every mesh of the level's world scenes is recorded, shown or hidden
  (`rt_record.cpp record_world`): the bake's inputs are the level's static meshes whatever its scripts show in the first
  frames - the same data at every bake (sch's sun no longer flips with what the first frame showed). Once the world's
  structures are built, its baked sun fitted (`rt_sun.h`, over the same meshes) and its emitters found
  (`rt_lights.h`), for `kSteadyFrames` frames: the meshes read once each by content in the order of their content's
  hash, their light traced (`kRelitBakeSkyRays` 128 sky rays and `kRelitBakeSunRays` 32 sun rays a vertex), the lamps'
  light, the model fitted over the whole level, the tints taken apart, the verdict decided, the file written, the game
  closed.
- **The light at each vertex** (`trace_relit_light`, `trace_relit_lamps`; `RayTracing::probe_many`, masks 0x05 - the
  opaque world and the cut-outs by their alpha): its normal (the mesh's, or its triangles' area-weighted, turned by the
  world's winding); S, the sun's share toward the baked sun (jittered over a disc 1.5 degrees across) times N.L; K, the
  sky's share (cosine-weighted rays that escape); B, the bounce - the same rays' hits, each bringing the baked colour
  where it lands; L, the lamps' light - every emitter and point light the lamps pass shines (`RelitLamp`: their colour
  x intensity, `light_falloff`'s core and reach) that the vertex faces, times the cosine toward it, where a shadow ray
  reaches it. The rays start 2 units off the surface.
- **The model** (`fit_relit_model`, then `fit_relit_lamps`): E = a S + b K + g B + c L per channel. a, b, g from the
  mesh edges (two vertices joined by an edge share their surface's colour, so y_i E_j = y_j E_i; each edge's error
  relative to its own light; reweighted toward the median; one-colour meshes left out; a traced shadow border counted
  only where the bake steps down too; the sun's strength set by the faces' turning). c from the edges along which the
  lamps' light changes: each edge's c is the one that makes its colours' ratio its light's, the votes' weighted median
  (at least `kRelitLampEdges` edges, none under 0). A lamp's light is then a term of its own, not hidden in the tints.
- **Tints and the rest** (`decompose_relit`): each vertex's tint y / E, its neighbourhood's where the model can't vouch
  for it; the light over the neighbourhood's prediction (1.2 - 1.8 times) the residual; the residual's sun-coloured part
  on a face toward the bake's sun the missed sun (`relit_split_residual`); then **the bake's sunlight never left in a
  tint** (`relit_sun_in_tint`): a vertex facing the bake's sun, partly in the rays' shade, whose tint comes out more than
  1.5 times its surface's sunlit tint (its mesh's sunlit vertices', else the level's) by the sun's colour gets the sun it
  needs as `sun_missed` - direct light, which a shadow can fall on (BO-2's courtyard: the skater's shadow). Then the
  lamps the list doesn't have, told across meshes (`relit_local_across`), and the level's `scale`.
- **The verdict:** relit; no sun (the baked sun's fit found none - indoors); no model (no sunlight found in the bake).
  Its margins are kept (the baked sun's confidence over `kMinConfidence`, the model's sun over `kRelitMinSunLight`) and
  logged; it is decided once, at the bake, from deterministic inputs - not again at each load.
- **The file** (`relit_data.h`): `thug2-decomp-relit\<the level's main scene checksum>.relit` beside the exe, versioned
  (`kRelitDataVersion`): the verdict and margins, the baked sun (direction, confidence, the agreement grid, the ground
  height - every effect's level sun comes from here), the model (a, b, g, c, scale), the world's winding, and each mesh
  by its content's hash (`relit_mesh_content`: positions, triangles, made vertices' sources - not the colours, which the scene lights and colour animations rewrite in the game) with its
  vertices' parts as half floats (S at the bake, K, B and its local part, the tint, the residual, L, the missed sun).
  The normals and the used vertices are made again from the mesh.
- **In the game** (`rt_relit_world::new_world`, each new world's first frame, whether the mode is on or not): the data
  read, the world's meshes matched by content, what the level is lit by decided and published - the baked sun
  (`set_baked_sun`; none without data) and the level's facts for the settings model (`config::set_level_light`). **No
  fallbacks:** a level without data, with data made from other meshes (under 95% of the world's vertices matched -
  another overlay, building relief, smoothed curves or the water's grid changed since), or whose data says no sun keeps the original
  game's lighting: `rt_relit` is out of effect there (the reasons name the build step: "This level's lighting isn't
  built - run relit_bake", "... is out of date - run relit_bake", "This level has no sunlight to relight"), and with it
  the time of day (with ray tracing on) and the lighting passes - `rt_shadows`, `rt_ao`, `rt_lights` need the level
  relit and turn `rt_relit` on (a pass replaces its own part of the relit light; none ever stacks on a bake). `rt_gi`
  is retired: the relit level bounces its light itself.
- **The real-time half** (what moves with the light): the clock and its `DayLight`; the sun's rays traced again as it
  moves (`trace_relit_sun`, `rt_relit_sun_rays`) and the bounce of the new light (`trace_relit_sky` with `keep_sky` -
  the data's K kept; `rt_relit_sky_rays`), one relight at a time, paced by time (`kRelitFrameMs`), the rays never waited
  for; the colours written (`rt_record::ColourWrite`), the light stream, the light field; the ray-traced shadows, sky
  and lamps.
- **A vertex relit:** scale x tint x (E' + c L) + residual, E' the model's light with the sun where it is now and the
  time of day's scales; blended with the bake by `rt_relit_strength`. The lamps' term and the residual are the level's
  own light (`relit_own_light`): constant through the day - the light stream's "residual" word (so the lamps pass adds
  max(L - own, 0)), the light field's local light, the bounce's local part.
- **The skater and the objects:** the game's directional lights turned toward the relit sun, the key light the direct
  light now; the fill and the ambient the game's own times (indirect now + lamps) over (indirect at the bake + lamps)
  where the thing stands; the lamps' directional part moves out of the ambient into a light of its own toward them by
  the share of the daylight gone since the bake (`pc_lights.cpp game_share`) - at the bake's own time exactly the
  game's own light (before, the lamps were added on top: BO-4's skater 1.3x the bake's; a first try that sent the
  directional part at the bake too left a skater facing away from a lamp at 0.74 in TR).
- **Retired with it:** the in-game analysis (the fit and the trace at the first still frame, the cache
  `thug2-decomp-relit\<scene>.bin`), `rt_relit_detail`, `rt_relit_cache`, and the draw-time de-lighting `rt_delight` /
  `rt_delight_strength` (the light painted into textures is taken out offline, by the de-lit texture sets).
- **Checked:** `thug2_vulkan_rt_relit_check` (the synthetic level whose bake is exact lighting - tints, sun, sky,
  bounce and a lamp - taken apart by the same functions the bake runs, and the data file written and read back: the
  level's colours from the file the bake's within a byte); `thug2_options_check` (relit, the time of day and the
  lighting passes out of effect without data, with stale data and without sunlight, with the build step in the reason).

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

### Time of day (`tod`, `tod_cycle`)

Every level can be played at any hour: the sun (and at night a full moon) moves along a real path over the level, the
sky turns through sunrise, sunset and night with stars, the level's light dims and turns blue, the lamps come on, and
the game's own day-and-night states follow the hour. `tod=level` (the default) changes nothing at all - the level as it
was made, to the bit.

**Settings** (`[Graphics]`; the Options screen's Lighting tab has *Time of day*, *Day cycle* and *Day length*, applied
at once on Vulkan):

| Key | Default | What it does |
| --- | --- | --- |
| `tod` | `level` | `level`: the level as it was made. `dawn` / `dusk`: the sun 3 degrees up, rising / setting. `morning` 9:00, `noon` 12:00, `afternoon` 15:30, `night` midnight (solar time), or a clock time `HH:MM` |
| `tod_cycle` | `off` | `on`: the clock runs from that time - day, sunset, night, sunrise (paused with the game) |
| `tod_cycle_minutes` | `24` | Minutes of game time for a whole day (1 - 1440): the simulation's steps, slow motion with them, paused with the game |
| `tod_levels` | (none) | Some levels' own times: `NO:night,AU:dusk` (the level's scene name, any case) |

The console's `rt tod [<tod> | cycle <on | off>]` sets them for one run and prints the clock (`rt tod` alone).

**Each level's own day** (`engine/gfx/rt_daylight.h` `fit_native_day`). The path is the standard one: the sun's
elevation from sin e = sin(lat) sin(dec) + cos(lat) cos(dec) cos(h) (h the hour angle, 15 degrees an hour from solar
noon), its azimuth from north through east; latitude 34 N and declination +15 (early May / August: sunrise 5:18, sunset
18:41, noon 71 degrees up). The moon is full - opposite the sun (12 hours on, the declination turned round): up all
night, highest (41 degrees) at midnight. The level's own lighting is one time of that day: the time after noon at which
the path's sun is as high as the level's sun - the sun its bake was made with (its relit data's baked sun, read from the
start of the level's file when the level arrives - with or without ray tracing), else the sun the game shows, else 45
degrees up - and north is turned so that the path's sun then is the level's sun exactly
(BO's baked sun, heading -75 and 52 up: 14:26; z_dt's 14:21; a sun higher than the path's noon lowers the latitude
until it fits). A level whose own time-of-day state is night (NO) has midnight as its time, evening sunset. So a time
moves the sun from where the level has it: `tod=afternoon` on BO is the same sun an hour later.

**Each level's compass** (added 2026-11-16, the user's "orient the level such that the sun follows that path"). Where a
level's north lies is data: `"north": <heading>` in its entry of the built-in level list
(`engine/gfx/rt_relit_levels.json`; `thug2-decomp-relit.json` beside the exe adds to it and wins). The clock reads it
(`rt_relit.h relit_level_entry`) when the level arrives, with its relit file's start, before fitting the level's day
(`NativeInputs::north_heading`) - level data, the same in every run and with any renderer - the path turned there, the level's own time still
the time its sun is as high on its side of noon (its own light unchanged), its own sun the path's at that time. One
producer: every effect reads `DayLight` (the sky, the disc and its glare, the relit level, the shadows, the air and its
god rays, the clouds and their shadows, the light field), so they all turn with it. No rebake: the relit data is the
bake taken apart, relit toward whatever sun the clock gives. Without an entry north is where the bake's sun puts it -
every other level unchanged. College (`sch`): north at heading 105.6 - its clock tower's faces are square to the axes
(the plaza's face looks along +z: from the plaza at (936.7, -412.8, 1384.1) the tower is at heading -175.6), so at
`tod=06:10` the sun, 10.3 degrees up, rises straight behind the tower seen head-on from the plaza (`start_at=936.7,
-412.8,1384.1,-175.6`). Its own time (15:52) then has its sun at heading 11 instead of the bake's -45: at that hour
the shadows fall 56 degrees round from the original's - the price of the compass; the level's own look is `tod=level`.

**The light at a time** (`day_light_at`; all relative to the level's own time, display-referred like the bake - a
linear ratio r shows as r ^ (1 / 2.2), so night is dim, not black):

- *Direct light*: the sun's transmittance through the sky model's atmosphere (`rt_sky.h`, its luminance over the level's
  sun's; its colour the model's over the level's sun's - warm when low), gone at the horizon. Once the sun is 4 degrees
  down the moon takes over: 0.22 of the level's sun (whole 12 degrees down, rising with the moon's height), blue
  (0.62, 0.74, 1).
- *Sky light*: the model's sky light on a level surface (its single scattering, cosine-weighted over the hemisphere: a
  table made once, -20 to 90 degrees) over the level's sun's; from sunset to 4 degrees down a night floor comes in
  (0.14, 0.18, 0.26 - the moonlit sky's blue; with the moon about the original's own night, its night state's level
  colour 50 65 75 of 128), while the twilight is still brighter than it, so the light never brightens into the night.
- *Lamps*: from 6 degrees up (before sunset) to on 4 degrees down; *stars* from 6 to 16 degrees down; *exposure* (HDR
  only) the light's luminance ^ -0.35, at most 1.3 x.
- *Which light*: the sun, or the moon at night, held at least 6 degrees up for the shadows and relit (a low light's
  shadows cross the whole level - and it is nearly gone by then).

**Where it acts.**

- **The scene's sun** (`rt_scene.h` `scene_sun`): a new origin, the clock's light, above `game` and `level` wherever relit
  relights the level (before that the baked sun; indoors none, as always; the console's override and a fixed
  `rt_sun_source` above it). The shadows, the bounce light, the fog's shafts and haze, the drawn sun and the relit level
  all follow it, as "One sun for every effect" describes. `tod` other than `level` turns `rt_relit` on, as
  `rt_sun_source` does.
- **The relit level** (`rt_relit`). The relit colours are `scale x tint x E' + residual` - the bake taken apart into its
  sun, sky and bounce - so the time scales each part: the sun's term by the direct light (its colour the clock's), the
  sky's by the sky light, the bounce by their mix - each the lighting state's own term ("One lighting path"); the
  bake's sun the rays missed (`sun_missed`) takes the direct light toward the bake's sun only; the residual (the lamps'
  and glows' light the artists baked) stays whole - lamp pools stay lit at night. The sun's rays are traced again as it moves (150,000 vertices a frame), the
  bounce again from the colours at the level's own strength; the colours are written at most 120,000 vertices a frame
  while the clock runs (a new time spreads over a few frames, as the original's time of day colours a part of the
  level a frame). The clock steps every 30 seconds of the day - twice a second with a 24-minute day. At the scales 1 the
  arithmetic is the same as without them.
- **The baked light without relit** - Direct3D 9, Vulkan without ray tracing, a level relit can't take apart (no sun in
  its bake, or opted out): the game's own time of day (`engine/gfx/tod_manager.h`) does it. Every level is loaded with
  four states (`tod_manager.qb`'s default morning, afternoon, evening and night - level colour, sky colour, fog, ambient
  and the two directional lights, bloom; AU's own sun angles) and set to its own (afternoon; NO night). The clock blends
  them by the sun's elevation (`tod_blend`: afternoon above 20 degrees, evening - or before noon, morning - at 4, night
  below -8, smoothly between) and sends them as the manager does: the light manager, the fog, the level's colour
  (sectors coloured a part a frame, per light group - outdoor sectors dim, indoor ones are reset to neutral, as the
  original's groups say). The nearest state is set with `set_tod_instant` as the game's own debug Time of Day menu does,
  so the level's own scripts for it run (`tod_manager_state_transition`: the night geometry and fake lights of BO, BA,
  BE, AU and NO, the cars' headlights). Where relit relights the level the level colour stays the level's own (relit
  dims the bake already); where the backend draws the sky (`rt_sky`) the dome's colour does too.
- **The skater and objects**: without relit, the original's states' ambient and directional lights (blended). Where
  relit relights the level they are lit by the one lighting path (below, "One lighting path"): the game's lights are
  the level's own state's, the key light times the lighting state's direct light, the fill and ambient times the light
  field's indirect light where the thing is; the objects' models the level's own colour times the field's light where
  each stands; unlit instances (BO's cafe table and chairs) the field's light as their instance colour; the particles
  the level's own colour times the level's light as a whole. *Replaced (2026-11-15):* `surface_light` (one global
  darkening for everything relit didn't relight), `lights_by_day`, `CtodManager::colour_models`' global factor and the
  global `unlit_object_colour` - a door under a lamp was darker than the lamp-lit wall round it, the skater darker than
  the road he stood on.
- **The sky** (`rt_sky`): drawn for the clock's sun - under the horizon too: the model's twilight - the atmosphere
  alone (not matched to the level's painted sky since the low sun's sky: its colours held a midday blue at dawn). The
  night sky's light (0.0028, 0.0045, 0.0095 exposed overhead, 1.5 x at the horizon) is added to the sky-view table, so the
  fog's aerial haze and the reflections see it too; the composite adds the stars (cells on a cube about the celestial
  pole, turned with the hours; about 6% of cells hold a star, most faint) and the moon's disc (0.009 rad, twice the
  real one, as the sun's is) before the clouds, which cover them. The clouds (`rt_daylight.h` `cloud_light`): their
  sky light is the drawn sky-view table's (the hour's sky, its night light in it); the sun's light on them - reddened
  by the air to the layer - fades out from the horizon to 6 degrees down (a high cloud keeps the sunset a little
  longer), and with the full moon up 0.5 x the night sky's light is added on them: at night a cloud is a dim, blue
  silhouette barely lighter than the sky round it; continuous through the dusk. The original's sun sprite and lens flare follow the
  clock's sun (`sun::set_sun`), hidden while it is down; the ray-traced disc likewise.
- **The shadows** (`rt_shadows`): their strength x the direct light's share of the light over its share at the level's
  own time (none at twilight, the moon's at night). **The lamps** (`rt_lights`): their strength x the lamps' amount (off
  by day, on at night). **The bounce light** (`rt_gi`): its sun x the direct light and its colour. **HDR**
  (`rt_hdr`): the exposure's factor x the clock's.

**A time set** (added 2026-11-15): without `tod_cycle` the original's colour sweep is done in one step and the models
get their colour every step (an object made is coloured before it is first drawn); only `tod_cycle` spreads the sweep
over steps. Still spread: the relit mode's retrace / writes (150k / 120k vertices a frame).

**The clock is simulation** (2026-11-16, `day_clock.h`): it runs in `engine::render_frame`'s step - every step, drawn
or not, before the views' sun steps (`lens_flare.h sun::step`, which reads the sun it places) - and its time moves by
the step's game time (`frame_clock::get_slomo_frame_seconds`: `tod_cycle_minutes` counts game time; stopped while the
game is paused, as the original's own day). It used to run in `draw_world` on the wall clock, setting the level's
time-of-day state - whose scripts make and remove level geometry with collision and trigger scripts (below) - from the
drawing, at moments that depended on the frame rate and the machine's load; and its level's own day waited for the
renderer's first frame (the baked sun's fit). Now the level's own day and the fact the time of day needs (data there,
a sun in it) come from the start of the level's relit file, read when the level arrives (`relit_data.h
read_relit_head`); data made from other meshes - which only the renderer can tell, on the level's first frame - keeps
relit out but no longer the time of day (the original's states then, as without ray tracing). A demo's playback has
the same time, state and sun on every step at any display rate and load (`tools/game/sim_check.ts`'s time-of-day
table). The drawing only reads the published `DayLight`; `tools/sim_draw_independence.test.ts` "the drawing calls
nothing that runs scripts" follows the calls from render_pc and gpu_vulkan into any function that starts a script.
`scripts_took_over` is an event now: `tod_manager.h tod_state_sets` counts every `set_tod_instant`, and a set the
clock didn't make pauses it.

**What the level's time-of-day scripts change** (the 2026-11-16 survey of every level's node array): the transition
(`tod_manager_state_transition`) kills and creates nodes by name (`<State>On` / `<State>Off`, `DefaultOn/Off`,
`TRG_SFX_TrigBox_Day/Night`) and fades `TRG_<State>_LevelLight` lights. In THUG2: AU 187 nodes (184 LevelGeometry with
collision, 3 sound boxes; a gap start - Kenny Fingers - on a night fountain light), BA 205 (197 geometry, a QP boost
trigger), BE 140 (138 geometry, 2 GameObjects; bulb-shatter triggers, MorningOn_ThunderGhostKill), BO 281 (275 geometry;
the library doors that teleport the skater, a sticker-slap trigger on NightOn_06_BO_NoSkating), NO 382 (377 geometry),
ST 183 (173 geometry, 10 sound boxes), TestLevel 1; the rest none. Ported: THUG1's NJ 57 (34 geometry, 23 sound
boxes), NY 84 (82 geometry), RU 17 (14 geometry), SD 9 and HI 2 sound boxes; KON 19 fake lights only; the others none.
Lighting alone: the fake lights, the cars' headlights and textures, `UpdateFxLighting`. Gameplay: the geometry
(collision, its trigger scripts - gaps, teleports, boosts, goal stickers), the sound boxes (trigger scripts).

**Online** (2026-11-16): the original never changes the time of day in a multiplayer game - `set_tod_instant`,
`reset_level_lights` and `enable_bloom` do nothing there (`tod_lights_locked`; THUGPRO's DLL lifts it for its peers'
time of day), its own cycle runs only in a career game, and a level's load asks for its state through the same locked
call. So in a network session the settings model limits the time of day to its lighting (`settings_model.h limits`,
`Cap::kNetworkGame` - the clock publishes the session as a fact; the Options rows say "Online: lighting only"): the
clock lights the level - DayLight, the original's blended lights and colours, the sun sprite - and never sets the
level's state, so every player keeps the same objects and collision. One rule, no per-level list.

**The original's own time of day wins** (`day_clock.h`): the clock does nothing in the front end, in a career level that
runs its own cycling time of day (`CtodManager::runs_its_day` - career mode, the manager active, `tod_manager_inactive`
not set), during a cutscene's temporary state, and once a script sets the level's state over the clock's (the park
editor's lighting, a network peer's time of day) - until `tod` is changed again. Turned back to `level`, it puts the
level's own state (where it moved it) and sun back. Online it is local and lighting only (above): each player sees their
own time over the same level.

**Checked:**

- `thug2_daylight_check` (processor, no game): the sun's path against an independent rotation of the celestial sphere
  (1920 places, 5 latitudes, 4 dates: within 1e-6 degrees), noon at 34 N / +15 (71 up, due south), the equator's equinox
  sunrise and sunset (due east 6:00, due west 18:00), 34 N at 15:00 (35.889 up, azimuth 240.79); the time at an
  elevation; four baked suns (BO, z_dt, sch, one 84 degrees up) - the path's sun at the level's time is the baked sun
  (1e-4), after noon, and the light then is the level's own (direct and sky 1, no lamps, the afternoon state), half an
  hour later lower and further west; the moon; the light from noon to midnight (the sun's never brightens to dusk, the
  sky's never into the night), dusk's lamps coming on, midnight moonlit, dark but lit, exposure up; the original's
  states' weights changing smoothly; the settings' text; the lighting state's bounce 1 at the level's own time, dark
  and blue at midnight - darker than the original's night; light_colour = direct x tint, the moon's light weak and much
  softer than the sun's; the light field (`rt_light_field.h`): a moonlit street's and a lamp-lit corner's ratios, their
  mix, the nearest cells, the level's whole; `cloud_light` as before without the clock and at the level's own time, at
  midnight no sun and no daylit sky, and no jump from the afternoon into the night.
- `thug2_rt_scene_check`: the clock's light in the scene sun's rules (above game and level when relit, the baked sun
  before, none indoors, below a fixed direction, a zero light ignored).
- `thug2_vulkan_rt_sky_check` (+11): without the night term the sky-view table is the level's to the bit; at noon, dusk
  (3 up), twilight (-6), 21:30 and midnight the device's table against the processor's (`sky_radiance_ms` + the night
  term: mean 0.0002 - 0.0011, 99th percentile 0.0006 - 0.0039 relative), the zenith darkening from noon to night and the
  night's light at midnight; the moon's disc where it is, stars over the night sky (1150 pixels a step over it at 320 x
  240), none with them off; back to `tod=level`, every pixel as before the clock ran.
- `thug2_vulkan_rt_lights_check` (+4): the lamps at noon - every pixel as drawn; at night and at half - the processor's
  lamp light x 1 and x 0.5 (0 wrong); `tod=level` as before.
- `thug2_vulkan_rt_relit_check` (+2): relit at half the light (the clock's scales) within 4 bytes of the processor's
  half-lit bake at 1077 of 1115 vertices; the scales at 1 change no colour.
- The other checks pass unchanged (RTX 5070 Ti): sun 87, relit 27, shadows 45, gi 50, fog 76, hdr 80, cel 24,
  reflections 125, effects 51, rt 22, options 3934; `thug2_ff_shader_check --no-gpu` 0 failed.
- **In the game** (two short runs in a workspace, BO, every ray-traced effect on, `rt_sky=1`, `rt_lights=1`, HDR): BO's own
  time found from its baked sun (heading 286, 52 degrees up: 14:26, north at heading 37); `tod=dusk` with a 1-minute day
  ran through sunset, night and the morning, the original's evening and night states set on the way (their scripts
  run), relit dimming the level (its mean vertex brightness 73 by day), stars and the moon's light at night;
  `rt tod level` gave the level its own state and sun back, `rt tod noon` took it again (71 degrees up). The night's
  floor and exposure were raised after the first run (the skater, lit by the original's night state, stood out
  against a darker level) - not seen in the game since. Not measured: the frame time while the clock runs.

**Not done / limits:**

- One path for every level (34 N, early May): a level's own latitude or date isn't known; per-level paths would be a
  `tod_levels`-like list.
- A level whose bake shows no sun (AP, CA, NO, SE, SE2, the indoor ones) isn't relit: its light is the original's states'
  (level colour, fog, the skater's lights) - its baked lamps dim with the rest; NO's own time is night, so `noon` there
  is the original's afternoon state over a night-baked level, with a day sky.
- Twilight is dark: the sky model's exposure is fixed for a 45-degree sun, so 6 degrees down the sky is already near
  the night's floor (HDR's exposure brings it up partly).
- The clouds (`rt_clouds`) are lit by the model's sky and sun only: at night they are dark shapes over the stars. The
  water's and the per-pixel surfaces' sun glints (`rt_water`, `rt_pbr`) still take the sun's direction from the sky
  model, not the moon's. No moon phases; the moon is always full.
- The sun's direction on the screen follows the engine's convention for headings (from +z toward +x, as `rt_sun_source`'s
  `heading,elevation` and the original's `sun_theta`); east and west are that convention's.
- Day-to-night changes of the original's states run the level's own scripts once per state (a frame's work); the
  clock's own colour sweeps are a light group's part a frame.

**In the game - what to look at** (for the user): BO, z_dt, AU, NO and the Foundry with `tod=dawn`, `noon`, `dusk`,
`night` and `tod_cycle=on` with `tod_cycle_minutes=12`: the sun's place against the shadows; the sky at sunset and the
stars and moon at night; the level dim and blue but readable at night, its lamps lit (with `rt_lights=1`) and its night
geometry (BO, BA, BE, AU, NO); the skater's light; switching `tod` back to `Level` in the Options screen giving the level
back exactly; a career level's own time of day unchanged; the frame time while the clock runs (relit's rewrites).

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

### Frame generation and Reflex (`frame_generation`, `reflex`)

Frames made between the drawn ones from their motion, the HUD kept out of them - `src/gpu_vulkan/vulkan_framegen*.cpp`
(`vulkan_framegen.h` has the frame's steps). `frame_generation=off` (the default) and `reflex=off`: nothing of it is
loaded or made - Streamline's features are DLSS's two as before (or Streamline isn't loaded at all), the swapchain is
Vulkan's own, no queue or feature is added, no listener runs (`thug2_vulkan_framegen_check`'s "off" case).

- **Providers.** `frame_generation_provider=auto` (the default): DLSS Frame Generation (Streamline 2.14.16's sl.dlss_g
  with nvngx_dlssg, NGX 310.9.16) where Streamline says the device and driver have it (an RTX 40 / 50, Windows' hardware
  GPU scheduling on), else AMD FSR 3.1.7 frame generation (FSR SDK 2.4.0's amd_fidelityfx_framegeneration_vk.dll; FSR 4's
  on an RDNA 4 GPU) on any device with three spare queues that present. `dlss` / `fsr` force one (none when it can't run).
  The multiplier: DLSS-G up to the device's `numFramesToGenerateMax + 1` (2x on an RTX 40; 4x offered of the 6x an RTX 50
  has), FSR 2x.
- **Inputs.** The upscaler's: its depth (R32F) and motion (0 - 1 of the drawn size, jitter out) at the drawn size, its
  unjittered camera - frame generation turns the upscaler on at native resolution (`auto`, DLAA / FSR native AA) when
  `upscaler=off`. DLSS-G gets Streamline's common constants once a frame (DLSS's provider gives them when it is the
  upscaler, else frame generation does - `streamline_constants`; one frame token shared by DLSS, DLSS-G and Reflex's
  markers), the depth and motion tags, the HUD-less colour and the UI image (`kBufferTypeUIColorAndAlpha`). FSR gets its
  "prepare" dispatch after the upscaler (the depth, motion, camera position and axes, `viewSpaceToMetersFactor` 0.0254)
  and, each frame, its configure (on / off, the HUD-less colour, the frame id moving by one a present).
- **The HUD.** A listener last at `world_drawn` (`ViewOrder::kFrameGeneration` = 999, after the art style and the grade)
  copies the back buffer out as the HUD-less colour (RGBA8, the swapchain's size, the present's own scaling). At the
  present a compute pass compares it with the finished back buffer: where they differ the 2D is there - the UI image
  holds the frame's colour, alpha 1 (premultiplied, so Final = UI + (1 - a) HUD-less holds exactly); elsewhere clear.
  DLSS-G composes the 2D over its made frames from them (and detects full-screen menus from them); FSR finds the 2D
  from the HUD-less colour itself. Not done: user-interface recomposition (DLSS-G's second UI path), FSR's UI callback.
- **On and off.** A frame is on when the upscaler gave inputs, the HUD-less colour was copied, and it isn't under a menu
  (`ViewDrawn::menu`: the pause menu, the front end, the level editor). Off: loading screens (no 3D view), split views
  and pictures (not the back buffer), the full-screen gamma ramp (the presented frame goes through it, the HUD-less
  colour doesn't), a multisampled back buffer. DLSS-G is turned off with `eRetainResourcesWhenOff` (no stutter coming
  back), its tags set to null; FSR's `frameGenerationEnabled` false. The first frame back on, and a cut
  (`frame_blend::cuts`: a teleport, a respawn, a camera snap - the upscaler's history reset then too), reset the history.
- **Present.** DLSS-G: Streamline's swapchain proxies (already routed for DLSS) - its plugin makes the swapchain off
  screen and presents the made frames from its pacer. Streamline must see the window's surface made (its
  `vkCreateWin32SurfaceKHR` hook), so with sl.dlss_g loaded the context's surface is made again through Streamline once
  the device is given to it (`ContextAdditions::create_surface`). The UI's own windows' swapchains are made with sl.dlss_g
  unloaded around them (`slSetFeatureLoaded`). FSR: its frame-generation swapchain replaces the context's
  (`SwapchainOwner`: made, listed, acquired, presented and destroyed through FSR's replacement calls - a resize remakes
  it whole) on three extra queues of the graphics family (present, image acquire, compute - its swapchain wants them
  apart; where the graphics family has one queue - AMD's - of another family that presents and computes:
  `DeviceWants::present_queues`), timeline semaphores on; its images are handed back in the shader-read layout its
  passes read them in; its threads' presents are waited for before the device is waited idle.
- **Reflex** (`reflex=on` / `boost`; always low latency with DLSS-G): Streamline's sl.reflex (NvLowLatencyVk,
  VK_NV_low_latency2 with VK_KHR_present_id) and sl.pcl. The main loop's markers (`GpuContext::latency_marker`): the
  simulation's start - `slReflexSleep` waits there for Reflex's time to begin - and end, the draws' recording; the
  present's start and end around `vkQueuePresentKHR`. A render-only frame (`max_fps`) is a frame of its own (its
  simulation markers back to back). On a device without it: logged, nothing done.
- **With `max_fps`.** Frame generation multiplies whatever is presented: each render-only frame is a drawn frame to it.
  Aim the drawn rate x the multiplier at the display: 60 Hz ticks (`max_fps=0`) + 2x for 120 Hz, + 4x for 240 Hz;
  `max_fps=120` + 2x for 240 Hz; `max_fps=uncapped` + 2x on a G-SYNC / FreeSync display with `present_mode=immediate`.
  With `present_mode=vsync` the frames shown are held to the display's rate (DLSS-G on Vulkan has no v-sync of its own -
  Streamline: "VSync with FG: not supported"): `mailbox` or `immediate` let the made frames through.
- **Latency.** Interpolation shows a drawn frame only after the frames made before it: about one drawn frame's time more
  (16.7 ms at 60, 8.3 ms with `max_fps=120`); Reflex takes back the queueing a busy GPU adds. A higher drawn rate (the
  upscaler's quality down, `max_fps` up) costs less latency than a higher multiplier. DLSS-G makes frames only while the
  window has the focus.
- **Counted.** `frame_generation_counts()` (the provider, multiplier, features made / released, frames on / off, frames
  shown as the provider reports them - `slDLSSGGetState`'s `numFramesActuallyPresented`, FSR's present count - and the
  resets); `GpuContext::frames_shown` (the FPS counter's "shown (frame gen)" line); the log a line every 3600 frames on.
- **Checked:** `thug2_vulkan_framegen_check` (the RTX 5070 Ti, 1280 x 720, a moving camera over a checkered ground, a
  HUD quad drawn after the world): off - no provider, no feature, no frame shown but the drawn, Vulkan's swapchain, no
  Reflex, the upscaler still running; DLSS-G 4x - the feature made (Reflex on, Streamline's swapchain), on for every
  moving frame, off under a menu and back on with one reset, a cut resetting once, released at the end; FSR 2x - the
  same, and 368 frames shown for 244 drawn with it on (the made ones). DLSS-G made no frames in the check: Streamline's
  log says "DLSS-G disabled: window not focused" - the check's window never takes the focus (the user may be at the
  machine), so its frames are noted, not checked. With validation: none of ours; inside FSR's own swapchain a pipeline
  with line width 0 and 13 of its objects left at the device's end, inside Streamline's pacer layout and semaphore notes
  on its own images.

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
  256, dark is low), normals by a Sobel filter x 3 x the texture's bump (below), roughness = the class's base ± half its range by
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
  | asphalt | 0.75 - 0.95 | 0.88 | 0.04 | 0.4 |
  | concrete | 0.7 - 0.95 | 0.85 | 0.04 | 0.6 |
  | stone, brick | 0.6 - 0.95 | 0.8 | 0.04 | 1.3 |
  | metal | 0.25 - 0.6 | 0.4 | 0.6 | 0.4 |
  | wood | 0.55 - 0.85 | 0.7 | 0.04 | 0.7 |
  | glass | 0.05 - 0.2 | 0.1 | 0.04 | 0 |
  | water | 0.03 - 0.12 | 0.06 | 0.02 | 0.2 |
  | foliage | 0.6 - 0.85 | 0.75 | 0.04 | 0.6 |
  | ground (dirt, sand) | 0.8 - 1.0 | 0.92 | 0.04 | 1.0 |
  | fabric, skin | 0.7 - 0.95 | 0.85 | 0.04 | 0.5 |
  | tile (glazed) | 0.2 - 0.5 | 0.35 | 0.04 | 0.6 |

- **Bump** - each texture's own (`surfaces.ts`), baked into its normals (no field in the file: it comes from the level,
  so changing it means running the tool again anyway, and old files stay as they are). The classical generator turns
  every speck of luma into relief; on a surface smooth by definition - a street, a slab, a sidewalk - that read as a
  field of melted lumps, worst where an old 64 - 128 texel texture is stretched over a whole street (each speck a lump
  a hand wide) and with `rt_pbr_normal_strength=2`. Two pieces of evidence from the level move the class's bump:
  - *the terrain* under the faces the texture is drawn on: each scene triangle (a material's first pass) is matched to
    the face of the collision object of its sector's checksum that holds its centre (plane within 6 units, normals
    within ~45 degrees, 2 units past the edges allowed) - in LA ~60% of the drawn area lands on a telling terrain (the
    rest DEFAULT); BO's huge backdrops have no collision, its streets do. Each terrain has a bump (smooth concrete 0.2, asphalt 0.2, sidewalk 0.25, glazed
    tile 0.3, smooth metal 0.15, masonite 0.2, rough concrete 0.6, wood 0.6, brick 1.3, rock 1.3, gravel 1.2, shingle
    1.0...; DEFAULT and GENERIC say nothing). A terrain is picked for its sound and a class from colour statistics, both
    err (THPS3's LA paves its street with rough concrete; a facade's wall is "smooth concrete"), so the bump is the
    geometric mean of the terrains' (area-weighted) and the class's, leaning to the class's when the telling terrains
    cover less than half of the texture's area;
  - *the texel size*: world units per texel (the area-weighted geometric mean of sqrt(world area / uv area) over the
    triangles, over sqrt(width x height)); coarser than 1.5 units (THUG2's units are about an inch) scales the bump by
    1.5 / size, down to 0.25 - a bump's slope is its height over its width, and a speck's height doesn't grow with the
    texel it was stretched over.

  LA (THUG2): the street (asphalt + smooth concrete, 3.7 units a texel) 0.8 -> 0.14, sidewalks and slabs 0.8 -> 0.2 -
  0.35, its brick 1.3 -> 1.29; THPS3's LA (converted): its brick (rough concrete there) 1.3 -> 0.88, slabs 0.5 -> 0.19.
  A model's maps (CHORD) are scaled to the same bump (x and y times bump / the class's, renormalised). The pictures
  (`--pictures`) are the textures covering the most of the level, named with their class, bump and evidence.

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

### Detail materials and de-lighting (`rt_detail`, `rt_delight`)

`gpu_vulkan/vulkan_detail` + `ff_key.h` `kDetail*` (the fragment key's `detail` byte, a former spare: the key stays
52 bytes), `[Graphics] rt_detail` / `rt_detail_strength`, `rt_delight` / `rt_delight_strength` (all off by default).
Both act only on the draws `rt_pbr` lights with a surface map (`kSurfaceMap`, never with cel shading's bands).

- **The library** (`formats/detail_material_format.h`: "T2DM" v1, `thug2-decomp-materials.t2dm` in the game folder)
  is made on the player's machine: `node tools/detail_materials/detail_materials.ts fetch` downloads one CC0 scan per
  kind (`materials.json`: the URLs, MD5s, sizes and licences - Poly Haven, every asset CC0 per polyhaven.com/license;
  1K PNGs, ~130 MB for the twelve, into `out\detail_materials`; nothing committed), `pack --game <folder>` makes each
  kind's detail texture (`detail_texels.ts`: the scan's grain only - the normal and roughness less a wrapping box blur
  of a sixteenth of the tile, the colour's luminance over its local mean (0.25 - 2) - so the scan's own layout, brick
  courses or slabs, doesn't print over the game's) and assigns every texture that gets a surface map a kind, its world
  size and its mean luminance.
- **The kinds** (asphalt `asphalt_02`, smooth concrete `concrete_floor_02`, rough concrete `granular_concrete`, brick
  `brick_wall_02`, stone `rock_04`, metal `metal_plate`, wood `fine_grained_wood`, dirt `dirt`, grass `sparse_grass`,
  tiles `interior_tiles`, plaster `plastered_wall`, gravel `gravel_floor`) and each one's real material - roughness,
  the detail's roughness swing, F0, metalness, how far the kind's roughness replaces the map's - in `materials.json`.
- **Which kind.** `texture_terrain.ts` looks every scene triangle up among the level's collision faces by its centroid
  (to 1/8 unit: the collision is made from the same geometry) and counts its area under the face's terrain type;
  `kinds.ts decideKind`: glass and water never; a terrain kind (CONCSMOOTH / SIDEWALK / WETCONC smooth concrete,
  CONCROUGH rough, ASPHALT, BRICK, ROCK stone, METAL*, WOOD*, DIRT / DIRTPACKED / SAND dirt, GRASS*, TILE, GRAVEL)
  covering half the matched area; mostly DEFAULT faces, at most 30% of the area level and a painted (default) class:
  plaster; else the surface map's class (`classes.ts`, the same classification as the maps). A texture the library
  doesn't list (made before a level was added) takes its class's kind in the game too. All the main install's levels
  (2026-11-14): 19,037 textures - smooth concrete 7,456, none 2,624, stone 1,945, wood 1,846, plaster 1,404, metal
  1,279, grass 861, rough concrete 436, dirt 369, tile 287, brick 281, asphalt 200, gravel 49; 6,505 by terrain, 12,532
  by class. `pack --report <file.tsv>` lists each texture's evidence.
- **The shader** (`ff_shaders.cpp` "the detail layer"; `DetailUniforms` at binding 15, the kind's texture at 16 - a
  1024-square RGBA8 texture with its mips, sampled repeating, trilinear, 8x anisotropic): the detail at stage 0's
  coordinates x the texture's repeats (its metres per texture unit - the area-weighted median over its triangles -
  over the scan's tile size); its weight = strength x (1 - smoothstep(0.5, 3, log2 of its texels a pixel)) - it fades
  as it shrinks, so far away the original look is untouched; its slope added to the map's tangent-space normal (x the
  map's z) before the per-pixel frame; the roughness moved toward the kind's and swung by the detail's; stage 0's
  colour x mix(1, the detail's luminance ratio, weight) - the luminance only, the texture keeps its colour; the specular
  tinted by the texel's colour by the kind's metalness. The kind's F0 replaces the map class's in the per-pixel
  highlight and - `surface_map_f0()` - in R3's material table (`MaterialRecord::f0`: the reflections and the hit
  shading).
- **De-lighting** (`kDetailDelight`): stage 0's colour x (mean / local)^strength, clamped to 1/1.6 - 1.6 - the local
  luminance a coarse mip's (8 texels across the texture), the mean the library's (else the texture's last mip),
  bilinear by `texelFetch` (the game's samplers often allow no mips). It flattens the shadows, occlusion and highlights
  painted into the 2004 colour maps toward the texture's average, leaving the fine detail, so the per-pixel and
  ray-traced lighting do the shading. In the shader: the player's files are not changed. Glass and water are left.
  Per texture with a de-lit texture set (`base.ts --delight`: the pipeline's `hd_textures/delight.ts` took the painted
  light out of some textures before upscaling - about 1% with its version 2, only where the evidence is clear): the
  set's `set.json` lists each file's de-lit textures ("delit", checksums - `formats/texture_set_record.h`). As a
  dictionary loads, its surface maps (`render_pc/surface_map_load.cpp`) take that file's list from
  `texture_sets::delit_textures(set, file)` and attach each map with `MapInfo::delit`; `detail_draw` gives a texture
  that came de-lit no `kDetailDelight` - one rule, in the key - and every other texture, in the same set too, keeps
  the setting. A set switch reads the dictionaries again, so their maps carry the new set's lists. The settings model
  has no rule for it: `rt_delight` stays in effect whatever set is chosen (Options: "textures that aren't already
  flat"). `thug2_vulkan_pbr_check` draws the same texture listed and not listed; `thug2_format_check` reads a record.
- **Off is as before.** With both off no key has `detail` set: every shader is the same - `thug2_ff_shader_check
  --no-gpu --surface --cel --motion --retro --dump` wrote 341,478 shaders byte-identical to main's. `--detail` (after
  `--surface`) adds every surface-mapped key with each of the three bit sets.
- **Checked.** `thug2_vulkan_pbr_check` (+6): a library handed over from memory (`load_detail_library`), the detail at
  full weight scales the albedo by its ratio exactly (0.5020, every channel), faded out the picture is as without it,
  the de-lighting flattens a half-dark, half-bright texture's 3:1 contrast to 1.00, half strength to 1.73 (its square
  root); validation 0. `thug2_format_check`: the file's reader and the TypeScript writer on one sample (the same bytes).
  `tools/detail_materials/*.test.ts`: the file, the kinds' rules, the detail texels (neutral scans, the normal's +v),
  the PNG reader (8 and 16 bits), `materials.json`.
- **Not done yet:** the bounce (GI) and hit shading read the material table's F0 but not the detail layer; skinned
  objects and models' dictionaries get no detail (their maps - "Characters" - are shaded as skin, cloth and hair instead); the kinds' numbers are first guesses for
  the user to tune in `materials.json` (re-run `pack`).

### Surface relief (`rt_relief`)

Brick, cobbles, stone, gravel, tiles and wood planks look flat even with `rt_pbr`'s per-pixel normals: the texture
doesn't shift with the view as a carved surface would. `[Graphics] rt_relief=1` (off by default; `rt_relief_strength`
0.5 / 1 / 1.5, ini up to 4) adds parallax occlusion mapping to the draws `rt_pbr` lights with a surface map. It is a
bit of the fragment key's `detail` byte (`kDetailRelief` - a free bit: the key stays 52 bytes) and the detail block's
last vector (`DetailUniforms::relief`, the former spare), so it needs no new binding.

- **How deep, per texture.** The surface maps' height (A) is the generator's band-passed luma, 0 - 1; its normals were
  made from it as (-dh/du k, -dh/dv k, 1) with k = 3 x the texture's bump - its class's, moved by its level's terrains
  and weakened for coarse texels (`tools/pbr_maps` `surfaces.ts bumpFor`). So the height's full range spans k of the
  map's texels, and that is the relief's depth: the shading and the parallax agree. k isn't in the file - it is read
  back from the map when it is attached (`relief_depth_of_map`: the least-squares ratio of the normals' slopes to the
  height's wrapping Sobel gradients - over AU, BA and AP's maps it gives exactly 3 x the generator's bump: stone median
  3.6 texels, wood 2.1, concrete 1.8), so the existing `.pbr` files work unchanged. Then x the surface kind's weight
  (`relief_weight`; the detail-material library's kind with `rt_detail`, else the map's class's): brick and stone 1,
  gravel 0.8, wood 0.7, tiles 0.6, rough concrete and dirt 0.5; smooth asphalt and concrete, metal, grass, plaster,
  glass, water, fabric and painted textures 0 - their maps' grain is paint and stains, not depth - x the strength.
  Below 0.05 texels the bit isn't set: those draws keep their shader.
- **The shader** (`ff_shaders.cpp` "parallax occlusion mapping", before the surface map is read): the gradients of u
  and v along the surface from the screen derivatives (the per-pixel lighting's frame with its length kept), the depth
  in eye units D = depth x w / sqrt(|grad u| W |grad v| H) (W x H the map's size), the ray toward the pixel, p = (grad
  u . r, grad v . r) D / (-r . N) - the coordinates' change from the height's top to its bottom along the ray. The ray
  starts at height 1 half a p back (so the height's 0.5 stays at the polygon: the texture doesn't slide on average and
  seams between textures line up), steps down in mix(32, 8, -r . N) layers until the map is at or above it, then halves
  the last layer 5 times; stage 0, the surface map and the detail layer are read at the coordinates found - with
  `textureGrad` and the original coordinates' derivatives, so the steps in the result aren't steps in the mip level.
  Later passes of a multi-pass material keep their own coordinates (they may be mapped differently).
- **Fades** (no swimming, no pop): w = (1 - smoothstep(0.5, 2.5, the map's level of detail)) x smoothstep(0.05, 0.2,
  -r . N); D goes to 0 with it - far away (from 1.4 map texels a pixel; gone at 5.7, where the whole relief shifts less
  than a pixel) and at grazing angles the relief flattens smoothly into the plain picture. The map's own mips (heights
  averaged) flatten it further with distance.
- **The sun's shadow** (the world's bake relit, not objects lit by lights): from the point found toward the sun, 8
  reads up to the top; the most the height stands above that ray, x 4, clamped, x 0.6 x w, darkens the sun's N.L in the
  bake's relighting and its highlight. Mortar and the gaps between cobbles fall into shade on the side away from the
  sun.
- **No depth is written** - `gl_FragDepth` would turn off early depth testing for every relief draw. So the relief has
  no silhouette (a wall's edge and the outline of a step stay straight), doesn't hide what stands in front of it at its
  depth, and casts no shadow on other things (ray-traced shadows and reflections see the flat polygon).
- **Tessellation**: `rt_tessellation` subdivides only curved faces, at load, on the processor - the flat walls and
  floors relief is for get no new vertices there, so displacing them would need a subdivision of its own (more
  triangles for every brick wall, for rays as well). Not done; parallax is the whole of it.
- **Off is as before.** No key has the bit with it off: `thug2_ff_shader_check --no-gpu --dump` writes the same
  50,180 shaders byte for byte as main's. `--detail` (opt-in, with `--surface`) adds the relief alone, with the detail
  layer, and with both detail bits.
- **Checked.** `thug2_vulkan_pbr_check` (+17, both GPUs): a brick-patterned height map (mortar 0, bricks 1 and 0.6) whose
  normals are made from it with k = 2.5 - read back as 2.485; flat normals: depth 0; at five view angles (facing, 35 /
  20, -50 / 10 mirrored, 20 / -40 with the mapping turned, 60 both mirrored) every judged pixel's colour (each texel its
  own) against the processor marching the same ray through the same heights: 99.96 - 100% the same, 2,320 - 5,524
  pixels moved to another texel by the relief; off, a smooth-concrete kind, a map without bump and the fade ended below
  the texels' size: the picture exactly as with relief off; the texture repeated 48 times (~5 texels a pixel): with the
  default fade the change 0.0000, unfaded 0.0004 (the mips already flatten most of it); the sun's shadow darkens 19,578
  channels and brightens none; lit by lights, and with the detail layer and the de-lighting: drawn, finite; validation 0.
  `thug2_options_check`: the two rows.
- **Cost** (`thug2_vulkan_pbr_check --perf`, a full-screen 1920 x 1080 layer facing the camera - the cheapest view, 8
  layers + 5 halvings + 8 shadow reads; grazing views take up to 32 layers): RTX 5070 Ti 0.09 ms a baked map, 0.29 ms
  with relief; the AMD Radeon integrated GPU 2.2 ms, 3.8 ms. Only near relief kinds pay it.
- **Not done yet:** the game run (the look - depth, shimmer at grazing angles, seams - is the user's to judge); the
  bump isn't stored in the `.pbr` file (read back instead - a CHORD map whose normals were rescaled to the texture's
  bump reads back that bump too, but a hand-made map with unrelated normals and height would get a wrong depth); the
  kinds' weights are first guesses.

### Characters (`rt_characters`)

The skaters, the CAS parts and the peds were the flattest things on screen with `rt_pbr` on: one texture of painted skin,
flat clothes, an ordinary highlight on hair. `[Graphics] rt_characters=1` (on by default since 2026-11-15 - nothing changes without the maps; with `rt_pbr`) gives their
models surface maps of their own and shades skin, cloth and hair as such; `rt_characters_skin` / `_cloth` / `_hair`
(0 - 2, default 1) set each; `rt_ao_characters` (0 - 1, default 1) is the ray-traced occlusion's strength on them.

- **Why the skinned draws had no maps.** Nothing in the draw path left them out: `pc_mesh_render`'s skinned meshes
  draw with the material's pixel shader (or the coloured one), lit by lights with normals, and `surface_draw` gives
  any such draw `kSurfaceLighting | kSurfaceLit` and - with a map on its first texture - `kSurfaceMap`; the model
  dictionaries (`load_model_textures` -> `load_tex_dict`, by name) are looked up for a `.pbr` like the levels'. The tool
  simply made none: `tools/pbr_maps` walked `levels\` only. So without maps they were already lit per pixel with
  their vertex normals - interpolated, smooth: the faceting fix the rays needed (the depth's per-triangle normals,
  `rays_mask`) is not needed here, and the maps' tangent frame (per triangle, from the screen derivatives) bends that
  smooth normal; skinned meshes get the same smooth shading in the per-pixel path as anything else.
- **The maps** (`tools/pbr_maps/models.ts`; `pbr_maps.ts make --game <folder> --models`, `--models-only`;
  `unpack_game.ts --pbr-maps` makes them too): the character dictionaries - `models\Skater_*`, `Ped_*`, `Peds`,
  `Cutscenes`, `Animals` (shadows and everything else under `models\` left out), loose, in `Data\pre`'s archives or the
  data overlay - with the model's `.skin.xbx` read as a scene for the materials (overlays, environment passes skipped
  as for the levels). Each entry carries `kFlagModel` (`formats/surface_map_format.h`, the entry's former spare
  flags byte; 0 in every older file): the game uses it only with `rt_characters` on - off, the draw is exactly as with
  no map (lit per pixel with its normals, as before), and neither the material table (`surface_map_for`) nor the
  reflections' F0 see it. No detail layer, de-lighting or relief on them.
- **The classes** (six new ones, 12 - 17; an older reader takes them as the default class):

  | class | roughness | base | F0 | bump | shading |
  |---|---|---|---|---|---|
  | skin | 0.45 - 0.7 | 0.55 | 0.028 | 0.08 | `kDetailSkin` |
  | cloth | 0.75 - 0.98 | 0.88 | 0.04 | 0.9 | `kDetailCloth` |
  | denim | 0.7 - 0.95 | 0.85 | 0.04 | 1.1 | `kDetailCloth` (sheen x 0.6) |
  | leather | 0.3 - 0.65 | 0.45 | 0.04 | 0.45 | GGX alone |
  | rubber (soles, pads) | 0.6 - 0.9 | 0.75 | 0.04 | 0.6 | GGX alone |
  | hair | 0.3 - 0.6 | 0.42 | 0.046 | 0.6 | `kDetailHair` |

  plus the world's metal (watches, rings, chains, buckles) and wood (boards). Skin's relief is pores and creases far
  below a 32 - 128 texel face: almost none; cloth's and denim's weave and painted folds strong.
- **How a texture is classed** (`classifyCharacter`): the game's dictionaries keep no texture names, but the CAS parts are
  one dictionary each, so the dictionary's name is the first evidence - `Hair_*` (and bangs, mohawk, ponytail...) hair;
  `Hands`, `Face_*`, `Head_*`, `*_Legs` skin; `pant_*`, `shorts`, `cargo` legwear (denim when blue: hue 195 - 240,
  saturation 0.15 - 0.65; else cloth); `*leather*` leather; `*denim*`, `*jean*` denim; shoes, kneepads rubber; shirts,
  hoodies, jackets, hats, socks cloth; watches, rings metal; boards wood; `Anl_*` fur (cloth). Then each texture's
  skin share (`skinShare`: the share of its visible texels whose colour is skin's - red over green over blue, hue
  (g - b) / (r - b) 0.1 - 0.9, saturation (r - b) / r 0.11 - 0.75, not near black): a skin-named dictionary's texture is
  skin from 30% (else default - eyes, teeth, brows: plain per-pixel lighting, no cloth bump embossing an eye); a whole body's (the pros, the peds, the
  cutscenes: `Skater_Hawk`, `Ped_*`, `*_BODY`, no telling name) skin from 50%, else denim when blue, else cloth; a
  garment is never skin - a brown jacket and a tanned arm are the same colour - but a garment with 15 - 85% skin texels
  (an atlas of a shirt and the arms in it) gets `kFlagSkinTexels`: its skin-coloured texels are shaded as skin. Hair
  keeps its cut-out strands (the level rules' cut-out skip waived, as foliage's). A hair texture records which way its
  strands run (`kFlagStrandsU`: its luma changes more along v than along u). On the main install: 628 dictionaries,
  1,452 maps in 27 s (60 MB) - cloth 640, skin 474, denim 81, default 78, wood 71, hair 57, rubber 32, metal 15,
  leather 4; skipped 674 (269 overlays, 191 see-through, 120 cut-outs, 92 flat art).
- **The shading** (`ff_shaders.cpp` "the characters", `character_lighting`; only with `kSurfaceMap` and `kSurfaceLit`:
  a character's map lit by the draw's lights): three free bits of the fragment key's `detail` byte - `kDetailSkin`,
  `kDetailCloth`, `kDetailHair` (the key stays 52 bytes; `normalised()` keeps them only lit by lights, never with the
  detail layer's bits, hair alone) - and a fifth vector of the detail block, `DetailUniforms::character` (strengths,
  how far skin follows the texel's colour, the strands' axis; the other detail blocks stop before it, so their shaders
  are unchanged). Per light:
  - skin: the light wrapped round the terminator per channel, w = (0.45, 0.2, 0.12) k, (N.L + w) / (1 + w) clamped -
    red reaches furthest into the shade, as light scattered under the skin does; a faint translucency, (1, 0.35, 0.2)
    x 0.25 k x max(V.-L, 0)^4 x (1 - |N.V|): light from behind through the thin edges seen edge-on (ears, fingers).
    k = `rt_characters_skin` x the texel's skin weight (stage 0's colour against skin's, `skinTexel` with soft edges) -
    followed 60% on a skin texture (eyes, brows and lips less skin), wholly on a garment's skin texels;
  - cloth: a sheen, 0.35 x s x (1 - N.V)^3 x (0.5 + 0.5 N.L), added to the light - a soft rim at grazing views, tinted
    by the cloth because the light is multiplied by the texture; s = `rt_characters_cloth` (x 0.6 denim; x (1 - skin
    weight) on a garment with skin texels);
  - hair: Kajiya-Kay's highlight along the strand - T stage 0's u or v direction from the per-pixel frame, shifted
    along the bent normal by (the map's height - 0.5) x 0.6 so the highlight wavers along the strands; sin(T, H)^e x
    0.4 x smoothstep(0, 0.2, N.L), e = 2 / roughness^4 (4 - 512) - in place of GGX's (x `rt_characters_hair`; GGX's
    x (1 - it) below 1).
  At strength 0 a class's bit isn't set: that draw is a plain surface-mapped one.
- **Occlusion on characters** (`rt_ao_characters`): the AO's rays already find, per pixel, whether it shows an object
  (`rays_mask`, a ray from the camera against the objects alone); where that object's instance is skinned
  (`rt::kSkinned` in the geometry table's instance flags) the pixel's occlusion is x the setting (the trace's push
  constants, per mille) before the history - the skater in a dark corner keeps more of his own light. 1 (the default)
  leaves every pixel as before.
- **Off is as before.** `rt_characters=0`: model maps unused (`map_used`), no character bits; `thug2_ff_shader_check
  --no-gpu --dump` writes the same 50,180 shaders byte for byte as main's. `--characters` (with `--surface`) adds every
  lit surface-mapped key with skin, cloth, both and hair (32 fragment keys: 99,074 shaders, none failed).
- **Checked.** `thug2_vulkan_pbr_check` (+19 checks, the RTX 5070 Ti and the AMD Radeon, validation 0): a model's map
  classed skin (lit from the front, from behind for the translucency, at strength 2 with u mirrored), cloth, denim with
  skin texels lit from behind, hair with its strands along v, along u with the mapping turned 90 degrees, at half
  strength - each of 11,775 judged pixels against the processor's same formulas with the quad's own tangent frame:
  100% within 0.003, the worst 0.0006; each moves 4,800 - 11,775 pixels from a plain map's picture; `rt_characters`
  off with a skin and a hair map (from behind): the picture of no map; at strength 0: a plain map's. `thug2_format_check`
  and `pbr_file.test.ts`: the class and flags byte. `tools/pbr_maps/models.test.ts`: the dictionaries picked, the names'
  hints, skin's texels, the rules, the strands' axis. `thug2_options_check`: the five rows.
- **Cost** (`thug2_vulkan_pbr_check --perf`, a full-screen 1920 x 1080 layer): RTX 5070 Ti 0.044 ms a plain lit map,
  0.056 skin, 0.060 a garment with skin texels, 0.059 hair; the AMD Radeon integrated GPU 2.09, 2.66, 2.75, 2.52 ms.
  The characters cover a few percent of the screen.
- **Not done yet:** the game run (the look is the user's to judge - see the checklist); a texture the CAS rebuilds into
  a new texture object (`PcTexture::copy_from`, an edit's kept copy) has no map (recoloured in place it keeps it); the
  asset loader's dictionaries handed over as memory (`load_tex_dict_from_memory`) get none, as the levels'; the classes
  come from names and colour, so a skin-coloured garment in a whole-body atlas is skin; leather and rubber have no
  shading of their own beyond their roughness; the translucency uses the silhouette as its thickness (no thickness map).

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
  `rt_style_outline_width` (1), `rt_style_saturation` (1.15). The Options screen's Style tab: Art style (its
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
tab: `rt_style` first, then every style's knobs (`rt_style_*`, `rt_cel_*`), each in effect only while its style is picked
(the settings model's needs - docs/design/settings.md; greyed otherwise). The four looks here (`gpu_vulkan/vulkan_style_looks`, formulas in its header) reuse the toon filter's passes and
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
  fit (README). Options: the Style tab's rows for each, each in effect only under the looks that use it (the
  settings model's needs, engine/config/settings_model.cpp). The push constants grew to 192 bytes (four look vectors; the device's limit checked).
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

### Snow (`snow`)

`engine/gfx/snow.h` (where it settles, the trails, the spray - on the processor), `render_pc/snow_world` (the world read),
`gpu_vulkan/vulkan_snow` (the cover, the layer, the air) - `[Graphics] snow` (off by default), `snow_depth` (4 inches),
`snow_trail_quality` (2), `snowfall` (1), all live. No ray tracing needed (with it, each surface's own view of the sky
and the flakes' light are traced); Direct3D 9 draws none. The way RDR2 does its snow: a thin layer of snow over the
level and a heightfield of what was pressed into it - not grains. Round 2 (the user's feedback): the default depth 8
inches (choices 3 / 8 / 14 / 20, at most 24), the slope's smooth curve, the traced sky visibility, the cover replacing
the surface's whole colour, body contacts and movers pressing, the falling snow as flakes in the world with a haze, the
winter sky in the lighting state, the decals under the snow. Round 4 (the user: "snowfall still kind of looks like a
filter in front of the screen"; depth only where the trails were): the flakes anchored in the world on every axis with
their own motion for the upscaler, streaks, the gusts' eddies, depth layering and the snow under them lighting them;
the snow's layer over all the open ground round the camera, banked against kerbs and walls.

- **The slope** (`snow::slope_cover`, the shader's twin): full on flat ground and gentle slopes (normal y 0.93 and over),
  thinning up ramps and banks to a light coat (0.3 of it) on steep faces - a quarter pipe's (y 0.3 - 0.4) -, none on a
  near-vertical wall (y 0.08 and under); never thicker for a steeper face. A light coat lies in patches (a noise over the
  surface: where it is thin only its higher patches hold snow). The sky map's cover is the same curve.
- **Open to the sky, traced** (`kTrailSnowTraced`, a device with ray tracing and a scene to trace): each covered pixel
  asks the frame's TLAS with a ray query straight up from just off its surface (its start moved up to 1.5 units by a
  smooth noise over the ground, so an eave's line isn't ruled) - nothing over it within 4000 units: open. Mask 0x05 -
  the level and the cut-out casters, opaque (trees, awnings and fences keep the snow off what is under them), not the
  objects (a car driving off leaves no clear patch). So a surface under anything drawn holds none, and one open to the
  sky does however steep or near something else - a rail under a ledge none, the ledge's top, a bench's seat, a car's
  roof, a stair's treads all. The TLAS is bound at the cel shading's binding (9) - per pixel, every frame: no cache to
  go stale (the cost below). Without ray tracing the sky map's cells, as before.
- **The surface's whole colour** (`kTrailSnowTint`): a relit world draw's vertex colour is the tint the bake gave it x
  the light; the cover reads the relit stream (`kVertexCelStream`, the cel shading's binding 2 - render_pc names the
  source while the snow is on) and mixes the albedo toward the snow over the tint (word 1), so a road's dark asphalt, a
  lawn's green and painted lines don't show through the snow - only the light stays. The pass colour (c0, the
  programs' x2) is set over too, a later texture stage's texel (a detail layer, a lawn's grass) goes to what leaves the
  colour as it is for its op, the untextured world program (`kVertexFactor`: the asphalt's) is covered as a texel, and
  the per-pixel lighting's surface map loses its relief and gloss under the snow (its normal toward the surface's own,
  its roughness 0.75).

- **Where it settles: every outdoor level, by its geometry, not its textures.** No shipped level has snow surfaces (no
  snow terrain in any collision), so snow is weather: on, it covers the open ground of any level with a sky. Outdoors is
  the level's own fact - a sky scene loaded (`snow_world` tells the settings, `config::set_level_outdoor`; the rule
  `Cap::kLevelOutdoor` puts `snow` out of effect in an indoor level, "This level is indoors"). The **sky map**
  (`snow::make_sky_map`, once a level from its collision: every collidable face of every sector, triggers left out) is
  the level seen from above on a grid of cells (4 units, coarser for a level wider than 2048 cells): each cell the
  topmost face's height at its centre and how much snow that face holds - the slope's curve of its normal's y (below),
  none on a face of a sector in the Indoor light group; a narrow face (a
  ledge's top, a rail's) gets at least its middle's cell. A surface below its cell's top (+ 2 units + 0.6 cells) is
  under something - a roof, a bridge, an awning: no snow.
- **The cover (the world's draws).** `render_pc` marks each draw of a world or scene-object mesh that isn't a
  character's (`surface_trail::set_current_ground`, beside the water's marks in `MeshRef::render`; `Singleton::render` says while
  it draws an instance with a skeleton - 24 bones or more -, skinned or a mesh a bone); such a draw on one of the
  per-pixel lighting's programs, or on the fixed-function stages with a texture on stage 0 (the road's markings, the
  decal meshes), gets `FragmentKey::trails`' snow bit (a byte of the two spares, shared with the paint: the key stays 52 bytes). Its
  fragment shader works out the surface's world position (the eye-space one handed on, the view's inverse), its own
  normal from the position's screen derivatives (turned toward the eye), the up share moved by a value noise (the edge
  of a slope's snow isn't a straight line) and the open share from the sky map's four cells (`textureGather`: a
  quarter each, so a roof's edge is soft), and mixes the albedo - stage 0's texel, or the material's passes' sum before
  the vertex colour - toward the snow's (0.86). Nothing else changes: the bake, the time of day's relit light, the
  per-pixel lighting and the traced shadows after it light the snow as they light the ground. Water is never covered
  (its own key); characters keep their colours. The surface's own normal decides, so the cover follows the drawn
  meshes, not the collision's coarser shapes: a car's roof, a bench's seat, a wall's top all hold snow where they face
  up and nothing is over them.
- **The trails** (`snow::TrailField` - a surface trails' field, below -, on the processor, kept for the whole level session): a heightfield in tiles of
  64 x 64 texels (2 units at quality 2; 3 at 1; 1.5 at 3), made where something first pressed; a texel holds four bytes
  - the press (the share of the snow pushed down), the berm (snow pushed up beside a trail) and two spares. A stamp
  is a capsule (from the presser's last place to this one) - the press full inside it, softened over its outer 40%; the
  berm in the ring 5 units outside (4t(1 - t) across it); each byte keeps the most it was given. The layer's height
  over the ground is the snow there x (1 - press) x (1 + 0.6 x berm) (`snow::layer_height`, the shaders' twin - the
  snow there `snow::surface_at`'s, below). While snow falls (`snowfall`, or the game's own snow weather) every press
  and berm drops by one 255th each 180 / 255 s - a full trail fills in over three minutes. (Rounds 1 - 3 kept an
  activity besides - where the layer was raised round a trail; round 4 raises it everywhere, so it is gone.)
- **What presses, once a simulation step** (`render_pc` `step_frame`, every step whether drawn or not: the same
  positions make the same trails however the frames go): the instances drawn with a skeleton (24 bones or more:
  skaters and peds - their origin, where they stand) and the wheels of those with a few bones (a bone off the middle by
  15 units and no higher than 40: vehicles' hubs). Read only. A character on the snow (no more than 6 units over its
  top) moving at 150 units a second or faster cuts its board's slice (6 units half width, the berm higher the faster);
  slower, a footprint each 26-unit stride, side by side; one that was in the air (18 units over the top for more than
  0.15 s) and comes down stamps a landing (12 units round) and bursts. A wheel within 20 units of the ground presses
  its track (4 units half width). A move longer than 240 units in a step (a restart) presses nothing.
- **The spray** (`snow::SprayParticle`, 2048 at most - the oldest give way): puffs thrown back and up off a riding
  board's sides - about 40 x (speed / 600) x (0.2 + the turn rate / 1.5, at most 2.5) a second, more on a carve; a
  landing's burst 24 - 90 all round. Each its start, velocity, life (0.45 - 1.5 s), size and shade from the snow's own
  random numbers (a PCG32 seeded the same each session - never the game's); it moves on its own (a throw under
  gravity, `snow::spray_place`), so the device draws it from its start and age alone.
- **The window and the layer (opaque_drawn, `ViewOrder::kSnow` 440; round 4).** The trails' texture (RGBA8) is a
  window of 10 tiles a side round the camera (6 at quality 1, 16 at 3), each tile in the slot of its place modulo 10
  (sampled repeating: the window is toroidal). As the camera moves the tiles that come in are copied into the slots of
  those that went - the others stay -, and a tile is copied again only when its version changed; nothing is lost near
  the camera and the processor keeps every trail. **The layer is the snow's own surface over all the open ground round
  the camera**, not only round the trails: a camera-centred grid of levels (`kSnowGridQuads` 128 cells a side; the
  first level's cell the trails' texel, each next twice the last's, as many as reach `kSnowLayerReach` 1500 units -
  five levels to 1536 at quality 3, 38 m - drawn with one 16-bit index buffer, the vertex's place from its index; near
  a level's edge its every other vertex moves onto the next level's grid, so the two meet without a crack, and a level
  leaves to the one inside it the square that one covers). Each vertex is the snow's top there - `snow::surface_at`,
  the shaders' twin: the sky map's ground (its cells blended where their tops agree within 6 units, or where they are
  one sloped face's - every cell holding some snow and less than all of it, a ramp or a bank, rising no more than two
  cells across; else the nearest cell's) plus `snow_depth` x the cover there, **banked against what stands higher
  round it**: three rings (0.5, 1, 1.7 x the depth, at least 4 units) of eight samples, a flat top among them (a kerb's,
  a wall's, a roof's: all the snow - a ramp sheds its own) holding its own snow's top there and falling 0.6 a unit
  toward the point, the point taking the highest, at most twice its own snow over its own ground. So a kerb lower than
  the snow is buried in a smooth bank, a wall gets a drift at its foot, a bench without collision under 20 inches of
  powder is under the snow (the layer is opaque and depth-tested), and ground that holds none (under a roof, indoors,
  a wall's face) is never raised. Its height fades to the flat cover from 0.6 of the reach to the reach; the trails
  cut it (`layer_height`; outside the trails' window nothing pressed). No triangle across a step the snow doesn't
  bridge - a rise over a grid cell of more than 10 units (at least the depth) + 0.8 x the cell (a wall, a roof's or an
  awning's edge: a sheet there would be snow up the wall or hanging from the awning), or where the sky map's cells
  disagree by more than a kerb (a car's or a ledge's shape too fine for its cells); the ground's own covered snow shows
  in the cell at its foot. With ray tracing, a vertex whose ray straight up meets something drawn (mask 0x05: a tree,
  an awning without collision - the cover's rule) holds no snow. The vertex also gives its shape: the normal and the
  occlusion (lower than round it) of the surface's top four cells round it, a wall's rise held to a step's so it
  doesn't tilt the bank beside it.
  Its colour is the shaded ground under it: a compute pass puts each texel of the trails' window's ground point on the
  screen - moved onto the drawn ground by the depth there (the sky map's height is its collision's, a blend of its
  cells: twice the point goes onto the surface the depth says is along the ray), the colour taken bilinearly - and,
  where the depth there is the ground's, keeps it (a toroidal image beside the trails'; a point hidden this frame
  keeps its last colour; a new sky map forgets them all). The layer takes the colour seen under it; beyond the trails'
  window (and ground never seen), what is behind its pixel - never where the gather saw the ground: behind the layer is
  what it buries (tried in the round: taking what is behind where it was much the colour seen showed a skater's shins
  through 20 inches of snow). It turns that by its own shape
  against the flat snow in the light now (the time of day's light and sky, else the level's own sun at its own light -
  65% direct): the banks' normal and occlusion from the vertices, the trails' relief, normal and occlusion and the
  shadow of their walls (a 6-step march over the heights toward the light), the packed snow in a trail darker (x 0.8)
  with a highlight. Depth-tested and written; the upscaler's motion is the camera's. (Fixed on the way: the gather's
  slot was GLSL's `%` of a negative texel - undefined: the texels west and south of the world's origin were never
  gathered.)
- **The air (world_drawn, the same order: before the fog and the tone mapping; round 4).** The user: "snowfall still
  kind of looks like a filter in front of the screen". What the frames showed (`capture` series still, strafing,
  rising, turning; BO): with the camera still, each falling flake left a vertical trail fixed on the screen - the
  flakes wrote the scene's motion behind them, so DLSS reprojected their history with the background and they smeared
  where the screen was, not where they fell; the flakes' height was the camera's (the box's bottom `camera.y - 0.4 x
  height` plus its fall: rising with the camera moved every flake with it); the near box was 40 times denser than the
  far and defocused through a 0.45-unit aperture, so the flakes near the lens were big faint blobs all over the
  picture; one size distribution, one brightness, one fall speed. Now: 22,000 flakes, the first 12,000 in a 440 x 320
  box round the camera, the rest in the 1400 x 900 one, each a hash of its number - **its place wrapped into its box in
  the world's own frame on every axis** (the box only says which copy of the pattern is drawn), so the camera moves
  through the snow (parallax both ways); falling 34 - 68 units a second (the bigger faster), carried downwind by the
  level's wind (`engine/gfx/wind.h`, its gusts added up on the snow's clock - `snow::wind_drift`), swinging on a spiral
  of its own, and moved by **the gusts' eddies**: rolls carried downwind at the wind's speed - a stream function's curl
  across the wind (260 x 340-unit cells, 16 units x the gust) and rolls round axes across it (200-unit cells, 26 units
  x the gust, up and down along the wind) - a displacement field the same for every flake at a place, so they swirl
  together and stay as evenly spread. None nearer the camera than 40 units (faded in from 16), the lens's defocus an
  eye's (0.15 units). Each a disc of its true size (0.25 + 0.6 x a share squared: most small, a few big), at least 0.75
  pixels, **drawn as a streak**: its own move on the screen since the frame before (its fall, drift, flutter and the
  eddies' - `before` in the block, never the wrap - and the camera's, the frame before's view) over half the frame, at
  most 16 pixels, a capsule whose alpha is spread over its length (the disc's area against the capsule's). With the
  upscaler's motion **a second draw of the flakes writes each one's own motion** (x, y) where it covers 30% of the pixel
  or more (the same vertex shader, MOTION_ONLY: no lighting, its shelter ray kept), and the first writes its share as
  the reactive mask (half of it where its own motion is the pixel's): the upscaler takes a flake's history from where
  it was. Lit from above by the sky's light and the direct light where its own ray toward it reaches it (traced; mask
  0x07), or more where the snow on the ground under it was lit more - the gather's colours over the snow's albedo, the
  one lighting path's own result: the lamps, the bounce (the lamps' grid of rounds 2 - 3 is gone: its local light at a
  lamp's cell was 4.7 against a night sky of 0.1, so the flakes glowed across the street from a lamp) -; from below by
  what that snow sends up (half a sphere's: a flake reads brighter than the snow behind it, as the ground isn't lit from
  below); the direct light scattered forward (a glint looking toward the sun or the moon) with each flake's twinkle;
  each flake's own brightness 0.78 - 1.12. Not drawn under a roof (a ray straight up; without ray tracing the sky map).
  `vk::flake_place` is the vertex shader's twin (the check's). The spray as before.
- **The haze**: the snow too far off to draw. While snow falls, after the flakes, a full-screen pass over the colour
  (the depth read, not attached): a medium of the snow's light (its albedo x the sky's light and half the direct
  light) with an extinction of 3.2e-4 a unit from 250 units out, the sky as at 9000 - colour x T + the haze x (1 - T).
  Before the volumetric fog, which adds its own medium after it (the two aren't one march yet - the fog's isn't snow).
- **The winter sky** (`rt::apply_overcast`, day_clock - the lighting state's one producer): with snow on the ground the
  state takes 0.55 of a full overcast, while it falls all of it - the direct light down to 0.3 at full and its radius
  toward 16 degrees (soft shadows), the sky's light greyer (0.75 of the way to its luminance) and gaining half of what
  the deck took from the sun, the stars and the moon's disc hidden; the bounce, the shadows' strength and the exposure
  follow. Only while the time of day lights the level (tod=level: the original's look).
- **The decals** (`rt_decals`) lie under the snow: each corner's weight x (1 - the sky map's cover there, where it is
  that cell's top face) - vulkan_snow.h `SnowGround`.
- **What presses** (round 2): a character's body joints (its rig's - `gfx::rig_data_of_matrices`: each joint's bind
  place through its bone's matrix; not the feet, whose bind pose stands under 14 units) within 5 units of the snow's top
  press their path 4.5 units wide (a bail's slide, a hand plant); an instance drawn without bones that moves 20 units a
  second or faster with its box's bottom within 30 units of the ground presses two tracks either side (0.4 of its box's
  width, 4 - 60 units). The raised layer's fall at its edge is 64 units wide now, and its shading (the normal, the
  occlusion, the walls' shadow) takes the trails' relief alone - the fall is the layer meeting the ground's own snow,
  not a shape of it, so no step shows where it ends (nor at the window's edge).
- **The cost (round 3).** The user's report: unplayable with snow on - in CA with snow_depth 20, 30 - 260 ms frames
  and one of 18.5 s. Measured (`tools/game/frame_bench.ts`, the user's ini, CA and BO, the new log lines below): the
  snow's step on the processor took 29 ms on average and up to 285 ms (frames to 725 ms) - 700 - 1000 pressers a step
  and 115 stamps, 113 of them body joints (in 20 inches of snow a walking ped's knees are inside it, so every ped
  presses every step), each writing its activity out to 74 units at the trails' texel: ~10,000 texels a stamp. Not
  the shaders: the snow's passes take 0.1 ms on the device and no pipeline was made while playing. So:
  - the activity is a field of its own (`TrailField::activity`, `snow::kActiveScale` = 8 trails' texels a side - 12
    units at quality 3): a stamp writes the press and the berm only as far as the berm's ring, the activity on its
    coarse texels (~150 a stamp). Its smooth 64-unit fall loses nothing at that size: the device samples it through
    its own toroidal window (binding 9, linear - `TrailField::active_at` its twin) and the layer is drawn on the
    trails' window tiles the activity raised (`TrailField::tile_active`), pressed or not;
  - a stroke (`surface_trail::Field::stroke`, the paint's too) visits each texel row's span of its capsule
    (`row_spans`: where the row meets the end discs and the two sides), not the capsule's box - the same texels (the
    check counts them against every texel of the box), a long slanted stroke no longer its square;
  - an outdoor level after the indoor front end gets its snow from its first frame (`config::set_level_outdoor` says
    when the fact changed and `snow_world` puts the settings to use - it waited for a change on the Options screen).
  The same steps (the same 115 stamps) now take 0.27 - 0.66 ms (the most 4.9). The log says it every 1200 steps
  (`snow: the trails' step takes ... pressers and ... stamps a step (... of them a body's), ... tiles`) and the
  device's time for the passes every 600 frames (`vulkan: snow: the passes take ... - the trails' layer, the flakes and
  spray, the haze`). The cover's own cost (the world shader's ray straight up, per pixel) is the rest of snow's: with
  snow on against off, 0.5 - 0.6 ms of the frame's device time at 1280 x 720 (DLSS's input for a 1080p window - the
  workspace's second monitor is 1080p), so about 1 ms at the user's 1707 x 960.
  Also: the fill (snowing) passes by the tiles already filled in and not pressed since (`TrailField` keeps each tile's
  version then) - it walked every tile the session made each 0.7 s (8 ms at 1,264 tiles after 3.5 minutes in BO, more
  the longer one rides). And the frames over 33 ms left with snow on were the renderer's cache saves every 10 s (the
  pipeline caches, above - not snow's), now only when the cache grew. After (the same scenario, the user's ini at the
  1080p the workspace allows, depth 20): CA 59.9 fps, 1% low 26 ms, the worst frame after the first second 65 ms
  (GPU spikes while the user's own game ran on the same card); BO 57 fps, none over 33 ms; a 210 s BO soak flat.
- **The cost (round 4).** `tools/game/frame_bench.ts` (the user's [Graphics], snow_depth 20, the workspace's 1080p window:
  DLSS quality 1280 x 720 input; 60 s of the scripted ride each) against round 3's build: the snow's passes on the device
  0.09 -> 0.18 ms in BO and 0.11 -> 0.19 ms in CA (the layer 0.04 -> 0.15: five grid levels, ~80,000 vertices, each
  its surface and its four neighbours' - 28 sky-map texels a surface - and a ray straight up; the gather over the
  whole trails' window, 256 tiles at quality 3, three depth reads a texel; the flakes 0.03 with their second, motion,
  draw); the processor's trails' step 0.35 -> 0.27 ms (BO) and 0.43 -> 0.30 ms (CA), the activity's strokes gone, and
  the lamps' grid's 324 light-field fits a frame gone. Frame rate unchanged (CA 60 / 60 fps, 99th percentile 19.95 /
  19.91 ms). `thug2_vulkan_snow_check`'s cost at 2560 x 1440 (not traced): 0.32 - 0.35 ms (round 3: 0.22). A 210 s BO
  soak (depth 20, riding): flat (10.3 -> 10.1 ms a frame, BLAS builds 1.58 -> 1.74 a frame; round 3's build the same:
  8.1 -> 8.6) - a first run that grew had ~20 s of 90 - 240 ms loops on the processor (the ray-traced scene's begin, the
  machine shared), not repeated.
- **Purely visual.** Nothing of the game's is written: the collision, the physics, the scripts and the random streams
  are untouched (the skater rides on the level's collision under the snow); `tools/game/sim_check.ts` matrix on the
  reference demos stays 32/32 and `tools/sim_draw_independence.test.ts` passes (the snow reads the step's state in
  `step_frame`, never whether a frame is drawn).
- **Checked.** `thug2_vulkan_snow_check` - on the processor: the sky map of a synthetic level (open ground holds snow,
  the roof's top is the top under it, a 60-degree bank and an indoor floor none, a kerb's step never blended, the same
  cells twice); a stamp (pressed inside, the berm beside, the activity round it, nothing beyond; the layer down in the
  trail and raised beside it); the steps (a rider presses along its path and throws spray - more carving; at most
  2048 alive; a landing bursts once; walking leaves a footprint a stride; a wheel presses; nothing from above the snow
  or indoors; the trails kept over 1000 steps; half filled in 90 s, all in 180, the activity kept; the same steps twice
  give the same trails and spray byte for byte; round 3: the press within its ring and the activity falling to nothing
  at its reach, a stamp's tiles few in both fields, four strokes visiting exactly the texels within their reach). On
  the device: the trails' window read back holds exactly the
  processor's texels, again after the camera moves 7 tiles, only the tiles that came in copied; (round 2, on the
  processor) the slope's curve, the overcast, a body joint's press and a mover's two tracks, the wind's drift twice the
  same, a 60-degree bank's light coat, (on the device) the haze whitening the far ground and not the near; the cover whitens the
  open ground drawn with the game's material shader, leaves the ground under the roof's footprint, a wall and an
  unmarked draw alone, and snow off draws the picture as before; the layer leaves pixels far from a trail unchanged,
  changes those on it, the packed snow darker than beside it; the falling snow over the sky; the spray drawn is what is
  alive; every pixel finite; validation silent. `thug2_options_check` has the four keys and the indoor rule. Cost
  (the check's, RTX 5070 Ti, 2560 x 1440): the trails' window full (70 tiles drawn), 16,000 flakes and a carve's spray -
  0.2 - 0.33 ms a frame on the device (one run 0.8 on a busy machine); round 2 (22,000 flakes, the haze, not traced -
  the check has no TLAS) 0.22 ms.
  Round 4 adds: on the processor, `snow::surface_at` - the depth on open ground, a kerb lower than the snow banked (more
  than the depth at its foot, less 12 units off, the depth 40 off, never over the kerb's own snow), a drift at a tall
  thing's foot (over the depth, at most twice it), nothing raised indoors; on the device, the layer over all the open
  ground (a wall's foot under its bank, its middle as it was), and the falling flakes against their twin
  (`vk::flake_place`): drawn where it puts them, still at their own place in the world when the camera moves 12 units
  aside and 8 up (none left where they were on the screen), and where their own move took them a tenth of a second on.
  The flakes' second (motion) draw isn't in the check (it has no upscaler): the game's log says "their own motion".
- **In the game** (BO, the user's [Graphics] with `tod=afternoon`, 853 x 480 DLSS input in a 1280 x 720 window): the
  sky map of 26,049 faces (3,633 indoor) in 85 ms - 2048 x 1989 cells of 5.7 units, 53,786 square metres open to the
  snow; the streets, sidewalks, planters and car roofs white, the building fronts not; a board's trail
  with its berms down the street, spray off a carve and a landing, flakes falling, the trail shallower a minute later.
  Known: the decals (`rt_decals`: cracks, oil, tyre marks) are drawn over the snow (their pass reads the shaded
  ground, not the cover); the trails' raised layer shows a soft step where its activity ends (24 units round a trail);
  foliage and trees without collision keep no snow off what is under them.
- **Not done:** the ray-traced reflections and bounce light see the ground's own colours, not the snow (their materials
  are the textures'; the hit shading would need the snow's state in each ray-traced pass - proposed: the snow's amount
  as a function of the material table's shared GLSL, the hit's place and normal, the sky test a ray straight up from
  the hit); the BLAS keeps the level's geometry (the trails are drawn, not traced); the haze is its own pass before the
  volumetric fog, not a medium in the fog's march (no light shafts through the falling snow); without ray tracing the
  sky map still decides what is open (a tree's canopy without collision keeps no snow off). Round 2's game run: a road
  draw whose vertices have no relit stream keeps its bake's tint under the snow (Boston's street a little greyer than
  its sidewalks). Round 4: the layer's colour is still the ground's as seen (not lit as a surface of its own): a thin
  shadow (a cable's) shows stepped on the layer (the gather's 1.5-unit texels); the layer's edges follow the sky map's cells (round a parked car - collision boxes, 4 - 6-unit cells - a
  ragged rim, its roof's slab not the body's shape); the trails beyond the trails' window aren't drawn (untouched snow
  there); a step the snow doesn't bridge leaves its foot's cell to the flat cover (a gap the depth deep, one grid cell
  wide - 1.5 units near, 24 at the reach); no shadow cast by the layer on the world (its own relief shades it).

### Surface trails: what the skaters leave on the level (snow, paint)

`engine/gfx/surface_trail.h` (the fields, the drawn world's marks, the trails' random numbers), `gpu_vulkan/vulkan_surface_trail`
(the window on the device) - one system the snow's trails and the paint trails both keep their state in and draw through.

- **A field** is a surface seen along one axis - from above (x, z) or from a side (z, y; x, y) - cut into tiles of 64 x 64
  texels, made where something first stamped and kept for the whole level session. A texel is one or more 32-bit words,
  its planes (the snow's one: press, berm, activity; the paint's two: the coat, the painted place). A **stroke** hands
  each texel within a capsule's reach (one step's move, from the last place to this one) to its owner's rule with its
  distance from the stroke - the snow presses and banks, the paint coats; each tile's version goes up when a texel of it
  changes. Each field made has a serial: a new session's field is never mistaken for the last one's.
- **The window** (`update_trail_window`): the device holds `tiles` x `tiles` tiles round the camera, each in the slot of
  its place modulo `tiles` (toroidal), one image a plane - several windows may sit side by side in one image (the
  paint's three). As the camera moves only the tiles that came in are copied, and a tile again only when its version
  changed; a slot given another place is reported (the snow clears the colours it saw there).
- **The drawn world's marks**: `render_pc` says for each draw whether it is the world's ground or a scene object's mesh
  that isn't skinned (`set_current_ground`), and while it draws a character (`set_current_character`, 24 bones or
  more): only marked draws take the snow's cover or the paint; characters and water never do.
- **On the world's draws** both are one key byte, `FragmentKey::trails` (kTrailSnow, kTrailPaint), on the per-pixel
  lighting's programs and the fixed stages with a texture: the shader works out the surface's world position and its
  own normal once (`trail_main`), the snow's amount and the paint's coat from them, and covers the albedo before the
  light - the snow, then the paint over it (`trail_albedo`) - so the one lighting path lights both.
- **The same every run**: the stamps are made once a simulation step (`render_pc` `step_frame`, drawn or not) from what
  the step left, and their random numbers are the trails' own (a PCG32 seeded the same each session - never the game's).

### Paint trails (`paint_trails`)

`engine/gfx/paint_trails.h` (the strokes, the splatter, the three fields - on the processor), `game/rider/paint_brushes`
(what the skaters do, read only), `gpu_vulkan/vulkan_paint` (the windows, the coat) - `[Graphics] paint_trails` (off by
default), `paint_trail_scale` (1), `paint_trail_colour` (by player), all live (the Options screen's Gameplay tab). No
ray tracing needed; Direct3D 9 draws none. Free skate as a paint session: every skater's board leaves a line of paint
wherever it rides, and the line shows the combo as it is built. No score; the paint stays for the session.

- **Where it paints: the modes that are free skate** - `is_freeskate` (one or two players), `is_singlesession`'s free
  skate and the online lobby (`netlobby`): `GameHooks::paint_brushes` hands over no brush anywhere else (the career, the
  scored games - their modes untouched).
- **The brushes, once a simulation step** (`render_pc` `snow_world::step`, beside the snow's, the loaded scenes' key the
  level session): for each skater of the skate module - its object id, its place (the skater's, on the board), its up
  (the surface's normal on the ground, a ramp, a rail and a wall: the skater's basis' up row) and forward rows, its
  contact from the skater state component (ground; rail or lip - a stripe; wall - a wallride; nothing in the air, a
  wallplant, walking, driving or a bail) and its combo's running score (its score's pot x the multiplier). Read only.
- **The fields**: three - along x, y and z (from the sides and from above), 2-unit texels, two planes: the coat (RGBA8,
  the colour premultiplied by its coverage) and the painted place along the axis (a float). A stroke goes into each
  field whose axis the surface faces by 0.45 or more of its normal (a 45-degree ramp into two). Each texel's place is
  the stroke's surface's plane at its centre; a fringe two texels past the coat writes the place where nothing is
  painted (the device reads the nearest places).
- **A coat** over a texel: the new colour x its coverage over what is there; a coat of another surface (more than 6
  units along the axis away - the ground under a rail, a ledge's top over the street) is kept unless the new one covers
  more. A line's coverage is 0.92 at its middle, its edge softened over its outer 35%.
- **The combo**: its size `combo_size(score)` = log10(1 + score / 50) / log10(1 + 2,000,000 / 50), clamped to 1 - a single
  300-point trick 0.18 (the line twice the board), 1000 points 0.29, 10,000 0.50, 100,000 0.72, 1,000,000 0.93. The line's half width is 4 units (the board) + 30 x the size,
  x `paint_trail_scale`; a rail's stripe 1.8 + 2.2 x the size. **The splatter** from a size of 0.02: 90 drops a second
  x the size, each thrown to a side (0.35 - 1 of the half width + 44 x the size) and ahead (-0.15 - 1 of 80 x the size)
  on the surface, 1 - (1.6 + 5.5 x the size) units round, with up to three specks round it; on a rail along its stripe
  only. All from the paint's own random numbers, the brushes in their ids' order: the same steps, the same paint.
- **The session**: a move of more than 240 units in a step (a restart) starts a new line; a skater not seen for 2 s is
  forgotten; a bail stamps nothing and wipes nothing - the paint stays. A new level (another scene key), `paint_trails`
  off, or the script command `PaintTrailsClear` (the console's) wipes it.
- **Colours**: by player slot (the object id) - pink, cyan, lime, orange, yellow, violet, red, white; this machine's
  first player takes `paint_trail_colour` when it is set (a name or #RRGGBB).
- **Online**: every client paints every skater from what it already has - their places, orientations and states
  (the skater state the game's messages carry). A remote player's combo isn't sent between players (the game sends a
  combo's points only when it lands - to the server - and the totals periodically): its line stays slim, and where its
  total rises the paint bursts there (`Brush::landed`, 10 - 80 drops by the points' size). Nothing new goes on the wire.
  Its colour is its slot's (the colour each player chose isn't sent). Not done: a custom online game mode of its own
  ("custom" in the host's list) - the lobby's free skate paints already.
- **The device**: the three fields' windows (20 tiles a side round the camera's place on each field's plane: 2560
  units, 65 m) side by side in two images - the coats (RGBA8) and the places (R32F), 3840 x 1280 each (39 MB) -,
  brought up to date by the frame's first view. A marked draw on a program that takes it gets kTrailPaint
  (`PaintUniforms` at binding 19, the images at 20 - 21): the fragment reads each field its own normal faces by 0.4 or
  more - the four texels round the point blended by hand, each counted only where its place is this surface's (6
  units + 1.5 texels x the slope across the field) -, takes the field with the most coat and mixes the albedo toward
  its colour by its coverage, before the light (the bake, the time of day's relit light, the per-pixel lighting, the
  traced shadows light it).
- **Purely visual.** Nothing of the game's is written: the brushes are read, the physics, the scores, the scripts and
  the game's random streams untouched; `tools/game/sim_check.ts` and `tools/sim_draw_independence.test.ts` are the proof.
- **Checked.** `thug2_vulkan_paint_check` - on the processor: the combo's curve and the colour's text; no combo, a slim
  line on the ground and nothing beyond it, in the field from above alone; wider and splattered more as the combo grows
  (three sizes); kept texel for texel through a 300-step bail; a rail's stripe on its top, narrow, the ground's line
  under it kept; a wallride in the field from the side; a 45-degree ramp in two fields; the same steps twice the same
  byte for byte; a restart draws no line between; a remote player's landed combo bursts; player 2's colour and this
  machine's chosen one. On the device: the three windows read back hold exactly the fields' coats and places, again
  after the camera moves 7 tiles - only the tiles that came in copied; the coat on a drawn ground (the line's colour, the
  ground beside it and under a rail's stripe 30 units up), on a wall (a wallride), none on an unmarked draw or with paint
  off; every pixel finite; validation silent. The cost (RTX 5070 Ti, 2560 x 1440): the ground and a wall with the coat
  0.047 ms against 0.028 without - about 0.02 ms a frame for the coat; a session's windows copied in once (146 tiles),
  then only the tiles a stroke changed. `thug2_ff_shader_check --trails` compiles and validates every key with the
  snow, the paint and both (123,329 shaders, none failed).
- **In the game** (BO, a workspace at 1280 x 720): a push down the street leaves a board-wide pink line; a manual's combo
  (to about 470 points) widens it two to three times with splatter specks beside it, narrower where the combo was
  smaller; a bail leaves the line as it was (a gap only where the board was in the air and the skater sliding); a demo's
  grind (`sim_grind`) a stripe along the ledge's top; the overview from high above shows every line of the session.
- **Known:** a line far off is thinner than a pixel and breaks up (the coats are read texel by texel, no smaller
  levels); a coat of another surface in the same texel is replaced when the new one covers more (a rail's stripe over
  the ground's line under it, within a texel); the windows reach 32 m round the camera, the paint beyond is kept but not
  drawn until the camera comes near; the ray-traced reflections and bounce light see the surfaces' own colours.

### Decals and clutter (`rt_decals`, `clutter`)

`engine/gfx/scatter.h` (the placement and the `Sink` contract), `engine/gfx/scatter_art.cpp` (the art),
`render_pc/scatter_world` (the world read), `gpu_vulkan/vulkan_scatter` (the passes) - `[Graphics] rt_decals`,
`rt_decals_density`, `clutter`, `clutter_density` (all off by default, all live; the Options screen's Surfaces group,
after the grass, each density row shown only while its switch is on). No ray tracing needed; Direct3D 9 installs no
`Sink` and draws none. Neither is ever in the acceleration structures; neither has collision.

- **Where they go.** Read from the collision like the grass: every collidable face (not a trigger), its terrain's
  surface (`scatter::surface_of_terrain`: ASPHALT asphalt; CONCSMOOTH, CONCROUGH, WETCONC and DEFAULT - the levels'
  commonest, their concrete and paint - concrete; SIDEWALK, TILE paving; BRICK, ROCK brick; DIRT, DIRTPACKED, SAND dirt;
  GRAVEL; GRASS, GRASSDRIED; the rest nothing), its vert flag and its shape by the game's winding
  (`TriMeshSource::get_triangle_normal`'s (v1 - v0) x (v2 - v0); the placement had it the other way round until
  2026-11-15, which made the ceilings and the undersides of slabs "ground" and most floors "ceilings" - found by the
  flat faces with nothing at all above them, 55 - 75% of the ones it turned down, 1 - 6% of the ones it turned up):
  ground (normal y >= 0.96), wall (|y| < 0.25), ramp
  (between, or flagged vert; a slope of dirt, gravel or grass is a hillside, not a ramp), the rest (ceilings, steep
  ground) nothing. The level's grind rails come through `render::GameHooks::level_rails` (the skate module's rail
  manager; climbing nodes left out). Placed by hashes of each face's corners, the item's number and the level's checksum:
  the same every run, whatever order the sectors load in; made again when the scenes, the rail count or a density change
  (Boston: 26,049 faces, 1,024 rails, 66 - 108 ms at the load).
- **Skate lines kept clear** (decals and clutter alike): nothing within 40 units of a ramp face (3D, from just below to
  48 above the point), within 20 of an edge that drops 3 or more (a ledge's top, a step, a kerb's top, a platform's edge),
  within 30 of a rail (seen from above, 12 below it to 72 over it), under anything 0.5 - 120 above, or on a raised
  platform - the ground followed out in 8-unit steps (up and down slopes) ends in a drop or over nothing within 160 units
  in six of eight ways (a wide ledge, a box, a planter). Clutter is also kept off the foot of stairs (a rise under 12
  with another rise within 20 past its top).
- **The decals.** Ground decals a square unit at density 1: asphalt 1.2e-4 (cracks 3, stains 2, oil 2, tyre marks 1.5,
  wet 1.5 by weight), concrete 0.8e-4 (stains 3, cracks 2, gum 1.5, wet 1), paving 1e-4 (gum 3, stains 2, cracks 1),
  brick 0.5e-4 (stains, a few cracks), dirt 0.3e-4 (wet only); nothing on gravel, grass, metal, wood, water, glass. A
  wet patch stays only in a dip (eight points round it all there, none lower, one 0.25 higher) or in a gutter (a rise of
  at most 10 within 14, on asphalt or concrete). Grime: at the foot of brick and concrete walls standing on flat ground
  (found on either side), one per 150 units of the foot at density 1, from 1 below the ground up 0.25 - 0.5 of its width.
  Each decal is a quad on its face's plane - sizes 14 - 96 units, turned by a hash - cut (Sutherland-Hodgman) to every
  face of the same plane and surface it overlaps: it crosses the seams of coplanar triangles but never an edge. Its
  vertices (24 bytes: where, the decal's uv, kind, variant, opacity, fade distance) are put in 512-unit tiles.
- **The clutter.** Candidates a square unit at density 1: hard ground 4e-3 (3e-3 brick), kept only within 30 of a rise
  (a wall or kerb at least 3 high starting at most 2 above), with the chance (1 - d/30)^2 - more the nearer, more in
  corners; within 8 of it 35% are weeds, the rest leaves 60%, paper 30%, cans 10%. Dirt 4e-3 (pebbles 70%, weeds 15%,
  leaves 15%), gravel 5e-3 (pebbles), grass 4e-4 (leaves). Items (32 bytes: root, rank, kind, variant, facing, colour
  hash, size, tilt) in tiles, each tile's by rank.
- **The art is made by code** (`scatter_art.cpp`, integer hashes and value noise: the same bytes every time - nothing
  fetched, nothing third-party, so there is no licence to keep and nothing to download): a 1024 x 1792 RGBA8 atlas, a
  row of four 256-square variants per kind - cracks (wandering branching lines with a dirty halo), stains (soft uneven
  blots with a faint tide line), oil (a dark spot, a sheen, drips), tyre marks (a skid's band, streaked along it,
  building up and fading), gum (clusters of round spots), wet patches (a soft blot, darker inside), grime (darkest at the
  foot, fading up, drips) - rgb what the surface's colour is multiplied by, alpha a fade to the cell's border; and five
  meshes of 48 vertices (a curled leaf, a crumpled scrap of paper, a can lying on its side, a weed of seven blades and four
  leaves, a pebble), each vertex with its own shade.
- **The decals' pass** (`ViewOrder::kDecals` 390, at opaque_drawn: after the shadows, occlusion, bounce and lamps,
  before the reflections, the grass and the fog): the tiles within 3,200 units and in view, their triangles drawn into
  the colour alone; each fragment reads the depth under it and is kept only where the scene there is the decal's own
  (within 0.3% of its distance + 1 unit - so nothing shows on the skater standing on a mark, through a wall, or from a
  decal whose collision floats over the drawn ground; a wall standing in a decal may take its foot's pixel row); the
  colour is multiplied - mix(1, texel rgb, alpha x opacity x fade) - so a mark darkens the lit ground as its own colour
  would: in shadow, in light, under a lamp. A decal fades out from 60% to all of 48 x its larger side (at most the
  3,200). With four samples the fragment reads sample 0's depth.
- **Puddles** (with `rt_reflections`, single-sampled targets): the decal pass also draws a wet mask (R8, the colour's
  size, cleared over the viewport) - each wet patch's darkening as its wetness, the most of any; the reflection pass
  (ViewOrder 400) reads it through a trace variant made from the trace's own text with the puddle lines put in (so the
  trace without decals is the same text and the same SPIR-V as before): where a world surface lies under a puddle, its
  reflectivity is the wetness (when more than its own) with water's Fresnel - `rt::kReflectWaterF0` 2% face-on, a mirror
  at grazing angles - and no environment mask. A frame whose only reflective things are puddles runs the reflection
  pass too.
- **The clutter's pass** (`ViewOrder::kClutter` 445: after the reflections, before the grass): as the grass's - the
  viewport's colour copied out, a compute pass keeps the ground's colour under each chosen item's root where the depth
  there is the root's (a buffer of half floats, so a root hidden this frame keeps the last seen; an item never seen isn't
  drawn and grows in over three sightings); then each item drawn instanced, 48 vertices, depth-tested and written (and
  the upscaler's motion). Its colour: a pebble the ground's own x 0.8 - 1.2; the rest their own (leaves' autumn colours,
  paper, a can's label and rim, a weed's green) x the ground's luminance over 0.4 (the ground's light) x the mesh's
  shade. Leaves and weeds sway downwind with the grass's two waves. Level of detail as the grass: every item to a quarter
  of 1,600 units, then (near / d)^1.5 of them by rank, each shrinking into the ground from 70% of the distance; at most
  400,000 a view.
- **Off is as before.** Both off: no listener, no `Sink`, `scatter_world::frame` tests one pointer; no fixed-function
  key or shader changes (`thug2_ff_shader_check --no-gpu`: 50,180 shaders, 0 failed); the reflection trace's text is
  unchanged (its puddle variant is made only when a wet mask exists); the check's picture with both turned off again is
  the same bytes as before they were on.
- **Checked.** `thug2_vulkan_scatter_check` (32 checks; the RTX 5070 Ti and the integrated AMD Radeon, validation 0). The
  placement on a synthetic street (an asphalt road of 96 triangles, a kerb, a paving sidewalk, a brick building, a
  quarter pipe, a box ledge, a rail, three steps, dirt, a lawn): the same 400 decals and 389 items byte for byte, others
  from another seed, the same counts from the faces reversed; every decal's kind its surface's, every vertex on its
  face's plane, none past the kerb or the road's end; grime, gutter puddles, tyre marks and gum all placed; no decal or
  item on a skate line (measured independently against the quarter pipe's ramp proper, the rail, the ledge's top and the
  steps), none at the stairs' foot; hard ground's items all within reach of a kerb, wall, ledge or step side, weeds
  within 8; pebbles only on dirt, only leaves on the lawn; the road's decals 303 for the rate's 372; the tiles hold each
  vertex and item once; densities 0 place nothing; the atlas and meshes the same every time. The passes over a road drawn
  in the view's depth with a wall standing on it: 13,131 pixels marked, every channel only darker, all inside a decal's
  footprint, none behind the wall; the same picture from a new scatter; decals put 30 units over the road mark nothing;
  the clutter only along the kerb's foot, finite, the same picture again; the wet mask drawn only while the reflections
  run, only under the puddle (700 pixels), not under a stain beside it; the puddle trace compiles with and without the
  sky; four samples run both passes; off as before. `thug2_vulkan_rt_reflections_check` unchanged (125). Cost (both
  passes, density 1, a street of decals and clutter at a skater's eye, 2560 x 1440): 0.05 ms on the RTX 5070 Ti, 0.84 ms
  on the integrated Radeon. `thug2_options_check` has the four keys.
- **Placement alone** (`thug2_scatter_check`, no device, no game): ground wound as the game's collision gets decals and
  the same faces the other way round (ceilings) none; a low roof covers, a canopy 400 above, a sheet 0.3 above the ground
  and the side of a thin branch don't; no decal under a roof on a big patch, plenty round it. `--level <folder>...`
  reads a level's own collision and counts what the placement makes (`--why` the faces' areas by shape and what covers
  the flat ground).
- **In the game** (load checks only, the workspace's 1280 x 720 with 8 samples): Boston - 1,105 decals (cracks 314,
  stains 486, gum 247, grime 50, wet 8) and 2,533 items (leaves 1,170, paper 614, cans 190, weeds 559) from 26,049 faces
  and 1,024 rails, 0.3 MB on the device; New Orleans 42 decals and 60 items; Skatopia almost nothing. Those counts
  were made with the faces' winding read the wrong way round (below); the same level files, read offline by
  `thug2_scatter_check --level <folder>` (density 1, no rails, the shipped terrain order), place as follows - before ->
  after: Skatopia 3 decals / 0 items -> 566 / 36,666; Boston 1,119 / 2,946 -> 6,891 / 13,056; New Orleans 44 / 35 ->
  3,756 / 6,032; Barcelona 84 / 275 -> 10,973 / 26,238; Los Angeles 960 / 957 -> 7,650 / 13,740; Australia 1,317 / 798 ->
  5,456 / 46,353. The look - sizes, darkness, density, the art - is the user's to
  judge.
- **Not done yet:** puddles reflect only with single-sampled targets (no wet mask with MSAA); the decals don't change the
  material's roughness for the ray-traced passes other than the reflections (GI, shadows) or for `rt_pbr`'s per-pixel
  highlight; marks on a face whose collision is a different terrain than its texture follow the collision (as the grass);
  no fetched CC0 decal scans - the art is procedural (a library like the detail materials' could replace the atlas's
  rows later); clutter takes no push from the skater; tree positions aren't known, so leaves gather by kerbs and walls,
  not under trees.

### Cel shading (`rt_cel`)

`engine/gfx/rt_cel` (the settings, the stream's layout, the bands' formulas and their twins on the processor),
`gpu_vulkan/vulkan_cel` (which draws are banded, their uniforms, the world's outlines), the fixed-function generator
(`ff_key.h` `kSurfaceCel*`, `ff_shaders.cpp` "the light in bands"), `render_pc/rt_relit_world` (the stream);
`[Graphics] rt_style=cel` (an art style in the Options screen's list; any other value turns it off - an older ini's
`rt_cel` line does nothing), `rt_cel_bands`, `rt_cel_shadow_colour`, `rt_cel_rim`,
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
- **The key.** A water draw's fragment key gets `FragmentKey::water` = 1 (2 on the moving surface: "Moving water" below; ff_key.h: a byte of its own, cel shading
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
  `rt_water_foam`, `rt_water_tint`, `rt_water_screen_reflections`, `rt_water_geometry` (below) - all live (the Options screen's Surfaces tab, the
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
  draws); the water's own geometry moves only with `rt_water_geometry` (below); stacked sheets are each shaded; the copy is
  made once per view, so transparent things drawn between it and a later water draw aren't seen through that water.

### Moving water (`rt_water_geometry`)

With `rt_water=1` and `rt_water_geometry=1` (the default) the water's surface itself moves: the swell's four Gerstner
waves that shade it also lift and shift its vertices, so the horizon of open water is no longer a straight line and the
water rises and falls along walls and posts. Calm water (`rt_water_waves=0`, or a level whose swell is flat) draws the
flat sheet to the bit; `rt_water=0` or `rt_water_geometry=0` draws as before.

- **The grid** (`engine/gfx/water_tess`, `render_pc/water_geometry`). The game's water meshes are big flat sheets - two
  triangles a quad, hundreds of units a side - with nothing to move in between. As a level loads with the setting on, a
  water material's mesh (the same rule as the shading's: `water_materials::is_water`) whose every triangle is near
  horizontal (|normal.y| at least 0.94 - not a waterfall, whose vertices would be lifted sideways) is split into a grid
  before its buffers are made: each edge into pieces of at most 40 units (its length / 40, rounded up, at most 64),
  each triangle into n x n smaller ones by its longest edge's pieces. An edge's points come from its two ends'
  positions in one order (the lower position's bits first), so two meshes that share an edge get the same points to
  the bit; on an edge with fewer pieces than its triangle's grid the border points are moved onto that edge's own
  points (degenerate slivers, no T-junctions - in the triangle's own coordinates the triangles beside a border keep
  their winding). Colours, texture coordinates and normals are blended from the corners; the original vertices stay
  first (the other levels of detail use them) and the strip is rebuilt; more than 16 bits of vertices or indices widen
  the spacing x 1.5 until it fits (logged as "coarser than asked"). The first level of detail only; the original strip
  is kept for the baked sun's samples (`mesh_smoothing::remember_original`). The ray tracing's copy of the mesh is this
  flat grid - its BLAS doesn't move (the shadows, occlusion and bounce light see the water at rest; its reflections are
  the screen's anyway). A level loaded while either setting was off stays flat until it loads again; the switch
  itself is live (no moving key without it). `[debug] rt_water_geometry_log=1` logs each water mesh split or left;
  each level's load logs one `rt_water_geometry:` total.
- **Which draws move.** `MeshRef::render` tells `water_surfaces` (`set_current_waves`) that the draw is a split mesh's
  first level of detail (cleared after the draw and by every `set_current`); an opaque draw kept back for the opaque
  world's end keeps the flag with it. Such a water draw's fragment key gets `FragmentKey::water` = 2 (`kWaterMoving`)
  and its vertex half `kVertexWaterMoving` in its surface bits; the water's block (binding 10) and the copy's linear
  depth (12) are the vertex shader's too (the set layout's stage flags; the copy's barriers include the vertex stage).
- **The move** (`water_shader.cpp` `water_lift`, after the camera-space position is made): the rest point in the
  world, each wave's slope amplitude (the same `water_wave` function and numbers as the normal's) faded where a pixel
  spans 5 - 20% of its wavelength (the pixel's footprint from the distance and the angle, as the fragment shader's
  derivatives find it) and where the grid is too coarse for it (40 units over 25 - 50% of the wavelength), the slopes
  together held at most 1 (Gerstner loops otherwise), each wave's height its slope / k, the sideways move Q = 0.6 of it
  along its direction (GPU Gems' Gerstner waves: the normal the fragment shader works out is this surface's normal). The
  whole is moved less toward the shore: the water's depth below the point from the copy behind the water at its place
  on the screen (as the foam's depth; the nearest edge's for a point off the screen), 0 at the bed to 1 at 48 units
  deep - where the water meets the sand or a wall's foot it doesn't move. The move is added in camera space (a still
  surface's position is the same to the bit) and handed on (x, z in `v_fog`'s spare components, y in
  `v_surface_eye`'s - the 16 locations are all used with motion vectors and cel shading); the fragment shader shades the
  rest point (`water_rest`: the waves, the ripples, which way the surface faces, its footprint) seen from where it moved
  to. Motion vectors move the frame before's position by the same amount (the waves' own drift over a frame left out).
- **How high.** Default settings: AU's sea about 17 + 9 + 5 + 3 units of height over its four waves (900 units
  swell), BA's harbour about 8 in all, BE's canal under 2; Rough (`rt_water_waves=2`, x 4) up to four times that, the
  slope cap holding it below breaking (AU's sea x 2.7).
- **Checked:** `thug2_tess_check` (the grid on the processor: the sea's two triangles -> 2,840, every edge at most 1.5 x
  the spacing, the same area, wound as before, every inside edge shared once each way, texture coordinates and colours
  blended, a canal beside it meeting at the same points to the bit, a waterfall, a small quad and a bad index left
  alone, a huge quad within 16 bits); `thug2_vulkan_water_check` (15 more checks): the shore's sea split and drawn as
  a split mesh's - it moves with the clock and repeats with it (opaque - kept back and replayed - and blended), not the
  flat sheet's picture, the sand in front of the flat water untouched by the moved water (102 pixels without the shore's
  fade, 0 with it); calm water, `rt_water_geometry=0` and `rt_water=0` the flat sheet's / the original's bytes; a patch
  with high waves and nothing behind it covers exactly the pixels the same grid moved on the processor covers (20,000 of
  35,000 pixels changed by the waves; 0 different). `thug2_ff_shader_check --water` adds every key flat and moving
  (compiled, validated, made into pipelines).
- **Not done:** floating things don't bob (the game's objects have no hook into the water's height - a
  `water_height(x, z, t)` on the processor from the same numbers would be the place to start); the ray-traced scene
  keeps the water flat; meshes too big for 16 bits split coarser (their shared edges with neighbours may then crack a
  little); the other levels of detail stay flat.

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

### Camera effects (`rt_motion_blur`, `rt_dof`, `rt_grade`)

Three optional post-processes in `src/gpu_vulkan/vulkan_camera_fx.cpp` (the looks' formulas and the grade's processor
twin in `vulkan_grade_looks.cpp`; the full pass description is the header comment of `vulkan_camera_fx.h`). Everything
off - the default - registers no listener and begins no motion vectors: every frame is drawn exactly as before. In
the Options screen's Camera tab (right after Colour).

- **Where.** Motion blur and depth of field at `rt::ViewOrder::kCameraEffects` (965): after the tone mapping (900), the
  upscaler (950) and cel shading's outlines (960), at the output resolution - a lens's blur belongs to the final
  picture, and the upscaler's history isn't smeared -, before the art styles (975), whose overlays (the VHS's REC, the
  retro cells) stay sharp. The grade at `kGrade` (985), last before the 2D. The depth and the motion vectors stay at the
  drawn (upscaler) resolution; each output pixel reads the drawn pixel under it.
- **Motion vectors without the upscaler.** The motion attachment (`vulkan_motion.h`) used to start only for upscaled
  views. Its start is now `begin_motion` (the upscaler calls it as before); with the blur on and no upscaler taking the
  view, the blur's own `view_begins` calls it. Pixels no draw wrote (the sky, cleared pixels) get the camera's motion
  from the depth, as the upscaler's preparation does.
- **Motion blur.** McGuire et al.'s reconstruction filter (2012): K x K tile maxima (K = height / 36), their 3 x 3
  neighbourhood maxima, a 16-tap gather along the neighbourhood's vector with depth-aware cone / cylinder weights and a
  per-pixel jitter. The vector is the frame's motion times the shutter / 2 (`rt_motion_blur_strength`, default 0.5 -
  180 degrees). With `max_fps` each render-only frame's vectors span its own time step, not a tick. Nothing is blurred
  after `frame_blend::cuts()` changes (a teleport, a respawn), after a camera jump (> 300 units or > 30 degrees in a
  frame - a level load, an unreported cut), under a menu, or while the game's own `SetScreenBlur` effect is on
  (`DisableBlur=0`): the original's blur is never doubled.
- **Depth of field.** Focused on the point the skater camera looks at (`rt::note_focus_point`, set each tick by the
  skate and walk cameras; render_pc turns it into `ViewDrawn::focus_distance`). The circle of confusion
  c(z) = clamp(A (1 - focus / z), -A, A), A = `rt_dof_strength` x 3% of the output's height (default 0.4: mild). Half
  resolution: the 2 x 2 blocks' colour and circle, a near-field reach per 8 x 8 tile over its 3 x 3 neighbours, a
  24-tap Vogel-disc scatter-as-gather into a far field and a near field with its coverage, composited at full
  resolution. In focus the picture is left bit for bit as it was. Off under a menu (paused, the front end), in the level
  editor (`ViewDrawn::menu`) and whenever no focus point is fresh (a cut-scene camera, the viewer).
- **Grade.** A 33^3 table per look, made on the processor from our own formulas (neutral, warm, cool, filmic, bleach
  bypass, skate video), read trilinearly from a storage buffer, mixed by `rt_grade_strength`; then the vignette
  (`rt_grade_vignette`) and monochrome grain (`rt_grade_grain`). An art style with its own vignette or grain keeps it.
  `rt_grade=level` takes each level's look from `rt_grade_levels` (`scene:look` pairs, the level's main scene as
  `LoadScene` names it). Independent of `rt_hdr`'s `rt_grade_lut`, which grades the HDR picture inside the tone map.
- **Not done.** Multisampled targets get the grade only (no blur or depth of field: logged once). A split view's depth
  of field focuses where the last camera to note a point looked. The live pause (the pause menu over a running game)
  isn't seen by render_pc as a menu: the depth of field stays on there.
- **Cost** (RTX 5070 Ti, 2560 x 1440, `thug2_vulkan_camera_fx_check`): motion blur and depth of field together about
  0.5 ms, the grade about 0.09 ms.

### Fisheye lens (`rt_style=fisheye`, `fisheye_vhs`)

Two art styles (`src/gpu_vulkan/vulkan_fisheye.cpp`, the maths in `src/engine/gfx/fisheye_lens.cpp`; the pass's full
description is the header comment of `vulkan_fisheye.h`): a 90s skate video's fisheye adapter on a camcorder - strong
barrel distortion over a wider field of view, the adapter's dark corners or a round frame, colour fringes and softness
toward the edges. `fisheye` (Fish Eye) is the lens alone - no style pass (`style_has_pass` false, as cel);
`fisheye_vhs` (Fish Eye + VHS) is the lens and the vhs style's pass at 975 with every `rt_style_vhs_*` knob but its own
barrel (`rt_style_vhs_fisheye` - and the corners' darkening that goes with it - is 0 there: `record_film_pass`; the
real lens replaces it). The lens is the camera effects' pass: `style_has_lens`, `apply_view_lens` (called by
`set_style_settings` and `set_camera_fx_settings`), the knobs `rt_fisheye_strength` / `_vignette` / `_circle` in
`CameraFxSettings`, listed in the Options screen's Style tab under the two styles. It combines with motion blur, depth
of field and the grade (`rt_grade=skate_video`). Every other style - off, the default - sets no `rt::ViewLens` and
registers no listener: the camera and every frame exactly as before. The lens' old switch `rt_fisheye` is gone: an
older ini's line does nothing (logged once) - no fallback turns the lens on from it (it would stay on under Art style
Off, as an old `rt_cel` line once kept cel shading on).

- **Not a crop.** Barrel-warping the normal picture magnifies its middle and pushes its edges out of the frame. Instead
  the game camera's field of view is widened for the lens and the wider picture is mapped through it: the middle at
  the original camera's scale (the skater the same size), the edges squeezed to show more.
- **The lens.** A family with the slope 1 at the axis for every strength s (`fisheye::radius_of`, focal units):
  tan(k phi) / k with k = 1 - 2s below 0.5 (0: rectilinear - nothing moves; 0.25 stereographic), phi at 0.5
  (equidistant), sin(k phi) / k with k = 2s - 1 above it (0.75 equisolid - the default -, 1 orthographic). At most 75
  degrees off the axis (`kMostAngle`): a wider camera is framed there.
- **The camera.** `rt::set_view_lens` (the backend, from the settings) asks render_pc to widen. `draw_viewport` fits
  the lens to the view (`fisheye::fit`): the frame's reach - the corners, a round frame (`rt_fisheye_circle`: from the
  corners at 0 to the shorter sides at 1) or the lens' 75 degrees -, and the factor `widen` that makes the widened
  rectilinear view just hold every place the lens shows (the farthest across is on the reach, at min(side, reach)
  along each axis; tight - `thug2_fisheye_check`). The camera's angle is drawn with its half angle's tangent times
  `widen` for the world's passes, the sky's and the world's again (`lens_angle`) - so the projection, `ViewShape` and
  with it the culling follow -, and `ViewDrawn::lens` carries the fit to the listeners. Only the window's one
  viewport (as the upscaler's), in a level (not the front end), without the level editor, a split comparison or a
  picture; a paused game keeps the lens (the world behind the pause menu doesn't jump).
- **Where.** The camera effects' listener at `rt::ViewOrder::kLens` (970): after the upscaler (950), cel shading's
  outlines (960) and motion blur / depth of field (965), at the output resolution; before the art styles (975: the VHS's
  REC stays straight) and the grade (985); the 2D after, undistorted. The motion vectors and the upscaler's history are
  the widened picture's - consistent, the warp comes after them.
- **The pass** (one full-screen pass on `film_kit`): per output pixel its place n in the original view's focal units,
  r = |n|, the widened picture read at n tan(angle_of(r)) / r / widen; red and blue read 0.6% x s x e^2 further out and
  nearer (e = r / reach), each channel three taps along the line kSoftShare x s x e^2 apart; the vignette
  1 - `rt_fisheye_vignette` x 0.92 x smoothstep(0.35, 1, e); black past a round frame's soft edge.
- **Checks.** `thug2_fisheye_check` (pure): the family's members, the centre's scale, the inverse, the fit tight for
  16:9 and 4:3 at each strength and frame, the widened projection's scale, the chain camera -> pass reading back what
  was drawn, a straight line bowed. `thug2_vulkan_camera_fx_check`: the pass against `fisheye_reference`, a line
  bowed, the centre at the original camera's scale (a line d pixels off the widened picture's middle shown widen x d
  off it), the vignette, a view not widened and `rt_style=off` bit for bit as they were; the lens on with
  `rt_style=fisheye` / `fisheye_vhs` (also with motion blur, depth of field and a grade), off with `vhs` and `off`.
  `thug2_vulkan_style_film_check`: `fisheye_vhs` against the vhs twin without its barrel, bit for bit the vhs at
  `rt_style_vhs_fisheye` 0 whatever that knob says; `fisheye` and `off` no style pass. `thug2_vulkan_style_check`:
  `fisheye` no pass, the names read back.
- **Not done.** What the game draws in 2D at a world point (names over skaters online, goal arrows) is placed by the
  widened rectilinear camera, not through the lens. The widened picture has fewer pixels per degree at the middle: the
  middle is upscaled by `widen` (x1.3 - 1.6 at the default) and a little softer. `rt_style_vhs_fisheye` stays the vhs
  style's own post warp of the normal picture (under `vhs` only).

### Changing the settings while the game runs (the Options screen)

`app/renderer_choice.cpp` reads each effect's settings in a function of its own (`read_rt_shadows`, `read_rt_ao`,
`read_rt_gi`, `read_rt_reflections`, `read_hdr`, `read_rt_fog`, `read_rt_sun`, `read_rt_sky`, `read_rt_relit`,
`read_upscale`, `read_rt_pbr`, `read_style`, `read_grass`, `read_rt_cel`, `read_water`, `read_camera_fx` - the camera effects and the fisheye lens) and installs `reload_live_key` through `render::graphics_options::set_live_reload`. When the new front
end's Options screen writes a `[Graphics]` key it calls `render::graphics_options::reload_live(key)`: a key of an
effect (`kLiveKeys`: the shadows', occlusion's, the bounce's, the reflections', the tone mapping's numbers, the fog's,
the sun / shafts / bloom / sky model's, the sky and clouds', relit's, the per-pixel lighting's - `rt_pbr`, `rt_pbr_*` -,
the art style's, the grass's - `grass`, `grass_*` -, cel shading's - `rt_cel`, `rt_cel_*` - and the water's - `rt_water`,
`rt_water_*` - and the time of day's - `tod`, `tod_*`: the clock's settings, then relit's)
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
| R5 | Reflections / ambient occlusion and a denoiser | R2, R3 | done - ambient occlusion and the denoiser: `gpu_vulkan/vulkan_rt_effects` (`[Graphics] rt_ao`, `rt_ao_strength` / `_radius` / `_rays` / `_denoise`, off by default; `rt_ao_characters` - "Characters") listens at `rt::opaque_view_drawn` after R4 and runs compute passes over images of the viewport's size - the rays (world position and normal rebuilt from the depth as R4 does - sample 0's own position in a multisampled target -, `rt_ao_rays` cosine-weighted ray queries up to `rt_ao_radius` world units, masks 0x01 + 0x02, directions from a 2D R2 sequence advanced each frame and turned per pixel by a hash), the temporal accumulation (the world position reprojected through the previous frame's camera of that viewport, bilinear over the texels whose guide agrees - on the pixel's plane, normal within ~25 degrees -, the history clamped to the 3x3 spread of this frame's rays plus a noise floor, up to 32 frames; nothing agrees: it starts again), three a-trous passes (5x5 B3 kernel at steps 1, 2, 4, weighted by normal, plane distance and the noise the history's length leaves) - then one full-screen multiplying pass, colour x (1 - strength x occlusion). Histories per viewport (rectangle + target size, up to 4). The TLAS comes from R2 through `rt_effects_set_scene_locked()`. `thug2_vulkan_rt_effects_check` (38 checks, both GPUs here, validation with synchronization 0): the raw rays against the same directions traced on the processor (50k clear-cut pixels exact, one sample and four), the denoised still camera against a 256-ray reference (mean error 0.003, 99th percentile 0.035), a moved camera's histories kept where the surface was seen (47k pixels) and restarted where it was disoccluded (1.5k), no NaN; `tools/game/rt_shadows_check.ts --effect ao` checks it in the game. Reflections: `gpu_vulkan/vulkan_rt_reflections` (`[Graphics] rt_reflections`, `rt_reflection_strength`, off by default), a listener after R4's and the AO's. Which surfaces: the game marks shiny ones with an environment-mapped material pass (pass flag 0x8, a sky picture sphere-mapped on); R1's registration gives `rt::Material::reflectivity` (that pass's brightest colour channel, doubled as drawn, times its fixed alpha where its blend mode takes one) and `environment_pass`, carried into R3's record (48 bytes a row). Hit shading: `rt::Geometry` has the texture coordinates' and colour's offsets and formats (R1 from the mesh's offsets and vertex format), R2 writes a geometry table per frame (`GeometryRecord`: the vertex / index buffers' device addresses as the frame uses them, the layout, strips as its own 32-bit lists) bound beside the TLAS, and `scene_geometry_glsl()` fetches a hit triangle's vertices through `GL_EXT_buffer_reference`. The pass: a primary ray per pixel (toward the depth's world point; kept when it ends there) identifies the surface and its material - no per-pixel material target, and nothing at all while the frame has no reflective instance; a reflective one gets the triangle's flat normal and one mirror ray (masks 0x01 + 0x02 + 0x04, alpha tested along the way), its hit shaded with the material's texture at the interpolated texture coordinates (the level from the ray's footprint: the pixel's spread over both legs), times the vertex colour doubled as the game's pixel shaders do (objects: the frame's ambient and directional lights); a miss leaves the pixel to the game's own environment map. A perfect mirror is one deterministic ray: no noise, so no denoiser. A full-screen pass mixes the colour toward it by reflectivity x strength (alpha kept; the pixels without a reflection discarded). `thug2_vulkan_rt_reflections_check` (39 checks, both GPUs, validation and synchronization validation 0): texture coordinates, colours and positions at 2100 hits through the table in every mesh layout (16- / 32-bit indices, a 16-bit strip from an odd index, unindexed lists and strips, offsets, an instance scaled, one with neither attribute) against the meshes' own; a mirror floor (a 32-bit strip) reflecting a box with an 8 x 8 texture, ~9000 clear-cut pixels each the processor's texel x vertex colour, one sample and four, strength 1 and 0.4; the box, the sky and the floor that mirrors nothing exactly as without the pass; nothing reflective or the setting off: no pass. `tools/game/rt_shadows_check.ts --effect reflections` checks it in the game. Not done: fog on the reflection, back faces treated as surfaces, rough (glossy) reflections beyond glass's (see "Water, glass and the skater's shadow": Fresnel, masks, rough glass) |
| H1 | HDR targets, tone mapping and colour grading (`rt_hdr`): the base the added light (GI, fog) needs | V7, V8 | done - `gpu_vulkan/vulkan_hdr` (above, "HDR and tone mapping"): float back buffer and world targets, the tone-mapping pass last at `world_drawn`, `thug2_vulkan_hdr_check`, `rt_shadows_check.ts --effect hdr` |
| R5 fog | Volumetric fog and light shafts | R2, R4's baked sun | done - `gpu_vulkan/vulkan_rt_fog` (`[Graphics] rt_fog`, off by default): the level's own linear fog as a medium of the same transmittance, lit by the baked sun through ray queries at half resolution with temporal accumulation and a depth-aware upsample; replaces the game's fog (`rt_fog_mode=0`) or adds the shafts over it (1); a level without fog gets a haze's shafts (`rt_fog_haze`) - "Volumetric fog and light shafts" above |
| GI | One-bounce diffuse indirect light ("secondary lighting") with ray queries | R2, R3, R5, the baked sun | done: `gpu_vulkan/vulkan_rt_effects` (`[Graphics] rt_gi`, `rt_gi_strength` / `_rays` / `_distance` / `_denoise`, off by default), a view listener at `kIndirect`: the sun's light bounced once off what is around a pixel (hits shaded with R3's material at R2's texture coordinates, a shadow ray toward the level's baked sun), R5's denoiser over the colour, added to the frame as strength x the pixel's albedo x the light through a screen blend (see "One-bounce indirect light"). `thug2_vulkan_rt_gi_check` (39 checks, both GPUs, validation and synchronization 0); `tools/game/rt_shadows_check.ts --effect gi`; 1080p on the RTX 5070 Ti: 1.5 / 2.0 / 2.4 ms for 1 / 2 / 4 rays |
| Sun | The sun, its glare, screen-space rays (retired by the volumetric light), an energy-conserving bloom and a sky model (aerial perspective) | R2, R4's baked sun, R5 fog, H1 | done: `gpu_vulkan/vulkan_rt_sun` + `vulkan_rt_sky`, `engine/gfx/rt_sky` (`[Graphics] rt_sun`, `rt_bloom`, `rt_sky_model`, off by default): the original's sun sprite and lens flare replaced by a disc and aureole where the game shows its sun, (radial rays over the depth's sky mask - removed: the fog's volumetric shafts), a 13-tap / tent mip-chain bloom over float intermediates (the same with `rt_hdr` 0 and 1), the haze as aerial perspective coloured by a single-scattering sky (see "The sun, its glare and the sky"). `thug2_vulkan_rt_sun_check` (both GPUs, validation and synchronization 0); `tools/game/rt_shadows_check.ts --effect sun` |
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

## Working on the renderer

The conventions a change here follows (`CONTRIBUTING.md` has the general ones):

- **Where Vulkan is seen.** The Vulkan, volk, memory-allocator and shader-compiler headers are included only in
  `src/gpu_vulkan` and the check programs listed in `tools/vulkan_headers.test.ts` (`CHECKS`); only `src/app`
  includes the backend's headers. A new `src/tests/vulkan_*_check.cpp` is added to that list.
- **The order of the passes** after the world is drawn is `rt::ViewOrder` in `src/engine/gfx/rt_scene.h` - each
  effect is a view listener at its own number (shadows, occlusion, bounce light, the lamps, decals, grass, the
  upscaler, the art styles, the camera effects, the lens, the grade, frame generation last). A new effect takes a
  free number there, with a comment on what it must come after and before.
- **The fixed-function key** (`FragmentKey`, `src/gpu_vulkan/ff_key.h`) has a fixed size: every byte is spoken for, so
  a new shader variant takes spare bits of an existing field and says so in the key's text form (`ff_key.cpp`).
- **Check programs.** Each feature has one (`thug2_vulkan_<feature>_check`, `thug2_ff_shader_check`,
  `thug2_daylight_check`, `thug2_options_check`, `thug2_rt_scene_check`): a change adds cases for what it changes. They
  open their window without taking focus (`src/tests/check_window.h`).
- **Settings** come with their README row, Options-screen row, settings report line and live reload where they can
  change while playing (`CONTRIBUTING.md`).

## One lighting path (the time of day's lighting state)

What the time of day lights, and how - one state, one path; it replaced the darkening steps layered on the original's
bake (the relit level's scales, the characters' lights scaled apart, a global colour for placed objects, one for unlit
objects, the residual added back unscaled), which disagreed with each other and assembled the night over the first
frames after a load. Without ray tracing, with `rt_relit=0` or in a level that keeps the original's lighting (no relit
data, or no sunlight in its bake - "Relit: the bake taken apart, offline"), nothing here runs:
the original's own time of day (`tod_manager.h`, blended by the clock) is the look, untouched.

1. **One lighting state per frame** (`engine/gfx/rt_daylight.h` `DayLight`, made by the clock - `day_clock.cpp` - and
   nothing else): toward the sun or the moon, its light's colour and strength over the level's own sun's
   (`light_colour` = `direct` x `direct_tint`: the moon's 0.22 and blue at night), its softness (`light_radius`: the
   sun's 1.5 degrees, the moon's 6 - weak and soft), the sky's light (the sky model's irradiance at the sun's
   elevation over the level's own, with the night sky's floor), the bounce (the direct light and the sky's in the
   level's own shares), the lamps (`lamps`, `rt_lights=auto`), the stars, the moon's disc and the exposure. Relit
   takes its terms as they are; the shadow pass's softness follows the light's radius; the bounce light's sun is
   `light_colour`; the lamps and the exposure read it. Nobody keeps a factor of its own.
2. **True surface colours** (`rt_relit.h`): the level's bake is taken apart into tint x light (de-lit, not scaled). The
   residual - the bake's light over the model - is taken apart by what it is (`relit_split_residual`): on a face toward
   the bake's sun, its part in the sun's colour that is at least 0.6 of the sun's whole light there is the bake's sun
   the rays missed (`sun_missed`: more visibility toward the bake's sun, at most twice its light), lit by the direct
   light now and only toward the bake's own sun
   (whole within 2 degrees, none past 10: toward the moon the traced visibility alone - no hard wedges of the bake's
   sun shadows at night); the rest - lamps, glows, the artists' lights - stays the residual, the level's own emitters,
   constant through the day (`rt_relit_residual=0` leaves it out). A lamp's light is told within a mesh (against the
   mesh's own neighbourhood), then across meshes (`relit_local_across`): a mesh a lamp lights whole takes the local light
   of its neighbours on other meshes facing the same way. The bounce keeps its part from the residuals where its rays
   land (`bounce_local`: a lamp-lit wall's light bounced into a recess) at full strength through the day. The bake's
   occlusion beyond the traced sky stays in the tint. `sun_baked` keeps the visibility toward the bake's sun for the
   field.
   **The fit** (round 4): an edge's error is relative to its own light and judged by the candidate itself (in the
   light's units the bounce - small everywhere - was the cheapest term); meshes of one colour all over (painted, not lit
   by the bake: 1238 of Berlin's 2843) give no edges; an edge across a traced shadow's border counts only where the
   bake steps down into the traced shade too (~18000 traced borders step 1.00 at the median in Berlin's and Boston's
   bakes - the traced geometry's own shadows); and the sun's strength against the rest is set per channel by the faces'
   turning, mesh by mesh (the faces toward the sun over those turned from it, the bake's against the model's).
   Berlin's sun 0.19 -> 0.55 (its faces 1.85x in the model, 2.17x in the bake), Boston's 0.24 -> 0.44. The fit logs
   these numbers. Relit's probe rays take the cut-outs by their alpha (`ProbeRequest::cutouts`, the shared test).
3. **One lighting path** (`engine/gfx/rt_light_field.h`): the relit level's model evaluated where a thing is - each
   vertex's direct light (a x N.L x visibility) and indirect (sky and bounce) now and at the bake, and the level's own
   lights, kept in 128-unit cells; a point's light is its cells' (trilinear, the empty ones left out, else the nearest
   within 3 cells, else the level's whole). What relit doesn't relight vertex by vertex was authored for the level's
   own light, so it takes the light now over the bake's where it stands: the objects' models (`colour_models_by_place`),
   unlit instances (`unlit_object_colour`), the game's lights for the skater, peds, cars and lit objects (the key light
   x the direct light; the fill and ambient x the field's indirect ratio - `pc_lights.cpp relight`; the level's own
   lights added as a light of their own - `local_at`: per cell a fit of the local light over the way a surface faces,
   c0 + c1 . n, given as an ambient part and a directional light toward the lamps, device light 2 - so a skater facing
   a lamp-lit wall is lit as the wall), the particles (the level's whole). The screen-space passes - shadows, occlusion, bounce, lamps (`rt_lights`), reflections - already
   light every pixel alike; every ray among them takes cut-outs by their alpha (`vulkan_rt_scene.h`
   `cutout_rays_glsl`). The clouds read the state's sun, sky and night sky (`cloud_light`). The air - the fog's and the
   haze's light shafts, the clouds' shadows in them and on the ground - is lit by the state's light
   (`rt_volume_light.h medium_light`: its direction, colour, strength and size; "Volumetric fog and light shafts").
4. **Night is light and exposure**: the moon's light, the night sky's, the level's own lamps and `rt_lights`' real
   lamps, and the exposure (`DayLight::exposure`, `rt_hdr`) - no darkening factors per kind of object. The exposure is
   the eye adapting to the relit level's own light (the light field's whole, its lamps in it): (1 / light)^0.5 within 1
   and 2, reached over ~1.5 s (`adapted_exposure`, `day_clock.cpp exposure_now`) - Boston's night 2.0, Berlin's 1.85,
   a dusk ~1.4, noon 1. The night sky's radiance is set for it.
5. **Ready before the first frame** (`rt_relit_world.h` `hold_first_light`): where the clock may light a level, the
   frames after its loading screen goes down are drawn (the world must stand still to be relit; the clock waits for the
   baked sun's fit; the night state's scripts run) under the loading screen's picture until the clock's light is
   published and the level is relit for it (every mesh written, the field made) ten frames in a row - at most 10 s.
   Why round 2's attempts left the level day-bright for good: relit keeps its entries by the vertex buffer's address,
   and the night state's scripts free and make meshes; a buffer made at a freed one's address was taken for it,
   already written, and drawn with its bake until the lighting changed again. `VertexBuffer::teardown` now forgets
   the entry (`rt_relit_world::buffer_released`), and a write of the game's own over a relit buffer marks it to be
   written again (`buffer_written`).

The test: Boston and Berlin at noon, dusk and night - a door under a lamp lit like the wall beside it, the skater and
the objects as bright as the ground they stand on, no hard-edged moon shadows, nothing appearing after the load.

### Each light counted once (the light energy audit)

The user, 2026-11-15: "Don't double shadow (baked in + real shadows), don't overbrighten (baked in + sun on it)". Every
part of a pixel's light has one owner. The relit level's vertex colours hold its whole light, worked out per vertex
(`rt_relit.h`: tint x (a x N.L x S + b x K + g x B) + residual); the ray-traced passes that light a pixel again -
shadows, occlusion, bounce, lamps - read what the colour was written with and replace their own part of it instead of
stacking another on top.

- **The light stream** (`engine/gfx/rt_cel.h`): five words a vertex - the ambient (sky and bounce), N.L, tint, the sun's
  visibility the colour was written with (`seen`, the missed sun in it), the residual, the direct light's colour and
  strength, the missed sun's share, the sky's part and K. Written by `render_pc/rt_relit_world` with the colours (the
  cel draws read words 0 - 2), handed to the RayTracing per mesh (`RayTracing::set_light_stream`; null while the mesh
  shows the bake: relit off, strength under 1, a level relit can't take apart); the geometry table carries its address
  (`GeometryRecord::light_address`, `kGeometryLight`), the passes read it at the pixel's triangle (`vulkan_rt_scene.h`
  `hit_world_light`, `world_light_with`). Its processor twins: `stream_parts`, `stream_light`.
- **Who owns what** (relit on, a level with a stream; elsewhere the passes as before):

  | term | level geometry | objects / props | characters | particles |
  |---|---|---|---|---|
  | sun / moon direct | relit (a x N.L x direct scale) | the field's ratio x their bake | the game's key light x the state's light colour | the level's whole ratio |
  | its shadowing | `rt_shadows` per pixel in place of relit's per-vertex S (T(v) / T(seen)); without it relit's S | `rt_shadows` (the world's shadows) | `rt_shadows` (the world's, on the key light alone) | - |
  | sky / ambient | relit (b x K) | the field | the game's fill / ambient x the field's indirect ratio | the level's whole |
  | occlusion | relit's K (the bake's own occlusion in the tint); `rt_ao` leaves the stream's pixels | `rt_ao` | `rt_ao` (`rt_ao_characters`) | - |
  | bounce | relit (g x B, `bounce_local`); `rt_gi` out of effect with relit | the field | the game's fill x the field | - |
  | lamps | relit's residual; `rt_lights` adds max(L - residual, 0) | the field's local light, or `rt_lights` while it lights (`rt::lamps_drawn`) | the same | - |
  | painted shading | per texture: `rt_delight` or the de-lit set (`delit-per-texture`) | the same | - | - |
  | exposure | `DayLight::exposure` (`rt_hdr`) once for all | | | |

  Before: `rt_shadows` multiplied the colour by 1 - strength x occlusion over relit's own traced S (the agreement grid
  and the sky test let it in where the bake was dark too) - shadows twice; `rt_ao` multiplied it over relit's K and the
  bake's occlusion in the tint - corners twice; `rt_gi` added the sun's bounce over relit's g x B; `rt_lights` added the
  lamps over the residual (the bake's lamps), and on the objects over the field's local light - lamp areas twice.
- **The shadows** (`vulkan_rt_shadows.cpp`, the alpha-tested variant - it has the geometry table): a camera ray finds the
  pixel's world triangle; with a stream the pixel's own visibility (`1 - occlusion + missed`, times what the objects
  block) replaces `seen` by `rt_shadow_strength` (1: wholly) and the colour is scaled by T(v) / T(seen) - a 2x blend
  (the shader writes half), so the pass may also lighten what the vertices' interpolated shade darkened. No agreement or
  sky test there: relit's residual and missed sun already say where the bake lit what the rays shade. `day.shadow` isn't
  used for it either: the stream's direct light is the state's.
- **The objects' shadows** (the same variant): an object drawn lit by the game's lights - the skater, the peds, the
  vehicles - has them in the frame record (`rt::ObjectLight`: the device's ambient, light 0 - the key light, the direct
  light -, light 1 - the fill -, the level's lamps' local light, as `pc_instance.cpp` sends them; `rt_record::object_lit`,
  a frame's lag: the record is made before the draws), and its instances' rows point at them
  (`GeometryRecord::object_light_address`, `kGeometryObjectLight`; `hit_object_light`, `object_light_with`). In the sun's
  shadow (the rays, the clouds' shadow) its pixel loses only the key light at its normal (the mesh's own, else the
  depth's): (A + F + L + K (1 - strength x occlusion)) / (A + F + L + K), each held to 1 as Direct3D's lighting is.
  Before, the pass multiplied the object's whole colour by 1 - strength x occlusion, so with `rt_shadow_strength=1` a
  skater under a cloud's shadow (06:10 in College's plaza) or a building's, or in moonlight's shade, was drawn black -
  only what the game draws unlit (the laces) left. An object drawn with its own colours (no lights: props lit by the
  field) has no record and keeps the old multiply. Every instance of a character gets its lights - its parts share one
  skeleton and are lit once (`send_lights` only for the first), so each takes the lights it is drawn with.
- **Check:** `thug2_vulkan_rt_lights_check` "each light counted once" - a floor written in the box's shadow keeps it
  (not darkened again) and is lit where the pixel sees the sun; one written lit takes A / (A + S) in the shadow; a lamp
  over a floor whose residual holds it adds nothing. `thug2_vulkan_rt_relit_check` 3b - the synthetic level whose bake
  is exact lighting, every term on at the bake's time, back to the bake within 2 bytes at 1011 of 1013 vertices; the old
  stack off by more than 4 at 536 of them.
- **Measured** (`tools/game/light_audit.ts`: each level's restarts, the game's camera, tod=level, the look effects that
  don't light off; the full look against the bake - `relit 0` and no ray-traced light). Median ratios over BO, BE, LA,
  NO and nj (20 places): before shade 0.973 (worst 0.72), corners 0.964 (0.72), whole 0.958 (0.65); after shade 1.004,
  corners 1.006, whole 0.991. The whole-level sweep and the census: `light_audit.ts walk / capture / compare / html`.
- **The audit's open items, solved by the offline data** (docs above, "Relit: the bake taken apart, offline"): the
  lamps are a term of the model (`E = a S + b K + g B + c L`, L traced from the emitters and point lights `rt_lights`
  shines) - the lamps pass replaces exactly that term (the own light, `relit_own_light`, the stream's residual word); a
  level keeping its bake never has a pass stacked on it (no data, or no sunlight: the original game's lighting, the
  lighting passes out of effect); the bake's sunlight is never left in a tint (`relit_sun_in_tint`: direct light,
  shadowed); the characters' fill and ambient are the game's own times (indirect + lamps) now over at the bake; and the
  bake's inputs are the level's static meshes, whatever the first frame shows (sch's sun no longer flips).
- **The fit's inputs fixed** (the audit's census found fits that changed between loads): the baked sun's samples are
  taken by where each triangle is (a hash of its centre, one in a power-of-two step - `rt_record.cpp bake_samples`), so a
  mesh more or less in the first frame changes only its own samples - before, every n-th triangle in the world's order
  moved them all and the sun by degrees (sch: heading -43 / -45), enough to tip relit's fit. A sun channel the edges gave
  nothing (a green sun: AU, sch) starts from the others' and the faces' turning sets it (relit cache v7).
