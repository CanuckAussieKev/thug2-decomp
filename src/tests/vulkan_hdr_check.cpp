// NOT ORIGINAL - thug2_vulkan_hdr_check.exe: the Vulkan Gpu's HDR targets and tone mapping (gpu_vulkan/vulkan_hdr.h)
// tried on this machine against the curve worked out on the processor (vulkan_hdr.h tone_map_reference).
//
// The device is made with rt_hdr on (its back buffer 16-bit float, reported as X8R8G8B8). Cases:
//   - the curve: a float render target filled through a lock with synthetic colours - every 16-bit float from 0 to 16,
//     random ones up to 8, and not-a-number, both infinities, negatives, the largest float, denormals - then the pass
//     run through rt_scene's listener (rt::world_view_drawn, as draw_viewport calls it) and each channel compared with
//     the reference: within 1/255, never outside 0 - 1, alpha untouched. Settings: the defaults, white point 1 (a clip),
//     exposure +2 and -1.5 stops, ACES, saturation and contrast, white point 4, a grading table (17^3, not the identity);
//   - identity: every value 0 - 1 bit for bit unchanged with white point 1, every value up to the knee with the
//     defaults;
//   - a viewport: only its rectangle mapped, the rest of the target left as it was;
//   - an 8-bit target: no pass, nothing changed; the setting off: no pass;
//   - four samples: a triangle drawn twice with an additive blend (values up to 1.5) over a cleared float target; the
//     resolve keeps the values above 1; after the pass each pixel is the mean of its mapped samples (its coverage found
//     from the resolve before); the target copied into an 8-bit render target (resolved in its own format, then
//     converted) matches the float resolve within one step;
//   - the back buffer: float, its format reported 8-bit; drawn into, mapped, presented; read_front_buffer and the
//     screenshot's read (read_render_target into a system surface) give the mapped colours as bytes;
//   - .cube files: a good one read, a 1D one, a short one and a missing size refused.
//   thug2_vulkan_hdr_check [--device N] [--display N] [--no-validation]
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
#include <initializer_list>
#include <limits>
#include <mutex>
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
#include "gpu_vulkan/vulkan_hdr.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "tests/check_window.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuRenderState;
using render::GpuStageState;
using render::GpuSurface;
namespace value = render::gpu_value;
namespace vk = render::vulkan;

constexpr int kWindowWidth = 256;
constexpr int kWindowHeight = 128;
constexpr uint32_t kWidth = 256;
constexpr uint32_t kHeight = 128;
constexpr int kBitsPerByte = 8;
constexpr std::size_t kHalfPixelBytes = 8;
constexpr std::size_t kChannels = 3; // (red, green, blue)
constexpr std::size_t kBytePixelBytes = 4;
constexpr float kStep = 1.0F / 255.0F;
constexpr float kSlack = 1.0e-4F; // (16-bit floats' rounding on top of a step)
constexpr uint16_t kHalfOne = 0x3C00;
constexpr uint16_t kHalfSixteen = 0x4C00;
constexpr uint16_t kHalfEight = 0x4800;
constexpr uint16_t kHalfAlpha = 0x3800; // 0.5: the alpha the pass must leave alone
constexpr uint32_t kSeed = 12345U;
constexpr uint32_t kFvf = 0x044; // XYZRHW | DIFFUSE
constexpr uint32_t kBackground = 0xFF204060;
constexpr uint32_t kTriangleColour = 0xFFC08040; // added twice over the background: red 1.63
constexpr uint32_t kFourSamples = 4;
constexpr uint32_t kLutSize = 17;
constexpr uint32_t kByteMask = 0xFF;
constexpr uint32_t kHighByte = 8;      // (a 16-bit float's second byte)
constexpr uint16_t kHalfSign = 0x8000;
constexpr float kByteSteps = 255.0F;
constexpr double kLutBend = 0.8;       // the check's table: green bent by this power,
constexpr double kLutRedScale = 0.9;   // red scaled
constexpr double kLutRedLift = 0.05;   // and lifted
constexpr int kLeastIdentityValues = 10000;
constexpr float kCoverageSlack = 0.05F; // (samples)
constexpr float kAboveOne = 1.4F;
constexpr int kLeastEdges = 100;

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

// --- 16-bit floats ------------------------------------------------------------------------------------------------

