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
//   - the reflection: a red wall standing on the calm water - the water mirroring it (found on the screen) where its
//     mirror image is on the screen, the sky elsewhere, against the processor;
//   - opaque water kept back and drawn after the opaque world (the calm cases are opaque draws);
//   - rt_water=0, and rt_water=1 with the draw not marked: the original's picture, byte for byte (the water the
//     program's colour); the floor byte for byte in every case;
//   - foam: changes only where the water is shallow (within 24 units of the floor below), and brightens it;
//   - waves (rt_water_waves=1, wind 1): the picture moves with the game's clock, the same clock draws the same bytes;
//   - the sun's glitter (the sun given by rt::set_sun_override toward the mirror direction of the picture's centre):
//     above 1 near the centre with HDR, nothing outside the water;
//   - the ripple texture's levels 0 and 4 read back against water_waves_reference;
//   - the moving surface ([Graphics] rt_water_geometry): the sea split as render_pc splits a water mesh
//     (engine/gfx/water_tess.h) and drawn as a split mesh's draw (water_surfaces.h set_current_waves) - it moves with the
//     game's clock, the same clock draws the same bytes (opaque, kept back and drawn after the opaque world, and blended),
//     it isn't the flat sheet's picture; calm water (rt_water_waves=0) and rt_water_geometry=0 draw the flat sheet's
//     bytes, rt_water=0 the original's; its shape: a patch with high waves and nothing behind it covers the pixels the
//     same grid moved on the processor covers (the vertex shader's numbers), within the rasteriser's ties on its edges;
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
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
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
#include "engine/gfx/smooth_tess.h"
#include "engine/gfx/water_surfaces.h"
#include "engine/gfx/water_tess.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_formats.h"
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
constexpr Vec3 kWallColour{0.85F, 0.15F, 0.1F};
constexpr float kWallZ = 150.0F;  // (the reflection's case: a wall standing on the water there, up to kWallTop)
constexpr float kWallTop = 80.0F;
constexpr float kVisibility = 450.0F;
constexpr float kShoreFoam = 24.0F;     // (the shader's foam depth)
constexpr float kTolerance = 4.0e-3F;   // a pixel's largest difference (16-bit floats, interpolated depths)
constexpr float kWorstTolerance = 0.03F;
constexpr double kShareWithin = 0.99;
constexpr std::size_t kLeastJudged = 20000;
constexpr float kEdgeDepth = 0.6F;      // (pixels this near the shoreline in depth left out: the depth's rasterisation)
constexpr float kFarMargin = 50.0F;     // (the water's far edge left out)
constexpr float kInFront = 0.5F;        // (the sand nearer than the water by this much: in front of it)
constexpr float kHeading = 30.0F;       // the wind's (degrees)
constexpr float kSwellLength = 300.0F;
constexpr float kUvTile = 64.0F;        // (units a texture repeat)
constexpr uint32_t kBlackOpaque = 0xFF000000U;
constexpr uint32_t kWaterMaterial = 0x57A7E400U; // (a made-up checksum for the log)
constexpr std::size_t kQuadCorners = 6;
constexpr std::size_t kMatrixFloats = 16;
constexpr std::size_t kColourBytes = 6; // (a half-float pixel's red, green, blue)
constexpr float kSameLevel = 1.0e-3F;   // (luma or channel differences below this: unchanged)
constexpr float kBrighter = 0.02F;
constexpr std::size_t kLeastWallPixels = 300;
constexpr std::size_t kLeastShallow = 100;
constexpr double kFoamSeconds = 3.0;
constexpr double kWaveSeconds = 10.0;
constexpr double kWaveLater = 10.5;
constexpr double kFramesASecond = 60.0;
constexpr float kOffScreen = 1.1F;      // (normalised device coordinates past the screen's edge)
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

Vec3 add(const Vec3& a, const Vec3& b)
{
    return {core::at(a, 0) + core::at(b, 0), core::at(a, 1) + core::at(b, 1), core::at(a, 2) + core::at(b, 2)};
}
Vec3 sub(const Vec3& a, const Vec3& b)
{
    return {core::at(a, 0) - core::at(b, 0), core::at(a, 1) - core::at(b, 1), core::at(a, 2) - core::at(b, 2)};
}
Vec3 scale(const Vec3& a, float by) { return {core::at(a, 0) * by, core::at(a, 1) * by, core::at(a, 2) * by}; }
float dot(const Vec3& a, const Vec3& b)
{
    return (core::at(a, 0) * core::at(b, 0)) + (core::at(a, 1) * core::at(b, 1)) + (core::at(a, 2) * core::at(b, 2));
}
Vec3 normalise(const Vec3& a)
{
    const float length = std::sqrt(dot(a, a));
    return length > 0.0F ? scale(a, 1.0F / length) : a;
}
float as_byte(float value) { return std::round(std::clamp(value, 0.0F, 1.0F) * kByteMax) / kByteMax; }
Vec3 as_bytes(const Vec3& a) { return {as_byte(core::at(a, 0)), as_byte(core::at(a, 1)), as_byte(core::at(a, 2))}; }

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
std::array<float, kMatrixFloats> flat(const Matrix& matrix)
{
    std::array<float, kMatrixFloats> out{};
    for (std::size_t i = 0; i < kMatrixFloats; ++i) {
        core::at(out, i) = core::at(core::at(matrix, i / 4), i % 4);
    }
    return out;
}

