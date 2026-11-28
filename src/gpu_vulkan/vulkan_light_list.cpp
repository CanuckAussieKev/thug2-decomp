// NOT ORIGINAL - the light list's device side, shared (vulkan_light_list.h).
#include "gpu_vulkan/vulkan_light_list.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <memory>
#include <span>
#include <string>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_brdf.h"
#include "gpu_vulkan/vulkan_rt_clouds.h"
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_light_list.h"
#include "engine/gfx/rt_path.h"
#include "engine/gfx/rt_scene.h"

namespace render::vulkan {

namespace {

// An instance's transform and material from its TLAS record (no hit needed), and a triangle's place in the list.
constexpr const char* kInstancesGlsl = R"(
layout(buffer_reference, buffer_reference_align = 4, scalar) readonly buffer ListInstanceWords { float words[]; };
// An instance's object-to-world transform (its TLAS record's rows).
mat4x3 instance_transform(uint i)
{
    ListInstanceWords w = ListInstanceWords(LIST_INSTANCE_ADDRESS);
    uint at = i * 16u;
    vec4 r0 = vec4(w.words[at], w.words[at + 1u], w.words[at + 2u], w.words[at + 3u]);
    vec4 r1 = vec4(w.words[at + 4u], w.words[at + 5u], w.words[at + 6u], w.words[at + 7u]);
    vec4 r2 = vec4(w.words[at + 8u], w.words[at + 9u], w.words[at + 10u], w.words[at + 11u]);
    return mat4x3(vec3(r0.x, r1.x, r2.x), vec3(r0.y, r1.y, r2.y), vec3(r0.z, r1.z, r2.z), vec3(r0.w, r1.w, r2.w));
}
// An instance's material id (its TLAS record's custom index).
uint instance_material(uint i)
{
    ListInstanceWords w = ListInstanceWords(LIST_INSTANCE_ADDRESS);
    return floatBitsToUint(w.words[i * 16u + 12u]) & 0xFFFFFFu;
}
// The triangle `primitive` of `instance`: its place in the emitters' list + 1 (0: not listed).
uint listed_slot(uint instance, uint primitive)
{
    if (instance >= LIST_INSTANCES || primitive >= membership[instance + 1u] - membership[instance]) {
        return 0u;
    }
    return slots[membership[instance] + primitive];
}
)";

// The emitters' scan: one invocation a triangle of the frame's instances (flattened by the instances' first triangles,
// membership[0 .. instances]). A triangle of an instance with a light store (the world's, and since round P10 an object
// drawn with its own colours, relit where it stands) - (round BIAS) whatever its class for rays -, whose light store emits
// at a corner (not hidden, not animated), is listed with its power: (round EMIT) its faces' count (emitter_sides: 1 or
// 2) x pi x its area x its emission's mean luminance - seen from its front - at the
// centroids of its TRIANGLE_LATTICE^2 equal sub-triangles (hit_surface's `emitted`: the store's emit x the texel x the
// lamp texels). (Round PT4b) Every such triangle is listed: one whose few lamp texels fall between those points (Boston:
// 109 of 332) measured nothing and was left out - its lamp's light lost, for its lamp is its surface's (rt_path.h
// analytic_lamps: one owner) and ReSTIR samples nothing else; it is listed at the most it can send (its corners' mean
// emission over its whole area - the texel and the lamp texels are at most 1): picked more often than its light needs (a
// sample between its texels brings nothing - the target says so), never lost. (The list's header word 1 counts the
// candidates: those emitting at a corner with an area - a strip's joining triangles have none.)
constexpr const char* kScanGlsl = R"(
layout(local_size_x = 64) in;
float emitter_power(uint instance, uint primitive, out float area)
{
    uint id = instance_material(instance);
    mat4x3 to_world = instance_transform(instance);
    float sum = 0.0;
    area = 0.0;
    // (round EMIT) looked at from the face it sends its light from (a one-sided emitter's front - emitter_sides)
    HitVertices v;
    if (!hit_vertices(instance, primitive, v)) {
        return 0.0;
    }
    vec3 across = cross(to_world * vec4(v.position[1], 1.0) - to_world * vec4(v.position[0], 1.0),
                        to_world * vec4(v.position[2], 1.0) - to_world * vec4(v.position[0], 1.0));
    float sides = emitter_sides(id);
    area = 0.5 * length(across);
    const float n = float(TRIANGLE_LATTICE);
    // (round SUBPIX) each sub-triangle's emission its mean: the emission map read at the level of a sub-triangle's
    // footprint in its texels (each level the mean of the emission - rt_emission_map.h), so the lattice's mean is the
    // triangle's whole emission however few its lamp texels. Before, its centroids' texels alone: a triangle whose
    // lamp texels fell between them measured nothing.
    float lod = 0.0;
    uint slot = geometry_record(instance).emission_slot;
    if (slot != 0u) {
        vec2 size = vec2(textureSize(sampler2D(material_textures[nonuniformEXT(slot)], material_samplers[0]), 0));
        vec2 du = v.uv[1] - v.uv[0];
        vec2 dv = v.uv[2] - v.uv[0];
        float texels = 0.5 * abs(du.x * dv.y - du.y * dv.x) * size.x * size.y / (n * n);
        lod = texels > 1.0 ? 0.5 * log2(texels) : 0.0;
    }
    for (uint i = 0u; i < TRIANGLE_LATTICE; ++i) {
        for (uint j = 0u; i + j < TRIANGLE_LATTICE; ++j) {
            // the upright sub-triangle (i, j) and, but on the last row, the one turned over beside it
            for (uint turned = 0u; turned < (i + j + 1u < TRIANGLE_LATTICE ? 2u : 1u); ++turned) {
                float c = turned == 0u ? 1.0 / 3.0 : 2.0 / 3.0;
                vec3 emitted;
                if (hit_emission_lod_of(instance, primitive, id, vec2((float(i) + c) / n, (float(j) + c) / n), v, lod,
                                        emitted)) {
                    sum += path_luma(max(emitted, vec3(0.0)));
                }
            }
        }
    }
    return sides * PATH_PI * area * sum / (n * n);
}
void main()
{
    uint g = gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * (gl_NumWorkGroups.x * 64u);
    if (g >= LIST_TRIANGLES) {
        return;
    }
    uint low = 0u;
    uint high = LIST_INSTANCES;
    while (high - low > 1u) {
        uint middle = (low + high) >> 1;
        if (membership[middle] <= g) {
            low = middle;
        } else {
            high = middle;
        }
    }
    uint instance = low;
    uint primitive = g - membership[instance];
    GeometryRecord record = geometry_record(instance);
    uvec3 triangle; // (round GF: read only within the record's triangles, vertices and light stream)
    // (round BIAS) whatever its class for rays: a seen-through or light-adding instance that emits (a lamp's globe, a lit
    // pane) is an emitter as an opaque one is - its light rays stop short of it (ray_light), so it needn't stop them.
    // Before, only instances that cast and weren't seen through were listed: ReSTIR DI never sampled the others (their
    // light on BO's plaza floor missing), the reference found them by its BSDF rays alone.
    if (!geometry_triangle(record, primitive, triangle) || !geometry_light_corners(record, triangle)) {
        return;
    }
    float most = 0.0;
    vec3 corners = vec3(0.0);
    for (uint k = 0u; k < 3u; ++k) {
        uint at = triangle[k] * 32u;
        vec4 flags = unpackUnorm4x8(geometry_word(record.light_address, at + 24u)).zyxw;
        if (flags.a < 0.75) {
            return; // (no store at an animated vertex; a hidden mesh)
        }
        vec3 e = stored_light(geometry_word(record.light_address, at + 28u));
        most = max(most, max(e.r, max(e.g, e.b)));
        corners += max(e, vec3(0.0)) * (1.0 / 3.0);
    }
    if (!(most > 0.0)) {
        return;
    }
    float area = 0.0;
    float power = emitter_power(instance, primitive, area);
    if (!(area > 0.0)) {
        return; // (a strip's joining triangle: no area, no light)
    }
    atomicAdd(emitter_spare, 1u);
    if (!(power > 0.0)) {
        power = emitter_sides(instance_material(instance)) * PATH_PI * area * path_luma(corners); // (no lamp texel at its
        // points: the most it can send)
    }
    if (!(power > 0.0) || isinf(power)) {
        return;
    }
    uint slot = atomicAdd(emitter_count, 1u);
    if (slot < LIST_CAPACITY) {
        emitter_entries[slot] = uvec4(instance, primitive, floatBitsToUint(power), 0u);
        slots[g] = slot + 1u;
    }
}
)";

