// NOT ORIGINAL - thug2_vulkan_style_looks_check.exe: the vhs, retro and watercolour art styles
// (gpu_vulkan/vulkan_style_looks.h) tried on this machine against their twins on the processor.
//
// A 256 x 128 target filled through a lock with a synthetic picture - noisy fields (a sky, a floor, a box, a ridge), a
// grey gradient band, not-a-number and infinities in a corner - and the pass run through the listener as the game's
// frames run it (rt::world_view_drawn). Cases:
//   - each style at its defaults and with other knobs: every channel within a tolerance of the twin's (a handful of
//     pixels past it: a hash or a font's edge on the other side of a rounding), every one finite and within 0 - 1,
//     alpha untouched (the bad values' corner left out of the comparison: the device's filter over an infinity);
//     the watercolour's paint (read back) within a step of its twin's, then the composite given it, then the whole twin;
//   - what each does: the vhs picture moves with the game clock (the same time twice: the same bytes; another time:
//     another picture) and its REC blinks; the retro picture is flat over each cell and has at most 2^bits shades a
//     channel; the watercolour calms the noise;
//   - a viewport: only its rectangle changed, everything outside it bit for bit as it was; strength 0: no pass;
//   - an 8-bit target; four samples (the colour resolved, filtered, written to every sample);
//   - the retro geometry: a 3D quad drawn through the fixed-function shaders between rt::view_begins and
//     world_view_drawn - its corners on the low-resolution grid with rt_style_retro_snap 1 and where the projection puts
//     them without; its vertex colours interpolated linearly on the screen with rt_style_retro_affine 1 and
//     perspective-correctly without, or with the style off;
//   - the cost of each at 1920x1080 and 2560x1440 (on a discrete card each at most kMostMilliseconds at 2560x1440).
//   thug2_vulkan_style_looks_check [--device N] [--display N] [--no-validation]
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
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_style_looks.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuRenderState;
using render::GpuStageState;
using render::GpuSurface;
namespace value = render::gpu_value;
namespace vk = render::vulkan;
using vk::LookRgb;

constexpr int kWindowWidth = 256;
constexpr int kWindowHeight = 128;
constexpr uint32_t kWidth = 256;
constexpr uint32_t kHeight = 128;
constexpr int kBitsPerByte = 8;
constexpr std::size_t kHalfPixelBytes = 8;
constexpr std::size_t kBytePixelBytes = 4;
constexpr std::size_t kChannels = 3;
constexpr float kByteSteps = 255.0F;
constexpr float kTolerance = 3.0F / 255.0F; // (the device's linear filter's 8-bit weights, sin and exp)
constexpr int kMostOff = 12;                // pixels allowed past it (a hash, a floor or a glyph's edge rounded across)
constexpr uint16_t kHalfAlpha = 0x3800;
constexpr uint32_t kHighByte = 8;
constexpr uint32_t kByteMask = 0xFF;
constexpr uint16_t kHalfSign = 0x8000;
constexpr uint32_t kSeed = 2024U;
constexpr uint32_t kFourSamples = 4;
constexpr float kHalf = 0.5F;
constexpr float kTwice = 2.0F;
constexpr uint8_t kOpaqueByte = 0xFF;
constexpr uint32_t kBadCorner = 4;     // the bad values' row and first column
constexpr uint32_t kBadReach = 24;     // the corner left out of the comparison (columns and rows below it)
constexpr double kMostMilliseconds = 1.5;
constexpr double kSomeSeconds = 12.25; // a game time (a band slot's quiet part: no tracking band)
constexpr double kBandSeconds = 9.9;   // a time with the tracking band rolling (slot 1, 0.9 s in)

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

struct Channel {
    std::size_t pixel = 0;
    std::size_t channel = 0;
};

uint16_t half_at(const std::vector<uint8_t>& bytes, Channel at)
{
    const std::size_t first = (at.pixel * kHalfPixelBytes) + (at.channel * 2);
    return static_cast<uint16_t>(core::at(bytes, first) | (core::at(bytes, first + 1) << kHighByte));
}

void put_half(std::span<uint8_t> bytes, Channel at, uint16_t bits)
{
    const std::size_t first = (at.pixel * kHalfPixelBytes) + (at.channel * 2);
    core::at(bytes, first) = static_cast<uint8_t>(bits & kByteMask);
    core::at(bytes, first + 1) = static_cast<uint8_t>(bits >> kHighByte);
}

// --- the picture ----------------------------------------------------------------------------------------------------

struct Box {
    uint32_t left = 0;
    uint32_t top = 0;
    uint32_t right = 0;
    uint32_t bottom = 0;
};
constexpr Box kNoisy{.left = 56, .top = 36, .right = 104, .bottom = 76}; // inside the box
constexpr float kBoxLeft = 40.25F;
constexpr float kBoxRight = 120.25F;
constexpr float kBoxTop = 20.25F;
constexpr float kBoxBottom = 90.25F;
constexpr float kFloorTop = 96.25F;
constexpr float kRidgeLeft = 140.25F;
constexpr float kRidgeRight = 250.25F;
constexpr float kRidgeTopEdge = 10.25F;
constexpr float kRidgeBottom = 80.25F;
constexpr uint32_t kGradientTop = 104;
constexpr uint32_t kGradientBottom = 112;
constexpr LookRgb kSkyBase{0.5F, 0.6F, 0.85F};
constexpr LookRgb kFloorBase{0.35F, 0.4F, 0.45F};
constexpr LookRgb kRidgeBase{0.3F, 0.65F, 0.35F};
constexpr LookRgb kBoxBase{0.75F, 0.3F, 0.2F};
constexpr float kNoise = 0.15F;
constexpr float kChannelNoise = 0.3F;
constexpr float kNoiseSteps = 65535.0F;

struct Picture {
    std::vector<LookRgb> colour; // (as a float target holds them)
    std::vector<uint16_t> bits;  // the halves written, red green blue per pixel
};

