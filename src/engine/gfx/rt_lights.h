#pragma once

// NOT ORIGINAL - the night lighting's light sources ([Graphics] rt_lights - gpu_vulkan/vulkan_rt_lights.h draws them;
// docs/design/vulkan.md "Lights at night"). API-neutral: the CPU half, which render_pc feeds and the Vulkan pass reads.
//
// A level at night (or indoors) looks flat because its lamps, signs and lit windows light nothing: the game draws them
// as bright textures and bakes their light into the vertex colours around them, with no shadows and nothing for the
// skater. Two kinds of light source are turned into real lights for the ray-traced pass:
//   - the level's own lights: the dynamic point lights of the frame record (rt::Light::Kind::kPoint - the light nodes a
//     level places, engine/gfx/scene_lights.h, and any a script makes), as the game gives them;
//   - emitters: the level's emissive surfaces found once a world from its meshes (render_pc/rt_record.cpp gathers the
//     triangles, cluster_emitters below joins them):
//       glow     - world meshes drawn additively (their first pass blend mode 1 or 2: add, add by the fixed alpha): the
//                  halos round lamps, neon tubes, light cones and pools - what the artists drew where light is. A flat
//                  patch facing up (a pool of light on the ground) is lifted by its size: the light falls from above;
//       self-lit - (the relit bake's candidates) opaque triangles whose vertex colours are all far brighter than the
//                  level's (self_lit_triangle): lamp heads, lit windows, signs. A flat patch facing up is left out (that
//                  is a baked pool of light, not a lamp);
//       textured - (lighting round A3, the relit bake's candidates) opaque triangles whose TEXTURE shows a lamp: texels
//                  far brighter than their texture's own median, near white or a lamp's colour, not on a floor
//                  (render_pc/rt_record.cpp textured_emitters - ceiling lights drawn as bright textures on ordinary
//                  vertex colours).
//     A self-lit or textured candidate is a lamp only when the bake shows its light (rt_relit.h solve_relit_lamps: told
//     and lit) - brightness alone is no evidence; a glow (the artists' mark of a lamp) shines by its own surface where the
//     bake doesn't tell it.
//     Triangles are joined into clusters kEmitterJoin across (one light per lamp, not per triangle; a long tube gives a
//     light every kEmitterJoin or so); each cluster is a light at its area-weighted centre, its colour the vertex and
//     pass colours' area-weighted mean (the pass multiplies in its texture's mean colour), its intensity that
//     colour's brightness, its falloff's core the radius of a disc of its area, its reach from that core.
// A view lights with at most kMaxViewLights of them: select_view_lights keeps those whose reach touches the view's
// frustum, the most important first (intensity x how much of the reach the camera is inside of).
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_scene.h"

