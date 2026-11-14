// NOT ORIGINAL - thug2_vulkan_camera_fx_check.exe: the camera effects (gpu_vulkan/vulkan_camera_fx.h) tried on this
// machine. The passes run through rt_scene's listeners as the game's frames run them (rt::view_begins, the depth cleared
// - the view's first pass -, rt::world_view_drawn, present), on a 256 x 128 target filled through a lock. Cases:
//   - everything off: no listener, no motion vectors begun, the picture bit for bit as it was;
//   - the grade: each look (and neutral with the vignette and grain, a look at strength 0.6, an 8-bit target, rt_grade=
//     level for a listed and an unlisted level) against grade_reference, the processor's twin, every channel within a
//     tolerance (a 16-bit float's or a byte's rounding); each look's table at its grid points the look's formula; the
//     looks doing what they say (warm warmer, cool cooler, bleach less saturated, skate video more); neutral with no
//     vignette or grain: no pass;
//   - the depth of field: a box at the focus in front of a far wall - the box's inside bit for bit as it was, the wall
//     blurred (its noise's spread down, its mean kept); focused on the wall instead: the box (now before the focus)
//     blurred, the wall away from it untouched; no focus known, or under a menu: no pass;
//   - the motion blur: the camera moved sideways between two frames over a flat wall (the motion from the depth and
//     the two cameras): vertical stripes smeared across, horizontal stripes left alone, a one-pixel line spread over
//     about the shutter's share of the move with its light kept; a cut (frame_blend::note_cut) and a camera jump: no
//     blur; the game's own screen blur on: none;
//   - the fisheye lens (vulkan_fisheye.h): each case against fisheye_reference, the processor's twin; a straight line
//     across the upper picture bowed (its ends nearer the middle than its middle); the centre at the original camera's
//     scale (a line d pixels off the centre of the widened picture shown widen x d off it); the adapter's vignette
//     darkening the corners and not the centre; a view the camera wasn't widened for (ViewDrawn::lens off: a menu, the
//     editor, split screen): no pass, the picture bit for bit; rt_fisheye off: no listener, ViewLens off;
//   - the cost of the three at 2560 x 1440 (the device's timestamps, printed).
//   thug2_vulkan_camera_fx_check [--device N] [--display N] [--no-validation]
// Exit code: 0 when every check passed, 1 when one failed, 2 when this machine has no Vulkan for this process.
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
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/fisheye_lens.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_scene.h"
#include "engine/sys/frame_blend.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_camera_fx.h"
#include "gpu_vulkan/vulkan_fisheye.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuSurface;
namespace value = render::gpu_value;
namespace vk = render::vulkan;
using vk::CameraRgb;

constexpr int kWindowWidth = 256;
constexpr int kWindowHeight = 128;
constexpr uint32_t kWidth = 256;
constexpr uint32_t kHeight = 128;
constexpr std::size_t kHalfPixelBytes = 8;
constexpr std::size_t kBytePixelBytes = 4;
constexpr std::size_t kChannels = 3;
constexpr float kByteSteps = 255.0F;
constexpr uint16_t kHalfAlpha = 0x3800;
constexpr uint32_t kHighByte = 8;
constexpr uint32_t kByteMask = 0xFF;
constexpr uint16_t kHalfSign = 0x8000;
constexpr float kHalf = 0.5F;
constexpr float kTwice = 2.0F;
constexpr int kBitsPerByte = 8;

// The view's projection (Direct3D's left-handed one, row vectors): near 1, far 10000, a vertical scale of 1.5.
constexpr float kNear = 1.0F;
constexpr float kFar = 10000.0F;
constexpr float kVerticalScale = 1.5F;

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

// --- 16-bit floats --------------------------------------------------------------------------------------------------

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

uint16_t to_half(float number)
{
    constexpr uint32_t kMantissaUnit = 0x400;
    constexpr int kExponentBias = 15;
    constexpr uint32_t kMantissaBits = 10;
    if (!(number > 0.0F)) {
        return 0;
    }
    int exponent = 0;
    const float fraction = std::frexp(number, &exponent);
    const int biased = exponent - 1 + kExponentBias;
    if (biased <= 0) {
        return 0;
    }
    auto mantissa = static_cast<uint32_t>(std::lround(((fraction * kTwice) - 1.0F) * static_cast<float>(kMantissaUnit)));
    auto power = static_cast<uint32_t>(biased);
    if (mantissa == kMantissaUnit) {
        mantissa = 0;
        ++power;
    }
    return static_cast<uint16_t>((power << kMantissaBits) | mantissa);
}

std::size_t pixel_of(uint32_t column, uint32_t row) { return (static_cast<std::size_t>(row) * kWidth) + column; }

// --- pictures ---------------------------------------------------------------------------------------------------------

using Picture = std::vector<CameraRgb>; // kWidth x kHeight, row by row (as a float target holds them)

// Noisy colours over a gradient (every value a 16-bit float exactly).
Picture noise_picture(uint32_t seed)
{
    Picture picture(static_cast<std::size_t>(kWidth) * kHeight);
    uint32_t random = seed;
    constexpr uint32_t kMultiplier = 1664525U;
    constexpr uint32_t kIncrement = 1013904223U;
    constexpr uint32_t kNoiseBits = 16;
    constexpr float kNoiseSteps = 65535.0F;
    constexpr float kNoise = 0.25F;
    for (uint32_t row = 0; row < kHeight; ++row) {
        for (uint32_t column = 0; column < kWidth; ++column) {
            const float base = 0.15F + (0.7F * static_cast<float>(column) / static_cast<float>(kWidth - 1));
            CameraRgb& c = core::at(picture, pixel_of(column, row));
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                random = (random * kMultiplier) + kIncrement;
                const float n = ((static_cast<float>(random >> kNoiseBits) / kNoiseSteps) - kHalf) * kTwice * kNoise;
                const float tint = channel == 0 ? 0.05F : (channel == 2 ? -0.05F : 0.0F);
                core::at(c, channel) = from_half(to_half(std::clamp(base + n + tint, 0.0F, 1.0F)));
            }
        }
    }
    return picture;
}

