// NOT ORIGINAL - thug2_vulkan_snow_check.exe: the snow (engine/gfx/snow.h - the sky map, the trails, the spray;
// gpu_vulkan/vulkan_snow.h - the cover, the layer, the air) tried on this machine.
//
// On the processor (no device needed):
//   - the sky map of a synthetic level: a ground square, a roof over part of it (only in the collision - the ground under
//     it is drawn, so the cover's "under something" can be seen), a 60-degree bank, an indoor sector's floor, a kerb's
//     10-unit step: open ground holds snow, the roof's top is the top under it, the bank and the indoor floor none, the
//     kerb's edge stays a step (never blended); the same faces give the same cells;
//   - the trails: a stamped capsule pressed down along it (the press full inside, the berm beside it, the activity round
//     it, nothing beyond); the layer's height there (snow::layer_height) - none in the trail, more than the depth on the
//     berm;
//   - the steps: a rider at 600 units a second presses a trail along its path and throws spray, more on a carve than
//     straight on; the spray never more than kMostSpray; a landing stamps once and bursts; walking leaves footprints a
//     stride apart; a wheel presses; nothing pressed from high above the snow, nor where no snow settles; the trails kept
//     over 1000 steps with nothing pressing; while snow falls the press fills in (half in kFillSeconds / 2, all in
//     kFillSeconds, the activity kept); the same steps twice (reset between) give the same trails and spray byte for byte.
// On the device (a 320x240 16-bit float target with a depth surface; the camera 60 units up looking 35 degrees down):
//   - the window: trails pressed near the camera and far from it; the trails' texture read back holds exactly the
//     window's tiles (every texel against the processor's), and again after the camera moves 7 tiles - the tiles that
//     stayed not copied again;
//   - the cover: a dark ground drawn with the game's single-texture material shader (kTextureVertexX4) and marked as the
//     ground turns white where it is open to the sky, stays dark under the roof's footprint and on a wall standing on it,
//     and isn't touched when the draw isn't marked or snow is off;
//   - the layer: a trail across the view - pixels whose ground is far from it unchanged, pixels on it changed, the packed
//     snow at its middle darker than the untouched snow beside it; every pixel finite;
//   - the air: the falling snow changes the sky's pixels, the spray drawn is what is alive (at most kMostSpray);
//   - the validation layer (with synchronization validation) says nothing all along;
//   - the cost at 2560x1440 (the window full of trails, snow falling, a carve's spray) printed.
//   thug2_vulkan_snow_check [--device N] [--display N] [--no-validation]
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
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
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
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/snow.h"
#include "engine/gfx/wind.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_snow.h"
#include "tests/check_window.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuRenderState;
using render::GpuSamplerState;
using render::GpuStageState;
using render::GpuSurface;
using render::GpuTexture;
namespace value = render::gpu_value;
namespace vk = render::vulkan;
namespace snow = render::snow;
using Vec3 = std::array<float, 3>;
using Matrix = std::array<std::array<float, 4>, 4>;

constexpr int kWindowSize = 128;
constexpr uint32_t kWidth = 320;
constexpr uint32_t kHeight = 240;
constexpr int kBitsPerByte = 8;
constexpr std::size_t kHalfPixelBytes = 8;
constexpr uint32_t kHighByte = 8;
constexpr uint16_t kHalfSign = 0x8000;
constexpr float kByteMax = 255.0F;
constexpr float kVerticalScale = 1.7320508F; // 1 / tan(30 degrees)
constexpr float kNear = 1.0F;
constexpr float kFar = 4000.0F;
constexpr float kPitch = 0.61086524F; // 35 degrees down
constexpr Vec3 kCamera{0.0F, 60.0F, -100.0F};
constexpr float kHalf = 0.5F;
constexpr std::size_t kMatrixFloats = 16;
constexpr std::size_t kQuadCorners = 6;
constexpr uint32_t kFvfPosition = 0x002;
constexpr uint32_t kFvfDiffuse = 0x040;
constexpr uint32_t kFvfTex1 = 0x100;
constexpr uint32_t kHalfGrey = 0x80808080U; // (the vertex colour 128: 1 after the shader's doubling)
constexpr uint32_t kSkyColour = 0xFF203040U;
constexpr Vec3 kLuma{0.2126F, 0.7152F, 0.0722F};
constexpr std::string_view kShader = "ps.1.1\ntex t0\nmul r0.rgb,t0,c0\n+mul_x2 r0.a,v0.a,t0.a\nmul_x4 r0.rgb,v0,r0\n";
constexpr double kStep = 1.0 / 60.0;

// The synthetic level (world units).
constexpr float kGround = 2000.0F;    // the ground square's half side
constexpr float kRoofLow = 150.0F;    // the roof (collision only) over x -100 - 100, z kRoofLow - kRoofHigh, 150 up
constexpr float kRoofHigh = 260.0F;
constexpr float kRoofSide = 100.0F;
constexpr float kRoofTop = 150.0F;
constexpr float kWallZ = 450.0F;      // a wall standing on the ground (drawn), x -60 - 60, up to kWallTop
constexpr float kWallSide = 60.0F;
constexpr float kWallTop = 80.0F;
constexpr float kKerbLow = 600.0F;    // a kerb's block 10 up, x kKerbLow - kKerbHigh
constexpr float kKerbHigh = 700.0F;
constexpr float kKerbTop = 10.0F;
constexpr float kIndoorLow = -800.0F; // an indoor sector's floor, half a unit up
constexpr float kIndoorHigh = -700.0F;
constexpr float kBankLow = -1500.0F;  // a 60-degree bank, x kBankLow - kBankLow + 100
constexpr float kTrailZ = -62.0F;     // the layer's trail: across the view along x at this z (near the bottom: seen from above)
constexpr float kTrailHalf = 80.0F;

