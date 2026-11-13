// NOT ORIGINAL - thug2_vulkan_water_check.exe: the Vulkan Gpu's water ([Graphics] rt_water, gpu_vulkan/vulkan_water.h;
// the shader water_shader.cpp writes) tried on this machine against the same water worked out on the processor.
//
// A synthetic shore: a camera 40 units above calm water (y = 0) looking down 35 degrees along +z; a sand floor sloping
// from 5 units above the water (the shore, z < 40) to 104 below it at the top of the picture; the water a quad marked as
// render_pc marks a water material (engine/gfx/water_surfaces.h) and drawn through render::Gpu with the game's
// single-texture material shader (kTextureVertexX4) into a 16-bit float target with a depth surface. Cases:
//   - calm (rt_water_waves=0, no foam): every water pixel against the processor - the floor seen through the water
//     (the copy made before the water: its colour and depth), absorbed along the light's path (to the floor and down to
//     it) by the tint's hue, the scattered deep colour, Fresnel toward the sky (the view's fog colour: no capture, the
//     reflected rays leave the view); the sand in front of the water the same as without water;
//   - rt_water=0, and rt_water=1 with the draw not marked: the original's picture, byte for byte (the water the
//     program's colour); the floor byte for byte in every case;
//   - foam: changes only where the water is shallow (within 24 units of the floor below), and brightens it;
//   - waves (rt_water_waves=1, wind 1): the picture moves with the game's clock, the same clock draws the same bytes;
//   - the sun's glitter (the sun given by rt::set_sun_override toward the mirror direction of the picture's centre):
//     above 1 near the centre with HDR, nothing outside the water;
//   - the ripple texture's levels 0 and 4 read back against water_waves_reference;
//   - every pixel of every case finite (no NaN or infinity), validation (and synchronization validation) messages 0.
//   thug2_vulkan_water_check [--device N] [--display N] [--no-validation] [--perf]
// --perf also times a frame of full-screen water at 2560 x 1440 with rt_water off and on (reported, not checked).
// Exit code: 0 when every check passed, 1 when one failed, 2 when this machine has no Vulkan for this process.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <array>
#include <chrono>
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
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/water_surfaces.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_water.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuRenderState;
using render::GpuSamplerState;
using render::GpuStageState;
using render::GpuSurface;
using render::GpuTexture;
namespace value = render::gpu_value;
namespace vk = render::vulkan;
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
constexpr float kFar = 2000.0F;
constexpr float kPitch = 0.61086524F; // 35 degrees down
constexpr Vec3 kCamera{0.0F, 40.0F, 0.0F};
constexpr float kShoreHeight = 5.0F;  // the floor's height at z = kShoreStart
constexpr float kShoreStart = 20.0F;
constexpr float kSlope = 0.25F;       // the floor falls this much a unit of z
constexpr float kFloorFar = 900.0F;   // (the quads' far edge)
constexpr float kSide = 600.0F;       // (their half width)
constexpr Vec3 kSand{0.8F, 0.7F, 0.5F};
constexpr Vec3 kTint{0.25F, 0.45F, 0.5F};
constexpr Vec3 kFog{0.5F, 0.6F, 0.7F};
constexpr float kVisibility = 450.0F;
constexpr float kShoreFoam = 24.0F;     // (the shader's foam depth)
constexpr float kTolerance = 4.0e-3F;   // a pixel's largest difference (16-bit floats, interpolated depths)
constexpr float kWorstTolerance = 0.03F;
constexpr double kShareWithin = 0.99;
constexpr std::size_t kLeastJudged = 20000;
constexpr float kEdgeDepth = 0.6F;      // (pixels this near the shoreline in depth left out: the depth's rasterisation)
constexpr uint32_t kFvfPosition = 0x002;
constexpr uint32_t kFvfDiffuse = 0x040;
constexpr uint32_t kFvfTex1 = 0x100;
constexpr uint32_t kHalfGrey = 0x80808080U; // (the vertex colour 128: 1 after the shader's doubling)
constexpr Vec3 kLuma{0.2126F, 0.7152F, 0.0722F};
constexpr std::string_view kShader = "ps.1.1\ntex t0\nmul r0.rgb,t0,c0\n+mul_x2 r0.a,v0.a,t0.a\nmul_x4 r0.rgb,v0,r0\n";

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

