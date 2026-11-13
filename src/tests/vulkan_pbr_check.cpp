// NOT ORIGINAL - thug2_vulkan_pbr_check.exe: the Vulkan Gpu's per-pixel lighting and surface maps ([Graphics] rt_pbr,
// gpu_vulkan/vulkan_surface.h; the shaders ff_shaders.cpp writes) tried on this machine against the same light worked
// out on the processor, pixel by pixel.
//
// A quad is drawn through render::Gpu with the game's single-texture material shader (kTextureVertexX4) into a 16-bit
// float target and read back; the processor ray-casts each pixel's centre (Direct3D 9's: the backend's half-pixel
// shift) onto the quad and computes the same formulas: the tangent frame from the quad's own u / v gradients (exactly
// what a per-mesh tangent pass would give a flat quad), the map's normal bent through it, Direct3D 9's diffuse lighting
// per pixel (lit) or the bake scaled toward the sun and the sky (baked), GGX specular. Pixels a texel edge or the quad's
// edge passes near (point sampling would pick either side) are left out. Cases:
//   - lit by a directional light: a quad facing the camera, one turned 35 / 20 degrees (a frame not on the screen's
//     axes), its texture coordinates mirrored in u, rotated 90 degrees, and both mirrored;
//   - baked (vertex colours, the sun given by rt::set_sun_override): sunlit and in the bake's shade;
//   - a vertex format without normals (the triangle's own normal from the screen derivatives);
//   - texture coordinates that don't change (no frame: the surface's normal), no map (objects lit per pixel);
//   - rt_pbr off: the original's shader - the same picture as Direct3D 9's vertex lighting, no specular;
//   - every pixel of every case finite (no NaN or infinity), validation (and synchronization validation) messages 0.
//   thug2_vulkan_pbr_check [--device N] [--display N] [--no-validation]
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
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/surface_maps.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_surface.h"
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

constexpr int kWindowSize = 128;
constexpr uint32_t kSize = 256;
constexpr int kBitsPerByte = 8;
constexpr std::size_t kHalfPixelBytes = 8;
constexpr uint32_t kHighByte = 8;
constexpr uint32_t kByteMask = 0xFF;
constexpr uint16_t kHalfSign = 0x8000;
constexpr uint32_t kTexels = 8; // the texture's and the map's side
constexpr float kFov = 1.7320508F; // 1 / tan(30 degrees)
constexpr float kNear = 1.0F;
constexpr float kFar = 100.0F;
constexpr float kDepth = 5.0F;
constexpr float kHalf = 1.6F; // the quad's half size
constexpr float kEdgeMargin = 0.03F;  // (of the quad: pixels nearer its edge left out)
constexpr float kTexelMargin = 0.06F; // (of a texel: pixels nearer a texel edge left out)
constexpr float kPi = 3.14159265F;
constexpr float kTolerance = 3.0e-3F;       // a pixel's largest difference (16-bit floats, interpolation)
constexpr float kWorstTolerance = 0.02F;    // the worst pixel's
constexpr double kShareWithin = 0.995;      // the pixels within kTolerance
constexpr uint32_t kFvfPosition = 0x002;
constexpr uint32_t kFvfNormal = 0x010;
constexpr uint32_t kFvfDiffuse = 0x040;
constexpr uint32_t kFvfTex1 = 0x100;
constexpr std::string_view kShader = "ps.1.1\ntex t0\nmul r0.rgb,t0,c0\n+mul_x2 r0.a,v0.a,t0.a\nmul_x4 r0.rgb,v0,r0\n";

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

// --- vectors ----------------------------------------------------------------------------------------------------------

Vec3 add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 scale(const Vec3& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(const Vec3& a, const Vec3& b) { return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]); }
Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
}
Vec3 normalise(const Vec3& a)
{
    const float length = std::sqrt(dot(a, a));
    return length > 0.0F ? scale(a, 1.0F / length) : a;
}
float clamp01(float v) { return std::clamp(v, 0.0F, 1.0F); }
// A colour as an 8-bit channel holds it (the vertex colours, the ambient, the texture).
float as_byte(float v) { return std::round(clamp01(v) * 255.0F) / 255.0F; }
Vec3 as_bytes(const Vec3& v) { return {as_byte(v[0]), as_byte(v[1]), as_byte(v[2])}; }
float smoothstep(float lo, float hi, float x)
{
    const float t = clamp01((x - lo) / (hi - lo));
    return t * t * (3.0F - (2.0F * t));
}