struct Options {
    int display = 0;
    int device = -1;
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

float from_half(uint16_t bits)
{
    constexpr uint32_t kMantissaBits = 10;
    constexpr uint32_t kExponentMask = 0x1F;
    constexpr uint32_t kMantissaMask = 0x3FF;
    constexpr uint32_t kImplicitOne = 0x400;
    constexpr int kSmallestPower = -24;
    constexpr int kPowerBias = 25;
    const uint32_t exponent = (static_cast<uint32_t>(bits) >> kMantissaBits) & kExponentMask;
    const uint32_t mantissa = bits & kMantissaMask;
    float magnitude = 0.0F;
    if (exponent == 0) {
        magnitude = std::ldexp(static_cast<float>(mantissa), kSmallestPower);
    } else if (exponent == kExponentMask) {
        magnitude = mantissa == 0 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
    } else {
        magnitude = std::ldexp(static_cast<float>(mantissa + kImplicitOne), static_cast<int>(exponent) - kPowerBias);
    }
    return (bits & kHalfSign) != 0 ? -magnitude : magnitude;
}

float dot(const Vec3& a, const Vec3& b)
{
    return (core::at(a, 0) * core::at(b, 0)) + (core::at(a, 1) * core::at(b, 1)) + (core::at(a, 2) * core::at(b, 2));
}
float luma(const Vec3& c) { return dot(c, kLuma); }

// --- the level on the processor -------------------------------------------------------------------------------------

void add_quad(std::vector<snow::Face>* faces, const std::array<Vec3, 4>& corners, bool settles = true)
{
    faces->push_back({.corners = {core::at(corners, 0), core::at(corners, 1), core::at(corners, 2)}, .settles = settles});
    faces->push_back({.corners = {core::at(corners, 0), core::at(corners, 2), core::at(corners, 3)}, .settles = settles});
}

void add_flat(std::vector<snow::Face>* faces, std::array<float, 4> area, float height, bool settles = true)
{
    const auto [x0, x1, z0, z1] = area;
    add_quad(faces, {{{x0, height, z0}, {x1, height, z0}, {x1, height, z1}, {x0, height, z1}}}, settles);
}

std::vector<snow::Face> level_faces()
{
    std::vector<snow::Face> faces;
    add_flat(&faces, {-kGround, kGround, -kGround, kGround}, 0.0F);
    add_flat(&faces, {-kRoofSide, kRoofSide, kRoofLow, kRoofHigh}, kRoofTop);
    add_flat(&faces, {kKerbLow, kKerbHigh, -kGround, kGround}, kKerbTop);
    add_flat(&faces, {kIndoorLow, kIndoorHigh, kIndoorLow, kIndoorHigh}, kHalf, false);
    constexpr float kBankRun = 100.0F;
    constexpr float kBankRise = 173.2F; // (60 degrees)
    add_quad(&faces, {{{kBankLow, 0.0F, -200.0F}, {kBankLow + kBankRun, kBankRise, -200.0F}, {kBankLow + kBankRun, kBankRise, 200.0F},
                       {kBankLow, 0.0F, 200.0F}}});
    return faces;
}

void check_sky_map(Tally* tally)
{
    const std::vector<snow::Face> faces = level_faces();
    const snow::SkyMap map = snow::make_sky_map(faces);
    const snow::SkyMap again = snow::make_sky_map(faces);
    tally->check(map.cells == again.cells && map.columns > 0, text::format("the sky map: %ux%u cells of %.1f units, the same twice",
                                                                            map.columns, map.rows, static_cast<double>(map.cell)));
    const snow::Ground open = snow::ground_at(map, -300.0F, 0.0F);
    tally->check(open.found && std::abs(open.height) < 0.05F && open.cover > 0.99F, "open ground: snow on it, at its height");
    const snow::Ground roof = snow::ground_at(map, 0.0F, (kRoofLow + kRoofHigh) * kHalf);
    tally->check(roof.found && std::abs(roof.height - kRoofTop) < 0.1F, "under the roof: the roof's top is the top");
    const snow::Ground indoor = snow::ground_at(map, (kIndoorLow + kIndoorHigh) * kHalf, (kIndoorLow + kIndoorHigh) * kHalf);
    tally->check(indoor.found && indoor.cover < 0.01F, "an indoor sector's floor: no snow");
    const snow::Ground bank = snow::ground_at(map, kBankLow + 50.0F, 0.0F);
    tally->check(bank.found && bank.cover > 0.2F && bank.cover < 0.45F,
                 text::format("a 60-degree bank: a light coat (%.2f)", static_cast<double>(bank.cover)));
    bool steps_kept = true;
    for (float x = kKerbLow - (2.0F * map.cell); x <= kKerbLow + (2.0F * map.cell); x += map.cell * 0.13F) {
        const float height = snow::ground_at(map, x, 0.0F).height;
        steps_kept = steps_kept && (std::abs(height) < 0.05F || std::abs(height - kKerbTop) < 0.05F);
    }
    tally->check(steps_kept, "the kerb's step: either side's height, never between");
}

// The slope's curve: full on flat ground, thinning up ramps, a light coat on a quarter pipe's steep face, none on a wall;
// never thicker for a steeper face.
void check_slope(Tally* tally)
{
    const auto at_degrees = [](float degrees) { return snow::slope_cover(std::cos(degrees * 0.017453293F)); };
    const float flat = at_degrees(0.0F);
    const float gentle = at_degrees(15.0F);
    const float ramp = at_degrees(45.0F);
    const float steep = at_degrees(75.0F);
    const float wall = at_degrees(87.0F);
    bool falling = true;
    float before = 2.0F;
    for (float degrees = 0.0F; degrees <= 90.0F; degrees += 0.5F) {
        const float now = at_degrees(degrees);
        falling = falling && now <= before + 1.0e-6F && now >= 0.0F && now <= 1.0F;
        before = now;
    }
    tally->check(flat > 0.999F && gentle > 0.99F && ramp > 0.45F && ramp < 0.8F && steep > 0.08F && steep < 0.3F && wall == 0.0F &&
                     falling,
                 text::format("the slope's cover: flat %.2f, 15 degrees %.2f, 45 %.2f, 75 %.2f (a light coat), 87 %.2f; never "
                              "thicker steeper",
                              static_cast<double>(flat), static_cast<double>(gentle), static_cast<double>(ramp),
                              static_cast<double>(steep), static_cast<double>(wall)));
}

// The winter sky over the lighting state: dimmer, softer direct light, a greyer sky, weaker shadows; none at share 0.
void check_overcast(Tally* tally)
{
    render::rt::DayLight light;
    light.on = true;
    light.direct = 1.2F;
    light.direct_tint = {1.0F, 0.95F, 0.85F};
    light.light_colour = {1.2F, 1.14F, 1.02F};
    light.sky = {0.8F, 0.95F, 1.3F};
    light.light_radius = render::rt::kSunRadiusDegrees;
    render::rt::DayLight same = light;
    render::rt::apply_overcast(&same, 0.0F);
    render::rt::DayLight grey = light;
    render::rt::apply_overcast(&grey, 1.0F);
    const auto spread = [](const render::rt::DayRgb& c) {
        return std::max({core::at(c, 0), core::at(c, 1), core::at(c, 2)}) - std::min({core::at(c, 0), core::at(c, 1), core::at(c, 2)});
    };
    tally->check(same.direct == light.direct && same.sky == light.sky, "the overcast at 0: the light as it was");
    tally->check(grey.direct < light.direct * 0.5F && grey.light_radius > 10.0F && spread(grey.sky) < spread(light.sky) * 0.5F &&
                     grey.shadow < 1.0F && std::isfinite(grey.exposure),
                 text::format("the overcast: direct %.2f -> %.2f, radius %.1f degrees, the sky's spread %.2f -> %.2f, shadows %.2f",
                              static_cast<double>(light.direct), static_cast<double>(grey.direct),
                              static_cast<double>(grey.light_radius), static_cast<double>(spread(light.sky)),
                              static_cast<double>(spread(grey.sky)), static_cast<double>(grey.shadow)));
}

void check_stamp(Tally* tally)
{
    snow::TrailField field(2.0F);
    constexpr float kRadius = snow::kBoardHalfWidth;
    field.stamp({.from = {0.0F, 0.0F}, .to = {100.0F, 0.0F}, .radius = kRadius, .press = 1.0F, .berm = 0.6F});
    const auto at = [&field](float x, float z) {
        const std::array<int, 2> texel = field.texel_of(x, z);
        return field.texel_at(core::at(texel, 0), core::at(texel, 1));
    };
    constexpr uint32_t kByte = 0xFFU;
    const uint32_t middle = at(50.0F, 0.5F);
    tally->check((middle & kByte) == kByte && ((middle >> snow::kActiveShift) & kByte) == kByte, "the trail's middle: pressed down, active");
    const uint32_t berm = at(50.0F, kRadius + 2.5F);
    tally->check(((berm >> snow::kBermShift) & kByte) > 0 && (berm & kByte) < kByte / 2, "beside it: a berm");
    const uint32_t beyond = at(50.0F, kRadius + snow::kBermWidth + snow::kActiveReach + 4.0F);
    tally->check(beyond == 0, "beyond its reach: untouched");
    constexpr float kDepth = 4.0F;
    tally->check(snow::layer_height(middle, kDepth, 1.0F) < 0.01F && snow::layer_height(berm, kDepth, 1.0F) > kDepth * 0.5F &&
                     snow::layer_height(at(50.0F, 20.0F), kDepth, 1.0F) > 0.0F,
                 "the layer: down in the trail, raised beside it");
}

// A rider's path: `steps` steps along +x from `from` at `speed`, turning by `turn` radians a step.
void ride(std::array<float, 3> from, float speed, float turn, int steps, uint64_t id = 1)
{
    float heading = 0.0F;
    std::array<float, 3> at = from;
    for (int step = 0; step < steps; ++step) {
        const snow::Presser presser{.id = id, .kind = snow::PresserKind::kCharacter, .at = at, .facing = {1.0F, 0.0F}};
        snow::step(std::span(&presser, 1), kStep, false);
        heading += turn;
        core::at(at, 0) += std::cos(heading) * speed * static_cast<float>(kStep);
        core::at(at, 2) += std::sin(heading) * speed * static_cast<float>(kStep);
    }
}

std::vector<uint32_t> all_texels()
{
    std::vector<uint32_t> out;
    snow::read_trails([&out](const snow::TrailField& field) {
        for (int z = -200; z < 200; ++z) {
            for (int x = -200; x < 400; ++x) {
                out.push_back(field.texel_at(x, z));
            }
        }
    });
    return out;
}

uint32_t texel_at_world(float x, float z)
{
    uint32_t found = 0;
    snow::read_trails([&](const snow::TrailField& field) {
        const std::array<int, 2> texel = field.texel_of(x, z);
        found = field.texel_at(core::at(texel, 0), core::at(texel, 1));
    });
    return found;
}

void check_steps(Tally* tally)
{
    snow::set_settings({.on = true, .depth = 4.0F, .quality = 2, .falling = true});
    std::vector<snow::Face> faces;
    add_flat(&faces, {-kGround, kGround, -kGround, kGround}, 0.0F);
    add_flat(&faces, {-kGround, kGround, 1000.0F, kGround}, kHalf, false); // (indoors beyond z 1000: the top there, no snow)
    snow::set_sky_map(std::make_shared<snow::SkyMap>(snow::make_sky_map(faces)));
    constexpr float kSpeed = 600.0F;
    constexpr int kSteps = 60;
    ride({0.0F, 0.0F, 0.0F}, kSpeed, 0.0F, kSteps);
    const snow::Counts straight = snow::counts();
    tally->check(straight.stamps >= kSteps - 2 && (texel_at_world(300.0F, 0.0F) & 0xFFU) == 0xFFU,
                 text::format("a rider at %.0f: a trail along its path (%llu stamps)", static_cast<double>(kSpeed),
                              static_cast<unsigned long long>(straight.stamps)));
    const std::vector<uint32_t> first = all_texels();
    const std::vector<snow::SprayParticle> first_spray = snow::live_spray();
    // the same session again
    snow::reset();
    ride({0.0F, 0.0F, 0.0F}, kSpeed, 0.0F, kSteps);
    const std::vector<snow::SprayParticle> again_spray = snow::live_spray();
    const bool same_spray = first_spray.size() == again_spray.size() &&
                            std::memcmp(first_spray.data(), again_spray.data(), first_spray.size() * sizeof(snow::SprayParticle)) == 0;
    tally->check(all_texels() == first && same_spray, "the same steps twice: the same trails and spray, byte for byte");
    // kept with nothing pressing (no snow falling)
    for (int step = 0; step < 1000; ++step) {
        snow::step({}, kStep, false);
    }
    tally->check(all_texels() == first, "the trails kept over 1000 steps");
    // carving throws more spray than riding straight
    snow::reset();
    ride({0.0F, 0.0F, 0.0F}, kSpeed, 0.05F, kSteps);
    const snow::Counts carve = snow::counts();
    tally->check(carve.spray > straight.spray && straight.spray > 0,
                 text::format("spray: %llu straight on, %llu carving", static_cast<unsigned long long>(straight.spray),
                              static_cast<unsigned long long>(carve.spray)));
    for (int lap = 0; lap < 40; ++lap) {
        ride({0.0F, 0.0F, 0.0F}, kSpeed * 2.0F, 0.2F, kSteps, 1);
    }
    tally->check(snow::live_spray().size() <= snow::kMostSpray,
                 text::format("the spray bounded: %zu alive (at most %zu)", snow::live_spray().size(), snow::kMostSpray));
    // a landing
    snow::reset();
    for (int step = 0; step < 30; ++step) {
        const snow::Presser high{.id = 2, .at = {0.0F, 100.0F - static_cast<float>(step) * 3.0F, 0.0F}};
        snow::step(std::span(&high, 1), kStep, false);
    }
    const snow::Presser down{.id = 2, .at = {0.0F, 0.0F, 0.0F}};
    snow::step(std::span(&down, 1), kStep, false);
    tally->check(snow::counts().landings == 1 && snow::counts().spray >= 24, "a landing: one burst");
    // walking: a footprint a stride
    snow::reset();
    ride({0.0F, 0.0F, -300.0F}, 100.0F, 0.0F, 6 * 60, 3);
    const uint64_t prints = snow::counts().stamps;
    tally->check(prints >= 20 && prints <= 25, text::format("walking 600 units: %llu footprints", static_cast<unsigned long long>(prints)));
    // a wheel
    snow::reset();
    for (int step = 0; step < 30; ++step) {
        const snow::Presser wheel{.id = 4, .kind = snow::PresserKind::kWheel, .at = {static_cast<float>(step) * 8.0F, 12.0F, 300.0F}};
        snow::step(std::span(&wheel, 1), kStep, false);
    }
    tally->check(snow::counts().stamps >= 28, "a wheel presses its track");
    // a body joint lying in the snow (a bail's slide) presses its path; one up in the air nothing
    snow::reset();
    for (int step = 0; step < 30; ++step) {
        const float x = static_cast<float>(step) * 6.0F;
        const std::array<snow::Presser, 2> joints{{
            {.id = 6, .kind = snow::PresserKind::kBody, .at = {x, 3.0F, -500.0F}, .size = 4.5F},
            {.id = 7, .kind = snow::PresserKind::kBody, .at = {x, 30.0F, -600.0F}, .size = 4.5F},
        }};
        snow::step(joints, kStep, false);
    }
    tally->check(snow::counts().body >= 28 && (texel_at_world(90.0F, -500.0F) & 0xFFU) > 200U && texel_at_world(90.0F, -600.0F) == 0,
                 text::format("a body joint in the snow presses its path (%llu), one in the air nothing",
                              static_cast<unsigned long long>(snow::counts().body)));
    // a mover without wheels found (no bones): two tracks either side of it, nothing between; a still one nothing
    snow::reset();
    for (int step = 0; step < 30; ++step) {
        const std::array<snow::Presser, 2> movers{{
            {.id = 8, .kind = snow::PresserKind::kMover, .at = {static_cast<float>(step) * 8.0F, 2.0F, 600.0F}, .facing = {0.0F, 1.0F},
             .size = 30.0F},
            {.id = 9, .kind = snow::PresserKind::kMover, .at = {0.0F, 2.0F, -900.0F}, .facing = {0.0F, 1.0F}, .size = 30.0F},
        }};
        snow::step(movers, kStep, false);
    }
    tally->check((texel_at_world(120.0F, 630.0F) & 0xFFU) == 0xFFU && (texel_at_world(120.0F, 570.0F) & 0xFFU) == 0xFFU &&
                     (texel_at_world(120.0F, 600.0F) & 0xFFU) == 0U && texel_at_world(0.0F, -870.0F) == 0,
                 "a mover: its two tracks pressed, not between them; a still one nothing");
    // the wind's drift: on the snow's clock, the same every run
    snow::reset();
    for (int step = 0; step < 600; ++step) {
        snow::step({}, kStep, false);
    }
    const double drift = snow::wind_drift();
    snow::reset();
    for (int step = 0; step < 600; ++step) {
        snow::step({}, kStep, false);
    }
    tally->check(drift > 0.5 * 10.0 * render::wind::kSpeed && drift < 1.5 * 10.0 * render::wind::kSpeed && drift == snow::wind_drift(),
                 text::format("the wind carried the snow %.0f units in 10 s, the same again", drift));
    // nothing from high above, nothing where no snow settles
    snow::reset();
    ride({0.0F, 60.0F, 0.0F}, kSpeed, 0.0F, kSteps);
    ride({0.0F, 0.0F, 1200.0F}, kSpeed, 0.0F, kSteps, 5);
    tally->check(snow::counts().stamps == 0, "nothing pressed from above the snow, nor indoors");
    // the filling in while snow falls
    snow::reset();
    ride({0.0F, 0.0F, 0.0F}, kSpeed, 0.0F, kSteps);
    const uint32_t pressed = texel_at_world(300.0F, 0.0F);
    const auto half_steps = static_cast<int>(snow::kFillSeconds * kHalf / kStep);
    for (int step = 0; step < half_steps; ++step) {
        snow::step({}, kStep, true);
    }
    const uint32_t half = texel_at_world(300.0F, 0.0F);
    for (int step = 0; step < half_steps + 2; ++step) {
        snow::step({}, kStep, true);
    }
    const uint32_t full = texel_at_world(300.0F, 0.0F);
    constexpr uint32_t kByte = 0xFFU;
    tally->check((pressed & kByte) == kByte && (half & kByte) >= 125 && (half & kByte) <= 130 && (full & kByte) == 0 &&
                     ((full >> snow::kActiveShift) & kByte) == ((pressed >> snow::kActiveShift) & kByte),
                 text::format("filled in while snow falls: %u, %u after %.0f s, %u after %.0f s (the activity kept)", pressed & kByte,
                              half & kByte, snow::kFillSeconds * kHalf, full & kByte, snow::kFillSeconds));
    snow::reset();
}

// --- the device -----------------------------------------------------------------------------------------------------

struct Axes {
    Vec3 right{1.0F, 0.0F, 0.0F};
    Vec3 up{};
    Vec3 forward{};
};
Axes camera_axes()
{
    return {.right = {1.0F, 0.0F, 0.0F},
            .up = {0.0F, std::cos(kPitch), std::sin(kPitch)},
            .forward = {0.0F, -std::sin(kPitch), std::cos(kPitch)}};
}

Matrix view_matrix(const Vec3& camera)
{
    const Axes axes = camera_axes();
    Matrix view{};
    for (std::size_t row = 0; row < 3; ++row) {
        core::at(core::at(view, row), 0) = core::at(axes.right, row);
        core::at(core::at(view, row), 1) = core::at(axes.up, row);
        core::at(core::at(view, row), 2) = core::at(axes.forward, row);
    }
    core::at(view, 3) = {-dot(axes.right, camera), -dot(axes.up, camera), -dot(axes.forward, camera), 1.0F};
    return view;
}
Matrix projection_matrix(uint32_t width, uint32_t height)
{
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    Matrix projection{};
    core::at(core::at(projection, 0), 0) = kVerticalScale / aspect;
    core::at(core::at(projection, 1), 1) = kVerticalScale;
    core::at(core::at(projection, 2), 2) = kFar / (kFar - kNear);
    core::at(core::at(projection, 2), 3) = 1.0F;
    core::at(core::at(projection, 3), 2) = -kNear * kFar / (kFar - kNear);
    return projection;
}
std::array<float, kMatrixFloats> flat(const Matrix& matrix)
{
    std::array<float, kMatrixFloats> out{};
    for (std::size_t i = 0; i < kMatrixFloats; ++i) {
        core::at(out, i) = core::at(core::at(matrix, i / 4), i % 4);
    }
    return out;
}

struct Frame {
    Vec3 camera = kCamera;
    uint32_t width = kWidth;
    uint32_t height = kHeight;
    bool marked = true; // the ground's draw marked as the ground
    bool wall = true;
};

render::rt::ViewDrawn view_of(const Frame& frame)
{
    render::rt::ViewDrawn view;
    view.rect = {0, 0, frame.width, frame.height};
    view.output_rect = view.rect;
    view.depth_range = {0.0F, 1.0F};
    view.camera.view = view_matrix(frame.camera);
    view.camera.projection = projection_matrix(frame.width, frame.height);
    view.camera.position = frame.camera;
    return view;
}

struct Vertex {
    std::array<float, 3> position;
    uint32_t colour;
    std::array<float, 2> uv;
};

struct Scene {
    GpuSurface* colour = nullptr;
    GpuSurface* depth = nullptr;
    GpuTexture* dark = nullptr;
    render::GpuPixelShader* shader = nullptr;
    uint32_t width = kWidth;
    uint32_t height = kHeight;
};

GpuTexture* uniform_texture(render::Gpu& gpu, float level)
{
    constexpr uint32_t kTexels = 4;
    GpuTexture* texture = nullptr;
    if (gpu.create_texture({.width = kTexels, .height = kTexels, .levels = 1, .usage = 0, .format = value::kFormatA8R8G8B8,
                            .pool = value::kPoolManaged},
                           &texture) != value::kResultOk) {
        return nullptr;
    }
    render::GpuLockedRect locked{};
    gpu.lock_texture(texture, 0, &locked, nullptr, 0);
    const std::span<uint8_t> bytes(static_cast<uint8_t*>(locked.bits),
                                   (static_cast<std::size_t>(locked.pitch) * (kTexels - 1)) + (std::size_t{kTexels} * 4));
    const auto byte = static_cast<uint8_t>(std::lround(level * kByteMax));
    constexpr uint8_t kOpaqueHalf = 128; // (the doubled alpha: 1)
    for (uint32_t row = 0; row < kTexels; ++row) {
        for (uint32_t column = 0; column < kTexels; ++column) {
            const std::size_t at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(locked.pitch)) + (std::size_t{column} * 4);
            core::at(bytes, at) = byte;
            core::at(bytes, at + 1) = byte;
            core::at(bytes, at + 2) = byte;
            core::at(bytes, at + 3) = kOpaqueHalf;
        }
    }
    gpu.unlock_texture(texture, 0);
    return texture;
}