struct ViewSize {
    uint32_t width = kWidth;
    uint32_t height = kHeight;
};

render::rt::ViewDrawn view_of(ViewSize size, double seconds)
{
    const uint32_t width = size.width;
    const uint32_t height = size.height;
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
std::array<Vertex, kQuadCorners> quad(bool floor)
{
    const auto at = [floor](float x, float z) {
        return Vertex{.position = {x, floor ? floor_height(z) : 0.0F, z}, .colour = kHalfGrey, .uv = {x / kUvTile, z / kUvTile}};
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
    GpuTexture* wall = nullptr; // (the reflection's case: a red wall standing on the water)
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

// (the moving surface's cases) The water drawn as these triangles (a split mesh) in place of the quad, its draw a split
// mesh's (its surface may move) when `moving`; without the sand.
struct Extra {
    const std::vector<Vertex>* grid = nullptr;
    bool moving = false;
    bool sand = true;
};

// One frame of the shore: the view begun (the listeners told), the sand, the water (marked unless `marked` false, alpha
// blended when `blended`), the view's world drawn.
void draw_frame(render::Gpu& gpu, const Scene& scene, double seconds, bool marked, bool blended, int layers = 1,
                bool wall = false, const Extra& extra = {})
{
    const render::rt::ViewDrawn view = view_of({.width = scene.width, .height = scene.height}, seconds);
    gpu.begin_scene();
    gpu.set_render_target(0, scene.colour);
    gpu.set_depth_surface(scene.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = scene.width, .height = scene.height, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth, kBlackOpaque, 1.0F, 0);
    render::rt::view_begins(view);
    common_states(gpu, scene);
    gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
    gpu.set_texture(0, scene.sand);
    render::water::set_current(nullptr, 0);
    const std::array<Vertex, kQuadCorners> sand = quad(true);
    if (extra.sand) {
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, 2, sand.data(), sizeof(Vertex));
    }
    if (wall) {
        gpu.set_texture(0, scene.wall);
        const auto at = [](float x, float y) {
            return Vertex{.position = {x, y, kWallZ}, .colour = kHalfGrey, .uv = {x / kUvTile, y / kUvTile}};
        };
        const std::array<Vertex, kQuadCorners> quad_of_wall{at(-kSide, 0.0F), at(kSide, 0.0F), at(-kSide, kWallTop), at(kSide, 0.0F),
                                                 at(kSide, kWallTop), at(-kSide, kWallTop)};
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, 2, quad_of_wall.data(), sizeof(Vertex));
    }
    for (int layer = 0; layer < layers; ++layer) {
        gpu.set_render_state(GpuRenderState::kZWriteEnable, 0);
        gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, blended ? 1 : 0);
        gpu.set_render_state(GpuRenderState::kSrcBlend, value::kBlendSrcAlpha);
        gpu.set_render_state(GpuRenderState::kDestBlend, value::kBlendInvSrcAlpha);
        gpu.set_texture(0, scene.water);
        render::water::set_current(marked ? scene.water : nullptr, kWaterMaterial);
        render::water::set_current_waves(extra.moving);
        if (extra.grid != nullptr) {
            gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(extra.grid->size() / 3), extra.grid->data(),
                                 sizeof(Vertex));
        } else {
            const std::array<Vertex, kQuadCorners> water = quad(false);
            gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, 2, water.data(), sizeof(Vertex));
        }
        render::water::set_current_waves(false);
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

// Where camera-space `point` is in normalised device coordinates (false: behind the camera).
bool on_screen(const Vec3& point, std::array<float, 2>* ndc)
{
    const Axes axes = camera_axes();
    const Vec3 relative = sub(point, kCamera);
    const float ahead = dot(relative, axes.forward);
    if (ahead <= 0.0F) {
        return false;
    }
    const float aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
    *ndc = {dot(relative, axes.right) * kVerticalScale / aspect / ahead, dot(relative, axes.up) * kVerticalScale / ahead};
    return true;
}