// --- vectors ----------------------------------------------------------------------------------------------------------

Vec3 add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 scale(const Vec3& a, float by) { return {a[0] * by, a[1] * by, a[2] * by}; }
float dot(const Vec3& a, const Vec3& b) { return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]); }
Vec3 normalise(const Vec3& a)
{
    const float length = std::sqrt(dot(a, a));
    return length > 0.0F ? scale(a, 1.0F / length) : a;
}
float as_byte(float value) { return std::round(std::clamp(value, 0.0F, 1.0F) * kByteMax) / kByteMax; }
Vec3 as_bytes(const Vec3& a) { return {as_byte(a[0]), as_byte(a[1]), as_byte(a[2])}; }
float smoothstep(float low, float high, float value)
{
    const float t = std::clamp((value - low) / (high - low), 0.0F, 1.0F);
    return t * t * (3.0F - (2.0F * t));
}

// --- the scene --------------------------------------------------------------------------------------------------------

// The camera's axes (Direct3D's left-handed: x right, y up, z forward).
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

// The view (world -> camera, Direct3D's row vectors) and the projection.
Matrix view_matrix()
{
    const Axes axes = camera_axes();
    Matrix view{};
    for (std::size_t row = 0; row < 3; ++row) {
        core::at(core::at(view, row), 0) = core::at(axes.right, row);
        core::at(core::at(view, row), 1) = core::at(axes.up, row);
        core::at(core::at(view, row), 2) = core::at(axes.forward, row);
    }
    core::at(view, 3) = {-dot(axes.right, kCamera), -dot(axes.up, kCamera), -dot(axes.forward, kCamera), 1.0F};
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
std::array<float, 16> flat(const Matrix& m)
{
    std::array<float, 16> out{};
    for (std::size_t i = 0; i < 16; ++i) {
        core::at(out, i) = core::at(core::at(m, i / 4), i % 4);
    }
    return out;
}

render::rt::ViewDrawn view_of(uint32_t width, uint32_t height, double seconds)
{
    render::rt::ViewDrawn view;
    view.rect = {0, 0, width, height};
    view.output_rect = view.rect;
    view.depth_range = {0.0F, 1.0F};
    view.camera.view = view_matrix();
    view.camera.projection = projection_matrix(width, height);
    view.camera.position = kCamera;
    view.fog = {.on = true, .start = kFar, .end = kFar, .colour = kFog};
    view.game_seconds = seconds;
    return view;
}

float floor_height(float z) { return kShoreHeight - (kSlope * (z - kShoreStart)); }

struct Vertex {
    std::array<float, 3> position;
    uint32_t colour;
    std::array<float, 2> uv;
};

// A horizontal (or sloped) quad from z = kShoreStart to kFloorFar.
std::array<Vertex, 6> quad(bool floor)
{
    const auto at = [floor](float x, float z) {
        return Vertex{.position = {x, floor ? floor_height(z) : 0.0F, z}, .colour = kHalfGrey, .uv = {x / 64.0F, z / 64.0F}};
    };
    const float first_z = floor ? 0.0F : kShoreStart;
    return {at(-kSide, first_z), at(kSide, first_z), at(-kSide, kFloorFar), at(kSide, first_z), at(kSide, kFloorFar), at(-kSide, kFloorFar)};
}

GpuTexture* uniform_texture(render::Gpu& gpu, const Vec3& colour, uint8_t alpha)
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
    for (uint32_t row = 0; row < kTexels; ++row) {
        for (uint32_t column = 0; column < kTexels; ++column) {
            const std::size_t at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(locked.pitch)) + (std::size_t{column} * 4);
            core::at(bytes, at) = static_cast<uint8_t>(std::lround(core::at(colour, 2) * kByteMax));
            core::at(bytes, at + 1) = static_cast<uint8_t>(std::lround(core::at(colour, 1) * kByteMax));
            core::at(bytes, at + 2) = static_cast<uint8_t>(std::lround(core::at(colour, 0) * kByteMax));
            core::at(bytes, at + 3) = alpha;
        }
    }
    gpu.unlock_texture(texture, 0);
    return texture;
}

