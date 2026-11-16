// NOT ORIGINAL - thug2_vulkan_rt_fog_check.exe: the Vulkan Gpu's volumetric fog and light shafts
// (gpu_vulkan/vulkan_rt_fog.h) tried on this machine against references worked out on the processor.
//
// A wall with a window in it, standing on a ground square, the sun low behind it (world, instance mask 0x01): the light
// comes through the window as a shaft into an even fog (the level's linear fog from kFogStart to kFogEnd, no height
// falloff). The camera looks along the wall's shadowed side, across the shaft. The scene is drawn through render::Gpu as
// render_pc draws a level; its acceleration structures are built here and handed over with rt_fog_set_scene;
// rt::world_view_drawn runs the passes through the listener, as draw_viewport does. The processor answers each pixel
// from the camera's own position and lens (not the matrices): the surface its ray hits, and the fog along it.
//   - One frame (the march as it is): every low-resolution texel's distance against the processor's; its transmittance
//     against exp(-integral) of the extinction - the game's own (end - d) / (end - start); its light against the
//     processor's march of the same pixel with the same jitter and steps (the shaft's edges may land on either side of a
//     sample: a share of texels may differ by up to one step's light); no NaN.
//   - The shaft: texels whose rays cross it are lighter than those that don't, by about what the processor's exact
//     integral says.
//   - A still camera: after kStillFrames frames the history against the exact integral (2048 steps): nearer than one
//     frame's march (the temporal accumulation converges), mean and 95th-percentile error bounded; the colour target
//     the composite: surface x T + light within a few levels where the pixel's neighbours are on its surface.
//   - A moved camera: no NaN, the error after it bounded (no ghost).
//   - Shafts mode (rt_fog_mode=1): T = 1 everywhere, pixels whose rays never cross sunlit fog unchanged.
//   - The sky: never dimmed; lighter only by the shafts' share.
//   - No fog in the level: with rt_fog_haze=0 no pass, nothing changed; with the haze nothing darker, the shaft
//     lighter. No scene, rt_fog off: no pass, nothing changed.
//   - (added by the volumetric light) The clock's light (rt_daylight.h DayLight through rt_volume_light.h
//     medium_light): with the clock lighting the level its light's direction lights the air - after still frames the
//     history against the exact integral for that direction, and against the other direction's worse (the shaft moved
//     with it); the fog noted that sun. At night the moon lights the haze's shafts: their light the moon's colour's
//     share of the sun's, bluer. (A lamp's cone in the air: thug2_vulkan_rt_lights_check; the clouds' shadow map:
//     thug2_vulkan_rt_sky_check.)
//   - (added by the street air) The street air (rt_volume_light.h street_air): its model's numbers - a dusk street's
//     optical depth across 20 metres enough for shafts, a 300-metre view's not whited out, the day's thinner; the
//     march at a quarter's resolution (4 x 4 blocks) against the processor's; then a street - two rows of buildings, an
//     alley through the near row, the sun 4 degrees up behind it at dusk - in nothing but the street air: after still
//     frames the history against the exact integral, and the beam through the alley lighter than the shade by a
//     measurable contrast. (The lamps' cones in the street air at night: thug2_vulkan_rt_lights_check.)
//   - (added by the cinematic air) The air's looks (rt_volume_light.h AirLook): the model's numbers per look - the
//     cinematic street's beam and a lamp's cone seen from the side several times the clear air's, the far view along a
//     street not whited out by any but Misty, noon thin -; the street seen from the side (the beam crossing the view at
//     a right angle to the light) at the cinematic step: the history against the exact integral and the beam well over
//     the shade - and the clear air's side view faint. A level the relit mode doesn't relight: no light in the air.
//   - (added by the distinct shafts) A bar across the alley's mouth: its thin shadow through the beam, seen edge-on from
//     the side at Medium's resolution (the air's sun map read at every step) - the line's width and depth against the
//     exact integral's. (The processor's march mirrors the pass's: every step's light inside the map's square.)
//   - --perf: the pass's cost at 2560 x 1440 at rt_fog_quality 0 / 1 / 2 (the device's timestamps), Medium held to a
//     bound.
//   thug2_vulkan_rt_fog_check [--device N] [--display N] [--no-validation] [--perf]
// Exit code: 0 when every check passed, 1 when one failed, 2 when this machine has no Vulkan ray queries for this process.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
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
#include "base/maths/view_transforms.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_volume_light.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_rt_fog.h"
#include "tests/check_window.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuSurface;
namespace value = render::gpu_value;
using Vec3 = std::array<double, 3>;

constexpr int kWindowWidth = 320;
constexpr int kWindowHeight = 240;
constexpr uint32_t kTargetWidth = 320;
constexpr uint32_t kTargetHeight = 240;
constexpr uint32_t kPerfWidth = 2560;
constexpr uint32_t kPerfHeight = 1440;
constexpr int kBitsPerByte = 8;
constexpr uint32_t kFvf = 0x042; // XYZ | DIFFUSE
constexpr uint32_t kClearColour = 0xFF405878;
constexpr float kNearZ = 2.0F;
constexpr float kFarZ = 2000.0F;
constexpr double kHalfFovTan = 0.57735026918962576; // tan 30 degrees: a 60-degree horizontal field of view
constexpr double kNoHit = std::numeric_limits<double>::infinity();
constexpr std::size_t kNoSurface = 0;
constexpr uint32_t kByteMask = 0xFF;
constexpr uint32_t kRedShift = 16;
constexpr uint32_t kGreenShift = 8;
constexpr std::size_t kPixelBytes = 4;
constexpr float kFogStart = 10.0F;
constexpr float kFogEnd = 500.0F;
constexpr std::array<float, 3> kFogColour{0.6F, 0.65F, 0.7F};
constexpr float kAnisotropy = 0.3F;
constexpr unsigned kSteps = 32;
constexpr int kStillFrames = 40;
// (the first frame of a history seeds it with kFogMaxHistory marches - vulkan_rt_history.h: its last march, the one the
// raw image holds, is frame index kFogMaxHistory - 1, and every frame after counts on from there)
constexpr uint64_t kFirstMarch = render::vulkan::kFogMaxHistory - 1;
constexpr double kPerByte = 1.0 / 255.0;
// (changed by the street air) the march's block (2: the exact cases; the quarter-resolution case 4)
uint32_t& march_scale()
{
    static uint32_t scale = 2;
    return scale;
}

struct Options {
    int display = 0;
    int device = -1;
    bool validation = true;
    bool perf = false;
};

