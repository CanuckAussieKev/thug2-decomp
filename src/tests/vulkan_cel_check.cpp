// NOT ORIGINAL - thug2_vulkan_cel_check.exe: cel shading ([Graphics] rt_cel; engine/gfx/rt_cel.h, gpu_vulkan/vulkan_cel.h,
// the shaders ff_shaders.cpp writes) tried on this machine against the same formulas on the processor.
//
// Quads are drawn through render::Gpu with the game's single-texture material shader (kTextureVertexX4, a white texture:
// the output is the light, 2 x v0) into a 16-bit float target and read back:
//   - the processor's twins (rt_cel.h): the shadow colour's text, the sun's bands, the stream's words;
//   - bands of a lit gradient (the relit world's stream: N.L from 0 at the left to 1 at the right) with 2, 3 and 4 bands:
//     every pixel away from an edge the twin's value, the row's plateaus as many as the bands, each edge at most two
//     pixels wide, the shadow band tinted by the shadow colour (its blue over red the colour's);
//   - the bake banded (no stream): a vertex-colour gradient in steps of the bake's stops, the twin's value;
//   - lit by a light (an object): N.L across the quad in bands, then the rim band where the normal turns from the eye;
//   - hard shadows (a device with ray tracing; this check's own acceleration structures): a ground lit from the stream
//     whose vertices say "no sun" (their share 0) - the sun's visibility traced per pixel instead -, an occluder above it:
//     every pixel lit or in the shadow band as the processor's ray says, nothing between but along the edge;
//   - outlines: a lit cube's inverted hull (ink round its silhouette, none further out, its faces unchanged) and the
//     world's lines from the depth (on a box's edge, none inside it or on the sky);
//   - flatter textures: a checkerboard's contrast lower with rt_cel_flatten;
//   - rt_cel=0: the original's picture (2 x v0) and the same bytes before the mode was ever on and after it was off;
//   - every pixel of every case finite; validation (and synchronization validation) messages 0.
//   thug2_vulkan_cel_check [--device N] [--display N] [--no-validation] [--perf]
// --perf: full-screen layers at 2560 x 1440 - the bands and the traced visibility, objects lit, the outlines' pass, and
// the original's - timed on the device (reported, not checked).
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
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/rt_relit.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_cel.h"
#include "gpu_vulkan/vulkan_cel_draw.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "platform/run_log.h"
#include "tests/check_window.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuRenderState;
using render::GpuSamplerState;
using render::GpuStageState;
using render::GpuSurface;
using render::GpuTexture;
using render::GpuVertexBuffer;
namespace value = render::gpu_value;
namespace vk = render::vulkan;
namespace rt = render::rt;
using Vec3 = std::array<float, 3>;

constexpr int kWindowSize = 128;
constexpr uint32_t kSize = 256;
constexpr int kBitsPerByte = 8;
constexpr std::size_t kHalfPixelBytes = 8;
constexpr uint32_t kHighByte = 8;
constexpr uint16_t kHalfSign = 0x8000;
constexpr float kFov = 1.7320508F; // 1 / tan(30 degrees)
constexpr float kNear = 1.0F;
constexpr float kFar = 1000.0F;
constexpr float kByteMax = 255.0F;
constexpr float kTwo = 2.0F;
constexpr float kMid = 0.5F;
constexpr uint32_t kRedShift = 16;
constexpr uint32_t kGreenShift = 8;
constexpr uint32_t kAlphaShift = 24;
constexpr uint32_t kRgbMask = 0x00FFFFFFU;
constexpr std::size_t kMatrixFloats = 16;
constexpr Vec3 kLuma{0.2126F, 0.7152F, 0.0722F};
constexpr float kTolerance = 3.0e-3F;  // a pixel's largest difference from the twin (16-bit floats, the bytes' rounding)
constexpr std::size_t kLeastJudged = 5000;
constexpr uint32_t kFvfPosition = 0x002;
constexpr uint32_t kFvfNormal = 0x010;
constexpr uint32_t kFvfDiffuse = 0x040;
constexpr uint32_t kFvfTex1 = 0x100;
constexpr std::string_view kShader = "ps.1.1\ntex t0\nmul r0.rgb,t0,c0\n+mul_x2 r0.a,v0.a,t0.a\nmul_x4 r0.rgb,v0,r0\n";
constexpr uint32_t kBackground = 0xFFCCCCCCU; // (0.8: the cleared colour)
constexpr uint32_t kWhiteTexel = 0x80FFFFFFU;  // (the doubled alpha 128: 1)
constexpr uint32_t kGreyVertex = 0x80808080U;  // (0.5: the light 1, alpha 1 after the doubling)
constexpr float kSceneAmbient = 0.2F;           // D3DRS_AMBIENT
constexpr float kQuadAmbient = 0.3F;            // the stream's ambient light in the quads
constexpr float kTwinEdge = 0.06F;              // (of a step: pixels this near a bake step's edge left out)
constexpr float kWideTolerance = 2.0F * 3.0e-3F; // (a lit pixel's: two interpolated colours rounded)
constexpr float kRatioTolerance = 0.03F;        // (the shadow tint's blue / red)
constexpr float kLeastLuma = 1.0e-4F;
constexpr float kBakeGreen = 0.9F;              // the bake gradient's green and blue against its red
constexpr float kBakeBlue = 0.8F;
constexpr Vec3 kObjectColour{0.8F, 0.7F, 0.6F}; // the lit quads' vertex colour
constexpr float kRimStrength = 0.5F;
constexpr float kRimEdge = 0.03F;               // (of 1 - N.V: pixels this near the rim's threshold left out)
constexpr float kRimLift = 0.02F;               // (how much brighter a rim pixel must be)
constexpr float kRowEdge = 0.05F;               // (of a band: pixels this near a band's edge left out of a row's twin)
constexpr std::size_t kPerMille = 1000;
constexpr std::size_t kWithinPerMille = 995;    // (the share of pixels that must match the twin)
constexpr std::size_t kPercent = 100;
constexpr std::size_t kInkPercent = 95; // (of a ring or an edge: inked)
constexpr std::size_t kLeastRing = 100;
constexpr int kNearRing = 2;           // (pixels from a silhouette: the hull's ring)
constexpr int kFarRing = 6;            // (... and past this: untouched)
constexpr float kInkShare = 0.4F;      // (an ink pixel: darker than this share of the background)
constexpr float kBackgroundGrey = 0.8F; // (kBackground's channels)
constexpr float kSameGrey = 0.01F;
constexpr float kHullLight = 0.8F;
constexpr Vec3 kObjectLight{0.3980149F, 0.5970223F, -0.6965260F}; // (0.4, 0.6, -0.7) normalised
constexpr uint32_t kInner = 40;        // (the pictures' border left out of a spread)
constexpr uint32_t kCheckerLight = 0x80CCCCCCU;
constexpr uint32_t kCheckerDark = 0x80333333U;
constexpr uint32_t kCheckerMean = 0x80808080U;
constexpr float kFlatten = 0.8F;
constexpr float kFlatterSpread = 0.7F;
constexpr Vec3 kTiltedNormal{0.0F, 0.2873479F, -0.9578263F}; // (0, 0.3, -1) normalised

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

// --- vectors ------------------------------------------------------------------------------------------------------------

Vec3 add(const Vec3& one, const Vec3& other)
{
    return {core::at(one, 0) + core::at(other, 0), core::at(one, 1) + core::at(other, 1), core::at(one, 2) + core::at(other, 2)};
}
Vec3 sub(const Vec3& one, const Vec3& other)
{
    return {core::at(one, 0) - core::at(other, 0), core::at(one, 1) - core::at(other, 1), core::at(one, 2) - core::at(other, 2)};
}
Vec3 scale(const Vec3& vec, float by) { return {core::at(vec, 0) * by, core::at(vec, 1) * by, core::at(vec, 2) * by}; }
float dot(const Vec3& one, const Vec3& other)
{
    return (core::at(one, 0) * core::at(other, 0)) + (core::at(one, 1) * core::at(other, 1)) + (core::at(one, 2) * core::at(other, 2));
}
Vec3 normalise(const Vec3& vec)
{
    const float length = std::sqrt(dot(vec, vec));
    return length > 0.0F ? scale(vec, 1.0F / length) : vec;
}
float clamp01(float value) { return std::clamp(value, 0.0F, 1.0F); }
float luma(const Vec3& colour) { return dot(colour, kLuma); }

// A D3DCOLOR word from 0 - 1 channels.
uint32_t word_of(const Vec3& rgb, float alpha)
{
    const auto byte = [](float value) { return static_cast<uint32_t>(std::lround(clamp01(value) * kByteMax)); };
    return (byte(alpha) << kAlphaShift) | (byte(core::at(rgb, 0)) << kRedShift) | (byte(core::at(rgb, 1)) << kGreenShift) |
           byte(core::at(rgb, 2));
}
// A channel as the vertex colour carries it (a byte).
float as_byte(float value) { return std::round(clamp01(value) * kByteMax) / kByteMax; }

// --- the device's picture ------------------------------------------------------------------------------------------------

struct Picture {
    std::vector<Vec3> pixels;
    bool finite = true;
    bool read = false;
    [[nodiscard]] const Vec3& at(uint32_t column, uint32_t row) const
    {
        return core::at(pixels, (static_cast<std::size_t>(row) * kSize) + column);
    }
};

Picture read_picture(GpuSurface* target)
{
    Picture picture;
    std::vector<uint8_t> bytes;
    if (!vk::read_surface(target, &bytes) || bytes.size() < static_cast<std::size_t>(kSize) * kSize * kHalfPixelBytes) {
        return picture;
    }
    picture.read = true;
    picture.pixels.resize(static_cast<std::size_t>(kSize) * kSize);
    for (std::size_t pixel = 0; pixel < picture.pixels.size(); ++pixel) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const std::size_t at = (pixel * kHalfPixelBytes) + (channel * 2);
            const float got = from_half(static_cast<uint16_t>(core::at(bytes, at) | (core::at(bytes, at + 1) << kHighByte)));
            picture.finite = picture.finite && std::isfinite(got);
            core::at(core::at(picture.pixels, pixel), channel) = got;
        }
    }
    return picture;
}

// --- drawing ----------------------------------------------------------------------------------------------------------

struct Vertex {
    std::array<float, 3> position;
    std::array<float, 3> normal;
    uint32_t colour;
    std::array<float, 2> uv;
};
constexpr std::size_t kVertexBytes = 36;
static_assert(sizeof(Vertex) == kVertexBytes, "XYZ | NORMAL | DIFFUSE | TEX1");