// Stripes one pixel wide: across (vertical lines, alternating columns) or down (horizontal lines).
Picture stripes(bool vertical)
{
    Picture picture(static_cast<std::size_t>(kWidth) * kHeight);
    for (uint32_t row = 0; row < kHeight; ++row) {
        for (uint32_t column = 0; column < kWidth; ++column) {
            const bool light = ((vertical ? column : row) / 2) % 2 == 0;
            const float v = light ? 0.875F : 0.125F;
            core::at(picture, pixel_of(column, row)) = {v, v, v};
        }
    }
    return picture;
}

// A dark picture with one bright vertical line at `column`.
Picture one_line(uint32_t column)
{
    Picture picture(static_cast<std::size_t>(kWidth) * kHeight, CameraRgb{0.0F, 0.0F, 0.0F});
    for (uint32_t row = 0; row < kHeight; ++row) {
        core::at(picture, pixel_of(column, row)) = {1.0F, 1.0F, 1.0F};
    }
    return picture;
}

// --- the device's targets -------------------------------------------------------------------------------------------

struct Targets {
    GpuSurface* colour = nullptr;
    GpuSurface* depth = nullptr;
    bool floats = true;
    uint32_t width = kWidth;
    uint32_t height = kHeight;
};

Targets make_targets(render::Gpu& gpu, bool floats, uint32_t width = kWidth, uint32_t height = kHeight)
{
    Targets targets;
    targets.floats = floats;
    targets.width = width;
    targets.height = height;
    const render::GpuSurfaceDesc colour{.width = width,
                                        .height = height,
                                        .format = floats ? value::kFormatA16B16G16R16F : value::kFormatA8R8G8B8,
                                        .samples = 0,
                                        .quality = 0,
                                        .lockable_or_discard = true};
    render::GpuSurfaceDesc depth = colour;
    depth.format = value::kFormatD24S8;
    depth.lockable_or_discard = false;
    if (gpu.create_render_target(colour, &targets.colour) != value::kResultOk) {
        targets.colour = nullptr;
    }
    if (gpu.create_depth_surface(depth, &targets.depth) != value::kResultOk) {
        targets.depth = nullptr;
    }
    return targets;
}

void release(render::Gpu& gpu, const Targets& targets)
{
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    for (GpuSurface* surface : {targets.colour, targets.depth}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
}

// The colour target filled with the picture (8-bit: rounded to bytes - `as_held` what it holds then).
bool fill(render::Gpu& gpu, const Targets& targets, const Picture& picture, Picture* as_held)
{
    render::GpuLockedRect locked{};
    if (gpu.lock_surface(targets.colour, &locked, nullptr, 0) != value::kResultOk) {
        return false;
    }
    const std::size_t pixels = static_cast<std::size_t>(targets.width) * targets.height;
    *as_held = picture;
    if (targets.floats) {
        const std::span<uint8_t> bytes(static_cast<uint8_t*>(locked.bits), pixels * kHalfPixelBytes);
        for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
            const CameraRgb& c = core::at(picture, pixel % picture.size());
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const uint16_t bits = channel < kChannels ? to_half(core::at(c, channel)) : kHalfAlpha;
                core::at(bytes, (pixel * kHalfPixelBytes) + (channel * 2)) = static_cast<uint8_t>(bits & kByteMask);
                core::at(bytes, (pixel * kHalfPixelBytes) + (channel * 2) + 1) = static_cast<uint8_t>(bits >> kHighByte);
            }
        }
    } else {
        const std::span<uint8_t> bytes(static_cast<uint8_t*>(locked.bits), pixels * kBytePixelBytes);
        for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
            CameraRgb& held = core::at(*as_held, pixel);
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                const auto byte = static_cast<uint8_t>(std::lround(std::clamp(core::at(held, channel), 0.0F, 1.0F) * kByteSteps));
                core::at(bytes, (pixel * kBytePixelBytes) + (2 - channel)) = byte;
                core::at(held, channel) = static_cast<float>(byte) / kByteSteps;
            }
            core::at(bytes, (pixel * kBytePixelBytes) + 3) = kByteMask;
        }
    }
    return gpu.unlock_surface(targets.colour) == value::kResultOk;
}

bool read_colour(const Targets& targets, Picture* colour)
{
    std::vector<uint8_t> bytes;
    if (!vk::read_surface(targets.colour, &bytes)) {
        return false;
    }
    const std::size_t pixels = static_cast<std::size_t>(targets.width) * targets.height;
    colour->resize(pixels);
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            if (targets.floats) {
                const std::size_t first = (pixel * kHalfPixelBytes) + (channel * 2);
                core::at(core::at(*colour, pixel), channel) =
                    from_half(static_cast<uint16_t>(core::at(bytes, first) | (core::at(bytes, first + 1) << kHighByte)));
            } else {
                core::at(core::at(*colour, pixel), channel) =
                    static_cast<float>(core::at(bytes, (pixel * kBytePixelBytes) + (2 - channel))) / kByteSteps;
            }
        }
    }
    return true;
}

// The depth value of a distance z (the projection below).
float depth_of(float z) { return (kFar / (kFar - kNear)) - (kNear * kFar / ((kFar - kNear) * z)); }