struct Scene {
    GpuSurface* colour = nullptr;
    GpuSurface* depth = nullptr;
    GpuTexture* sand = nullptr;
    GpuTexture* water = nullptr;
    render::GpuPixelShader* shader = nullptr;
    uint32_t width = kWidth;
    uint32_t height = kHeight;
};

void common_states(render::Gpu& gpu, const Scene& scene)
{
    gpu.set_render_state(GpuRenderState::kCullMode, value::kCullNone);
    gpu.set_render_state(GpuRenderState::kAlphaTestEnable, 0);
    gpu.set_render_state(GpuRenderState::kFogEnable, 0);
    gpu.set_render_state(GpuRenderState::kSpecularEnable, 0);
    gpu.set_render_state(GpuRenderState::kLighting, 0);
    gpu.set_render_state(GpuRenderState::kZEnable, 1);
    gpu.set_render_state(GpuRenderState::kZFunc, value::kCompareLessEqual);
    const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    gpu.set_transform(render::GpuTransform::kWorld, identity.data());
    const std::array<float, 16> view = flat(view_matrix());
    gpu.set_transform(render::GpuTransform::kView, view.data());
    const std::array<float, 16> projection = flat(projection_matrix(scene.width, scene.height));
    gpu.set_transform(render::GpuTransform::kProjection, projection.data());
    for (const GpuSamplerState state : {GpuSamplerState::kMinFilter, GpuSamplerState::kMagFilter}) {
        gpu.set_sampler_state(0, state, value::kFilterLinear);
    }
    gpu.set_sampler_state(0, GpuSamplerState::kMipFilter, value::kFilterNone);
    gpu.set_sampler_state(0, GpuSamplerState::kAddressU, value::kAddressWrap);
    gpu.set_sampler_state(0, GpuSamplerState::kAddressV, value::kAddressWrap);
    gpu.set_stage_state(0, GpuStageState::kTexCoordIndex, 0);
    gpu.set_pixel_shader(scene.shader);
    const std::array<float, 4> c0{0.5F, 0.5F, 0.5F, 0.5F};
    gpu.set_pixel_constants(0, c0.data(), 1);
    gpu.set_vertex_format(kFvfPosition | kFvfDiffuse | kFvfTex1);
}

// One frame of the shore: the view begun (the listeners told), the sand, the water (marked unless `marked` false, alpha
// blended when `blended`), the view's world drawn.
void draw_frame(render::Gpu& gpu, const Scene& scene, double seconds, bool marked, bool blended, int layers = 1)
{
    const render::rt::ViewDrawn view = view_of(scene.width, scene.height, seconds);
    gpu.begin_scene();
    gpu.set_render_target(0, scene.colour);
    gpu.set_depth_surface(scene.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = scene.width, .height = scene.height, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth, 0xFF000000U, 1.0F, 0);
    render::rt::view_begins(view);
    common_states(gpu, scene);
    gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
    gpu.set_texture(0, scene.sand);
    render::water::set_current(nullptr, 0);
    const std::array<Vertex, 6> sand = quad(true);
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, 2, sand.data(), sizeof(Vertex));
    for (int layer = 0; layer < layers; ++layer) {
        gpu.set_render_state(GpuRenderState::kZWriteEnable, 0);
        gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, blended ? 1 : 0);
        gpu.set_render_state(GpuRenderState::kSrcBlend, value::kBlendSrcAlpha);
        gpu.set_render_state(GpuRenderState::kDestBlend, value::kBlendInvSrcAlpha);
        gpu.set_texture(0, scene.water);
        render::water::set_current(marked ? scene.water : nullptr, 0x57A7E400U);
        const std::array<Vertex, 6> water = quad(false);
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, 2, water.data(), sizeof(Vertex));
    }
    render::water::set_current(nullptr, 0);
    render::rt::opaque_view_drawn(view);
    render::rt::world_view_drawn(view);
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    gpu.end_scene();
}

struct Picture {
    std::vector<Vec3> rgb;
    std::vector<uint8_t> bytes;
    bool read = false;
};