std::array<float, kMatrixFloats> projection()
{
    const float depth_scale = kFar / (kFar - kNear);
    return {kFov, 0, 0, 0, 0, kFov, 0, 0, 0, 0, depth_scale, 1, 0, 0, -kNear * depth_scale, 0};
}

// A texel's place in a texture.
struct TexelAt {
    uint32_t column = 0;
    uint32_t row = 0;
    uint32_t level = 0;
};

// A texture of side `side` with `levels` mip levels, each texel from `colour(TexelAt)` (A8R8G8B8).
template <typename Colour>
GpuTexture* make_texture(render::Gpu& gpu, uint32_t side, uint32_t levels, Colour colour)
{
    GpuTexture* texture = nullptr;
    if (gpu.create_texture({.width = side, .height = side, .levels = levels, .usage = 0, .format = value::kFormatA8R8G8B8,
                            .pool = value::kPoolManaged},
                           &texture) != value::kResultOk) {
        return nullptr;
    }
    for (uint32_t level = 0; level < levels; ++level) {
        const uint32_t size = std::max(1U, side >> level);
        render::GpuLockedRect locked{};
        if (gpu.lock_texture(texture, level, &locked, nullptr, 0) != value::kResultOk || locked.bits == nullptr) {
            continue;
        }
        const std::span<uint8_t> bytes(static_cast<uint8_t*>(locked.bits),
                                       (static_cast<std::size_t>(locked.pitch) * (size - 1)) + (std::size_t{size} * 4));
        for (uint32_t row = 0; row < size; ++row) {
            for (uint32_t column = 0; column < size; ++column) {
                const uint32_t argb = colour(TexelAt{.column = column, .row = row, .level = level});
                const std::size_t at =
                    (static_cast<std::size_t>(row) * static_cast<std::size_t>(locked.pitch)) + (std::size_t{column} * 4);
                std::memcpy(bytes.subspan(at, 4).data(), &argb, 4);
            }
        }
        gpu.unlock_texture(texture, level);
    }
    return texture;
}

GpuTexture* white_texture(render::Gpu& gpu)
{
    return make_texture(gpu, 1, 1, [](const TexelAt&) { return kWhiteTexel; });
}

struct Light {
    Vec3 toward{};
    Vec3 colour{};
};

// The state every quad is drawn with: the material shader (c0 = 0.5: the output 2 x v0 x the texture), a depth test,
// lighting as asked.
void set_state(render::Gpu& gpu, render::GpuPixelShader* shader, GpuTexture* texture, const Light* light)
{
    constexpr std::array<float, kMatrixFloats> kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    gpu.set_render_state(GpuRenderState::kZEnable, 1);
    gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
    gpu.set_render_state(GpuRenderState::kZFunc, value::kCompareLessEqual);
    gpu.set_render_state(GpuRenderState::kCullMode, value::kCullNone);
    gpu.set_render_state(GpuRenderState::kAlphaTestEnable, 0);
    gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    gpu.set_render_state(GpuRenderState::kFogEnable, 0);
    gpu.set_render_state(GpuRenderState::kSpecularEnable, 0);
    gpu.set_render_state(GpuRenderState::kLighting, light != nullptr ? 1 : 0);
    gpu.set_render_state(GpuRenderState::kColourVertex, 1);
    gpu.set_render_state(GpuRenderState::kDiffuseMaterialSource, value::kSourceColour1);
    gpu.set_render_state(GpuRenderState::kAmbientMaterialSource, value::kSourceMaterial);
    gpu.set_render_state(GpuRenderState::kEmissiveMaterialSource, value::kSourceMaterial);
    gpu.set_render_state(GpuRenderState::kAmbient, word_of({kSceneAmbient, kSceneAmbient, kSceneAmbient}, 0.0F) & kRgbMask);
    gpu.set_render_state(GpuRenderState::kNormalizeNormals, 1);
    gpu.set_transform(render::GpuTransform::kWorld, kIdentity.data());
    gpu.set_transform(render::GpuTransform::kView, kIdentity.data());
    const std::array<float, kMatrixFloats> proj = projection();
    gpu.set_transform(render::GpuTransform::kProjection, proj.data());
    gpu.set_material({.diffuse = {1, 1, 1, 1}, .ambient = {1, 1, 1, 1}, .specular = {}, .emissive = {}, .power = 0});
    if (light != nullptr) {
        render::GpuLight made{};
        made.type = value::kLightDirectional;
        made.diffuse = {core::at(light->colour, 0), core::at(light->colour, 1), core::at(light->colour, 2), 1};
        made.direction = scale(light->toward, -1.0F);
        gpu.set_light(0, made);
        gpu.enable_light(0, true);
    } else {
        gpu.enable_light(0, false);
    }
    gpu.set_texture(0, texture);
    gpu.set_sampler_state(0, GpuSamplerState::kMinFilter, value::kFilterLinear);
    gpu.set_sampler_state(0, GpuSamplerState::kMagFilter, value::kFilterPoint);
    gpu.set_sampler_state(0, GpuSamplerState::kMipFilter, value::kFilterLinear);
    gpu.set_sampler_state(0, GpuSamplerState::kAddressU, value::kAddressWrap);
    gpu.set_sampler_state(0, GpuSamplerState::kAddressV, value::kAddressWrap);
    gpu.set_stage_state(0, GpuStageState::kTexCoordIndex, 0);
    gpu.set_pixel_shader(shader);
    const std::array<float, 4> c0{kMid, kMid, kMid, kMid};
    gpu.set_pixel_constants(0, c0.data(), 1);
    gpu.set_vertex_format(kFvfPosition | kFvfNormal | kFvfDiffuse | kFvfTex1);
}

// A vertex buffer holding `vertices` (and, when `stream` isn't empty, the cel stream beside it - both released after
// the draw: `buffers` keeps them until then).
struct Drawn {
    GpuVertexBuffer* vertices = nullptr;
    GpuVertexBuffer* stream = nullptr;
};

GpuVertexBuffer* buffer_of(render::Gpu& gpu, std::span<const std::byte> bytes)
{
    GpuVertexBuffer* buffer = nullptr;
    if (gpu.create_vertex_buffer(static_cast<uint32_t>(bytes.size()), value::kUsageWriteOnly, 0, value::kPoolDefault,
                                 &buffer) != value::kResultOk) {
        return nullptr;
    }
    void* locked = nullptr;
    if (gpu.lock_vertex_buffer(buffer, 0, 0, &locked, 0) == value::kResultOk && locked != nullptr) {
        std::memcpy(locked, bytes.data(), bytes.size());
        gpu.unlock_vertex_buffer(buffer);
    }
    return buffer;
}

Drawn draw_triangles(render::Gpu& gpu, std::span<const Vertex> vertices, std::span<const rt::CelWords> stream)
{
    Drawn drawn;
    drawn.vertices = buffer_of(gpu, std::as_bytes(vertices));
    if (!stream.empty()) {
        drawn.stream = buffer_of(gpu, std::as_bytes(stream));
    }
    gpu.set_stream_source(0, drawn.vertices, 0, sizeof(Vertex));
    gpu.set_cel_source({.vertices = drawn.vertices, .offset = 0, .stream = drawn.stream});
    gpu.draw(render::GpuPrimitive::kTriangleList, 0, static_cast<uint32_t>(vertices.size() / 3));
    gpu.set_cel_source({});
    return drawn;
}

void release(render::Gpu& gpu, const Drawn& drawn)
{
    gpu.set_stream_source(0, nullptr, 0, 0);
    for (GpuVertexBuffer* buffer : {drawn.vertices, drawn.stream}) {
        if (buffer != nullptr) {
            gpu.release_vertex_buffer(buffer);
        }
    }
}

// A quad facing the camera at depth z from x0..x1, y0..y1 (as two triangles), each corner's colour / normal / stream
// word from its corner (0: left top, 1: right top, 2: right bottom, 3: left bottom).
struct Corners {
    std::array<uint32_t, 4> colour{};
    std::array<Vec3, 4> normal{};
    std::array<rt::CelWords, 4> stream{};
    std::array<std::array<float, 2>, 4> uv{};
};
constexpr std::array<std::size_t, 6> kQuadOrder{0, 1, 2, 0, 2, 3};

std::vector<Vertex> quad_vertices(float x0, float x1, float y0, float y1, float z, const Corners& corners,
                                  std::vector<rt::CelWords>* stream)
{
    const std::array<Vec3, 4> at{{{x0, y1, z}, {x1, y1, z}, {x1, y0, z}, {x0, y0, z}}};
    std::vector<Vertex> vertices;
    for (const std::size_t corner : kQuadOrder) {
        vertices.push_back({.position = core::at(at, corner),
                            .normal = core::at(corners.normal, corner),
                            .colour = core::at(corners.colour, corner),
                            .uv = core::at(corners.uv, corner)});
        if (stream != nullptr) {
            stream->push_back(core::at(corners.stream, corner));
        }
    }
    return vertices;
}

// The camera's ray through a pixel's centre (Direct3D 9's: the whole number), in camera space (z forward).
Vec3 pixel_ray(uint32_t px, uint32_t py)
{
    const float ndc_x = ((kTwo * static_cast<float>(px)) / static_cast<float>(kSize)) - 1.0F;
    const float ndc_y = 1.0F - ((kTwo * static_cast<float>(py)) / static_cast<float>(kSize));
    return {ndc_x / kFov, ndc_y / kFov, 1.0F};
}

// The screen-space fraction across a camera-facing quad from x0 to x1 at depth z (-1 outside it).
struct Pixel {
    uint32_t column = 0;
    uint32_t row = 0;
};
struct QuadArea {
    float left = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
    float top = 0.0F;
    float depth = 0.0F;
};
float across(Pixel pixel, const QuadArea& quad)
{
    const Vec3 ray = pixel_ray(pixel.column, pixel.row);
    const float x = core::at(ray, 0) * quad.depth;
    const float y = core::at(ray, 1) * quad.depth;
    if (x < quad.left || x > quad.right || y < quad.bottom || y > quad.top) {
        return -1.0F;
    }
    return (x - quad.left) / (quad.right - quad.left);
}

struct Scene {
    GpuSurface* target = nullptr;
    GpuSurface* depth = nullptr;
    render::GpuPixelShader* shader = nullptr;
    GpuTexture* white = nullptr;
};