// A view over the whole target, its camera at `across` on x (looking down +z).
render::rt::ViewDrawn view_of(const Targets& targets, float across)
{
    render::rt::ViewDrawn view;
    view.rect = {0, 0, targets.width, targets.height};
    view.output_rect = view.rect;
    view.depth_range = {0.0F, 1.0F};
    auto& projection = view.camera.projection;
    const float aspect = static_cast<float>(targets.width) / static_cast<float>(targets.height);
    core::at(core::at(projection, 0), 0) = kVerticalScale / aspect;
    core::at(core::at(projection, 1), 1) = kVerticalScale;
    core::at(core::at(projection, 2), 2) = kFar / (kFar - kNear);
    core::at(core::at(projection, 2), 3) = 1.0F;
    core::at(core::at(projection, 3), 2) = -kNear * kFar / (kFar - kNear);
    for (std::size_t i = 0; i < 4; ++i) {
        core::at(core::at(view.camera.view, i), i) = 1.0F;
    }
    core::at(core::at(view.camera.view, 3), 0) = -across;
    view.camera.position = {across, 0.0F, 0.0F};
    return view;
}

// A depth rectangle: z over the target's [left, right) x [top, bottom).
struct DepthBox {
    bool on = false;
    std::array<int32_t, 4> box{}; // left, top, right, bottom
    float z = 0.0F;
};

// One frame as draw_viewport makes it: the view begun, the depth cleared to `wall` (and the box), the world drawn.
void run_frame(render::Gpu& gpu, const Targets& targets, const render::rt::ViewDrawn& view, float wall, const DepthBox& box,
               bool present)
{
    gpu.begin_scene();
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = targets.width, .height = targets.height, .min_z = 0.0F, .max_z = 1.0F});
    render::rt::view_begins(view);
    gpu.clear(0, nullptr, value::kClearDepth, 0, depth_of(wall), 0);
    if (box.on) {
        const render::GpuRect rect{.left = core::at(box.box, 0), .top = core::at(box.box, 1), .right = core::at(box.box, 2),
                                   .bottom = core::at(box.box, 3)};
        gpu.clear(1, &rect, value::kClearDepth, 0, depth_of(box.z), 0);
    }
    render::rt::world_view_drawn(view);
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    gpu.end_scene();
    if (present) {
        gpu.present();
    }
}

// --- statistics -----------------------------------------------------------------------------------------------------

struct Region {
    uint32_t left = 0;
    uint32_t top = 0;
    uint32_t right = 0;
    uint32_t bottom = 0;
};

struct Spread {
    double mean = 0.0;
    double deviation = 0.0;
};

Spread spread_of(const Picture& picture, const Region& region, uint32_t width = kWidth)
{
    double sum = 0.0;
    double squares = 0.0;
    double count = 0.0;
    for (uint32_t row = region.top; row < region.bottom; ++row) {
        for (uint32_t column = region.left; column < region.right; ++column) {
            const double v = core::at(core::at(picture, (static_cast<std::size_t>(row) * width) + column), 0);
            sum += v;
            squares += v * v;
            count += 1.0;
        }
    }
    const double mean = sum / std::max(count, 1.0);
    return {.mean = mean, .deviation = std::sqrt(std::max((squares / std::max(count, 1.0)) - (mean * mean), 0.0))};
}

// Whether every pixel of the region is bit for bit (as read back) the same in both.
bool same_in(const Picture& a, const Picture& b, const Region& region)
{
    for (uint32_t row = region.top; row < region.bottom; ++row) {
        for (uint32_t column = region.left; column < region.right; ++column) {
            if (core::at(a, pixel_of(column, row)) != core::at(b, pixel_of(column, row))) {
                return false;
            }
        }
    }
    return true;
}

// The largest channel difference and the pixels past `tolerance`.
struct Compared {
    float worst = 0.0F;
    int off = 0;
};
Compared compare(const Picture& got, const Picture& expected, float tolerance)
{
    Compared result;
    for (std::size_t pixel = 0; pixel < got.size(); ++pixel) {
        float pixel_worst = 0.0F;
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            const float g = core::at(core::at(got, pixel), channel);
            const float e = core::at(core::at(expected, pixel), channel);
            pixel_worst = std::max(pixel_worst, std::isfinite(g) ? std::abs(g - e) : 1.0F);
        }
        result.worst = std::max(result.worst, pixel_worst);
        if (pixel_worst > tolerance) {
            if (result.off < 3) {
                text::print("    pixel (%zu, %zu): %.4f %.4f %.4f, reference %.4f %.4f %.4f\n", pixel % kWidth, pixel / kWidth,
                            static_cast<double>(core::at(core::at(got, pixel), 0)),
                            static_cast<double>(core::at(core::at(got, pixel), 1)),
                            static_cast<double>(core::at(core::at(got, pixel), 2)),
                            static_cast<double>(core::at(core::at(expected, pixel), 0)),
                            static_cast<double>(core::at(core::at(expected, pixel), 1)),
                            static_cast<double>(core::at(core::at(expected, pixel), 2)));
            }
            ++result.off;
        }
    }
    return result;
}

void settings(const vk::CameraFxSettings& fx) { vk::set_camera_fx_settings(fx); }

constexpr float kWall = 5000.0F; // the far wall's distance
constexpr float kBoxZ = 100.0F;  // the box's

// --- the cases ------------------------------------------------------------------------------------------------------

void check_off(render::Gpu& gpu, Tally* tally)
{
    text::print("everything off\n");
    settings({});
    tally->check(!render::rt::view_listened(), "no listener while every camera effect is off");
    const Targets targets = make_targets(gpu, true);
    Picture held;
    Picture after;
    const vk::CameraPassCounts before = vk::camera_pass_counts();
    render::rt::ViewDrawn view = view_of(targets, 0.0F);
    view.focus_known = true;
    view.focus_distance = kBoxZ;
    const bool ran = fill(gpu, targets, noise_picture(7U), &held) &&
                     (run_frame(gpu, targets, view, kWall, {}, true), fill(gpu, targets, noise_picture(7U), &held)) &&
                     (run_frame(gpu, targets, view_of(targets, 10.0F), kWall, {}, false), read_colour(targets, &after));
    const vk::CameraPassCounts now = vk::camera_pass_counts();
    tally->check(ran && after == held, "the picture bit for bit as it was");
    tally->check(now.blurs == before.blurs && now.focuses == before.focuses && now.grades == before.grades &&
                     now.lenses == before.lenses,
                 "no pass recorded");
    tally->check(!render::rt::view_lens().on, "the game camera not widened (no ViewLens)");
    {
        const std::scoped_lock lock(vk::backend().mutex);
        tally->check(!vk::backend().motion.active && vk::backend().motion.draws == 0, "no motion vectors begun");
    }
    release(gpu, targets);
}

