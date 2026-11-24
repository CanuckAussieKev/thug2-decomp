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
  the editor), colour-only writes not counted. (Path tracing, stage 3: a mesh skinned on the CPU is no longer
  registered itself - its buffers held the frame before's last pose, shared by every instance -; each instance's pose of
  the frame is a mesh of its own, skinned at the record with that instance's bones: engine/gfx/rt_poses.h, "Everything
  drawn in the acceleration structure" below. Round CPU: skinned once a frame - the frame's skinned meshes are skinned
  ahead of the record over a few worker threads (`MeshRef::skin_ahead`, base/core/parallel_for.h; each worker takes the
  game thread's floating-point state), into memory of their own with the bones they used; the pose copies them, and the
  draw's `render()` copies them too when it skins the same mesh for the same instance with the same bones that frame -
  the same function on the same inputs, so the same bytes. Billboard cards are turned in memory of their own and copied
  into the locked buffer, the rays' copy taken from there: upload memory is never read back.)
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

### HDR and tone mapping (`rt_hdr`)

(The metering below was rewritten by the physical air - "The physical air, sun and camera": one controller, both ways,
a calibrated camera.)

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
- **The metering - auto exposure** (`rt_exposure_adapt`, stops; 2 by default, 0 off; added after the user's College
  screenshot at 06:20: "the sun is completely blinding" - a white blot over the middle third of the picture that ate the
  buildings round it). Measured first at that view (captures at `rt_exposure -6`, each term switched off in turn; display
  values, the original's white 1): toward the 12-degree sun the frame held 3.5 (the sky's own sun and the sun pass's
  disc and glare 1.1, the sky model's haze at the user's Thick `rt_fog_haze_density` +1.2, the physical street air
  +0.2, the cinematic air's excess +0.9), the buildings 20 - 30 degrees off it 0.4 - 0.5 (0.2 without the air), the
  street 0.46 - and the curve shows nothing above its white point (1.25). Physically the glow is right (a low sun's
  aureole through morning haze is many times a sunlit wall); what was missing is what a camera or an eye does looking
  there: stop down. So before the curve each viewport is metered (`vulkan_hdr.h` "The metering"): 64 x 64 samples of
  its linear luminance (each at most 16 - a sensor's white), weighted toward the centre, their root mean square (a
  highlight-weighted meter: a plain mean hardly moved for a glow over a tenth of the picture); above `kMeterKey` (0.55 -
  the brightest ordinary view's: College at noon 0.35 - 0.56, Berlin and Boston by day and dusk 0.1 - 0.44 measured from
  captures - none touched) the exposure comes down by log2(meter / key) stops, at most `rt_exposure_adapt`, never up (the
  clock's exposure brings the night up); toward darker over 0.4 s, back over 1.2 s, a new or long-unseen viewport at its
  target at once. One compute group over the viewport's copy writes the viewport's slot (four, by its rectangle), the
  curve's pass reads it - no read-back. The user's view: ~1.8 stops down - the glow round the sun, the buildings and
  flags against it, the street darker (out/pending/glare.md's captures). College's tower views toward the sun 0.6 - 1
  stop down. Cost: within the pass's 0.04 ms at 1920 x 1080 (RTX 5070 Ti). Checked (`thug2_vulkan_hdr_check`): a white
  view's meter and stops against the processor's twins (meter_light / meter_target_stops), a mid-grey one untouched, the
  check's random image's meter within 0.2% of the processor's, the way back up gradual, a 0.6 s gap starting at the
  target, the bound, `rt_exposure_adapt=0` the curve alone.
- **Not done:** HDR output to an HDR display (the swapchain stays 8-bit sRGB-less UNORM).

### The physical air, sun and camera (`rt_fog_air` as the weather, `rt_sun`, `rt_bloom`, `rt_exposure_adapt`)

Lighting is physical: the air, the sky, the sun and the camera are what a path tracer would use, and nothing is tuned
toward a look (the user's rule, 2026-11-16: fix inputs, never outputs; levels may look different). Before it, a low
sun through the "misty" air with Strong god rays showed a ~40-degree white blot (the painted aureole x the cinematic
air's glow x the god rays' x2 x a Thick haze capped "for Barcelona's harbour", over a meter that only went down):

- **One unit.** The sky model's radiances per unit of the sun's irradiance x `rt::sky_exposure()` = pi / E, E the
  horizontal irradiance under the reference day (the sun 45 degrees up in the clear weather's air, every order of
  scattering): linear 1 is a white diffuser under that day - the game's white. The sky's zenith then is 0.09 of white
  and its horizon 2.9 x the zenith a quarter turn from the sun (a measured clear sky's 0.05 - 0.2 and about 2 - 3;
  `daylight_check`), where the old target put the zenith at 0.3 and the horizon at 5 - 9 x it.
- **One medium** (`engine/gfx/rt_sky.h` Aerosol, `rt_volume_light.h`): the weather (`rt_fog_air`: clear 40 km, hazy
  10 km, misty 2 km at the ground - a 100 m mist under the clear day's haze) is a visibility (Koschmieder: 3.912 / the
  extinction at 550 nm, the air's own Rayleigh included); the aerosol's extinction follows Angstrom's law over the
  model's 680 / 550 / 440 nm, its single-scattering albedo and its Cornette-Shanks g are its type's (continental haze
  0.9 / 0.8, droplets 0.97 / 0.8). The sky's three tables (the aerosol a push constant: the transmittance and Psi made
  again when it changes), the sun's transmittance and colour, the level's air (the fog pass's aerial march) and the
  level fog's street air read the same `rt::aerosol()`. Rain (5 km) and falling snow (3 km) add a grey layer near the
  ground. The clear day's aerosol optical depth overhead is 0.10 (it was 0.01 - a sky with almost no haze).
- **The aerial march** (`vulkan_rt_fog` AERIAL): Rayleigh + aerosol single scattering of the light's irradiance (its
  shadow ray / the air's sun map / the clouds' map) through both phases, every higher order as the sky's own (Psi at
  the ground) x the open share of the sky at the point (added by the indoor air: one ray a pair of steps toward the upper
  hemisphere, even in solid angle, averaged by the history - a closed room's air gets none of the sky's light: 0.3% of
  the open air's in `rt_sun_check`'s closed box; TR's warehouse under a hazy sky lost a grey veil of +47% over its far
  walls), the lamps' irradiance (pi x their light) x their shadow ray; each segment exact for an even medium; one transmittance (the
  extinction's luma); composited in linear light over a copy of the colour. The sky's pixels are left alone where the
  atmosphere is drawn (its sky-view table holds the whole ray). Gone: the cap toward the sun (`kFogAerialGlow`), the
  sky's colour as the haze's light, the shade/sun shares, the time-of-day and water multipliers, the cinematic and misty
  looks (`AirArt`: the volume round the eye, side lobes, glows - x3 the lamps), `rt_fog_strength`,
  `rt_fog_anisotropy`, `rt_fog_haze_density` / `_height`. The air's sun map is a sampling device only (`kAirMapReach`
  100 m; the march's steps `steps` inside it, half as many past it). The air is lit from the true sun (the light's
  direction held 6 degrees up for the level's shadows is not the air's).
- **God rays** (`rt_fog_god_rays` on / off): the shadowing of the aerosol phase's forward peak (over its value 30
  degrees out) found at the screen's resolution - every point of a view ray toward the light projects onto the same
  screen line (epipolar) -, the share of the air before what stands there the medium's own ((1 - e^(-sigma d)) / (1 -
  T)). Unshadowed it is the march's own light, moved, not made (`rt_fog_check`: the phases add up; open at most 1).
- **The sun** (`rt_sun`): the real sun - 0.53 degrees across, limb-darkened (u 0.6), its mean radiance the atmosphere's
  transmitted sun over its solid angle in the unit (`rt::sun_disc_radiance`: 5 x 10^4 at noon, a few thousand at 3
  degrees) - drawn in the sky by the sky pass (behind the clouds) with `rt_sky`, else by the sun's pass. No painted
  aureole (it is the sky's own forward scattering: Cornette-Shanks g 0.8 falls to half 9.7 degrees out), no 2 x disc,
  no `rt_sun_size`, no `rt_sun_colour`.
- **The lens** (`rt_bloom`): one energy-conserving spread of all the light in linear light - the eye's veiling glare
  (Stiles and Holladay, L = 10 E / theta^2: kGlarePerOctave = 1.3% of the light per doubling of the angle, one per
  level of the chain); no separate sun glare, no `rt_bloom_strength`.
- **The camera** (`vulkan_hdr`, `rt_exposure_adapt`): one controller - the centre-weighted log average of the linear
  luminance, exposed as a calibrated camera (ISO 2720 K = 12.5: the average at 0.104 of white) with the eye's
  adaptation (Krawczyk et al.: the key falls in the dark - the unit made absolute by the sun's 128 000 lux at the top),
  both ways within +-`rt_exposure_adapt` stops. The clock's own exposure (DayLight::exposure, up only) and the meter's
  down-only RMS are gone; the night sky's radiance is no longer tuned for an exposure.
- **One light state**: the direct light's share of the level's own light from the model at its own sun
  (`DayLight::direct_share`, `rt::direct_share_at`) for the bounce, the shadows' strength, the overcast and the snow -
  not 0.65 for every level; the sun's colour without the clock always the atmosphere's (no relight waiting for the sky
  model to publish); the clouds' shadows only as the sun's visibility in the shadows' pass (the multiply over all the
  light gone); glass F0 0.04 (`rt_glass_reflectance` retired).
- **What a path tracer reads**: sigma_s / sigma_a per band (`rt::kRayleigh`, `rt::aerosol()`, `kOzoneAbsorption`, the
  densities' heights), the phases (Rayleigh, Cornette-Shanks g), the sun (a disc of `kSunAngularRadius`, radiance
  `sun_disc_radiance`, transmittance `sun_transmittance` - next-event estimation), the sky's radiance (the sky-view
  table) for rays that leave, the unit (`sky_exposure`), and the camera (2^stops, then the tone curve; the glare's
  spread).
- **Still not physical** (named, for later rounds): the level fog's own path (a level's art fog, display-referred, its
  street air's light `kFogStreetAirLight`) (the moon's light and the night sky's radiance: physical since round S),

  the light's direction held 6 degrees up, lamp intensities - solved from the bakes since lighting round A2. (The frame's display-referred colour target:
  fixed by round F, below.)

### A linear, scene-referred frame (round F; `rt_hdr` retired)

Before it the world's target held the game's display-referred colours and each effect converted `^2.2` / `^(1/2.2)`
round its own maths; `rt_hdr=0` kept 8-bit targets and with them a second lighting pipeline (the fog's light clamped to
1, the sky and the sun's composite clipped, the water's glitter capped at 1, the bounce and the lamps screen-blended).
Now:

- **One transfer** (`engine/gfx/rt_display.h`): the original's colours (textures, vertex colours, the bake, fog and
  clear colours, the relit stream's light) are display-referred - gamma 2.2, 1 its white; `linear_from_display` /
  `display_from_linear` and their GLSL twins are the only conversions. A pure power law (not sRGB's piecewise curve):
  it is multiplicative, so a lit surface (texel x light) decodes the same at its inputs or its output, and a ratio of
  display-referred light decodes as its power.
- **The frame**: on Vulkan always 16-bit float, linear light in the sky's unit (1 = a white diffuser under the
  reference day). From a world view's `view_begins` to its tone mapping (`Backend::linear_phase`) a draw into the float
  target decodes its fragment at the output (`ff_shaders.cpp fragment_linear_output` - before the fog, which mixes
  the decoded fog colour, and before the blend: alpha, additive glows and multiplying decals all compose light); a
  stage sampling a float texture (a copy of this view's world) encodes it back so the game's colour arithmetic keeps
  its inputs; clears are decoded. The 2D, loading screens, movies, 8-bit targets (the sky capture, previews) and the
  game's own screen blur and bloom (drawn after the tone mapping) keep the display's colours, as the original's.
- **The effects** compose linear light: the fog's composites (`colour x T + in`; the level fog's display-referred light
  decoded), the sun's disc and glare, the sky pass, the reflections (the sky as radiance; a hit - still the bake's
  light, M8 - decoded), the bounce and the lamps (added, their albedos decoded), the shadows (`colour x factor` on the
  float frame; a ratio of display-referred light decoded, a share of linear light as it is). Effects whose own model
  is still display-referred (the snow layer and flakes, clutter, water) encode what they read from the frame and
  decode what they write - their rounds replace the model.
- **One output transform** (`vulkan_hdr`, `ViewOrder::kToneMapping` 972: after the upscaler, cel, motion blur / depth
  of field and the lens, before the art styles and the grade): exposure `2^rt_exposure` x the meter's `2^stops` on the
  light (the meter reads linear luminance), then the display's encoding once, then the display's preferences
  (saturation, contrast, the Game / ACES curve, the grade LUT) on the encoded colour.
- **Upscalers** take the linear HDR colour: DLSS / Ray Reconstruction `colorBuffersHDR`, FSR
  `ENABLE_HIGH_DYNAMIC_RANGE`; exposure 1 (the unit's white is 1).
- **Not done**: an HDR display's output (an HDR10 / scRGB swap chain - a key of its own choosing only the output); the
  the game's own lights on the objects are still display-referred (round M's per-pixel BRDF). (Lighting round A2: the
  relit light store is linear in the unit - relit data v5 decodes the bake before the fit; its colour word is tint x
  light encoded, so the draw's decode undoes only the texel's encoding; the shadows', lamps' and bounce passes decode
  only the texel.)

### Volumetric fog and light shafts

(Round FOG - rewritten as froxel volumetric fog, as Wronski's "Volumetric Fog" (AC4, 2014), Hillaire's "Physically Based
and Unified Volumetric Rendering in Frostbite" (2015) and Unreal Engine's volumetric fog do it; vulkan_rt_fog.h has the
passes. The per-pixel march with its few resampled lights a step pair and its ReLAX-style histories (round NK, below) is
retired: near small, very bright emitters - BO's neon strips at night - it gave soft blue-white blobs that bubbled and
crawled with the camera still. Now: a camera-aligned grid (a column per `rt_fog_quality` 16 / 16 / 12 pixels, 48 / 64 / 64
slices spread exponentially from the eye to the fog's end or the aerial air's reach); each froxel takes 2 / 4 / 6 jittered
samples a frame of the light that reaches it - the sun's or moon's share (its ray, the air's sun map, the clouds' map),
the light list's local lights by RIS (candidates from ReSTIR DI's light grid - ReGIR - inside it, the pool outside; a
light's point never taken nearer than the froxel's own size), the sky's open share - blended with the grid of the frame
before reprojected (1 / frames, then 1 / 20: 0.95 history); a 3 x 3 x 3 tent over the froxels' local-light light as it is
integrated; each column integrated front to back (each medium's optical depth exact over a slice: the level's fog
transmittance stays the game's f); the composite reads the integral before the pixel's slice and follows the slice's
source to the pixel's depth along its own ray. Ray Reconstruction's transparency layer gets the same at the front layer's
depth. The composite stays where it was, before the upscaler. BO at night (1280 x 720, the brief's spot, held still -
speck_count std of the building): the picture 4.11 -> 2.49, the fog term 1.85 -> 1.14 with no flickering specks left;
what remains is a slow drift of the air near the strips over about a second: ReGIR's cell picks are shared by every point
of a cell for a frame, and the history only averages that over its 20 frames.)

(Its air was rewritten by the physical air - "The physical air, sun and camera": the weather's aerosol, no looks, no
cap; what follows is its history.)

(Round NK - the history, as NVIDIA NRD ReLAX accumulates a noisy signal: a fast history (up to 6 frames, never clamped)
and the slow one (up to 30 frames, was 12) clamped to the fast history's 3 x 3 mean +- 2 deviations (was this frame's own
march's 3 x 3 +- 1.5). Each frame's march is one resampled light a step pair and one ray of each kind - noisy by design;
clamped to that one march's neighbourhood, where its nine samples missed the lamps' light the box shut and the history
was thrown away: at night, with the camera ~12 stops up, the dark air showed it as moving grain ("an untuned TV"), and
DLSS Ray Reconstruction, taking the air's light in its input for surface light, made blotches of it. Measured at the
user's Boston spot (1920x1080, DLSS Quality + RR, still camera, temporal deviation of the shown luma over the lit air
under the archway): 0.45 steps with the fog against 0.25 without before; 0.19 - 0.22 against 0.21 after - the fog adds
none. thug2_vulkan_rt_lights_check "the night air's grain": the history's deviation 0.118 -> 0.072 of its light, its
mean 0.985 -> 0.990 of the marches'. A history that starts again is still seeded with 12 marches - `kFogSeedFrames`.)

One volumetric light system: when something stands between the eye and the sun - a building, a tree, a cloud - the
air shows it, as a shadow shaft in lit haze or a lit column through a gap; at night the moon and the street lights do
the same. `gpu_vulkan/vulkan_rt_fog` (`[Graphics] rt_fog`, off by default; `rt_fog_air`, `rt_fog_strength`, `rt_fog_quality`,
`rt_fog_mode`, `rt_fog_density`, `rt_fog_height`, `rt_fog_anisotropy`, `rt_fog_steps`, `rt_fog_haze*`) listens at
`rt::world_view_drawn` (`ViewOrder::kFog`, 500: after the transparent passes, particles and weather, before the sun's
glare, tone mapping and the 2D). Its model is `engine/gfx/rt_volume_light` (API-neutral; the checks' processor march
uses the same functions). The screen-space rays the sun pass drew over the sky's silhouettes (`rt_shafts`) were retired
by it: the shafts are this pass's, the only light in the air; the sun pass keeps the disc, its glare and the bloom.

- **The light: one state.** The air is lit by `rt::medium_light()`: the scene's sun (`scene_sun` - the one every effect
  lights by) and, while the clock lights the level (its origin `kClock` - "Time of day"), the clock's `DayLight` for it:
  the sun by day, the moon at night, `light_colour` (the light now over the level's own sun's - the moon's 0.22 and
  blue) and `light_radius` (the sun's 1.5 degrees, the moon's 6: each shadow ray aims at a point on the light's disc,
  so the shafts' edges are as soft as the shadows'). Without the clock the light is the level's own: colour 1, a point
  (the shafts as before the clock). So the shafts follow `tod`, a moved `rt_sun_source` and the console's `rt sun`.
  The sky model (`rt_sky_model=1` or `rt_sun_colour`) colours it: the air's colour for the sun the level was lit with
  (the clock's `native` sun - its colour is relative to that), luma kept. The game's own fog (modes 0 / 1) takes only
  the light's hue: the game sets the fog's colour for the time of day itself (its states' fog colours), so the fog
  colour carries the brightness; the haze, the pass's own medium, takes the light as it is.
- **The medium.** The game's fog read as a medium whose extinction is 1 / (end - d) between start and end: its
  transmittance is exactly the game's f (to the power `rt_fog_density`), integrated per segment in closed form, so the
  step count changes only how the light is sampled, never how thick the fog is; `rt_fog_height` H thins it by
  exp(-(y - camera y) / H). A level without fog (most of them) gets, with `rt_fog_haze=1` (the default), a haze: with
  `rt_sky_model=1` aerial perspective (extinction 1/12000 a unit x `rt_fog_haze_density` at the level's ground,
  thinning by e every `rt_fog_haze_height` above it, followed to 40000 units), else a thin linear haze to 4000 units
  whose light alone is added. The haze's conditions (`haze_condition`): thicker round sunrise and sunset - x (1 + 1.5
  exp(-((e - 2) / 8)^2)) with e the sun's elevation in degrees, 2.5 x at its peak, back to 1 by day and deep in the
  night; and, with the material table, over water: a view ray that ends on water (its material's reflection class
  `kReflectWater`, found by one ray from the camera) crosses a wetter layer over the water's surface - 2 x the haze's
  ground extinction, thinning by e every 400 units. The game's own fog keeps its thickness (only the haze varies).
- **The street air** (added in the second round: at the defaults the physical haze - an optical depth of ~0.05 over a
  street - showed neither shafts between buildings nor anything round lamps). `rt_volume_light.h street_air`: the air's
  lowest layer, outdoors (where a sun or moon lights the level), a second medium along every ray in every mode:
  extinction 3e-4 / m at the level's street level (BakedSun::street - the middle of the upward faces' heights, so
  Berlin's plaza, not its sunken streets) x `air_condition` - 8 x round dawn and dusk (a Gaussian round 3 degrees, 10
  wide), 12 x at night, +3 in rain - as the look (below) shapes it, thinning by e every 8 m (4 m at night: a night's mist lies low), falling snow (the game's, or `snow` with
  snowfall) 1.2e-3 / m more over 60 m, the wetter layer over water. Its light the aerial haze's per unit of light, its
  shade the replaced fog's ambient (the sky's in the aerial march, none where only shafts are added); the two media's
  light mixed by their share of each segment's optical depth. Numbers (the check's): across a 20 m street 2 m up 0.035
  at dusk, 0.044 at night; along 300 m 0.56 at dusk, 0.07 by day. Physically the beams show looking toward the light
  (the phase), a lamp's glow looking at it; a lamp's cone seen from the side stays faint unless the air is Thick or wet.
  render_pc publishes the weather each frame (`set_air_weather`).