// The sky's grid (sky_grid_glsl; moved here from the reference by path tracing, stage 5 - ReSTIR GI fills it too).
constexpr const char* kSkyGridGlsl = R"(
layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
    uvec2 c = gl_GlobalInvocationID.xy;
    if (c.x >= SKY_WIDTH || c.y >= SKY_HEIGHT) {
        return;
    }
    vec3 dir = sky_cell_centre(c);
    vec3 radiance;
#ifdef SKY_TABLE
    vec3 table = max(sky_view_linear(dir, pass.sky_table.xyz), vec3(0.0));
#ifdef CLOUDS
    CloudRay ray;
    ray.camera_altitude = pass.cloud_ray.z;
    ray.offset = pass.cloud_ray.xy;
    ray.direction = dir;
    ray.to_sun = pass.cloud_toward.xyz;
    ray.coverage = pass.cloud_ray.w;
    ray.density_scale = pass.cloud_toward.w;
    ray.steps = int(pass.cloud_sun.w);
    ray.jitter = 0.5;
    ray.sun_light = pass.cloud_sun.rgb;
    vec3 bottom;
    vec3 top;
    cloud_sky_light(bottom, top);
    ray.ambient_bottom = bottom;
    ray.ambient_top = top;
    CloudResult r = march_clouds(ray);
    float fade = exp(-max(r.distance, 0.0) / CLOUD_AERIAL_DISTANCE);
    float kept = mix(1.0, clamp(r.transmittance, 0.0, 1.0), fade);
    radiance = (table * kept + max(r.light, vec3(0.0)) * fade) * pass.sky_table.w;
#else
    radiance = table * pass.sky_table.w;
#endif
#else
    radiance = dir.y > 0.0 ? pass.sky.rgb : vec3(0.0);
#endif
    if (any(isnan(radiance)) || any(isinf(radiance))) {
        radiance = vec3(0.0);
    }
    sky_cells[c.y * SKY_WIDTH + c.x] = vec4(radiance, 0.0);
}
)";

} // namespace

SceneLighting scene_lighting(bool every_lamp_analytic, bool keep_dark)
{
    SceneLighting out;
    const rt::DayLight day = rt::day_light();
    const rt::CelWorld world = rt::cel_world();
    const float pre = day.pre_exposure > 0.0F ? day.pre_exposure : 1.0F;
    out.cut = day.cut;
    out.store_emission = rt::store_emission(); // (round PX)
    if (world.relit) {
        out.sun_toward = world.toward;
        out.sun_light = world.sun;
        out.sky = world.sky;
    } else if (day.on) {
        out.sun_toward = day.light;
        for (std::size_t ch = 0; ch < 3; ++ch) {
            core::at(out.sun_light, ch) = core::at(day.sun_light, ch) / pre;
            core::at(out.sky, ch) = core::at(day.sky_light, ch) / pre;
        }
    }
    if (day.on) {
        out.sun_radius_degrees = day.light_radius;
    }
    AtmosphereInput input;
    if (atmosphere_view(&input) && input.exposure > 0.0F) {
        out.table = true;
        out.table_input = input;
        out.cloud_frame = rt_sky_frame();
        out.clouds = out.cloud_frame.clouds;
        out.table_share = out.clouds || !day.on ? 1.0F : day.cloud_sky;
        out.table_factor = out.table_share / input.exposure;
    }
    const std::shared_ptr<const rt::LevelEmitters> level = rt::level_emitters();
    const float open = rt::open_sky_lamps(day, level.get());
    if (level) {
        const std::vector<rt::Emitter> analytic =
            every_lamp_analytic ? level->emitters : rt::path::analytic_lamps(level->emitters, level->surfaced);
        out.owned = static_cast<uint32_t>(level->emitters.size() - analytic.size());
        const float sky_now = rt::lamp_sky_now(day, level.get());
        for (std::size_t i = 0; i < level->emitters.size() && i < level->surfaced.size(); ++i) {
            if (core::at(level->surfaced, i) != 0) {
                rt::Emitter lamp = core::at(level->emitters, i);
                const float on = rt::lamp_switch(lamp.game_light, lamp.switch_sky, sky_now, open);
                lamp.intensity *= on;
                out.owned_power += rt::lights::lamp_power(lamp);
            }
        }
        for (rt::Emitter lamp : analytic) {
            lamp.intensity *= rt::lamp_switch(lamp.game_light, lamp.switch_sky, sky_now, open);
            if (lamp.intensity > 0.0F || keep_dark) {
                lamp.intensity = std::max(lamp.intensity, 0.0F);
                out.lamps.push_back(lamp);
            }
        }
    }
    if (!level || !level->solved) {
        for (const rt::Light& light : rt::frame_record().lights) {
            rt::Emitter lamp;
            if (rt::lights::point_emitter(light, std::clamp(open, 0.0F, 1.0F), &lamp) && lamp.intensity > 0.0F) {
                out.lamps.push_back(lamp);
                ++out.points;
            }
        }
    }
    return out;
}

