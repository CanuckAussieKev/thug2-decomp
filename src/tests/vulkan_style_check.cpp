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
//   - the other looks (vulkan_style_looks.h: comic, sketch, noir, thermal): each against its twin at the frame's seed
//     (the edge pass too), and what it does - ink along the edges and none inside planes or on the sky, halftone dots
//     over the mid-darks, hatching over the dark end of the gradient and bare paper at its light end, the same pixels
//     frame to frame (comic, sketch); the duotone line, crushed blacks, grain moving with the frame (noir); a figure in
//     front of a wall warmer all over, planes and the sky not (thermal); each in a viewport; the reserved names (vhs,
//     retro, watercolour) record nothing;
//   - the cost of each look at 1920x1080 and 2560x1440 (printed; under 1.5 ms at 2560x1440 on a discrete device).
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
#include "gpu_vulkan/vulkan_style_looks.h"
#include "tests/check_window.h"
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

// --- the other looks (vulkan_style_looks.h) ----------------------------------------------------------------------

// The pass's edge image read back (the target's size).
bool read_edges(std::vector<float>* edges)
{
    vk::Backend& state = vk::backend();
    const std::scoped_lock lock(state.mutex);
    vk::finish_work(state);
    vk::Extent size;
    return vk::read_style_edges(state, edges, &size) && size.width == kWidth && size.height == kHeight;
}

// The pixels where two pictures differ by more than `visible` in any channel.
std::vector<bool> difference_map(const std::vector<StyleRgb>& a, const std::vector<StyleRgb>& b, float visible)
{
    std::vector<bool> map(a.size());
    for (std::size_t pixel = 0; pixel < map.size(); ++pixel) {
        float difference = 0.0F;
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            difference = std::max(difference, std::abs(core::at(core::at(a, pixel), channel) - core::at(core::at(b, pixel), channel)));
        }
        map.at(pixel) = difference > visible;
    }
    return map;
}

int box_area(const Box& box) { return static_cast<int>((box.right - box.left) * (box.bottom - box.top)); }

float mean_of(const std::vector<StyleRgb>& colour, const Box& box, std::size_t channel)
{
    double sum = 0.0;
    for (uint32_t row = box.top; row < box.bottom; ++row) {
        for (uint32_t column = box.left; column < box.right; ++column) {
            sum += core::at(core::at(colour, pixel_of(column, row)), channel);
        }
    }
    return static_cast<float>(sum / static_cast<double>(box_area(box)));
}

// One run of a look: the target filtered, read back with the pass's own images and its parameters (the frame's seed).
struct LookRun {
    bool ok = false;
    std::vector<StyleRgb> input;
    std::vector<StyleRgb> out;
    std::vector<float> alpha;
    std::vector<float> depth;
    std::vector<StyleRgb> smoothed;
    std::vector<float> edges;
    vk::StyleParameters parameters;
};

LookRun run_look(render::Gpu& gpu, const vk::StyleSettings& settings, const Picture& picture)
{
    LookRun run;
    vk::set_style_settings(settings);
    const Targets targets = make_targets(gpu, true, 0);
    if (targets.colour == nullptr || targets.depth == nullptr || !prepare(gpu, targets, picture, &run.input) ||
        run_pass(gpu, targets, {0, 0, kWidth, kHeight}) != 1) {
        release(gpu, targets);
        return run;
    }
    run.parameters = vk::style_last_parameters();
    run.ok = read_colour(targets.colour, true, &run.out, &run.alpha) &&
             (!vk::look_reads_depth(settings.style) || read_linear(&run.depth)) &&
             (run.parameters.radius == 0 || read_smoothed(&run.smoothed)) &&
             (!vk::look_draws_ink(settings.style) || read_edges(&run.edges));
    release(gpu, targets);
    return run;
}

std::vector<StyleRgb> reference_of(const LookRun& run, const vk::StyleParameters& parameters)
{
    return vk::style_reference(parameters,
                               {.colour = &run.input,
                                .depth = run.depth.empty() ? nullptr : &run.depth,
                                .smoothed = run.smoothed.empty() ? nullptr : &run.smoothed},
                               kSize);
}