Picture read_picture(GpuSurface* target, uint32_t width, uint32_t height)
{
    Picture picture;
    picture.read = vk::read_surface(target, &picture.bytes) &&
                   picture.bytes.size() >= static_cast<std::size_t>(width) * height * kHalfPixelBytes;
    if (!picture.read) {
        return picture;
    }
    picture.rgb.resize(static_cast<std::size_t>(width) * height);
    for (std::size_t pixel = 0; pixel < picture.rgb.size(); ++pixel) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const std::size_t at = (pixel * kHalfPixelBytes) + (channel * 2);
            core::at(core::at(picture.rgb, pixel), channel) =
                from_half(static_cast<uint16_t>(core::at(picture.bytes, at) | (core::at(picture.bytes, at + 1) << kHighByte)));
        }
    }
    return picture;
}

// What a pixel shows on the processor: the ray from the camera through its centre (Direct3D 9's: the backend's half
// pixel), where it meets the floor and the water.
struct Ray {
    Vec3 direction{};
    float to_water = 0.0F; // distances along the normalised direction (infinite: never)
    float to_floor = 0.0F;
};
Ray ray_of(uint32_t px, uint32_t py)
{
    const Axes axes = camera_axes();
    const float ndc_x = ((2.0F * static_cast<float>(px)) / static_cast<float>(kWidth)) - 1.0F;
    const float ndc_y = 1.0F - ((2.0F * static_cast<float>(py)) / static_cast<float>(kHeight));
    const float aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
    Ray ray;
    ray.direction = normalise(add(add(scale(axes.right, ndc_x * aspect / kVerticalScale), scale(axes.up, ndc_y / kVerticalScale)),
                                  axes.forward));
    const float dy = core::at(ray.direction, 1);
    ray.to_water = dy < 0.0F ? -core::at(kCamera, 1) / dy : std::numeric_limits<float>::infinity();
    // the floor y = h0 - s (z - z0): camera y + t dy = h0 - s (t dz - z0)
    const float dz = core::at(ray.direction, 2);
    const float denominator = dy + (kSlope * dz);
    ray.to_floor = denominator < 0.0F ? (kShoreHeight + (kSlope * kShoreStart) - core::at(kCamera, 1)) / denominator
                                      : std::numeric_limits<float>::infinity();
    return ray;
}

// The calm water's colour at a pixel (rt_water_waves=0, no foam, no sun): false where the pixel isn't judged (the
// floor in front, the shoreline's edge, off the quads).
bool expected_calm(uint32_t px, uint32_t py, Vec3* out, float* depth_down)
{
    const Ray ray = ray_of(px, py);
    if (!std::isfinite(ray.to_water) || !std::isfinite(ray.to_floor) || ray.to_floor < ray.to_water) {
        return false;
    }
    const Vec3 point = add(kCamera, scale(ray.direction, ray.to_water));
    if (core::at(point, 2) < kShoreStart + 1.0F || core::at(point, 2) > kFloorFar - 50.0F) {
        return false;
    }
    if (core::at(kCamera, 2) + (core::at(ray.direction, 2) * ray.to_floor) > kFloorFar - 1.0F) {
        return false; // (the floor ends before the ray meets it)
    }
    const float path = ray.to_floor - ray.to_water;
    const Vec3 v = scale(ray.direction, -1.0F);
    *depth_down = path * core::at(v, 1);
    if (*depth_down < kEdgeDepth) {
        return false;
    }
    const Vec3 tint = as_bytes(kTint);
    const float most = std::max({core::at(tint, 0), core::at(tint, 1), core::at(tint, 2)});
    const Vec3 hue = scale(tint, 1.0F / most);
    constexpr float kProgramGain = 2.0F * 128.0F / kByteMax; // (4 v0 c0: v0 the vertex colour 128 / 255, c0 0.5)
    const float light = dot(tint, kLuma) * kProgramGain;
    const Vec3 deep = scale(hue, light * 0.75F / dot(hue, kLuma));
    const Vec3 sand = as_bytes(kSand);
    const float fresnel = 0.02F + (0.98F * std::pow(1.0F - core::at(v, 1), 5.0F));
    for (std::size_t c = 0; c < 3; ++c) {
        const float sigma = (3.0F / kVisibility) * (0.2F + (1.6F * (1.0F - core::at(hue, c))));
        const float through = std::exp(-sigma * path);
        const float below = std::exp(-sigma * *depth_down);
        const float refracted = (core::at(sand, c) * through * below) + (core::at(deep, c) * (1.0F - through));
        out->at(c) = std::lerp(refracted, core::at(kFog, c), fresnel);
    }
    return true;
}