void common_states(render::Gpu& gpu, const Scene& scene, const Frame& frame)
{
    gpu.set_render_state(GpuRenderState::kCullMode, value::kCullNone);
    gpu.set_render_state(GpuRenderState::kAlphaTestEnable, 0);
    gpu.set_render_state(GpuRenderState::kFogEnable, 0);
    gpu.set_render_state(GpuRenderState::kSpecularEnable, 0);
    gpu.set_render_state(GpuRenderState::kLighting, 0);
    gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    gpu.set_render_state(GpuRenderState::kZEnable, 1);
    gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
    gpu.set_render_state(GpuRenderState::kZFunc, value::kCompareLessEqual);
    const std::array<float, kMatrixFloats> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    gpu.set_transform(render::GpuTransform::kWorld, identity.data());
    const std::array<float, kMatrixFloats> view = flat(view_matrix(frame.camera));
    gpu.set_transform(render::GpuTransform::kView, view.data());
    const std::array<float, kMatrixFloats> projection = flat(projection_matrix(frame.width, frame.height));
    gpu.set_transform(render::GpuTransform::kProjection, projection.data());
    for (const GpuSamplerState state : {GpuSamplerState::kMinFilter, GpuSamplerState::kMagFilter}) {
        gpu.set_sampler_state(0, state, value::kFilterLinear);
    }
    gpu.set_sampler_state(0, GpuSamplerState::kMipFilter, value::kFilterNone);
    gpu.set_stage_state(0, GpuStageState::kTexCoordIndex, 0);
    gpu.set_pixel_shader(scene.shader);
    const std::array<float, 4> c0{0.5F, 0.5F, 0.5F, 0.5F};
    gpu.set_pixel_constants(0, c0.data(), 1);
    gpu.set_vertex_format(kFvfPosition | kFvfDiffuse | kFvfTex1);
    gpu.set_texture(0, scene.dark);
}

