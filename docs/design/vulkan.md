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
                                               gpu_vulkan/  Vulkan (its Gpu and GpuContext: V6; draws: V7)
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

**The key.** `ShaderKey` = `VertexKey` (36 bytes) + `FragmentKey` (48 bytes): plain bytes with no padding, compared and
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
  loading steps - decided by V10. `loading_console_panel` saves the device's whole state (`Gpu::save_state`, a state
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
  `imgui_texture_id.h` (Dear ImGui's backend, `render::UiBackend`; V9 makes the Vulkan one) and `d3d_matrix.h` (unused
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
  written every 10 s when something new was made. foun makes 52 pipelines from 40 shaders (0.9 s of glslang in the
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
  backend's lock), as they are; V10 decides how they should.
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
