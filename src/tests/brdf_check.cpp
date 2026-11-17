// NOT ORIGINAL - thug2_brdf_check.exe: round M's light model (engine/gfx/rt_brdf.h) and material table
// (engine/gfx/rt_material_table.h) checked on the processor - no game, no device:
//   - Fresnel: F0 face-on, 1 at grazing;
//   - the white furnace: a surface in an environment of radiance 1 returns at most 1 at every roughness and view angle
//     (the specular lobe of a white metal with its multiple-scattering compensation, a white dielectric's specular
//     plus its coupled diffuse, cloth's sheen over its base), integrated numerically (GGX importance sampling for the
//     lobe, a fine sphere grid for the broad ones);
//   - direct() is the sum of its parts at a few directions; skin's wrap keeps Lambert's total over a sphere of lights;
//   - the material table: kinds looked up from the surface maps' classes and the detail kinds, values recomputed from
//     their cited sources (an index's F0, Jensen et al.'s skin coefficients).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_brdf.h"
#include "engine/gfx/rt_material_table.h"
#include "tests/no_error_dialogs.h"

namespace {

namespace brdf = render::rt::brdf;
using render::rt::MaterialKind;

int g_failures = 0;
int g_passes = 0;

void expect(bool ok, const std::string& what)
{
    if (ok) {
        ++g_passes;
    } else {
        ++g_failures;
    }
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

std::string number(double value)
{
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%.4f", value);
    return text.data();
}

// The Hammersley point i of n (radical inverse in base 2).
std::array<float, 2> hammersley(uint32_t i, uint32_t n)
{
    uint32_t bits = i;
    bits = (bits << 16U) | (bits >> 16U);
    bits = ((bits & 0x55555555U) << 1U) | ((bits & 0xAAAAAAAAU) >> 1U);
    bits = ((bits & 0x33333333U) << 2U) | ((bits & 0xCCCCCCCCU) >> 2U);
    bits = ((bits & 0x0F0F0F0FU) << 4U) | ((bits & 0xF0F0F0F0U) >> 4U);
    bits = ((bits & 0x00FF00FFU) << 8U) | ((bits & 0xFF00FF00U) >> 8U);
    return {(static_cast<float>(i) + 0.5F) / static_cast<float>(n), static_cast<float>(bits) * 2.3283064365386963e-10F};
}

constexpr uint32_t kSamples = 1U << 16U;

// The compensated specular lobe's directional albedo for one channel's F0 (normal +z, the eye at angle acos(nv)),
// integrated by importance-sampling GGX's normals.
double lobe_albedo(float f0, float nv, float roughness)
{
    const float alpha = brdf::alpha_of(roughness);
    const brdf::Vec3 v{std::sqrt(std::max(1.0F - (nv * nv), 0.0F)), 0.0F, nv};
    const brdf::Dfg terms = brdf::dfg(nv, roughness);
    double sum = 0.0;
    for (uint32_t i = 0; i < kSamples; ++i) {
        const std::array<float, 2> u = hammersley(i, kSamples);
        const float phi = 2.0F * brdf::kPi * std::get<0>(u);
        const float cos_h = std::sqrt((1.0F - std::get<1>(u)) / (1.0F + (((alpha * alpha) - 1.0F) * std::get<1>(u))));
        const float sin_h = std::sqrt(std::max(1.0F - (cos_h * cos_h), 0.0F));
        const brdf::Vec3 h{sin_h * std::cos(phi), sin_h * std::sin(phi), cos_h};
        const float vh = brdf::dot(v, h);
        const brdf::Vec3 l{(2.0F * vh * h.x) - v.x, (2.0F * vh * h.y) - v.y, (2.0F * vh * h.z) - v.z};
        if (l.z <= 0.0F || vh <= 0.0F) {
            continue;
        }
        const float vis = brdf::smith_visibility(std::max(nv, brdf::kLeastCosine), l.z, alpha);
        sum += static_cast<double>(4.0F * vis * brdf::fresnel(f0, vh) * l.z * vh / cos_h);
    }
    return (sum / kSamples) * static_cast<double>(brdf::compensation(f0, terms));
}

// A broad lobe's directional albedo by a sphere grid: the radiance direct() returns per unit `light`, over the
// hemisphere of lights of radiance 1 (each cell's light = its solid angle / pi).
double grid_albedo(const brdf::Material& m, float nv, std::size_t channel)
{
    constexpr int kSteps = 512;
    const brdf::Vec3 n{0.0F, 0.0F, 1.0F};
    const brdf::Vec3 v{std::sqrt(std::max(1.0F - (nv * nv), 0.0F)), 0.0F, nv};
    double sum = 0.0;
    for (int a = 0; a < kSteps; ++a) {
        const float cos_t = (static_cast<float>(a) + 0.5F) / kSteps; // uniform in cos theta: equal solid angles
        const float sin_t = std::sqrt(1.0F - (cos_t * cos_t));
        for (int b = 0; b < kSteps; ++b) {
            const float phi = 2.0F * brdf::kPi * (static_cast<float>(b) + 0.5F) / kSteps;
            const brdf::Vec3 l{sin_t * std::cos(phi), sin_t * std::sin(phi), cos_t};
            const float cell = 2.0F * brdf::kPi / (kSteps * kSteps);
            const float light = cell / brdf::kPi;
            const brdf::Rgb out = brdf::direct(m, n, v, l, {light, light, light});
            sum += static_cast<double>(core::at(out, channel));
        }
    }
    return sum;
}

// The split-sum terms of the uncompensated lobe (F0 x a + b its directional albedo), integrated: the DFG table's source.
brdf::Dfg integrate_dfg(float nv, float roughness, uint32_t samples)
{
    const float alpha = brdf::alpha_of(roughness);
    const float cosine = std::max(nv, brdf::kLeastCosine);
    const brdf::Vec3 v{std::sqrt(std::max(1.0F - (cosine * cosine), 0.0F)), 0.0F, cosine};
    double a = 0.0;
    double b = 0.0;
    for (uint32_t i = 0; i < samples; ++i) {
        const std::array<float, 2> u = hammersley(i, samples);
        const float phi = 2.0F * brdf::kPi * std::get<0>(u);
        const float cos_h = std::sqrt((1.0F - std::get<1>(u)) / (1.0F + (((alpha * alpha) - 1.0F) * std::get<1>(u))));
        const float sin_h = std::sqrt(std::max(1.0F - (cos_h * cos_h), 0.0F));
        const brdf::Vec3 h{sin_h * std::cos(phi), sin_h * std::sin(phi), cos_h};
        const float vh = brdf::dot(v, h);
        const brdf::Vec3 l{(2.0F * vh * h.x) - v.x, (2.0F * vh * h.y) - v.y, (2.0F * vh * h.z) - v.z};
        if (l.z <= 0.0F || vh <= 0.0F) {
            continue;
        }
        const double g = static_cast<double>(4.0F * brdf::smith_visibility(cosine, l.z, alpha) * l.z * vh / cos_h);
        const double fc = std::pow(1.0 - static_cast<double>(vh), 5.0);
        a += g * (1.0 - fc);
        b += g * fc;
    }
    return {.a = static_cast<float>(a / samples), .b = static_cast<float>(b / samples)};
}

// `--table`: rt_brdf.h's kDfgTable printed (rows by roughness, columns by sqrt(N.V), each a then b).
void print_table()
{
    constexpr uint32_t kTableSamples = 1U << 20U;
    std::printf("inline constexpr std::array<float, kDfgSize * kDfgSize * 2> kDfgTable{{\n");
    for (std::size_t row = 0; row < brdf::kDfgSize; ++row) {
        std::printf("   ");
        for (std::size_t column = 0; column < brdf::kDfgSize; ++column) {
            const float roughness = static_cast<float>(row) / static_cast<float>(brdf::kDfgSize - 1);
            const float root = static_cast<float>(column) / static_cast<float>(brdf::kDfgSize - 1);
            const brdf::Dfg terms = integrate_dfg(root * root, roughness, kTableSamples);
            std::printf(" %.5fF, %.5fF,", static_cast<double>(terms.a), static_cast<double>(terms.b));
        }
        std::printf("\n");
    }
    std::printf("}};\n");
}

// The table read back against a fresh integration between its points.
void check_table_dfg()
{
    double worst = 0.0;
    for (const float r : {0.06F, 0.13F, 0.27F, 0.41F, 0.58F, 0.79F, 0.93F}) {
        for (const float nv : {0.03F, 0.11F, 0.23F, 0.47F, 0.66F, 0.89F}) {
            const brdf::Dfg read = brdf::dfg(nv, r);
            const brdf::Dfg truth = integrate_dfg(nv, r, kSamples);
            worst = std::max({worst, std::fabs(static_cast<double>(read.a - truth.a)), std::fabs(static_cast<double>(read.b - truth.b))});
        }
    }
    expect(worst < 0.02, "the DFG table between its points within 0.02 of the integral (16 x 16: the white furnace above is the measure) - " + number(worst));
}

void check_fresnel()
{
    expect(std::fabs(brdf::fresnel(0.04F, 1.0F) - 0.04F) < 1.0e-6F, "Fresnel face-on is F0 (0.04)");
    expect(std::fabs(brdf::fresnel(0.04F, 0.0F) - 1.0F) < 1.0e-6F, "Fresnel at grazing is 1 (dielectric)");
    expect(std::fabs(brdf::fresnel(0.56F, 0.0F) - 1.0F) < 1.0e-6F, "Fresnel at grazing is 1 (iron)");
    const brdf::Rgb e = brdf::specular_albedo({0.04F, 0.04F, 0.04F}, 0.02F, 0.045F);
    expect(std::get<0>(e) > 0.6F, "a smooth dielectric seen at grazing reflects most of its environment (" +
                                      number(std::get<0>(e)) + ")");
}

void check_furnace()
{
    const std::array<float, 8> roughness{0.045F, 0.1F, 0.2F, 0.35F, 0.5F, 0.7F, 0.85F, 1.0F};
    const std::array<float, 6> views{0.05F, 0.15F, 0.3F, 0.5F, 0.75F, 1.0F};
    double worst_metal_high = 0.0;
    double worst_metal_low = 1.0;
    double worst_dielectric = 0.0;
    double worst_dielectric_low = 1.0;
    double uncompensated_low = 1.0;
    for (const float r : roughness) {
        for (const float nv : views) {
            // a white metal (F0 1): the compensated lobe alone
            const double metal = lobe_albedo(1.0F, nv, r);
            worst_metal_high = std::max(worst_metal_high, metal);
            worst_metal_low = std::min(worst_metal_low, metal);
            uncompensated_low = std::min(uncompensated_low, metal / static_cast<double>(brdf::compensation(1.0F, brdf::dfg(nv, r))));
            // a white dielectric (albedo 1, F0 0.04): its lobe plus the coupled Lambert, which integrates to 1 - E
            const double lobe = lobe_albedo(0.04F, nv, r);
            const double diffuse = 1.0 - static_cast<double>(std::get<0>(brdf::specular_albedo({0.04F, 0.04F, 0.04F}, nv, r)));
            worst_dielectric = std::max(worst_dielectric, lobe + diffuse);
            worst_dielectric_low = std::min(worst_dielectric_low, lobe + diffuse);
        }
    }
    std::printf("     white metal, compensated: %.4f - %.4f (uncompensated down to %.4f); white dielectric: %.4f - %.4f\n",
                worst_metal_low, worst_metal_high, uncompensated_low, worst_dielectric_low, worst_dielectric);
    expect(worst_metal_high <= 1.03, "white furnace, metal: no lobe over 1 (+3% for the DFG fit) - " + number(worst_metal_high));
    expect(worst_metal_low >= 0.95, "white furnace, metal: the compensation keeps the rough lobe's energy - " +
                                        number(worst_metal_low) + " (without it " + number(uncompensated_low) + ")");
    expect(worst_dielectric <= 1.02, "white furnace, dielectric + diffuse at most 1 (+2%) - " + number(worst_dielectric));
    expect(worst_dielectric_low >= 0.97, "white furnace, dielectric + diffuse within 3% of 1 - " + number(worst_dielectric_low));
}

void check_direct_parts()
{
    brdf::Material m;
    m.albedo = {0.5F, 0.4F, 0.3F};
    m.f0 = {0.04F, 0.04F, 0.04F};
    m.roughness = 0.4F;
    const brdf::Vec3 n{0.0F, 0.0F, 1.0F};
    const brdf::Vec3 v = brdf::normalise({0.3F, 0.1F, 0.9F});
    const brdf::Vec3 l = brdf::normalise({-0.4F, 0.2F, 0.8F});
    const brdf::Rgb out = brdf::direct(m, n, v, l, {2.0F, 2.0F, 2.0F});
    const float nl = brdf::dot(n, l);
    const float nv = brdf::dot(n, v);
    const brdf::Vec3 h = brdf::normalise({v.x + l.x, v.y + l.y, v.z + l.z});
    const float alpha = brdf::alpha_of(m.roughness);
    const float spec = brdf::kPi * brdf::ggx_distribution(brdf::dot(n, h), alpha) * brdf::smith_visibility(nv, nl, alpha) *
                       brdf::fresnel(0.04F, brdf::dot(v, h)) * brdf::compensation(0.04F, brdf::dfg(nv, m.roughness)) * nl;
    const float e = std::get<0>(brdf::specular_albedo(m.f0, nv, m.roughness));
    const float red = 2.0F * ((0.5F * (1.0F - e) * nl) + spec);
    expect(std::fabs(std::get<0>(out) - red) < 1.0e-5F, "direct() = light x (albedo (1 - E) N.L + pi D V F comp N.L)");
    expect(std::get<0>(brdf::direct(m, n, v, {0.0F, 0.6F, -0.8F}, {1.0F, 1.0F, 1.0F})) == 0.0F, "no light from below the surface");
    // skin: the wrap keeps Lambert's total over a sphere of light directions
    constexpr int kSteps = 1024;
    double lambert = 0.0;
    double wrapped = 0.0;
    brdf::Material skin;
    skin.albedo = {1.0F, 1.0F, 1.0F};
    skin.f0 = {0.0F, 0.0F, 0.0F};
    skin.roughness = 1.0F;
    skin.scatter_mm = render::rt::kSkinScatterMm;
    skin.curvature_mm = 5.0F; // (red's w 0.73)
    brdf::Material plain = skin;
    plain.scatter_mm = {};
    for (int a = 0; a < kSteps; ++a) {
        const float z = -1.0F + (2.0F * (static_cast<float>(a) + 0.5F) / kSteps);
        const float r = std::sqrt(1.0F - (z * z));
        const brdf::Vec3 dir{r, 0.0F, z};
        lambert += static_cast<double>(std::get<0>(brdf::direct(plain, n, n, dir, {1.0F, 1.0F, 1.0F})));
        wrapped += static_cast<double>(std::get<0>(brdf::direct(skin, n, n, dir, {1.0F, 1.0F, 1.0F})));
    }
    expect(std::fabs((wrapped / lambert) - 1.0) < 0.01, "skin's wrap keeps Lambert's light over a sphere of lights - " +
                                                            number(wrapped / lambert));
}

void check_sheen()
{
    brdf::Material cloth;
    cloth.albedo = {1.0F, 1.0F, 1.0F};
    cloth.f0 = {0.04F, 0.04F, 0.04F};
    cloth.roughness = 0.88F;
    cloth.sheen = {0.5F, 0.5F, 0.5F}; // (far above a fibre's 0.04: the bound under test)
    cloth.sheen_roughness = 0.6F;
    double worst = 0.0;
    for (const float nv : {0.1F, 0.4F, 0.8F, 1.0F}) {
        worst = std::max(worst, grid_albedo(cloth, nv, 0));
    }
    expect(worst <= 1.0, "white furnace, cloth's sheen over its base at most 1 - " + number(worst));
}

void check_hair()
{
    // a white fibre (no absorption) lit from every direction of a sphere: what it sends toward the eye at most 1
    constexpr int kSteps = 400;
    const brdf::Vec3 t{0.0F, 1.0F, 0.0F};
    double worst = 0.0;
    for (const brdf::Vec3 v : {brdf::Vec3{0.0F, 0.0F, 1.0F}, brdf::normalise({0.0F, 0.5F, 0.8F}), brdf::normalise({0.6F, -0.3F, 0.7F})}) {
        for (const float roughness : {0.3F, 0.42F, 0.6F}) {
            double sum = 0.0;
            for (int a = 0; a < kSteps; ++a) {
                const float z = -1.0F + (2.0F * (static_cast<float>(a) + 0.5F) / kSteps);
                const float r = std::sqrt(1.0F - (z * z));
                for (int b = 0; b < kSteps; ++b) {
                    const float phi = 2.0F * brdf::kPi * (static_cast<float>(b) + 0.5F) / kSteps;
                    const brdf::Vec3 l{r * std::cos(phi), r * std::sin(phi), z};
                    const float light = (4.0F * brdf::kPi / (kSteps * kSteps)) / brdf::kPi;
                    sum += static_cast<double>(std::get<0>(brdf::hair_direct({1.0F, 1.0F, 1.0F}, roughness, t, v, l, {light, light, light})));
                }
            }
            worst = std::max(worst, sum);
        }
    }
    expect(worst <= 1.005, "a white hair fibre in a white furnace returns at most 1 (+0.5%: the grid integral) - " + number(worst));
}

void check_table()
{
    using render::rt::kind_of_detail;
    using render::rt::kind_of_map_class;
    using render::rt::material_f0;
    using render::rt::material_values;
    expect(kind_of_map_class(4) == MaterialKind::kMetal && std::fabs(std::get<0>(material_f0(MaterialKind::kMetal)) - 0.56F) < 1e-6F,
           "a metal-classed map is iron, F0 0.56 (Hoffman 2013)");
    expect(std::fabs(std::get<0>(material_f0(MaterialKind::kWater)) - 0.0204F) < 2e-4F, "water's F0 from n 1.333: 0.0204");
    expect(std::fabs(std::get<0>(material_f0(MaterialKind::kGlass)) - 0.0426F) < 2e-4F, "glass's F0 from n 1.52: 0.0426");
    expect(std::fabs(std::get<0>(material_f0(MaterialKind::kHair)) - 0.0465F) < 2e-4F, "keratin's F0 from n 1.55: 0.0465");
    // Jensen et al. 2001 skin1: 1 / sqrt(3 sigma_a (sigma_a + sigma_s')) per channel
    constexpr std::array<float, 3> kScatter{0.74F, 0.88F, 1.01F};
    constexpr std::array<float, 3> kAbsorb{0.032F, 0.17F, 0.48F};
    bool skin = kind_of_map_class(12) == MaterialKind::kSkin;
    for (std::size_t ch = 0; ch < 3; ++ch) {
        const float a = core::at(kAbsorb, ch);
        const float path = 1.0F / std::sqrt(3.0F * a * (a + core::at(kScatter, ch)));
        skin = skin && std::fabs(path - core::at(material_values(MaterialKind::kSkin).scatter_mm, ch)) < 0.01F;
    }
    expect(skin, "skin's mean free path from Jensen et al.'s skin1 coefficients (3.67, 1.37, 0.68 mm)");
    expect(kind_of_detail(4) == MaterialKind::kBrick && material_values(MaterialKind::kBrick).relief_mm == 6.0F,
           "a brick detail kind is brick: its joint 6 mm deep (BIA TN 7B's 1/4 in rake)");
    expect(kind_of_map_class(200) == MaterialKind::kPainted, "an unknown class is painted");
    bool sources = true;
    for (const render::rt::MaterialValues& values : render::rt::kMaterialTable) {
        sources = sources && !values.source.empty() && !values.name.empty() && values.roughness >= 0.0F && values.roughness <= 1.0F;
    }
    expect(sources, "every kind names its source and has a roughness in 0 - 1");
    expect(material_values(MaterialKind::kGlass).light_transmission > 0.8F && material_values(MaterialKind::kGlass).air_transmission == 0.0F,
           "glass lets light through, not air");
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    if (argc > 1 && std::string(core::at(std::span<char*>(argv, static_cast<std::size_t>(argc)), 1)) == "--table") {
        print_table();
        return 0;
    }
    check_table_dfg();
    check_fresnel();
    check_furnace();
    check_direct_parts();
    check_sheen();
    check_hair();
    check_table();
    std::printf("%d passed, %d failed\n", g_passes, g_failures);
    return g_failures == 0 ? 0 : 1;
}
