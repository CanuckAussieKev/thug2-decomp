#pragma once

// NOT ORIGINAL - ray tracing's contract (docs/design/vulkan.md "Ray tracing", tasks R1-R5): what the renderer records
// each frame (R1: render_pc fills it as it walks the scene - the D3D9 path too, it costs little) and what a backend that
// traces rays is given (R2: acceleration structures, R3: the material table, R4 / R5: the effects). API-neutral: no
// Vulkan here. Direct3D 9 has no RayTracing (ray_tracing() is null); Vulkan's (gpu_vulkan) is installed when the device
// has VK_KHR_acceleration_structure + VK_KHR_ray_query and [Graphics] ray_tracing=1.
//
// The work streams meet here: change this file only by adding (a member at a struct's end, a virtual with a default
// body), so the others' branches still build.

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "engine/gfx/gpu_handles.h"

namespace render::rt {

using MeshId = uint32_t;     // stable while the mesh lives; 0: none
using MaterialId = uint32_t; // an entry of the material table (R3); 0: none (shaded as untextured white)

// A mesh's triangles where the GPU already has them (the buffers render_pc draws from - nothing copied): positions are
// three floats at `position_offset` in each vertex of `vertex_stride` bytes.
struct Geometry {
    GpuVertexBuffer* vertices = nullptr;
    uint32_t vertex_offset = 0; // bytes into the buffer of the first vertex
    uint32_t vertex_stride = 0;
    uint32_t vertex_count = 0;
    uint32_t position_offset = 0;
    GpuIndexBuffer* indices = nullptr; // null: not indexed
    uint32_t index_offset = 0;         // indices (not bytes) into the buffer of the first one
    uint32_t index_count = 0;
    bool indices_32bit = false;
    bool strip = false;   // a triangle strip (the backend makes a list of it), else a list
    bool dynamic = false; // rewritten by the CPU (skinned, deformed): mesh_changed() when it is
    // (added by R1) The buffers' sizes in bytes as far as the renderer knows them (0: not known) - at least this many:
    // a share of a larger buffer gives the end of its own part - and the indices' copy in memory where the renderer
    // keeps one (16- or 32-bit as indices_32bit, from index_offset; null: none), valid while the mesh is registered.
    uint32_t vertex_buffer_bytes = 0;
    uint32_t index_buffer_bytes = 0;
    const void* cpu_indices = nullptr;
    // (added by R5's reflections) Where a hit shader finds a vertex's other attributes, in bytes from the vertex's start:
    // its first texture coordinates (two floats) and its diffuse colour (Direct3D's A8R8G8B8 - 128 is 1 for the
    // game's passes); a format of kAttributeNone: the mesh has none.
    uint32_t uv_offset = 0;
    uint32_t uv_format = 0; // kAttributeNone or kUvFloat2
    uint32_t colour_offset = 0;
    uint32_t colour_format = 0; // kAttributeNone or kColourA8R8G8B8
};
inline constexpr uint32_t kAttributeNone = 0;
inline constexpr uint32_t kUvFloat2 = 1;
inline constexpr uint32_t kColourA8R8G8B8 = 1;

enum InstanceFlag : uint32_t {
    kOpaque = 1U << 0,      // no alpha test or blend: any hit ends a shadow ray
    kAlphaTested = 1U << 1, // the material's alpha decides a hit (R3)
    kCastsShadow = 1U << 2,
    kSkinned = 1U << 3,
    kWorld = 1U << 4,       // the level's own geometry (sectors)
    kObject = 1U << 5,      // a moving object, ped, vehicle, the skater
    kTransparent = 1U << 6, // blended: seen through, casts no shadow by default
};

// One draw of a mesh this frame.
struct Instance {
    MeshId mesh = 0;
    MaterialId material = 0;
    uint32_t flags = 0;
    std::array<std::array<float, 4>, 3> object_to_world{}; // rows: x' = row0 . (x, y, z, 1)
};

struct Light {
    enum class Kind : uint8_t { kAmbient, kDirectional, kPoint };
    Kind kind = Kind::kAmbient;
    std::array<float, 3> colour{};
    std::array<float, 3> direction{}; // directional: the way the light travels (normalised)
    std::array<float, 3> position{};  // point
    float radius = 0.0F;              // point
};

struct Camera {
    uint32_t viewport = 0;
    std::array<std::array<float, 4>, 4> view{};       // D3D's row-vector layout, as GpuTransform's matrices
    std::array<std::array<float, 4>, 4> projection{};
    std::array<float, 3> position{};
};

inline constexpr uint32_t kNoEnvironmentPass = 0xFFFFFFFFU; // (Material::environment_pass: none)

// A surface as a hit shader reads it (R3): its first pass's texture and how it is used.
struct Material {
    GpuTexture* texture = nullptr; // null: untextured
    uint32_t uv_set = 0;
    uint32_t blend = 0; // the pass's blend mode, as the game's material passes number them
    float alpha_ref = 0.0F;
    std::array<float, 4> colour{1.0F, 1.0F, 1.0F, 1.0F};
    uint32_t flags = 0; // InstanceFlag's kAlphaTested / kTransparent where they come from the material
    // (R3) how the texture is sampled, as the pass's sampler states: address modes as gpu_value::kAddress* (1 wrap,
    // 2 mirror, 3 clamp, 4 border - transparent black, 5 mirror once: read as mirror), the filter as gpu_value::kFilter*
    // (none / point: nearest, linear / anisotropic: linear). alpha_ref above is 0..1 (Direct3D's ALPHAREF / 255), passed
    // when the texel's alpha (times colour's) is at least it, as the alpha test's GREATEREQUAL.
    uint32_t address_u = 1;
    uint32_t address_v = 1;
    uint32_t filter = 2;
    // (added by R5's reflections) How much the surface mirrors its surroundings (0 - 1; 0: not at all): the game marks
    // a shiny surface with an environment-mapped pass, whose strength (the pass's colour and fixed alpha) is this; and
    // which pass that is (its index; kNoEnvironmentPass: none).
    float reflectivity = 0.0F;
    uint32_t environment_pass = kNoEnvironmentPass;
    // (added for the water and glass) What kind of reflective surface it is (kReflectShiny / Water / Glass; render_pc/rt_record.cpp
    // classify_reflective says how it is told): it picks the Fresnel reflectance face-on and the reflection's roughness.
    uint32_t reflection_class = 0;
    // (added for the glass) Where on the texture the surface reflects (kReflectEverywhere / WhereBaseClear / WhereBaseOpaque): the game's windows are often
    // painted into a wall's texture, the environment-mapped pass lerped in by the first pass's alpha.
    uint32_t reflection_mask = 0;
};

// Material::reflection_class's values (the reflection shaders' CLASS_*).
inline constexpr uint32_t kReflectShiny = 0; // metal, paint, polished floors: the game's reflectivity face-on as well
inline constexpr uint32_t kReflectWater = 1; // the sea, pools, puddles: 2% face-on, a mirror at grazing angles
inline constexpr uint32_t kReflectGlass = 2; // windows: little face-on (rt_glass_reflectance), slightly rough

// Material::reflection_mask's values (the shaders' MASK_*): how the first pass's texel alpha (a) scales the reflectivity.
inline constexpr uint32_t kReflectEverywhere = 0;      // 1
inline constexpr uint32_t kReflectWhereBaseClear = 1;  // 1 - a (the environment pass's blend mode 13: lerped in where
                                                       // the base is clear)
inline constexpr uint32_t kReflectWhereBaseOpaque = 2; // a (blend mode 12: lerped in where the base is opaque)

// What was drawn this frame (R1): filled by render_pc between begin_frame_record and end_frame_record.
struct FrameRecord {
    uint64_t frame = 0;
    std::vector<Instance> instances;
    std::vector<Light> lights;
    std::vector<Camera> cameras;
};

inline constexpr uint32_t kAllMasks = 0xFF; // a ray that can hit every instance

// One ray traced through the scene the backend last built (R2: the automation channel's rtprobe - a check against the
// game's own collision): from `origin` along `direction` (any length) to `max_distance`. `traced` false: nothing to
// trace against (no backend, or no frame built yet).
struct ProbeRequest {
    std::array<float, 3> origin{};
    std::array<float, 3> direction{};
    float max_distance = 0.0F;
    uint32_t cull_mask = kAllMasks; // (added for R4's sun) the instance masks the ray can hit (the backend's)
};
struct ProbeResult {
    bool traced = false;
    bool hit = false;
    float distance = 0.0F;   // along the direction normalised
    uint32_t instance = 0;   // its index in the frame record's instances
    MaterialId material = 0; // the instance's
    MeshId mesh = 0;         // the instance's
    uint32_t primitive = 0;  // the triangle in the mesh (a strip's: counted along it)
};

// A backend that traces rays (R2 on): meshes registered as they are made and released, materials likewise, then each
// frame's record - from which it builds the scene it traces (the TLAS) before the frame is drawn.
class RayTracing {
public:
    virtual ~RayTracing() = default;
    [[nodiscard]] virtual bool available() const = 0;
    virtual void add_mesh(MeshId mesh, const Geometry& geometry) = 0;
    virtual void remove_mesh(MeshId mesh) = 0;
    virtual void mesh_changed(MeshId mesh) = 0; // a dynamic mesh's vertices rewritten
    virtual void set_material(MaterialId material, const Material& surface) { (void)material, (void)surface; }
    virtual void remove_material(MaterialId material) { (void)material; }
    virtual void begin_frame(const FrameRecord& record) = 0;
    // (R2) The ray through the last frame's scene, traced now (slow: the frame's work is finished first) - for tools,
    // not for frames.
    virtual ProbeResult probe(const ProbeRequest& ray)
    {
        (void)ray;
        return {};
    }
    // (added for R4's sun) Many rays at once, as probe (slow the same way, but one wait for all of them): `results`
    // gets one answer per ray, in order.
    virtual void probe_many(std::span<const ProbeRequest> rays, std::vector<ProbeResult>* results)
    {
        results->clear();
        for (const ProbeRequest& ray : rays) {
            results->push_back(probe(ray));
        }
    }
    // (added by R1) A registered mesh's triangles now somewhere else (another of its buffers - a skinned mesh's
    // vertex buffers take turns -, buffers made again after a device reset, an edit): the same id, new geometry, and
    // its contents changed as mesh_changed says.
    virtual void update_mesh(MeshId mesh, const Geometry& geometry)
    {
        remove_mesh(mesh);
        add_mesh(mesh, geometry);
    }
    // (added by R1, for checks) Whether mesh_drawn is wanted: render_pc then reports every draw of a registered mesh in
    // the world's and the objects' passes (a world transform read back per draw: a backend leaves it false).
    [[nodiscard]] virtual bool wants_draws() const { return false; }
    virtual void mesh_drawn(MeshId mesh, const std::array<std::array<float, 4>, 3>& object_to_world)
    {
        (void)mesh, (void)object_to_world;
    }
    // (added by the first frame - docs/design/vulkan.md "The first frame after a load") A new world's first frame, after
    // its begin_frame: the builds it recorded run now and every structure that can be made smaller is (the frame's work
    // waited for, the frame's scene built again from `record`) - before the baked sun is fitted to it and anything is
    // traced. Slow: once a load.
    virtual void settle(const FrameRecord& record) { (void)record; }
    // (added by the first frame) Each frame after begin_frame (and the baked sun's fit, when one was made): what every
    // view of the frame shares made ready from the frame's sun before the first view is drawn (the Vulkan Gpu: the sky
    // model's table and the sun's light the effects read).
    virtual void prepare_frame() {}
};

// The installed backend (null: none - Direct3D 9, or no ray tracing on this device / off in the ini).
RayTracing* ray_tracing();
void set_ray_tracing(RayTracing* backend);

// The frame being recorded (render_pc writes it; ray_tracing()->begin_frame reads it).
FrameRecord& frame_record();

// Ids: a new mesh / material id each call (never 0, never reused while the game runs).
MeshId next_mesh_id();
MaterialId next_material_id();

// --- R4: screen-space effects over a viewport's opaque world -------------------------------------------------------

// A viewport's opaque world drawn: render_pc's draw_viewport calls opaque_view_drawn after the world's opaque meshes,
// the objects' opaque passes, the texture splats and projections, the imposters and the sky - before the transparent
// passes, the particles, the weather, the 2D. The colour and depth targets bound then hold what is drawn so far.
// (added by the volumetric fog) The level's distance fog as the device was told when the view was drawn: the game's
// linear range fog - over the distance from the eye d, f = (end - d) / (end - start) clamped to 0 - 1, and the colour
// becomes mix(fog colour, colour, f). `on` false: the level draws no fog.
struct ViewFog {
    bool on = false;
    float start = 0.0F;
    float end = 0.0F;
    std::array<float, 3> colour{}; // 0 - 1
};

struct ViewDrawn {
    Camera camera;                      // the world's camera (not the sky's, which sits at the origin)
    std::array<uint32_t, 4> rect{};     // the viewport in the target's pixels: x, y, width, height
    std::array<float, 2> depth_range{}; // the viewport's min / max z (0, 1)
    ViewFog fog;                        // (added by the volumetric fog)
    // (added by the sun's glare) The game's own bloom is on for this frame (a script's SetScreenEffects bloom: drawn
    // over the frame after the world - render_pc's screen effects): a bloom added by a listener leaves the frame's own
    // light alone then, so the two don't double up.
    bool game_bloom = false;
    // (added by the sky) The game clock (frame_clock::game_clock_time, in seconds): what moves with the game - the
    // clouds' drift - follows it, so a demo replays the same picture.
    double game_seconds = 0.0;
    // (added by the upscaler) The view drawn at a lower resolution, to be upscaled (set_view_scale below): `rect` is
    // where the world was drawn (its top-left part of the output), `output_rect` the viewport it is shown in - the same
    // as `rect` when the view isn't scaled. `jitter`: the sub-pixel offset the projection was drawn with this frame, in
    // pixels of `rect` (x right, y down; 0 when not jittered) - camera.projection includes it (the depth matches).
    std::array<uint32_t, 4> output_rect{};
    std::array<float, 2> jitter{};
};

// What listens (a backend's screen-space effect: the Vulkan Gpu's ray-traced sun shadows). Called on the thread that
// draws the frame, between the Gpu's draws.
class ViewListener {
public:
    virtual ~ViewListener() = default;
    virtual void opaque_drawn(const ViewDrawn& view) = 0;
    // The viewport's whole world drawn - its transparent passes, particles, weather, the sun's glare, the glows and
    // flashes too - before the 2D (the HUD, the menus, text): volumetric fog, then tone mapping, go here.
    virtual void world_drawn(const ViewDrawn& view) { (void)view; }
    // When this listener runs among the others, at both points: lower first (ViewOrder; equal: as added).
    [[nodiscard]] virtual int order() const { return 0; }
    // (added by the sky) While a backend draws the sky (sky_replacement().on): the viewport's sky backdrop drawn - the
    // world's opaque meshes, the objects', and behind them the sky scene's opaque meshes (its dome) - before the sky
    // scene's layers (its transparent meshes) and before opaque_drawn. `view` is the world's camera, as at opaque_drawn.
    virtual void sky_drawn(const ViewDrawn& view) { (void)view; }
    // (added by the upscaler) The viewport's world camera chosen, before anything of the view is drawn (its rect and
    // camera as ViewDrawn's; the world's passes follow).
    virtual void view_begins(const ViewDrawn& view) { (void)view; }
};
// The listeners' order (the effects build on each other: tone mapping needs every light added first).
namespace ViewOrder {
inline constexpr int kShadows = 100;    // R4: ray-traced sun shadows
inline constexpr int kOcclusion = 200;  // R5: ambient occlusion
inline constexpr int kIndirect = 300;   // global illumination (one bounce)
inline constexpr int kReflections = 400;
inline constexpr int kFog = 500;        // volumetric fog / light shafts (world_drawn)
inline constexpr int kSun = 600;        // (added by the sun's glare) the sun's disc, its rays and the bloom (world_drawn)
inline constexpr int kToneMapping = 900; // HDR -> the display (world_drawn, last)
inline constexpr int kSky = 50;          // (added by the sky) the atmosphere and the clouds (sky_drawn)
inline constexpr int kUpscale = 950;      // (added by the upscaler) the view's lower resolution up to its output_rect
} // namespace ViewOrder
void add_view_listener(ViewListener* listener);
void remove_view_listener(ViewListener* listener);
// Each listener told (render_pc; nothing happens when none listens).
void opaque_view_drawn(const ViewDrawn& view);
void world_view_drawn(const ViewDrawn& view);
void view_begins(const ViewDrawn& view); // (added by the upscaler)

// --- Upscaling (added by the upscaler round; the Vulkan Gpu's [Graphics] upscaler) ----------------------------------
// A backend that upscales asks for its views' world to be drawn smaller: render_pc then draws a viewport's world
// (everything up to world_view_drawn) into the top-left `scale` x `scale` part of its rectangle - ViewDrawn::rect - with
// the projection moved by `jitter` (pixels of that smaller rectangle, a new one each frame), and the listener at
// ViewOrder::kUpscale fills ViewDrawn::output_rect from it. The 2D is drawn after at the full size, as before. Only a
// view that is the frame's one viewport and isn't an editor's split view or picture is scaled. `on` false (the default,
// Direct3D 9): nothing changes.
struct ViewScale {
    bool on = false;
    float scale = 1.0F;              // 0.25 - 1 of the width and the height (1: native - jittered only, DLAA)
    std::array<float, 2> jitter{};   // the projection's offset this frame, in pixels of the scaled rectangle
};
void set_view_scale(const ViewScale& scale);
ViewScale view_scale();
// The rectangle `output` (x, y, width, height) drawn at `scale`: its top-left part, at least a pixel.
std::array<uint32_t, 4> scaled_rect(const std::array<uint32_t, 4>& output, float scale);
// The projection (D3D's row-vector layout) moved by `jitter` pixels of a `width` x `height` viewport: the clip-space x and
// y moved by 2 jitter / size (y down on the screen: up in clip space) times w.
void jitter_projection(std::array<std::array<float, 4>, 4>* projection, std::array<float, 2> jitter, uint32_t width,
                       uint32_t height);
// Whether any listens (render_pc skips filling a ViewDrawn when none does).
bool view_listened();

// --- HDR: the world's colour in floating point (added by the HDR round; the Vulkan Gpu's [Graphics] rt_hdr) ---------
// While hdr_colour() is true the targets the world is drawn into hold 16-bit floats (RGBA16F): the back buffer (its
// format still reported as the settings' 8-bit one - copies and read-backs out of it converted), the full-size screen
// copies the world goes into while a blur or the bloom is on, [Graphics] msaa's scene surface and the game view's
// picture (render_pc asks for them through world_target_format). Values above 1 are kept there - light the effects add
// at opaque_drawn (R4 / R5, one-bounce light) and world_drawn (fog) - until the tone mapping (ViewOrder::kToneMapping,
// last at world_drawn) brings the view back to 0 - 1 in place; the 2D is drawn over that as before. False (the
// default, Direct3D 9, rt_hdr=0): 8-bit targets, every value clipped to 0 - 1 by each draw as the original's.
// The values are the game's own display-referred colours (not linear): 1 is the original's white.
bool hdr_colour();
void set_hdr_colour(bool on); // (the backend, when it makes its device; before render_pc makes its targets)
// `format` (a Direct3D 9 colour format) for a target the world is drawn into: A8R8G8B8 / X8R8G8B8 as
// gpu_value::kFormatA16B16G16R16F / kFormatX16B16G16R16F while hdr_colour(), else unchanged.
uint32_t world_target_format(uint32_t format);

// (added for R4's sun) A sun set by hand - the console's `rt sun <x> <y> <z>` (toward the sun, any length), `rt sun
// off` - used by the shadow pass instead of the one it finds itself. Null / false: none set.
void set_sun_override(const std::array<float, 3>* toward_sun);
bool sun_override(std::array<float, 3>* toward_sun);

// (added for R4's sun) The sun the level's lighting was baked with, found from the bake itself (rt_sun.h: the
// direction whose ray-traced shadows best match the world's baked vertex colours) - the game's own directional lights
// light the skater and the objects and are the same in every level, not the level's sun. `scene` counts the fits (a new
// value: a new level's answer); `found` false with a fit made: the bake shows no sun (an indoor level) - no sun
// shadows there.
struct BakedSun {
    uint64_t scene = 0;     // 0: no fit yet for the world now loaded
    bool found = false;
    std::array<float, 3> toward{}; // toward the sun, normalised
    float confidence = 0.0F;       // the fit's correlation (0 - 1)
    // Where the bake agrees, over the level seen from above (a grid of cells along x and z): 1 - where the sun is
    // blocked, the baked colours are dark (the bake has that shadow: a traced one is welcome); 0 - they are bright
    // anyway (lamps, a roof or a caster the bake left out: no traced shadow there). Empty: 1 everywhere.
    std::vector<float> agreement; // row by row (z), each row along x
    uint32_t columns = 0;
    uint32_t rows = 0;
    std::array<float, 2> origin{}; // the grid's lowest x and z
    float cell = 0.0F;             // a cell's side (world units)
    // (added by the sun's glare) The level's ground height (world y): low among its upward faces (rt_sun.h
    // kGroundShare) - the haze thickens toward it. ground_known false: none found.
    float ground = 0.0F;
    bool ground_known = false;
};
void set_baked_sun(BakedSun sun);
const BakedSun& baked_sun();
// Whether a fit is wanted (the shadow pass asks for one; render_pc fits a new world in its first frame).
void set_baked_sun_wanted(bool wanted);
bool baked_sun_wanted();

// (added by the sun's glare) The sun's light as a sky model gives it (engine/gfx/rt_sky.h; the Vulkan Gpu's
// [Graphics] rt_sky_model, or rt_sun_colour set by hand): `on` false - none given (every effect lights with a white sun,
// as before). Colours display-referred as the game's (1 its white): `colour` the sun's (its brightest channel 1: warm
// when low, white high), `sky` the sky's light overhead. Effects may tint their sunlight by `colour` (the one-bounce
// light does).
struct SunLight {
    bool on = false;
    std::array<float, 3> toward{};               // toward the sun, normalised (the baked sun's, or the override's)
    std::array<float, 3> colour{1.0F, 1.0F, 1.0F};
    std::array<float, 3> sky{};
};
void set_sun_light(const SunLight& light);
SunLight sun_light();

// (added by the sun's glare) Whether a backend draws the sun itself this frame (the Vulkan Gpu's [Graphics] rt_sun: its
// disc, glare and rays): render_pc then leaves out the original's sun sprite, its corona and the lens flare (the flare's
// screen elements hidden). False (the default, Direct3D 9): the original's sun drawn as always.
void set_sun_glare_replaced(bool replaced);
bool sun_glare_replaced();

// (added for the skater's shadow) Whether a backend's shadow pass casts the objects' shadows - the skater's, the peds',
// the vehicles' - from the sun this frame (the Vulkan Gpu's [Graphics] rt_shadows with rt_shadow_objects, a pass drawn):
// the objects' own shadows (engine/objects/shadow_component: the skater's projected one, the peds' blobs) are then
// hidden. False (the default, Direct3D 9, no sun in the level, the pass off): the original's shadows drawn as always.
void set_object_shadows_replaced(bool replaced);
bool object_shadows_replaced();

// (added by the sun's glare) Where the game shows its sun (render_pc: the sun sprite's direction from the camera - a
// level's SetSun / InitSun, the time of day -, toward it, normalised; told each viewport whether it is drawn or
// replaced): the sun the player sees, which needn't be the baked one the level's lighting was made with. Null / false:
// the game shows none.
void set_game_sun(const std::array<float, 3>* toward_sun);
bool game_sun(std::array<float, 3>* toward_sun);

// --- (added by the sky) a sky drawn by the backend - the Vulkan Gpu's [Graphics] rt_sky ------------------------------
// While `on`, render_pc's draw_viewport draws the sky scene's opaque meshes (the backdrop: its dome, a ground ring), then
// tells the listeners (sky_view_drawn -> ViewListener::sky_drawn), who draw the sky over the pixels nothing of the world
// covers (the depth's far value), then draws the sky scene's transparent meshes (its layers: painted cloud cards, haze
// bands, glows) only with `keep_layers`. Off (the default, Direct3D 9): the sky drawn as always, no call.
struct SkyReplacement {
    bool on = false;
    bool keep_layers = true;
};
void set_sky_replacement(const SkyReplacement& replacement);
SkyReplacement sky_replacement();
void sky_view_drawn(const ViewDrawn& view);

// (added by the sky) The level's own sky as it looks: while sky_capture_wanted(), render_pc draws the sky scene's meshes
// (its backdrop and its layers) once per sky scene into a texture from the origin in six directions - kSkyCaptureFaces, a 90-degree square
// view each, laid out 3 across and 2 down (face i at column i % 3, row i / 3), face_size pixels a side, cleared to
// `clear` (the background colour) where the backdrop draws nothing - and hands it over (set_sky_capture). The texture
// stays valid until the next capture (a new serial). What a backend's sky is matched to.
struct SkyCaptureFace {
    std::array<float, 3> forward{}; // the face's centre direction (world, y up)
    std::array<float, 3> up{};      // the direction toward the face's top edge
};
inline constexpr std::array<SkyCaptureFace, 6> kSkyCaptureFaces{{
    {.forward = {1.0F, 0.0F, 0.0F}, .up = {0.0F, 1.0F, 0.0F}},
    {.forward = {-1.0F, 0.0F, 0.0F}, .up = {0.0F, 1.0F, 0.0F}},
    {.forward = {0.0F, 0.0F, 1.0F}, .up = {0.0F, 1.0F, 0.0F}},
    {.forward = {0.0F, 0.0F, -1.0F}, .up = {0.0F, 1.0F, 0.0F}},
    {.forward = {0.0F, 1.0F, 0.0F}, .up = {0.0F, 0.0F, -1.0F}},
    {.forward = {0.0F, -1.0F, 0.0F}, .up = {0.0F, 0.0F, 1.0F}},
}};
struct SkyCapture {
    GpuTexture* texture = nullptr; // A8R8G8B8, 3 x face_size by 2 x face_size; null: none
    uint32_t face_size = 0;
    uint64_t serial = 0;           // a new value per capture (0: none yet)
    uint32_t scene = 0;            // the sky scene's checksum
    std::array<float, 3> clear{};  // the colour where the backdrop drew nothing (0 - 1)
};
void set_sky_capture_wanted(bool wanted);
bool sky_capture_wanted();
void set_sky_capture(const SkyCapture& capture);
SkyCapture sky_capture();

// --- (added by the split comparison) two sets of settings, one each side of a divider ------------------------------
// [Graphics] compare_split (devtools/compare_split.h): the first viewport drawn twice, its left part with the effects'
// settings A and its right part with B (render::graphics_options::compare_*) - each view a crop of the whole viewport's
// projection (crop_clip_space), so every pass sees its side's rectangle and keeps its own per-rectangle history.
// While settings_swap() is true, a backend's settings set again are such a swap between the two sides: the histories,
// matches and caches made for a side are kept (each side's view has its own) - nothing starts again.
void set_settings_swap(bool swapping);
bool settings_swap();

// The clip-space scale and offset that crop a projection to `part` (x, y, width, height - 0..1 of the viewport, y down):
// x' = x * scale_x + w * offset_x, y' = y * scale_y + w * offset_y - {scale_x, scale_y, offset_x, offset_y}. The part's
// square of normalised device coordinates becomes the whole -1..1 (D3D's: x right, y up).
[[nodiscard]] inline std::array<float, 4> crop_clip_space(const std::array<float, 4>& part)
{
    const double width = std::get<2>(part) > 0.0F ? static_cast<double>(std::get<2>(part)) : 1.0;
    const double height = std::get<3>(part) > 0.0F ? static_cast<double>(std::get<3>(part)) : 1.0;
    const double centre_x = (2.0 * static_cast<double>(std::get<0>(part))) + width - 1.0;
    const double centre_y = 1.0 - (2.0 * static_cast<double>(std::get<1>(part))) - height;
    return {static_cast<float>(1.0 / width), static_cast<float>(1.0 / height), static_cast<float>(-centre_x / width),
            static_cast<float>(-centre_y / height)};
}

} // namespace render::rt