Options read_options(std::span<char*> arguments)
{
    Options options;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string_view argument = core::at(arguments, i);
        const bool has_value = i + 1 < arguments.size();
        if (argument == "--no-validation") {
            options.validation = false;
        } else if (argument == "--perf") {
            options.perf = true;
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

// --- the scene ---------------------------------------------------------------------------------------------------

struct Solid {
    Vec3 low{};
    Vec3 high{};
    uint32_t colour = 0;
};

constexpr double kGroundHalf = 300.0;
constexpr uint32_t kMaskWorld = 0x01;
// (added by the street air) the street, 30000 units (760 m) from the window's scene - neither sees nor shades the other:
// a ground square, the near row of buildings (25 m high, 30 m deep) with an alley 10 m wide through it at x -200 - 200;
// the street 50 m wide north of it (z kStreetZ - kStreetZ + 2000)
constexpr double kStreetZ = -30000.0;
constexpr double kStreetHalf = 4000.0;
constexpr double kStreetHigh = 1000.0;
constexpr double kStreetDeep = 1200.0;
constexpr double kAlleyHalf = 200.0;
// (added by the distinct shafts) a bar across the alley's mouth (x -200 - 200), 12 units (0.3 m) square, 300 units up:
// its shadow a thin sheet through the beam - seen edge-on from the side, a dark line across it
constexpr double kBarLow = 300.0;
constexpr double kBarSide = 12.0;
constexpr double kBarZ = kStreetZ - 40.0;
// the ground, then the wall (z -2 - 2) round a window x -20 - 20, y 60 - 100; then the street's (and its bar)
const std::array<Solid, 9> kSolids{{
    {.low = {-kGroundHalf, 0.0, -kGroundHalf}, .high = {kGroundHalf, 0.0, kGroundHalf}, .colour = 0xFF909090},
    {.low = {-150.0, 0.0, -2.0}, .high = {-20.0, 150.0, 2.0}, .colour = 0xFFA08060},
    {.low = {20.0, 0.0, -2.0}, .high = {150.0, 150.0, 2.0}, .colour = 0xFFA08060},
    {.low = {-20.0, 0.0, -2.0}, .high = {20.0, 60.0, 2.0}, .colour = 0xFFA08060},
    {.low = {-20.0, 100.0, -2.0}, .high = {20.0, 150.0, 2.0}, .colour = 0xFFA08060},
    {.low = {-kStreetHalf, 0.0, kStreetZ - kStreetHalf},
     .high = {kStreetHalf, 0.0, kStreetZ + kStreetHalf},
     .colour = 0xFF606060},
    {.low = {-kStreetHalf, 0.0, kStreetZ - kStreetDeep}, .high = {-kAlleyHalf, kStreetHigh, kStreetZ}, .colour = 0xFF706050},
    {.low = {kAlleyHalf, 0.0, kStreetZ - kStreetDeep}, .high = {kStreetHalf, kStreetHigh, kStreetZ}, .colour = 0xFF706050},
    {.low = {-kAlleyHalf, kBarLow, kBarZ}, .high = {kAlleyHalf, kBarLow + kBarSide, kBarZ + kBarSide}, .colour = 0xFF404040},
}};

constexpr Vec3 kLightTravels{0.15, -0.45, 1.0}; // (the sun behind the wall, low: the shaft goes down into +z)
constexpr Vec3 kEye{160.0, 70.0, 150.0};
constexpr Vec3 kMovedEye{140.0, 80.0, 175.0};
constexpr Vec3 kAt{0.0, 40.0, 60.0};
constexpr Vec3 kUp{0.0, 1.0, 0.0};
// (added by the street air) where the camera looks and how far it sees: the window's scene's, or the street's
Vec3& look_at()
{
    static Vec3 at = kAt;
    return at;
}
float& far_z()
{
    static float reach = kFarZ;
    return reach;
}

double x_of(const Vec3& v) { return core::at(v, 0); }
double y_of(const Vec3& v) { return core::at(v, 1); }
double z_of(const Vec3& v) { return core::at(v, 2); }

Vec3 normalised(const Vec3& v)
{
    const double length = std::hypot(x_of(v), y_of(v), z_of(v));
    return {x_of(v) / length, y_of(v) / length, z_of(v) / length};
}
double dot(const Vec3& one, const Vec3& other)
{
    return (x_of(one) * x_of(other)) + (y_of(one) * y_of(other)) + (z_of(one) * z_of(other));
}
Vec3 cross(const Vec3& one, const Vec3& other)
{
    return {(y_of(one) * z_of(other)) - (z_of(one) * y_of(other)),
            (z_of(one) * x_of(other)) - (x_of(one) * z_of(other)),
            (x_of(one) * y_of(other)) - (y_of(one) * x_of(other))};
}
Vec3 step(const Vec3& from, const Vec3& along, double scale)
{
    return {x_of(from) + (x_of(along) * scale), y_of(from) + (y_of(along) * scale), z_of(from) + (z_of(along) * scale)};
}
Vec3 minus(const Vec3& one, const Vec3& other) { return step(one, other, -1.0); }
std::array<float, 3> floats(const Vec3& v)
{
    return {static_cast<float>(x_of(v)), static_cast<float>(y_of(v)), static_cast<float>(z_of(v))};
}

using Corner = std::array<int, 3>;
std::vector<std::array<float, 3>> triangles_of(const Solid& solid)
{
    const auto corner = [&solid](const Corner& at) {
        std::array<float, 3> out{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            core::at(out, axis) =
                static_cast<float>(core::at(at, axis) != 0 ? core::at(solid.high, axis) : core::at(solid.low, axis));
        }
        return out;
    };
    std::vector<std::array<float, 3>> out;
    const auto quad = [&](const std::array<Corner, 4>& corners) {
        for (const std::size_t index : {0U, 1U, 2U, 0U, 2U, 3U}) {
            out.push_back(corner(core::at(corners, index)));
        }
    };
    quad({{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}});
    if (y_of(solid.high) == y_of(solid.low)) {
        return out;
    }
    quad({{{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}}});
    quad({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    quad({{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}});
    quad({{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}});
    quad({{{1, 0, 0}, {1, 0, 1}, {1, 1, 1}, {1, 1, 0}}});
    return out;
}

struct Ray {
    Vec3 origin{};
    Vec3 direction{};
};
// A ray against a box (slabs): the distance to its first hit (infinity: none, or the origin inside it).
double intersect(const Solid& solid, const Ray& ray)
{
    constexpr double kParallel = 1e-12;
    double enter = 0.0;
    double leave = kNoHit;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double origin = core::at(ray.origin, axis);
        const double direction = core::at(ray.direction, axis);
        const double low = core::at(solid.low, axis);
        const double high = core::at(solid.high, axis);
        if (std::abs(direction) < kParallel) {
            if (origin < low || origin > high) {
                return kNoHit;
            }
            continue;
        }
        const bool rising = direction > 0.0;
        enter = std::max(enter, ((rising ? low : high) - origin) / direction);
        leave = std::min(leave, ((rising ? high : low) - origin) / direction);
        if (enter > leave) {
            return kNoHit;
        }
    }
    return enter <= 0.0 ? kNoHit : enter;
}

using Rect = std::array<uint32_t, 4>;

struct View {
    Vec3 eye{};
    Vec3 right{};
    Vec3 up{};
    Vec3 back{};
    double half_width = 0.0;
    double half_height = 0.0;
    uint32_t width = 0;
    uint32_t height = 0;
};
View view_from(const Vec3& eye, uint32_t width, uint32_t height)
{
    View view;
    view.eye = eye;
    view.back = normalised(minus(eye, look_at()));
    view.right = normalised(cross(kUp, view.back));
    view.up = cross(view.back, view.right);
    view.half_width = kHalfFovTan;
    view.half_height = kHalfFovTan * static_cast<double>(height) / static_cast<double>(width);
    view.width = width;
    view.height = height;
    return view;
}

// A pixel of the target (or the viewport: the whole target here).
struct Pixel {
    int x = 0;
    int y = 0;
};

// The direction through a pixel (Direct3D 9's pixel centres: whole coordinates).
Vec3 direction_at(const View& view, Pixel pixel)
{
    const double ndc_x = (2.0 * pixel.x / view.width) - 1.0;
    const double ndc_y = 1.0 - (2.0 * pixel.y / view.height);
    return normalised(step(step(step(Vec3{}, view.right, ndc_x * view.half_width), view.up, ndc_y * view.half_height),
                           view.back, -1.0));
}

// The first surface along a ray: its solid (index + 1; 0: the sky) and distance.
struct Seen {
    std::size_t solid = kNoSurface;
    double distance = -1.0;
};
Seen seen_along(const Ray& ray)
{
    Seen seen;
    double nearest = kNoHit;
    for (std::size_t index = 0; index < kSolids.size(); ++index) {
        const double hit = intersect(core::at(kSolids, index), ray);
        if (hit < nearest) {
            nearest = hit;
            seen.solid = index + 1;
        }
    }
    if (seen.solid != kNoSurface) {
        seen.distance = nearest;
    }
    return seen;
}

// --- the fog, as the pass marches it -----------------------------------------------------------------------------

// (changed by the volumetric light: the light the air is lit by may move - the clock's case)
Vec3& light_toward()
{
    static Vec3 toward = normalised(step(Vec3{}, kLightTravels, -1.0));
    return toward;
}
const Vec3& to_sun() { return light_toward(); }
// While the clock lights the scene (DayLight), the frames don't set the console's `rt sun`.
bool& clock_lights()
{
    static bool lights = false;
    return lights;
}

bool sunlit(const Vec3& at)
{
    return std::ranges::none_of(kSolids, [&](const Solid& solid) {
        return intersect(solid, {.origin = at, .direction = to_sun()}) <=
               static_cast<double>(render::vulkan::kFogSunDistance);
    });
}

constexpr double kLeastLeft = 1e-4;             // the shortest way to the fog's end a segment counts (x its length)
constexpr double kMostDepth = 40.0;             // a segment's optical depth at most

// (the air's phase - engine/gfx/rt_volume_light.h medium_phase: the march's)
double phase(double cosine) { return render::rt::medium_phase(cosine, kAnisotropy); }

// (the pass's tint: the baked sun has no colour)
std::array<double, 3> tint() { return {1.0, 1.0, 1.0}; }

struct Fogged {
    std::array<double, 3> light{};
    double transmittance = 1.0;
    double largest_step = 0.0; // (one step's light at most, any channel)
};

// A view ray: from the eye along the direction to the surface `distance` away (-1: the sky).
struct ViewRay {
    Vec3 eye{};
    Vec3 direction{};
    double distance = -1.0;
};
// How it is marched: `steps` segments, the light sampled at `jitter` in each (0 - 1); shafts mode or not; (added by the
// street air) the light's rays a pair of steps at a time - at the first or the second (`parity`, the frame's) - or each
// step's own; `dark`: no sun (the shade's light alone).
struct Marching {
    uint32_t steps = 0;
    double jitter = 0.0;
    bool shafts = false;
    bool pairs = false;
    uint32_t parity = 0;
    bool dark = false;
};

// The share of the fog's light the sun gives where the replacing mode isn't drawing (the sky, shafts mode).
double sunlit_share(bool sky, bool replace)
{
    if (sky) {
        return render::vulkan::kFogSkyShare;
    }
    return replace ? 1.0 : render::vulkan::kFogShaftShare;
}

// (added by the street air) The media the case marches: the level's fog (its range, thickness and colour) and the
// street air (rt_volume_light.h street_air; none in the window's cases) over the ground at y `ground`.
struct Media {
    double start = kFogStart;
    double end = kFogEnd;
    double density = 1.0;
    std::array<float, 3> colour = kFogColour;
    render::rt::StreetAir air{};
    double ground = 0.0;
    render::rt::AirArt art{}; // (added by the cinematic air) the street air's look: its shade's light, its even lobe
};
Media& media()
{
    static Media now;
    return now;
}

// A layer's optical depth between s0 and s1 along a ray from height `eye_y` rising `dy` a unit (the pass's
// layer_depth): `sigma` at `ground` and below, thinning by e every 1 / inv_h above it.
double layer_depth(double eye_y, double dy, double s0, double s1, double sigma, double inv_h, double ground)
{
    constexpr double kFlat = 1e-12;
    constexpr double kShort = 1e-4;
    const double u0 = eye_y - ground;
    double lo = s0;
    double hi = s1;
    double below = 0.0;
    if (inv_h > 0.0) {
        if (std::abs(dy) < kFlat) {
            if (u0 <= 0.0) {
                below = s1 - s0;
                hi = lo;
            }
        } else {
            const double cut = std::clamp(-u0 / dy, s0, s1);
            if (dy > 0.0) {
                below = cut - s0;
                lo = cut;
            } else {
                below = s1 - cut;
                hi = cut;
            }
        }
    }
    double depth = sigma * below;
    if (hi > lo) {
        const double k = dy * inv_h;
        if (std::abs(k) * (hi - lo) < kShort) {
            depth += sigma * std::exp(-std::max(u0 + (dy * 0.5 * (lo + hi)), 0.0) * inv_h) * (hi - lo);
        } else {
            depth += sigma *
                     (std::exp(-std::max(u0 + (dy * lo), 0.0) * inv_h) - std::exp(-std::max(u0 + (dy * hi), 0.0) * inv_h)) /
                     k;
        }
    }
    return depth;
}

// The street air's optical depth between s0 and s1 (the pass's air_depth; no water here).
double air_depth(const ViewRay& ray, double s0, double s1)
{
    const Media& now = media();
    const double eye_y = y_of(ray.eye);
    const double dy = y_of(ray.direction);
    double depth = 0.0;
    if (now.air.extinction > 0.0) {
        // (its volume round the eye: the cinematic air's - rt_volume_light.h air_reach, at the segment's middle)
        depth += layer_depth(eye_y, dy, s0, s1, now.air.extinction,
                             now.air.height > 0.0 ? 1.0 / now.air.height : 0.0, now.ground) *
                 render::rt::air_reach(0.5 * (s0 + s1), now.art, dot(ray.direction, to_sun()));
    }
    if (now.air.snow > 0.0) {
        depth += layer_depth(eye_y, dy, s0, s1, now.air.snow,
                             now.air.snow_height > 0.0 ? 1.0 / now.air.snow_height : 0.0, now.ground);
    }
    return depth;
}

constexpr double kLeastTransmittance = 1e-4; // the march stops under it
constexpr double kSplitPast = 1.05;          // (the pass's: a ray this far past the volume round the eye leaves it)
constexpr uint32_t kLeastFarSteps = 4;

// The fog along a view ray as the pass marches it (or, with many steps, every step traced and the middle, the exact
// integral's stand-in). (Changed by the street air:) the steps closer together near the eye (first + span x t^2); the
// light's ray traced once a pair of steps at the first or the second (`parity`); the street air a second medium.
Fogged march(const ViewRay& ray, const Marching& how)
{
    Fogged out;
    const Media& now = media();
    const bool sky = ray.distance < 0.0;
    const bool replace = !how.shafts && !sky;
    const bool air_on = now.air.extinction > 0.0 || now.air.snow > 0.0;
    const double start = now.start;
    const double end = now.end;
    const double first = air_on ? 0.0 : std::max(start, 0.0);
    const double last = sky ? end : std::min(ray.distance, end);
    if (!(last > first)) {
        return out;
    }
    const double span = last - first;
    const double fade = end - start;
    const double shaped = how.dark ? 0.0 : phase(dot(ray.direction, to_sun()));
    const double share = sunlit_share(sky, replace);
    const double ambient = render::vulkan::kFogAmbientShare;
    const double scatter = (1.0 - render::vulkan::kFogAerialSkyShare) * render::rt::kHazeSunlit * (sky ? render::vulkan::kFogSkyShare : 1.0);
    const std::array<double, 3> sun_tint = tint();
    // (the pass's march_at: with the cinematic air's volume round the eye left by the ray, `steps` steps in it (t^2) and
    // half as many more past it in a geometric series - the volume stretched toward the light: air_stretch)
    const double volume_end =
        now.art.range * render::rt::kUnitsPerMetre * render::rt::air_stretch(dot(ray.direction, to_sun()), now.art);
    const bool split = air_on && volume_end > first && last > kSplitPast * volume_end;
    const uint32_t near_steps = split ? how.steps : 0;
    const uint32_t total = split ? how.steps + std::max<uint32_t>(how.steps / 2, kLeastFarSteps) : how.steps;
    const auto boundary = [&](double x) {
        if (near_steps == 0) {
            const double t = x / total;
            return first + (span * t * t);
        }
        if (x <= near_steps) {
            const double t = x / near_steps;
            return first + ((volume_end - first) * t * t);
        }
        const double t = (x - near_steps) / (total - near_steps);
        return volume_end * std::pow(last / volume_end, t);
    };
    const auto point_of = [&](uint32_t i) { return boundary(static_cast<double>(i) + how.jitter); };
    // (the air's sun map: the pass reads it at every step in its square - rt_volume_light.h air_map_half - and traces
    // the light's rays in pairs elsewhere)
    const double half_side = render::rt::air_map_half(now.art);
    const auto in_map = [&](const Vec3& at) {
        if (!(half_side > 0.0) || how.dark) {
            return false;
        }
        const Vec3 toward = to_sun();
        const Vec3 across = std::abs(y_of(toward)) < 0.9 ? Vec3{0.0, 1.0, 0.0} : Vec3{1.0, 0.0, 0.0};
        const Vec3 side = normalised(cross(toward, across));
        const Vec3 up = cross(side, toward);
        const Vec3 from_eye = minus(at, ray.eye);
        return std::abs(dot(from_eye, side)) <= half_side && std::abs(dot(from_eye, up)) <= half_side;
    };
    double seen = 0.0;
    double pair_seen = 0.0;
    for (uint32_t i = 0; i < total; ++i) {
        const double near_end = boundary(static_cast<double>(i));
        const double far_end = boundary(static_cast<double>(i + 1));
        const Vec3 point = step(ray.eye, ray.direction, point_of(i));
        if (!how.pairs || in_map(point)) {
            seen = sunlit(point) ? 1.0 : 0.0;
        } else {
            if ((i & 1U) == 0 || in_map(step(ray.eye, ray.direction, point_of(i - 1)))) {
                const uint32_t traced = (i & 1U) == 0 ? std::min(i + how.parity, total - 1) : i;
                const Vec3 at = step(ray.eye, ray.direction, point_of(traced));
                pair_seen = sunlit(in_map(at) ? point : at) ? 1.0 : 0.0;
            }
            seen = pair_seen;
        }
        const double x0 = std::clamp(near_end, start, end);
        const double x1 = std::clamp(far_end, start, end);
        const double fog_depth =
            x1 > x0 ? now.density * std::log(std::max(end - x0, kLeastLeft * fade) / std::max(end - x1, kLeastLeft * fade))
                    : 0.0;
        const double depth_air = air_on ? air_depth(ray, near_end, far_end) : 0.0;
        const double depth = std::max(fog_depth, 0.0) + std::max(depth_air, 0.0);
        if (!(depth > 1e-9)) {
            continue;
        }
        const double kept = std::exp(-std::min(depth, kMostDepth));
        const double lit = seen * shaped;
        double step_light = 0.0;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const double fog = core::at(now.colour, channel);
            const double tinted = lit * core::at(sun_tint, channel);
            const double here_fog = replace ? fog * (ambient + ((1.0 - ambient) * tinted))
                                            : fog * (1.0 - ambient) * tinted * share;
            // (the street air's light: its look's even lobe added to the phase, its shade x the look's share)
            const double lit_air =
                how.dark ? 0.0 : seen * (shaped + now.art.side) * now.art.glow * core::at(sun_tint, channel);
            const double here_air = (replace ? fog * ambient * now.art.shade : 0.0) + (scatter * lit_air);
            const double here = ((std::max(fog_depth, 0.0) * here_fog) + (std::max(depth_air, 0.0) * here_air)) / depth;
            const double added = out.transmittance * (1.0 - kept) * here;
            core::at(out.light, channel) += added;
            step_light = std::max(step_light, added);
        }
        out.largest_step = std::max(out.largest_step, step_light);
        out.transmittance *= kept;
        if (out.transmittance < kLeastTransmittance) {
            out.transmittance = 0.0;
            break;
        }
    }
    if (!replace) {
        out.transmittance = 1.0;
    }
    return out;
}

// The pass's jitter for a pixel (the viewport's) in frame `frame` (interleaved gradient noise + the golden ratio).
double jitter_of(Pixel pixel, uint64_t frame)
{
    constexpr float kNoiseScale = 52.9829189F;
    constexpr float kNoiseX = 0.06711056F;
    constexpr float kNoiseY = 0.00583715F;
    constexpr double kGoldenRatio = 0.6180339887498949;
    const float noise =
        kNoiseScale *
        std::fmod((kNoiseX * static_cast<float>(pixel.x)) + (kNoiseY * static_cast<float>(pixel.y)), 1.0F);
    const double frame_offset = std::fmod(static_cast<double>(frame) * kGoldenRatio, 1.0);
    const double sum = static_cast<double>(noise - std::floor(noise)) + static_cast<float>(frame_offset);
    return sum - std::floor(sum);
}

// The exact integral (2048 steps, each sampled at its middle) of the fog through a pixel.
constexpr uint32_t kFineSteps = 2048;
constexpr double kMiddle = 0.5;
Fogged exact_at(const View& view, Pixel pixel, bool shafts)
{
    const Vec3 direction = direction_at(view, pixel);
    const Seen seen = seen_along({.origin = view.eye, .direction = direction});
    return march({.eye = view.eye, .direction = direction, .distance = seen.distance},
                 {.steps = kFineSteps, .jitter = kMiddle, .shafts = shafts});
}
// The same with the sun blocked everywhere (the shade's light alone).
Fogged dark_at(const View& view, Pixel pixel)
{
    const Vec3 direction = direction_at(view, pixel);
    const Seen seen = seen_along({.origin = view.eye, .direction = direction});
    return march({.eye = view.eye, .direction = direction, .distance = seen.distance},
                 {.steps = kFineSteps, .jitter = kMiddle, .dark = true});
}

// The exact transmittance: the game's linear fog, ((end - distance) / (end - start)) clamped.
double exact_transmittance(double distance)
{
    if (distance < 0.0) {
        return 1.0;
    }
    const double clamped = std::clamp(distance, media().start, media().end);
    return (media().end - clamped) / (media().end - media().start);
}

// --- the acceleration structures (the check's own) -----------------------------------------------------------------

struct Structure {
    render::vulkan::DeviceBuffer storage;
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    VkDeviceAddress address = 0;
};

struct Scene {
    std::vector<render::vulkan::DeviceBuffer> inputs;
    std::vector<Structure> bottoms;
    Structure top;
    render::vulkan::DeviceBuffer scratch;
};

VkDeviceAddress address_of(VkDevice device, VkBuffer buffer)
{
    const VkBufferDeviceAddressInfo info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .pNext = nullptr, .buffer = buffer};
    return vkGetBufferDeviceAddress(device, &info);
}

VkDeviceAddress aligned(VkDeviceAddress address, VkDeviceSize alignment)
{
    return (address + alignment - 1) & ~(alignment - 1);
}

bool make_structure(render::vulkan::Backend& state, VkAccelerationStructureTypeKHR type, VkDeviceSize bytes,
                    Structure* out)
{
    if (!state.memory.create_buffer(
            bytes, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            render::vulkan::BufferPlace::kDevice, &out->storage)) {
        return false;
    }
    const VkAccelerationStructureCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
                                                    .pNext = nullptr,
                                                    .createFlags = 0,
                                                    .buffer = out->storage.buffer,
                                                    .offset = 0,
                                                    .size = bytes,
                                                    .type = type,
                                                    .deviceAddress = 0};
    const VkDevice device = state.context.device();
    if (vkCreateAccelerationStructureKHR(device, &info, nullptr, &out->handle) != VK_SUCCESS) {
        return false;
    }
    const VkAccelerationStructureDeviceAddressInfoKHR where{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .pNext = nullptr,
        .accelerationStructure = out->handle};
    out->address = vkGetAccelerationStructureDeviceAddressKHR(device, &where);
    return true;
}

