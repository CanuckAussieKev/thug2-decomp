// NOT ORIGINAL - the sky model on the device (vulkan_rt_sky.h; engine/gfx/rt_sky.h is the model and its reference).
#include "gpu_vulkan/vulkan_rt_sky.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sky.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kReportFailed = 0x534B0001U;
constexpr uint64_t kReportFirst = 0x534B0002U;
constexpr uint64_t kReportAtmosphereFailed = 0x534B0003U;

constexpr uint32_t kGroupSide = 8;
constexpr double kShortest = 1e-6;
constexpr float kSameDirection = 0.99999F; // (cosine) the table kept while the sun moves less than this
constexpr rt::SkySteps kTableSteps{.view = rt::kTableViewSteps, .light = rt::kTableLightSteps};
constexpr double kDegreesPerRadian = 57.29577951308232;
constexpr rt::SkySteps kColourSteps{.view = 128, .light = 64}; // the processor's zenith colour (sky_colours)

// The table's pass: rt_sky.h's integrals, few steps (the processor's are the reference).
constexpr const char* kTableHeader = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0, rgba16f) uniform writeonly image2D o_table;
layout(push_constant) uniform Sky {
    vec4 sun;      // xyz the model's sun (cos e, sin e, 0); w the exposure
    ivec4 steps;   // x segments along a view ray, y along a ray toward the sun
    vec4 view_sun; // (added by the sky) xyz the drawn sky's sun (world); w 1: the rows are read from its sky-view table
} sky;
#ifdef FROM_SKY_VIEW
layout(set = 0, binding = 1) uniform sampler2D u_view;
#endif
)";

// rt_sky.h's model as GLSL functions (the table's and the atmosphere's passes).
constexpr const char* kModelFunctions = R"(
const float PI = 3.14159265358979;

float ray_length(vec3 o, vec3 d, out bool ground)
{
    float r = length(o);
    float mu = dot(o, d) / r;
    float base = (r * r) * (mu * mu - 1.0);
    ground = false;
    if (mu < 0.0) {
        float inside = base + GROUND * GROUND;
        if (inside >= 0.0) {
            ground = true;
            return max(0.0, -r * mu - sqrt(inside));
        }
    }
    return max(0.0, -r * mu + sqrt(max(0.0, base + TOP * TOP)));
}

vec2 densities(vec3 p)
{
    float h = max(0.0, length(p) - GROUND);
    return vec2(exp(-h / RAYLEIGH_HEIGHT), exp(-h / MIE_HEIGHT));
}

vec2 segment(float len, int i, int n)
{
    float a = float(i) / float(n);
    float b = float(i + 1) / float(n);
    return vec2(len * a * a, len * b * b);
}

vec3 extinction(vec2 depth) { return RAYLEIGH * depth.x + vec3(MIE_SCATTERING / MIE_ALBEDO) * depth.y; }

bool depth_to_top(vec3 o, vec3 d, int steps, out vec2 depth)
{
    bool ground;
    float len = ray_length(o, d, ground);
    depth = vec2(0.0);
    if (ground) {
        return false;
    }
    for (int i = 0; i < steps; ++i) {
        vec2 s = segment(len, i, steps);
        depth += densities(o + d * (0.5 * (s.x + s.y))) * (s.y - s.x);
    }
    return true;
}

vec3 viewer() { return vec3(0.0, GROUND + VIEWER_ALTITUDE, 0.0); }
)";

constexpr const char* kTableMain = R"(
vec3 transmittance(vec3 d)
{
    vec2 depth;
    if (!depth_to_top(viewer(), d, sky.steps.y, depth)) {
        return vec3(0.0);
    }
    return exp(-extinction(depth));
}

vec3 radiance(vec3 view, vec3 sun)
{
    bool ground;
    vec3 o = viewer();
    float len = ray_length(o, view, ground);
    float mu = dot(view, sun);
    float rayleigh_phase = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    float g = MIE_G;
    float mie_phase = 3.0 / (8.0 * PI) * ((1.0 - g * g) * (1.0 + mu * mu)) /
                      ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * mu, 1.5));
    vec3 sum = vec3(0.0);
    vec2 travelled = vec2(0.0);
    int n = sky.steps.x;
    for (int i = 0; i < n; ++i) {
        vec2 s = segment(len, i, n);
        float ds = s.y - s.x;
        vec3 p = o + view * (0.5 * (s.x + s.y));
        vec2 here = densities(p);
        vec2 to_sun;
        if (depth_to_top(p, sun, sky.steps.y, to_sun)) {
            vec3 tau = extinction(travelled + 0.5 * here * ds + to_sun);
            sum += exp(-tau) * (RAYLEIGH * here.x * rayleigh_phase + vec3(MIE_SCATTERING * here.y * mie_phase)) * ds;
        }
        travelled += here * ds;
    }
    return sum;
}

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (q.x >= WIDTH || q.y > HEIGHT) {
        return;
    }
    vec3 sun = sky.sun.xyz;
    vec4 value = vec4(0.0, 0.0, 0.0, 1.0);
    if (q.y < HEIGHT) {
        float u = (float(q.x) + 0.5) / float(WIDTH);
        float v = (float(q.y) + 0.5) / float(HEIGHT);
        float azimuth = u * PI;
        float elevation = LOWEST + v * v * (0.5 * PI - LOWEST);
        vec3 view = vec3(cos(elevation) * cos(azimuth), sin(elevation), cos(elevation) * sin(azimuth));
#ifdef FROM_SKY_VIEW
        // (added by the sky) the drawn sky's colour there: its sky-view table (rt_sky.h's sky_view_uv)
        float e = asin(clamp(view.y, -1.0, 1.0));
        vec2 at = vec2(sqrt(azimuth / PI), 0.5 + 0.5 * sign(e) * sqrt(abs(e) / (0.5 * PI)));
        value.rgb = max(textureLod(u_view, at, 0.0).rgb, vec3(0.0));
#else
        value.rgb = radiance(view, sun) * sky.sun.w;
#endif
    } else if (q.x == 3) {
        value = sky.view_sun; // (added by the sky: w 0 - the rows are the model's for the lighting sun)
    } else if (q.x == 0) {
        value.rgb = transmittance(sun);
    } else if (q.x == 1) {
        vec3 here = transmittance(sun);
        vec3 overhead = transmittance(vec3(0.0, 1.0, 0.0));
        vec3 shown = pow(max(here / max(overhead, vec3(1e-30)), vec3(0.0)), vec3(1.0 / DISPLAY_GAMMA));
        float most = max(shown.r, max(shown.g, shown.b));
        value.rgb = most > 0.0 ? shown / most : vec3(0.0);
    } else if (q.x == 2) {
        value.rgb = radiance(vec3(0.0, 1.0, 0.0), sun) * sky.sun.w;
    }
    imageStore(o_table, q, value);
}
)";