uint64_t lighting_key_of(const SceneLighting& lighting)
{
    uint64_t key = 1469598103934665603ULL;
    const auto mix = [&key](const void* data, std::size_t bytes) {
        const std::span<const uint8_t> from(static_cast<const uint8_t*>(data), bytes);
        for (const uint8_t byte : from) {
            key = (key ^ byte) * 1099511628211ULL;
        }
    };
    mix(&lighting.cut, sizeof(lighting.cut));
    mix(lighting.sun_toward.data(), sizeof(lighting.sun_toward));
    mix(lighting.sun_light.data(), sizeof(lighting.sun_light));
    mix(&lighting.sun_radius_degrees, sizeof(lighting.sun_radius_degrees));
    mix(lighting.sky.data(), sizeof(lighting.sky));
    mix(&lighting.table, sizeof(lighting.table));
    mix(&lighting.clouds, sizeof(lighting.clouds));
    mix(&lighting.table_factor, sizeof(lighting.table_factor));
    for (const rt::Emitter& lamp : lighting.lamps) {
        mix(lamp.position.data(), sizeof(lamp.position));
        mix(&lamp.intensity, sizeof(lamp.intensity));
    }
    return key;
}

std::array<float, kLampRecordFloats> lamp_record(const rt::Emitter& lamp)
{
    return {core::at(lamp.position, 0),
            core::at(lamp.position, 1),
            core::at(lamp.position, 2),
            lamp.size,
            core::at(lamp.colour, 0) * lamp.intensity,
            core::at(lamp.colour, 1) * lamp.intensity,
            core::at(lamp.colour, 2) * lamp.intensity,
            lamp.reach,
            0.0F,
            lamp.core,
            static_cast<float>(static_cast<uint32_t>(lamp.shape)),
            0.0F,
            core::at(lamp.axis, 0),
            core::at(lamp.axis, 1),
            core::at(lamp.axis, 2),
            lamp.half_length};
}

bool disc_of(const SceneLighting& lighting, std::array<float, 4>* sun, std::array<float, 4>* radiance)
{
    constexpr double kDegreesToRadians = 0.017453292519943295;
    const float most =
        std::max({core::at(lighting.sun_light, 0), core::at(lighting.sun_light, 1), core::at(lighting.sun_light, 2)});
    const auto cos_radius =
        static_cast<float>(std::cos(static_cast<double>(lighting.sun_radius_degrees) * kDegreesToRadians));
    const std::array<float, 3>& toward = lighting.sun_toward;
    const float toward_length = std::sqrt((core::at(toward, 0) * core::at(toward, 0)) +
                                          (core::at(toward, 1) * core::at(toward, 1)) +
                                          (core::at(toward, 2) * core::at(toward, 2)));
    if (!(most > 0.0F) || !(toward_length > 0.0F) || !(cos_radius < 1.0F)) {
        *sun = {0.0F, 1.0F, 0.0F, kNoDisc};
        *radiance = {0.0F, 0.0F, 0.0F, 1.0F};
        return false;
    }
    *sun = {core::at(toward, 0) / toward_length, core::at(toward, 1) / toward_length, core::at(toward, 2) / toward_length,
            cos_radius};
    const rt::brdf::Rgb disc = rt::path::disc_radiance(lighting.sun_light, cos_radius);
    *radiance = {core::at(disc, 0), core::at(disc, 1), core::at(disc, 2), rt::path::cone_solid_angle(cos_radius)};
    return true;
}

SkyGridSource sky_grid_source(const SceneLighting& lighting)
{
    if (!lighting.table) {
        return SkyGridSource::kEven;
    }
    return lighting.clouds ? SkyGridSource::kTableClouds : SkyGridSource::kTable;
}

SkyGridInputs sky_grid_inputs(const SceneLighting& lighting)
{
    SkyGridInputs out;
    out.sky = {core::at(lighting.sky, 0), core::at(lighting.sky, 1), core::at(lighting.sky, 2), lighting.table ? 1.0F : 0.0F};
    out.sky_table = {core::at(lighting.table_input.sun, 0), core::at(lighting.table_input.sun, 1),
                     core::at(lighting.table_input.sun, 2), lighting.table_factor};
    const RtSkyFrame& clouds = lighting.cloud_frame;
    out.cloud_ray = {static_cast<float>(core::at(clouds.offset, 0)), static_cast<float>(core::at(clouds.offset, 1)),
                     static_cast<float>(clouds.camera_altitude), clouds.coverage};
    out.cloud_sun = {core::at(clouds.sun_light, 0), core::at(clouds.sun_light, 1), core::at(clouds.sun_light, 2),
                     static_cast<float>(std::max(clouds.steps, 1U))};
    out.cloud_toward = {core::at(clouds.cloud_toward, 0), core::at(clouds.cloud_toward, 1), core::at(clouds.cloud_toward, 2),
                        1.0F};
    return out;
}

std::string sky_grid_prelude(SkyGridSource source, uint32_t transmittance_binding, uint32_t sky_view_binding,
                             uint32_t cloud_noise_binding)
{
    std::string out;
    if (source != SkyGridSource::kEven) {
        out += "#define SKY_TABLE\n" + atmosphere_glsl(transmittance_binding, sky_view_binding);
        if (source == SkyGridSource::kTableClouds) {
            out += "#define CLOUDS\n" + cloud_glsl(cloud_noise_binding) + rt_sky_cloud_light_glsl();
            out += "const float CLOUD_AERIAL_DISTANCE = " + std::to_string(kCloudAerialDistance) + ";\n";
        }
    }
    return out;
}

std::string_view sky_grid_glsl() { return kSkyGridGlsl; }

std::vector<std::array<uint32_t, 2>> sky_alias_words(const rt::lights::SkyDistribution& sky)
{
    std::vector<std::array<uint32_t, 2>> alias;
    alias.reserve(sky.rows.size() + sky.cells.size());
    for (const std::vector<rt::lights::AliasEntry>* table : {&sky.rows, &sky.cells}) {
        for (const rt::lights::AliasEntry& entry : *table) {
            alias.push_back({std::bit_cast<uint32_t>(entry.threshold), entry.alias});
        }
    }
    return alias;
}

std::string_view light_instances_glsl() { return kInstancesGlsl; }

std::string_view emitter_scan_glsl() { return kScanGlsl; }

std::string light_pool_reader_glsl(std::string_view pool)
{
    const std::string p(pool);
    return "#define LIGHT_POOL_SIZE " + std::to_string(kLightPoolSize) +
           "u\nlayout(buffer_reference, buffer_reference_align = 16, std430) readonly buffer LightPoolWords { uvec4 words[]; };\n"
           "#define light_pool (LightPoolWords(" + p + ".xy).words)\n#define LL_POOL (" + p + ".z != 0u)\n"
           "#define LL_POOL_SHARE uintBitsToFloat(" + p + ".w)\n";
}