// --- the scene --------------------------------------------------------------------------------------------------------

// The quad: P = corner + s edge_u + t edge_v (s, t in 0 - 1); its texture coordinates uv = uv0 + s duv_s + t duv_t.
struct Quad {
    Vec3 corner{};
    Vec3 edge_s{};
    Vec3 edge_t{};
    std::array<float, 2> uv0{};
    std::array<float, 2> duv_s{};
    std::array<float, 2> duv_t{};
};

enum class Mapping : uint8_t { kPlain, kMirrored, kRotated, kBoth, kConstant };

Quad make_quad(float yaw, float pitch, Mapping mapping)
{
    const auto turn = [yaw, pitch](const Vec3& v) {
        const Vec3 y{(v[0] * std::cos(yaw)) + (v[2] * std::sin(yaw)), v[1], (-v[0] * std::sin(yaw)) + (v[2] * std::cos(yaw))};
        return Vec3{y[0], (y[1] * std::cos(pitch)) - (y[2] * std::sin(pitch)), (y[1] * std::sin(pitch)) + (y[2] * std::cos(pitch))};
    };
    Quad quad;
    const Vec3 centre{0.0F, 0.0F, kDepth};
    quad.edge_s = turn({2.0F * kHalf, 0.0F, 0.0F});
    quad.edge_t = turn({0.0F, -2.0F * kHalf, 0.0F}); // (t down the screen, as v down a texture)
    quad.corner = sub(centre, scale(add(quad.edge_s, quad.edge_t), 0.5F));
    switch (mapping) {
    case Mapping::kPlain:
        quad.uv0 = {0.0F, 0.0F}, quad.duv_s = {1.0F, 0.0F}, quad.duv_t = {0.0F, 1.0F};
        break;
    case Mapping::kMirrored:
        quad.uv0 = {1.0F, 0.0F}, quad.duv_s = {-1.0F, 0.0F}, quad.duv_t = {0.0F, 1.0F};
        break;
    case Mapping::kRotated:
        quad.uv0 = {0.0F, 1.0F}, quad.duv_s = {0.0F, -1.0F}, quad.duv_t = {1.0F, 0.0F};
        break;
    case Mapping::kBoth:
        quad.uv0 = {1.0F, 1.0F}, quad.duv_s = {-1.0F, 0.0F}, quad.duv_t = {0.0F, -1.0F};
        break;
    case Mapping::kConstant:
        quad.uv0 = {0.3F, 0.6F}, quad.duv_s = {0.0F, 0.0F}, quad.duv_t = {0.0F, 0.0F};
        break;
    }
    return quad;
}

Vec3 quad_normal(const Quad& quad)
{
    Vec3 n = normalise(cross(quad.edge_s, quad.edge_t));
    if (dot(n, quad.corner) > 0.0F) { // toward the eye
        n = scale(n, -1.0F);
    }
    return n;
}

// The map's and the texture's texels: normals tilted by texel, roughness stepped, the colour varied.
struct Texel {
    std::array<uint8_t, 4> map{};
    Vec3 colour{};
};

Texel texel_at(uint32_t a, uint32_t b)
{
    constexpr float kTiltStep = 0.11F;
    constexpr float kMiddle = 3.5F;
    constexpr float kRoughBase = 0.15F;
    constexpr float kRoughStep = 0.1F;
    const float nx = (static_cast<float>(a) - kMiddle) * kTiltStep;
    const float ny = (static_cast<float>(b) - kMiddle) * kTiltStep * 0.8F;
    const auto byte = [](float v) { return static_cast<uint8_t>(std::lround(std::clamp((v * 127.5F) + 127.5F, 0.0F, 255.0F))); };
    const float rough = kRoughBase + (kRoughStep * static_cast<float>((a + (2 * b)) % kTexels));
    return {.map = {byte(nx), byte(ny), static_cast<uint8_t>(std::lround(rough * 255.0F)), 128},
            .colour = as_bytes({0.35F + (0.07F * static_cast<float>(a)), 0.55F, 0.3F + (0.06F * static_cast<float>(b))})};
}