// --- (added by the sky) the atmosphere's three tables (rt_sky.h's multiple scattering) -------------------------------

// What they share: their push constants, the bindings (0 the table written, 1 the transmittance, 2 Psi), the reads.
constexpr const char* kAtmosphereHeader = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0, rgba16f) uniform writeonly image2D o_out;
layout(set = 0, binding = 1) uniform sampler2D u_transmittance;
layout(set = 0, binding = 2) uniform sampler2D u_multiple;
layout(push_constant) uniform Atmosphere {
    vec4 sun;        // xyz the model's sun (cos e, sin e, 0); w the exposure
    vec4 gains[3];   // the horizon's, half way up's, overhead's gains (rgb); w its elevation (radians)
    vec4 night;      // (added by the time of day) rgb the night sky's light overhead, exposed (0 by day)
} atmosphere;
)";

constexpr const char* kAtmosphereReads = R"(
const float SPAN = sqrt(TOP * TOP - GROUND * GROUND);

// The transmittance table at (r, mu) - Bruneton's mapping (rt_sky.h transmittance_lut_uv).
vec3 transmittance_lut(float r, float mu)
{
    float rho = sqrt(max(r * r - GROUND * GROUND, 0.0));
    float d = max(0.0, -r * mu + sqrt(max(r * r * (mu * mu - 1.0) + TOP * TOP, 0.0)));
    float d_min = TOP - r;
    float d_max = rho + SPAN;
    float x_mu = d_max > d_min ? (d - d_min) / (d_max - d_min) : 0.0;
    vec2 uv = vec2(0.5 / float(TRANS_W) + clamp(x_mu, 0.0, 1.0) * (1.0 - 1.0 / float(TRANS_W)),
                   0.5 / float(TRANS_H) + clamp(rho / SPAN, 0.0, 1.0) * (1.0 - 1.0 / float(TRANS_H)));
    return textureLod(u_transmittance, uv, 0.0).rgb;
}

// The sun's transmittance from p (from the planet's centre) toward `sun` (0 when the ground is in the way).
vec3 sun_seen_from(vec3 p, vec3 sun)
{
    float r = length(p);
    float mu = dot(p, sun) / r;
    if (mu < 0.0 && r * r * (mu * mu - 1.0) + GROUND * GROUND >= 0.0) {
        return vec3(0.0);
    }
    return transmittance_lut(r, mu);
}

// Psi at p for the sun along `sun` (rt_sky.h multiple_from_table)
vec3 multiple_at(vec3 p, vec3 sun)
{
    float r = length(p);
    vec2 uv = vec2(dot(p, sun) / r * 0.5 + 0.5, clamp((r - GROUND) / (TOP - GROUND), 0.0, 1.0));
    return textureLod(u_multiple, uv, 0.0).rgb;
}
)";

// 1. The transmittance table (rt_sky.h transmittance_lut_point / transmittance_to_top).
constexpr const char* kTransmittanceMain = R"(
void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (q.x >= TRANS_W || q.y >= TRANS_H) {
        return;
    }
    float u = (float(q.x) + 0.5) / float(TRANS_W);
    float v = (float(q.y) + 0.5) / float(TRANS_H);
    float x_mu = clamp((u - 0.5 / float(TRANS_W)) / (1.0 - 1.0 / float(TRANS_W)), 0.0, 1.0);
    float x_r = clamp((v - 0.5 / float(TRANS_H)) / (1.0 - 1.0 / float(TRANS_H)), 0.0, 1.0);
    float rho = SPAN * x_r;
    float r = sqrt(rho * rho + GROUND * GROUND);
    float d_min = TOP - r;
    float d_max = rho + SPAN;
    float d = d_min + x_mu * (d_max - d_min);
    float mu = d <= 0.0 ? 1.0 : clamp((SPAN * SPAN - rho * rho - d * d) / (2.0 * r * d), -1.0, 1.0);
    vec3 o = vec3(0.0, r, 0.0);
    vec3 dir = vec3(sqrt(max(0.0, 1.0 - mu * mu)), mu, 0.0);
    float ds = d / float(TRANS_STEPS);
    vec2 depth = vec2(0.0);
    for (int i = 0; i < TRANS_STEPS; ++i) {
        depth += densities(o + dir * ((float(i) + 0.5) * ds)) * ds;
    }
    imageStore(o_out, q, vec4(exp(-extinction(depth)), 1.0));
}
)";

// 2. Psi (rt_sky.h multiple_scattering; the sun's transmittance from the table).
constexpr const char* kMultipleMain = R"(
void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (q.x >= MS_W || q.y >= MS_H) {
        return;
    }
    float cos_sun = ((float(q.x) + 0.5) / float(MS_W)) * 2.0 - 1.0;
    float altitude = (float(q.y) + 0.5) / float(MS_H) * (TOP - GROUND);
    vec3 o = vec3(0.0, GROUND + altitude, 0.0);
    vec3 sun = vec3(sqrt(max(0.0, 1.0 - cos_sun * cos_sun)), cos_sun, 0.0);
    vec3 second = vec3(0.0);
    vec3 transfer = vec3(0.0);
    for (int i = 0; i < MS_SIDE; ++i) {
        float cos_theta = 1.0 - 2.0 * (float(i) + 0.5) / float(MS_SIDE);
        float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
        for (int j = 0; j < MS_SIDE; ++j) {
            float phi = 2.0 * PI * (float(j) + 0.5) / float(MS_SIDE);
            vec3 w = vec3(sin_theta * cos(phi), cos_theta, sin_theta * sin(phi));
            bool ground;
            float len = ray_length(o, w, ground);
            float ds = len / float(MS_STEPS);
            vec3 throughput = vec3(1.0);
            for (int k = 0; k < MS_STEPS; ++k) {
                vec3 p = o + w * ((float(k) + 0.5) * ds);
                vec2 here = densities(p);
                vec3 sigma_s = RAYLEIGH * here.x + vec3(MIE_SCATTERING * here.y);
                vec3 sigma_t = extinction(here);
                vec3 kept = exp(-sigma_t * ds);
                vec3 integral = (1.0 - kept) / max(sigma_t, vec3(1e-30));
                second += throughput * sigma_s * sun_seen_from(p, sun) * (1.0 / (4.0 * PI)) * integral;
                transfer += throughput * sigma_s * integral;
                throughput *= kept;
            }
            if (ground) {
                vec3 g = o + w * len;
                float up = dot(g, sun) / max(length(g), 1.0);
                second += throughput * sun_seen_from(g, sun) * max(up, 0.0) * GROUND_ALBEDO / PI;
            }
        }
    }
    float directions = float(MS_SIDE * MS_SIDE);
    vec3 psi = (second / directions) / max(vec3(1.0) - transfer / directions, vec3(1e-6));
    imageStore(o_out, q, vec4(psi, 1.0));
}
)";

