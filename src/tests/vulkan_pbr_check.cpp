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
//   - every pixel of every case finite (no NaN or infinity), validation (and synchronization validation) messages 0;
//   - (NOT ORIGINAL - rt_detail / rt_delight, vulkan_detail.h) a detail-material library handed over from memory: the
//     detail at full weight scales the albedo by its ratio (the colour's channels alike), faded out it leaves the picture
//     as without it; the de-lighting flattens a texture dark on one half and bright on the other toward its mean, half
//     strength halfway.
//   - (added by surface relief - rt_relief) parallax occlusion mapping: a brick-patterned height map (its normals made
//     from it with a known bump) on a stone texture whose texels each have their own colour - the depth read back from
//     the map, and at five view angles every pixel's texel against the processor marching the same ray through the same
//     heights; off, a smooth-concrete kind and a map with no bump leave the picture exactly as without it, faded out (by
//     its level of detail) too; far away (the texture repeated) the default fade all but flattens it; the sun's shadow
//     darkens, never brightens.
//   - (added for the characters - rt_characters) a character model's map (surface_map_format kFlagModel) classed skin,
//     cloth, a garment with skin texels, hair (strands along u and along v): the same quad lit from the front and from
//     behind, each pixel against the processor's skin (wrapped light, red terminator, translucency - weighted per
//     texel by its colour), sheen and Kajiya-Kay highlight; rt_characters off: the picture as with no map; on with the
//     strengths at 0: as a plain map's.
//   thug2_vulkan_pbr_check [--device N] [--display N] [--no-validation] [--perf]
// --perf also times a full-screen layer at 1920 x 1080 with the setting off and on (reported, not checked).
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
#include <chrono>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/surface_maps.h"
#include "formats/detail_material_format.h"
#include "formats/surface_map_format.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_detail.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_surface.h"
#include "tests/check_window.h"
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
using dmf_kind = detail_material_format::Kind;

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
constexpr float kByteMax = 255.0F;
constexpr float kSignedHalf = 127.5F;    // (a map's signed byte: 0 - 255 for -1 - 1)
constexpr uint8_t kHalfAlpha = 128;      // (the game's doubled alpha: 1)
constexpr float kTwo = 2.0F;
constexpr float kLeastDet = 1.0e-8F;
constexpr float kLeastRough = 0.08F;     // (the shader's clamp)
constexpr float kLeastAlpha = 0.0064F;
constexpr float kLeastNv = 1.0e-4F;
constexpr float kFresnelPower = 5.0F;
constexpr float kMostSpecular = 16.0F;
constexpr float kLeastShare = 0.05F;
constexpr std::size_t kLights = 8;
constexpr std::size_t kLeastJudged = 10000;
constexpr std::size_t kLeastChanged = 1000;
constexpr std::size_t kLeastCharacterMoved = 1000; // (added for the characters)
constexpr uint32_t kRedShift = 16;
constexpr uint32_t kGreenShift = 8;
constexpr uint32_t kRgbMask = 0x00FFFFFFU;
constexpr std::size_t kCornerCount = 6;
constexpr std::size_t kMatrixFloats = 16;
constexpr float kMid = 0.5F;
constexpr float kRedBase = 0.35F;
constexpr float kRedStep = 0.07F;
constexpr float kGreen = 0.55F;
constexpr float kBlueBase = 0.3F;
constexpr float kBlueStep = 0.06F;
constexpr float kTiltRatio = 0.8F;
constexpr std::array<float, 2> kConstantUv{0.3F, 0.6F};
constexpr Vec3 kLuma{0.2126F, 0.7152F, 0.0722F};
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
Vec3 cross(const Vec3& one, const Vec3& other)
{
    return {(core::at(one, 1) * core::at(other, 2)) - (core::at(one, 2) * core::at(other, 1)),
            (core::at(one, 2) * core::at(other, 0)) - (core::at(one, 0) * core::at(other, 2)),
            (core::at(one, 0) * core::at(other, 1)) - (core::at(one, 1) * core::at(other, 0))};
}
Vec3 normalise(const Vec3& vec)
{
    const float length = std::sqrt(dot(vec, vec));
    return length > 0.0F ? scale(vec, 1.0F / length) : vec;
}
float clamp01(float value) { return std::clamp(value, 0.0F, 1.0F); }
// A colour as an 8-bit channel holds it (the vertex colours, the ambient, the texture).
float as_byte(float value) { return std::round(clamp01(value) * kByteMax) / kByteMax; }
Vec3 as_bytes(const Vec3& vec) { return {as_byte(core::at(vec, 0)), as_byte(core::at(vec, 1)), as_byte(core::at(vec, 2))}; }
float smoothstep(float low, float high, float value)
{
    constexpr float kThree = 3.0F;
    const float along = clamp01((value - low) / (high - low));
    return along * along * (kThree - (kTwo * along));
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
    const auto turn = [yaw, pitch](const Vec3& vec) {
        const Vec3 yawed{(core::at(vec, 0) * std::cos(yaw)) + (core::at(vec, 2) * std::sin(yaw)), core::at(vec, 1),
                         (-core::at(vec, 0) * std::sin(yaw)) + (core::at(vec, 2) * std::cos(yaw))};
        return Vec3{core::at(yawed, 0), (core::at(yawed, 1) * std::cos(pitch)) - (core::at(yawed, 2) * std::sin(pitch)),
                    (core::at(yawed, 1) * std::sin(pitch)) + (core::at(yawed, 2) * std::cos(pitch))};
    };
    Quad quad;
    const Vec3 centre{0.0F, 0.0F, kDepth};
    quad.edge_s = turn({kTwo * kHalf, 0.0F, 0.0F});
    quad.edge_t = turn({0.0F, -kTwo * kHalf, 0.0F}); // (t down the screen, as v down a texture)
    quad.corner = sub(centre, scale(add(quad.edge_s, quad.edge_t), kMid));
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
        quad.uv0 = kConstantUv, quad.duv_s = {0.0F, 0.0F}, quad.duv_t = {0.0F, 0.0F};
        break;
    }
    return quad;
}

Vec3 quad_normal(const Quad& quad)
{
    Vec3 normal = normalise(cross(quad.edge_s, quad.edge_t));
    if (dot(normal, quad.corner) > 0.0F) { // toward the eye
        normal = scale(normal, -1.0F);
    }
    return normal;
}

// The map's and the texture's texels: normals tilted by texel, roughness stepped, the colour varied.
struct Texel {
    std::array<uint8_t, 4> map{};
    Vec3 colour{};
};

Texel texel_at(uint32_t column, uint32_t row)
{
    constexpr float kTiltStep = 0.11F;
    constexpr float kMiddle = 3.5F;
    constexpr float kRoughBase = 0.15F;
    constexpr float kRoughStep = 0.1F;
    const float nx = (static_cast<float>(column) - kMiddle) * kTiltStep;
    const float ny = (static_cast<float>(row) - kMiddle) * kTiltStep * kTiltRatio;
    const auto byte = [](float value) {
        return static_cast<uint8_t>(std::lround(std::clamp((value * kSignedHalf) + kSignedHalf, 0.0F, kByteMax)));
    };
    const float rough = kRoughBase + (kRoughStep * static_cast<float>((column + (2 * row)) % kTexels));
    return {.map = {byte(nx), byte(ny), static_cast<uint8_t>(std::lround(rough * kByteMax)), kHalfAlpha},
            .colour = as_bytes({kRedBase + (kRedStep * static_cast<float>(column)), kGreen,
                                kBlueBase + (kBlueStep * static_cast<float>(row))})};
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
    bool wrap = false;    // (added by surface relief) the texture repeats (else clamped)
    // (added for the characters) the light; the map's class and flags (surface_map_format); rt_characters and its
    // strengths
    Light light{};
    uint8_t material_class = 2;
    uint8_t flags = 0;
    bool characters = false;
    float skin = 1.0F;
    float cloth = 1.0F;
    float hair = 1.0F;
};

constexpr Vec3 kAmbient{0.15F, 0.15F, 0.18F};
constexpr Vec3 kMaterialAmbient{0.8F, 0.8F, 0.8F};
// (unit vectors: normalised by hand)
constexpr Light kLight{.toward = {-0.5014059F, 0.6016871F, -0.6217433F}, .colour = {0.9F, 0.85F, 0.8F}};
constexpr Vec3 kSunToward{0.4056161F, 0.7605302F, -0.5070201F};
constexpr float kMapF0 = 0.04F;

// (added for the characters) The case's light: its own, else kLight.
const Light& light_of(const Case& test)
{
    return dot(test.light.toward, test.light.toward) > 0.0F ? test.light : kLight;
}

// (added for the characters - ff_shaders.cpp kDetailCharacterBlock's formulas) What a character map's class makes of a
// light: skin's per-channel wrap and translucency, cloth's sheen, hair's highlight.
struct Character {
    bool on = false;
    float skin = 0.0F;  // k
    float sheen = 0.0F;
    bool hair = false;
    float hair_strength = 0.0F;
    bool along_u = false;
};

// The shader's character_skin_weight.
float skin_weight(const Vec3& colour)
{
    const float r = core::at(colour, 0);
    const float g = core::at(colour, 1);
    const float b = core::at(colour, 2);
    const float order = smoothstep(-0.03F, 0.0F, r - g) * smoothstep(-0.03F, 0.0F, g - b);
    const float saturation = (r - b) / std::max(r, 1.0e-4F);
    const float hue = (g - b) / std::max(r - b, 1.0e-4F);
    return order * smoothstep(0.06F, 0.16F, saturation) * (1.0F - smoothstep(0.7F, 0.8F, saturation)) *
           smoothstep(0.03F, 0.15F, hue) * (1.0F - smoothstep(0.85F, 0.97F, hue)) * smoothstep(0.1F, 0.2F, r);
}