namespace {
// (round PERF3) The light grid's slot for a point: its cell (the cells kept by their place in the world - each axis's index
// the world cell's modulo the cells across, so a cell keeps its index while the grid follows the camera) and the slot `u`
// picks there - false outside the grid or where the cell wasn't made this frame (the grid makes only the cells read
// lately: its state word's x the frame it was made, y the frame it was last wanted - written here; the caller then takes
// the pool - the same light, from fewer near candidates, for the frame it takes the grid to make the cell).
constexpr std::string_view kLightGridSlotGlsl = R"(
bool light_grid_slot(vec3 cell_at, float u, out uint slot)
{
    slot = 0u;
    uvec4 cells = light_grid[1];
    if (cells.w == 0u || any(lessThan(cell_at, vec3(0.0))) || any(greaterThanEqual(cell_at, vec3(cells.xyz)))) {
        return false;
    }
    uvec4 head = light_grid[2]; // (the frame's stamp, the cells' shift on each axis)
    uvec3 c = (uvec3(cell_at) + head.yzw) % cells.xyz;
    uint cell = (c.y * cells.z + c.z) * cells.x + c.x;
    uint state_at = 2u * (2u + cells.x * cells.y * cells.z * cells.w) + cell;
    uvec4 state = light_grid[state_at];
    if (state.y != head.x) {
        light_grid_marks[state_at].y = head.x;
    }
    if (state.x != head.x) {
        return false;
    }
    slot = 2u + cell * cells.w + min(uint(u * float(cells.w)), cells.w - 1u);
    return true;
}
)";
} // namespace

std::string light_grid_reader_glsl(std::string_view grid)
{
    const std::string g(grid);
    return "#define LL_GRID\nlayout(buffer_reference, buffer_reference_align = 16, std430) readonly buffer LightGridWords { "
           "uvec4 words[]; };\nlayout(buffer_reference, buffer_reference_align = 16, std430) buffer LightGridMarks { "
           "uvec4 words[]; };\n#define light_grid (LightGridWords(" + g + ".xy).words)\n#define light_grid_marks "
           "(LightGridMarks(" + g + ".xy).words)\n#define LL_GRID_ON (" + g + ".z != 0u)\n" + std::string(kLightGridSlotGlsl);
}