Picture make_picture()
{
    Picture picture;
    picture.colour.resize(static_cast<std::size_t>(kWidth) * kHeight);
    picture.bits.resize(picture.colour.size() * kChannels);
    uint32_t random = kSeed;
    constexpr uint32_t kMultiplier = 1664525U;
    constexpr uint32_t kIncrement = 1013904223U;
    constexpr uint32_t kNoiseBits = 16;
    const auto noise = [&] {
        random = (random * kMultiplier) + kIncrement;
        return ((static_cast<float>(random >> kNoiseBits) / kNoiseSteps) - kHalf) * kTwice * kNoise;
    };
    for (uint32_t row = 0; row < kHeight; ++row) {
        for (uint32_t column = 0; column < kWidth; ++column) {
            const auto across = static_cast<float>(column);
            const auto down = static_cast<float>(row);
            LookRgb base = kSkyBase;
            if (down > kFloorTop) {
                base = kFloorBase;
            }
            if (across > kRidgeLeft && across < kRidgeRight && down > kRidgeTopEdge && down < kRidgeBottom) {
                base = kRidgeBase;
            }
            if (across > kBoxLeft && across < kBoxRight && down > kBoxTop && down < kBoxBottom) {
                base = kBoxBase;
            }
            LookRgb colour{};
            if (row >= kGradientTop && row < kGradientBottom) {
                const float grey = across / static_cast<float>(kWidth - 1);
                colour = {grey, grey, grey};
            } else {
                const float shake = noise();
                for (std::size_t channel = 0; channel < kChannels; ++channel) {
                    core::at(colour, channel) = std::clamp(core::at(base, channel) + shake + (noise() * kChannelNoise), 0.0F, 1.0F);
                }
            }
            const std::size_t pixel = pixel_of(column, row);
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                const uint16_t bits = to_half(core::at(colour, channel));
                core::at(picture.bits, (pixel * kChannels) + channel) = bits;
                core::at(core::at(picture.colour, pixel), channel) = from_half(bits);
            }
        }
    }
    constexpr std::array<uint16_t, 4> kBad{0x7E00, 0x7C00, 0xFC00, 0xBC00}; // NaN, +inf, -inf, a negative
    for (std::size_t bad = 0; bad < kBad.size(); ++bad) {
        const std::size_t pixel = pixel_of(static_cast<uint32_t>(kBadCorner + bad), kBadCorner);
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            core::at(picture.bits, (pixel * kChannels) + channel) = core::at(kBad, bad);
            core::at(core::at(picture.colour, pixel), channel) = from_half(core::at(kBad, bad));
        }
    }
    return picture;
}

std::vector<uint8_t> picture_bytes(const Picture& picture, std::vector<LookRgb>* as_read)
{
    std::vector<uint8_t> bytes(picture.colour.size() * kBytePixelBytes);
    as_read->resize(picture.colour.size());
    for (std::size_t pixel = 0; pixel < picture.colour.size(); ++pixel) {
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            const float value = core::at(core::at(picture.colour, pixel), channel);
            const auto byte =
                static_cast<uint8_t>(std::lround(std::clamp(std::isfinite(value) ? value : 0.0F, 0.0F, 1.0F) * kByteSteps));
            core::at(bytes, (pixel * kBytePixelBytes) + (2 - channel)) = byte;
            core::at(core::at(*as_read, pixel), channel) = static_cast<float>(byte) / kByteSteps;
        }
        core::at(bytes, (pixel * kBytePixelBytes) + 3) = kOpaqueByte;
    }
    return bytes;
}

// --- the device's targets -------------------------------------------------------------------------------------------

struct Targets {
    GpuSurface* colour = nullptr;
    GpuSurface* depth = nullptr;
    bool floats = true;
};

Targets make_targets(render::Gpu& gpu, bool floats, uint32_t samples, uint32_t width = kWidth, uint32_t height = kHeight)
{
    Targets targets;
    targets.floats = floats;
    const render::GpuSurfaceDesc colour{.width = width,
                                        .height = height,
                                        .format = floats ? value::kFormatA16B16G16R16F : value::kFormatA8R8G8B8,
                                        .samples = samples,
                                        .quality = 0,
                                        .lockable_or_discard = samples == 0};
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

// The depth cleared, the colour target filled with the picture.
bool prepare(render::Gpu& gpu, const Targets& targets, const Picture& picture, std::vector<LookRgb>* input)
{
    gpu.begin_scene();
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = kWidth, .height = kHeight, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth, 0, 1.0F, 0);
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    gpu.end_scene();
    render::GpuLockedRect locked{};
    if (gpu.lock_surface(targets.colour, &locked, nullptr, 0) != value::kResultOk) {
        return false;
    }
    const std::size_t pixels = static_cast<std::size_t>(kWidth) * kHeight;
    if (targets.floats) {
        const std::span<uint8_t> bytes(static_cast<uint8_t*>(locked.bits), pixels * kHalfPixelBytes);
        for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                put_half(bytes, {.pixel = pixel, .channel = channel}, core::at(picture.bits, (pixel * kChannels) + channel));
            }
            put_half(bytes, {.pixel = pixel, .channel = 3}, kHalfAlpha);
        }
        *input = picture.colour;
    } else {
        const std::vector<uint8_t> made = picture_bytes(picture, input);
        const std::span<uint8_t> bytes(static_cast<uint8_t*>(locked.bits), pixels * kBytePixelBytes);
        std::ranges::copy(made, bytes.begin());
    }
    return gpu.unlock_surface(targets.colour) == value::kResultOk;
}

render::rt::ViewDrawn view_of(std::array<uint32_t, 4> rect, double seconds)
{
    render::rt::ViewDrawn view;
    view.rect = rect;
    view.output_rect = rect;
    view.depth_range = {0.0F, 1.0F};
    view.game_seconds = seconds;
    auto& projection = view.camera.projection;
    const float aspect = static_cast<float>(core::at(rect, 2)) / static_cast<float>(std::max(1U, core::at(rect, 3)));
    core::at(core::at(projection, 0), 0) = kVerticalScale / aspect;
    core::at(core::at(projection, 1), 1) = kVerticalScale;
    core::at(core::at(projection, 2), 2) = kFar / (kFar - kNear);
    core::at(core::at(projection, 2), 3) = 1.0F;
    core::at(core::at(projection, 3), 2) = -kNear * kFar / (kFar - kNear);
    for (std::size_t i = 0; i < 4; ++i) {
        core::at(core::at(view.camera.view, i), i) = 1.0F;
    }
    return view;
}

// The style: the toon's settings (rt_style, the strength) and the looks' knobs, as renderer_choice reads them.
void set_style(unsigned style, float strength, const vk::LookSettings& looks = {})
{
    vk::set_style_settings({.style = style, .strength = strength});
    vk::LookSettings settings = looks;
    settings.style = style;
    vk::set_look_settings(settings);
}

// The pass run on the targets over `rect` through the listener (inside a scene, as the game's frames); passes recorded.
uint64_t run_pass(render::Gpu& gpu, const Targets& targets, std::array<uint32_t, 4> rect, double seconds)
{
    gpu.begin_scene();
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    const uint64_t before = vk::look_passes();
    render::rt::world_view_drawn(view_of(rect, seconds));
    const uint64_t passes = vk::look_passes() - before;
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    gpu.end_scene();
    return passes;
}