void check_grade(render::Gpu& gpu, Tally* tally)
{
    text::print("the grade\n");
    // (the tables at their grid points: the formulas)
    for (unsigned look = 1; look < vk::kGradeLookCount - 1; ++look) {
        const auto which = static_cast<vk::GradeLook>(look);
        const std::vector<CameraRgb> table = vk::grade_table(which);
        float worst = 0.0F;
        for (uint32_t i = 0; i < vk::kGradeTableSize; i += 4) {
            const float v = static_cast<float>(i) / static_cast<float>(vk::kGradeTableSize - 1);
            const CameraRgb at = vk::grade_table_colour(table, vk::kGradeTableSize, {v, 1.0F - v, v * v});
            const CameraRgb want = vk::grade_look_colour(which, {v, 1.0F - v, v * v});
            for (std::size_t c = 0; c < kChannels; ++c) {
                worst = std::max(worst, std::abs(core::at(at, c) - core::at(want, c)));
            }
        }
        tally->check(worst < 0.03F, text::format("%s: the table follows the look's formula (worst %.4f)",
                                                 vk::grade_look_name(which), static_cast<double>(worst)));
    }
    // (what the looks do, on a mid grey-ish colour)
    {
        const CameraRgb mid{0.5F, 0.45F, 0.4F};
        const auto ratio = [](const CameraRgb& c) { return core::at(c, 0) / std::max(core::at(c, 2), 1e-3F); };
        const auto chroma = [](const CameraRgb& c) {
            return std::max({core::at(c, 0), core::at(c, 1), core::at(c, 2)}) - std::min({core::at(c, 0), core::at(c, 1), core::at(c, 2)});
        };
        tally->check(ratio(vk::grade_look_colour(vk::GradeLook::kWarm, mid)) > ratio(mid), "warm is warmer");
        tally->check(ratio(vk::grade_look_colour(vk::GradeLook::kCool, mid)) < ratio(mid), "cool is cooler");
        tally->check(chroma(vk::grade_look_colour(vk::GradeLook::kBleach, mid)) < chroma(mid), "bleach bypass is less saturated");
        tally->check(chroma(vk::grade_look_colour(vk::GradeLook::kSkateVideo, mid)) > chroma(mid), "skate video is more saturated");
        tally->check(vk::grade_look_colour(vk::GradeLook::kNeutral, mid) == mid, "neutral changes no colour");
    }
    struct Case {
        const char* name;
        vk::GradeLook look;
        float strength = 1.0F;
        float vignette = 0.0F;
        float grain = 0.0F;
        bool floats = true;
    };
    const std::array<Case, 10> cases{{
        {.name = "neutral, vignette and grain", .look = vk::GradeLook::kNeutral, .vignette = 1.0F, .grain = 1.0F},
        {.name = "warm", .look = vk::GradeLook::kWarm},
        {.name = "cool", .look = vk::GradeLook::kCool},
        {.name = "filmic", .look = vk::GradeLook::kFilmic},
        {.name = "bleach", .look = vk::GradeLook::kBleach},
        {.name = "skate video", .look = vk::GradeLook::kSkateVideo},
        {.name = "skate video, strength 0.6, vignette 0.5, grain 0.35", .look = vk::GradeLook::kSkateVideo, .strength = 0.6F,
         .vignette = 0.5F, .grain = 0.35F},
        {.name = "filmic, an 8-bit target", .look = vk::GradeLook::kFilmic, .floats = false},
        {.name = "warm, an 8-bit target with grain", .look = vk::GradeLook::kWarm, .grain = 0.65F, .floats = false},
        {.name = "cool, strength 0.5", .look = vk::GradeLook::kCool, .strength = 0.5F},
    }};
    const Picture picture = noise_picture(11U);
    for (const Case& test : cases) {
        settings({.grade = test.look, .grade_strength = test.strength, .grade_vignette = test.vignette, .grade_grain = test.grain});
        const Targets targets = make_targets(gpu, test.floats);
        Picture held;
        Picture got;
        const uint64_t before = vk::camera_pass_counts().grades;
        const bool ran = fill(gpu, targets, picture, &held) &&
                         (run_frame(gpu, targets, view_of(targets, 0.0F), kWall, {}, false), read_colour(targets, &got));
        const bool passed = vk::camera_pass_counts().grades == before + 1;
        const vk::GradeParameters parameters = vk::grade_last_parameters();
        const Picture expected = vk::grade_reference(parameters, held, {.width = kWidth, .height = kHeight});
        const float tolerance = test.floats ? 1.5F / kByteSteps : 1.5F / kByteSteps;
        const Compared result = compare(got, expected, tolerance);
        tally->check(ran && passed && result.off <= 4,
                     text::format("%s: within %.4f of the twin (worst %.4f, %d past)", test.name,
                                  static_cast<double>(tolerance), static_cast<double>(result.worst), result.off));
        release(gpu, targets);
    }
    // (neutral without vignette or grain: nothing to do - no pass, the picture as it was)
    {
        settings({.grade = vk::GradeLook::kNeutral});
        const Targets targets = make_targets(gpu, true);
        Picture held;
        Picture got;
        const uint64_t before = vk::camera_pass_counts().grades;
        const bool ran = fill(gpu, targets, picture, &held) &&
                         (run_frame(gpu, targets, view_of(targets, 0.0F), kWall, {}, false), read_colour(targets, &got));
        tally->check(ran && vk::camera_pass_counts().grades == before && got == held, "neutral alone: no pass, as it was");
        release(gpu, targets);
    }
    // (rt_grade=level: the listed level's look; an unlisted one neutral)
    {
        settings({.grade = vk::GradeLook::kLevel, .grade_levels = "TST:warm, other:cool"});
        const Targets targets = make_targets(gpu, true);
        Picture held;
        Picture got;
        render::rt::ViewDrawn view = view_of(targets, 0.0F);
        view.level = checksums::hash_from_text("TST");
        const bool ran = fill(gpu, targets, picture, &held) && (run_frame(gpu, targets, view, kWall, {}, false), read_colour(targets, &got));
        const vk::GradeParameters parameters = vk::grade_last_parameters();
        const Compared result = compare(got, vk::grade_reference(parameters, held, {.width = kWidth, .height = kHeight}), 1.5F / kByteSteps);
        tally->check(ran && parameters.look == vk::GradeLook::kWarm && result.off <= 4, "rt_grade=level: a listed level's look");
        const uint64_t before = vk::camera_pass_counts().grades;
        view.level = checksums::hash_from_text("XYZ");
        const bool again = fill(gpu, targets, picture, &held) && (run_frame(gpu, targets, view, kWall, {}, false), read_colour(targets, &got));
        tally->check(again && vk::camera_pass_counts().grades == before && got == held, "rt_grade=level: an unlisted level neutral");
        release(gpu, targets);
    }
    std::vector<vk::GradeLevel> levels;
    tally->check(!vk::parse_grade_levels("BO:warm,nonsense,BA:purple", &levels) && levels.size() == 1,
                 "rt_grade_levels: the readable pairs kept, the others refused");
    settings({});
}