std::string_view light_sample_glsl()
{
    static constexpr std::string_view kSource = R"(
#ifndef LL_LOCAL_CANDIDATES
#define LL_LOCAL_CANDIDATES 8u // (round DD) the local lights' candidates a sample resamples (the pool's - LL_POOL)
#endif
// (round FA) Whether this sample may take its local lights' candidates from the light grid's cells (LL_GRID). A cell's
// picks are shared by every point in it (128 units) for the frame: the error they carry is the same over that whole
// region and new every frame - ReGIR's correlation (Boksansky et al. 2021). An estimator that resamples over frames
// (ReSTIR GI's diffuse reservoirs) averages it out; a one-shot one - GI's specular ray, every frame's own - passed it
// to the frame whole: the lamp pools on BO's floors brightening and darkening by 2 - 4 x from frame to frame, which
// Ray Reconstruction keeps as picture. Such a caller sets this false around its sample: its candidates then come from
// the frame's pool, each point's its own (RTXDI's presampled light tiles).
bool ll_cell_candidates = true;
// The disc's light the clouds let through to `at` (their shadow map - the shadows pass's clouds_through); 1 without it.
float sun_kept(vec3 at)
{
#ifdef CLOUD_MAP
    return cloud_map_kept(at, LL_CLOUD_PLACE, LL_CLOUD_LIGHT);
#else
    return 1.0;
#endif
}
// The sky's radiance toward `dir` (the disc not in it): the grid in use's cell.
vec3 sky_radiance(vec3 dir) { return LL_SKY_DIST > 0.0 ? sky_cells[sky_cell_of(dir)].rgb : vec3(0.0); }
// The density (solid angle) of the sky's sampling toward `dir`: its cell's luminance over the grid's integral.
float sky_density(vec3 dir) { return LL_SKY_DIST > 0.0 ? path_luma(sky_cells[sky_cell_of(dir)].rgb) * LL_SKY_DIST : 0.0; }

// One light of the full list (the disc, the sky, an emitting triangle, a lamp - picked by power), one ray to it (the one
// light ray, `mask`): the light arriving at `p` along `dir` x what of it gets through, over the sample's density - x a
// BSDF x the cosine (a surface) or x a phase (a medium) it is the estimate of the light reflected or scattered. `n` /
// `ng` the receiver's shading and geometric normals - directions below either bring nothing (no ray); vec3(0.0) for a
// point in a medium (every direction).
vec3 light_sample(vec3 p, vec3 n, vec3 ng, uint mask, inout uint rng, out vec3 dir)
{
    float u = path_next(rng);
    float u1 = path_next(rng);
    float u2 = path_next(rng);
    float u3 = path_next(rng);
    float u4 = path_next(rng);
    dir = vec3(0.0, 1.0, 0.0);
    uint count = LL_COUNT;
    if (count == 0u) {
        return vec3(0.0);
    }
    uint picked = light_pick(u, count);
    LightEntry e = light_entries[picked];
    if (!(e.pdf > 0.0)) {
        return vec3(0.0);
    }
    bool surface = dot(ng, ng) > 0.0;
    uint kind = light_kind(e.payload);
    // (round PERF3) each kind's light and reach, then the one light ray for the three - one copy of the ray's code in the
    // shader: three inlined copies of ray_light's two walks made the path tracer's shaders far larger (ReSTIR GI's
    // samples and cache passes 255 registers with spills, 390 - 420 KB of code; a pass's occupancy goes with its registers)
    vec3 carried = vec3(0.0);
    float reach = LL_REACH;
    if (kind == LIGHT_DISC) {
#ifdef LL_NO_DISC
        return vec3(0.0); // (round FL: the caller estimates the disc's light itself - the rest of the list by this sample)
#endif
        dir = path_to_world(LL_SUN.xyz, path_cone_direction(LL_SUN.w, u1, u2));
        if (surface && (dot(dir, ng) <= 0.0 || dot(dir, n) <= 0.0)) {
            return vec3(0.0);
        }
        float pdf = e.pdf / LL_SUN_RADIANCE.w;
        carried = LL_SUN_RADIANCE.rgb * sun_kept(p) / pdf;
    } else if (kind == LIGHT_SKY) {
        uint cell;
        dir = sky_sample(u1, u2, u3, u4, cell);
        if (surface && (dot(dir, ng) <= 0.0 || dot(dir, n) <= 0.0)) {
            return vec3(0.0);
        }
        vec3 radiance = sky_cells[cell].rgb;
        float pdf = e.pdf * path_luma(radiance) * LL_SKY_DIST;
        if (!(pdf > 0.0)) {
            return vec3(0.0);
        }
        carried = radiance / pdf;
    } else {
    LocalLight l;
#ifdef LL_POOL
    // (round OM - RTXDI's presampled light tiles, as the fog's candidates) the local group's light from the pool: a pick
    // of the group by power and a uniform point on it, as local_light_at's - its pdf the group's; x the group's share of
    // the full list's picks (LL_POOL_SHARE) it is the pdf of the pick above. The same distribution; the triangle's corners,
    // instance and emission texel not read here.
    // (round DD) Resampled importance sampling over LL_LOCAL_CANDIDATES of them (Talbot et al. 2005 - what RTXDI's
    // ReSTIR GI does for its secondary surfaces' local lights, and the fog's list_lights here): each a pool slot, weighed
    // toward the light it sends to `p` (its luminance x the receiver's cosine - unshadowed), one chosen and its ray
    // traced. One pick by power alone left the light of a small or far emitter with a pick probability far below its
    // share here - its rare picks fireflies (BO at night: bright specks over the street, the bank's ceiling and the bus
    // stop, moving frame to frame - ReSTIR GI's samples reused, and Ray Reconstruction keeps such outliers).
    if (LL_POOL) {
        float wsum = 0.0;
        float chosen_target = 0.0;
        LocalLight c;
        // (round DO - RTXDI's ReGIR for secondary surfaces) a point inside ReSTIR DI's light grid takes its candidates from
        // its cell's picks (light_grid_reader_glsl) - each a light near it with its slot's pdf (1 / its unbiased
        // contribution weight: RIS over candidates of their own weights stays unbiased - generalised RIS, Lin et al. 2022);
        // each candidate's cell that of the point moved by up to half a cell (ReSTIR DI's pixels' jitter: no cell edges
        // in the picture) - its own: the candidates of one point spread over the neighbouring cells' picks, not all from
        // the 16 its whole cell shares this frame (one cell's picks alone left faint grain the denoiser kept)
        for (uint k = 0u; k < LL_LOCAL_CANDIDATES; ++k) {
            float uk = k == 0u ? u1 : path_next(rng);
            float uc = path_next(rng);
            LightPick pick;
            bool from_grid = false;
#ifdef LL_GRID
            if (LL_GRID_ON && ll_cell_candidates) {
                vec4 place = uintBitsToFloat(light_grid[0]);
                vec3 jitter = (vec3(path_next(rng), path_next(rng), path_next(rng)) - 0.5) * place.w;
                uint slot;
                if (place.w > 0.0 && light_grid_slot(floor((p + jitter - place.xyz) / place.w), uk, slot)) {
                    pick = light_pick_words(light_grid[2u * slot], light_grid[2u * slot + 1u]);
                    from_grid = true;
                }
            }
#endif
            if (!from_grid) {
                pick = light_pool_read(min(uint(uk * float(LIGHT_POOL_SIZE)), LIGHT_POOL_SIZE - 1u));
            }
            if (!light_seen_from(pick, p, c) || !(c.pdf > 0.0)) {
                continue;
            }
            // (the receiver's side as the sample below is kept: above both normals)
            float cosine = surface ? (dot(c.dir, ng) > 0.0 && dot(c.dir, n) > 1.0e-4 ? dot(c.dir, n) : 0.0) : 1.0;
            float target = path_luma(c.light) * cosine;
            float w = target / c.pdf;
            if (!(w > 0.0) || isinf(w)) {
                continue;
            }
            wsum += w;
            if (uc * wsum < w) {
                l = c;
                chosen_target = target;
            }
        }
        if (!(chosen_target > 0.0)) {
            return vec3(0.0);
        }
        // (the estimate's weight: the candidates' mean weight over the chosen one's target, in place of 1 / its pdf)
        l.pdf = LL_POOL_SHARE * chosen_target * float(LL_LOCAL_CANDIDATES) / wsum;
    } else
#endif
    if (!local_light_at(picked, count, vec2(u1, u2), p, l) || !(l.pdf > 0.0)) {
        return vec3(0.0);
    }
    dir = l.dir;
    if (surface && (dot(dir, ng) <= 0.0 || !(dot(dir, n) > 1.0e-4))) {
        return vec3(0.0);
    }
    carried = l.light / l.pdf;
    reach = l.reach;
    }
    return carried * ray_light_leaving(mask, p, ng, dir, reach);
}

#ifdef LL_CACHE
// --- (round MB) the radiance cache: the light further bounces bring --------------------------------------------------
// NVIDIA's SHaRC (spatially hashed radiance cache - Cyberpunk 2077's path tracing reads it where its paths end; Binder
// et al. 2019's hash grid without its jitter): a hash table of cells of the world, each a point's position
// quantised by a side that grows with its distance from the camera (powers of 2 - SHaRC's levels) and the axis its
// surface faces (the two sides of a wall apart). An entry holds the bounce a white diffuser there gets - the light off
// other surfaces (E / pi; the lights' own direct light - the disc, the sky, the emitters, the lamps - is not in it: next-
// event estimation owns it at every vertex). Written by ReSTIR GI's sparse update paths (vulkan_restir_gi.cpp), read at
// its secondary vertices and (round WF) wherever a surface the prepass doesn't hold is lit by the list - the reflections'
// hits: there the light they reflect is their direct light (light_sample) + their reflectance x the cache's bounce.
// (Round BUDGET2) An entry also holds the direct light a white diffuser there gets (E / pi, the update paths' light
// samples): a path may then END at a cell - its vertex's light its reflectance x the cell's whole light, no light sample -
// where its footprint covers the cell (cache_lookup_whole: SHaRC's intended use - its paths end at the cache, not only
// on a miss). The bounce alone stays what the readers that sample their own direct light take.
// (round BUDGET2) An entry is CACHE_STRIDE uvec4s: its key, its bounce's sums (low, high words) and light, then the light
// a white diffuser there gets straight from the lights (E / pi - next-event estimation's, the same unit) - its sums and
// light - and a spare: the second half lets a path END at the cache (SHaRC's intended use - cache_lookup_whole).
const uint CACHE_STRIDE = 8u;
uint cache_hash(uint x)
{
    // (the "lowbias32" integer hash - Chris Wellons 2018)
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}
// The table's slot a point's cell starts its probes at and its check word (another hash of the cell - never 0).
void cache_key(vec3 at, vec3 ng, out uint slot, out uint check)
{
    float distance = length(at - LL_EYE);
    float level = max(floor(log2(max(distance * LL_CACHE_CELLS / CACHE_CELL_LEAST, 1.0))), 0.0);
    ivec3 c = ivec3(floor(at / (CACHE_CELL_LEAST * exp2(level))));
    vec3 a = abs(ng);
    uint axis = a.x >= a.y && a.x >= a.z ? 0u : (a.y >= a.z ? 1u : 2u);
    float along = axis == 0u ? ng.x : (axis == 1u ? ng.y : ng.z);
    uint w = uint(level) | ((axis * 2u + (along < 0.0 ? 1u : 0u)) << 8);
    uvec3 u = uvec3(c);
    slot = cache_hash(u.x ^ cache_hash(u.y ^ cache_hash(u.z ^ cache_hash(w)))) & (CACHE_CAPACITY - 1u);
    check = cache_hash(u.z ^ cache_hash(u.y + cache_hash(u.x ^ cache_hash(w ^ 0x9E3779B9u))));
    check = check == 0u ? 1u : check;
}
// (round BIAS) The bounce at a point - true where the cache has CACHE_TRUSTED_SAMPLES for its cell; false where it has
// fewer or none (a cell not seen yet, or claimed this frame and not resolved): a miss (`bounce` the young entry's light, 0
// for none). SHaRC's query (SharcGetCachedRadiance) is the same test, and on a miss its caller goes on tracing the path -
// a miss is not "no light": read as 0 it darkened every newly seen region's bounce for the ~64 frames the cache takes to
// fill (round MV: GI's diffuse there 0.57 - 0.73 x converged). ReSTIR GI's secondary vertices continue the path
// (trace_secondary's go_on), and so does the front layer's point (round LAYER - vulkan_front_layer.cpp traced_bounce);
// the readers that can't trace on (the update paths' last vertex, the reflections' and the front layer's hits -
// cache_bounce) take the young entry's light, else 0. With the cache off: true and 0 (the one-bounce tier).
// (SHaRC's sample threshold: the kept samples an entry needs before a caller that can go on takes it - a young entry's
// update paths end in the cache's other young entries, low: trusted at 8 the cold room's first frames were -7 %, at 32
// within 0.2 % - thug2_vulkan_restir_gi_check)
// (round PERF) The threshold counts the samples the entry has taken since it was claimed (its key's z - the resolve pass
// adds each frame's), not its light's faded weight: that weight fades by 1 - 1 / CACHE_FRAMES every frame, so an entry
// fewer than 2 samples a frame reach (a cell behind the camera, only an update path's later vertex meets) settled under
// 32 and stayed a miss for good - every secondary vertex there continued its path, up to 12 vertices with a light sample
// each (BO at night, 1707 x 960: GI's temporal pass 16.5 ms, 7.3 ms with every entry trusted). SHaRC's own threshold is
// a sample count too.
// (round PERF2) ... or CACHE_TRUSTED_FRAMES frames since it was claimed (its key's w - the resolve pass counts them),
// with any samples at all: what a young entry lacks is the frames its light takes to fold in the bounces beyond it (each
// frame's update paths add three more), not a number of samples - a cell few paths reach (behind the camera, a far
// wall) is as settled after CACHE_FRAMES (its history's length) as a busy one. Counting its samples alone kept such cells
// a miss for 32+ frames, and every secondary vertex there continued its path (BO at night, 1707 x 960: GI 20.8 -> 14.5
// ms with both lobes traced). The cold room (thug2_vulkan_restir_gi_check, its first two frames) is unchanged: there
// every entry is younger than both.
const uint CACHE_TRUSTED_SAMPLES = 32u;
const uint CACHE_TRUSTED_FRAMES = 16u;
// (round BUDGET2) the direct part's kept samples a path may end on (16: the continued pass +0.4 ms at the bench spot, the
// GI term's error after a fast turn no different within the sessions' spread)
const float CACHE_TRUSTED_DIRECT = 4.0;
// The entry of a point's cell, CACHE_CAPACITY for none.
uint cache_find(vec3 at, vec3 ng)
{
    uint slot;
    uint check;
    cache_key(at, ng, slot, check);
    for (uint k = 0u; k < CACHE_PROBES; ++k) {
        uint i = (slot + k) & (CACHE_CAPACITY - 1u);
        uint there = cache[i * CACHE_STRIDE].x;
        if (there == check) {
            return i;
        }
        if (there == 0u) {
            break;
        }
    }
    return CACHE_CAPACITY;
}
bool cache_lookup(vec3 at, vec3 ng, out vec3 bounce)
{
    bounce = vec3(0.0);
    if (!(LL_CACHE_ON)) {
        return true;
    }
    uint i = cache_find(at, ng);
    if (i == CACHE_CAPACITY) {
        return false;
    }
    vec4 l = uintBitsToFloat(cache[i * CACHE_STRIDE + 3u]);
    if (!(l.w > 0.0) || any(isnan(l.rgb)) || any(isinf(l.rgb))) {
        return false;
    }
    bounce = max(l.rgb, vec3(0.0));
    // (a young entry: still a miss - its light is returned all the same)
    uvec4 key = cache[i * CACHE_STRIDE];
    return key.z >= CACHE_TRUSTED_SAMPLES || key.w >= CACHE_TRUSTED_FRAMES;
}
// (round BUDGET2) A cell's side at a point (world units - cache_key's quantisation): how far the cache's light is smeared.
float cache_cell_side(vec3 at)
{
    float distance = length(at - LL_EYE);
    return CACHE_CELL_LEAST * exp2(max(floor(log2(max(distance * LL_CACHE_CELLS / CACHE_CELL_LEAST, 1.0))), 0.0));
}
// (round BUDGET2) The whole light a white diffuser at a point gets (E / pi): its bounce and the lights' own (direct) -
// true where both are trusted (the bounce as cache_lookup; the direct part has its samples). SHaRC's intended use: a path
// ENDS where it meets a trusted cell its footprint covers - no light sample, no ray past it (the vertex then sends its
// reflectance x this, a view-averaged light: SHaRC's cached radiance is too).
bool cache_lookup_whole(vec3 at, vec3 ng, out vec3 bounce, out vec3 direct)
{
    bounce = vec3(0.0);
    direct = vec3(0.0);
    uint i = LL_CACHE_ON ? cache_find(at, ng) : CACHE_CAPACITY;
    if (i == CACHE_CAPACITY) {
        return false;
    }
    uvec4 key = cache[i * CACHE_STRIDE];
    vec4 l = uintBitsToFloat(cache[i * CACHE_STRIDE + 3u]);
    vec4 d = uintBitsToFloat(cache[i * CACHE_STRIDE + 6u]);
    if (!(key.z >= CACHE_TRUSTED_SAMPLES || key.w >= CACHE_TRUSTED_FRAMES) || !(l.w > 0.0) ||
        !(d.w >= CACHE_TRUSTED_DIRECT) || any(isnan(l.rgb)) || any(isinf(l.rgb)) || any(isnan(d.rgb)) || any(isinf(d.rgb))) {
        return false;
    }
    bounce = max(l.rgb, vec3(0.0));
    direct = max(d.rgb, vec3(0.0));
    return true;
}
// (round LW2) Whether a path may END at a vertex whose specular lobe reflects `specular_albedo` of an even light (its
// directional albedo - brdf_specular_albedo at the view): the cell's whole light is E / pi, the light a white diffuser
// gets - exact for the diffuse lobe (a Lambertian surface sends albedo x E / pi whatever the light's directions), not for
// the specular one, which sends the light from its own directions: the cell's light taken as even, a lobe that looks
// away from the lights (a floor met at a grazing angle, its lobe on the horizon, lamps overhead) sent its albedo x the
// whole E / pi - paths ending at the cache came out 15 % (Boston at night) - 25 % (Training) brighter in ReSTIR GI's
// light than paths traced on, which sample the lights through the vertex's BSDF. Where the lobe reflects more than
// CACHE_END_SPECULAR_MOST the path goes on (its light sample takes the lobe's own directions); under it the error is at
// most a few % of the vertex's light (Boston: ending vs traced 1.00, the continued paths +0.07 ms at 853 x 480).
const float CACHE_END_SPECULAR_MOST = 0.05;
bool cache_may_end(vec3 specular_albedo) { return path_luma(specular_albedo) <= CACHE_END_SPECULAR_MOST; }
// The bounce at a point, 0 on a miss (a reader that can't continue the path: the front layer's).
vec3 cache_bounce(vec3 at, vec3 ng)
{
    vec3 bounce;
    cache_lookup(at, ng, bounce);
    return bounce;
}
#endif
)";
    return kSource;
}