Character character_of(const Case& test, const Vec3& colour)
{
    namespace smf = surface_map_format;
    Character out;
    if (!test.characters || !test.map || !test.pbr || !test.lit || (test.flags & smf::kFlagModel) == 0U) {
        return out;
    }
    constexpr float kSkinFollows = 0.6F;
    constexpr float kDenim = 0.6F;
    const float weight = skin_weight(colour);
    switch (static_cast<smf::MaterialClass>(test.material_class)) {
    case smf::MaterialClass::kSkin:
        out.on = test.skin > 0.0F;
        out.skin = test.skin * std::lerp(1.0F, weight, kSkinFollows);
        break;
    case smf::MaterialClass::kCloth:
    case smf::MaterialClass::kDenim: {
        const bool texels = (test.flags & smf::kFlagSkinTexels) != 0U && test.skin > 0.0F;
        const float sheen = test.cloth * (test.material_class == static_cast<uint8_t>(smf::MaterialClass::kDenim) ? kDenim : 1.0F);
        out.on = sheen > 0.0F || texels;
        out.skin = texels ? test.skin * weight : 0.0F;
        out.sheen = sheen * (texels ? 1.0F - weight : 1.0F);
        break;
    }
    case smf::MaterialClass::kHair:
        out.on = test.hair > 0.0F;
        out.hair = out.on;
        out.hair_strength = test.hair;
        out.along_u = (test.flags & smf::kFlagStrandsU) != 0U;
        break;
    default:
        break;
    }
    return out;
}

struct Surface {
    float rough = 0.0F;
    float f0 = 0.0F;
};

// The shader's surface_ggx: pi x the GGX BRDF x N.L.
float ggx(const Vec3& normal, const Vec3& view, const Vec3& light, const Surface& surface)
{
    constexpr float kFour = 4.0F;
    const float nl = std::max(dot(normal, light), 0.0F);
    if (nl <= 0.0F) {
        return 0.0F;
    }
    const Vec3 half = normalise(add(view, light));
    const float nv = std::max(dot(normal, view), kLeastNv);
    const float nh = std::max(dot(normal, half), 0.0F);
    const float vh = std::max(dot(view, half), 0.0F);
    const float alpha = std::max(surface.rough * surface.rough, kLeastAlpha);
    const float alpha2 = alpha * alpha;
    const float lobe = (nh * nh * (alpha2 - 1.0F)) + 1.0F;
    const float distribution = alpha2 / (kPi * lobe * lobe);
    const float smith = kMid * alpha;
    const float geometry = (nl / ((nl * (1.0F - smith)) + smith)) * (nv / ((nv * (1.0F - smith)) + smith));
    const float fresnel = surface.f0 + ((1.0F - surface.f0) * std::pow(1.0F - vh, kFresnelPower));
    return std::min(kPi * distribution * geometry * fresnel / (kFour * nv), kMostSpecular);
}

// The colour the processor expects at a pixel, or false where the pixel isn't judged (off the quad, near an edge).
bool expected(const Case& given, const Quad& quad, uint32_t px, uint32_t py, Vec3* out)
{
    Case test = given;
    test.bake = as_bytes(given.bake);
    const Vec3 ambient = as_bytes(kAmbient);
    const float ndc_x = ((kTwo * static_cast<float>(px)) / static_cast<float>(kSize)) - 1.0F;
    const float ndc_y = 1.0F - ((kTwo * static_cast<float>(py)) / static_cast<float>(kSize));
    const Vec3 ray{ndc_x / kFov, ndc_y / kFov, 1.0F};
    // lambda ray = corner + s edge_s + t edge_t: Cramer's rule
    const Vec3 neg_s = scale(quad.edge_s, -1.0F);
    const Vec3 neg_t = scale(quad.edge_t, -1.0F);
    const float det = dot(ray, cross(neg_s, neg_t));
    if (std::fabs(det) < kLeastDet) {
        return false;
    }
    const float along_s = dot(ray, cross(quad.corner, neg_t)) / det;
    const float along_t = dot(ray, cross(neg_s, quad.corner)) / det;
    const float lambda = dot(quad.corner, cross(neg_s, neg_t)) / det;
    if (along_s < kEdgeMargin || along_s > 1.0F - kEdgeMargin || along_t < kEdgeMargin || along_t > 1.0F - kEdgeMargin ||
        lambda <= 0.0F) {
        return false;
    }
    const Vec3 point = scale(ray, lambda);
    const float u_at = core::at(quad.uv0, 0) + (along_s * core::at(quad.duv_s, 0)) + (along_t * core::at(quad.duv_t, 0));
    const float v_at = core::at(quad.uv0, 1) + (along_s * core::at(quad.duv_s, 1)) + (along_t * core::at(quad.duv_t, 1));
    const float tu = u_at * static_cast<float>(kTexels);
    const float tv = v_at * static_cast<float>(kTexels);
    const float fu = tu - std::floor(tu);
    const float fv = tv - std::floor(tv);
    if (fu < kTexelMargin || fu > 1.0F - kTexelMargin || fv < kTexelMargin || fv > 1.0F - kTexelMargin) {
        return false;
    }
    const auto column = static_cast<uint32_t>(std::clamp(tu, 0.0F, static_cast<float>(kTexels - 1)));
    const auto row = static_cast<uint32_t>(std::clamp(tv, 0.0F, static_cast<float>(kTexels - 1)));
    const Texel texel = texel_at(column, row);
    const Vec3 geo = quad_normal(quad);
    Vec3 bent = geo;
    Surface surface{.rough = vk::RtPbrSettings{}.roughness, .f0 = vk::RtPbrSettings{}.f0};
    // (added for the characters) a character model's map is used only with rt_characters on
    const bool map_used = test.map && ((test.flags & surface_map_format::kFlagModel) == 0U || test.characters);
    if (map_used && test.pbr) {
        surface = {.rough = static_cast<float>(core::at(texel.map, 2)) / kByteMax, .f0 = kMapF0};
        const float tilt_x = (static_cast<float>(core::at(texel.map, 0)) - kSignedHalf) / kSignedHalf;
        const float tilt_y = (static_cast<float>(core::at(texel.map, 1)) - kSignedHalf) / kSignedHalf;
        const Vec3 nt{tilt_x, tilt_y, std::sqrt(std::max(1.0F - (tilt_x * tilt_x) - (tilt_y * tilt_y), 0.0F))};
        // the gradients of u and v along the quad (its dual basis), scaled by the longer
        const float area = dot(geo, cross(quad.edge_s, quad.edge_t));
        const Vec3 grad_s = scale(cross(quad.edge_t, geo), 1.0F / area);
        const Vec3 grad_t = scale(cross(geo, quad.edge_s), 1.0F / area);
        const Vec3 tangent = add(scale(grad_s, core::at(quad.duv_s, 0)), scale(grad_t, core::at(quad.duv_t, 0)));
        const Vec3 bitangent = add(scale(grad_s, core::at(quad.duv_s, 1)), scale(grad_t, core::at(quad.duv_t, 1)));
        const float longest = std::max(dot(tangent, tangent), dot(bitangent, bitangent));
        if (longest > 0.0F) {
            const float inv = 1.0F / std::sqrt(longest);
            bent = normalise(add(add(scale(tangent, inv * core::at(nt, 0)), scale(bitangent, inv * core::at(nt, 1))),
                                 scale(geo, core::at(nt, 2))));
        }
    }
    surface.rough = std::clamp(surface.rough, kLeastRough, 1.0F);
    const Vec3 view = normalise(scale(point, -1.0F));
    const vk::RtPbrSettings settings;
    Vec3 v0{};
    Vec3 specular{};
    const Light& light = light_of(test);
    const Character character = character_of(test, texel.colour); // (added for the characters)
    if (test.lit && character.on) {
        constexpr Vec3 kWrap{0.45F, 0.2F, 0.12F};
        constexpr Vec3 kThrough{1.0F, 0.35F, 0.2F};
        constexpr float kThroughShare = 0.25F;
        constexpr float kSheen = 0.35F;
        constexpr float kShift = 0.6F;
        constexpr float kKk = 0.4F;
        const float nl = dot(bent, light.toward);
        const float nv = std::max(dot(bent, view), 0.0F);
        const float thin = 1.0F - std::fabs(dot(geo, view));
        const float rim = kSheen * character.sheen * (1.0F - nv) * (1.0F - nv) * (1.0F - nv);
        const float through = kThroughShare * character.skin * thin * std::pow(std::max(-dot(view, light.toward), 0.0F), 4.0F);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const float wrap = kWrap.at(channel) * character.skin;
            const float diffuse = clamp01((nl + wrap) / (1.0F + wrap)) + (kThrough.at(channel) * through) + (rim * (kMid + (kMid * nl)));
            v0.at(channel) = clamp01((kMaterialAmbient.at(channel) * ambient.at(channel)) +
                                     (test.bake.at(channel) * light.colour.at(channel) * diffuse));
        }
        float highlight = ggx(bent, view, light.toward, surface);
        if (character.hair) {
            const float area = dot(geo, cross(quad.edge_s, quad.edge_t));
            const Vec3 grad_s = scale(cross(quad.edge_t, geo), 1.0F / area);
            const Vec3 grad_t = scale(cross(geo, quad.edge_s), 1.0F / area);
            const Vec3 along_u = add(scale(grad_s, core::at(quad.duv_s, 0)), scale(grad_t, core::at(quad.duv_t, 0)));
            const Vec3 along_v = add(scale(grad_s, core::at(quad.duv_s, 1)), scale(grad_t, core::at(quad.duv_t, 1)));
            const float height = static_cast<float>(core::at(texel.map, 3)) / kByteMax;
            const Vec3 strand = normalise(add(normalise(character.along_u ? along_u : along_v), scale(bent, (height - kMid) * kShift)));
            const float exponent = std::clamp(kTwo / std::max(std::pow(surface.rough, 4.0F), 1.0e-4F), 4.0F, 512.0F);
            const float th = dot(strand, normalise(add(light.toward, view)));
            const float kk = std::pow(std::sqrt(std::max(1.0F - (th * th), 0.0F)), exponent) * kKk * smoothstep(0.0F, 0.2F, nl);
            highlight = (highlight * std::max(1.0F - character.hair_strength, 0.0F)) + (kk * character.hair_strength);
        }
        specular = scale(light.colour, highlight);
    } else if (test.lit) {
        const float nl = std::max(dot(test.pbr ? bent : geo, light.toward), 0.0F);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            v0.at(channel) = clamp01((kMaterialAmbient.at(channel) * ambient.at(channel)) +
                                     (test.bake.at(channel) * light.colour.at(channel) * nl));
        }
        if (test.pbr) {
            specular = scale(light.colour, ggx(bent, view, light.toward, surface));
        }
    } else if (test.pbr) {
        const float level = kTwo * dot(test.bake, kLuma);
        const float sunlit = smoothstep(settings.shade_low, settings.shade_high, level);
        const float share = std::max(settings.ambient_share, kLeastShare);
        const float sun_detail =
            (share + std::max(dot(bent, kSunToward), 0.0F)) / (share + std::max(dot(geo, kSunToward), 0.0F));
        const Vec3 up{0.0F, 1.0F, 0.0F};
        constexpr float kSkyOffset = 1.5F;
        const float sky_detail = (kSkyOffset + dot(bent, up)) / (kSkyOffset + dot(geo, up));
        const float mixed = std::lerp(1.0F, sky_detail, settings.sky_detail);
        const float detail = std::clamp(std::lerp(mixed, sun_detail, sunlit), settings.detail_low, settings.detail_high);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            v0.at(channel) = clamp01(test.bake.at(channel) * detail);
        }
        specular = scale(Vec3{1.0F, 1.0F, 1.0F}, ggx(bent, view, kSunToward, surface) * sunlit);
    } else {
        v0 = test.bake;
    }
    for (std::size_t channel = 0; channel < 3; ++channel) {
        // r0 = t0 x c0 (0.5) x v0 x 4, + the specular x its strength (1), clamped
        out->at(channel) =
            clamp01((kTwo * texel.colour.at(channel) * v0.at(channel)) + (specular.at(channel) * settings.specular));
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
    const auto byte = [](float value) { return static_cast<uint32_t>(std::lround(clamp01(value) * kByteMax)); };
    constexpr uint32_t kOpaque = 0x80000000U; // (alpha 128: 1 after the shader's doubling)
    return kOpaque | (byte(core::at(colour, 0)) << kRedShift) | (byte(core::at(colour, 1)) << kGreenShift) |
           byte(core::at(colour, 2));
}