void begin(render::Gpu& gpu, const Scene& scene)
{
    gpu.begin_scene();
    gpu.set_render_target(0, scene.target);
    gpu.set_depth_surface(scene.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = kSize, .height = kSize, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth, kBackground, 1.0F, 0);
}

Picture finish(render::Gpu& gpu, const Scene& scene)
{
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    gpu.end_scene();
    return read_picture(scene.target);
}

// --- the processor's twins ------------------------------------------------------------------------------------------

void check_twins(Tally* tally)
{
    std::array<float, 3> colour{};
    const bool commas = rt::parse_cel_colour("0.5, 0.25,1", &colour) && colour == std::array<float, 3>{0.5F, 0.25F, 1.0F};
    const bool bytes = rt::parse_cel_colour("255,128,0", &colour) && std::abs(std::get<1>(colour) - (128.0F / kByteMax)) < 1e-6F;
    const bool hex = rt::parse_cel_colour("#ff8000", &colour) && std::get<0>(colour) == 1.0F && std::get<2>(colour) == 0.0F;
    const bool refused = !rt::parse_cel_colour("blue", &colour) && !rt::parse_cel_colour("1,2", &colour) &&
                         !rt::parse_cel_colour("#12345", &colour);
    tally->check(commas && bytes && hex && refused, "rt_cel_shadow_colour's text: r,g,b (0 - 1 or bytes), #rrggbb; others refused");
    bool levels_ok = true;
    for (uint32_t bands = rt::kCelLeastBands; bands <= rt::kCelMostBands; ++bands) {
        std::vector<float> seen;
        constexpr int kSteps = 1000;
        for (int step = 0; step <= kSteps; ++step) {
            const float level = rt::cel_sun_band(static_cast<float>(step) / kSteps, {}, bands);
            if (std::ranges::find(seen, level) == seen.end()) {
                seen.push_back(level);
            }
        }
        levels_ok = levels_ok && seen.size() == bands && seen.front() == 0.0F && seen.back() == 1.0F;
    }
    tally->check(levels_ok, "the sun's bands: exactly `bands` levels from 0 to 1 (2, 3, 4)");
    // the stream's words: a known vertex - its parts and the bytes they make (light x 128, shares x 255)
    constexpr Vec3 kModelSky{0.4F, 0.4F, 0.5F};
    constexpr Vec3 kModelBounce{0.5F, 0.5F, 0.5F};
    constexpr float kVertexShare = 0.5F;
    constexpr float kVertexSky = 0.5F;
    constexpr Vec3 kVertexBounce{0.2F, 0.2F, 0.2F};
    constexpr Vec3 kVertexTint{1.5F, 1.0F, 0.5F};
    constexpr Vec3 kVertexResidual{0.25F, 0.0F, 0.0F};
    constexpr uint32_t kFull = 255;
    constexpr uint32_t kAmbientRed = 38;  // (0.4 x 0.5 + 0.5 x 0.2 = 0.3)
    constexpr uint32_t kAmbientBlue = 45; // (0.5 x 0.5 + 0.1 = 0.35)
    constexpr uint32_t kTintRed = 192;    // (1.5)
    constexpr uint32_t kShareByte = 128;  // (0.5)
    constexpr uint32_t kResidualRed = 32; // (0.25)
    constexpr uint32_t kByteBits = 0xFFU;
    rt::RelitModel model;
    model.valid = true;
    model.sun = {1.0F, 1.0F, 1.0F};
    model.sky = kModelSky;
    model.bounce = kModelBounce;
    model.scale = 1.0F;
    rt::RelitVertex vertex;
    vertex.used = true;
    vertex.normal = {0.0F, 1.0F, 0.0F};
    vertex.sun = kVertexShare;
    vertex.sky = kVertexSky;
    vertex.bounce = kVertexBounce;
    vertex.tint = kVertexTint;
    vertex.residual = kVertexResidual;
    const rt::CelWords words =
        rt::cel_stream_words(model, vertex, {.toward = {0.0F, 1.0F, 0.0F}, .sun_colour = {1, 1, 1}, .strength = 1, .residual = true});
    const auto channel = [](uint32_t word, uint32_t shift) { return (word >> shift) & kByteBits; };
    const bool words_ok =
        channel(std::get<0>(words), kAlphaShift) == kFull && channel(std::get<0>(words), kRedShift) == kAmbientRed &&
        channel(std::get<0>(words), 0) == kAmbientBlue && channel(std::get<1>(words), kRedShift) == kTintRed &&
        channel(std::get<1>(words), kAlphaShift) == kShareByte && channel(std::get<2>(words), kRedShift) == kResidualRed &&
        channel(std::get<2>(words), kAlphaShift) == kFull;
    vertex.used = false;
    const bool unused = rt::cel_stream_words(model, vertex, {}) == rt::kCelNoStream;
    tally->check(words_ok && unused, "the stream's words: ambient, N.L, tint, share, residual, the flag; an unused vertex none");
}

// --- the cases ------------------------------------------------------------------------------------------------------------

constexpr float kQuadZ = 5.0F;
constexpr float kQuadHalf = 2.6F;
constexpr QuadArea kQuad{.left = -kQuadHalf, .right = kQuadHalf, .bottom = -kQuadHalf, .top = kQuadHalf, .depth = kQuadZ};
constexpr uint32_t kInsideFirst = 22; // (a row's pixels well inside the quads drawn with kQuadHalf)
constexpr uint32_t kInsideLast = kSize - 23;
constexpr Vec3 kAmbient{kQuadAmbient, kQuadAmbient, kQuadAmbient};
constexpr Vec3 kSun{0.6F, 0.6F, 0.6F};

rt::CelSettings settings_with(uint32_t bands)
{
    rt::CelSettings settings;
    settings.on = true;
    settings.bands = bands;
    settings.rim = 0.0F;
    settings.outline_width = 0.0F;
    settings.flatten = 0.0F;
    return settings;
}

// The plateaus of a row's red channel: runs of pixels within kTolerance of their neighbour; the widest step between two.
struct Plateaus {
    std::vector<float> values;
    int widest_edge = 0;
};
Plateaus plateaus_of(const Picture& picture, uint32_t row, uint32_t first, uint32_t last)
{
    Plateaus out;
    constexpr int kLeastRun = 4;
    int run = 0;
    int edge = 0;
    float value = core::at(picture.at(first, row), 0);
    for (uint32_t column = first + 1; column <= last; ++column) {
        const float now = core::at(picture.at(column, row), 0);
        if (std::abs(now - value) <= kTolerance) {
            ++run;
            if (run == kLeastRun) {
                if (out.values.empty() || std::abs(out.values.back() - value) > kTolerance) {
                    out.values.push_back(value);
                    // (the steps between the two plateaus: one more than the pixels between them)
                    out.widest_edge = std::max({out.widest_edge, edge - 1, 0});
                }
                edge = 0;
            }
        } else {
            if (run < kLeastRun) {
                ++edge;
            } else {
                edge = 1;
            }
            run = 0;
        }
        value = now;
    }
    return out;
}

void check_stream_bands(render::Gpu& gpu, const Scene& scene, uint32_t bands, Tally* tally)
{
    const rt::CelSettings settings = settings_with(bands);
    vk::set_cel_settings(settings);
    rt::set_cel_world({.relit = true, .sun = kSun, .toward = {0.0F, 1.0F, 0.0F}});
    Corners corners;
    for (std::size_t corner = 0; corner < 4; ++corner) {
        const bool right = corner == 1 || corner == 2;
        corners.colour.at(corner) = kGreyVertex;
        corners.normal.at(corner) = {0.0F, 0.0F, -1.0F};
        corners.stream.at(corner) = {word_of(scale(kAmbient, kMid), right ? 1.0F : 0.0F), word_of({kMid, kMid, kMid}, 1.0F),
                                     word_of({0.0F, 0.0F, 0.0F}, 1.0F)};
    }
    std::vector<rt::CelWords> stream;
    const std::vector<Vertex> vertices = quad_vertices(-kQuadHalf, kQuadHalf, -kQuadHalf, kQuadHalf, kQuadZ, corners, &stream);
    begin(gpu, scene);
    set_state(gpu, scene.shader, scene.white, nullptr);
    const Drawn drawn = draw_triangles(gpu, vertices, stream);
    const Picture picture = finish(gpu, scene);
    release(gpu, drawn);
    if (!picture.read) {
        tally->check(false, "bands: read back");
        return;
    }
    // the twin per pixel, away from the band's edges
    const float ambient_light = as_byte(kMid * kQuadAmbient) * kTwo;
    const Vec3 ambient = {ambient_light, ambient_light, ambient_light};
    std::size_t judged = 0;
    std::size_t within = 0;
    float worst = 0.0F;
    const auto steps = static_cast<float>(bands - 1);
    for (uint32_t py = 0; py < kSize; py += 2) {
        for (uint32_t px = 0; px < kSize; ++px) {
            const float along = across({.column = px, .row = py}, kQuad);
            if (along < 0.0F) {
                continue;
            }
            const float band_t = (along * steps) + rt::kCelBandBias;
            const float part = band_t - std::floor(band_t);
            constexpr float kEdgeShare = 0.04F; // (of a step: pixels this near an edge left out)
            if (std::abs(part - kMid) < kEdgeShare * steps) {
                continue;
            }
            const float tint = as_byte(kMid) * kTwo;
            const rt::CelStreamPixel pixel{.ambient = ambient, .n_dot_l = along, .tint = {tint, tint, tint}, .share = 1.0F, .residual = {}};
            const std::array<float, 3> want = rt::cel_world_light(settings, kSun, pixel, 1.0F, {});
            const Vec3& got = picture.at(px, py);
            float difference = 0.0F;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                difference = std::max(difference, std::abs(core::at(got, channel) - clamp01(core::at(want, channel))));
            }
            ++judged;
            within += difference <= kTolerance ? 1U : 0U;
            worst = std::max(worst, difference);
        }
    }
    const Plateaus row = plateaus_of(picture, kSize / 2, kInsideFirst, kInsideLast);
    tally->check(judged > kLeastJudged && within == judged,
                 text::format("%u bands: %zu pixels the twin's (worst %.4f)", bands, judged, static_cast<double>(worst)));
    tally->check(row.values.size() == bands && row.widest_edge <= 2,
                 text::format("%u bands: %zu plateaus along a row, each edge %d pixel%s wide at most", bands, row.values.size(),
                              row.widest_edge, row.widest_edge == 1 ? "" : "s"));
    // the shadow band's tint: its blue over red the shadow colour's
    const Vec3& dark = picture.at(kInsideFirst, kSize / 2);
    const float ratio = core::at(dark, 2) / std::max(core::at(dark, 0), kLeastLuma);
    const float want_ratio = std::get<2>(settings.shadow_colour) / std::get<0>(settings.shadow_colour);
    tally->check(std::abs(ratio - want_ratio) < kRatioTolerance * want_ratio && picture.finite,
                 text::format("%u bands: the shadow band tinted (blue / red %.3f, the colour's %.3f); every pixel finite", bands,
                              static_cast<double>(ratio), static_cast<double>(want_ratio)));
}