// --- the fisheye lens ------------------------------------------------------------------------------------------------

// A smooth picture (slow waves over a gradient - the filtered reads' rounding small; every value a 16-bit float).
Picture smooth_picture()
{
    Picture picture(static_cast<std::size_t>(kWidth) * kHeight);
    for (uint32_t row = 0; row < kHeight; ++row) {
        for (uint32_t column = 0; column < kWidth; ++column) {
            const float x = static_cast<float>(column) / static_cast<float>(kWidth);
            const float y = static_cast<float>(row) / static_cast<float>(kHeight);
            CameraRgb& c = core::at(picture, pixel_of(column, row));
            core::at(c, 0) = from_half(to_half(0.5F + (0.35F * std::sin(9.0F * x) * std::cos(5.0F * y))));
            core::at(c, 1) = from_half(to_half(0.2F + (0.6F * x)));
            core::at(c, 2) = from_half(to_half(0.5F + (0.3F * std::cos((7.0F * y) + (4.0F * x)))));
        }
    }
    return picture;
}

// A dark picture with a soft bright line (a 3-pixel bump) along a row (`across`) or a column.
Picture soft_line(bool across, uint32_t at)
{
    Picture picture(static_cast<std::size_t>(kWidth) * kHeight, CameraRgb{0.05F, 0.05F, 0.05F});
    for (uint32_t row = 0; row < kHeight; ++row) {
        for (uint32_t column = 0; column < kWidth; ++column) {
            const auto d = static_cast<float>(static_cast<int>(across ? row : column) - static_cast<int>(at));
            const float v = from_half(to_half(0.05F + (0.9F * std::max(0.0F, 1.0F - (std::fabs(d) / 2.0F)))));
            core::at(picture, pixel_of(column, row)) = {v, v, v};
        }
    }
    return picture;
}

// The view's lens as render_pc fits it: the check's camera (72 degrees across, the target's aspect).
render::rt::ViewDrawn::Lens lens_of(float strength, float circle)
{
    const float tan_x = std::tan(0.62831853F);
    const float tan_y = tan_x * static_cast<float>(kHeight) / static_cast<float>(kWidth);
    const render::fisheye::Fit fit = render::fisheye::fit(tan_x, tan_y, strength, circle);
    return {.on = true,
            .strength = strength,
            .circle = circle,
            .tangents = {tan_x, tan_y},
            .widen = fit.widen,
            .reach = fit.reach,
            .framed = fit.framed};
}

// One frame with the lens: the picture held before it and after it, and whether a pass ran.
bool lens_frame(render::Gpu& gpu, const Targets& targets, const Picture& picture, const render::rt::ViewDrawn::Lens& lens,
                Picture* held, Picture* got, bool* passed)
{
    render::rt::ViewDrawn view = view_of(targets, 0.0F);
    view.lens = lens;
    const uint64_t before = vk::camera_pass_counts().lenses;
    const bool ran = fill(gpu, targets, picture, held) &&
                     (run_frame(gpu, targets, view, kWall, {}, false), read_colour(targets, got));
    *passed = vk::camera_pass_counts().lenses == before + 1;
    return ran;
}

// The green channel's peak down a column (`down`) or along a row, to a fraction of a pixel (the centroid of the three
// around the brightest, above their least), in pixel-centre coordinates (+ 0.5).
float peak_of(const Picture& picture, bool down, uint32_t at)
{
    const uint32_t count = down ? kHeight : kWidth;
    const auto value = [&](uint32_t i) { return core::at(core::at(picture, down ? pixel_of(at, i) : pixel_of(i, at)), 1); };
    uint32_t best = 1;
    for (uint32_t i = 1; i + 1 < count; ++i) {
        if (value(i) > value(best)) {
            best = i;
        }
    }
    const float a = value(best - 1);
    const float b = value(best);
    const float c = value(best + 1);
    const float least = std::min({a, b, c});
    const float sum = (a - least) + (b - least) + (c - least);
    const float shift = sum > 0.0F ? ((c - least) - (a - least)) / sum : 0.0F;
    return static_cast<float>(best) + kHalf + shift;
}