namespace render::rt {

struct LightCards; // (round light cards - light_cards.h)

// (round indoor lamps) kPool: a lamp found from its pool of light in the bake (rt_relit.h find_pool_lamps) - where the
// bake shows light no listed lamp explains, at the mount above it.
enum class EmitterKind : uint8_t { kGlow = 0, kSelfLit = 1, kPoint = 2, kTextured = 3, kPool = 4 };
// (lighting round A3 - the original's light cards) A world mesh whose first pass adds (blend mode 1 - add - or 2 - add
// by the fixed alpha) and whose texture doesn't move (no UV wibble or texture animation: not water, not an effect) is a
// light card: a halo, light cone, pool or starburst the artists drew round a lamp. It is the lamp's evidence (a glow -
// the relit bake finds lamps by it) and never light of its own: the relit level hides it (render_pc rt_relit_world).
inline constexpr uint32_t kCardBlendAdd = 1;
inline constexpr uint32_t kCardBlendFixedAdd = 2;
[[nodiscard]] constexpr bool light_card(uint32_t first_pass_blend, bool moving_texture)
{
    return (first_pass_blend == kCardBlendAdd || first_pass_blend == kCardBlendFixedAdd) && !moving_texture;
}
// (round A3) A candidate the bake must confirm (its light seen in the bake) - not an artist's glow or a point light.
// (round indoor lamps) A lamp found from its pool too: a hypothesis the bake's light must tell.
[[nodiscard]] constexpr bool candidate_kind(EmitterKind kind)
{
    return kind == EmitterKind::kSelfLit || kind == EmitterKind::kTextured || kind == EmitterKind::kPool;
}

// A triangle of an emissive surface (world space), its colour (the vertices' x the pass's, 1 = the game's white; mean
// of the corners) and its material.
struct EmitterTriangle {
    std::array<std::array<float, 3>, 3> corners{};
    std::array<float, 3> colour{};
    MaterialId material = 0;
    EmitterKind kind = EmitterKind::kGlow;
    // (round L - a lamp's luminous area) the share of the triangle that is the lamp's luminous surface: a textured
    // candidate's lamp texels, a light card's saturated core (its texels' brightest channel at least kTexelBright - the
    // lamp itself, not the halo the art drew round it); 1 for a self-lit triangle (bright vertex colours throughout)
    float lit_share = 1.0F;
    // (round relit-misses) the mesh it is a triangle of (its id in the RayTracing; 0: none told) and its corners'
    // vertices in that mesh - its lamp's own surface is these vertices, not every vertex of the level at the same place
    // (a neighbouring face's, a card's laid over it)
    MeshId mesh = 0;
    std::array<uint32_t, 3> vertices{};
    // (round SL, the bake's) its mesh is shown only in another of the level's lighting states (its night's geometry -
    // hidden in the bake's): a candidate only as one of the game's lights' head (rt_relit.h relit_adopt_game_lights)
    bool other_state = false;
};

// (lighting round A2 - indoor ceiling lights) An emitter's shape, from its triangles (cluster_emitters: their spread's
// principal axes): a point (a lamp head, a glow's halo - spread every way), a panel (a flat light - a ceiling's lit
// panel, a lit window: thin across, its triangles facing one way) or a line (a tube - long along one axis).
enum class EmitterShape : uint8_t { kPoint = 0, kPanel = 1, kLine = 2 };
inline constexpr float kLineRatio = 4.0F;      // a cluster this many times longer (its spread) than wide is a line
inline constexpr float kPanelThin = 0.25F;     // ... a cluster this thin across against its width is flat ...
inline constexpr float kPanelFacing = 0.8F;    // ... and a panel when its triangles face one way this much (|sum n A| / A)
inline constexpr float kSpreadToHalfLength = 1.7320508F; // a uniform spread's half length over its deviation (sqrt 3)

// A light the pass shines (an emitter found at load, or the frame's point light).
struct Emitter {
    std::array<float, 3> position{};
    float size = 0.0F;    // its radius (world units): a shadow ray stops this short of it
    std::array<float, 3> colour{}; // hue, the brightest channel 1
    float intensity = 0.0F;        // x colour: the light right at it (half that at the core's edge), face-on
    float reach = 0.0F;            // where its light ends (a smooth window to 0)
    MaterialId material = 0;       // whose texture's mean tints it (0: none)
    EmitterKind kind = EmitterKind::kGlow;
    bool other_state = false; // (round SL) its triangles' (EmitterTriangle::other_state) - clustered apart
    // (added for the washed-out streets) its falloff's core (light_falloff): an emitter's emitting area as a disc's
    // radius (emitter_core), a point light's share of its reach (point_core)
    float core = 0.0F;
    // (lighting round A2) its shape: a panel's normal or a line's direction (unit), a line's half length
    EmitterShape shape = EmitterShape::kPoint;
    std::array<float, 3> axis{0.0F, 1.0F, 0.0F};
    float half_length = 0.0F;
    // (lighting round A3) its switch: the open sky's light (luma, the global unit) under which it is on - lamp_on
    float switch_sky = 0.0F;
    // (round SL) the game's own light it is (a light node's name - rt_scene.h GameLight; 0: none): then the game
    // switches it - lamp_switch -, not a photocell
    uint32_t game_light = 0;
};

// (lighting round A2) The light a lamp sends to a surface point per unit of its intensity - one model for relit's L,
// the lamps' solve and the lamps pass (vulkan_rt_lights.cpp's GLSL lamp_geometry is the same), x the window to `reach`
// (light_falloff's):
//   - point: N.L x core^2 / (core^2 + d^2) - a sphere of its area's radius;
//   - panel: the same x |its normal . L| - a two-sided Lambertian disc of its area (on its axis, the exact disc);
//   - line: N.L toward the segment's nearest point x core (sin b - sin a) / (2 D), D^2 = d^2 + core^2, d the distance to
//     the line, a and b the segment's ends' angles seen from it - a uniform line of light (1/d near it, 1/d^2 far).
// 1 right at a point or a panel; `toward` the point the light comes from (the shadow ray's end). 0 behind the surface.
struct LampShape {
    std::array<float, 3> position{};
    EmitterShape shape = EmitterShape::kPoint;
    std::array<float, 3> axis{0.0F, 1.0F, 0.0F};
    float half_length = 0.0F;
    float core = 0.0F;
    float reach = 0.0F;
};
[[nodiscard]] float lamp_geometry(const LampShape& lamp, const std::array<float, 3>& at, const std::array<float, 3>& normal,
                                  std::array<float, 3>* toward);

inline constexpr float kEmitterJoin = 96.0F;        // a triangle joins a cluster whose first is this near (world units)
inline constexpr float kEmitterReachPerCore = 12.0F; // reach = core x this (the light down to under 1%), within:
inline constexpr float kEmitterMinReach = 200.0F;
inline constexpr float kEmitterMaxReach = 1500.0F;
inline constexpr float kEmitterMinSize = 4.0F;
inline constexpr float kEmitterMinCore = 4.0F;
inline constexpr float kFlatUpShare = 0.85F;     // a cluster facing up this much (area-weighted |n.y|) ...
inline constexpr float kFlatHeightShare = 0.25F; // ... no taller than this share of its width is flat
// Self-lit candidates: a triangle whose dimmest corner is at least kSelfLitLuma (luma of the bytes, 128 the game's white)
// and kSelfLitOverLevel times the level's median triangle. (Lighting round A3) In every level, in the relit bake: the
// "dark level" rule (a median vertex brightness at most 64 - which read 16 sunlit afternoon levels as dark, their
// textures carrying the brightness) is gone; the bake's own light decides (solve_relit_lamps), and when a lamp is on is
// its own daylight's (lamp_on).
inline constexpr float kSelfLitLuma = 112.0F;
inline constexpr float kSelfLitOverLevel = 2.5F;
// (lighting round A3) Textured candidates (render_pc/rt_record.cpp): a texel whose brightest channel is at least
// kTexelBright (linear, decoded: the texture's white 1) and whose luminance is at least kTexelOverTexture times its own
// texture's median - a lamp drawn in a darker texture, not white paint -, its colour a lamp's: grey to white, or running
// monotonically warm (red >= green >= blue) or cool (blue >= green >= red) with its weakest channel at least
// kTexelLeastShare of its brightest (a blackbody's run, not a saturated sign's colour).
inline constexpr float kTexelBright = 0.7F;
inline constexpr float kTexelOverTexture = 2.5F;
inline constexpr float kTexelLeastShare = 0.2F;
[[nodiscard]] bool lamp_texel(const std::array<float, 3>& linear, float texture_median);
// (round FG - one lamp-texel test) The same test with its luminance floor given (kTexelOverTexture x the texture's
// median) - what the shading reads from the light store, so a lamp drawn in a texture shows its emission on the texels the
// bake measured as its luminous area and no others (before, the shading lit every texel whose brightest channel passed
// 0.6 - 0.7: a pale stone's or mortar's texels too, at the lamp's luminance - the energy the bake gave the lamp's own
// texels spread over a whole surface's bright ones).
[[nodiscard]] bool lamp_texel_over(const std::array<float, 3>& linear, float floor_luma);
// The light store's code for a lamp drawn in a texture (rt_cel.h word 6's blue byte): 0 - not drawn in a texture (the
// whole surface emits); else bit 7 - only the texels drawn solid (texel alpha at least kLampTexelSolidAlpha: a blended
// surface's, as the bake reads them), bits 0 - 6 - 1 + the floor's luminance in 126ths of kLampTexelMostFloor (a floor at
// the most: no texel shows a lamp).
inline constexpr float kLampTexelMostFloor = 1.0F;
inline constexpr float kLampTexelSolidAlpha = 0.5F; // (render_pc/rt_record.cpp kCutoutAlpha)
inline constexpr uint32_t kLampTexelSolidBit = 0x80U;
inline constexpr uint32_t kLampTexelFloorSteps = 126U;
[[nodiscard]] uint8_t lamp_texel_code(float floor_luma, bool solid_only);
// What a coded test says of a texel (linear colour, its drawn alpha 0 - 1): true for code 0.
[[nodiscard]] bool lamp_texel_coded(const std::array<float, 3>& linear, float alpha, uint32_t code);
// The GLSL twin: `bool lamp_texel_shown(vec3 linear, float alpha, uint code)` (the ff shaders' relit surfaces and the
// ray-traced hit surfaces - one test).
[[nodiscard]] const std::string& lamp_texel_glsl();
// A point light's falloff core (point_core): this share of its reach (the game's radius).
inline constexpr float kPointCoreShare = 0.1F;
// (lighting round A: the lamps' sum is no longer held under a knee - the lamps are lit by their data, the HDR tone map
// alone compresses the result)
inline constexpr std::size_t kMaxViewLights = 252; // (round A2: 64 bytes a light with its shape - the lamps pass's
                                                   // uniform block within the 16 KB every device gives)

[[nodiscard]] constexpr bool self_lit_triangle(float dimmest_corner_luma, float median_luma)
{
    return dimmest_corner_luma >= kSelfLitLuma && dimmest_corner_luma >= kSelfLitOverLevel * median_luma;
}

// The triangles joined into lights (the same triangles in the same order give the same lights) - every one of them (round
// light cards: no longer at most 4096 - Proving Ground's whole world has more lamp candidates, and the cut kept the
// brightest, losing whole districts' lamps; a view still shines at most kMaxViewLights),
// the strongest (intensity x reach) first.
// (lighting round A2) `triangle_emitter`, when given: each triangle's light among the result (kNoEmitter: none).
// `facing`: the world's winding (+1 when a triangle's (b - a) x (c - a) points out of its front - render_pc's
// world_facing, rt_relit.h relit_facing's rule): a flat self-lit cluster facing up is a pool of light baked on a floor
// (no light), one facing down a ceiling's lit panel (a panel light); 0 - unknown: every flat self-lit cluster a pool.
inline constexpr uint32_t kNoEmitter = 0xFFFFFFFFU;
[[nodiscard]] std::vector<Emitter> cluster_emitters(std::span<const EmitterTriangle> triangles,
                                                    std::vector<uint32_t>* triangle_emitter = nullptr,
                                                    float facing = 0.0F);

// The emitters of the world now loaded (render_pc sets them in a new world's first frame, whenever ray tracing records:
// the level's lamps are its own whether the lamps pass draws them or relit lights them per vertex - lighting round A).
struct LevelEmitters {
    uint64_t serial = 0; // a new value for each world's set (0: none yet)
    std::vector<Emitter> emitters;
    std::size_t glow_triangles = 0;
    std::size_t self_lit_triangles = 0;
    std::size_t textured_triangles = 0; // (round A3)
    float median_luma = 0.0F; // (the self-lit candidates' reference)
    // (lighting round A3 - lamps by their own daylight, not a "dark level") the level's own time's light without the
    // clock: its open sky's light (luma, the global unit - what lamp_on compares each lamp's switch with) and the open
    // sky's lamps (rt_daylight.h DayLight::lamps at its own time: the rule for a lamp whose switch isn't known)
    float own_sky = 0.0F;
    float own_lamps = 0.0F;
    std::size_t glow_meshes = 0; // (round A3, reported) the additive world meshes - the original's light cards
    std::size_t other_state_cards = 0; // (round P10, reported) of them, cards hidden in the bake's lighting state
    // (lighting round A2 - a path tracer's emitters) the emissive triangles the lights were made from, and each one's
    // light among `emitters` (kNoEmitter: none - dark, or dropped)
    std::vector<EmitterTriangle> triangles;
    std::vector<uint32_t> triangle_emitter;
    // (round O) `emitters` are the relit data's solved lamps (their intensities from the bake, in the global unit): the
    // level's lights are those alone - the frame record's point lights (the game's colours) are not shone beside them
    bool solved = false;
    // (path tracing, stage 1 - one owner per lamp) per emitter, 1 when its lamp has a surface of its own that emits (the
    // relit data's emitting vertices: the light store's emit word carries its radiance) - a path tracer samples that
    // geometry and not the analytic light (rt_path.h analytic_lamps); empty: none has
    std::vector<uint8_t> surfaced;
    // (round light cards - the relit bake's: light_cards.h) the light cards' triangles with their textures and the world
    // meshes' surface colours - the cards lying on a surface are that surface's light; null outside the bake
    std::shared_ptr<const LightCards> cards;
};
void set_level_emitters(LevelEmitters emitters);
[[nodiscard]] std::shared_ptr<const LevelEmitters> level_emitters();
// (added by the light energy audit - docs/design/vulkan.md "Each light counted once") How much the lamps pass lights
// (0 - 1: its lamps' amount, 0 when it doesn't own them): on the objects and characters it then owns the level's own
// lights - the light field's local light (rt_light_field.h local_at) gives way to it by as much
// (render_pc/pc_lights.cpp), as relit's per-vertex lamps do on the world. (Round S2: one state, not the last frame's:
// whether the pass owns the lamps is its settings' - rt_lights on, a strength, ray tracing, the pass made -, set when
// they are put to use (set_lamps_owner); the amount is the lighting state's lamps then. Before, the pass set it each
// view it recorded - 0 until its first one and between views -, so a level's first light took the lamps per vertex as
// well and kept them, a light set later didn't: the same night lit two ways.)
// `automatic`: rt_lights=auto (kept; round A3 - each lamp's amount is its own, lamp_on, in its light: owning the lamps
// the pass owns them whole, lamps_drawn 1).
void set_lamps_owner(bool owner, bool automatic);
[[nodiscard]] float lamps_drawn();
// (lighting round A3 - each lamp by its own daylight; one rule for the lamps pass and relit's per-vertex lamps) How much
// a lamp is on: a photocell's rule - on while the daylight round it is under a photocell's turn-on. `switch_sky` is the
// open sky's light (luma, the global unit) at which the sky's light on the surfaces it lights (x their sky share K)
// reaches it (the relit bake's: rt_relit.h relit_lamp_switches, kPhotocellLux; kLampAlwaysOn where no sky reaches them -
// indoors); `sky_now` the open sky's light now (lamp_sky_now). Smooth over kLampSwitchBand either side (in log: a
// photocell's on / off band), and never less than `open_lamps` - the open sky's own photocell, the clock's lamps
// (DayLight::lamps, by the sun's elevation) or the level's own time's: the art's night sky is display-referred, far
// brighter than a real night's, and can't hold a lamp off. A switch of 0 - not known (a lamp found without the level's
// data, the game's own point lights) - takes `open_lamps` alone.
inline constexpr float kLampSwitchBand = 2.0F;
inline constexpr float kLampAlwaysOn = 1.0e30F;
[[nodiscard]] float lamp_on(float switch_sky, float sky_now, float open_lamps);
// (round SL - the game's own lights switched by the game, as RTX Remix keeps a game's light state) How bright the game has
// its light `owner` now (rt_scene.h GameLight::strength - the node's Brightness, then its scripts' FakeLights: the time
// of day's transition scripts turn each state's lights on and the others off); 0 when the frame has no such light - a
// light the game never made or never turns on gives none.
[[nodiscard]] float game_light_now(const FrameRecord& record, uint32_t owner);
// A lamp's switch now - one rule for the lamps pass, ReSTIR's list, the path tracer and relit's per-vertex lamps and
// emitting surfaces: a lamp that is one of the game's lights (`game_light` != 0) as bright as the game has that light
// (game_light_now, the frame record's); any other by its photocell (lamp_on).
[[nodiscard]] float lamp_switch(uint32_t game_light, float switch_sky, float sky_now, float open_lamps);
// The open sky's light now (luma, the global unit - a pre-exposed state's pre-exposure divided out): the clock's
// (DayLight::sky_light) while it lights the level, else the
// level's own time's (LevelEmitters::own_sky). And the open sky's lamps: the clock's, else the level's own time's.
[[nodiscard]] float lamp_sky_now(const DayLight& day, const LevelEmitters* level);
[[nodiscard]] float open_sky_lamps(const DayLight& day, const LevelEmitters* level);
// (round PX) The emission the world's light store holds: a number that changes each time the relit world has written a
// new emission into every mesh's store (a lamp's own surface switched, a mesh's store written for the first time) or
// taken its stores away - what the passes that find the emitting triangles in the store (ReSTIR DI's scan) are made
// again by. They read the store itself; keyed by what was asked of it (the lamps' switches) they could scan it before it
// was written and keep the empty list (Training: its 34 lamps whose own surface emits were scanned at the level's first
// frame, before any store held their light, and never again - no light from them at all, the analytic lamps alone).
void note_store_emission_written();
[[nodiscard]] uint64_t store_emission();


// A view's lights: the frame record's point lights and the level's emitters whose reach touches the frustum of
// `clip_from_world` (D3D's row-vector view x projection), the most important `max` of them (at most kMaxViewLights).
struct ViewLightRequest {
    std::array<float, 3> camera{};
    std::array<std::array<float, 4>, 4> view_projection{};
    std::size_t max = 64;
    bool points = true;          // the frame record's point lights
    bool emitters = true;        // the level's emitters
    float emitter_scale = 1.0F;  // x their intensity
    float point_scale = 1.0F;    // x the point lights'
    // (round A3) each emitter x lamp_on(its switch, sky_now, open_lamps); the point lights x open_lamps
    float sky_now = 0.0F;
    float open_lamps = 1.0F;
};
[[nodiscard]] std::vector<Emitter> select_view_lights(const FrameRecord& record, const LevelEmitters* level,
                                                      const ViewLightRequest& request);
// A light's importance to a view from `camera` (select_view_lights' order).
[[nodiscard]] float light_importance(const Emitter& light, const std::array<float, 3>& camera);
// Whether a sphere touches the frustum (the planes of a row-vector view x projection, D3D's 0 - w depth).
[[nodiscard]] bool sphere_in_frustum(const std::array<std::array<float, 4>, 4>& view_projection,
                                     const std::array<float, 3>& centre, float radius);

// The light's falloff at `distance` (the pass's - vulkan_rt_lights.cpp's GLSL is the same): core^2 / (core^2 + d^2) - an
// inverse square softened by the core: 1 at the light, 1/2 at the core's edge -, times (1 - (d / reach)^4)^2, a smooth
// window to 0 at `reach`. With an emitter's core (emitter_core) it is a disc of its area and brightness: the light right
// at it its brightness, falling with the square of the distance past its own size - a pool, not a flood. (Before, the
// core was at least a quarter of the reach, up to 375 units: a lamp lit a whole street evenly, as the sun does.)
[[nodiscard]] float light_falloff(float distance, float core, float reach);
// An emitter's core: the radius of a disc of its area (at least kEmitterMinCore); a point light's: kPointCoreShare x
// its reach.
[[nodiscard]] float emitter_core(float area);
[[nodiscard]] float point_core(float reach);

// (round UNITS - one physical unit for every level's lamps) A lamp's reach - where its light ends - from its own light,
// not its size or the original's radius. The light falls with the inverse square without end; a renderer ends it where
// it no longer shows (Frostbite's influence radius from a threshold, Lagarde and de Rousiers 2014; Unreal's attenuation
// radius with its windowed falloff - the same (1 - (d / r)^4)^2 window light_falloff applies). The threshold is an
// illuminance: kLampReachLux, a starlit night's light - about 7 stops under middle grey at the camera's least exposure
// (EV100 -4.1) - so the window's last stretch lies under anything the camera shows. Before, a found emitter reached 12
// cores (200 - 1500 units) and a light node its original's outer radius, where its light was still ~5 lux (rt_relit.h
// kGameLightReachLux): at night, metered for ~1 lux, a pool's edge was a sharp circle.
// The intensity: the light right at the lamp (Emitter::intensity, the global unit) x unit_illuminance x its core's area
// (a point or a panel - its far field I = E r^2) or its core x half length (a line - I = E r h far off broadside):
// reach = sqrt(I / kLampReachLux), at least two cores. The relit data's lamps (their intensities physical) take it at
// load (render_pc/rt_record.cpp).
inline constexpr double kLampReachLux = 1.0e-3;
[[nodiscard]] float lamp_reach(const Emitter& lamp);

} // namespace render::rt