// What the mirror ray from the water at `point` along `reflected` shows with the wall standing: 1 the wall (clearly -
// on the screen away from its edges), 0 the sky (clearly not near the wall), -1 neither clearly.
int mirror_shows_wall(const Vec3& point, const Vec3& reflected)
{
    constexpr float kMargin = 3.0F;    // (units of the wall's height)
    constexpr float kInside = 0.78F;   // (of the screen: the reflection's fade toward its edges, from 0.8, left out)
    if (core::at(reflected, 2) <= 0.0F) {
        return 0;
    }
    const Vec3 hit = add(point, scale(reflected, (kWallZ - core::at(point, 2)) / core::at(reflected, 2)));
    std::array<float, 2> ndc{};
    const bool seen = on_screen(hit, &ndc) && std::fabs(core::at(ndc, 0)) < 1.0F && std::fabs(core::at(ndc, 1)) < 1.0F;
    if (core::at(hit, 1) > kWallTop + kMargin || core::at(hit, 1) < -kMargin || !seen) {
        // the wall's points the camera doesn't see can't be found on the screen: the sky (unless near the screen's
        // edge, where the march may end on the wall's last rows)
        return seen || std::fabs(core::at(ndc, 1)) > kOffScreen || core::at(hit, 1) > kWallTop + kMargin ? 0 : -1;
    }
    if (core::at(hit, 1) < kMargin || std::fabs(core::at(ndc, 0)) > kInside || std::fabs(core::at(ndc, 1)) > kInside) {
        return -1;
    }
    return 1;
}

// The calm water's colour at a pixel (rt_water_waves=0, no foam, no sun; with the wall: what it reflects of it): false
// where the pixel isn't judged (the floor or the wall in front, the shoreline's edge, off the quads).
bool expected_calm(uint32_t px, uint32_t py, Vec3* out, float* depth_down, bool wall = false)
{
    const Ray ray = ray_of(px, py);
    if (!std::isfinite(ray.to_water) || !std::isfinite(ray.to_floor) || ray.to_floor < ray.to_water) {
        return false;
    }
    const Vec3 point = add(kCamera, scale(ray.direction, ray.to_water));
    int shows_wall = 0;
    if (wall) {
        if (core::at(point, 2) > kWallZ - 1.0F) {
            return false; // (behind the wall)
        }
        const Vec3 reflected{core::at(ray.direction, 0), -core::at(ray.direction, 1), core::at(ray.direction, 2)};
        shows_wall = mirror_shows_wall(point, reflected);
        if (shows_wall < 0) {
            return false;
        }
    }
    if (core::at(point, 2) < kShoreStart + 1.0F || core::at(point, 2) > kFloorFar - kFarMargin) {
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
    const Vec3 deep_hue{core::at(hue, 0) * core::at(hue, 0), core::at(hue, 1) * core::at(hue, 1), core::at(hue, 2) * core::at(hue, 2)};
    const Vec3 deep = scale(deep_hue, light * 0.65F / dot(deep_hue, kLuma));
    const Vec3 sand = as_bytes(kSand);
    const Vec3 reflected = shows_wall == 1 ? scale(as_bytes(kWallColour), kProgramGain) : kFog;
    const float fresnel = 0.02F + (0.98F * std::pow(1.0F - core::at(v, 1), 5.0F));
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const float sigma = (3.0F / kVisibility) * (0.2F + (1.6F * (1.0F - core::at(hue, channel))));
        const float through = std::exp(-sigma * path);
        const float below = std::exp(-sigma * *depth_down);
        const float refracted = (core::at(sand, channel) * through * below) + (core::at(deep, channel) * (1.0F - through));
        out->at(channel) = std::lerp(refracted, core::at(reflected, channel), fresnel);
    }
    return true;
}

bool sand_pixel(uint32_t px, uint32_t py)
{
    const Ray ray = ray_of(px, py);
    return std::isfinite(ray.to_floor) && (ray.to_floor + kInFront < ray.to_water || !std::isfinite(ray.to_water));
}

bool all_finite(const Picture& picture)
{
    return std::ranges::all_of(picture.rgb, [](const Vec3& colour) {
        return std::ranges::all_of(colour, [](float channel) { return std::isfinite(channel); });
    });
}