void check_lens(render::Gpu& gpu, Tally* tally)
{
    text::print("the fisheye lens\n");
    struct Case {
        const char* name;
        float strength;
        float vignette;
        float circle;
        bool floats = true;
    };
    const std::array<Case, 6> cases{{
        {.name = "equisolid (0.75), vignette 0.5, the whole frame", .strength = 0.75F, .vignette = 0.5F, .circle = 0.0F},
        {.name = "orthographic (1), vignette 1, the porthole", .strength = 1.0F, .vignette = 1.0F, .circle = 1.0F},
        {.name = "0.4, no vignette, rounded", .strength = 0.4F, .vignette = 0.0F, .circle = 0.5F},
        {.name = "equidistant (0.5), vignette 0.25", .strength = 0.5F, .vignette = 0.25F, .circle = 0.0F},
        {.name = "strength 0, vignette 1 (the corners only)", .strength = 0.0F, .vignette = 1.0F, .circle = 0.0F},
        {.name = "0.75, an 8-bit target", .strength = 0.75F, .vignette = 0.5F, .circle = 0.0F, .floats = false},
    }};
    const Picture picture = smooth_picture();
    for (const Case& test : cases) {
        settings({.fisheye = true, .fisheye_strength = test.strength, .fisheye_vignette = test.vignette,
                  .fisheye_circle = test.circle});
        const Targets targets = make_targets(gpu, test.floats);
        Picture held;
        Picture got;
        bool passed = false;
        const bool ran = lens_frame(gpu, targets, picture, lens_of(test.strength, test.circle), &held, &got, &passed);
        const vk::FilmConstants constants = vk::lens_last_constants();
        const std::vector<vk::FilmRgb> colour(held.begin(), held.end());
        const std::vector<vk::FilmRgb> reference = vk::fisheye_reference(constants, colour);
        const Picture expected(reference.begin(), reference.end());
        constexpr float kTolerance = 1.5F / kByteSteps;
        const Compared result = compare(got, expected, kTolerance);
        tally->check(ran && passed && result.off <= 8,
                     text::format("%s: within %.4f of the twin (worst %.4f, %d past)", test.name,
                                  static_cast<double>(kTolerance), static_cast<double>(result.worst), result.off));
        release(gpu, targets);
    }
    // the lens' shape: a straight line bowed, the centre at the original camera's scale (no vignette, the whole frame)
    {
        constexpr float kStrength = 0.75F;
        settings({.fisheye = true, .fisheye_strength = kStrength, .fisheye_vignette = 0.0F});
        const render::rt::ViewDrawn::Lens lens = lens_of(kStrength, 0.0F);
        const Targets targets = make_targets(gpu, true);
        Picture held;
        Picture got;
        bool passed = false;
        constexpr uint32_t kLineRow = 44; // 19.5 pixels above the middle (its pixel's centre)
        const bool ran = lens_frame(gpu, targets, soft_line(true, kLineRow), lens, &held, &got, &passed);
        const float middle_row = static_cast<float>(kHeight) * kHalf;
        const float at_centre = middle_row - peak_of(got, true, kWidth / 2);
        const float at_side = middle_row - peak_of(got, true, kWidth / 8);
        const float at_edge = middle_row - peak_of(got, true, 6);
        tally->check(ran && passed && at_edge < at_side && at_side < at_centre,
                     text::format("a straight line bowed: %.2f pixels above the middle at the centre, %.2f an eighth in, "
                                  "%.2f at the edge (widened x%.3f)",
                                  static_cast<double>(at_centre), static_cast<double>(at_side), static_cast<double>(at_edge),
                                  static_cast<double>(lens.widen)));
        constexpr uint32_t kOff = 6; // a vertical line 6.5 pixels right of the middle (its pixel's centre)
        const bool again = lens_frame(gpu, targets, soft_line(false, (kWidth / 2) + kOff), lens, &held, &got, &passed);
        const float shown = peak_of(got, false, kHeight / 2) - (static_cast<float>(kWidth) * kHalf);
        const float expected = (static_cast<float>(kOff) + kHalf) * lens.widen;
        tally->check(again && passed && std::fabs(shown - expected) < 0.6F,
                     text::format("the centre at the original camera's scale: a line %.1f pixels off the widened "
                                  "picture's middle shown %.2f off it (x widen: %.2f)",
                                  static_cast<double>(kOff) + 0.5, static_cast<double>(shown), static_cast<double>(expected)));
        release(gpu, targets);
    }
    // the adapter's vignette: the corners darker, the centre as it was
    {
        const Picture flat(static_cast<std::size_t>(kWidth) * kHeight, CameraRgb{0.75F, 0.75F, 0.75F});
        const Targets targets = make_targets(gpu, true);
        Picture held;
        Picture plain;
        Picture dark;
        bool first = false;
        bool second = false;
        settings({.fisheye = true, .fisheye_strength = 0.75F, .fisheye_vignette = 0.0F});
        const bool ran = lens_frame(gpu, targets, flat, lens_of(0.75F, 0.0F), &held, &plain, &first);
        settings({.fisheye = true, .fisheye_strength = 0.75F, .fisheye_vignette = 1.0F});
        const bool again = lens_frame(gpu, targets, flat, lens_of(0.75F, 0.0F), &held, &dark, &second);
        const float corner_plain = core::at(core::at(plain, pixel_of(1, 1)), 1);
        const float corner_dark = core::at(core::at(dark, pixel_of(1, 1)), 1);
        const float centre_dark = core::at(core::at(dark, pixel_of(kWidth / 2, kHeight / 2)), 1);
        tally->check(ran && again && first && second && corner_dark < corner_plain * 0.25F &&
                         std::fabs(centre_dark - 0.75F) < 0.002F,
                     text::format("the vignette: a corner %.3f without, %.3f with it; the centre %.3f",
                                  static_cast<double>(corner_plain), static_cast<double>(corner_dark),
                                  static_cast<double>(centre_dark)));
        release(gpu, targets);
    }
    // a view the camera wasn't widened for, and the lens off: nothing
    {
        settings({.fisheye = true});
        tally->check(render::rt::view_lens().on && render::rt::view_lens().strength == vk::kFisheyeStrength,
                     "rt_fisheye on: the game camera asked to widen (ViewLens)");
        const Targets targets = make_targets(gpu, true);
        Picture held;
        Picture got;
        bool passed = false;
        render::rt::ViewDrawn::Lens off = lens_of(0.75F, 0.0F);
        off.on = false;
        const bool ran = lens_frame(gpu, targets, noise_picture(5U), off, &held, &got, &passed);
        tally->check(ran && !passed && got == held, "a view not widened (a menu, the editor, split screen): no pass, as it was");
        settings({});
        tally->check(!render::rt::view_lens().on, "rt_fisheye off: ViewLens off");
        const bool after = lens_frame(gpu, targets, noise_picture(5U), lens_of(0.75F, 0.0F), &held, &got, &passed);
        tally->check(after && !passed && got == held, "rt_fisheye off: no pass, the picture bit for bit as it was");
        release(gpu, targets);
    }
    settings({});
}