float from_half(uint16_t bits)
{
    constexpr uint32_t kMantissaBits = 10;
    constexpr uint32_t kExponentMask = 0x1F;
    constexpr uint32_t kMantissaMask = 0x3FF;
    constexpr uint32_t kImplicitOne = 0x400;
    constexpr int kSmallestPower = -24; // a denormal's unit: 2^(1 - 15 - 10)
    constexpr int kPowerBias = 25;      // 15 + 10
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

// Where a channel of a pixel is in a float target's bytes.
struct Channel {
    std::size_t pixel = 0;
    std::size_t channel = 0;
};
std::size_t offset_of(Channel at) { return (at.pixel * kHalfPixelBytes) + (at.channel * 2); }

uint16_t half_at(const std::vector<uint8_t>& bytes, Channel at)
{
    const std::size_t first = offset_of(at);
    return static_cast<uint16_t>(core::at(bytes, first) | (core::at(bytes, first + 1) << kHighByte));
}

void put_half(std::span<uint8_t> bytes, Channel at, uint16_t bits)
{
    const std::size_t first = offset_of(at);
    core::at(bytes, first) = static_cast<uint8_t>(bits & kByteMask);
    core::at(bytes, first + 1) = static_cast<uint8_t>(bits >> kHighByte);
}

// The inputs: every value 0 - 16, then the special ones, then random ones up to 8 (an LCG: the same each run).
std::vector<uint16_t> input_values()
{
    std::vector<uint16_t> values;
    for (uint32_t bits = 0; bits <= kHalfSixteen; ++bits) {
        values.push_back(static_cast<uint16_t>(bits));
    }
    for (const uint16_t special : {0x7E00, 0x7C00, 0xFC00, 0xBC00, 0x7BFF, 0x0001, 0x8000, 0xFE00, 0x5BFF}) {
        values.push_back(special);
    }
    uint32_t random = kSeed;
    constexpr uint32_t kMultiplier = 1664525U;
    constexpr uint32_t kIncrement = 1013904223U;
    while (values.size() < static_cast<std::size_t>(kWidth) * kHeight) {
        random = (random * kMultiplier) + kIncrement;
        values.push_back(static_cast<uint16_t>((random >> kHighByte) % (kHalfEight + 1U)));
    }
    return values;
}

// The pixel's three inputs (decorrelated walks through the values).
std::array<uint16_t, 3> pixel_inputs(const std::vector<uint16_t>& values, std::size_t pixel)
{
    constexpr std::size_t kGreenStride = 7919;
    constexpr std::size_t kBlueStride = 15013;
    const std::size_t count = values.size();
    return {core::at(values, pixel % count), core::at(values, (pixel + kGreenStride) % count),
            core::at(values, (pixel + kBlueStride) % count)};
}

// --- the device's targets -------------------------------------------------------------------------------------------

render::rt::ViewDrawn view_of(std::array<uint32_t, 4> rect)
{
    render::rt::ViewDrawn view;
    view.rect = rect;
    view.depth_range = {0.0F, 1.0F};
    return view;
}

GpuSurface* make_target(render::Gpu& gpu, uint32_t format, uint32_t samples, bool lockable)
{
    GpuSurface* surface = nullptr;
    const long made = gpu.create_render_target({.width = kWidth,
                                                .height = kHeight,
                                                .format = format,
                                                .samples = samples,
                                                .quality = 0,
                                                .lockable_or_discard = lockable},
                                               &surface);
    return made == value::kResultOk ? surface : nullptr;
}

// A lockable float target's pixels written.
bool fill(render::Gpu& gpu, GpuSurface* target, const std::vector<uint16_t>& values)
{
    render::GpuLockedRect locked{};
    if (gpu.lock_surface(target, &locked, nullptr, 0) != value::kResultOk) {
        return false;
    }
    const std::span<uint8_t> bytes(static_cast<uint8_t*>(locked.bits), static_cast<std::size_t>(kWidth) * kHeight * kHalfPixelBytes);
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(kWidth) * kHeight; ++pixel) {
        const std::array<uint16_t, 3> rgb = pixel_inputs(values, pixel);
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            put_half(bytes, {.pixel = pixel, .channel = channel}, core::at(rgb, channel));
        }
        put_half(bytes, {.pixel = pixel, .channel = 3}, kHalfAlpha);
    }
    return gpu.unlock_surface(target) == value::kResultOk;
}

// The pass run on `target` over `rect` through the listener (inside a scene, as the game's frames); passes recorded.
uint64_t run_pass(render::Gpu& gpu, GpuSurface* target, std::array<uint32_t, 4> rect)
{
    gpu.begin_scene();
    gpu.set_render_target(0, target);
    gpu.set_depth_surface(nullptr);
    const uint64_t before = vk::tone_map_passes();
    render::rt::world_view_drawn(view_of(rect));
    const uint64_t passes = vk::tone_map_passes() - before;
    gpu.end_scene();
    return passes;
}

// --- the curve ------------------------------------------------------------------------------------------------------

struct CurveCase {
    const char* name = "";
    vk::HdrSettings settings;
    bool lut = false;
};