// One frame: the view begun, the ground (and the wall) drawn - marked as the ground or not -, the view's world drawn.
void draw_frame(render::Gpu& gpu, const Scene& scene, const Frame& frame)
{
    const render::rt::ViewDrawn view = view_of(frame);
    gpu.begin_scene();
    gpu.set_render_target(0, scene.colour);
    gpu.set_depth_surface(scene.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = frame.width, .height = frame.height, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth, kSkyColour, 1.0F, 0);
    render::rt::view_begins(view);
    common_states(gpu, scene, frame);
    const auto at = [](float x, float y, float z) { return Vertex{.position = {x, y, z}, .colour = kHalfGrey, .uv = {x / 64.0F, z / 64.0F}}; };
    constexpr float kNearZ = -60.0F;
    const float x0 = core::at(frame.camera, 0) - kGround;
    const float x1 = core::at(frame.camera, 0) + kGround;
    const float z0 = core::at(frame.camera, 2) + kNearZ;
    const float z1 = core::at(frame.camera, 2) + kGround;
    const std::array<Vertex, kQuadCorners> ground{at(x0, 0.0F, z0), at(x1, 0.0F, z0), at(x0, 0.0F, z1),
                                                  at(x1, 0.0F, z0), at(x1, 0.0F, z1), at(x0, 0.0F, z1)};
    render::surface_trail::set_current_ground(frame.marked);
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, 2, ground.data(), sizeof(Vertex));
    if (frame.wall) {
        const std::array<Vertex, kQuadCorners> wall{at(-kWallSide, 0.0F, kWallZ), at(kWallSide, 0.0F, kWallZ), at(-kWallSide, kWallTop, kWallZ),
                                                    at(kWallSide, 0.0F, kWallZ), at(kWallSide, kWallTop, kWallZ), at(-kWallSide, kWallTop, kWallZ)};
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, 2, wall.data(), sizeof(Vertex));
    }
    render::surface_trail::set_current_ground(false);
    render::rt::opaque_view_drawn(view);
    render::rt::world_view_drawn(view);
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    gpu.end_scene();
}