template <typename T>
std::span<const uint8_t> bytes_of(std::span<const T> items)
{
    const void* const start = items.data();
    return {static_cast<const uint8_t*>(start), items.size_bytes()};
}

bool make_input(render::vulkan::Backend& state, std::span<const uint8_t> bytes, render::vulkan::DeviceBuffer* out,
                VkDeviceAddress* address)
{
    constexpr VkDeviceSize kAlignment = 16;
    if (!state.memory.create_buffer(bytes.size() + kAlignment,
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    render::vulkan::BufferPlace::kUpload, out) ||
        out->mapped == nullptr) {
        return false;
    }
    const VkDeviceAddress start = address_of(state.context.device(), out->buffer);
    *address = aligned(start, kAlignment);
    const VkDeviceSize offset = *address - start;
    const std::span<uint8_t> into(static_cast<uint8_t*>(out->mapped), static_cast<std::size_t>(out->bytes));
    std::ranges::copy(bytes, into.subspan(static_cast<std::size_t>(offset)).begin());
    state.memory.flush(*out, 0, out->bytes);
    return true;
}

struct BuildInput {
    VkAccelerationStructureTypeKHR type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    const VkAccelerationStructureGeometryKHR* geometry = nullptr;
    uint32_t primitives = 0;
};
bool build(render::vulkan::Backend& state, Scene* scene, const BuildInput& input, Structure* out)
{
    const VkDevice device = state.context.device();
    VkAccelerationStructureBuildGeometryInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    info.type = input.type;
    info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    info.geometryCount = 1;
    info.pGeometries = input.geometry;
    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info,
                                            &input.primitives, &sizes);
    if (!make_structure(state, input.type, sizes.accelerationStructureSize, out)) {
        return false;
    }
    constexpr VkDeviceSize kScratchAlignment = 256;
    if (scene->scratch.buffer != VK_NULL_HANDLE) {
        state.memory.destroy_buffer(&scene->scratch);
    }
    if (!state.memory.create_buffer(sizes.buildScratchSize + kScratchAlignment,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    render::vulkan::BufferPlace::kDevice, &scene->scratch)) {
        return false;
    }
    info.dstAccelerationStructure = out->handle;
    info.scratchData.deviceAddress = aligned(address_of(device, scene->scratch.buffer), kScratchAlignment);
    const VkAccelerationStructureBuildRangeInfoKHR range{
        .primitiveCount = input.primitives, .primitiveOffset = 0, .firstVertex = 0, .transformOffset = 0};
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    return state.memory.submit_now(
        [&](VkCommandBuffer commands) { vkCmdBuildAccelerationStructuresKHR(commands, 1, &info, &ranges); });
}

// One BLAS of every solid and the TLAS of its one instance (mask 0x01).
bool make_scene(render::vulkan::Backend& state, Scene* scene)
{
    std::vector<std::array<float, 3>> vertices;
    for (const Solid& solid : kSolids) {
        const std::vector<std::array<float, 3>> more = triangles_of(solid);
        vertices.insert(vertices.end(), more.begin(), more.end());
    }
    render::vulkan::DeviceBuffer& input = scene->inputs.emplace_back();
    VkDeviceAddress vertex_address = 0;
    if (!make_input(state, bytes_of(std::span<const std::array<float, 3>>(vertices)), &input, &vertex_address)) {
        return false;
    }
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    VkAccelerationStructureGeometryTrianglesDataKHR& triangles = geometry.geometry.triangles;
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = vertex_address;
    triangles.vertexStride = sizeof(std::array<float, 3>);
    triangles.maxVertex = static_cast<uint32_t>(vertices.size() - 1);
    triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
    Structure& bottom = scene->bottoms.emplace_back();
    if (!build(state, scene,
               {.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                .geometry = &geometry,
                .primitives = static_cast<uint32_t>(vertices.size() / 3)},
               &bottom)) {
        return false;
    }
    VkAccelerationStructureInstanceKHR instance{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        core::at(core::at(std::span(instance.transform.matrix), axis), axis) = 1.0F;
    }
    instance.instanceCustomIndex = 0;
    instance.mask = kMaskWorld;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = bottom.address;
    const std::array<VkAccelerationStructureInstanceKHR, 1> instances{instance};
    render::vulkan::DeviceBuffer& list = scene->inputs.emplace_back();
    VkDeviceAddress list_address = 0;
    if (!make_input(state, bytes_of(std::span<const VkAccelerationStructureInstanceKHR>(instances)), &list,
                    &list_address)) {
        return false;
    }
    VkAccelerationStructureGeometryKHR top{};
    top.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    top.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    top.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    top.geometry.instances.arrayOfPointers = VK_FALSE;
    top.geometry.instances.data.deviceAddress = list_address;
    return build(state, scene,
                 {.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, .geometry = &top, .primitives = 1},
                 &scene->top);
}

void release_scene(render::vulkan::Backend& state, Scene* scene)
{
    const VkDevice device = state.context.device();
    vkDeviceWaitIdle(device);
    scene->bottoms.push_back(scene->top);
    for (Structure& structure : scene->bottoms) {
        if (structure.handle != VK_NULL_HANDLE) {
            vkDestroyAccelerationStructureKHR(device, structure.handle, nullptr);
        }
        if (structure.storage.buffer != VK_NULL_HANDLE) {
            state.memory.destroy_buffer(&structure.storage);
        }
    }
    for (render::vulkan::DeviceBuffer& buffer : scene->inputs) {
        state.memory.destroy_buffer(&buffer);
    }
    if (scene->scratch.buffer != VK_NULL_HANDLE) {
        state.memory.destroy_buffer(&scene->scratch);
    }
    *scene = Scene{};
}

// --- drawing it through the Gpu, as render_pc draws a level -----------------------------------------------------

struct ColourVertex {
    std::array<float, 3> position{};
    uint32_t colour = 0;
};
static_assert(sizeof(ColourVertex) == (3 * sizeof(float)) + sizeof(uint32_t), "XYZ | DIFFUSE");

constexpr std::size_t kMatrixFloats = 16;
using Matrix = std::array<float, kMatrixFloats>;
struct Matrices {
    Matrix view{};
    Matrix projection{};
};
Matrices matrices_for(const Vec3& eye, uint32_t width, uint32_t height)
{
    Matrices out;
    maths::matrix_look_at_rh(out.view, floats(eye), floats(look_at()), floats(kUp));
    const double lens_width = 2.0 * kNearZ * kHalfFovTan;
    const double lens_height = lens_width * static_cast<double>(height) / static_cast<double>(width);
    maths::matrix_perspective_rh(out.projection, static_cast<float>(lens_width), static_cast<float>(lens_height),
                                 kNearZ, far_z());
    return out;
}

void draw_scene(render::Gpu& gpu, const Vec3& eye, uint32_t width, uint32_t height)
{
    using render::GpuRenderState;
    using render::GpuStageState;
    gpu.set_viewport({.x = 0, .y = 0, .width = width, .height = height, .min_z = 0.0F, .max_z = 1.0F});
    const Matrices matrices = matrices_for(eye, width, height);
    constexpr Matrix kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    gpu.set_transform(render::GpuTransform::kWorld, kIdentity.data());
    gpu.set_transform(render::GpuTransform::kView, matrices.view.data());
    gpu.set_transform(render::GpuTransform::kProjection, matrices.projection.data());
    gpu.set_render_state(GpuRenderState::kZEnable, 1);
    gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
    gpu.set_render_state(GpuRenderState::kLighting, 0);
    gpu.set_render_state(GpuRenderState::kCullMode, value::kCullNone);
    gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    gpu.set_vertex_format(kFvf);
    gpu.set_texture(0, nullptr);
    gpu.set_stage_state(0, GpuStageState::kColourOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kColourArg1, value::kArgDiffuse);
    gpu.set_stage_state(0, GpuStageState::kAlphaOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kAlphaArg1, value::kArgDiffuse);
    std::vector<ColourVertex> vertices;
    for (const Solid& solid : kSolids) {
        for (const std::array<float, 3>& corner : triangles_of(solid)) {
            vertices.push_back({.position = corner, .colour = solid.colour});
        }
    }
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(vertices.size() / 3),
                         vertices.data(), sizeof(ColourVertex));
}

render::rt::ViewDrawn view_drawn(const Vec3& eye, uint32_t width, uint32_t height, bool fog_on)
{
    const Matrices matrices = matrices_for(eye, width, height);
    render::rt::ViewDrawn view;
    constexpr std::size_t kSide = 4;
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            core::at(core::at(view.camera.view, row), column) = core::at(matrices.view, (row * kSide) + column);
            core::at(core::at(view.camera.projection, row), column) =
                core::at(matrices.projection, (row * kSide) + column);
        }
    }
    view.camera.position = floats(eye);
    view.rect = {0, 0, width, height};
    view.depth_range = {0.0F, 1.0F};
    view.fog = {.on = fog_on,
                .start = static_cast<float>(media().start),
                .end = static_cast<float>(media().end),
                .colour = media().colour};
    return view;
}

// --- the targets and a frame ---------------------------------------------------------------------------------------

struct Targets {
    GpuSurface* colour = nullptr;
    GpuSurface* depth = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
};
bool make_targets(render::Gpu& gpu, uint32_t width, uint32_t height, Targets* out)
{
    out->width = width;
    out->height = height;
    const render::GpuSurfaceDesc colour{.width = width,
                                        .height = height,
                                        .format = value::kFormatX8R8G8B8,
                                        .samples = 0,
                                        .quality = 0,
                                        .lockable_or_discard = true};
    render::GpuSurfaceDesc depth = colour;
    depth.format = value::kFormatD24S8;
    depth.lockable_or_discard = false;
    return gpu.create_render_target(colour, &out->colour) == value::kResultOk &&
           gpu.create_depth_surface(depth, &out->depth) == value::kResultOk;
}

void release_targets(render::Gpu& gpu, const Targets& targets)
{
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    for (GpuSurface* surface : {targets.colour, targets.depth}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
}

uint64_t draw_frame(render::Gpu& gpu, const Targets& targets, const Vec3& eye, bool fog_on = true)
{
    gpu.begin_scene();
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = targets.width, .height = targets.height, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth | value::kClearStencil, kClearColour, 1.0F, 0);
    draw_scene(gpu, eye, targets.width, targets.height);
    // (the sun as the console's `rt sun` sets it: the level's baked one otherwise - none in this scene)
    const std::array<float, 3> toward = floats(to_sun());
    render::rt::set_sun_override(clock_lights() ? nullptr : &toward);
    const uint64_t before = render::vulkan::rt_fog_passes();
    render::rt::world_view_drawn(view_drawn(eye, targets.width, targets.height, fog_on));
    const uint64_t passes = render::vulkan::rt_fog_passes() - before;
    gpu.end_scene();
    return passes;
}