// A grading table that is not the identity: channels swapped and bent.
vk::GradeLut test_lut()
{
    std::string cube = "# the check's table\nTITLE \"swap\"\nLUT_3D_SIZE 17\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n";
    const double top = kLutSize - 1;
    for (uint32_t blue = 0; blue < kLutSize; ++blue) {
        for (uint32_t green = 0; green < kLutSize; ++green) {
            for (uint32_t red = 0; red < kLutSize; ++red) {
                cube += text::format("%.6f %.6f %.6f\n", std::pow(green / top, kLutBend), blue / top,
                                     (kLutRedScale * red / top) + kLutRedLift);
            }
        }
    }
    vk::GradeLut lut;
    std::string error;
    if (!vk::parse_cube(cube, &lut, &error)) {
        text::print("  the check's own table refused: %s\n", error.c_str());
    }
    return lut;
}

void apply(const CurveCase& test, const vk::GradeLut& lut)
{
    vk::set_hdr_settings(test.settings);
    vk::set_hdr_lut(test.lut ? lut : vk::GradeLut{});
}

void check_curve(render::Gpu& gpu, const CurveCase& test, const vk::GradeLut& lut, const std::vector<uint16_t>& values,
                 Tally* tally)
{
    text::print("%s:\n", test.name);
    apply(test, lut);
    const vk::ToneParameters parameters = vk::tone_parameters(test.settings);
    GpuSurface* target = make_target(gpu, value::kFormatA16B16G16R16F, 0, true);
    tally->check(target != nullptr && fill(gpu, target, values), "a float target filled");
    if (target == nullptr) {
        return;
    }
    tally->check(run_pass(gpu, target, {0, 0, kWidth, kHeight}) == 1, "the listener recorded the pass");
    std::vector<uint8_t> out;
    const bool read = vk::read_surface(target, &out) && out.size() == static_cast<std::size_t>(kWidth) * kHeight * kHalfPixelBytes;
    tally->check(read, "the target read back");
    if (read) {
        float worst = 0.0F;
        int wrong = 0;
        int outside = 0;
        int alpha = 0;
        for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(kWidth) * kHeight; ++pixel) {
            const std::array<uint16_t, 3> in = pixel_inputs(values, pixel);
            const std::array<float, 3> expected = vk::tone_map_reference(
                parameters, test.lut ? &lut : nullptr,
                {from_half(core::at(in, 0)), from_half(core::at(in, 1)), from_half(core::at(in, 2))});
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                const float got = from_half(half_at(out, {.pixel = pixel, .channel = channel}));
                if (!(got >= 0.0F && got <= 1.0F)) {
                    ++outside;
                    continue;
                }
                const float error = std::abs(got - core::at(expected, channel));
                worst = std::max(worst, error);
                if (error > kStep + kSlack) {
                    if (wrong < 3) {
                        text::print("    pixel %zu channel %zu: in 0x%04X -> %.6f, reference %.6f\n", pixel, channel,
                                    core::at(in, channel), static_cast<double>(got), static_cast<double>(core::at(expected, channel)));
                    }
                    ++wrong;
                }
            }
            alpha += half_at(out, {.pixel = pixel, .channel = 3}) != kHalfAlpha ? 1 : 0;
        }
        tally->check(wrong == 0, text::format("every channel within 1/255 of the reference (%d off; the largest "
                                              "difference %.5f = %.2f steps)",
                                              wrong, static_cast<double>(worst), static_cast<double>(worst * kByteSteps)));
        tally->check(outside == 0, text::format("every channel finite and within 0 - 1 (%d not)", outside));
        tally->check(alpha == 0, text::format("alpha untouched (%d changed)", alpha));
        // the special inputs: not a number and minus infinity give 0, plus infinity white
        const std::array<std::pair<uint16_t, float>, 3> specials{{{0x7E00, 0.0F}, {0xFC00, 0.0F}, {0x7C00, 1.0F}}};
        if (test.settings.saturation == 1.0F && test.settings.contrast == 1.0F && !test.lut &&
            test.settings.tonemap == 0) {
            int special_wrong = 0;
            for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(kWidth) * kHeight; ++pixel) {
                const std::array<uint16_t, 3> inputs = pixel_inputs(values, pixel);
                const uint16_t in = core::at(inputs, 0);
                for (const auto& [bits, wanted] : specials) {
                    if (in == bits && std::abs(from_half(half_at(out, {.pixel = pixel, .channel = 0})) - wanted) > kStep) {
                        ++special_wrong;
                    }
                }
            }
            tally->check(special_wrong == 0, "not a number and -infinity shown black, +infinity white");
        }
    }
    gpu.release_surface(target);
}

