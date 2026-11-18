// NOT ORIGINAL - the light list's device side, shared (vulkan_light_list.h).
#include "gpu_vulkan/vulkan_light_list.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_light_list.h"
#include "engine/gfx/rt_path.h"
#include "engine/gfx/rt_scene.h"

namespace render::vulkan {

namespace {

constexpr float kSwitchSteps = 32.0F; // (render_pc/rt_relit_world.cpp switch_lamps: the lamps' surfaces switch in 1/32s)

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
// membership[0 .. instances]). A triangle of a world instance that casts and isn't seen through, whose light store emits
// at a corner (not hidden, not animated), is measured - its power, 2 pi x its area x its emission's mean luminance at the
// centroids of its TRIANGLE_LATTICE^2 equal sub-triangles (hit_surface's `emitted`: the store's emit x the lamp texels) -
// and listed when it sends any. (The list's header word 1 counts the candidates: those emitting at a corner.)
constexpr const char* kScanGlsl = R"(
layout(local_size_x = 64) in;
const uint LIST_CASTS = 4u;        // (rt::kCastsShadow)
const uint LIST_TRANSPARENT = 64u; // (rt::kTransparent)
float emitter_power(uint instance, uint primitive)
{
    uint id = instance_material(instance);
    mat4x3 to_world = instance_transform(instance);
    float sum = 0.0;
    float area = 0.0;
    const float n = float(TRIANGLE_LATTICE);
    for (uint i = 0u; i < TRIANGLE_LATTICE; ++i) {
        for (uint j = 0u; i + j < TRIANGLE_LATTICE; ++j) {
            // the upright sub-triangle (i, j) and, but on the last row, the one turned over beside it
            for (uint turned = 0u; turned < (i + j + 1u < TRIANGLE_LATTICE ? 2u : 1u); ++turned) {
                float c = turned == 0u ? 1.0 / 3.0 : 2.0 / 3.0;
                HitSurface s;
                if (hit_surface(instance, primitive, id, vec2((float(i) + c) / n, (float(j) + c) / n), to_world,
                                vec3(0.0, -1.0, 0.0), s)) {
                    area = s.area;
                    if (!s.cut_out && !s.hidden) {
                        sum += path_luma(max(s.emitted, vec3(0.0)));
                    }
                }
            }
        }
    }
    return 2.0 * PATH_PI * area * sum / (n * n);
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
    if ((record.flags & GEOMETRY_LIVE) == 0u || (record.flags & GEOMETRY_LIGHT) == 0u ||
        (record.instance_flags & HIT_INSTANCE_OBJECT) != 0u || (record.instance_flags & LIST_CASTS) == 0u ||
        (record.instance_flags & LIST_TRANSPARENT) != 0u) {
        return;
    }
    float most = 0.0;
    for (uint k = 0u; k < 3u; ++k) {
        uint at = geometry_corner(record, primitive, k) * 32u;
        vec4 flags = unpackUnorm4x8(geometry_word(record.light_address, at + 24u)).zyxw;
        if (flags.a < 0.75) {
            return; // (no store at an animated vertex; a hidden mesh)
        }
        vec3 e = stored_light(geometry_word(record.light_address, at + 28u));
        most = max(most, max(e.r, max(e.g, e.b)));
    }
    if (!(most > 0.0)) {
        return;
    }
    atomicAdd(emitter_spare, 1u);
    float power = emitter_power(instance, primitive);
    if (!(power > 0.0) || isinf(power)) {
        return; // (no lamp texel at its points: not listed - a BSDF ray finds it, weighed 1)
    }
    uint slot = atomicAdd(emitter_count, 1u);
    if (slot < LIST_CAPACITY) {
        emitter_entries[slot] = uvec4(instance, primitive, floatBitsToUint(power), 0u);
        slots[g] = slot + 1u;
    }
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
        out.table_factor = (out.clouds || !day.on ? 1.0F : day.cloud_sky) / input.exposure;
    }
    const std::shared_ptr<const rt::LevelEmitters> level = rt::level_emitters();
    const float open = rt::open_sky_lamps(day, level.get());
    if (level) {
        const std::vector<rt::Emitter> analytic =
            every_lamp_analytic ? level->emitters : rt::path::analytic_lamps(level->emitters, level->surfaced);
        out.owned = static_cast<uint32_t>(level->emitters.size() - analytic.size());
        const float sky_now = rt::lamp_sky_now(day, level.get());
        uint64_t switched = 1469598103934665603ULL;
        for (std::size_t i = 0; i < level->emitters.size() && i < level->surfaced.size(); ++i) {
            if (core::at(level->surfaced, i) != 0) {
                rt::Emitter lamp = core::at(level->emitters, i);
                const float on = rt::lamp_on(lamp.switch_sky, sky_now, open);
                lamp.intensity *= on;
                out.owned_power += rt::lights::lamp_power(lamp);
                switched = (switched ^ static_cast<uint64_t>(std::lround(on * kSwitchSteps))) * 1099511628211ULL;
            }
        }
        out.surfaces_switched = switched;
        for (rt::Emitter lamp : analytic) {
            lamp.intensity *= rt::lamp_on(lamp.switch_sky, sky_now, open);
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

std::string_view light_instances_glsl() { return kInstancesGlsl; }

std::string_view emitter_scan_glsl() { return kScanGlsl; }

} // namespace render::vulkan
