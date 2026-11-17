#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>

#include "base/core/checked_index.h"

// NOT ORIGINAL - round M (materials and the light model; docs/design/vulkan.md "One light model: the BRDF and the
// material table"): the one physically based light model every surface, character and - later - every ray hit is
// shaded with. It takes a light and a material and gives the radiance leaving toward the eye; WHICH light reaches a
// point (the relit store's terms, the lamps, the probes) is the caller's (round O's for objects and ray hits).
//
// Units: a light is given as `light` = the radiance of a white Lambertian diffuser it lights face-on (E / pi, the
// frame's unit - rt_sky.h: 1 a white diffuser under the reference day). The radiance a surface sends toward the eye
// from one directional light is then pi x f(v, l) x light x N.L; from an environment of even radiance it is
// the directional albedo x that radiance.
//
// The model (each part a published model, no tuned factor):
//   - specular: GGX / Trowbridge-Reitz D (Walter et al. 2007, "Microfacet Models for Refraction through Rough
//     Surfaces"), the height-correlated Smith visibility V = G2 / (4 N.L N.V) (Heitz 2014, "Understanding the
//     Masking-Shadowing Function in Microfacet-Based BRDFs", eq. 99), Schlick's Fresnel (Schlick 1994) from F0 to 1 at
//     grazing; alpha = roughness^2 (Burley 2012, "Physically-Based Shading at Disney");
//   - multiple scattering between the microfacets: the single-scattering lobe x (1 + F0 (1 / E_ss - 1)) (Turquin 2019,
//     "Practical multiple scattering compensation for microfacet models"; Fdez-Aguera 2019, JCGT 8(1)) - E_ss the
//     lobe's directional albedo with F0 = 1, from a table of the split-sum DFG terms (Karis 2013, "Real Shading in
//     Unreal Engine 4") integrated from this lobe itself (kDfgTable): the white furnace stays at 1 at every roughness
//     instead of losing up to two thirds of a rough lobe's energy at grazing angles;
//   - diffuse: Lambert x (1 - E(N.V)), E the specular's directional albedo (with its Fresnel and compensation) - the
//     light the specular doesn't reflect enters the surface; a white surface in a white furnace returns exactly 1
//     (Fdez-Aguera 2019's coupling; not reciprocal, energy-exact);
//   - skin (scatter_mm > 0): the diffuse wrapped round the terminator per channel by how far light diffuses under the
//     surface against how sharply it curves, w = min(scatter / curvature radius, 1), (N.L + w) / (1 + w)^2 - the
//     energy-conserving wrap (its integral over the light's directions on a sphere is Lambert's); the mean free path is
//     the material's (rt_material_table.h: Jensen et al. 2001's skin), the curvature the pixel's;
//   - cloth (sheen > 0): Estevez & Kulla's "Charlie" sheen distribution (2017, "Production Friendly Microfacet Sheen
//     BRDF") with Neubelt & Pettineo's visibility (2013, "Crafting a Next-Gen Material Pipeline for The Order: 1886"),
//     the base layer below scaled by 1 - the sheen's largest channel (a bound: the sheen never reflects more than that);
//   - hair (hair_direct): Marschner et al.'s three lobes (2003, "Light Scattering from Human Hair Fibers") - R, TT, TRT
//     - in Karis's real-time form (2016, "Physically Based Hair Shading in Unreal"): longitudinal Gaussians shifted by
//     the cuticle's tilt, the azimuthal terms fitted there, keratin's index 1.55; the fibre's colour its absorption.
//
// Not modelled yet (round M's record): a thickness for skin's translucency, anisotropic GGX, a clear coat, iridescence;
// the diffuse coupling's reciprocity (Kulla-Conty's E_avg form).
namespace render::rt::brdf {

using Rgb = std::array<float, 3>;

inline constexpr float kPi = 3.14159265358979F;
// The least perceptual roughness: GGX's peak at alpha^2 under ~4e-6 overflows a 16-bit float target (Filament's
// MIN_PERCEPTUAL_ROUGHNESS for its high-precision path) - a numerical floor, not a material's.
inline constexpr float kLeastRoughness = 0.045F;
inline constexpr float kLeastCosine = 1.0e-4F;
// Keratin's refractive index (Marschner et al. 2003) and the cuticle scales' tilt: alpha_R -10 to -5 degrees there; the
// middle (radians). The TT and TRT lobes shift by -alpha / 2 and -3 alpha / 2.
inline constexpr float kHairIndex = 1.55F;
inline constexpr float kHairTilt = -0.1309F;

struct Material {
    Rgb albedo{};             // the diffuse albedo (linear; 0 for a metal)
    Rgb f0{0.04F, 0.04F, 0.04F}; // the specular reflectance face-on
    float roughness = 0.5F;   // perceptual (alpha = roughness^2)
    Rgb scatter_mm{};         // (skin) the diffuse mean free path per channel, millimetres (0: none)
    float curvature_mm = 0.0F; // (skin) the surface's radius of curvature at the point, millimetres (0: flat)
    Rgb sheen{};              // (cloth) the sheen's colour (0: none)
    float sheen_roughness = 0.0F;
};

[[nodiscard]] inline float saturate(float value) { return std::clamp(value, 0.0F, 1.0F); }
[[nodiscard]] inline float alpha_of(float roughness)
{
    const float r = std::clamp(roughness, kLeastRoughness, 1.0F);
    return r * r;
}
// GGX's normal distribution D(h).
[[nodiscard]] inline float ggx_distribution(float n_dot_h, float alpha)
{
    const float a2 = alpha * alpha;
    const float q = (n_dot_h * n_dot_h * (a2 - 1.0F)) + 1.0F;
    return a2 / (kPi * q * q);
}
// The height-correlated Smith visibility, G2 / (4 N.L N.V).
[[nodiscard]] inline float smith_visibility(float n_dot_v, float n_dot_l, float alpha)
{
    const float a2 = alpha * alpha;
    const float lv = n_dot_l * std::sqrt((n_dot_v * n_dot_v * (1.0F - a2)) + a2);
    const float ll = n_dot_v * std::sqrt((n_dot_l * n_dot_l * (1.0F - a2)) + a2);
    return 0.5F / std::max(lv + ll, 1.0e-7F);
}
[[nodiscard]] inline float fresnel(float f0, float cosine)
{
    const float c = 1.0F - saturate(cosine);
    const float c2 = c * c;
    return f0 + ((1.0F - f0) * c2 * c2 * c);
}
// The dielectric reflectance for a cosine inside a medium of index `eta` (Schlick from the face-on (eta-1)^2/(eta+1)^2).
[[nodiscard]] inline float f0_of_index(float eta) { return ((eta - 1.0F) * (eta - 1.0F)) / ((eta + 1.0F) * (eta + 1.0F)); }

// The split-sum DFG terms: the lobe's directional albedo is F0 x a + b (F0 = 1: a + b). A table of the integral
// (thug2_brdf_check --table: 2^20 importance samples of this file's own lobe a point) over roughness 0 - 1 and sqrt(N.V)
// 0 - 1, read bilinearly - no fit's error enters the energy compensation (Karis 2014's analytic fit was off by up to
// 0.3 at grazing angles; the check measures the table against the integral between its points).
inline constexpr std::size_t kDfgSize = 16;
inline constexpr std::array<float, kDfgSize * kDfgSize * 2> kDfgTable{{
    0.04681F, 0.94684F, 0.03430F, 0.88589F, 0.08929F, 0.90304F, 0.18614F, 0.81246F, 0.30919F, 0.69038F, 0.44542F, 0.55440F, 0.58194F, 0.41797F, 0.70720F, 0.29276F, 0.81241F, 0.18757F, 0.89261F, 0.10738F, 0.94705F, 0.05293F, 0.97888F, 0.02111F, 0.99394F, 0.00605F, 0.99904F, 0.00096F, 0.99996F, 0.00004F, 1.00000F, 0.00000F,
    0.08753F, 0.90735F, 0.06366F, 0.82802F, 0.09868F, 0.86580F, 0.19032F, 0.80240F, 0.31126F, 0.68656F, 0.44644F, 0.55269F, 0.58240F, 0.41719F, 0.70735F, 0.29243F, 0.81241F, 0.18746F, 0.89254F, 0.10737F, 0.94698F, 0.05296F, 0.97882F, 0.02113F, 0.99390F, 0.00607F, 0.99901F, 0.00096F, 0.99994F, 0.00004F, 0.99998F, 0.00000F,
    0.23671F, 0.76161F, 0.21633F, 0.73584F, 0.19482F, 0.69695F, 0.23651F, 0.68551F, 0.33392F, 0.63049F, 0.45750F, 0.52689F, 0.58720F, 0.40533F, 0.70867F, 0.28734F, 0.81197F, 0.18569F, 0.89140F, 0.10711F, 0.94574F, 0.05323F, 0.97782F, 0.02145F, 0.99318F, 0.00625F, 0.99852F, 0.00103F, 0.99958F, 0.00005F, 0.99968F, 0.00000F,
    0.38747F, 0.61153F, 0.37536F, 0.60153F, 0.35011F, 0.57414F, 0.34707F, 0.54369F, 0.39433F, 0.51011F, 0.48568F, 0.45162F, 0.59779F, 0.36555F, 0.71005F, 0.26870F, 0.80883F, 0.17832F, 0.88661F, 0.10518F, 0.94095F, 0.05346F, 0.97382F, 0.02216F, 0.99019F, 0.00676F, 0.99636F, 0.00122F, 0.99792F, 0.00008F, 0.99830F, 0.00000F,
    0.52026F, 0.47894F, 0.51285F, 0.47320F, 0.49426F, 0.45578F, 0.47980F, 0.42907F, 0.49122F, 0.39646F, 0.53936F, 0.35523F, 0.61813F, 0.29956F, 0.71057F, 0.23141F, 0.79977F, 0.16092F, 0.87423F, 0.09905F, 0.92877F, 0.05254F, 0.96352F, 0.02291F, 0.98218F, 0.00753F, 0.99030F, 0.00158F, 0.99318F, 0.00015F, 0.99434F, 0.00000F,
    0.62925F, 0.37005F, 0.62404F, 0.36585F, 0.60985F, 0.35303F, 0.59587F, 0.33228F, 0.59214F, 0.30480F, 0.61001F, 0.27145F, 0.65337F, 0.23146F, 0.71635F, 0.18468F, 0.78698F, 0.13450F, 0.85298F, 0.08729F, 0.90603F, 0.04907F, 0.94303F, 0.02292F, 0.96533F, 0.00827F, 0.97692F, 0.00202F, 0.98242F, 0.00026F, 0.98532F, 0.00001F,
    0.71486F, 0.28447F, 0.71033F, 0.28104F, 0.69825F, 0.27073F, 0.68507F, 0.25419F, 0.67577F, 0.23212F, 0.67748F, 0.20544F, 0.69541F, 0.17491F, 0.73016F, 0.14122F, 0.77690F, 0.10589F, 0.82749F, 0.07188F, 0.87385F, 0.04287F, 0.91066F, 0.02157F, 0.93629F, 0.00859F, 0.95229F, 0.00243F, 0.96175F, 0.00039F, 0.96768F, 0.00001F,
    0.78022F, 0.21911F, 0.77537F, 0.21615F, 0.76334F, 0.20750F, 0.74956F, 0.19394F, 0.73710F, 0.17613F, 0.73011F, 0.15494F, 0.73238F, 0.13129F, 0.74617F, 0.10615F, 0.77083F, 0.08065F, 0.80284F, 0.05638F, 0.83708F, 0.03523F, 0.86866F, 0.01894F, 0.89446F, 0.00827F, 0.91365F, 0.00266F, 0.92729F, 0.00051F, 0.93714F, 0.00002F,
    0.82922F, 0.17010F, 0.82336F, 0.16746F, 0.80994F, 0.16001F, 0.79407F, 0.14864F, 0.77838F, 0.13406F, 0.76542F, 0.11708F, 0.75771F, 0.09858F, 0.75721F, 0.07944F, 0.76474F, 0.06060F, 0.77951F, 0.04301F, 0.79930F, 0.02774F, 0.82115F, 0.01570F, 0.84232F, 0.00740F, 0.86105F, 0.00265F, 0.87681F, 0.00059F, 0.88992F, 0.00003F,
    0.86554F, 0.13374F, 0.85816F, 0.13133F, 0.84236F, 0.12476F, 0.82326F, 0.11503F, 0.80353F, 0.10288F, 0.78520F, 0.08908F, 0.76997F, 0.07441F, 0.75929F, 0.05960F, 0.75415F, 0.04539F, 0.75482F, 0.03242F, 0.76078F, 0.02129F, 0.77082F, 0.01248F, 0.78341F, 0.00622F, 0.79714F, 0.00243F, 0.81101F, 0.00062F, 0.82449F, 0.00004F,
    0.89232F, 0.10692F, 0.88303F, 0.10464F, 0.86406F, 0.09871F, 0.84082F, 0.09021F, 0.81626F, 0.07990F, 0.79233F, 0.06849F, 0.77041F, 0.05665F, 0.75155F, 0.04501F, 0.73658F, 0.03409F, 0.72601F, 0.02436F, 0.72000F, 0.01614F, 0.71832F, 0.00966F, 0.72044F, 0.00502F, 0.72568F, 0.00209F, 0.73337F, 0.00059F, 0.74292F, 0.00005F,
    0.91204F, 0.08715F, 0.90089F, 0.08494F, 0.87794F, 0.07945F, 0.84972F, 0.07187F, 0.81965F, 0.06293F, 0.78967F, 0.05332F, 0.76111F, 0.04360F, 0.73487F, 0.03429F, 0.71161F, 0.02577F, 0.69179F, 0.01833F, 0.67570F, 0.01217F, 0.66346F, 0.00738F, 0.65505F, 0.00393F, 0.65029F, 0.00172F, 0.64894F, 0.00052F, 0.65071F, 0.00005F,
    0.92659F, 0.07254F, 0.91332F, 0.07034F, 0.88589F, 0.06515F, 0.85220F, 0.05822F, 0.81602F, 0.05033F, 0.77957F, 0.04208F, 0.74423F, 0.03397F, 0.71089F, 0.02639F, 0.68013F, 0.01963F, 0.65231F, 0.01386F, 0.62767F, 0.00918F, 0.60637F, 0.00559F, 0.58848F, 0.00303F, 0.57401F, 0.00137F, 0.56292F, 0.00044F, 0.55512F, 0.00005F,
    0.93737F, 0.06169F, 0.92181F, 0.05946F, 0.88955F, 0.05443F, 0.84988F, 0.04799F, 0.80717F, 0.04088F, 0.76393F, 0.03367F, 0.72169F, 0.02679F, 0.68140F, 0.02053F, 0.64363F, 0.01508F, 0.60869F, 0.01054F, 0.57675F, 0.00694F, 0.54790F, 0.00422F, 0.52213F, 0.00231F, 0.49943F, 0.00107F, 0.47975F, 0.00036F, 0.46302F, 0.00004F,
    0.94539F, 0.05359F, 0.92738F, 0.05128F, 0.88992F, 0.04633F, 0.84399F, 0.04022F, 0.79447F, 0.03371F, 0.74426F, 0.02731F, 0.69510F, 0.02138F, 0.64807F, 0.01613F, 0.60375F, 0.01169F, 0.56248F, 0.00808F, 0.52438F, 0.00527F, 0.48944F, 0.00320F, 0.45759F, 0.00175F, 0.42873F, 0.00082F, 0.40270F, 0.00029F, 0.37938F, 0.00004F,
    0.95141F, 0.04748F, 0.93081F, 0.04508F, 0.88793F, 0.04012F, 0.83544F, 0.03424F, 0.77894F, 0.02819F, 0.72172F, 0.02244F, 0.66578F, 0.01726F, 0.61233F, 0.01281F, 0.56205F, 0.00914F, 0.51528F, 0.00623F, 0.47213F, 0.00403F, 0.43256F, 0.00243F, 0.39644F, 0.00133F, 0.36359F, 0.00063F, 0.33379F, 0.00023F, 0.30682F, 0.00003F,
}};
struct Dfg {
    float a = 0.0F;
    float b = 0.0F;
};
[[nodiscard]] inline Dfg dfg(float n_dot_v, float roughness)
{
    constexpr float kLast = static_cast<float>(kDfgSize - 1);
    const float row = std::clamp(roughness, 0.0F, 1.0F) * kLast;
    const float column = std::sqrt(std::clamp(n_dot_v, 0.0F, 1.0F)) * kLast;
    const auto row0 = static_cast<std::size_t>(std::min(row, kLast - 1.0F));
    const auto column0 = static_cast<std::size_t>(std::min(column, kLast - 1.0F));
    const float fr = row - static_cast<float>(row0);
    const float fc = column - static_cast<float>(column0);
    const auto at = [](std::size_t r, std::size_t c, std::size_t part) {
        return core::at(kDfgTable, (((r * kDfgSize) + c) * 2) + part);
    };
    Dfg out;
    for (std::size_t part = 0; part < 2; ++part) {
        const float top = (at(row0, column0, part) * (1.0F - fc)) + (at(row0, column0 + 1, part) * fc);
        const float bottom = (at(row0 + 1, column0, part) * (1.0F - fc)) + (at(row0 + 1, column0 + 1, part) * fc);
        (part == 0 ? out.a : out.b) = (top * (1.0F - fr)) + (bottom * fr);
    }
    return out;
}
// The multiple-scattering compensation for one channel's F0.
[[nodiscard]] inline float compensation(float f0, const Dfg& terms)
{
    const float single = std::max(terms.a + terms.b, 1.0e-4F);
    return 1.0F + (f0 * ((1.0F / single) - 1.0F));
}
// The specular's directional albedo toward the eye per channel (compensated: the share of an even environment it
// reflects); the diffuse gets 1 - it.
[[nodiscard]] inline Rgb specular_albedo(const Rgb& f0, float n_dot_v, float roughness)
{
    const Dfg terms = dfg(n_dot_v, roughness);
    Rgb out{};
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        core::at(out, ch) = saturate(((core::at(f0, ch) * terms.a) + terms.b) * compensation(core::at(f0, ch), terms));
    }
    return out;
}