// Values that must pass bit for bit: [0, 1] with white point 1, [0, knee] with the defaults.
void check_identity(render::Gpu& gpu, float white_point, const std::vector<uint16_t>& values, Tally* tally)
{
    vk::HdrSettings settings{.on = true};
    settings.white_point = white_point;
    vk::set_hdr_settings(settings);
    vk::set_hdr_lut({});
    const float knee = vk::tone_parameters(settings).knee;
    text::print("identity up to %.4f (white point %.2f):\n", static_cast<double>(knee), static_cast<double>(white_point));
    GpuSurface* target = make_target(gpu, value::kFormatA16B16G16R16F, 0, true);
    if (target == nullptr || !fill(gpu, target, values) || run_pass(gpu, target, {0, 0, kWidth, kHeight}) != 1) {
        tally->check(false, "the pass run");
        if (target != nullptr) {
            gpu.release_surface(target);
        }
        return;
    }
    std::vector<uint8_t> out;
    if (!vk::read_surface(target, &out)) {
        tally->check(false, "the target read back");
        gpu.release_surface(target);
        return;
    }
    int compared = 0;
    int changed = 0;
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(kWidth) * kHeight; ++pixel) {
        const std::array<uint16_t, 3> in = pixel_inputs(values, pixel);
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            const float input = from_half(core::at(in, channel));
            if (input >= 0.0F && input <= knee && (core::at(in, channel) & kHalfSign) == 0) {
                ++compared;
                changed += half_at(out, {.pixel = pixel, .channel = channel}) != core::at(in, channel) ? 1 : 0;
            }
        }
    }
    tally->check(changed == 0 && compared > kLeastIdentityValues,
                 text::format("%d values 0 - %.4f unchanged bit for bit (%d changed)", compared, static_cast<double>(knee), changed));
    gpu.release_surface(target);
}

// Only the viewport mapped; an 8-bit target and the setting off left alone.
void check_skips(render::Gpu& gpu, const std::vector<uint16_t>& values, Tally* tally)
{
    text::print("a viewport, an 8-bit target, the setting off:\n");
    constexpr float kTwoStops = 2.0F;
    vk::set_hdr_settings({.on = true, .exposure = kTwoStops});
    vk::set_hdr_lut({});
    GpuSurface* target = make_target(gpu, value::kFormatA16B16G16R16F, 0, true);
    constexpr uint32_t kLeft = 64;
    constexpr uint32_t kTop = 32;
    constexpr uint32_t kSide = 64;
    std::vector<uint8_t> out;
    const bool ran = target != nullptr && fill(gpu, target, values) &&
                     run_pass(gpu, target, {kLeft, kTop, kSide, kSide}) == 1 && vk::read_surface(target, &out);
    tally->check(ran, "the pass over a 64 x 64 viewport");
    if (ran) {
        int inside_changed = 0;
        int outside_changed = 0;
        for (uint32_t row = 0; row < kHeight; ++row) {
            for (uint32_t column = 0; column < kWidth; ++column) {
                const std::size_t pixel = (static_cast<std::size_t>(row) * kWidth) + column;
                const bool inside =
                    column >= kLeft && column < kLeft + kSide && row >= kTop && row < kTop + kSide;
                const std::array<uint16_t, 3> in = pixel_inputs(values, pixel);
                bool changed = false;
                for (std::size_t channel = 0; channel < kChannels; ++channel) {
                    changed = changed || half_at(out, {.pixel = pixel, .channel = channel}) != core::at(in, channel);
                }
                (inside ? inside_changed : outside_changed) += changed ? 1 : 0;
            }
        }
        tally->check(outside_changed == 0 && std::cmp_greater(inside_changed, kSide * kSide / 2),
                     text::format("pixels changed: %d inside, %d outside", inside_changed, outside_changed));
    }
    if (target != nullptr) {
        gpu.release_surface(target);
    }
    // an 8-bit target: no pass
    GpuSurface* bytes = make_target(gpu, value::kFormatX8R8G8B8, 0, true);
    tally->check(bytes != nullptr && run_pass(gpu, bytes, {0, 0, kWidth, kHeight}) == 0, "an 8-bit target: no pass");
    if (bytes != nullptr) {
        gpu.release_surface(bytes);
    }
    // off: the listener gone
    vk::set_hdr_settings({});
    GpuSurface* floats = make_target(gpu, value::kFormatA16B16G16R16F, 0, true);
    tally->check(floats != nullptr && run_pass(gpu, floats, {0, 0, kWidth, kHeight}) == 0, "rt_hdr off: no pass");
    if (floats != nullptr) {
        gpu.release_surface(floats);
    }
}

// --- four samples -------------------------------------------------------------------------------------------------

struct ScreenVertex {
    float x, y, z, rhw;
    uint32_t colour;
};

