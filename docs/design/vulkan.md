# A Vulkan renderer, and ray tracing after it

Status: design, being implemented: V1 (the libraries), V2 (the context), V3 (the fixed-function emulation, "V3"
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
                                               gpu_vulkan/  Vulkan
engine  --render::UiBackend-->  d3d9 / vulkan UI backends (ImGui's dx9 / vulkan renderer backends)
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

`src/render_vulkan`: `ff_key.h` (the key), `ff_shaders.h` (key -> GLSL), `ff_uniforms.h` (the uniform blocks,
bindings and attribute locations), `glsl_compiler.h` (GLSL -> SPIR-V, and a library of compiled shaders by key);
`src/tests/ff_shader_check.cpp` (the check).

**What the game draws with, measured from `src/render_d3d9`.** With pixel shader support (every Vulkan device), the
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
SDK; its GLSL front end and SPIR-V back end only) is linked into `render_vulkan` and compiles each key the first time
it is drawn (`ShaderLibrary`); V7 adds the pipeline cache on disk and can warm it from a scene's materials. SPIRV-Tools'
validator (same release; its build needs Python 3) is linked by the check alone. Shaders target Vulkan 1.0 / SPIR-V 1.0.

**The key.** `ShaderKey` = `VertexKey` (36 bytes) + `FragmentKey` (48 bytes): plain bytes with no padding, compared and
hashed as bytes, every enumerator with Direct3D 9's own number. `normalised()` clears what a half doesn't read (stages
after the first disabled one, arguments an operation doesn't read, CURRENT at stage 0 as DIFFUSE, lighting fields
with lighting off, coordinates of stages not sampled, material modes without shader code...), so the halves are
compiled separately and shared by every pipeline that uses them. It lives in `render_vulkan`: `render::Gpu` (V5a)
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
| `UiBackend` | ImGui's SDL2 + Vulkan backends (`imgui_impl_vulkan`); `UiTextureId` = a descriptor set's handle (64 bits); the preview and atlas as images with their own depth; `invalidate_device_objects` = drop the font image |
| MSAA | sample counts 2/4/8 as the device has them; resolve before the screen copies and present |

**Libraries** (fetched and pinned like the others, licences to confirm before adding):
- Vulkan-Headers (Apache-2.0);
- volk, the loader (MIT);
- Vulkan Memory Allocator (MIT);
- a GLSL -> SPIR-V compiler at build time: glslang (BSD-3 / Apache-2.0) or shaderc (Apache-2.0);
- ImGui's Vulkan backend (MIT, already fetched with ImGui).

The validation layers come from the Vulkan SDK, debug builds only.

**32-bit.** The raster backend doesn't need x64. Windows Vulkan drivers ship 32-bit ICDs (to confirm per vendor for
current drivers), so the Vulkan backend can be brought to parity on the 32-bit build, next to the Direct3D 9 backend
and the same frames.

### V5a: `render::Gpu` (done)

`engine/gfx/gpu.h` declares it (beside the handles, `engine/gfx/gpu_handles.h`, which gained `GpuPixelShader` and
`GpuQuery`); `render_d3d9/gpu_d3d9.cpp` is Direct3D 9's, installed by `app::install_renderer` with the device.

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

## Order of work

| # | task | depends on | start |
|---|---|---|---|
| V1 | `src/render_vulkan` skeleton: CMake option `THUG2_VULKAN`, the fetched libraries (Vulkan-Headers, volk, VMA, glslang / shaderc), `tools/layers.json` / `cmake_layers.ts` entries, a header test (Vulkan headers only in `render_vulkan`) | - | now, against `stage11` |
| V2 | A Vulkan context test program (`src/tests`): instance, device selection, an SDL window with a Vulkan surface, swapchain, a cleared frame, resize / minimise / out-of-date handling, validation in debug | V1 | now, against `stage11` |
| V3 | The fixed-function emulation generator: the state key -> GLSL -> SPIR-V, the 6 fixed `ps.1.1` shaders and the per-material generator in GLSL, compiled offline in a test program and checked by reading the key's terms back | V1 | now, against `stage11` |
| V4 | Choose the renderer at start-up: `[Graphics] renderer=d3d9\|vulkan`; `app/` installs the matching `Device` / `UiBackend`; the SDL window flag through `sys::create_sdl_window` | V1 | after stage 11 - done: `app/renderer_choice`, falls back to Direct3D 9 until the device exists |
| V5a | `render::Gpu`: the interface, and `render_state`'s cache and `primitive_states` through it (D3D9 implementation = today's calls) | - | after stage 11 - done: `engine/gfx/gpu.h`, `render_d3d9/gpu_d3d9.cpp` |
| V5b | Meshes, materials, skinning, mesh lists, instances through `Gpu` | V5a | after stage 11 - done |
| V5c | Particles, billboards, weather, grass, glow, texture splats, shatter through `Gpu` | V5a | after stage 11 |
| V5d | Screen effects, draw frame, shadows (texture and stencil), texture projection, viewports through `Gpu` | V5a | after stage 11 |
| V5e | 2D, text, fonts, sprites, loading screen / bar / console, movie, screenshots, device creation and reset through `Gpu` | V5a | after stage 11 |
| V5f | The folder split: `render_d3d9` -> `render_pc` (API-neutral, no `<d3d9.h>`) + `gpu_d3d9`; `tools/d3d_headers.test.ts` extended | V5b-e | after stage 11 |
| V6 | `gpu_vulkan` resources: textures (format conversions), buffers (static, dynamic, the `UP` ring), render targets, depth formats, samplers | V2, V5a | after stage 11 |
| V7 | `gpu_vulkan` draws: pipelines from V3's keys, the state block, transforms, indexed / ring draws; a level's world drawn | V3, V6, V5b | after stage 11 |
| V8 | `gpu_vulkan` effects: screen copies, blur, bloom, occlusion queries, texture shadows, stencil volumes, flash, MSAA resolve, gamma pass | V7, V5c, V5d | after stage 11 |
| V9 | `UiBackend` for Vulkan: ImGui's Vulkan backend, the preview and atlas targets, images | V6 | after stage 11 |
| V10 | Movies, loading screens, screenshots and the editor's methods on Vulkan | V7, V5e | after stage 11 |
| V11 | Parity: a screenshot comparison between the two backends at the same spot and frame (the automation channel's teleport and capture), with each backend's run-to-run noise as the baseline; the front end, the editor, five levels; frame times | V8-V10 | after stage 11 |
| R1 | The frame as data: `render_pc` fills the per-frame instance / light / camera record; stable mesh and material ids | V5f | after V11 and the x64 port |
| R2 | Acceleration structures: a BLAS per static mesh at scene load, refit / rebuild for CPU-skinned meshes, the TLAS per frame | R1, V7, x64 | after V11 and the x64 port |
| R3 | The material table and bindless textures | R1, V6 | after V11 and the x64 port |
| R4 | Ray-traced directional shadows (ray queries in the lighting pass), an ini option, off by default | R2 | after R2 |
| R5 | Reflections / ambient occlusion and a denoiser | R2, R3 | after R4 |

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
V10 2, V11 2. That is about **23-26 rounds to raster parity**. Ray tracing (R1-R4) is about 8-10 more, R5 open.

## How the numbers were measured

A Python scan over `src/` (comments stripped) counts the Direct3D calls by name, the states, values, formats, pools,
FVF combinations and primitive types named beside them, and the pixel shader sources. Values passed through
variables (material tables, the renderer's cache) are counted as such: the scan lists what is named, not every value
used at run time.
