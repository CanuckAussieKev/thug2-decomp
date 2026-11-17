#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_brdf.h"
#include "engine/gfx/rt_light_field.h"
#include "engine/gfx/rt_material_table.h"

// NOT ORIGINAL - round W2: thin leaves lit by the one light (docs/design/vulkan.md "Round W2: grass and clutter lit by
// the one light"). A grass blade, a weed's leaf, a fallen leaf or a sheet of paper is a thin sheet: the side toward the
// eye reflects what reaches it through the one BRDF (rt_brdf.h: GGX + the coupled Lambert, the material's albedo, F0 and
// roughness - rt_material_table.h's kinds), and the light reaching its other side comes through it diffusely - its
// diffuse transmittance per channel (the kind's, measured), leaving through the near face's interface (1 - E(N.V), as
// the diffuse does). The light: the light store's probe at the sheet (light_window.h - the sky's, the bounce's, the
// lamps'; a white diffuser facing n receives probe(n)) and the sun's or the moon's light per unit of N.L x its traced
// visibility. The environment round the mirror direction - the specular's - is the probe toward it.
//
// In a lawn a blade is shaded by the blades above it: the canopy's gap fraction (Monsi and Saeki 1953, Beer's law in a
// canopy) for the leaf area above the point - the drawn blades' own: their count a square unit x a blade's area, the part
// above a height t of a tapered blade (1 - t)^2 of it. Leaves with a spherical angle distribution (G = 1/2: Ross 1981,
// The Radiation Regime and Architecture of Plant Stands) let the beam through exp(-G L / cos(zenith)) and the sky's even
// light 2 x the integral of that over the hemisphere's cosines. A blade's tip - no leaf area above it - is lit as a flat
// sheet of its material: exactly what the world's surfaces get from the same light (leaf_radiance with gap 1 facing up
// = rt_brdf.h's ambient + direct for a flat surface of the material - the check's case).
namespace render::rt::foliage {

inline constexpr float kLeafProjection = 0.5F; // G: a spherical leaf-angle distribution's shadow per leaf area
inline constexpr float kLeastCosine = 0.05F;   // (the beam's path through the canopy held below 20 x the vertical one)
inline constexpr int kGapSteps = 8;            // (the diffuse gap's integral: midpoints in the cosine)

// The beam's gap fraction under `leaf_area` (leaf area a ground area above the point) at the sun's cosine of zenith.
[[nodiscard]] inline float beam_gap(float leaf_area, float sun_cosine)
{
    return std::exp(-kLeafProjection * std::max(leaf_area, 0.0F) / std::max(sun_cosine, kLeastCosine));
}
// The even sky's: 2 x the integral over mu of exp(-G L / mu) mu dmu.
[[nodiscard]] inline float diffuse_gap(float leaf_area)
{
    float sum = 0.0F;
    for (int k = 0; k < kGapSteps; ++k) {
        const float mu = (static_cast<float>(k) + 0.5F) / static_cast<float>(kGapSteps);
        sum += std::exp(-kLeafProjection * std::max(leaf_area, 0.0F) / mu) * mu;
    }
    return 2.0F * sum / static_cast<float>(kGapSteps);
}

// A thin sheet's material: its BRDF's (albedo, F0, roughness) and its diffuse transmittance.
struct Sheet {
    brdf::Material surface;
    brdf::Rgb transmittance{};
};
// A kind's sheet with `albedo` (a texture's or the kind's colour - material_colour) and the kind's transmittance.
[[nodiscard]] inline Sheet sheet_of(MaterialKind kind, const brdf::Rgb& albedo)
{
    Sheet out;
    out.surface.albedo = albedo;
    out.surface.f0 = material_f0(kind);
    out.surface.roughness = material_values(kind).roughness;
    out.transmittance = material_transmittance(kind);
    return out;
}

// The light at the sheet: the store's probe there (indirect), the sun's or the moon's (toward, per unit of N.L), its
// visibility (the traced ray's x the canopy's beam gap) and the canopy's diffuse gap.
struct SheetLight {
    ProbeL1 probe;
    brdf::Vec3 toward{0.0F, 1.0F, 0.0F};
    brdf::Rgb sun{};
    float seen = 1.0F;
    float diffuse_gap = 1.0F;
};

// The radiance toward `v` (unit, toward the eye) of a sheet whose normal is `n` (unit; either side - the side toward the
// eye is the one reflecting).
[[nodiscard]] inline brdf::Rgb sheet_radiance(const Sheet& sheet, const brdf::Vec3& normal, const brdf::Vec3& v,
                                              const SheetLight& light)
{
    const float facing = brdf::dot(normal, v);
    const brdf::Vec3 n = facing < 0.0F ? brdf::Vec3{-normal.x, -normal.y, -normal.z} : normal;
    const float n_dot_v = std::max(std::abs(facing), brdf::kLeastCosine);
    const float v_dot_n = brdf::dot(v, n);
    const brdf::Vec3 r{(2.0F * v_dot_n * n.x) - v.x, (2.0F * v_dot_n * n.y) - v.y, (2.0F * v_dot_n * n.z) - v.z};
    const FieldRgb front = light.probe.at({n.x, n.y, n.z});
    const FieldRgb back = light.probe.at({-n.x, -n.y, -n.z});
    const FieldRgb around = light.probe.at({r.x, r.y, r.z});
    brdf::Rgb front_light{};
    brdf::Rgb around_light{};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        core::at(front_light, ch) = core::at(front, ch) * light.diffuse_gap;
        core::at(around_light, ch) = core::at(around, ch) * light.diffuse_gap;
    }
    brdf::Rgb out = brdf::ambient(sheet.surface, n_dot_v, front_light, around_light);
    brdf::Rgb sun{};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        core::at(sun, ch) = core::at(light.sun, ch) * light.seen;
    }
    const brdf::Rgb lit = brdf::direct(sheet.surface, n, v, light.toward, sun);
    const brdf::Rgb e = brdf::specular_albedo(sheet.surface.f0, n_dot_v, sheet.surface.roughness);
    const float through_sun = std::max(-brdf::dot(n, light.toward), 0.0F);
    for (std::size_t ch = 0; ch < 3; ++ch) {
        const float behind = (core::at(back, ch) * light.diffuse_gap) + (core::at(sun, ch) * through_sun);
        core::at(out, ch) += core::at(lit, ch) +
                             (core::at(sheet.transmittance, ch) * (1.0F - core::at(e, ch)) * behind);
    }
    return out;
}