void check_bake_bands(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    const rt::CelSettings settings = settings_with(3);
    vk::set_cel_settings(settings);
    rt::set_cel_world({});
    constexpr float kDark = 0.03F;
    constexpr float kBright = 0.5F; // (v0: the light 1)
    Corners corners;
    for (std::size_t corner = 0; corner < 4; ++corner) {
        const bool right = corner == 1 || corner == 2;
        const float v0 = right ? kBright : kDark;
        corners.colour.at(corner) = word_of({v0, v0 * kBakeGreen, v0 * kBakeBlue}, kMid);
        corners.normal.at(corner) = {0.0F, 0.0F, -1.0F};
    }
    const std::vector<Vertex> vertices = quad_vertices(-kQuadHalf, kQuadHalf, -kQuadHalf, kQuadHalf, kQuadZ, corners, nullptr);
    begin(gpu, scene);
    set_state(gpu, scene.shader, scene.white, nullptr);
    const Drawn drawn = draw_triangles(gpu, vertices, {});
    const Picture picture = finish(gpu, scene);
    release(gpu, drawn);
    std::size_t judged = 0;
    std::size_t within = 0;
    float worst = 0.0F;
    const float stops = rt::cel_bake_stops(settings.bands);
    const Vec3 left{as_byte(kDark), as_byte(kDark * kBakeGreen), as_byte(kDark * kBakeBlue)};
    const Vec3 right{as_byte(kBright), as_byte(kBright * kBakeGreen), as_byte(kBright * kBakeBlue)};
    for (uint32_t py = 0; picture.read && py < kSize; py += 2) {
        for (uint32_t px = 0; px < kSize; ++px) {
            const float along = across({.column = px, .row = py}, kQuad);
            if (along < 0.0F) {
                continue;
            }
            const Vec3 bake = scale(add(scale(left, 1.0F - along), scale(right, along)), kTwo);
            const float steps = std::log2(std::max(luma(bake), kLeastLuma)) / stops;
            const float part = steps - std::floor(steps);
            if (std::abs(part - kMid) < kTwinEdge) {
                continue;
            }
            const std::array<float, 3> want = rt::cel_bake_light(settings, bake, {});
            const Vec3& got = picture.at(px, py);
            float difference = 0.0F;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                difference = std::max(difference, std::abs(core::at(got, channel) - clamp01(core::at(want, channel))));
            }
            ++judged;
            within += difference <= kWideTolerance ? 1U : 0U;
            worst = std::max(worst, difference);
        }
    }
    const Plateaus row = plateaus_of(picture, kSize / 2, kInsideFirst, kInsideLast);
    tally->check(picture.read && judged > kLeastJudged && within * kPerMille >= judged * kWithinPerMille && row.values.size() >= 3 &&
                     picture.finite,
                 text::format("the bake banded (no stream): %zu pixels, %.2f%% the twin's (worst %.4f), %zu plateaus, finite",
                              judged, judged != 0 ? static_cast<double>(kPercent) * static_cast<double>(within) / static_cast<double>(judged) : 0.0,
                              static_cast<double>(worst), row.values.size()));
}

// An object lit by a light: N.L across the quad from the normal at its left to the one at its right.
Picture draw_lit(render::Gpu& gpu, const Scene& scene, const Vec3& left, const Vec3& right, const Light& light)
{
    Corners corners;
    for (std::size_t corner = 0; corner < 4; ++corner) {
        const bool is_right = corner == 1 || corner == 2;
        corners.colour.at(corner) = word_of(kObjectColour, 1.0F);
        corners.normal.at(corner) = is_right ? right : left;
    }
    const std::vector<Vertex> vertices = quad_vertices(-kQuadHalf, kQuadHalf, -kQuadHalf, kQuadHalf, kQuadZ, corners, nullptr);
    begin(gpu, scene);
    set_state(gpu, scene.shader, scene.white, &light);
    const Drawn drawn = draw_triangles(gpu, vertices, {});
    const Picture picture = finish(gpu, scene);
    release(gpu, drawn);
    return picture;
}

void check_lit(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    const rt::CelSettings settings = settings_with(3);
    vk::set_cel_settings(settings);
    rt::set_cel_world({});
    const Vec3 left = normalise({-0.95F, 0.0F, -0.31F});
    const Vec3 right = normalise({0.6F, 0.0F, -0.8F});
    const Light light{.toward = normalise({0.7F, 0.2F, -0.68F}), .colour = {0.5F, 0.45F, 0.4F}};
    const Picture picture = draw_lit(gpu, scene, left, right, light);
    const Vec3 cd{as_byte(core::at(kObjectColour, 0)), as_byte(core::at(kObjectColour, 1)), as_byte(core::at(kObjectColour, 2))};
    const float ambient = as_byte(kSceneAmbient);
    std::size_t judged = 0;
    std::size_t within = 0;
    float worst = 0.0F;
    const auto steps = static_cast<float>(settings.bands - 1);
    for (uint32_t py = 0; picture.read && py < kSize; py += 2) {
        for (uint32_t px = 0; px < kSize; ++px) {
            const float along = across({.column = px, .row = py}, kQuad);
            if (along < 0.0F) {
                continue;
            }
            const Vec3 normal = normalise(add(scale(left, 1.0F - along), scale(right, along)));
            const float n_dot_l = clamp01(dot(normal, light.toward));
            const float band_t = (n_dot_l * steps) + rt::kCelBandBias;
            if (std::abs((band_t - std::floor(band_t)) - kMid) < kRowEdge) {
                continue;
            }
            const float band = rt::cel_sun_band(n_dot_l, {}, settings.bands);
            Vec3 want{};
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const float shade = core::at(settings.shadow_colour, channel) + ((1.0F - core::at(settings.shadow_colour, channel)) * band);
                core::at(want, channel) = clamp01((ambient * shade) + (core::at(cd, channel) * core::at(light.colour, channel) * band)) *
                                          kTwo * kMid * kTwo; // (2 x v0)
                core::at(want, channel) = clamp01(core::at(want, channel));
            }
            const Vec3& got = picture.at(px, py);
            float difference = 0.0F;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                difference = std::max(difference, std::abs(core::at(got, channel) - core::at(want, channel)));
            }
            ++judged;
            within += difference <= kWideTolerance ? 1U : 0U;
            worst = std::max(worst, difference);
        }
    }
    const Plateaus row = plateaus_of(picture, kSize / 2, kInsideFirst, kInsideLast);
    tally->check(picture.read && judged > kLeastJudged && within * kPerMille >= judged * kWithinPerMille && row.values.size() == 3 && picture.finite,
                 text::format("lit by a light: %zu pixels, %.2f%% the twin's (worst %.4f), %zu plateaus, finite", judged,
                              judged != 0 ? static_cast<double>(kPercent) * static_cast<double>(within) / static_cast<double>(judged) : 0.0,
                              static_cast<double>(worst), row.values.size()));
    // the rim: brighter where 1 - N.V passes the threshold, the rest the same
    rt::CelSettings rimmed = settings;
    rimmed.rim = kRimStrength;
    vk::set_cel_settings(rimmed);
    const Picture with_rim = draw_lit(gpu, scene, left, right, light);
    std::size_t brighter = 0;
    std::size_t same = 0;
    std::size_t rim_pixels = 0;
    std::size_t other = 0;
    for (uint32_t px = 4; with_rim.read && picture.read && px + 4 < kSize; ++px) {
        const uint32_t py = kSize / 2;
        const float along = across({.column = px, .row = py}, kQuad);
        if (along < 0.0F) {
            continue;
        }
        const Vec3 normal = normalise(add(scale(left, 1.0F - along), scale(right, along)));
        const Vec3 to_eye = normalise(scale(pixel_ray(px, py), -1.0F));
        const float rim_t = 1.0F - std::max(dot(normal, to_eye), 0.0F);
        if (std::abs(rim_t - rt::kCelRimThreshold) < kRimEdge) {
            continue;
        }
        const float before = luma(picture.at(px, py));
        const float after = luma(with_rim.at(px, py));
        if (rim_t > rt::kCelRimThreshold) {
            ++rim_pixels;
            brighter += after > before + kRimLift ? 1U : 0U;
        } else {
            ++other;
            same += std::abs(after - before) <= kTolerance ? 1U : 0U;
        }
    }
    tally->check(rim_pixels > 4 && brighter == rim_pixels && same == other,
                 text::format("the rim: %zu of %zu pixels past the threshold brighter, %zu of %zu others unchanged", brighter,
                              rim_pixels, same, other));
}

// --- hard shadows: this check's own acceleration structures ---------------------------------------------------------------

constexpr float kGroundY = -20.0F;
constexpr std::array<float, 2> kGroundX{-120.0F, 120.0F};
constexpr std::array<float, 2> kGroundZ{40.0F, 300.0F};
constexpr Vec3 kBoxLow{-24.0F, -2.0F, 100.0F};
constexpr Vec3 kBoxHigh{24.0F, 12.0F, 150.0F};
constexpr std::size_t kLeastShadowed = 500; // (the occluder's shadow's pixels at least)
constexpr Vec3 kSunToward{0.3178534F, 0.9081526F, -0.2724458F}; // (0.35, 1, -0.3) normalised

struct Structure {
    vk::DeviceBuffer storage;
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    VkDeviceAddress address = 0;
};
struct Traced {
    std::vector<vk::DeviceBuffer> inputs;
    std::vector<Structure> structures;
    vk::DeviceBuffer scratch;
    VkAccelerationStructureKHR top = VK_NULL_HANDLE;
};

VkDeviceAddress address_of(VkDevice device, VkBuffer buffer)
{
    const VkBufferDeviceAddressInfo info{.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .pNext = nullptr, .buffer = buffer};
    return vkGetBufferDeviceAddress(device, &info);
}