void set_water(bool on, float waves, float foam, bool hdr = true)
{
    vk::set_water_settings({.on = on, .waves = waves, .wind = 1.0F, .heading = kHeading, .clarity = 1.0F, .foam = foam, .hdr = hdr});
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
            for (std::size_t channel = 0; channel < 3; ++channel) {
                difference = std::max(difference, std::fabs(have.at(channel) - want.at(channel)));
            }
            ++judged;
            within += difference <= kTolerance ? 1U : 0U;
            if (!(difference <= worst)) {
                worst = std::isfinite(difference) ? difference : std::numeric_limits<float>::infinity();
                worst_at = text::format(" (at %u, %u, %.1f deep: %.4f %.4f %.4f, expected %.4f %.4f %.4f)", px, py,
                                        static_cast<double>(down), static_cast<double>(core::at(have, 0)), static_cast<double>(core::at(have, 1)),
                                        static_cast<double>(core::at(have, 2)), static_cast<double>(core::at(want, 0)), static_cast<double>(core::at(want, 1)),
                                        static_cast<double>(core::at(want, 2)));
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
            for (std::size_t k = 0; k < kColourBytes; ++k) {
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
            original += std::fabs(core::at(have, 0) - (core::at(tint, 0) * kProgramGain)) < kSameLevel &&
                                std::fabs(core::at(have, 1) - (core::at(tint, 1) * kProgramGain)) < kSameLevel &&
                                std::fabs(core::at(have, 2) - (core::at(tint, 2) * kProgramGain)) < kSameLevel
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

// The reflection: a red wall standing on calm water - the water in front of it mirrors it (found on the screen) where
// its mirror image is on the screen, the sky elsewhere.
void check_reflection(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    set_water(true, 0.0F, 0.0F);
    draw_frame(gpu, scene, 1.0, true, false, 1, true);
    const Picture got = read_picture(scene.colour, kWidth, kHeight);
    std::size_t walls = 0;
    std::size_t skies = 0;
    std::size_t within = 0;
    float worst = 0.0F;
    std::string worst_at;
    for (uint32_t py = 0; py < kHeight; ++py) {
        for (uint32_t px = 0; px < kWidth; ++px) {
            Vec3 want{};
            float down = 0.0F;
            if (!got.read || !expected_calm(px, py, &want, &down, true)) {
                continue;
            }
            const Ray ray = ray_of(px, py);
            const Vec3 point = add(kCamera, scale(ray.direction, ray.to_water));
            const bool wall = mirror_shows_wall(point, {core::at(ray.direction, 0), -core::at(ray.direction, 1), core::at(ray.direction, 2)}) == 1;
            (wall ? walls : skies) += 1;
            const Vec3& have = core::at(got.rgb, (static_cast<std::size_t>(py) * kWidth) + px);
            float difference = 0.0F;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                difference = std::max(difference, std::fabs(have.at(channel) - want.at(channel)));
            }
            constexpr float kReflectionTolerance = 0.01F;
            within += difference <= kReflectionTolerance ? 1U : 0U;
            if (difference > worst) {
                worst = difference;
                worst_at = text::format(" (at %u, %u%s: %.3f %.3f %.3f, expected %.3f %.3f %.3f)", px, py, wall ? ", the wall" : "",
                                        static_cast<double>(core::at(have, 0)), static_cast<double>(core::at(have, 1)), static_cast<double>(core::at(have, 2)),
                                        static_cast<double>(core::at(want, 0)), static_cast<double>(core::at(want, 1)), static_cast<double>(core::at(want, 2)));
            }
        }
    }
    const std::size_t judged = walls + skies;
    const double share = judged != 0 ? static_cast<double>(within) / static_cast<double>(judged) : 0.0;
    constexpr double kReflectionShare = 0.98;
    tally->check(walls > kLeastWallPixels && share >= kReflectionShare,
                 text::format("reflection: %zu pixels mirroring the wall, %zu the sky, %.2f%% within 0.01 of the processor, "
                              "the worst %.3f%s",
                              walls, skies, share * 100.0, static_cast<double>(worst), worst_at.c_str()));
}

void check_foam(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    set_water(true, 0.0F, 0.0F);
    draw_frame(gpu, scene, kFoamSeconds, true, false);
    const Picture without = read_picture(scene.colour, kWidth, kHeight);
    set_water(true, 0.0F, 1.0F);
    draw_frame(gpu, scene, kFoamSeconds, true, false);
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
                deep_changed += std::fabs(after - before) > kSameLevel ? 1U : 0U;
            } else if (down < kShoreFoam * kInFront) {
                ++shallow;
                shallow_brighter += after > before + kBrighter ? 1U : 0U;
            }
        }
    }
    tally->check(without.read && with.read && deep_changed == 0, text::format("foam: no change where the water is deeper than %.0f units (%zu changed)", static_cast<double>(kShoreFoam), deep_changed));
    tally->check(shallow > kLeastShallow && shallow_brighter * 4 > shallow,
                 text::format("foam: the shallow water brightened (%zu of %zu pixels)", shallow_brighter, shallow));
}