bool sand_pixel(uint32_t px, uint32_t py)
{
    const Ray ray = ray_of(px, py);
    return std::isfinite(ray.to_floor) && (ray.to_floor + 0.5F < ray.to_water || !std::isfinite(ray.to_water));
}

bool all_finite(const Picture& picture)
{
    return std::ranges::all_of(picture.rgb, [](const Vec3& c) { return std::isfinite(c[0]) && std::isfinite(c[1]) && std::isfinite(c[2]); });
}

void set_water(bool on, float waves, float foam, bool hdr = true)
{
    vk::set_water_settings({.on = on, .waves = waves, .wind = 1.0F, .heading = 30.0F, .clarity = 1.0F, .foam = foam, .hdr = hdr});
}

void check_calm(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    set_water(true, 0.0F, 0.0F);
    draw_frame(gpu, scene, 1.0, true, false);
    const Picture got = read_picture(scene.colour, kWidth, kHeight);
    if (!got.read) {
        tally->check(false, "calm water: read back");
        return;
    }
    std::size_t judged = 0;
    std::size_t within = 0;
    float worst = 0.0F;
    std::string worst_at;
    for (uint32_t py = 0; py < kHeight; ++py) {
        for (uint32_t px = 0; px < kWidth; ++px) {
            Vec3 want{};
            float down = 0.0F;
            if (!expected_calm(px, py, &want, &down)) {
                continue;
            }
            const Vec3& have = core::at(got.rgb, (static_cast<std::size_t>(py) * kWidth) + px);
            float difference = 0.0F;
            for (std::size_t c = 0; c < 3; ++c) {
                difference = std::max(difference, std::fabs(have.at(c) - want.at(c)));
            }
            ++judged;
            within += difference <= kTolerance ? 1U : 0U;
            if (!(difference <= worst)) {
                worst = std::isfinite(difference) ? difference : std::numeric_limits<float>::infinity();
                worst_at = text::format(" (at %u, %u, %.1f deep: %.4f %.4f %.4f, expected %.4f %.4f %.4f)", px, py,
                                        static_cast<double>(down), static_cast<double>(have[0]), static_cast<double>(have[1]),
                                        static_cast<double>(have[2]), static_cast<double>(want[0]), static_cast<double>(want[1]),
                                        static_cast<double>(want[2]));
            }
        }
    }
    const double share = judged != 0 ? static_cast<double>(within) / static_cast<double>(judged) : 0.0;
    tally->check(judged > kLeastJudged && share >= kShareWithin && worst <= kWorstTolerance,
                 text::format("calm water: %zu pixels against the processor (refraction, absorption, deep colour, Fresnel), "
                              "%.2f%% within %.3f, the worst %.4f%s",
                              judged, share * 100.0, static_cast<double>(kTolerance), static_cast<double>(worst),
                              worst > kTolerance ? worst_at.c_str() : ""));
    tally->check(all_finite(got), "calm water: every pixel finite");
}

// The pictures' bytes equal over the pixels `where` says.
template <typename Where>
std::size_t differing(const Picture& a, const Picture& b, Where where)
{
    std::size_t count = 0;
    for (uint32_t py = 0; py < kHeight; ++py) {
        for (uint32_t px = 0; px < kWidth; ++px) {
            if (!where(px, py)) {
                continue;
            }
            const std::size_t at = ((static_cast<std::size_t>(py) * kWidth) + px) * kHalfPixelBytes;
            for (std::size_t k = 0; k < 6; ++k) {
                if (core::at(a.bytes, at + k) != core::at(b.bytes, at + k)) {
                    ++count;
                    break;
                }
            }
        }
    }
    return count;
}