bool make_input(vk::Backend& state, std::span<const std::byte> bytes, vk::DeviceBuffer* out, VkDeviceAddress* address)
{
    constexpr VkDeviceSize kAlignment = 16;
    if (!state.memory.create_buffer(bytes.size() + kAlignment,
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    vk::BufferPlace::kUpload, out) ||
        out->mapped == nullptr) {
        return false;
    }
    const VkDeviceAddress start = address_of(state.context.device(), out->buffer);
    *address = (start + kAlignment - 1) & ~(kAlignment - 1);
    const std::span<std::byte> into(static_cast<std::byte*>(out->mapped), static_cast<std::size_t>(out->bytes));
    std::ranges::copy(bytes, into.subspan(static_cast<std::size_t>(*address - start)).begin());
    state.memory.flush(*out, 0, out->bytes);
    return true;
}

bool build(vk::Backend& state, Traced* traced, VkAccelerationStructureTypeKHR type,
           const VkAccelerationStructureGeometryKHR& geometry, uint32_t primitives, Structure* out)
{
    const VkDevice device = state.context.device();
    VkAccelerationStructureBuildGeometryInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    info.type = type;
    info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    info.geometryCount = 1;
    info.pGeometries = &geometry;
    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &primitives, &sizes);
    if (!state.memory.create_buffer(sizes.accelerationStructureSize,
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    vk::BufferPlace::kDevice, &out->storage)) {
        return false;
    }
    const VkAccelerationStructureCreateInfoKHR create{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
                                                      .pNext = nullptr,
                                                      .createFlags = 0,
                                                      .buffer = out->storage.buffer,
                                                      .offset = 0,
                                                      .size = sizes.accelerationStructureSize,
                                                      .type = type,
                                                      .deviceAddress = 0};
    if (vkCreateAccelerationStructureKHR(device, &create, nullptr, &out->handle) != VK_SUCCESS) {
        return false;
    }
    const VkAccelerationStructureDeviceAddressInfoKHR where{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, .pNext = nullptr, .accelerationStructure = out->handle};
    out->address = vkGetAccelerationStructureDeviceAddressKHR(device, &where);
    constexpr VkDeviceSize kScratchAlignment = 256;
    if (traced->scratch.buffer != VK_NULL_HANDLE) {
        state.memory.destroy_buffer(&traced->scratch);
    }
    if (!state.memory.create_buffer(sizes.buildScratchSize + kScratchAlignment,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    vk::BufferPlace::kDevice, &traced->scratch)) {
        return false;
    }
    info.dstAccelerationStructure = out->handle;
    info.scratchData.deviceAddress = (address_of(device, traced->scratch.buffer) + kScratchAlignment - 1) & ~(kScratchAlignment - 1);
    const VkAccelerationStructureBuildRangeInfoKHR range{
        .primitiveCount = primitives, .primitiveOffset = 0, .firstVertex = 0, .transformOffset = 0};
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    return state.memory.submit_now([&](VkCommandBuffer commands) { vkCmdBuildAccelerationStructuresKHR(commands, 1, &info, &ranges); });
}

// The box's twelve triangles (the occluder; the ground isn't in the scene - it receives, as the world's floor does).
std::vector<std::array<float, 3>> box_triangles()
{
    const auto corner = [](int index) {
        return std::array<float, 3>{(index & 1) != 0 ? core::at(kBoxHigh, 0) : core::at(kBoxLow, 0),
                                    (index & 2) != 0 ? core::at(kBoxHigh, 1) : core::at(kBoxLow, 1),
                                    (index & 4) != 0 ? core::at(kBoxHigh, 2) : core::at(kBoxLow, 2)};
    };
    constexpr std::array<std::array<int, 4>, 6> kFaces{{{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}}};
    std::vector<std::array<float, 3>> out;
    for (const std::array<int, 4>& face : kFaces) {
        for (const int index : {0, 1, 2, 0, 2, 3}) {
            out.push_back(corner(core::at(face, static_cast<std::size_t>(index))));
        }
    }
    return out;
}

bool make_traced(vk::Backend& state, Traced* traced)
{
    const std::vector<std::array<float, 3>> vertices = box_triangles();
    vk::DeviceBuffer& input = traced->inputs.emplace_back();
    VkDeviceAddress vertex_address = 0;
    if (!make_input(state, std::as_bytes(std::span(vertices)), &input, &vertex_address)) {
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
    Structure& bottom = traced->structures.emplace_back();
    if (!build(state, traced, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geometry,
               static_cast<uint32_t>(vertices.size() / 3), &bottom)) {
        return false;
    }
    VkAccelerationStructureInstanceKHR instance{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        core::at(core::at(std::span(instance.transform.matrix), axis), axis) = 1.0F;
    }
    instance.mask = 0x01; // (the opaque level geometry: the mask the draws trace)
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = bottom.address;
    vk::DeviceBuffer& list = traced->inputs.emplace_back();
    VkDeviceAddress list_address = 0;
    if (!make_input(state, std::as_bytes(std::span(&instance, 1)), &list, &list_address)) {
        return false;
    }
    VkAccelerationStructureGeometryKHR top{};
    top.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    top.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    top.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    top.geometry.instances.arrayOfPointers = VK_FALSE;
    top.geometry.instances.data.deviceAddress = list_address;
    Structure& made = traced->structures.emplace_back();
    if (!build(state, traced, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, top, 1, &made)) {
        return false;
    }
    traced->top = made.handle;
    return true;
}

void release_traced(vk::Backend& state, Traced* traced)
{
    vkDeviceWaitIdle(state.context.device());
    for (Structure& structure : traced->structures) {
        if (structure.handle != VK_NULL_HANDLE) {
            vkDestroyAccelerationStructureKHR(state.context.device(), structure.handle, nullptr);
        }
        state.memory.destroy_buffer(&structure.storage);
    }
    for (vk::DeviceBuffer& buffer : traced->inputs) {
        state.memory.destroy_buffer(&buffer);
    }
    if (traced->scratch.buffer != VK_NULL_HANDLE) {
        state.memory.destroy_buffer(&traced->scratch);
    }
    *traced = {};
}

// Whether a ray from `origin` toward `to` meets the box (slabs).
bool hits_box(const Vec3& origin, const Vec3& to)
{
    constexpr float kFarAway = 1.0e9F;
    constexpr float kParallel = 1.0e-9F;
    float enter = 0.0F;
    float leave = kFarAway;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float start = core::at(origin, axis);
        const float step = core::at(to, axis);
        if (std::abs(step) < kParallel) {
            if (start < core::at(kBoxLow, axis) || start > core::at(kBoxHigh, axis)) {
                return false;
            }
            continue;
        }
        float t0 = (core::at(kBoxLow, axis) - start) / step;
        float t1 = (core::at(kBoxHigh, axis) - start) / step;
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        enter = std::max(enter, t0);
        leave = std::min(leave, t1);
    }
    return enter <= leave;
}

// The ground's point under a pixel and whether the sun reaches it (as the shader starts its ray: up off the surface,
// a little toward the sun); false: the pixel isn't on the ground.
bool ground_at(uint32_t px, uint32_t py, bool* lit)
{
    const Vec3 ray = pixel_ray(px, py);
    if (core::at(ray, 1) >= -kLeastLuma) { // (level or upward: not toward the ground)
        return false;
    }
    const Vec3 point = scale(ray, kGroundY / core::at(ray, 1));
    if (core::at(point, 0) < core::at(kGroundX, 0) || core::at(point, 0) > core::at(kGroundX, 1) ||
        core::at(point, 2) < core::at(kGroundZ, 0) || core::at(point, 2) > core::at(kGroundZ, 1)) {
        return false;
    }
    constexpr float kOffset = 2.0F;
    constexpr float kAlong = 1.0F;
    const Vec3 origin = add(add(point, {0.0F, kOffset, 0.0F}), scale(kSunToward, kAlong));
    *lit = !hits_box(origin, kSunToward);
    return true;
}

// Whether a ground pixel lies within two pixels of the shadow's other side (the band's anti-aliasing reads the 2 x 2
// block's derivatives, and a ray grazing the occluder's edge may land a pixel either way), or of the ground's edge.
bool near_shadow_edge(uint32_t px, uint32_t py, bool lit)
{
    constexpr int kEdgeReach = 2;
    for (int dy = -kEdgeReach; dy <= kEdgeReach; ++dy) {
        for (int dx = -kEdgeReach; dx <= kEdgeReach; ++dx) {
            bool other = lit;
            const int column = static_cast<int>(px) + dx;
            const int row = static_cast<int>(py) + dy;
            if (column < 0 || row < 0 || !ground_at(static_cast<uint32_t>(column), static_cast<uint32_t>(row), &other) ||
                other != lit) {
                return true;
            }
        }
    }
    return false;
}

bool same_colour(const Vec3& got, const std::array<float, 3>& want)
{
    for (std::size_t channel = 0; channel < 3; ++channel) {
        if (std::abs(core::at(got, channel) - clamp01(core::at(want, channel))) > kTolerance) {
            return false;
        }
    }
    return true;
}

struct ShadowTally {
    std::size_t judged = 0;
    std::size_t right = 0;
    std::size_t in_shadow = 0;
    std::size_t between = 0;
    std::size_t edge = 0;
};

ShadowTally count_shadow_pixels(const Picture& picture, const std::array<float, 3>& lit_colour,
                                const std::array<float, 3>& shadow_colour)
{
    ShadowTally out;
    for (uint32_t py = 1; picture.read && py + 1 < kSize; ++py) {
        for (uint32_t px = 1; px + 1 < kSize; ++px) {
            bool lit = false;
            if (!ground_at(px, py, &lit)) {
                continue;
            }
            if (near_shadow_edge(px, py, lit)) {
                ++out.edge;
                continue;
            }
            const Vec3& got = picture.at(px, py);
            ++out.judged;
            out.in_shadow += lit ? 0U : 1U;
            out.right += same_colour(got, lit ? lit_colour : shadow_colour) ? 1U : 0U;
            out.between += !same_colour(got, lit_colour) && !same_colour(got, shadow_colour) ? 1U : 0U;
        }
    }
    return out;
}