// Estevez & Kulla's Charlie distribution and Neubelt & Pettineo's visibility.
[[nodiscard]] inline float sheen_distribution(float n_dot_h, float roughness)
{
    const float inverse = 1.0F / std::max(alpha_of(roughness), 1.0e-3F);
    const float sin2 = std::max(1.0F - (n_dot_h * n_dot_h), 0.0F);
    return (2.0F + inverse) * std::pow(sin2, inverse * 0.5F) / (2.0F * kPi);
}
[[nodiscard]] inline float sheen_visibility(float n_dot_v, float n_dot_l)
{
    return 1.0F / std::max(4.0F * (n_dot_l + n_dot_v - (n_dot_l * n_dot_v)), 1.0e-4F);
}

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};
[[nodiscard]] inline float dot(const Vec3& a, const Vec3& b) { return (a.x * b.x) + (a.y * b.y) + (a.z * b.z); }
[[nodiscard]] inline Vec3 normalise(const Vec3& a)
{
    const float length = std::sqrt(dot(a, a));
    return length > 0.0F ? Vec3{a.x / length, a.y / length, a.z / length} : a;
}

// The radiance toward `v` from one directional light (toward `l`, `light` as above) on a surface of normal `n`.
[[nodiscard]] inline Rgb direct(const Material& m, const Vec3& n, const Vec3& v, const Vec3& l, const Rgb& light)
{
    const float n_dot_l_raw = dot(n, l);
    const float n_dot_v = std::max(dot(n, v), kLeastCosine);
    const Vec3 h = normalise({v.x + l.x, v.y + l.y, v.z + l.z});
    const float n_dot_h = saturate(dot(n, h));
    const float v_dot_h = saturate(dot(v, h));
    const float alpha = alpha_of(m.roughness);
    const Rgb e = specular_albedo(m.f0, n_dot_v, m.roughness);
    const Dfg terms = dfg(n_dot_v, m.roughness);
    const float sheen_most = std::max({std::get<0>(m.sheen), std::get<1>(m.sheen), std::get<2>(m.sheen)});
    const float base = 1.0F - saturate(sheen_most);
    Rgb out{};
    const float n_dot_l = saturate(n_dot_l_raw);
    const float lobe = n_dot_l > 0.0F ? kPi * ggx_distribution(n_dot_h, alpha) * smith_visibility(n_dot_v, n_dot_l, alpha) * n_dot_l
                                      : 0.0F;
    const float sheen_lobe = n_dot_l > 0.0F && sheen_most > 0.0F
                                 ? kPi * sheen_distribution(n_dot_h, m.sheen_roughness) * sheen_visibility(n_dot_v, n_dot_l) * n_dot_l
                                 : 0.0F;
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        float diffuse_cosine = n_dot_l;
        if (core::at(m.scatter_mm, ch) > 0.0F && m.curvature_mm > 0.0F) {
            const float w = std::min(core::at(m.scatter_mm, ch) / m.curvature_mm, 1.0F);
            diffuse_cosine = std::max(n_dot_l_raw + w, 0.0F) / ((1.0F + w) * (1.0F + w));
        }
        const float diffuse = core::at(m.albedo, ch) * (1.0F - core::at(e, ch)) * diffuse_cosine;
        const float specular = lobe * fresnel(core::at(m.f0, ch), v_dot_h) * compensation(core::at(m.f0, ch), terms);
        core::at(out, ch) = (((diffuse + specular) * base) + (sheen_lobe * core::at(m.sheen, ch))) * core::at(light, ch);
    }
    return out;
}