struct Light {
    Vec3 toward{}; // camera space (the view is the identity)
    Vec3 colour{};
};

struct Case {
    std::string name;
    float yaw = 0.0F;
    float pitch = 0.0F;
    Mapping mapping = Mapping::kPlain;
    bool lit = true;      // lighting on (objects), else the bake
    bool normals = true;  // the vertex format has normals
    bool map = true;      // the texture has a surface map
    bool pbr = true;      // rt_pbr on
    Vec3 bake{0.45F, 0.42F, 0.4F}; // the vertex colour
};

constexpr Vec3 kAmbient{0.15F, 0.15F, 0.18F};
constexpr Vec3 kMaterialAmbient{0.8F, 0.8F, 0.8F};
const Light kLight{.toward = normalise({-0.5F, 0.6F, -0.62F}), .colour = {0.9F, 0.85F, 0.8F}};
const Vec3 kSunToward = normalise({0.4F, 0.75F, -0.5F});
constexpr float kMapF0 = 0.04F;

float ggx(const Vec3& n, const Vec3& v, const Vec3& l, float rough, float f0)
{
    const float nl = std::max(dot(n, l), 0.0F);
    if (nl <= 0.0F) {
        return 0.0F;
    }
    const Vec3 h = normalise(add(v, l));
    const float nv = std::max(dot(n, v), 1.0e-4F);
    const float nh = std::max(dot(n, h), 0.0F);
    const float vh = std::max(dot(v, h), 0.0F);
    const float a = std::max(rough * rough, 0.0064F);
    const float a2 = a * a;
    const float q = (nh * nh * (a2 - 1.0F)) + 1.0F;
    const float d = a2 / (kPi * q * q);
    const float k = 0.5F * a;
    const float g = (nl / ((nl * (1.0F - k)) + k)) * (nv / ((nv * (1.0F - k)) + k));
    const float f = f0 + ((1.0F - f0) * std::pow(1.0F - vh, 5.0F));
    return std::min(kPi * d * g * f / (4.0F * nv), 16.0F);
}