// The background cleared, then a triangle added twice (an additive blend: values above 1 in a float target).
void draw_triangles(render::Gpu& gpu)
{
    gpu.clear(0, nullptr, value::kClearTarget, kBackground, 1.0F, 0);
    gpu.set_render_state(GpuRenderState::kZEnable, 0);
    gpu.set_render_state(GpuRenderState::kZWriteEnable, 0);
    gpu.set_render_state(GpuRenderState::kLighting, 0);
    gpu.set_render_state(GpuRenderState::kCullMode, value::kCullNone);
    gpu.set_render_state(GpuRenderState::kAlphaTestEnable, 0);
    gpu.set_render_state(GpuRenderState::kFogEnable, 0);
    gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 1);
    gpu.set_render_state(GpuRenderState::kSrcBlend, value::kBlendOne);
    gpu.set_render_state(GpuRenderState::kDestBlend, value::kBlendOne);
    gpu.set_render_state(GpuRenderState::kBlendOp, value::kBlendOpAdd);
    gpu.set_vertex_format(kFvf);
    gpu.set_texture(0, nullptr);
    gpu.set_stage_state(0, GpuStageState::kColourOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kColourArg1, value::kArgDiffuse);
    gpu.set_stage_state(0, GpuStageState::kAlphaOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kAlphaArg1, value::kArgDiffuse);
    // (the background's own colour is cleared, not drawn: the triangle's sides cross the pixels at odd slopes)
    const std::array<ScreenVertex, 3> triangle{{
        {.x = 20.3F, .y = 10.7F, .z = 0.5F, .rhw = 1.0F, .colour = kTriangleColour},
        {.x = 230.6F, .y = 40.2F, .z = 0.5F, .rhw = 1.0F, .colour = kTriangleColour},
        {.x = 90.1F, .y = 120.9F, .z = 0.5F, .rhw = 1.0F, .colour = kTriangleColour},
    }};
    for (int pass = 0; pass < 2; ++pass) {
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, 1, triangle.data(), sizeof(ScreenVertex));
    }
    gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
}

std::array<float, 3> colour_of(uint32_t argb)
{
    constexpr uint32_t kRedShift = 16;
    constexpr uint32_t kGreenShift = 8;
    return {static_cast<float>((argb >> kRedShift) & kByteMask) / kByteSteps,
            static_cast<float>((argb >> kGreenShift) & kByteMask) / kByteSteps,
            static_cast<float>(argb & kByteMask) / kByteSteps};
}

// Inside the triangle: the background with the triangle's colour added twice.
std::array<float, 3> inside_colour()
{
    const std::array<float, 3> background = colour_of(kBackground);
    const std::array<float, 3> added = colour_of(kTriangleColour);
    constexpr float kTimesAdded = 2.0F;
    std::array<float, 3> inside{};
    for (std::size_t channel = 0; channel < inside.size(); ++channel) {
        core::at(inside, channel) = core::at(background, channel) + (kTimesAdded * core::at(added, channel));
    }
    return inside;
}