// The radiance toward `v` from an environment: `diffuse_light` the light a white diffuser facing `n` gets from it (as
// `light` above), `specular_light` the environment's radiance round the mirror direction (an even environment: the
// same value).
[[nodiscard]] inline Rgb ambient(const Material& m, float n_dot_v, const Rgb& diffuse_light, const Rgb& specular_light)
{
    const Rgb e = specular_albedo(m.f0, std::max(n_dot_v, kLeastCosine), m.roughness);
    const float base = 1.0F - saturate(std::max({std::get<0>(m.sheen), std::get<1>(m.sheen), std::get<2>(m.sheen)}));
    Rgb out{};
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        core::at(out, ch) = ((core::at(m.albedo, ch) * (1.0F - core::at(e, ch)) * core::at(diffuse_light, ch)) + (core::at(e, ch) * core::at(specular_light, ch))) * base;
    }
    return out;
}

// Marschner's hair lobes (Karis 2016's form) for a fibre along `t` (unit), the light toward `l`, the eye toward `v`;
// `colour` the fibre's colour (its absorption), `roughness` the cuticle's (perceptual). The radiance per `light`
// (as above), without a cosine: a fibre has none.
[[nodiscard]] inline float hair_gaussian(float width, float x)
{
    return std::exp(-0.5F * x * x / (width * width)) / (std::sqrt(2.0F * kPi) * width);
}
[[nodiscard]] inline Rgb hair_direct(const Rgb& colour, float roughness, const Vec3& t, const Vec3& v, const Vec3& l, const Rgb& light)
{
    const float sin_l = std::clamp(dot(l, t), -1.0F, 1.0F);
    const float sin_v = std::clamp(dot(v, t), -1.0F, 1.0F);
    const float cos_d = std::cos(0.5F * std::fabs(std::asin(sin_v) - std::asin(sin_l)));
    const Vec3 lp{l.x - (sin_l * t.x), l.y - (sin_l * t.y), l.z - (sin_l * t.z)};
    const Vec3 vp{v.x - (sin_v * t.x), v.y - (sin_v * t.y), v.z - (sin_v * t.z)};
    const float cos_phi = dot(lp, vp) / std::sqrt((dot(lp, lp) * dot(vp, vp)) + 1.0e-4F);
    const float cos_half_phi = std::sqrt(saturate(0.5F + (0.5F * cos_phi)));
    const float r = std::clamp(roughness, kLeastRoughness, 1.0F);
    const float r2 = r * r;
    const float f0 = f0_of_index(kHairIndex);
    const float sum_sin = sin_l + sin_v;
    // R: reflected off the cuticle
    const float m_r = hair_gaussian(r2, sum_sin + (2.0F * kHairTilt));
    const float n_r = 0.25F * cos_half_phi * fresnel(f0, std::sqrt(saturate(0.5F + (0.5F * dot(l, v)))));
    // TT: through the fibre
    const float m_tt = hair_gaussian(0.5F * r2, sum_sin - kHairTilt);
    const float index = (1.19F / cos_d) + (0.36F * cos_d);
    const float a = 1.0F / index;
    const float h = (1.0F + (a * (0.6F - (0.8F * cos_phi)))) * cos_half_phi;
    const float f_tt = fresnel(f0, cos_d * std::sqrt(saturate(1.0F - (h * h))));
    const float n_tt = std::exp((-3.65F * cos_phi) - 3.98F) * (1.0F - f_tt) * (1.0F - f_tt);
    const float power_tt = 0.5F * std::sqrt(saturate(1.0F - (h * a * h * a))) / cos_d;
    // TRT: reflected inside it
    const float m_trt = hair_gaussian(2.0F * r2, sum_sin - (3.0F * kHairTilt));
    const float f_trt = fresnel(f0, 0.5F * cos_d);
    const float n_trt = std::exp((17.0F * cos_phi) - 16.78F) * (1.0F - f_trt) * (1.0F - f_trt) * f_trt;
    const float power_trt = 0.8F / cos_d;
    Rgb out{};
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        const float c = std::max(core::at(colour, ch), 1.0e-4F);
        const float s = (m_r * n_r) + (m_tt * n_tt * std::pow(c, power_tt)) + (m_trt * n_trt * std::pow(c, power_trt));
        core::at(out, ch) = kPi * s * core::at(light, ch);
    }
    return out;
}