// 3. The sky seen from the viewer (rt_sky.h sky_radiance_ms x the exposure), matched to the level (the gains). Below the
// horizon the horizon's haze (the sky just above it, in the same azimuth) giving way to the model's own far ground by
// BELOW_SHARE over BELOW_FADE radians down - the level's dome is never asked what is down there (a painted ring of
// ground would come back as a band): the far world's aerial haze, which reads this table, meets the sky without one.
constexpr const char* kSkyViewMain = R"(
vec3 radiance_ms(vec3 view, vec3 sun)
{
    bool ground;
    vec3 o = viewer();
    float len = ray_length(o, view, ground);
    float mu = dot(view, sun);
    float rayleigh_phase = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    float g = MIE_G;
    float mie_phase = 3.0 / (8.0 * PI) * ((1.0 - g * g) * (1.0 + mu * mu)) /
                      ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * mu, 1.5));
    vec3 sum = vec3(0.0);
    vec2 travelled = vec2(0.0);
    for (int i = 0; i < VIEW_STEPS; ++i) {
        vec2 s = segment(len, i, VIEW_STEPS);
        float ds = s.y - s.x;
        vec3 p = o + view * (0.5 * (s.x + s.y));
        vec2 here = densities(p);
        vec3 seen = exp(-extinction(travelled + 0.5 * here * ds));
        vec3 rayleigh_s = RAYLEIGH * here.x;
        vec3 mie_s = vec3(MIE_SCATTERING * here.y);
        vec3 single = sun_seen_from(p, sun) * (rayleigh_s * rayleigh_phase + mie_s * mie_phase);
        vec3 multiple = (rayleigh_s + mie_s) * multiple_at(p, sun);
        sum += seen * (single + multiple) * ds;
        travelled += here * ds;
    }
    if (ground) {
        vec3 gp = o + view * len;
        float up = dot(gp, sun) / max(length(gp), 1.0);
        sum += exp(-extinction(travelled)) * sun_seen_from(gp, sun) * max(up, 0.0) * GROUND_ALBEDO / PI;
    }
    return sum;
}

vec3 gain_at(float e)
{
    vec4 h = atmosphere.gains[0];
    vec4 m = atmosphere.gains[1];
    vec4 z = atmosphere.gains[2];
    if (e <= h.w) {
        return h.rgb;
    }
    if (e <= m.w) {
        return mix(h.rgb, m.rgb, (e - h.w) / max(m.w - h.w, 1e-4));
    }
    if (e <= z.w) {
        return mix(m.rgb, z.rgb, (e - m.w) / max(z.w - m.w, 1e-4));
    }
    return z.rgb;
}

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (q.x >= VIEW_W || q.y >= VIEW_H) {
        return;
    }
    float u = (float(q.x) + 0.5) / float(VIEW_W);
    float v = (float(q.y) + 0.5) / float(VIEW_H);
    float azimuth = PI * u * u;
    float s = 2.0 * v - 1.0;
    float e = sign(s) * s * s * (0.5 * PI);
    vec3 view = vec3(cos(e) * cos(azimuth), sin(e), cos(e) * sin(azimuth));
    vec3 value = radiance_ms(view, atmosphere.sun.xyz) * atmosphere.sun.w * gain_at(e);
    if (e < 0.0) {
        vec3 level = vec3(cos(HAZE_ELEVATION) * cos(azimuth), sin(HAZE_ELEVATION), cos(HAZE_ELEVATION) * sin(azimuth));
        vec3 haze = radiance_ms(level, atmosphere.sun.xyz) * atmosphere.sun.w * gain_at(HAZE_ELEVATION);
        value = mix(haze, value, BELOW_SHARE * smoothstep(0.0, 1.0, -e / BELOW_FADE));
    }
    // (added by the time of day) the night sky's own light - the moon's scattered and the airglow: overhead, half as
    // much again toward the horizon (more air), the horizon's below it
    if (atmosphere.night.r + atmosphere.night.g + atmosphere.night.b > 0.0) {
        value += atmosphere.night.rgb * mix(NIGHT_HORIZON, 1.0, sqrt(clamp(e / (0.5 * PI), 0.0, 1.0)));
    }
    imageStore(o_out, q, vec4(max(value, vec3(0.0)), 1.0));
}
)";

struct TableConstants {
    std::array<float, 4> sun{};
    std::array<int32_t, 4> steps{};
    std::array<float, 4> view_sun{}; // (added by the sky)
};
constexpr std::size_t kTableConstantBytes = 48;
static_assert(sizeof(TableConstants) == kTableConstantBytes, "the shader's push-constant block");

struct AtmosphereConstants {
    std::array<float, 4> sun{};
    std::array<std::array<float, 4>, 3> gains{};
    std::array<float, 4> night{}; // (added by the time of day) rgb the night sky overhead (exposed linear)
};
constexpr std::size_t kAtmosphereConstantBytes = 80;
static_assert(sizeof(AtmosphereConstants) == kAtmosphereConstantBytes, "the shaders' push-constant block");

constexpr uint32_t kTransmittanceSteps = 40;

// The atmosphere's objects and tables.
struct AtmosphereState {
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline transmittance = VK_NULL_HANDLE;
    VkPipeline multiple = VK_NULL_HANDLE;
    VkPipeline sky_view = VK_NULL_HANDLE;
    VkPipeline table_from_view = VK_NULL_HANDLE; // the fog's table filled from the sky-view table
    Image transmittance_table;
    Image multiple_table;
    Image sky_view_table;
    bool fixed_made = false; // the transmittance and Psi (they don't depend on the sun)
    bool view_valid = false;
    AtmosphereInput input;   // what the sky-view table was made for
    bool failed = false;
    uint64_t generation = 0; // the sky-view table's (a new one each time it is made)
    uint64_t updates = 0;
    // the fog's table follows the sky-view table (rt_sky): while on, from which generation and with which sun
    bool table_follows = false;
    std::array<float, 3> table_view_sun{};
    uint64_t table_generation = 0;
};