std::string_view radiance_cache_write_glsl()
{
    // (round PERF - moved from ReSTIR GI's passes: one copy for every pass that writes the cache)
    static constexpr std::string_view kSource = R"(
// (round MB) The cache's entry for a point (claimed if it has none yet: the probes' first free slot); false when the
// probes are all taken by other cells (the sample is dropped).
bool cache_claim(vec3 at, vec3 ng, out uint entry)
{
    uint slot;
    uint check;
    cache_key(at, ng, slot, check);
    for (uint k = 0u; k < CACHE_PROBES; ++k) {
        uint i = (slot + k) & (CACHE_CAPACITY - 1u);
        uint there = atomicCompSwap(cache[i * CACHE_STRIDE].x, 0u, check);
        if (there == 0u || there == check) {
            cache[i * CACHE_STRIDE].y = LL_CACHE_FRAME;
            entry = i;
            return true;
        }
    }
    entry = 0u;
    return false;
}
// One word of a 64-bit sum: the low word added, its carry into the high word (32-bit atomics alone).
// (`low` the sums' low words' place in the entry: 1 the bounce's, 4 - round BUDGET2 - the direct light's; the high words
// next to them)
void cache_add_word(uint entry, uint low, uint channel, uint value)
{
    if (value == 0u) {
        return;
    }
    uint at = entry * CACHE_STRIDE + low;
    uint before = channel == 0u ? atomicAdd(cache[at].x, value)
                                : (channel == 1u ? atomicAdd(cache[at].y, value) : atomicAdd(cache[at].z, value));
    if (before + value < before) {
        if (channel == 0u) {
            atomicAdd(cache[at + 1u].x, 1u);
        } else if (channel == 1u) {
            atomicAdd(cache[at + 1u].y, 1u);
        } else {
            atomicAdd(cache[at + 1u].z, 1u);
        }
    }
}
void cache_add_sums(uint entry, uint low, vec3 light, inout uint rng)
{
    vec3 fixed_point = clamp((any(isnan(light)) || any(isinf(light)) ? vec3(0.0) : light) * LL_CACHE_EXPOSURE * CACHE_SCALE, vec3(0.0), vec3(4.0e9));
    uvec3 words = uvec3(fixed_point + vec3(path_next(rng), path_next(rng), path_next(rng)));
    cache_add_word(entry, low, 0u, words.r);
    cache_add_word(entry, low, 1u, words.g);
    cache_add_word(entry, low, 2u, words.b);
    atomicAdd(cache[entry * CACHE_STRIDE + low].w, 1u);
}
// A sample of a point's bounce added to its entry's sums: the frame's exposed light in fixed point, rounded at random
// (unbiased however small the light).
void cache_add(vec3 at, vec3 ng, vec3 light, inout uint rng)
{
    uint entry;
    if (cache_claim(at, ng, entry)) {
        cache_add_sums(entry, 1u, light, rng);
    }
}
// (round BUDGET2) A sample of the direct light a white diffuser at a point gets (E / pi) added to its entry's other sums.
void cache_add_direct(vec3 at, vec3 ng, vec3 light, inout uint rng)
{
    uint entry;
    if (cache_claim(at, ng, entry)) {
        cache_add_sums(entry, 4u, light, rng);
    }
}
)";
    return kSource;
}