void check_multisampled(render::Gpu& gpu, Tally* tally)
{
    text::print("four samples:\n");
    const vk::HdrSettings settings{.on = true, .exposure = 1.0F};
    vk::set_hdr_settings(settings);
    vk::set_hdr_lut({});
    const vk::ToneParameters parameters = vk::tone_parameters(settings);
    GpuSurface* msaa = make_target(gpu, value::kFormatA16B16G16R16F, kFourSamples, false);
    GpuSurface* before = make_target(gpu, value::kFormatA16B16G16R16F, 0, true);
    GpuSurface* after = make_target(gpu, value::kFormatA16B16G16R16F, 0, true);
    GpuSurface* bytes = make_target(gpu, value::kFormatA8R8G8B8, 0, true);
    if (msaa == nullptr || before == nullptr || after == nullptr || bytes == nullptr) {
        tally->check(false, "the targets made");
        for (GpuSurface* surface : {msaa, before, after, bytes}) {
            if (surface != nullptr) {
                gpu.release_surface(surface);
            }
        }
        return;
    }
    gpu.begin_scene();
    gpu.set_render_target(0, msaa);
    gpu.set_depth_surface(nullptr);
    gpu.set_viewport({.x = 0, .y = 0, .width = kWidth, .height = kHeight, .min_z = 0.0F, .max_z = 1.0F});
    draw_triangles(gpu);
    const bool resolved_before = gpu.copy_surface(msaa, nullptr, before, nullptr, value::kFilterNone) == value::kResultOk;
    const uint64_t passes = vk::tone_map_passes();
    gpu.set_render_target(0, msaa);
    render::rt::world_view_drawn(view_of({0, 0, kWidth, kHeight}));
    const bool mapped = vk::tone_map_passes() == passes + 1;
    const bool resolved_after = gpu.copy_surface(msaa, nullptr, after, nullptr, value::kFilterNone) == value::kResultOk;
    const bool converted = gpu.copy_surface(msaa, nullptr, bytes, nullptr, value::kFilterNone) == value::kResultOk;
    gpu.set_render_target(0, nullptr);
    gpu.end_scene();
    tally->check(resolved_before && mapped && resolved_after,
                 "drawn, resolved, the pass recorded on the multisampled target, resolved again");
    tally->check(converted, "the multisampled float target copied into an 8-bit render target");
    std::vector<uint8_t> was;
    std::vector<uint8_t> now;
    std::vector<uint8_t> eight;
    const bool read = vk::read_surface(before, &was) && vk::read_surface(after, &now) && vk::read_surface(bytes, &eight);
    tally->check(read, "the resolves read back");
    if (read) {
        const std::array<float, 3> background = colour_of(kBackground);
        const std::array<float, 3> shape = inside_colour();
        const std::array<float, 3> mapped_background = vk::tone_map_reference(parameters, nullptr, background);
        const std::array<float, 3> mapped_shape = vk::tone_map_reference(parameters, nullptr, shape);
        float brightest = 0.0F;
        int edges = 0;
        int wrong = 0;
        int bad_coverage = 0;
        int byte_wrong = 0;
        for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(kWidth) * kHeight; ++pixel) {
            const float red = from_half(half_at(was, {.pixel = pixel, .channel = 0}));
            brightest = std::max({brightest, red});
            const float covered =
                (red - core::at(background, 0)) / (core::at(shape, 0) - core::at(background, 0)) * kFourSamples;
            const float samples = std::round(covered);
            if (std::abs(covered - samples) > kCoverageSlack || samples < 0.0F || samples > kFourSamples) {
                ++bad_coverage;
                continue;
            }
            const float part = samples / kFourSamples;
            edges += samples > 0.0F && samples < kFourSamples ? 1 : 0;
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                const float expected = (core::at(mapped_shape, channel) * part) + (core::at(mapped_background, channel) * (1.0F - part));
                const float got = from_half(half_at(now, {.pixel = pixel, .channel = channel}));
                if (std::abs(got - expected) > kStep + kSlack) {
                    ++wrong;
                }
                // the 8-bit copy (BGRA): the float resolve rounded
                const auto byte = static_cast<int>(core::at(eight, (pixel * kBytePixelBytes) + (2 - channel)));
                if (std::abs(byte - static_cast<int>(std::lround(std::clamp(got, 0.0F, 1.0F) * kByteSteps))) > 1) {
                    ++byte_wrong;
                }
            }
        }
        tally->check(brightest > kAboveOne, text::format("the float resolve keeps values above 1 (brightest red %.3f)",
                                                     static_cast<double>(brightest)));
        tally->check(bad_coverage == 0, text::format("every pixel's coverage read from the resolve (%d not)", bad_coverage));
        tally->check(wrong == 0 && edges > kLeastEdges,
                     text::format("each pixel the mean of its mapped samples, %d edge pixels among them (%d off)", edges, wrong));
        tally->check(byte_wrong == 0, text::format("the 8-bit copy within a step of the float resolve (%d off)", byte_wrong));
    }
    for (GpuSurface* surface : {msaa, before, after, bytes}) {
        gpu.release_surface(surface);
    }
}

// --- the back buffer ------------------------------------------------------------------------------------------------