void check_off(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    set_water(false, 1.0F, 1.0F);
    draw_frame(gpu, scene, 1.0, true, false);
    const Picture off = read_picture(scene.colour, kWidth, kHeight);
    set_water(true, 1.0F, 1.0F);
    draw_frame(gpu, scene, 1.0, false, false);
    const Picture unmarked = read_picture(scene.colour, kWidth, kHeight);
    draw_frame(gpu, scene, 1.0, true, false);
    const Picture on = read_picture(scene.colour, kWidth, kHeight);
    const auto everywhere = [](uint32_t, uint32_t) { return true; };
    tally->check(off.read && unmarked.read && differing(off, unmarked, everywhere) == 0,
                 "rt_water=1 with the draw not marked: the same bytes as rt_water=0");
    // the water as the program drew it: the texture's colour x 4 v0 c0
    const Vec3 tint = as_bytes(kTint);
    std::size_t water_pixels = 0;
    std::size_t original = 0;
    for (uint32_t py = 0; py < kHeight; ++py) {
        for (uint32_t px = 0; px < kWidth; ++px) {
            Vec3 want{};
            float down = 0.0F;
            if (!expected_calm(px, py, &want, &down)) {
                continue;
            }
            ++water_pixels;
            const Vec3& have = core::at(off.rgb, (static_cast<std::size_t>(py) * kWidth) + px);
            constexpr float kProgramGain = 2.0F * 128.0F / kByteMax;
            original += std::fabs(have[0] - (tint[0] * kProgramGain)) < 1.0e-3F &&
                                std::fabs(have[1] - (tint[1] * kProgramGain)) < 1.0e-3F &&
                                std::fabs(have[2] - (tint[2] * kProgramGain)) < 1.0e-3F
                            ? 1U
                            : 0U;
        }
    }
    tally->check(water_pixels > kLeastJudged && original == water_pixels,
                 text::format("rt_water=0: the water is the program's colour (%zu of %zu pixels)", original, water_pixels));
    const std::size_t sand_changed = differing(off, on, sand_pixel);
    tally->check(off.read && on.read && sand_changed == 0, text::format("the sand in front of the water: the same bytes with rt_water on (%zu differ)", sand_changed));
    tally->check(on.read && differing(off, on, everywhere) > kLeastJudged, "rt_water=1: the water changed");
}

void check_foam(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    set_water(true, 0.0F, 0.0F);
    draw_frame(gpu, scene, 3.0, true, false);
    const Picture without = read_picture(scene.colour, kWidth, kHeight);
    set_water(true, 0.0F, 1.0F);
    draw_frame(gpu, scene, 3.0, true, false);
    const Picture with = read_picture(scene.colour, kWidth, kHeight);
    std::size_t deep_changed = 0;
    std::size_t shallow = 0;
    std::size_t shallow_brighter = 0;
    for (uint32_t py = 0; py < kHeight; ++py) {
        for (uint32_t px = 0; px < kWidth; ++px) {
            Vec3 want{};
            float down = 0.0F;
            if (!expected_calm(px, py, &want, &down)) {
                continue;
            }
            const std::size_t at = (static_cast<std::size_t>(py) * kWidth) + px;
            const float before = dot(core::at(without.rgb, at), kLuma);
            const float after = dot(core::at(with.rgb, at), kLuma);
            if (down > kShoreFoam + 1.0F) {
                deep_changed += std::fabs(after - before) > 1.0e-3F ? 1U : 0U;
            } else if (down < kShoreFoam * 0.5F) {
                ++shallow;
                shallow_brighter += after > before + 0.02F ? 1U : 0U;
            }
        }
    }
    tally->check(without.read && with.read && deep_changed == 0, text::format("foam: no change where the water is deeper than %.0f units (%zu changed)", static_cast<double>(kShoreFoam), deep_changed));
    tally->check(shallow > 100 && shallow_brighter * 4 > shallow,
                 text::format("foam: the shallow water brightened (%zu of %zu pixels)", shallow_brighter, shallow));
}

void check_waves(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    set_water(true, 1.0F, 1.0F);
    draw_frame(gpu, scene, 10.0, true, true);
    const Picture first = read_picture(scene.colour, kWidth, kHeight);
    draw_frame(gpu, scene, 10.0, true, true);
    const Picture again = read_picture(scene.colour, kWidth, kHeight);
    draw_frame(gpu, scene, 10.5, true, true);
    const Picture later = read_picture(scene.colour, kWidth, kHeight);
    set_water(true, 0.0F, 1.0F);
    draw_frame(gpu, scene, 10.0, true, true);
    const Picture calm = read_picture(scene.colour, kWidth, kHeight);
    const auto everywhere = [](uint32_t, uint32_t) { return true; };
    tally->check(first.read && again.read && differing(first, again, everywhere) == 0, "waves: the same game time, the same bytes");
    const std::size_t moved = differing(first, later, everywhere);
    tally->check(later.read && moved > kLeastJudged / 2, text::format("waves: half a second later the water moved (%zu pixels)", moved));
    const std::size_t waved = differing(first, calm, everywhere);
    tally->check(calm.read && waved > kLeastJudged / 2, text::format("waves: the waves change the calm water (%zu pixels)", waved));
    tally->check(all_finite(first) && all_finite(later), "waves (alpha blended): every pixel finite");
    tally->check(differing(first, calm, sand_pixel) == 0, "waves: the sand in front of the water untouched");
}