namespace {
// (round VEIL) local_light_glsl in two: what a pick sends a point (no scene table read - the forward pass's lamps read
// it alone: local_light_seen_glsl) and the pick made from the list (the triangles' corners, emission and instances)
constexpr std::string_view kLocalSeenGlsl = R"(
struct LocalLight {
    vec3 light;
    vec3 dir;
    float reach;
    float pdf;
};
float local_light_offset(vec3 p) { return 0.02 + 2.0e-5 * max(abs(p.x), max(abs(p.y), abs(p.z))); }
// (round PF) A point of a local light as the list picks it - its entry `e`, and `uv` the point on a triangle: what of it
// doesn't depend on who looks (a triangle's point y, its normal, its emission there and the pdf per unit of its area; a
// lamp's record index and the entry's pdf). local_light_at = this + light_seen_from; the presampled pool (below) keeps it.
struct LightPick {
    vec3 y;
    vec3 n;
    vec3 emitted;
    float pdf;     // (0: no light here)
    uint lamp;     // (a lamp's record; LIGHT_PICK_TRIANGLE: a triangle's point)
    float sides;   // (round EMIT - a triangle's: emitter_sides; n its front, (b - a) x (c - a) x EMITTER_FRONT_SIGN)
};
const uint LIGHT_PICK_TRIANGLE = 0xFFFFFFFFu;
LightPick no_light_pick() { return LightPick(vec3(0.0), vec3(0.0, 1.0, 0.0), vec3(0.0), 0.0, LIGHT_PICK_TRIANGLE, 2.0); }
// The light a picked point sends `at` (LocalLight, as local_light_at's).
bool light_seen_from(LightPick p, vec3 at, out LocalLight l)
{
    l = LocalLight(vec3(0.0), vec3(0.0, 1.0, 0.0), 0.0, 0.0);
    if (!(p.pdf > 0.0)) {
        return false;
    }
    if (p.lamp == LIGHT_PICK_TRIANGLE) {
        vec3 to = p.y - at;
        float d2 = dot(to, to);
        float d = sqrt(d2);
        if (!(d > 0.0)) {
            return false;
        }
        vec3 dir = to / d;
        float cos_there = emitter_cos(p.sides, p.n * EMITTER_FRONT_SIGN, -dir); // (round EMIT: a one-sided one's front)
        if (!(cos_there > 0.0)) {
            return false;
        }
        l.light = max(p.emitted, vec3(0.0)) * (cos_there / d2);
        l.dir = dir;
        l.reach = max(d * 0.999 - local_light_offset(p.y), 0.0);
        l.pdf = p.pdf;
        return max(l.light.r, max(l.light.g, l.light.b)) > 0.0;
    }
    LightRecord lamp = lamps[p.lamp];
    vec3 from;
    lamp_geometry(lamp, at, vec3(0.0), from); // (where its light comes from - the point's own: no facing yet)
    vec3 to = from - at;
    float d = length(to);
    if (!(d > 0.0)) {
        return false;
    }
    vec3 dir = to / d;
    float g = lamp_geometry(lamp, at, dir, from);
    if (!(g > 0.0)) {
        return false;
    }
    l.light = max(lamp.colour.rgb, vec3(0.0)) * (PATH_PI * g);
    l.dir = dir;
    l.reach = max(d - lamp.at.w, 0.0);
    l.pdf = p.pdf;
    return max(l.light.r, max(l.light.g, l.light.b)) > 0.0;
}

// (round PF - NVIDIA RTXDI's presampled light tiles: Wyman and Panteleev 2021, "Rearchitecting spatiotemporal resampling
// for production") The pool: LIGHT_POOL_SIZE picks of the local group made once a frame (light_pool_write - an entry by
// power, a uniform point on a triangle, as local_light_at's), two uvec4 each: y and the pdf (a lamp: its record index in
// x, the pdf negative), the emission and the normal (octahedral, 16-bit). A pass that resamples many candidates a point
// (the fog's) takes them from here - a random slot each, the same distribution as a pick of its own - and reads two words
// instead of the triangle's corners, its instance and its emission texel. The caller declares `uvec4 light_pool[]`.
#ifdef LIGHT_POOL_SIZE
vec2 light_pool_oct(vec3 n)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 w = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return n.z >= 0.0 ? n.xy : w;
}
vec3 light_pool_unoct(vec2 e)
{
    vec3 n = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}