void check_waves(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    set_water(true, 1.0F, 1.0F);
    draw_frame(gpu, scene, kWaveSeconds, true, true);
    const Picture first = read_picture(scene.colour, kWidth, kHeight);
    draw_frame(gpu, scene, kWaveSeconds, true, true);
    const Picture again = read_picture(scene.colour, kWidth, kHeight);
    draw_frame(gpu, scene, kWaveLater, true, true);
    const Picture later = read_picture(scene.colour, kWidth, kHeight);
    set_water(true, 0.0F, 1.0F);
    draw_frame(gpu, scene, kWaveSeconds, true, true);
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
    for (const Vec3& colour : sunny.rgb) {
        brightest = std::max({brightest, core::at(colour, 0), core::at(colour, 1), core::at(colour, 2)});
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
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const float have = from_half(core::at(halves, (texel * 4) + channel));
                finite = finite && std::isfinite(have);
                worst = std::max(worst, std::fabs(have - core::at(core::at(want, texel), channel)));
            }
        }
        constexpr float kTextureTolerance = 4.0e-3F;
        tally->check(read && halves.size() == want.size() * 4 && finite && worst <= kTextureTolerance,
                     text::format("the ripple texture's level %u (%ux%u) against the processor: the worst %.5f", level, size.width,
                                  size.height, static_cast<double>(worst)));
    }
}

// --- the moving surface (rt_water_geometry) -------------------------------------------------------------------------

// The water quad x0 - x1, z0 - z1 at y = 0 split as render_pc splits a water mesh (water_tess.h): its triangles' corners
// in drawing order (empty: not split).
std::vector<Vertex> split_quad(float x0, float x1, float z0, float z1)
{
    const auto at = [](float x, float z) { return Vertex{.position = {x, 0.0F, z}, .colour = kHalfGrey, .uv = {x / kUvTile, z / kUvTile}}; };
    const std::array<Vertex, 4> corners{at(x0, z0), at(x1, z0), at(x0, z1), at(x1, z1)};
    constexpr std::array<uint16_t, 4> kStrip{0, 1, 2, 3};
    std::vector<uint8_t> bytes(sizeof(corners));
    std::memcpy(bytes.data(), corners.data(), sizeof(corners));
    const render::tess::VertexLayout layout{.stride = sizeof(Vertex),
                                            .normal_offset = -1,
                                            .colour_offset = static_cast<int>(offsetof(Vertex, colour)),
                                            .uv_offset = static_cast<int>(offsetof(Vertex, uv)),
                                            .uv_sets = 1};
    const render::water_tess::Result split =
        render::water_tess::subdivide(bytes, static_cast<uint32_t>(corners.size()), layout, kStrip, render::water_tess::Options{});
    std::vector<Vertex> out;
    if (!split.changed) {
        return out;
    }
    std::vector<Vertex> vertices(split.vertex_count);
    std::memcpy(vertices.data(), split.vertices.data(), vertices.size() * sizeof(Vertex));
    for (const auto& triangle : render::tess::strip_triangles(split.strip)) {
        for (const uint16_t index : triangle) {
            out.push_back(core::at(vertices, index));
        }
    }
    return out;
}

float smooth_step(float low, float high, float value)
{
    const float t = std::clamp((value - low) / (high - low), 0.0F, 1.0F);
    return t * t * (3.0F - (2.0F * t));
}

// The moved water's point on the processor: the vertex shader's water_lift (water_shader.cpp) with the view's block -
// nothing behind the water (the shore's weight 1).
Vec3 lifted(const vk::WaterUniforms& block, const Vec3& rest)
{
    constexpr float kTwoPi = 6.2831853F;
    constexpr float kGravity = 386.0F;
    constexpr float kSharpness = 0.6F;
    constexpr float kCell = 40.0F;
    constexpr float kMostSlope = 1.0F;
    constexpr std::array<float, 4> kLengths{1.0F, 0.63F, 0.41F, 0.27F};
    constexpr std::array<float, 4> kTurns{0.0F, 0.42F, -0.57F, 1.05F};
    constexpr std::array<float, 4> kSteepness{0.12F, 0.10F, 0.085F, 0.07F};
    constexpr float kPhaseStep = 1.913F;
    constexpr std::size_t kProjectionYY = 5;
    const Vec3 camera{core::at(block.camera, 0), core::at(block.camera, 1), core::at(block.camera, 2)};
    const Vec3 to_eye = sub(camera, rest);
    const float dist = std::max(std::sqrt(dot(to_eye, to_eye)), 1.0e-3F);
    const float focal = std::max(std::fabs(core::at(block.projection, kProjectionYY)) * core::at(block.viewport, 3) * 0.5F, 1.0F);
    const float footprint = dist / (focal * std::max(std::fabs(core::at(to_eye, 1)) / dist, 0.1F));
    const float raised = core::at(block.waves, 0) * (0.4F + (0.6F * core::at(block.time, 3)));
    const float wind_x = core::at(block.time, 1);
    const float wind_z = core::at(block.time, 2);
    const float seconds = core::at(block.time, 0);
    float slopes = 0.0F;
    Vec3 sum{};
    for (std::size_t i = 0; i < kLengths.size(); ++i) {
        const float k = kTwoPi / (core::at(kLengths, i) * core::at(block.misc, 1));
        const float wavelength = kTwoPi / k;
        const float speed = std::sqrt(kGravity / k);
        const float c = std::cos(core::at(kTurns, i));
        const float s = std::sin(core::at(kTurns, i));
        const float dx = (wind_x * c) - (wind_z * s);
        const float dz = (wind_x * s) + (wind_z * c);
        const float phase = (k * ((dx * core::at(rest, 0)) + (dz * core::at(rest, 2)) - (speed * seconds))) + (static_cast<float>(i) * kPhaseStep);
        const float slope = core::at(kSteepness, i) * raised * (1.0F - smooth_step(0.05F, 0.2F, footprint / wavelength)) *
                            (1.0F - smooth_step(0.25F, 0.5F, kCell / wavelength));
        slopes += slope;
        const float height = slope / k;
        const float across = kSharpness * height * std::cos(phase);
        sum = add(sum, {dx * across, height * std::sin(phase), dz * across});
    }
    return add(rest, scale(sum, std::min(1.0F, kMostSlope / std::max(slopes, 1.0e-6F))));
}