constexpr int kLookMostOff = static_cast<int>(kWidth * kHeight / 200); // (0.5%: a pattern's edge, a band's)

// The look against its twin: every pixel, finite, alpha; true when it ran.
bool check_look_twin(const char* name, const LookRun& run, Tally* tally)
{
    text::print("%s:\n", name);
    tally->check(run.ok, "the pass recorded, the target and the pass's images read back");
    if (!run.ok) {
        return false;
    }
    if (!run.edges.empty()) {
        const std::vector<float> twin = vk::style_edges(run.parameters, run.depth, kSize);
        float worst = 0.0F;
        int off = 0;
        constexpr float kEdgeSlack = 2.0e-3F;
        for (std::size_t pixel = 0; pixel < twin.size(); ++pixel) {
            const float difference = std::abs(core::at(run.edges, pixel) - core::at(twin, pixel));
            worst = std::max(worst, std::isfinite(difference) ? difference : 1.0F);
            off += difference > kEdgeSlack ? 1 : 0;
        }
        tally->check(off <= kMostOff, text::format("the edge pass as style_edges (%d pixels past %.3f; the largest "
                                                   "difference %.4f)",
                                                   off, static_cast<double>(kEdgeSlack), static_cast<double>(worst)));
    }
    const Compared compared = compare(run.out, reference_of(run, run.parameters), kTolerance);
    tally->check(compared.off <= kLookMostOff,
                 text::format("every pixel within %.1f steps of the reference but %d (%d past it; the largest "
                              "difference %.2f steps)",
                              static_cast<double>(kTolerance * kByteSteps), kLookMostOff, compared.off,
                              static_cast<double>(compared.worst * kByteSteps)));
    tally->check(compared.outside == 0, text::format("every channel finite and within 0 - 1 (%d not)", compared.outside));
    int alpha_changed = 0;
    constexpr float kHalfAlphaValue = 0.5F;
    constexpr float kAlphaSlack = 1.0e-3F;
    for (const float value : run.alpha) {
        alpha_changed += std::abs(value - kHalfAlphaValue) > kAlphaSlack ? 1 : 0;
    }
    tally->check(alpha_changed == 0, text::format("alpha untouched (%d changed)", alpha_changed));
    return true;
}

// Where a look's ink lies: the reference with outlines against without.
void check_ink(const LookRun& run, Tally* tally)
{
    vk::StyleParameters without = run.parameters;
    without.outlines = 0.0F;
    constexpr float kVisible = 0.1F;
    const std::vector<bool> lines = difference_map(reference_of(run, run.parameters), reference_of(run, without), kVisible);
    const int box_top = count_in(lines, kBoxTopEdge);
    const int box_left = count_in(lines, kBoxLeftEdge);
    const int sky = count_in(lines, kSkyAboveBox) + count_in(lines, kSkyLeftOfBox) + count_in(lines, kSkyBetween);
    const int ridge = count_in(lines, kRidgeCrease);
    const int planes = count_in(lines, kRidgeLeftSide) + count_in(lines, kRidgeRightSide) + count_in(lines, kFloorRight);
    text::print("    ink pixels: box top %d, left %d; the sky beside it %d; the ridge %d; inside the planes %d\n", box_top,
                box_left, sky, ridge, planes);
    constexpr int kAlong = 40;
    tally->check(box_top > kAlong && box_left > kAlong && ridge > kAlong,
                 "ink along the box's edges and down the ridge's crease");
    tally->check(sky == 0 && planes == 0, "none on the sky beside the box, none inside a plane");
}