// The colour the processor expects at a pixel, or false where the pixel isn't judged (off the quad, near an edge).
bool expected(const Case& given, const Quad& quad, uint32_t px, uint32_t py, Vec3* out)
{
    Case test = given;
    test.bake = as_bytes(given.bake);
    const Vec3 ambient = as_bytes(kAmbient);
    const float ndc_x = ((2.0F * static_cast<float>(px)) / static_cast<float>(kSize)) - 1.0F;
    const float ndc_y = 1.0F - ((2.0F * static_cast<float>(py)) / static_cast<float>(kSize));
    const Vec3 ray{ndc_x / kFov, ndc_y / kFov, 1.0F};
    // lambda ray = corner + s edge_s + t edge_t: Cramer's rule
    const Vec3 neg_s = scale(quad.edge_s, -1.0F);
    const Vec3 neg_t = scale(quad.edge_t, -1.0F);
    const float det = dot(ray, cross(neg_s, neg_t));
    if (std::fabs(det) < 1.0e-8F) {
        return false;
    }
    const float s = dot(ray, cross(quad.corner, neg_t)) / det;
    const float t = dot(ray, cross(neg_s, quad.corner)) / det;
    const float lambda = dot(quad.corner, cross(neg_s, neg_t)) / det;
    if (s < kEdgeMargin || s > 1.0F - kEdgeMargin || t < kEdgeMargin || t > 1.0F - kEdgeMargin || lambda <= 0.0F) {
        return false;
    }
    const Vec3 p = scale(ray, lambda);
    const float u = quad.uv0[0] + (s * quad.duv_s[0]) + (t * quad.duv_t[0]);
    const float v = quad.uv0[1] + (s * quad.duv_s[1]) + (t * quad.duv_t[1]);
    const float tu = u * static_cast<float>(kTexels);
    const float tv = v * static_cast<float>(kTexels);
    const float fu = tu - std::floor(tu);
    const float fv = tv - std::floor(tv);
    if (fu < kTexelMargin || fu > 1.0F - kTexelMargin || fv < kTexelMargin || fv > 1.0F - kTexelMargin) {
        return false;
    }
    const auto a = static_cast<uint32_t>(std::clamp(tu, 0.0F, static_cast<float>(kTexels - 1)));
    const auto b = static_cast<uint32_t>(std::clamp(tv, 0.0F, static_cast<float>(kTexels - 1)));
    const Texel texel = texel_at(a, b);
    const Vec3 geo = quad_normal(quad);
    Vec3 n = geo;
    float rough = vk::RtPbrSettings{}.roughness;
    float f0 = vk::RtPbrSettings{}.f0;
    if (test.map && test.pbr) {
        rough = static_cast<float>(texel.map[2]) / 255.0F;
        f0 = kMapF0;
        const float x = (static_cast<float>(texel.map[0]) - 127.5F) / 127.5F;
        const float y = (static_cast<float>(texel.map[1]) - 127.5F) / 127.5F;
        const Vec3 nt{x, y, std::sqrt(std::max(1.0F - (x * x) - (y * y), 0.0F))};
        // the gradients of u and v along the quad (its dual basis), scaled by the longer
        const float area = dot(geo, cross(quad.edge_s, quad.edge_t));
        const Vec3 grad_s = scale(cross(quad.edge_t, geo), 1.0F / area);
        const Vec3 grad_t = scale(cross(geo, quad.edge_s), 1.0F / area);
        const Vec3 tangent = add(scale(grad_s, quad.duv_s[0]), scale(grad_t, quad.duv_t[0]));
        const Vec3 bitangent = add(scale(grad_s, quad.duv_s[1]), scale(grad_t, quad.duv_t[1]));
        const float longest = std::max(dot(tangent, tangent), dot(bitangent, bitangent));
        if (longest > 0.0F) {
            const float inv = 1.0F / std::sqrt(longest);
            n = normalise(add(add(scale(tangent, inv * nt[0]), scale(bitangent, inv * nt[1])), scale(geo, nt[2])));
        }
    }
    rough = std::clamp(rough, 0.08F, 1.0F);
    const Vec3 view = normalise(scale(p, -1.0F));
    const vk::RtPbrSettings settings;
    Vec3 v0{};
    Vec3 specular{};
    if (test.lit) {
        const float nl = std::max(dot(test.pbr ? n : geo, kLight.toward), 0.0F);
        for (std::size_t c = 0; c < 3; ++c) {
            v0.at(c) = clamp01((kMaterialAmbient.at(c) * ambient.at(c)) + (test.bake.at(c) * kLight.colour.at(c) * nl));
        }
        if (test.pbr) {
            specular = scale(kLight.colour, ggx(n, view, kLight.toward, rough, f0));
        }
    } else if (test.pbr) {
        const float level = 2.0F * dot(test.bake, {0.2126F, 0.7152F, 0.0722F});
        const float sunlit = smoothstep(settings.shade_low, settings.shade_high, level);
        const float share = std::max(settings.ambient_share, 0.05F);
        const float sun_detail = (share + std::max(dot(n, kSunToward), 0.0F)) / (share + std::max(dot(geo, kSunToward), 0.0F));
        const Vec3 up{0.0F, 1.0F, 0.0F};
        const float sky_detail = (1.0F + dot(n, up)) / std::max(1.0F + dot(geo, up), 0.05F);
        const float mixed = std::lerp(1.0F, sky_detail, settings.sky_detail);
        const float detail = std::clamp(std::lerp(mixed, sun_detail, sunlit), settings.detail_low, settings.detail_high);
        for (std::size_t c = 0; c < 3; ++c) {
            v0.at(c) = clamp01(test.bake.at(c) * detail);
        }
        specular = scale(Vec3{1.0F, 1.0F, 1.0F}, ggx(n, view, kSunToward, rough, f0) * sunlit);
    } else {
        v0 = test.bake;
    }
    for (std::size_t c = 0; c < 3; ++c) {
        // r0 = t0 x c0 (0.5) x v0 x 4, + the specular x its strength (1), clamped
        out->at(c) = clamp01((2.0F * texel.colour.at(c) * v0.at(c)) + (specular.at(c) * settings.specular));
    }
    return true;
}

