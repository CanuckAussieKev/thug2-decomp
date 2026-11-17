// NOT ORIGINAL - thug2_vulkan_rain_check.exe: the rain (engine/gfx/rain.h - the weather's one state; engine/gfx/
// precip_occlusion.h - where it reaches; gpu_vulkan/vulkan_rain.h, vulkan_precip.h - its passes) tried on this machine.
//
// On the processor (no device needed):
//   - the drops: Marshall-Palmer's sizes (N(D) at its formula; the number from the closed form against the integral; the
//     sizes the particles take - the distribution's inverse - back to their shares), Atlas's fall speeds against Gunn and
//     Kinzer's measurements (within 3%), the rate the drops carry back to the rate (Marshall-Palmer with Atlas's speeds:
//     within 20% from 1 to 40 mm/h - the fit's own: about 15% more than the rate it was fitted at), the air's extinction against the untruncated closed form pi N0 / Lambda^3, the drops
//     striking a square metre a second;
//   - the wind: the log profile (5 m/s at 10 m is 2.3 m/s at 2 m over a town), the fall's slant against the median drop;
//     the cloud deck the rain implies (overcast);
//   - the water on the ground: a level entered while it rains is wet already, a dry one fills its pores in the minutes
//     the rate takes and dries at kEvaporation after; Nusselt's film thinner on steeper slopes and the stored water on
//     level ground; puddles - none dry, more as the water rises, their mean depth over the relief the stored water;
//   - the wet material: the water's hemispherical reflectances (from the air ~6.6%, from inside by reciprocity
//     1 - (1 - Re) / n^2), Lekner and Dorf's darkening never brighter than dry and stronger for darker albedos (the wet
//     albedo against a power of the dry), the energy of the wet surface at most 1 (exactly 1 for a white one), a puddle's
//     roughness and F0 the water's, a dry non-porous surface untouched; the rings' cells give back the drops' rate;
//   - where it reaches (the processor's reference over triangles): under a roof none, beside it all; with the wind the
//     roof's shadow moved downwind and the ground under its upwind edge wet; a wall's lee in its rain shadow, its windward
//     side wet;
//   - the drops' places (vk::drop_place, the compute shader's twin): inside their boxes, the far ones inside the near box
//     not drawn, their sizes' mean the distribution's.
// On the device with ray tracing (a ground, a roof and a wall in the scene's TLAS):
//   - the precipitation occlusion map traced along a vertical fall and a slanted one and read back: its texels' heights
//     the roof's over the ground under it, the ground's beside it, the wall's in its lee - and reached as the reference
//     says at every test point;
//   - every rain pass's shaders and pipelines made (drops, splashes, their motion, the wet mask);
//   - the validation layer says nothing.
//   thug2_vulkan_rain_check [--device N] [--display N] [--no-validation]
// Exit code: 0 when every check passed, 1 when one failed, 2 when this machine has no Vulkan for this process.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/precip_occlusion.h"
#include "engine/gfx/rain.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_precip.h"
#include "gpu_vulkan/vulkan_rain.h"
#include "tests/check_window.h"
#include "tests/no_error_dialogs.h"

namespace {

namespace value = render::gpu_value;
namespace vk = render::vulkan;
namespace rain = render::rain;
namespace precip = render::precip;
namespace rt = render::rt;
using Vec3 = std::array<float, 3>;

constexpr int kWindowSize = 128;
constexpr int kBitsPerByte = 8;
constexpr uint32_t kFvf = 0x142; // XYZ | DIFFUSE | TEX1: 24 bytes
constexpr uint32_t kStride = 24;

struct Options {
    int device = -1;
    int display = 0;
    bool validation = true;
};

Options read_options(std::span<char*> arguments)
{
    Options options;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string_view argument = core::at(arguments, i);
        const bool has_value = i + 1 < arguments.size();
        if (argument == "--no-validation") {
            options.validation = false;
        } else if (argument == "--device" && has_value) {
            options.device = text::to_int(core::at(arguments, ++i));
        } else if (argument == "--display" && has_value) {
            options.display = text::to_int(core::at(arguments, ++i));
        }
    }
    return options;
}

int& validation_messages()
{
    static int count = 0;
    return count;
}

void print_line(const std::string& line)
{
    if (line.starts_with("validation ")) {
        ++validation_messages();
    }
    text::print("  %s\n", line.c_str());
}