void check_comic(render::Gpu& gpu, const Picture& picture, Tally* tally)
{
    const LookRun run = run_look(gpu, {.style = vk::ArtStyle::kComic}, picture);
    if (!check_look_twin("comic (the defaults)", run, tally)) {
        return;
    }
    check_ink(run, tally);
    // the halftone: dots where the fill is mid-dark (the floor, the ridge), none in the light sky's right half
    vk::StyleParameters plain = run.parameters;
    plain.halftone = 0.0F;
    constexpr float kVisible = 0.05F;
    const std::vector<bool> dots = difference_map(reference_of(run, run.parameters), reference_of(run, plain), kVisible);
    const int floor = count_in(dots, kFloorRight);
    const int box = count_in(dots, kNoisy);
    text::print("    halftone pixels: floor %d of %d, the box %d of %d\n", floor, box_area(kFloorRight), box,
                box_area(kNoisy));
    constexpr float kSome = 0.15F;
    constexpr float kMost = 0.85F; // (dots, not a flat shade: some of the floor between them)
    tally->check(static_cast<float>(floor) > kSome * static_cast<float>(box_area(kFloorRight)) &&
                     static_cast<float>(floor) < kMost * static_cast<float>(box_area(kFloorRight)) &&
                     static_cast<float>(box) > kSome * static_cast<float>(box_area(kNoisy)),
                 "halftone dots over the mid-dark fills, the paper between them");
    // flat fills: the box's noisy field posterized (the dots left out: the twin without them)
    const float before = spread(run.input, kNoisy);
    const float after = spread(reference_of(run, plain), kNoisy);
    constexpr float kCalmer = 0.5F;
    tally->check(after < before * kCalmer, text::format("the fills flattened (spread %.4f -> %.4f)",
                                                        static_cast<double>(before), static_cast<double>(after)));
    // the pattern doesn't move from frame to frame
    const LookRun again = run_look(gpu, {.style = vk::ArtStyle::kComic}, picture);
    tally->check(again.ok && again.out == run.out, "the next frame the same, pixel for pixel (no shimmer)");
}

void check_sketch(render::Gpu& gpu, const Picture& picture, Tally* tally)
{
    const LookRun run = run_look(gpu, {.style = vk::ArtStyle::kSketch}, picture);
    if (!check_look_twin("sketch (the defaults)", run, tally)) {
        return;
    }
    check_ink(run, tally);
    // the hatching by darkness: the gradient's dark end crossed with strokes (much spread), its light end bare paper
    constexpr Box kDarkEnd{.left = 2, .top = kGradientTop, .right = 50, .bottom = kGradientBottom};
    constexpr Box kLightEnd{.left = 225, .top = kGradientTop, .right = 254, .bottom = kGradientBottom};
    const float dark_spread = spread(run.out, kDarkEnd);
    const float light_spread = spread(run.out, kLightEnd);
    const float dark_mean = mean_of(run.out, kDarkEnd, 0);
    const float light_mean = mean_of(run.out, kLightEnd, 0);
    text::print("    the gradient's dark end: mean %.3f, spread %.4f; its light end: mean %.3f, spread %.4f\n",
                static_cast<double>(dark_mean), static_cast<double>(dark_spread), static_cast<double>(light_mean),
                static_cast<double>(light_spread));
    constexpr float kStrokes = 0.08F;
    constexpr float kBare = 0.03F;
    constexpr float kPaperLight = 0.85F;
    tally->check(dark_spread > kStrokes && dark_mean < light_mean, "strokes over the dark end");
    tally->check(light_spread < kBare && light_mean > kPaperLight, "bare paper at the light end");
    // grey with the wash off (the paper's tint aside: red, green and blue within the paper's own spread)
    const LookRun grey = run_look(gpu, {.style = vk::ArtStyle::kSketch, .wash = 0.0F, .paper = 0.0F}, picture);
    float most_chroma = 1.0F;
    if (grey.ok) {
        most_chroma = 0.0F;
        for (const StyleRgb& colour : grey.out) {
            const float high = std::max({core::at(colour, 0), core::at(colour, 1), core::at(colour, 2)});
            const float low = std::min({core::at(colour, 0), core::at(colour, 1), core::at(colour, 2)});
            most_chroma = std::max(most_chroma, high - low);
        }
    }
    constexpr float kGraphiteTint = 0.04F; // (the graphite's own blue)
    tally->check(most_chroma <= kGraphiteTint,
                 text::format("no wash, white paper: pencil grey (the widest channel spread %.3f)", static_cast<double>(most_chroma)));
    const LookRun again = run_look(gpu, {.style = vk::ArtStyle::kSketch}, picture);
    tally->check(again.ok && again.out == run.out, "the next frame the same, pixel for pixel (no shimmer)");
}