AtmosphereState& atmosphere_state()
{
    static AtmosphereState state;
    return state;
}

struct SkyState {
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    Image table;
    bool valid = false;
    std::array<float, 3> toward{};
    bool failed = false;
    uint64_t updates = 0;
    RtSunSettings settings;
    // sky_colours' last answer (worked out again when the sun moves or the settings change)
    bool colours_valid = false;
    std::array<float, 3> colours_toward{};
    SkyColours colours;
    // sun_colour_for's (the visible sun's colour)
    bool visible_valid = false;
    std::array<float, 3> visible_toward{};
    std::array<float, 3> visible_colour{1.0F, 1.0F, 1.0F};
    // (added by the sky) what the table was last made from: the sky-view table (and which one) or the model
    bool made_follow = false;
    uint64_t made_generation = 0;
};

SkyState& sky_state()
{
    static SkyState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

// A GLSL float literal (never an int: 6360000 squared would overflow).
std::string number(double value)
{
    std::string out = text::format("%.9g", value);
    if (out.find_first_of(".e") == std::string::npos) {
        out += ".0";
    }
    return out;
}

std::string defines()
{
    std::string out;
    const auto add = [&out](const char* name, const std::string& value) {
        out += "#define ";
        out += name;
        out += " ";
        out += value;
        out += "\n";
    };
    add("GROUND", number(rt::kGroundRadius));
    add("TOP", number(rt::kTopRadius));
    add("VIEWER_ALTITUDE", number(rt::kViewerAltitude));
    add("RAYLEIGH", "vec3(" + number(core::at(rt::kRayleigh, 0)) + ", " + number(core::at(rt::kRayleigh, 1)) + ", " +
                        number(core::at(rt::kRayleigh, 2)) + ")");
    add("RAYLEIGH_HEIGHT", number(rt::kRayleighHeight));
    add("MIE_SCATTERING", number(rt::kMieScattering));
    add("MIE_ALBEDO", number(rt::kMieAlbedo));
    add("MIE_HEIGHT", number(rt::kMieHeight));
    add("MIE_G", number(rt::kMieG));
    add("DISPLAY_GAMMA", number(rt::kDisplayGamma));
    add("LOWEST", number(rt::kSkyLutLowest));
    add("WIDTH", std::to_string(rt::kSkyLutWidth));
    add("HEIGHT", std::to_string(rt::kSkyLutHeight));
    // (added by the sky: the atmosphere's tables)
    add("GROUND_ALBEDO", number(rt::kGroundAlbedo));
    add("TRANS_W", std::to_string(rt::kTransmittanceWidth));
    add("TRANS_H", std::to_string(rt::kTransmittanceHeight));
    add("TRANS_STEPS", std::to_string(kTransmittanceSteps));
    add("MS_W", std::to_string(rt::kMultipleWidth));
    add("MS_H", std::to_string(rt::kMultipleHeight));
    add("MS_SIDE", std::to_string(rt::kMultipleDirectionsSide));
    add("MS_STEPS", std::to_string(rt::kMultipleSteps));
    add("VIEW_W", std::to_string(rt::kSkyViewWidth));
    add("VIEW_H", std::to_string(rt::kSkyViewHeight));
    add("VIEW_STEPS", std::to_string(rt::kSkyViewSteps));
    add("HAZE_ELEVATION", number(kHazeElevation));
    add("BELOW_FADE", number(kBelowFade));
    add("BELOW_SHARE", number(kBelowShare));
    add("NIGHT_HORIZON", number(kNightHorizon)); // (added by the time of day)
    return out;
}

VkPipeline make_compute(VkDevice device, VkPipelineLayout layout, const std::string& source, std::string* error)
{
    const CompiledShader compiled = compile_rt_glsl(RtShaderStage::kCompute, source);
    if (!compiled.ok) {
        *error = compiled.log;
        return VK_NULL_HANDLE;
    }
    const VkShaderModuleCreateInfo module_info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                               .pNext = nullptr,
                                               .flags = 0,
                                               .codeSize = compiled.spirv.size() * sizeof(uint32_t),
                                               .pCode = compiled.spirv.data()};
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &module_info, nullptr, &module) != VK_SUCCESS) {
        *error = "vkCreateShaderModule failed";
        return VK_NULL_HANDLE;
    }
    const VkComputePipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                           .pNext = nullptr,
                                           .flags = 0,
                                           .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                     .pNext = nullptr,
                                                     .flags = 0,
                                                     .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                                     .module = module,
                                                     .pName = "main",
                                                     .pSpecializationInfo = nullptr},
                                           .layout = layout,
                                           .basePipelineHandle = VK_NULL_HANDLE,
                                           .basePipelineIndex = -1};
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult made = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, module, nullptr);
    if (made != VK_SUCCESS) {
        *error = "vkCreateComputePipelines failed";
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

// A push-descriptor set layout of a storage image at 0 and `samplers` combined image samplers after it, and a
// pipeline layout of it with `constants` bytes of push constants.
struct LayoutShape {
    uint32_t samplers = 0;  // combined image samplers after the storage image
    uint32_t constants = 0; // push constants' bytes
};
bool make_layout(VkDevice device, const LayoutShape& shape, VkDescriptorSetLayout* set_layout, VkPipelineLayout* layout)
{
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (uint32_t binding = 0; binding <= shape.samplers; ++binding) {
        bindings.push_back({.binding = binding,
                            .descriptorType = binding == 0 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                           : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .descriptorCount = 1,
                            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                            .pImmutableSamplers = nullptr});
    }
    const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                   .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                   .pBindings = bindings.data()};
    if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, set_layout) != VK_SUCCESS) {
        *set_layout = VK_NULL_HANDLE;
        return false;
    }
    const VkPushConstantRange range{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = shape.constants};
    const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .setLayoutCount = 1,
                                                 .pSetLayouts = set_layout,
                                                 .pushConstantRangeCount = 1,
                                                 .pPushConstantRanges = &range};
    if (vkCreatePipelineLayout(device, &layout_info, nullptr, layout) != VK_SUCCESS) {
        *layout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool make_objects(Backend& state, std::string* error)
{
    SkyState& own = sky_state();
    if (own.pipeline != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = 0.0F;
    // (binding 1: the sky-view table, read by the table's FROM_SKY_VIEW variant - added by the sky)
    if (vkCreateSampler(device, &sampler_info, nullptr, &own.sampler) != VK_SUCCESS ||
        !make_layout(device, {.samplers = 1, .constants = sizeof(TableConstants)}, &own.set_layout, &own.layout)) {
        *error = "the descriptor layout, the pipeline layout or the sampler";
        return false;
    }
    own.pipeline = make_compute(device, own.layout, "#version 460\n" + defines() + kTableHeader + kModelFunctions + kTableMain,
                                error);
    return own.pipeline != VK_NULL_HANDLE;
}

bool make_table(Backend& state)
{
    SkyState& own = sky_state();
    if (own.table.image != VK_NULL_HANDLE) {
        return true;
    }
    const ImageRequest request{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = {.width = rt::kSkyLutWidth, .height = rt::kSkyLutHeight + 1},
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, &own.table)) {
        return false;
    }
    own.table.resting = VK_IMAGE_LAYOUT_GENERAL;
    return true;
}

double elevation_of(const std::array<float, 3>& toward)
{
    return std::asin(std::clamp(static_cast<double>(core::at(toward, 1)), -1.0, 1.0));
}

std::array<float, 3> display_of(const rt::Rgb& linear)
{
    std::array<float, 3> out{};
    for (std::size_t channel = 0; channel < out.size(); ++channel) {
        core::at(out, channel) =
            static_cast<float>(std::pow(std::max(core::at(linear, channel), 0.0), 1.0 / rt::kDisplayGamma));
    }
    return out;
}

} // namespace