// The same in GLSL (after brdf_glsl()): beam_gap, diffuse_gap, and
//   vec3 sheet_radiance(BrdfMaterial m, vec3 transmittance, vec3 normal, vec3 v, vec3 front, vec3 back, vec3 around,
//                       vec3 toward, vec3 sun)
// - front / back / around the probe's light toward n, -n and the mirror direction (each x the diffuse gap: the caller's),
// sun the direct light x its visibility.
inline constexpr const char* kFoliageGlsl = R"(
const float FOLIAGE_G = 0.5;
float beam_gap(float leaf_area, float sun_cosine)
{
    return exp(-FOLIAGE_G * max(leaf_area, 0.0) / max(sun_cosine, 0.05));
}
float diffuse_gap(float leaf_area)
{
    float sum = 0.0;
    for (int k = 0; k < 8; ++k) {
        float mu = (float(k) + 0.5) / 8.0;
        sum += exp(-FOLIAGE_G * max(leaf_area, 0.0) / mu) * mu;
    }
    return 2.0 * sum / 8.0;
}
vec3 sheet_radiance(BrdfMaterial m, vec3 transmittance, vec3 n, vec3 v, vec3 front, vec3 back, vec3 around, vec3 toward,
                    vec3 sun)
{
    float nv = max(dot(n, v), 1.0e-4);
    vec3 e = brdf_specular_albedo(m.f0, nv, m.roughness);
    vec3 out_light = brdf_ambient(m, nv, front, around) + brdf_direct(m, n, v, toward, sun);
    return out_light + transmittance * (vec3(1.0) - e) * (back + sun * max(-dot(n, toward), 0.0));
}
)";

} // namespace render::rt::foliage