struct Tally {
    int passed = 0;
    int failed = 0;
    void check(bool ok, const std::string& what)
    {
        if (ok) {
            ++passed;
        } else {
            ++failed;
        }
        text::print("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    }
};

bool near_share(double a, double b, double share) { return std::abs(a - b) <= share * std::abs(b); }

// --- the drops ---------------------------------------------------------------------------------------------------------

void check_drops(Tally* tally)
{
    text::print("the drops\n");
    // Marshall-Palmer at 1 mm/h: Lambda 4.1, N(1 mm) = 8000 e^-4.1
    tally->check(
        near_share(rain::size_slope(1.0), 4.1, 1e-9) &&
            near_share(rain::size_density(1.0, 1.0), 8000.0 * std::exp(-4.1), 1e-9) &&
            rain::size_density(0.0, 1.0) == 0.0 && rain::size_density(4.0, 7.0) == 0.0,
        text::format(
            "Marshall-Palmer: Lambda(1 mm/h) %.3f mm^-1, N(1 mm) %.1f m^-3 mm^-1; none without rain or past %.0f mm",
            rain::size_slope(1.0), rain::size_density(1.0, 1.0), rain::kMostDiameter));
    // the number: the closed form against Simpson's rule
    double integral = 0.0;
    constexpr int kSteps = 20000;
    const double h = (rain::kMostDiameter - rain::kLeastDiameter) / kSteps;
    for (int i = 0; i < kSteps; ++i) {
        integral += rain::size_density(4.0, rain::kLeastDiameter + ((i + 0.5) * h)) * h;
    }
    tally->check(near_share(rain::number_density(4.0), integral, 1e-4),
                 text::format("drops a cubic metre at 4 mm/h: %.1f (the integral %.1f); %.1f of 0.5 mm and over",
                              rain::number_density(4.0), integral, rain::number_density_over(4.0, 0.5)));
    // the sizes the particles take: the inverse's shares back
    bool inverse = true;
    for (const double u : {0.01, 0.25, 0.5, 0.75, 0.99}) {
        const double d = rain::diameter_at(4.0, u);
        const double share = 1.0 - (rain::number_density_over(4.0, d) / rain::number_density(4.0));
        inverse = inverse && std::abs(share - u) < 1e-6;
    }
    tally->check(inverse,
                 text::format("the sizes' inverse: share 0.5 is %.3f mm, 0.99 is %.3f mm (back to their shares)",
                              rain::diameter_at(4.0, 0.5), rain::diameter_at(4.0, 0.99)));
    // Gunn and Kinzer (1949) measured: 0.5 mm 2.06 m/s, 1 mm 4.03, 2 mm 6.49, 3 mm 8.06, 4 mm 8.83, 5 mm 9.09
    const std::array<std::array<double, 2>, 6> measured{
        {{0.5, 2.06}, {1.0, 4.03}, {2.0, 6.49}, {3.0, 8.06}, {4.0, 8.83}, {5.0, 9.09}}};
    double worst = 0.0;
    for (const auto& [d, v] : measured) {
        worst = std::max(worst, std::abs(rain::fall_speed(d) - v) / v);
    }
    tally->check(worst < 0.03,
                 text::format("fall speeds against Gunn and Kinzer's: at most %.1f%% apart (2 mm: %.2f m/s)",
                              worst * 100.0, rain::fall_speed(2.0)));
    // the rate back
    std::string rates;
    bool back = true;
    for (const double rate : {1.0, 4.0, 12.0, 40.0}) {
        const double carried = rain::rate_from_drops(rate);
        back = back && near_share(carried, rate, 0.2);
        rates += text::format(" %.0f -> %.2f", rate, carried);
    }
    tally->check(back, "the rate the drops carry back (mm/h):" + rates);
    // the extinction against the untruncated closed form
    const double slope = rain::size_slope(4.0);
    const double closed = std::numbers::pi * rain::kMarshallPalmerN0 / (slope * slope * slope) * 1e-6;
    tally->check(near_share(rain::extinction(4.0), closed, 0.05) && rain::extinction(0.0) == 0.0 &&
                     rain::extinction(12.0) > rain::extinction(4.0),
                 text::format("extinction at 4 mm/h %.3e /m (closed form %.3e): %.2f km of visibility; 12 mm/h %.2f km",
                              rain::extinction(4.0), closed, 3.912 / rain::extinction(4.0) / 1000.0,
                              3.912 / rain::extinction(12.0) / 1000.0));
    tally->check(rain::impact_rate(4.0) > rain::impact_rate(1.0) &&
                     rain::impact_rate(4.0, 1.0) < rain::impact_rate(4.0),
                 text::format("drops striking a square metre a second at 4 mm/h: %.0f (%.0f of 1 mm and over)",
                              rain::impact_rate(4.0), rain::impact_rate(4.0, 1.0)));
}

void check_wind(Tally* tally)
{
    text::print("the wind and the sky\n");
    const double low = rain::wind_at(5.0, 2.0);
    tally->check(std::abs(low - (5.0 * std::log(4.0) / std::log(20.0))) < 1e-9 && rain::wind_at(5.0, 10.0) == 5.0,
                 text::format("the log profile over a town: 5 m/s at 10 m is %.2f m/s at 2 m", low));
    const rain::Fall still = rain::fall(4.0, 0.0, {1.0, 0.0});
    const rain::Fall windy = rain::fall(4.0, 5.0, {1.0, 0.0});
    const double v0 = rain::fall_speed(rain::median_volume_diameter(4.0));
    const double length = std::sqrt((core::at(windy.direction, 0) * core::at(windy.direction, 0)) +
                                    (core::at(windy.direction, 1) * core::at(windy.direction, 1)) +
                                    (core::at(windy.direction, 2) * core::at(windy.direction, 2)));
    tally->check(
        core::at(still.direction, 1) == -1.0 && std::abs(length - 1.0) < 1e-9 &&
            std::abs(core::at(windy.slant, 0) - (low / v0)) < 1e-9 && core::at(windy.direction, 0) > 0.0,
        text::format(
            "the fall: straight down in still air; in 5 m/s the median drop (%.2f mm, %.2f m/s) slants %.2f a unit",
            rain::median_volume_diameter(4.0), v0, core::at(windy.slant, 0)));
    tally->check(rain::cloud_cover(1.0) == 1.0 && rain::cloud_cover(0.0) == 0.0,
                 "rain falls from a deck covering the sky");
}

void check_ground(Tally* tally)
{
    text::print("the water on the ground\n");
    rain::reset();
    rain::set_settings({.rate = 4.0});
    rain::step(0.0);
    tally->check(rain::stored() == rain::kDepressionStorage,
                 "a level entered while it rains: its ground as long rain leaves it");
    rain::reset();
    rain::set_settings({.rate = 0.0});
    rain::step(0.0);
    rain::set_settings({.rate = 4.0});
    double seconds = 0.0;
    while (rain::stored() < rain::kPoreStorage && seconds < 3600.0) {
        rain::step(1.0);
        seconds += 1.0;
    }
    tally->check(std::abs(seconds - (rain::kPoreStorage / 4.0 * 3600.0)) <= 1.0,
                 text::format("a dry level rained on at 4 mm/h: its pores full in %.0f s", seconds));
    rain::set_stored(rain::kDepressionStorage);
    rain::set_settings({.rate = 0.0});
    seconds = 0.0;
    while (rain::stored() > 0.0 && seconds < 36000.0) {
        rain::step(10.0);
        seconds += 10.0;
    }
    tally->check(std::abs(seconds - (rain::kDepressionStorage / rain::kEvaporation * 3600.0)) <= 10.0,
                 text::format("the rain stopped: dry in %.0f min", seconds / 60.0));
    rain::reset();
    // Nusselt's film
    const double steep = rain::film_thickness(4.0, 1.0, 1.0);
    const double gentle = rain::film_thickness(4.0, 0.05, 1.0);
    tally->check(
        steep > 0.0 && steep < gentle && rain::film_thickness(4.0, 0.0, 0.7) == 0.7 &&
            rain::film_thickness(0.0, 0.5, 1.0) == 0.0,
        text::format("the film at 4 mm/h: %.3f mm on a wall, %.3f mm on a 5%% slope; on level ground the water stored",
                     steep, gentle));
    // the puddles
    const double some = rain::puddle_cover(0.3);
    const double more = rain::puddle_cover(1.0);
    const double level_film = rain::film_thickness(4.0, rain::relief_slope(), 1.0);
    tally->check(rain::relief_slope() > 0.0 && rain::relief_slope() < 0.05 && rain::flat_share(level_film) < 1.0 &&
                     rain::flat_share(0.0) == 0.0 && rain::flat_share(10.0) == 1.0,
                 text::format("level ground between the puddles: the relief's slope %.4f drains a %.2f mm film at 4 mm/h - "
                              "%.0f%% of it flat water",
                              rain::relief_slope(), level_film, rain::flat_share(level_film) * 100.0));
    tally->check(rain::puddle_cover(0.0) == 0.0 && some > 0.0 && more > some && more < 1.0,
                 text::format("the puddles: none dry, %.0f%% of level ground at 0.3 mm stored, %.0f%% at 1 mm",
                              some * 100.0, more * 100.0));
}

void check_material(Tally* tally)
{
    text::print("the wet material\n");
    const double outside = rain::water_outside_reflectance();
    const double inside = rain::water_inside_reflectance();
    const double n = rain::kWaterIndex;
    tally->check(
        std::abs(outside - 0.066) < 0.003 && std::abs(inside - (1.0 - ((1.0 - outside) / (n * n)))) < 2e-3 &&
            std::abs(rain::water_f0() - 0.0204) < 5e-4,
        text::format("water's reflectance: from the air %.4f, from inside %.4f (reciprocity %.4f), face-on %.4f",
                     outside, inside, 1.0 - ((1.0 - outside) / (n * n)), rain::water_f0()));
    bool darker = true;
    std::string powers;
    for (const double a : {0.05, 0.1, 0.2, 0.3, 0.5, 0.7, 0.9}) {
        const double wet = rain::wet_albedo(a);
        darker = darker && wet < a && wet > 0.0;
        powers += text::format(" %.2f->%.3f (^%.2f)", a, wet, std::log(wet) / std::log(a));
    }
    tally->check(darker &&
                     std::log(rain::wet_albedo(0.2)) / std::log(0.2) < std::log(rain::wet_albedo(0.6)) / std::log(0.6),
                 "Lekner and Dorf: darker wet, the power over the dry albedo:" + powers);
    double most = 0.0;
    for (int ai = 0; ai <= 20; ++ai) {
        for (int si = 0; si <= 10; ++si) {
            most = std::max(most, rain::wet_reflectance(ai / 20.0, si / 10.0));
        }
    }
    tally->check(most <= 1.0 + 1e-12 && std::abs(rain::wet_reflectance(1.0, 1.0) - 1.0) < 1e-12 &&
                     rain::wet_reflectance(0.5, 1.0) < 0.5,
                 text::format("the wet surface's energy: at most %.6f (a white one's 1 under water; grey 0.5 -> %.3f)",
                              most, rain::wet_reflectance(0.5, 1.0)));
    const rain::Material dry{.albedo = {0.4, 0.3, 0.2}, .roughness = 0.8, .f0 = 0.04, .porosity = 0.0};
    const rain::Material same = rain::wet_material(dry, {.pores = 1.0, .film = 0.0, .puddle = 0.0});
    const rain::Material puddle = rain::wet_material(dry, {.pores = 1.0, .film = 1.0, .puddle = 1.0, .flat = 1.0});
    tally->check(same.albedo == dry.albedo && same.roughness == dry.roughness &&
                     std::abs(puddle.roughness - rain::kWaterRoughness) < 1e-12 &&
                     std::abs(puddle.f0 - rain::water_f0()) < 1e-12 &&
                     core::at(puddle.albedo, 0) < core::at(dry.albedo, 0),
                 "a non-porous surface with full pores unchanged; under a puddle the water's roughness and F0, darker");
    const rain::Material porous =
        rain::wet_material({.albedo = {0.4, 0.4, 0.4}, .roughness = 0.8, .f0 = 0.04, .porosity = 1.0},
                           {.pores = 1.0, .film = 0.0, .puddle = 0.0});
    tally->check(std::abs(core::at(porous.albedo, 0) - rain::wet_albedo(0.4)) < 1e-12 && porous.roughness == 0.8,
                 text::format("a porous surface with its pores full: darkened (0.40 -> %.3f), its roughness its own",
                              core::at(porous.albedo, 0)));
    tally->check(rain::porosity_of(0.9) == 1.0 && rain::porosity_of(0.1) == 0.0,
                 "porosity from roughness (the material table's seam)");
    // through the one light model (rt_brdf.h): a white furnace over every view angle never returns more than 1 for a wet
    // material, and a white wet surface returns what the dry one does
    double brightest = 0.0;
    for (int ai = 0; ai <= 10; ++ai) {
        for (int ri = 0; ri <= 10; ++ri) {
            for (int wi = 0; wi <= 4; ++wi) {
                const float a = static_cast<float>(ai) / 10.0F;
                const render::rt::brdf::Material dry_m{.albedo = {a, a, a}, .f0 = {0.04F, 0.04F, 0.04F},
                                                       .roughness = 0.05F + (0.095F * static_cast<float>(ri))};
                const double share = wi / 4.0;
                const render::rt::brdf::Material wet_m = rain::wet_brdf_material(
                    dry_m, 1.0, {.pores = share, .film = share, .puddle = share, .flat = share});
                for (int vi = 1; vi <= 16; ++vi) {
                    const float nv = static_cast<float>(vi) / 16.0F;
                    const render::rt::brdf::Rgb albedo = render::rt::brdf::specular_albedo(wet_m.f0, nv, wet_m.roughness);
                    const float spec = core::at(albedo, 0);
                    brightest = std::max(brightest, static_cast<double>(spec + ((1.0F - spec) * core::at(wet_m.albedo, 0))));
                }
            }
        }
    }
    tally->check(brightest <= 1.0 + 1e-4,
                 text::format("the wet material through the BRDF (rt_brdf.h): a white furnace at most %.5f over albedos, "
                              "roughnesses, wetness and view angles",
                              brightest));
    const rain::Ripples ripples = rain::ripples_of(4.0);
    const double cell = ripples.cell / rain::kUnitsPerMetre;
    const double rate = ripples.layers / (cell * cell * ripples.life);
    tally->check(ripples.cell > 0.0 && near_share(rate, rain::impact_rate(4.0, rain::kRippleLeast), 1e-9) &&
                     rain::ripples_of(0.0).cell == 0.0,
                 text::format("the rings at 4 mm/h: %d layers of %.1f-unit cells - %.0f rings a square metre a second",
                              ripples.layers, ripples.cell, rate));
}

// --- where it reaches --------------------------------------------------------------------------------------------------

constexpr float kRoofY = 150.0F;
constexpr float kRoofX = 100.0F;  // x -100 - 100
constexpr float kRoofZ0 = 150.0F; // z 150 - 260
constexpr float kRoofZ1 = 260.0F;
constexpr float kWallZ = -400.0F; // a wall x -300 - 300, y 0 - 300 at this z
constexpr float kWallX = 300.0F;
constexpr float kWallTop = 300.0F;
constexpr float kGroundSide = 3000.0F;

std::vector<precip::Triangle> scene_triangles()
{
    const auto quad = [](std::vector<precip::Triangle>* out, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
        out->push_back({a, b, c});
        out->push_back({a, c, d});
    };
    std::vector<precip::Triangle> triangles;
    quad(&triangles, {-kGroundSide, 0.0F, -kGroundSide}, {kGroundSide, 0.0F, -kGroundSide},
         {kGroundSide, 0.0F, kGroundSide}, {-kGroundSide, 0.0F, kGroundSide});
    quad(&triangles, {-kRoofX, kRoofY, kRoofZ0}, {kRoofX, kRoofY, kRoofZ0}, {kRoofX, kRoofY, kRoofZ1},
         {-kRoofX, kRoofY, kRoofZ1});
    quad(&triangles, {-kWallX, 0.0F, kWallZ}, {kWallX, 0.0F, kWallZ}, {kWallX, kWallTop, kWallZ},
         {-kWallX, kWallTop, kWallZ});
    return triangles;
}

struct Probe {
    const char* what;
    Vec3 at;
    bool reached;
};

// The test points (on the ground, 1 unit up) and what the reference says for each fall.
std::vector<Probe> probes_for(const std::array<float, 3>& direction)
{
    const std::vector<precip::Triangle> triangles = scene_triangles();
    const std::vector<std::pair<const char*, Vec3>> points{
        {"under the roof", {0.0F, 1.0F, 205.0F}},
        {"open ground", {0.0F, 1.0F, 0.0F}},
        {"on the roof", {0.0F, kRoofY + 1.0F, 205.0F}},
        {"under the roof's -x edge", {-85.0F, 1.0F, 205.0F}},
        {"past the roof's +x edge", {150.0F, 1.0F, 205.0F}},
        {"the wall's -z side", {0.0F, 1.0F, kWallZ - 40.0F}},
        {"the wall's +z side", {0.0F, 1.0F, kWallZ + 40.0F}},
    };
    std::vector<Probe> probes;
    for (const auto& [what, at] : points) {
        probes.push_back({what, at, precip::reached(triangles, at, direction, 4000.0F)});
    }
    return probes;
}

void check_reach_reference(Tally* tally)
{
    text::print("where it reaches (the processor's reference)\n");
    const std::vector<precip::Triangle> triangles = scene_triangles();
    const Vec3 down{0.0F, -1.0F, 0.0F};
    const auto reached = [&](Vec3 at, Vec3 d) { return precip::reached(triangles, at, d, 4000.0F); };
    tally->check(!reached({0.0F, 1.0F, 205.0F}, down) && reached({0.0F, 1.0F, 0.0F}, down) &&
                     reached({0.0F, kRoofY + 1.0F, 205.0F}, down),
                 "straight down: none under the roof, all beside it and on it");
    // the wind along +x, a 30-degree slant: the roof's shadow moved downwind
    const Vec3 slanted{0.5F, -0.8660254F, 0.0F};
    tally->check(reached({-85.0F, 1.0F, 205.0F}, slanted) && !reached({150.0F, 1.0F, 205.0F}, slanted),
                 "a wind along +x: the ground under the roof's upwind edge wet, the ground past its downwind edge dry");
    // the wind along +z: the wall's lee dry, its windward side wet
    const Vec3 toward_z{0.0F, -0.8660254F, 0.5F};
    tally->check(!reached({0.0F, 1.0F, kWallZ + 40.0F}, toward_z) && reached({0.0F, 1.0F, kWallZ - 40.0F}, toward_z),
                 "a wind along +z: the wall's lee in its rain shadow, its windward side wet");
    tally->check(std::abs(precip::line_height(triangles, {0.0F, 1.0F, 205.0F}, down, 4000.0F, -1000.0F) - kRoofY) <
                     1e-3F,
                 "a line's first hit is the roof's height");
}

void check_drop_places(Tally* tally)
{
    text::print("the drops' places\n");
    const Vec3 camera{100.0F, 50.0F, -30.0F};
    const vk::RainClock clock{.clock = 12.345, .drift = 321.0};
    bool inside = true;
    uint32_t hidden = 0;
    double sum = 0.0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < vk::kRainNearDrops + vk::kRainFarDrops; ++i) {
        const vk::DropPlace place = vk::drop_place(i, clock, camera, 4.0, {1.0F, 0.0F});
        const float box = place.in_near_box ? vk::kRainNearBox : vk::kRainFarBox;
        const float height = place.in_near_box ? vk::kRainNearHeight : vk::kRainFarHeight;
        const float dx = core::at(place.world, 0) - core::at(camera, 0);
        const float dy = core::at(place.world, 1) - core::at(camera, 1);
        const float dz = core::at(place.world, 2) - core::at(camera, 2);
        inside = inside && std::abs(dx) <= (0.5F * box) + 1e-2F && std::abs(dz) <= (0.5F * box) + 1e-2F &&
                 dy >= (-0.6F * height) - 1e-2F && dy <= (0.4F * height) + 1e-2F;
        hidden += place.drawn ? 0U : 1U;
        sum += place.diameter;
        ++count;
    }
    // the truncated exponential's mean over [0.5, 6] mm
    const double l = rain::size_slope(4.0);
    const double a = vk::kRainLeastDrawn;
    const double b = rain::kMostDiameter;
    const double mean = (((a + (1.0 / l)) * std::exp(-l * a)) - ((b + (1.0 / l)) * std::exp(-l * b))) /
                        (std::exp(-l * a) - std::exp(-l * b));
    const double expected_hidden = vk::kRainFarDrops * (vk::kRainNearBox * vk::kRainNearBox * vk::kRainNearHeight) /
                                   (vk::kRainFarBox * vk::kRainFarBox * vk::kRainFarHeight);
    tally->check(
        inside && near_share(sum / count, mean, 0.02) && near_share(hidden, expected_hidden, 0.1),
        text::format(
            "every drop inside its box; the sizes' mean %.3f mm (the distribution's %.3f); %u far drops inside the "
            "near box left out (%.0f expected)",
            sum / count, mean, hidden, expected_hidden));
}

// --- the device ------------------------------------------------------------------------------------------------------

struct Mesh {
    rt::MeshId id = 0;
    render::GpuVertexBuffer* vertices = nullptr;
};

bool make_mesh(render::Gpu& gpu, const std::vector<precip::Triangle>& triangles, rt::MeshId id, Mesh* mesh)
{
    std::vector<uint8_t> bytes(triangles.size() * 3 * kStride, 0);
    std::size_t at = 0;
    for (const precip::Triangle& triangle : triangles) {
        for (const Vec3& corner : triangle) {
            std::memcpy(bytes.data() + at, corner.data(), sizeof(corner));
            at += kStride;
        }
    }
    if (gpu.create_vertex_buffer(static_cast<uint32_t>(bytes.size()), 0, kFvf, value::kPoolDefault, &mesh->vertices) !=
        value::kResultOk) {
        return false;
    }
    void* data = nullptr;
    if (gpu.lock_vertex_buffer(mesh->vertices, 0, 0, &data, 0) != value::kResultOk) {
        return false;
    }
    std::memcpy(data, bytes.data(), bytes.size());
    if (gpu.unlock_vertex_buffer(mesh->vertices) != value::kResultOk) {
        return false;
    }
    mesh->id = id;
    rt::ray_tracing()->add_mesh(id, {.vertices = mesh->vertices,
                                     .vertex_offset = 0,
                                     .vertex_stride = kStride,
                                     .vertex_count = static_cast<uint32_t>(triangles.size() * 3),
                                     .position_offset = 0});
    return true;
}

// The processor's reading of the map (precip_reached's twin over the texels read back).
float twin_reached(const std::vector<uint32_t>& texels, const vk::PrecipMap& map, const Vec3& world)
{
    const std::array<float, 2> uv{
        core::at(world, 0) + (core::at(map.slant, 0) * (core::at(world, 1) - core::at(map.slant, 2))),
        core::at(world, 2) + (core::at(map.slant, 1) * (core::at(world, 1) - core::at(map.slant, 2)))};
    const auto texel = [&](int32_t x, int32_t z, int layer, float slack) {
        const auto size = static_cast<int32_t>(precip::kMapSize);
        const int32_t sx = ((x % size) + size) % size;
        const int32_t sz = ((z % size) + size) % size;
        const std::size_t index =
            ((static_cast<std::size_t>(sz) * precip::kMapSize * precip::kCascades) +
             (static_cast<std::size_t>(layer) * precip::kMapSize) + static_cast<std::size_t>(sx)) *
            2;
        const uint32_t tag = core::at(texels, index + 1);
        if (tag == 0U) {
            return 1.0F;
        }
        float height = 0.0F;
        const uint32_t bits = core::at(texels, index);
        std::memcpy(&height, &bits, sizeof(height));
        return core::at(world, 1) + slack >= height ? 1.0F : 0.0F;
    };
    const auto cascade = [&](int layer, const std::array<float, 4>& window) {
        const float fx = (core::at(uv, 0) / core::at(window, 2)) - 0.5F;
        const float fz = (core::at(uv, 1) / core::at(window, 2)) - 0.5F;
        const auto bx = static_cast<int32_t>(std::floor(fx));
        const auto bz = static_cast<int32_t>(std::floor(fz));
        const float tx = fx - static_cast<float>(bx);
        const float tz = fz - static_cast<float>(bz);
        const int32_t lx = bx - static_cast<int32_t>(core::at(window, 0));
        const int32_t lz = bz - static_cast<int32_t>(core::at(window, 1));
        if (lx < 0 || lz < 0 || lx + 1 >= static_cast<int32_t>(precip::kMapSize) ||
            lz + 1 >= static_cast<int32_t>(precip::kMapSize)) {
            return -1.0F;
        }
        const float slack = core::at(window, 3);
        const float a =
            texel(bx, bz, layer, slack) + ((texel(bx + 1, bz, layer, slack) - texel(bx, bz, layer, slack)) * tx);
        const float b = texel(bx, bz + 1, layer, slack) +
                        ((texel(bx + 1, bz + 1, layer, slack) - texel(bx, bz + 1, layer, slack)) * tx);
        return a + ((b - a) * tz);
    };
    const float inner = cascade(0, map.near_window);
    if (inner >= 0.0F) {
        return inner;
    }
    const float outer = cascade(1, map.far_window);
    return outer >= 0.0F ? outer : 1.0F;
}

void check_map(render::Gpu& gpu, Tally* tally)
{
    text::print("the precipitation occlusion map (on the device)\n");
    std::vector<precip::Triangle> ground;
    std::vector<precip::Triangle> roof;
    std::vector<precip::Triangle> wall;
    const std::vector<precip::Triangle> all = scene_triangles();
    ground.assign(all.begin(), all.begin() + 2);
    roof.assign(all.begin() + 2, all.begin() + 4);
    wall.assign(all.begin() + 4, all.end());
    std::array<Mesh, 3> meshes{};
    const bool made = make_mesh(gpu, ground, 101, &core::at(meshes, 0)) &&
                      make_mesh(gpu, roof, 102, &core::at(meshes, 1)) &&
                      make_mesh(gpu, wall, 103, &core::at(meshes, 2));
    tally->check(made, "a ground, a roof and a wall made and in the scene");
    if (!made) {
        return;
    }
    rt::FrameRecord record;
    record.frame = 1;
    for (const Mesh& mesh : meshes) {
        rt::Instance instance;
        instance.mesh = mesh.id;
        instance.flags = rt::kCastsShadow | rt::kOpaque; // (the level: mask 0x01)
        instance.object_to_world = {{{1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}}};
        record.instances.push_back(instance);
    }
    rt::ViewDrawn view;
    view.camera.position = {0.0F, 60.0F, 0.0F};
    const std::array<std::pair<const char*, std::array<double, 3>>, 3> falls{{
        {"straight down", {0.0, -1.0, 0.0}},
        {"a wind along +x", {0.5, -0.8660254, 0.0}},
        {"a wind along +z", {0.0, -0.8660254, 0.5}},
    }};
    for (const auto& [what, direction] : falls) {
        rt::ray_tracing()->begin_frame(record);
        ++record.frame;
        vk::Backend& state = vk::backend();
        std::vector<uint32_t> texels;
        vk::PrecipMap map;
        {
            const std::scoped_lock lock(state.mutex);
            vk::update_precip_map(state, view, direction);
            map = vk::precip_map(state);
            vk::read_precip_map(state, &texels);
        }
        gpu.begin_scene();
        gpu.end_scene();
        gpu.present();
        const Vec3 fall{static_cast<float>(core::at(direction, 0)), static_cast<float>(core::at(direction, 1)),
                        static_cast<float>(core::at(direction, 2))};
        std::string misses;
        bool all_agree = map.on && !texels.empty();
        for (const Probe& probe : probes_for(fall)) {
            const float got = map.on && !texels.empty() ? twin_reached(texels, map, probe.at) : -1.0F;
            const bool ok = probe.reached ? got > 0.75F : got < 0.25F;
            all_agree = all_agree && ok;
            misses += text::format(" %s %s%s;", probe.what, probe.reached ? "wet" : "dry",
                                   ok ? "" : text::format(" (map %.2f)", got).c_str());
        }
        if (!all_agree) { // (what the map holds: how many texels traced, their heights' range)
            std::size_t traced = 0;
            float low = 1e30F;
            float high = -1e30F;
            for (std::size_t i = 0; i + 1 < texels.size(); i += 2) {
                if (core::at(texels, i + 1) != 0U) {
                    ++traced;
                    float height = 0.0F;
                    const uint32_t bits = core::at(texels, i);
                    std::memcpy(&height, &bits, sizeof(height));
                    low = std::min(low, height);
                    high = std::max(high, height);
                }
            }
            text::print(
                "    (the map: on %d, %zu of %zu texels traced, heights %.1f - %.1f; the near window %.0f %.0f %.1f "
                "%.1f)\n",
                map.on ? 1 : 0, traced, texels.size() / 2, static_cast<double>(low), static_cast<double>(high),
                static_cast<double>(core::at(map.near_window, 0)), static_cast<double>(core::at(map.near_window, 1)),
                static_cast<double>(core::at(map.near_window, 2)), static_cast<double>(core::at(map.near_window, 3)));
        }
        tally->check(all_agree, text::format("%s - the map as the reference:%s", what, misses.c_str()));
    }
    for (const Mesh& mesh : meshes) {
        rt::ray_tracing()->remove_mesh(mesh.id);
        gpu.release_vertex_buffer(mesh.vertices);
    }
    const vk::PrecipStats stats = vk::precip_stats();
    tally->check(stats.updates >= 3 && stats.generation >= 3,
                 text::format("traced %llu times, %u generations (each new slant)",
                              static_cast<unsigned long long>(stats.updates), stats.generation));
}

void check_passes(Tally* tally)
{
    text::print("the passes\n");
    vk::Backend& state = vk::backend();
    std::string error;
    bool made = false;
    {
        const std::scoped_lock lock(state.mutex);
        made = vk::make_rain_passes(state, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_D32_SFLOAT, true, &error) &&
               vk::make_rain_passes(state, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_D32_SFLOAT, false, &error);
    }
    tally->check(
        made,
        "every pass made (the drops placed and lit, traced and not; drawn, their motion; the splashes; the wet mask)" +
            (error.empty() ? std::string() : ": " + error));
}

int window_position(int display)
{
    int displays = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&displays);
    int chosen = display;
    if (chosen <= 0) {
        chosen = displays > 1 ? 2 : 1;
    }
    int position = static_cast<int>(SDL_WINDOWPOS_CENTERED);
    if (ids != nullptr && chosen <= displays) {
        const std::span<const SDL_DisplayID> listed(ids, static_cast<size_t>(displays));
        const SDL_DisplayID id = core::at(listed, chosen - 1);
        position = static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(id));
    }
    SDL_free(ids);
    return position;
}