// The texture (8 x 8 A8R8G8B8) and its surface map.
GpuTexture* make_texture(render::Gpu& gpu, bool map, uint8_t material_class = 2, uint8_t flags = 0)
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
                                   (static_cast<std::size_t>(locked.pitch) * (kTexels - 1)) + (std::size_t{kTexels} * 4));
    std::vector<uint8_t> maps(static_cast<std::size_t>(kTexels) * kTexels * 4);
    for (uint32_t row = 0; row < kTexels; ++row) {
        for (uint32_t column = 0; column < kTexels; ++column) {
            const Texel texel = texel_at(column, row);
            const std::size_t at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(locked.pitch)) +
                                   (std::size_t{column} * 4);
            core::at(bytes, at) = static_cast<uint8_t>(std::lround(core::at(texel.colour, 2) * kByteMax));
            core::at(bytes, at + 1) = static_cast<uint8_t>(std::lround(core::at(texel.colour, 1) * kByteMax));
            core::at(bytes, at + 2) = static_cast<uint8_t>(std::lround(core::at(texel.colour, 0) * kByteMax));
            core::at(bytes, at + 3) = kHalfAlpha;
            for (std::size_t channel = 0; channel < 4; ++channel) {
                core::at(maps, (((static_cast<std::size_t>(row) * kTexels) + column) * 4) + channel) =
                    core::at(texel.map, channel);
            }
        }
    }
    gpu.unlock_texture(texture, 0);
    if (map && render::surface::surface_maps() != nullptr) {
        render::surface::surface_maps()->attach(
            texture, {.width = kTexels, .height = kTexels, .material_class = material_class, .f0 = kMapF0, .flags = flags}, maps);
    }
    return texture;
}

std::array<float, kMatrixFloats> projection()
{
    const float depth_scale = kFar / (kFar - kNear);
    return {kFov, 0, 0, 0, 0, kFov, 0, 0, 0, 0, depth_scale, 1, 0, 0, -kNear * depth_scale, 0};
}

void draw_case(render::Gpu& gpu, const Case& test, const Quad& quad, GpuTexture* texture, render::GpuPixelShader* shader)
{
    constexpr std::array<float, kMatrixFloats> kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
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
    gpu.set_render_state(GpuRenderState::kAmbient, argb(kAmbient) & kRgbMask);
    gpu.set_render_state(GpuRenderState::kNormalizeNormals, 1);
    gpu.set_transform(render::GpuTransform::kWorld, kIdentity.data());
    gpu.set_transform(render::GpuTransform::kView, kIdentity.data());
    const std::array<float, kMatrixFloats> proj = projection();
    gpu.set_transform(render::GpuTransform::kProjection, proj.data());
    gpu.set_material({.diffuse = {1, 1, 1, 1},
                      .ambient = {core::at(kMaterialAmbient, 0), core::at(kMaterialAmbient, 1), core::at(kMaterialAmbient, 2), 1},
                      .specular = {},
                      .emissive = {},
                      .power = 0});
    render::GpuLight light{};
    const Light& given = light_of(test); // (added for the characters: a case's own light)
    light.type = value::kLightDirectional;
    light.diffuse = {core::at(given.colour, 0), core::at(given.colour, 1), core::at(given.colour, 2), 1};
    light.direction = scale(given.toward, -1.0F);
    gpu.set_light(0, light);
    gpu.enable_light(0, true);
    for (uint32_t index = 1; index < kLights; ++index) {
        gpu.enable_light(index, false);
    }
    gpu.set_texture(0, texture);
    for (const GpuSamplerState state : {GpuSamplerState::kMinFilter, GpuSamplerState::kMagFilter, GpuSamplerState::kMipFilter}) {
        gpu.set_sampler_state(0, state, value::kFilterPoint);
    }
    gpu.set_sampler_state(0, GpuSamplerState::kAddressU, test.wrap ? value::kAddressWrap : value::kAddressClamp);
    gpu.set_sampler_state(0, GpuSamplerState::kAddressV, test.wrap ? value::kAddressWrap : value::kAddressClamp);
    gpu.set_stage_state(0, GpuStageState::kTexCoordIndex, 0);
    gpu.set_pixel_shader(shader);
    const std::array<float, 4> c0{kMid, kMid, kMid, kMid};
    gpu.set_pixel_constants(0, c0.data(), 1);
    const Vec3 normal = quad_normal(quad);
    const uint32_t colour = argb(test.bake);
    std::array<Vertex, kCornerCount> vertices{};
    std::array<VertexNoNormal, kCornerCount> plain{};
    constexpr std::array<std::array<float, 2>, kCornerCount> kCorners{{{0, 0}, {1, 0}, {0, 1}, {1, 0}, {1, 1}, {0, 1}}};
    for (std::size_t i = 0; i < kCorners.size(); ++i) {
        const float along_s = core::at(kCorners.at(i), 0);
        const float along_t = core::at(kCorners.at(i), 1);
        const Vec3 point = add(quad.corner, add(scale(quad.edge_s, along_s), scale(quad.edge_t, along_t)));
        const std::array<float, 2> uv{
            core::at(quad.uv0, 0) + (along_s * core::at(quad.duv_s, 0)) + (along_t * core::at(quad.duv_t, 0)),
            core::at(quad.uv0, 1) + (along_s * core::at(quad.duv_s, 1)) + (along_t * core::at(quad.duv_t, 1))};
        vertices.at(i) = {.position = point, .normal = normal, .colour = colour, .uv = uv};
        plain.at(i) = {.position = point, .colour = colour, .uv = uv};
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
    settings.characters = test.characters; // (added for the characters)
    settings.skin = test.skin;
    settings.cloth = test.cloth;
    settings.hair = test.hair;
    vk::set_rt_pbr_settings(settings);
    GpuTexture* texture = make_texture(gpu, test.map, test.material_class, test.flags);
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
    // (added for the characters) the pixels their shading moves from a plain map's (the strengths at 0): the check means
    // something
    Case plain = test;
    plain.skin = 0.0F;
    plain.cloth = 0.0F;
    plain.hair = 0.0F;
    const bool shaded = test.characters && (test.skin > 0.0F || test.cloth > 0.0F || test.hair > 0.0F);
    std::size_t moved = 0;
    for (uint32_t py = 0; py < kSize; ++py) {
        for (uint32_t px = 0; px < kSize; ++px) {
            const std::size_t pixel = (static_cast<std::size_t>(py) * kSize) + px;
            Vec3 got{};
            bool all_finite = true;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const std::size_t at = (pixel * kHalfPixelBytes) + (channel * 2);
                got.at(channel) =
                    from_half(static_cast<uint16_t>(core::at(bytes, at) | (core::at(bytes, at + 1) << kHighByte)));
                all_finite = all_finite && std::isfinite(got.at(channel));
            }
            finite += all_finite ? 1U : 0U;
            Vec3 want{};
            if (!expected(test, quad, px, py, &want)) {
                continue;
            }
            ++judged;
            Vec3 unshaded{};
            if (shaded && expected(plain, quad, px, py, &unshaded)) {
                float apart = 0.0F;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    apart = std::max(apart, std::fabs(want.at(channel) - unshaded.at(channel)));
                }
                moved += apart > kTolerance ? 1U : 0U;
            }
            float difference = 0.0F;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                difference = std::max(difference, std::fabs(got.at(channel) - want.at(channel)));
            }
            if (!all_finite || difference > worst) {
                worst = all_finite ? difference : std::numeric_limits<float>::infinity();
                worst_at = text::format(" (at %u, %u: %.4f %.4f %.4f, expected %.4f %.4f %.4f)", px, py,
                                        static_cast<double>(core::at(got, 0)), static_cast<double>(core::at(got, 1)), static_cast<double>(core::at(got, 2)),
                                        static_cast<double>(core::at(want, 0)), static_cast<double>(core::at(want, 1)), static_cast<double>(core::at(want, 2)));
            }
            within += difference <= kTolerance ? 1U : 0U;
        }
    }
    const double share = judged != 0 ? static_cast<double>(within) / static_cast<double>(judged) : 0.0;
    tally->check(judged > kLeastJudged && share >= kShareWithin && worst <= kWorstTolerance,
                 text::format("%s: %zu pixels against the processor, %.2f%% within %.3f, the worst %.4f%s", test.name.c_str(),
                              judged, share * 100.0, static_cast<double>(kTolerance), static_cast<double>(worst),
                              worst > kTolerance ? worst_at.c_str() : ""));
    tally->check(finite == static_cast<std::size_t>(kSize) * kSize, test.name + ": every pixel finite");
    if (shaded) {
        tally->check(moved >= kLeastCharacterMoved,
                     text::format("%s: %zu pixels shaded differently from a plain map", test.name.c_str(), moved));
    }
}