void check_focus(render::Gpu& gpu, Tally* tally)
{
    text::print("the depth of field\n");
    const Targets targets = make_targets(gpu, true);
    const Picture picture = noise_picture(23U);
    const DepthBox box{.on = true, .box = {96, 32, 160, 96}, .z = kBoxZ};
    const auto run = [&](float focus, bool known, bool menu, Picture* held, Picture* got) {
        render::rt::ViewDrawn view = view_of(targets, 0.0F);
        view.focus_known = known;
        view.focus_distance = focus;
        view.menu = menu;
        return fill(gpu, targets, picture, held) && (run_frame(gpu, targets, view, kWall, box, false), read_colour(targets, got));
    };
    settings({.dof = true, .dof_strength = 1.0F});
    const float aperture = vk::kDofAperture * static_cast<float>(kHeight); // A at strength 1
    const auto margin = static_cast<uint32_t>(std::ceil(aperture)) + 4;
    Picture held;
    Picture got;
    // focused on the box
    uint64_t before = vk::camera_pass_counts().focuses;
    bool ran = run(kBoxZ, true, false, &held, &got);
    tally->check(ran && vk::camera_pass_counts().focuses == before + 1, "focused on the box: a pass");
    tally->check(same_in(got, held, {.left = 96, .top = 32, .right = 160, .bottom = 96}),
                 "the box at the focus bit for bit as it was");
    const Region wall{.left = 0, .top = 0, .right = 96 - margin, .bottom = kHeight};
    const Spread wall_before = spread_of(held, wall);
    const Spread wall_after = spread_of(got, wall);
    tally->check(wall_after.deviation < wall_before.deviation * 0.6 && std::abs(wall_after.mean - wall_before.mean) < 0.02,
                 text::format("the far wall blurred (spread %.4f -> %.4f, mean %.4f -> %.4f)", wall_before.deviation,
                              wall_after.deviation, wall_before.mean, wall_after.mean));
    // focused on the wall: the box (before the focus) blurred, the wall away from it untouched
    ran = run(kWall, true, false, &held, &got);
    const Region inside{.left = 104, .top = 40, .right = 152, .bottom = 88};
    const Spread box_before = spread_of(held, inside);
    const Spread box_after = spread_of(got, inside);
    tally->check(ran && box_after.deviation < box_before.deviation * 0.6,
                 text::format("focused on the wall: the box before it blurred (spread %.4f -> %.4f)", box_before.deviation,
                              box_after.deviation));
    const uint32_t reach = margin + (2 * static_cast<uint32_t>(vk::kDofTile) * 2);
    tally->check(same_in(got, held, {.left = 0, .top = 0, .right = 96 - std::min(reach, 95U), .bottom = kHeight}),
                 "focused on the wall: the wall away from the box bit for bit as it was");
    // no focus known; under a menu
    before = vk::camera_pass_counts().focuses;
    ran = run(kBoxZ, false, false, &held, &got) && got == held;
    ran = ran && run(kBoxZ, true, true, &held, &got) && got == held;
    tally->check(ran && vk::camera_pass_counts().focuses == before, "no focus known, or under a menu: no pass");
    settings({});
    release(gpu, targets);
}

// Two frames over a flat wall at kBoxZ: the camera at 0, then at `across`; the second frame's picture (and its blur
// passes added to `blurs`).
bool blur_frames(render::Gpu& gpu, const Targets& targets, const Picture& picture, float across, bool cut, bool game_blur,
                 Picture* held, Picture* got, uint64_t* blurs = nullptr)
{
    Picture ignored;
    if (!fill(gpu, targets, picture, &ignored)) {
        return false;
    }
    run_frame(gpu, targets, view_of(targets, 0.0F), kBoxZ, {}, true);
    if (cut) {
        frame_blend::note_cut();
    }
    render::rt::ViewDrawn view = view_of(targets, across);
    view.game_blur = game_blur;
    if (!fill(gpu, targets, picture, held)) {
        return false;
    }
    const uint64_t before = vk::camera_pass_counts().blurs;
    run_frame(gpu, targets, view, kBoxZ, {}, true);
    if (blurs != nullptr) {
        *blurs += vk::camera_pass_counts().blurs - before;
    }
    return read_colour(targets, got);
}