void check_back_buffer(render::Gpu& gpu, Tally* tally)
{
    text::print("the back buffer:\n");
    const vk::HdrSettings settings{.on = true, .exposure = 1.0F};
    vk::set_hdr_settings(settings);
    vk::set_hdr_lut({});
    GpuSurface* back = nullptr;
    render::GpuSurfaceDesc desc{};
    const bool have = gpu.back_buffer(&back) == value::kResultOk && back != nullptr &&
                      gpu.surface_desc(back, &desc) == value::kResultOk;
    tally->check(have && desc.format == value::kFormatX8R8G8B8,
                 "its format reported as the settings' X8R8G8B8");
    {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        tally->check(state.back_buffer != nullptr && state.back_buffer->image.format == vk::kHdrFormat,
                     "its image 16-bit float");
    }
    if (!have) {
        return;
    }
    gpu.begin_scene();
    gpu.set_render_target(0, back);
    gpu.set_depth_surface(nullptr);
    gpu.set_viewport({.x = 0, .y = 0, .width = kWidth, .height = kHeight, .min_z = 0.0F, .max_z = 1.0F});
    draw_triangles(gpu);
    const uint64_t passes = vk::tone_map_passes();
    render::rt::world_view_drawn(view_of({0, 0, kWidth, kHeight}));
    tally->check(vk::tone_map_passes() == passes + 1, "the pass recorded on the back buffer");
    gpu.end_scene();
    // the screenshot's read: the back buffer into a system-memory surface
    GpuSurface* system = nullptr;
    std::vector<uint8_t> shot;
    if (gpu.create_offscreen_surface(kWidth, kHeight, desc.format, value::kPoolSystemMemory, &system) == value::kResultOk &&
        gpu.read_render_target(back, system) == value::kResultOk) {
        render::GpuLockedRect locked{};
        if (gpu.lock_surface(system, &locked, nullptr, value::kLockReadOnly) == value::kResultOk) {
            const std::span<const uint8_t> from(static_cast<const uint8_t*>(locked.bits),
                                                static_cast<std::size_t>(kWidth) * kHeight * kBytePixelBytes);
            shot.assign(from.begin(), from.end());
            gpu.unlock_surface(system);
        }
    }
    tally->check(!shot.empty(), "the screenshot's read of the back buffer");
    const bool presented = gpu.present() == value::kResultOk;
    tally->check(presented, "presented (blitted from float into the swapchain)");
    std::vector<uint8_t> front;
    GpuSurface* screen = nullptr;
    render::GpuSize display{};
    if (gpu.display_size(&display) == value::kResultOk &&
        gpu.create_offscreen_surface(display.width, display.height, value::kFormatA8R8G8B8, value::kPoolScratch, &screen) ==
            value::kResultOk &&
        gpu.read_front_buffer(screen) == value::kResultOk) {
        render::GpuLockedRect locked{};
        if (gpu.lock_surface(screen, &locked, nullptr, value::kLockReadOnly) == value::kResultOk) {
            const auto pitch = static_cast<std::size_t>(locked.pitch);
            const std::span<const uint8_t> whole(static_cast<const uint8_t*>(locked.bits),
                                                 pitch * static_cast<std::size_t>(display.height));
            for (uint32_t row = 0; row < kHeight; ++row) {
                const std::span<const uint8_t> line =
                    whole.subspan(static_cast<std::size_t>(row) * pitch, static_cast<std::size_t>(kWidth) * kBytePixelBytes);
                front.insert(front.end(), line.begin(), line.end());
            }
            gpu.unlock_surface(screen);
        }
    }
    tally->check(!front.empty(), "read_front_buffer");
    if (!shot.empty()) {
        const vk::ToneParameters parameters = vk::tone_parameters(settings);
        const std::array<float, 3> inside = vk::tone_map_reference(parameters, nullptr, inside_colour());
        const std::array<float, 3> outside = vk::tone_map_reference(parameters, nullptr, colour_of(kBackground));
        // the triangle's middle and a corner of the background
        constexpr std::size_t kInsidePixel = (60 * kWidth) + 110;
        constexpr std::size_t kOutsidePixel = (120 * kWidth) + 250;
        int wrong = 0;
        for (const auto& [pixel, wanted] : {std::pair{kInsidePixel, inside}, std::pair{kOutsidePixel, outside}}) {
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                const int expected = static_cast<int>(std::lround(core::at(wanted, channel) * kByteSteps));
                const int got = core::at(shot, (pixel * kBytePixelBytes) + (2 - channel));
                wrong += std::abs(got - expected) > 1 ? 1 : 0;
                if (!front.empty()) {
                    wrong += std::abs(static_cast<int>(core::at(front, (pixel * kBytePixelBytes) + (2 - channel))) - expected) > 1 ? 1 : 0;
                }
            }
        }
        tally->check(wrong == 0, "the shot and the front buffer show the mapped colours as bytes (inside and outside "
                                 "the triangle, within a step)");
    }
    for (GpuSurface* surface : {system, screen, back}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
}

// --- the cost ------------------------------------------------------------------------------------------------------

// The pass's time on the device over a 1920 x 1080 float target (one sample, then four), from timestamps around
// kTimedPasses passes: printed, not checked.
void measure_cost(render::Gpu& gpu)
{
    constexpr uint32_t kTimedWidth = 1920;
    constexpr uint32_t kTimedHeight = 1080;
    constexpr int kTimedPasses = 20;
    constexpr double kNanosecondsPerMillisecond = 1.0e6;
    vk::set_hdr_settings({.on = true});
    vk::set_hdr_lut({});
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
    for (const uint32_t samples : {0U, kFourSamples}) {
        GpuSurface* target = nullptr;
        if (gpu.create_render_target({.width = kTimedWidth,
                                      .height = kTimedHeight,
                                      .format = value::kFormatA16B16G16R16F,
                                      .samples = samples,
                                      .quality = 0,
                                      .lockable_or_discard = false},
                                     &target) != value::kResultOk) {
            continue;
        }
        gpu.begin_scene();
        gpu.set_render_target(0, target);
        gpu.set_depth_surface(nullptr);
        gpu.clear(0, nullptr, value::kClearTarget, kBackground, 1.0F, 0);
        render::rt::world_view_drawn(view_of({0, 0, kTimedWidth, kTimedHeight})); // (the pipeline and copy made first)
        {
            const std::scoped_lock lock(state.mutex);
            VkCommandBuffer commands = vk::outside_pass(state);
            vkCmdResetQueryPool(commands, pool, 0, 2);
            vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool, 0);
        }
        for (int i = 0; i < kTimedPasses; ++i) {
            render::rt::world_view_drawn(view_of({0, 0, kTimedWidth, kTimedHeight}));
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
        gpu.end_scene();
        gpu.release_surface(target);
        const double each = static_cast<double>(stamps.back() - stamps.front()) * static_cast<double>(properties.limits.timestampPeriod) /
                            kNanosecondsPerMillisecond / kTimedPasses;
        text::print("the cost: %.3f ms a pass at %ux%u, %u sample%s (the copy and the curve)\n", each, kTimedWidth,
                    kTimedHeight, samples == 0 ? 1U : samples, samples == 0 ? "" : "s");
    }
    vkDestroyQueryPool(state.context.device(), pool, nullptr);
}