bool read_colour(GpuSurface* target, bool floats, std::vector<LookRgb>* colour, std::vector<float>* alpha)
{
    std::vector<uint8_t> bytes;
    if (!vk::read_surface(target, &bytes)) {
        return false;
    }
    const std::size_t pixels = static_cast<std::size_t>(kWidth) * kHeight;
    colour->resize(pixels);
    alpha->resize(pixels);
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            core::at(core::at(*colour, pixel), channel) =
                floats ? from_half(half_at(bytes, {.pixel = pixel, .channel = channel}))
                       : static_cast<float>(core::at(bytes, (pixel * kBytePixelBytes) + (2 - channel))) / kByteSteps;
        }
        core::at(*alpha, pixel) = floats ? from_half(half_at(bytes, {.pixel = pixel, .channel = 3}))
                                         : static_cast<float>(core::at(bytes, (pixel * kBytePixelBytes) + 3)) / kByteSteps;
    }
    return true;
}

// The watercolour's paint read back, cut to the viewport's half size.
bool read_paint(vk::Extent half, std::vector<LookRgb>* paint)
{
    vk::Backend& state = vk::backend();
    const std::scoped_lock lock(state.mutex);
    vk::finish_work(state);
    std::vector<uint16_t> halves;
    vk::Extent size;
    if (!vk::read_look_paint(state, &halves, &size) || size.width < half.width || size.height < half.height) {
        return false;
    }
    paint->resize(static_cast<std::size_t>(half.width) * half.height);
    for (uint32_t row = 0; row < half.height; ++row) {
        for (uint32_t column = 0; column < half.width; ++column) {
            const std::size_t texel = (static_cast<std::size_t>(row) * size.width) + column;
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                core::at(core::at(*paint, (static_cast<std::size_t>(row) * half.width) + column), channel) =
                    from_half(core::at(halves, (texel * 4) + channel));
            }
        }
    }
    return true;
}

// --- the comparisons ------------------------------------------------------------------------------------------------

struct Compared {
    float worst = 0.0F;
    int off = 0;
    int outside = 0;
};

bool in_bad_corner(std::size_t pixel)
{
    return pixel % kWidth < kBadReach && pixel / kWidth < kBadReach;
}