bool read_colour(const Targets& targets, std::vector<uint8_t>* texels)
{
    return render::vulkan::read_surface(targets.colour, texels) &&
           texels->size() == static_cast<std::size_t>(targets.width) * targets.height * kPixelBytes;
}

bool read_fog(render::vulkan::RtFogImage which, render::vulkan::RtFogTexels* out)
{
    render::vulkan::Backend& state = render::vulkan::backend();
    const std::scoped_lock lock(state.mutex);
    const Rect whole{0, 0, kTargetWidth, kTargetHeight};
    return render::vulkan::read_rt_fog_image(state, whole, which, out) &&
           out->width == (kTargetWidth + march_scale() - 1) / march_scale() &&
           out->height == (kTargetHeight + march_scale() - 1) / march_scale();
}

// --- comparing ---------------------------------------------------------------------------------------------------

// The limits the comparisons hold the pass to.
constexpr int kMostSkyDifferences = 4; // texels whose sky / surface verdict differs from the processor's
constexpr int kLeastSurfaceTexels = 10000;
constexpr double kNinetyEighth = 0.98; // percentiles
constexpr double kNinetyFifth = 0.95;
constexpr double kNinetyNinth = 0.99;
constexpr double kAll = 1.0;
constexpr double kMostDistanceError = 2e-3;      // relative
constexpr double kMostTransmittanceError = 4e-3; // (16-bit floats)
constexpr double kMostLightError = 4e-3;         // 98% of the texels
constexpr double kStepSlack = 0.01;              // all of them: one step's light and this
constexpr double kInsideShaft = 0.05;            // the exact integral's light above the ambient: crosses the shaft
constexpr double kOutsideShaft = 0.002;          // ... crosses no sunlit fog
constexpr std::size_t kLeastShaftTexels = 200;
constexpr double kShaftContrast = 0.05; // inside above outside by at least this
constexpr double kShaftError = 0.02;    // inside's mean against the exact integral's
constexpr double kMostConvergedMean = 0.006;
constexpr double kMostConverged95 = 0.025;
constexpr std::size_t kLeastInteriorPixels = 2000;
constexpr double kMostCompositeMean = 2.5; // levels
constexpr double kMostComposite95 = 7.0;
constexpr int kLeastSkyPixels = 100;
constexpr double kLevelSlack = 1.5; // levels: a channel changed when it moved by more
constexpr double kLevels = 255.0;
constexpr double kNoLight = 1e-6;
constexpr int kLeastDarkPixels = 300;
constexpr int kPerMille = 1000;
constexpr int kChannelsPerMillePixel = 3; // (a ray query grazing the wall's edge may decide otherwise)
constexpr float kRestarted = 1.5F;        // a history shorter than this started again
constexpr int kLeastKept = 5000;
constexpr double kMostGhost95 = 0.05;
constexpr int kInteriorRadius = 2;         // the pixels around one that must see its surface
constexpr int kDarkRadius = 3;             // ... whose rays must cross no sunlit fog
constexpr uint32_t kDarkSteps = 256;       // the march that tells which rays cross sunlit fog
constexpr int kLeastLighterChannels = 300; // the haze's shaft

using Rgb = std::array<double, 3>;
Rgb rgb_of(uint32_t colour)
{
    return {static_cast<double>((colour >> kRedShift) & kByteMask) * kPerByte,
            static_cast<double>((colour >> kGreenShift) & kByteMask) * kPerByte,
            static_cast<double>(colour & kByteMask) * kPerByte};
}
Rgb pixel_at(const std::vector<uint8_t>& texels, Pixel pixel)
{
    const std::size_t at =
        ((static_cast<std::size_t>(pixel.y) * kTargetWidth) + static_cast<std::size_t>(pixel.x)) * kPixelBytes;
    return {core::at(texels, at + 2) * kPerByte, core::at(texels, at + 1) * kPerByte, core::at(texels, at) * kPerByte};
}

// The pixel a texel's info names (its x and y at `first`, `first` + 1).
Pixel pixel_of(const std::array<float, 4>& info, std::size_t first)
{
    return {.x = static_cast<int>(std::lround(core::at(info, first))),
            .y = static_cast<int>(std::lround(core::at(info, first + 1)))};
}

int count_not_finite(const render::vulkan::RtFogTexels& image)
{
    int count = 0;
    for (const std::array<float, 4>& texel : image.texels) {
        count += std::ranges::any_of(texel, [](float value) { return !std::isfinite(value); }) ? 1 : 0;
    }
    return count;
}

double percentile(std::vector<double> values, double share)
{
    if (values.empty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    const auto at = static_cast<std::size_t>(share * static_cast<double>(values.size() - 1));
    return core::at(values, at);
}

double mean(const std::vector<double>& values)
{
    double sum = 0.0;
    for (const double value : values) {
        sum += value;
    }
    return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
}

double light_error(const std::array<float, 4>& got, const Fogged& want)
{
    double error = 0.0;
    for (std::size_t channel = 0; channel < 3; ++channel) {
        error = std::max(error, std::abs(static_cast<double>(core::at(got, channel)) - core::at(want.light, channel)));
    }
    return error;
}

Seen seen_at(const View& view, Pixel pixel)
{
    return seen_along({.origin = view.eye, .direction = direction_at(view, pixel)});
}

// One frame's march, texel by texel, against the processor's.
void compare_raw(const View& view, uint64_t frame, bool shafts, Tally* tally)
{
    render::vulkan::RtFogTexels raw;
    render::vulkan::RtFogTexels info;
    const bool read =
        read_fog(render::vulkan::RtFogImage::kRaw, &raw) && read_fog(render::vulkan::RtFogImage::kRawInfo, &info);
    tally->check(read, "the march's images read back");
    if (!read) {
        return;
    }
    tally->check(
        count_not_finite(raw) == 0 && count_not_finite(info) == 0,
        text::format("no NaN or infinity in the march (%d, %d texels)", count_not_finite(raw), count_not_finite(info)));
    std::vector<double> distance_errors;
    std::vector<double> transmittance_errors;
    std::vector<double> light_errors;
    int sky_wrong = 0;
    int surface_pixels = 0;
    int sky_pixels = 0;
    double one_step = 0.0;
    std::vector<double> step_lights;
    for (std::size_t i = 0; i < raw.texels.size(); ++i) {
        const std::array<float, 4>& got = core::at(raw.texels, i);
        const std::array<float, 4>& where = core::at(info.texels, i);
        const Pixel pixel = pixel_of(where, 1);
        const Vec3 direction = direction_at(view, pixel);
        const Seen seen = seen_along({.origin = view.eye, .direction = direction});
        const double gpu_distance = core::at(where, 0);
        if (seen.solid == kNoSurface || gpu_distance < 0.0) {
            sky_wrong += (seen.solid == kNoSurface) != (gpu_distance < 0.0) ? 1 : 0;
            ++sky_pixels;
            continue;
        }
        ++surface_pixels;
        distance_errors.push_back(std::abs(gpu_distance - seen.distance) / seen.distance);
        const double transmittance = shafts ? 1.0 : exact_transmittance(seen.distance);
        transmittance_errors.push_back(std::abs(static_cast<double>(core::at(got, 3)) - transmittance));
        const Fogged want = march({.eye = view.eye, .direction = direction, .distance = gpu_distance},
                                  {.steps = kSteps,
                                   .jitter = jitter_of(pixel, frame),
                                   .shafts = shafts,
                                   .pairs = true,
                                   .parity = static_cast<uint32_t>(frame & 1U)});
        light_errors.push_back(light_error(got, want));
        // (a sample on either side of the shaft's edge: a pair of steps' light - the light's rays a pair at a time)
        one_step = std::max(one_step, 2.0 * want.largest_step);
    }
    tally->check(sky_wrong <= kMostSkyDifferences,
                 text::format("the sky where the processor sees it (%d of %d texels differ)", sky_wrong, sky_pixels));
    const int scale_squared = static_cast<int>((march_scale() / 2) * (march_scale() / 2));
    tally->check(surface_pixels > kLeastSurfaceTexels / scale_squared,
                 text::format("%d texels on a surface", surface_pixels));
    tally->check(percentile(distance_errors, kNinetyNinth) < kMostDistanceError,
                 text::format("the distance marched to: 99th percentile relative error %.2g",
                              percentile(distance_errors, kNinetyNinth)));
    tally->check(percentile(transmittance_errors, kAll) < kMostTransmittanceError,
                 text::format("the transmittance = exp(-integral) = %s: largest error %.2g (16-bit floats)",
                              shafts ? "1 (shafts mode)" : "(end - d) / (end - start)",
                              percentile(transmittance_errors, kAll)));
    tally->check(percentile(light_errors, kNinetyEighth) < kMostLightError &&
                     percentile(light_errors, kAll) < one_step + kStepSlack,
                 text::format("the light against the processor's march (same jitter, %u steps): 98%% within %.2g, all "
                              "within %.3f (a pair of steps' %.3f)",
                              kSteps, percentile(light_errors, kNinetyEighth), percentile(light_errors, kAll),
                              one_step));
}

// The shaft: texels whose ray crosses sunlit fog against those that don't (the exact integral says which).
void compare_shaft(const View& view, Tally* tally)
{
    render::vulkan::RtFogTexels history;
    render::vulkan::RtFogTexels info;
    if (!read_fog(render::vulkan::RtFogImage::kHistory, &history) ||
        !read_fog(render::vulkan::RtFogImage::kHistoryInfo, &info)) {
        tally->check(false, "the history read back");
        return;
    }
    // the light the ambient alone gives along each ray, against what the GPU found: the shaft's extra
    std::vector<double> inside;
    std::vector<double> outside;
    std::vector<double> inside_want;
    const uint32_t width = history.width;
    for (std::size_t i = 0; i < history.texels.size(); ++i) {
        const Pixel pixel{
            .x = std::min(static_cast<int>(i % width) * static_cast<int>(march_scale()), static_cast<int>(kTargetWidth) - 1),
            .y = std::min(static_cast<int>(i / width) * static_cast<int>(march_scale()),
                          static_cast<int>(kTargetHeight) - 1)};
        const Seen seen = seen_at(view, pixel);
        if (seen.solid == kNoSurface) {
            continue;
        }
        const Fogged exact = exact_at(view, pixel, false);
        const double ambient_only =
            core::at(kFogColour, 0) * render::vulkan::kFogAmbientShare * (1.0 - exact_transmittance(seen.distance));
        const double extra_want = core::at(exact.light, 0) - ambient_only;
        const double extra_got = static_cast<double>(core::at(core::at(history.texels, i), 0)) - ambient_only;
        if (extra_want > kInsideShaft) {
            inside.push_back(extra_got);
            inside_want.push_back(extra_want);
        } else if (extra_want < kOutsideShaft) {
            outside.push_back(extra_got);
        }
    }
    tally->check(
        inside.size() > kLeastShaftTexels && outside.size() > kLeastShaftTexels,
        text::format("%zu texels' rays cross the shaft, %zu cross no sunlit fog", inside.size(), outside.size()));
    tally->check(mean(inside) > mean(outside) + kShaftContrast &&
                     std::abs(mean(inside) - mean(inside_want)) < kShaftError,
                 text::format("the shaft: its rays' light above the ambient %.3f (the exact integral's %.3f), the "
                              "others' %.3f",
                              mean(inside), mean(inside_want), mean(outside)));
}

// The exact light for a texel's block (the pixels its frames march, averaged: 2 x 2, or 4 x 4); false when they don't
// all see one surface.
bool block_light(const View& view, Pixel pixel, Fogged* out)
{
    const int scale = static_cast<int>(march_scale());
    const Pixel corner{.x = (pixel.x / scale) * scale, .y = (pixel.y / scale) * scale};
    *out = Fogged{};
    std::size_t first = kNoSurface;
    const int pixels = scale * scale;
    for (int which = 0; which < pixels; ++which) {
        const Pixel each{.x = std::min(corner.x + (which % scale), static_cast<int>(kTargetWidth) - 1),
                         .y = std::min(corner.y + (which / scale), static_cast<int>(kTargetHeight) - 1)};
        const Seen seen = seen_at(view, each);
        if (which == 0) {
            first = seen.solid;
        }
        if (seen.solid != first || seen.solid == kNoSurface) {
            return false;
        }
        const Fogged one = exact_at(view, each, false);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            core::at(out->light, channel) += core::at(one.light, channel) / static_cast<double>(pixels);
        }
    }
    return true;
}

// The history after a still camera's frames against the exact integral, and one frame's march against it.
// (`all_full`: every history at its longest - a still camera; the street's grazing ground keeps restarting some, its
// blocks' pixels too far apart for the history's distance test)
void compare_converged(const View& view, Tally* tally, bool all_full = true)
{
    render::vulkan::RtFogTexels history;
    render::vulkan::RtFogTexels info;
    render::vulkan::RtFogTexels raw;
    const bool read = read_fog(render::vulkan::RtFogImage::kHistory, &history) &&
                      read_fog(render::vulkan::RtFogImage::kHistoryInfo, &info) &&
                      read_fog(render::vulkan::RtFogImage::kRaw, &raw);
    tally->check(read, "the history read back");
    if (!read) {
        return;
    }
    tally->check(count_not_finite(history) == 0, text::format("no NaN in the history (%d)", count_not_finite(history)));
    std::vector<double> history_errors;
    std::vector<double> raw_errors;
    int full = 0;
    int counted = 0;
    for (std::size_t i = 0; i < history.texels.size(); ++i) {
        const std::array<float, 4>& was = core::at(info.texels, i);
        Fogged want;
        if (!block_light(view, pixel_of(was, 2), &want)) {
            continue;
        }
        ++counted;
        full += core::at(was, 1) >= static_cast<float>(render::vulkan::kFogMaxHistory) ? 1 : 0;
        history_errors.push_back(light_error(core::at(history.texels, i), want));
        raw_errors.push_back(light_error(core::at(raw.texels, i), want));
    }
    tally->check(full == counted || (!all_full && full * 10 >= counted * 9),
                 text::format("every history full (%d of %d at %u frames)", full, counted,
                                               render::vulkan::kFogMaxHistory));
    tally->check(mean(history_errors) < kMostConvergedMean &&
                     percentile(history_errors, kNinetyFifth) < kMostConverged95,
                 text::format("converged against the exact integral: mean error %.4f, 95th percentile %.4f",
                              mean(history_errors), percentile(history_errors, kNinetyFifth)));
    tally->check(mean(history_errors) < mean(raw_errors),
                 text::format("nearer than one frame's march (its mean error %.4f)", mean(raw_errors)));
}