#ifdef LIGHT_POOL_WRITE
void light_pool_write(uint slot, LightPick p)
{
    bool lamp = p.lamp != LIGHT_PICK_TRIANGLE;
    light_pool[2u * slot] = lamp ? uvec4(p.lamp, 0u, 0u, floatBitsToUint(-p.pdf))
                                 : uvec4(floatBitsToUint(p.y), floatBitsToUint(p.pdf));
    // (round EMIT) a one-sided triangle's point: the sign bit of its emission's red word set (the emission is never
    // negative - light_pick_at's max), its front in the normal
    uvec3 emitted = floatBitsToUint(max(p.emitted, vec3(0.0)));
    emitted.x |= !lamp && p.sides < 1.5 ? 0x80000000u : 0u;
    light_pool[2u * slot + 1u] = uvec4(emitted, packSnorm2x16(light_pool_oct(p.n)));
}
#else
// A pick from its two words (the pool's, or - round DO - the light grid's: the same layout)
LightPick light_pick_words(uvec4 w0, uvec4 w1)
{
    float pdf = uintBitsToFloat(w0.w);
    if (pdf < 0.0) {
        return LightPick(vec3(0.0), vec3(0.0, 1.0, 0.0), vec3(0.0), -pdf, w0.x, 2.0);
    }
    return LightPick(uintBitsToFloat(w0.xyz), light_pool_unoct(unpackSnorm2x16(w1.w)),
                     uintBitsToFloat(w1.xyz & uvec3(0x7FFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu)), pdf, LIGHT_PICK_TRIANGLE,
                     (w1.x & 0x80000000u) != 0u ? 1.0 : 2.0);
}
LightPick light_pool_read(uint slot) { return light_pick_words(light_pool[2u * slot], light_pool[2u * slot + 1u]); }
#endif
#endif
)";
constexpr std::string_view kLocalPickGlsl = R"(
LightPick light_pick_at(uint e, uint count, vec2 uv)
{
    LightPick p = no_light_pick();
    if (e >= count) {
        return p;
    }
    LightEntry entry = light_entries[e];
    uint kind = light_kind(entry.payload);
    uint index = light_index(entry.payload);
    if (kind == LIGHT_TRIANGLE) {
        uvec4 listed = emitter_entries[index];
        HitVertices tri;
        if (!hit_vertices(listed.x, listed.y, tri)) {
            return p;
        }
        mat4x3 to_world = instance_transform(listed.x);
        vec3 a = to_world * vec4(tri.position[0], 1.0);
        vec3 b = to_world * vec4(tri.position[1], 1.0);
        vec3 c = to_world * vec4(tri.position[2], 1.0);
        vec3 across = cross(b - a, c - a);
        float twice = length(across);
        if (!(twice > 0.0)) {
            return p;
        }
        vec3 bc = path_triangle_point(uv.x, uv.y);
        vec3 emitted;
        if (!hit_emission_of(listed.x, listed.y, instance_material(listed.x), bc.yz, tri, emitted)) { // (round PERF2)
            return p;
        }
        p.y = a * bc.x + b * bc.y + c * bc.z;
        p.n = across / twice * EMITTER_FRONT_SIGN;
        p.sides = emitter_sides(instance_material(listed.x));
        p.emitted = emitted;
        p.pdf = entry.pdf / (0.5 * twice);
        return p;
    }
    if (kind == LIGHT_LAMP) {
        p.lamp = index;
        p.pdf = entry.pdf;
    }
    return p;
}
bool local_light_at(uint e, uint count, vec2 uv, vec3 at, out LocalLight l)
{
    return light_seen_from(light_pick_at(e, count, uv), at, l);
}
)";
} // namespace

std::string_view local_light_seen_glsl() { return kLocalSeenGlsl; }

std::string_view local_light_glsl()
{
    static const std::string made = std::string(kLocalSeenGlsl) + std::string(kLocalPickGlsl);
    return made;
}

} // namespace render::vulkan