// Every pixel's channels finite and within 0 - 1 (`outside`); the largest difference from the expectation and the
// pixels past `tolerance`, the bad values' corner left out.
Compared compare(const std::vector<LookRgb>& got, const std::vector<LookRgb>& expected, float tolerance)
{
    Compared result;
    for (std::size_t pixel = 0; pixel < got.size(); ++pixel) {
        float pixel_worst = 0.0F;
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            const float value = core::at(core::at(got, pixel), channel);
            if (!(value >= 0.0F && value <= 1.0F)) {
                ++result.outside;
                continue;
            }
            pixel_worst = std::max(pixel_worst, std::abs(value - core::at(core::at(expected, pixel), channel)));
        }
        if (in_bad_corner(pixel)) {
            continue;
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

float spread(const std::vector<LookRgb>& colour, const Box& box)
{
    double sum = 0.0;
    double squares = 0.0;
    double count = 0.0;
    for (uint32_t row = box.top; row < box.bottom; ++row) {
        for (uint32_t column = box.left; column < box.right; ++column) {
            const double red = core::at(core::at(colour, pixel_of(column, row)), 0);
            sum += red;
            squares += red * red;
            count += 1.0;
        }
    }
    const double mean = sum / count;
    return static_cast<float>(std::sqrt(std::max((squares / count) - (mean * mean), 0.0)));
}

// The reference of a style over the whole target.
std::vector<LookRgb> reference(unsigned style, const vk::LookSettings& looks, float strength, double seconds,
                               const std::vector<LookRgb>& input, const std::vector<LookRgb>* paint = nullptr)
{
    const std::array<uint32_t, 4> whole{0, 0, kWidth, kHeight};
    switch (style) {
    case vk::kLookVhs:
        return vk::vhs_reference(vk::vhs_constants(looks.vhs, strength, whole, seconds), input);
    case vk::kLookRetro:
        return vk::retro_reference(vk::retro_constants(looks.retro, strength, whole), input);
    default:
        return vk::watercolour_reference(vk::watercolour_constants(looks.watercolour, strength, whole), input, paint);
    }
}

const char* style_name(unsigned style)
{
    if (style == vk::kLookVhs) {
        return "vhs";
    }
    return style == vk::kLookRetro ? "retro" : "watercolour";
}

// --- the cases ------------------------------------------------------------------------------------------------------

struct StyleCase {
    const char* name = "";
    unsigned style = 0;
    vk::LookSettings looks;
    float strength = 1.0F;
    double seconds = kSomeSeconds;
    bool floats = true;
};

// A pass's input as the target held it, its output and the output's alpha, read back.
struct Filtered {
    std::vector<LookRgb> input;
    std::vector<LookRgb> out;
    std::vector<float> alpha;
};

// The pass over the whole target, read back; false when it couldn't.
bool filtered(render::Gpu& gpu, const StyleCase& test, const Picture& picture, Filtered* result, Tally* tally)
{
    set_style(test.style, test.strength, test.looks);
    const Targets targets = make_targets(gpu, test.floats, 0);
    const bool ready = targets.colour != nullptr && targets.depth != nullptr && prepare(gpu, targets, picture, &result->input);
    tally->check(ready, "the targets made, the colour filled");
    if (!ready) {
        release(gpu, targets);
        return false;
    }
    tally->check(run_pass(gpu, targets, {0, 0, kWidth, kHeight}, test.seconds) == 1, "the listener recorded the pass");
    const bool read = read_colour(targets.colour, test.floats, &result->out, &result->alpha);
    release(gpu, targets);
    tally->check(read, "the target read back");
    return read;
}

void check_style(render::Gpu& gpu, const StyleCase& test, const Picture& picture, Tally* tally)
{
    text::print("%s:\n", test.name);
    Filtered result;
    if (!filtered(gpu, test, picture, &result, tally)) {
        return;
    }
    const std::vector<LookRgb>& input = result.input;
    const std::vector<LookRgb>& out = result.out;
    const float tolerance = test.floats ? kTolerance : kTolerance + (1.0F / kByteSteps);
    std::vector<LookRgb> paint;
    if (test.style == vk::kLookWatercolour) {
        const vk::LookConstants constants =
            vk::watercolour_constants(test.looks.watercolour, test.strength, {0, 0, kWidth, kHeight});
        const vk::Extent half{.width = static_cast<uint32_t>(core::at(constants.info, 0)),
                              .height = static_cast<uint32_t>(core::at(constants.info, 1))};
        const bool read = read_paint(half, &paint);
        tally->check(read, "the paint read back");
        if (read) {
            const std::vector<LookRgb> twin = vk::watercolour_paint(constants, input);
            float worst = 0.0F;
            for (std::size_t texel = 0; texel < twin.size(); ++texel) {
                if (texel % half.width < kBadReach / 2 && texel / half.width < kBadReach / 2) {
                    continue; // (the bad values' corner)
                }
                for (std::size_t channel = 0; channel < kChannels; ++channel) {
                    worst = std::max(worst, std::abs(core::at(core::at(paint, texel), channel) -
                                                     core::at(core::at(twin, texel), channel)));
                }
            }
            tally->check(worst <= 1.0F / kByteSteps,
                         text::format("the paint (half resolution, edge-aware) as the twin's (largest difference %.5f)",
                                      static_cast<double>(worst)));
        }
    }
    const std::vector<LookRgb> expected =
        reference(test.style, test.looks, test.strength, test.seconds, input, paint.empty() ? nullptr : &paint);
    const Compared compared = compare(out, expected, tolerance);
    tally->check(compared.off <= kMostOff,
                 text::format("every pixel within %.1f steps of the reference (%d past it; the largest difference %.2f steps)",
                              static_cast<double>(tolerance * kByteSteps), compared.off,
                              static_cast<double>(compared.worst * kByteSteps)));
    tally->check(compared.outside == 0, text::format("every channel finite and within 0 - 1 (%d not)", compared.outside));
    if (!paint.empty()) {
        const Compared whole = compare(out, reference(test.style, test.looks, test.strength, test.seconds, input), tolerance);
        constexpr int kMostWhole = static_cast<int>(kWidth * kHeight / 100);
        tally->check(whole.off <= kMostWhole, text::format("the whole style against the twin alone: %d pixels past %.1f steps "
                                                           "(the largest difference %.2f steps)",
                                                           whole.off, static_cast<double>(tolerance * kByteSteps),
                                                           static_cast<double>(whole.worst * kByteSteps)));
    }
    int alpha_changed = 0;
    constexpr float kHalfAlphaValue = 0.5F;
    constexpr float kAlphaSlack = 1.0e-3F;
    for (const float value : result.alpha) {
        alpha_changed += std::abs(value - (test.floats ? kHalfAlphaValue : 1.0F)) > kAlphaSlack ? 1 : 0;
    }
    tally->check(alpha_changed == 0, text::format("alpha untouched (%d changed)", alpha_changed));
}

constexpr float kTwoPixelLines = 64.0F; // (retro: 2 x 2 cells on this 128-line target)
constexpr float kShadeSteps = 1000.0F;  // (a shade told apart from another to a thousandth)
constexpr std::size_t kFiveBitShades = 32;

// What each style does, beyond matching its twin.
void check_semantics(render::Gpu& gpu, const Picture& picture, Tally* tally)
{
    text::print("what each style does:\n");
    Filtered first;
    Filtered again;
    Filtered later;
    // vhs: the game clock drives it
    const StyleCase vhs{.name = "vhs", .style = vk::kLookVhs};
    StyleCase other = vhs;
    other.seconds = kSomeSeconds + 1.0;
    if (filtered(gpu, vhs, picture, &first, tally) && filtered(gpu, vhs, picture, &again, tally) &&
        filtered(gpu, other, picture, &later, tally)) {
        tally->check(first.out == again.out, "vhs: the same game time twice - the same picture (a paused game holds it)");
        int moved = 0;
        for (std::size_t pixel = 0; pixel < first.out.size(); ++pixel) {
            moved += core::at(first.out, pixel) != core::at(later.out, pixel) ? 1 : 0;
        }
        tally->check(std::cmp_greater(moved, first.out.size() / 4),
                     text::format("vhs: a second later the tape moved (%d pixels changed)", moved));
    }
    // vhs: the REC blinks (on at x.25 s, off at x.75 s): the overlay's bright dots near the top left
    const auto bright_top_left = [&](double seconds) {
        StyleCase test = vhs;
        test.seconds = seconds;
        Filtered result;
        if (!filtered(gpu, test, picture, &result, tally)) {
            return -1;
        }
        const vk::LookConstants constants = vk::vhs_constants({}, 1.0F, {0, 0, kWidth, kHeight}, seconds);
        const auto& places = core::at(constants.values, 4);
        int red = 0;
        constexpr float kRed = 0.55F;
        constexpr float kOther = 0.35F;
        // (the record dot's cell: 6 x 7 font pixels from the REC's corner, a few pixels of wobble either side)
        const float font = core::at(core::at(constants.values, 3), 3);
        constexpr float kWobbleRoom = 3.0F;
        constexpr float kCellColumns = 6.0F;
        constexpr float kCellRows = 7.0F;
        for (uint32_t row = 0; row < kHeight / 2; ++row) {
            for (uint32_t column = 0; column < kWidth / 2; ++column) {
                const LookRgb& colour = core::at(result.out, pixel_of(column, row));
                const auto across = static_cast<float>(column);
                const auto down = static_cast<float>(row);
                const bool in_dot = across >= core::at(places, 0) - kWobbleRoom &&
                                    across < core::at(places, 0) + (kCellColumns * font) + kWobbleRoom &&
                                    down >= core::at(places, 1) && down < core::at(places, 1) + (kCellRows * font);
                red += in_dot && core::at(colour, 0) > kRed && core::at(colour, 1) < kOther && core::at(colour, 2) < kOther ? 1 : 0;
            }
        }
        return red;
    };
    constexpr double kBlinkOff = kSomeSeconds + 0.5;
    const int on = bright_top_left(kSomeSeconds);
    const int off = bright_top_left(kBlinkOff);
    tally->check(on > 4 && off == 0, text::format("vhs: the red record dot blinks (%d red pixels, then %d)", on, off));
    // retro: flat cells, few shades
    vk::LookSettings looks;
    looks.retro.lines = kTwoPixelLines;
    Filtered retro;
    if (filtered(gpu, {.name = "retro", .style = vk::kLookRetro, .looks = looks}, picture, &retro, tally)) {
        const std::vector<LookRgb>& out = retro.out;
        int uneven = 0;
        std::array<std::set<int>, 3> shades;
        for (uint32_t row = 0; row < kHeight; row += 2) {
            for (uint32_t column = 0; column < kWidth; column += 2) {
                const LookRgb& corner = core::at(out, pixel_of(column, row));
                for (const auto& [across, down] : std::array<std::pair<uint32_t, uint32_t>, 3>{{{1, 0}, {0, 1}, {1, 1}}}) {
                    uneven += core::at(out, pixel_of(column + across, row + down)) != corner ? 1 : 0;
                }
                for (std::size_t channel = 0; channel < kChannels; ++channel) {
                    core::at(shades, channel).insert(static_cast<int>(std::lround(core::at(corner, channel) * kShadeSteps)));
                }
            }
        }
        const std::size_t most =
            std::max({core::at(shades, 0).size(), core::at(shades, 1).size(), core::at(shades, 2).size()});
        tally->check(uneven == 0, text::format("retro: each 2 x 2 cell one colour (%d pixels not)", uneven));
        tally->check(most <= kFiveBitShades, text::format("retro: at most 32 shades a channel (5 bits; %zu)", most));
    }
    // watercolour: the noise calmed
    Filtered painted;
    if (filtered(gpu, {.name = "watercolour", .style = vk::kLookWatercolour}, picture, &painted, tally)) {
        const float before = spread(painted.input, kNoisy);
        const float after = spread(painted.out, kNoisy);
        constexpr float kCalmer = 0.6F;
        tally->check(after < before * kCalmer, text::format("watercolour: the paint calms the noise (spread %.4f -> %.4f)",
                                                            static_cast<double>(before), static_cast<double>(after)));
    }
}

// Only the viewport changed; strength 0 changes nothing.
void check_skips(render::Gpu& gpu, const Picture& picture, Tally* tally)
{
    text::print("a viewport, strength 0:\n");
    const auto changed = [&](std::array<uint32_t, 4> rect, uint64_t* passes, int* inside_changed) {
        const Targets targets = make_targets(gpu, true, 0);
        std::vector<LookRgb> input;
        int outside = -1;
        if (targets.colour != nullptr && targets.depth != nullptr && prepare(gpu, targets, picture, &input)) {
            *passes = run_pass(gpu, targets, rect, kSomeSeconds);
            std::vector<uint8_t> bytes;
            if (vk::read_surface(targets.colour, &bytes)) {
                outside = 0;
                *inside_changed = 0;
                for (uint32_t row = 0; row < kHeight; ++row) {
                    for (uint32_t column = 0; column < kWidth; ++column) {
                        const std::size_t pixel = pixel_of(column, row);
                        bool differs = false;
                        for (std::size_t channel = 0; channel < kChannels; ++channel) {
                            differs = differs || half_at(bytes, {.pixel = pixel, .channel = channel}) !=
                                                     core::at(picture.bits, (pixel * kChannels) + channel);
                        }
                        const bool inside = column >= core::at(rect, 0) && column < core::at(rect, 0) + core::at(rect, 2) &&
                                            row >= core::at(rect, 1) && row < core::at(rect, 1) + core::at(rect, 3);
                        (inside ? *inside_changed : outside) += differs ? 1 : 0;
                    }
                }
            }
        }
        release(gpu, targets);
        return outside;
    };
    constexpr std::array<uint32_t, 4> kViewport{64, 16, 128, 96};
    for (const unsigned style : {vk::kLookVhs, vk::kLookRetro, vk::kLookWatercolour}) {
        set_style(style, 1.0F);
        uint64_t passes = 0;
        int inside = 0;
        const int outside = changed(kViewport, &passes, &inside);
        tally->check(passes == 1 && outside == 0 && std::cmp_greater(inside, core::at(kViewport, 2) * core::at(kViewport, 3) / 2),
                     text::format("%s, a 128 x 96 viewport: %d pixels changed inside, %d outside (bit for bit)",
                                  style_name(style), inside, outside));
        set_style(style, 0.0F);
        passes = 1;
        const int none = changed({0, 0, kWidth, kHeight}, &passes, &inside);
        tally->check(passes == 0 && none == 0 && inside == 0, text::format("%s, strength 0: no pass, every pixel as it was",
                                                                           style_name(style)));
    }
    set_style(0, 1.0F);
}

// The scene's quads (pre-transformed, flat colours) drawn into the bound targets.
void draw_fields(render::Gpu& gpu)
{
    struct ScreenVertex {
        float x, y, z, rhw;
        uint32_t colour;
    };
    constexpr uint32_t kFvf = 0x044; // XYZRHW | DIFFUSE
    constexpr uint32_t kSkyColour = 0xFF8090C0;
    gpu.set_viewport({.x = 0, .y = 0, .width = kWidth, .height = kHeight, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth, kSkyColour, 1.0F, 0);
    gpu.set_render_state(GpuRenderState::kZEnable, 0);
    gpu.set_render_state(GpuRenderState::kLighting, 0);
    gpu.set_render_state(GpuRenderState::kCullMode, value::kCullNone);
    gpu.set_render_state(GpuRenderState::kAlphaTestEnable, 0);
    gpu.set_render_state(GpuRenderState::kFogEnable, 0);
    gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    gpu.set_vertex_format(kFvf);
    gpu.set_texture(0, nullptr);
    gpu.set_stage_state(0, GpuStageState::kColourOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kColourArg1, value::kArgDiffuse);
    gpu.set_stage_state(0, GpuStageState::kAlphaOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kAlphaArg1, value::kArgDiffuse);
    std::vector<ScreenVertex> quads;
    const auto quad = [&](float left, float top, float right, float bottom, uint32_t colour) {
        constexpr float kDepth = 0.5F;
        const std::array<ScreenVertex, 4> corners{{{.x = left, .y = top, .z = kDepth, .rhw = 1.0F, .colour = colour},
                                                   {.x = right, .y = top, .z = kDepth, .rhw = 1.0F, .colour = colour},
                                                   {.x = right, .y = bottom, .z = kDepth, .rhw = 1.0F, .colour = colour},
                                                   {.x = left, .y = bottom, .z = kDepth, .rhw = 1.0F, .colour = colour}}};
        for (const std::size_t which : {0U, 1U, 2U, 0U, 2U, 3U}) {
            quads.push_back(core::at(corners, which));
        }
    };
    constexpr uint32_t kBoxColour = 0xFFC04030;
    constexpr uint32_t kFloorColour = 0xFF607080;
    quad(0.0F, kFloorTop, static_cast<float>(kWidth), static_cast<float>(kHeight), kFloorColour);
    quad(kBoxLeft, kBoxTop, kBoxRight, kBoxBottom, kBoxColour);
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(quads.size() / 3), quads.data(),
                         sizeof(ScreenVertex));
}

// Four samples: flat fields drawn, resolved, filtered, resolved again - each pixel the style of the resolved colour.
void check_multisampled(render::Gpu& gpu, Tally* tally)
{
    text::print("four samples:\n");
    for (const unsigned style : {vk::kLookVhs, vk::kLookRetro, vk::kLookWatercolour}) {
        set_style(style, 1.0F);
        const Targets targets = make_targets(gpu, true, kFourSamples);
        GpuSurface* before = nullptr;
        GpuSurface* after = nullptr;
        const render::GpuSurfaceDesc single{.width = kWidth,
                                            .height = kHeight,
                                            .format = value::kFormatA16B16G16R16F,
                                            .samples = 0,
                                            .quality = 0,
                                            .lockable_or_discard = true};
        const bool made = targets.colour != nullptr && targets.depth != nullptr &&
                          gpu.create_render_target(single, &before) == value::kResultOk &&
                          gpu.create_render_target(single, &after) == value::kResultOk;
        tally->check(made, "the multisampled targets made");
        if (made) {
            gpu.begin_scene();
            gpu.set_render_target(0, targets.colour);
            gpu.set_depth_surface(targets.depth);
            draw_fields(gpu);
            const bool resolved = gpu.copy_surface(targets.colour, nullptr, before, nullptr, value::kFilterNone) == value::kResultOk;
            gpu.set_render_target(0, targets.colour);
            gpu.set_depth_surface(targets.depth);
            const uint64_t passes = vk::look_passes();
            render::rt::world_view_drawn(view_of({0, 0, kWidth, kHeight}, kSomeSeconds));
            const bool ran = vk::look_passes() == passes + 1;
            const bool resolved_after = gpu.copy_surface(targets.colour, nullptr, after, nullptr, value::kFilterNone) == value::kResultOk;
            gpu.set_render_target(0, nullptr);
            gpu.set_depth_surface(nullptr);
            gpu.end_scene();
            std::vector<LookRgb> input;
            std::vector<LookRgb> out;
            std::vector<float> alpha;
            const bool read = resolved && ran && resolved_after && read_colour(before, true, &input, &alpha) &&
                              read_colour(after, true, &out, &alpha);
            tally->check(read, text::format("%s: drawn, resolved, filtered, resolved again", style_name(style)));
            if (read) {
                const Compared compared = compare(out, reference(style, {}, 1.0F, kSomeSeconds, input), kTolerance);
                constexpr int kMostResolved = static_cast<int>(kWidth * kHeight / 100);
                tally->check(compared.off <= kMostResolved && compared.outside == 0,
                             text::format("%s: each pixel the style of the resolved colour (%d past %.1f steps; %d outside "
                                          "0 - 1)",
                                          style_name(style), compared.off, static_cast<double>(kTolerance * kByteSteps),
                                          compared.outside));
            }
        }
        release(gpu, targets);
        for (GpuSurface* surface : {before, after}) {
            if (surface != nullptr) {
                gpu.release_surface(surface);
            }
        }
    }
    set_style(0, 1.0F);
}

// --- the retro geometry -------------------------------------------------------------------------------------------

// The quad: a floor-like plane from near (its bottom edge) to far (its top), its vertex colours red 0 near, 1 far, green
// everywhere (where it is drawn).
constexpr float kQuadNearZ = 2.0F;
constexpr float kQuadFarZ = 20.0F;
constexpr float kQuadNearY = -0.55F;
constexpr float kQuadFarY = 3.0F;
constexpr float kQuadNearX = 1.1F;
constexpr float kQuadFarX = 11.0F;

constexpr uint32_t kMiddleRow = kHeight * 9 / 16; // (inside the quad: between its top ~50 and bottom ~91)

struct Covered {
    int left = -1;   // the first covered column on the quad's middle row
    int right = -1;  // the last
    int top = -1;    // the first covered row on its middle column
    int bottom = -1; // the last
    float middle_red = -1.0F; // the red half way down between the top and bottom edges (middle column)
};

// The quad drawn as the game draws a world (view_begins, the draw, world_view_drawn); the coverage read back.
bool draw_quad(render::Gpu& gpu, bool retro, const vk::RetroSettings& settings, Covered* covered)
{
    struct Vertex {
        float x, y, z;
        uint32_t colour;
    };
    constexpr uint32_t kFvf = 0x042; // XYZ | DIFFUSE
    constexpr uint32_t kNearColour = 0xFF00FF00;
    constexpr uint32_t kFarColour = 0xFFFFFF00;
    vk::LookSettings looks;
    looks.retro = settings;
    set_style(retro ? vk::kLookRetro : 0, 0.0F, looks); // (strength 0: the picture's pass skipped, the geometry kept)
    const Targets targets = make_targets(gpu, true, 0);
    if (targets.colour == nullptr || targets.depth == nullptr) {
        release(gpu, targets);
        return false;
    }
    constexpr std::size_t kMatrixFloats = 16;
    const std::array<float, kMatrixFloats> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const render::rt::ViewDrawn view = view_of({0, 0, kWidth, kHeight}, kSomeSeconds);
    std::array<float, kMatrixFloats> projection{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            core::at(projection, (row * 4) + column) = core::at(core::at(view.camera.projection, row), column);
        }
    }
    gpu.begin_scene();
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = kWidth, .height = kHeight, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth, 0, 1.0F, 0);
    render::rt::view_begins(view);
    gpu.set_render_state(GpuRenderState::kZEnable, 0);
    gpu.set_render_state(GpuRenderState::kLighting, 0);
    gpu.set_render_state(GpuRenderState::kCullMode, value::kCullNone);
    gpu.set_render_state(GpuRenderState::kAlphaTestEnable, 0);
    gpu.set_render_state(GpuRenderState::kFogEnable, 0);
    gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    gpu.set_transform(render::GpuTransform::kWorld, identity.data());
    gpu.set_transform(render::GpuTransform::kView, identity.data());
    gpu.set_transform(render::GpuTransform::kProjection, projection.data());
    gpu.set_vertex_format(kFvf);
    gpu.set_texture(0, nullptr);
    gpu.set_stage_state(0, GpuStageState::kColourOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kColourArg1, value::kArgDiffuse);
    gpu.set_stage_state(0, GpuStageState::kAlphaOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kAlphaArg1, value::kArgDiffuse);
    const std::array<Vertex, 4> corners{{{.x = -kQuadFarX, .y = kQuadFarY, .z = kQuadFarZ, .colour = kFarColour},
                                         {.x = kQuadFarX, .y = kQuadFarY, .z = kQuadFarZ, .colour = kFarColour},
                                         {.x = kQuadNearX, .y = kQuadNearY, .z = kQuadNearZ, .colour = kNearColour},
                                         {.x = -kQuadNearX, .y = kQuadNearY, .z = kQuadNearZ, .colour = kNearColour}}};
    std::vector<Vertex> triangles;
    for (const std::size_t which : {0U, 1U, 2U, 0U, 2U, 3U}) {
        triangles.push_back(core::at(corners, which));
    }
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, 2, triangles.data(), sizeof(Vertex));
    render::rt::world_view_drawn(view);
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    gpu.end_scene();
    std::vector<LookRgb> colour;
    std::vector<float> alpha;
    const bool read = read_colour(targets.colour, true, &colour, &alpha);
    release(gpu, targets);
    if (!read) {
        return false;
    }
    const auto drawn = [&](uint32_t column, uint32_t row) { return core::at(core::at(colour, pixel_of(column, row)), 1) > kHalf; };
    const uint32_t middle_row = kMiddleRow;
    const uint32_t middle_column = kWidth / 2;
    for (uint32_t column = 0; column < kWidth; ++column) {
        if (drawn(column, middle_row)) {
            covered->left = covered->left < 0 ? static_cast<int>(column) : covered->left;
            covered->right = static_cast<int>(column);
        }
    }
    for (uint32_t row = 0; row < kHeight; ++row) {
        if (drawn(middle_column, row)) {
            covered->top = covered->top < 0 ? static_cast<int>(row) : covered->top;
            covered->bottom = static_cast<int>(row);
        }
    }
    if (covered->top >= 0) {
        const auto half_way = static_cast<uint32_t>((covered->top + covered->bottom + 1) / 2);
        covered->middle_red = core::at(core::at(colour, pixel_of(middle_column, half_way)), 0);
    }
    return true;
}