void check_shadows(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    vk::Backend& state = vk::backend();
    if (!state.context.ray_tracing_enabled()) {
        text::print("  (no ray tracing on this device: the traced shadows not checked)\n");
        return;
    }
    Traced traced;
    bool made = false;
    {
        const std::scoped_lock lock(state.mutex);
        made = make_traced(state, &traced);
        vk::cel_set_scene_locked(made ? traced.top : VK_NULL_HANDLE);
    }
    tally->check(made, "hard shadows: the occluder's acceleration structures made");
    if (!made) {
        return;
    }
    const rt::CelSettings settings = settings_with(3);
    vk::set_cel_settings(settings);
    rt::set_cel_world({.relit = true, .sun = kSun, .toward = kSunToward});
    // the ground: N.L 1 everywhere, its vertices' own share of the sun 0 - only the traced visibility lights it
    const std::array<Vec3, 4> at{{{core::at(kGroundX, 0), kGroundY, core::at(kGroundZ, 1)},
                                  {core::at(kGroundX, 1), kGroundY, core::at(kGroundZ, 1)},
                                  {core::at(kGroundX, 1), kGroundY, core::at(kGroundZ, 0)},
                                  {core::at(kGroundX, 0), kGroundY, core::at(kGroundZ, 0)}}};
    std::vector<Vertex> vertices;
    std::vector<rt::CelWords> stream;
    for (const std::size_t corner : kQuadOrder) {
        vertices.push_back({.position = core::at(at, corner), .normal = {0.0F, 1.0F, 0.0F}, .colour = kGreyVertex, .uv = {}});
        stream.push_back({word_of(scale(kAmbient, kMid), 1.0F), word_of({kMid, kMid, kMid}, 0.0F), word_of({}, 1.0F)});
    }
    begin(gpu, scene);
    set_state(gpu, scene.shader, scene.white, nullptr);
    const uint64_t shadowed_before = vk::cel_counts().shadowed;
    const Drawn drawn = draw_triangles(gpu, vertices, stream);
    const Picture picture = finish(gpu, scene);
    release(gpu, drawn);
    const bool traced_draw = vk::cel_counts().shadowed > shadowed_before;
    const float ambient_light = as_byte(kMid * kQuadAmbient) * kTwo;
    const Vec3 ambient{ambient_light, ambient_light, ambient_light};
    const float tint = as_byte(kMid) * kTwo;
    const rt::CelStreamPixel pixel{.ambient = ambient, .n_dot_l = 1.0F, .tint = {tint, tint, tint}, .share = 0.0F, .residual = {}};
    const std::array<float, 3> lit_colour = rt::cel_world_light(settings, kSun, pixel, 1.0F, {});
    const std::array<float, 3> shadow_colour = rt::cel_world_light(settings, kSun, pixel, 0.0F, {});
    const ShadowTally counted = count_shadow_pixels(picture, lit_colour, shadow_colour);
    const std::size_t judged = counted.judged;
    const std::size_t right = counted.right;
    const std::size_t in_shadow = counted.in_shadow;
    const std::size_t between = counted.between;
    const std::size_t edge_pixels = counted.edge;
    tally->check(traced_draw && judged > kLeastJudged && in_shadow > kLeastShadowed && right == judged && between == 0 && picture.finite,
                 text::format("hard shadows: %zu ground pixels (%zu in the occluder's shadow) lit or in the shadow band as the "
                              "processor's ray says (%zu right), none between; %zu edge pixels left out",
                              judged, in_shadow, right, edge_pixels));
    {
        const std::scoped_lock lock(state.mutex);
        vk::cel_set_scene_locked(VK_NULL_HANDLE);
        release_traced(state, &traced);
    }
}

// --- outlines ------------------------------------------------------------------------------------------------------------

// A cube's placing: its half side, its centre's depth, its turn about y.
struct CubePlace {
    float side = 1.0F;
    float depth = 6.0F;
    float yaw = 0.5F;
};
constexpr Vec3 kCubeColour{0.9F, 0.5F, 0.3F};

// A cube: 36 vertices, each's normal pointing out from the centre (the hull's).
std::vector<Vertex> cube_vertices(const CubePlace& place)
{
    const float side = place.side;
    const float z = place.depth;
    const float yaw = place.yaw;
    const auto corner = [&](int index) {
        Vec3 point{(index & 1) != 0 ? side : -side, (index & 2) != 0 ? side : -side, (index & 4) != 0 ? side : -side};
        const Vec3 normal = normalise(point);
        const float cosine = std::cos(yaw);
        const float sine = std::sin(yaw);
        const auto turn = [cosine, sine](const Vec3& vec) {
            return Vec3{(core::at(vec, 0) * cosine) + (core::at(vec, 2) * sine), core::at(vec, 1),
                        (-core::at(vec, 0) * sine) + (core::at(vec, 2) * cosine)};
        };
        point = add(turn(point), {0.0F, 0.0F, z});
        return Vertex{.position = point, .normal = turn(normal), .colour = word_of(kCubeColour, 1.0F), .uv = {}};
    };
    constexpr std::array<std::array<int, 4>, 6> kFaces{{{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}}};
    std::vector<Vertex> out;
    for (const std::array<int, 4>& face : kFaces) {
        for (const int index : {0, 1, 2, 0, 2, 3}) {
            out.push_back(corner(core::at(face, static_cast<std::size_t>(index))));
        }
    }
    // Direct3D's winding: a face's front clockwise as seen from outside (left-handed: (b - a) x (c - a) points out)
    const Vec3 centre{0.0F, 0.0F, z};
    for (std::size_t first = 0; first + 2 < out.size(); first += 3) {
        const Vec3 apex = core::at(out, first).position;
        const Vec3 second = core::at(out, first + 1).position;
        const Vec3 third = core::at(out, first + 2).position;
        const Vec3 ab = sub(second, apex);
        const Vec3 ac = sub(third, apex);
        const Vec3 normal{(core::at(ab, 1) * core::at(ac, 2)) - (core::at(ab, 2) * core::at(ac, 1)),
                          (core::at(ab, 2) * core::at(ac, 0)) - (core::at(ab, 0) * core::at(ac, 2)),
                          (core::at(ab, 0) * core::at(ac, 1)) - (core::at(ab, 1) * core::at(ac, 0))};
        constexpr float kThird = 1.0F / 3.0F;
        const Vec3 middle = scale(add(add(apex, second), third), kThird);
        if (dot(normal, sub(middle, centre)) < 0.0F) {
            std::swap(core::at(out, first + 1), core::at(out, first + 2));
        }
    }
    return out;
}

Picture draw_cube(render::Gpu& gpu, const Scene& scene, float width)
{
    rt::CelSettings settings = settings_with(3);
    settings.outline_width = width;
    vk::set_cel_settings(settings);
    rt::set_cel_world({});
    const Light light{.toward = kObjectLight, .colour = {kHullLight, kHullLight, kHullLight}};
    const std::vector<Vertex> vertices = cube_vertices({});
    begin(gpu, scene);
    set_state(gpu, scene.shader, scene.white, &light);
    const Drawn drawn = draw_triangles(gpu, vertices, {});
    gpu.set_render_state(GpuRenderState::kLighting, 0);
    const Picture picture = finish(gpu, scene);
    release(gpu, drawn);
    return picture;
}

void check_hull(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    const Picture plain = draw_cube(gpu, scene, 0.0F);
    const uint64_t hulls_before = vk::cel_counts().hulls;
    constexpr float kWide = 4.0F; // (rt::kCelHullPixels x 4 x 256 / 720: about 3.6 pixels)
    const Picture outlined = draw_cube(gpu, scene, kWide);
    const bool drew = vk::cel_counts().hulls > hulls_before;
    const float background = luma({kBackgroundGrey, kBackgroundGrey, kBackgroundGrey});
    std::size_t ring = 0;
    std::size_t ink = 0;
    std::size_t far_out = 0;
    std::size_t far_kept = 0;
    std::size_t inside = 0;
    std::size_t inside_kept = 0;
    const auto is_background = [background](const Vec3& colour) { return std::abs(luma(colour) - background) < kSameGrey; };
    for (uint32_t py = 2; plain.read && outlined.read && py + 2 < kSize; ++py) {
        for (uint32_t px = 2; px + 2 < kSize; ++px) {
            const bool cube = !is_background(plain.at(px, py));
            // the distance (in pixels, up to kFarRing) to the cube's silhouette, from outside
            int nearest = kFarRing + 1;
            for (int dy = -kFarRing; dy <= kFarRing && !cube; ++dy) {
                for (int dx = -kFarRing; dx <= kFarRing; ++dx) {
                    const int column = static_cast<int>(px) + dx;
                    const int row = static_cast<int>(py) + dy;
                    if (column >= 0 && row >= 0 && std::cmp_less(column, kSize) && std::cmp_less(row, kSize) &&
                        !is_background(plain.at(static_cast<uint32_t>(column), static_cast<uint32_t>(row)))) {
                        nearest = std::min(nearest, std::max(std::abs(dx), std::abs(dy)));
                    }
                }
            }
            if (cube) {
                ++inside;
                inside_kept += std::abs(luma(plain.at(px, py)) - luma(outlined.at(px, py))) <= kTolerance ? 1U : 0U;
            } else if (nearest <= kNearRing) {
                ++ring;
                ink += luma(outlined.at(px, py)) < kInkShare * background ? 1U : 0U;
            } else if (nearest > kFarRing) {
                ++far_out;
                far_kept += is_background(outlined.at(px, py)) ? 1U : 0U;
            }
        }
    }
    tally->check(drew && ring > kLeastRing && ink * kPercent >= ring * kInkPercent && far_kept == far_out &&
                     inside_kept * kPerMille >= inside * kWithinPerMille &&
                     outlined.finite,
                 text::format("a lit cube's hull: %zu of %zu pixels within two of its silhouette ink, %zu of %zu further out "
                              "untouched, %zu of %zu of its own pixels unchanged",
                              ink, ring, far_kept, far_out, inside_kept, inside));
}

// The world's lines: screen-space quads at known depths (a far wall, a near box) and the pass through the listener.
struct ScreenVertex {
    float x, y, z, rhw;
    uint32_t colour;
};

float depth_of(float distance)
{
    const float p22 = kFar / (kFar - kNear);
    const float p32 = -kNear * kFar / (kFar - kNear);
    return p22 + (p32 / distance);
}

constexpr float kBoxLeft = 80.0F;
constexpr float kBoxRight = 176.0F;
constexpr float kBoxTop = 70.0F;
constexpr float kBoxBottom = 180.0F;
constexpr float kWallTop = 40.0F;
constexpr float kWallDistance = 400.0F;
constexpr float kNearBoxDistance = 60.0F;
constexpr uint32_t kWallColour = 0xFF6080A0U;
constexpr uint32_t kNearBoxColour = 0xFFC08040U;
constexpr float kInkDarker = 0.6F;  // (an inked pixel: darker than this share of its own colour)
constexpr float kAway = 3.0F;      // (pixels this far from the box's edge or more: no line)
constexpr float kCornerSkip = 2.0F; // (the edge's pixels this near a corner left out)