using Picture = std::vector<Vec3>;

Picture read_picture(GpuSurface* target, uint32_t width = kWidth, uint32_t height = kHeight)
{
    std::vector<uint8_t> bytes;
    Picture picture;
    if (!vk::read_surface(target, &bytes) || bytes.size() < static_cast<std::size_t>(width) * height * kHalfPixelBytes) {
        return picture;
    }
    picture.resize(static_cast<std::size_t>(width) * height);
    for (std::size_t pixel = 0; pixel < picture.size(); ++pixel) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const std::size_t first = (pixel * kHalfPixelBytes) + (channel * 2);
            core::at(core::at(picture, pixel), channel) =
                from_half(static_cast<uint16_t>(core::at(bytes, first) | (core::at(bytes, first + 1) << kHighByte)));
        }
    }
    return picture;
}

// Where a pixel's ray meets the ground (y = 0); false when it doesn't (the sky).
bool ground_of(uint32_t px, uint32_t py, std::array<float, 2>* xz, float* distance = nullptr)
{
    const Axes axes = camera_axes();
    const float ndc_x = ((2.0F * (static_cast<float>(px) + kHalf)) / static_cast<float>(kWidth)) - 1.0F;
    const float ndc_y = 1.0F - ((2.0F * (static_cast<float>(py) + kHalf)) / static_cast<float>(kHeight));
    const float aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
    Vec3 direction{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        core::at(direction, axis) = (core::at(axes.right, axis) * ndc_x * aspect / kVerticalScale) +
                                    (core::at(axes.up, axis) * ndc_y / kVerticalScale) + core::at(axes.forward, axis);
    }
    if (core::at(direction, 1) >= 0.0F) {
        return false;
    }
    const float t = -core::at(kCamera, 1) / core::at(direction, 1);
    *xz = {core::at(kCamera, 0) + (t * core::at(direction, 0)), core::at(kCamera, 2) + (t * core::at(direction, 2))};
    if (distance != nullptr) {
        *distance = t * std::sqrt(dot(direction, direction));
    }
    return true;
}

// (looking at a case: with THUG2_SNOW_CHECK_DUMP set to a folder, a picture written there as <name>.rgbf - the 320 x 240
// pixels' red, green and blue floats)
void dump_picture(const Picture& picture, const char* name)
{
    const char* folder = SDL_getenv("THUG2_SNOW_CHECK_DUMP");
    if (folder == nullptr || picture.empty()) {
        return;
    }
    std::ofstream out(std::string(folder) + "/" + name + ".rgbf", std::ios::binary);
    const void* const bytes = picture.data();
    out.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(picture.size() * sizeof(Vec3)));
}