// --- (added by surface relief - [Graphics] rt_relief, gpu_vulkan/vulkan_surface.h) parallax occlusion mapping ----------

constexpr uint32_t kReliefSide = 16;      // the relief texture's and its map's side
constexpr uint32_t kCourse = 4;           // (its bricks: 4 texels high, 4 wide, every other course shifted by 2)
constexpr float kReliefBump = 2.5F;       // k: the map's normals made from its height with it (the generator's 3 x bump)
constexpr float kLowBrick = 0.6F;         // (every third brick's height; the others 1, the mortar 0)
constexpr uint8_t kStoneClass = 3;        // (surface_map_format::MaterialClass::kStone: its kind's relief whole)
constexpr uint8_t kConcreteClass = 2;     // (kConcrete: smooth concrete - no relief)
constexpr float kReliefShare = 0.97F;     // the judged pixels within kTolerance (a read near a texel's edge may go either way)
constexpr std::size_t kLeastMoved = 2000; // the judged pixels whose texel the relief changed (the check means something)
constexpr std::size_t kLeastReliefJudged = 4000; // (pixels whose moved coordinates fall near a texel's edge aren't judged)
constexpr int kMostSteps = 32;            // (the shader's layers: mix(32, 8, rn))
constexpr int kLeastSteps = 8;
constexpr int kHalvings = 5;
constexpr float kGrazeLow = 0.05F;        // (the grazing fade: smoothstep(0.05, 0.2, rn))
constexpr float kGrazeHigh = 0.2F;
constexpr float kSobel = 8.0F;
constexpr float kMapRough = 0.5F;
constexpr float kRepeats = 48.0F;         // (the far case: the texture repeated - its texels ~5 a pixel)

float relief_height_at(uint32_t column, uint32_t row)
{
    const uint32_t course = row / kCourse;
    const uint32_t shifted = column + ((kCourse / 2) * (course % 2));
    if (row % kCourse == 0 || shifted % kCourse == 0) {
        return 0.0F;
    }
    return ((shifted / kCourse) + course) % 3 == 0 ? kLowBrick : 1.0F;
}

Vec3 relief_colour_at(uint32_t column, uint32_t row)
{
    constexpr float kBase = 0.1F;
    constexpr float kStep = 0.03F;
    constexpr float kReliefGreen = 0.3F;
    return as_bytes({kBase + (kStep * static_cast<float>(column)), kReliefGreen, kBase + (kStep * static_cast<float>(row))});
}

// The map: the height's bytes, the normals the generator would make from them with k (tools/pbr_maps classical.ts
// normalsFrom: a wrapping Sobel, (-du k, -dv k, 1) normalised) - flat normals with `flat` (no bump: no relief).
std::vector<uint8_t> relief_map(bool flat)
{
    const auto height = [](uint32_t column, uint32_t row) {
        return std::round(relief_height_at(column % kReliefSide, row % kReliefSide) * kByteMax) / kByteMax;
    };
    const auto signed_byte = [](float value) {
        return static_cast<uint8_t>(std::lround(std::clamp((value * kSignedHalf) + kSignedHalf, 0.0F, kByteMax)));
    };
    const float bump = flat ? 0.0F : kReliefBump;
    std::vector<uint8_t> map(static_cast<std::size_t>(kReliefSide) * kReliefSide * 4);
    for (uint32_t row = 0; row < kReliefSide; ++row) {
        for (uint32_t column = 0; column < kReliefSide; ++column) {
            const uint32_t left = column + kReliefSide - 1;
            const uint32_t right = column + 1;
            const uint32_t up = row + kReliefSide - 1;
            const uint32_t down = row + 1;
            const float du = (height(right, up) + (kTwo * height(right, row)) + height(right, down) - height(left, up) -
                              (kTwo * height(left, row)) - height(left, down)) /
                             kSobel;
            const float dv = (height(left, down) + (kTwo * height(column, down)) + height(right, down) - height(left, up) -
                              (kTwo * height(column, up)) - height(right, up)) /
                             kSobel;
            const float nx = -du * bump;
            const float ny = -dv * bump;
            const float length = std::sqrt((nx * nx) + (ny * ny) + 1.0F);
            const std::size_t at = ((static_cast<std::size_t>(row) * kReliefSide) + column) * 4;
            map.at(at) = signed_byte(nx / length);
            map.at(at + 1) = signed_byte(ny / length);
            map.at(at + 2) = static_cast<uint8_t>(std::lround(kMapRough * kByteMax));
            map.at(at + 3) = static_cast<uint8_t>(std::lround(height(column, row) * kByteMax));
        }
    }
    return map;
}

// The texture (each texel its own colour) with `map` attached as a map of `material_class`.
GpuTexture* relief_texture(render::Gpu& gpu, const std::vector<uint8_t>& map, uint8_t material_class, uint32_t checksum = 0,
                           uint64_t content_hash = 0)
{
    GpuTexture* texture = nullptr;
    if (gpu.create_texture({.width = kReliefSide, .height = kReliefSide, .levels = 1, .usage = 0,
                            .format = value::kFormatA8R8G8B8, .pool = value::kPoolManaged},
                           &texture) != value::kResultOk) {
        return nullptr;
    }
    render::GpuLockedRect locked{};
    if (gpu.lock_texture(texture, 0, &locked, nullptr, 0) == value::kResultOk && locked.bits != nullptr) {
        const std::span<uint8_t> bytes(static_cast<uint8_t*>(locked.bits),
                                       (static_cast<std::size_t>(locked.pitch) * (kReliefSide - 1)) + (std::size_t{kReliefSide} * 4));
        for (uint32_t row = 0; row < kReliefSide; ++row) {
            for (uint32_t column = 0; column < kReliefSide; ++column) {
                const Vec3 colour = relief_colour_at(column, row);
                const std::size_t at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(locked.pitch)) + (std::size_t{column} * 4);
                core::at(bytes, at) = static_cast<uint8_t>(std::lround(core::at(colour, 2) * kByteMax));
                core::at(bytes, at + 1) = static_cast<uint8_t>(std::lround(core::at(colour, 1) * kByteMax));
                core::at(bytes, at + 2) = static_cast<uint8_t>(std::lround(core::at(colour, 0) * kByteMax));
                core::at(bytes, at + 3) = kHalfAlpha;
            }
        }
        gpu.unlock_texture(texture, 0);
    }
    if (render::surface::surface_maps() != nullptr) {
        render::surface::surface_maps()->attach(
            texture,
            {.width = kReliefSide, .height = kReliefSide, .material_class = material_class, .f0 = kMapF0, .checksum = checksum,
             .content_hash = content_hash},
            map);
    }
    return texture;
}

// A texel of the side's grid at (u, v), point sampled - clamped, or repeating with `wrap`.
std::array<uint32_t, 2> relief_texel(float u, float v, bool wrap)
{
    const auto index = [wrap](float value) {
        const float at = std::floor(value * static_cast<float>(kReliefSide));
        if (wrap) {
            const float side = static_cast<float>(kReliefSide);
            return static_cast<uint32_t>(at - (side * std::floor(at / side)));
        }
        return static_cast<uint32_t>(std::clamp(at, 0.0F, static_cast<float>(kReliefSide - 1)));
    };
    return {index(u), index(v)};
}