void check_cube_files(Tally* tally)
{
    text::print(".cube files:\n");
    vk::GradeLut lut;
    std::string error;
    constexpr std::size_t kCorners = 8;
    tally->check(vk::parse_cube("LUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n", &lut, &error) &&
                     lut.size == 2 && lut.entries.size() == kCorners,
                 "a 2^3 identity read");
    const vk::ToneParameters clip = vk::tone_parameters({.on = true, .white_point = 1.0F});
    constexpr std::array<float, 3> kSome{0.25F, 0.5F, 0.75F};
    constexpr float kExact = 1e-6F;
    const std::array<float, 3> graded = vk::tone_map_reference(clip, &lut, kSome);
    bool same = true;
    for (std::size_t channel = 0; channel < kSome.size(); ++channel) {
        same = same && std::abs(core::at(graded, channel) - core::at(kSome, channel)) < kExact;
    }
    tally->check(same, "the identity table changes nothing");
    const std::array<std::pair<const char*, const char*>, 3> refused{{
        {"a 1D table", "LUT_1D_SIZE 2\n0 0 0\n1 1 1\n"},
        {"a short table", "LUT_3D_SIZE 2\n0 0 0\n1 1 1\n"},
        {"no size", "0 0 0\n"},
    }};
    for (const auto& [name, cube] : refused) {
        error.clear();
        const bool read = vk::parse_cube(cube, &lut, &error);
        tally->check(!read && !error.empty(), text::format("%s refused: %s", name, error.c_str()));
    }
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
    vk::set_context_options({.validation = options.validation, .device_index = options.device, .on_message = print_line},
                            window);
    // (on before the device: its back buffer made in floats)
    vk::set_hdr_settings({.on = true});
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
    tally.check(render::rt::hdr_colour(), "rt::hdr_colour() set by the device");
    render::Gpu& gpu = vk::gpu();
    const std::vector<uint16_t> values = input_values();
    const vk::GradeLut lut = test_lut();
    const vk::HdrSettings defaults{.on = true};
    vk::HdrSettings clip = defaults;
    clip.white_point = 1.0F;
    vk::HdrSettings brighter = defaults;
    constexpr float kBrighter = 2.0F;
    constexpr float kDarker = -1.5F;
    constexpr float kLessSaturated = 0.5F;
    constexpr float kMoreContrast = 1.3F;
    constexpr float kWideWhite = 4.0F;
    constexpr float kHalfStop = 0.5F;
    brighter.exposure = kBrighter;
    vk::HdrSettings darker = defaults;
    darker.exposure = kDarker;
    vk::HdrSettings aces = defaults;
    aces.tonemap = 1;
    vk::HdrSettings graded = defaults;
    graded.saturation = kLessSaturated;
    graded.contrast = kMoreContrast;
    vk::HdrSettings wide = defaults;
    wide.white_point = kWideWhite;
    vk::HdrSettings lut_case = defaults;
    lut_case.exposure = kHalfStop;
    const std::array<CurveCase, 8> curves{{
        {.name = "the defaults (white point 1.25)", .settings = defaults},
        {.name = "white point 1: a clip", .settings = clip},
        {.name = "exposure +2", .settings = brighter},
        {.name = "exposure -1.5", .settings = darker},
        {.name = "ACES", .settings = aces},
        {.name = "saturation 0.5, contrast 1.3", .settings = graded},
        {.name = "white point 4", .settings = wide},
        {.name = "a grading table (17^3), exposure +0.5", .settings = lut_case, .lut = true},
    }};
    for (const CurveCase& test : curves) {
        check_curve(gpu, test, lut, values, &tally);
    }
    check_identity(gpu, 1.0F, values, &tally);
    check_identity(gpu, vk::kHdrWhitePoint, values, &tally);
    check_skips(gpu, values, &tally);
    check_multisampled(gpu, &tally);
    check_back_buffer(gpu, &tally);
    check_cube_files(&tally);
    measure_cost(gpu);
    vk::set_hdr_settings({});
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
    text::print("thug2_vulkan_hdr_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
        SDL_CreateWindow("thug2 Vulkan HDR check", kWindowWidth, kWindowHeight, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
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