// The same model in GLSL (names brdf_*; BrdfMaterial mirrors Material): brdf_glsl() below - this text after the DFG
// table's declaration.
inline constexpr const char* kBrdfGlsl = R"(const float BRDF_PI = 3.14159265358979;
const float BRDF_LEAST_ROUGHNESS = 0.045;
const float BRDF_HAIR_INDEX = 1.55;
const float BRDF_HAIR_TILT = -0.1309;
struct BrdfMaterial {
    vec3 albedo;
    vec3 f0;
    float roughness;
    vec3 scatter_mm;
    float curvature_mm;
    vec3 sheen;
    float sheen_roughness;
};
BrdfMaterial brdf_material(vec3 albedo, vec3 f0, float roughness)
{
    return BrdfMaterial(albedo, f0, roughness, vec3(0.0), 0.0, vec3(0.0), 0.0);
}
float brdf_alpha(float roughness) { float r = clamp(roughness, BRDF_LEAST_ROUGHNESS, 1.0); return r * r; }
float brdf_ggx(float nh, float a)
{
    float a2 = a * a;
    float q = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (BRDF_PI * q * q);
}
float brdf_visibility(float nv, float nl, float a)
{
    float a2 = a * a;
    float lv = nl * sqrt(nv * nv * (1.0 - a2) + a2);
    float ll = nv * sqrt(nl * nl * (1.0 - a2) + a2);
    return 0.5 / max(lv + ll, 1.0e-7);
}
float brdf_fresnel(float f0, float c) { float x = 1.0 - clamp(c, 0.0, 1.0); float x2 = x * x; return f0 + (1.0 - f0) * x2 * x2 * x; }
vec3 brdf_fresnel(vec3 f0, float c) { float x = 1.0 - clamp(c, 0.0, 1.0); float x2 = x * x; return f0 + (1.0 - f0) * (x2 * x2 * x); }
vec2 brdf_dfg_at(int r, int c) { int i = (r * BRDF_DFG_SIZE + c) * 2; return vec2(BRDF_DFG[i], BRDF_DFG[i + 1]); }
vec2 brdf_dfg(float nv, float roughness)
{
    float last = float(BRDF_DFG_SIZE - 1);
    float row = clamp(roughness, 0.0, 1.0) * last;
    float column = sqrt(clamp(nv, 0.0, 1.0)) * last;
    int r0 = int(min(row, last - 1.0));
    int c0 = int(min(column, last - 1.0));
    float fr = row - float(r0);
    float fc = column - float(c0);
    vec2 top = mix(brdf_dfg_at(r0, c0), brdf_dfg_at(r0, c0 + 1), fc);
    vec2 bottom = mix(brdf_dfg_at(r0 + 1, c0), brdf_dfg_at(r0 + 1, c0 + 1), fc);
    return mix(top, bottom, fr);
}
vec3 brdf_compensation(vec3 f0, vec2 terms) { return 1.0 + f0 * (1.0 / max(terms.x + terms.y, 1.0e-4) - 1.0); }
vec3 brdf_specular_albedo(vec3 f0, float nv, float roughness)
{
    vec2 terms = brdf_dfg(nv, roughness);
    return clamp((f0 * terms.x + terms.y) * brdf_compensation(f0, terms), 0.0, 1.0);
}
float brdf_sheen_d(float nh, float roughness)
{
    float inverse = 1.0 / max(brdf_alpha(roughness), 1.0e-3);
    float sin2 = max(1.0 - nh * nh, 0.0);
    return (2.0 + inverse) * pow(sin2, inverse * 0.5) / (2.0 * BRDF_PI);
}
float brdf_sheen_v(float nv, float nl) { return 1.0 / max(4.0 * (nl + nv - nl * nv), 1.0e-4); }
vec3 brdf_direct(BrdfMaterial m, vec3 n, vec3 v, vec3 l, vec3 light)
{
    float nl_raw = dot(n, l);
    float nv = max(dot(n, v), 1.0e-4);
    vec3 h = normalize(v + l);
    float nh = clamp(dot(n, h), 0.0, 1.0);
    float vh = clamp(dot(v, h), 0.0, 1.0);
    float a = brdf_alpha(m.roughness);
    vec2 terms = brdf_dfg(nv, m.roughness);
    vec3 e = clamp((m.f0 * terms.x + terms.y) * brdf_compensation(m.f0, terms), 0.0, 1.0);
    float sheen_most = max(m.sheen.r, max(m.sheen.g, m.sheen.b));
    float base = 1.0 - clamp(sheen_most, 0.0, 1.0);
    float nl = clamp(nl_raw, 0.0, 1.0);
    float lobe = nl > 0.0 ? BRDF_PI * brdf_ggx(nh, a) * brdf_visibility(nv, nl, a) * nl : 0.0;
    float sheen_lobe = nl > 0.0 && sheen_most > 0.0 ? BRDF_PI * brdf_sheen_d(nh, m.sheen_roughness) * brdf_sheen_v(nv, nl) * nl : 0.0;
    vec3 cosine = vec3(nl);
    if (m.curvature_mm > 0.0 && max(m.scatter_mm.r, max(m.scatter_mm.g, m.scatter_mm.b)) > 0.0) {
        vec3 w = min(m.scatter_mm / m.curvature_mm, vec3(1.0));
        cosine = mix(vec3(nl), max(vec3(nl_raw) + w, vec3(0.0)) / ((1.0 + w) * (1.0 + w)), step(vec3(1.0e-6), m.scatter_mm));
    }
    vec3 diffuse = m.albedo * (1.0 - e) * cosine;
    vec3 specular = lobe * brdf_fresnel(m.f0, vh) * brdf_compensation(m.f0, terms);
    return ((diffuse + specular) * base + sheen_lobe * m.sheen) * light;
}
vec3 brdf_ambient(BrdfMaterial m, float nv, vec3 diffuse_light, vec3 specular_light)
{
    vec3 e = brdf_specular_albedo(m.f0, max(nv, 1.0e-4), m.roughness);
    float base = 1.0 - clamp(max(m.sheen.r, max(m.sheen.g, m.sheen.b)), 0.0, 1.0);
    return (m.albedo * (1.0 - e) * diffuse_light + e * specular_light) * base;
}
float brdf_hair_gaussian(float width, float x) { return exp(-0.5 * x * x / (width * width)) / (sqrt(2.0 * BRDF_PI) * width); }
vec3 brdf_hair_direct(vec3 colour, float roughness, vec3 t, vec3 v, vec3 l, vec3 light)
{
    float sin_l = clamp(dot(l, t), -1.0, 1.0);
    float sin_v = clamp(dot(v, t), -1.0, 1.0);
    float cos_d = cos(0.5 * abs(asin(sin_v) - asin(sin_l)));
    vec3 lp = l - sin_l * t;
    vec3 vp = v - sin_v * t;
    float cos_phi = dot(lp, vp) / sqrt(dot(lp, lp) * dot(vp, vp) + 1.0e-4);
    float cos_half_phi = sqrt(clamp(0.5 + 0.5 * cos_phi, 0.0, 1.0));
    float r = clamp(roughness, BRDF_LEAST_ROUGHNESS, 1.0);
    float r2 = r * r;
    float f0 = (BRDF_HAIR_INDEX - 1.0) * (BRDF_HAIR_INDEX - 1.0) / ((BRDF_HAIR_INDEX + 1.0) * (BRDF_HAIR_INDEX + 1.0));
    float sum_sin = sin_l + sin_v;
    float m_r = brdf_hair_gaussian(r2, sum_sin + 2.0 * BRDF_HAIR_TILT);
    float n_r = 0.25 * cos_half_phi * brdf_fresnel(f0, sqrt(clamp(0.5 + 0.5 * dot(l, v), 0.0, 1.0)));
    float m_tt = brdf_hair_gaussian(0.5 * r2, sum_sin - BRDF_HAIR_TILT);
    float index = 1.19 / cos_d + 0.36 * cos_d;
    float a = 1.0 / index;
    float h = (1.0 + a * (0.6 - 0.8 * cos_phi)) * cos_half_phi;
    float f_tt = brdf_fresnel(f0, cos_d * sqrt(clamp(1.0 - h * h, 0.0, 1.0)));
    float n_tt = exp(-3.65 * cos_phi - 3.98) * (1.0 - f_tt) * (1.0 - f_tt);
    float power_tt = 0.5 * sqrt(clamp(1.0 - h * a * h * a, 0.0, 1.0)) / cos_d;
    float m_trt = brdf_hair_gaussian(2.0 * r2, sum_sin - 3.0 * BRDF_HAIR_TILT);
    float f_trt = brdf_fresnel(f0, 0.5 * cos_d);
    float n_trt = exp(17.0 * cos_phi - 16.78) * (1.0 - f_trt) * (1.0 - f_trt) * f_trt;
    float power_trt = 0.8 / cos_d;
    vec3 c = max(colour, vec3(1.0e-4));
    vec3 s = vec3(m_r * n_r) + m_tt * n_tt * pow(c, vec3(power_tt)) + m_trt * n_trt * pow(c, vec3(power_trt));
    return BRDF_PI * s * light;
}
)";

// The GLSL to paste before first use: the DFG table as a constant array, then kBrdfGlsl.
[[nodiscard]] inline const std::string& brdf_glsl()
{
    static const std::string text = [] {
        std::string out = "const int BRDF_DFG_SIZE = " + std::to_string(kDfgSize) + ";\nconst float BRDF_DFG[" +
                          std::to_string(kDfgTable.size()) + "] = float[](";
        for (std::size_t i = 0; i < kDfgTable.size(); ++i) {
            std::array<char, 24> number{};
            std::snprintf(number.data(), number.size(), "%s%.5f", i == 0 ? "" : ",", static_cast<double>(core::at(kDfgTable, i)));
            out += number.data();
        }
        out += ");\n";
        out += kBrdfGlsl;
        return out;
    }();
    return text;
}

} // namespace render::rt::brdf