void check_noir(render::Gpu& gpu, const Picture& picture, Tally* tally)
{
    const vk::StyleSettings settings{.style = vk::ArtStyle::kNoir};
    const LookRun run = run_look(gpu, settings, picture);
    if (!check_look_twin("noir (the defaults: black to warm paper)", run, tally)) {
        return;
    }
    // every pixel on the duotone's line: the light colour x a tone (the dark is black)
    float worst = 0.0F;
    for (const StyleRgb& colour : run.out) {
        const float tone = core::at(colour, 1) / core::at(vk::kStyleNoirLight, 1);
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            worst = std::max(worst, std::abs(core::at(colour, channel) - (tone * core::at(vk::kStyleNoirLight, channel))));
        }
    }
    constexpr float kOnLine = 3.0F / kByteSteps;
    tally->check(worst < kOnLine, text::format("every pixel a shade between the two colours (off by %.4f at most)",
                                               static_cast<double>(worst)));
    // the contrast: the gradient's darkest quarter crushed to black, its lightest near the light colour
    constexpr Box kDarkQuarter{.left = 2, .top = kGradientTop, .right = 40, .bottom = kGradientBottom};
    constexpr Box kLightQuarter{.left = 200, .top = kGradientTop, .right = 240, .bottom = kGradientBottom};
    const float dark = mean_of(run.out, kDarkQuarter, 1);
    const float light = mean_of(run.out, kLightQuarter, 1);
    text::print("    the gradient: dark quarter %.3f, light quarter %.3f\n", static_cast<double>(dark), static_cast<double>(light));
    constexpr float kCrushed = 0.06F;
    constexpr float kBright = 0.6F;
    tally->check(dark < kCrushed && light > kBright, "the blacks crushed, the lights kept");
    // the grain changes with the frame (and each frame matches its own seed's twin)
    const LookRun next = run_look(gpu, settings, picture);
    bool next_matches = false;
    if (next.ok) {
        next_matches = compare(next.out, reference_of(next, next.parameters), kTolerance).off <= kLookMostOff;
    }
    tally->check(next.ok && next.parameters.seed != run.parameters.seed && next.out != run.out && next_matches,
                 text::format("the grain moves with the frame (seed %u -> %u), each frame as its twin", run.parameters.seed,
                              next.parameters.seed));
    // the orange / teal pair
    const LookRun teal = run_look(gpu,
                                  {.style = vk::ArtStyle::kNoir,
                                   .noir_dark = {0.04F, 0.16F, 0.2F},
                                   .noir_light = {1.0F, 0.62F, 0.3F}},
                                  picture);
    if (check_look_twin("noir (orange / teal)", teal, tally)) {
        const float blue_dark = mean_of(teal.out, kDarkQuarter, 2);
        const float red_light = mean_of(teal.out, kLightQuarter, 0);
        tally->check(blue_dark > mean_of(teal.out, kDarkQuarter, 0) && red_light > mean_of(teal.out, kLightQuarter, 2),
                     "teal shadows, orange lights");
    }
}