// --- drawing ----------------------------------------------------------------------------------------------------------

struct Vertex {
    std::array<float, 3> position;
    std::array<float, 3> normal;
    uint32_t colour;
    std::array<float, 2> uv;
};
struct VertexNoNormal {
    std::array<float, 3> position;
    uint32_t colour;
    std::array<float, 2> uv;
};

uint32_t argb(const Vec3& colour)
{
    const auto byte = [](float v) { return static_cast<uint32_t>(std::lround(clamp01(v) * 255.0F)); };
    constexpr uint32_t kOpaque = 0x80000000U; // (alpha 128: 1 after the shader's doubling)
    return kOpaque | (byte(colour[0]) << 16) | (byte(colour[1]) << 8) | byte(colour[2]);
}

// The texture (8 x 8 A8R8G8B8) and its surface map.
GpuTexture* make_texture(render::Gpu& gpu, bool map)
{
    GpuTexture* texture = nullptr;
    if (gpu.create_texture({.width = kTexels, .height = kTexels, .levels = 1, .usage = 0, .format = value::kFormatA8R8G8B8,
                            .pool = value::kPoolManaged},
                           &texture) != value::kResultOk) {
        return nullptr;
    }
    render::GpuLockedRect locked{};
    gpu.lock_texture(texture, 0, &locked, nullptr, 0);
    const std::span<uint8_t> bytes(static_cast<uint8_t*>(locked.bits),
                                   (static_cast<std::size_t>(locked.pitch) * (kTexels - 1)) + (kTexels * 4));
    std::vector<uint8_t> maps(static_cast<std::size_t>(kTexels) * kTexels * 4);
    for (uint32_t b = 0; b < kTexels; ++b) {
        for (uint32_t a = 0; a < kTexels; ++a) {
            const Texel texel = texel_at(a, b);
            const std::size_t at = (static_cast<std::size_t>(b) * static_cast<std::size_t>(locked.pitch)) + (a * 4);
            core::at(bytes, at) = static_cast<uint8_t>(std::lround(texel.colour[2] * 255.0F));
            core::at(bytes, at + 1) = static_cast<uint8_t>(std::lround(texel.colour[1] * 255.0F));
            core::at(bytes, at + 2) = static_cast<uint8_t>(std::lround(texel.colour[0] * 255.0F));
            core::at(bytes, at + 3) = 128;
            for (std::size_t c = 0; c < 4; ++c) {
                core::at(maps, (((static_cast<std::size_t>(b) * kTexels) + a) * 4) + c) = core::at(texel.map, c);
            }
        }
    }
    gpu.unlock_texture(texture, 0);
    if (map && render::surface::surface_maps() != nullptr) {
        render::surface::surface_maps()->attach(texture, {.width = kTexels, .height = kTexels, .material_class = 2, .f0 = kMapF0},
                                                maps);
    }
    return texture;
}

std::array<float, 16> projection()
{
    const float q = kFar / (kFar - kNear);
    return {kFov, 0, 0, 0, 0, kFov, 0, 0, 0, 0, q, 1, 0, 0, -kNear * q, 0};
}