// Whether the pixels around one (within `radius`) see its surface.
bool interior(const View& view, Pixel pixel, std::size_t solid)
{
    for (int dy = -kInteriorRadius; dy <= kInteriorRadius; ++dy) {
        for (int dx = -kInteriorRadius; dx <= kInteriorRadius; ++dx) {
            if (seen_at(view, {.x = pixel.x + dx, .y = pixel.y + dy}).solid != solid) {
                return false;
            }
        }
    }
    return true;
}

// The colour target: surface x T + light (from the processor's exact answers), where the pixel's neighbours share its
// surface; the sky never dimmed.
void compare_composite(const View& view, const std::vector<uint8_t>& texels, bool shafts, Tally* tally)
{
    std::vector<double> errors;
    int sky_dimmed = 0;
    int sky = 0;
    const Rgb clear = rgb_of(kClearColour);
    for (int row = kInteriorRadius; row < static_cast<int>(kTargetHeight) - kInteriorRadius; row += 2) {
        for (int column = kInteriorRadius; column < static_cast<int>(kTargetWidth) - kInteriorRadius; column += 2) {
            const Pixel pixel{.x = column, .y = row};
            const Seen seen = seen_at(view, pixel);
            const Rgb got = pixel_at(texels, pixel);
            if (seen.solid == kNoSurface) {
                ++sky;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    sky_dimmed += core::at(got, channel) + (kLevelSlack * kPerByte) < core::at(clear, channel) ? 1 : 0;
                }
                continue;
            }
            if (!interior(view, pixel, seen.solid)) {
                continue;
            }
            const Fogged exact = exact_at(view, pixel, shafts);
            const Rgb surface = rgb_of(core::at(kSolids, seen.solid - 1).colour);
            double error = 0.0;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const double want =
                    std::min(1.0, (core::at(surface, channel) * exact.transmittance) + core::at(exact.light, channel));
                error = std::max(error, std::abs(core::at(got, channel) - want));
            }
            errors.push_back(error * kLevels);
        }
    }
    tally->check(errors.size() > kLeastInteriorPixels, text::format("%zu interior pixels compared", errors.size()));
    tally->check(mean(errors) < kMostCompositeMean && percentile(errors, kNinetyFifth) < kMostComposite95,
                 text::format("the colour = surface x T + light: mean %.2f levels, 95th percentile %.2f", mean(errors),
                              percentile(errors, kNinetyFifth)));
    tally->check(sky > kLeastSkyPixels && sky_dimmed == 0,
                 text::format("the sky never dimmed (%d of %d pixels' channels darker)", sky_dimmed, sky));
}

// Shafts mode: where no ray crosses sunlit fog the colour is the surface's own.
void compare_shafts_untouched(const View& view, const std::vector<uint8_t>& texels, Tally* tally)
{
    // (which pixels' rays cross sunlit fog: a march of each at the middle of its segments)
    const int columns = static_cast<int>(kTargetWidth);
    const int rows = static_cast<int>(kTargetHeight);
    const auto index = [columns](Pixel pixel) {
        return (static_cast<std::size_t>(pixel.y) * static_cast<std::size_t>(columns)) +
               static_cast<std::size_t>(pixel.x);
    };
    std::vector<bool> lit(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const Pixel pixel{.x = column, .y = row};
            const Vec3 direction = direction_at(view, pixel);
            const Seen seen = seen_along({.origin = view.eye, .direction = direction});
            const Fogged fogged = march({.eye = view.eye, .direction = direction, .distance = seen.distance},
                                        {.steps = kDarkSteps, .jitter = kMiddle, .shafts = true});
            lit.at(index(pixel)) = core::at(fogged.light, 0) > kNoLight;
        }
    }
    const auto dark_round = [&](Pixel pixel) {
        for (int dy = -kDarkRadius; dy <= kDarkRadius; ++dy) {
            for (int dx = -kDarkRadius; dx <= kDarkRadius; ++dx) {
                const Pixel around{.x = std::clamp(pixel.x + dx, 0, columns - 1),
                                   .y = std::clamp(pixel.y + dy, 0, rows - 1)};
                if (lit.at(index(around))) {
                    return false;
                }
            }
        }
        return true;
    };
    int compared = 0;
    int changed = 0;
    double largest = 0.0;
    for (int row = 1; row < rows - 1; row += 2) {
        for (int column = 1; column < columns - 1; column += 2) {
            const Pixel pixel{.x = column, .y = row};
            const Seen seen = seen_at(view, pixel);
            if (seen.solid == kNoSurface || !dark_round(pixel)) {
                continue;
            }
            ++compared;
            const Rgb got = pixel_at(texels, pixel);
            const Rgb surface = rgb_of(core::at(kSolids, seen.solid - 1).colour);
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const double difference = std::abs(core::at(got, channel) - core::at(surface, channel));
                changed += difference > kLevelSlack * kPerByte ? 1 : 0;
                largest = std::max(largest, difference / kPerByte);
            }
        }
    }
    tally->check(compared > kLeastDarkPixels && changed * kPerMille <= compared * kChannelsPerMillePixel,
                 text::format("shafts mode: %d pixels in no sunlit fog unchanged (%d channels changed, at most %.1f "
                              "levels)",
                              compared, changed, largest));
}

// After the camera moved: no NaN, no ghost (the history near the new view's exact answer).
void compare_moved(const View& view, Tally* tally)
{
    render::vulkan::RtFogTexels history;
    render::vulkan::RtFogTexels info;
    if (!read_fog(render::vulkan::RtFogImage::kHistory, &history) ||
        !read_fog(render::vulkan::RtFogImage::kHistoryInfo, &info)) {
        tally->check(false, "the history read back after the move");
        return;
    }
    std::vector<double> errors;
    int kept = 0;
    int restarted = 0;
    for (std::size_t i = 0; i < history.texels.size(); ++i) {
        const std::array<float, 4>& was = core::at(info.texels, i);
        const Pixel pixel = pixel_of(was, 2);
        if (seen_at(view, pixel).solid == kNoSurface) {
            continue;
        }
        kept += core::at(was, 1) > kRestarted ? 1 : 0;
        restarted += core::at(was, 1) < kRestarted ? 1 : 0;
        errors.push_back(light_error(core::at(history.texels, i), exact_at(view, pixel, false)));
    }
    tally->check(count_not_finite(history) == 0, "no NaN after the move");
    tally->check(kept > kLeastKept, text::format("histories kept across the move: %d (restarted %d)", kept, restarted));
    tally->check(percentile(errors, kNinetyFifth) < kMostGhost95,
                 text::format("no ghost: 95th percentile error after the move %.4f", percentile(errors, kNinetyFifth)));
}

void compare_untouched(const std::vector<uint8_t>& texels, const std::vector<uint8_t>& plain, Tally* tally)
{
    constexpr std::size_t kAlphaByte = 3;
    int differ = 0;
    for (std::size_t i = 0; i < texels.size(); ++i) {
        differ += (i % kPixelBytes) != kAlphaByte && core::at(texels, i) != core::at(plain, i) ? 1 : 0;
    }
    tally->check(differ == 0, text::format("the picture as without the pass (%d bytes differ)", differ));
}

// --- the cases -----------------------------------------------------------------------------------------------------

render::vulkan::RtFogSettings settings_of(bool shafts)
{
    return {.on = true,
            .density = 1.0F,
            .height = 0.0F,
            .anisotropy = kAnisotropy,
            .steps = kSteps,
            .mode = shafts ? render::vulkan::RtFogMode::kShafts : render::vulkan::RtFogMode::kReplace,
            .hdr = false,
            .haze = false,
            .air = render::rt::AirLook::kNone, // (the street air: its own cases)
            .scale = march_scale()};
}

void use_settings(const render::vulkan::RtFogSettings& settings, VkAccelerationStructureKHR scene)
{
    render::vulkan::set_rt_fog_settings(settings);
    render::vulkan::rt_fog_set_scene(scene);
}

void run_replace(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    text::print("the level's fog replaced (mode 0, %u steps):\n", kSteps);
    use_settings(settings_of(false), scene.top.handle);
    Targets targets;
    const bool made = make_targets(gpu, kTargetWidth, kTargetHeight, &targets);
    tally->check(made, "the targets made");
    if (made) {
        const View view = view_from(kEye, kTargetWidth, kTargetHeight);
        tally->check(draw_frame(gpu, targets, kEye) == 1, "the listener recorded the passes");
        tally->check(render::vulkan::rt_fog_replaces_level_fog(), "the draws leave the level's fog out");
        compare_raw(view, kFirstMarch, false, tally);
        text::print("  the first frame (its history seeded - vulkan_rt_history.h):\n");
        compare_converged(view, tally);
        int passes = 1;
        for (int frame = 1; frame < kStillFrames; ++frame) {
            passes += static_cast<int>(draw_frame(gpu, targets, kEye));
        }
        tally->check(passes == kStillFrames, text::format("%d frames, %d passes", kStillFrames, passes));
        compare_raw(view, kFirstMarch + kStillFrames - 1, false, tally);
        compare_converged(view, tally);
        compare_shaft(view, tally);
        std::vector<uint8_t> texels;
        const bool read = read_colour(targets, &texels);
        tally->check(read, "the target read back");
        if (read) {
            compare_composite(view, texels, false, tally);
        }
        tally->check(draw_frame(gpu, targets, kMovedEye) == 1, "the moved camera's frame");
        compare_moved(view_from(kMovedEye, kTargetWidth, kTargetHeight), tally);
    }
    release_targets(gpu, targets);
}

void run_shafts(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    text::print("shafts over the level's fog (mode 1):\n");
    use_settings(settings_of(true), scene.top.handle);
    Targets targets;
    const bool made = make_targets(gpu, kTargetWidth, kTargetHeight, &targets);
    tally->check(made, "the targets made");
    if (made) {
        const View view = view_from(kEye, kTargetWidth, kTargetHeight);
        tally->check(draw_frame(gpu, targets, kEye) == 1, "the listener recorded the passes");
        tally->check(!render::vulkan::rt_fog_replaces_level_fog(), "the draws keep the level's fog");
        compare_raw(view, kFirstMarch, true, tally);
        for (int frame = 1; frame < kStillFrames; ++frame) {
            draw_frame(gpu, targets, kEye);
        }
        std::vector<uint8_t> texels;
        const bool read = read_colour(targets, &texels);
        tally->check(read, "the target read back");
        if (read) {
            compare_composite(view, texels, true, tally);
            compare_shafts_untouched(view, texels, tally);
        }
    }
    release_targets(gpu, targets);
}

// The pass skipped: the picture as drawn without it.
void run_untouched(render::Gpu& gpu, const char* name, const render::vulkan::RtFogSettings& settings,
                   VkAccelerationStructureKHR scene, bool fog_on, Tally* tally)
{
    text::print("%s:\n", name);
    Targets targets;
    const bool made = make_targets(gpu, kTargetWidth, kTargetHeight, &targets);
    tally->check(made, "the targets made");
    if (made) {
        use_settings({}, VK_NULL_HANDLE);
        draw_frame(gpu, targets, kEye, fog_on);
        std::vector<uint8_t> plain;
        const bool read_plain = read_colour(targets, &plain);
        use_settings(settings, scene);
        tally->check(draw_frame(gpu, targets, kEye, fog_on) == 0, "no pass recorded");
        tally->check(!render::vulkan::rt_fog_replaces_level_fog(), "the draws keep the level's fog");
        std::vector<uint8_t> texels;
        const bool read = read_plain && read_colour(targets, &texels);
        tally->check(read, "the target read back");
        if (read) {
            compare_untouched(texels, plain, tally);
        }
    }
    release_targets(gpu, targets);
}

// No fog in the level, rt_fog_haze=1: the haze's sunlit light added - nothing darker, the shaft lighter, the draws'
// own fog kept.
void run_haze(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    text::print("no fog in the level, the haze (rt_fog_haze=1):\n");
    Targets targets;
    const bool made = make_targets(gpu, kTargetWidth, kTargetHeight, &targets);
    tally->check(made, "the targets made");
    if (!made) {
        return;
    }
    use_settings({}, VK_NULL_HANDLE);
    draw_frame(gpu, targets, kEye, false);
    std::vector<uint8_t> plain;
    const bool read_plain = read_colour(targets, &plain);
    render::vulkan::RtFogSettings settings = settings_of(false);
    settings.haze = true;
    use_settings(settings, scene.top.handle);
    tally->check(draw_frame(gpu, targets, kEye, false) == 1, "the pass recorded");
    tally->check(!render::vulkan::rt_fog_replaces_level_fog(), "the draws keep the level's fog");
    render::vulkan::RtFogTexels raw;
    tally->check(
        read_fog(render::vulkan::RtFogImage::kRaw, &raw) && count_not_finite(raw) == 0 &&
            std::ranges::all_of(raw.texels, [](const std::array<float, 4>& texel) { return texel.back() == 1.0F; }),
        "the march: no NaN, nothing dimmed (T = 1)");
    std::vector<uint8_t> texels;
    const bool read = read_plain && read_colour(targets, &texels);
    tally->check(read, "the target read back");
    if (read) {
        int darker = 0;
        int lighter = 0;
        for (std::size_t i = 0; i < texels.size(); ++i) {
            const int difference = static_cast<int>(core::at(texels, i)) - static_cast<int>(core::at(plain, i));
            darker += difference < -1 ? 1 : 0;
            lighter += difference > 1 ? 1 : 0;
        }
        tally->check(darker == 0 && lighter > kLeastLighterChannels,
                     text::format("nothing darker (%d channels), the shaft lighter (%d channels)", darker, lighter));
    }
    release_targets(gpu, targets);
}

