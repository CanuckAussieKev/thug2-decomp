#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "engine/gfx/rt_brdf.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - one light list with physical power (path tracing plan, stage 2; P3 + P4): every light the path tracer
// samples, each with its power in the global unit (rt_sky.h: 1 a white diffuser's radiance under the reference day;
// a light's "light" per N.L is its irradiance / pi), in one alias table a light is picked from - the reference path
// tracer's next-event estimation (gpu_vulkan/vulkan_path_reference.h) and later ReSTIR's candidates. API-neutral: the
// processor builds the tables, the device reads them (kLightListGlsl, the twin of the sampling here).
//
// The lights (one owner each - rt_path.h analytic_lamps), in two groups:
//   distant - their power per unit of the scene's cross-section (a distant light's flux over a scene of radius r is
//   pi r^2 x this - pbrt's DistantLight / infinite lights' Phi without the scene's size):
//   - the disc: the lighting state's direct light - the sun, or the moon at night (rt_daylight.h DayLight::light: one
//     direct light at a time) - a cone of its real solid angle; pi x its light per N.L (its irradiance face-on);
//   - the sky: an environment over the whole sphere (an equirectangular grid of its radiance, SkyDistribution below);
//     the integral of its luminance over the sphere;
//   local - their flux:
//   - emitting triangles (the light store's emission x the lamp texels - vulkan_rt_scene.h hit_surface's `emitted`,
//     the same function): a Lambertian emitter of mean radiance L over area A sends pi A L from each face it emits from
//     (round EMIT: a one-sided surface its front alone, a two-sided one both - pi A L or 2 pi A L)
//     (triangle_power; the device's scan measures L at the centroids of kTriangleLattice^2 equal sub-triangles);
//   - analytic lamps (rt_lights.h's lamp model, those without a surface of their own) and the frame record's point
//     lights where the level has no solved lamps: flux from their photometry (lamp_power).
// The pick: an alias table (Walker 1977, Vose 1991's construction - O(1) a sample), one entry a light: the two groups
// evenly (those there are - pbrt-v4's BVHLightSampler's split of its infinite lights against its local ones, the sun and
// the sky taken together as the one distant light they are), each light by its power within its group. One table over
// all of them by flux alone (pbrt-v3's PowerLightDistribution) needs the distant lights' flux through the scene,
// pi r^2 x the above: the scene's size then decides between the sun and the lamps - the check's 120 m floor gave the dim
// sky over 99 % of the picks beside a bright lamp. No spatial bound within the local group: a lamp across the level is
// picked by its flux as one beside the point is (the light BVH and ReSTIR's reuse are the later stages' answers).
// LightWeights::kKindsEvenly is stage 1's distribution (the kinds there are evenly, each kind's lights evenly) - for
// comparisons.
//
// The sky's distribution: kSkyGridWidth x kSkyGridHeight cells of equal azimuth and polar-angle steps, the sky's
// radiance constant over each (the grid holds it: what a ray that leaves the world sees and what the lights sample are
// one function). A direction is picked in two alias tables - a row by its luminance x solid angle, then a cell of it by
// its luminance - and uniformly by solid angle within the cell, so its pdf (solid angle) is the cell's luminance over the
// grid's integral: exactly what a BSDF ray that misses evaluates for MIS.
namespace render::rt::lights {

using Rgb = brdf::Rgb;
using Vec3 = brdf::Vec3;

inline constexpr float kPi = brdf::kPi;

// --- the alias table ----------------------------------------------------------------------------------------------
// One entry: kept with probability `threshold` when picked (else its `alias`); `pdf` the entry's own probability (its
// weight over the total - what MIS reads); `payload` what it stands for (the light list's: payload_of).
struct AliasEntry {
    float threshold = 0.0F;
    uint32_t alias = 0;
    float pdf = 0.0F;
    uint32_t payload = 0;
};
static_assert(sizeof(AliasEntry) == 16);
// Built from weights (>= 0; non-finite as 0). An entry of weight 0 is never picked (threshold 0, aliased to one that
// is). All 0 (or empty): every pdf 0. `total` the weights' sum.
struct AliasTable {
    std::vector<AliasEntry> entries;
    double total = 0.0;
};
[[nodiscard]] AliasTable build_alias_table(std::span<const double> weights);
// The pick for one number in [0, 1): the entry floor(u n), kept by its threshold against the fraction left.
[[nodiscard]] uint32_t alias_pick(std::span<const AliasEntry> entries, float u);

// --- the lights -----------------------------------------------------------------------------------------------------
enum class LightKind : uint32_t { kDisc = 0, kSky = 1, kTriangle = 2, kLamp = 3 };
inline constexpr uint32_t kKindShift = 30;
inline constexpr uint32_t kIndexMask = (1U << kKindShift) - 1U;
[[nodiscard]] constexpr uint32_t payload_of(LightKind kind, uint32_t index)
{
    return (static_cast<uint32_t>(kind) << kKindShift) | (index & kIndexMask);
}
[[nodiscard]] constexpr LightKind kind_of(uint32_t payload) { return static_cast<LightKind>(payload >> kKindShift); }
[[nodiscard]] constexpr uint32_t index_of(uint32_t payload) { return payload & kIndexMask; }
// The list's order: the disc, the sky, the triangles (in the scan's order), the lamps.
inline constexpr uint32_t kDiscEntry = 0;
inline constexpr uint32_t kSkyEntry = 1;
inline constexpr uint32_t kFirstTriangleEntry = 2;
// A triangle's mean radiance is measured at the centroids of the kTriangleLattice^2 equal sub-triangles its edges cut
// in kTriangleLattice make (a lamp drawn in a texture lights only its lamp texels - a few points miss them).
inline constexpr uint32_t kTriangleLattice = 8;

[[nodiscard]] float luma(const Rgb& c);
// A distant disc of light per N.L `light` (its irradiance / pi): pi x its luminance (per unit of cross-section).
[[nodiscard]] double disc_power(const Rgb& light);
// The sky of luminance integral `integral` (over the sphere, solid angle): that (per unit of cross-section).
[[nodiscard]] double sky_power(double integral);
// An emitting triangle of mean luminance `radiance` and `area` sending from `sides` faces (round EMIT: 1 - the face a
// one-sided surface is drawn with - or 2 - a two-sided one; GLSL emitter_sides).
[[nodiscard]] double triangle_power(double radiance, double area, double sides);
// A lamp of rt_lights.h's model (its colour x intensity): its intensity toward a direction is pi x its light per N.L x d^2
// far away - a point pi I core^2 every way (flux 4 pi^2 I core^2); a panel (a two-sided disc of radiance I, radius core)
// pi I core^2 |cos| (2 pi^2 I core^2); a line (a tube of length 2 h) pi I core h sin of the angle to its axis
// (pi^3 I core h). The reach's window is left out (the flux before it: a bound).
[[nodiscard]] double lamp_power(const Emitter& lamp);
// (one point light rule - rt_lights.cpp select_view_lights reads it too) The frame record's point light as a lamp: its
// colour's brightest channel its intensity (x `scale`), its radius its reach, point_core its core. False: not a point
// light, or no light.
[[nodiscard]] bool point_emitter(const Light& light, float scale, Emitter* out);

enum class LightWeights : uint8_t { kPower = 0, kKindsEvenly = 1 };

// What the list is made of: the disc's light per N.L and cosine of its radius (no disc: 1), the sky's luminance
// integral (0: none), each listed triangle's power (the device's scan), the lamps.
struct LightSet {
    Rgb disc_light{};
    float disc_cos = 1.0F;
    double sky_integral = 0.0;
    std::vector<float> triangle_powers;
    std::vector<Emitter> lamps;
};
struct LightList {
    AliasTable table; // kDiscEntry, kSkyEntry, the triangles, the lamps
    uint32_t first_lamp = kFirstTriangleEntry;
    std::array<double, 4> kind_power{}; // the disc's, the sky's, the triangles', the lamps' (the log's)
    std::array<double, 4> kind_pick{};  // each kind's share of the picks
};
[[nodiscard]] LightList build_light_list(const LightSet& set, LightWeights weights);

// --- the sky ---------------------------------------------------------------------------------------------------------
inline constexpr uint32_t kSkyGridWidth = 1024;
inline constexpr uint32_t kSkyGridHeight = 512;
// A direction's cell: column by its azimuth atan2(z, x) from -pi, row by its polar angle from +y (up).
[[nodiscard]] uint32_t sky_cell_of(const Vec3& dir, uint32_t width, uint32_t height);
// The cell's centre direction, and a point of it uniform in solid angle (u1: azimuth, u2: cosine).
[[nodiscard]] Vec3 sky_cell_centre(uint32_t column, uint32_t row, uint32_t width, uint32_t height);
[[nodiscard]] Vec3 sky_cell_point(uint32_t column, uint32_t row, uint32_t width, uint32_t height, float u1, float u2);
[[nodiscard]] double sky_cell_solid_angle(uint32_t row, uint32_t width, uint32_t height);
// The two-level alias tables (rows, then each row's cells - only threshold and alias are read) and the integral of the
// luminance over the sphere (0: no sky).
struct SkyDistribution {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<AliasEntry> rows;  // height
    std::vector<AliasEntry> cells; // height x width, row by row
    std::vector<float> luminance;  // per cell (row by row)
    double integral = 0.0;
};
// From the grid's radiance (row by row, rgb + unused).
[[nodiscard]] SkyDistribution build_sky_distribution(std::span<const std::array<float, 4>> radiance, uint32_t width,
                                                     uint32_t height);
struct SkySample {
    Vec3 dir;
    float pdf = 0.0F; // solid angle
    uint32_t cell = 0;
};
[[nodiscard]] SkySample sample_sky(const SkyDistribution& sky, float u0, float u1, float u2, float u3);
[[nodiscard]] float sky_pdf(const SkyDistribution& sky, const Vec3& dir);

// The device's twin (after rt_path.h's kPathGlsl; the caller declares the buffers - `light_entries` (AliasEntry: float
// threshold, uint alias, float pdf, uint payload), `sky_alias` (uvec2: threshold bits, alias; the rows then the cells)
// and `sky_cells` (vec4) - and defines SKY_WIDTH / SKY_HEIGHT as uint constants):
//   uint light_pick(float u, uint count)
//   uint sky_cell_of(vec3 dir); vec3 sky_cell_centre(uvec2 cell)
//   vec3 sky_sample(float u0, float u1, float u2, float u3, out uint cell)
//   LIGHT_DISC, LIGHT_SKY, LIGHT_TRIANGLE, LIGHT_LAMP, light_kind(payload), light_index(payload)
extern const char* const kLightListGlsl;

} // namespace render::rt::lights