void draw_case(render::Gpu& gpu, const Case& test, const Quad& quad, GpuTexture* texture, render::GpuPixelShader* shader)
{
    constexpr std::array<float, 16> kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    gpu.clear(0, nullptr, value::kClearTarget, 0, 1.0F, 0);
    gpu.set_render_state(GpuRenderState::kZEnable, 0);
    gpu.set_render_state(GpuRenderState::kZWriteEnable, 0);
    gpu.set_render_state(GpuRenderState::kCullMode, value::kCullNone);
    gpu.set_render_state(GpuRenderState::kAlphaTestEnable, 0);
    gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    gpu.set_render_state(GpuRenderState::kFogEnable, 0);
    gpu.set_render_state(GpuRenderState::kSpecularEnable, 0);
    gpu.set_render_state(GpuRenderState::kLighting, test.lit ? 1 : 0);
    gpu.set_render_state(GpuRenderState::kColourVertex, 1);
    gpu.set_render_state(GpuRenderState::kDiffuseMaterialSource, value::kSourceColour1);
    gpu.set_render_state(GpuRenderState::kAmbientMaterialSource, value::kSourceMaterial);
    gpu.set_render_state(GpuRenderState::kEmissiveMaterialSource, value::kSourceMaterial);
    gpu.set_render_state(GpuRenderState::kAmbient, argb(kAmbient) & 0x00FFFFFFU);
    gpu.set_render_state(GpuRenderState::kNormalizeNormals, 1);
    gpu.set_transform(render::GpuTransform::kWorld, kIdentity.data());
    gpu.set_transform(render::GpuTransform::kView, kIdentity.data());
    const std::array<float, 16> proj = projection();
    gpu.set_transform(render::GpuTransform::kProjection, proj.data());
    gpu.set_material({.diffuse = {1, 1, 1, 1},
                      .ambient = {kMaterialAmbient[0], kMaterialAmbient[1], kMaterialAmbient[2], 1},
                      .specular = {},
                      .emissive = {},
                      .power = 0});
    render::GpuLight light{};
    light.type = value::kLightDirectional;
    light.diffuse = {kLight.colour[0], kLight.colour[1], kLight.colour[2], 1};
    light.direction = scale(kLight.toward, -1.0F);
    gpu.set_light(0, light);
    gpu.enable_light(0, true);
    for (uint32_t index = 1; index < 8; ++index) {
        gpu.enable_light(index, false);
    }
    gpu.set_texture(0, texture);
    for (const GpuSamplerState state : {GpuSamplerState::kMinFilter, GpuSamplerState::kMagFilter, GpuSamplerState::kMipFilter}) {
        gpu.set_sampler_state(0, state, value::kFilterPoint);
    }
    gpu.set_sampler_state(0, GpuSamplerState::kAddressU, value::kAddressClamp);
    gpu.set_sampler_state(0, GpuSamplerState::kAddressV, value::kAddressClamp);
    gpu.set_stage_state(0, GpuStageState::kTexCoordIndex, 0);
    gpu.set_pixel_shader(shader);
    const std::array<float, 4> c0{0.5F, 0.5F, 0.5F, 0.5F};
    gpu.set_pixel_constants(0, c0.data(), 1);
    const Vec3 normal = quad_normal(quad);
    const uint32_t colour = argb(test.bake);
    std::array<Vertex, 6> vertices{};
    std::array<VertexNoNormal, 6> plain{};
    constexpr std::array<std::array<float, 2>, 6> kCorners{{{0, 0}, {1, 0}, {0, 1}, {1, 0}, {1, 1}, {0, 1}}};
    for (std::size_t i = 0; i < kCorners.size(); ++i) {
        const float s = kCorners.at(i)[0];
        const float t = kCorners.at(i)[1];
        const Vec3 p = add(quad.corner, add(scale(quad.edge_s, s), scale(quad.edge_t, t)));
        const std::array<float, 2> uv{quad.uv0[0] + (s * quad.duv_s[0]) + (t * quad.duv_t[0]),
                                      quad.uv0[1] + (s * quad.duv_s[1]) + (t * quad.duv_t[1])};
        vertices.at(i) = {.position = p, .normal = normal, .colour = colour, .uv = uv};
        plain.at(i) = {.position = p, .colour = colour, .uv = uv};
    }
    if (test.normals) {
        gpu.set_vertex_format(kFvfPosition | kFvfNormal | kFvfDiffuse | kFvfTex1);
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, 2, vertices.data(), sizeof(Vertex));
    } else {
        gpu.set_vertex_format(kFvfPosition | kFvfDiffuse | kFvfTex1);
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, 2, plain.data(), sizeof(VertexNoNormal));
    }
}