RtSunSettings& sun_settings_held() { return sky_state().settings; }

bool frame_sun(std::array<float, 3>* toward)
{
    if (!rt::baked_sun_wanted()) {
        rt::set_baked_sun_wanted(true);
    }
    const rt::SceneSun sun = rt::scene_sun();
    if (!sun.found()) {
        return false;
    }
    *toward = sun.toward;
    return true;
}

bool visible_sun(std::array<float, 3>* toward)
{
    // (added by the time of day) the clock's sun, wherever it is - under the horizon at night (the sky then dark, the
    // disc not drawn): the scene's light is the moon then, not the sun the sky is drawn for
    const rt::DayLight day = rt::day_light();
    if (day.on) {
        *toward = day.sun;
        return true;
    }
    if (frame_sun(toward)) {
        return true;
    }
    // (no scene sun - indoors, or the fit not made yet: the sun drawn where the game shows it, lighting nothing)
    std::array<float, 3> found{};
    if (!rt::game_sun(&found)) {
        return false;
    }
    const double length = std::hypot(static_cast<double>(core::at(found, 0)), static_cast<double>(core::at(found, 1)),
                                     static_cast<double>(core::at(found, 2)));
    if (!(length > kShortest) || !std::isfinite(length)) {
        return false;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        core::at(*toward, i) = static_cast<float>(static_cast<double>(core::at(found, i)) / length);
    }
    return true;
}

std::array<float, 3> sun_colour_for(const std::array<float, 3>& toward)
{
    SkyState& own = sky_state();
    if (own.settings.colour_set) {
        return own.settings.colour;
    }
    if (!own.visible_valid || own.visible_toward != toward) {
        own.visible_colour = rt::sun_colour_at(elevation_of(toward));
        own.visible_toward = toward;
        own.visible_valid = true;
    }
    return own.visible_colour;
}

SkyColours sky_colours(const std::array<float, 3>& toward)
{
    SkyState& own = sky_state();
    if (own.colours_valid && own.colours_toward == toward) {
        return own.colours;
    }
    const RtSunSettings& settings = own.settings;
    SkyColours colours;
    const double elevation = elevation_of(toward);
    colours.sun = settings.colour_set ? settings.colour : rt::sun_colour_at(elevation);
    const rt::Rgb zenith = rt::sky_radiance({0.0, 1.0, 0.0}, rt::sun_direction_at(elevation), kColourSteps);
    colours.zenith = display_of({core::at(zenith, 0) * rt::sky_exposure(), core::at(zenith, 1) * rt::sky_exposure(),
                                 core::at(zenith, 2) * rt::sky_exposure()});
    own.colours = colours;
    own.colours_toward = toward;
    own.colours_valid = true;
    return colours;
}