int run(const Options& options, SDL_Window* window)
{
    Tally tally;
    check_drops(&tally);
    check_wind(&tally);
    check_ground(&tally);
    check_material(&tally);
    check_reach_reference(&tally);
    check_drop_places(&tally);
    vk::set_context_options({.validation = options.validation,
                             .device_index = options.device,
                             .on_message = print_line,
                             .ray_tracing = true},
                            window);
    render::GpuContext& context = vk::gpu_context();
    if (!context.create_instance()) {
        text::print("the device could not be opened\n");
        text::print("%d checks passed, %d failed\n", tally.passed, tally.failed);
        return vk::loader_version() == 0 ? 2 : 1;
    }
    const vk::Backend& state = vk::backend();
    const bool layer_on = state.context.validation_on();
    text::print("validation layer: %s\n", layer_on ? "on" : "off (not installed, or not asked for)");
    context.configure({.width = kWindowSize,
                       .height = kWindowSize,
                       .windowed = true,
                       .vsync = false,
                       .samples = 0,
                       .sample_quality = 0,
                       .colour_format = value::kFormatX8R8G8B8,
                       .depth_format = value::kFormatD24X8});
    const long made = context.create_device(
        {.width = kWindowSize, .height = kWindowSize, .hardware_vertices = true, .samples = 0, .sample_quality = 0});
    if (made != value::kResultOk) {
        text::print("create_device failed (%ld)\n", made);
        context.shutdown();
        return 1;
    }
    context.mark_ready();
    text::print("device: %s\n", context.adapter().description.data());
    if (state.context.ray_tracing_enabled()) {
        check_map(vk::gpu(), &tally);
    } else {
        text::print("this device has no ray tracing: the map's checks skipped\n");
    }
    check_passes(&tally);
    context.shutdown();
    tally.check(validation_messages() == 0, text::format("validation messages: %d%s", validation_messages(),
                                                         layer_on ? "" : " (the layer wasn't on)"));
    text::print("%d checks passed, %d failed\n", tally.passed, tally.failed);
    return tally.failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_rain_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
    if (!vk::open_loader()) {
        text::print("no Vulkan loader (vulkan-1.dll) for this process\n");
        return 2;
    }
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        text::print("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const int position = window_position(options.display);
    SDL_Window* window =
        SDL_CreateWindow("thug2 Vulkan rain check", kWindowSize, kWindowSize, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        text::print("SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    SDL_SetWindowPosition(window, position, position);
    tests::show_behind(window);
    int result = 1;
    try {
        result = run(options, window);
    } catch (const std::exception& error) {
        text::print("the check stopped: %s\n", error.what());
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