void run_case(render::Gpu& gpu, GpuSurface* target, const Case& test, render::GpuPixelShader* shader, Tally* tally)
{
    vk::RtPbrSettings settings;
    settings.on = test.pbr;
    vk::set_rt_pbr_settings(settings);
    GpuTexture* texture = make_texture(gpu, test.map);
    const Quad quad = make_quad(test.yaw, test.pitch, test.mapping);
    gpu.begin_scene();
    gpu.set_render_target(0, target);
    gpu.set_depth_surface(nullptr);
    gpu.set_viewport({.x = 0, .y = 0, .width = kSize, .height = kSize, .min_z = 0.0F, .max_z = 1.0F});
    draw_case(gpu, test, quad, texture, shader);
    gpu.set_render_target(0, nullptr);
    gpu.end_scene();
    std::vector<uint8_t> bytes;
    const bool read = vk::read_surface(target, &bytes) && bytes.size() >= static_cast<std::size_t>(kSize) * kSize * kHalfPixelBytes;
    gpu.release_texture(texture);
    if (!read) {
        tally->check(false, test.name + ": read back");
        return;
    }
    std::size_t judged = 0;
    std::size_t within = 0;
    std::size_t finite = 0;
    float worst = 0.0F;
    std::string worst_at;
    for (uint32_t py = 0; py < kSize; ++py) {
        for (uint32_t px = 0; px < kSize; ++px) {
            const std::size_t pixel = (static_cast<std::size_t>(py) * kSize) + px;
            Vec3 got{};
            bool all_finite = true;
            for (std::size_t c = 0; c < 3; ++c) {
                const std::size_t at = (pixel * kHalfPixelBytes) + (c * 2);
                got.at(c) = from_half(static_cast<uint16_t>(core::at(bytes, at) | (core::at(bytes, at + 1) << kHighByte)));
                all_finite = all_finite && std::isfinite(got.at(c));
            }
            finite += all_finite ? 1U : 0U;
            Vec3 want{};
            if (!expected(test, quad, px, py, &want)) {
                continue;
            }
            ++judged;
            float difference = 0.0F;
            for (std::size_t c = 0; c < 3; ++c) {
                difference = std::max(difference, std::fabs(got.at(c) - want.at(c)));
            }
            if (!all_finite || difference > worst) {
                worst = all_finite ? difference : std::numeric_limits<float>::infinity();
                worst_at = text::format(" (at %u, %u: %.4f %.4f %.4f, expected %.4f %.4f %.4f)", px, py,
                                        static_cast<double>(got[0]), static_cast<double>(got[1]), static_cast<double>(got[2]),
                                        static_cast<double>(want[0]), static_cast<double>(want[1]), static_cast<double>(want[2]));
            }
            within += difference <= kTolerance ? 1U : 0U;
        }
    }
    const double share = judged != 0 ? static_cast<double>(within) / static_cast<double>(judged) : 0.0;
    tally->check(judged > 10000 && share >= kShareWithin && worst <= kWorstTolerance,
                 text::format("%s: %zu pixels against the processor, %.2f%% within %.3f, the worst %.4f%s", test.name.c_str(),
                              judged, share * 100.0, static_cast<double>(kTolerance), static_cast<double>(worst),
                              worst > kTolerance ? worst_at.c_str() : ""));
    tally->check(finite == static_cast<std::size_t>(kSize) * kSize, test.name + ": every pixel finite");
}