// The pixels the triangles cover on the processor (moved by `block` when given): each pixel's centre - the backend's
// half pixel: Direct3D 9's centres - inside a triangle.
std::vector<uint8_t> covered(const std::vector<Vertex>& triangles, const vk::WaterUniforms* block)
{
    std::vector<uint8_t> mask(static_cast<std::size_t>(kWidth) * kHeight, 0);
    for (std::size_t first = 0; first + 2 < triangles.size(); first += 3) {
        std::array<std::array<float, 2>, 3> screen{};
        bool seen = true;
        for (std::size_t k = 0; k < 3; ++k) {
            const auto& position = core::at(triangles, first + k).position;
            Vec3 point{core::at(position, 0), core::at(position, 1), core::at(position, 2)};
            if (block != nullptr) {
                point = lifted(*block, point);
            }
            std::array<float, 2> ndc{};
            seen = seen && on_screen(point, &ndc);
            core::at(screen, k) = {(core::at(ndc, 0) + 1.0F) * 0.5F * static_cast<float>(kWidth),
                                   (1.0F - core::at(ndc, 1)) * 0.5F * static_cast<float>(kHeight)};
        }
        if (!seen) {
            continue;
        }
        const auto edge = [](const std::array<float, 2>& a, const std::array<float, 2>& b, float x, float y) {
            return ((core::at(b, 0) - core::at(a, 0)) * (y - core::at(a, 1))) - ((core::at(b, 1) - core::at(a, 1)) * (x - core::at(a, 0)));
        };
        float low_x = core::at(core::at(screen, 0), 0);
        float high_x = low_x;
        float low_y = core::at(core::at(screen, 0), 1);
        float high_y = low_y;
        for (const auto& corner : screen) {
            low_x = std::min(low_x, core::at(corner, 0));
            high_x = std::max(high_x, core::at(corner, 0));
            low_y = std::min(low_y, core::at(corner, 1));
            high_y = std::max(high_y, core::at(corner, 1));
        }
        const int x0 = std::max(0, static_cast<int>(std::floor(low_x)));
        const int x1 = std::min(static_cast<int>(kWidth) - 1, static_cast<int>(std::ceil(high_x)));
        const int y0 = std::max(0, static_cast<int>(std::floor(low_y)));
        const int y1 = std::min(static_cast<int>(kHeight) - 1, static_cast<int>(std::ceil(high_y)));
        for (int py = y0; py <= y1; ++py) {
            for (int px = x0; px <= x1; ++px) {
                const auto x = static_cast<float>(px);
                const auto y = static_cast<float>(py);
                const float e0 = edge(core::at(screen, 0), core::at(screen, 1), x, y);
                const float e1 = edge(core::at(screen, 1), core::at(screen, 2), x, y);
                const float e2 = edge(core::at(screen, 2), core::at(screen, 0), x, y);
                if ((e0 >= 0.0F && e1 >= 0.0F && e2 >= 0.0F) || (e0 <= 0.0F && e1 <= 0.0F && e2 <= 0.0F)) {
                    core::at(mask, (static_cast<std::size_t>(py) * kWidth) + static_cast<std::size_t>(px)) = 1;
                }
            }
        }
    }
    return mask;
}

std::size_t mask_difference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        count += core::at(a, i) != core::at(b, i) ? 1U : 0U;
    }
    return count;
}