bool finite_picture(const Picture& picture)
{
    return std::ranges::all_of(picture, [](const Vec3& c) {
        return std::isfinite(core::at(c, 0)) && std::isfinite(core::at(c, 1)) && std::isfinite(core::at(c, 2));
    });
}

void set_level(const std::vector<snow::Face>& faces)
{
    snow::set_sky_map(std::make_shared<snow::SkyMap>(snow::make_sky_map(faces)));
}

constexpr vk::SnowSettings kOn{.on = true, .depth = 6.0F, .quality = 2, .falling = false};

void check_window(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    vk::set_snow_settings(kOn);
    set_level(level_faces());
    // a trail near the camera, one far off along +x
    ride({-200.0F, 0.0F, 100.0F}, 600.0F, 0.0F, 40, 11);
    ride({1500.0F, 0.0F, 100.0F}, 600.0F, 0.0F, 40, 12);
    const auto compare = [&](const char* what) {
        std::vector<uint32_t> texels;
        uint32_t side = 0;
        {
            vk::Backend& state = vk::backend();
            const std::scoped_lock lock(state.mutex);
            vk::read_snow_window(state, &texels, &side);
        }
        const vk::SnowStats stats = vk::snow_stats();
        const int first_x = core::at(stats.window, 0) * snow::kTileTexels;
        const int first_z = core::at(stats.window, 1) * snow::kTileTexels;
        const auto n = static_cast<int>(side);
        std::size_t wrong = 0;
        std::size_t pressed = 0;
        snow::read_trails([&](const snow::TrailField& field) {
            for (int z = first_z; z < first_z + n; ++z) {
                for (int x = first_x; x < first_x + n; ++x) {
                    const auto slot = (static_cast<std::size_t>(((z % n) + n) % n) * side) + static_cast<std::size_t>(((x % n) + n) % n);
                    const uint32_t expected = field.texel_at(x, z);
                    wrong += core::at(texels, slot) == expected ? 0U : 1U;
                    pressed += (expected & 0xFFU) != 0 ? 1U : 0U;
                }
            }
        });
        tally->check(side > 0 && wrong == 0 && pressed > 0,
                     text::format("%s: the window's %ux%u texels as the trails hold them (%zu wrong, %zu pressed)", what, side, side,
                                  wrong, pressed));
    };
    draw_frame(gpu, scene, {});
    compare("the window round the camera");
    const uint64_t copied = vk::snow_stats().tiles_copied;
    const float tile_units = snow::trail_texel(kOn.quality) * static_cast<float>(snow::kTileTexels);
    draw_frame(gpu, scene, {.camera = {7.0F * tile_units, core::at(kCamera, 1), core::at(kCamera, 2)}});
    compare("after the camera moved 7 tiles");
    const uint64_t moved_copies = vk::snow_stats().tiles_copied - copied;
    draw_frame(gpu, scene, {.camera = {7.0F * tile_units, core::at(kCamera, 1), core::at(kCamera, 2)}});
    tally->check(vk::snow_stats().tiles_copied - copied == moved_copies && moved_copies <= 7U * 10U,
                 text::format("only the tiles that came in were copied (%llu), nothing again the next frame",
                              static_cast<unsigned long long>(moved_copies)));
    snow::reset();
}

void check_cover(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    vk::set_snow_settings({});
    draw_frame(gpu, scene, {});
    const Picture off = read_picture(scene.colour);
    vk::set_snow_settings(kOn);
    set_level(level_faces());
    draw_frame(gpu, scene, {});
    const Picture on = read_picture(scene.colour);
    draw_frame(gpu, scene, {.marked = false});
    const Picture unmarked = read_picture(scene.colour);
    std::size_t open = 0;
    std::size_t open_white = 0;
    std::size_t roofed = 0;
    std::size_t roofed_same = 0;
    for (uint32_t py = 0; py < kHeight; ++py) {
        for (uint32_t px = 0; px < kWidth; ++px) {
            std::array<float, 2> xz{};
            const std::size_t pixel = (static_cast<std::size_t>(py) * kWidth) + px;
            constexpr float kAwayFromKerb = 550.0F; // (the kerb's block is in the collision alone: the ground drawn under it)
            if (!ground_of(px, py, &xz) || core::at(xz, 1) > kWallZ - 40.0F || std::abs(core::at(xz, 0)) > kAwayFromKerb) {
                continue;
            }
            const float x = core::at(xz, 0);
            const float z = core::at(xz, 1);
            const bool under = std::abs(x) < kRoofSide - 12.0F && z > kRoofLow + 12.0F && z < kRoofHigh - 12.0F;
            const bool clear = std::abs(x) > kRoofSide + 12.0F || z < kRoofLow - 12.0F || z > kRoofHigh + 12.0F;
            const float before = luma(core::at(off, pixel));
            const float after = luma(core::at(on, pixel));
            if (under) {
                ++roofed;
                roofed_same += std::abs(after - before) < 0.01F ? 1U : 0U;
            } else if (clear) {
                ++open;
                open_white += after > before + 0.4F ? 1U : 0U;
            }
        }
    }
    tally->check(open > 1000 && open_white == open,
                 text::format("the open ground turned white: %zu of %zu pixels", open_white, open));
    tally->check(roofed > 100 && roofed_same == roofed,
                 text::format("under the roof: unchanged, %zu of %zu pixels", roofed_same, roofed));
    tally->check(unmarked == off, "a draw not marked as the ground: no snow");
    // the wall's middle (facing the camera): its own colour
    std::size_t wall = 0;
    std::size_t wall_same = 0;
    for (uint32_t py = 0; py < kHeight; ++py) {
        for (uint32_t px = 0; px < kWidth; ++px) {
            std::array<float, 2> xz{};
            float distance = 0.0F;
            if (ground_of(px, py, &xz, &distance) && core::at(xz, 1) > kWallZ + 30.0F && std::abs(core::at(xz, 0)) < kWallSide * 0.3F) {
                // the ray passes the wall's plane before the ground: the wall is what it shows (well inside it)
                const std::size_t pixel = (static_cast<std::size_t>(py) * kWidth) + px;
                const float height_at_wall = core::at(kCamera, 1) * (1.0F - ((kWallZ - core::at(kCamera, 2)) / (core::at(xz, 1) - core::at(kCamera, 2))));
                if (height_at_wall > 10.0F && height_at_wall < kWallTop - 10.0F) {
                    ++wall;
                    wall_same += std::abs(luma(core::at(on, pixel)) - luma(core::at(off, pixel))) < 0.01F ? 1U : 0U;
                }
            }
        }
    }
    tally->check(wall >= 10 && wall_same == wall, text::format("the wall: no snow, %zu of %zu pixels", wall_same, wall));
    vk::set_snow_settings({});
    draw_frame(gpu, scene, {});
    tally->check(read_picture(scene.colour) == off, "snow off: the picture as before");
    tally->check(finite_picture(on), "every pixel finite");
}

