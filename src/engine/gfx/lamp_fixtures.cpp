// NOT ORIGINAL - (round L) lamps by physics: see lamp_fixtures.h.
#include "engine/gfx/lamp_fixtures.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

#include "base/core/checked_index.h"

namespace render::rt {

namespace {

// (the figures and their sources: lamp_fixtures.h)
constexpr std::array<FixtureValues, kLampFixtures> kFixtures{{
    {.name = "unknown", .luminance = 0.0F, .least = 0.0F, .most = 0.0F, .kelvin = 0.0F, .least_area = 0.0F, .most_area = 0.0F},
    {.name = "sodium street light", .luminance = 2.1e4F, .least = 1.0e3F, .most = 1.2e6F, .kelvin = 2000.0F,
     .least_area = 0.01F, .most_area = 0.5F, .lumens = 9500.0F, .least_lumens = 2250.0F, .most_lumens = 55800.0F},
    {.name = "white street light", .luminance = 2.2e4F, .least = 1.5e3F, .most = 1.0e6F, .kelvin = 4000.0F,
     .least_area = 0.01F, .most_area = 0.5F, .lumens = 10000.0F, .least_lumens = 3400.0F, .most_lumens = 50000.0F},
    {.name = "fluorescent tube", .luminance = 1.0e4F, .least = 4.0e3F, .most = 2.8e4F, .kelvin = 4000.0F,
     .least_area = 0.005F, .most_area = 0.6F, .lumens = 3100.0F, .least_lumens = 1190.0F, .most_lumens = 9500.0F,
     .least_width = 0.015F, .most_width = 0.6F, .most_length = 2.44F},
    {.name = "ceiling panel", .luminance = 3.2e3F, .least = 1.0e3F, .most = 2.0e4F, .kelvin = 4000.0F,
     .least_area = 0.05F, .most_area = 1.5F, .lumens = 3600.0F, .least_lumens = 1000.0F, .most_lumens = 10000.0F},
    {.name = "bulb", .luminance = 2.4e4F, .least = 5.0e3F, .most = 2.0e5F, .kelvin = 2800.0F, .least_area = 0.002F,
     .most_area = 0.3F, .lumens = 855.0F, .least_lumens = 120.0F, .most_lumens = 5000.0F},
    {.name = "neon tube", .luminance = 8.7e2F, .least = 45.0F, .most = 1.8e4F, .kelvin = 0.0F, .least_area = 0.001F,
     .most_area = 2.0F, .lumens = 400.0F, .least_lumens = 7.0F, .most_lumens = 21500.0F, .least_width = 0.006F,
     .most_width = 0.15F, .most_length = 10.0F},
    {.name = "sign or lit window", .luminance = 4.0e2F, .least = 5.0e1F, .most = 1.0e3F, .kelvin = 0.0F,
     .least_area = 0.02F, .most_area = 20.0F, .lumens = 1300.0F, .least_lumens = 3.0F, .most_lumens = 63000.0F},
}};

constexpr std::array<float, 3> kLuma{0.2126F, 0.7152F, 0.0722F};

float luma_of(const std::array<float, 3>& colour)
{
    return (kLuma[0] * colour[0]) + (kLuma[1] * colour[1]) + (kLuma[2] * colour[2]);
}

} // namespace

const FixtureValues& fixture_values(LampFixture fixture)
{
    const auto at = static_cast<std::size_t>(fixture);
    return at < kFixtures.size() ? core::at(kFixtures, at) : kFixtures.front();
}

std::string_view fixture_name(LampFixture fixture) { return fixture_values(fixture).name; }

float colour_kelvin(const std::array<float, 3>& colour)
{
    // linear sRGB -> XYZ -> xy, McCamy's cubic (meaningful from about 2000 to 12 500 K)
    const double r = colour[0];
    const double g = colour[1];
    const double b = colour[2];
    const double big_x = (0.4124 * r) + (0.3576 * g) + (0.1805 * b);
    const double big_y = (0.2126 * r) + (0.7152 * g) + (0.0722 * b);
    const double big_z = (0.0193 * r) + (0.1192 * g) + (0.9505 * b);
    const double sum = big_x + big_y + big_z;
    if (!(sum > 1e-12)) {
        return 0.0F;
    }
    const double x = big_x / sum;
    const double y = big_y / sum;
    const double slope = (x - 0.3320) / (0.1858 - y);
    const double cct = (449.0 * slope * slope * slope) + (3525.0 * slope * slope) + (6823.3 * slope) + 5520.33;
    constexpr double kLeast = 1000.0;
    constexpr double kMost = 25000.0;
    return cct >= kLeast && cct <= kMost ? static_cast<float>(cct) : 0.0F;
}

namespace {
// CIE 1960 uv of a linear sRGB colour (false: black)
bool uv_of(const std::array<float, 3>& colour, double* u, double* v)
{
    const double r = colour[0];
    const double g = colour[1];
    const double b = colour[2];
    const double big_x = (0.4124 * r) + (0.3576 * g) + (0.1805 * b);
    const double big_y = (0.2126 * r) + (0.7152 * g) + (0.0722 * b);
    const double big_z = (0.0193 * r) + (0.1192 * g) + (0.9505 * b);
    const double d = big_x + (15.0 * big_y) + (3.0 * big_z);
    if (!(d > 1e-12)) {
        return false;
    }
    *u = 4.0 * big_x / d;
    *v = 6.0 * big_y / d;
    return true;
}
} // namespace

bool saturated_colour(const std::array<float, 3>& colour)
{
    double u = 0.0;
    double v = 0.0;
    if (!uv_of(colour, &u, &v)) {
        return false; // (black: no colour to tell)
    }
    const float kelvin = colour_kelvin(colour);
    if (!(kelvin > 0.0F)) {
        return true;
    }
    double lu = 0.0;
    double lv = 0.0;
    if (!uv_of(blackbody_colour(kelvin), &lu, &lv)) {
        return true;
    }
    return std::hypot(u - lu, v - lv) > static_cast<double>(kLightDuv);
}

LampFixture fixture_of(const FixtureEvidence& evidence)
{
    constexpr uint8_t kPanelShape = 1;
    constexpr uint8_t kLineShape = 2;
    const bool panel = evidence.shape == kPanelShape;
    const bool line = evidence.shape == kLineShape;
    if (saturated_colour(evidence.colour)) {
        return panel ? LampFixture::kSign : LampFixture::kNeon;
    }
    if (panel && std::abs(evidence.axis[1]) < kVerticalPanel) {
        return LampFixture::kSign;
    }
    if (line) {
        return LampFixture::kTube;
    }
    if (evidence.indoors) {
        return panel ? LampFixture::kPanel : LampFixture::kBulb;
    }
    const float kelvin = colour_kelvin(evidence.colour);
    if (kelvin > 0.0F && kelvin < kSodiumKelvin) {
        return LampFixture::kSodiumStreet;
    }
    if (kelvin > 0.0F && kelvin < kFilamentKelvin) {
        return LampFixture::kBulb;
    }
    return LampFixture::kWhiteStreet;
}

std::array<float, 3> blackbody_colour(float kelvin)
{
    const double t = std::clamp(static_cast<double>(kelvin), 1667.0, 25000.0);
    const double t1 = 1.0 / t;
    const double t2 = t1 * t1;
    const double t3 = t2 * t1;
    // Kim et al. 2002: the Planckian locus in xy
    const double x = t <= 4000.0 ? (-0.2661239e9 * t3) - (0.2343589e6 * t2) + (0.8776956e3 * t1) + 0.179910
                                 : (-3.0258469e9 * t3) + (2.1070379e6 * t2) + (0.2226347e3 * t1) + 0.240390;
    const double x2 = x * x;
    const double x3 = x2 * x;
    double y = 0.0;
    if (t <= 2222.0) {
        y = (-1.1063814 * x3) - (1.34811020 * x2) + (2.18555832 * x) - 0.20219683;
    } else if (t <= 4000.0) {
        y = (-0.9549476 * x3) - (1.37418593 * x2) + (2.09137015 * x) - 0.16748867;
    } else {
        y = (3.0817580 * x3) - (5.87338670 * x2) + (3.75112997 * x) - 0.37001483;
    }
    const double big_x = x / y;
    const double big_z = (1.0 - x - y) / y;
    std::array<float, 3> rgb{static_cast<float>(std::max(0.0, (3.2406 * big_x) - 1.5372 - (0.4986 * big_z))),
                             static_cast<float>(std::max(0.0, (-0.9689 * big_x) + 1.8758 + (0.0415 * big_z))),
                             static_cast<float>(std::max(0.0, (0.0557 * big_x) - 0.2040 + (1.0570 * big_z)))};
    const float luma = luma_of(rgb);
    for (float& channel : rgb) {
        channel = luma > 0.0F ? channel / luma : 1.0F;
    }
    return rgb;
}

} // namespace render::rt