bool update_sky(Backend& state, VkCommandBuffer commands, const std::array<float, 3>& lighting_sun)
{
    // (added by the time of day) the table's sky is the clock's sun's (the scene may be lit by the moon)
    const rt::DayLight day = rt::day_light();
    const std::array<float, 3> toward = day.on ? day.sun : lighting_sun;
    SkyState& own = sky_state();
    AtmosphereState& atmosphere = atmosphere_state();
    if (own.failed) {
        return false;
    }
    // (added by the sky) while the drawn sky (rt_sky) has its sky-view table, the rows are read from it - the fog's
    // haze fades toward the sky drawn - made again with each new one
    const bool follow = atmosphere.table_follows && atmosphere.view_valid;
    const float same = (core::at(toward, 0) * core::at(own.toward, 0)) + (core::at(toward, 1) * core::at(own.toward, 1)) +
                       (core::at(toward, 2) * core::at(own.toward, 2));
    if (own.valid && same >= kSameDirection && own.made_follow == follow &&
        (!follow || own.made_generation == atmosphere.generation)) {
        return true;
    }
    std::string error;
    if (!make_objects(state, &error) || !make_table(state)) {
        own.failed = true;
        if (first_report(state, kReportFailed)) {
            get_log("vulkan: rt_sky: the sky's table couldn't be made (%s) - no sky colours",
                    error.empty() ? "its image" : error.c_str());
        }
        return false;
    }
    VkPipeline pipeline = own.pipeline;
    if (follow) {
        if (atmosphere.table_from_view == VK_NULL_HANDLE) {
            atmosphere.table_from_view =
                make_compute(state.context.device(), own.layout,
                             "#version 460\n#define FROM_SKY_VIEW\n" + defines() + kTableHeader + kModelFunctions + kTableMain,
                             &error);
        }
        if (atmosphere.table_from_view == VK_NULL_HANDLE) {
            own.failed = true;
            if (first_report(state, kReportFailed)) {
                get_log("vulkan: rt_sky: the sky's table couldn't be made (%s) - no sky colours", error.c_str());
            }
            return false;
        }
        pipeline = atmosphere.table_from_view;
    }
    const double elevation = elevation_of(toward);
    const rt::Dir model = rt::sun_direction_at(elevation);
    TableConstants constants{};
    constants.sun = {static_cast<float>(core::at(model, 0)), static_cast<float>(core::at(model, 1)),
                     static_cast<float>(core::at(model, 2)), static_cast<float>(rt::sky_exposure())};
    constants.steps = {static_cast<int32_t>(kTableSteps.view), static_cast<int32_t>(kTableSteps.light), 0, 0};
    if (follow) {
        const std::array<float, 3>& sun = atmosphere.input.sun;
        constants.view_sun = {core::at(sun, 0), core::at(sun, 1), core::at(sun, 2), 1.0F};
    }
    Memory::transition(commands, &own.table, VK_IMAGE_LAYOUT_GENERAL, true);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    const VkDescriptorImageInfo out{.sampler = VK_NULL_HANDLE, .imageView = own.table.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo view{.sampler = own.sampler,
                                     .imageView = atmosphere.sky_view_table.view,
                                     .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    const std::array<VkWriteDescriptorSet, 2> writes{{
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .pNext = nullptr,
         .dstSet = VK_NULL_HANDLE,
         .dstBinding = 0,
         .dstArrayElement = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .pImageInfo = &out,
         .pBufferInfo = nullptr,
         .pTexelBufferView = nullptr},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .pNext = nullptr,
         .dstSet = VK_NULL_HANDLE,
         .dstBinding = 1,
         .dstArrayElement = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .pImageInfo = &view,
         .pBufferInfo = nullptr,
         .pTexelBufferView = nullptr},
    }};
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0, follow ? 2U : 1U, writes.data());
    vkCmdPushConstants(commands, own.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commands, (rt::kSkyLutWidth + kGroupSide - 1) / kGroupSide,
                  (rt::kSkyLutHeight + 1 + kGroupSide - 1) / kGroupSide, 1);
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &written, 0,
                         nullptr, 0, nullptr);
    state.bound = BoundState{};
    own.table.used = state.frames.serial();
    if (follow) {
        atmosphere.sky_view_table.used = state.frames.serial();
    }
    own.toward = toward;
    own.valid = true;
    own.made_follow = follow;
    own.made_generation = atmosphere.generation;
    ++own.updates;
    // what the other effects are told (the bounce light's sun colour)
    const SkyColours colours = sky_colours(toward);
    const bool published = own.settings.sky_model || own.settings.colour_set;
    rt::set_sun_light({.on = published, .toward = toward, .colour = colours.sun, .sky = colours.zenith});
    if (first_report(state, kReportFirst)) {
        get_log("vulkan: rt_sky: the sky's table made (sun %.1f degrees up; its colour %.3f %.3f %.3f%s)",
                elevation * kDegreesPerRadian, static_cast<double>(core::at(colours.sun, 0)),
                static_cast<double>(core::at(colours.sun, 1)), static_cast<double>(core::at(colours.sun, 2)),
                own.settings.colour_set ? ", set by rt_sun_colour" : "");
    }
    return true;
}

void invalidate_sky()
{
    sky_state().valid = false;
    sky_state().colours_valid = false;
    sky_state().visible_valid = false;
}

void sky_without_sun()
{
    SkyState& own = sky_state();
    if (own.valid) {
        own.valid = false;
        rt::set_sun_light({});
    }
}

VkDescriptorImageInfo sky_table_descriptor()
{
    const SkyState& own = sky_state();
    return {.sampler = own.sampler, .imageView = own.table.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
}

std::string sky_table_glsl(uint32_t binding)
{
    std::string out = "layout(set = 0, binding = " + std::to_string(binding) + ") uniform sampler2D u_sky;\n";
    out += "const float SKY_LOWEST = " + number(rt::kSkyLutLowest) + ";\n";
    out += "const float SKY_ROWS = " + std::to_string(rt::kSkyLutHeight) + ".0;\n";
    out += "const float SKY_GAMMA = " + number(rt::kDisplayGamma) + ";\n";
    out += R"(
vec3 sky_linear(vec3 dir, vec3 to_sun)
{
    // (added by the sky) a table read from the drawn sky's sky-view table is about its own sun (the last row's
    // entry 3, w 1)
    vec4 own_sun = texelFetch(u_sky, ivec2(3, int(SKY_ROWS)), 0);
    vec2 hs = own_sun.w > 0.5 ? own_sun.xz : to_sun.xz;
    vec2 hv = dir.xz;
    float ls = length(hs);
    float lv = length(hv);
    float c = (ls > 1e-5 && lv > 1e-5) ? dot(hs, hv) / (ls * lv) : 1.0;
    float azimuth = acos(clamp(c, -1.0, 1.0));
    float elevation = asin(clamp(dir.y, -1.0, 1.0));
    float v = sqrt(clamp((elevation - SKY_LOWEST) / (1.57079632679 - SKY_LOWEST), 0.0, 1.0));
    float row = clamp(v * SKY_ROWS, 0.5, SKY_ROWS - 0.5);
    return max(textureLod(u_sky, vec2(azimuth / 3.14159265358979, row / (SKY_ROWS + 1.0)), 0.0).rgb, vec3(0.0));
}
vec3 sky_display(vec3 dir, vec3 to_sun) { return pow(sky_linear(dir, to_sun), vec3(1.0 / SKY_GAMMA)); }
)";
    return out;
}

bool read_sky_table(Backend& state, std::vector<std::array<float, 4>>* texels)
{
    SkyState& own = sky_state();
    texels->clear();
    if (!own.valid || !finish_work(state)) {
        return false;
    }
    const std::size_t count = static_cast<std::size_t>(rt::kSkyLutWidth) * (rt::kSkyLutHeight + 1);
    constexpr std::size_t kChannels = 4;
    std::vector<uint8_t> bytes(count * kChannels * sizeof(uint16_t));
    if (!state.memory.read_image_level(&own.table, 0, &bytes)) {
        return false;
    }
    texels->resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            uint16_t half = 0;
            std::memcpy(&half, &core::at(bytes, ((i * kChannels) + channel) * sizeof(uint16_t)), sizeof(half));
            core::at(core::at(*texels, i), channel) = half_to_float(half);
        }
    }
    return true;
}

uint64_t sky_table_updates() { return sky_state().updates; }