// The colour the processor expects at a pixel of the relief quad (the bake 0.5 and the normals' strength 0 leave the
// texel's colour: 2 x t x v0), the ray marched through the map's heights as the shader does with the depth `depth` (k x
// the kind's weight x strength); false where it isn't judged. `moved`: the texel isn't the one without relief.
bool relief_expected(const Quad& quad, uint32_t px, uint32_t py, const std::vector<uint8_t>& map, float depth, Vec3* out,
                     bool* moved)
{
    const float ndc_x = ((kTwo * static_cast<float>(px)) / static_cast<float>(kSize)) - 1.0F;
    const float ndc_y = 1.0F - ((kTwo * static_cast<float>(py)) / static_cast<float>(kSize));
    const Vec3 ray{ndc_x / kFov, ndc_y / kFov, 1.0F};
    const Vec3 neg_s = scale(quad.edge_s, -1.0F);
    const Vec3 neg_t = scale(quad.edge_t, -1.0F);
    const float det = dot(ray, cross(neg_s, neg_t));
    if (std::fabs(det) < kLeastDet) {
        return false;
    }
    const float along_s = dot(ray, cross(quad.corner, neg_t)) / det;
    const float along_t = dot(ray, cross(neg_s, quad.corner)) / det;
    const float lambda = dot(quad.corner, cross(neg_s, neg_t)) / det;
    if (along_s < kEdgeMargin || along_s > 1.0F - kEdgeMargin || along_t < kEdgeMargin || along_t > 1.0F - kEdgeMargin ||
        lambda <= 0.0F) {
        return false;
    }
    const Vec3 point = scale(ray, lambda);
    std::array<float, 2> uv{core::at(quad.uv0, 0) + (along_s * core::at(quad.duv_s, 0)) + (along_t * core::at(quad.duv_t, 0)),
                            core::at(quad.uv0, 1) + (along_s * core::at(quad.duv_s, 1)) + (along_t * core::at(quad.duv_t, 1))};
    const std::array<uint32_t, 2> before = relief_texel(core::at(uv, 0), core::at(uv, 1), false);
    const Vec3 geo = quad_normal(quad);
    const float area = dot(geo, cross(quad.edge_s, quad.edge_t));
    const Vec3 grad_s = scale(cross(quad.edge_t, geo), 1.0F / area);
    const Vec3 grad_t = scale(cross(geo, quad.edge_s), 1.0F / area);
    const Vec3 grad_u = add(scale(grad_s, core::at(quad.duv_s, 0)), scale(grad_t, core::at(quad.duv_t, 0)));
    const Vec3 grad_v = add(scale(grad_s, core::at(quad.duv_s, 1)), scale(grad_t, core::at(quad.duv_t, 1)));
    const Vec3 toward = normalise(point);
    const float rn = -dot(toward, geo);
    const float weight = smoothstep(kGrazeLow, kGrazeHigh, rn);
    const auto height = [&map](float u, float v) {
        const std::array<uint32_t, 2> texel = relief_texel(u, v, false);
        return static_cast<float>(map.at((((static_cast<std::size_t>(core::at(texel, 1)) * kReliefSide) + core::at(texel, 0)) * 4) + 3)) /
               kByteMax;
    };
    if (weight > 0.0F) {
        const float side = static_cast<float>(kReliefSide);
        const float texels = std::sqrt(std::sqrt(dot(grad_u, grad_u)) * side * std::sqrt(dot(grad_v, grad_v)) * side);
        const float scaled = depth * weight / texels;
        const std::array<float, 2> shift{dot(grad_u, toward) * (scaled / rn), dot(grad_v, toward) * (scaled / rn)};
        const auto along = [&](float level) {
            return std::array<float, 2>{core::at(uv, 0) + (core::at(shift, 0) * (kMid - level)),
                                        core::at(uv, 1) + (core::at(shift, 1) * (kMid - level))};
        };
        const int steps = static_cast<int>((static_cast<float>(kMostSteps) * (1.0F - rn)) + (static_cast<float>(kLeastSteps) * rn));
        const float layer = 1.0F / static_cast<float>(steps);
        float above = 1.0F;
        float below = 1.0F;
        std::array<float, 2> at = along(1.0F);
        float found = height(core::at(at, 0), core::at(at, 1));
        for (int step = 0; step < steps && found < below; ++step) {
            above = below;
            below -= layer;
            at = along(below);
            found = height(core::at(at, 0), core::at(at, 1));
        }
        for (int halving = 0; halving < kHalvings; ++halving) {
            const float mid = kMid * (above + below);
            at = along(mid);
            if (height(core::at(at, 0), core::at(at, 1)) < mid) {
                above = mid;
            } else {
                below = mid;
            }
        }
        uv = along(below);
    }
    const float tu = core::at(uv, 0) * static_cast<float>(kReliefSide);
    const float tv = core::at(uv, 1) * static_cast<float>(kReliefSide);
    const float fu = tu - std::floor(tu);
    const float fv = tv - std::floor(tv);
    if (fu < kTexelMargin || fu > 1.0F - kTexelMargin || fv < kTexelMargin || fv > 1.0F - kTexelMargin) {
        return false;
    }
    const std::array<uint32_t, 2> texel = relief_texel(core::at(uv, 0), core::at(uv, 1), false);
    *moved = texel != before;
    const Vec3 bake = as_bytes({kMid, kMid, kMid});
    const Vec3 colour = relief_colour_at(core::at(texel, 0), core::at(texel, 1));
    for (std::size_t channel = 0; channel < 3; ++channel) {
        out->at(channel) = clamp01(kTwo * colour.at(channel) * bake.at(channel));
    }
    return true;
}

// The baked quad (0.5 grey) with `texture`, as a float picture (four values a pixel).
std::vector<float> relief_picture(render::Gpu& gpu, GpuSurface* target, render::GpuPixelShader* shader, GpuTexture* texture,
                                  const Quad& quad, bool wrap, bool lit = false)
{
    const Case test{.name = "relief", .lit = lit, .bake = {kMid, kMid, kMid}, .wrap = wrap};
    gpu.begin_scene();
    gpu.set_render_target(0, target);
    gpu.set_depth_surface(nullptr);
    gpu.set_viewport({.x = 0, .y = 0, .width = kSize, .height = kSize, .min_z = 0.0F, .max_z = 1.0F});
    draw_case(gpu, test, quad, texture, shader);
    gpu.set_render_target(0, nullptr);
    gpu.end_scene();
    std::vector<uint8_t> raw;
    (void)vk::read_surface(target, &raw);
    std::vector<float> out(raw.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out.at(i) = from_half(static_cast<uint16_t>(raw.at(i * 2) | (raw.at((i * 2) + 1) << kHighByte)));
    }
    return out;
}

// The settings the relief cases draw with: per-pixel lighting, no highlight, the map's normals flat (the colour is the
// texel's alone), the relief at `strength` and its shadow at `shadow`; the fade as given.
vk::RtPbrSettings relief_settings(bool on, float shadow, float fade_start = vk::kRtReliefFadeStart,
                                  float fade_end = vk::kRtReliefFadeEnd)
{
    vk::RtPbrSettings settings;
    settings.on = true;
    settings.specular = 0.0F;
    settings.normal_strength = 0.0F;
    settings.relief = on;
    settings.relief_strength = 1.0F;
    settings.relief_fade_start = fade_start;
    settings.relief_fade_end = fade_end;
    settings.relief_shadow = shadow;
    return settings;
}

// The mean difference of two pictures' colour channels (NaN if either is short or not finite).
float mean_difference(const std::vector<float>& one, const std::vector<float>& other)
{
    if (one.size() != other.size() || one.empty()) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < one.size(); ++i) {
        if (i % 4 == 3) {
            continue;
        }
        sum += std::fabs(static_cast<double>(one.at(i)) - static_cast<double>(other.at(i)));
        ++count;
    }
    return static_cast<float>(sum / static_cast<double>(count));
}

