// NOT ORIGINAL - thug2_vulkan_style_check.exe: the art style's toon filter (gpu_vulkan/vulkan_style.h) tried on this
// machine against its twin on the processor (vulkan_style.h style_reference).
//
// A synthetic view: a 256 x 128 target over a depth surface drawn with screen-space quads at known depths - a box (a
// silhouette against the sky and the floor), a tilted floor (a plane: no lines inside it), a ridge (two tilted halves:
// a crease down its middle) and the sky (nothing drawn: no lines) - and colours filled through a lock: noisy fields, a
// grey gradient, not-a-number and infinities. Cases:
//   - the filter alone (smoothing alone, bands alone), at its defaults and with other knobs: the Kuwahara pass's
//     half-resolution output (read back) within a step of the twin's; every channel of the target within kTolerance of
//     the reference fed the pass's own linear depth and smoothed colour, and at most 2% of the pixels past it against the
//     twin alone (the smoothing's 16-bit rounding crossing a band's soft edge); every one finite and within 0 - 1, alpha
//     untouched; the linear depth as the projection says (the box's distance, the sky marked);
//   - what it does: the smoothing calms the noise (the noisy field's spread a fraction of the input's), the bands make
//     plateaus along the gradient, the outlines lie along the box's edges (on the box, not the sky beside it) and down
//     the ridge, never inside the floor or the sky;
//   - a viewport: only its rectangle changed, everything outside it bit for bit as it was (the 2D's place);
//   - off, strength 0: no pass, nothing changed;
//   - an 8-bit target; four samples (the colour resolved, written to every sample);
//   - the cost at 1920x1080 and 2560x1440 (printed).
//   thug2_vulkan_style_check [--device N] [--display N] [--no-validation]
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
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_style.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuRenderState;
using render::GpuStageState;
using render::GpuSurface;
namespace value = render::gpu_value;
namespace vk = render::vulkan;
using vk::StyleRgb;

constexpr int kWindowWidth = 256;
constexpr int kWindowHeight = 128;
constexpr uint32_t kWidth = 256;
constexpr uint32_t kHeight = 128;
constexpr int kBitsPerByte = 8;
constexpr std::size_t kHalfPixelBytes = 8;
constexpr std::size_t kBytePixelBytes = 4;
constexpr std::size_t kChannels = 3;
constexpr float kByteSteps = 255.0F;
constexpr float kTolerance = 2.5F / 255.0F; // (16-bit float images between the passes, the device's pow and exp)
constexpr int kMostOff = 3;                 // pixels allowed past the tolerance (a smoothstep's edge rounded differently)
constexpr uint16_t kHalfAlpha = 0x3800;
constexpr uint32_t kHighByte = 8;
constexpr uint32_t kByteMask = 0xFF;
constexpr uint16_t kHalfSign = 0x8000;
constexpr uint32_t kFvf = 0x044; // XYZRHW | DIFFUSE
constexpr uint32_t kSeed = 2024U;
constexpr uint32_t kFourSamples = 4;
constexpr float kHalf = 0.5F;
constexpr float kTwice = 2.0F;
constexpr uint8_t kOpaqueByte = 0xFF;

// The view's projection (Direct3D's left-handed one, row vectors): near 1, far 10000, a vertical scale of 1.5.
constexpr float kNear = 1.0F;
constexpr float kFar = 10000.0F;
constexpr float kVerticalScale = 1.5F;
// The scene's depths (world units along the view).
constexpr float kBoxZ = 200.0F;
constexpr float kFloorNear = 150.0F; // (the floor's bottom row)
constexpr float kFloorFar = 300.0F;  // (its top row)
constexpr float kRidgeEdge = 400.0F; // (the ridge's sides)
constexpr float kRidgeTop = 250.0F;  // (its middle)
// The scene's shapes (pixels; the quarters keep pixel centres off their edges).
constexpr float kBoxLeft = 40.25F;
constexpr float kBoxRight = 120.25F;
constexpr float kBoxTop = 20.25F;
constexpr float kBoxBottom = 90.25F;
constexpr float kFloorTop = 96.25F;
constexpr float kRidgeLeft = 140.25F;
constexpr float kRidgeMiddle = 195.25F;
constexpr float kRidgeRight = 250.25F;
constexpr float kRidgeTopEdge = 10.25F;
constexpr float kRidgeBottom = 80.25F;
constexpr float kOffscreen = 300.0F;

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