// --- (added by the volumetric light) the clock's light ------------------------------------------------------------

constexpr Vec3 kOtherLightTravels{-0.35, -0.45, 1.0}; // (the sun moved round: the shaft through the window moves)
constexpr std::array<float, 3> kMoonColour{0.16F, 0.19F, 0.27F}; // (the clock's moon: weak and blue)
constexpr double kMostMoonRatioError = 0.25; // the moon's shafts' light over the sun's against its colour's luma
constexpr double kLeastBluer = 1.3;          // the moon's shafts' blue over red against the sun's

// The clock lighting the scene with its light toward `toward` (the level relit, a baked sun found - as rt_sun.h's
// resolve_scene_sun asks before it takes the clock's light).
void clock_light(const Vec3& toward, const std::array<float, 3>& colour, bool moon)
{
    namespace rt = render::rt;
    light_toward() = normalised(toward);
    clock_lights() = true;
    // (the level's ground at y 0: the street air's - added by the street air)
    rt::set_baked_sun({.scene = 1,
                       .found = true,
                       .toward = floats(normalised(step(Vec3{}, kLightTravels, -1.0))),
                       .confidence = 1.0F,
                       .ground = 0.0F,
                       .ground_known = true});
    rt::set_sun_relight({.relights = true});
    rt::DayLight day;
    day.on = true;
    day.light = floats(light_toward());
    day.moon = day.light;
    day.sun = moon ? std::array<float, 3>{0.0F, -0.5F, 0.866F} : day.light;
    day.moonlit = moon;
    day.light_colour = colour;
    day.light_radius = 0.0F; // (a point: the exact integral's)
    rt::set_day_light(day);
}

void clock_off()
{
    namespace rt = render::rt;
    rt::set_day_light({});
    rt::set_sun_relight({});
    rt::set_baked_sun({});
    clock_lights() = false;
    light_toward() = normalised(step(Vec3{}, kLightTravels, -1.0));
}

// The converged history's mean error against the exact integral for the light now.
double converged_error(const View& view)
{
    render::vulkan::RtFogTexels history;
    render::vulkan::RtFogTexels info;
    if (!read_fog(render::vulkan::RtFogImage::kHistory, &history) ||
        !read_fog(render::vulkan::RtFogImage::kHistoryInfo, &info)) {
        return 1.0;
    }
    std::vector<double> errors;
    for (std::size_t i = 0; i < history.texels.size(); ++i) {
        Fogged want;
        if (block_light(view, pixel_of(core::at(info.texels, i), 2), &want)) {
            errors.push_back(light_error(core::at(history.texels, i), want));
        }
    }
    return errors.empty() ? 1.0 : mean(errors);
}

// The history's light summed over every texel (rgb).
std::array<double, 3> history_sum()
{
    render::vulkan::RtFogTexels history;
    std::array<double, 3> sum{};
    if (read_fog(render::vulkan::RtFogImage::kHistory, &history)) {
        for (const std::array<float, 4>& texel : history.texels) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                core::at(sum, channel) += core::at(texel, channel);
            }
        }
    }
    return sum;
}

void run_clock(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    namespace rt = render::rt;
    text::print("the clock's light (DayLight: the sun moved, the moon at night):\n");
    use_settings(settings_of(false), scene.top.handle);
    Targets targets;
    const bool made = make_targets(gpu, kTargetWidth, kTargetHeight, &targets);
    tally->check(made, "the targets made");
    if (made) {
        const View view = view_from(kEye, kTargetWidth, kTargetHeight);
        const Vec3 first = normalised(step(Vec3{}, kLightTravels, -1.0));
        const Vec3 other = normalised(step(Vec3{}, kOtherLightTravels, -1.0));
        std::array<double, 2> own_error{};
        std::array<double, 2> other_error{};
        for (std::size_t which = 0; which < 2; ++which) {
            const Vec3& toward = which == 0 ? first : other;
            clock_light(toward, {1.0F, 1.0F, 1.0F}, false);
            for (int frame = 0; frame < kStillFrames; ++frame) {
                draw_frame(gpu, targets, kEye);
            }
            const rt::SunUsed used = rt::sun_used(rt::SunUser::kFog);
            const Vec3 noted{core::at(used.toward, 0), core::at(used.toward, 1), core::at(used.toward, 2)};
            tally->check(rt::scene_sun().origin == rt::SunOrigin::kClock && used.have && dot(noted, toward) > 0.99999,
                         text::format("  light %zu: the scene's sun the clock's, the fog lit by it", which + 1));
            core::at(own_error, which) = converged_error(view);
            light_toward() = which == 0 ? other : first;
            core::at(other_error, which) = converged_error(view);
            light_toward() = toward;
        }
        for (std::size_t which = 0; which < 2; ++which) {
            tally->check(core::at(own_error, which) < kMostConvergedMean &&
                             core::at(other_error, which) > 3.0 * core::at(own_error, which),
                         text::format("  light %zu: the history against its own direction's integral %.4f, the other "
                                      "direction's %.4f (the shaft moved with the light)",
                                      which + 1, core::at(own_error, which), core::at(other_error, which)));
        }
        // the moon: the haze (no level fog) lit by its light - the moon's colour's share of the sun's, bluer
        render::vulkan::RtFogSettings haze = settings_of(false);
        haze.haze = true;
        use_settings(haze, scene.top.handle);
        std::array<std::array<double, 3>, 2> sums{};
        for (std::size_t which = 0; which < 2; ++which) {
            clock_light(first, which == 0 ? std::array<float, 3>{1.0F, 1.0F, 1.0F} : kMoonColour, which == 1);
            for (int frame = 0; frame < kStillFrames; ++frame) {
                draw_frame(gpu, targets, kEye, false);
            }
            core::at(sums, which) = history_sum();
        }
        const auto luma = [](const std::array<double, 3>& c) {
            return (0.2126 * core::at(c, 0)) + (0.7152 * core::at(c, 1)) + (0.0722 * core::at(c, 2));
        };
        const double moon_luma = luma({core::at(kMoonColour, 0), core::at(kMoonColour, 1), core::at(kMoonColour, 2)});
        const double ratio = luma(sums.front()) > 0.0 ? luma(sums.back()) / luma(sums.front()) : 0.0;
        const auto blue_over_red = [](const std::array<double, 3>& c) {
            return core::at(c, 0) > 0.0 ? core::at(c, 2) / core::at(c, 0) : 0.0;
        };
        tally->check(ratio > 0.0 && std::abs((ratio / moon_luma) - 1.0) < kMostMoonRatioError &&
                         blue_over_red(sums.back()) > kLeastBluer * blue_over_red(sums.front()),
                     text::format("  the moon's shafts in the haze: %.3f of the sun's light (its colour's luma %.3f), "
                                  "blue over red %.2f against the sun's %.2f",
                                  ratio, moon_luma, blue_over_red(sums.back()), blue_over_red(sums.front())));
    }
    clock_off();
    release_targets(gpu, targets);
}

// --- (added by the street air) ----------------------------------------------------------------------------------------

// The model's numbers: a horizontal view 2 m over the ground through the street air.
constexpr double kEyeMetres = 2.0;
constexpr double kStreetMetres = 20.0;
constexpr double kFarMetres = 300.0;
constexpr double kLeastStreetDepth = 0.02;  // a dusk (night) street's across: enough for shafts
constexpr double kMostFarDepth = 0.75;      // ... and a 300-metre view's at dusk: not whited out
constexpr double kMostFarDayDepth = 0.15;   // ... by day
constexpr double kLeastGlowLevels = 4.0;    // a lamp's glow in the night's air (levels of 255)
// (changed by the cinematic air: the mist in the look's volume round the eye - air_reach)
double horizontal_depth(const render::rt::StreetAir& air, double metres, const render::rt::AirArt& art = {})
{
    constexpr int kSamples = 600;
    const double at_eye = air.height > 0.0 ? std::exp(-kEyeMetres * render::rt::kUnitsPerMetre / air.height) : 1.0;
    const double snow =
        air.snow_height > 0.0 ? air.snow * std::exp(-kEyeMetres * render::rt::kUnitsPerMetre / air.snow_height) : 0.0;
    const double length = metres * render::rt::kUnitsPerMetre;
    double reach = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        reach += render::rt::air_reach((static_cast<double>(i) + 0.5) * length / kSamples, art) / kSamples;
    }
    return ((air.extinction * at_eye * reach) + snow) * length;
}

// (added by the cinematic air) the cinematic step's least: a beam across a street at dusk seen from the side and a lamp's
// cone (levels of 255), x the clear air's at least; along 300 m at dusk its optical depth and the veil of open sunlit air
// seen from the side (dusk, noon) at most
constexpr double kLeastCinematicBeam = 10.0;
constexpr double kLeastCinematicCone = 3.5;
constexpr double kLeastCinematicOverClear = 4.0;
constexpr double kMostCinematicFarDepth = 0.6;
constexpr double kMostCinematicNoonDepth = 0.15;
constexpr double kMostDuskVeil = 1.2; // (linear light, before the tone map; the game at dusk: a few tens of levels)
constexpr double kMostNoonVeil = 0.16;