// Where the projection puts a camera-space point on the screen (pixels, Vulkan's: y down; Direct3D 9's half pixel added
// as the fixed-function shaders add it).
std::array<float, 2> on_screen(const std::array<float, 3>& point)
{
    const auto [x, y, z] = point;
    const float aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
    const float ndc_x = (kVerticalScale / aspect) * x / z;
    const float ndc_y = -kVerticalScale * y / z;
    return {((ndc_x + 1.0F) * kHalf * static_cast<float>(kWidth)) + kHalf,
            ((ndc_y + 1.0F) * kHalf * static_cast<float>(kHeight)) + kHalf};
}

// The first pixel whose centre is right of (below) an edge at `edge`, and the last left of it.
int first_after(float edge) { return static_cast<int>(std::ceil(edge - kHalf)); }
int last_before(float edge) { return static_cast<int>(std::ceil(edge - kHalf)) - 1; }

void check_retro_geometry(render::Gpu& gpu, Tally* tally)
{
    text::print("the retro geometry:\n");
    constexpr float kLines = 64.0F; // (2 x 2 pixel cells)
    const std::array<int32_t, 2> cells = vk::retro_cells(kLines, {.width = kWidth, .height = kHeight});
    const float cell_x = static_cast<float>(kWidth) / static_cast<float>(core::at(cells, 0));
    const float cell_y = static_cast<float>(kHeight) / static_cast<float>(core::at(cells, 1));
    const auto snap = [](float at, float cell) { return std::round(at / cell) * cell; };
    // the edges (the near corners' x at the middle row's... the quad's sides are straight lines on the screen; at the
    // middle row each side lies between its near and far corner)
    const std::array<float, 2> near_left = on_screen({-kQuadNearX, kQuadNearY, kQuadNearZ});
    const std::array<float, 2> far_left = on_screen({-kQuadFarX, kQuadFarY, kQuadFarZ});
    const float top = core::at(far_left, 1);
    const float bottom = core::at(near_left, 1);
    const auto side_at = [](std::array<float, 2> a, std::array<float, 2> b, float row) {
        const float along = (row - core::at(a, 1)) / (core::at(b, 1) - core::at(a, 1));
        return core::at(a, 0) + ((core::at(b, 0) - core::at(a, 0)) * along);
    };
    const float middle_row = static_cast<float>(kMiddleRow) + kHalf;
    // perspective: the red half way down the screen between the edges, from 1/z interpolated
    const float perspective_red = (kHalf / kQuadFarZ) / ((kHalf / kQuadNearZ) + (kHalf / kQuadFarZ));
    constexpr float kRedSlack = 0.04F;
    vk::RetroSettings settings;
    settings.lines = kLines;
    // off: where the projection puts it, perspective-correct
    Covered plain;
    const bool off_drawn = draw_quad(gpu, false, settings, &plain);
    tally->check(off_drawn && plain.top == first_after(top) && plain.bottom == last_before(bottom) &&
                     plain.left == first_after(side_at(near_left, far_left, middle_row)) &&
                     std::abs(plain.middle_red - perspective_red) < kRedSlack,
                 text::format("the style off: the quad where the projection puts it (rows %d - %d, columns %d - %d) and its "
                              "colours perspective-correct (red %.3f, ~%.3f)",
                              plain.top, plain.bottom, plain.left, plain.right, static_cast<double>(plain.middle_red),
                              static_cast<double>(perspective_red)));
    // retro: snapped corners, affine colours
    Covered snapped;
    const bool retro_drawn = draw_quad(gpu, true, settings, &snapped);
    const float snapped_top = snap(top, cell_y);
    const float snapped_bottom = snap(bottom, cell_y);
    const std::array<float, 2> snapped_near{snap(core::at(near_left, 0), cell_x), snapped_bottom};
    const std::array<float, 2> snapped_far{snap(core::at(far_left, 0), cell_x), snapped_top};
    tally->check(retro_drawn && snapped.top == first_after(snapped_top) && snapped.bottom == last_before(snapped_bottom) &&
                     snapped.left == first_after(side_at(snapped_near, snapped_far, middle_row)),
                 text::format("retro: the corners on the %dx%d grid (rows %d - %d, expected %d - %d; left column %d, "
                              "expected %d)",
                              core::at(cells, 0), core::at(cells, 1), snapped.top, snapped.bottom, first_after(snapped_top),
                              last_before(snapped_bottom), snapped.left,
                              first_after(side_at(snapped_near, snapped_far, middle_row))));
    tally->check(retro_drawn && std::abs(snapped.middle_red - kHalf) < kRedSlack,
                 text::format("retro: the colours interpolated on the screen - affine (red half way down %.3f, ~0.5)",
                              static_cast<double>(snapped.middle_red)));
    // retro with snap 0 and affine 0: as the style off
    settings.snap = 0.0F;
    settings.affine = 0.0F;
    Covered neither;
    const bool neither_drawn = draw_quad(gpu, true, settings, &neither);
    tally->check(neither_drawn && neither.top == plain.top && neither.bottom == plain.bottom && neither.left == plain.left &&
                     std::abs(neither.middle_red - plain.middle_red) < 1.0F / kByteSteps,
                 "retro, snap 0 and affine 0: the quad as with the style off");
    set_style(0, 1.0F);
}