void release_sky(Backend& state)
{
    release_atmosphere(state); // (added by the sky)
    SkyState& own = sky_state();
    const VkDevice device = state.context.device();
    if (own.table.image != VK_NULL_HANDLE) {
        state.memory.destroy_image(&own.table);
    }
    own.table = Image{};
    if (device != nullptr) {
        if (own.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, own.pipeline, nullptr);
        }
        if (own.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, own.layout, nullptr);
        }
        if (own.set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, own.set_layout, nullptr);
        }
        if (own.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, own.sampler, nullptr);
        }
    }
    own.pipeline = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.valid = false;
    own.failed = false;
    rt::set_sun_light({});
}

// --- (added by the sky) the atmosphere ----------------------------------------------------------------------------

namespace {

bool make_atmosphere_objects(Backend& state, std::string* error)
{
    AtmosphereState& own = atmosphere_state();
    if (own.sky_view != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    if (own.layout == VK_NULL_HANDLE &&
        !make_layout(device, {.samplers = 2, .constants = sizeof(AtmosphereConstants)}, &own.set_layout, &own.layout)) {
        *error = "the atmosphere's layouts";
        return false;
    }
    const std::string common =
        "#version 460\n" + defines() + kAtmosphereHeader + kModelFunctions + kAtmosphereReads;
    if (own.transmittance == VK_NULL_HANDLE) {
        own.transmittance = make_compute(device, own.layout, common + kTransmittanceMain, error);
    }
    if (own.multiple == VK_NULL_HANDLE && own.transmittance != VK_NULL_HANDLE) {
        own.multiple = make_compute(device, own.layout, common + kMultipleMain, error);
    }
    if (own.multiple != VK_NULL_HANDLE) {
        own.sky_view = make_compute(device, own.layout, common + kSkyViewMain, error);
    }
    return own.sky_view != VK_NULL_HANDLE;
}

bool make_storage(Backend& state, uint32_t width, uint32_t height, Image* out)
{
    if (out->image != VK_NULL_HANDLE) {
        return true;
    }
    const ImageRequest request{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = {.width = width, .height = height},
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, out)) {
        return false;
    }
    out->resting = VK_IMAGE_LAYOUT_GENERAL;
    return true;
}

// One of the atmosphere's passes: `target` written, the transmittance and Psi read.
void atmosphere_pass(VkCommandBuffer commands, VkPipeline pipeline, Image* target, const AtmosphereConstants& constants)
{
    const AtmosphereState& own = atmosphere_state();
    const VkSampler sampler = sky_state().sampler;
    Memory::transition(commands, target, VK_IMAGE_LAYOUT_GENERAL, true);
    const std::array<VkDescriptorImageInfo, 3> infos{{
        {.sampler = VK_NULL_HANDLE, .imageView = target->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = sampler, .imageView = own.transmittance_table.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = sampler, .imageView = own.multiple_table.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
    }};
    std::array<VkWriteDescriptorSet, 3> writes{};
    for (std::size_t i = 0; i < writes.size(); ++i) {
        VkWriteDescriptorSet& write = core::at(writes, i);
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = static_cast<uint32_t>(i);
        write.descriptorCount = 1;
        write.descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &core::at(infos, i);
    }
    // (the table being written is not read: only the others bound beside it)
    uint32_t count = 3U;
    if (pipeline == own.transmittance) {
        count = 1U;
    } else if (pipeline == own.multiple) {
        count = 2U;
    }
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0, count, writes.data());
    vkCmdPushConstants(commands, own.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commands, (target->extent.width + kGroupSide - 1) / kGroupSide,
                  (target->extent.height + kGroupSide - 1) / kGroupSide, 1);
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &written, 0,
                         nullptr, 0, nullptr);
}

bool read_half_image(Backend& state, Image* image, std::vector<std::array<float, 4>>* texels)
{
    texels->clear();
    if (image->image == VK_NULL_HANDLE || !finish_work(state)) {
        return false;
    }
    const std::size_t count = static_cast<std::size_t>(image->extent.width) * image->extent.height;
    constexpr std::size_t kChannels = 4;
    std::vector<uint8_t> bytes(count * kChannels * sizeof(uint16_t));
    if (!state.memory.read_image_level(image, 0, &bytes)) {
        return false;
    }
    texels->resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            uint16_t half = 0;
            std::memcpy(&half, &core::at(bytes, ((i * kChannels) + channel) * sizeof(uint16_t)), sizeof(half));
            core::at(core::at(*texels, i), channel) = half_to_float(half);
        }
    }
    return true;
}

} // namespace

bool update_atmosphere(Backend& state, VkCommandBuffer commands, const AtmosphereInput& input)
{
    AtmosphereState& own = atmosphere_state();
    if (own.failed) {
        return false;
    }
    if (own.view_valid && own.input == input) {
        return true;
    }
    std::string error;
    if (!make_objects(state, &error) || !make_atmosphere_objects(state, &error) ||
        !make_storage(state, rt::kTransmittanceWidth, rt::kTransmittanceHeight, &own.transmittance_table) ||
        !make_storage(state, rt::kMultipleWidth, rt::kMultipleHeight, &own.multiple_table) ||
        !make_storage(state, rt::kSkyViewWidth, rt::kSkyViewHeight, &own.sky_view_table)) {
        own.failed = true;
        if (first_report(state, kReportAtmosphereFailed)) {
            get_log("vulkan: rt_sky: the atmosphere's tables couldn't be made (%s) - the original sky kept",
                    error.empty() ? "their images" : error.c_str());
        }
        return false;
    }
    const double elevation = elevation_of(input.sun);
    const rt::Dir model = rt::sun_direction_at(elevation);
    AtmosphereConstants constants{};
    constants.sun = {static_cast<float>(core::at(model, 0)), static_cast<float>(core::at(model, 1)),
                     static_cast<float>(core::at(model, 2)), static_cast<float>(rt::sky_exposure())};
    for (std::size_t band = 0; band < constants.gains.size(); ++band) {
        const std::array<float, 3>& gain = core::at(input.gains, band);
        core::at(constants.gains, band) = {core::at(gain, 0), core::at(gain, 1), core::at(gain, 2),
                                           core::at(input.gain_elevations, band)};
    }
    constants.night = {core::at(input.night, 0), core::at(input.night, 1), core::at(input.night, 2), 0.0F};
    if (!own.fixed_made) {
        atmosphere_pass(commands, own.transmittance, &own.transmittance_table, constants);
        atmosphere_pass(commands, own.multiple, &own.multiple_table, constants);
        own.fixed_made = true;
    }
    atmosphere_pass(commands, own.sky_view, &own.sky_view_table, constants);
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    for (Image* image : {&own.transmittance_table, &own.multiple_table, &own.sky_view_table}) {
        image->used = serial;
    }
    own.input = input;
    own.view_valid = true;
    ++own.generation;
    ++own.updates;
    return true;
}