void check_bake_moves(render::Gpu& gpu, GpuSurface* target, render::GpuPixelShader* shader, Tally* tally)
{
    // The same quad with the map and without: the map's detail must change the picture (a check that the map is used).
    Case with{.name = "map used", .lit = false, .bake = {0.5F, 0.5F, 0.5F}};
    Case without = with;
    without.map = false;
    std::array<std::vector<uint8_t>, 2> pictures;
    for (std::size_t i = 0; i < 2; ++i) {
        vk::set_rt_pbr_settings({.on = true});
        GpuTexture* texture = make_texture(gpu, i == 0);
        gpu.begin_scene();
        gpu.set_render_target(0, target);
        gpu.set_depth_surface(nullptr);
        gpu.set_viewport({.x = 0, .y = 0, .width = kSize, .height = kSize, .min_z = 0.0F, .max_z = 1.0F});
        draw_case(gpu, i == 0 ? with : without, make_quad(0.0F, 0.0F, Mapping::kPlain), texture, shader);
        gpu.set_render_target(0, nullptr);
        gpu.end_scene();
        (void)vk::read_surface(target, &pictures.at(i));
        gpu.release_texture(texture);
    }
    std::size_t differ = 0;
    for (std::size_t i = 0; i < pictures[0].size() && i < pictures[1].size(); i += 2) {
        differ += pictures[0].at(i) != pictures[1].at(i) ? 1U : 0U;
    }
    tally->check(differ > 1000, text::format("the map changes the baked picture (%zu channels differ)", differ));
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
    text::print("validation layer: %s\n", vk::backend().context.validation_on() ? "on" : "off (not installed, or not asked for)");
    context.configure({.width = kWindowSize,
                       .height = kWindowSize,
                       .windowed = true,
                       .vsync = true,
                       .samples = 0,
                       .sample_quality = 0,
                       .colour_format = value::kFormatX8R8G8B8,
                       .depth_format = value::kFormatD24S8});
    const long made = context.create_device(
        {.width = kWindowSize, .height = kWindowSize, .hardware_vertices = true, .samples = 0, .sample_quality = 0});
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
    render::GpuPixelShader* shader = nullptr;
    GpuSurface* target = nullptr;
    const bool made = gpu.create_pixel_shader(kShader.data(), static_cast<uint32_t>(kShader.size()), &shader) == value::kResultOk &&
                      gpu.create_render_target({.width = kSize,
                                                .height = kSize,
                                                .format = value::kFormatA16B16G16R16F,
                                                .samples = 0,
                                                .quality = 0,
                                                .lockable_or_discard = true},
                                               &target) == value::kResultOk;
    tally.check(made, "the material shader and a 16-bit float target made");
    if (made) {
        render::rt::set_sun_override(&kSunToward);
        constexpr float kYaw = 0.61F;   // 35 degrees
        constexpr float kPitch = 0.35F; // 20 degrees
        const Vec3 dark{0.12F, 0.11F, 0.1F};
        const std::array<Case, 12> cases{{
            {.name = "lit, facing the camera"},
            {.name = "lit, turned 35 / 20 degrees", .yaw = kYaw, .pitch = kPitch},
            {.name = "lit, u mirrored", .yaw = kYaw, .pitch = kPitch, .mapping = Mapping::kMirrored},
            {.name = "lit, mapping turned 90 degrees", .yaw = -kYaw, .pitch = kPitch, .mapping = Mapping::kRotated},
            {.name = "lit, u and v mirrored", .yaw = kYaw, .pitch = -kPitch, .mapping = Mapping::kBoth},
            {.name = "baked, sunlit", .yaw = kYaw, .pitch = kPitch, .lit = false},
            {.name = "baked, in the shade", .yaw = -kYaw, .pitch = kPitch, .mapping = Mapping::kMirrored, .lit = false, .bake = dark},
            {.name = "baked, no normals in the vertices", .yaw = kYaw, .pitch = -kPitch, .lit = false, .normals = false},
            {.name = "lit, texture coordinates constant (no frame)", .yaw = kYaw, .mapping = Mapping::kConstant},
            {.name = "lit, no map (per-pixel lights)", .yaw = kYaw, .pitch = kPitch, .map = false},
            {.name = "rt_pbr off, lit: the original's", .yaw = kYaw, .pitch = kPitch, .pbr = false},
            {.name = "rt_pbr off, baked: the original's", .yaw = kYaw, .lit = false, .pbr = false},
        }};
        for (const Case& test : cases) {
            run_case(gpu, target, test, shader, &tally);
        }
        check_bake_moves(gpu, target, shader, &tally);
        render::rt::set_sun_override(nullptr);
        tally.check(vk::surface_maps_attached() > 0, "maps attached through engine/gfx/surface_maps.h");
    }
    if (target != nullptr) {
        gpu.release_surface(target);
    }
    if (shader != nullptr) {
        gpu.release_pixel_shader(shader);
    }
    vk::set_rt_pbr_settings({});
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
    text::print("thug2_vulkan_pbr_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
        SDL_CreateWindow("thug2 Vulkan per-pixel lighting check", kWindowSize, kWindowSize, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
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