Picture draw_lines_scene(render::Gpu& gpu, const Scene& scene, bool listener)
{
    rt::CelSettings settings = settings_with(3);
    settings.outline_width = 1.0F;
    vk::set_cel_settings(settings);
    begin(gpu, scene);
    gpu.set_render_state(GpuRenderState::kZEnable, 1);
    gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
    gpu.set_render_state(GpuRenderState::kZFunc, value::kCompareLessEqual);
    gpu.set_render_state(GpuRenderState::kLighting, 0);
    gpu.set_render_state(GpuRenderState::kCullMode, value::kCullNone);
    gpu.set_pixel_shader(nullptr);
    gpu.set_vertex_format(0x004 | kFvfDiffuse); // (XYZRHW | DIFFUSE)
    gpu.set_texture(0, nullptr);
    gpu.set_stage_state(0, GpuStageState::kColourOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kColourArg1, value::kArgDiffuse);
    gpu.set_stage_state(0, GpuStageState::kAlphaOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kAlphaArg1, value::kArgDiffuse);
    std::vector<ScreenVertex> quads;
    const auto add_quad = [&quads](float x0, float x1, float y0, float y1, float z, uint32_t colour) {
        const std::array<ScreenVertex, 4> corners{{{.x = x0, .y = y0, .z = z, .rhw = 1.0F, .colour = colour},
                                                   {.x = x1, .y = y0, .z = z, .rhw = 1.0F, .colour = colour},
                                                   {.x = x1, .y = y1, .z = z, .rhw = 1.0F, .colour = colour},
                                                   {.x = x0, .y = y1, .z = z, .rhw = 1.0F, .colour = colour}}};
        for (const std::size_t corner : kQuadOrder) {
            quads.push_back(core::at(corners, corner));
        }
    };
    add_quad(0.0F, static_cast<float>(kSize), kWallTop, static_cast<float>(kSize), depth_of(kWallDistance), kWallColour);
    add_quad(kBoxLeft, kBoxRight, kBoxTop, kBoxBottom, depth_of(kNearBoxDistance), kNearBoxColour);
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(quads.size() / 3), quads.data(),
                         sizeof(ScreenVertex));
    if (listener) {
        rt::ViewDrawn view;
        view.rect = {0, 0, kSize, kSize};
        view.output_rect = view.rect;
        view.depth_range = {0.0F, 1.0F};
        auto& matrix = view.camera.projection;
        const std::array<float, kMatrixFloats> proj = projection();
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                core::at(core::at(matrix, row), column) = core::at(proj, (row * 4) + column);
            }
            core::at(core::at(view.camera.view, row), row) = 1.0F;
        }
        rt::world_view_drawn(view);
    }
    return finish(gpu, scene);
}

void check_world_lines(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    const Picture plain = draw_lines_scene(gpu, scene, false);
    const uint64_t passes = vk::cel_counts().outline_passes;
    const Picture lined = draw_lines_scene(gpu, scene, true);
    const bool ran = vk::cel_counts().outline_passes == passes + 1;
    std::size_t edge = 0;
    std::size_t inked = 0;
    std::size_t elsewhere = 0;
    std::size_t kept = 0;
    for (uint32_t py = 0; plain.read && lined.read && py < kSize; ++py) {
        for (uint32_t px = 0; px < kSize; ++px) {
            const auto x = static_cast<float>(px);
            const auto y = static_cast<float>(py);
            const bool on_box = x >= kBoxLeft && x < kBoxRight && y >= kBoxTop && y < kBoxBottom;
            const float from_edge = std::min({x - kBoxLeft, kBoxRight - 1.0F - x, y - kBoxTop, kBoxBottom - 1.0F - y});
            const bool darker = luma(lined.at(px, py)) < kInkDarker * luma(plain.at(px, py));
            if (on_box && from_edge >= 0.0F && from_edge < 1.0F && x > kBoxLeft + kCornerSkip && x < kBoxRight - kAway) {
                ++edge;
                inked += darker ? 1U : 0U;
            } else if ((on_box && from_edge > kAway) ||
                       (!on_box && (x < kBoxLeft - kAway || x > kBoxRight + kCornerSkip || y < kBoxTop - kAway ||
                                    y > kBoxBottom + kCornerSkip) &&
                        y > kWallTop + kAway + 1.0F)) {
                ++elsewhere;
                kept += std::abs(luma(lined.at(px, py)) - luma(plain.at(px, py))) <= kTolerance ? 1U : 0U;
            }
        }
    }
    tally->check(ran && edge > kLeastRing && inked * kPercent >= edge * kInkPercent && kept == elsewhere && lined.finite,
                 text::format("the world's lines: %zu of %zu pixels along the box's edge (on the box) inked, %zu of %zu inside "
                              "it, on the wall or the sky unchanged",
                              inked, edge, kept, elsewhere));
}

// --- flatter textures and the mode off ----------------------------------------------------------------------------------

Picture draw_textured(render::Gpu& gpu, const Scene& scene, GpuTexture* texture, const rt::CelSettings& settings)
{
    vk::set_cel_settings(settings);
    rt::set_cel_world({});
    Corners corners;
    constexpr float kRepeat = 4.0F;
    const std::array<std::array<float, 2>, 4> uv{{{0.0F, 0.0F}, {kRepeat, 0.0F}, {kRepeat, kRepeat}, {0.0F, kRepeat}}};
    for (std::size_t corner = 0; corner < 4; ++corner) {
        corners.colour.at(corner) = word_of({kMid, kMid, kMid}, kMid);
        corners.normal.at(corner) = {0.0F, 0.0F, -1.0F};
        corners.uv.at(corner) = core::at(uv, corner);
    }
    const std::vector<Vertex> vertices = quad_vertices(-kQuadHalf, kQuadHalf, -kQuadHalf, kQuadHalf, kQuadZ, corners, nullptr);
    begin(gpu, scene);
    set_state(gpu, scene.shader, texture, nullptr);
    const Drawn drawn = draw_triangles(gpu, vertices, {});
    const Picture picture = finish(gpu, scene);
    release(gpu, drawn);
    return picture;
}

float spread_of(const Picture& picture)
{
    double sum = 0.0;
    double squares = 0.0;
    std::size_t count = 0;
    for (uint32_t py = kInner; py < kSize - kInner; ++py) {
        for (uint32_t px = kInner; px < kSize - kInner; ++px) {
            const double value = luma(picture.at(px, py));
            sum += value;
            squares += value * value;
            ++count;
        }
    }
    const double mean = sum / static_cast<double>(std::max<std::size_t>(count, 1));
    return static_cast<float>(std::sqrt(std::max(0.0, (squares / static_cast<double>(std::max<std::size_t>(count, 1))) - (mean * mean))));
}

GpuTexture* checker_texture(render::Gpu& gpu)
{
    constexpr uint32_t kSide = 32;
    constexpr uint32_t kLevels = 6;
    return make_texture(gpu, kSide, kLevels, [](const TexelAt& at) {
        constexpr uint32_t kSquare = 4;
        if (at.level >= 2) {
            return kCheckerMean; // (a box filter of 4 x 4 squares: their mean)
        }
        const uint32_t square = kSquare >> at.level;
        const bool light = (((at.column / square) + (at.row / square)) % 2) == 0;
        return light ? kCheckerLight : kCheckerDark;
    });
}

void check_flatten_and_off(render::Gpu& gpu, const Scene& scene, const Picture& before_any, Tally* tally)
{
    GpuTexture* checker = checker_texture(gpu);
    rt::CelSettings flat = settings_with(4);
    rt::CelSettings sharp = flat;
    flat.flatten = kFlatten;
    sharp.flatten = 0.0F;
    const float sharp_spread = spread_of(draw_textured(gpu, scene, checker, sharp));
    const float flat_spread = spread_of(draw_textured(gpu, scene, checker, flat));
    tally->check(flat_spread < kFlatterSpread * sharp_spread,
                 text::format("rt_cel_flatten 0.8: a checkerboard's spread %.4f against %.4f without", static_cast<double>(flat_spread),
                              static_cast<double>(sharp_spread)));
    // the mode off: the same bytes as before it was ever on
    rt::CelSettings off;
    off.on = false;
    const Picture after = draw_textured(gpu, scene, checker, off);
    gpu.release_texture(checker);
    const bool same = before_any.read && after.read && before_any.pixels.size() == after.pixels.size();
    std::size_t differ = 0;
    for (std::size_t pixel = 0; same && pixel < after.pixels.size(); ++pixel) {
        differ += core::at(after.pixels, pixel) != core::at(before_any.pixels, pixel) ? 1U : 0U;
    }
    tally->check(same && differ == 0, text::format("rt_cel=0: the same picture as before the mode was ever on (%zu pixels differ)", differ));
    // and the original's formula: 2 x v0 x the texel
    std::size_t judged = 0;
    std::size_t right = 0;
    for (uint32_t py = kInner; after.read && py < kSize - kInner; ++py) {
        for (uint32_t px = kInner; px < kSize - kInner; ++px) {
            const float got = core::at(after.at(px, py), 0);
            const bool light = std::abs(got - (2.0F * as_byte(kMid) * (static_cast<float>(0xCC) / kByteMax))) <= kTolerance;
            const bool dark = std::abs(got - (2.0F * as_byte(kMid) * (static_cast<float>(0x33) / kByteMax))) <= kTolerance;
            const bool mixed = got > (2.0F * as_byte(kMid) * 0.2F) && got < (2.0F * as_byte(kMid) * 0.8F); // (the mips' blend)
            ++judged;
            right += light || dark || mixed ? 1U : 0U;
        }
    }
    tally->check(judged > 0 && right == judged, "rt_cel=0: the original's shader (2 x v0 x the texel)");
}

// --- the cost --------------------------------------------------------------------------------------------------------------

