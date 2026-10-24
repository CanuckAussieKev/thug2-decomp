# Graphics basics (image quality, step 1)

Branch worktree-agent-a7dd508bbc7531b21. The user: "step 1" of making the game look more modern - image-quality basics as
ini switches whose defaults keep the original behaviour. All of it NOT ORIGINAL, in `src/game/gfx/graphics_options.h/.cpp`
plus small marked blocks in rewritten functions.

## The switches ([Graphics] in thug2-decomp.ini)

| key | default | effect |
| --- | --- | --- |
| `anisotropy=` | 0 | 0: the original's filtering. 2-16: anisotropic minification on all four samplers wherever the original picks linear (clamped to the device's MaxAnisotropy; off when the caps lack D3DPTFILTERCAPS_MINFANISOTROPY). |
| `msaa=` | 0 | 0: the original (AntiAliasing=1 -> 2 samples, else none). 2-16: the most samples the device has from that number down (CheckDeviceMultiSampleType on X8R8G8B8 and D24X8, windowed or not), at its highest quality level; AntiAliasing is then ignored. |
| `draw_distance_scale=` | 1 | multiplies the projection's far plane (96000, PartyMod's, or the DistanceClipping distance x 18), with it the distance clipping's mesh cull distance (0x00786CAC), and the cameras' default far clip (Camera::Construct). 0.25-8. |
| `lod_distance_scale=` | 1 | multiplies objects' lod_dist1 (beyond it they stop animating) and lod_dist2 (beyond it they're not drawn) - CSuspendComponent::InitFromStructure, after the script's LOD0/LOD1DistanceMultiplier. 0.25-8. NoLOD objects untouched. |

A scale of exactly 1 returns the same float (bit for bit), so the defaults change nothing. The values in effect are
logged at start-up (`graphics options: ...`, only when one isn't the default) and the msaa choice when the device is made.

## Anisotropic filtering - where filters are set

Every D3DSAMP_MINFILTER write in the exe (scan of `call [reg+0x114]` with `push 6`, out\scripts\scan_sampler.py):
- 0x004D8269/0x004D82A0 ApplyRenderState (rewritten): the original's sampler-0-at-<=480-lines anisotropy. Now: with the
  option, every sampler ANISOTROPIC + MAXANISOTROPY n.
- 0x004DB003/0x004DB013 SetState kFilter0-3 (rewritten): the per-material filter (bit 0 linear / point). Now linear ->
  anisotropic with the option. This is the one that matters: the first material overwrote ApplyRenderState's filter.
- All the others (0x0044F961/A36, 0x004B3141/95, 0x004B6D71/6EEA, 0x004B93C3/9542, 0x004C7563/75EF, 0x004D543C/56F2,
  0x004DBF86, 0x004DC318/C5EB, 0x004DC91F/C94D, 0x004DF405/F89F, scene_render's shadow pass, the sky pass in render.cpp,
  loading_console, movie): save the filter, set LINEAR (or POINT) for a screen-space / special draw, put the saved one
  back. They restore whatever we set, so nothing un-rewritten needed changing (several are in the renderer range another
  agent is decompiling: untouched).
- MAXANISOTROPY is only written by ApplyRenderState (original and ours), which runs after every device (re)creation.

## MSAA - what had to change

- InitDirect3D / CreateDevice (device.cpp): the sample count and quality chosen before the device is made; the existing
  fallbacks (hardware / software vertex processing, then no multisampling) unchanged. D3DRS_MULTISAMPLEANTIALIAS is
  set with the option too (ApplyRenderState).
- The plain path (no motion blur, no bloom) draws straight into the multisampled back buffer with its auto depth: works
  as the original's AntiAliasing=1 did.
- The screen-copy path (motion blur 0x00787328 > 2, or bloom 0x00705720 with pixel shaders): the original draws the whole
  frame into the full-size screen copy textures (0x00786D1C/20) with the plain depth surface 0x00786D64 - so no MSAA at
  all in those frames. With the option, a multisampled colour surface + its own multisampled depth (back buffer size,
  same type/quality; default pool, released in ReleaseRenderTargets and made in CreateRenderTargets, so device resets
  remake them) stand in for those copies as render target (`SetSceneTarget`: screen_fx.cpp SetTarget /
  BeginScreenEffects - both branches, incl. the call to the un-rewritten 0x004B6070 -, render.cpp DrawScreenBlur), and
  are resolved (StretchRect, no filter, same size) into the copy just before it is read (`ResolveSceneTarget`:
  CopyScreenQuarter before the quarter-size copy, DrawScreenBlur before the copy is drawn onto the back buffer). A
  separate depth (not the back buffer's) so the HUD drawn later onto the back buffer sees the same depth as before.
- Already fine: the screenshot key / capture (screenshot_capture.cpp resolves a multisampled back buffer itself),
  PrintWindow (DWM), shadow textures (own 256x256 targets, no depth; restore the saved target), the depth texture target
  (own 256x256), the level editor's ImGui backend (saves / restores its targets), movies (turn
  MULTISAMPLEANTIALIAS off and back), GetFrontBufferData (0x004B25BB). The game itself never calls StretchRect or
  GetRenderTargetData.
- Not done: alpha-to-coverage. The vendor switches (NVIDIA ATOC via D3DRS_ADAPTIVETESS_Y, AMD A2M1 via D3DRS_POINTSIZE)
  apply to every alpha-tested draw, and THUG2 leaves alpha test on (ref 1) for nearly everything, blended surfaces
  included - they would dither. It would need a per-material decision (cut-out, not blended), not a simple safe toggle.

## Draw distance - fog

Fog is left as the level sets it (SetFogDistance / the TOD manager: start = distance, end = 4 x start; with distance
clipping on, start + 7200). Moving the far plane out only moves the clip line further behind the same fog, so a line
the fog hid stays hidden; ported levels with their own fog / clip settings go through the same calls. Scaling fog too
would thin every level's atmosphere, which is a look change rather than a distance one.

## Self-tests

`TestGraphicsOptions` (src/hook/selftests_graphics_options.cpp, Early): parsing / clamping, the device's anisotropy, the
filter picked, the sample count picked (fake device: 2/4/8), the default scale leaving floats bit-identical. The existing
original-vs-ours comparisons of ApplyRenderState, SetState, SetCamera, Camera::Construct, CSuspendComponent and
CreateDevice assume the options at their defaults - run self-tests with them off.

## Results

(below)