void check_relief(render::Gpu& gpu, GpuSurface* target, render::GpuPixelShader* shader, Tally* tally)
{
    vk::set_rt_detail_settings({});
    const std::vector<uint8_t> map = relief_map(false);
    const float depth = vk::relief_depth_of_map(map, kReliefSide, kReliefSide);
    tally->check(std::fabs(depth - kReliefBump) < kReliefBump * 0.03F,
                 text::format("rt_relief: the map's depth read back from its normals and height: %.3f texels (made with %.3f)",
                              static_cast<double>(depth), static_cast<double>(kReliefBump)));
    tally->check(vk::relief_depth_of_map(relief_map(true), kReliefSide, kReliefSide) == 0.0F,
                 "rt_relief: a map with flat normals has no depth");
    vk::set_rt_pbr_settings(relief_settings(true, 0.0F));
    GpuTexture* stone = relief_texture(gpu, map, kStoneClass);
    struct View {
        const char* name;
        float yaw;
        float pitch;
        Mapping mapping;
    };
    constexpr float kDegrees = kPi / 180.0F;
    const std::array<View, 5> views{{
        {.name = "facing the camera", .yaw = 0.0F, .pitch = 0.0F, .mapping = Mapping::kPlain},
        {.name = "turned 35 / 20 degrees", .yaw = 35.0F * kDegrees, .pitch = 20.0F * kDegrees, .mapping = Mapping::kPlain},
        {.name = "turned -50 / 10 degrees, u mirrored", .yaw = -50.0F * kDegrees, .pitch = 10.0F * kDegrees, .mapping = Mapping::kMirrored},
        {.name = "turned 20 / -40 degrees, mapping turned 90", .yaw = 20.0F * kDegrees, .pitch = -40.0F * kDegrees, .mapping = Mapping::kRotated},
        {.name = "turned 60 degrees, u and v mirrored", .yaw = 60.0F * kDegrees, .pitch = 0.0F, .mapping = Mapping::kBoth},
    }};
    for (const View& view : views) {
        const Quad quad = make_quad(view.yaw, view.pitch, view.mapping);
        const std::vector<float> picture = relief_picture(gpu, target, shader, stone, quad, false);
        std::size_t judged = 0;
        std::size_t within = 0;
        std::size_t moved = 0;
        bool finite = picture.size() >= static_cast<std::size_t>(kSize) * kSize * 4;
        for (uint32_t py = 0; py < kSize && finite; ++py) {
            for (uint32_t px = 0; px < kSize; ++px) {
                const std::size_t pixel = (static_cast<std::size_t>(py) * kSize) + px;
                Vec3 got{picture.at(pixel * 4), picture.at((pixel * 4) + 1), picture.at((pixel * 4) + 2)};
                finite = finite && std::isfinite(got[0]) && std::isfinite(got[1]) && std::isfinite(got[2]);
                Vec3 want{};
                bool shifted = false;
                if (!relief_expected(quad, px, py, map, depth, &want, &shifted)) {
                    continue;
                }
                ++judged;
                float difference = 0.0F;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    difference = std::max(difference, std::fabs(got.at(channel) - want.at(channel)));
                }
                within += difference <= kTolerance ? 1U : 0U;
                moved += shifted ? 1U : 0U;
            }
        }
        const double share = judged != 0 ? static_cast<double>(within) / static_cast<double>(judged) : 0.0;
        tally->check(finite && judged > kLeastReliefJudged && share >= kReliefShare && moved > kLeastMoved,
                     text::format("rt_relief, %s: %zu pixels' texels against the processor's march, %.2f%% the same "
                                  "(%zu moved by the relief)%s",
                                  view.name, judged, share * 100.0, moved, finite ? "" : ", NOT FINITE"));
    }
    // Off: the picture as without relief - the setting off, a kind without relief, a map without bump, faded out.
    const Quad turned = make_quad(views[1].yaw, views[1].pitch, Mapping::kPlain);
    vk::set_rt_pbr_settings(relief_settings(false, 0.0F));
    const std::vector<float> off = relief_picture(gpu, target, shader, stone, turned, false);
    vk::set_rt_pbr_settings(relief_settings(true, 0.0F));
    const std::vector<float> on = relief_picture(gpu, target, shader, stone, turned, false);
    GpuTexture* concrete = relief_texture(gpu, map, kConcreteClass);
    const std::vector<float> smooth = relief_picture(gpu, target, shader, concrete, turned, false);
    GpuTexture* flat = relief_texture(gpu, relief_map(true), kStoneClass);
    const std::vector<float> no_bump = relief_picture(gpu, target, shader, flat, turned, false);
    vk::set_rt_pbr_settings(relief_settings(false, 0.0F));
    const std::vector<float> concrete_off = relief_picture(gpu, target, shader, concrete, turned, false);
    const std::vector<float> flat_off = relief_picture(gpu, target, shader, flat, turned, false);
    vk::set_rt_pbr_settings(relief_settings(true, 0.0F, -31.0F, -30.0F));
    const std::vector<float> faded = relief_picture(gpu, target, shader, stone, turned, false);
    tally->check(mean_difference(on, off) > 0.001F,
                 text::format("rt_relief: on changes the picture (mean %.4f)", static_cast<double>(mean_difference(on, off))));
    tally->check(smooth == concrete_off, "rt_relief: a smooth-concrete texture's picture the same as with it off");
    tally->check(no_bump == flat_off, "rt_relief: a map with no bump - the picture the same as with it off");
    tally->check(faded == off, "rt_relief: faded out (the fade ending below its texels' size) - the picture the same as off");
    // Far away: the texture repeated so its texels are ~5 a pixel - the default fade has all but flattened the relief.
    Quad distant = turned;
    distant.duv_s = {kRepeats, 0.0F};
    distant.duv_t = {0.0F, kRepeats};
    vk::set_rt_pbr_settings(relief_settings(false, 0.0F));
    const std::vector<float> far_off = relief_picture(gpu, target, shader, stone, distant, true);
    vk::set_rt_pbr_settings(relief_settings(true, 0.0F, 30.0F, 31.0F));
    const std::vector<float> far_whole = relief_picture(gpu, target, shader, stone, distant, true);
    vk::set_rt_pbr_settings(relief_settings(true, 0.0F));
    const std::vector<float> far_faded = relief_picture(gpu, target, shader, stone, distant, true);
    const float whole = mean_difference(far_whole, far_off);
    const float fading = mean_difference(far_faded, far_off);
    // (the map's mips - heights averaged - already flatten it there; the fade takes the rest)
    tally->check(whole > 0.0002F && fading < whole * 0.25F,
                 text::format("rt_relief: far away the fade flattens it (mean change %.4f, %.4f unfaded)",
                              static_cast<double>(fading), static_cast<double>(whole)));
    // The sun's shadow: the sunlit bake darkened beside the bricks, nowhere brightened.
    vk::set_rt_pbr_settings(relief_settings(true, 1.0F));
    const std::vector<float> shadowed = relief_picture(gpu, target, shader, stone, make_quad(0.0F, 0.0F, Mapping::kPlain), false);
    vk::set_rt_pbr_settings(relief_settings(true, 0.0F));
    const std::vector<float> unshadowed = relief_picture(gpu, target, shader, stone, make_quad(0.0F, 0.0F, Mapping::kPlain), false);
    std::size_t darker = 0;
    std::size_t brighter = 0;
    for (std::size_t i = 0; i < shadowed.size() && i < unshadowed.size(); ++i) {
        if (i % 4 == 3) {
            continue;
        }
        darker += shadowed.at(i) < unshadowed.at(i) - 0.01F ? 1U : 0U;
        brighter += shadowed.at(i) > unshadowed.at(i) + 0.001F ? 1U : 0U;
    }
    tally->check(darker > kLeastChanged && brighter == 0,
                 text::format("rt_relief: the sun's shadow darkens %zu channels beside the bricks, brightens %zu", darker, brighter));
    gpu.release_texture(stone);
    gpu.release_texture(concrete);
    gpu.release_texture(flat);
    vk::set_rt_pbr_settings({});
}

// --perf: the cost of a full-screen layer at 1920 x 1080 (a quad filling the target, drawn many times in one scene)
// with rt_pbr off, and on lit and baked with a map - the wall time per layer once the device has finished (not a check).
void measure_cost(render::Gpu& gpu, render::GpuPixelShader* shader)
{
    constexpr uint32_t kWide = 1920;
    constexpr uint32_t kTall = 1080;
    constexpr int kLayers = 200;
    constexpr float kCover = 3.0F; // (the quad grown to fill the screen)
    constexpr double kMilliseconds = 1000.0;
    GpuSurface* target = nullptr;
    if (gpu.create_render_target({.width = kWide, .height = kTall, .format = value::kFormatA8R8G8B8, .samples = 0, .quality = 0,
                                  .lockable_or_discard = true},
                                 &target) != value::kResultOk) {
        return;
    }
    Quad quad = make_quad(0.0F, 0.0F, Mapping::kPlain);
    quad.edge_s = scale(quad.edge_s, kCover);
    quad.edge_t = scale(quad.edge_t, kCover);
    quad.corner = sub(Vec3{0.0F, 0.0F, kDepth}, scale(add(quad.edge_s, quad.edge_t), kMid));
    struct Mode {
        const char* name;
        bool pbr;
        bool lit;
        bool relief = false; // (added by surface relief: a stone map with relief, its shadow on)
        uint8_t character = 0; // (added for the characters: a model's map of this class, rt_characters on)
        uint8_t flags = 0;
    };
    constexpr auto kSkinClass = static_cast<uint8_t>(surface_map_format::MaterialClass::kSkin);
    constexpr auto kClothClass = static_cast<uint8_t>(surface_map_format::MaterialClass::kCloth);
    constexpr auto kHairClass = static_cast<uint8_t>(surface_map_format::MaterialClass::kHair);
    constexpr uint8_t kModel = surface_map_format::kFlagModel;
    for (const Mode mode : {Mode{.name = "rt_pbr off", .pbr = false, .lit = true}, Mode{.name = "lit, a map", .pbr = true, .lit = true},
                            Mode{.name = "baked, a map", .pbr = true, .lit = false},
                            Mode{.name = "baked, a stone map with relief", .pbr = true, .lit = false, .relief = true},
                            Mode{.name = "lit, a character's skin map", .pbr = true, .lit = true, .character = kSkinClass, .flags = kModel},
                            Mode{.name = "lit, a character's garment with skin texels", .pbr = true, .lit = true, .character = kClothClass,
                                 .flags = kModel | surface_map_format::kFlagSkinTexels},
                            Mode{.name = "lit, a character's hair map", .pbr = true, .lit = true, .character = kHairClass, .flags = kModel}}) {
        vk::RtPbrSettings settings{.on = mode.pbr};
        settings.relief = mode.relief;
        settings.characters = mode.character != 0; // (added for the characters)
        vk::set_rt_pbr_settings(settings);
        GpuTexture* texture = mode.relief ? relief_texture(gpu, relief_map(false), kStoneClass)
                              : mode.character != 0 ? make_texture(gpu, true, mode.character, mode.flags)
                                                    : make_texture(gpu, true);
        const Case test{.name = mode.name, .lit = mode.lit};
        double seconds = 0.0;
        for (int pass = 0; pass < 2; ++pass) { // (the first warms the pipelines)
            const auto start = std::chrono::steady_clock::now();
            gpu.begin_scene();
            gpu.set_render_target(0, target);
            gpu.set_depth_surface(nullptr);
            gpu.set_viewport({.x = 0, .y = 0, .width = kWide, .height = kTall, .min_z = 0.0F, .max_z = 1.0F});
            for (int layer = 0; layer < kLayers; ++layer) {
                draw_case(gpu, test, quad, texture, shader);
            }
            gpu.set_render_target(0, nullptr);
            gpu.end_scene();
            std::vector<uint8_t> bytes;
            (void)vk::read_surface(target, &bytes);
            seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }
        text::print("  cost: %s - %.3f ms a full-screen layer (%d layers at %ux%u, with the clear per layer)\n", mode.name,
                    seconds * kMilliseconds / kLayers, kLayers, kWide, kTall);
        gpu.release_texture(texture);
    }
    gpu.release_surface(target);
}