void check_thermal(render::Gpu& gpu, const Picture& picture, Tally* tally)
{
    const LookRun run = run_look(gpu, {.style = vk::ArtStyle::kThermal}, picture);
    if (!check_look_twin("thermal (the defaults)", run, tally)) {
        return;
    }
    // what stands in front glows (the twin - the device's match proven above - on a depth of its own): a figure 8 x 24
    // pixels at 200 units in front of a wall at 1000 over a tilted floor - warmer with focus 1 than 0 all over, the wall
    // away from it and the floor (planes) not
    constexpr Box kFigure{.left = 116, .top = 40, .right = 124, .bottom = 64}; // (a skater's build at this size)
    constexpr float kWallZ = 1000.0F;
    constexpr float kFigureZ = 200.0F;
    constexpr uint32_t kFloorFrom = 90;
    std::vector<float> depth(static_cast<std::size_t>(kWidth) * kHeight, kWallZ);
    for (uint32_t row = 0; row < kHeight; ++row) {
        for (uint32_t column = 0; column < kWidth; ++column) {
            float z = kWallZ;
            if (row >= kFloorFrom) { // (1/z affine down the screen: a plane)
                const float along = static_cast<float>(row - kFloorFrom) / static_cast<float>(kHeight - kFloorFrom);
                z = 1.0F / ((1.0F / kWallZ) + (along * ((1.0F / kFloorNear) - (1.0F / kWallZ))));
            }
            if (column >= kFigure.left && column < kFigure.right && row >= kFigure.top && row < kFigure.bottom) {
                z = kFigureZ;
            }
            core::at(depth, pixel_of(column, row)) = z;
        }
    }
    const std::vector<StyleRgb> flat(depth.size(), StyleRgb{kHalf, kHalf, kHalf});
    vk::StyleParameters hot = run.parameters;
    hot.focus = 1.0F;
    vk::StyleParameters cold = run.parameters;
    cold.focus = 0.0F;
    constexpr float kVisible = 0.05F;
    const std::vector<bool> warmer =
        difference_map(vk::style_reference(hot, {.colour = &flat, .depth = &depth}, kSize),
                       vk::style_reference(cold, {.colour = &flat, .depth = &depth}, kSize), kVisible);
    constexpr Box kWallAway{.left = 10, .top = 10, .right = 60, .bottom = 80};
    constexpr Box kFloorAway{.left = 10, .top = 100, .right = 250, .bottom = 120};
    const int figure = count_in(warmer, kFigure);
    const int wall = count_in(warmer, kWallAway);
    const int floor = count_in(warmer, kFloorAway);
    text::print("    warmer with focus: the figure %d of %d pixels, the wall %d, the floor %d\n", figure, box_area(kFigure), wall,
                floor);
    constexpr float kAllOver = 0.9F;
    tally->check(static_cast<float>(figure) >= kAllOver * static_cast<float>(box_area(kFigure)) && wall == 0 && floor == 0,
                 "a figure in front of a wall warmer all over (its middle too), the wall and the floor not");
    // the sky cold: dark, blue over red
    const float sky_red = mean_of(run.out, kSkyBetween, 0);
    const float sky_blue = mean_of(run.out, kSkyBetween, 2);
    constexpr float kCold = 0.45F;
    tally->check(sky_blue > sky_red && sky_blue < kCold,
                 text::format("the sky cold (red %.3f, blue %.3f)", static_cast<double>(sky_red), static_cast<double>(sky_blue)));
    const LookRun next = run_look(gpu, {.style = vk::ArtStyle::kThermal}, picture);
    bool next_matches = false;
    if (next.ok) {
        next_matches = compare(next.out, reference_of(next, next.parameters), kTolerance).off <= kLookMostOff;
    }
    tally->check(next.ok && next.out != run.out && next_matches, "the noise flickers with the frame, each frame as its twin");
}