void check_glitter(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    // the sun along the mirror direction of the picture's centre
    const Ray centre = ray_of(kWidth / 2, kHeight / 2);
    const Vec3 toward = normalise({core::at(centre.direction, 0), -core::at(centre.direction, 1), core::at(centre.direction, 2)});
    render::rt::set_sun_override(&toward);
    set_water(true, 0.0F, 0.0F);
    draw_frame(gpu, scene, 1.0, true, false);
    const Picture sunny = read_picture(scene.colour, kWidth, kHeight);
    render::rt::set_sun_override(nullptr);
    draw_frame(gpu, scene, 1.0, true, false);
    const Picture plain = read_picture(scene.colour, kWidth, kHeight);
    float brightest = 0.0F;
    for (const Vec3& c : sunny.rgb) {
        brightest = std::max(brightest, std::max({c[0], c[1], c[2]}));
    }
    const auto at_centre = [](uint32_t px, uint32_t py) {
        return std::abs(static_cast<int>(px) - static_cast<int>(kWidth / 2)) <= 3 && std::abs(static_cast<int>(py) - static_cast<int>(kHeight / 2)) <= 3;
    };
    const auto away = [&at_centre](uint32_t px, uint32_t py) { return !at_centre(px, py) && sand_pixel(px, py); };
    tally->check(sunny.read && brightest > 1.0F && all_finite(sunny), text::format("the sun's glitter: above 1 with HDR (%.2f at most)", static_cast<double>(brightest)));
    tally->check(plain.read && differing(sunny, plain, at_centre) > 0, "the sun's glitter: at the mirror direction");
    tally->check(differing(sunny, plain, away) == 0, "the sun's glitter: none on the sand");
}

void check_waves_texture(Tally* tally)
{
    vk::Backend& state = vk::backend();
    for (const uint32_t level : {0U, 4U}) {
        std::vector<uint16_t> halves;
        vk::Extent size;
        bool read = false;
        {
            const std::scoped_lock lock(state.mutex);
            read = vk::finish_work(state) && vk::read_water_waves(state, level, &halves, &size);
        }
        const std::vector<std::array<float, 4>> want = vk::water_waves_reference(level);
        float worst = 0.0F;
        bool finite = true;
        for (std::size_t texel = 0; read && texel < want.size() && (texel * 4) + 3 < halves.size(); ++texel) {
            for (std::size_t c = 0; c < 4; ++c) {
                const float have = from_half(core::at(halves, (texel * 4) + c));
                finite = finite && std::isfinite(have);
                worst = std::max(worst, std::fabs(have - core::at(core::at(want, texel), c)));
            }
        }
        constexpr float kTextureTolerance = 4.0e-3F;
        tally->check(read && halves.size() == want.size() * 4 && finite && worst <= kTextureTolerance,
                     text::format("the ripple texture's level %u (%ux%u) against the processor: the worst %.5f", level, size.width,
                                  size.height, static_cast<double>(worst)));
    }
}