// --- the cost -------------------------------------------------------------------------------------------------------

void measure_cost(render::Gpu& gpu, Tally* tally)
{
    constexpr int kTimedPasses = 20;
    constexpr double kNanosecondsPerMillisecond = 1.0e6;
    vk::Backend& state = vk::backend();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    if (properties.limits.timestampComputeAndGraphics == VK_FALSE) {
        text::print("the cost: no timestamps on this device\n");
        return;
    }
    VkQueryPool pool = VK_NULL_HANDLE;
    const VkQueryPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                     .pNext = nullptr,
                                     .flags = 0,
                                     .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                     .queryCount = 2,
                                     .pipelineStatistics = 0};
    if (vkCreateQueryPool(state.context.device(), &info, nullptr, &pool) != VK_SUCCESS) {
        return;
    }
    constexpr std::array<std::array<uint32_t, 2>, 2> kSizes{{{1920, 1080}, {2560, 1440}}};
    for (const unsigned style : {vk::kLookVhs, vk::kLookRetro, vk::kLookWatercolour}) {
        set_style(style, 1.0F);
        for (const auto& [width, height] : kSizes) {
            const Targets targets = make_targets(gpu, true, 0, width, height);
            if (targets.colour == nullptr || targets.depth == nullptr) {
                release(gpu, targets);
                continue;
            }
            gpu.begin_scene();
            gpu.set_render_target(0, targets.colour);
            gpu.set_depth_surface(targets.depth);
            const render::rt::ViewDrawn view = view_of({0, 0, width, height}, kSomeSeconds);
            render::rt::world_view_drawn(view); // (the pipelines and images made first)
            {
                const std::scoped_lock lock(state.mutex);
                VkCommandBuffer commands = vk::outside_pass(state);
                vkCmdResetQueryPool(commands, pool, 0, 2);
                vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool, 0);
            }
            for (int i = 0; i < kTimedPasses; ++i) {
                render::rt::world_view_drawn(view);
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
            release(gpu, targets);
            const double each = static_cast<double>(stamps.back() - stamps.front()) *
                                static_cast<double>(properties.limits.timestampPeriod) / kNanosecondsPerMillisecond /
                                kTimedPasses;
            text::print("the cost: %s %.3f ms a pass at %ux%u\n", style_name(style), each, width, height);
            // (the budget a discrete card's: an integrated one shares its memory's bandwidth with the processor)
            constexpr uint32_t kLargest = 2560;
            const bool discrete = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
            tally->check(std::isfinite(each) && each > 0.0 && (width != kLargest || !discrete || each <= kMostMilliseconds),
                         text::format("%s timed at %ux%u (a discrete card: at most %.1f ms at 2560x1440)", style_name(style),
                                      width, height, kMostMilliseconds));
        }
    }
    vkDestroyQueryPool(state.context.device(), pool, nullptr);
    set_style(0, 1.0F);
}