// Every look in a viewport: only its rectangle changed; cel (cel shading, no post-process) records nothing here (the
// vhs, retro and watercolour passes: thug2_vulkan_style_film_check).
void check_look_viewports(render::Gpu& gpu, const Picture& picture, Tally* tally)
{
    text::print("the looks in a viewport; cel:\n");
    constexpr std::array<uint32_t, 4> kViewport{64, 16, 128, 96};
    const auto changes = [&](vk::ArtStyle style, uint64_t* passes, int* inside, int* outside) {
        vk::set_style_settings({.style = style});
        const Targets targets = make_targets(gpu, true, 0);
        std::vector<StyleRgb> input;
        *outside = -1;
        if (targets.colour != nullptr && targets.depth != nullptr && prepare(gpu, targets, picture, &input)) {
            *passes = run_pass(gpu, targets, kViewport);
            std::vector<uint8_t> bytes;
            if (vk::read_surface(targets.colour, &bytes)) {
                *inside = 0;
                *outside = 0;
                for (uint32_t row = 0; row < kHeight; ++row) {
                    for (uint32_t column = 0; column < kWidth; ++column) {
                        const std::size_t pixel = pixel_of(column, row);
                        bool changed = false;
                        for (std::size_t channel = 0; channel < kChannels; ++channel) {
                            changed = changed || half_at(bytes, {.pixel = pixel, .channel = channel}) !=
                                                     core::at(picture.bits, (pixel * kChannels) + channel);
                        }
                        const bool in = column >= core::at(kViewport, 0) && column < core::at(kViewport, 0) + core::at(kViewport, 2) &&
                                        row >= core::at(kViewport, 1) && row < core::at(kViewport, 1) + core::at(kViewport, 3);
                        (in ? *inside : *outside) += changed ? 1 : 0;
                    }
                }
            }
        }
        release(gpu, targets);
    };
    for (const vk::ArtStyle style : {vk::ArtStyle::kComic, vk::ArtStyle::kSketch, vk::ArtStyle::kNoir, vk::ArtStyle::kThermal}) {
        uint64_t passes = 0;
        int inside = 0;
        int outside = 0;
        changes(style, &passes, &inside, &outside);
        tally->check(passes == 1 && outside == 0 &&
                         std::cmp_greater(inside, core::at(kViewport, 2) * core::at(kViewport, 3) / 2),
                     text::format("%s: %d pixels changed inside the viewport, %d outside", vk::art_style_name(style), inside,
                                  outside));
    }
    {
        uint64_t passes = 1;
        int inside = 0;
        int outside = 0;
        changes(vk::ArtStyle::kCel, &passes, &inside, &outside);
        tally->check(passes == 0 && inside == 0 && outside == 0,
                     "cel (cel shading: no post-process): no pass, every pixel as it was");
        passes = 1;
        changes(vk::ArtStyle::kFisheye, &passes, &inside, &outside);
        tally->check(passes == 0 && inside == 0 && outside == 0,
                     "fisheye (the camera effects' lens: no style pass): no pass, every pixel as it was");
    }
    // the names: each read back as itself, the old numbers, anything else refused
    bool names = true;
    for (unsigned index = 0; index < vk::kArtStyleCount; ++index) {
        vk::ArtStyle read = vk::ArtStyle::kOff;
        names = names && vk::parse_art_style(vk::art_style_name(static_cast<vk::ArtStyle>(index)), &read) &&
                read == static_cast<vk::ArtStyle>(index);
    }
    vk::ArtStyle old = vk::ArtStyle::kOff;
    names = names && vk::parse_art_style("1", &old) && old == vk::ArtStyle::kToon && vk::parse_art_style("0", &old) &&
            old == vk::ArtStyle::kOff && vk::parse_art_style(" Noir ", &old) && old == vk::ArtStyle::kNoir &&
            !vk::parse_art_style("sepia", &old) && old == vk::ArtStyle::kNoir &&
            vk::parse_art_style("Fisheye_VHS", &old) && old == vk::ArtStyle::kFisheyeVhs;
    tally->check(names, "rt_style's names read back; 0 / 1 as off / toon; an unknown name refused");
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
    vk::set_style_settings({.style = vk::ArtStyle::kToon});
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
    vk::set_style_settings({.style = vk::ArtStyle::kToon, .strength = 0.0F});
    passes = 1;
    const int none = unchanged_outside({0, 0, 0, 0}, &passes, &inside);
    tally->check(passes == 0 && none == 0, "strength 0: no pass, every pixel as it was");
}