// --perf: frames of full-screen water at 2560 x 1440 (the sand, then the water - its copy and shading) with rt_water off
// and on - the wall time per frame once the device has finished (not a check).
void measure_cost(render::Gpu& gpu, Scene scene)
{
    constexpr uint32_t kWide = 2560;
    constexpr uint32_t kTall = 1440;
    constexpr int kFrames = 60;
    constexpr double kMilliseconds = 1000.0;
    scene.width = kWide;
    scene.height = kTall;
    if (gpu.create_render_target({.width = kWide, .height = kTall, .format = value::kFormatA16B16G16R16F, .samples = 0, .quality = 0,
                                  .lockable_or_discard = true},
                                 &scene.colour) != value::kResultOk ||
        gpu.create_depth_surface({.width = kWide, .height = kTall, .format = value::kFormatD24S8, .samples = 0, .quality = 0,
                                  .lockable_or_discard = false},
                                 &scene.depth) != value::kResultOk) {
        return;
    }
    for (const bool on : {false, true}) {
        set_water(on, 1.0F, 1.0F);
        double seconds = 0.0;
        for (int pass = 0; pass < 2; ++pass) { // (the first warms the pipelines)
            const auto start = std::chrono::steady_clock::now();
            for (int frame = 0; frame < kFrames; ++frame) {
                draw_frame(gpu, scene, frame / 60.0, true, true);
            }
            std::vector<uint8_t> bytes;
            (void)vk::read_surface(scene.colour, &bytes);
            seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }
        text::print("  cost: rt_water %s - %.3f ms a frame (%d frames at %ux%u: the sand and the water, full screen)\n", on ? "on" : "off",
                    seconds * kMilliseconds / kFrames, kFrames, kWide, kTall);
    }
    gpu.release_surface(scene.colour);
    gpu.release_surface(scene.depth);
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
    vk::set_context_options({.validation = options.validation, .device_index = options.device, .on_message = print_line}, window);
    render::GpuContext& context = vk::gpu_context();
    if (!context.create_instance()) {
        text::print("the device could not be opened\n");
        return false;
    }
    text::print("validation layer: %s\n", vk::backend().context.validation_on() ? "on" : "off (not installed, or not asked for)");
    context.configure({.width = kWindowSize,
                       .height = kWindowSize,
                       .windowed = true,
                       .vsync = true,
                       .samples = 0,
                       .sample_quality = 0,
                       .colour_format = value::kFormatX8R8G8B8,
                       .depth_format = value::kFormatD24S8});
    const long made = context.create_device({.width = kWindowSize, .height = kWindowSize, .hardware_vertices = true, .samples = 0, .sample_quality = 0});
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
    Scene scene;
    constexpr uint8_t kOpaqueHalf = 128; // (the doubled alpha: 1)
    const bool made =
        gpu.create_pixel_shader(kShader.data(), static_cast<uint32_t>(kShader.size()), &scene.shader) == value::kResultOk &&
        gpu.create_render_target({.width = kWidth, .height = kHeight, .format = value::kFormatA16B16G16R16F, .samples = 0, .quality = 0,
                                  .lockable_or_discard = true},
                                 &scene.colour) == value::kResultOk &&
        gpu.create_depth_surface({.width = kWidth, .height = kHeight, .format = value::kFormatD24S8, .samples = 0, .quality = 0,
                                  .lockable_or_discard = false},
                                 &scene.depth) == value::kResultOk;
    scene.sand = uniform_texture(gpu, kSand, kOpaqueHalf);
    scene.water = uniform_texture(gpu, kTint, kOpaqueHalf);
    tally.check(made && scene.sand != nullptr && scene.water != nullptr, "the material shader, a 16-bit float target, a depth surface and the textures made");
    if (made) {
        render::water::set_level_water({.level = 1, .waves = 1.0F, .swell_length = 300.0F, .visibility = kVisibility, .heading = 30.0F});
        check_off(gpu, scene, &tally);
        check_calm(gpu, scene, &tally);
        check_foam(gpu, scene, &tally);
        check_waves(gpu, scene, &tally);
        check_glitter(gpu, scene, &tally);
        check_waves_texture(&tally);
        const vk::WaterStats stats = vk::water_stats();
        tally.check(stats.draws > 0 && stats.copies > 0, text::format("water draws %llu, copies %llu", static_cast<unsigned long long>(stats.draws),
                                                                         static_cast<unsigned long long>(stats.copies)));
        if (options.perf) {
            measure_cost(gpu, scene);
        }
    }
    vk::set_water_settings({});
    for (GpuTexture* texture : {scene.sand, scene.water}) {
        if (texture != nullptr) {
            gpu.release_texture(texture);
        }
    }
    for (GpuSurface* surface : {scene.colour, scene.depth}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
    if (scene.shader != nullptr) {
        gpu.release_pixel_shader(scene.shader);
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
    text::print("thug2_vulkan_water_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan water check", kWindowSize, kWindowSize, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
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