void run_air_model(Tally* tally)
{
    namespace rt = render::rt;
    using rt::AirLook;
    text::print("the street air's model (rt_volume_light.h street_air; its looks - AirLook):\n");
    const rt::StreetAir dusk = rt::street_air(2.0, {}, AirLook::kClear);
    const rt::StreetAir night = rt::street_air(-20.0, {}, AirLook::kClear);
    const rt::StreetAir day = rt::street_air(50.0, {}, AirLook::kClear);
    const rt::StreetAir misty = rt::street_air(2.0, {}, AirLook::kMisty);
    const rt::StreetAir wet = rt::street_air(50.0, {.rain = true}, AirLook::kClear);
    const rt::StreetAir snowing = rt::street_air(50.0, {.snow = true}, AirLook::kClear);
    const rt::StreetAir none = rt::street_air(2.0, {}, AirLook::kNone);
    const rt::StreetAir cine_dusk = rt::street_air(2.0, {}, AirLook::kCinematic);
    const rt::StreetAir cine_night = rt::street_air(-20.0, {}, AirLook::kCinematic);
    const rt::StreetAir cine_day = rt::street_air(50.0, {}, AirLook::kCinematic);
    tally->check(horizontal_depth(dusk, kStreetMetres) > kLeastStreetDepth &&
                     horizontal_depth(night, kStreetMetres) > kLeastStreetDepth,
                 text::format("  clear: across a 20 m street 2 m up: dusk's optical depth %.3f, night's %.3f",
                              horizontal_depth(dusk, kStreetMetres), horizontal_depth(night, kStreetMetres)));
    tally->check(horizontal_depth(dusk, kFarMetres) < kMostFarDepth &&
                     horizontal_depth(day, kFarMetres) < kMostFarDayDepth,
                 text::format("  clear: along 300 m: dusk's %.3f, the day's %.3f (the far end not whited out)",
                              horizontal_depth(dusk, kFarMetres), horizontal_depth(day, kFarMetres)));
    const rt::AirArt cine = rt::air_art(AirLook::kCinematic);
    const double cine_far = horizontal_depth(cine_dusk, kFarMetres, cine);
    const double cine_noon = horizontal_depth(cine_day, kFarMetres, cine);
    // (the veil: open air in the sun all the way, seen at a right angle to the light - (1 - T) x its light per unit)
    const double side_light = (1.0 - render::vulkan::kFogAerialSkyShare) * rt::kHazeSunlit * cine.glow *
                              rt::air_phase(0.0, render::vulkan::kRtFogAnisotropy, cine);
    const double dusk_veil = (1.0 - std::exp(-cine_far)) * side_light;
    const double noon_veil = (1.0 - std::exp(-cine_noon)) * side_light;
    tally->check(cine_far < kMostCinematicFarDepth && cine_noon < kMostCinematicNoonDepth && dusk_veil < kMostDuskVeil &&
                     noon_veil < kMostNoonVeil,
                 text::format("  cinematic: along 300 m: dusk's %.3f, noon's %.3f (its volume %.0f m round the eye: the "
                              "far world through the haze alone); open sunlit air from the side veils dusk %.1f levels, "
                              "noon %.1f; across 20 m at dusk %.3f, at night %.3f",
                              cine_far, cine_noon, cine.range, dusk_veil * kLevels, noon_veil * kLevels,
                              horizontal_depth(cine_dusk, kStreetMetres, cine),
                              horizontal_depth(cine_night, kStreetMetres, cine)));
    tally->check(dusk.extinction > 3.0 * day.extinction && night.extinction > 3.0 * day.extinction &&
                     misty.extinction > 1.5 * cine_dusk.extinction && cine_dusk.extinction > dusk.extinction &&
                     cine_day.extinction > day.extinction && wet.extinction > day.extinction && snowing.snow > 0.0 &&
                     day.snow == 0.0 && none.extinction == 0.0,
                 text::format("  clear: denser at dusk (x %.1f the day's) and night (x %.1f), wet x %.1f, falling snow "
                              "%.2g a unit; cinematic x %.1f clear's at dusk, x %.1f at noon; misty x %.1f cinematic's; "
                              "none: none",
                              dusk.extinction / day.extinction, night.extinction / day.extinction,
                              wet.extinction / day.extinction, snowing.snow, cine_dusk.extinction / dusk.extinction,
                              cine_day.extinction / day.extinction, misty.extinction / cine_dusk.extinction));
    // a street lamp at night (its head 4.5 m up; intensity 4, core 20 units, reach 12 cores - rt_lights.h's emitters)
    // seen from 15 m away, 2 m up: the glow round it looking at it, and the cone under it seen from the side (a ray
    // 1.5 m up passing under it) - the air's light per the pass's march (x the haze's light share, the look's phase)
    const auto lamp_air = [](const rt::StreetAir& night, const rt::AirArt& art, const Vec3& from, const Vec3& to) {
        constexpr Vec3 kLamp{0.0, 4.5 * render::rt::kUnitsPerMetre, 0.0};
        constexpr double kIntensity = 4.0;
        constexpr double kCore = 20.0;
        constexpr double kReach = 240.0;
        constexpr int kSamples = 4000;
        const Vec3 along = minus(to, from);
        const double length = std::hypot(x_of(along), y_of(along), z_of(along));
        const Vec3 direction = normalised(along);
        const double scatter = (1.0 - render::vulkan::kFogAerialSkyShare) * render::rt::kHazeSunlit;
        double light = 0.0;
        for (int i = 0; i < kSamples; ++i) {
            const double s = (static_cast<double>(i) + 0.5) * length / kSamples;
            const Vec3 at = step(from, direction, s);
            const Vec3 to_lamp = minus(kLamp, at);
            const double d = std::hypot(x_of(to_lamp), y_of(to_lamp), z_of(to_lamp));
            const double x = d / kReach;
            const double window = d < kReach ? (1.0 - (x * x * x * x)) : 0.0;
            const double falloff = window * window * kCore * kCore / ((kCore * kCore) + (d * d));
            const double sigma = night.extinction * std::exp(-std::max(y_of(at), 0.0) / night.height);
            light += sigma * (length / kSamples) * kIntensity * falloff * art.lamp_glow *
                     rt::air_phase(dot(direction, normalised(to_lamp)), render::vulkan::kRtFogAnisotropy, art) * scatter;
        }
        return light;
    };
    const double metre = render::rt::kUnitsPerMetre;
    const Vec3 glow_from{-15.0 * metre, 2.0 * metre, 0.0};
    const Vec3 glow_to{0.0, 4.5 * metre, 0.0};
    const Vec3 cone_from{-15.0 * metre, 1.5 * metre, 6.0 * metre};
    const Vec3 cone_to{15.0 * metre, 1.5 * metre, -6.0 * metre};
    const rt::AirArt clear_art = rt::air_art(AirLook::kClear);
    const rt::AirArt cine_art = rt::air_art(AirLook::kCinematic);
    const double glow = lamp_air(night, clear_art, glow_from, glow_to);
    const double cone = lamp_air(night, clear_art, cone_from, cone_to);
    const double cine_glow = lamp_air(cine_night, cine_art, glow_from, glow_to);
    const double cine_cone = lamp_air(cine_night, cine_art, cone_from, cone_to);
    tally->check(glow * kLevels > kLeastGlowLevels,
                 text::format("  clear: a street lamp at night: its glow looking at it %.1f levels, its cone from the "
                              "side %.1f (the air's forward scattering: halos; cones from the side faint)",
                              glow * kLevels, cone * kLevels));
    tally->check(cine_cone * kLevels > kLeastCinematicCone && cine_cone > kLeastCinematicOverClear * cone,
                 text::format("  cinematic: the lamp's glow %.1f levels, its cone from the side %.1f (x %.1f clear's)",
                              cine_glow * kLevels, cine_cone * kLevels, cone > 0.0 ? cine_cone / cone : 0.0));
    // a beam across a street at dusk seen from the side: a sunlit slab 10 m wide (the alley's), the view 2 m up crossing
    // it at 100 degrees from the light's way - its light over the shade's (single scattering, x the sun's 1)
    const auto beam = [](const rt::StreetAir& air, const rt::AirArt& art) {
        constexpr double kBeamMetres = 10.0;
        constexpr double kSideCosine = -0.17;
        const double scatter = (1.0 - render::vulkan::kFogAerialSkyShare) * render::rt::kHazeSunlit;
        const double sigma = air.extinction * std::exp(-kEyeMetres * render::rt::kUnitsPerMetre / air.height);
        return sigma * kBeamMetres * render::rt::kUnitsPerMetre * scatter * art.glow *
               rt::air_phase(kSideCosine, render::vulkan::kRtFogAnisotropy, art);
    };
    const double clear_beam = beam(dusk, clear_art);
    const double cine_beam = beam(cine_dusk, cine_art);
    tally->check(cine_beam * kLevels > kLeastCinematicBeam && cine_beam > kLeastCinematicOverClear * clear_beam,
                 text::format("  a beam 10 m wide across a street at dusk from the side: clear %.1f levels, cinematic "
                              "%.1f (x %.1f), misty %.1f",
                              clear_beam * kLevels, cine_beam * kLevels, cine_beam / clear_beam,
                              beam(misty, rt::air_art(AirLook::kMisty)) * kLevels));
    // (the relit level's light alone: a scene sun on a level the relit mode doesn't relight lights no air)
    const rt::SceneSun sun{.origin = rt::SunOrigin::kLevel, .toward = {0.0F, 0.5F, 0.866F}};
    tally->check(rt::medium_light_from(sun, {.relights = true}, {}).have &&
                     !rt::medium_light_from(sun, {.relights = false}, {}).have,
                 "  the air lit only on a relit level (its sun the relit mode's)");
}

// The march at a quarter's resolution: one frame's against the processor's, then the history after still frames.
void run_quarter(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    text::print("the march at a quarter's resolution (4 x 4 blocks - rt_fog_quality 0 and 1):\n");
    march_scale() = 4;
    use_settings(settings_of(false), scene.top.handle);
    Targets targets;
    const bool made = make_targets(gpu, kTargetWidth, kTargetHeight, &targets);
    tally->check(made, "the targets made");
    if (made) {
        const View view = view_from(kEye, kTargetWidth, kTargetHeight);
        tally->check(draw_frame(gpu, targets, kEye) == 1, "the listener recorded the passes");
        compare_raw(view, kFirstMarch, false, tally);
        for (int frame = 1; frame < kStillFrames; ++frame) {
            draw_frame(gpu, targets, kEye);
        }
        compare_raw(view, kFirstMarch + kStillFrames - 1, false, tally);
        compare_converged(view, tally, false);
    }
    release_targets(gpu, targets);
    march_scale() = 2;
}

// The street: the sun 4 degrees up behind the near row, its light through the alley into the street's air.
constexpr Vec3 kStreetToward{0.17, 0.0703, -1.0}; // (about 4 degrees up, a little east of the alley's line)
constexpr Vec3 kStreetEye{600.0, 80.0, kStreetZ + 1900.0};
constexpr Vec3 kStreetAt{0.0, 250.0, kStreetZ};
constexpr float kStreetFarZ = 40000.0F;
constexpr double kStreetFogEnd = 20000.0;
constexpr std::array<float, 3> kStreetShade{0.45F, 0.42F, 0.40F}; // (the level's fog colour: the air's shade)
constexpr double kBeamLeast = 0.004;     // a texel's ray crosses the beam: the exact light above the shade's by this
constexpr double kShadeMost = 0.0005;    // ... crosses none
constexpr double kLeastStreetContrast = 0.008; // the beam over the shade at least (2 levels)
constexpr double kMostBeamError = 0.25;        // the beam's light against the exact integral's (share)
constexpr std::size_t kLeastBeamTexels = 100;
constexpr double kNinetieth = 0.9;

// (added by the cinematic air) the street seen from the side: the eye in the street 25 m east of the alley's beam (in the
// cinematic air's volume round the eye), looking west across it (the view at about 100 degrees from the light's way), a
// little down - the ground beyond the beam
constexpr Vec3 kSideEye{1000.0, 80.0, kStreetZ + 800.0};
constexpr Vec3 kSideAt{0.0, 40.0, kStreetZ + 800.0};
constexpr double kLeastSideContrast = 0.04; // the cinematic beam seen from the side over the shade at least (10 levels)

void street_on(render::rt::AirLook look)
{
    namespace rt = render::rt;
    const Vec3 toward = normalised(kStreetToward);
    const double elevation = std::asin(y_of(toward)) * 180.0 / std::numbers::pi;
    media() = {.start = 0.0,
               .end = kStreetFogEnd,
               .density = 0.0,
               .colour = kStreetShade,
               .air = rt::street_air(elevation, {}, look),
               .ground = 0.0,
               .art = rt::air_art(look)};
    look_at() = kStreetAt;
    far_z() = kStreetFarZ;
    clock_light(toward, {1.0F, 1.0F, 1.0F}, false);
}

void street_off()
{
    clock_off();
    media() = Media{};
    look_at() = kAt;
    far_z() = kFarZ;
}

double luma3(const std::array<double, 3>& c)
{
    return (0.2126 * core::at(c, 0)) + (0.7152 * core::at(c, 1)) + (0.0722 * core::at(c, 2));
}

// A street at dusk in the street air alone, seen from `eye` toward `at` in a look: the history against the exact
// integral, the beam through the alley over the shade by `least` at least. Its core's contrast (the brightest tenth of
// the beam's texels over the shade's mean), -1 when it couldn't be measured.
struct StreetCase {
    const char* name = "";
    render::rt::AirLook look = render::rt::AirLook::kClear;
    Vec3 eye{};
    Vec3 at{};
    double least = 0.0;
    bool ground_only = false; // the texels whose rays end on the ground (the beam crossed low, nothing past it)
    double beam_least = kBeamLeast; // a texel's ray crosses the beam: its exact light over the shade's by this
};
double run_street(render::Gpu& gpu, const Scene& scene, const StreetCase& how, Tally* tally)
{
    text::print("%s (the level's fog's thickness 0; rt_fog_air=%s):\n", how.name, render::rt::air_look_name(how.look));
    street_on(how.look);
    look_at() = how.at;
    render::vulkan::RtFogSettings settings = settings_of(false);
    settings.density = 0.0F;
    settings.air = how.look;
    use_settings(settings, scene.top.handle);
    Targets targets;
    double contrast = -1.0;
    const bool made = make_targets(gpu, kTargetWidth, kTargetHeight, &targets);
    tally->check(made, "the targets made");
    if (made) {
        const View view = view_from(how.eye, kTargetWidth, kTargetHeight);
        for (int frame = 0; frame < kStillFrames; ++frame) {
            draw_frame(gpu, targets, how.eye);
        }
        compare_converged(view, tally, false);
        render::vulkan::RtFogTexels history;
        render::vulkan::RtFogTexels info;
        const bool read = read_fog(render::vulkan::RtFogImage::kHistory, &history) &&
                          read_fog(render::vulkan::RtFogImage::kHistoryInfo, &info);
        std::vector<double> inside;
        std::vector<double> inside_want;
        std::vector<double> outside;
        for (std::size_t i = 0; read && i < history.texels.size(); i += 2) {
            if (how.ground_only && std::get<0>(core::at(info.texels, i)) < 0.0F) {
                continue; // (the side view's sky: its rays leave the street over the roofs' shadow)
            }
            const Pixel pixel = pixel_of(core::at(info.texels, i), 2);
            const Fogged lit = exact_at(view, pixel, false);
            const Fogged dark = dark_at(view, pixel);
            const double want = luma3(lit.light) - luma3(dark.light);
            const std::array<float, 4>& texel = core::at(history.texels, i);
            const double got =
                luma3({core::at(texel, 0), core::at(texel, 1), core::at(texel, 2)}) - luma3(dark.light);
            if (want > how.beam_least) {
                inside.push_back(got);
                inside_want.push_back(want);
            } else if (want < kShadeMost) {
                outside.push_back(got);
            }
        }
        tally->check(inside.size() > kLeastBeamTexels && outside.size() > kLeastBeamTexels,
                     text::format("  %zu texels' rays cross the beam through the alley, %zu only the shade",
                                  inside.size(), outside.size()));
        // (the beam's brightest tenth: the rays along its core - the rest graze its edges)
        const double core_light = percentile(inside, kNinetieth);
        contrast = core_light - mean(outside);
        tally->check(core_light - mean(outside) > how.least &&
                         std::abs(mean(inside) - mean(inside_want)) < kMostBeamError * mean(inside_want),
                     text::format("  the beam: its core %.4f over the shade (%.1f levels), its mean %.4f (the exact "
                                  "integral's %.4f), the shade's own %.4f",
                                  core_light - mean(outside), (core_light - mean(outside)) * kLevels, mean(inside),
                                  mean(inside_want), mean(outside)));
    }
    release_targets(gpu, targets);
    street_off();
    return contrast;
}

void run_streets(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    using render::rt::AirLook;
    run_street(gpu, scene,
               {.name = "a street at dusk in the street air alone, toward the low sun",
                .look = AirLook::kClear,
                .eye = kStreetEye,
                .at = kStreetAt,
                .least = kLeastStreetContrast},
               tally);
    // (added by the cinematic air) the beam seen from the side: the cinematic step's over the shade by a clear margin,
    // several times the physical air's
    const double clear = run_street(gpu, scene,
                                    {.name = "the street from the side, the clear air",
                                     .look = AirLook::kClear,
                                     .eye = kSideEye,
                                     .at = kSideAt,
                                     .least = 0.0,
                                     .ground_only = true,
                                     .beam_least = 0.1 * kBeamLeast}, // (the clear air's faint beam)
                                    tally);
    const double cinematic = run_street(gpu, scene,
                                        {.name = "the street from the side, the cinematic air",
                                         .look = AirLook::kCinematic,
                                         .eye = kSideEye,
                                         .at = kSideAt,
                                         .least = kLeastSideContrast,
                                         .ground_only = true},
                                        tally);
    tally->check(clear >= 0.0 && cinematic > kLeastCinematicOverClear * clear,
                 text::format("  from the side: the cinematic beam %.1f levels over its shade, the clear air's %.1f (x %.1f)",
                              cinematic * kLevels, clear * kLevels, clear > 0.0 ? cinematic / clear : 0.0));
}