void measure_cost(render::Gpu& gpu, render::GpuPixelShader* shader, GpuTexture* white)
{
    constexpr uint32_t kWide = 2560;
    constexpr uint32_t kTall = 1440;
    constexpr int kLayers = 50;
    GpuSurface* target = nullptr;
    GpuSurface* depth = nullptr;
    if (gpu.create_render_target({.width = kWide, .height = kTall, .format = value::kFormatA8R8G8B8, .samples = 0, .quality = 0,
                                  .lockable_or_discard = true},
                                 &target) != value::kResultOk ||
        gpu.create_depth_surface({.width = kWide, .height = kTall, .format = value::kFormatD24S8, .samples = 0, .quality = 0,
                                  .lockable_or_discard = false},
                                 &depth) != value::kResultOk) {
        return;
    }
    vk::Backend& state = vk::backend();
    Traced traced;
    if (state.context.ray_tracing_enabled()) {
        const std::scoped_lock lock(state.mutex);
        if (make_traced(state, &traced)) {
            vk::cel_set_scene_locked(traced.top);
        }
    }
    VkQueryPool pool = VK_NULL_HANDLE;
    const VkQueryPoolCreateInfo pool_info{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                          .pNext = nullptr,
                                          .flags = 0,
                                          .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                          .queryCount = 2,
                                          .pipelineStatistics = 0};
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    if (vkCreateQueryPool(state.context.device(), &pool_info, nullptr, &pool) != VK_SUCCESS) {
        pool = VK_NULL_HANDLE;
    }
    struct Mode {
        bool cel = false;
        bool lit = false;
        bool lines = false;
        bool traced = true;
    };
    const auto stamp = [&state, pool](uint32_t which) {
        const std::scoped_lock lock(state.mutex);
        VkCommandBuffer commands = vk::outside_pass(state);
        if (which == 0) {
            vkCmdResetQueryPool(commands, pool, 0, 2);
        }
        vkCmdWriteTimestamp(commands, which == 0 ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool,
                            which);
    };
    // the device's time per full-screen layer (a depth clear, the quad, and the outlines' pass when asked)
    const auto layer_time = [&](const Mode& mode) {
        rt::CelSettings settings = settings_with(3);
        settings.on = mode.cel;
        settings.outline_width = mode.lines ? 1.0F : 0.0F;
        settings.rim = rt::kCelRim;
        settings.flatten = rt::kCelFlatten;
        vk::set_cel_settings(settings);
        rt::set_cel_world({.relit = true, .sun = kSun, .toward = kSunToward});
        {
            const std::scoped_lock lock(state.mutex);
            vk::cel_set_scene_locked(mode.traced ? traced.top : VK_NULL_HANDLE);
        }
        Corners corners;
        std::vector<rt::CelWords> stream;
        for (std::size_t corner = 0; corner < 4; ++corner) {
            corners.colour.at(corner) = kGreyVertex;
            corners.normal.at(corner) = kTiltedNormal;
            corners.stream.at(corner) = {word_of(scale(kAmbient, kMid), 1.0F), word_of({kMid, kMid, kMid}, 0.0F), word_of({}, 1.0F)};
        }
        const std::vector<Vertex> vertices =
            quad_vertices(-400.0F, 400.0F, -400.0F, 400.0F, 120.0F, corners, mode.lit ? nullptr : &stream);
        const Light light{.toward = kObjectLight, .colour = {0.8F, 0.8F, 0.8F}};
        Drawn drawn;
        drawn.vertices = buffer_of(gpu, std::as_bytes(std::span(vertices)));
        if (!mode.lit) {
            drawn.stream = buffer_of(gpu, std::as_bytes(std::span(stream)));
        }
        rt::ViewDrawn view;
        view.rect = {0, 0, kWide, kTall};
        view.output_rect = view.rect;
        view.depth_range = {0.0F, 1.0F};
        const std::array<float, kMatrixFloats> proj = projection();
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                core::at(core::at(view.camera.projection, row), column) = core::at(proj, (row * 4) + column);
            }
        }
        const auto layer = [&]() {
            gpu.clear(0, nullptr, value::kClearDepth, 0, 1.0F, 0);
            set_state(gpu, shader, white, mode.lit ? &light : nullptr);
            gpu.set_stream_source(0, drawn.vertices, 0, sizeof(Vertex));
            gpu.set_cel_source({.vertices = drawn.vertices, .offset = 0, .stream = drawn.stream});
            gpu.draw(render::GpuPrimitive::kTriangleList, 0, static_cast<uint32_t>(vertices.size() / 3));
            if (mode.lines) {
                rt::world_view_drawn(view);
            }
        };
        gpu.begin_scene();
        gpu.set_render_target(0, target);
        gpu.set_depth_surface(depth);
        gpu.set_viewport({.x = 0, .y = 0, .width = kWide, .height = kTall, .min_z = 0.0F, .max_z = 1.0F});
        layer(); // (the pipelines made first)
        stamp(0);
        for (int each = 0; each < kLayers; ++each) {
            layer();
        }
        stamp(1);
        std::array<uint64_t, 2> stamps{};
        {
            const std::scoped_lock lock(state.mutex);
            vk::finish_work(state);
            vkGetQueryPoolResults(state.context.device(), pool, 0, 2, sizeof(stamps), stamps.data(), sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        }
        gpu.set_cel_source({});
        gpu.set_render_target(0, nullptr);
        gpu.set_depth_surface(nullptr);
        gpu.end_scene();
        release(gpu, drawn);
        constexpr double kNanosecondsPerMillisecond = 1.0e6;
        return static_cast<double>(stamps.back() - stamps.front()) * static_cast<double>(properties.limits.timestampPeriod) /
               kNanosecondsPerMillisecond / kLayers;
    };
    if (pool != VK_NULL_HANDLE) {
        const double original = layer_time({.cel = false});
        const double bands = layer_time({.cel = true, .traced = false});
        const double world = layer_time({.cel = true});
        const double objects = layer_time({.cel = true, .lit = true});
        const double lined = layer_time({.cel = true, .lines = true});
        text::print("  cost at %ux%u on the device, a full-screen layer: the original's %.3f ms; the world in bands %.3f ms, "
                    "with the traced visibility%s %.3f ms; objects lit in bands, the rim and the hull %.3f ms; the world with "
                    "the outlines' pass %.3f ms - the mode's own: bands %+.3f ms, the rays %+.3f ms, the lines %+.3f ms\n",
                    kWide, kTall, original, bands, traced.top != VK_NULL_HANDLE ? "" : " (no ray tracing: none)", world, objects,
                    lined, bands - original, world - bands, lined - world);
        vkDestroyQueryPool(state.context.device(), pool, nullptr);
    }    if (traced.top != VK_NULL_HANDLE) {
        const std::scoped_lock lock(state.mutex);
        vk::cel_set_scene_locked(VK_NULL_HANDLE);
        release_traced(state, &traced);
    }
    gpu.release_surface(depth);
    gpu.release_surface(target);
}

// --- the device ----------------------------------------------------------------------------------------------------------

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
    vk::set_context_options(
        {.validation = options.validation, .device_index = options.device, .on_message = print_line, .ray_tracing = true}, window);
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
    const long made =
        context.create_device({.width = kWindowSize, .height = kWindowSize, .hardware_vertices = true, .samples = 0, .sample_quality = 0});
    if (made != value::kResultOk) {
        text::print("create_device failed (%ld)\n", made);
        context.shutdown();
        return false;
    }
    context.mark_ready();
    text::print("device: %s (ray tracing %s)\n", context.adapter().description.data(),
                vk::backend().context.ray_tracing_enabled() ? "on" : "off");
    return true;
}

// The backend's log lines that say something went wrong, shown with the checks.
void show_log_line(const char* line)
{
    const std::string_view text(line);
    if (text.find("couldn't") != std::string_view::npos || text.find("skipped") != std::string_view::npos ||
        text.find("no pipeline") != std::string_view::npos || text.find("refused") != std::string_view::npos) {
        text::print("  log: %s\n", line);
    }
}

int run(const Options& options, SDL_Window* window)
{
    set_log_listener(show_log_line);
    rt::set_depth_sampling_wanted(true); // (before the device: the depth surfaces made samplable - the world's lines)
    if (!open_device(options, window)) {
        return vk::loader_version() == 0 ? 2 : 1;
    }
    Tally tally;
    render::Gpu& gpu = vk::gpu();
    check_twins(&tally);
    Scene scene;
    const bool made =
        gpu.create_pixel_shader(kShader.data(), static_cast<uint32_t>(kShader.size()), &scene.shader) == value::kResultOk &&
        gpu.create_render_target({.width = kSize, .height = kSize, .format = value::kFormatA16B16G16R16F, .samples = 0,
                                  .quality = 0, .lockable_or_discard = true},
                                 &scene.target) == value::kResultOk &&
        gpu.create_depth_surface({.width = kSize, .height = kSize, .format = value::kFormatD24S8, .samples = 0, .quality = 0,
                                  .lockable_or_discard = false},
                                 &scene.depth) == value::kResultOk;
    scene.white = made ? white_texture(gpu) : nullptr;
    tally.check(made && scene.white != nullptr, "the material shader, a 16-bit float target and its depth made");
    if (made && scene.white != nullptr) {
        // (before the mode is ever on: the picture rt_cel=0 must give again)
        GpuTexture* checker = checker_texture(gpu);
        const Picture before_any = draw_textured(gpu, scene, checker, rt::CelSettings{});
        gpu.release_texture(checker);
        for (uint32_t bands = rt::kCelLeastBands; bands <= rt::kCelMostBands; ++bands) {
            check_stream_bands(gpu, scene, bands, &tally);
        }
        check_bake_bands(gpu, scene, &tally);
        check_lit(gpu, scene, &tally);
        check_shadows(gpu, scene, &tally);
        check_hull(gpu, scene, &tally);
        check_world_lines(gpu, scene, &tally);
        check_flatten_and_off(gpu, scene, before_any, &tally);
        const vk::CelCounts counts = vk::cel_counts();
        text::print("  draws: %llu world with the stream, %llu world banding the bake, %llu lit, %llu traced, %llu hulls, "
                    "%llu outline passes\n",
                    static_cast<unsigned long long>(counts.world_stream), static_cast<unsigned long long>(counts.world_bake),
                    static_cast<unsigned long long>(counts.lit), static_cast<unsigned long long>(counts.shadowed),
                    static_cast<unsigned long long>(counts.hulls), static_cast<unsigned long long>(counts.outline_passes));
        if (options.perf) {
            measure_cost(gpu, scene.shader, scene.white);
        }
    }
    vk::set_cel_settings({});
    rt::set_cel_world({});
    if (scene.white != nullptr) {
        gpu.release_texture(scene.white);
    }
    for (GpuSurface* surface : {scene.target, scene.depth}) {
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
    text::print("thug2_vulkan_cel_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan cel shading check", kWindowSize, kWindowSize, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
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
