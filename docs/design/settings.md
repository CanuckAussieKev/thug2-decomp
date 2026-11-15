# The settings model

Every setting of `thug2-decomp.ini` the README documents is declared once, in `src/engine/config/settings_model.cpp`,
and read through one resolver. The renderer, the engine's graphics options, the Options screen, the screenshot's
settings report and the log all ask it; none of them decides on its own whether a setting is in effect.

## A declaration

Each setting has its `[section]` and key, its kind (a switch, a choice, a number or text), its default, its Options
screen tab, name and help line (`settings_text.h` holds every word), whether a change is seen at once or at the next
start (and, for a live renderer setting, the part of the renderer it changes - `Effect`), and its rules:

- **needs** - what it needs to be in effect: conditions over other settings' *effective* values (`is`, `is_not`,
  `all`, `any`, `negate`, across tabs), capabilities (`has(Cap::...)`: Vulkan, ray queries, the 64-bit game, NVIDIA, an
  RTX / RTX 40 / RTX 50, surface maps, the material library, height maps, a level without fog), a texture set
  installed, or a computed test (the time of day running, more frames than the display shows). A setting out of
  effect takes its `off` value (a switch's 0), so whatever needs it is out of effect too.
- **choices** - a choice may have needs of its own and a choice that stands in for it (`3x` frame generation needs an
  RTX 50: on an RTX 40, `2x`; `upscaler=dlss` needs an RTX: elsewhere `fsr`; a texture set that isn't installed:
  `original`; `renderer=vulkan` without a Vulkan GPU: `d3d9`).
- **forces** (`forces()`) - a value one setting sets on another while a condition holds. The target's needs still
  apply.
- **conflicts** (`conflicts()`) - settings that work against each other; both stay in effect, each row says so.

## The resolver

`config::resolve(Inputs)` - the ini's values, the machine's `Capabilities` and the level's facts - gives each setting:
the value asked, the effective value, whether it is in effect, whether a force set it, the reason when it isn't what
was asked (short, for the player: "Needs a ray tracing GPU", "On - needed by Frame generation", "Off while upscaling"),
its conflicts, and which of its choices are possible here. It is a pure function (`thug2_options_check` runs it on six
machines). A reason is given at the root: Shadow darkness on a GPU without ray queries says "Needs a ray tracing GPU",
not "Needs Shadows".

At run time (`settings_state.h`): `app/settings_capabilities.cpp` probes the Vulkan device (whatever renderer the ini
picks), the build and the game folder's data once at start-up; `running()` is the ini as it is now for the live
settings and as it was at start-up for the ones read once (the device was made for them); `current()` is the ini as it
is now (the Options screen's view, the settings report's). When the Options screen changes a live setting, the running
resolution is made again and every renderer part whose settings' effective values moved is read again - the key's own
and any another rule moved with it.

## The rules (what is enforced)

- `ray_tracing` needs the Vulkan renderer, a ray tracing GPU and the 64-bit game. Shadows, occlusion, bounce light,
  lamps, reflections, volumetric fog, the relit mode, the sky model and Ray Reconstruction need ray tracing.
- Vulkan only: every `rt_*` effect, grass, decals, clutter, water, the art styles, the camera effects, HDR, the
  upscaler, frame generation, Reflex, `vulkan_*`, `compare_split`. Any renderer: the time of day, smooth curves,
  texture sets, the frame pacing, anisotropy and msaa, the distance scales.
- The upscaler and frame generation need the 64-bit game; Reflex an NVIDIA GPU; DLSS an RTX; DLSS frame generation an
  RTX 40 / 50, its 3x / 4x an RTX 50 (FSR's makes 2x).
- Frame generation with no upscaler: the upscaler on (`auto`) at native size (`dlaa`). DLSS frame generation: Reflex
  on. A sun other than the level's (`rt_sun_source`) or a running time of day: the relit mode on - and its knobs in
  effect with it. `OnlineGameName=thugpro`: `thugpro_peer` on.
- The time of day's sun replaces `rt_sun_source=game`; a fixed direction beats the clock (a conflict). Frame generation
  under V-sync, and `max_fps` times the multiplier past the display's refresh rate, are conflicts.
- msaa and `AntiAliasing` are off while upscaling; `AntiAliasing` is out while msaa picks the samples. The AO and
  bounce denoisers are out while Ray Reconstruction cleans up. FSR's sharpening and version need FSR upscaling (or
  `auto` off an RTX).
- `rt_pbr` needs surface maps; its knobs, `rt_relief`, `rt_detail` and `rt_characters` need it;
  `rt_reflection_surfaces` needs reflections and `rt_pbr`. (`rt_delight` is retired: the de-lit texture sets take the
  painted light out offline.)
- (the offline relit data - vulkan.md "Relit: the bake taken apart, offline") The level's facts say what it is lit
  from (`LevelFacts::light`, render_pc/rt_relit_world.cpp at each new level): `rt_relit` needs its relit data built
  (`Cap::kLevelLightData`), made from the level's meshes as they are (`kLevelLightCurrent`) and sunlight in its bake
  (`kLevelSunInBake`) - the reasons name the build step (tools/game/relit_bake.ts). With ray tracing on, the time of day
  (`tod`, `tod_cycle`, `tod_levels`) needs the same; without it the original's own time of day runs. The lighting passes
  (`rt_shadows`, `rt_ao`, `rt_lights`) need `rt_relit` and turn it on (a Force): none ever stacks on a bake. `rt_gi`,
  `rt_relit_detail`, `rt_relit_cache` are retired.
- The haze is the volumetric fog's (it needs `rt_fog`) in a level without fog; its density and height need the haze and
  the sky model. The sun's colour needs the sun, its rays, the bloom or the sky model; the lens dirt the bloom or the
  sun; clouds the real sky.
- The grade's vignette and grain are out under an art style with its own; each art style's knobs need that style; the
  grade's strength needs a look other than neutral; per-level grades `rt_grade=level`.
- Smooth curves' knobs need `rt_tessellation`; the smoothing strength needs smooth shapes; displacement needs height
  maps or the test pattern.
- Building relief (`building_relief`, on by default) needs the Vulkan renderer and the facade files
  (`Cap::kReliefMaps`: a `.relief` beside a level dictionary); its strength needs it.
- Each effect's knobs need its switch; `start_at` a start level; `language` the unpacked scripts; the BMX's knobs
  `thaw_bmx`; `collision_event_screenshots` `collision_events`.

## Adding a setting

Declare it in `settings_model.cpp` (its words in `settings_text.h`), add its README row (the check compares the
tables with the model, defaults included), and read it through the resolution (`running()` / `current()`), never
`build_info` directly. A live renderer setting names its `Effect`; `app/renderer_choice.cpp` reads that part. Then
`thug2_options_check`: the model's checks (every rule's key and value exist, no cycle, reasons for compound
conditions, defaults among the choices) and the rules' checks run on every declaration.