// Four samples: the colour (drawn: flat fields) resolved, filtered, written to every sample.
void check_multisampled(render::Gpu& gpu, Tally* tally)
{
    text::print("four samples:\n");
    vk::set_style_settings({.style = vk::ArtStyle::kToon});
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
            vk::style_parameters(vk::StyleSettings{.style = vk::ArtStyle::kToon}, view_of({0, 0, kWidth, kHeight}), {.output = kHeight, .depth = kHeight});
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

// The passes' time on the device over float targets of the game's sizes (the scene drawn, a look's defaults): printed;
// at 2560 x 1440 under kMostCost.
constexpr double kMostCost = 1.5; // (milliseconds; the RTX 5070 Ti's budget - an integrated device is only printed)
void measure_cost(render::Gpu& gpu, vk::ArtStyle look, Tally* tally)
{
    constexpr int kTimedPasses = 5;
    constexpr double kNanosecondsPerMillisecond = 1.0e6;
    vk::set_style_settings({.style = look});
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
        // (the fastest of several short rounds: the device's clocks rise under the first, and other programs' work on
        // the device - other games, other checks - lands in some)
        constexpr int kRounds = 16;
        double each = std::numeric_limits<double>::infinity();
        for (int round = 0; round < kRounds; ++round) {
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
            each = std::min(each, static_cast<double>(stamps.back() - stamps.front()) *
                                      static_cast<double>(properties.limits.timestampPeriod) / kNanosecondsPerMillisecond /
                                      kTimedPasses);
        }
        gpu.set_render_target(0, nullptr);
        gpu.set_depth_surface(nullptr);
        gpu.end_scene();
        release(gpu, targets);
        text::print("the cost of %s: %.3f ms a pass at %ux%u (its defaults: smoothing radius %d)\n",
                    vk::art_style_name(look), each, width, height,
                    vk::style_parameters({.style = look}, view, {.output = height, .depth = height}).radius);
        tally->check(std::isfinite(each) && each > 0.0, text::format("%s timed at %ux%u", vk::art_style_name(look), width, height));
        if (width == core::at(core::at(kSizes, 1), 0) && properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            tally->check(each < kMostCost, text::format("%s under %.1f ms at %ux%u on a discrete device",
                                                        vk::art_style_name(look), kMostCost, width, height));
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
    const vk::StyleSettings defaults{.style = vk::ArtStyle::kToon};
    const vk::StyleSettings strong{
        .style = vk::ArtStyle::kToon, .strength = 1.0F, .smooth = kStrongSmooth, .bands = kFewBands, .outlines = 1.0F,
        .outline_width = kWideLines, .saturation = kStrongColour};
    const vk::StyleSettings soft{
        .style = vk::ArtStyle::kToon, .strength = kSoftMix, .smooth = 0.0F, .bands = 0.0F, .outlines = kSoftLines,
        .outline_width = 1.0F, .saturation = vk::kStyleSaturation};
    const vk::StyleSettings smoothing{
        .style = vk::ArtStyle::kToon, .strength = 1.0F, .smooth = kStrongSmooth, .bands = 0.0F, .outlines = 0.0F, .outline_width = 1.0F,
        .saturation = 1.0F};
    const vk::StyleSettings banding{
        .style = vk::ArtStyle::kToon, .strength = 1.0F, .smooth = 0.0F, .bands = kFewBands, .outlines = 0.0F, .outline_width = 1.0F,
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
    check_comic(gpu, picture, &tally);
    check_sketch(gpu, picture, &tally);
    check_noir(gpu, picture, &tally);
    check_thermal(gpu, picture, &tally);
    check_look_viewports(gpu, picture, &tally);
    for (const vk::ArtStyle look :
         {vk::ArtStyle::kToon, vk::ArtStyle::kComic, vk::ArtStyle::kSketch, vk::ArtStyle::kNoir, vk::ArtStyle::kThermal}) {
        measure_cost(gpu, look, &tally);
    }
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