// (added by the distinct shafts) The bar's shadow in the beam, seen from the side at Medium's resolution (a quarter's,
// 1280 x 720 - the game's window): a thin sheet of shade through the lit air, edge-on - a dark line across the beam.
// Down columns of texels, where the exact integral shows the line (a notch: the light on both sides of it at least
// kNotchShare over its floor), the pass's history's line against it: its width (the texels under half its depth) within
// kMostNotchWidthGap texels, its depth at least kLeastNotchDepth of the exact one's - the air's shadows sharp, not
// smeared by the march, the history or the map.
constexpr uint32_t kBarWidth = 1280;
constexpr uint32_t kBarHeight = 720;
constexpr int kNotchReach = 6;            // texels each side of a notch its sides are looked for in
constexpr double kNotchShare = 0.3;       // a notch at least this share of its sides' light deep
constexpr double kLeastNotchLight = 0.01; // (its sides lit at least this)
constexpr int kNotchFollow = 3;           // the pass's notch looked for this many texels round the exact one's
constexpr double kMostNotchWidthGap = 1.0;
constexpr double kLeastNotchDepth = 0.5;
constexpr std::size_t kLeastNotchColumns = 5;
struct Notch {
    int at = -1;
    double depth = 0.0;
    int width = 0;
};
// The notch in a column's light (`light` top to bottom) at `centre` (or the deepest anywhere when centre < 0)
Notch notch_in(const std::vector<double>& light, int centre)
{
    Notch best;
    const int count = static_cast<int>(light.size());
    const int from = centre < 0 ? kNotchReach : std::max(kNotchReach, centre - kNotchFollow);
    const int to = centre < 0 ? count - kNotchReach : std::min(count - kNotchReach, centre + kNotchFollow + 1);
    for (int y = from; y < to; ++y) {
        double above = 0.0;
        double below = 0.0;
        for (int k = 1; k <= kNotchReach; ++k) {
            above = std::max(above, core::at(light, static_cast<std::size_t>(y - k)));
            below = std::max(below, core::at(light, static_cast<std::size_t>(y + k)));
        }
        const double sides = std::min(above, below);
        const double depth = sides - core::at(light, static_cast<std::size_t>(y));
        const bool counts = centre >= 0 || (sides > kLeastNotchLight && depth > kNotchShare * sides);
        if (counts && depth > best.depth) {
            best = {.at = y, .depth = depth, .width = 0};
        }
    }
    if (best.at >= 0) {
        const double half = core::at(light, static_cast<std::size_t>(best.at)) + (0.5 * best.depth);
        int top = best.at;
        int bottom = best.at;
        while (top > 0 && core::at(light, static_cast<std::size_t>(top - 1)) < half) {
            --top;
        }
        while (bottom + 1 < count && core::at(light, static_cast<std::size_t>(bottom + 1)) < half) {
            ++bottom;
        }
        best.width = bottom - top + 1;
    }
    return best;
}

void run_bar(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    text::print("the bar's shadow in the beam from the side (cinematic, a quarter's resolution at %ux%u):\n", kBarWidth,
                kBarHeight);
    street_on(render::rt::AirLook::kCinematic);
    look_at() = kSideAt;
    march_scale() = 4;
    render::vulkan::RtFogSettings settings = settings_of(false);
    settings.density = 0.0F;
    settings.air = render::rt::AirLook::kCinematic;
    use_settings(settings, scene.top.handle);
    Targets targets;
    const bool made = make_targets(gpu, kBarWidth, kBarHeight, &targets);
    tally->check(made, "the targets made");
    if (made) {
        const View view = view_from(kSideEye, kBarWidth, kBarHeight);
        for (int frame = 0; frame < kStillFrames; ++frame) {
            draw_frame(gpu, targets, kSideEye);
        }
        render::vulkan::RtFogTexels history;
        render::vulkan::RtFogTexels info;
        bool read = false;
        {
            render::vulkan::Backend& state = render::vulkan::backend();
            const std::scoped_lock lock(state.mutex);
            const Rect whole{0, 0, kBarWidth, kBarHeight};
            read = render::vulkan::read_rt_fog_image(state, whole, render::vulkan::RtFogImage::kHistory, &history) &&
                   render::vulkan::read_rt_fog_image(state, whole, render::vulkan::RtFogImage::kHistoryInfo, &info);
        }
        tally->check(read && history.texels.size() == info.texels.size(), "the history read back");
        std::vector<double> width_gaps;
        std::vector<double> depth_shares;
        std::vector<double> exact_widths;
        constexpr uint32_t kColumnStep = 8;
        for (uint32_t x = kColumnStep / 2; read && x < history.width; x += kColumnStep) {
            std::vector<double> exact(history.height);
            std::vector<double> got(history.height);
            for (uint32_t y = 0; y < history.height; ++y) {
                const std::size_t i = (static_cast<std::size_t>(y) * history.width) + x;
                const std::array<float, 4>& texel = core::at(history.texels, i);
                core::at(got, y) = luma3({core::at(texel, 0), core::at(texel, 1), core::at(texel, 2)});
                core::at(exact, y) = luma3(exact_at(view, pixel_of(core::at(info.texels, i), 2), false).light);
            }
            const Notch want = notch_in(exact, -1);
            if (want.at < 0) {
                continue;
            }
            const Notch seen = notch_in(got, want.at);
            width_gaps.push_back(std::abs(static_cast<double>(seen.width - want.width)));
            depth_shares.push_back(seen.depth / want.depth);
            exact_widths.push_back(want.width);
        }
        tally->check(width_gaps.size() >= kLeastNotchColumns &&
                         mean(width_gaps) <= kMostNotchWidthGap && mean(depth_shares) >= kLeastNotchDepth,
                     text::format("  the bar's line in %zu columns: %.1f texels wide (exact), the pass's width off by %.2f "
                                  "texels (%.1f pixels) on average, its depth %.2f of the exact one's",
                                  width_gaps.size(), mean(exact_widths), mean(width_gaps),
                                  mean(width_gaps) * static_cast<double>(march_scale()), mean(depth_shares)));
    }
    release_targets(gpu, targets);
    march_scale() = 2;
    street_off();
}

// The passes at 2560 x 1440 timed on the device (their own timestamps) at rt_fog_quality 0, 1, 2 - in the street at
// dusk (the street air on, the alley's beam): Medium held to kMostMediumMilliseconds.
constexpr double kMostMediumMilliseconds = 1.0;
void run_perf(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    text::print("cost at %ux%u in the street at dusk (the passes' timestamps on the device, 60 frames each):\n",
                kPerfWidth, kPerfHeight);
    street_on(render::rt::AirLook::kCinematic);
    render::vulkan::RtFogSettings base = settings_of(false);
    base.density = 0.0F;
    base.air = render::rt::AirLook::kCinematic;
    use_settings(base, scene.top.handle); // (on before the targets: their depth made samplable)
    Targets targets;
    if (!make_targets(gpu, kPerfWidth, kPerfHeight, &targets)) {
        text::print("  the targets couldn't be made\n");
        street_off();
        return;
    }
    constexpr int kFrames = 60;
    render::vulkan::Backend& state = render::vulkan::backend();
    const auto timing = [&state] {
        const std::scoped_lock lock(state.mutex);
        render::vulkan::finish_work(state);
        return render::vulkan::rt_fog_timing();
    };
    constexpr std::array<const char*, 3> kNames{"Low", "Medium", "High"};
    for (std::size_t quality = 0; quality < kNames.size(); ++quality) {
        render::vulkan::RtFogSettings settings = base;
        settings.steps = core::at(render::vulkan::kRtFogQualitySteps, quality);
        settings.scale = core::at(render::vulkan::kRtFogQualityScale, quality);
        settings.map = core::at(render::vulkan::kRtFogQualityMap, quality);
        use_settings(settings, scene.top.handle);
        const render::vulkan::RtFogTiming before = timing();
        for (int frame = 0; frame < kFrames; ++frame) {
            draw_frame(gpu, targets, kStreetEye);
            const std::scoped_lock lock(state.mutex);
            render::vulkan::finish_work(state);
        }
        const render::vulkan::RtFogTiming after = timing();
        const uint64_t samples = after.samples - before.samples;
        const double milliseconds =
            samples == 0 ? 0.0 : (after.milliseconds - before.milliseconds) / static_cast<double>(samples);
        text::print("  %-6s (%u steps, 1/%u): %.3f ms (%llu passes timed)\n", core::at(kNames, quality), settings.steps,
                    settings.scale, milliseconds, static_cast<unsigned long long>(samples));
        if (quality == 1) {
            tally->check(samples > 0 && milliseconds < kMostMediumMilliseconds,
                         text::format("  Medium within %.1f ms at %ux%u", kMostMediumMilliseconds, kPerfWidth,
                                      kPerfHeight));
        }
    }
    release_targets(gpu, targets);
    street_off();
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

enum class Opened : uint8_t { kYes, kNoDevice, kNoRayQueries };
Opened open_device(const Options& options, SDL_Window* window)
{
    render::vulkan::set_context_options({.validation = options.validation,
                                         .device_index = options.device,
                                         .on_message = print_line,
                                         .ray_tracing = true},
                                        window);
    // (on before the device: its depth surfaces are made samplable)
    render::vulkan::set_rt_fog_settings(settings_of(false));
    render::GpuContext& context = render::vulkan::gpu_context();
    if (!context.create_instance()) {
        text::print("the device could not be opened\n");
        return Opened::kNoDevice;
    }
    const render::vulkan::Backend& state = render::vulkan::backend();
    text::print("validation layer: %s\n",
                state.context.validation_on() ? "on" : "off (not installed, or not asked for)");
    context.configure({.width = kWindowWidth,
                       .height = kWindowHeight,
                       .windowed = true,
                       .vsync = true,
                       .samples = 0,
                       .sample_quality = 0,
                       .colour_format = value::kFormatX8R8G8B8,
                       .depth_format = value::kFormatD24S8});
    const long made = context.create_device(
        {.width = kWindowWidth, .height = kWindowHeight, .hardware_vertices = true, .samples = 0, .sample_quality = 0});
    if (made != value::kResultOk) {
        text::print("create_device failed (%ld)\n", made);
        context.shutdown();
        return Opened::kNoDevice;
    }
    context.mark_ready();
    text::print("device: %s\n", context.adapter().description.data());
    if (!state.context.ray_tracing_enabled()) {
        text::print("this device has no ray queries (VK_KHR_acceleration_structure + VK_KHR_ray_query) - nothing to "
                    "check\n");
        context.shutdown();
        return Opened::kNoRayQueries;
    }
    return Opened::kYes;
}

int run(const Options& options, SDL_Window* window)
{
    const Opened opened = open_device(options, window);
    if (opened == Opened::kNoRayQueries) {
        return 2;
    }
    if (opened == Opened::kNoDevice) {
        return render::vulkan::loader_version() == 0 ? 2 : 1;
    }
    render::vulkan::Backend& state = render::vulkan::backend();
    Tally tally;
    Scene scene;
    bool built = false;
    {
        const std::scoped_lock lock(state.mutex);
        built = make_scene(state, &scene);
    }
    tally.check(built, "the check's acceleration structures built (a BLAS of the wall and the ground, a TLAS)");
    if (built) {
        render::Gpu& gpu = render::vulkan::gpu();
        run_air_model(&tally); // (added by the street air)
        run_replace(gpu, scene, &tally);
        run_quarter(gpu, scene, &tally); // (added by the street air)
        run_shafts(gpu, scene, &tally);
        run_untouched(gpu, "no fog in the level, rt_fog_haze=0: the pass skipped", settings_of(false), scene.top.handle,
                      false, &tally);
        run_haze(gpu, scene, &tally);
        run_clock(gpu, scene, &tally);  // (added by the volumetric light)
        run_streets(gpu, scene, &tally); // (added by the street air; the side view by the cinematic air)
        run_bar(gpu, scene, &tally);     // (added by the distinct shafts)
        run_untouched(gpu, "no scene: the pass skipped", settings_of(false), VK_NULL_HANDLE, true, &tally);
        render::vulkan::RtFogSettings off = settings_of(false);
        off.on = false;
        run_untouched(gpu, "rt_fog off: no pass", off, scene.top.handle, true, &tally);
        if (options.perf) {
            run_perf(gpu, scene, &tally);
        }
    }
    render::vulkan::rt_fog_set_scene(VK_NULL_HANDLE);
    render::vulkan::set_rt_fog_settings({});
    render::rt::set_sun_override(nullptr);
    {
        const std::scoped_lock lock(state.mutex);
        render::vulkan::finish_work(state);
        release_scene(state, &scene);
    }
    render::vulkan::gpu_context().shutdown();
    tally.check(validation_messages() == 0, text::format("validation messages: %d", validation_messages()));
    text::print("%d checks passed, %d failed\n", tally.passed, tally.failed);
    return tally.failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_rt_fog_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
    if (!render::vulkan::open_loader()) {
        text::print("no Vulkan loader (vulkan-1.dll) for this process\n");
        return 2;
    }
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        text::print("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const int position = window_position(options.display);
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan volumetric fog check", kWindowWidth, kWindowHeight,
                                          SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
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