void check_layer(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    vk::set_snow_settings({.on = true, .depth = 2.0F, .quality = 2, .falling = false}); // (a shallow trail: its floor seen past the near berm)
    std::vector<snow::Face> faces;
    add_flat(&faces, {-kGround, kGround, -kGround, kGround}, 0.0F);
    set_level(faces);
    for (int frame = 0; frame < 3; ++frame) {
        draw_frame(gpu, scene, {.wall = false});
    }
    const Picture untouched = read_picture(scene.colour);
    ride({-kTrailHalf, 0.0F, kTrailZ}, 600.0F, 0.0F, static_cast<int>(2.0F * kTrailHalf / 10.0F) + 1, 21);
    for (int step = 0; step < 180; ++step) { // (the spray settled: the layer alone)
        snow::step({}, kStep, false);
    }
    for (int frame = 0; frame < 3; ++frame) {
        draw_frame(gpu, scene, {.wall = false});
    }
    const Picture trail = read_picture(scene.colour);
    dump_picture(trail, "layer");
    std::size_t distant = 0;
    std::size_t distant_same = 0;
    std::size_t middle = 0;
    std::size_t middle_darker = 0;
    std::size_t changed = 0;
    float beside_sum = 0.0F;
    float middle_sum = 0.0F;
    std::size_t beside = 0;
    for (uint32_t py = 0; py < kHeight; ++py) {
        for (uint32_t px = 0; px < kWidth; ++px) {
            std::array<float, 2> xz{};
            if (!ground_of(px, py, &xz)) {
                continue;
            }
            const std::size_t pixel = (static_cast<std::size_t>(py) * kWidth) + px;
            const float x = core::at(xz, 0);
            const float dz = std::abs(core::at(xz, 1) - kTrailZ);
            const float reach = snow::kBoardHalfWidth + snow::kBermWidth + snow::kActiveReach;
            const float before = luma(core::at(untouched, pixel));
            const float after = luma(core::at(trail, pixel));
            changed += std::abs(after - before) > 0.005F ? 1U : 0U;
            if (dz > reach + 30.0F || std::abs(x) > kTrailHalf + reach + 30.0F) {
                ++distant;
                distant_same += std::abs(after - before) < 0.002F ? 1U : 0U;
            } else if (std::abs(x) < kTrailHalf - 25.0F && dz < 1.0F) {
                ++middle;
                middle_sum += after;
                middle_darker += after < before * 0.95F ? 1U : 0U;
            } else if (std::abs(x) < kTrailHalf - 25.0F && dz > 20.0F && dz < 24.0F) {
                ++beside;
                beside_sum += after;
            }
        }
    }
    tally->check(distant > 1000 && distant_same == distant, text::format("far from the trail: unchanged, %zu of %zu pixels", distant_same, distant));
    tally->check(changed > 100, text::format("the trail drawn: %zu pixels changed", changed));
    tally->check(middle > 5 && middle_darker == middle && beside > 5 &&
                     middle_sum / static_cast<float>(middle) < beside_sum / static_cast<float>(beside),
                 text::format("the packed snow in the trail darker than the snow beside it (%zu of %zu pixels darker; %.3f "
                              "against %.3f)",
                              middle_darker, middle, static_cast<double>(middle_sum / static_cast<float>(std::max<std::size_t>(middle, 1))),
                              static_cast<double>(beside_sum / static_cast<float>(std::max<std::size_t>(beside, 1)))));
    tally->check(finite_picture(trail), "every pixel finite");
    snow::reset();
}

void check_air(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    vk::set_snow_settings(kOn);
    std::vector<snow::Face> faces;
    add_flat(&faces, {-kGround, kGround, -kGround, kGround}, 0.0F);
    set_level(faces);
    // (the ground drawn unmarked - dark: white flakes show on it)
    const uint64_t passes = vk::snow_stats().air_passes;
    draw_frame(gpu, scene, {.marked = false, .wall = false});
    const Picture still = read_picture(scene.colour);
    tally->check(vk::snow_stats().air_passes == passes, "snowfall off, no spray: no air pass");
    vk::set_snow_settings({.on = true, .depth = 6.0F, .quality = 2, .falling = true});
    set_level(faces);
    for (int step = 0; step < 10; ++step) {
        snow::step({}, kStep, true);
    }
    draw_frame(gpu, scene, {.marked = false, .wall = false});
    const Picture falling = read_picture(scene.colour);
    std::size_t flake_pixels = 0;
    for (std::size_t pixel = 0; pixel < still.size() && pixel < falling.size(); ++pixel) {
        flake_pixels += luma(core::at(falling, pixel)) > luma(core::at(still, pixel)) + 0.01F ? 1U : 0U;
    }
    tally->check(flake_pixels > 20 && vk::snow_stats().last_flakes == vk::kSnowMostFlakes,
                 text::format("snow falling: %zu pixels lighter (%u flakes)", flake_pixels, vk::snow_stats().last_flakes));
    // the haze: the far ground whitened, the near hardly
    double near_sum = 0.0;
    double far_sum = 0.0;
    std::size_t near_count = 0;
    std::size_t far_count = 0;
    for (uint32_t py = 0; py < kHeight; ++py) {
        for (uint32_t px = 0; px < kWidth; ++px) {
            std::array<float, 2> xz{};
            float distance = 0.0F;
            if (!ground_of(px, py, &xz, &distance)) {
                continue;
            }
            const std::size_t pixel = (static_cast<std::size_t>(py) * kWidth) + px;
            const double lighter = static_cast<double>(luma(core::at(falling, pixel)) - luma(core::at(still, pixel)));
            if (distance < vk::kSnowHazeStart * 0.6F) {
                near_sum += lighter;
                ++near_count;
            } else if (distance > vk::kSnowHazeStart * 2.0F) { // (the view's farthest ground is about 700 units off)
                far_sum += lighter;
                ++far_count;
            }
        }
    }
    const double near_mean = near_sum / static_cast<double>(std::max<std::size_t>(near_count, 1));
    const double far_mean = far_sum / static_cast<double>(std::max<std::size_t>(far_count, 1));
    tally->check(vk::snow_stats().haze_passes > 0 && near_count > 100 && far_count > 100 && far_mean > near_mean + 0.02,
                 text::format("the falling snow's haze: the far ground %.3f lighter, the near %.3f (%llu passes)", far_mean, near_mean,
                              static_cast<unsigned long long>(vk::snow_stats().haze_passes)));
    for (int lap = 0; lap < 4; ++lap) {
        ride({0.0F, 0.0F, 150.0F}, 900.0F, 0.15F, 60, 31);
    }
    draw_frame(gpu, scene, {.wall = false});
    const Picture spraying = read_picture(scene.colour);
    const std::size_t alive = snow::live_spray().size();
    tally->check(vk::snow_stats().last_spray == alive && alive > 0 && alive <= snow::kMostSpray,
                 text::format("the spray drawn: %u particles (%zu alive)", vk::snow_stats().last_spray, alive));
    tally->check(finite_picture(falling) && finite_picture(spraying), "every pixel finite");
    snow::reset();
}