void check_blur(render::Gpu& gpu, Tally* tally)
{
    text::print("the motion blur\n");
    const Targets targets = make_targets(gpu, true);
    constexpr float kShutter = 0.5F;
    constexpr float kAcross = 12.0F; // world units the camera moves between the frames
    // the move in pixels: across x P00 / z x the half width
    const float moved = kAcross * (kVerticalScale / (static_cast<float>(kWidth) / static_cast<float>(kHeight))) / kBoxZ *
                        (static_cast<float>(kWidth) * kHalf);
    settings({.motion_blur = true, .motion_blur_strength = kShutter});
    const Region middle{.left = 32, .top = 16, .right = kWidth - 32, .bottom = kHeight - 16};
    Picture held;
    Picture got;
    // vertical stripes smeared
    uint64_t before = vk::camera_pass_counts().blurs;
    bool ran = blur_frames(gpu, targets, stripes(true), kAcross, false, false, &held, &got);
    const Spread across_before = spread_of(held, middle);
    const Spread across_after = spread_of(got, middle);
    tally->check(ran && vk::camera_pass_counts().blurs >= before + 1, "a blur pass");
    tally->check(across_after.deviation < across_before.deviation * 0.5 && std::abs(across_after.mean - across_before.mean) < 0.03,
                 text::format("vertical stripes smeared across (spread %.4f -> %.4f)", across_before.deviation,
                              across_after.deviation));
    // horizontal stripes kept
    ran = blur_frames(gpu, targets, stripes(false), kAcross, false, false, &held, &got);
    const Compared kept = compare(got, held, 0.01F);
    tally->check(ran && kept.off == 0, text::format("horizontal stripes left alone (worst %.4f)", static_cast<double>(kept.worst)));
    // a line spread over about the shutter's share of the move, its light kept
    ran = blur_frames(gpu, targets, one_line(kWidth / 2), kAcross, false, false, &held, &got);
    const uint32_t row = kHeight / 2;
    int lit = 0;
    double light = 0.0;
    for (uint32_t column = 0; column < kWidth; ++column) {
        const float v = core::at(core::at(got, pixel_of(column, row)), 0);
        lit += v > 0.02F ? 1 : 0;
        light += v;
    }
    const float streak = moved * kShutter;
    tally->check(ran && static_cast<float>(lit) >= streak - 2.0F && static_cast<float>(lit) <= streak + 3.0F &&
                     std::abs(light - 1.0) < 0.2,
                 text::format("a line spread over %d pixels (the shutter's share of a %.1f-pixel move: %.1f), its light %.3f",
                              lit, static_cast<double>(moved), static_cast<double>(streak), light));
    // a cut, a jump, the game's own blur: nothing blurred
    const Picture lines = stripes(true);
    uint64_t skipped_blurs = 0;
    const uint64_t skips = vk::camera_pass_counts().cut_skips;
    ran = blur_frames(gpu, targets, lines, kAcross, true, false, &held, &got, &skipped_blurs);
    tally->check(ran && got == held && vk::camera_pass_counts().cut_skips > skips, "after a cut: nothing blurred");
    ran = blur_frames(gpu, targets, lines, vk::kCutDistance * 2.0F, false, false, &held, &got, &skipped_blurs);
    tally->check(ran && got == held, "after a camera jump: nothing blurred");
    ran = blur_frames(gpu, targets, lines, kAcross, false, true, &held, &got, &skipped_blurs);
    tally->check(ran && got == held, "the game's own screen blur on: nothing blurred");
    tally->check(skipped_blurs == 0, "no blur pass in those frames");
    // still: the picture as it was
    ran = blur_frames(gpu, targets, lines, 0.0F, false, false, &held, &got);
    tally->check(ran && got == held, "a still camera over a still wall: bit for bit as it was");
    settings({});
    release(gpu, targets);
}

void measure_cost(render::Gpu& gpu, Tally* tally)
{
    text::print("the cost\n");
    constexpr uint32_t kBigWidth = 2560;
    constexpr uint32_t kBigHeight = 1440;
    const Targets targets = make_targets(gpu, true, kBigWidth, kBigHeight);
    if (targets.colour == nullptr || targets.depth == nullptr) {
        tally->check(false, "the 2560x1440 targets");
        return;
    }
    settings({.motion_blur = true, .motion_blur_strength = 1.0F, .dof = true, .dof_strength = 1.0F,
              .grade = vk::GradeLook::kSkateVideo, .grade_vignette = 0.5F, .grade_grain = 0.5F});
    constexpr int kFrames = 12;
    for (int frame = 0; frame < kFrames; ++frame) {
        render::rt::ViewDrawn view = view_of(targets, static_cast<float>(frame) * 3.0F);
        view.focus_known = true;
        view.focus_distance = kBoxZ;
        run_frame(gpu, targets, view, kWall, {.on = true, .box = {900, 400, 1700, 1100}, .z = kBoxZ}, true);
    }
    {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        vk::finish_work(state);
    }
    const vk::CameraTimings timings = vk::camera_timings();
    text::print("  2560x1440: motion blur and depth of field %.3f ms, the grade %.3f ms\n", timings.camera, timings.grade);
    tally->check(timings.camera >= 0.0 && timings.grade >= 0.0, "the passes timed on the device");
    settings({});
    release(gpu, targets);
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
        position = static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(core::at(listed, chosen - 1)));
    }
    SDL_free(ids);
    return position;
}

bool open_device(const Options& options, SDL_Window* window)
{
    vk::set_context_options({.validation = options.validation, .device_index = options.device, .on_message = print_line},
                            window);
    render::GpuContext& context = vk::gpu_context();
    if (!context.create_instance()) {
        text::print("the device could not be opened\n");
        return false;
    }
    const vk::Backend& state = vk::backend();
    text::print("validation layer: %s\n", state.context.validation_on() ? "on" : "off (not installed, or not asked for)");
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
        return false;
    }
    context.mark_ready();
    text::print("device: %s\n", context.adapter().description.data());
    return true;
}

int run(const Options& options, SDL_Window* window)
{
    if (!open_device(options, window)) {
        return vk::loader_version() == 0 ? 2 : 1;
    }
    Tally tally;
    render::Gpu& gpu = vk::gpu();
    check_off(gpu, &tally);
    check_grade(gpu, &tally);
    check_focus(gpu, &tally);
    check_blur(gpu, &tally);
    check_lens(gpu, &tally);
    measure_cost(gpu, &tally);
    settings({});
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
    text::print("thug2_vulkan_camera_fx_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan camera effects check", kWindowWidth, kWindowHeight,
                                          SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        text::print("SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    SDL_SetWindowPosition(window, position, position);
    SDL_ShowWindow(window);
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