- **The air's look - a departure from physical, on purpose** (added in the third round; `rt_volume_light.h AirLook`,
  `[Graphics] rt_fog_air`, Options "Atmosphere"). The street air above is the physical one, and at the defaults it showed
  what real air shows: beams only looking toward a low sun, lamps a faint halo, nothing from the side. The user chose
  (2026-11-16) the look of modern games - RDR2's god rays between buildings at a low sun, cones under street lamps,
  columns through cloud gaps, all readable from the side. Physically that needs air so thick (a 10 m beam at 20 levels
  over its shade wants ~8 x a dusk street's extinction) that followed a few hundred metres it whites the far world out
  - so games don't do it physically: their volumetric fog is a volume round the camera whose scattering is set apart
  from its thickness, each light with its own share in it. The look does the same, in steps:
  - `clear`: the physical model, every factor 1 (round two's air exactly).
  - `cinematic` (the default): the street air x 2.5 (x 2.0 since the distinct shafts, below: its layer is deeper) (`density`; no base over a clear day's - `least` 1: the low sun's
    and the night's own conditions carry it, noon stays clear), the night's mist x 1.5; it scatters the sun's or moon's
    light x 3 (`glow`; since the low sun's glare only its even lobe's - `air_sun_phase`: medium_phase + side x glow, the
    forward lobe the physical one: x 3 on the whole phase put 3 x real haze's forward peak in the eye looking into a low
    sun, +0.9 of white at College's 06:20 view, now +0.3; the beams toward the sun 15 - 30% dimmer, from the side as
    before, the tower column's contrast kept - 0.50 / 0.48 - 0.55 across it) and the lamps' x 5 (`lamp_glow`: a lamp's few hundred lumens against the sun's hundred thousand
    - a film lights its practicals for the haze) while it dims only by its extinction - not energy-conserving, by
    design; its phase gets an even lobe of 6 over `medium_phase` (`side`, x 4 pi - an even medium's light is 1): the lit
    air reads from the side, not only toward the light (2.5 was tried first: in the game the beams across Berlin's
    river street and the light past College's tower read only looking toward the sun); its shade's light (the sky's,
    the fog's ambient) x 0.35 (`shade`; 0.2 since the distinct shafts): the air out of the light stays clear and the beams stand out; and it is a
    volume round the eye - whole to 30 m, gone by 60 m (`range`, smoothstep on each segment's middle distance; a ray
    more than 4 x that long marches half its steps in it): the beams near the player, the far world seen through the
    aerial haze alone.
  - `misty`: x 5, at least 4 x a clear day's, glow 1, lamps x 3, side 1.5, its shade whole, 150 m round the eye.
- **Distinct shafts** (added in the fourth round, the user's "option A": god rays with dark gaps). Measured first: the
  same pose with `rt_fog_air` cinematic and clear, the difference the cinematic air's own light
  (`tools/game/shaft_contrast.ts`: veil = its median, beam / gap = its 90th / 10th percentiles, structure = how much of
  it is shafts and gaps rather than a smooth glow). Round three at dusk added a beam of 4 - 9 levels (Boston toward the
  sun 8.8, Berlin's river street 4.1, College 06:20 7 - 14), at night an even veil (Berlin 45 levels median, structure
  0.03). The march's resolution and history were not the cause - the check's thin bar (below) is drawn within a texel
  of the exact integral -, the model was:
  - the volume round the eye was too short: at a 6 - 12 degree sun the air near the eye is in the buildings' shadows
    (a 20 m roof shades 190 m), the beams lie further out. It now reaches `along` (4) x its range looking straight at
    the light, x (1 + 3 cos^2) between (`air_stretch`) - and thins as it lengthens (its mist / the stretch: the same
    optical depth toward the light over a longer way, so the glow toward a low sun is no thicker; without it College's
    high view whited out toward the 12-degree sun);
  - the layer was too shallow: thinning by e every 8 m, it has all but gone at the roofs a low sun's beams come over.
    By day it is `deep` (4) x that - 32 m; a night's mist stays 4 m (the lamps' heads near its top). Its density x 2.0
    instead of 2.5 keeps the eye-level veil (noon from the side under the check's bound);
  - the shade's light x 0.2 (was 0.35), the lamps' glow x 3 (was 5: the night's even veil).
  - The air's shadows: **the air's sun map** (`rt_volume_light.h air_map_half`, `vulkan_rt_fog` step 0): each frame a
    square across the light's way round the eye (its half side the volume's widest reach across the light, the stretch
    included: 1.77 x the range), 512 / 1024 / 1536 texels a side by `rt_fog_quality` (0.15 m at Medium), each texel the
    distance down the light's way to the first thing its ray hits (the cut-outs by their alpha - the one lighting
    path's test; rays from `kAirMapLift` up the light's way, as far as the light's own rays look). The march reads it
    at every step inside the square - a bilinear compare, moved across the light's disc by the distance from what
    shadows it (the penumbra) - instead of a ray each other step, alternating frame to frame; beyond the square the rays
    as before. A ray that leaves the volume marches `steps` in it (t^2) and half as many past it in a geometric series:
    the first kilometres (the clouds' gaps, read from the clouds' map every step) as finely as the far haze.
  - The history is held to its 3 x 3 neighbours' mean +- 1.5 deviations (inside their least and most) - a moved
    camera's history doesn't smear a beam's edge.
  The check (thug2_vulkan_rt_fog_check): a 0.3 m bar across the alley, its shadow through the beam seen edge-on from
  the side at 1280 x 720 at Medium's quarter resolution: the line 6 texels wide in the exact integral, the pass's within
  0.3 - 0.8 texels of it, as deep (0.9 - 1.2). (A 512-texel map lost that bar at one map size - the axis-aligned box's
  edges fell between the texels' rays; Medium is 1024.) In the game (the user's settings, 1280 x 720 with DLSS quality,
  tod held): College 06:20 - the tower's shadow column a dark wedge through the lit air from the high view and round
  the tower from behind it (beam 49 - 57 levels, round three 7 - 12); Boston / Berlin toward the dusk sun dark gaps
  between buildings in the lit air (structure 0.46, was 0.41) but faint (beams ~10 - 18 levels: at 6 degrees the
  streets are in shade); night's veil halved in Berlin's street (median 8.6 levels, was 19.4) and Berlin's start
  (33 vs 45); noon and the far view unchanged (median ~0).
  Only the street air takes the look - the aerial haze, the level's own fog and the falling snow keep theirs. The
  check's numbers (thug2_vulkan_rt_fog_check; linear light, the sun at 1, before the tone map): a 10 m beam across a
  street at dusk seen from the side 0.4 levels clear, 67 cinematic; the side view of the check's street (the device's
  march) 35.6 levels over the shade, the clear air's 0.7; a street lamp's cone seen from the side 0.0 / 10.9 levels
  (its glow looking at it 6.5 / 149); along 300 m at dusk an optical depth of 0.56 clear, 0.21 cinematic (the far
  world clearer than physical). In the game (Boston, Berlin at dusk, College at 06:20 and noon, the tone map and the
  clock's sun strength 0.5 - 0.9 on top): the lit air adds ~5 - 30 levels where the sun reaches it, at noon a median of
  ~2 - 5 over the clear air.
- **God rays** (added in the fifth round, `rt_fog_god_rays`, Options "God rays" Off / On / Strong - 0 / 1 / 2; the
  user's reference: streaks radiating from a low sun, broken by branches into many fine rays). Measured first: looking
  toward College's low sun the air showed a glow and the big shadow columns only - the march's quarter-resolution
  texels and its steps can't hold a branch's shadow a few pixels wide. The light the phase gathers near the light's
  way is what shows those: its forward peak (`rt_volume_light.h god_ray_peak`: 0.85 x (HG(cos, g) - HG(cos 30 deg, g)),
  none past 30 degrees) is taken out of the march (`march_sun_phase`) and drawn at the screen's resolution instead - the
  same light, moved, not made:
  - the march adds up the peak's potential beside its light (the media's light per unit of light x what reaches the
    eye x the clouds' shadow, unshadowed by the world - luma, the peak taking the light's colour), in its info image;
  - step 2a, half the viewport's pixels each way: each texel's share of sky (the depth's far value - cut-outs' gaps are
    sky) and the distance of what stands there; step 2b, three radial passes of 10 taps toward the light's place on the
    screen, over 1/100, 1/10 and the whole of the way (1000 points: sharp rays, the same every frame - no history, so
    nothing smears under DLSS);
  - the composite adds peak(view) x potential x open x the light's colour x the setting x the look's `rays`, where open =
    1 - (1 - the line's sky share) x the share of the air before what stands on it (its mean distance): a thing shadows
    the air between it and the eye along the lines toward the light - air at a distance t is shadowed by what the line
    holds at the fraction 1 - t / d of its way, evenly, so the line's plain mean; the air past it is lit. The air's look
    sets that share (`AirArt::rays_near`): `clear` the volume as it lies (physical - faint: the air in front of a tree
    15 m away is a fifth of the beams' 60 m), `cinematic` and `misty` all of it near (the beams in the near air, as a
    hazer puts them) and cinematic's light x 1.5 (`rays`).
  - The light behind the eye: its peak isn't on the screen (no radial passes; the composite draws it unshadowed - 0
    but at a wide view's edge). Off the screen in front: the lines run on from the screen's edge (clamped).
  The check (thug2_vulkan_rt_fog_check): the march's phase plus the peak is the phase; a comb of five posts against a
  10-degree sun seen from 40 m: across a row above the comb, four rays (one per gap) each 0.12 over its dips. In the
  game (College 06:10, the sun behind the clock tower and through the plaza's trees): a fan of rays round the tower's
  spire and its dark streak; open sky unchanged (no occluder, no rays - the glow as before). Cost: the four half-size
  passes 0.05 - 0.1 ms at 1280 x 720.
- **The light is the relit level's** (third round): `medium_light` lights the air only where the relit mode relights
  the level (`SunRelight::relights` - its offline data built and current, `rt_relit` on, the level not opted out). A
  level without relit data has no sunlit air and no street air: the fog pass draws the level's own fog (its colour
  whole, the lamps' light in it) or, without fog, nothing - one owner of the level's sun, as for the shadows. The
  settings model says so: `rt_fog_air` needs `rt_relit` (greyed with the relit data's reason). The console's `rt sun`
  override still lights it (an experiment; the checks' sun).
- **The light at a sample.** Each segment's light is sampled at one jittered point (interleaved gradient noise + the
  frame's golden-ratio offset): `here = ambient + medium x strength x (sun + lamps)`.
  - ambient: the replaced fog's colour x 0.5 (a level with no light - indoors - its whole colour: the game's fog
    exactly); the aerial haze's sky colour toward the ray x 0.45 (the shade's share - the far world fades into the
    sky); nothing in the shafts mode and the thin haze.
  - medium: the light the air sends the eye per unit of light - the fog's (or the thin haze's grey) colour x 0.5, x 0.5
    over the game's fog (mode 1) and the thin haze, x 0.1 on the sky's pixels there; the aerial haze's 0.55 x
    `kHazeSunlit` (0.55: an unshaded haze lit from the side as bright as the day sky by the horizon).
  - sun: the light's colour x visible (a ray query toward a point on its disc; masks 0x01 + 0x02, with the material
    table the cut-outs by their alpha too - `cutout_rays_glsl`, the one lighting path's test: light through foliage,
    fences and grates) x the clouds (below) x the phase.
  - the phase (`medium_phase`): 0.85 x Henyey-Greenstein(g = `rt_fog_anisotropy`, 0.75 by default - aerosols';
    0.3 before) + 0.15 x HG(-0.3), x 4 pi (1 on average: an unshadowed fog keeps the game's look): strong forward
    scattering, as haze's droplets do - the shafts stand out looking toward the light (24 x at it, 15 x 10 degrees off,
    6 x at 20, 1 x at 45), faint looking away. (0.5 was tried first: in Berlin at dusk the default haze showed no
    shafts at all - the haze is thin, its light only shows where the phase gathers it.)
  - lamps and emitters (round WF - path tracing, stage 7; was the lamps pass's lit set): the light list's local group
    (`vulkan_light_list.h` `restir_local_lights` - every analytic lamp and emitting triangle of the level by power, the
    lamps switched by the lighting state: ReSTIR DI's, GI's and the reference's lights, kept up here in both tiers), by
    resampled importance sampling: `rt_fog_quality`'s 2 / 4 / 8 candidates a step pair from the alias table, the target
    the luminance of the irradiance each sends the point x the phase toward the eye, one chosen and its shadow ray
    traced (`ray_light`), its light x the RIS weight (`local_light_glsl`'s `LocalLight`, x the frame's pre-exposure).
    The volumetric form of RIS light sampling (Lin, Wyman and Yuksel 2021's ReSTIR for participating media, without the
    reuse - the fog's history averages the frames). Unbiased; checked against the processor's march of one lamp
    (thug2_vulkan_rt_lights_check: the open light 1.000 x, the box's shadow 0.988 x, the night street air 0.998 x).
    Before, the lamps' light was not pre-exposed (the night's lamps in the air ~1e-4 of the surfaces'), a lamp the
    lamps pass didn't pick (its 64-light view cap) lit no air, and emitting surfaces lit none.
  Accumulated front to back: in += T (1 - e^-tau) here, T *= e^-tau.
- **The clouds' shadows in the air** (`rt_clouds`): one map, owned by the sky pass (`vulkan_rt_sky_pass`
  `CloudShadowMap`): at each viewport's `opaque_drawn`, while the clouds are drawn and `rt_cloud_shadows` is on
  (third round: the fog no longer asks for it alone - at a low sun the map's long way through the layer darkened
  the air of a street whose ground the clouds don't shade: two lights; now the air takes the clouds' shadows exactly
  where the ground does), a 256 x 256 compute pass over 3.2 km of the cloud base's plane, centred where the camera's way toward the
  light meets it (snapped to whole texels), each texel the light's transmittance through the layer from there (8
  densities, no detail, the light at least 0.08 up; faded out between 0.02 and 0.15). A point reads the texel where
  its way toward the light meets the base: each march step takes it (crepuscular rays - lit columns through gaps,
  shadow shafts under clouds), and the shadows on the ground (`rt_cloud_shadows`) are the same map - with `rt_shadows`
  the sun's visibility in that pass (only the sun's own light drops; see "Cloud shadows"), else the colour multiplied
  toward it by 0.65 x the light's strength. Before, the
  ground's shadows marched four densities per pixel toward the drawn sky's sun.
- **The passes.** A quarter of the resolution each way at `rt_fog_quality` 0 and 1, half at 2 (each texel marches one
  pixel of its 4 x 4 or 2 x 2 block, a different one each frame in ordered-dither order); the steps spaced b t^2 (dense
  near the eye) and in pairs - one point of each pair, the first or the second by turns frame to frame, traces the
  light's shadow ray and the picked lamp's for both; lamps a view ray never comes within reach of left out (a bit mask
  per texel; the pick one random number streamed over the lamps); the march stops once T is under 1e-4. Then a
  temporal pass (the texel's point reprojected through the viewport's previous camera, bilinear over the history
  texels whose distance agrees, clamped to this frame's 3 x 3 neighbourhood, up to 12 frames; a new history seeded with
  12 marches), then a full-screen blend colour x T + light, upsampled with weights that fall off with the distance
  difference (depth-aware). Per viewport histories (split screen), up to 4. The march's variants (a multisampled depth,
  the aerial haze, the cut-outs and water - the material table and this frame's geometry for the same scene -, the
  clouds' map) are made on first use.
- **Modes.** `rt_fog_mode=0` (the default) replaces the level's fog: the Vulkan Gpu's fixed-function emulation leaves
  the device's fog out of every draw while the pass draws (`rt_fog_replaces_level_fog`), and the pass draws it - the
  same colour and distances. Its limit: transparent passes and particles don't write depth, so they get the fog of the
  surface behind them. `rt_fog_mode=1` keeps the game's fog as drawn and adds only the light's part: shafts over the
  original, nothing taken away. The sky (the depth's far value) is never dimmed by the game's fog - it gets the light x
  0.1; the aerial haze hazes it as a surface at the far plane.
- **Which fog.** THUG2's levels draw fog only where a script turns it on, or with `[Graphics] DistanceClipping=1` and
  `Fog=1` - then every mesh fogs over the last quarter before the far plane (pc_mesh_render.cpp), which `ViewDrawn::fog`
  reports. A level without fog and without a light or lamps (rt_fog_haze=0, or indoors by day) gets no pass.
- **Settings.** `rt_fog_air` (cinematic; Options "Atmosphere" Clear / Cinematic / Misty - the ini also takes 0 / 1 / 2;
  round two's numbers 0 - 4 were never released): the street air's look, above. `rt_fog_strength` (1, the ini's) scales the light the air scatters - sun, moon, lamps - not the
  fog's ambient. `rt_fog_quality` (1; Options "Light shaft quality"): 16 / 32 / 48 steps, 8 / 16 / 32 lamps, a quarter /
  a quarter / half the resolution; `rt_fog_steps` above 0 overrides the steps. The High preset turns `rt_fog` on (its shafts were the retired `rt_shafts`).
- **Cost** (device timestamps around the passes, logged every 240 as "rt_fog: the passes take"): the check's scene at
  1920 x 1080 before the volumetric light - RTX 5070 Ti 0.48 / 0.99 / 2.03 ms at 16 / 32 / 64 steps, the AMD Radeon
  integrated GPU 14 / 28 / 52 ms (the ray queries dominate: linear in the steps); a lamp adds one ray a step where
  lamps light the air, the cut-outs' test a little per ray, the clouds' map one texture read a step and its 256 x 256
  pass (a few hundredths of a ms). The first round in Berlin at 1440p: 5 - 10 ms. With the quarter resolution, the
  rays every other step and the lamps culled (the street air on): the check's street at 2560 x 1440 0.24 / 0.39 / 1.69
  ms at Low / Medium / High (--perf holds Medium under 1 ms); in the game at 2560 x 1440, Medium, clouds and lamps on:
  Berlin 0.6 - 1.3 ms (one window 1.8), Boston 0.55 - 1.1 ms.
- **Checks.** `thug2_vulkan_rt_fog_check` (82): a wall with a window, the sun through it into an even fog - every
  low-resolution texel's distance, transmittance against exp(-integral) (largest error 5e-4) and light against the
  processor's march of the same pixel with the same jitter (the same `medium_phase`); the shaft lighter by what the
  exact integral says; still frames converge on the exact integral; the composite; a moved camera without ghosts;
  shafts mode; the sky never dimmed; the haze; no pass without fog, a scene or the setting. Added by the volumetric
  light: the clock lighting the level (a baked sun, the level relit, `DayLight` on) - the scene's sun is the clock's,
  the fog lit by it, and after still frames the history matches its own direction's integral (0.0008) while the other
  direction's is 40 x off (the shaft moved with `DayLight`); at night the moon's shafts in the haze carry its colour's
  luma (0.190 of the sun's against 0.189) and are bluer (blue over red 1.69 against 1.00).
  `thug2_vulkan_rt_lights_check` "the lamp in the air": a lamp low beside the box, the haze lit by `rt_lights`' set
  through the real material table and geometry (the cut-outs' variant) - after still frames 5,214 texels in the lamp's
  open light at 0.998 x the processor's march of its falloff and phase, 1,410 across the box's shadow at 0.988 x the
  shadowed march and 0.70 of the unshadowed (the lamp's cone cut by the box). `thug2_vulkan_rt_sky_check`: the clouds'
  shadow map made at the opaque world's end toward the scene's light, every 8th texel against the processor's
  transmittance (`rt_clouds.h cloud_density`, worst 0.016), gaps (38% at least 0.8 clear) and shade (61% at most 0.4).
  `thug2_vulkan_rt_sun_check`: the aerial haze's light between the shade's share of the sky and that share + the sunlit
  haze through the phase. `tools/game/rt_shadows_check.ts --effect fog` checks it in the game.
- **Not yet.** The lamps' light uses their colour, not their texture's tint (the lamps' pass's); the water layer only
  where the view ray ends on water (a pier's pixels beside the sea don't cross it); clouds' shadows only with
  `rt_clouds` (the painted sky's cards cast none); the clouds' map ignores the atmosphere's curvature past 1.6 km.

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

- **Not yet**: the lamps' light in the bounce (`rt_gi`) and in the reflections' hits; the lights already baked into the
  vertex colours aren't taken out (the traced light adds to them - `rt_lights_strength` balances it); no light grid
  (a view with more than `rt_lights_max` lamps drops the least important).

### The sun, its glare and the sky (`rt_sun`, `rt_bloom`, `rt_sky_model`)

(The disc, the aureole and the glare were rewritten by the physical air - "The physical air, sun and camera": the real
sun, the sky's own aureole, the eye's glare.)

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
- (Changed by the volumetric light: the rays below - `rt_shafts`, `rt_shaft_strength` - were removed; the light in the
  air is the fog pass's, "Volumetric fog and light shafts". What follows describes them as they were.)
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
  carries each one's texture slot, sampler and mode (100 bytes a row then; 108 since the surface reflections with the surface maps' two fields before them), and the reflection pass multiplies the weight by
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
- **Surfaces glossy by their surface maps** (`rt_reflection_surfaces`, 1; needs `rt_pbr`'s maps; added 2026-11-15):
  before, only the environment-mapped materials reflected - polished floors and metal the game left without a shine
  never did. Now the material table (`vulkan_rt_materials.cpp` surface_reflection) gives a material with no shine of its
  own, not seen through, whose texture has a map a reflectivity from the map's surface kind (`vulkan_surface.cpp`
  surface_map_gloss: the detail library's kind where there is one, else the map's class - metal, glazed tiles, glass and
  water 1, stone 0.2, wood 0.25, paint 0.2 - since the night's kerb, which mirrored a shoe: before 0.5, 0.35 and 0.3 -;
  asphalt, concrete, brick, plaster, ground, plants, cloth and the
  characters' maps 0), class `rt::kReflectSurface`, the map's F0, and the kind's roughness and its weight (the library's;
  the record grew to 108 bytes). The pass reads the map's roughness at the pixel (mixed toward the kind's as the per-pixel
  lighting does): weight = gloss x `rt_reflection_surfaces` x min(1, 2 x strength) x (F0 + (max(1 - r, F0) - F0)(1 - c)^5)
  x (1 - smoothstep(0.525, 0.7, r)) - a rough surface never turns into a mirror at grazing angles, the roughest (r above
  0.7: the maps' stone, plaster, paint) not at all - and its rays spread over a cone of r^2 radians (at most 0.5) as
  rough glass's. Each kind has a roughness it never goes under (`least_roughness`, the record now 112 bytes): metal
  0.05, glazed tiles 0.12, glass and water 0, wood 0.35, paint 0.4, stone 0.5 - a kerb or a painted wall is a faint,
  blurred sheen at most, never a mirror, whatever its map says - and its F0 is held to 0.05 but for metal (only metal
  reflects more than a clear coat face-on).
- **Objects' clear coat and smooth normals** (2026-11-15): a shiny object (environment-mapped, not water, glass or a
  map's surface) - a car's paint - reflected as the shiny surfaces do, its whole shine face-on: a dark hood was a sharp,
  bright mirror. With Fresnel on it is a clear coat now: its shine x min(1, 2 x strength) x Schlick's from F0 0.05, its
  rays over a 0.06 radian cone. And where the reflecting mesh has normals of its own (the objects) the mirror direction
  and the Fresnel follow them, interpolated (`hit_normal`, by the instance's inverse transpose) - the low-poly bodies'
  triangles no longer show in the reflection; the triangle's normal still keeps the ray off the surface (and replaces
  a smooth normal that would send the ray into it). Such materials count as reflective instances. The maps make few surfaces glossy (THUG2's levels: the
  metal kinds - rails, the Airport's moving walkways - and glazed tiles; stone and paint stay matte), so most views
  change little. Off (`rt_reflections=0`) and without maps nothing changes; `rt_reflection_surfaces=0` gave the earlier
  look.
- **One light model for the weight** (2026-11-17; supersedes the Fresnel, rough glass, surface and clear coat weights
  above - `rt_reflection_strength`, `rt_reflection_fresnel`, `rt_glass_roughness` and `rt_reflection_surfaces` are
  retired): a pixel's weight is its material's E - `rt_brdf.h` `brdf_specular_albedo` for its F0 and roughness at the
  cosine it is seen at (the split-sum directional albedo, multiple scattering compensated) - the share of the
  environment the light model gives the specular, the diffuse keeping 1 - E: reflection + diffuse never above 1,
  toward 1 at grazing angles, a white metal 1 everywhere. A metal's E is coloured (the reflection carries its F0's
  colour; the weight is the largest channel). The lobe's rays spread over a cone of GGX's alpha radians (at most 0.5;
  narrower than three pixels' angle - what the apply's 3 x 3 blur resolves - one mirror ray). Two ways in, so the
  environment's specular is counted once:
  - **added**: every surface the light model shades through its surface map (`rt::kReflectSurface` - every mapped
    surface but water and glass, with or without the game's shine; the raster's `SurfaceDraw::traced`, which blended
    draws no longer set: the pass sees only what the depth holds) left its environment's specular out where its texel
    is smoother than `kReflectionRoughest` (`surface_finish` keeps smoothstep(0.525, 0.7, r) of its estimate): the pass
    adds E x the share left out - F0 the material table's for the map's kind (a metal's its texel's colour), roughness
    the map's at the hit. Rays that miss everything without a sky drawn take the surface's own estimate (its light store
    word, probe or bake - `own_environment`), never a hole;
  - **in place**: water, glass and the game's shine on a surface without a map hold the game's sphere-mapped sky in
    their colour - the surface's estimate, which can't be taken out of the colour: colour x (1 - E) + reflection x E
    (the base layer's alpha carries what of the drawn pixel is replaced), the materials the table's water (n 1.333),
    glass (n 1.52, smooth) and a new `lacquer` kind (n 1.5, roughness 0.25 unmeasured - the old coat's 0.06 rad lobe)
    for car paint and the game's sheens. The game's shine (`reflectivity`) only says that a surface reflects; the
    mask passes still say where.
  Checks: `thug2_vulkan_rt_reflections_check` - the perfect mirror (a white metal map: E 1, drawn black), water, glass,
  lacquer, the masks and covers, a rough map, the sky's misses and a mapped tile / stone (added) against the processor's
  E; the light model's energy, Fresnel rise and white furnace on the processor.
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
  `ViewOrder::kSky`). `draw_viewport` tells the listeners, who draw the sky over every pixel still at the far plane,
  then draws of the sky scene only its **scenery** (below), both passes in the game's order over that sky. (Before the
  sky-decals round: the opaque backdrop drawn and painted over, the transparent layers kept only without `rt_clouds` -
  `keep_layers` - so with clouds can's mountains and tree line were dropped, and without them every painted cloud card
  stayed, daylight-coloured at night.)
- **The sky scene's parts** (`engine/gfx/sky_parts.h` the rule, `render_pc/sky_scenery.h` its use;
  `tools/game/sky_survey.ts` its twin over the files, `thug2_sky_parts_check` its check). Read once per sky scene from
  the copies in memory - each mesh's vertices seen from the origin (its original triangles when smoothed) and its first
  texture's alpha (`texel_decode.h`) - nothing listed per level: a pass that adds, subtracts, multiplies or brightens is
  a **glow** (light of the air); a mesh whose texture has no holes (under 2% of texels under half alpha) is the
  **painted** sky (domes, bands, painted ground rings); a **solid silhouette standing on the ground** is **scenery** -
  its edge a cut, not a medium (at most 5% of its covered texels only faintly covered: a cloud's or a haze's edge thins
  out over many texels, a mountain's is a texel of antialiasing), at least 60% of its opaque texels in columns standing
  on one ground line at the card's foot (the columns' lowest opaque texels within 10% of the texture's height of their
  median, that within 10% of the bottom edge: mountains, skylines, a tree line - not blobs each with sky under them, not
  shapes hanging from the top), and grounded (its opaque part reaching the horizon or below, or the top of grounded
  scenery in front of it at the same azimuth: a range behind a skyline); anything else with a silhouette is a **cloud**
  (cloud cards, a painted moon, haze). Glows, painted sky and clouds are the renderer's sky's (the atmosphere, the
  volumetric clouds, the sun, moon and stars) and are not drawn; scenery is drawn over it. The survey of the 69 sky
  scenes (THUG2's 22 and the overlay's 47 ported ones): 433 painted, 4 glows (LA's sun glow and haze, VC's), 40 clouds
  (AP, DJ, NO, PH, SC, FL, RU's cloud cards - soft-edged banks standing on the horizon, faint 18%; HI's, THP8's; LA's
  haze strips; skateheaven's moon), 11 scenery (can's two mountain ranges; sub's skyline and the three ranges behind it;
  VC's skyline; skateheaven's cloud floor and its three copies - a floor below the horizon, lit as ground). Known
  misses: can's tree line card is painted with soft foliage (faint 17%) and reads as a cloud (dropped; it lies 16 - 4
  degrees under the horizon, mostly behind the level's own trees); a painted dome band with a skyline painted into it
  (no alpha) is painted sky.
- **The scenery's light**: as the world's (`day_clock.cpp`): the sky scene's own colour (its TOD sky system's at the
  level's own state) times the light now over the level's own, channel by channel - the level colour's ratio (the relit
  mode's level light, `rt_light_field.h` whole, when the level is relit). At `tod=level` nothing changes. Not yet: the
  aerial haze over it (the fog pass leaves the sky's pixels to the atmosphere; the sky scene's distances aren't
  physical - a backdrop drawn from the origin).
- **The level's own sky** (`SkyCapture`, `rt_sky_match.h`; since the low sun's sky only its clouds' share is used - the
  coverage). Once per sky scene `render_pc` draws all its meshes from the
  origin into six 90-degree faces (a 384 x 256 texture, cleared to the background colour) and hands it over; the pass
  reads it back once and analyses it: per band of elevation - just below the horizon (-8 to -0.5 degrees: a painted sea,
  the far ground - logged, never matched), the horizon (0.5 - 10), half way (15 - 45), overhead (55 - 90) - the clear sky's colour (texels weighted
  by 1 - cloudiness, cloudiness from saturation: 0.32 and more clear, 0.10 and less cloud) and everything's mean; the
  clouds' share between 3 and 35 degrees up (the sky a player sees) and their colour. Logged: "rt_sky: the level's sky".
- **The atmosphere** (`rt_sky.h`, `vulkan_rt_sky`): the existing model - Rayleigh, Mie, the same radii, scale heights and
  phases, so the sun's colour, the fog's haze and the drawn sky are one model - and (added by the low sun's sky) ozone:
  absorption only, Hillaire's 0.650 / 1.881 / 0.085 per 10^6 m in a tent 10 - 40 km up. Overhead it takes 3% of the
  green; along a low sun's path it takes the orange and green out of the light reaching the far sky, so with the sun
  3 degrees down the sky away from it shows the Earth's shadow (1 - 3 degrees up, blue over red 1.4, dark) under the
  Belt of Venus (9 - 15 degrees, blue over red 0.76, pink, 1.5 x brighter) and the twilight zenith stays blue (without
  ozone that band was a greenish yellow). The light scattered more than once
  added after Hillaire (EGSR 2020): Psi(altitude, sun) = L2 / (1 - f) from 8 x 8 directions round each point (the second
  order with an even phase and the ground's light, albedo 0.3), added as scattering x Psi along each view ray. Three
  tables, 16-bit floats: the transmittance (256 x 64, Bruneton's mapping, made once), Psi (32 x 32, once), and the sky
  from the viewer (192 x 108: across sqrt(azimuth / pi) from the sun, down the elevation's signed square root - fine near
  the sun and the horizon, below it too), made again when the sun moves. Exposure as before (the zenith
  0.3 with the sun 45 degrees up): the multiple scattering lifts the sky 1.15 x (sun 10 degrees up) to 1.5 x (45).
- **No match to the painted sky** (since 2026-11-16, the user: "If we implement TOD, we need TOD. Maintaining the old
  style is not part of that."). Before, `rt_sky_match` (0.8 by default) multiplied the model per channel toward the
  dome's colours band by band (horizon, half way, overhead), the gains worked out once for the level's own sun and kept
  at every hour, and the clouds took the dome's cloud hue and brightness; at College 06:10 that held a midday blue
  away from a 10-degree sun (the gains there: horizon 0.27 0.25 0.33, half way 0.40 0.39 0.48, overhead 0.65 0.61 0.77 -
  their blue over red 1.2 x, applied to a sky the model had already turned pale) and white clouds lit by the noon
  model's horizon colour. Now the sky-view table is the model's alone, at every time of day, driven by the clock's sun
  (`rt::DayLight`); `rt_sky_match` is retired (a setting that does nothing). From the capture only the amount of cloud
  is taken (the clouds' coverage, auto). The original look is `rt_sky=0` (the painted dome) - and `tod=level` keeps the
  level's own lighting. Below the horizon the dome is still not asked (its painted ground came back as a band): the
  sky-view table shows the horizon's haze there (the sky 0.6 degrees up in the same azimuth) giving way to the model's
  own far ground by up to 60% over 20 degrees down - the far world's aerial haze, which reads this table, and the sky
  meet without a band.
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
  (six growing steps; the sun's light at the layer is the atmosphere's transmittance half way up it - red over blue
  2.2 with the sun 10 degrees up, 8.9 at 3), Wrenninge's three octaves (Beer's law, multiple scattering), two
  Henyey-Greenstein lobes (0.8 forward - the silver lining toward the sun - and -0.3), a powder term, the sky's light;
  Hillaire's energy-conserving step. The sky's light (`rt_clouds.h cloud_sky_light`, since the low sun's sky) is read
  from the drawn sky-view table by the march itself - their tops the mean light of the upper hemisphere, their bases of
  the lower one (the far ground lit by the sun and the sky, through the haze), 6 elevations x 4 azimuths each, cos e
  weighted, every tap a texel's centre (the processor's twin reads the same texels of the table read back): the sky as
  it is now, the night sky's light in it at night. Before, it was the model's horizon and zenith colours at the level's
  own time scaled by the hour's brightness (and matched to the dome): noon blue on the dawn clouds' shade, and with the
  horizon band 2 - 3 x the hemisphere's mean the shade outshone the sun's side - white clouds at dawn. The clouds now
  take nothing of the dome's colour (its cloud hue and brightness were a gain on them); at 06:10 their sun side is
  warm, at dusk peach and gold. Coverage `rt_cloud_coverage`, auto the weather's (round S2: the air's typical cover -
  read from the level's painted dome before, 0.2 + 0.7 x its share). The layer is a
  shell round the planet
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
- **Cloud shadows** (`rt_cloud_shadows=1`, off by default; at `opaque_drawn`): the clouds' shadow map (changed by the
  volumetric light - one map toward the scene's light, the sun or the moon, read by the ground and by the fog's march:
  "Volumetric fog and light shafts"). With the ray-traced shadows on (`vulkan_rt_shadows`' CLOUDS variant, bindings 4
  and 5) the map is the sun's visibility: where the sun's rays reach a pixel the clouds take their share of the sun's
  light - a relit pixel's `seen` times the transmittance (its light rebuilt from its parts: the sky's, the bounce's and
  the lamps' stay), any other pixel's occlusion raised by it - and a pixel already in the sun's shade (or under a roof)
  loses nothing more. Without them (no ray tracing, cel's own shadows) the fallback: the colour x mix(1, transmittance,
  0.65 x the light's strength) over all of it. (2026-11-16, the freeze audit: the multiply over all the light took the
  sky's light too - a Boston street in a building's morning shade went near black under a blue sky.)
- **The reflections' misses** (`rt_sky_for_rays`; `vulkan_rt_reflections`' SKY variant, bindings 9 - 12): with
  `rt_sky=1` a reflection ray that leaves the world sees the sky drawn - the sky-view table toward it and, where its
  direction falls on the viewport, this frame's clouds (their history, read as the composite reads it, faded out over
  the viewport's last 8%; beyond it the sky alone) - as the game's colour; with `rt_sky=0` the misses keep the game's
  environment map as before.
- **Not done**: (the time of day: "Time of day" below); clouds in reflections only where the reflected
  direction is on screen.
- **Checked**: `thug2_vulkan_rt_sky_check` (92 checks since the low sun's sky; the RTX 5070 Ti and the AMD Radeon integrated GPU, validation and
  synchronization validation 0, no ray tracing asked for): the transmittance table against 2048 segments, Psi against
  the processor's integral, the sky-view table against `sky_radiance_ms` (below the horizon: the same haze-to-ground
  blend on the processor) at 10 and 45 degrees, the multiple scattering brightening the sky, the zenith bluer than the
  horizon; a painted capture analysed on the device exactly as on the processor, the clouds' coverage from it and the
  sky-view table with it the model's to the bit (no colour of the dome in it); below the horizon the horizon's haze,
  not the capture's dark sea (across the horizon luma within 2%, chromaticity within 0.01); a khaki ring painted under
  the horizon seen by the ground band and changing nothing; (the low sun's sky) the sun's light reddened (red over blue
  3.2 at 10 degrees up, 22 at 3, 2.3 at the clouds at 10), the anti-solar sky 5 - 40 degrees up paler with the sun 10
  degrees up than at noon (saturation 0.34 against 0.46), with it 3 degrees down the Earth's shadow under the Belt of
  Venus (above), the clouds seen toward a 10-degree sun warm (red over blue 1.65; 0.94 with it 45 up); the composite in an 8-bit and a
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
- **Dark levels**: (retired with the match) the sun on the clouds was x a smoothstep of the dome's brightness, so NO's
  painted night kept its clouds dim. Now night is the clock's: with `tod` running NO is lit at midnight (the moon's
  sky, the clouds' sun gone); with `tod=level` the sky is drawn for the game's sun as the level sets it.

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

**Smoothing (`rt_tess_smooth`, `rt_tess_smooth_strength`)** - with `rt_tessellation=1`, off by default; `rt_tess_smooth=0`
is the crease angle's rule above to the bit (`thug2_tess_check --level` hashes every result of five levels: the same
hashes as the build before it). The crease angle alone can't tell a six-sided rail (60 degrees between sides) from a
box's corner, nor a ledge's 45-degree chamfer from a curve. With smoothing an edge's two faces are joined by these rules
in order (`smooth_tess.cpp` `joins`):

1. over 85 degrees: hard (a box's corners, stairs, a ledge's top - whatever the normals say);
2. a **prism's edge**: round. Rails, pipes and poles: both faces run along the edge (`|n . e| < 0.03`), the edge is at
   least twice as long as they are wide, they are alike in width (within 25%) and at most 16 units wide, the angle is
   over the crease angle and at most 75 degrees (5 - 7 sides), and across each face the next edge parallel to it is such
   an edge at an angle within 10 degrees - a ring of equal sides (a kerb's or a roof's profile isn't one: its next edge
   meets the ground at another angle). Round whatever its normals say: the levels' rails are mostly flat-shaded;
3. the **mesh's normals** where it has them: both ends of the edge sharing their vertices or their normals (under 8
   degrees apart) - smooth (up to 85 degrees: a car's roof, a prop modelled with smooth normals); split normals - hard
   from 20 degrees (bevels, chamfers, a kerb's edges);
4. otherwise the crease angle (50 degrees), as without smoothing.

The level: the error's (as above, up to `rt_tess_level`), or more - enough pieces that none of an edge's turns by more
than 15 degrees (up to 4; a curve the cap flattens counted by its capped bulge), so outlines read round whatever their
size on screen: a six-sided rail is cut into 4 pieces a side. The strength scales Phong's shape factor (0.5 - 1 of the
way from the facets to the arc). Everything else - crack-free edges, the cap, the original vertices first, displacement
off on thin meshes - is the same code.

- **Checked** (`thug2_tess_check`, 21 more checks): a flat-shaded six-sided rail of radius 3 (split normals, every edge
  welded by position) unchanged without smoothing, round with it - 24 prism edges, level 4, the new points within 0.027
  of the circle and the drawn surface (sampled across every triangle) within 0.046 inside it, the facets' 0.40; no
  cracks; the original vertices first; the same bytes smoothed twice; strength 0.5 moves it half as far, 0 not at all;
  a six-sided column 40 wide and a square beam stay faceted; a box with averaged normals keeps its corners; a ledge's
  flat-shaded 45-degree chamfer rounded without smoothing and kept crisp with it (2 edges kept by their normals); a
  six-sided cylinder of radius 100 with shared normals capped at 1 (11.9 uncapped) without cracks; the seamed cylinder
  crack-free.
- **The levels** (`thug2_tess_check --level <folder>...`, offline: the game's .scn and .col read, every mesh the game
  would smooth smoothed): BO, AU, NO, BA and sch - 58 - 305 prism edges a level rounded, 307 - 2,423 edges joined by
  their shared normals, 28 - 482 kept hard by split ones; indices (the smoothed meshes') off -> on: BO 294,202 ->
  393,990, AU 499,727 -> 626,063, NO 267,197 -> 297,156, BA 347,282 -> 413,401, sch 650,697 -> 708,694 (+9 - 34%); load
  time 27 -> 30, 37 -> 41, 25 -> 26, 27 -> 29, 44 -> 45 ms.
- **The wheels and the collision**: the collision is the game's own (`.col`, never touched). For every new point whose
  place on the original surface lies on the collision (within 0.1) where the collision is a slope - the vert faces
  (quarter pipes' faces carry the vert flag) and the other slopes (banks, bowls, transitions; normal's up share 0.05 -
  0.97) - its gap to the collision. Quarter pipes with smoothing: median 0.22 - 0.53, 95% 1.38 - 1.81, at most the cap
  (2) - the same as without it (0.22 - 0.53, 1.30 - 1.82); nearly all behind the collision (the arc lies behind its
  chords: the wheels float over the drawn curve between its original vertices, never sink - the most in front 0.8 -
  1.97 on convex parts). With `rt_tess_max_deviation=1`: quarter pipes' median 0.11 - 0.32, 95% 0.80 - 0.99, at most 1.
- **Ray tracing**: unchanged - the BLAS is built from the drawn (smoothed) buffers, so shadows, occlusion and
  reflections fall on the rounded shapes; the baked sun's samples keep reading the original strips.
- **Not done**: the optional bevel highlight on hard edges (a lighting change, not geometry); a per-distance level.

### Building relief (`building_relief`)

THUG2's buildings are boxes with pictures of buildings on them: the windows, their frames and sills, the floor bands and
cornices are painted on flat walls, and in a ray-traced frame that reads at once - the sun lights a window like the
brick round it, no sill throws a shadow, no recess darkens. `[Graphics] building_relief=1` (the default on Vulkan when
the facade files are there; `0`, any other renderer or no files: exactly what was drawn before) builds that structure
as real geometry. Stage 1: the building fronts on real box buildings. The flat skyline cards outside the levels -
pictures of whole streets - are stage 2 (counted by the survey below, not relieved).

- **Where the structure comes from: the player's own textures, offline** (`tools/pbr_maps/building_relief.ts make
  --game <folder>`, beside the surface maps; `facades.ts` the detector, `relief_file.ts` / `formats/relief_format.h`
  the file). Not brightness as depth: a texture is a facade when it shows a regular set of rectangles unlike its wall -
  windows - found as structure: the wall's colour (the median), each texel's distance from it, the texture's repeats
  across and down (a shift by a period correlates, half of it doesn't - a smooth texture is the other way round), a
  grid's cells averaged into one; the windows the connected pieces unlike the wall that fill their box, alike in size,
  snapped into columns and rows; round each window a frame (a brighter ring), a sill (brighter rows under it - also when
  painted against the glass), and floor bands (even rows without windows, unlike the wall). Heights by kind, in texture
  widths (so the game scales them by how wide the texture is drawn): a window 0.35 of its narrower side in, a frame 0.06
  out, a sill 0.15 out, a band half its own height out. What isn't clearly one - brick, stone, bookshelves, doors,
  lit-window cards, specks - is skipped: the windows must be at least 2, at least 5% of the texture, rectangles after
  snapping, in a grid (a whole building's front), its wall even, ribbons (glass all across) no lighter than the wall;
  and in the world (the level's own texture coordinates on its walls) a window must be 16 - 480 units wide and 20 - 480
  tall. Which textures: a material's first pass, not see-through, drawn mostly (60%) on walls (within 15 degrees of
  upright), not mostly on skyline cards. The structure is found in a texture set's texels where one serves the
  dictionary (the ini's set first, then any installed: SeedVR2's or PBRify's 4x texels give the windows' edges better)
  and keyed to the original's checksum and content hash, as the surface maps are - the game finds it for the original
  and any set alike. A dictionary whose sources (its bytes, the set's, the detector's version) are unchanged since its
  file was made is skipped; `--pictures <folder>` writes each wall texture beside its height map (the structure drawn:
  grey the wall, dark in, light out; blue: no structure, left flat).
- **The geometry: once, at load, on the processor** (`engine/gfx/facade_relief`, `render_pc/building_relief`) - the
  same place as the smoothed curves (after them: walls are flat, smoothing leaves them) and for the same reasons: every
  renderer draws it, the BLAS is built from the drawn buffers (`rt_record`), so **shadows, occlusion and reflections
  fall on the relief** - a sill shadows the wall under it at noon, the window recesses darken with ray-traced AO, a low
  sun rakes the bands. A scene mesh drawn with a facade texture: its walls (triangles within 15 degrees of upright, with
  texture coordinates) joined into planes (sharing an edge - welded by position -, wound alike, facing the same way, the
  same texture coordinates along it); each wall triangle cut along the structure's rectangle edges (lines of constant u
  and v in the texture's cells - a convex polygon clipped line by line) into pieces wholly inside or outside every
  rectangle; a piece moved along its plane's normal by its height (the last rectangle holding it x the units the plane
  draws a texture width across x `building_relief_strength`, capped at 12 units in, 4 out; under 0.25 left flat).
  Between pieces of different heights a side is built from the higher one down to the lower (facing the lower side),
  across cut lines and across a plane's shared edges alike; along a plane's outline (an open edge, a crease - the
  building's corner, the roof line) a piece off the surface is closed by a side back to the original edge. So the
  surface stays closed and its outline is where the original was: no cracks, nothing to see through; the silhouette at
  a building's corner gains the windows' notches and the sills' ends.
- **Crack-free by construction**: a corner is the corner; a cut line's point on a triangle's edge is made from the
  edge's two ends in one order (the lower welded position first) and the line - both triangles on the edge make it to
  the bit; a point inside a triangle from the two lines crossing there. A plane with any piece off its surface is built
  whole (every triangle of it cut the same way: no T-junctions along its shared edges); one with a triangle too finely
  cut (more than 96 lines along u or v) stays as it is. The original vertices stay first and unchanged (other levels of
  detail, the editor's moves, the colour writes); the new ones follow; a mesh too big for 16 bits is tried with the
  windows alone, then left as it was. The baked sun's samples keep the file's strip (`remember_original`), as with the
  smoothed curves.
- **Visual only**: the collision is the game's own `.col` (never touched); the simulation reads none of it. Drawing
  only reads it.
- **Level of detail**: what a level draws far away is its meshes' own coarser levels of detail, which keep the original
  triangles; the cost is bounded by the structure (a window is ~10 pieces and 4 sides) and the caps above, not by
  distance. A per-distance level (a second strip per mesh, chosen by distance) is not done.
- **Checked**: `thug2_relief_check` (processor only): a wall of two triangles with a window, its sill and its frame in
  each of two cells - the window's middle 12 units in (the cap), the sill 4 out (the cap), the frame 1.6 out, the wall
  where it was; closed (every edge shared by two triangles wound opposite ways, but the original outline's, used once
  and on it); the original vertices first and unchanged; the same bytes twice; half the strength half the height; a
  wall of two quads with a texture seam (vertices repeated) joined, closed across the seam; a corner of two walls, each
  closed back to the corner; a floor, no texture coordinates, a strength of 0, no rectangles, too many cuts - unchanged;
  a big wall of 40 x 30 cells within 16 bits (the windows alone, or as it was); the file read back, damaged files
  refused, the same bytes as the tool's writer. `thug2_vulkan_rt_shadow_check`'s case "building relief: a sill's
  shadow on its wall": a box whose front is facade_relief's geometry for one sill, drawn and traced through the Vulkan
  Gpu, the frame compared pixel by pixel with the processor's answer for the same wall with the sill as a box of its
  own - the sill's shadow on the wall under it there (at least 40 clear-cut pixels of it; measured 443 of 41,110 compared,
  none wrong). The reference's shadow rays start as far off the surface as the pass's (about a unit): with a sill 4
  out, that push moves the band's lower edge up by more than a pixel or two - a property of the shadow pass's bias
  that relief makes visible (contact shadows a unit short), not of the geometry. The tool:
  `tools/pbr_maps/facades.test.ts` (synthetic facades: a 4 x 4 grid's cell found, windows in, sills out, the wall
  flat; a whole building's front; plain grain, brick courses, one door, a busy picture left flat; the repeats of a
  period, not of a smooth texture), `relief_file.test.ts`.
- **The survey** (`building_relief.ts survey --game <folder> [--levels ...]`): per level, the facade textures, their
  wall triangles and area at the playable area's edge (sectors with collision) and in the background (without), with
  samples; and the skyline cards (flat sectors without collision outside the collision's bounds grown by 500 units).
- **The relit light** (`rt_relit`, the time of day's): the relief's vertices are lit as the flat wall at their place -
  `rt_relit.h` RelitMesh::sources: each new vertex's sources (the wall triangle's corners and weights, recorded by
  facade_relief) - their traced sun, sky and bounce blended, never traced from the relief's own corners. Traced there,
  a vertex at the foot of a sill's side or in a recess corner saw half its rays blocked and spread that over its whole
  piece: BE's window grid came out dark at noon (the first run). The relief's own shadows and occlusion are the
  per-pixel passes' (shadows, AO); the original vertices the sources name are traced for them though no triangle uses
  them any more. A relieved mesh's cache entry counts its sources (its content hash).
- **Measured** (x64, RTX 5070 Ti, 2560 x 1440, the main install's `[Graphics]`, the files made from its SeedVR2 / PBRify
  sets): the survey over every level in the game folder - 417 facade textures (the `_net` copies counted too), 19,969
  wall triangles drawn with them; 913 skyline cards (stage 2). BO: 7 textures, 10 of 10 facade meshes relieved - 168
  wall triangles cut into 8,175 pieces and 6,711 sides, +40,029 vertices, their indices 617 -> 177,414, 20 - 23 ms at
  load; BE: 4 textures, 6 of 7 meshes (one with the windows alone) - 50 triangles, 8,293 pieces, 4,739 sides, +29,882
  vertices, 361 -> 152,478 indices, 23 ms. The device's time a frame with the
  camera held on BO's and BE's first facade (frame profile, 1800 frames, two runs each): off 23.8 / 4.1 ms (BO), 14.0 /
  8.0 (BE); on 7.5 / 6.9, 7.3 / 5.9 - the run-to-run spread (4 - 24 ms for the same settings) is larger than any cost
  the relief adds; ~60k more triangles a level drawn and in the BLAS.
- **Not done**: the skyline cards (stage 2); a per-distance level of detail; the detector's misses and false positives
  (a sign's letter tiles - BE's BERLIN - read as windows; low-resolution fronts with no set texels found partly) -
  `make --pictures` shows what was found; where three heights meet at a side's end the sides meet along one line split
  in two (collinear: closed, not edge-matched); a recess cut by a seam between two meshes of one wall shows an end wall
  at the seam (the planes are per mesh).

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
lamps; (round A2) the clock's daylight is the physical sky's and sun's, like every level's (below: "Lighting round A2"). The baked sun's search
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
  fallbacks:** a level without data, with data made from other meshes (under 95% of the level's own meshes' vertices
  matched - objects that join the world later aren't counted - another overlay or the water's grid changed since), or
  whose data says no sun keeps the original
  game's lighting: `rt_relit` is out of effect there (the reasons name the build step: "This level's lighting isn't
  built - run relit_bake", "... is out of date - run relit_bake", "This level has no sunlight to relight"), and with it
  the time of day (with ray tracing on) and the lighting passes - `rt_shadows`, `rt_ao`, `rt_lights` need the level
  relit and turn `rt_relit` on (a pass replaces its own part of the relit light; none ever stacks on a bake). `rt_gi`
  is retired: the relit level bounces its light itself.
- **Data whatever the geometry settings** (relit data v18, round LV): smoothed curves or building relief turned on or
  off since the bake don't make the data another level's. Each data mesh is also keyed by the mesh as the level's file
  gives it (`relit_mesh_original`: its file's vertices, still first, and the file's triangles); a mesh not found by its
  content is found by that key - its file's vertices take the data's own, each vertex the load's passes made
  (smoothing's edge and inner points, relief's pieces - `mesh_smoothing::made_of`) its sources' blend
  (`relit_data_for_geometry`: the stored parts, the lamp links by weight, a lamp's surface where all its sources are
  that lamp's); the sun and the bounce are traced for every vertex in the game as always. Before, a Boston bake made
  with smoothing off was "made from other meshes" for a game with it on (546 meshes not found, 88% of the vertices
  matched) and the level kept the original's lighting. The log's misses line counts the meshes found this way. A v17
  file is still read (no original keys: found by content alone).
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

### Objects relit (round P10, relit data v16)

The level's objects (instances of models) come in two kinds, told by how the game draws them (render_pc/geom.h
`instance_drawn_lit`, Singleton::render's own test): **lit by the game's lights** - every mesh asks for light, or skinned
by the vertex shader: Direct3D's colour vertex makes the vertex colour the material's diffuse (Cd), so those colours are
paint (Boston: 93 instances - cars, peds, the skater); and **drawn with their own baked colours** (lighting off: the
colour is the light the model was given - Boston: 63 - lamp globes, lanterns, bollards, signs, props).
- **Paint:** hit_surface's base colour for an object the game lights is the texel x the vertex colour, linear - the
  albedo the frame's per-pixel lighting uses (ff_shaders.cpp: the albedo x linear Cd). Its environment pass stays a
  reflection (the material's F0). Boston's car is grey-green in `pt ref` as in the frame (was near white).
- **Own colours:** an object drawn with its own colours that stands still - its transform unchanged for
  `kObjectStillFrames` (30) records, not animated -, each of its meshes placed once (one buffer, one place), is one of the
  world's meshes for the relit mode (rt_relit_world.h `WorldMesh::object`: its vertices read through its transform, keyed
  in the data by its content where it stands). The bake takes it apart with the level's model (the model is fitted over
  the level's own meshes alone; the objects come after them), its triangles are lamp candidates as the world's (a lamp
  whose own surface is an object's: the owner rule as for the world - `emitting`), and the game gives it a light store:
  the raster draw, hit_surface, ReSTIR's light list (vulkan_light_list.cpp: any instance with a store) and the
  path tracer read the same tint and emission. An object that stands elsewhere later lets its entry go (its light was
  for its place) and is drawn as before, with the light field's ratio, mesh by mesh where it stands.
- **One tint per model:** an object's colours are its model's, the same at every place (Boston: each model at 2 - 6
  places byte for byte alike, whatever the sun and the walls round them) - not the bake's light at each place. So the
  tints of a model's meshes are one: per vertex and channel the median over its places of what the decomposition made
  there (rt_relit.h `relit_object_tints`; the bake logs how far its places' tints were apart: Boston's median model x1.09,
  the most x3.5).
- **Lamps in another lighting state:** a light card hidden in the state the bake is made in (a night state's) is the
  artists' mark of a lamp too: its glow found as a shown card's (switched by its daylight), never laid on a surface.
- **Boston's globe lamps** (the 6 objects of 64E93310 on posts at y 360) are not lamps by the data: no light of them in
  the bake (no pool, no edge), no light card at them in any state, their colours the model's (luma 53 - 104 of 128 at
  every place). They are relit as yellow paint - the same in the frame and `pt ref`.
- **Checked:** `thug2_vulkan_rt_relit_check` (a model at three places: one tint, its colour over the middle place's
  light; a mesh that isn't an object left alone), `thug2_vulkan_path_check` (the emitting disc as an object's mesh in
  its own space moved by its transform: listed and lit as the world's).

### The game's own lights (round SL, relit data v17)

How a path-traced remaster of an existing game lights it (RTX Remix): the game's own fixed-function and scripted lights
are converted to path-traced lights by default, its glowing surfaces become emitters, and the game keeps deciding which
are on. Before this round the relit bake made lamps only from what its afternoon bake evidenced ("brightness alone is no
evidence") and from the frame's point lights that were on at the bake's time - so a level's night lights never existed.
- **The game's light data.** A level's LevelLight nodes (engine/gfx/scene_lights.h `create_light_from_node`): each
  makes a dynamic light named after its node at load, whatever its Brightness - colour, outer radius, brightness / 100.
  The time of day's transition scripts (tod_manager.qb: `FakeLights prefix=TRG_<state>_LevelLight percent=100` for the
  state coming, `percent=0` for the one going; `TODManager_Create prefix=<state>On_` / `kill prefix=<state>Off_` for its
  geometry) turn a state's lights and geometry on. Boston: 195 light nodes - 23 on at Brightness 60 - 100 in every state
  (its arcade, Jeers, the Big Dig lamps), 172 `TRG_Night_LevelLight_*` at Brightness 0 that the night turns on (its
  street lamps, wall lights, court lights, windows' spill); its night geometry `NightOn_*` (street lamps' lit glass,
  glows, signs) and day geometry `NightOff_*`.
- **The frame's game lights** (rt_scene.h `GameLight`, `FrameRecord::game_lights`): every light node's dynamic light (the
  flag at +0x2C - set only for a node's light, not an object's), on or off, its colour at full brightness and the
  brightness the game has it at now. The simulation is untouched: the renderer reads what the scripts set.
- **Listed in the bake** (relit_bake.cpp `level_lamps`): every one, on or off. One off at the bake's time takes no part
  in the solve (its light isn't in the bake's colours - it would only take light the others don't explain); its verdict
  is "off in the bake". The meshes hidden in the bake's state (another state's geometry) are scanned for lamp heads too
  (`EmitterTriangle::other_state`, clustered apart from the shown ones).
- **One owner** (rt_relit.h `relit_adopt_game_lights`): a found emitter within its own extent and the light's core
  (point_core of its reach) of a light is that light's head - one lamp: the emitter's shape, area and emitting surface,
  the light's switch and reach (and its colour where the bake can't see it lit). A night light takes another state's
  surface first (its night glass), a light on in the bake a shown one; an emitter is one light's head at most.
  (Round FG) A light off at the bake's time takes only another state's surface or a light card: a shown textured or
  self-lit surface's brightness is its daylight look (paint, mortar, a poster), no evidence of that light - A3's rule
  for every candidate. Before, Boston's night window lights took the brick facades' mortar and stone as heads (rows of
  "tubes" 25 - 55 m long, 34 000 - 118 000 lm, blown white with a speckle of lit texels) and a wall light took a poster
  (0.019 m^2 of "lamp texels" at 53 000 cd/m^2 - the grain in the fog round it, the courtyard tinted the poster's
  colour); BO 43 -> 13 lights carried by heads.
- **One lamp-texel test** (round FG, rt_lights.h `lamp_texel_code` / `lamp_texel_glsl`): a lamp drawn in a texture shows
  its emission on the texels the bake measured as its luminous area - the bake's own test (bright, a white or
  blackbody's colour, kTexelOverTexture x its texture's median; a blended surface's solid texels alone), its texture's
  floor coded in the light store's flags (rt_cel.h word 6's blue, the same code on every vertex of the mesh) and read by
  the relit surfaces' shading (ff_shaders) and the ray-traced hit surface (`store_emission`). The shading's own test
  before (any texel whose brightest channel passed 0.6 - 0.7) spread the lamp's luminance over every pale texel of the
  surface.
- **One emission map** (round SUBPIX, engine/gfx/rt_emission_map.h; relit data v23): the test runs once, at full
  resolution: each emitting mesh's first texture becomes an emission map under the mesh's code - level 0 the texel's
  linear colour where the test passes on it (its colour and its own alpha), 0 elsewhere; every further level the box mean
  of the one above, so a mip holds the mean emission of its footprint (flux kept at every level, as the emissive mips
  of shipping engines are made). A surface emits its store's luminance x that map, read by every consumer through it:
  the draws (ff_shaders' relit branch - kEmissionMapBinding, sampled as stage 0 is, at the raster's mip), the rays
  (vulkan_rt_scene.cpp `store_emission` - GeometryRecord::emission_slot, at the ray cone's level: ReSTIR GI's and the
  reflections' filtered hits; level 0 for a point - ReSTIR DI's targets, the path reference), the light list's power
  (the scan reads each lattice cell's mean) and the bake (rt_record.cpp `emission_texels`: the drawn area the flux is
  spread over). Before, both renderers tested the filtered texel: a footprint with lamp texels among darker ones emitted
  nothing - the flux the bake conserves at level 0 was lost at distance and in every cone-filtered hit. Neither the
  material's colour nor the vertex alpha enters the emission (a texture-space map holds neither; the bake conserves the
  flux under the same definition).
- **Emitters smaller than a pixel** (round SUBPIX, rt_emission_map.h `emission_splat_share`): under Ray Reconstruction
  an emitting triangle drawn over a pixel or two is hit by the raster's one sample in some jitter phases only - a firefly
  RR drops (round NOBIAS2's probe: 0.43 of a 1-pixel emitter's light kept, 0.02 of one lit one frame in four). Its
  directly seen light is split by its drawn area: whole at most 2 pixels, none from 8, log-linear between - the draws
  keep (1 - share) (their fragment's barycentric derivatives give the triangle's drawn area: VK_KHR_fragment_shader_
  barycentric), and ReSTIR DI's splat pass (vulkan_restir_di.cpp kSplatBody) projects each listed emitting triangle with
  the view's jittered projection, takes the same share of its flux toward the eye (its mean radiance - the map at its
  lattice cells' level - x its drawn pixels) where a ray from the eye reaches its centre, and spreads it as a Gaussian of
  half a pixel round its true place, normalised over the 3 x 3 pixels it covers, into RR's transparency layer every
  frame (NIGHTSKY's stars: a flux-conserving point the upscaler keeps; the layer is upscaled, not denoised). The layer
  is made for the view whenever there is a splat; the transparencies are drawn over it. Probe (thug2_vulkan_upscale_check
  --rr-probe, SPLAT): 1.00 of 1- and 2-pixel emitters' light kept, flat or noisy. Seen-through, light-adding and
  drawn-apart instances keep their own emission (their draws are the layer's already).
- **Its strength** (rt_relit.h `relit_lamp_models`, `RelitLampSource::kGameLight`): a light (or its head) the bake can't
  tell takes its fixture's model at full brightness - the kind from its own colour, shape and place (lamp_fixtures.h
  `fixture_of`: sodium / filament / white by its colour temperature, neon or sign when saturated, roofed or not, a
  high-bay by its mount), its flux pi L A over a head's luminous area where that is a fixture's, else its kind's typical
  fixture's. A light node with no head has no surface to size it, but the artists gave it a reach (its outer radius):
  its light falls to `kGameLightReachLux` (5 lux - EN 13201-2 class P4, a residential street's average) there - I = E
  R^2, flux 4 pi I -, within its kind's flux range (a 500-unit street lamp 10 100 lm, a 100-unit wall light its kind's
  least). A solved one is put at full brightness (its solve over the game's brightness then). One rule for every level -
  no per-level or per-light values.
- **Its switch** (rt_lights.h `lamp_switch`, `game_light_now`): a lamp that is one of the game's lights is as bright as
  the game has that light now - in the lamps pass, ReSTIR's list, the path tracer and relit's per-vertex lamps and
  emitting surfaces; any other lamp by its photocell as before.
- **False positives kept out by the data:** a light the game never turns on is no light - one at Brightness 0 that no
  script raises stays dark because the game's brightness for it stays 0, whatever the bake made of it (a level's own
  scripts switch theirs too: nj's 42 nodes, Brightness 0, by its port's FakeLights id= calls); a surface of another state that no light takes is no lamp (`kNone`: the game shows it at night, but
  nothing says it lights); a candidate the afternoon bake can't see lit keeps round relit-misses' rule (a glow at it,
  else none).
- **Checked:** `thug2_vulkan_rt_relit_check` (a night light carried by the night's glass, the day's by the day's glass,
  a light with no head stays a light; a night light the bake can't see takes its fixture's model - a sodium head's
  pi L A, a bare light its kind's typical flux; a night window no light takes and a bright texture with no light stay
  dark; the switch: on at the game's brightness, never-on and never-made off), `thug2_vulkan_rt_lights_check` (the view's
  lights at noon: two game lights the scripts turned on shine at 1 and 0.5 although the photocell has them off; the one
  never turned on stays dark).
- **One energy:** a head's surface shows its light's flux over its drawn luminous area (luminance = flux / (pi A)), not its
  kind's luminance over an area that is no fixture's (Boston's heads had sent 12 x their lamps' flux from their surfaces).
- **Boston (baked):** 535 -> 615 lamps listed, 113 -> 227 shining; 139 light nodes (23 on in every state, 116 the night's),
  43 of them one lamp with their heads. At night the street at the archway is 14 x brighter (linear). Not closed: the
  realtime estimators (ReSTIR and the classic pass alike) light the lamp-lit opaque street 0.24 - 0.5 x the `pt ref`
  (`analytic` / whole) - the realtime under-lighting found in round PT-night (0.66 there before this round).
  Round LG took it apart (same spot, same EV, the realtime frame as `pt ref` copies it - after the upscaler): the
  reference holds (its next-event estimation alone and its BSDF rays alone agree within 6 %; the street is lit by
  the lamps' own emitting surfaces, `only emit`, not by analytic lamps). Without an upscaler the realtime frame is
  0.67 x the reference on the lit ground (ReSTIR DI + GI; 0.77 with RTXDI's boiling filter off; 0.87 x the
  reference's direct light alone); the classic lamps pass 1.0 - 1.1 x. DLSS without Ray Reconstruction keeps the
  surfaces (0.93 - 1.0 x its input) and loses lamp heads' highlights (the courtyard 0.52 - 0.68 x). **Ray
  Reconstruction loses 30 - 45 % of its own input's light at night** (its output 0.56 - 0.69 x the input it is given,
  the same with the classic pass's noiseless lamps - not a noise effect), none at noon (0.99 x): unchanged by the
  preExposure (the frame's, 1, the camera's), an exposure texture, exposureScale, the guides, the reactive mask, the
  transparency layer or the preset (D, E); it depends on the input's level alone - the same frame scaled 16 x before
  it (and the preExposure with it): 0.85 x, 256 x: 0.98 x. Open: the level Ray Reconstruction is given (the night's
  lamp heads, ~10^5 x the lit ground, leave no fixed scale that lifts the ground and keeps the heads in 16-bit floats).
  Round NOBIAS2 measured it again (thug2_vulkan_upscale_check --rr-probe; New Orleans at night with `pt frames ...
  inputs`): Ray Reconstruction's output is bit for bit the same with preExposure 1 or the frame's, an exposure texture
  at the pre-exposure or 1, exposureScale, or the bias-current-colour hint on the emitters - it takes no exposure. What
  it loses is small bright lights seen directly: at New Orleans' street 5 pixels held 80 % of the emission view's light
  (lamp surfaces smaller than a pixel, lit in 1 - 5 of 8 jittered frames) and it kept 0.16 of it (lamps view 0.72,
  the whole picture 0.78; the lamps view less its emission 1.03). The probe: 40 emitters 100 x white kept 0.31 (1 px),
  0.64 (2 px), 0.04 (2 px lit one frame in four - a sub-pixel light under the jitter); in the transparency layer
  (alpha 0, added over the surface, not denoised) 0.68 (1 px), 0.99 (2 px), but a light lit one frame in four isn't
  accumulated there (0.26 over four frames). Open: how directly seen sub-pixel emitters reach the picture.

### Lighting round A2: one light unit, the sun by the relit fit, lamps by their light (`rt_relit`, `rt_lights`)

The user's standard (2026-11-17): lighting handled physically, as in modern games - no per-level or per-object tuned
values, no blending toward the bake, no compensation; a path tracer (ReSTIR DI/GI + DLSS Ray Reconstruction) is the end
goal, so every change produces the physical inputs it reads. Relit data v4:

- **One light unit for every level.** The light a relit surface gets is the global unit's (`rt_daylight.h`
  `DayLight::sun_light` / `sky_light`, `absolute_light_at` from `rt_sky.h ground_light_table`: the sun's transmitted light
  per unit of N.L and the sky's every-order light on a level surface, x `sky_exposure() / pi` - 1 on a level surface under
  the reference day, a white diffuser's radiance, `daylight_check`: 1.000). `RelitLighting` carries that light; a level's
  own time (tod=level) is its own day's (`fit_native_day` at its own state) in the same unit. Each level's fitted a, b no
  longer scale anything: the fit only takes the bake apart. Gone: `kRelitDaySun` / `kRelitDaySky` (the sunless levels'
  borrowed corpus median), `direct_scale` / `sky_scale` / `sun_colour` on the relit light.
- **Albedos by energy, not by a statistic** (`relit_to_unit`): the bounce B is the surroundings' radiance in the bake's
  own colours, so a surface takes it whole (coefficient 1) whatever the bake's exposure; the fit's g (per channel) is
  then the factor between its tints (median 1, an arbitrary scale) and albedos. Tints x g are the albedos (the check:
  the known tints again within 0.1%); the same rule for a sunlit, a night and an indoor bake. The lamps solved in the
  fit's units are divided by it too. The game's colours are radiance in the unit (round B's definition), so a bake's own
  daylight against the physical is reported (`daylight_ratio`), never used to scale.
- **The light's colours its sources'** (round relit-misses; `fit_relit_model`'s `RelitLightColours`): the model a level is
  relit by is fitted with one strength each for the sun, the sky and the bounce over all three channels, the sun's and
  the sky's colours the physical ones at the bake's own time (`own_day_light`), the bounce's colour its hits'. The
  per-channel fit (each channel alone, scaled to a median tint of 1 in each) split the sky's and the bounce's terms -
  both large on open surfaces - differently per channel and put the level's colour into its light: Barcelona's sky came
  out green in the unit (0.29 0.68 0.39) over shade its bake paints blue (0.14 0.16 0.21 on the open floors), and g per
  channel then carried that colour into the unit. Now the bake's colour beyond the light is the tints' (the art's), g is
  one number and Barcelona's sky reads 0.30 0.44 0.70. The per-channel fit is still the evidence the sun's presence and
  direction are told by (the candidates' `sun_needed` below) - it reads the bake's own light without assuming colours.
  The candidates are told apart on a sample of a big level (the meshes in their content hash's order up to
  `kCandidateVertices` 500 000 vertices - a random sample), the pick then traced and fitted over the whole: Proving
  Ground's whole world (2.5 million vertices) spent most of its 20 minutes on 32 candidates and was given up; it now bakes in 5.4 minutes.
- **The baked sun picked by the relit fit** (`relit_bake.cpp`): the correlation search's candidates
  (`BakedSun::candidates`: its best, the coarse set's next best `kCandidateApartDegrees` apart, and a low sun's ring) are
  each traced and fitted; the one whose sun improves the fit most (`RelitModel::sun_needed`, the edges' objective without
  the sun over with it - the edges across its traced shadows count only where the bake steps down there too) is the sun,
  when that gain is at least `kRelitMinSunGain` (1.01: SE - Pro Skater, evening - 1.006, kyoto 1.008; the outdoor
  afternoon levels from 1.015 - LA, CA, SE2, which the correlation alone missed, from 1.04) and the level's own data
  allows one - not a night state (its bake's directional light is the moon's), not roofed (its floors' median traced sky
  share at least `kRelitIndoorSky` 0.15: with the gain alone the skate shops' and warehouses' lamp pools "found" low suns);
  its confidence is the share its sun carries of an open surface's light (`relit_sun_share`: a sin e /
  (a sin e + b) - the artists' sun against their ambient, reported). The mean edge error (`relit_edge_error`) is reported,
  not used: it favours the weakest sun.
- **Lamps by their light** (`solve_relit_lamps`): one intensity per lamp and channel from the bake - y_i (E_j + sum I G_j)
  = y_j (E_i + sum I G_i) along every mesh edge (a shared surface colour), linear in the intensities, solved as
  non-negative least squares (projected coordinate descent, Cauchy-reweighted) - no level-wide c (which came out negative
  at night). Only a bake whose lamps were on (its own time's lamps, a dark level) tells their light; otherwise, and for a
  lamp no edge tells, a lamp shines by its own surface: a Lambertian emitter of the radiance its glow shows, pi x it.
  The game's lamps pass shines the data's solved lamps (`rt_relit_world::data_emitters`), relit's L is made from the
  same; the level's own point lights stay the game's.
- **Shapes** (`rt_lights.h EmitterShape`, `lamp_geometry` - the same function in the lamps pass's GLSL): a cluster's
  principal axes make it a point, a panel (thin, facing one way: a ceiling's lit panel, a lit window - a two-sided
  Lambertian disc of its area) or a line (4 x longer than wide: a tube - its light 1/d near it, 1/d^2 far). A flat
  self-lit cluster facing down by the world's winding (`world_facing`) is a ceiling light, facing up a baked pool (no
  light). Self-lit surfaces are looked for in a level whose bake shows no sun however bright it is (indoors, at night).
- **The bounce, judged by energy**: the white furnace (a closed room of radiance R: K 0, B R exactly) and the albedo-weighted
  series (rho E0 (1 + rho + ... + rho^n) after the game's `kRelitBouncePasses` = 3 passes a light) in
  `thug2_vulkan_rt_relit_check`. The world's bounce is relit's (per vertex, from the relit colours, three passes); live
  `rt_gi` lights the objects and characters - the per-pixel world bounce is the path tracer's (ReSTIR GI).
- **One direct share** (`rt_daylight.h direct_share_now`): the shadows pass's share for pixels without a light store - the
  clock's light's own (sun_light x sine over that plus sky_light), the model's at the level's sun without it (0 without
  one); the light field's measured share is deleted. `direct_share_at` uses the same absolute light.
- **What a path tracer reads (the relit data, v4)**: per vertex the albedo (tint x the material's texture), S/K/B/L as a
  hybrid's cache; the lamps (`RelitLamp`: position, shape - point / panel / line with its axis and half length -, core,
  reach, intensity per channel in the global unit, candela, lumens and correlated colour temperature, whether the bake
  told it); the emissive triangles (`RelitEmissive`: corners, radiance = intensity / pi, their lamp) for light sampling
  (ReSTIR DI); the sun (`BakedSun`, `sun_disc_radiance`), the sky (the sky-view table), the medium (round B), all in the
  one unit (`unit_illuminance()` lux = 1). **Still missing / not physical**: the bake's lamps are its displayed glow
  (clipped at 2 x white) where the bake doesn't tell them - real lamp surfaces are 10^2 - 10^4 x brighter; the moon's and
  the night sky's light are art values (`moon_light_absolute`, `night_sky_absolute` from kMoonLight / kNightSkyLight);
  the objects' and characters' lights are still the game's ratios (`object_lighting`'s key colour); the frame is
  display-referred (the light store is linear in the unit, the colour word tint x light read as a display colour);
  materials' roughness / metalness for the reflections; the rays' 2-unit offset leaks at concave seams (a vertex on a
  wall's foot starts its rays on the floor's plane: K too high there - the furnace check measures its interior only).

### Lighting round A3: lamps by their own daylight, ceiling lights in textures, the rays off concave seams, light cards hidden

Relit data v7 (round S took v6 for its sun radius first).

- **No "dark level".** The rule that read a level as dark from its median vertex brightness (64 of 128) is gone: it
  read 16 sunlit afternoon levels as dark (their textures carry the brightness) and turned their lamps on and their
  self-lit search with it. What consumed it, and what each reads now:
  - *the lamps' solve* (was: solve only in a bake whose lamps were "on" - its own time's, or a dark level): every lamp is
    solved in every bake; the bake tells a lamp when it has the edges and its solved light is a share of the light where
    it lights (weighted by its geometry) at least the fit's own noise (`RelitModel::lamp_noise`: the median relative
    error of the edges no lamp reaches), its solve is plausible (its pool's surfaces, per channel, no darker than the
    1st percentile of the surfaces no lamp reaches; its colour a lamp's - weakest channel at least 0.2 of the
    brightest) and - a candidate - its own switch has it on in the bake's own sky. Told: its intensity solved.
    The solve is an instrumental-variable one (`LampTerm::instrument`, the edge's mean colour x the geometry): plain least
    squares on y1 G2 - y2 G1 - a difference of near-equal products holding the 8-bit colours' noise - shrank a lamp that
    is most of the light toward 0 (a hall lit by its panel: 0.08 of it; with the instrument 1.02). A second solve leaves
    out the candidates the first gave light but the bake didn't confirm.
    Not told - off in the bake, swamped by daylight, no edges - a glow or point light (the artists' mark of a lamp)
    shines by its own surface; a *candidate* (a self-lit or textured surface: brightness alone) gives no light.
  - *when a lamp is on* (was: the clock's lamps, else a dark level's): each lamp has a **switch** (`RelitLamp::switch_sky`,
    `relit_lamp_switches`) by the geometry round it. Its surfaces roofed (their sky share K under `kRelitIndoorSky`, the
    rule that tells a roofed level's floors): an interior light, on whenever. Otherwise a street light's photocell: on
    when the daylight round it (the open sky's light x K) falls under `kPhotocellLux` (16 lux, ANSI C136.10's turn-on) -
    the switch is the open sky's light at which that happens. `rt_lights.h lamp_on` compares it with the open sky now
    (the clock's `sky_light`, or the level's own time's - `LevelEmitters::own_sky`), smooth over a factor of 2 either
    side, and never under the open sky's own photocell (`DayLight::lamps`, by the sun's elevation: the night sky is an
    art value ~10^3 x a real night's and can't hold a lamp off). Not by the lamp's own light: tried first - a lamp solved
    from a night bake carries that bake's art exposure, and New York lit 108 of its lamps at noon by their own light. The lamps pass multiplies each emitter by it; relit makes each
    vertex's L again from its stored lamp links with each lamp's amount (`relit_mesh_lamp_light`). A lamp whose switch
    isn't known (no data; the game's own point lights) follows the open sky's rule (`DayLight::lamps`, or the level's own
    time's).
  - *the self-lit search* (was: a dark level or a sunless bake): the relit bake's, in every level - candidates the solve
    confirms.
- **Ceiling lights drawn as textures.** The bake decodes each solid material's first texture on the processor
  (`engine/gfx/texel_decode` - A8R8G8B8, X8R8G8B8, R5G6B5, X1R5G5B5, A1R5G5B5, A4R4G4B4, DXT1/3/5; a small shared reader
  the material table can use too) and samples 15 points of each triangle: a texel showing a lamp (`lamp_texel`: its
  brightest channel at least 0.7 linear, its luminance 2.5 x its texture's median or more, grey-white or a blackbody's
  monotone warm / cool run) on a triangle not facing up makes a *textured* candidate (`EmitterKind::kTextured`), joined
  into panels and lines like the self-lit ones and solved the same way. A confirmed candidate's vertices are its
  **emitting vertices** (`RelitEmitting`): they show its radiance (its intensity / pi, x how much it is on - the bulb,
  the lit panel; `RelitVertex::emit`, added to what relit_colour reflects). (Round relit-misses: its own triangles'
  vertices in its own mesh - `EmitterTriangle::mesh`, `vertices` -, no longer every vertex of the level at their
  places; and an outdoor candidate the bake can't see lit is a luminaire only with the artists' mark, a light card at
  it - `glow_marks`: College's striped stand shone as a street light all night.)
- **The rays' start off concave seams.** A vertex's rays now start `kRelitInward` (2 units) into its own triangles as
  well as 2 off its surface (`relit_ray_origin`, `RelitVertex::inward` - the triangles' centres' mean less the vertex: 0
  for an inner vertex). A floor vertex at a wall's foot no longer starts on the wall's plane: the furnace room's edge and
  corner vertices see 0.000 of the sky (were 0.5 - 0.78), their bounce within 0.005 of the room's radiance; the
  albedo-weighted series' median 0.7454 against 0.7500 (was 0.7422).
- **The original's light cards hidden.** A world mesh drawn additively with a still texture (`rt_lights.h light_card`)
  is a halo, cone, pool or starburst the artists drew round a lamp: the bake keeps it as a lamp's evidence (a glow), the
  relit level hides it (its light store's colour word `kCelHiddenColour`, as the bake's shadows drawn as geometry). A
  lamp shows its own surface and the light it casts (the lamps pass: inverse square, shadows); bloom and in-scatter are
  the frame's.
- **Glow radiance**: no longer held at 2 in `emitter_of`; decoded (rt_display.h) before it is a lamp's surface light. The
  8-bit art itself (a vertex colour's 2 x a pass colour) is the limit: a lamp's real luminance is only known from a bake
  with it lit.

**Still not physical**: an untold glow's light is its displayed radiance (a lower bound); the light store's colour word is
8 bits (a bulb's radiance clips at 2 x white there - an HDR store is the fix); a lamp's switch compares the sky's light
only (the sun's direct light at dusk is left out); the light cards' lamps that have no surface of their own show none.

### Round O: one light for everything (objects, characters, ray hits)

Everything that receives light reads the same light: the relit light store (linear, in the global unit - round A2), the
lighting state's sun or moon, and the solved lamps. Before it the skater, the peds, the vehicles and the lit objects were
lit by the game's own display-referred lights turned toward the sun and scaled by ratios of the store's light (clamped
to 2; the ambient held to 1), a reflection's hit by the game's vertex colours x 2 (the bake: at night a reflection showed
the daylit level), the occlusion multiplied all of an object's light, and the bounce pass added a second bounce over
the store's.

- **The light field's probes** (`engine/gfx/rt_light_field.h`): per 128-unit cell, first-order irradiance probes (SH L1:
  c0 + c1 . n per channel) fitted by least squares over the relit vertices' normals - one for the sky's light (the
  store's sky x its traced visibility K), one for the bounce (B), one for the lamps (L x the lamps on). Where the
  vertices' normals don't tell a direction (an open plaza: every vertex facing up), the fit is held toward a prior: the
  sky's from above - a surface under an open hemisphere of radiance S receives S (1 + n . up) / 2, exactly first order -,
  the bounce and the lamps from every side (`kProbeRidge`: a regulariser on the fit, not a light's value). The field
  also keeps the direct light it was made for (the relit lighting's sun: toward, its light per unit of N.L). A point's
  light (`light_at`): E(n) = sun x max(n . toward, 0) x visibility + sky(n) + bounce(n) + lamps(n) x the share the lamps
  pass doesn't light per pixel (`rt_lights.h lamps_drawn`). `daylight_check`: a grey object's face turned as each wall
  of a corner gets that wall's light within 3.1% per term (sky, bounce, lamps, the whole with the sun); the open
  plaza's sky 0.8 up, 0.4 to the side, 0 down.
- **The objects and characters** (`render_pc/pc_lights.h OneLightSent`, `pc_instance.cpp send_lights`): while the level
  is relit, a lit instance is drawn with the device's lights from the field where it stands (`device_lights`): light 0
  the sun (its shadow the shadow pass's rays, per pixel), light 1 the indirect probe's first-order part toward its
  direction with c0 as the light's ambient, light 2 the same part's other side with the opposite colour (with light 1:
  c0 + k (n . toward) exactly, through Direct3D's clamped lights), the device's ambient black and
  `GpuRenderState::kLinearLights` on: the draws then sum the lights as linear light on the material's diffuse colour and
  encode once (`ff_shaders.cpp vertex_lighting`, `one_light_v0` - the relit world's colour word is made the same way).
  The game's light colours, a model's scripted lights (`Obj_` ambient / diffuse light members) and its dynamic lights
  no longer light anything there; the original's look (no relit level) keeps them all. The props drawn with their own
  baked colours take the store's light now over the bake's (linear) as its display factor (`scaled_colour`).
- **Ray hits** (`vulkan_rt_reflections.cpp hit_light`, through round M's light model `engine/gfx/rt_brdf.h`): a
  reflection's hit on the relit world is lit by the store at the hit - its sky and bounce, the sun's light x a
  visibility traced from the hit, the store's per-vertex lamps and the lamps the lamps pass lit this frame (the view's
  32 most important, `rt_lamp_glsl` - the pass's own lamp model); a hit on an object lit by the one light by its
  record (`rt::ObjectLight::linear`); the glass's own base layer the same; the apply no longer holds the reflection to
  1. `thug2_vulkan_rt_reflections_check`: the box's reflection at night is the store's night light (night / day 0.100
  against the stream's 0.102 - the bake's would be 1).
- **Occlusion** (`rt_ao`): a visibility of the indirect light alone - the trace scales its occlusion by the indirect
  light's share at the pixel (an object's: its record's light without the key, the key's part with its visibility
  traced; elsewhere 1 - the lighting state's direct share); no strength (`rt_ao_strength` retired), one rule for every
  object (`rt_ao_characters` retired), its reach one light field cell (`rt_ao_radius` retired: what the probes don't
  resolve; farther occluders are in the probe's light through the store's traced sky). `thug2_vulkan_rt_lights_check`:
  an object lit by its key light alone is unchanged by the occlusion; lit by the indirect alone it darkens near the
  floor.
- **Bounce** (`rt_gi`): adds nothing while the level has a light field - the world's and the objects' bounce is the
  store's (one owner); `rt_gi_strength` retired.
- **Lamps**: on a level relit from its data the lamps pass shines the solved emitters alone
  (`rt::LevelEmitters::solved`) - the frame record's point lights carry the game's colours.
- **Still not done**: the objects' raster shading is still Direct3D's form summed linearly (Lambert per light, the
  specular of the old GGX on the linear lights, added after the program) - round M's per-pixel `brdf_direct` /
  `brdf_ambient` on the same light terms is its work; the probes are per cell from the vertices (a path tracer's
  irradiance cache / ReSTIR GI replaces them); a hit's lamps are unshadowed; the clouds' shadow isn't on a hit's sun;
  the field's sun has no visibility of its own (without `rt_shadows` an object in a building's shadow keeps its sun).

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

### Round S: the light sources and the sky's state (`tod_moon`, `rt_exposure_adapt` as the eye)

The lights a path tracer samples (the sun, the moon, the sky, the clouds) and the eye that sees them, made physical; the
art values of the night, the lights' chosen sizes and snow's own overcast are gone (the user, 2026-11-17: "if we
implement TOD, we need TOD"). `engine/gfx/rt_daylight.h`, `rt_clouds.h`, `gpu_vulkan/vulkan_hdr.h`.

- **One sun and one moon.** Their angular radii live in `rt_sky.h` alone - `kSunAngularRadius` (0.53 degrees across)
  and `kMoonAngularRadius` (1737.4 / 384 400 km: 0.52 degrees) - and everything reads them: the drawn discs (the sky
  pass's sun and moon), relit's jittered sun rays (`kRelitSunRadiusDegrees` is `kSunRadiusDegrees`), the shadows pass's
  cone and `DayLight::light_radius`. Gone: the bake's 1.5-degree sun, the moon's 6 degrees, the sky pass's 0.009-radian
  moon, `rt_shadow_softness` (the player picking the sun's size; `rt_shadow_rays` only samples the disc, 1 its centre).
  The sunset is the disc's share above the horizon (`disc_above`) times the transmittance - no ramp.
- **The moon** (`moon_irradiance`): the sun's light reflected - the geometric albedo 0.12 (V), redder than grey by the
  moon's colour indices over the sun's (x 1.19 red, x 0.78 blue), x (its radius over its distance)^2, x the lunar
  phase law 10^(-0.4 (0.026 |a| + 4e-9 a^4)) (Allen; Krisciunas and Schaefer 1991) at the phase angle from its age
  (`tod_moon`, a day older each game day). The full moon: 0.31 lux at the top, ~0.25 lux on the ground through the same
  atmosphere (`daylight_check`: 0.1 - 0.3), sun : moon ~4 x 10^5. Its place: east of the sun by its elongation (the
  hour angle less it, the declination x cos). Its colour is the sun's reflected - never blue. Its sky: the atmosphere's
  for a source at the moon's elevation x its light. The night sky's own light (airglow, stars, zodiacal) 0.002 lux.
  **(Round NIGHTSKY) The moon's sky is drawn as the sun's is**: the sky-view image holds a second table, the same
  multiple-scattering model about the moon x its light at the top (`DayLight::moon_top`), rows 108 - 215 under the
  sun's, and a marker texel (toward the moon, on / off); `sky_view_linear` adds the two, the clouds' sky light reads
  both, the fog's table folds the moon's into its sun-relative rows. Before, the moon's sky was spread evenly
  (its illuminance as a uniform radiance, 1.5 x at the horizon): a flat grey 0.011 - 0.017 cd/m^2 - 3 - 5 x the measured
  moonlit sky away from the moon. Now (full moon 41 degrees up) 5.9e-3 cd/m^2 62 degrees from it, 45 up (Krisciunas and
  Schaefer's model: 4.9e-3 - 5.8e-3), bright about the moon, dark across from it, blue (Rayleigh's) as by day, ~3 x
  brighter toward the horizon (the air mass). Drawn only while the sun is down (by day it is under the table's 16-bit
  resolution). The airglow floor stays the uniform night sky's own light (6.4e-4 cd/m^2 overhead).
  **The stars** are points of their magnitude's flux: a Gaussian of half a pixel's sigma about the star's place,
  normalised to its flux (the flux-conserving point sprite space and flight games draw under TAA / DLSS: a sub-pixel
  point stays in every jittered frame instead of flickering in and out), the 3 x 3 cells round a pixel read; their
  radiance pi E / (lux x solid angle) - the pi had been missing (1.24 magnitudes faint); dimmed by the air's
  transmittance toward them (not a ramp at the horizon); the cells' share of stars by their solid angle (the cube's
  corners no longer 5 x as crowded). Under a full moon a few stars to magnitude ~3 show; moonless, many more.
  The scene's light is whichever direct light is brighter on the ground (the moon once the sun is down) - no switch
  elevations. The lamps by their photocells: on from 50 lux down, all by 10.
- **Clouds dim the light** (`rt_clouds.h` cloud_layer_light): one coverage for everything (`cloud_coverage`: the
  settings' value, else the level's own sky's, at least snow's and rain's deck), and one cloud model - the same weather
  map, shape noise, coverage curve and extinction the march and the shadow map use - read as 256 columns, each its
  vertical optical depth, each an independent plane-parallel layer: the beam exp(-tau / mu), the deck's diffuse light
  by Eddington's two-stream transmittance of a conservative layer (g 0.85), the ground and the deck's base lighting each
  other. Overcast: no direct sun, the deck's grey light (~0.2 of the clear day's on the ground, `daylight_check`). The
  light store holds the mean; the clouds' shadow map now holds each point's transmittance over that mean, so the
  shadows pass and the fog replace the mean by the local value (one shadow model). The drawn clouds are lit by the
  scene's light (the moon at night) through the air down to the layer. Gone: `apply_overcast` and its kOvercastDirect /
  Grey / Scatter / RadiusDegrees, kCloudMoonGlow, the clouds' sunset ramp.
- **The frame's pre-exposure** (`pre_expose`, `frame_pre_exposure`): a moonlit wall is ~10^-6 of the unit - a few
  denormal steps of the 16-bit frame. The published state's light is scaled by 1 / the light on a level surface (at
  least 1, at most 65536), and every pass that adds light the state doesn't carry multiplies by the same factor (the sky
  tables, the sun's and the moon's discs, the stars, the clouds' light, the air, the lamps); the meter divides it out,
  so what is shown is the physical light. Bakes and checks use the unscaled light.
  **(Round PX - UE5's / Frostbite's PreExposure) The pre-exposure is the camera's**: the meter's exposure of the frame
  before (its slot's stops, read back a frame or two behind - `follow_camera` -> `set_camera_exposure`), within
  1/256 .. 2^24 (round RX: no longer held under the brightest emission - below); the old rule (1 / the light on an open surface) only until
  the camera has been read. So everything a pass writes into the 16-bit frame, the light store's reads and the
  upscaler's input stand about where they are shown, indoors or out: inside Training by day the camera is 12 stops
  over the outdoor daylight, and with the daylight's pre-exposure everything not lit by the hall's physical light
  (emitters, glows, display-referred colours) was shown x 4 300 - white blobs, blown copings. The published state is
  physical and carries the frame's pre-exposure apart (`DayLight::pre_exposure`, chosen by day_clock with
  `pre_exposure_for`, latched by `set_day_light` - not part of the lighting: no serial, no relight); `day_light()`
  returns it pre-exposed by that. The histories kept in the frame's unit take a change into the new one (now / then,
  as UE5 corrects a history by PreExposure): the volumetric fog's, the drawn clouds', the one-bounce light's (by its
  sun's unit); DLSS / FSR are told the frame's pre-exposure; ReSTIR's reservoirs hold physical light. The meter's
  readout opens its window when the pre-exposure has moved a stop.
- **The eye** (`rt_exposure_adapt` on / off; `vulkan_hdr.h`): the meter adapts to the physical luminance with
  Krawczyk's key (it falls in the dark: a full moon's night ~4 stops darker than day), down to the scotopic floor
  (0.001 cd/m^2) and no further; then rod vision (`rod_vision`, Krawczyk, Myszkowski and Seidel 2005 after Jensen et al.
  2000): the rods' share 0.04 / (0.04 + Y cd/m^2), the scotopic luminance V shown in the rods' blue - this is where night
  looks blue. `rt_exposure` stays the player's compensation.
- **Retired**: `rt_sun_source` (the game's sky sun and the per-level `"sun": "level"` opt-outs: the clock's sun is the
  sun; the console's `rt sun source` keeps a fixed direction), `rt_shadow_softness`, `sun_colour_for` (a second sun
  colour no one read: the sun's colour is the lighting state's).
- **Still to come**: the textured, phased moon disc and a star catalogue (the disc's radiance is the seam); light
  pollution over a city from its lamps' upward light; the overcast sky's radiance distribution in the light store (the
  drawn clouds have it; the store takes the mean on a level surface); the eye's slow dark adaptation (minutes) - the
  meter keeps its seconds; the night's emissive art textures (unlit game colours) still at the frame's pre-exposed scale.

#### Round S2: one lighting at a time (the lighting's cut, the bounce, the lamps' owner, the weather's cover)

Changing the time or the moon while playing left the picture blown out or wrong for seconds, while the same settings
from a fresh start looked otherwise. The meter's readout (below) showed three stores holding light of the lighting
before - none of it a 16-bit overflow (no sample over the format's range in any run):

- **The eye** carried its adaptation across: a night's eye (+14 stops) on a dusk (target +5.7) was 4 stops over for a
  second or two; a dusk's eye on a night under for three.
- **The vertices' bounce** (relit's B, traced from the relit colours themselves, one more bounce a pass) started from the
  light before: a fresh start's from the bake's (the level's own time, not pre-exposed), a live change's from the last
  lighting (at its pre-exposure). A new-moon night kept a dusk's bounce - 11 x the fresh start's light, for good.
- **The lamps' owner** was the lamps pass's last frame (`set_lamps_drawn`, 0 until its first view and between views): a
  level's first light took the lamps per vertex as well as the pass shining them, a light set later didn't.

Now:

- **The lighting's cut** (`DayLight::cut`, made by the clock): a new number whenever the light changed by more than the
  running clock - a time, a moon, a weather or a cloud cover set, another level. What holds light keyed to a lighting
  starts again at a cut: the eye starts at its target and follows the picture without adapting while the world's light
  is still being made (`cut_settling`, at most `kCutMostSeconds`); relit restarts the vertices' bounce from none (its
  passes add the new light's bounces) and hurries the relight as a first light; once the world shows the cut's light
  (`note_cut_shown`: the relight, the light field and the bounce passes done) the temporal histories start again,
  seeded (the one-bounce light, the occlusion, the fog, the clouds) - the camera cut's rule (`vulkan_rt_history.h`).
  The running clock is no cut: everything follows it as it goes, the upscalers told the frame's pre-exposure
  (`preExposure`, FSR's and DLSS's) so their history is taken at it.
- **The lamps' owner** is the settings' (`set_lamps_owner`: rt_lights on, a strength, ray tracing, the pass made) and
  the amount the lighting state's: relit, the objects and the pass read one state.
- **The weather's cover**: `rt_cloud_coverage=auto` is the air's typical cover (`weather_cloud_cover`, in oktas: clear
  1/8, hazy 2/8, misty 6/8; rain and snow their decks) - the painted dome's reading (0.2 + 0.7 x its clouds' share; BO's
  noon kept 0.35 of the sun) and the dome's analysis (`rt_sky_match`'s bands) retired.
- **Relit data** records the sun's radius it was baked with (version 6): data of another sun's size is out of date.
- **The meter's readout** (`vulkan_hdr.cpp` log_readout): when the pre-exposure changes, every half second for 8 s -
  the pre-exposure, the eye's stops and target, the metered physical luminance (cd/m^2), the brightest sample and the
  samples over the 16-bit range (or not a number) and negative; any over-range sample at most every 5 s after.

Still to come: relit's colour words hold pre-exposed light in 8 bits (display-encoded, at most 255/128), so a
pre-exposure change needs every vertex written again - a store of physical light (a shared exponent) with the frame's
pre-exposure applied where it is drawn would make the change atomic without the hurried relight. The emissive art (lamp
heads, glow cards) and the original's light cards are still game colours, unscaled by the pre-exposure (A3).

#### The passes on every surface; the night's luminance measured

New Jersey (a THUG1 port) showed no sun shadow - not the skater's, the peds' or the poles' - and an evenly lit street,
and its night was grey everywhere. Measured (the meter's readout and `rt probe`, below):

- **The screen-space passes ran before part of the world.** A scene draws its meshes in two groups: the ones before
  the first transparent material, then the rest (`kSkyFreeScenePasses`).
  `opaque_drawn` came between the two, so every surface of the later group was drawn over what the passes did - New
  Jersey's roads and pavements (first pass blended, flags 0x64, blend 5) among them: no sun or object shadows, no lamps'
  pools, no decals, AO or bounce light there; the game's own skater shadow, hidden while the pass casts the objects'
  shadows, came back only while the pass waited for the baked sun's fit. At the user's spot the sun's ray from the
  shadow's place met the skater (the objects' casters block it) and the clouds' map kept the whole sun there (a gap:
  2.16 x the mean beam of 0.463 at cover 0.25) - not a cloud shadow. `opaque_drawn` now comes after both groups
  (`draw_frame.cpp`; rt_scene.h ViewListener): the skater's, the peds' and the poles' shadows, and the decals, show on
  those roads.
- **The lamps' switch read the pre-exposed sky** (`rt_lights.cpp lamp_sky_now`): the published state's sky is x the
  pre-exposure, the switches in the global unit - a night's sky read 65536 x brighter; only the open sky's photocell
  (`DayLight::lamps`) held the lamps on. The pre-exposure is divided out now.
- **The readout gives the luminance** (`log_readout`, also on each lighting cut): the samples' median, 95th and 99.5th
  percentiles and the brightest in cd/m^2 (a 1/4-stop histogram from 2^-24 to 2^24 in the meter's pass) with the rods'
  share at each. `rt probe` adds the light at the probed point (`RayTracing::describe_light`): the shadows' light, whether
  the world's and the objects' casters block it, the cloud cover, the mean beam, the direct share and the clouds' map
  there.
- New Jersey at full-moon midnight (0.127 lux, clouds 0.25): median 0.0036 cd/m^2 (the rods' share 0.92), 95th 0.034
  (0.54), 99.5th 0.04 (0.50), brightest 24 cd/m^2; the moonlit asphalt at 0.004 - 0.02 cd/m^2 is at the scotopic end of
  the mesopic range (CIE 191: 0.005 - 5 cd/m^2), so near-grey is right there. Its lamps don't reach the street at that
  spot: of 364 lamps 44 shine (32 interiors always on, 12 glows); 300 give no light (the day bake can't tell a street
  lamp that was off - a bake matter). City sky glow from the level's own lamps is not modelled: the bake's 6.8 x 10^7 lm
  in all (a fifth upward, a tenth of that back over a patch ~4 km across: ~0.03 lux) is dominated by a few solved lamps
  of 10^5 - 10^6 lm (a street lamp sends ~10^4), so it is no physical input yet; and a real city's glow is its whole
  region's light, not one level's lamps - weather / site data, like the cloud cover.

### Round L: a physical light store, lamps by physics, a camera's exposure

**The light store holds physical light** (`engine/gfx/light_store.h`). The relit world's per-vertex light - its stream
(`rt_cel.h`), the light field, the bounce's hits - is light in the global unit, not pre-exposed. Before, the stream was
six 8-bit words of pre-exposed light x 128: a lamp's head held at 2 (about 1.8 cd/m^2 at a night's pre-exposure of
65 536), the bounce traced from those bytes (lamp-lit walls clipped), and every pre-exposure change rewrote the whole level
(the hurried relight at a cut). Now:

- A light word is RGB9E5 (Vulkan's E5B9G9R9 layout, EXT_texture_shared_exponent's encoding; decoded in the shaders from a
  uint attribute or a buffer word) x `kLightStoreScale` 2^10: 6e-8 .. 64 units - 1.5e-3 .. 1.7 x 10^6 cd/m^2 -, its 9
  bits from 7.8e-4 cd/m^2 (1e-4 cd/m^2 within 1 %). 4 bytes a colour as before; the stream is 8 words (32 bytes): the
  light words 0, 2 - 5, 7 (indirect, lamps, sun, sky, the light drawn, the vertex's own emission), the tint and its sun
  share (word 1) and the flags (word 6: N.L, K, "emission on lamp texels only", has the stream / hidden) as D3DCOLORs.
- The frame's pre-exposure is applied where the light is read, one value a frame (`frame_pre_exposure`): the draws'
  `VertexUniforms::light_store`, the ray-traced passes' `GeometryRecord::light_exposure` (hit_world_light), the light
  field's reads (`LightField::light_at / at / whole`; `physical_light_at` for the light window, whose halves hold physical
  light x 2^10 and whose origin's w carries P / 2^10), the relit sun of the surface and cel draws. relit's lighting
  (`day_scales`) divides the published state's pre-exposure out; `vertex_lamps` and the bulbs' emission no longer carry it;
  the lighting's hash no longer holds it - **a pre-exposure change writes nothing** (rt_relit_check: the same words at
  P 1, 4096 and 65 536).
- **The bounce's hits are physical light** (`RelitMesh::light`, `relit_received`): tint x light, no 8 bits, no hold at 2,
  and without the emission (a lamp's own light is the lamps' L - counted once).
- ~~The pre-exposure is held under the brightest emission~~ (round L's `kFrameLightMost`, **retired in round RX**). Held
  there, the frame sat under the display's range whenever the camera wanted more than the emitters allowed (BO's night
  with a lamp head in view: 7 600 - 9 800 against a camera of up to +16 stops). Now, as UE5 and Frostbite do, the
  pre-exposure is the camera's alone, and light past the 16-bit floats is written as their largest finite value (`kFrameHalfMost` = 65 504): the draws' output (ff
  `o_colour`), the lamps pass, the effects' added light, the reflections' composite, ReSTIR DI / GI, the fog, the sky,
  the sun, the path reference - and the upscaler's input once more (its preparation pass: past the range 65 504, not a
  number 0; blended light can sum past it). A lamp head 10^5 cd/m^2 is then held at 65 504 in the frame - still about
  12 stops over white after the tone curve; what it loses is only bloom energy from the part past the range.
  What round LG took for Ray Reconstruction losing dim light (round RX, BO's archway at night, `pt ref 128` against
  the realtime): RR on its own keeps a floor's light at any level - `thug2_vulkan_upscale_check --dlss` (and its
  `--rr-probe`): 0.985 - 1.002 of its input from 0.003 to 0.18, noisy or not, and it takes its history into a new
  pre-exposure at once. In the game, once the camera has settled, RR's output / its input (the input shown in its
  place) is 1.0 - 1.1 on the lit street and ground, 0.9 in the courtyard of lamp heads. LG's 0.56 - 0.69 were first
  captures in a game with the metered camera live: there the old build lost light everywhere (0.42 - 0.47 of the
  reference against 0.72 - 0.80 a minute later), this one keeps the lit surfaces (0.64 - 0.74, its input 0.64 - 0.67)
  and still loses ~30 % of the lamp heads' highlights (courtyard 0.35 - 0.51 against its input 0.68) until the
  camera stops moving (the meter set by hand: 0.66 - 0.69).
  **Round AE - the meter before the denoiser; one pre-exposure rule.** Two things made "live" and "set by hand" differ:
  1. *The meter read RR's output* (it ran at the tone mapping, after the upscaler): whatever RR lost raised the
     exposure, which became the next frame's pre-exposure - a loop through the denoiser (the reading fell 0.108 -> 0.073
     cd/m^2 over 40 s, the camera crept +0.4 stops). Now the meter is its own listener at `rt::ViewOrder::kMeter` (940:
     after every lit pass, before the upscaler) and reads the frame as drawn (`ViewDrawn::rect`), the slot kept by where
     the view is shown; the tone-mapping pass reads that slot (`record_meter_pass` -> `record_tone_map_pass`). Each of
     the 64 x 64 samples is the linear mean of 4 x 4 taps over its cell (`kMeterTaps`, `meter_tap_place`): the input is
     a 1-spp, firefly-laden frame, and the log of one noisy pixel averages under its light (an exponential sample's by
     0.83 stops) - box-filtered first, as engines meter a downsample, it reads -0.07 stops (thug2_vulkan_hdr_check).
     UE5 meters after TSR; with a denoiser in place of TSR that is the loop above, so the input is metered here.
  2. *The camera set by hand fell back to the daylight's pre-exposure* (1 / the light on a level surface - ~100 x the
     camera's at night), so `pt ref` captures gave RR an input at another level. Now the frame's pre-exposure is the
     exposure in use, metered or not (UE5: PreExposure follows the exposure, manual or automatic): the manual camera's
     `exposure_at_ev100`, and 1 for the fixed camera of `rt_exposure_adapt=0`.
  Within a frame every consumer reads one value: day_clock latches it once a step before `draw_world`
  (`set_day_light`), the meter divides by that value (its readout's "metered with" = the frame's), and DLSS / FSR get it
  as `preExposure` (NVIDIA's DLSS guide: the pre-exposure the input was multiplied by, each frame; the history is
  rescaled by it - no reset). A drifting pre-exposure costs RR almost nothing: 40 4-px emitters 12 stops over a floor,
  x 1.0001 / 1.001 / 1.01 a frame keep 0.889 / 0.886 / 0.869 of the frame's light against 0.889 held still
  (thug2_vulkan_upscale_check --dlss: the drift's cost within 2 %). What RR keeps of small emitters at all (~0.89 of
  their light, at every level and drift) is its own. BO's archway at night, still (`pt upscaler 32`, the averaged
  input vs output): camera EV100 3.03 - 3.19 over 70 s with no drift, RR's frame out / in 0.85 - 0.92 live and 0.90 set by
  hand; the courtyard's lamp-head cells 0.55 - 0.91 / 0.66 - 1.16 (both noisy: their input is a few fireflies).
  `pt upscaler [frames]` logs the upscaler's input and output, averaged over the frames, on an 8 x 8 grid.

**Lamps by physics where the bake can't tell** (`engine/gfx/lamp_fixtures.h`, `rt_relit.h relit_lamp_models`). The
bake measures a lamp only when it was on and outshone the rest; New Jersey's afternoon bake told 34 of 364 lamps - 300
"told dark" were street lamps that were off - and some solves ran off (860 000 lm from one lamp; a street lamp sends
~10^4). Now each lamp has a source:

- **solved** - told by the bake, and its surface luminance (its flux / (pi x its luminous area)) within its kind's range;
- **model** - the bake couldn't tell it (no edges; off in the bake by its own switch): a Lambertian emitter of its kind's
  luminance L over its own luminous area A, Phi = pi L A, put into the unit's intensity by its shape
  (`relit_lamp_intensity_for`, the photometry's inverse);
- **implausible solve -> model** - solved, but outside its kind's range: a failed measurement, logged per lamp
  ("relit bake: lamp N (...): an implausible solve - now model ...");
- **own surface** - the level's own point lights (no surface to model), within their kind's flux range; outside it, and
  a solved lamp with no luminous area outside it, its kind's typical fixture (12 000 lm a street light, 3 000 a tube...);
- **not a fixture's size** - a glow or candidate whose luminous area no one fixture of its kind has (a lit wall, a band
  of white paint, a halo card with no saturated core): no light. A3 lit an untold glow by its card's displayed radiance
  over the whole card - College's magenta card at the origin, core 172 units, sent 46 million lm and blew the plaza
  magenta;
- none - a candidate (a bright surface) the bake doesn't confirm, unless it was off in the bake and is shaped like a
  luminaire outdoors (not a sign, a window or a saturated colour).

Each kind has a luminous-area and a flux range besides its luminance (lamp_fixtures.h). New Jersey after: 363 lamps, 1.38
Mlm in all (median 4 910 lm a shining lamp, most 34 662; was 67.8 Mlm, one of 860 000); College 0.78 Mlm (was 146 Mlm).

The luminous area is the lamp's emissive triangles x their lit share (`EmitterTriangle::lit_share`): a light card's
saturated core (texels whose brightest channel is at least kTexelBright - the lamp the halo is drawn round), a texture's
lamp texels, a self-lit triangle whole. The kind (`fixture_of`) from the lamp's own data: a colour off the Planckian locus
by more than 0.03 in CIE 1960 uv (a neon or a sign - a magenta card is a neon tube with a neon's flux, in its colour), a
vertical panel (a sign or a lit window), roofed (a tube, a ceiling panel or a bulb), outdoors by its colour temperature
(sodium under 2500 K, a bulb or globe lantern under 3500 K, else a white street light). The table's luminances and their
sources are in the header (checked against published sources since the lamp solve check, below). The colour: the lamp's own where it
is a light's, a sign's or a neon's; else its kind's colour temperature (`blackbody_colour`, Kim et al. 2002's locus).

**The art's lamp exposure (the lamp solve check, relit data v9).** Every lamp the first L bakes measured was judged
implausible (New Jersey 22 of 22, Boston 20, Baltimore 17, College 24 of 25). Not a unit error - the photometry is
consistent (New Jersey's lamp 40: 1.081 in the unit x 82 477 lux x its 0.30 m core x 6.0 m half length = 164 000 cd,
x pi^2 = 1.6 Mlm, its pool 61 000 lux at the vertex it lights most). The bake's colours are display-referred: its
daylight is physical (the bake's own 1.1 - 2.5 x the physical day), but its artists painted lamp-lit rooms, a church and
plazas about as bright as their sunlit streets - pools of 1 500 - 110 000 lux where an office is lit to 500 and a street
to 2 - 15 - so every solved flux was 10^4 - 10^7 lm. The solve tells each lamp's light up to one factor, the art's
exposure of its lamp light, so that factor is told from the level's own solved lamps (`relit_lamp_models`: the median of
log(kind's typical flux x fittings / solved flux), at least kLeastExposureLamps = 3): New Jersey 0.0054, Boston 0.0019,
Baltimore 0.0039, College 0.0018 - the art 190 - 570 x brighter than its lamps make it. Each solved lamp is then its
solved light x that: its flux against the level's others and its colour, measured, are kept; it is plausible within its
kind's flux (x its fittings for a row) - not its luminous area's luminance, which holds the drawn housing and glare
(New Jersey's textured panels 0.6 - 9.5 m^2, Boston's church cards 23 m^2). Kept now: 11 / 22, 10 / 20, 17 / 18, 19 /
25. Also: a line whose luminous width is a fitting's (0.015 - 0.6 m a tube's) is a row of fittings, its length over 2.44
m (an 8 ft T12) the count - for a glow or a told lamp; an unconfirmed candidate must be one fitting (New Jersey's "110
outdoor tubes" were lamp-texel strips 5 - 20 m long and 2 - 30 mm wide: now 7 tubes); a glow whose saturated core is no
fixture's size is its kind's typical fixture (its glare is blown out with it - Boston's church lanterns), a card with no
core still none (College); each lamp's switch is told where its light falls even while its light is 0 (a candidate's
model read "outdoors" before). The fixture table's figures are now checked against published sources, cited per figure in
lamp_fixtures.h (Flagstaff's lamp tables, the Philips / Signify leaflets, DIALux's and Wikipedia's luminance tables, CIE
150's sign limits).

**Lamp heads at their luminance**: a lamp's own surface (its emitting vertices) shows its luminance - the kind's or its
solved flux over pi A - in its colour (`RelitVertex::emit`, the stream's word 7), not "its light / pi". The surface
draws add it per pixel (x the texel; only on the lamp texels for a lamp drawn in a texture), the reflections' hits and
the shadows' ratio take it; the meter and the bloom see it.

**Relit data v8**: each lamp's source, fixture, area and luminance. Rebake every level.

**The cloud cover's meaning** (`rt_clouds.h cloud_amount`): "a quarter of the sky covered" left 44 - 54 % of the light
model's columns clouded (a 39-degree sun kept 0.46 of its beam on average). The table (kCloudAmounts) was measured from
three places' views and sampled the weather's patches too sparsely; the columns took the shape noise at its nearest
texel without the march's detail erosion. Now the columns sample the noise as the march does (trilinear, with the
detail; 32 x 32 over the weather's period) and the amount for each tenth of cover is found by bisection on the model's
own clouded share (overhead transmittance under 0.9): a cover of 0.25 clouds 0.25 of the columns, and a 39-degree sun
keeps 0.76 of its beam on average (0.5: 0.50 / 0.52). The vertical cloud fraction is the cover; an observer's sky is a
little more covered still (slant views meet the clouds' sides).

**A camera, not an eye** (the user: "physical, but what a camera sees"). The output stage's rod vision (round S's
scotopic desaturation and Purkinje blue) and the scotopic floor are gone. The auto exposure (`rt_exposure_adapt` on/off)
is a camera's: the reflected-light meter (ISO 2720, K 12.5) with the scene key that falls in dim scenes (Krawczyk's law)
within a real camera's range - EV100 -4.1 (f/1.4, 1/30 s, ISO 102 400) to 21 (f/16, 1/8000 s, ISO 100) (`kCameraLeastEv100`,
`kCameraMostEv100`, `camera_stops_range`). A full moon's night is shown about 4.4 stops darker than a day, in full colour
as a sensor records it; a starless overcast night is beyond the camera's least EV and recorded darker. White balance is a
fixed daylight (D65) white - moonlight records slightly warm, sodium lamps orange. The readout logs the camera's EV100.
`rt_exposure` stays the player's exposure compensation.

### Round light cards: cards laid on surfaces, one owner for a blended surface's lamps, no cap on the lamps

- **A light card laid on a surface is that surface's light** (`engine/gfx/light_cards.h`, relit data v12). A card (an
  additive still mesh) adds its texture x its colours over what is behind it; the relit level hides it. Where it lies on
  a surface (parallel within about 18 degrees, the surface's vertex within twice the rays' offset of its plane, the
  vertex's own area sampled 5 x 5 inside it) its light divided by the surface's texture colour is added to the vertex
  colour before the fit - what the vertex colour would be had the bake put that light into it. Off a floor the card's
  saturated core is the lamp itself and is left out. The cards' own colours are no surface's light: left out of the fit.
  Training: 69 card triangles, 33 on a surface, 403 vertices. Its lower floor's cards are weak (blend 2, fixed alpha
  0.08): they were not why the floor was black.
- **Why Training's lower floor was black at night**: the floor is an alpha-blended mesh (material 9A1DDE2D, three blend-5
  passes). The lamps pass finds a pixel's surface with a camera ray against the opaque and cut-out instances only
  (`kLightsSurfaceMask`), so it never lit the floor; and the relit store lit no lamps per vertex while the pass owned them
  (`lamps_drawn`). Now each surface has one owner for its lamp light: a world mesh the pass can find
  (`WorldMesh::lit_per_pixel` - opaque, or cut out by an alpha test) by the pass, a blended one by its light store
  (`write_streams`: its lamps x 1, each lamp's own switch in its L).
- **No cap on a level's lamps** (`cluster_emitters`): the 4096 kept the brightest candidates; Proving Ground's whole world
  has 8581 (721 glows) - whole districts' lamps were never looked at. A view still shines at most `kMaxViewLights`.
- **Light cones** (reported only, `log_cones`): a blended mesh whose walls stand all round an axis from a lamp (its
  narrow end at the lamp, opening away from it, `cone_fit`) - Training has 3 - 4 (cones under its wall lamps). Its grey
  cones under the hall's troffers (8-vertex alpha-blended frusta, material FD86DE2E, unlit white vertex colours) hang
  from no lamp the data holds on their axis: not told, so nothing is hidden by shape yet.

### Round W: the air and the wind, weather surfaces through the material path

Light and air are separate transports (the user, 2026-11-17: "if a ray from the sun cannot reach us without bouncing off
another surface, we are indoors ... no wind through glass"). Indoors and outdoors are derived from the level's geometry
and what its surfaces are made of, never flagged.

- **The air's transport** (`engine/gfx/air_volume.h`, built by `render_pc/air_world.h` at load on a worker, published,
  sampled): the level's collision cut into 16-unit cells in 8^3 bricks (a brick no face touches is one value), each face
  rasterised conservatively with its share open to air - its terrain's material kind (`rt_material_table.h
  kind_of_terrain`: `TERRAIN_CHAINLINK` a chain-link fence 0.86, `TERRAIN_METALGRATING` a bar grating 0.79 - their open
  areas by their standard dimensions; `TERRAIN_PLEXIGLASS` glass and everything else 0). Each cell's passage width (the
  narrowest of its runs of passable cells along x, y and z); the free stream flooded from the sky over the level through
  cells open every way for 8 cells (3.25 m); elsewhere a best-first search out of it, the wind's share T x min(1, 6.2 w /
  s) - T the screens passed, w the narrowest passage, s the way come (a free jet's potential core: Rajaratnam 1976). A
  closed room is still (0); a doorway's draught keeps the free stream's speed about 6 door widths in and falls off after;
  glass blocks the air while the light store lights the room through it. Also each cell's sky reach (the open shares
  straight above multiplied). Not modelled: the wind's direction (no lee wakes), street-canyon recirculation, buoyancy.
  Cost: a 600 m street grid of 400 buildings 2.2 s on a worker, about 100 MB kept; a sample 0.08 us.
- **One wind** (`engine/gfx/wind.h`): the weather's 10 m wind (`rt_wind`, now for everything, not only `rt_water`), the
  log profile over a town (z0 0.5 m) to a thing's height - near the ground at 2 m -, aloft at the boundary layer's top
  turned 30 degrees by the Ekman spiral (the clouds), gusts whose spread is the turbulence intensity 1 / ln(10 / z0),
  times the air transport's share where the thing is. Every consumer reads it: the water's bodies (their shelter - the
  height-field sky openness is gone), the grass (a blade a cantilever bent by its drag: E 0.3 GPa leaf, 4 x 0.3 mm, the
  log profile over its own z0 0.1 h; gusts carried downwind at the wind's speed), the clutter's leaves and weeds (the
  same bend), the falling snow's drift and eddies, the rain's slant, the clouds' drift (`rt_cloud_speed` retired). On the
  device the GPU's consumers sample a window of the volume round the camera (`gpu_vulkan/vulkan_air.h`: 128 x 32 x 128
  cells of 32 units, RG8, filled on a worker). `tools/one_wind.test.ts` keeps any other wind constant out.
- **Snow through the one light model** (`gpu_vulkan/vulkan_snow.cpp` - the audit's S1 - S4): the layer is lit with
  `rt_brdf.h` and the material table's snow (fresh 0.86, packed 0.70 - Warren 1982; ice F0 from n 1.31): the
  snow-covered ground's radiance as the world drew it (the light store's sky, bounce and lamps, the sun and its traced
  shadow) over the snow's albedo, less the sunlight traced at the ground under each layer vertex, is everything but the
  sun, carried round the layer's surface as an even hemisphere in its hollows' shade; the sun is DayLight's `sun_light`
  by the layer's own traced visibility. A flat patch matches the covered ground within 0.2% in sun, shade and lamp
  light. The flakes, the spray and the haze are a single scatterer's light: the sky above and the snow below averaged
  over the sphere, the direct light by an ice Henyey-Greenstein phase (g 0.75). One snowfall in the air:
  `snow::extinction(rate)` from Gunn and Marshall's flakes (1 mm/h - moderate - 5.3e-3 /m, 740 m), read by the fog's
  march and by the snow's own haze, which steps aside while rt_fog marches. Where the falling snow reaches is the
  precipitation occlusion map's (the rain's map, one map - `kTrailSnowTraced` reads it at the rain's binding), traced
  straight down: flakes follow the air and settle where it slows, so a wall's lee gathers snow rather than staying dry as
  in a rain shadow (tried along the wind-slanted fall first: it bared the lee strips that fill in reality). Drifting
  (deposition by the wind's speed) is not modelled.
- **Wet patches** (the decals' `kWet`) only where the rain reaches (the volume's sky).

### Round W2: grass and clutter lit by the one light; the air's and the rain's leftovers

- **The light store round the camera** (`engine/gfx/light_window.h`, `gpu_vulkan/vulkan_light_window.h`): Round O's
  light field sampled at its own cells' centres in a box round the camera (96 x 16 x 96 cells of 128 units - 312 m
  across, 52 m up and down), one first-order probe a cell holding the whole indirect light (sky, bounce and every lamp:
  the lamps pass lights only what was drawn before the blades and items). Three RGBA16F planes in one 2D image, filled on
  a worker when the camera moves an eighth of the window or the field changes (0.9 s for the 147k probes), read
  trilinearly (`window_probe`, `probe_light`). Without a field the probe at the camera (the field's, else the clock's
  open sky) stands everywhere.
- **Thin sheets** (`engine/gfx/foliage_light.h`): a blade, a leaf, a weed, a scrap of paper is a thin sheet - the face
  toward the eye reflects through `rt_brdf.h` (the coupled Lambert and GGX with the material's albedo, F0 and roughness),
  the light on its other face comes through it by its diffuse transmittance (measured: Gates et al. 1965 for a green
  leaf, Elvidge 1990 for dried plant material - `rt_material_table.h` kinds `leaf`, `leaf_dry`, `paper`, with per-channel
  colours), leaving through the near face's interface (1 - E). In a lawn the blades above shade a blade: the canopy's
  gap fraction (Beer's law, Monsi and Saeki 1953; a spherical leaf-angle distribution, G = 1/2) over the drawn blades'
  own leaf area (their count a square unit x a tapered blade's area, (1 - t)^2 of it above a height t). A tip facing up -
  nothing above it - is lit exactly as a flat surface of its material is (the grass check's case, 6e-8).
- **Grass**: a compute pass shapes each chosen blade as the draw will, traces its sun ray from its middle (ray tracing
  on), takes the window's probe there and works out its radiance toward the camera at its root and its tip (16 bytes a
  blade); the draw interpolates. The frame's colour is never read: the colour copy, the depth match and the "known"
  learning are gone. Albedo and transmittance the leaf's (a dried blade the dried leaf's) x (1 +- 10%) per blade.
- **Clutter**: a compute pass traces each item's sun ray; the vertex shader lights each vertex through the same sheet
  model with the item's material (fallen leaves the dried leaf's, weeds the living leaf's, paper, a can's bare metal and
  painted label, pebbles the gravel's albedo range) and the mesh's normal turned with it. The ground's luminance over a
  hand albedo and the mesh's baked shading are gone.
- **Clutter where the air goes** (`scatter.h` Input::air): leaves and paper are kept as often as the level's air
  transport says the wind reaches the place (sampled a cell and a half up: the ground's own cells are walls), weeds as
  often as the sky's fall does; cans and pebbles keep their own rules. The scatter is made once the volume is published
  (`air_world::settled`) and again when it changes.
- **Decals**: they still multiply the lit pixel (after the light). A material edit before lighting needs the decals in
  front of the world's shading - a deferred decal buffer the world shader reads (albedo x, roughness, normal), which needs
  the world's depth before its colour (a depth prepass or the path tracer's visibility buffer). The forward renderer
  draws depth and colour together, so decals can't be put before the light without that prepass.
- **God rays under a roof**: the march keeps the peak's potential under its own shadow (`kRawLit`: the sun's rays, the
  air's sun map, the clouds') beside the unshadowed one; the composite takes min(unshadowed x the screen's open share,
  shadowed) - air the world shadows (a roof the view doesn't show) gets none of the peak, whatever the screen shows.
- **The street air's shade light** (levels keeping their own fog): only as much as the sky its air sees - four
  stratified rays a pair toward the upper hemisphere; the fog check's processor reference traces the same rays (the
  pass's hash) and integrates the open share for its exact reference.
- **The aerial composite** blends (colour x T + light) like the level fog's: the frame is linear since round F, so the
  colour copy it was read through is gone.
- **Wet ground in the reflections**: a surface shaded through its map takes the world shader's wet material in the
  trace - F0 and roughness toward water's by the flat water share - and the reflection is added by the share its shading
  left out; standing water's normal carries the rain's rings (`rain_ripple`, the world shader's) in the trace too.
- **The flakes' eddies**: their swing sqrt(2) I L / 2 pi from the turbulence (wind.h `turbulence_intensity`, lateral 0.75
  and vertical 0.5 of it - Panofsky and Dutton), their turning sqrt(2) sigma / (L / 2 pi): none in still air (were 16 and
  26 units by hand, turning at fixed rates).

### Round V: a prepass and the light per surface (`gpu_vulkan/vulkan_surface_light.h`)

Before, the world was drawn by the original's forward renderer with the one light model in its shading, and the
ray-traced shadows, the lamps, the occlusion and the decals were full-screen passes multiplying and adding onto finished
colours at `opaque_drawn`. What was drawn after them lost them (New Jersey's roads, drawn with a blended first pass in
a scene's later group, had no shadows until the passes were moved after both groups), and what was drawn before them
took them whether or not they were its light (glass, water, glows under a shadow's multiply). Round V evaluates the
light once per surface: the passes make the surface's light *inputs*, and each surface's own shading reads them.

While a view of a relit level (`rt::cel_world().relit`) is drawn with ray tracing into the linear frame, single-sampled
and without cel shading:

1. **The world kept, its depth first.** Every draw render_pc makes in the view's world passes
   (`rt::world_surfaces_drawing`, set with `rt_record::set_world_passes`: the scenes, the objects, the splats and
   projections - not the sky) into the view's target is kept (`surface_light_keep`: the state block, the call and the
   per-draw state render_pc names before it - the light store's source, the trails' ground, the water's material and
   body - named again when it is drawn). Each kept draw is *prepared* once, as it comes, as its shading will draw it
   (`gpu_vulkan/vulkan_prepared_draw.h`: `record_draw` is `prepare_draw` - the keys, the pipeline, the uniform blocks in
   the ring, the textures made ready, the descriptors, the dynamic states - then `emit_draw`, the commands alone). A draw
   that writes depth and doesn't add or multiply has its depth emitted at once from that preparation with a depth-only
   pipeline made from its own key: the same vertex shader (so the same depth, `gl_Position` `invariant` besides), its
   fragment half `depth_only_fragment` (`FragmentKey::light` `kLightDepthOnly`: the stages for the alpha test and nothing
   of the surface's keys; an alpha-blended one's pixels under half opaque discarded - `kLightDepthHalf` - so a translucent
   edge isn't the surface), nothing written to the colour, the light inputs not pushed. As a modern renderer submits one
   prepared draw list to its depth prepass and its base pass, no draw is translated twice. Water render_pc marks is left
   to rt_water (kept back to the opaque world's end, as before): kept with its state block and recorded at the shading
   (its copy behind the water is made when it is drawn) - as is a draw whose preparation no longer holds (prepared in a
   submission a read-back ended mid-view: its ring blocks were that submission's). A vertex or index buffer written
   again while kept draws read it (skinned meshes written part after part) is copied for them first
   (`surface_light_buffer_rewritten`); a texture destroyed meanwhile is forgotten by them
   (`surface_light_forget_image`: its view lives to the frame's end).
2. **The inputs** (`ViewOrder::kSurfaceInputs`, 60): a compute pass copies the depth (R32F) and starts the images of the
   target's size - the visibility (RGBA16F: the sun's or the moon's visibility, -1 where nothing traced; the direct
   light's share; the indirect light's visibility; 1 where made), the lamps' light (a white diffuser's radiance), the
   lamps' light on the traced albedo, the decals (1: none) and Ray Reconstruction's guides (RGBA32UI, 0). The passes
   write into them instead of over the colour (`surface_inputs`): the sun shadows the visibility and the share
   (`SURFACE_INPUTS` variant - no camera ray for the light store any more: the visibility itself), the occlusion its
   indirect visibility (the occlusion alone, not its share - the blue channel), the lamps their light and their light on
   the albedo (two attachments, added), the decals their marks (their multiplying blend into the decals' input). The
   bounce pass adds nothing over colours there (a relit level's bounce is the store's).
3. **The shading** (`ViewOrder::kSurfaceShading`, 392 - after the lamps and the decals, before the rain's wet mask and the
   reflections): the depth is cleared and the kept draws emitted again in their order as prepared (the textures they
   sample put back in the layout their descriptors name first, if anything moved one), each with its own depth test and
   write - the prepass's depth, the nearest surface, would otherwise hide what a translucent layer drawn later lets
   through (a shirt's print). A fragment whose depth is the prepass's (within 1% of its distance -
   `light_here`: a later pass of its material; not a glass pane, a glow or a particle in front) reads the inputs:
   - lit per pixel (`kLightInputs`; the relit world - `kSurfaceRelit` - and the lit objects - `kSurfaceLit` - are lit per
     pixel in such a view whether or not rt_pbr is on: the inputs need the light model's terms apart): the sun's
     visibility in place of the vertex's (the store's `v_cel_tint.a`; an object's key light - the one light's light 0),
     the environment's light x the indirect visibility, the lamps' light on its albedo, the albedo x the decals (a
     material's edit: lit with the surface, its specular untouched);
   - a colour lit already (`kLightShare`: an unlit prop's baked colour, an animated vertex, the bake): x (the direct
     share x the visibility + the rest x the indirect visibility) x the decals, + the lamps' light on the traced albedo.
     Only on a draw that doesn't add or multiply (opaque, or blended by its alpha).
   Every other fragment keeps its own light: no pass multiplies glass, water, glows or particles any more.
4. **Ray Reconstruction's guides** (`kLightGuides`, while it denoises): the fragment the inputs were made for writes,
   once kept (after its alpha test), the material the light model was given - its albedo and specular albedo
   (`brdf_specular_albedo`), roughness and camera-space normal; a colour lit already writes its lit colour (its light
   isn't separated). The upscaler's preparation puts them into DLSS-RR's albedo, specular albedo and packed
   normal-roughness guides; where none was written (the sky, glows) the lit colour and the depth's normal stay, as
   before. DLSS-RR's inputs: diffuse albedo, specular albedo (the pre-integrated environment BRDF), normals with the
   roughness packed in their alpha (`DLSSDNormalRoughnessMode::ePacked`, the camera-space normal with the
   world-to-camera matrix given), depth and motion.

Without ray tracing, relit data, a single-sampled linear target, or with cel shading, nothing changes: the draws are made
as they come and the passes draw over the colour as before - the original's look keeps its path.

Checks: `thug2_vulkan_rt_lights_check` "round V" - a floor drawn with a blended first pass before and after the box gives
the same picture with the box's shadow (x (1 - the direct share) in it); glass over the shadow keeps its own light; a
glow adds its light whole; a decal (the decals' input x 0.5) is lit with its surface in sun and shadow; the albedo guide
is the floor's own under the glass too. `thug2_ff_shader_check --light` compiles every key's prepass, shares, inputs and
guides variants.

What it costs and what remains: see the round's record. Open: the replay draws the world's full overdraw (the prepass's
depth can't be kept for it - translucent layers drawn with depth); per-pixel lamps on translucent surfaces (only the
surface the inputs were made for has them); a colour lit already has no albedo of its own for the guides; the
reflections pass still adds over the shaded colour (round M's M6).

### Round DB: decals and painted layers as material layers

The deferred decal's model (UE's DBuffer decals, RTX Remix's decal textures): a mark or a painted layer edits the
surface's material before it is lit, and the surface is lit once with the composite material - by its own shading,
ReSTIR GI's specular on its specular albedo, and Ray Reconstruction's guides alike. Before, a decal was an albedo factor
(x 0.3 at an oil spot's core: at dusk, under the sky alone, a soft dark patch read exactly as a shadow), and a layer the
game paints over a surface (Boston's crosswalk, 0.8 units over its asphalt) was lit by two models across its alpha.

- **Two layer inputs** (`vulkan_surface_light.h` SurfaceInputs::decals / layer, bindings 28 and 31; RGBA16F, begun as
  (0, 0, 0, 1)): albedo' = albedo x decals.a + decals.rgb, roughness' = roughness x layer.a + layer.r, F0' = F0 x
  layer.a + layer.g. The decals' pass (`vulkan_scatter.h`, LAYER) lays each mark over the last with DBuffer's blend
  (src x 1 + dst x src alpha; alpha dst x src alpha). A pigment mark writes (its colour x coverage, 1 - coverage) and
  (its kind's roughness and F0 x coverage, 1 - coverage); the wet patch is water in the surface - (0, its darkening)
  and water's roughness and F0 by its coverage, the puddles' wet mask the same coverage.
- **One rule for the marks' materials** (`scatter.h` decal_material): the atlas's rgb is the mark's own colour
  (display-encoded, as the game's textures), its alpha the coverage; crack, stain and grime the material table's
  ground, tyre and gum rubber, oil a film of motor oil (index 1.47 - F0 0.036 - levelled to roughness 0.3), wet still
  water (`rain.h`). The art composes inside a tile as layers do (coverage 1 - (1 - a1)(1 - a2), the colour's
  premultiplied sum); oil's painted "sheen" (a lighter ring) became a thin film of its own colour - its gloss is its
  material's now. Without per-surface lighting the marks still multiply the lit colour by mix(1, colour, coverage) -
  for one mark exactly the old factor.
- **Where the layers are laid** (`ff_shaders.cpp` decal_layer_onto): where the surface's albedo is final - a relit
  surface's after its tint, an object's on both its ambient material and its lit one -, before any light. The
  environment's weight (the share of the store's specular estimate the reflections pass may take over) stays the
  surface's own roughness's: the reflections pass sees the surface, not its decals. A colour lit already (no albedo
  apart from its light) takes the layer as over a white surface: x (a + rgb).
- **A layer lying on the surface** (`ff_key.h` kLayerLift, 2 units - 4 since round DECAL; `light_find` at the start of
  the fragment shader):
  a fragment lit per pixel is the surface the inputs were made for when its depth is the prepass's (within 1 % of the
  distance, as before) or when it lies within kLayerLift of that surface along its own normal - the depth is affine in
  1 / w, its slope read off the fragment's own screen derivatives, the prepass surface's 1 / w follows from its depth,
  the gap along the ray is |w_p / w - 1| x the eye distance and x |N.V| the gap along the normal. So the crosswalk's
  paint where the prepass kept the asphalt, and the asphalt under the paint the prepass kept, take the same inputs: the
  alpha blend of two surfaces lit by the same light is the light on their blended material (diffuse exactly, specular
  through F0 - linear). Ray Reconstruction's guides keep the exact test (the surface the prepass kept writes them).

Checks: `thug2_vulkan_rt_lights_check` "round DB" - a floor lit per pixel under an even environment, seen low: an oil
layer over the whole view gives the light model's result for oil alone (A (1 - E) + E at each pixel's N.V), darker than
the bare floor and lighter than a rough layer of its colour, and the albedo guide carries its colour and roughness; a
white layer painted 0.8 over the white floor with a checker alpha either side of one half is, at a 3 - 7 degree view,
the floor alone at every pixel. `thug2_scatter_check`: the kinds' materials and the atlas's colour / coverage.
Not done: ReSTIR GI's traced specular lobe is the surface's own (the layer re-weights it: exact under an even sky, a
glossy layer's sharper reflection of structure not traced); the reflections pass doesn't see the layers (it adds for
the surface's own roughness); the lamps-on input (lamps x the traced albedo, for colours lit already) is the traced
surface's - the paint is relit, so it never reads it.

### Round FL: the front layer (see-through surfaces lit per pixel)

UE5 Lumen's Front Layer Translucency (`gpu_vulkan/vulkan_front_layer.h`). Glass, soft overlays and rt_water's water write
no depth - what is behind them shows through - so the prepass has no pixel for them and they kept their vertices' light
store (the bake's light: no per-pixel lamps, no traced sun shadow). Lumen's answer: the nearest see-through surface of
each pixel - the front layer - gets a G-buffer of its own and is lit like an opaque surface; the layers behind it keep
their own light.

- **Its depth** (ViewOrder::kFrontLayer = 391: the inputs made, before the shading): the kept draws that read the layer
  (`kLightFront`: see-through - alpha-blended, no depth written - and lit by the light model, the relit world's or an
  object's; and rt_water's water, prepared as its plain rest surface - no copy behind it made then) drawn into a depth
  of their own (`vulkan_surface_light.h` surface_light_front_prepass; the view's depth format, cleared, each draw
  testing and writing, from its own preparation with its depth-only pipeline). A pixel has a front layer where that
  depth is in front of the prepass's and not within 1 % of its distance (an overlay lying on its wall is the wall's -
  round DB's kLayerLift lights it as the wall).
- **Its light** (compute, ray queries): the point from the layer's depth, its normal from the layer's own neighbours (the
  nearer along the view on each axis - a pane or the water's rest surface is flat), and the light list ReSTIR GI used
  this frame (`FrameLightList` - the disc, the sky's grid, the emitting triangles, the lamps; one list, one power):
  - the rest of the list by the shared `light_sample` (2 draws a pixel, one light and one ray each) with the disc's picks
    left out (`LL_NO_DISC` - the estimate is of the others), x cos / pi: a white diffuser's radiance, the lamps input's
    unit; + the radiance cache's bounce (SHaRC - every further bounce); pre-exposed;
  - the disc (sun or moon): its visibility toward a point of its cone (`ray_light`, the clouds' shadow map) - the shading
    puts the disc's light through the light model with it, as the shadows pass's visibility on an opaque surface - and
    its light on a white diffuser there (for the water);
  - accumulated over frames (Ray Reconstruction doesn't denoise its transparency layer, where see-through surfaces are
    drawn - DLSS-RR guide 3.4.10): the point through the last view's camera, the layer there within 2 % of the distance
    + 1 unit, a moving average of at most 16 frames, the history scaled by the pre-exposure's change.
- **Its use** (`ff_shaders.cpp` kLightFrontGlsl; `water_shader.cpp`): a fragment of a kLightFront key at the layer's
  depth (and not the inputs' surface - that one's own light wins) takes the layer's light in place of its light store's:
  the disc's visibility for its sun term, the rest as the lamps input's light on its albedo, the store's environment
  without weight (as where ReSTIR GI lit a surface). Its environment specular stays the reflections pass's (the in-place
  classes: glass, water - one owner). The water finds the layer along the view within its waves' reach (2 Hs + 1 unit:
  the layer holds the rest surface); its body's colour is the texture's mean lit by the layer's light (the disc's and the
  rest's), its glitter shown where the disc's traced visibility is.
- **The reflections pass**: painted glass's own colour under its reflection (the base layer) is lit by the light list in
  a view ReSTIR GI lit (`list_hit_light`), not by the light store - the same light as the front layer's.
- **The light inputs as a descriptor set of their own**: the push-descriptor set of the fixed-function draws was at the
  device's limit (32 bindings with ray queries). The view-wide inputs - the round V / DB inputs and the front layer's -
  are set 1 (`ff_uniforms.h` kLightInputsSet, 11 bindings), one descriptor set a view from a pool per frame slot (reset
  when the slot's frame comes round), bound once with vkCmdBindDescriptorSets: set 0's push descriptors hold the per-draw
  ones (24 now).
- **One owner per light**: on the layer the list's lights arrive through this pass alone (the light store's K, B and
  lamps are not read there). Only in a view ReSTIR GI lit; elsewhere - the fallback tier, no ray tracing, a view not lit
  per surface - nothing changes (the layer is cleared at the inputs' begin; its draws find no match). The fallback tier's
  reflections still take the lamps pass's lit set (`RtLitLamps`, its lamp owner there) - its only reader.

Check: `thug2_vulkan_rt_lights_check` "round FL" - a night scene under one point lamp with ReSTIR DI and GI: the glass
pane (the front layer) gets the lamp model's light (`rt_lights.h` lamp_geometry on a white diffuser) as the opaque floor's
lamps input (ReSTIR DI's) does; with indirect_light passes no layer is lit.
Not done: the layers behind the front one keep their own light (Lumen's too); light from behind a pane (transmitted
diffuse) is not the layer's; the lamps' specular highlight on glass is the reflections pass's emitters only (analytic
lamps without a surface give none there).

**Round FRONT: the particles are see-through surfaces of the front layer.** The particle systems' alpha-blended quads
(smoke, mist, a fountain's spray) were drawn after the world's passes, as they came, in the game's colour: the level's
light as a whole on every particle (`day_clock.cpp`, the light energy audit's kParticlesWhole - one value for the level,
no place). At night that value is 0: Barcelona's fountain mist was black and alpha-blended over the scaffold net and the
building behind it (the scene's top-left cells 0.02 - 0.12 of the path-traced reference). The path tracer meets the same
quads as surfaces by their coverage, lit where they are. Now (`render_pc/draw_frame.cpp`): the particles are drawn inside
the world's passes, before the effects are told the opaque world is drawn (`rt::set_particles_drawing` marks them), so the
per-surface lighting keeps them with its other see-through draws; `surface_light_finish` gives such a draw - a colour lit
already (kLightShare), blended by its alpha, writing no depth - kLightFront:
- its depth goes into the front layer where it covers at all (its prepass discards alpha under 1 / 255: a quad's clear
  corners would hide the glass or water behind them from the layer);
- its colour is the program's under the game's neutral light (v0 0.5 - the material's, as the per-pixel lighting's albedo
  and the path tracer's surface are; the vertex colour's own tint is not the albedo, as everywhere since round ALBEDO);
- its light is the front layer's at its depth or in front of it (`light_front_find_behind`: the nearest see-through
  surface's light for the whole of a cloud of particles - Lumen's front layer lights only the nearest; the layers behind
  it take its light rather than none), the disc's and the rest's on a white diffuser (`ff_shaders.cpp` share_front);
  without a layer there, the game's light on it (its vertex colour doubled, as the bake's).
Open: at Barcelona's fountain the particles cover far more in the raster than in the rays - the mist hides ~99 % of the
building behind it in the raster, ~30 % in the path tracer (with the particles left out of both, the facade agrees within
0.6 - 0.9). A quad's alpha is the same formula in both (the particles' program, kTextureVertexX4: 2 x the vertex alpha x
the texel's; the rays' material_drawn_alpha), so it is which quads each has (rt_apart's capture against the draws) - to
trace.

### Round DECAL: the game's overlay cards laid into their surfaces (DBuffer decals)

Boston's walls carry the art's painted overlays - tree shadows, grime and stain cards: 4- and 10-vertex quads 2.7 - 3.3
units off their brick, alpha-blended (blend 5), alpha-tested, **no depth written**. Such a draw was a transparency (round
RG: blended by its alpha without writing depth): lit apart - its own light, the front layer's or the wall's raw inputs -
and drawn into Ray Reconstruction's transparency layer, which RR composites over the denoised picture but never
denoises (DLSS-RR guide 3.4.10). The bounce light's raw noise on those cards (ReSTIR GI's diffuse and specular, the
lamps') therefore reached the screen as coloured specks over the cards' footprints - most of each card is fully opaque
(58 % of one card's texels at 255) - while the rest of the wall was clean. Measured at the user's spot (the night wall
right of the planter): every alpha-blended draw left out - specks 20.7 -> 0 a frame; the cards drawn in the colour instead
of the transparency layer - 0; the guides and the prepass kept as they were with the cards not drawn by the prepass
(they never were: no depth) - no change. So not the guides alternating (round FA's guess), the transparency layer.

The standard (UE's DBuffer decals; RTX Remix's decal textures): a mark lying on a surface edits that surface's material
before it is lit, and the surface is lit once with the composite. Round DB built the inputs for the procedural marks
(`SurfaceInputs::decals / layer`); now the game's own overlays use them:

- **Which**: a kept draw of the relit world lit per pixel, blended by its alpha, writing no depth, without an
  environment-mapped stage (the game's painted reflections - no material) - `surface_light_finish` adds
  `FragmentKey::light`'s `kLightLayer`. Objects' overlays (a character's prints, hair cards) are not laid.
- **Where**: per fragment, where it lies on the surface the inputs were made for - round DB's layer test (`light_find`,
  the distance along the fragment's own normal from the prepass's surface) with `kLayerLift` now 4 units (was 2: the
  cards stand 2.7 - 3.3 off). Elsewhere (a card hanging off an edge, a sign in the air) it is drawn as before.
- **Laid** (`vulkan_surface_light.cpp` `lay_layers`, at the shading's start - after the decals' pass): each such draw is
  emitted from its own preparation with its laying variant (`kLightLayerWrite`: the material the shading would light -
  a relit surface's albedo after its tint, roughness, F0 - written as (albedo x a, 1 - a) and (roughness x a, F0 x a, 0,
  1 - a)) into a pass of the two inputs alone (`PipelineKey::dbuffer`: DBuffer's blend, each layer over the last, the
  game's order). The variant reads none of the inputs it writes.
- **Shaded**: the surface under it reads the composite material as it reads a procedural mark's - its colour, ReSTIR GI's
  specular on the composite's specular albedo, the lamps input on the composite's diffuse, and Ray Reconstruction's
  guides written from it. The card's own shaded draw (colour and transparency layer alike) discards the laid fragments.
- **The rays agree** (`vulkan_rt_scene.h`): a camera ray's surface (`trace_drawn`) takes the solid surface behind a
  blended world triangle lying on it (within `RAY_LAYER_LIFT` = kLayerLift along its normal, facing as it does -
  `RAY_LAYER_FACING`, ~25 degrees) unless the pixel's depth is the blended one's (a layer that writes depth: the
  crosswalk's paint, kept by the prepass); and a light ray leaving a surface (`ray_light_leaving`: ReSTIR DI's
  visibility, ReSTIR GI's and the light list's samples, the sun shadows' and the lamps pass's rays) passes the blended
  world layers lying on that surface - before, the card shaded its own wall by its alpha (darkened twice). A card
  standing across a surface still shades it.

Measured (1280 x 720, DLSS Quality + RR, `measure on`; speck_count over the band, `out/scripts/peaks.ts` - isolated
points a frame, for the pans - a pixel's own median means nothing while the picture moves): the user's spot, held 28.6
-> 0 flickering specks, deviation 1.46 -> 0.46; pan (0.15 degrees a frame) isolated specks 5.6 -> 2.6 (median 5 -> 1).
Round FA's 12-56-57 spot: the wall held 67.5 -> 0.05, deviation 2.19 -> 0.32, the column 22 -> 0.1; pan isolated specks
10.5 -> 0.2. The cards' brightness against the plain
wall (path-traced reference, 4096 samples): reference 1.29 / 0.80 (two cards), before 0.81 / 0.60, after 1.24 - 1.35 /
1.05 - the cards were too dark (lit apart, their wall shaded by them); now within the reference's noise for one, a
quarter over it for the other.

Check: `thug2_vulkan_rt_lights_check` "round DECAL" - a card 3.3 units over the floor, alpha-blended without depth, half
alpha: the floor's colour equals the floor with the card's layer set in the decals' inputs, the albedo guide is the
composite (floor x (1 - a) + card x a), nothing of the card in the transparency layer (all three fail without the
laying). `thug2_ff_shader_check --light` compiles the relit keys' layer and laying variants.

Not done: the path tracer's reference still meets the cards by their coverage as surfaces of their own (3.3 units off -
the same light); a ray's hit on the wall (a bounce, a reflection) sees the bare wall, the card apart (Lumen's surface
cache captures decals; ours would need the card in the hit's material); the ambient occlusion's rays still count a card
over their surface; a draw recorded again (its preparation lost mid-view) discards where it lies without being laid.

### Round UNITS: one physical unit for every level's fog and lamps

Lighting is global: every level's light, emission and fog goes through one conversion, derived from what the original
renderer did with each value (Unreal's and Frostbite's physical units: candela / lumens for lights, cd/m^2 for
emission, extinction per metre with a scattering albedo for media, EV100 for the camera). The census of every level
through it: `node tools/game/light_units.ts census --game <folder>`.

| input | the original's use | before | after (unit) |
|---|---|---|---|
| fog distances | SetFogDistance(start): a linear blend to the fog colour from start to end = 4 start (start + 7200 with distance clipping) - at the end a thing is gone | a medium of extinction 1 / (end - d) per unit (the game's f exactly, but a medium that moves with the camera) | an even medium of Koschmieder's extinction 3.912 / (end - start) per world unit (the end is its visibility at 2 % contrast - rt_sky.h's rule) from `start` on - Unreal's height fog's Start Distance (rt_volume_light.h `level_fog_extinction`) |
| fog colour | the light a fully fogged pixel shows | the medium's light: colour x (0.5 + 0.5 (sun + lamps)), taken as display-referred and decoded with the 2.2 power at the composite | not an input: the medium is the weather's aerosol (mist and haze droplets - its albedo `aerosol_albedo`, its phase), lit in the global unit x pre-exposure by the light's irradiance x phase / 4 pi, the lamps' estimate / 4, the open sky (half its light on a level surface); linear radiance. The game's colour stays the look of a level without physical light (no relit data, no clock) |
| a level without fog | - | a 100-metre linear "haze" (kFogHazeEnd) lit as above | the weather's street air alone (`rt_fog_air`), with or without a sun (kFogHazeEnd is only the grid's reach) |
| a lamp's reach | (light nodes) the outer radius; (found emitters) 12 cores, 200 - 1500 units | the window (1 - (d/r)^4)^2 to that reach: the light still 5 lux there (median; up to 126 lux - BO's 223 lamps all over 0.1 lux) | where its illuminance falls to kLampReachLux = 10^-3 lux (rt_lights.h `lamp_reach`: sqrt(I / E), I from the light at the lamp x its core's area, or core x half length for a line) - Frostbite's influence radius from a threshold; median 540 m |
| a lamp head's surface | (the bake) every small triangle within the luminous radius + the lamp's size | Barcelona's facade glow (a 400 lm line) took 1096 vertices of the facade and benches at 870 cd/m^2 | the candidates nearest the lamp until their area is its luminous area flux / (pi L): the head emits the lamp's flux, no more (relit_bake.cpp `emitting_vertices`; needs a rebake) |
| a lamp's own surface (round NOBIAS) | (the bake) a self-lit or textured candidate's triangles | every corner of them marked: the store's per-vertex emission blended over every triangle a corner touches - New Orleans' wall light 1010 (its lamp drawn in a facade's texture) lit the facade's white trim 11 m away at street level; the 381 lamps' surfaces sent 27 x their flux in the light list; `pt ref` at the street spot was 80 % that trim (realtime / reference 0.096) | a vertex shows its lamp only where every triangle round it is that lamp's own (the heads' rule), and a lamp's luminance is its flux over pi x the area its marks draw (each triangle's area x lit share x the share of its corners marked: `drawn_luminances`) - a surface sends exactly its lamp's flux; a lamp left with no surface is its analytic light (one owner). Needs a rebake |
| an emitter's faces (round EMIT) | culled (one-sided) materials draw their front alone, two-sided ones both | every emitting triangle sent from both faces in every ray pass, the light list measuring 2 pi A L: New Orleans' emitting triangles 1.7 x the flux of the lamps they show (2.4 x before NOBIAS's fix) | a one-sided emitter sends from its front alone ((b - a) x (c - a) x rt_scene.h kFrontFaceSign, +1: measured on every test level's one-sided floors - `rt: the world's winding` in the log), a two-sided one (rt::kTwoSided, the material's no-culling flag) from both with the bake's luminance split between them: GLSL emitter_sides / emitter_cos read by hit_surface (every ray hit), the light list's scan (sides x pi A L), ReSTIR DI's light points and the presampled pool / light grid (one-sided marked in the pool's emission sign bit), so the fog's lights too. The bake's drawn area counts the faces (RelitMesh::two_sided). Needs a rebake (v21) |
| a lamp's flux (round EMIT) | (the bake) a solve held to its kind's flux range; a glow's row of fittings pi L A over its card's core up to its fittings' brightest | New Orleans' sign 51 000 lm (5.97 m^2 - 2 700 cd/m^2 on a kind of at most 1 000), a 39 m glow row 150 000 lm, School's rows 58 000 - 81 000 lm; tiny surfaces holding a whole lamp's flux (School 4 x 10^6, New Orleans 3.3 x 10^6 cd/m^2) | a solve whose flux is over pi x its kind's brightest luminance x its drawn luminous area (an upper bound of the lamp's own) is a failed measurement - its model; a glow row's flux at most its fittings' typical (one fixture's x the count its length holds) and at most its core at its kind's luminance; a surface owns its lamp only when its drawn area can send the flux at a luminance its kind has (else the lamp's analytic light alone) |
| a lamp surface's luminance (round EMIT2) | (the bake) the surface emits its store's luminance x the colour as drawn (the linear texel) on the texels that emit | L set as though the texel were white: the emitting surfaces sent 0.21 (BO) - 0.66 (AU) of their lamps' flux (round BOUNCE's lumen census) | L = flux / (pi x the drawn area weighted by the drawn colour's mean on the emitting texels, relative to the lamp's colour - relit_bake.cpp emitting_vertices' `emitted`, rt_record::emission_texels; relit data v22): the solved flux sent, the texture's pattern kept; a surface drawing no emitting texel leaves its lamp analytic |
| a lamp surface's emission at distance (round SUBPIX) | (both renderers) the lamp-texel test on the texel filtered at the raster's mip / the ray cone's level | a footprint holding lamp texels among darker ones emitted nothing: the flux the bake conserves at level 0 lost at distance and in ReSTIR GI's and the reflections' filtered hits | the mesh's emission map (rt_emission_map.h: the test at full resolution, each mip the mean of the emission), read by the draws, the rays, the light list and the bake; relit data v23 |
| a lamp smaller than a pixel seen directly (round SUBPIX) | (the raster) one jittered sample a pixel | lit in some jitter phases only: Ray Reconstruction drops it as a firefly (0.43 of a 1-pixel emitter's light kept, 0.02 of one lit one frame in four) | its flux splatted into RR's transparency layer every frame as a half-pixel point at its true place, the draws keeping the share the split leaves them (emission_splat_share: whole up to 2 drawn pixels, none from 8): 1.00 kept |

The glowing balls round street lamps were the 2.2 power: a lamp's pre-exposed light in the air (tens to thousands at
night, the pre-exposure ~6 000) raised to its 2.2th power. In linear radiance the same lamps in Australia's clear night
air show no halo (its fog starts 200 m out); in a mist (THUG2's night fog: visibility 610 m, 66 x a clear night's
extinction) a street lamp's halo is about as bright as the ground it lights - what Koschmieder's mist does physically.
Compromises (as shipping engines make them): single scattering with the sky as an ambient term (Unreal's / Frostbite's
volumetric fog), the fog-free range before `start` (Unreal's Start Distance), a lamp's light ended at a threshold
(Frostbite's influence radius).

Round FW narrowed what counts as physical and what counts as a medium:
- **Physical light is the relit world's** (current relit data). The clock alone isn't: over a level whose relit data is
  out of date the surfaces are drawn with the original's light and the lamps are the found emitters at their display
  brightness. Before, the clock made such a level "physical", and those lamps x the night's pre-exposure (5 000 - 30 000)
  lit the street air of New Orleans into orange, green and magenta fog. Now its air keeps the game's fog colour as its
  own light (the unit its surfaces are drawn in) and no lamp lights it: a light with no physical value has no place in
  a physical medium. The full rebake makes such a level physical.
- **Under the clock the time of day's fog is no medium.** The fog the clock hands the draws is the original's time of
  day states' (each level's TODManager_SetParams or tod_manager.qb's defaults - night 8 000 - 32 000 units): a linear
  blend into a dark colour that stood for the night's darkness and the day's haze. The clock has the physical sky,
  exposure and air for those, so the air there is the weather's alone (`rt_fog_air`, the atmosphere's with the sky
  model) - as in a level without fog - and the game's fog stays out of the draws. Made a medium, every night was a 610 m
  mist (66 x a clear night's extinction). A level the clock doesn't light (`tod=level`) keeps its own fog as the medium
  above.

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

- **The textures' level of detail** (round GRIT): the draws of the view being upscaled add log2(drawn / output width) to
  their samplers' bias (`upscale_texture_lod_bias`, `with_lod_bias` - the game's stage textures, their surface maps and
  the detail materials; -0.58 at Quality, 0 at DLAA - NVIDIA's DLSS Programming Guide's mip bias; one rule for FSR too).
  Before, the textures were sampled for the drawn pixels: a level blurrier than the output can show, and Ray
  Reconstruction's albedo guide with them. BO noon against `pt ref`: the fine band 0.660 -> 0.704 of the reference's,
  the 15 x 15 local contrast 0.952 -> 0.982.

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

### One light model: the BRDF and the material table (round M)

The per-pixel lighting (`rt_pbr`), the characters and - next - every ray hit are shaded by one model,
`engine/gfx/rt_brdf.h` (C++ and its GLSL twin, `brdf_glsl()`): it takes a light and a material and gives the radiance
toward the eye; which light reaches a point is the caller's. Lights are given as the radiance of a white diffuser they
light face-on (the frame's unit).

- **The model**: GGX D, the height-correlated Smith visibility, Schlick's Fresnel to 1 at grazing; multiple scattering
  compensated by 1 + F0 (1 / E_ss - 1), E_ss from a 16 x 16 table of the split-sum terms integrated from the lobe itself
  (`thug2_brdf_check --table`); Lambert x (1 - E) - a white furnace returns 1 (metal 0.995 - 1.011, dielectric + diffuse
  0.998 - 1.020 over roughness 0.045 - 1 and N.V 0.05 - 1). Skin: the energy-conserving wrap (N.L + w) / (1 + w)^2 per
  channel, w = the mean free path over the surface's radius of curvature (from the pixel's derivatives). Cloth: the
  Charlie sheen with Neubelt and Pettineo's visibility over a base held to 1 - the sheen. Hair: Marschner's R, TT and
  TRT lobes in Karis's real-time form, keratin's index, the fibre's colour as its absorption.
- **The material table** (`engine/gfx/rt_material_table.h`): per kind (painted, asphalt, concretes, brick, stone,
  metal, wood, ground, grass, tile, plaster, gravel, glass, water, skin, cloth, denim, leather, rubber, hair) the
  refractive index (F0) or a metal's measured F0, the roughness (the detail library's CC0 scans' roughness maps,
  measured), the albedo range of real samples, the mean free path, the sheen, the light and air transmission (for the
  weather round) and the relief depth in millimetres - each with its source. A draw's kind is its surface map's class,
  or the detail library's kind where it has one.
- **The surfaces** (`ff_shaders.cpp` surface_finish): the draw's program makes the albedo under white light (v0 = 0.5),
  decoded; the relit world's draws read the light store's words (sky + bounce, tint and the sun's visibility, lamps -
  `kSurfaceRelit`) and get the sun through `brdf_direct` with the bent normal and the rest as an environment through
  `brdf_ambient`, its irradiance on the bent normal that of an even hemisphere round the surface's normal; the bake
  without a store is the environment's light; objects and characters their lights decoded. The result is linear light,
  not clamped. The environment's specular is the store's estimate where the reflections pass doesn't trace it (texels
  rougher than `kReflectionRoughest`), faded across the same range as the pass's own fade.
- **One albedo definition** (round ALBEDO): every pass - the raster's surfaces and Ray Reconstruction's albedo guide,
  every ray's `hit_surface` (ReSTIR DI / GI, `pt ref`, the light list, the front layer, the reflections) - takes a
  surface's albedo as its texture's reflectance: the texel x the material's own pass colours (0.5 leaves the texture
  as it is), the layers composed as the game composes them, (round MATALB: not x the relit store's tint - below). Never x the game's colour on the
  geometry - a sector's time-of-day colour or an object's light where it stands (`Gpu::takes_pass_tint`: the Vulkan
  device gets the material's own colours and that tint apart; a surface lit per pixel takes the tint as light where
  its light is the original's own - the vertex's, the draw's lights -, the other draws scale the colours as the
  original) - and never x the draw's diffuse colour Cd (0.5, the game's neutral under its doubled light, made an
  object 0.22 of its texture). New Orleans' night colour (0.2 - 0.29 on every pass colour) had darkened its raster
  albedo to 0.2 - 0.3 of the rays'. (Round BAKEALB) The relit bake reads the same definition: its bounce B brings each
  hit's light x the albedo of the texel the ray lands on (the ray probe's `hit_albedo`), so the fit's unit makes the
  tint the part of the albedo the texture doesn't hold (stored a byte x 64, up to ~4); an albedo (texel x tint) is
  held to 1 in every pass. A camera ray's surface (`camera_surface`: ReSTIR's shared primary) samples each pass at the
  raster's mip (ray differentials from the neighbouring pixels, the upscaler's level bias, the anisotropy) and lays the
  blended world layer lying on it over its material as the raster's DBuffer does.
- **A surface's reflectance from its texture and material alone** (round MATALB, after BAKEALB): the albedo is the
  texel x the material's colour, calibrated to its kind's measured reflectance - never the relit store's tint. The tint
  is the original's vertex colours over the bake's light model; one fitted unit a level can't tell a surface's colour
  from the art's local fill light in it (BAKEALB's tints put New Orleans' brick at 0.61 - 0.67 and Training's concrete
  floor at 0.085), and a census of the test levels' vertex colours (`tools/pbr_maps/albedo_census.ts`) found no colour
  in them that light doesn't explain as well. So: the light store's tint word holds 1 (rt_relit.h `kRelitStoreTint`; the
  bake still makes the tint and the data keeps it, reported), its lamps' word is L alone, and the relit bounce's hits
  bring the light they receive x the hit's material albedo (`relit_received` x the ray probe's `hit_albedo`). The
  calibration (rt_material_table.h `material_albedo_scale`): the texture's texels' mean albedo (linear luminance of its
  opaque texels - `tools/pbr_maps` measures it, the .pbr entry's byte 19, sRGB-encoded; 0 not measured) inside its
  kind's measured range (the table's `albedo`: LBNL, Oke 1987 ...) is the albedo as painted; outside, the texture is
  brought to the range's nearest end, its texels' spread kept - the base-colour validation PBR pipelines do (Lagarde &
  de Rousiers 2014's albedo range check; Substance's PBR Validate), applied. The kind is the surface map's class (the
  detail library's where it lists the texture); metals, glass and water have no diffuse range (as painted). One factor
  a material, read in one place by each side: the draws' `SurfaceUniforms::albedo` (surface_finish's `surface_albedo`)
  and the rays' `MaterialRecord::albedo_scale` (`material_albedo` - hit_surface, the probe, the reflections, the
  classic lamps and effects passes). A lamp's emission takes the colour as drawn. The relit bake reads the maps
  (`rt_pbr` on in its settings) so its bounce's albedo is the game's. Relit data v20.
- **Retired**: rt_pbr_specular, rt_pbr_normal_strength, rt_relief_strength, rt_detail_strength, rt_characters_skin /
  _cloth / _hair ("Set by the material now"); the bake relit by luma thresholds (kRtPbrAmbientShare, kRtPbrShade*,
  kRtPbrDetail*, kRtPbrSkyDetail, kRtPbrGameSun); the hue-found skin texels; the relief's per-kind weight and shadow
  strength; the per-kind gloss table of the reflections.
- **Not done** (round M's record): a per-texel material mask in the character maps (a garment's
  skin texels are cloth now); skin's translucency needs a thickness; the lamps' specular on the relit world (the store
  has no directions); a directional sky term (SH-L1 / bent normals) instead of the even hemisphere; real displacement
  in place of parallax for ray visibility.

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
  at `world_drawn`, which take the blades with the rest; never in the BLAS). (Round W2: the blades are lit by the one
  light - the light store's window, their own traced sun ray, the canopy's gaps, the leaf's material through rt_brdf.h -
  and no longer read the frame's colour; the copy, the depth match and the colours' learning below are history - see
  "Round W2".) Until round W2 the blades took the shaded ground's colour at their roots. The first try ran the pass before the
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

### Rain (`rain`)

`[Graphics] rain` is the weather's rain as one physical input - its rate in millimetres of water an hour on level ground
(Options: Light 1, Moderate 4, Heavy 12, Downpour 40 - the American Meteorological Society's classes; 0 off). Everything
else follows from it in one state, `engine/gfx/rain.h`, which every consumer reads:

| From the rate | The model | Where it is read |
|---|---|---|
| the drops' sizes | Marshall and Palmer (1948): N(D) = 8000 e^(-Lambda D) m^-3 mm^-1, Lambda = 4.1 R^-0.21, 0.1 - 6 mm | the drops' compute pass (each drop's size from the distribution's inverse) |
| their fall speeds | Atlas, Srivastava and Sekhon (1973): v(D) = 9.65 - 10.3 e^(-0.6 D) m/s (within 2% of Gunn and Kinzer's measurements) | the drops, the fall's slant |
| how many | the integral of N (about 1,900 a cubic metre at 4 mm/h, 560 of them 0.5 mm and over); N v the impacts a square metre a second | the drops' weights, the splashes, the rings |
| the air | geometric optics (extinction efficiency 2): 2 x pi/4 the integral of D^2 N - 8.7e-4 /m at 4 mm/h (4.5 km of visibility), 2.25 km at 12 | `rt_volume_light.h precipitation_extinction` - the volumetric fog's march (the game's own scripted rain taken as moderate) |
| the slant | the weather's wind (`wind.h`, `rt_wind`) brought from 10 m down to the drops' 2 m by the neutral log profile over a town (z0 0.5 m), against the median-volume drop's speed | the occlusion map's lines of fall, the drops' drift (with the gusts) |
| the sky | rain falls from a deck covering the sky (nimbostratus): coverage 1 | the sky pass's clouds (`vulkan_rt_sky_pass.cpp`: the larger of the level's and the weather's) |
| the ground's water | stored up to a paved surface's depression storage (1 mm) at the rate while it rains, evaporating at 0.3 mm/h after; a level entered while it rains starts wet | the wet ground (the world's draws, the wet mask) |

**Where the rain reaches** - the precipitation occlusion map (`engine/gfx/precip_occlusion.h`, `gpu_vulkan/vulkan_precip.h`):
a shared module (the rain now; the snow's settling and the weather surfaces later - the first piece of
`renderer-hack-audit`'s "exposure from physics"). Rain falls along one direction d; seen along d the level is a height
field - for each line of fall the height where it first meets the level or a cut-out caster (TLAS mask 0x05). A point is
reached where it lies at or above that height on its line: nothing under roofs, overhangs or indoors; a wall's lee is in
its rain shadow when the wind slants the rain. The lines are numbered where they cross y = 0 (uv = P.xz + slant (P.y));
two cascades of 1024 x 1024 texels round the camera's line (3 and 12 units a texel: +-39 m, +-156 m), RG32UI (the first
hit's height, the line's tag). The map is kept on the device frame to frame - the level is static: a compute pass traces
only the lines new to the window as the camera moves, 1/16 of the others each frame (a level loaded, something moved:
every line again within 16 frames), and every line when the slant moves (a new generation). `precip_reached` (its GLSL
twin) blends the four texels round a point's line. Without ray tracing the map is made untraced: everything is reached.
The check traces a roof and a wall on the device and compares the map with the processor's reference.

**The wet ground** (the world's draws: `kTrailWet`, ff_shaders.cpp `wet_block`, `rain::surface_glsl`): on the same draws
and programs as the snow's cover (render_pc marks the ground and scene objects). Per pixel: whether the rain reaches it
(the map), how hard it strikes it (its face against d over level ground's), and the water there (`rain_wetness`):
- the pores filled (the stored water over 0.2 mm), x the surface's porosity - from its roughness until the material table
  gives it (`rain::porosity_of`, the seam Round M's table fills);
- a film: Nusselt's steady film draining a plane under the rain's supply, h = (3 mu q L / rho g sin)^1/3 (0.07 mm on a
  wall at 4 mm/h, 0.19 mm on a 5% slope), the stored water on level ground; whole from 0.1 mm;
- puddles: the ground's small relief (a smooth noise of 3 mm deviation over 1.2 m - the pavement's unevenness) under the
  stored water's level - the level solved on the processor from the relief's own measured distribution so the mean depth
  over the relief is the stored water (20% of level ground at 0.3 mm stored, 45% at 1 mm), lowered on slopes by their fall.
On level ground the film drains over the relief's own slopes (its RMS slope, 0.010: a 0.32 mm film at 4 mm/h between the
puddles); the water lies flat - a mirror - in the puddles and where the film rises over the surface's texture (a mean
texture depth of 0.7 mm, ISO 13473-1): `flat_share`, what the wet mask and the roughness take.
The material then changes (`rain::wet_material`, its GLSL twin): the albedo under water darkens by Lekner and Dorf (1988)
- light trapped by total internal reflection in the water returns to the surface and is absorbed again: a' = (1 - Re) a
(1 - ri) / (1 - a ri), Re 0.066 the water's hemispherical reflectance from the air, ri 0.475 from inside (1 - (1 - Re)/n^2)
- by the share the water holds (pores x porosity, film, puddle), in linear light; the roughness goes to still water's
(0.05) and its F0 to water's where the water lies flat; a puddle's normal is the water's with the drops' rings on it. The energy of the wet
surface is at most 1 (the check: exactly 1 for a white one). The seam on the one light model:
`rain::wet_brdf_material(rt::brdf::Material, porosity, Wetness)` changes rt_brdf.h's material inputs (the check runs a
white furnace through `brdf::specular_albedo` and the diffuse coupling over albedos, roughnesses, wetness and view angles:
at most 1); the world shader applies its twin: stage 0's texel darkened, then the per-pixel lighting's BrdfMaterial takes
the wet roughness and its F0 goes to water's by the flat share.

**The wet mask** (opaque_drawn, `rt::ViewOrder::kRain` 395 - after the decals, before the reflections): a full-screen pass
puts the same `rain_wetness` (the depth back to the world, the normal from its neighbours) into an R8 mask - the film's or
the puddle's share, the decals' puddles over it; the ray-traced reflections' trace reads it as the decals' (`rain_wet_mask`
before `scatter_wet_mask`): a wet surface reflects as water (Fresnel from F0 0.02). Single-sampled targets (the
reflections' own rule).

**The drops** (world_drawn, the same order: before the fog and the tone mapping): 131,072 drops in a 12 x 12 x 8 m box
round the camera and 65,536 in a 40 x 40 x 20 m one (the near box's region left out of it), each a drop of 0.5 mm and over
(smaller ones are the air's extinction alone) of the distribution, falling at its own speed and drifting downwind with
the gusts, wrapped into its box in the world's frame (the camera moves through the rain). Each drawn drop stands for the
true number in its box over those drawn (5 near, 70 far at 4 mm/h). A compute pass places and lights each:
- reached by the map, else hidden;
- its light, in the frame's linear unit: what lies behind it - a water sphere refracts its surroundings over about 165
  degrees (Garg and Nayar 2006): the sky above the horizon (its mean radiance, sky_light) and the scene below it by the
  share of that cone above the horizon (0.5 + 0.504 sin of its axis's elevation), the scene's part from the frame blurred
  to about 32 texels across (a mip chain blitted from the colour target); plus the sun's or moon's light it scatters (`rt::DayLight::sun_light`, a ray to the light through the
  scene - shadowed under a roof's edge) and the view's 16 most important lamps (the lamps pass's choice, their falloff,
  unshadowed), each by Henyey-Greenstein with a water sphere's asymmetry 0.87 (Hansen and Travis) - L = E p / 4 pi, a
  quarter of the light per unit of N.L: bright backlit and under a street lamp at night, faint against a lit background.
Drawn as a streak: its move on the screen (its own fall and drift and the camera's, the frame before's view) over the
shutter's share of the frame (the snow's camera - one shutter, one aperture), widened by the defocus, its coverage spread
over the streak, times the drops it stands for. With the upscaler's motion: the reactive mask, and a second draw writes
its own motion where it covers enough of a pixel (the snow's round-4 fix: no streaks stuck to the screen).

**The splashes**: 16,384 impacts on whatever the rain meets within 10 m (the map's first hit along a random line), each a
crown growing to 8 drop radii over 50 ms, the impacts of drops of 1 mm and over at their true rate (each drawn standing for
its share), lit as the drops.

**The rings** (`rain::ripple_glsl`): a drop striking water sends out a ring of capillary waves at about their slowest speed
(0.23 m/s) and dies as it reaches half a cell (at most 0.4 s - so a point needs only its four nearest cells); drawn on
three jittered grids whose cells give back the rate of drops of 1 mm and over (600 a square metre a second at 4 mm/h:
5.2-unit cells, 0.29 s rings): on puddles (the world shader's normal) and on water bodies (`water_shader.cpp`, x the
body's openness to the sky - a puddle under a roof stays still).

The cost: see the record's numbers (the occlusion map, the wet mask, the drops and splashes timed separately - the log's
"vulkan: rain: the passes take").

Not yet physical (the honest list): (the rain's deck dims the sun and the sky through round S's cloud model since then);
the lamps' light on the drops is unshadowed and point-like; a drop's environment is the frame (what is off-screen above it
is missing); a drawn drop stands for several (variance, not bias); the splashes' crown is a sprite; water running in from
elsewhere and the geometry's own hollows don't feed the puddles (the relief alone); the wet albedo applies to the texel, not
texel x the relit tint (the per-pixel BRDF fixes it); porosity is the roughness's until the material table; the near
drops' extinction is counted twice over the first metres (the fog's medium starts at the eye); the occlusion map treats
cut-outs (trees) as solid; the RT reflections use the triangle's normal (no rings in them).

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
  foot, fading up, drips) - rgb the mark's own colour, alpha its coverage faded to the cell's border (round DB: the marks
  are material layers - "Round DB" under the ray tracing); and five
  meshes of 48 vertices (a curled leaf, a crumpled scrap of paper, a can lying on its side, a weed of seven blades and four
  leaves, a pebble), each vertex with its own shade.
- **The decals' pass** (`ViewOrder::kDecals` 390, at opaque_drawn: after the shadows, occlusion, bounce and lamps,
  before the reflections, the grass and the fog): the tiles in view (the frustum alone), their triangles drawn into
  the colour alone; each fragment reads the depth under it and is kept only where the scene there is the decal's own
  (within 0.3% of its distance + 1 unit - so nothing shows on the skater standing on a mark, through a wall, or from a
  decal whose collision floats over the drawn ground; a wall standing in a decal may take its foot's pixel row); the
  colour is multiplied - mix(1, texel rgb, alpha x opacity) - so a mark darkens the lit ground as its own colour
  would: in shadow, in light, under a lamp. With four samples the fragment reads sample 0's depth.
  **No distance fade, no cut-off** (since the sky-decals round, after the user's floor whose cracks appeared only near
  the camera: each decal faded out from 60% to all of 48 x its larger side and none were drawn past 3,200 units - a
  crack 40 units long was gone 1,900 units off, popping in as the skater came near): a decal is drawn wherever it is
  seen, and a far one is resolved as any texture is - by its atlas's mips. Each mip is the alpha-weighted mean of the
  2 x 2 above it (rgb the mean of rgb x alpha over the mean alpha), so a far mark is the mean of the light it takes away
  over its texels (a far crack a faint darkening of its patch, not a line that shimmers); a cell never mixes with
  another's (cells are 256, a power of two, and the fragment keeps its coordinate half a texel of the mip it reads -
  `textureQueryLod` - inside its cell); the sampler is anisotropic as the game's textures are (`[Graphics] anisotropy`,
  the device's most at most). The tiles are culled by the view's frustum only. Clutter is unchanged (items are 3D:
  below).
- **Puddles** (with `rt_reflections`, single-sampled targets): the decal pass also draws a wet mask (R8, the colour's
  size, cleared over the viewport) - each wet patch's darkening as its wetness, the most of any; the reflection pass
  (ViewOrder 400) reads it through a trace variant made from the trace's own text with the puddle lines put in (so the
  trace without decals is the same text and the same SPIR-V as before): where a world surface lies under a puddle, its
  reflectivity is the wetness (when more than its own) with water's Fresnel - `rt::kReflectWaterF0` 2% face-on, a mirror
  at grazing angles - and no environment mask. A frame whose only reflective things are puddles runs the reflection
  pass too.
- **The clutter's pass** (`ViewOrder::kClutter` 445: after the reflections, before the grass): (round W2) a compute
  pass traces each chosen item's sun ray; then each item drawn instanced, 48 vertices, depth-tested and written (and the
  upscaler's motion), each vertex lit by the one light as a thin sheet of the item's material (see "Round W2": the light
  store's window, the traced sun, rt_brdf.h) - a pebble the gravel's albedo range; the rest their own colours (leaves'
  autumn colours, paper, a can's label, a weed's green) as albedo; a can's body bare metal. (Until round W2 the colour
  under each root was copied from the frame and the items took the ground's luminance over 0.4 x the mesh's shade.) Leaves and weeds sway downwind with the grass's two waves. Distance (the clutter-LOD round): the game's
  own rule for its small objects - drawn within an object's default lod_dist2 (1,500 units) x `lod_distance_scale`, every
  item at its full size (no thinning by rank, no shrinking, no per-view budget); an item under half a pixel tall is left
  out (sub-pixel, no pop).
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
  looks like water's - not warm (red over blue by 0.04 - 0.12: lava, foliage, sunlight pools). (Round LW: the near-white
  and near-black tests are gone - a shadow or a light pool is never a water material by its blend, rt_lights.h
  blend_shows_surface, and Barcelona's fountain pool, EDA1D05C, a dark pebbled water texture, was left a flat sheet.)
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
- **1. Waves** (rewritten by the physical waves - "Waves from the wind" below: each body's own wind sea). Four
  Gerstner waves about the wind's way (wavelengths 1, 0.63, 0.41, 0.27 x the body's spectral peak, each with the
  energy of its band of the body's JONSWAP spectrum, crests sharpened with Q = 0.6, their speeds the dispersion's at the
  body's depth with capillarity), each faded out where a pixel spans 5 - 20% of its wavelength; and two layers of a
  tiling ripple texture drifting along the wind, as steep as the spectrum's shorter waves over the texture's band (none
  on still water). The texture (256 x 256, RGBA16F: slope x, slope z, height, the foam's pattern) is made on the device at
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
- **5. Foam** where the waves break - water shallower than the body's significant wave height / 0.78 (McCowan; the
  screen-space depth difference: a beach, the waterline round anything standing in the water) - and whitecaps on the
  sharpened crests in the share of the surface the wind whitens (Monahan and O'Muircheartaigh 1980, 3.84e-6 U^3.41, at
  full strength from 1%), in the ripples' drifting foam pattern, x `rt_water_foam`; white at the surface's light. Still
  water has none.
- **6. The sun's glitter.** Normalised Blinn-Phong (power 1500, x 190) of the scene's sun (`rt::scene_sun`, else the
  sun the game shows) on the wave normals, Fresnel at the half vector, where the bake says the surface is sunlit (the
  vertex colour's luma 0.3 - 0.6); at most 6 with `rt_hdr` (the bloom takes it up), 1 without.
- **Alpha.** Where the game drew any water (alpha above a quarter) the written alpha is 1 - what is behind is in the
  colour already; where it drew none it stays as it was. Stacked water sheets of different materials each replace what
  is under them, so the upper one's edge shows (a seam in NO's river).
- **Levels** (water_materials.cpp's table, by the scene's name): how clear the water is - AU 900 units, BA 600, BE 220,
  NO 180, SE 450, other levels 450 (material data, still per level). The waves, swell lengths and wind headings that
  table held per level are gone (the physical waves).
- **Waves from the wind** (added by the physical waves, engine/gfx/water_waves.h, water_bodies.h, render_pc/
  water_bodies.h). Every water body's waves are its wind sea's: the weather's wind (`rt_wind`, metres a second 10 m up;
  auto: the air's - clear 5, hazy 3, misty 1.5; engine/gfx/wind.h, along its one direction) x how open the body is to
  the sky, its fetch along the wind and its depth go through Young and Verhagen's finite-depth growth law (1996: in deep
  water over a short fetch JONSWAP's - the check holds it within 15% of Hasselmann et al.'s height over 1 - 20 km -,
  fully developed Pierson-Moskowitz's 0.24 U^2 / g, never higher than 0.78 of the depth; no waves below 1 m/s, Kahma and
  Donelan). The bodies are measured once a level's water meshes load (render_pc notes each water material's triangles,
  measured at its first water draw): the level's water laid on a grid (up to 1024 cells a side) with the collision seen
  from above; a body is the water's surface joined cell to cell at one height (a sea drawn as many tiles is one body - no
  seams; a mesh well under another, a sea floor's caustics, another); its fetch walked upwind to where its water ends -
  at anything (a shore, a wall, a puddle's floor) or at nothing (the level's world stops: the sea goes on, a fully
  developed sea); its depth to the highest collision face under it; its openness the share of the sky's hemisphere seen
  past the collision's highest points within 50 m (0 under a face that covers it). A puddle under a roof sees no wind
  and is still; a 2-cm puddle in the open gets millimetre ripples; BA's sea in 3 m/s a 0.22 m swell 8 m long. Meshes
  drawn as an instance's (their triangles in a model's space) and meshes too small to cover a grid cell are drawn still.
  Rain on the water makes no rings yet (nothing drew them before either).
- **Settings:** `rt_water`, `rt_wind`, `rt_water_clarity`, `rt_water_foam`, `rt_water_tint`,
  `rt_water_screen_reflections`, `rt_water_geometry` (below) - all live; `rt_water_waves`, `rt_water_wind` and
  `rt_water_wind_heading` retired by the physical waves (the Options screen's Surfaces tab, the
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
- **Round LW: the traced mirror.** Where the front layer (below, "Round FL") is the water's rest surface and faces up,
  its pass traces the view's ray reflected about the rest normal: the hit lit as ReSTIR GI's secondary vertices and the
  reflections pass's hits are (its emission, one light-list sample through its BSDF with its own candidates, the disc by
  its own ray, its reflectance x the radiance cache's bounce), else the sky grid's light; pre-exposed, averaged over at
  most 8 frames through the mirror image's point (as far behind the surface along the view as the hit is in front of it).
  The water's shading takes it in place of the screen's march, moved on the screen by twice the waves' tilt x the hit's
  distance (the image's alpha). The screen march stays where the layer isn't the water (another see-through surface in
  front of it). Rays and the path tracer meet every water material (rt::kReflectWater - render_pc gives the class to the
  water materials whether they have an environment pass or not) as a Fresnel interface: water's F0 (the material
  table's), its texture's coverage its own colour, the rest seen through (`pt ref`: the whole surface reflects by Fresnel
  first, then coverage or through - water_shader.cpp's model; a thin interface: no bending, no absorption on the path).
- **Not done:** ray-traced reflection rays from the water (with ray tracing the reflection is still the screen's and the
  sky's - a ray query in the water's shader would need the TLAS, the geometry table and the material set bound to the
  draws); the water's own geometry moves only with `rt_water_geometry` (below); stacked sheets are each shaded; the copy is
  made once per view, so transparent things drawn between it and a later water draw aren't seen through that water.

### Moving water (`rt_water_geometry`)

With `rt_water=1` and `rt_water_geometry=1` (the default) the water's surface itself moves: the swell's four Gerstner
waves that shade it also lift and shift its vertices, so the horizon of open water is no longer a straight line and the
water rises and falls along walls and posts. Still water (a body with no wind sea - water_waves.h) draws the
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
- **How high** (changed by the physical waves): each body's wind sea - BA's sea in the hazy weather's 3 m/s a
  significant height of 0.22 m (8.7 units) over an 8 m peak wavelength, a sheltered puddle none; the slope cap still
  holds the sum below breaking.
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
   published, the level's own scripts for the state it set have ended (`tod_manager.h` `tod_transition_running`: the
   night geometry and fake lights they show over their frames are in the world) and the level is relit for it (every
   world mesh written, the field made) - read from that state each frame, no count of frames - at most 10 s. A mesh
   shown later is drawn with the light field's ratio over its bake until its own light is written
   (`rt_relit_world.h` `unlit_world_colour`).
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

## The reference path tracer (`pt ref`)

The yardstick for every realtime light pass: an unbiased, progressive path tracer over the frame's own acceleration
structures, material table and light store (gpu_vulkan/vulkan_path_reference.h; its sampling engine/gfx/rt_path.h with a
CPU twin). With the game paused, the console's `pt ref <samples> [EV100] [even] [analytic] [open] [only <kinds>]` accumulates that many paths a pixel from the
camera as it stood (another camera or scene starts it again), shows their mean in the frame at a manual exposure
(vulkan_hdr.h `set_manual_ev100`: the given EV100, else the meter's when it began; the EV100 is logged) and saves
`Screenshots\pt_<stamp>_reference.exr/.png` and `_realtime.exr/.png` - the EXRs linear in the global unit, the PNGs
through the tone curve at that EV. `pt off` ends it.

- One hit surface for every ray (vulkan_rt_scene.h `hit_surface_glsl`): position, triangle and shading normals, base
  colour (round MATALB: the material's albedo - its texel x its colour, calibrated to its kind's measured range - for every surface alike), the light model's material, alpha and cut-out, and the light
  store's emission in the global unit.
- A path: camera rays jittered in the pixel; at each hit next-event estimation to one light of the run's light list
  (below); the BSDF sampled (GGX visible normals + cosine Lambert); MIS by the power heuristic, a BSDF ray that meets a
  light weighed by the pdf the list gives that light and direction; Russian roulette from the third bounce; cut-outs by
  their alpha on every ray.
- One light list with power (engine/gfx/rt_light_list.h; path tracing stage 2), made once a run - its lighting frozen with
  its camera, another lighting cut or a lamp switched starting it again: the sun's or the moon's disc (the lighting's
  direct light, its real solid angle, x the clouds' shadow map where the clouds draw one), the sky (a 1024 x 512 grid
  over the sphere - the sky-view table x the drawn clouds' transmittance + their light, the same march as the drawn
  clouds, else x the clouds' kept share; without the table an even sky - sampled by its luminance with two alias tables,
  the grid also what a ray that leaves the world sees), every emitting triangle (2 pi x area x its emission's mean
  luminance at 64 points, hit_surface's own emission; measured on the device in the run's first frame - round PT4b: one
  whose lamp texels all fall between those points at the most it can send, its corners' emission over its area, so no
  emitting triangle is left out) and the
  analytic lamps (flux from the lamp model's photometry: a point 4 pi^2 I core^2, a panel 2 pi^2 I core^2, a line
  pi^3 I core h) with the frame record's point lights where the level's lamps aren't solved. One alias table picks a
  light: the distant group (disc and sky) and the local group evenly, each light by its power within its group (pbrt-v4's
  split of infinite lights against local ones; one table by flux alone needs the distant lights' flux through the
  scene's cross-section, and the level's size then decides between the sun and the lamps). `pt ref <samples> [EV100]
  even` picks as stage 1 did (the kinds evenly, each kind's lights evenly) - for comparisons.
- One owner per lamp: a lamp whose own surface emits (the relit data's emitting vertices - rt_lights.h
  `LevelEmitters::surfaced`) is sampled as that surface, its analytic light left out (rt_path.h `analytic_lamps`).
- Diagnostics (the night's light, decomposed): `pt ref <samples> ... only disc,sky,emit,lamps` counts those kinds of
  light alone (their next-event samples and what the BSDF rays meet of them; the paths unchanged - the runs of each kind
  add up to the whole); `analytic` makes every lamp its analytic light and counts no surface's emission (the other
  owner); `open` lets the light's rays pass the blended surfaces as the realtime tier's rays do; (round LG) `nee` /
  `bsdf` count the light by next-event estimation alone / by the BSDF's rays alone (MIS off) - each is the whole on its
  own, so a light the list misses (or a sample its next-event estimation can't reach) shows as their difference; (round
  MB) `bounces <n>` cuts the paths at n bounces (`direct` is `bounces 1`; `bounces 2` is what a one-bounce indirect light
  reaches - its difference from the whole is the further bounces' share). The light list's log
  line gives the owned lamps' model flux beside the listed triangles' measured power. `rt probe` prints the light store's
  terms in the unit (%g), each nearby vertex's light as drawn and its lamp links (lamp, weight, share of L).
- **The realtime copy before the camera's exposure is set by hand** (round BOUNCE). The run's EV100 (given, or the
  meter's) is set as the camera's exposure only once the realtime frame has been copied, and a run started again by a
  lighting change keeps its copy. Before, the exposure was set when the run started and the copy taken a frame or more
  later: the frame's pre-exposure had already jumped to the hand-set EV, while Ray Reconstruction's history (it doesn't
  rescale its history to a new pre-exposure) still held the frames before it - the copy divided by the new pre-exposure.
  With an EV100 far from the meter's the whole-scene ratio was wrong by up to 2 x: Boston's bank at night (metered EV100
  2.82) 0.515 of the reference with `pt ref 256 0.89`, 0.968 metered - the same reference; after the change 0.960 with
  0.89. The rounds MATALB and EMIT measured with EV100 0.89 at every spot: their "bounce light half the reference's"
  (BO, BA at night) and "Australia's pool 1.11 x" were this, not the light.
- **The procedure** (`tools/game/scene_bias.ts`, round BOUNCE): NO's street, BO p1, BA's fountain, AU's lamp pool and
  Training (the user's spot) at noon and at night - the level loaded at its time of day, the skater teleported, the
  camera held, `console measure on`, 500 frames, `pt ref 256` at the meter's EV100; the log's mean-luma ratio and a
  trimmed one (pixels over 20 x the reference's 99th percentile left out of both). `scene_bias.ts terms <spot>` takes a
  spot apart term by term (each debug view's 8 frames averaged, the references' `direct`, `bounces 2`, `nee`, `bsdf`,
  `only emit bounces 0`...); `terms-analyse` prints the table.
- What the reference showed by night in Boston (relit data v14, full moon, EV100 fixed): the frame 3.6 - 29 x the
  reference, varying from one load to the next. Two causes on the realtime side: (1) the meshes met after the
  lighting's cut (the night state's) kept the bake's afternoon bounce B (~0.1 in the unit), and the bounce passes
  spread it - fixed (rt_relit.h `relit_vertex_from_data`: under the clock's lighting a vertex from the data starts with
  no bounce), the frame now 3.4 x, the same every load; (2) the blended world meshes drawn opaque (BO's brick
  facades: blend 5, opaque texels, flagged seen-through) are no occluder for any realtime or bake ray (mask 0x80) - the
  relit bake's lamp links light them through the buildings, and the per-pixel passes skip their pixels; with `open` the
  reference agrees (0.92 x). Not fixed yet: one visibility for every ray (the reference's seen_through) and their
  classification by drawn coverage, then a rebake.
- Checked by thug2_vulkan_path_check: the white furnace at every roughness, a floor under an emitting disc against its
  integral, MIS / lights-only / BSDF-only agreeing, the processor's twin, the owner rule, no NaN; the light list on the
  processor (the alias table's picks against its weights, the groups' shares, each lamp shape's flux against its light
  integrated over a sphere, the sky's pdf integrating to 1 and its samples falling as luminance x solid angle says) and
  on the device (by power against the kinds evenly: the same mean; under 32 lamps of very different power, less noise).

### Everything drawn in the acceleration structure (path tracing, stage 3)

- **A world that stands still.** A reference's run key is every instance's mesh, material, flags and transform and the
  record's `poses_key` (the bones each skinned pose was written with): anything that moves starts it again. `pt ref`
  turns a live pause (the world running behind the pause menu - game/front_end/live_pause.h) into the original's
  freeze, as the level editor does; before, the cars drove on through the sums (the car absent, a green smear where it
  had been) - the run key left the objects out.
- **Skinned meshes in their instance's pose of the frame** (engine/gfx/rt_poses.h, render_pc/rt_record.cpp
  `pose_id`): a mesh skinned on the processor is skinned again at the record, with its instance's bones (the draw's
  code: `MeshRef::skin_for_rays`, the draw's palette), into a vertex buffer of its own per (instance, mesh) - dynamic,
  registered as a dynamic mesh with the drawn mesh's indices and layout, refitted each frame. The meshes' own buffers
  (the last draw's pose, a frame old, shared by every instance) are never registered. A pose not asked for in a frame
  is released. `mesh_drawn` matches a draw with its instance's pose (the contract check: 0 violations).
- **Blended surfaces traced by what their material draws** (vulkan_rt_scene.h `hit_blend`, from the first pass's
  blend mode, alpha and class - never per object): an alpha blend (5, 6, or a blended pass that replaces what is behind
  it) is a surface over its coverage - hit by chance as its alpha there (the walls drawn with a blended first pass and
  opaque texels are solid); glass (reflection class glass, seen through) is its painted coverage plus a thin pane:
  Fresnel reflection as a mirror (the material table's glass F0), the rest straight through; a multiply (7, 8, 15) is a
  filter on the light behind it; an add / subtract / brighten (1 - 4, 9, 10: glows, light cards, grime) lets light
  through and adds its own store emission. Shadow rays (sun, sky, lamps, emitters) take the expected light through
  them (`seen_through`). The path tracer's rays meet the seen-through instances (mask 0x80) and those that add light
  (0x40); since "Blended surfaces in every ray" (below) every light ray takes the same visibility.
- **The alpha a cut-out is tested with** is the one the game draws and tests (vulkan_rt_materials.h
  `material_drawn_alpha`: the texel's alpha, or the vertex alpha x it doubled - never the pass's fixed alpha, which is 0
  on nearly every material). Before, an alpha-tested material with a reference above 1 was cut away entirely on every
  ray that takes cut-outs (a whole stone wall in BO): this changes the realtime passes too - such surfaces (fences,
  foliage, walls drawn alpha-tested) now cast and occlude by their texels - and the relit bake's rays.
- The BLASes are built with `VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR`: the light through blended surfaces
  is a product over the candidates a ray meets.
- Particles, billboards and clutter: round PG ("What is drawn apart, in the rays" below); grass and decals stay out by design.
  Not yet: objects' baked colours (an
  object drawn with its own colours - BO's lamp globes, lit yellow by their baked colours - is white and unlit in the
  reference: its colours are neither tint nor emission until objects are de-baked like the world).

### Blended surfaces in every ray (round PT-B)

- **One class of each surface for every ray** (engine/gfx/rt_surface_class.h, decided in render_pc/rt_record.cpp
  `add_instance` from the material's own data and the mesh's vertex alpha - never per object or level): opaque; cut-out
  (an alpha test cuts its holes); **blended opaque** - drawn blended, but its drawn alpha is 1 everywhere, so nothing
  behind it ever shows: blend 5 whose least drawn alpha is 1 - the game's pixel shader draws 2 x vertex alpha x texel
  alpha held to 1 (the texel's alone with pass flag 0x1000), so min(1, 2 x the mesh's least vertex alpha x the texels'
  least alpha) (`Material::least_texel_alpha`, read by the backend from the texture's first level as the device samples
  it: `RayTracing::texture_alpha`, vulkan_formats.h `level_alpha` - A8R8G8B8 / A1R5G5B5 / DXT1 / 3 / 5 decoded, the X
  formats, R5G6B5 and P8 opaque), blend 6 with fixed alpha 1, a blended pass that replaces what is behind (0, 11 - 13);
  **seen through** - blended with real alpha, glass, a multiply (7, 8, 15); **adds light** - 1 - 4, 9, 10 (glows,
  light cards, grime). The coverage rule is hit_blend's: the class says where its coverage is 1 everywhere.
- A blended-opaque instance is an opaque instance (mask 0x01 / 0x02, forced opaque, casts): every ray - the sun's
  shadows, the lamps', occlusion, bounce, reflections, fog, the relit bake's sun / sky / lamp / bounce rays, the path
  tracer's - meets it as any wall, and the per-pixel passes find its pixels (the lamps pass lights it per pixel:
  `lit_per_pixel`). Before, Boston's brick facades and road markings (blend 5, the transparent flag, opaque texels)
  were mask 0x80: no ray met them - the bake's lamp links lit them through the buildings and they were lit per vertex.
- A seen-through instance stays mask 0x80; an instance that adds light gets its own mask 0x40 (no light ray asks for
  it - light passes it whole; a path's rays and the camera's `rt probe` do). **Every light ray's visibility is one
  function** (vulkan_rt_scene.h `ray_light`): nothing solid before its end (`ray_blocked`, the cut-outs by their
  alpha), times what the seen-through surfaces of its kinds (world / objects, by the ray's mask) let through -
  hit_blend_transmittance multiplied over each one crossed (`seen_through`). The sun's shadows, the lamps pass, the
  occlusion, the bounce light's sun rays, the fog's light rays and the path tracer's light rays call it; a pass that
  keeps one visibility a ray takes the mean of its three channels (`ray_light_share`) - a filter's tint is kept only
  by the bounce light's rays and the path tracer. The bounce light's rays find their hit as before (the solid
  surfaces) and take its light (or the sky's) times what the seen-through surfaces before it let through.
- (Round PERF3) **One inlined `ray_light` a code path.** GLSL has no calls: each call site is a copy of the two walks and
  the blended surfaces' candidate code. With `[debug] frame_profile` the ray-traced passes log their compiled shaders'
  statistics (`shader stats:` - VK_KHR_pipeline_executable_properties: registers, code size, local memory); they showed
  ReSTIR GI's samples and cache passes at 255 registers (8 warps an SM) with 390 - 420 KB of code and the reflections'
  at 1.1 MB, and an experiment that skipped the seen-through walk's candidates at run time with the code kept cost the
  same as with them - the code's size and registers were the cost, not the work (~5 ms over the frame). So
  `light_sample` takes each kind's light and reach (disc, sky, local) and traces one ray at its end; the reflections'
  rough cone and mirror share one loop; the temporal reuses' two exclusive rays and the spatial reuse's chosen ray and
  Z rays (DI) share one call. A new caller keeps to one call site a path (or a loop around it). Merging the two walks
  into one query was measured slower (a blocked ray then evaluates the blended candidates met before its blocker).
- **The relit bake** (rt::ProbeRequest with `cutouts`): the probe returns the same share (`ProbeResult::seen_through`
  - the mean of the channels, before the hit or the ray's end); the sun's and the sky's open rays count by it, a
  lamp's link weight is multiplied by it, a bounce ray's hit colour too. Relit data v15 (relit_data.h).
- Still binary: the fog's air map (a distance to the first thing hit per texel - a map of distances holds no share:
  the seen-through surfaces don't shadow the shafts there; the fog's own light rays take them), and the reflections'
  rays (a view ray, not a light ray: they show the closest solid surface - the seen-through ones are not in reflections,
  as before).

### ReSTIR DI: the lamps' light in real time (path tracing, stage 4)

The first real-time stage of the path tracer (gpu_vulkan/vulkan_restir_di.h; `[Graphics] direct_light`). In a view lit
per surface (round V: ray tracing, a relit level, the linear frame, single-sampled, no cel), with `rt_lights` on and
`direct_light` `restir` or `auto` (round GRIT: with or without Ray Reconstruction) - ReSTIR DI (Bitterli et al. 2020)
makes the lamps' inputs (`lamps`: a white diffuser's radiance in the frame's pre-exposed unit; `lamps_on`: the same on
the traced albedo) in place of the lamps pass, which then draws nothing in the view (it still chooses the view's lamps:
the fog scatters them). The shading is unchanged: it stays the one consumer. (Round BR) The same chosen sample through
the surface's specular lobe (its traced F0 and roughness), over its specular albedo E, goes into the specular input
(binding 30; ReSTIR GI adds the environment's after): the lights' specular is this pass's as their diffuse light is,
sampled on the light - before, it was ReSTIR GI's, an emitter's face met by a reflected ray (sparkle). The depth's own
surface (no material known) gets none. thug2_vulkan_restir_check: a glossy floor (roughness 0.2), diffuse + E x the
specular input against the reference's direct light, -0.30 %.

- **The lights** are the light list's local group (vulkan_light_list.h, shared with `pt ref`): every emitting triangle the
  shared scan lists (hit_surface's `emitted`, its power measured at 64 points; round PT4b: every triangle with an area
  that emits at a corner - one whose few lamp texels the 64 points miss at the most it can send, its corners' emission
  over its area: Boston had 19 such, their lamps' light lost, for a lamp with a surface is its surface's alone) and
  every analytic lamp (the level's lamps
  without a surface of their own - one owner - and the frame record's point lights where the level's lamps aren't
  solved), picked by power from an alias table. No view cap, no "most important N": the lamps pass's 64-of-103 cut is
  gone. The analytic lamps are uploaded as they are each frame (a moving or switching lamp keeps its place: dark lamps
  stay in the list at intensity 0); the triangles are scanned again when the world's instances, the lighting cut or the
  emission the light store holds change (round PX - `rt_lights.h` store_emission: told by the relit world once every
  mesh's store holds a changed emission, not when the lamps' switches are asked for: keyed by those, Training's list was
  scanned before any store was written and stayed empty - its 34 surface-owned lamps lit nothing) - the scan's list is copied to host memory in the frame and read once that frame
  has run (no wait; the old list meanwhile, none across a world change). The alias table is rebuilt on the processor
  when the powers change.
- **The sun's or the moon's disc and the sky are not in the reservoir** (separate terms, their owners unchanged): the
  disc is one light whose only unknown is its visibility - the shadows pass's ray(s) toward it are already its estimator
  and resampling has nothing to choose; in a reservoir it would take nearly every sample by day and leave the lamps
  noisier. The sky is the light store's environment term (K x the sky, the occlusion per pixel) in the shading's
  ambient slot together with the bounce - in a ReSTIR GI view (stage 5, below) sampled with its visibility instead.
- **Per pixel** (two compute passes with ray queries, after the inputs are begun - ViewOrder::kLights - 1):
  1. The surface: the prepass's depth gives the point, a camera ray its triangle (hit_surface: the point on the
     triangle, its normal and the shading normal, the albedo); where the ray doesn't end at the depth's point (a
     seen-through surface drawn in the prepass) the pixel gets no lamps, as the lamps pass gave none. Kept for the next
     frame (position, both normals as octahedral words, the albedo).
  2. 8 candidates (16 before round PERF - RTXDI's default is 8) from the alias table (a point uniform on a triangle, a lamp's own point - rt_lights.h's lamp model),
     resampled toward the target p^ = the luminance of the light a white diffuser there would get from it, unshadowed -
     a triangle's own emission at the sample (vulkan_rt_scene.h `hit_emission`: the store's emission x the texel, on a
     lamp drawn in a texture its lamp texels alone). The BRDF is left out: the shading puts the light on its own
     material. (Round PT4b: the target was the triangle's mean radiance - its listed power / 2 pi area - and the exact
     emission only where shaded. On a lamp drawn in a texture, light on 1 texel in 10 - 100, a sample between the texels
     kept its weight and brought nothing, one on them brought 1 / their share of the mean; the reuse kept and spread both
     for frames, and Ray Reconstruction took the black and white blotches for the picture - Boston's walls blown white
     with hard edges, speckled pools. The check's lamp drawn in a texture: per-frame relative error 5.5 -> 0.86. Cost:
     the emission read at every candidate - BO at night 1.1 - 1.3 -> 2.0 - 2.6 ms at 1280x720.)
     (Round PT4c: the candidates come from a **light grid** round the camera - ReGIR, Boksansky et al. 2021: 32 x 16 x 32
     cells of 128 units, each with 64 slots, each slot one light resampled from 8 of the list's by power toward the most
     that light can give the cell (widened by half a cell: a pixel's point is moved by up to half a cell before its
     cell is found) - a triangle's flux / 2 pi d^2 up to pi x its mean radiance, a lamp its model with every cosine 1,
     0 beyond its reach; kept with its unbiased contribution weight. A pixel's candidates are slots of its cell,
     weighted by its target x the slot's W (generalised RIS, Lin et al. 2022): every light a pixel can get light from has
     a bound above 0 there, so unbiased; outside the grid the list by power. Before, a level's far lamps took nearly
     every candidate - Boston's courtyard lit by 1 candidate in tens, the reservoirs finding and losing its lamps over
     seconds. The check's 4 near lamps among 120 far: each pixel's variation over the frames 2.5 x lower with the
     candidates alone and with the temporal reuse. Cost +0.3 ms at 1280x720.)
     (Round PERF3: only the cells read lately are made. Every reader - ReSTIR DI's pixels, the light list's readers at
     secondary vertices and froxels - marks the cell it reads in a state word a cell (after the grid's picks: the frame
     it was made in, the frame it was last wanted); the grid makes a cell while it was wanted within 16 grid frames; a
     reader whose cell wasn't made this frame takes the list by power (DI) or the pool (the list's readers) - unbiased,
     fewer near candidates for the frame the grid takes to make it. The cells keep their place in the world: each axis's
     index is the world cell's modulo the cells across, so a cell keeps its index and its marks while the grid follows
     the camera. All 16384 cells x 512 slots were made every frame before: BO at night 2.2 -> 0.41 ms.)
  3. One ray (vulkan_rt_scene.h `ray_light`: cut-outs by their alpha, seen-through surfaces by their transmittance) to
     the chosen light; one it doesn't reach is dropped (W = 0, M kept: visibility reuse).
  4. Temporal: the point taken back through last frame's camera to last frame's reservoir there, kept when that
     surface's normal agrees (cos >= 0.9) and both planes are within 2 % of the distance; its M capped at 20 x 16.
  5. Spatial: the reservoir and those of 2 neighbours within 24 pixels passing the same tests, resampled toward this
     pixel's target; a ray to the chosen light; W = sum / (Z p^) with Z the M of every reservoir that could have made it
     (its surface's target and a ray from it - Bitterli et al.'s unbiased combination; the same in the temporal step,
     with a ray from last frame's surface). The result - the light x what gets through x W - goes into the inputs; the
     reservoir is the next frame's history.
- **Bias, stated**: none for a world that stands still (the check: within 0.1 % of the reference). Where it moves, last
  frame's reservoirs were made against last frame's occluders and a moved lamp's last-frame target is evaluated at its
  new place: a frame's worth. Light only by the list - which since round PT4b holds every emitting triangle of the
  world (an object's emission isn't in the store: its lamps, e.g. Boston's globe street lamps drawn in their vertex
  colours, are no light anywhere until objects are relit - the plan's P10). The lamps' specular is not in the inputs (as
  before - the shading puts the lamps on the albedo; stage 5's glossy lobe). The skater's and objects' pixels take the
  world's shadows only (as the lamps pass).
- **Ray Reconstruction**: the noisy lamps are in the shaded colour RR takes; its guides are round V's (albedo, specular
  albedo, normal-roughness from the surfaces' shading) - the light inputs need no guide of their own. No specular hit
  distance is needed for diffuse direct light.
- Not taken by ReSTIR: `rt_lights_strength`, `_shadowed`, `_max`, `_emissive`, `_level` (the lamps pass's knobs; ReSTIR
  lights by the lights' own power). With MSAA, cel shading or a level without relit data, the lamps pass lights.
- Check: thug2_vulkan_restir_check (the reference's direct light within 2 % for a still scene and for a camera swapped
  between two views every frame; a moving lamp against the exact light; the per-frame error against the light list
  alone; round PT4b: a lamp drawn in a texture - every triangle listed, the mean within 2 %, the per-frame error low;
  no NaN).

### ReSTIR GI: the indirect light in real time (path tracing, stage 5)

The second real-time stage of the path tracer (gpu_vulkan/vulkan_restir_gi.h; `[Graphics] indirect_light`). In a view
lit per surface, with `indirect_light` `restir` or `auto` (round GRIT: with or without Ray Reconstruction) - ReSTIR GI
(Ouyang et al. 2021; the reuse with Lin et al. 2022's reconnection shift) makes the light that reaches a surface from the
sky and off the surfaces around it, and its environment's specular, in place of the light store's environment term (its
traced sky K and bounce B) and of the occlusion and reflections passes. The shading stays the one consumer.

- **One sample a pixel** (pass 1, ViewOrder::kLights + 1 - after the lamps' inputs): the surface as ReSTIR DI finds it
  (the prepass's depth, a camera ray's triangle - its normals, albedo, F0, roughness kept for the next frame), a direction
  drawn from a mix of the sky's distribution (a quarter: the light list's sky grid by its luminance) and the cosine lobe,
  its pdf the mix's, traced:
  - nothing hit: the sky that way - the grid's radiance (the disc is not in it: the shadows pass owns the sun's and the
    moon's direct light) - a sample at infinity;
  - a surface hit: its reflected light toward the pixel by next-event estimation through the **full light list** - the
    disc (its cone, the clouds' shadow map), the sky (its grid), the emitting triangles and the analytic lamps, one picked
    by power, one ray (the reference's light sampling without its MIS - nothing continues from there); not its emission
    (round BR): the light straight from an emitter, on a diffuser and in a specular lobe, is ReSTIR DI's - one owner,
    sampled on the light (found by a reflected ray, a small bright emitter's face made single bright pixels that changed
    from frame to frame - Training's sparkle under Ray Reconstruction).
  The list's local group is ReSTIR DI's (vulkan_light_list.h `restir_local_lights`: one scan, one set of lamp records);
  the sky's grid is the reference's (`sky_grid_*`, made on the device when the sun or the sky changes, read back without a
  wait and taken into use with its alias tables - the grid in use and the tables always one pair: the clouds' drift since
  is not in it).
- **The target** p^ = the luminance of what a white diffuser's lobe takes of the sample's reflected light (the lamps
  input's unit): the reservoirs carry the diffuse light alone.
- **Reuse**: temporal (the point taken back through last frame's camera, ReSTIR DI's surface tests, M at most 20) and
  spatial (2 neighbours within 24 pixels); a reused sample reconnected to this surface - its weight x the Jacobian
  (cos at the sample / d^2 here over the same at its own pixel; 1 for the sky), Bitterli et al.'s unbiased combination
  (Z the M of every reservoir whose surface could have made the chosen sample: its target there and a ray from there).
  A pixel's next-frame history is its temporal reservoir, not the spatial one: fed back, the neighbours' samples return
  through their own histories into their spatial reuse - the inputs no longer independent and the combination's weights
  no longer unbiased (the check's white floor under an even sky lost 1.5 % so; 0.4 % with the chains apart).
- **Out** (pass 2): the chosen sample traced from the pixel (`ray_light`: cut-outs, seen-through surfaces' transmittance);
  - the diffuse - its reflected light x cos / pi x W x what gets through - added into the lamps input (and on the traced
    albedo into lamps_on): the shading puts it on the surface's albedo as it puts the lamps;
  - the visibility input's indirect visibility set to 0: the store's environment - diffuse and specular, its sky and its
    bounce - has no weight where this pass lit the surface;
  - the specular, over the surface's specular albedo E (demodulated), into a new input (binding 30,
    kLightSpecularBinding): the shading adds brdf_specular_albedo x it (ff_shaders.cpp) - every surface's from one ray
    along a GGX visible normal of its lobe (pass 1: weight F G2/G1, what it meets - its reflected light - or the sky),
    added to the lights' own specular ReSTIR DI wrote there first. (Round BR: surfaces of roughness 0.3 and more took it
    from the diffuse reservoir's sample through their lobe - a sample that lands in the lobe by chance: single bright
    pixels from frame to frame on Training's walls and pillars that Ray Reconstruction passed as sparkle. Resampling for
    the diffuse light and a ray a pixel for the reflections is Kajiya's and Lumen's split. The depth's own surface - no
    material known, the camera ray missed the scene's copy - is a diffuser alone in both passes: its ray started inside
    the skater's skinned copy and outlined him in white, and E of a guessed material under the shading's own made him
    pale - green where F0 0 left E near 0.) Cost: + the specular ray and its light ray on every pixel - Training at
    853x480: 0.94 -> 1.45 ms;
  - the specular ray's hit distance (R16F) - Ray Reconstruction's specular hit distance guide
    (kBufferTypeSpecularHitDistance, the view at its rect).
- **The fallback tier** (`passes`; round GRIT: no longer `auto` without Ray Reconstruction): the store's sky and bounce with `rt_ao`; the
  reflections pass's surfaces shaded through their maps (`kReflectSurface`'s added reflection) are this pass's in a ReSTIR
  GI view (their specular is the input's), its in-place reflections - water, glass, the game's own shine - stay its own.
- **Glass and water**: seen-through surfaces aren't in the prepass - they keep their own light and reflections (water its
  rt_water path, glass the reflections pass's base layer); the bounce rays and the light rays pass them by their
  transmittance (ray_light), as every light ray.
- **Bias, stated**: none for a world that stands still beyond the 0.4 % above. Where the world or its light changes,
  reused samples carry their stored light for up to their M cap (20 frames' worth); a secondary surface's glossy
  reflection toward a neighbour is taken as toward the pixel that found it (its light is stored, ReSTIR GI's
  approximation).
- **Further bounces: a radiance cache** (round MB - NVIDIA's SHaRC, the spatially hashed radiance cache Cyberpunk 2077's
  path tracing reads where its paths end; the hash grid of Binder et al. 2019 / Gautron 2020). Before, the secondary
  vertex had its direct light alone: one bounce - interiors lit through an opening (Training) far darker than the
  reference. Chosen over more path vertices per pixel (Russian roulette at every pixel): each vertex costs two rays on
  every pixel every frame (~+2 ms at 1280x720 a bounce, over the budget at night), and the extra vertices are noise that
  Ray Reconstruction would have to remove; the cache's cost is a sparse set of paths and a hash lookup, and what it
  returns is already an average. Lumen's surface cache needs mesh cards the level has no data for; NRC a network trained
  on the device. The cache:
  - a hash table (2^19 entries) of cells of the world: a point's position quantised by a side that grows in powers of 2
    with its distance from the camera (4 units near it, beyond that ~12 pixels wide where it is seen - SHaRC's levels;
    in pixels, so every cell seen gets ~9 update samples a frame at any resolution: with a fixed 1/48 radian the room
    check at 160x120 was 10 % dark, its cells reached too seldom to keep their entries) and the axis its surface faces
    (a wall's two sides apart); open addressing, 8 linear probes, a 32-bit check word; 2^19 entries of 64 bytes (32 MB);
  - an entry holds the **bounce a white diffuser there gets** (E / pi of the light off other surfaces; the lights' own
    direct light - disc, sky, emitters, lamps - not in it: next-event estimation owns it at every vertex);
  - written by **update paths**: one pixel of each 4 x 4 block a frame (at random), 3 rays each along the cosine lobe of
    the vertex they leave; at every surface met its direct light (next_event) and its reflectance (the light model's
    directional albedo, `brdf_ambient` of an even light - diffuse and specular); back from the last vertex, the light
    into a vertex along its ray (the next vertex's direct light + its reflectance x its bounce - the next ray's light,
    past the last the cache's own) is a one-sample estimate of that vertex's bounce, added (fixed point: the exposed
    light x 2^16, rounded at random, 64-bit sums from 32-bit atomics) into its entry;
  - each entry's frame (the resolve pass): the frame's samples' mean folded into its light; the history is frames, not
    samples (SHaRC's accumulated frames): the kept light's weight (64 samples at most) fades by 1 - 1/16 a frame - the
    further bounces settle within ~4 x 16 frames (the check counts after 64); an entry not updated for 64 frames freed;
  - read at ReSTIR GI's secondary vertices (the diffuse sample and the specular ray): the light they send toward the
    pixel = their direct light + their reflectance x the cache's bounce. The paths ending in the cache make every frame
    one bounce deeper: the sum of all bounces, as the reference's unbounded path (Russian roulette) has.
  - Bias, stated: the cell's average for the point (a cell's points share one bounce; the specular lobe takes the bounce
    as an even light), the history's lag (64 samples) where light changes, the first frames after a view moves into
    cells it hasn't seen (their bounce 0 until written). The store's baked bounce B stays unused on this path.
  - Measured (round MB, 1280x720 DLAA + Ray Reconstruction; the GPU shared with a shader sweep, so absolute times high):
    the cache's passes 0.48 - 0.50 ms, the GI total +0.8 - 1.1 ms (the lookups at the secondary vertices). The
    reference's further bounces (`pt ref 128` against `pt ref 128 bounces 2`) are +0.5 % of the frame at Training's
    spot, +1.0 % at Boston's archway by day, +1.7 % by night; the realtime frame moved by about as much (0.921 -> 0.928,
    0.972 -> 0.981) - the rest of those frames' gap to the reference is not missing bounces.
- Check: thug2_vulkan_restir_gi_check - the reference's indirect light (round MB: its image of every bounce less its
  one-bounce image of the disc, the emitters and the lamps) within 2 % on a still scene (glossy; smooth within 3 %), a camera swapped
  between two views, a red wall's bleeding (red and green within 2 %), (round BOUNCE) the same wall behind a row of posts (the temporal history's samples often unseen from the pixel: within 3 % - dropping such a sample stays; keeping it, as ReSTIR DI does, measured +13.5 %), a room lit through a window in its roof (every
  bounce within 3 %; without the cache under -10 %; the camera circling - within 3 %, each frame's mean steady), a white
  floor under an even sky (energy), no NaN.

### The camera as shipping engines model it (round EX: `rt_exposure_adapt`, `rt_local_exposure`, `rt_tonemap`)
The output is what a game camera records, each step the industry's standard (vulkan_hdr.h):
- **Auto exposure: a histogram** (Unreal Engine 5's default method; Frostbite's physical camera units - Lagarde and de
  Rousiers 2014). The meter's 64 x 64 samples, on the linear light before any tone mapping, go into a centre-weighted
  (Unity HDRP's "centre weighted" metering) histogram of log2 cd/m^2 - 1/4-stop bins, each keeping its samples' mean
  place, so the average is exact. The weighted mean between the 10th and 90th percentiles of its weight (UE5's
  AutoExposureLowPercent / HighPercent defaults) is the scene's average: a few lit spots or a black corner don't move
  the exposure. It is put at middle grey 0.18 (UE5's target, ACES's scene-referred grey; it was ISO 12232's
  saturation-based 0.104, made for a curve that clipped at 1) times the exposure compensation curve keyed by the
  scene's luminance (UE5 has the slot; ours is Krawczyk et al.'s key over its daylight value - the same for every level),
  within the camera's EV100 range. EV100 = log2(L x 100 / 12.5) (ISO 2720). Exponential adaptation (0.4 s darker,
  1.2 s brighter): monotonic, no overshoot; the meter reads unexposed light, so nothing feeds back. The old average
  meter is retired (no setting kept: the histogram with a 0 - 100 % window is that meter).
- **Local exposure** (UE5's Lens > Local Exposure, its new-project values: highlight and shadow contrast 0.8, detail
  strength 1, blurred luminance blend 0.6, blurred kernel 50 % of the width, middle grey bias 0): a bilateral grid
  (Chen, Paris and Durand 2007) of 64-pixel tiles x 32 one-stop slices of the exposed log luminance, blurred 3 x 3 x 3,
  read trilinearly at each pixel's own level, mixed with a Gaussian of the tiles' luminance; the base's distance from
  the pivot (the key x 2^rt_exposure) scaled by the contrast. Two compute passes after the meter; the curve's pass reads
  the grid. Off while the camera is set by hand (the path tracer's comparison maps pixel by pixel).
- **The tone curve: ACES** (`rt_tonemap=2`, the default): the RRT + sRGB ODT in Stephen Hill's fit (AP1 matrices and
  the rational curve). Middle grey 0.18 -> 0.105 of white (36 % encoded, where the old key put the average), white
  reached about 6.5 stops above middle grey. Chosen over AgX (Blender / Godot), Khronos PBR Neutral (product colours
  under even light) and Gran Turismo's (one studio's) because UE5 and HDRP ship ACES and it is defined on the 0.18
  scene grey the meter targets. 0 (the shoulder) and 1 (Narkowicz) are kept.
- Checks (thug2_vulkan_hdr_check "the camera"): a uniform daylight view metered to 0.18; a night street with 12-stop
  lamp-lit patches under a tenth of the view - the window excludes them, local exposure + ACES keep them below white
  (all clip without); adaptation monotonic; local exposure leaves a uniform view at the key unchanged, keeps the log
  mean of two halves 4 stops either side, and an 8-stop edge inside a tile keeps 92 % of its step with each side's
  factor within the blurred share (0.6 x 0.2 x 8 stops) - the grid doesn't cross the edge.
- **Round NK - the compensation curve is display-referred.** Krawczyk, Myszkowski and Seidel 2005's key
  a = 1.03 - 2 / (2 + log10(L + 1)) is the key of Reinhard et al. 2002's photographic operator L / (1 + L), which shows
  the scene's log average at a / (1 + a) of white - a displayed brightness. Round EX used the ratio of the keys on the
  scene light (key = 0.18 x a / a_day); with a linear toe (the old curve 0) that is nearly the same thing, but ACES's toe
  showed a night street ~1.4 stops darker again (Boston at the user's spot: the frame's mean encoded luma 16.9 with ACES,
  42.1 with the old curve). Now the average is shown at Reinhard's a / (1 + a) over daylight's share of what the curve
  in use shows middle grey at, and the key is the exposed light the curve shows there (`shown_grey` inverted by halving
  in stops, the shader's and `meter_key`'s - one curve for every level, still UE5's compensation slot). Daylight's
  average keeps 0.18 exactly with any curve; daylight scenes 2 stops either side move under 0.06 stops. The night street
  at the user's spot: key 0.047 (was 0.014), frame mean luma 41.6 with ACES (the old curve's 42.1). The lit walls lose
  ~2 stops of headroom: the check's lamp-lit patches are now 9 stops over the street (were 12) to stay below white with
  local exposure (they show 251 - 252 of 255; ~254 without it).

### ReSTIR under Ray Reconstruction (round PT4d)

ReSTIR's estimate is unbiased per frame, but its error is correlated: a reservoir keeps its sample for up to its history's
20 frames and hands it to its neighbours. A temporal denoiser - DLSS Ray Reconstruction here - averages error that
changes from frame to frame; error that lasts for frames and covers patches it keeps as picture. Boston's courtyard at
night (DLSS Quality + RR, 1080p, still camera): each pixel's standard deviation of display luma over 90 frames 3.6 - 4.5
with ReSTIR DI + GI against 0.4 with the noiseless lamps pass. NVIDIA's RTXDI has the settings for a denoised output;
both ReSTIR passes follow them as RTXDI defines them (gpu_vulkan/vulkan_light_list.h `restir_denoiser_glsl`, in DI's
and GI's temporal passes):
- **Permutation sampling** (RTXDI's temporal resampling, `enablePermutationSampling`): the reservoir taken from last
  frame is another pixel's of the same 4 x 4 block - the block mirrored in x and y after an offset that changes every
  frame. A pixel's history is a different neighbour's chain each frame, so its error changes from frame to frame. The
  neighbour's surface passes the same tests and its sample takes the same correction (its own surface's target and ray).
- **The boiling filter** (RTXDI's, its default strength 0.2): after the temporal reuse, a reservoir whose contribution
  (target x W) is over 10 / 0.2 - 9 = 41 x the mean of its 8 x 8 group's non-zero contributions is dropped - the sample
  found by chance whose weight dwarfs its neighbours', which the reuse would keep and spread as a bright patch.
- **Bias, stated**: in DI none measurable (the check's cases unchanged within their noise; near and far lamps +0.5 %).
  In GI (its check, against the reference): permutation sampling -0.3 % (glossy floor -0.51 -> -0.82 %, smooth -0.33 ->
  -1.07 %), the boiling filter's dropped outliers -1.3 % more on the smooth floor (its rare bright glossy samples) -
  together glossy -1.0 %, smooth -2.3 %. RTXDI's stated trade.
- **The depth's own surface**: a pixel whose camera ray doesn't find the drawn surface at its depth (outlines drawn
  smoothed by the tessellation, skinned bodies, grass and litter with no ray-traced copy) is lit from the depth - its
  point, its normal from the nearer neighbours' depths, a diffuse material. Before, ReSTIR DI gave such a pixel no lamps
  and ReSTIR GI left it the light store's environment - another owner's light, far brighter at night than ReSTIR GI's:
  white halos round the archways and the skater that flickered with the jitter.
- **The prepass's own surface** (round CW): the camera ray finds what the per-surface lighting's prepass kept at the
  pixel (vulkan_rt_scene.h `trace_drawn`: the opaque and cut-out kinds by their alpha test, an alpha-blended layer where
  it is at least half opaque - the prepass's kLightDepthHalf - seen-through instances included). Before, it took the
  opaque kinds alone: a blended layer the prepass kept - Boston's crosswalk paint, 0.8 units over the asphalt - was
  missed. Where the paint's depth was too far in front of the asphalt's hit the pixel fell to the depth's own surface
  (no albedo for a colour lit already, and ReSTIR GI took the indirect light's visibility to 0 there: no indirect light
  at all); elsewhere the asphalt under the paint was the surface, its light rays shaded by the paint over it. The paint
  showed its alpha's pattern as dark blotches at dusk (thug2_vulkan_restir_gi_check case 6: the painted patch 0.0 of the
  sky's light before, 1.0 now).
- Measured (the same spot, 90 frames, the first clean series of a launch): courtyard 3.6 - 4.5 -> 1.35 - 1.7, the wall
  right of the green door 2.0 -> 0.65 - 1.0, the ornament above it 3.8 - 4.6 -> 1.3 - 1.9, the whole frame 2.5 - 5 ->
  0.8 - 1.0. With the lamps by ReSTIR DI and the indirect light by the passes, 0.39 - the lamps pass's own. What remains
  is mostly at outlines (the skater's idle animation and its skinned outline). Cost (1280x720): permutation sampling and
  the boiling filter within the runs' spread (DI 2.70 -> 2.69 ms, GI 3.14 -> 3.04 ms); the depth's own surface about
  +0.1 ms in each (the pixels it lights now get the full work).
- Check: thug2_vulkan_restir_check case 7 - near lamps among far ones, the error's frame-to-frame correlation 0.34 ->
  0.02 and what a running average (history weight 0.9, a temporal denoiser's) still varies by x 0.73, the mean within
  2 % of the exact light.

### Water, glass and the air on the path tracer's light (path tracing, stage 7 - round WF)
One light list, one way to sample it:
- `light_sample_glsl` (vulkan_light_list.h): ReSTIR GI's next-event estimation moved out - one light of the full list
  (the disc with the clouds' map, the sky's grid, the emitting triangles, the lamps) by power, one `ray_light`, the
  light along the way x its visibility over its density; a surface multiplies by its BSDF x cosine, a medium by its
  phase. GI's secondary vertices now call it (thug2_vulkan_restir_gi_check bit-for-bit as before: furnace 0.99679,
  glossy 0.26543). The radiance cache's lookups (`cache_key`, `cache_bounce`) moved with it.
- `local_light_glsl`: a local light (triangle or lamp) seen from any point - the medium's sampling and the shared
  sampler's local branch.
- The air (Volumetric fog above): the local group by RIS, in both tiers; the sun or moon stays the fog's own
  estimator of the same light (the air's sun map and its shadow rays - the medium's shadow map, as UE5 / Frostbite
  froxel fog keep the directional light's), the sky's light the fog's ambient.
- The reflections' hits (`vulkan_rt_reflections.h`): in a view ReSTIR GI lit (`restir_gi_frame_lights` - the full
  list, the sky grid and the cache GI used this frame) the hit is lit as a GI secondary vertex - `hit_surface`'s
  material and emission + `light_sample` through its BSDF + its reflectance x the cache's bounce, pre-exposed. The
  in-place classes - glass, water without rt_water, the game's shine - reflect the light the rest of the frame is lit
  by; the light store's sky K, bounce B and the lamps pass's lit set are not read there. The fallback tier (no ReSTIR
  GI) keeps the store's light and the lamps pass's set (the fallback's lamp owner).
- rt_water's water: its reflection (screen-space over the path-traced frame) and refraction (the frame copied behind
  it) already carry the path tracer's light; what it still takes from the store is its body's scattered colour (the
  vertex's light) and the glitter's sunlit test.
- Glass and water lit per pixel themselves (their diffuse / scattered light): round FL's front layer ("Round FL: the
  front layer" above).

### The path tracer's cost (round PF)
- **Every pass timed** (`[debug] frame_profile`, vulkan_pass_profile.h): the GPU crash markers' marks (gpu_markers.h -
  each view listener's call at its ViewOrder, the world's surfaces, the scene's builds, the UI, the present, and since
  this round the steps inside ReSTIR DI / GI and the fog, `FrameStep`) also write a timestamp; paired as a stack, each
  pass's inclusive and own device time a frame goes to the log every 300 frames (`gpu pass:`). One profiler for every
  pass - no pass times itself for it.
- **The light rays' hit tests read lean** (vulkan_rt_scene.h `cutout_kept`, `hit_blend`): a cut-out's or a seen-through
  surface's candidate hit on every light ray (ray_light) was the largest single cost of the path tracer (BO at night:
  ~10 ms of 26 at 1280x720 across DI, GI and the fog). The same values from the triangle's record, its corners' UVs and
  alphas and one texel (was: every corner's position / UV / colour, the material record and the texel twice).
  Opacity micromaps (VK_EXT_opacity_micromap, offered by the RTX 5070 Ti's driver) are the next step: most of what
  remains is the seen-through instances' candidates (BO: 714 instances, 42,500 triangles, all blend mode 5 - alpha).
- **Presampled lights** (NVIDIA RTXDI's presampled light tiles - Wyman and Panteleev 2021): a pool of 128 x 1024 picks
  of the local group (an entry by power, a uniform point on a triangle, its emission read once - vulkan_light_list.h
  `kLightPoolSize`, `light_pick_at`, `light_pool_read`) made once a submission by restir_local_lights. The fog's RIS
  takes its candidates from random slots: the same distribution as picking at the point, two words read instead of the
  triangle's corners, instance and emission texel (the fog's march 10.3 -> 4.7 ms at BO night, 640x360). The normal is
  kept 16-bit octahedral. ReSTIR DI (its own light_point, the grid's slots) and GI's next-event estimation (its
  descriptor set is full - 32 pushed bindings) still pick at the point.
- **ReSTIR GI's sky grid keyed on the sky's light** (SceneLighting::table_share): the key held the table's factor,
  1 / the camera's exposure - at night the exposure moves every frame and the grid (its march on the device, its alias
  tables on the main thread: 17 % of the main thread's samples) was made again every other frame for the same sky.
- Shader execution reordering (VK_NV_ray_tracing_invocation_reorder) is for ray-tracing pipelines; every pass here uses
  ray queries in compute - not available without moving a pass to a pipeline.

### Opacity micromaps, one primary surface, the pool for GI (round OM)
- **Opacity micromaps** (VK_EXT_opacity_micromap - NVIDIA's standard for alpha-tested geometry; vulkan_rt_micromaps.h):
  a static world mesh drawn by a cut-out or a seen-through instance gets a 4-state micromap, baked on the device from
  the texels the rays read (the material table's texture, sampler and address modes, the mesh's UVs set 0 and vertex
  alphas) and built into its BLAS. A micro-triangle is **transparent** where the alpha test rejects every point of it
  (with a texel's margin round its footprint, 128's vertex alpha counted too - the bounce and reflection rays' test) -
  no candidate at all; **opaque** where a cut-out's test keeps every point, or a blended surface (blend 5 / 6 / 0 /
  11 - 13, not glass) covers wholly - committed at once (seen_through counts a committed blended world hit as 0); else
  **unknown** - the shader's test as before. Never for objects, dynamic meshes, filters, glass or added surfaces (the
  path tracer counts them). An instance of the mesh with another material or mode is traced with
  DISABLE_OPACITY_MICROMAPS. Baked again with its BLAS when the material's record, its texture's level 0, the mesh's
  vertex alphas (Buffer::writes, hashed) or the mesh change. The shaders that traverse the scene declare the SPIR-V
  capability RayTracingOpacityMicromapEXT (glslang 1.4.304 doesn't - vulkan_rt_glsl.h adds it to every ray-query
  module, the SPIR-V cache's too). `[debug] rt_opacity_micromaps=0`, or a device without the extension: none.
  Rays traced with gl_RayFlagsOpaqueEXT (the passes' "everything solid" camera and occlusion rays, the relit bake's
  lamp-mount probe) now pass the transparent micro-triangles - where the game draws nothing.
  A micromap that decides under a quarter of its micro-triangles is dropped once its bake's counts are read back (its
  lookups would cost every hit more than they save - NVIDIA's OMM guidance), its mesh built again without it.
  BO: 993 baked, 760 dropped (nearly all soft-alpha overlays), 228 kept; the cut-outs 75 % opaque / 14 % transparent
  micro-triangles, the blended overlays 7 % transparent, 5 % opaque, 88 % unknown (their share varies texel by texel).
- **One primary surface a view** (NVIDIA's RTXDI / ReSTIR GI samples' G-buffer; vulkan_restir_di.h SharedPrimary):
  ReSTIR DI's first pass writes the pixel's surface it finds (trace_drawn + hit_surface, or the depth's own) in full
  precision, 80 bytes a pixel; ReSTIR GI's passes read it by its device address where it is this view's (the same
  submission, rectangle, camera and matrix) instead of tracing the camera ray again. By day, when DI doesn't run, GI
  traces its own.
- **ReSTIR GI's next-event estimation from the presampled pool**: a local light picked by the full list (its kind by
  power) takes its pick and point from the pool - the group's pdf x the group's share of the list's picks; the pool is
  read through its device address (GI's pushed set is full). The fog's haze is now run where the light list's emitting
  triangles alone light it (no sun, no analytic lamp: it was skipped).

### Ray Reconstruction's inputs at the drawn size, local lights by RIS at GI's secondary vertices (round DD)
- **Every Ray Reconstruction input an image of exactly the drawn size** (vulkan_upscale.cpp prepare): the transparency
  layer (vulkan_surface_light.h surface_light_layer - the colour target's size and format) and ReSTIR GI's specular hit
  distance (the light inputs' size) are copied into the upscaler's own images of the render size, the view at their
  top-left, and tagged with that extent. Given the colour target's image tagged with the drawn sub-rectangle, DLSS-RR
  laid the layer over its output twice - at its place and the whole image over the output: BO at night, every light cone
  round the statue shown again at drawn / output of its place (thug2_vulkan_upscale_check measured 6.0 at the place and
  2.1 at the ghost for a square of 8). The DLSS provider tags a transparency layer only when it is the drawn size.
- **ReSTIR GI's next-event estimation: RIS over the local lights** (vulkan_light_list.h light_sample_glsl, LL_POOL):
  the local group's light at a secondary vertex is chosen by resampled importance sampling (Talbot et al. 2005) over
  8 of the presampled pool's picks (kLocalCandidates; RTXDI's ReSTIR GI takes 8 local light samples at its secondary
  surfaces too), target = the light's luminance at the point x the receiver's cosine (unshadowed), one ray for the one
  chosen; its weight = the candidates' mean weight over its target, x the group's share of the list's picks. One pick by
  power alone gave a small or far emitter (a lit sign's or window's triangles) a pick probability far below its share
  at the point: its rare picks were fireflies, reused by GI's reservoirs and kept by Ray Reconstruction (BO at night:
  bright specks moving over the street, the bank's ceiling and the bus stop). Passes without the pool (the reflections'
  list-lit hits, the front layer) keep one pick.

### GI's specular in a reservoir of its own, DI's target with the whole BRDF (round WS)

- **ReSTIR GI's specular reservoir** (vulkan_restir_gi.cpp; ReSTIR GI's resampling of the whole BSDF - Ouyang et al.
  2021 -, RTXDI's GI with a BRDF-aware target): round BR gave every surface its own GGX visible-normal ray, its noise
  (the secondary vertex's next-event estimate seen through the lobe) shown as it came. That ray is now the candidate of
  a second reservoir per pixel (binding 16, 4 uvec4 a pixel: this frame's, the history), target p^ = the luminance of
  the specular lobe x the sample's light at the surface's own view - the reuse re-evaluates the lobe where the sample
  goes. Temporal: the point's own pixel's history (not permuted - a neighbour's view of a lobe puts its samples in this
  lobe's tails: the check's floor had 5 x the pixel-frames over 4 x their mean with permutation, none without);
  spatial: one neighbour (the weights' rays grow with the square of the reservoirs) within a radius shrinking with the
  lobe. Both reconnected (Lin et al. 2022), RTXDI's GI sample validation (a Jacobian outside 1/10 .. 10 not taken - round
  LC's finding, the clamp to 1/3 .. 3 not done), combined by the **generalised balance heuristic** (each sample's weight
  its share of every reservoir's M x target at it in the area measure, with an any-hit ray: 1 / Z let a sample from
  another lobe's tail come out x 100s - the near-mirror floor -43 %); the result's ray only for what seen-through
  surfaces let through. Roughness-aware: a history or neighbour only within 0.1 of the roughness, no reuse at all below
  0.2 (a near-mirror keeps its own ray: with a moving camera its history's samples sit in the old view's lobe - the
  check's 0.12 floor -17 % under the boiling filter, -6 % without). The boiling filter runs on the specular reservoirs
  apart from the diffuse ones.
  **Off in the game** (kSpecularReuseInGame): measured there (BO at night, the ledge under "100% INTRO APR" and the
  brick wall, 60-frame series under DLSS Ray Reconstruction) it made the picture noisier - the ledge's specks a frame
  338 -> 392, each pixel's temporal deviation 2.43 -> 2.83 (0-255), the wall's 0.58 -> 0.70 - for +2.6 ms (GI 6.1 ->
  8.7 ms at 1280x720): the unpermuted history carries the error from frame to frame and Ray Reconstruction keeps it
  as detail. Each frame's own ray (a reservoir of one candidate) until a decorrelated specular reuse is found.
- **ReSTIR DI's target: the whole BRDF** (vulkan_restir_di.cpp light_target - RTXDI's target for a light sample): the
  luminance of the light through a white diffuser's lobe plus the material's specular lobe toward the eye, unshadowed.
  The diffuse lobe's alone gave the lights' specular (the chosen sample through the specular lobe) from samples drawn
  for the diffuse - the flaw round BR removed from GI. Unbiased either way (the check 24 / 0, the glossy floor -0.41 %).
- **Measured** (thug2_vulkan_restir_gi_check, the specular alone - RestirGiOverrides::specular_only -, F0 0.04): at
  roughness 0.35 / 0.25 the pixel-frames over 4 x their mean 0.22 % -> 0.00 %, each pixel's variation 0.47 -> 0.21 /
  0.37 -> 0.23; the mean the own rays' within 0.5 % still and 0.8 / 2.6 % with the camera orbiting. In the game BO's
  ledge specks are not GI's specular: they stay with GI's light (diffuse and specular) zeroed, with only ReSTIR DI or
  only ReSTIR GI on (119 a frame with GI's light zeroed and the lamps by the passes), and go with both by the passes. The specks on the kickers in front
  the bank (round NK's build) likewise come from each of ReSTIR DI, GI's diffuse and GI's specular on its own, and the
  specular reuse doesn't change them - a common cause in that surface's path through the shading / Ray Reconstruction is
  suspected, not found yet. (Round FG: the common cause was in the lights - night lights carried by shown surfaces'
  bright texels, each a tiny luminous area at tens of thousands of cd/m^2 that every estimator samples by area and rarely
  hits; with those heads gone - "One owner" above - the kickers' specks fell 1375 -> 15 a frame.)

  suspected, not found yet. (Found in round SK, below: neither - the reflections pass.)

### The reflections' hits lit as GI's secondary vertices are (round SK)
- **The specks on BO's kickers and ledges at night were the reflections pass's.** Those surfaces carry the game's shine
  (an environment-mapped pass: reflectivity 0.19, no surface map - the lacquer material, a near-mirror): the pass traces
  one mirror ray a pixel and, in a view ReSTIR GI lit, lights its hit by the light list (list_hit_light - round WF). That
  next-event sample was one local light picked by power from the whole list (the pool of round OM was GI's only): a small
  lamp near the hit in about one pick in hundreds, its light then that many times its share - a bright pixel, new each
  frame, added to the colour after the shading. Not RR's, not a material's: specks with DLSS SR as with RR (1232 / 1232 a
  frame), with the roughness 1 / F0 0.04 / no normal map / no decal layer / the original textures / rt_pbr 0 / rt_detail
  0 (1050 - 1273), gone with rt_reflections 0 (6.8). Zeroing ReSTIR DI's or GI's outputs never touched them - the reason
  round WS found "three estimators" each making them.
- **One way of sampling the list from a surface** (vulkan_light_list.h): FrameLightList carries the pool ReSTIR GI read
  (`pool`: its address, on, the local group's share), and every pass that lights a surface from the list - GI's secondary
  vertices, the reflections' hits, the front layer - reads it through `light_pool_reader_glsl`: the local lights by RIS
  over 8 of the pool's picks (round DD's) instead of one pick by power.
- Tried and not kept: the candidates weighed by the hit's whole BSDF toward the ray's origin (RTXDI's target) in place
  of the cosine - no measurable change (the glossy-wall night in the GI check: mean and flicker the same; the ledge
  within run-to-run noise).
- **Measured** (THUG2-4, the user's two spots, held cameras, 1920 x 1080 window / 1280 x 720 render, DLSS Q + RR, 50
  frames; tools/game/speck_count.ts - a pixel over its 5 x 5 median and its own median over the series by 30 / 255):
  kickers 1232 -> 12 specks a frame (per-pixel std 4.33 -> 1.48); ledge 595 -> 47 (its right part 431 -> 0; the left
  part's remaining ones are the reflections' light from local lamps the pool's 8 picks by power rarely hold - gone with
  the local lights off, not with the disc, the sky or the emitting triangles off; next: the light grid's cells (ReGIR,
  round PT4c - ReSTIR DI's) as the secondary and reflection hits' candidates).

### The list's readers take their candidates from the light grid (round DO)
- **The sparkle that appears when the camera moves and fades when it stands still** (BO at night, the granite planter
  top by the bank's wall) was the reflections pass's hit lighting, not a disocclusion of ReSTIR DI or GI: the band sat
  where the planter's mirror rays meet the lamp-lit wall, it went with `rt_reflections` 0 and with `indirect_light`
  passes, not with ReSTIR's outputs; with the reflections off the frame's specks stayed level from the first frame after a
  stop on (no settling to remove). The reflections pass keeps no history - each frame's one next-event sample at its hit
  is Ray Reconstruction's to average, which it does while the camera stands still; moving, the reflection moves across
  the surface (its specular history rejected) and the raw sample shows. That sample's local light: RIS over 8 of the
  pool's picks by power (round SK) - a lamp near the hit among the level's hundreds in few of them, its rare picks the
  specks. With 64 of the pool's picks a hit (an experiment, across launches) the pans' specks fell from tens to about one a frame: the
  candidates' quality, not the estimator, is what was missing.
- **Fix: NVIDIA RTXDI's ReGIR at secondary surfaces.** ReSTIR DI's light grid round the camera (round PT4c: each cell's
  64 slots resampled from the list toward the most a light can give the cell, with their unbiased contribution weights)
  is also written, each frame it is made, as presampled picks - the first `kLightGridPicks` (16) slots of every cell with
  a point each, in the pool's layout, their pdf the slot's (1 / its weight; a triangle's per unit of its area) - and
  FrameLightList carries their address (`grid`). `light_sample` (vulkan_light_list.h - one GLSL for ReSTIR GI's secondary
  vertices, the reflections' hits and the front layer) takes a point inside the grid's candidates from the cells' picks -
  each candidate from the cell of the point moved by its own jitter of up to half a cell (as ReSTIR DI's pixels are: no
  cell edges; one jitter for all 8 drew them from the 16 picks a whole cell shares in a frame - a faint grain the denoiser
  kept with the camera still) -, outside it from the pool as before. Unbiased: RIS over candidates that carry their own
  contribution weights (generalised RIS, Lin et al. 2022), and the grid's bound covers every light that can reach a
  point of a cell's box widened by half a cell.
- **Measured** (THUG2-4 synced to the user's 21-33-46 sidecar, 1920 x 1080 window, DLSS Q + RR, BO night; the camera at
  the user's pose, panned 90 units sideways over 60 frames and stopped; tools/game/speck_count.ts over the planter
  1190,520 725x555, threshold 30; grid and pool alternated 5 times in one launch - the lighting differs from launch to
  launch): specks a frame during the pan (its last 40 frames) 65.9 -> 13.8; the 30 frames after the stop 270 -> 99;
  standing still 7.0 -> 8.9 (both within the launch's noise). The check (restir_gi_check, the night of a far
  floodlight and six small lamps by the wall, ReSTIR DI beside GI): the floor's flicker 1.357 -> 0.899 of its mean, its
  mean +0.94 % (pool) / +1.26 % (grid) of the reference's.
- Cost (quiet GPU, 1280 x 720 render, the same spot held, 14 x 300 frames each): the light grid 0.074 -> 0.141 ms (it
  writes 262 144 picks a frame - 16 a cell), ReSTIR GI's samples 4.15 -> 4.43 ms, its spatial reuse 1.10 -> 1.18 ms, the
  reflections 0.95 -> 1.03 ms; about +0.5 ms in all.
- Also: a rough reflection's rays each start their own random sequence (all of a pixel's rays started the same one -
  the same candidates at each hit).
- Tried and not kept: RTXDI's disocclusion boost (8 spatial neighbours, not 2, for a pixel whose reservoir's history
  is under the cap - DI and GI). After a camera cut (half a turn: every pixel new) the frame-wide specks of the frames
  after it the same with and without it (frame 1 ~38 000, 2 ~3 600, 3 ~900, 5 ~140 in both, alternated three times in one
  launch): the noise of those frames is Ray Reconstruction's empty history, the neighbours as new as the pixel.

### The lighting's debug views, and the light grid's shared picks kept to the estimators that reuse (round FA)
- **Debug views** (`gpu_vulkan/vulkan_debug_view.h`; `[debug] rt_debug_view`, live: `set rt_debug_view <name>` on the
  console or the automation channel) - as UE5's Lumen / path tracer visualisations and RTXDI's debug outputs, one term of
  the frame's light at a time through the whole chain (shading, Ray Reconstruction, tone mapping): `lamps` (ReSTIR DI's
  diffuse), `lamps_specular`, `gi` (GI's diffuse from surfaces), `gi_specular`, `sky` (the disc's visibility and GI's
  samples that saw the sky), `reflections`, `fog` (the in-scattered light), `front` (the front layer), `emission` (none
  of them). Every other producer writes nothing into the frame (DI's and GI's put_result by their flags, the
  reflections' apply by its push constant, the fog's composite and the front layer by a block word); the surfaces' own
  emission stays in every view (the shading adds it, no producer carries it); the exposure is held at the whole frame's
  meter (set_manual_ev100) while a term view is on, so a term is as bright as it is in the picture. `flicker`: the
  finished picture as a heat map of each pixel's temporal standard deviation over the last 60 frames (display luma;
  white at 16 / 255) - a running Welford mean and variance after the grade (ViewOrder::kDebugView = 990). The camera
  effects own the setting (their listener runs only while a view is on).
- **Attribution** (THUG2-4 synced to the user's 12-56-xx sidecars, 1920 x 1080 window, DLSS Q + RR, BO night, held
  cameras; 60 frames a view a spot; tools/game/speck_count.ts --heatmap). The largest per-pixel temporal deviation
  where the user saw flicker came from GI's specular (BO's lamp-pool floor and wall: 3.7 / 3.8 of 255) and the reflections
  pass (the kickers' shine: 7.7). Their region means moved 2 - 4 x from frame to frame (the floor under a wall lamp
  through GI's specular alone: 24 - 106), every pixel of a region together - not specks, the picture breathing.
- **Cause: the light grid's picks are every point's of a cell.** Since round DO the list's readers took a point's local
  light candidates from ReSTIR DI's light grid (ReGIR): 16 presampled picks a cell of 128 units, made again each frame.
  The error those 16 carry (how many of them the cell's main lamp got this frame) is the same for every point of the
  cell and new every frame - ReGIR's known correlation (Boksansky et al. 2021). GI's diffuse reservoirs average it out
  over their frames; GI's specular ray (every frame's own - its reuse is off in the game, round WS) and the reflections'
  list-lit hits (no history) passed it to the frame whole, and Ray Reconstruction, whose history follows the picture,
  kept it as picture.
- **Fix:** an estimator that doesn't resample over frames takes its candidates per point from the frame's pool (RTXDI's
  presampled light tiles - each point its own random slots, the errors independent between pixels, which RR removes):
  `light_sample`'s `ll_cell_candidates`, false around GI's specular ray and in the reflections' hits; the reflections'
  hits RIS over 24 of the pool (kListHitCandidates; GI's vertices 8 - their reservoirs' reuse does the rest). GI's
  diffuse vertices and the front layer (its own 16-frame accumulation) keep the grid.
- **Measured** (per-pixel temporal deviation of display luma / flickering specks a frame; before -> after): the
  kickers 4.97 -> 0.83 (their reflections alone 7.70 -> 0.53), the lamp-pool floor 2.43 -> 0.35 (GI specular alone 3.66
  -> 0.35), its wall's GI specular 3.83 -> 0.59; the brick wall with the overlays 2.81 / 342 -> 2.57 / 241, the column
  3.03 / 111 -> 2.37 / 64; whole frames 0.76 -> 0.55, 1.54 -> 0.65, 1.50 / 634 -> 1.32 / 370. The planter's pan (round
  DO's): 40.8 -> 10.9 specks a frame during it in one A / B pair (this measure varies by x4 from run to run), standing
  still 0 / 0. Cost (the planter held, 1280 x 720, quiet GPU, 300-frame blocks): reflections 0.97 -> 1.38 ms, GI's
  samples 4.49 -> 4.18 ms (the pool's candidates cheaper than the grid's): about +0.1 ms.
- **Launch to launch**: three launches at the three spots, the same frames after the same commands - whole-frame means
  within 0.3 - 0.7 %, cells within 2 - 5 %; three windows of one launch differ as much or more (the radiance cache and the
  exposure still settling 4 - 10 s after a cut). No state that differs between launches was found; round PT4d's 2 x
  (window 76 - 153) is not reproduced.
- **Open**: (1) the brick wall's painted overlays at BO (blended quads 3.3 units off the wall - a tree's shadow painted)
  keep coloured specks of GI's raw noise: not their rays (out of every ray - no change), not their texture set, not the
  camera ray's choice of them; with Ray Reconstruction off the whole wall has the same coloured noise and RR clears all of
  it but over them - the guides there alternate per pixel between the overlay and the wall (the prepass keeps the overlay
  where its filtered alpha is over half). The standard: DBuffer decals - the guides as the layered material (round DB's
  model, which keeps the guides of the kept surface). (Round DECAL found the cause: the overlays write no depth - the
  prepass never kept them -, they were transparencies drawn into Ray Reconstruction's transparency layer, never
  denoised; now laid into the wall's material - "Round DECAL" above.) (2) The scaffold's lamp (P3) in ReSTIR DI's lamps: 4.8 / 255.

### The picture in motion against the reference, and the light grid's slots (round MV)
- **A repeatable move** (tools/game/motion_error.ts; README "tools/game"): the automation's `camera ... <turn> <frames>`
  turns the held camera about the world's up and stops it after that many frames - the pose a function of the frame
  count (game/rider/test_view.h `camera_pose_at`); the console's `measure on` freezes the world as the original's pause
  and draws every logic frame once (no late frame skipped, no max_fps frames); `pt frames <count> <folder> [every]
  [inputs]` saves the frames' linear light after the upscaler (Ray Reconstruction's output, over the frame's
  pre-exposure) as EXR with each frame's camera, and with `inputs` Ray Reconstruction's own inputs (the colour, depth,
  motion, guides); `pt ref ... as <name>` names a reference, the automation's `waitfor pt` waits for it. A session at a
  spot: per motion (a pan of 15 degrees in a second, a 90-degree turn in half a second, a dolly of 90 units in a second)
  and per lighting term (`rt_debug_view`) the hold, the move and 30 frames after it; each term's picture converged at the
  move's middle and end (300 frames held); two independent references there (2048 paths a pixel each - their
  difference is the reference's own noise, taken out of the error). The error: display luma after one exposure (ACES +
  sRGB), split into its fine part (less its 15 x 15 mean: noise, specks) and its broad part (the mean: blotches, a region
  too bright or dark), newly revealed pixels (seen by fewer than 8 frames before) apart from those with history.
- **What it showed** (BO night, the user's p1 - the bank's kickers -, 1280 x 720 drawn, DLSS Quality + RR):
  - against the reference, the realtime picture is mostly biased, not noisy: its mean 0.55 x the reference's in the pan's
    view (the bank's windows: emitting in the reference, ~300 x the light the realtime frame gives them), 0.88 in the
    turn's; eight replays of the same move averaged come no closer than one (display RMSE 19.8 -> 19.3), and its fine
    error against the reference is the same moving and converged (7.6 / 7.7) - the picture's detail differs, not its noise;
  - against its own converged picture, the move's error is ~70 % broad: blotches 30 - 300 pixels across that move with the
    camera and take seconds to settle (30 frames after a stop they are still 2.8 of 3.6) - "the noise that goes when you
    stand still long enough". By term the largest were ReSTIR DI's lamps (broad 2.9 in the pan, 2.7 in the turn) and their
    specular (2.4), then the reflections pass's shine on the kickers (fine 2.7);
  - Ray Reconstruction's motion vectors are right: against the depth and the two frames' cameras 0.002 - 0.03 pixels, none
    over a pixel (a static world turned 3 degrees a frame); its output follows its input's mean (0.95 - 0.97) except while
    the camera's exposure climbs fast after a turn (pre-exposure x 1.05 - 1.08 a frame: 0.83 - 0.87) - the darkening that
    lingers after a camera move.
- **The cause of the lamps' blotches: the light grid's shared draw** (ReGIR's correlation - Boksansky et al. 2021). A cell's
  slots are drawn again each frame and shared by every pixel of its 128 units; with 64 of them how many the cell's main
  lamps got was an error common to the whole region, which the reservoirs then keep for their history. Tried, one at a
  time, against the converged picture of each: no spatial reuse - worse (fine noise up); the history from the temporal
  reservoir alone (GI's way) - no better; a history of 5 frames instead of 20 - worse; no grid (the list by power) -
  broad down, fine up, the turn worse; 256 and 512 slots - broad down, fine unchanged.
- **Fix: 512 slots a cell** (`kGridSlots` - RTXDI's ReGIR default, lightsPerCell 512). Its build +0.6 ms at 1280 x 720
  (ReSTIR DI 2.8 -> 3.4 ms). p1, against each run's converged picture: the lamps' broad error in the pan 2.9 -> 1.8, at
  its stop 2.3 -> 1.5, their specular's at the stop 2.3 -> 1.2, in the turn 2.7 -> 1.8; the whole picture during the pan
  4.0 -> 3.6, the turn 7.2 -> 4.8, the dolly 3.8 -> 3.3. What stays: the darkening after a move (the exposure, above),
  GI's revealed pixels, the shine's reflections.
- **Tried and taken back:** the reflections pass under Ray Reconstruction with one GGX visible-normal sample a pixel and
  frame (no fixed cone pattern, no 3 x 3 blur) and the guides edited where its reflection replaces the pixel's specular
  (specular albedo, roughness, hit distance): its term fell little and the edited guides showed the shine's quads round
  the bank's ceiling lights as darker rectangles. The standard is one specular path for those surfaces (the material's
  coat as the specular lobe GI's ray samples, its guides written by the shading) - not done in this round.

### What is drawn apart, in the rays (round PG)

The last gap of stage 3: what the game draws in passes of its own after the ray-traced lighting - the clutter's items,
the particles, the level's billboards - is in the acceleration structure as shipping path-traced games put it there,
so it casts shadows and is met by the bounce, reflection and fog light rays and by the path tracer.

- **One kind of instance** (`rt::kDrawnApart`, instance mask `kMaskDrawnApart` 0x08): a light ray's and a secondary
  ray's occluder and surface - the sun's and the lamps' shadows, the occlusion, ReSTIR DI / GI's light rays and GI's
  bounces, the reflected rays and their sun rays (`kReflectedRayMask` - the reflections' camera ray keeps
  `kReflectionMask`), the fog's light and air map, the front layer, the path tracer - never a camera ray's (its pixels
  aren't in the depth those passes read: RAY_DRAWN 0x87 leaves it out, `drawn_kept` refuses a seen-through one). Its
  class is the one classification (rt_surface_class.h): solid and cut-out ones 0x08 (by their alpha, `cutout_kept`),
  seen-through ones 0x80 with the flag (`seen_through` takes them when the ray's mask has 0x08), added ones 0x40 (light
  passes; a path's ray meets them). The relit bake never asks for 0x08: the baked light stays the level's.
- **Clutter** (`render_pc/rt_record.cpp` record_clutter, `engine/gfx/scatter.h` "the clutter in the rays" - UE's
  instanced static meshes / RTX Remix's instances): one static BLAS per item kind's part (the five kinds' meshes, a
  can's label its own), an instance per item within the raster's range - the game's LOD distance of its small objects x
  `lod_distance_scale`, measured from the first view's camera - placed by the vertex shader's transform without the
  wind's bend; its material a flat colour (each part's colour at four shades of the item's colour hash). The item
  pass's own sun ray leaves them out (an item never shadows itself). `[debug] rt_clutter=0`: not traced.
- **Particles and billboards** (`render_pc/rt_apart.h`, `engine/gfx/rt_apart.h` - RTX Remix's captured particle draws,
  the alpha-tested particle BLAS of Cyberpunk 2077's RT Overdrive): the first view's particle draws (their material
  handed in, their vertices as the dynamic buffer took them, drawn as quads or a list) and billboard draws (the card as
  turned for that camera) kept as triangle lists, grouped by material and vertex format; at the next record each group
  is a mesh of the frame's triangles - a dynamic buffer (grown by doubling; the vertices past this frame's NaN: inactive
  triangles), `rt::Geometry::rebuilt`: built anew each frame with PREFER_FAST_BUILD, never refitted. A frame late (the
  record comes before the draws, as the objects' lights do), turned toward the camera that drew them, only what the
  view drew (a culled system casts nothing). The glows drawn from memory are added light and not kept. `[debug]
  rt_particles=0`: not traced. Billboards are no longer registered as world meshes (their buffer holds the last draw's
  turned card). While the particle passes are kept, the renderer's dynamic vertex buffer hands its writers the
  processor's own copy (copied into the buffer at the unlock): the buffer's memory is made for writing in order, and
  reading it back to keep the draw was the particles' whole cost at first. A group's buffer and structure are kept for
  120 frames after its last use (a system the view culls for a moment makes nothing again).
- **Grass stays out** (UE5's hardware ray tracing and Cyberpunk 2077 leave dense grass out of the BVH; its own pass
  traces each blade's sun visibility against the scene): a grassy level's field is up to ~4 million blades (the logs:
  3.95 M, 3.73 M; BO 0.31 M) at ~7 triangles each - a BLAS of 25+ million triangles, rebuilt as the wind bends it -
  and a card proxy would put shadows where no blade is drawn.
- **Decals stay material layers** (round DB): never geometry.
- The geometry table's row is made once per mesh a frame (`write_instances`), the clutter's instances come in runs of
  one mesh and material (no lookup in a run), the writer's arrays are the scene's own (no allocation a frame), and the
  micromaps' planning skips what is drawn apart. Cost (BO, the archway, 1280x720 DLSS Quality + RR, clutter density 3,
  `lod_distance_scale` 3 - 26,000 items within range, ~31,000 instances in the TLAS): the main thread ~+1.3 ms (the
  TLAS's writer +0.8, the clutter's record 0.4), the device +0.15 ms (clutter) and +0.3 ms (particles) at equal clocks,
  the scene's builds +0.07 ms.

### Each renderer's surfaces and emitters as the frame's (round BIAS)

Round MV found the realtime picture at BO's night spots 0.55 - 0.9 x the reference and no closer when averaged: a bias.
Measured term by term against the reference at a held camera (`tools/game/motion_error.ts terms` - every
`rt_debug_view` term converged against `pt ref ... only <kinds> | direct | bounces 0 | 2`, `pt ref ... bounces 0` new:
what the camera sees emit), each mismatch traced to the side that was wrong:

- **A hit's colour is the surface's composed colour** (reference and every ray wrong). The frame's shading lights the
  colour the game composes from a material's passes (render_pc/pixel_shaders.cpp generated_pixel_shader); a ray's hit
  read the first pass alone. BO's bank ceiling (a dark layer over an olive base) was 2 x lighter in every ray, and the
  bank's windows - glass panes whose only pass is environment mapped - read the environment map's texture at the
  mesh's coordinates: white, lit by the lamps 140 units in front of them, the reference's "300 x" windows. Now
  rt::Material::layers carries each later pass (texture, mode, coordinates, alpha source, colour) and
  vulkan_rt_materials.h `material_colour` composes them as the pixel shader does (modes 1 - 10, 12, 13); hit_surface and
  hit_emission read it (`hit_composed_colour`) - so the path tracer, ReSTIR DI's traced albedo, GI's secondary
  surfaces, the reflections' hits and the emitters' scan see the frame's surface. Record 112 -> 196 bytes.
- **An environment-mapped pass is no albedo** (both wrong). Its colour is a painted reflection; the reflections pass
  traces the surface's reflection. On a surface lit per pixel the frame took the environment map's texel (sampled
  along the reflection vector) as albedo and lit it - a second, lamp-lit reflection; now that stage's texel reads black
  (ff_shaders.cpp `environment_texel_black`, its alpha kept; the stages are the draw's: u.alpha_reference.w - the
  fragment key has no room), and the rays' composite takes the same rule (an environment first pass black, environment
  layers colour 0). BO's bank windows are dark glass with their reflection.
- **The lamps input on the diffuse lobe is A (1 - E)** (realtime wrong). The light model's diffuse (brdf_direct,
  brdf_ambient, the reference's BSDF) is albedo x (1 - E), E the specular lobe's directional albedo; the shading put the
  lamps input - ReSTIR DI's lamps, GI's diffuse, the front layer's - on albedo x it, counting what the specular reflects
  twice (4 - 30 % too bright on dielectrics, most at grazing views). The processor check's model already had it.
- **Emitters whatever their class for rays** (realtime wrong, reference noisy). The emitters' scan listed only
  instances that cast and weren't seen through: a lamp's globe or a lit pane that emits (masks 0x80 / 0x40) was never
  sampled by ReSTIR DI - its light on BO's plaza floor missing - and the reference found it by its BSDF rays alone. Now
  every instance with a store that emits is listed; the reference's next-event estimation takes an emitter as ReSTIR DI
  does (the sampled point's own surface, a light ray that stops short of it - `ray_light`), and a BSDF ray meeting a
  listed added surface takes the MIS weight.
- **The radiance cache's miss continues the path** (realtime wrong). `cache_lookup` (vulkan_light_list.cpp) tells a
  cell without resolved samples (SHaRC's query miss); ReSTIR GI's secondary vertex then traces on along its cosine lobe
  (`trace_secondary` with go_on: the next vertex's direct light + its reflectance x its bounce, the cache read again at
  each; two vertices, then Russian roulette by the light the path carries, at most 12) instead of taking 0. An entry is
  trusted from 32 samples taken since it was claimed (round PERF: counted in its key, not its faded weight - that settled
  under 32 wherever fewer than 2 samples a frame arrive, a miss for good; SHaRC's sample threshold: a young entry's update paths end in other young entries - at 8
  the cold room's first frames were -7 %). Readers that can't trace on (the update paths' last vertex, the reflections'
  hits, the front layer) take a young entry's light, else 0. One trace call in the loop: a second inlined
  trace_secondary (ray queries and the light sample) made the GI shader lose the device.
  Round PERF2: an entry is also trusted 16 frames after it was claimed (its key's w; CACHE_FRAMES, its history's
  length) with any samples - a young entry lacks the frames its feedback takes, not samples; and a first secondary
  vertex that misses adds its continued path's light to its cell (the update paths' sample), so cells only the
  pixels' rays reach (specular hits, surfaces behind the camera) get entries instead of continuing every frame.
- **Round PERF2: one secondary path a pixel, in its own pass.** ReSTIR GI's samples are PASS_SAMPLE, before the
  temporal reuse (RTXDI's order: initial samples, then temporal resampling) - the shader that did both lost the device
  when edited (an instruction fault in the shader's own code, from the first frames), split it doesn't. Each pixel
  traces one path, its lobe chosen by its share of the reflectance (`lobe_share`: the reference's
  path_specular_probability held to 0.1 - 0.9, a metal always specular), its candidate's W 1 / (pdf x the lobe's
  chance); the other reservoir's candidate that frame is none (M 1). One BRDF-sampled path a pixel is what RTXDI's
  ReSTIR GI and Cyberpunk 2077's path tracing trace; both lobes' paths every pixel before. BO at night, 1707 x 960
  drawn: GI samples + temporal 20.8 -> 6 ms.
- **Round PERF5: the paths past a cache miss in a pass of their own** (PASS_CONTINUE - wavefront path tracing's
  split). PASS_SAMPLE traces the first vertex alone; where its cell has no trusted cache entry it puts the vertex in the
  pixel's own reservoir words and the path (pixel, lobe, random state, pdf, direction) on a queue after res_a's
  reservoirs, counting the groups of an indirect dispatch in the counters buffer; PASS_CONTINUE runs the old loop
  (CACHE_MISS_SURE vertices, then Russian roulette up to CACHE_MISS_VERTICES) over the queue alone, adds the miss's
  cache sample and stores the pixel's samples. The same rays, numbers and random sequences as the one loop. The sample
  pass's registers 241 -> 128 (the loop's live state was the peak, not the duplicated code); the continued pass 229
  over a dense queue. BO at night, 1707 x 960: GI samples 3.61 -> 2.40 + 0.16 ms; a second spot 2.95 -> 1.89 + 0.24.
- **Round PERF6: ReSTIR DI's first pass in three** (the same wavefront split). PASS_PRIMARY finds each pixel's surface
  (the camera ray, its hit surface) and writes the shared rows (round OM - now always, GI reading them or not);
  PASS_CANDIDATES resamples the 8 initial candidates (light points alone) and keeps the pick - entry, point, the
  weights' sum, its target, the random state after them; PASS_TEMPORAL evaluates the pick's light point again, traces
  its ray, takes last frame's reservoir and traces the reuse's ray. The same samples, numbers and random sequence (the
  check's means equal to the last digit). Registers 168 -> 80 / 80 / 128. With frame_profile, the passes count what
  they did each frame (pixels, grid fallbacks, candidates, rays, the candidates the rays meet, history, cells made) and a
  pass far above its usual time is logged with the counts ("restir slow pass").
- **Round BUDGET: the lighting passes to a fixed budget** (120 fps at 2560 x 1440 DLSS Quality + Ray Reconstruction on
  an RTX 5070 Ti). Approved compromises, each the practice of shipping real-time path tracers:
  - ReSTIR DI, RTXDI's shape: PASS_BOUNDS prepares each listed triangle once a frame - its corners in the world and 16
    points drawn uniformly on it with their emission found there (RTXDI's "prepare lights" and presampled light
    samples); a candidate takes one of its triangle's points (the same pdf as a point drawn per pixel) and evaluates the
    geometry term alone (light_point_at) - no corners, transform, store or texel read per candidate. A reservoir keeps
    its sample's emission (a second word a pixel) so the reuse's targets need no texel either; the chosen sample is
    shaded with light_point's own light once a pixel. One light ray a pixel: the shading's (its sample dropped from the
    kept reservoir where it isn't seen - visibility reuse); the temporal and spatial Z by the targets alone (RTXDI's
    basic bias correction), one spatial neighbour (RTXDI's default), 16 initial candidates (cheap now; 8 left the
    check's lamp-in-a-few-texels case over its noise bound with one neighbour). The candidates are in the temporal pass
    again (neither traces a ray).
  - ReSTIR GI on a trace grid (kTraceScale): at 2 a cell is a 2 x 2 block, its traced pixel turning over the block
    (0,0), (1,1), (1,0), (0,1) - half resolution as Metro Exodus Enhanced Edition's ray-traced GI and Embark's kajiya
    trace it; the samples, continued paths, temporal and spatial reuse and the radiance cache's update paths (a 4 x 4
    block of cells) per cell, and PASS_UPSAMPLE gives each pixel one of its four nearest cells whose surface is its own
    (alike, like roughness), drawn by a tent weight, demodulated. **Not used (kTraceScale 1, the passes as before)**:
    Ray Reconstruction keeps a cell's outlier spread over its block as detail - bright specks over the whole picture
    (the bank at night: the frame's motion error 3.9 -> 12 of 255; averaging the cells gave 2 x 2 blocks, a weighted pick
    smaller specks). RR wants each pixel's own sample; the half-resolution route needs its own answer to that first.
  - The fog: 1 / 2 / 2 samples a froxel a frame at rt_fog_quality 0 / 1 / 2 (were 2 / 4 / 6), the froxels' history 32
    frames (was 20) - UE's and Frostbite's budget.
  - Not done this round: the reflections folded into ReSTIR GI's specular path (water, glass and lacquer replace E of
    the game's environment map - a design of its own).
- **Round BUDGET2: the budget, second round** (same target). Each change the practice of shipping path tracers:
  - The radiance cache ends paths (SHaRC's intended use, not only its misses): an entry also keeps the direct light a
    white diffuser there gets (E / pi, the update paths' light samples - the bounce alone stays what the readers that
    sample their own direct light take). A ReSTIR GI path ends at its first vertex where the cell is trusted and the
    ray's footprint (its length x 1 / sqrt(pi x its lobe's pdf)) covers the cell: the vertex sends its reflectance x the
    cell's whole light, no light sample. The sample pass then holds no light sampling at all: a path that doesn't end is
    queued whole and the continued pass traces its first ray again (96 registers, was 128 / 166).
  - (Round LW2) A path ends there only where the vertex's specular lobe reflects at most 5 % of an even light (its
    directional albedo at the view - vulkan_light_list.h cache_may_end; GI's vertices and the reflections' hits alike):
    the cell's E / pi through the diffuse lobe is exact, through the specular one it assumes the light even - a floor
    met at a grazing angle, its lobe on the horizon and the lamps overhead, sent its whole specular albedo x E / pi.
    Ended paths were 15 % brighter than paths traced on at BO's bank; now 1.00 (Training too), the continued pass +0.07
    ms at 853 x 480. A sample whose path ended there has a confidence of at most 8 in the temporal reuse (the
    generalised balance heuristic's c_i(x): the same in its weight and in z) - round LW's age limit dropped such a
    reservoir past 16 frames, a drop that depends on the sample and so biased (the check's red wall -2.5 %).
  - ReSTIR GI's spatial reuse with one neighbour, two where the pixel's history is under 4 samples (RTXDI's disocclusion
    boost; its rays-for-Z kept); the specular reservoir's reuse code (off in the game since round WS) compiled only into
    the check's shaders.
  - Measured (BO at night, 1707 x 960 drawn, the bank): ReSTIR GI 6.3 -> 4.5 ms, reflections 2.8 -> 1.2, the GPU frame
    17.9 -> 15.3; the picture's motion error (pan / turn, whole frame) 5.2 / 6.9 -> 4.0 / 6.3 of 255; the converged
    picture's mean against the path-traced reference +1.9 % / +2.1 %. Open: GI's own term after a fast turn (its broad
    error 1.1 -> 2.5: cells of the cache newly trusted).
  - RTXDI's basic bias correction for GI (Z by targets, no rays) was measured and **not kept**: the check's room -5.8 %
    (spatial alone) / -12 % (temporal and spatial), the glossy floor -2.2 / -4.8 %.
  - The reflections pass: a rough pixel (lacquer, a kerb's sheen) traces one ray a frame, its point on the cone's disc
    drawn anew each frame (the same distribution as the four rays of a fixed disc it replaces; the apply's 3 x 3 blur and
    Ray Reconstruction's history gather the directions), and its reflected hit ends at the radiance cache where the
    footprint covers the cell; in a view ReSTIR GI lit, the map-shaded surfaces (weight 0 there) skip the reads.
  - Not done: folding the reflections into GI's specular path - water, glass, car paint and puddles replace E of the
    game's painted environment map, painted glass keeps its own colour under the reflection, puddles follow the decals'
    wet mask and the rain's rings: GI's specular has none of these classes; moving them is a design of its own.
- Not changed, measured equal: the light lists (175 lamps, 611 emitting triangles - one scan, one record per lamp),
  ReSTIR DI's estimator (an exact sum over every lamp at each pixel - a diagnostic - equal to ReSTIR's result within
  2 %; no grid, no boiling filter, no reuse: the same means), Ray Reconstruction (its output's mean its input's).

Checks: thug2_vulkan_rt_materials_check (a material's passes composed - alpha blend, fixed blend, modulate, add, an
environment first pass black - texel for texel), thug2_vulkan_rt_lights_check (the lamps input on the oil layer: A
(1 - E) x it at every floor pixel's N.V), thug2_vulkan_restir_gi_check (the room's first two frames with the cache cold:
within 3 % of the reference's every bounce - -0.1 %; a miss read as 0: -60 %).

Measured (BO at night, the user's three spots, realtime / reference whole picture, before -> after; the references' own
noise 0.4 - 0.6 %): p1 (the bank) 0.56 -> 1.14 (less the game's additive glows the frame draws and the store doesn't
emit - 21 % of p1's light, open: 0.94), its lamps' and emitters' direct light 0.43 -> 0.95, its indirect with the
reflections pass 0.93 -> 0.99; p2 0.83 -> 0.88, direct 0.88 -> 0.95, GI 0.40 -> 0.51; p3 0.60 -> 0.71, direct 0.64 ->
0.77, GI 0.30 -> 0.29. Open: GI's diffuse under RTXDI's boiling filter (-29 % at p2 - near-lamp bounces are its
"outliers"), GI's remaining deficit near lamps (p2 0.68 without the filter, p3 ~0.3), the additive glows' single owner.
- **Round NOISE: ReSTIR DI's bias, flicker after cuts** (the user's flickering lights).
  - ReSTIR DI keeps the sample it shades even when its one light ray finds it hidden (RTXDI's `discardInvisibleSamples`
    off - its default). Round BUDGET dropped such a sample (W 0) but kept its M: the next frames read the reservoir as M
    candidates that had brought nothing, so the pixel's light fell to one frame's candidates over the whole history and
    climbed back over its length. Wherever candidates often meet a hidden light (emitters beyond a wall, lamps round a
    corner) the direct light sat 25 - 40 % low and lights came and went over seconds. Barcelona's fountain at night,
    the lamps' and emitters' direct light against `pt ref ... only emit,lamps direct` (less what the camera sees emit),
    240 frames averaged: 0.59 -> 0.77 of the reference (its opaque surfaces 0.66 - 0.86 -> 0.93 - 0.98; the rest is the
    see-through net and pool, which ReSTIR DI doesn't light - the front layer does). Resetting such a reservoir to M 0
    instead was ~1.5 x too bright (the history then keeps only seen samples) - not used.
  - The presampled points on an emitting triangle are stratified (64 a frame, one in each cell of an 8 x 8 grid over
    the unit square the triangle is mapped from): each still uniform for the candidate that takes one at random, the
    frame's set covering the triangle evenly (a lamp drawn in a few texels no longer caught by 0 - 4 of 16 points).
  - RTXDI's disocclusion boost in the spatial reuse: a pixel without history takes 4 neighbours, not 1.
  - Ray Reconstruction is reset on a cut (`frame_blend::note_cut` - a teleport, a respawn, a level load), as the DLSS-RR
    guide asks; before, only with frame generation on.
  - The radiance cache's update paths take their light samples from the pool (candidates of their own), not the light
    grid cell's shared picks: the cache's history is these samples' only reuse.
  - Open (measured, not fixed): a held, frozen view still has a slow wave of ReSTIR GI's term (Training: the whole term
    +-5 % over ~4 s, its regions 8 - 14 %), not from the light grid's picks (tried); the see-through scaffold net at
    Barcelona is dark with bright cell-sized squares in the front layer (its own bounce ray tried - no change).
- **Round NOISE2: the light grid's cells drawn from their own CDF** (the user's lights flickering in every level).
  - Measured held, world frozen, `pt frames` (`still_series.ts` - cells' smoothed light, 2.5 s): Training's lamps
    (indoors, no fog or sky) wandered 7 - 8 % (cells' median) / 13 - 15 % (most) in Ray Reconstruction's input and
    3 - 4 / 8 - 11 % in its output, the whole frame 3 - 4 % - the light grid off: 3.0 / 5.9 %; the presampled points,
    the boiling filter and 64 RIS candidates a slot made no difference or worse. Bias first: Training's whole picture
    against `pt ref 512` 0.998 (no bias - a variance with a long memory).
  - Cause: each cell's 512 slots were RIS over 8 picks of the list by power, drawn anew each frame. A light's number of
    slots in a cell has a binomial spread (a lamp with 4 slots: +-50 % from frame to frame); every pixel of the cell
    takes its candidates from the same slots, so the error is the whole region's, and the temporal reuse keeps each
    frame's for ~20 frames - lights fading in and out over seconds. The list's other readers (GI's vertices, the fog's
    froxels, the reflections' hits) take 16 picks a cell from the same slots.
  - Fix: a cell is a workgroup that sums every entry's weight for its box into the cell's CDF (subgroup prefix sums,
    1024 entries a round in shared memory) and draws its slots by **systematic resampling** (one offset a cell; the
    readers' 16 picks every 32nd slot from a random first) - a light owns floor or ceil of its share of the slots, each
    slot still a pick by weight / sum (W = sum / weight: unbiased). The weight is a **light tree's cluster importance**
    (Conty Estevez and Kulla 2018): power / (distance from the box's centre^2 + a cell's side^2), a lamp's falloff at that
    distance and its window at the box's nearest point - not the most the light could bring to the box's nearest point,
    which gave a tiny bright triangle next to the box (a sign's letter) as much weight as the street lamp lighting it. A
    triangle's numbers are now in light_point's unit (they were pi x a lamp's). The bounds buffer keeps the entries'
    centres and numbers packed at its start (every cell reads them all).
  - After (same spots): Training lamps view 3.3 - 3.9 / 6.1 - 7.8 % in RR's input, 1.2 - 1.3 / 2.4 - 2.6 % in its output,
    whole picture 0.8 / 1.6 % (whole frame 0.4 %), `pt ref` 1.002; New Orleans' street at night (the parade float that
    drives through the view left out) the lamps view's cells 4 -> 2 % median, the fog view's 15 -> 7 %. Cost (the grid,
    bounds + cells, 853 x 480): Training 0.55 -> 0.49 ms, New Orleans (2665 entries) 0.80 -> 0.78 ms.