// The moving surface on the shore (the sea split as render_pc splits it, the draw marked as a split mesh's): it moves
// with the clock, the same clock draws the same bytes (opaque: kept back and drawn after the opaque world - and blended),
// calm water and rt_water_geometry=0 draw the flat sheet to the bit, rt_water=0 the original's.
void check_moving(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    const std::vector<Vertex> sea = split_quad(-kSide, kSide, kShoreStart, kFloorFar);
    tally->check(sea.size() > kQuadCorners * 100, text::format("the moving surface: the sea split into %zu triangles", sea.size() / 3));
    if (sea.empty()) {
        return;
    }
    const Extra flat{.grid = &sea, .moving = false};
    const Extra moving{.grid = &sea, .moving = true};
    const auto everywhere = [](uint32_t, uint32_t) { return true; };
    const uint64_t moving_before = vk::water_stats().moving;
    set_water(true, 1.0F, 1.0F);
    Picture opaque_moved;
    Picture opaque_flat;
    for (const bool blended : {false, true}) {
        const char* how = blended ? "blended" : "opaque";
        draw_frame(gpu, scene, kWaveSeconds, true, blended, 1, false, moving);
        const Picture first = read_picture(scene.colour, kWidth, kHeight);
        draw_frame(gpu, scene, kWaveSeconds, true, blended, 1, false, moving);
        const Picture again = read_picture(scene.colour, kWidth, kHeight);
        draw_frame(gpu, scene, kWaveLater, true, blended, 1, false, moving);
        const Picture later = read_picture(scene.colour, kWidth, kHeight);
        draw_frame(gpu, scene, kWaveSeconds, true, blended, 1, false, flat);
        const Picture still = read_picture(scene.colour, kWidth, kHeight);
        tally->check(first.read && again.read && differing(first, again, everywhere) == 0,
                     text::format("the moving surface (%s): the same game time, the same bytes", how));
        const std::size_t moved = differing(first, later, everywhere);
        tally->check(later.read && moved > kLeastJudged / 2, text::format("the moving surface (%s): half a second later it moved (%zu pixels)", how, moved));
        const std::size_t shaped = differing(first, still, everywhere);
        tally->check(still.read && shaped > kLeastJudged / 4,
                     text::format("the moving surface (%s): not the flat sheet's picture (%zu pixels differ)", how, shaped));
        tally->check(all_finite(first) && all_finite(later), text::format("the moving surface (%s): every pixel finite", how));
        if (!blended) {
            opaque_moved = first;
            opaque_flat = still;
        }
    }
    // the shore: the water still where it meets the sand (its move fades out where the water below it is shallow) - the
    // sand in front of the flat water hardly covered by the moved water (without the fade about a hundred pixels are)
    std::size_t sand = 0;
    for (uint32_t py = 0; py < kHeight; ++py) {
        for (uint32_t px = 0; px < kWidth; ++px) {
            sand += sand_pixel(px, py) ? 1U : 0U;
        }
    }
    const std::size_t sand_covered = differing(opaque_moved, opaque_flat, sand_pixel);
    constexpr std::size_t kMostSandCovered = 20;
    tally->check(opaque_moved.read && opaque_flat.read && sand > kLeastShallow && sand_covered <= kMostSandCovered,
                 text::format("the moving surface: the waterline stays on the sand (%zu of %zu sand pixels changed)", sand_covered, sand));
    tally->check(vk::water_stats().moving > moving_before, "the moving surface: its draws drawn moving");
    // calm (rt_water_waves=0): the flat sheet, to the bit
    set_water(true, 0.0F, 1.0F);
    draw_frame(gpu, scene, kWaveSeconds, true, false, 1, false, moving);
    const Picture calm_marked = read_picture(scene.colour, kWidth, kHeight);
    draw_frame(gpu, scene, kWaveSeconds, true, false, 1, false, flat);
    const Picture calm_flat = read_picture(scene.colour, kWidth, kHeight);
    tally->check(calm_marked.read && calm_flat.read && differing(calm_marked, calm_flat, everywhere) == 0,
                 "the moving surface: calm water (rt_water_waves=0) the flat sheet's bytes");
    // rt_water_geometry=0: the flat sheet with its waves only in the shading, to the bit
    vk::WaterSettings without = vk::water_settings();
    without.waves = 1.0F;
    without.geometry = false;
    vk::set_water_settings(without);
    draw_frame(gpu, scene, kWaveSeconds, true, false, 1, false, moving);
    const Picture geometry_off = read_picture(scene.colour, kWidth, kHeight);
    draw_frame(gpu, scene, kWaveSeconds, true, false, 1, false, flat);
    const Picture geometry_flat = read_picture(scene.colour, kWidth, kHeight);
    tally->check(geometry_off.read && geometry_flat.read && differing(geometry_off, geometry_flat, everywhere) == 0,
                 "the moving surface: rt_water_geometry=0 the flat sheet's bytes");
    // rt_water=0: the original's, to the bit (the split sheet drawn by the program)
    set_water(false, 1.0F, 1.0F);
    draw_frame(gpu, scene, kWaveSeconds, true, false, 1, false, moving);
    const Picture off_marked = read_picture(scene.colour, kWidth, kHeight);
    draw_frame(gpu, scene, kWaveSeconds, false, false, 1, false, flat);
    const Picture off_flat = read_picture(scene.colour, kWidth, kHeight);
    tally->check(off_marked.read && off_flat.read && differing(off_marked, off_flat, everywhere) == 0,
                 "the moving surface: rt_water=0 the original's bytes");
}