void check_bake_moves(render::Gpu& gpu, GpuSurface* target, render::GpuPixelShader* shader, Tally* tally)
{
    // The same quad with the map and without: the map's detail must change the picture (a check that the map is used).
    const Case with{.name = "map used", .lit = false, .bake = {kMid, kMid, kMid}};
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
    for (std::size_t i = 0; i < core::at(pictures, 0).size() && i < core::at(pictures, 1).size(); i += 2) {
        differ += core::at(pictures, 0).at(i) != core::at(pictures, 1).at(i) ? 1U : 0U;
    }
    tally->check(differ > kLeastChanged, text::format("the map changes the baked picture (%zu channels differ)", differ));
}

// --- (NOT ORIGINAL - rt_detail / rt_delight, gpu_vulkan/vulkan_detail.h) the detail layer and the de-lighting -----------

constexpr uint32_t kDetailChecksum = 0x0DE7A11U;
constexpr uint64_t kDetailHash = 0x5EEDF00DULL;
constexpr uint8_t kDetailAlbedo = 64; // (the detail's albedo ratio x 0.5: 64 / 127.5 - the texel about halved)
constexpr uint32_t kDetailSide = 16;
constexpr uint32_t kDelitSide = 64;
constexpr uint32_t kDelitLevels = 7;
constexpr float kDarkHalf = 0.2F;
constexpr float kBrightHalf = 0.6F;
constexpr float kDelitMean = 0.4F;
constexpr float kRatioTolerance = 0.01F;

// A library of one kind (smooth concrete) whose detail is flat with the albedo ratio kDetailAlbedo, the check's texture
// assigned to it (its mean luminance `mean`).
std::vector<uint8_t> detail_library(float mean, dmf_kind kind = dmf_kind::kConcreteSmooth, uint32_t checksum = kDetailChecksum)
{
    namespace dmf = detail_material_format;
    dmf::Library library;
    library.kinds.push_back({.kind = kind,
                             .side = kDetailSide,
                             .tile_metres = 1.0F,
                             .roughness = 0.8F,
                             .roughness_swing = 0.1F,
                             .f0 = 0.04F,
                             .metalness = 0.0F,
                             .roughness_weight = 0.5F});
    library.assignments.push_back({.checksum = checksum,
                                   .content_hash = kDetailHash,
                                   .kind = kind,
                                   .evidence = dmf::Evidence::kTerrain,
                                   .repeats = 2.0F,
                                   .mean_luma = mean});
    std::vector<uint8_t> texels(static_cast<std::size_t>(kDetailSide) * kDetailSide * 4);
    for (std::size_t at = 0; at < texels.size(); at += 4) {
        texels.at(at) = kHalfAlpha;
        texels.at(at + 1) = kHalfAlpha;
        texels.at(at + 2) = kHalfAlpha;
        texels.at(at + 3) = kDetailAlbedo;
    }
    return dmf::write_file(library, std::vector<std::vector<uint8_t>>{texels});
}

// A grey texture `side` texels square with `levels` levels (each the box mean of the one above), its left half `left`
// and right half `right`, with a flat surface map attached under the check's checksum and hash.
GpuTexture* detail_texture(render::Gpu& gpu, uint32_t side, uint32_t levels, float left, float right)
{
    GpuTexture* texture = nullptr;
    if (gpu.create_texture({.width = side, .height = side, .levels = levels, .usage = 0, .format = value::kFormatA8R8G8B8,
                            .pool = value::kPoolManaged},
                           &texture) != value::kResultOk) {
        return nullptr;
    }
    std::vector<float> grey(static_cast<std::size_t>(side) * side);
    for (uint32_t row = 0; row < side; ++row) {
        for (uint32_t column = 0; column < side; ++column) {
            grey.at((static_cast<std::size_t>(row) * side) + column) = column < side / 2 ? left : right;
        }
    }
    uint32_t size = side;
    for (uint32_t level = 0; level < levels; ++level) {
        render::GpuLockedRect locked{};
        if (gpu.lock_texture(texture, level, &locked, nullptr, 0) == value::kResultOk && locked.bits != nullptr) {
            const std::span<uint8_t> bytes(static_cast<uint8_t*>(locked.bits),
                                           (static_cast<std::size_t>(locked.pitch) * (size - 1)) + (std::size_t{size} * 4));
            for (uint32_t row = 0; row < size; ++row) {
                for (uint32_t column = 0; column < size; ++column) {
                    const auto byte = static_cast<uint8_t>(std::lround(grey.at((static_cast<std::size_t>(row) * size) + column) * kByteMax));
                    const std::size_t at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(locked.pitch)) + (std::size_t{column} * 4);
                    bytes[at] = byte;
                    bytes[at + 1] = byte;
                    bytes[at + 2] = byte;
                    bytes[at + 3] = kHalfAlpha;
                }
            }
            gpu.unlock_texture(texture, level);
        }
        if (size > 1) {
            const uint32_t down = size / 2;
            std::vector<float> next(static_cast<std::size_t>(down) * down);
            for (uint32_t row = 0; row < down; ++row) {
                for (uint32_t column = 0; column < down; ++column) {
                    const auto at = [&](uint32_t y, uint32_t x) { return grey.at((static_cast<std::size_t>(y) * size) + x); };
                    next.at((static_cast<std::size_t>(row) * down) + column) =
                        (at(row * 2, column * 2) + at(row * 2, (column * 2) + 1) + at((row * 2) + 1, column * 2) +
                         at((row * 2) + 1, (column * 2) + 1)) /
                        4.0F;
                }
            }
            grey = std::move(next);
            size = down;
        }
    }
    constexpr uint32_t kMapSide = 4;
    std::vector<uint8_t> map(static_cast<std::size_t>(kMapSide) * kMapSide * 4);
    for (std::size_t at = 0; at < map.size(); at += 4) {
        map.at(at) = kHalfAlpha;
        map.at(at + 1) = kHalfAlpha;
        map.at(at + 2) = static_cast<uint8_t>(kByteMax * 0.8F);
        map.at(at + 3) = kHalfAlpha;
    }
    if (render::surface::surface_maps() != nullptr) {
        render::surface::surface_maps()->attach(texture, {.width = kMapSide, .height = kMapSide, .material_class = 2, .f0 = kMapF0,
                                                          .checksum = kDetailChecksum, .content_hash = kDetailHash},
                                                map);
    }
    return texture;
}

// The baked quad facing the camera with `texture`, as a float picture (four halves a pixel).
std::vector<float> detail_picture(render::Gpu& gpu, GpuSurface* target, render::GpuPixelShader* shader, GpuTexture* texture)
{
    const Case test{.name = "detail", .lit = false, .bake = {kMid, kMid, kMid}};
    gpu.begin_scene();
    gpu.set_render_target(0, target);
    gpu.set_depth_surface(nullptr);
    gpu.set_viewport({.x = 0, .y = 0, .width = kSize, .height = kSize, .min_z = 0.0F, .max_z = 1.0F});
    draw_case(gpu, test, make_quad(0.0F, 0.0F, Mapping::kPlain), texture, shader);
    gpu.set_render_target(0, nullptr);
    gpu.end_scene();
    std::vector<uint8_t> raw;
    (void)vk::read_surface(target, &raw);
    std::vector<float> out(raw.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out.at(i) = from_half(static_cast<uint16_t>(raw.at(i * 2) | (raw.at((i * 2) + 1) << kHighByte)));
    }
    return out;
}

// The pixels where both pictures have colour: the share whose `with` / `without` is `ratio` (within kRatioTolerance), and
// whether every value is finite.
struct RatioCount {
    std::size_t judged = 0;
    std::size_t within = 0;
    bool finite = true;
};
RatioCount ratio_count(const std::vector<float>& with, const std::vector<float>& without, float ratio)
{
    RatioCount count;
    for (std::size_t i = 0; i < with.size() && i < without.size(); ++i) {
        count.finite = count.finite && std::isfinite(with.at(i));
        if (i % 4 == 3 || without.at(i) < 0.05F) {
            continue;
        }
        ++count.judged;
        count.within += std::fabs((with.at(i) / without.at(i)) - ratio) <= kRatioTolerance ? 1U : 0U;
    }
    return count;
}

// The red channel's mean over the middle row's quarter and three-quarter points (a few pixels each).
std::array<float, 2> halves_of(const std::vector<float>& picture)
{
    constexpr uint32_t kQuarter = kSize / 4;
    constexpr uint32_t kSpan = 4;
    std::array<float, 2> means{};
    for (std::size_t side = 0; side < 2; ++side) {
        const uint32_t middle = side == 0 ? (kSize / 2) - (kQuarter / 2) - 4 : (kSize / 2) + (kQuarter / 2) + 4;
        float sum = 0.0F;
        for (uint32_t x = middle - kSpan; x <= middle + kSpan; ++x) {
            sum += picture.at(((static_cast<std::size_t>(kSize / 2) * kSize) + x) * 4);
        }
        means.at(side) = sum / static_cast<float>((2 * kSpan) + 1);
    }
    return means;
}