// --- the device -----------------------------------------------------------------------------------------------------

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
    const Picture picture = make_picture();
    // (every knob at its range's top - the retro: 64 lines, 3 bits, half the dither -; every knob off - the retro: 720
    // lines, 8 bits, no dither)
    constexpr float kTop = 2.0F;
    constexpr float kStrongColour = 1.4F;
    constexpr float kFewBits = 3.0F;
    constexpr float kHalfDither = 0.5F;
    constexpr float kMostLines = 720.0F;
    constexpr float kAllBits = 8.0F;
    vk::LookSettings strong;
    strong.vhs = {.fisheye = 1.0F, .bleed = kTop, .wobble = kTop, .noise = kTop, .scanlines = 1.0F,
                  .saturation = kStrongColour, .overlay = true};
    strong.retro = {.lines = kTwoPixelLines, .bits = kFewBits, .dither = kHalfDither, .snap = 1.0F, .affine = 1.0F};
    strong.watercolour = {.bleed = kTop, .flow = kTop, .granulation = kTop, .edges = kTop, .paper = 1.0F, .outlines = 1.0F};
    vk::LookSettings plain;
    plain.vhs = {.fisheye = 0.0F, .bleed = 0.0F, .wobble = 0.0F, .noise = 0.0F, .scanlines = 0.0F, .saturation = 1.0F,
                 .overlay = false};
    plain.retro = {.lines = kMostLines, .bits = kAllBits, .dither = 0.0F, .snap = 0.0F, .affine = 0.0F};
    plain.watercolour = {.bleed = 0.0F, .flow = 0.0F, .granulation = 0.0F, .edges = 0.0F, .paper = 0.0F, .outlines = 0.0F};
    constexpr float kSoftMix = 0.6F;
    const std::array<StyleCase, 12> cases{{
        {.name = "vhs, the defaults", .style = vk::kLookVhs},
        {.name = "vhs, the tracking band rolling", .style = vk::kLookVhs, .seconds = kBandSeconds},
        {.name = "vhs, every knob up", .style = vk::kLookVhs, .looks = strong, .seconds = kBandSeconds},
        {.name = "vhs, every knob off, strength 0.6", .style = vk::kLookVhs, .looks = plain, .strength = kSoftMix},
        {.name = "vhs, an 8-bit target", .style = vk::kLookVhs, .floats = false},
        {.name = "retro, the defaults", .style = vk::kLookRetro},
        {.name = "retro, 64 lines, 3 bits, half the dither", .style = vk::kLookRetro, .looks = strong},
        {.name = "retro, 720 lines, 8 bits, no dither, strength 0.6", .style = vk::kLookRetro, .looks = plain, .strength = kSoftMix},
        {.name = "watercolour, the defaults", .style = vk::kLookWatercolour},
        {.name = "watercolour, every knob up", .style = vk::kLookWatercolour, .looks = strong},
        {.name = "watercolour, every knob off, strength 0.6", .style = vk::kLookWatercolour, .looks = plain, .strength = kSoftMix},
        {.name = "watercolour, an 8-bit target", .style = vk::kLookWatercolour, .floats = false},
    }};
    constexpr uint32_t kBandFlag = 4U;
    const vk::LookConstants band = vk::vhs_constants({}, 1.0F, {0, 0, kWidth, kHeight}, kBandSeconds);
    tally.check((static_cast<uint32_t>(core::at(band.info, 2)) & kBandFlag) != 0,
                "vhs: a tracking band rolls at the band cases' game time");
    for (const StyleCase& test : cases) {
        check_style(gpu, test, picture, &tally);
    }
    check_semantics(gpu, picture, &tally);
    check_skips(gpu, picture, &tally);
    check_multisampled(gpu, &tally);
    check_retro_geometry(gpu, &tally);
    measure_cost(gpu, &tally);
    set_style(0, 1.0F);
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
    text::print("thug2_vulkan_style_looks_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan style looks check", kWindowWidth, kWindowHeight,
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