void set_table_follows_sky_view(bool follow) { atmosphere_state().table_follows = follow; }

VkDescriptorImageInfo atmosphere_descriptor(AtmosphereTable which)
{
    const AtmosphereState& own = atmosphere_state();
    const Image* image = &own.sky_view_table;
    if (which == AtmosphereTable::kTransmittance) {
        image = &own.transmittance_table;
    } else if (which == AtmosphereTable::kMultiple) {
        image = &own.multiple_table;
    }
    return {.sampler = sky_state().sampler, .imageView = image->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
}

void atmosphere_used(uint64_t serial)
{
    AtmosphereState& own = atmosphere_state();
    for (Image* image : {&own.transmittance_table, &own.multiple_table, &own.sky_view_table}) {
        image->used = serial;
    }
}

std::string atmosphere_glsl(uint32_t transmittance_binding, uint32_t sky_view_binding)
{
    std::string out = "layout(set = 0, binding = " + std::to_string(transmittance_binding) +
                      ") uniform sampler2D u_transmittance;\n";
    out += "layout(set = 0, binding = " + std::to_string(sky_view_binding) + ") uniform sampler2D u_sky_view;\n";
    out += "const float ATMOSPHERE_GROUND = " + number(rt::kGroundRadius) + ";\n";
    out += "const float ATMOSPHERE_TOP = " + number(rt::kTopRadius) + ";\n";
    out += "const float TRANS_W_F = " + number(rt::kTransmittanceWidth) + ";\n";
    out += "const float TRANS_H_F = " + number(rt::kTransmittanceHeight) + ";\n";
    out += R"(
// the transmittance from (r, mu) toward the top (rt_sky.h transmittance_lut_uv); 0 when the ground is in the way
vec3 atmosphere_transmittance(float r, float mu)
{
    if (mu < 0.0 && r * r * (mu * mu - 1.0) + ATMOSPHERE_GROUND * ATMOSPHERE_GROUND >= 0.0) {
        return vec3(0.0);
    }
    float span = sqrt(ATMOSPHERE_TOP * ATMOSPHERE_TOP - ATMOSPHERE_GROUND * ATMOSPHERE_GROUND);
    float rho = sqrt(max(r * r - ATMOSPHERE_GROUND * ATMOSPHERE_GROUND, 0.0));
    float d = max(0.0, -r * mu + sqrt(max(r * r * (mu * mu - 1.0) + ATMOSPHERE_TOP * ATMOSPHERE_TOP, 0.0)));
    float d_min = ATMOSPHERE_TOP - r;
    float d_max = rho + span;
    float x_mu = d_max > d_min ? (d - d_min) / (d_max - d_min) : 0.0;
    vec2 uv = vec2(0.5 / TRANS_W_F + clamp(x_mu, 0.0, 1.0) * (1.0 - 1.0 / TRANS_W_F),
                   0.5 / TRANS_H_F + clamp(rho / span, 0.0, 1.0) * (1.0 - 1.0 / TRANS_H_F));
    return textureLod(u_transmittance, uv, 0.0).rgb;
}
// the drawn sky toward dir (world, y up) with its sun along `sun`: exposed linear (rt_sky.h sky_view_uv)
vec3 sky_view_linear(vec3 dir, vec3 sun)
{
    vec2 hs = sun.xz;
    vec2 hv = dir.xz;
    float ls = length(hs);
    float lv = length(hv);
    float c = (ls > 1e-5 && lv > 1e-5) ? dot(hs, hv) / (ls * lv) : 1.0;
    float azimuth = acos(clamp(c, -1.0, 1.0));
    float e = asin(clamp(dir.y, -1.0, 1.0));
    vec2 uv = vec2(sqrt(azimuth / 3.14159265358979), 0.5 + 0.5 * sign(e) * sqrt(abs(e) / 1.57079632679));
    return max(textureLod(u_sky_view, uv, 0.0).rgb, vec3(0.0));
}
)";
    return out;
}

bool read_atmosphere_table(Backend& state, AtmosphereTable which, AtmosphereTexels* out)
{
    AtmosphereState& own = atmosphere_state();
    Image* image = &own.sky_view_table;
    if (which == AtmosphereTable::kTransmittance) {
        image = &own.transmittance_table;
    } else if (which == AtmosphereTable::kMultiple) {
        image = &own.multiple_table;
    }
    out->width = image->extent.width;
    out->height = image->extent.height;
    return own.view_valid && read_half_image(state, image, &out->texels);
}

uint64_t atmosphere_generation() { return atmosphere_state().generation; }

void release_atmosphere(Backend& state)
{
    AtmosphereState& own = atmosphere_state();
    const VkDevice device = state.context.device();
    for (Image* image : {&own.transmittance_table, &own.multiple_table, &own.sky_view_table}) {
        if (image->image != VK_NULL_HANDLE) {
            state.memory.destroy_image(image);
        }
        *image = Image{};
    }
    if (device != nullptr) {
        for (const VkPipeline pipeline : {own.transmittance, own.multiple, own.sky_view, own.table_from_view}) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
        }
        if (own.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, own.layout, nullptr);
        }
        if (own.set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, own.set_layout, nullptr);
        }
    }
    const bool follows = own.table_follows;
    own = AtmosphereState{};
    own.table_follows = follows;
}

float half_to_float(uint16_t half)
{
    constexpr uint32_t kExponentShift = 10;
    constexpr uint32_t kExponentMask = 0x1F;
    constexpr uint32_t kMantissaMask = 0x3FF;
    constexpr int kBias = 15;
    const uint32_t sign = (static_cast<uint32_t>(half) >> 15U) & 1U;
    const uint32_t exponent = (static_cast<uint32_t>(half) >> kExponentShift) & kExponentMask;
    const uint32_t mantissa = static_cast<uint32_t>(half) & kMantissaMask;
    float value = 0.0F;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(mantissa), 1 - kBias - static_cast<int>(kExponentShift));
    } else if (exponent == kExponentMask) {
        value = mantissa == 0 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
    } else {
        value = std::ldexp(static_cast<float>(mantissa | (1U << kExponentShift)),
                           static_cast<int>(exponent) - kBias - static_cast<int>(kExponentShift));
    }
    return sign != 0 ? -value : value;
}

} // namespace render::vulkan