void measure_cost(render::Gpu& gpu, Scene scene, Tally* tally)
{
    constexpr uint32_t kBigWidth = 2560;
    constexpr uint32_t kBigHeight = 1440;
    constexpr int kTimed = 20;
    constexpr double kNanosecondsPerMillisecond = 1.0e6;
    vk::Backend& state = vk::backend();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    if (properties.limits.timestampComputeAndGraphics == VK_FALSE) {
        text::print("the cost: no timestamps on this device\n");
        return;
    }
    VkQueryPool pool = VK_NULL_HANDLE;
    const VkQueryPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .pNext = nullptr, .flags = 0,
                                     .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2, .pipelineStatistics = 0};
    if (vkCreateQueryPool(state.context.device(), &info, nullptr, &pool) != VK_SUCCESS) {
        return;
    }
    scene.width = kBigWidth;
    scene.height = kBigHeight;
    const bool made =
        gpu.create_render_target({.width = kBigWidth, .height = kBigHeight, .format = value::kFormatA16B16G16R16F, .samples = 0,
                                  .quality = 0, .lockable_or_discard = true},
                                 &scene.colour) == value::kResultOk &&
        gpu.create_depth_surface({.width = kBigWidth, .height = kBigHeight, .format = value::kFormatD24S8, .samples = 0, .quality = 0,
                                  .lockable_or_discard = false},
                                 &scene.depth) == value::kResultOk;
    if (made) {
        vk::set_snow_settings({.on = true, .depth = 4.0F, .quality = 2, .falling = true});
        std::vector<snow::Face> faces;
        add_flat(&faces, {-kGround, kGround, -kGround, kGround}, 0.0F);
        set_level(faces);
        // the window full of trails: lines every 40 units across it, a carve's spray
        for (int line = 0; line < 30; ++line) {
            ride({-600.0F, 0.0F, -300.0F + (40.0F * static_cast<float>(line))}, 900.0F, 0.0F, 80, 100 + static_cast<uint64_t>(line));
        }
        ride({0.0F, 0.0F, 200.0F}, 900.0F, 0.2F, 120, 200);
        const Frame frame{.width = kBigWidth, .height = kBigHeight, .wall = false};
        for (int warm = 0; warm < 3; ++warm) {
            draw_frame(gpu, scene, frame);
        }
        const render::rt::ViewDrawn view = view_of(frame);
        gpu.begin_scene();
        gpu.set_render_target(0, scene.colour);
        gpu.set_depth_surface(scene.depth);
        gpu.set_viewport({.x = 0, .y = 0, .width = kBigWidth, .height = kBigHeight, .min_z = 0.0F, .max_z = 1.0F});
        render::rt::view_begins(view);
        {
            const std::scoped_lock lock(state.mutex);
            VkCommandBuffer commands = vk::outside_pass(state);
            vkCmdResetQueryPool(commands, pool, 0, 2);
            vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool, 0);
            for (int i = 0; i < kTimed; ++i) {
                vk::record_snow_layer(state, view);
                vk::record_snow_air(state, view);
            }
        }
        std::array<uint64_t, 2> stamps{};
        {
            const std::scoped_lock lock(state.mutex);
            vkCmdWriteTimestamp(vk::outside_pass(state), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool, 1);
            vk::finish_work(state);
            vkGetQueryPoolResults(state.context.device(), pool, 0, 2, sizeof(stamps), stamps.data(), sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        }
        gpu.set_render_target(0, nullptr);
        gpu.set_depth_surface(nullptr);
        gpu.end_scene();
        const double each = static_cast<double>(core::at(stamps, 1) - core::at(stamps, 0)) *
                            static_cast<double>(properties.limits.timestampPeriod) / kNanosecondsPerMillisecond / kTimed;
        const vk::SnowStats stats = vk::snow_stats();
        text::print("the cost: %.3f ms a frame at %ux%u (the layer: %u tiles; the air: %u flakes, %u spray particles)\n", each,
                    kBigWidth, kBigHeight, stats.last_tiles, stats.last_flakes, stats.last_spray);
        tally->check(std::isfinite(each) && each > 0.0, text::format("timed at %ux%u", kBigWidth, kBigHeight));
        snow::reset();
    }
    for (GpuSurface* surface : {scene.colour, scene.depth}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
    vkDestroyQueryPool(state.context.device(), pool, nullptr);
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

bool open_device(const Options& options, SDL_Window* window)
{
    vk::set_context_options({.validation = options.validation, .device_index = options.device, .on_message = print_line}, window);
    render::GpuContext& context = vk::gpu_context();
    if (!context.create_instance()) {
        text::print("the device could not be opened\n");
        return false;
    }
    const vk::Backend& state = vk::backend();
    text::print("validation layer: %s\n", state.context.validation_on() ? "on" : "off (not installed, or not asked for)");
    context.configure({.width = kWindowSize,
                       .height = kWindowSize,
                       .windowed = true,
                       .vsync = true,
                       .samples = 0,
                       .sample_quality = 0,
                       .colour_format = value::kFormatX8R8G8B8,
                       .depth_format = value::kFormatD24S8});
    const long made =
        context.create_device({.width = kWindowSize, .height = kWindowSize, .hardware_vertices = true, .samples = 0, .sample_quality = 0});
    if (made != value::kResultOk) {
        text::print("create_device failed (%ld)\n", made);
        context.shutdown();
        return false;
    }
    context.mark_ready();
    text::print("device: %s\n", context.adapter().description.data());
    return true;
}

int run(const Options& options, SDL_Window* window)
{
    Tally tally;
    check_sky_map(&tally);
    check_slope(&tally);
    check_overcast(&tally);
    check_stamp(&tally);
    check_steps(&tally);
    if (!open_device(options, window)) {
        return vk::loader_version() == 0 ? 2 : 1;
    }
    render::Gpu& gpu = vk::gpu();
    Scene scene;
    constexpr float kDark = 0.2F;
    const bool made =
        gpu.create_pixel_shader(kShader.data(), static_cast<uint32_t>(kShader.size()), &scene.shader) == value::kResultOk &&
        gpu.create_render_target({.width = kWidth, .height = kHeight, .format = value::kFormatA16B16G16R16F, .samples = 0, .quality = 0,
                                  .lockable_or_discard = true},
                                 &scene.colour) == value::kResultOk &&
        gpu.create_depth_surface({.width = kWidth, .height = kHeight, .format = value::kFormatD24S8, .samples = 0, .quality = 0,
                                  .lockable_or_discard = false},
                                 &scene.depth) == value::kResultOk;
    scene.dark = uniform_texture(gpu, kDark);
    tally.check(made && scene.dark != nullptr, "the material shader, a 16-bit float target, a depth surface and the texture made");
    if (made && scene.dark != nullptr) {
        check_window(gpu, scene, &tally);
        check_cover(gpu, scene, &tally);
        check_layer(gpu, scene, &tally);
        check_air(gpu, scene, &tally);
        measure_cost(gpu, scene, &tally);
    }
    vk::set_snow_settings({});
    for (GpuSurface* surface : {scene.colour, scene.depth}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
    vk::gpu_context().shutdown();
    tally.check(validation_messages() == 0, text::format("validation messages: %d", validation_messages()));
    text::print("%d checks passed, %d failed\n", tally.passed, tally.failed);
    return tally.failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_snow_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan snow check", kWindowSize, kWindowSize, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
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