// The moving surface's shape against the processor: a patch of water (nothing behind it: the shore's weight 1) with high
// waves - the pixels it covers as the vertex shader moved it against the same grid moved on the processor (water_lift's
// numbers), and well away from the flat grid's.
void check_moving_shape(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    constexpr float kPatchSide = 30.0F;
    constexpr float kPatchNear = 30.0F;
    constexpr float kPatchFar = 200.0F;
    const std::vector<Vertex> patch = split_quad(-kPatchSide, kPatchSide, kPatchNear, kPatchFar);
    if (patch.empty()) {
        tally->check(false, "the moving surface's shape: the patch split");
        return;
    }
    set_water(true, 2.0F, 0.0F);
    draw_frame(gpu, scene, kWaveSeconds, true, false, 1, false, {.grid = &patch, .moving = true, .sand = false});
    const Picture got = read_picture(scene.colour, kWidth, kHeight);
    const vk::WaterUniforms block = vk::water_uniforms(
        vk::water_settings(), {.view = view_of({.width = kWidth, .height = kHeight}, kWaveSeconds), .target = {.width = kWidth, .height = kHeight}, .copied = true});
    std::vector<uint8_t> drawn(static_cast<std::size_t>(kWidth) * kHeight, 0);
    for (std::size_t pixel = 0; pixel < drawn.size() && got.read; ++pixel) {
        constexpr float kSomething = 1.0e-3F;
        core::at(drawn, pixel) = dot(core::at(got.rgb, pixel), kLuma) > kSomething ? 1 : 0;
    }
    const std::vector<uint8_t> want = covered(patch, &block);
    const std::vector<uint8_t> still = covered(patch, nullptr);
    const std::size_t by_waves = mask_difference(want, still);
    const std::size_t off_moved = mask_difference(drawn, want);
    const std::size_t off_still = mask_difference(drawn, still);
    std::size_t area = 0;
    for (const uint8_t pixel : want) {
        area += pixel;
    }
    constexpr std::size_t kLeastMoved = 300;
    constexpr std::size_t kEdgeShare = 20; // (pixels on a triangle's edge: the rasteriser's ties - 1 in 20 of those moved)
    tally->check(got.read && by_waves > kLeastMoved && off_moved * kEdgeShare <= by_waves && off_moved * 4 < off_still,
                 text::format("the moving surface's shape: %zu pixels covered, %zu of them changed by the waves on the processor; "
                              "the device's picture %zu pixels from the processor's moved surface, %zu from the flat one",
                              area, by_waves, off_moved, off_still));
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
                draw_frame(gpu, scene, frame / kFramesASecond, true, true);
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
    scene.wall = uniform_texture(gpu, kWallColour, kOpaqueHalf);
    tally.check(made && scene.sand != nullptr && scene.water != nullptr, "the material shader, a 16-bit float target, a depth surface and the textures made");
    if (made) {
        render::water::set_level_water(
            {.level = 1, .waves = 1.0F, .swell_length = kSwellLength, .visibility = kVisibility, .heading = kHeading});
        check_off(gpu, scene, &tally);
        check_calm(gpu, scene, &tally);
        check_reflection(gpu, scene, &tally);
        check_foam(gpu, scene, &tally);
        check_waves(gpu, scene, &tally);
        check_glitter(gpu, scene, &tally);
        check_waves_texture(&tally);
        check_moving(gpu, scene, &tally);
        check_moving_shape(gpu, scene, &tally);
        const vk::WaterStats stats = vk::water_stats();
        tally.check(stats.draws > 0 && stats.copies > 0 && stats.replayed > 0,
                    text::format("water draws %llu (opaque water kept back for the opaque world's end: %llu), copies %llu",
                                 static_cast<unsigned long long>(stats.draws), static_cast<unsigned long long>(stats.replayed),
                                 static_cast<unsigned long long>(stats.copies)));
        if (options.perf) {
            measure_cost(gpu, scene);
        }
    }
    vk::set_water_settings({});
    for (GpuTexture* texture : {scene.sand, scene.water, scene.wall}) {
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
