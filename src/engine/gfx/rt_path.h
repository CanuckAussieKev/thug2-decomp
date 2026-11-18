#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_brdf.h"
#include "engine/gfx/rt_lights.h"

// NOT ORIGINAL - the path tracer's sampling (path tracing plan, stage 1: the reference path tracer - gpu_vulkan/
// vulkan_path_reference.h runs it). What a path needs beyond the light model (rt_brdf.h): where to send its next ray and
// with what probability, and the pick of a light - each a published method, no tuned value:
//   - the light model's BSDF importance sampled: a mixture of its two lobes, picked by their directional albedos toward
//     the eye (specular_probability) - the specular by GGX's distribution of visible normals (Heitz 2018, "Sampling the
//     GGX Distribution of Visible Normals", JCGT 7(4)), its pdf G1(v) D(h) / (4 N.V); the diffuse by the cosine
//     (Malley's method, pdf N.L / pi); the mixture's pdf the two weighted (one-sample MIS between the lobes, Veach 1997);
//   - a light's direction: a cone of directions uniformly (the sun's disc: its real solid angle 2 pi (1 - cos r)), a
//     triangle uniformly by area (Turk 1990's square-root map), the sky by the cosine about the normal;
//   - multiple importance sampling: the power heuristic (Veach & Guibas 1995, beta 2);
//   - random numbers: a PCG hash (Jarzynski & Olano 2020, "Hash Functions for GPU Rendering", pcg) run as a sequence.
// Units: the light model's (rt_brdf.h) - radiance in the global unit (rt_sky.h: 1 a white diffuser under the reference
// day). bsdf_cosine(m, n, v, l) is f(v, l) x N.L, so a light of radiance L over a solid angle dw sends f L N.L dw:
// brdf::direct(m, n, v, l, light) / pi with light 1. The GLSL twin (kPathGlsl, after rt_brdf.h's brdf_glsl()) is kept
// in step with this file: thug2_vulkan_path_check compares them.
namespace render::rt::path {

using brdf::Material;
using brdf::Rgb;
using brdf::Vec3;

inline constexpr float kPi = brdf::kPi;
inline constexpr float kTwoPi = 2.0F * brdf::kPi;
inline constexpr float kInversePi = 1.0F / brdf::kPi;
inline constexpr uint32_t kMostBounces = 32;     // a path's bounces at most (Russian roulette ends most far sooner)
inline constexpr uint32_t kRouletteFrom = 3;      // the bounce Russian roulette starts at
inline constexpr float kRouletteLeast = 0.05F;    // the least survival probability (a path's weight at most x 20)
inline constexpr float kRouletteMost = 0.95F;

// --- random numbers -------------------------------------------------------------------------------------------------
[[nodiscard]] inline uint32_t pcg(uint32_t value)
{
    const uint32_t state = (value * 747796405U) + 2891336453U;
    const uint32_t word = ((state >> ((state >> 28U) + 4U)) ^ state) * 277803737U;
    return (word >> 22U) ^ word;
}
// A sequence's start for a pixel, a sample and a seed.
[[nodiscard]] inline uint32_t sequence_start(uint32_t x, uint32_t y, uint32_t sample, uint32_t seed)
{
    return pcg(pcg(pcg(x + pcg(y)) ^ sample) + seed);
}
// The next number in [0, 1) (24 bits).
[[nodiscard]] inline float next_number(uint32_t* state)
{
    *state = pcg(*state);
    return static_cast<float>(*state >> 8U) * (1.0F / 16777216.0F);
}

// --- vectors ---------------------------------------------------------------------------------------------------------
[[nodiscard]] inline Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
[[nodiscard]] inline Vec3 scaled(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
[[nodiscard]] inline Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {(a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x)};
}
// An orthonormal basis about a unit `n` (Duff et al. 2017, "Building an Orthonormal Basis, Revisited"): tangent,
// bitangent.
struct Basis {
    Vec3 t;
    Vec3 b;
    Vec3 n;
};
[[nodiscard]] inline Basis basis_of(const Vec3& n)
{
    const float sign = n.z >= 0.0F ? 1.0F : -1.0F;
    const float a = -1.0F / (sign + n.z);
    const float b = n.x * n.y * a;
    return {.t = {1.0F + (sign * n.x * n.x * a), sign * b, -sign * n.x}, .b = {b, sign + (n.y * n.y * a), -n.y}, .n = n};
}
[[nodiscard]] inline Vec3 to_world(const Basis& basis, const Vec3& local)
{
    return add(add(scaled(basis.t, local.x), scaled(basis.b, local.y)), scaled(basis.n, local.z));
}
[[nodiscard]] inline Vec3 to_local(const Basis& basis, const Vec3& world)
{
    return {brdf::dot(world, basis.t), brdf::dot(world, basis.b), brdf::dot(world, basis.n)};
}

// --- directions ------------------------------------------------------------------------------------------------------
// The cosine-weighted hemisphere about +z (pdf z / pi).
[[nodiscard]] inline Vec3 cosine_direction(float u1, float u2)
{
    const float r = std::sqrt(u1);
    const float phi = kTwoPi * u2;
    return {r * std::cos(phi), r * std::sin(phi), std::sqrt(std::max(0.0F, 1.0F - u1))};
}
// A cone about +z of half angle acos(cos_most), uniformly (pdf 1 / cone_solid_angle).
[[nodiscard]] inline float cone_solid_angle(float cos_most) { return kTwoPi * (1.0F - cos_most); }
[[nodiscard]] inline Vec3 cone_direction(float cos_most, float u1, float u2)
{
    const float cos_theta = 1.0F - (u1 * (1.0F - cos_most));
    const float sin_theta = std::sqrt(std::max(0.0F, 1.0F - (cos_theta * cos_theta)));
    const float phi = kTwoPi * u2;
    return {sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta};
}
// A point of a triangle uniformly by area: its barycentrics (weights of corners 0, 1, 2).
[[nodiscard]] inline std::array<float, 3> triangle_point(float u1, float u2)
{
    const float root = std::sqrt(u1);
    return {1.0F - root, root * (1.0F - u2), root * u2};
}
// The power heuristic (beta 2): the weight of a sample of pdf `a` against another strategy's `b`.
[[nodiscard]] inline float power_heuristic(float a, float b)
{
    const float a2 = a * a;
    const float sum = a2 + (b * b);
    return sum > 0.0F ? a2 / sum : 0.0F;
}

// --- the BSDF --------------------------------------------------------------------------------------------------------
// Smith's G1 for GGX (the masking of the eye's direction).
[[nodiscard]] inline float ggx_g1(float n_dot_v, float alpha)
{
    const float a2 = alpha * alpha;
    return 2.0F * n_dot_v / std::max(n_dot_v + std::sqrt(a2 + ((1.0F - a2) * n_dot_v * n_dot_v)), 1.0e-7F);
}
// A visible normal of GGX (alpha) seen from `v` (local, z the normal) - Heitz 2018's sampling.
[[nodiscard]] inline Vec3 ggx_visible_normal(const Vec3& v, float alpha, float u1, float u2)
{
    const Vec3 vh = brdf::normalise({alpha * v.x, alpha * v.y, v.z});
    const float length2 = (vh.x * vh.x) + (vh.y * vh.y);
    const Vec3 t1 = length2 > 0.0F ? scaled(Vec3{-vh.y, vh.x, 0.0F}, 1.0F / std::sqrt(length2)) : Vec3{1.0F, 0.0F, 0.0F};
    const Vec3 t2 = cross(vh, t1);
    const float r = std::sqrt(u1);
    const float phi = kTwoPi * u2;
    const float p1 = r * std::cos(phi);
    const float s = 0.5F * (1.0F + vh.z);
    const float p2 = ((1.0F - s) * std::sqrt(std::max(0.0F, 1.0F - (p1 * p1)))) + (s * r * std::sin(phi));
    const float p3 = std::sqrt(std::max(0.0F, 1.0F - (p1 * p1) - (p2 * p2)));
    const Vec3 nh = add(add(scaled(t1, p1), scaled(t2, p2)), scaled(vh, p3));
    return brdf::normalise({alpha * nh.x, alpha * nh.y, std::max(0.0F, nh.z)});
}
// The specular lobe's pdf of `l` (by its half vector): G1(v) D(h) / (4 N.V).
[[nodiscard]] inline float ggx_reflection_pdf(float n_dot_v, float n_dot_h, float alpha)
{
    return ggx_g1(n_dot_v, alpha) * brdf::ggx_distribution(n_dot_h, alpha) / (4.0F * std::max(n_dot_v, brdf::kLeastCosine));
}
[[nodiscard]] inline float luma(const Rgb& c)
{
    return (0.2126F * std::get<0>(c)) + (0.7152F * std::get<1>(c)) + (0.0722F * std::get<2>(c));
}
// The probability of picking the specular lobe toward the eye: its directional albedo over the two lobes' (the light
// model's: E and albedo x (1 - E)); a half when neither reflects.
[[nodiscard]] inline float specular_probability(const Material& m, float n_dot_v)
{
    const Rgb e = brdf::specular_albedo(m.f0, std::max(n_dot_v, brdf::kLeastCosine), m.roughness);
    const float specular = luma(e);
    const float diffuse = luma({std::get<0>(m.albedo) * (1.0F - std::get<0>(e)), std::get<1>(m.albedo) * (1.0F - std::get<1>(e)),
                                std::get<2>(m.albedo) * (1.0F - std::get<2>(e))});
    const float sum = specular + diffuse;
    return sum > 0.0F ? specular / sum : 0.5F;
}
// f(v, l) x N.L (each channel; 0 below the surface).
[[nodiscard]] inline Rgb bsdf_cosine(const Material& m, const Vec3& n, const Vec3& v, const Vec3& l)
{
    if (brdf::dot(n, l) <= 0.0F) {
        return {};
    }
    const Rgb out = brdf::direct(m, n, v, l, {1.0F, 1.0F, 1.0F});
    return {std::get<0>(out) * kInversePi, std::get<1>(out) * kInversePi, std::get<2>(out) * kInversePi};
}
// The mixture's pdf of `l` (solid angle; 0 below the surface).
[[nodiscard]] inline float bsdf_pdf(const Material& m, const Vec3& n, const Vec3& v, const Vec3& l)
{
    const float n_dot_l = brdf::dot(n, l);
    const float n_dot_v = brdf::dot(n, v);
    if (n_dot_l <= 0.0F || n_dot_v <= 0.0F) {
        return 0.0F;
    }
    const float p = specular_probability(m, n_dot_v);
    const Vec3 h = brdf::normalise(add(v, l));
    const float specular = ggx_reflection_pdf(n_dot_v, brdf::saturate(brdf::dot(n, h)), brdf::alpha_of(m.roughness));
    return (p * specular) + ((1.0F - p) * n_dot_l * kInversePi);
}
// A direction from the mixture: `u0` picks the lobe, (u1, u2) the direction. `valid` false: below the surface (the
// path ends - its weight 0).
struct BsdfSample {
    Vec3 l;
    float pdf = 0.0F;
    bool valid = false;
};
[[nodiscard]] inline BsdfSample sample_bsdf(const Material& m, const Vec3& n, const Vec3& v, float u0, float u1, float u2)
{
    const float n_dot_v = brdf::dot(n, v);
    if (n_dot_v <= 0.0F) {
        return {};
    }
    const Basis frame = basis_of(n);
    Vec3 l;
    if (u0 < specular_probability(m, n_dot_v)) {
        const Vec3 h = to_world(frame, ggx_visible_normal(to_local(frame, v), brdf::alpha_of(m.roughness), u1, u2));
        const float v_dot_h = brdf::dot(v, h);
        l = add(scaled(h, 2.0F * v_dot_h), scaled(v, -1.0F));
    } else {
        l = to_world(frame, cosine_direction(u1, u2));
    }
    const float pdf = bsdf_pdf(m, n, v, l);
    return {.l = l, .pdf = pdf, .valid = pdf > 0.0F && std::isfinite(pdf)};
}

// The radiance of a light's disc of angular radius `cos_most` (its cosine) giving `light` per unit of N.L (the light
// model's light: a white diffuser facing it shows `light`): pi x light / its solid angle.
[[nodiscard]] inline Rgb disc_radiance(const Rgb& light, float cos_most)
{
    const float omega = cone_solid_angle(cos_most);
    const float scale = omega > 0.0F ? kPi / omega : 0.0F;
    return {std::get<0>(light) * scale, std::get<1>(light) * scale, std::get<2>(light) * scale};
}

// --- one owner per lamp ----------------------------------------------------------------------------------------------
// A lamp with emitting geometry is sampled as that geometry (its triangles carry its radiance - the light store's emit);
// its analytic shape is the path tracer's light only where no geometry exists. `surfaced`: per emitter of `lamps`
// (LevelEmitters::surfaced), 1 when it has an emitting surface; shorter than `lamps`: the rest have none. The lamps kept
// (in their order) - NEE then never adds a lamp twice (its surface hit by a ray and its analytic light sampled).
[[nodiscard]] inline std::vector<Emitter> analytic_lamps(std::span<const Emitter> lamps, std::span<const uint8_t> surfaced)
{
    std::vector<Emitter> out;
    std::size_t i = 0;
    for (const Emitter& lamp : lamps) {
        const bool has_surface = i < surfaced.size() && core::at(surfaced, i) != 0;
        ++i;
        if (!has_surface) {
            out.push_back(lamp);
        }
    }
    return out;
}

// --- the reference's request (the console's `pt`: API-neutral, the backend reads it) ------------------------------------
// `pt ref <samples> [ev100]`: with the game paused, the backend's reference integrator accumulates `samples` paths a
// pixel while the camera and the scene stay as they are, shows the result in the frame at a fixed manual exposure (the
// camera's EV100: the given one, else the metered one when asked), then saves the reference and the realtime frame -
// each as a linear EXR (the global unit) and a PNG through the same tone curve at that EV.
struct PathReferenceRequest {
    uint32_t samples = 0;          // 0: none (`pt off`)
    bool ev_given = false;
    float ev100 = 0.0F;
    uint32_t bounces = kMostBounces;
    // (stage 2) `pt ref <samples> [EV100] even`: the light list picks the kinds of light evenly and each kind's lights
    // evenly (stage 1's sampling - rt_light_list.h LightWeights::kKindsEvenly) instead of by power: a comparison
    bool even_lights = false;
    // (the night's light, decomposed) `pt ref <samples> ... only <kinds>`: only these kinds of light are counted - their
    // next-event samples and what a path's BSDF rays meet of them (the others still picked and sampled, counting 0, so
    // the sums of runs of each kind alone are the whole); the paths, their bounces and the surfaces are unchanged. A
    // diagnostic: which light a pixel's radiance is made of. kLightKind* bits; kAllLightKinds the reference.
    uint32_t light_kinds = 0xFU;
    // `pt ref <samples> ... analytic`: every lamp is its analytic light (rt_lights.h's model, as the lamps pass shines
    // it) and no surface's emission is counted - the other owner of a lamp's light, for comparison (one owner still)
    bool analytic_lamps = false;
    // `pt ref <samples> ... open`: the light's rays (next-event estimation) pass the blended surfaces as the realtime
    // tier's rays do (vulkan_rt_scene.h's masks: the seen-through instances are no caster) - for comparison; the paths
    // still meet them
    bool blended_open = false;
    uint64_t serial = 0;         // (set_path_reference numbers each request)
};
inline constexpr uint32_t kLightKindDisc = 1U;     // the sun's or the moon's disc
inline constexpr uint32_t kLightKindSky = 2U;      // the sky (its grid)
inline constexpr uint32_t kLightKindEmitters = 4U; // emitting surfaces (the light store's emission, listed or not)
inline constexpr uint32_t kLightKindLamps = 8U;    // the analytic lamps
inline constexpr uint32_t kAllLightKinds = 0xFU;
// `only`'s words (disc / sun / moon, sky, emit / emitters, lamps) as kLightKind* bits, comma-separated; 0: a word
// isn't one.
[[nodiscard]] uint32_t light_kinds_of(const std::string& words);
// The kinds as words ("all" for kAllLightKinds).
[[nodiscard]] std::string light_kinds_text(uint32_t kinds);
void request_path_reference(PathReferenceRequest request);
[[nodiscard]] PathReferenceRequest path_reference_request();
// What the backend reports back (the console's `pt` prints it).
struct PathReferenceStatus {
    uint64_t serial = 0;    // the request's
    bool running = false;
    uint32_t samples = 0;   // accumulated so far
    uint32_t wanted = 0;
    float ev100 = 0.0F;
    std::string saved;      // the captures' stem when saved ("" before)
    std::string note;       // why it isn't running, or the last restart's reason
};
void set_path_reference_status(PathReferenceStatus status);
[[nodiscard]] PathReferenceStatus path_reference_status();
// The console's `pt` command: its output.
[[nodiscard]] std::string path_console_command(const std::string& arguments);

// The GLSL twin (names path_*; after rt_brdf.h's brdf_glsl()).
inline constexpr const char* kPathGlsl = R"(const float PATH_PI = 3.14159265358979;
const float PATH_TWO_PI = 6.28318530717959;
uint path_pcg(uint value)
{
    uint state = value * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
uint path_sequence_start(uint x, uint y, uint sample_index, uint seed)
{
    return path_pcg(path_pcg(path_pcg(x + path_pcg(y)) ^ sample_index) + seed);
}
float path_next(inout uint state)
{
    state = path_pcg(state);
    return float(state >> 8) * (1.0 / 16777216.0);
}
void path_basis(vec3 n, out vec3 t, out vec3 b)
{
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float c = n.x * n.y * a;
    t = vec3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = vec3(c, s + n.y * n.y * a, -n.y);
}
vec3 path_to_world(vec3 n, vec3 local)
{
    vec3 t, b;
    path_basis(n, t, b);
    return t * local.x + b * local.y + n * local.z;
}
vec3 path_to_local(vec3 n, vec3 world)
{
    vec3 t, b;
    path_basis(n, t, b);
    return vec3(dot(world, t), dot(world, b), dot(world, n));
}
vec3 path_cosine_direction(float u1, float u2)
{
    float r = sqrt(u1);
    float phi = PATH_TWO_PI * u2;
    return vec3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u1)));
}
float path_cone_solid_angle(float cos_most) { return PATH_TWO_PI * (1.0 - cos_most); }
vec3 path_cone_direction(float cos_most, float u1, float u2)
{
    float c = 1.0 - u1 * (1.0 - cos_most);
    float s = sqrt(max(0.0, 1.0 - c * c));
    float phi = PATH_TWO_PI * u2;
    return vec3(s * cos(phi), s * sin(phi), c);
}
vec3 path_triangle_point(float u1, float u2)
{
    float root = sqrt(u1);
    return vec3(1.0 - root, root * (1.0 - u2), root * u2);
}
float path_power_heuristic(float a, float b)
{
    float a2 = a * a;
    float sum = a2 + b * b;
    return sum > 0.0 ? a2 / sum : 0.0;
}
float path_ggx_g1(float nv, float a)
{
    float a2 = a * a;
    return 2.0 * nv / max(nv + sqrt(a2 + (1.0 - a2) * nv * nv), 1.0e-7);
}
vec3 path_ggx_visible_normal(vec3 v, float a, float u1, float u2)
{
    vec3 vh = normalize(vec3(a * v.x, a * v.y, v.z));
    float length2 = vh.x * vh.x + vh.y * vh.y;
    vec3 t1 = length2 > 0.0 ? vec3(-vh.y, vh.x, 0.0) / sqrt(length2) : vec3(1.0, 0.0, 0.0);
    vec3 t2 = cross(vh, t1);
    float r = sqrt(u1);
    float phi = PATH_TWO_PI * u2;
    float p1 = r * cos(phi);
    float s = 0.5 * (1.0 + vh.z);
    float p2 = (1.0 - s) * sqrt(max(0.0, 1.0 - p1 * p1)) + s * r * sin(phi);
    float p3 = sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2));
    vec3 nh = t1 * p1 + t2 * p2 + vh * p3;
    return normalize(vec3(a * nh.x, a * nh.y, max(0.0, nh.z)));
}
float path_ggx_reflection_pdf(float nv, float nh, float a)
{
    return path_ggx_g1(nv, a) * brdf_ggx(nh, a) / (4.0 * max(nv, 1.0e-4));
}
float path_luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
float path_specular_probability(BrdfMaterial m, float nv)
{
    vec3 e = brdf_specular_albedo(m.f0, max(nv, 1.0e-4), m.roughness);
    float specular = path_luma(e);
    float diffuse = path_luma(m.albedo * (1.0 - e));
    float sum = specular + diffuse;
    return sum > 0.0 ? specular / sum : 0.5;
}
vec3 path_bsdf_cosine(BrdfMaterial m, vec3 n, vec3 v, vec3 l)
{
    if (dot(n, l) <= 0.0) {
        return vec3(0.0);
    }
    return brdf_direct(m, n, v, l, vec3(1.0)) / PATH_PI;
}
float path_bsdf_pdf(BrdfMaterial m, vec3 n, vec3 v, vec3 l)
{
    float nl = dot(n, l);
    float nv = dot(n, v);
    if (nl <= 0.0 || nv <= 0.0) {
        return 0.0;
    }
    float p = path_specular_probability(m, nv);
    vec3 h = normalize(v + l);
    float specular = path_ggx_reflection_pdf(nv, clamp(dot(n, h), 0.0, 1.0), brdf_alpha(m.roughness));
    return p * specular + (1.0 - p) * nl / PATH_PI;
}
// false: below the surface (the path ends)
bool path_sample_bsdf(BrdfMaterial m, vec3 n, vec3 v, float u0, float u1, float u2, out vec3 l, out float pdf)
{
    l = n;
    pdf = 0.0;
    float nv = dot(n, v);
    if (nv <= 0.0) {
        return false;
    }
    if (u0 < path_specular_probability(m, nv)) {
        vec3 h = path_to_world(n, path_ggx_visible_normal(path_to_local(n, v), brdf_alpha(m.roughness), u1, u2));
        l = 2.0 * dot(v, h) * h - v;
    } else {
        l = path_to_world(n, path_cosine_direction(u1, u2));
    }
    pdf = path_bsdf_pdf(m, n, v, l);
    return pdf > 0.0 && !isinf(pdf) && !isnan(pdf);
}
vec3 path_disc_radiance(vec3 light, float cos_most)
{
    float omega = path_cone_solid_angle(cos_most);
    return omega > 0.0 ? light * (PATH_PI / omega) : vec3(0.0);
}
)";

} // namespace render::rt::path