void check_detail(render::Gpu& gpu, GpuSurface* target, render::GpuPixelShader* shader, Tally* tally)
{
    vk::RtPbrSettings pbr;
    pbr.on = true;
    pbr.specular = 0.0F; // (the colour alone: the detail's albedo is what is measured)
    vk::set_rt_pbr_settings(pbr);
    vk::set_rt_detail_settings({});
    tally->check(vk::load_detail_library(detail_library(kDelitMean)), "a detail-material library read and its texture made");
    tally->check(vk::detail_textures_made() > 0, "the kind's detail texture made");
    GpuTexture* texture = detail_texture(gpu, kTexels, 1, kMid, kMid);
    const std::vector<float> off = detail_picture(gpu, target, shader, texture);
    // (the detail at full weight: everywhere on the quad, its texels being far below the fade's start)
    vk::set_rt_detail_settings({.on = true, .strength = 1.0F, .fade_start = 30.0F, .fade_end = 31.0F});
    const std::vector<float> full = detail_picture(gpu, target, shader, texture);
    const float ratio = static_cast<float>(kDetailAlbedo) / kSignedHalf;
    const RatioCount albedo = ratio_count(full, off, ratio);
    tally->check(albedo.judged > kLeastJudged && albedo.within >= albedo.judged * 99 / 100 && albedo.finite,
                 text::format("rt_detail: the albedo x %.4f where the detail is whole (%zu of %zu channels)", static_cast<double>(ratio),
                              albedo.within, albedo.judged));
    // (faded out: its texels far above the fade's end - the picture as without it)
    vk::set_rt_detail_settings({.on = true, .strength = 1.0F, .fade_start = -31.0F, .fade_end = -30.0F});
    const std::vector<float> faded = detail_picture(gpu, target, shader, texture);
    const RatioCount same = ratio_count(faded, off, 1.0F);
    tally->check(same.judged > kLeastJudged && same.within == same.judged && same.finite,
                 text::format("rt_detail: faded out, the picture as without it (%zu of %zu channels)", same.within, same.judged));
    gpu.release_texture(texture);
    // (the de-lighting: a texture dark on the left, bright on the right, flattened toward its mean)
    GpuTexture* halves = detail_texture(gpu, kDelitSide, kDelitLevels, kDarkHalf, kBrightHalf);
    vk::set_rt_detail_settings({});
    const std::array<float, 2> before = halves_of(detail_picture(gpu, target, shader, halves));
    vk::set_rt_detail_settings({.delight = true, .delight_strength = 1.0F, .delight_limit = 4.0F});
    const std::vector<float> delit = detail_picture(gpu, target, shader, halves);
    const std::array<float, 2> after = halves_of(delit);
    const auto contrast = [](const std::array<float, 2>& means) {
        return std::max(means[0], means[1]) / std::max(std::min(means[0], means[1]), 1.0e-4F);
    };
    tally->check(contrast(before) > 2.5F && contrast(after) < 1.1F,
                 text::format("rt_delight: the halves' contrast %.2f flattened to %.2f", static_cast<double>(contrast(before)),
                              static_cast<double>(contrast(after))));
    vk::set_rt_detail_settings({.delight = true, .delight_strength = 0.5F, .delight_limit = 4.0F});
    const std::array<float, 2> half = halves_of(detail_picture(gpu, target, shader, halves));
    tally->check(contrast(half) > contrast(after) && contrast(half) < contrast(before),
                 text::format("rt_delight: half strength between (%.2f)", static_cast<double>(contrast(half))));
    gpu.release_texture(halves);
    vk::set_rt_detail_settings({});
}

// (added by surface relief) The relief's other shader variants made and drawn: lit by lights (objects - no sun shadow),
// and with the detail layer and the de-lighting (a library whose stone kind the relief texture is assigned to).
void check_relief_variants(render::Gpu& gpu, GpuSurface* target, render::GpuPixelShader* shader, Tally* tally)
{
    constexpr uint32_t kReliefChecksum = 0x5E11EFU;
    constexpr uint64_t kReliefHash = 0xB41C4ULL;
    const auto all_finite = [](const std::vector<float>& picture) {
        return !picture.empty() && std::ranges::all_of(picture, [](float value) { return std::isfinite(value); });
    };
    const Quad quad = make_quad(0.61F, 0.35F, Mapping::kPlain);
    vk::set_rt_pbr_settings(relief_settings(false, 0.0F)); // (first: the maps are taken only with rt_pbr on)
    GpuTexture* stone = relief_texture(gpu, relief_map(false), kStoneClass);
    const std::vector<float> lit_off = relief_picture(gpu, target, shader, stone, quad, false, true);
    vk::set_rt_pbr_settings(relief_settings(true, 1.0F));
    const std::vector<float> lit_on = relief_picture(gpu, target, shader, stone, quad, false, true);
    tally->check(all_finite(lit_on) && mean_difference(lit_on, lit_off) > 0.001F,
                 text::format("rt_relief, lit by lights: drawn, every pixel finite (mean change %.4f)",
                              static_cast<double>(mean_difference(lit_on, lit_off))));
    tally->check(vk::load_detail_library(detail_library(kDelitMean, dmf_kind::kStone, kReliefChecksum)),
                 "rt_relief: a library with the relief texture's stone kind read");
    GpuTexture* assigned = relief_texture(gpu, relief_map(false), kStoneClass, kReliefChecksum, kReliefHash);
    vk::set_rt_detail_settings({.on = true, .strength = 1.0F, .delight = true, .delight_strength = 0.5F});
    vk::set_rt_pbr_settings(relief_settings(false, 0.0F));
    const std::vector<float> detail_off = relief_picture(gpu, target, shader, assigned, quad, false);
    vk::set_rt_pbr_settings(relief_settings(true, 1.0F));
    const std::vector<float> detail_on = relief_picture(gpu, target, shader, assigned, quad, false);
    tally->check(all_finite(detail_on) && mean_difference(detail_on, detail_off) > 0.001F,
                 text::format("rt_relief with the detail layer and the de-lighting: drawn, every pixel finite (mean change %.4f)",
                              static_cast<double>(mean_difference(detail_on, detail_off))));
    vk::set_rt_detail_settings({});
    vk::set_rt_pbr_settings({});
    gpu.release_texture(stone);
    gpu.release_texture(assigned);
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

// (added for the characters - rt_characters) A character model's maps: skin, cloth, a garment with skin texels, hair -
// lit from the front and from behind - against the processor; off and at strength 0 as without them.
void check_characters(render::Gpu& gpu, GpuSurface* target, render::GpuPixelShader* shader, Tally* tally)
{
    namespace smf = surface_map_format;
    constexpr float kYaw = 0.61F;
    constexpr float kPitch = 0.35F;
    constexpr auto kSkin = static_cast<uint8_t>(smf::MaterialClass::kSkin);
    constexpr auto kCloth = static_cast<uint8_t>(smf::MaterialClass::kCloth);
    constexpr auto kDenim = static_cast<uint8_t>(smf::MaterialClass::kDenim);
    constexpr auto kHair = static_cast<uint8_t>(smf::MaterialClass::kHair);
    constexpr uint8_t kModel = smf::kFlagModel;
    // (from behind and above the quad, toward the camera's side a little: the translucency's)
    const Light behind{.toward = normalise({0.35F, 0.25F, 0.9F}), .colour = {0.9F, 0.85F, 0.8F}};
    const Vec3 warm{0.8F, 0.7F, 0.65F};
    const std::array<Case, 11> cases{{
        {.name = "characters: skin, lit", .yaw = kYaw, .pitch = kPitch, .bake = warm, .material_class = kSkin, .flags = kModel, .characters = true},
        {.name = "characters: skin, lit from behind", .yaw = kYaw, .pitch = kPitch, .bake = warm, .light = behind,
         .material_class = kSkin, .flags = kModel, .characters = true},
        {.name = "characters: skin at strength 2, u mirrored", .yaw = -kYaw, .pitch = kPitch, .mapping = Mapping::kMirrored,
         .bake = warm, .material_class = kSkin, .flags = kModel, .characters = true, .skin = 2.0F},
        {.name = "characters: cloth's sheen", .yaw = kYaw, .pitch = kPitch, .bake = warm, .material_class = kCloth,
         .flags = kModel, .characters = true},
        {.name = "characters: denim, its skin texels skin", .yaw = -kYaw, .pitch = -kPitch, .bake = warm, .light = behind,
         .material_class = kDenim, .flags = kModel | smf::kFlagSkinTexels, .characters = true},
        {.name = "characters: hair, strands along v", .yaw = kYaw, .pitch = kPitch, .bake = warm, .material_class = kHair,
         .flags = kModel, .characters = true},
        {.name = "characters: hair, strands along u, mapping turned", .yaw = -kYaw, .pitch = kPitch, .mapping = Mapping::kRotated,
         .bake = warm, .material_class = kHair, .flags = kModel | smf::kFlagStrandsU, .characters = true},
        {.name = "characters: hair at half strength", .yaw = kYaw, .pitch = -kPitch, .bake = warm, .material_class = kHair,
         .flags = kModel, .characters = true, .hair = 0.5F},
        {.name = "characters off: a model's skin map unused", .yaw = kYaw, .pitch = kPitch, .bake = warm,
         .material_class = kSkin, .flags = kModel},
        {.name = "characters off: a model's hair map unused, lit from behind", .yaw = kYaw, .pitch = kPitch, .bake = warm,
         .light = behind, .material_class = kHair, .flags = kModel | smf::kFlagStrandsU},
        {.name = "characters at strength 0: a plain map", .yaw = kYaw, .pitch = kPitch, .bake = warm, .material_class = kSkin,
         .flags = kModel, .characters = true, .skin = 0.0F, .cloth = 0.0F, .hair = 0.0F},
    }};
    for (const Case& test : cases) {
        run_case(gpu, target, test, shader, tally);
    }
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
        check_characters(gpu, target, shader, &tally); // (added for the characters - rt_characters)
        check_bake_moves(gpu, target, shader, &tally);
        check_detail(gpu, target, shader, &tally); // (NOT ORIGINAL - rt_detail / rt_delight)
        check_relief(gpu, target, shader, &tally); // (added by surface relief - rt_relief)
        check_relief_variants(gpu, target, shader, &tally);
        if (options.perf) {
            measure_cost(gpu, shader);
        }
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