// A value 0 - 1 as a 16-bit float (the nearest; the inputs are made of these).
uint16_t to_half(float number)
{
    constexpr uint32_t kMantissaUnit = 0x400;
    constexpr int kExponentBias = 15;
    constexpr uint32_t kMantissaBits = 10;
    if (!(number > 0.0F)) {
        return 0;
    }
    int exponent = 0;
    const float fraction = std::frexp(number, &exponent); // number = fraction x 2^exponent, fraction 0.5 - 1
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

// Where a channel of a pixel is in a float target's bytes.
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

// --- the synthetic scene ------------------------------------------------------------------------------------------

// A rectangle of pixels: columns left - right, rows top - bottom (the right and bottom ones not in it).
struct Box {
    uint32_t left = 0;
    uint32_t top = 0;
    uint32_t right = 0;
    uint32_t bottom = 0;
};
struct Spot {
    uint32_t column = 0;
    uint32_t row = 0;
};
constexpr vk::Extent kSize{.width = kWidth, .height = kHeight};
constexpr vk::Extent kHalfSize{.width = (kWidth + 1) / 2, .height = (kHeight + 1) / 2};
// Where the checks look (the shapes below; either pixel-centre convention puts a shape's edge pixels inside these).
constexpr Box kNoisy{.left = 56, .top = 36, .right = 104, .bottom = 76};          // inside the box, away from its edges
constexpr Box kBoxTopEdge{.left = 44, .top = 20, .right = 117, .bottom = 23};     // along the box's top edge
constexpr Box kBoxLeftEdge{.left = 40, .top = 24, .right = 43, .bottom = 88};     // its left
constexpr Box kSkyAboveBox{.left = 44, .top = 14, .right = 117, .bottom = 20};    // the sky just above it
constexpr Box kSkyLeftOfBox{.left = 34, .top = 24, .right = 40, .bottom = 88};    // and left of it
constexpr Box kRidgeCrease{.left = 192, .top = 20, .right = 199, .bottom = 70};   // the ridge's middle
constexpr Box kRidgeLeftSide{.left = 150, .top = 20, .right = 185, .bottom = 70};
constexpr Box kRidgeRightSide{.left = 205, .top = 20, .right = 240, .bottom = 70};
constexpr Box kFloorLeft{.left = 10, .top = 100, .right = 30, .bottom = 124};
constexpr Box kFloorRight{.left = 130, .top = 100, .right = 250, .bottom = 124};
constexpr Box kSkyBetween{.left = 125, .top = 2, .right = 135, .bottom = 90};     // between the box and the ridge
constexpr Spot kBoxProbe{.column = 80, .row = 55};
constexpr Spot kSkyProbe{.column = 10, .row = 10};
constexpr float kSameShade = 1.5F / 255.0F;

// The projection's depth value at a distance along the view.
float depth_of(float distance)
{
    const float p22 = kFar / (kFar - kNear);
    const float p32 = -kNear * kFar / (kFar - kNear);
    return p22 + (p32 / distance);
}

render::rt::ViewDrawn view_of(std::array<uint32_t, 4> rect)
{
    render::rt::ViewDrawn view;
    view.rect = rect;
    view.depth_range = {0.0F, 1.0F};
    auto& projection = view.camera.projection;
    const float aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
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

struct ScreenVertex {
    float x, y, z, rhw;
    uint32_t colour;
};

// A quad's two triangles (corners clockwise from the top left), each corner's depth value given.
void add_quad(std::vector<ScreenVertex>* out, std::array<float, 4> xs, std::array<float, 4> ys, std::array<float, 4> zs,
              uint32_t colour)
{
    const auto corner = [&](std::size_t which) {
        return ScreenVertex{
            .x = core::at(xs, which), .y = core::at(ys, which), .z = core::at(zs, which), .rhw = 1.0F, .colour = colour};
    };
    for (const std::size_t which : {0U, 1U, 2U, 0U, 2U, 3U}) {
        out->push_back(corner(which));
    }
}

std::vector<ScreenVertex> scene_quads()
{
    std::vector<ScreenVertex> quads;
    constexpr uint32_t kBoxColour = 0xFFC04030;
    constexpr uint32_t kFloorColour = 0xFF607080;
    constexpr uint32_t kRidgeColour = 0xFF40A050;
    const float box = depth_of(kBoxZ);
    // the floor: its depth affine on the screen from the far value at its top to the near one at the bottom - a plane
    const float floor_top = depth_of(kFloorFar);
    const float floor_bottom = depth_of(kFloorNear);
    add_quad(&quads, {0.0F, kOffscreen, kOffscreen, 0.0F}, {kFloorTop, kFloorTop, static_cast<float>(kHeight), static_cast<float>(kHeight)},
             {floor_top, floor_top, floor_bottom, floor_bottom}, kFloorColour);
    add_quad(&quads, {kBoxLeft, kBoxRight, kBoxRight, kBoxLeft}, {kBoxTop, kBoxTop, kBoxBottom, kBoxBottom}, {box, box, box, box},
             kBoxColour);
    // the ridge: two halves meeting nearest at its middle
    const float edge = depth_of(kRidgeEdge);
    const float top = depth_of(kRidgeTop);
    add_quad(&quads, {kRidgeLeft, kRidgeMiddle, kRidgeMiddle, kRidgeLeft}, {kRidgeTopEdge, kRidgeTopEdge, kRidgeBottom, kRidgeBottom},
             {edge, top, top, edge}, kRidgeColour);
    add_quad(&quads, {kRidgeMiddle, kRidgeRight, kRidgeRight, kRidgeMiddle}, {kRidgeTopEdge, kRidgeTopEdge, kRidgeBottom, kRidgeBottom},
             {top, edge, edge, top}, kRidgeColour);
    return quads;
}

// The scene's depth (and a colour of its own) drawn into the bound targets, the depth cleared to the sky first.
void draw_scene(render::Gpu& gpu, uint32_t width, uint32_t height)
{
    constexpr uint32_t kSkyColour = 0xFF8090C0;
    gpu.set_viewport({.x = 0, .y = 0, .width = width, .height = height, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth, kSkyColour, 1.0F, 0);
    gpu.set_render_state(GpuRenderState::kZEnable, 1);
    gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
    gpu.set_render_state(GpuRenderState::kZFunc, value::kCompareLessEqual);
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
    std::vector<ScreenVertex> quads = scene_quads();
    if (width != kWidth || height != kHeight) {
        const float sx = static_cast<float>(width) / static_cast<float>(kWidth);
        const float sy = static_cast<float>(height) / static_cast<float>(kHeight);
        for (ScreenVertex& vertex : quads) {
            vertex.x *= sx;
            vertex.y *= sy;
        }
    }
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(quads.size() / 3), quads.data(),
                         sizeof(ScreenVertex));
}

// The colours: noisy fields per region (box, ridge, floor, sky), a grey gradient band across the floor, a few bad
// values.
struct Picture {
    std::vector<StyleRgb> colour; // (as the target holds them)
    std::vector<uint16_t> bits;   // (float targets: the halves written, red green blue per pixel)
};

constexpr uint32_t kGradientTop = 104; // the gradient's rows
constexpr uint32_t kGradientBottom = 112;
// The regions' colours (noise added): the sky, the floor, the ridge, the box.
constexpr StyleRgb kSkyBase{0.5F, 0.6F, 0.85F};
constexpr StyleRgb kFloorBase{0.35F, 0.4F, 0.45F};
constexpr StyleRgb kRidgeBase{0.3F, 0.65F, 0.35F};
constexpr StyleRgb kBoxBase{0.75F, 0.3F, 0.2F};
constexpr float kNoise = 0.15F;        // the noise shared by a pixel's channels: up to this either way
constexpr float kChannelNoise = 0.3F;  // each channel's own, as a share of it
constexpr float kNoiseSteps = 65535.0F; // (16 bits of the generator)

Picture make_picture()
{
    Picture picture;
    picture.colour.resize(static_cast<std::size_t>(kWidth) * kHeight);
    picture.bits.resize(picture.colour.size() * kChannels);
    uint32_t random = kSeed;
    constexpr uint32_t kMultiplier = 1664525U;
    constexpr uint32_t kIncrement = 1013904223U;
    constexpr uint32_t kNoiseBits = 16;
    const auto noise = [&] { // (-kNoise - kNoise)
        random = (random * kMultiplier) + kIncrement;
        return ((static_cast<float>(random >> kNoiseBits) / kNoiseSteps) - kHalf) * kTwice * kNoise;
    };
    for (uint32_t row = 0; row < kHeight; ++row) {
        for (uint32_t column = 0; column < kWidth; ++column) {
            const auto across = static_cast<float>(column);
            const auto down = static_cast<float>(row);
            StyleRgb base = kSkyBase;
            if (down > kFloorTop) {
                base = kFloorBase;
            }
            if (across > kRidgeLeft && across < kRidgeRight && down > kRidgeTopEdge && down < kRidgeBottom) {
                base = kRidgeBase;
            }
            if (across > kBoxLeft && across < kBoxRight && down > kBoxTop && down < kBoxBottom) {
                base = kBoxBase;
            }
            StyleRgb colour{};
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
    // bad values: not a number, both infinities, a negative (in the sky's corner)
    constexpr std::array<uint16_t, 4> kBad{0x7E00, 0x7C00, 0xFC00, 0xBC00};
    constexpr uint32_t kBadCorner = 4;
    for (std::size_t bad = 0; bad < kBad.size(); ++bad) {
        const std::size_t pixel = pixel_of(static_cast<uint32_t>(kBadCorner + bad), kBadCorner);
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            core::at(picture.bits, (pixel * kChannels) + channel) = core::at(kBad, bad);
            core::at(core::at(picture.colour, pixel), channel) = from_half(core::at(kBad, bad));
        }
    }
    return picture;
}

// The picture as 8-bit colour (B, G, R, A).
std::vector<uint8_t> picture_bytes(const Picture& picture, std::vector<StyleRgb>* as_read)
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

// The scene's depth drawn, then the colour target filled with the picture.
bool prepare(render::Gpu& gpu, const Targets& targets, const Picture& picture, std::vector<StyleRgb>* input)
{
    gpu.begin_scene();
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    draw_scene(gpu, kWidth, kHeight);
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

// The pass run on the targets over `rect` through the listener (inside a scene, as the game's frames); passes recorded.
uint64_t run_pass(render::Gpu& gpu, const Targets& targets, std::array<uint32_t, 4> rect)
{
    gpu.begin_scene();
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    const uint64_t before = vk::style_passes();
    render::rt::world_view_drawn(view_of(rect));
    const uint64_t passes = vk::style_passes() - before;
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    gpu.end_scene();
    return passes;
}

// The target's colour read back (as floats).
bool read_colour(GpuSurface* target, bool floats, std::vector<StyleRgb>* colour, std::vector<float>* alpha)
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

// The pass's linear depth read back (the target's size).
bool read_linear(std::vector<float>* depth)
{
    vk::Backend& state = vk::backend();
    const std::scoped_lock lock(state.mutex);
    vk::finish_work(state);
    vk::Extent size;
    return vk::read_style_depth(state, depth, &size) && size.width == kWidth && size.height == kHeight;
}

// The pass's smoothed half-resolution colour read back (the target's half size).
bool read_smoothed(std::vector<StyleRgb>* smoothed)
{
    vk::Backend& state = vk::backend();
    const std::scoped_lock lock(state.mutex);
    vk::finish_work(state);
    std::vector<uint16_t> halves;
    vk::Extent size;
    if (!vk::read_style_smoothed(state, &halves, &size) || size.width != kHalfSize.width || size.height != kHalfSize.height) {
        return false;
    }
    smoothed->resize(static_cast<std::size_t>(size.width) * size.height);
    for (std::size_t texel = 0; texel < smoothed->size(); ++texel) {
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            core::at(core::at(*smoothed, texel), channel) = from_half(core::at(halves, (texel * 4) + channel));
        }
    }
    return true;
}

// --- the comparisons ----------------------------------------------------------------------------------------------

struct Compared {
    float worst = 0.0F;
    int off = 0;
    int outside = 0;
};

Compared compare(const std::vector<StyleRgb>& got, const std::vector<StyleRgb>& expected, float tolerance)
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

// The spread (standard deviation, red) over a rectangle.
float spread(const std::vector<StyleRgb>& colour, const Box& box)
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

// Where the outlines are: the pixels the reference changes when outlines are on (against off).
std::vector<bool> outline_map(const vk::StyleParameters& parameters, const std::vector<StyleRgb>& input,
                              const std::vector<float>& depth)
{
    vk::StyleParameters without = parameters;
    without.outlines = 0.0F;
    const std::vector<StyleRgb> lined = vk::style_reference(parameters, {.colour = &input, .depth = &depth}, kSize);
    const std::vector<StyleRgb> plain = vk::style_reference(without, {.colour = &input, .depth = &depth}, kSize);
    std::vector<bool> map(input.size());
    constexpr float kVisible = 0.05F;
    for (std::size_t pixel = 0; pixel < map.size(); ++pixel) {
        float difference = 0.0F;
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            difference = std::max(difference, std::abs(core::at(core::at(lined, pixel), channel) -
                                                       core::at(core::at(plain, pixel), channel)));
        }
        map.at(pixel) = difference > kVisible;
    }
    return map;
}

int count_in(const std::vector<bool>& map, const Box& box)
{
    int count = 0;
    for (uint32_t row = box.top; row < box.bottom; ++row) {
        for (uint32_t column = box.left; column < box.right; ++column) {
            count += map.at(pixel_of(column, row)) ? 1 : 0;
        }
    }
    return count;
}

// --- the cases ----------------------------------------------------------------------------------------------------

struct FilterCase {
    const char* name = "";
    vk::StyleSettings settings;
    bool floats = true;
    bool semantics = false; // the smoothing, bands and outlines checked where they belong
};

void check_filter(render::Gpu& gpu, const FilterCase& test, const Picture& picture, Tally* tally)
{
    text::print("%s:\n", test.name);
    vk::set_style_settings(test.settings);
    const Targets targets = make_targets(gpu, test.floats, 0);
    std::vector<StyleRgb> input;
    const bool ready = targets.colour != nullptr && targets.depth != nullptr && prepare(gpu, targets, picture, &input);
    tally->check(ready, "the targets made, the depth drawn, the colour filled");
    if (!ready) {
        release(gpu, targets);
        return;
    }
    tally->check(run_pass(gpu, targets, {0, 0, kWidth, kHeight}) == 1, "the listener recorded the pass");
    std::vector<StyleRgb> out;
    std::vector<float> alpha;
    std::vector<float> depth;
    std::vector<StyleRgb> smoothed;
    const vk::StyleParameters parameters =
        vk::style_parameters(test.settings, view_of({0, 0, kWidth, kHeight}), {.output = kHeight, .depth = kHeight});
    const bool read = read_colour(targets.colour, test.floats, &out, &alpha) && read_linear(&depth) &&
                      (parameters.radius == 0 || read_smoothed(&smoothed));
    tally->check(read, "the target, the linear depth and the smoothed colour read back");
    release(gpu, targets);
    if (!read) {
        return;
    }
    tally->check(vk::style_outlined() == (test.settings.outlines > 0.0F), "outlines drawn as asked (a samplable depth)");
    // the linear depth as the projection says
    constexpr float kExactly = 1.0e-3F; // (the box: a plane facing the camera - D24's steps)
    constexpr float kRoughly = 0.05F;   // (the floor's bottom row: half a pixel up its slope)
    const float box = core::at(depth, pixel_of(kBoxProbe.column, kBoxProbe.row));
    const float sky = core::at(depth, pixel_of(kSkyProbe.column, kSkyProbe.row));
    const float floor_row = core::at(depth, pixel_of(kSkyProbe.column, kHeight - 1));
    tally->check(std::abs(box - kBoxZ) < kBoxZ * kExactly && sky == vk::kStyleSky &&
                     std::abs(floor_row - kFloorNear) < kFloorNear * kRoughly,
                 text::format("the linear depth: box %.2f (%.0f), sky marked, floor's bottom row %.1f (~%.0f)",
                              static_cast<double>(box), static_cast<double>(kBoxZ), static_cast<double>(floor_row),
                              static_cast<double>(kFloorNear)));
    const float tolerance = test.floats ? kTolerance : kTolerance + (1.0F / kByteSteps);
    if (parameters.radius > 0) {
        // the smoothing (half resolution, radius r) against the twin's: within the 16-bit floats' rounding
        const std::vector<StyleRgb> twin = vk::style_kuwahara(vk::style_half(input, kSize), kHalfSize, parameters.radius);
        float worst = 0.0F;
        for (std::size_t texel = 0; texel < twin.size(); ++texel) {
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                worst = std::max(worst, std::abs(core::at(core::at(smoothed, texel), channel) -
                                                 core::at(core::at(twin, texel), channel)));
            }
        }
        // (a step of 8-bit colour: the 16-bit floats' rounding in the images through the weights' 4th power)
        constexpr float kSmoothSlack = 1.0F / kByteSteps;
        tally->check(worst <= kSmoothSlack,
                     text::format("the Kuwahara filter (radius %d at half resolution) as the twin's (largest difference "
                                  "%.5f)",
                                  parameters.radius, static_cast<double>(worst)));
    }
    // the composite: the twin given the pass's own smoothed colour and linear depth
    const std::vector<StyleRgb> expected =
        vk::style_reference(parameters,
                            {.colour = &input, .depth = &depth, .smoothed = parameters.radius > 0 ? &smoothed : nullptr},
                            kSize);
    const Compared compared = compare(out, expected, tolerance);
    tally->check(compared.off <= kMostOff,
                 text::format("every pixel within %.1f steps of the reference (%d past it; the largest difference %.2f steps)",
                              static_cast<double>(tolerance * kByteSteps), compared.off,
                              static_cast<double>(compared.worst * kByteSteps)));
    tally->check(compared.outside == 0, text::format("every channel finite and within 0 - 1 (%d not)", compared.outside));
    if (parameters.radius > 0) {
        // the whole filter against the twin alone: the smoothing's last-bit differences can cross a band's soft edge
        // (a step there is ~12 times steeper) - a few pixels, never many
        const Compared whole =
            compare(out, vk::style_reference(parameters, {.colour = &input, .depth = &depth}, kSize), tolerance);
        constexpr int kMostCrossings = static_cast<int>(kWidth * kHeight / 50); // (2% of the pixels)
        tally->check(whole.off <= kMostCrossings,
                     text::format("the whole filter against the twin alone: %d pixels past %.1f steps (the largest "
                                  "difference %.2f steps)",
                                  whole.off, static_cast<double>(tolerance * kByteSteps),
                                  static_cast<double>(whole.worst * kByteSteps)));
    }
    int alpha_changed = 0;
    constexpr float kHalfAlphaValue = 0.5F;
    constexpr float kAlphaSlack = 1.0e-3F;
    for (const float value : alpha) {
        alpha_changed += std::abs(value - (test.floats ? kHalfAlphaValue : 1.0F)) > kAlphaSlack ? 1 : 0;
    }
    tally->check(alpha_changed == 0, text::format("alpha untouched (%d changed)", alpha_changed));
    if (!test.semantics) {
        return;
    }
    // the smoothing: the box's noisy field calmed
    const float before = spread(input, kNoisy);
    const float after = spread(out, kNoisy);
    constexpr float kCalmer = 0.4F;
    tally->check(after < before * kCalmer, text::format("the smoothing calms the noise (spread %.4f -> %.4f)",
                                                        static_cast<double>(before), static_cast<double>(after)));
    // the bands: plateaus along the gradient (most neighbours equal), and a few distinct shades
    int flat = 0;
    int pairs = 0;
    const uint32_t row = (kGradientTop + kGradientBottom) / 2;
    for (uint32_t column = 1; column < kWidth; ++column) {
        const float left = core::at(core::at(out, pixel_of(column - 1, row)), 1);
        const float right = core::at(core::at(out, pixel_of(column, row)), 1);
        flat += std::abs(left - right) < kSameShade ? 1 : 0;
        ++pairs;
    }
    constexpr float kMostlyFlat = 0.6F;
    tally->check(static_cast<float>(flat) > kMostlyFlat * static_cast<float>(pairs),
                 text::format("the bands: %d of %d neighbours along the gradient the same shade", flat, pairs));
    // the outlines: along the box's edges (on the box), down the ridge; none inside the floor or the sky
    const std::vector<bool> lines = outline_map(parameters, input, depth);
    const int box_top = count_in(lines, kBoxTopEdge);
    const int box_left = count_in(lines, kBoxLeftEdge);
    const int sky_above = count_in(lines, kSkyAboveBox);
    const int sky_left = count_in(lines, kSkyLeftOfBox);
    const int ridge = count_in(lines, kRidgeCrease);
    const int ridge_sides = count_in(lines, kRidgeLeftSide) + count_in(lines, kRidgeRightSide);
    const int floor_inside = count_in(lines, kFloorLeft) + count_in(lines, kFloorRight);
    const int sky_inside = count_in(lines, kSkyBetween);
    text::print("    outline pixels: box top %d, left %d; sky beside it %d, %d; ridge %d, its sides %d; floor %d, sky %d\n",
                box_top, box_left, sky_above, sky_left, ridge, ridge_sides, floor_inside, sky_inside);
    constexpr int kAlong = 40;
    tally->check(box_top > kAlong && box_left > kAlong, "the outline along the box's edges, on the box");
    tally->check(sky_above == 0 && sky_left == 0, "none on the sky beside it");
    tally->check(ridge > kAlong, "the crease down the ridge's middle");
    tally->check(ridge_sides == 0 && floor_inside == 0 && sky_inside == 0, "none inside a plane or the sky");
}

// Only the viewport changed; the setting off and strength 0 change nothing.
void check_skips(render::Gpu& gpu, const Picture& picture, Tally* tally)
{
    text::print("a viewport, the setting off, strength 0:\n");
    const auto unchanged_outside = [&](std::array<uint32_t, 4> rect, uint64_t* passes, int* inside_changed) {
        const Targets targets = make_targets(gpu, true, 0);
        std::vector<StyleRgb> input;
        int outside = -1;
        if (targets.colour != nullptr && targets.depth != nullptr && prepare(gpu, targets, picture, &input)) {
            *passes = run_pass(gpu, targets, rect);
            std::vector<uint8_t> bytes;
            if (vk::read_surface(targets.colour, &bytes)) {
                outside = 0;
                *inside_changed = 0;
                for (uint32_t row = 0; row < kHeight; ++row) {
                    for (uint32_t column = 0; column < kWidth; ++column) {
                        const std::size_t pixel = pixel_of(column, row);
                        bool changed = false;
                        for (std::size_t channel = 0; channel < kChannels; ++channel) {
                            changed = changed || half_at(bytes, {.pixel = pixel, .channel = channel}) != core::at(picture.bits, (pixel * kChannels) + channel);
                        }
                        const bool inside = column >= core::at(rect, 0) && column < core::at(rect, 0) + core::at(rect, 2) &&
                                            row >= core::at(rect, 1) && row < core::at(rect, 1) + core::at(rect, 3);
                        (inside ? *inside_changed : outside) += changed ? 1 : 0;
                    }
                }
            }
        }
        release(gpu, targets);
        return outside;
    };
    vk::set_style_settings({.style = 1});
    uint64_t passes = 0;
    int inside = 0;
    constexpr std::array<uint32_t, 4> kViewport{64, 16, 128, 96};
    const int outside = unchanged_outside(kViewport, &passes, &inside);
    tally->check(passes == 1 && outside == 0 && std::cmp_greater(inside, core::at(kViewport, 2) * core::at(kViewport, 3) / 2),
                 text::format("a 128 x 96 viewport: %d pixels changed inside, %d outside (bit for bit)", inside, outside));
    vk::set_style_settings({});
    passes = 1;
    const int off = unchanged_outside({0, 0, 0, 0}, &passes, &inside);
    tally->check(passes == 0 && off == 0, "rt_style=0: no pass, every pixel as it was");
    vk::set_style_settings({.style = 1, .strength = 0.0F});
    passes = 1;
    const int none = unchanged_outside({0, 0, 0, 0}, &passes, &inside);
    tally->check(passes == 0 && none == 0, "strength 0: no pass, every pixel as it was");
}

// Four samples: the colour (drawn: flat fields) resolved, filtered, written to every sample.
void check_multisampled(render::Gpu& gpu, Tally* tally)
{
    text::print("four samples:\n");
    vk::set_style_settings({.style = 1});
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
    if (!made) {
        release(gpu, targets);
        for (GpuSurface* surface : {before, after}) {
            if (surface != nullptr) {
                gpu.release_surface(surface);
            }
        }
        return;
    }
    gpu.begin_scene();
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    draw_scene(gpu, kWidth, kHeight);
    const bool resolved_before = gpu.copy_surface(targets.colour, nullptr, before, nullptr, value::kFilterNone) == value::kResultOk;
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    const uint64_t passes = vk::style_passes();
    render::rt::world_view_drawn(view_of({0, 0, kWidth, kHeight}));
    const bool filtered = vk::style_passes() == passes + 1;
    const bool resolved_after = gpu.copy_surface(targets.colour, nullptr, after, nullptr, value::kFilterNone) == value::kResultOk;
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    gpu.end_scene();
    tally->check(resolved_before && filtered && resolved_after, "drawn, resolved, filtered, resolved again");
    std::vector<StyleRgb> input;
    std::vector<StyleRgb> out;
    std::vector<float> alpha;
    std::vector<float> depth;
    const bool read = read_colour(before, true, &input, &alpha) && read_colour(after, true, &out, &alpha) && read_linear(&depth);
    tally->check(read, "the resolves and the linear depth read back");
    if (read) {
        const vk::StyleParameters parameters =
            vk::style_parameters(vk::StyleSettings{.style = 1}, view_of({0, 0, kWidth, kHeight}), {.output = kHeight, .depth = kHeight});
        const std::vector<StyleRgb> expected = vk::style_reference(parameters, {.colour = &input, .depth = &depth}, kSize);
        const Compared compared = compare(out, expected, kTolerance);
        tally->check(compared.off <= kMostOff && compared.outside == 0,
                     text::format("each pixel the filter of the resolved colour, written to every sample (%d past "
                                  "%.1f steps; %d outside 0 - 1)",
                                  compared.off, static_cast<double>(kTolerance * kByteSteps), compared.outside));
    }
    release(gpu, targets);
    gpu.release_surface(before);
    gpu.release_surface(after);
}

// The passes' time on the device over float targets of the game's sizes (the scene drawn, defaults): printed.
void measure_cost(render::Gpu& gpu, Tally* tally)
{
    constexpr int kTimedPasses = 20;
    constexpr double kNanosecondsPerMillisecond = 1.0e6;
    vk::set_style_settings({.style = 1});
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
    for (const auto& [width, height] : kSizes) {
        const Targets targets = make_targets(gpu, true, 0, width, height);
        if (targets.colour == nullptr || targets.depth == nullptr) {
            release(gpu, targets);
            continue;
        }
        gpu.begin_scene();
        gpu.set_render_target(0, targets.colour);
        gpu.set_depth_surface(targets.depth);
        draw_scene(gpu, width, height);
        const render::rt::ViewDrawn view = view_of({0, 0, width, height});
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
                            static_cast<double>(properties.limits.timestampPeriod) / kNanosecondsPerMillisecond / kTimedPasses;
        text::print("the cost: %.3f ms a pass at %ux%u (defaults: smoothing radius %d, outlines)\n", each, width, height,
                    vk::style_parameters({.style = 1}, view, {.output = height, .depth = height}).radius);
        tally->check(std::isfinite(each) && each > 0.0, text::format("timed at %ux%u", width, height));
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

// The cases' other settings.
constexpr float kStrongSmooth = 6.0F;
constexpr float kFewBands = 3.0F;
constexpr float kWideLines = 2.0F;
constexpr float kStrongColour = 1.5F;
constexpr float kSoftMix = 0.6F;
constexpr float kSoftLines = 0.5F;

int run(const Options& options, SDL_Window* window)
{
    if (!open_device(options, window)) {
        return vk::loader_version() == 0 ? 2 : 1;
    }
    Tally tally;
    render::Gpu& gpu = vk::gpu();
    const Picture picture = make_picture();
    const vk::StyleSettings defaults{.style = 1};
    const vk::StyleSettings strong{
        .style = 1, .strength = 1.0F, .smooth = kStrongSmooth, .bands = kFewBands, .outlines = 1.0F,
        .outline_width = kWideLines, .saturation = kStrongColour};
    const vk::StyleSettings soft{
        .style = 1, .strength = kSoftMix, .smooth = 0.0F, .bands = 0.0F, .outlines = kSoftLines,
        .outline_width = 1.0F, .saturation = vk::kStyleSaturation};
    const vk::StyleSettings smoothing{
        .style = 1, .strength = 1.0F, .smooth = kStrongSmooth, .bands = 0.0F, .outlines = 0.0F, .outline_width = 1.0F,
        .saturation = 1.0F};
    const vk::StyleSettings banding{
        .style = 1, .strength = 1.0F, .smooth = 0.0F, .bands = kFewBands, .outlines = 0.0F, .outline_width = 1.0F,
        .saturation = vk::kStyleSaturation};
    const std::array<FilterCase, 6> cases{{
        {.name = "smoothing alone", .settings = smoothing, .floats = true},
        {.name = "bands alone", .settings = banding, .floats = true},
        {.name = "the defaults", .settings = defaults, .floats = true, .semantics = true},
        {.name = "smoothing 6, 3 bands, lines twice as wide, saturation 1.5", .settings = strong, .floats = true},
        {.name = "strength 0.6, no smoothing, no bands, outlines 0.5", .settings = soft, .floats = true},
        {.name = "an 8-bit target", .settings = defaults, .floats = false},
    }};
    for (const FilterCase& test : cases) {
        check_filter(gpu, test, picture, &tally);
    }
    check_skips(gpu, picture, &tally);
    check_multisampled(gpu, &tally);
    measure_cost(gpu, &tally);
    vk::set_style_settings({});
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
    text::print("thug2_vulkan_style_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
        SDL_CreateWindow("thug2 Vulkan style check", kWindowWidth, kWindowHeight, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
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
