// NOT ORIGINAL - the precipitation occlusion map's geometry and reference (precip_occlusion.h).
#include "engine/gfx/precip_occlusion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

#include "base/core/checked_index.h"

namespace render::precip {

namespace {

constexpr float kHalf = 0.5F;

std::array<float, 3> sub(const std::array<float, 3>& a, const std::array<float, 3>& b)
{
    return {core::at(a, 0) - core::at(b, 0), core::at(a, 1) - core::at(b, 1), core::at(a, 2) - core::at(b, 2)};
}

std::array<float, 3> cross(const std::array<float, 3>& a, const std::array<float, 3>& b)
{
    return {(core::at(a, 1) * core::at(b, 2)) - (core::at(a, 2) * core::at(b, 1)),
            (core::at(a, 2) * core::at(b, 0)) - (core::at(a, 0) * core::at(b, 2)),
            (core::at(a, 0) * core::at(b, 1)) - (core::at(a, 1) * core::at(b, 0))};
}

float dot(const std::array<float, 3>& a, const std::array<float, 3>& b)
{
    return (core::at(a, 0) * core::at(b, 0)) + (core::at(a, 1) * core::at(b, 1)) + (core::at(a, 2) * core::at(b, 2));
}

// Moller-Trumbore, both faces: the distance along `direction` to the triangle, or -1.
float hit(const Triangle& triangle, const std::array<float, 3>& origin, const std::array<float, 3>& direction)
{
    constexpr float kParallel = 1.0e-9F;
    const std::array<float, 3> e1 = sub(core::at(triangle, 1), core::at(triangle, 0));
    const std::array<float, 3> e2 = sub(core::at(triangle, 2), core::at(triangle, 0));
    const std::array<float, 3> p = cross(direction, e2);
    const float det = dot(e1, p);
    if (std::abs(det) < kParallel) {
        return -1.0F;
    }
    const float inv = 1.0F / det;
    const std::array<float, 3> s = sub(origin, core::at(triangle, 0));
    const float u = dot(s, p) * inv;
    if (u < 0.0F || u > 1.0F) {
        return -1.0F;
    }
    const std::array<float, 3> q = cross(s, e1);
    const float v = dot(direction, q) * inv;
    if (v < 0.0F || u + v > 1.0F) {
        return -1.0F;
    }
    return dot(e2, q) * inv;
}

} // namespace

std::array<float, 2> line_of(const std::array<float, 3>& point, const std::array<float, 2>& slant)
{
    const float rise = core::at(point, 1) - kReferenceHeight;
    return {core::at(point, 0) + (core::at(slant, 0) * rise), core::at(point, 2) + (core::at(slant, 1) * rise)};
}

Window window_of(const std::array<float, 3>& camera, const std::array<float, 2>& slant, uint32_t cascade)
{
    const float texel = core::at(kCascadeTexel, std::min(cascade, kCascades - 1));
    const std::array<float, 2> uv = line_of(camera, slant);
    const auto half = static_cast<int32_t>(kMapSize / 2);
    return {.first = {static_cast<int32_t>(std::floor(core::at(uv, 0) / texel)) - half,
                      static_cast<int32_t>(std::floor(core::at(uv, 1) / texel)) - half},
            .texel = texel};
}

uint32_t tag_of(int32_t x, int32_t z, uint32_t generation)
{
    constexpr uint32_t kLow = 0xFFFU;
    constexpr uint32_t kZShift = 12U;
    constexpr uint32_t kGenerationShift = 24U;
    const uint32_t tag = (static_cast<uint32_t>(x) & kLow) | ((static_cast<uint32_t>(z) & kLow) << kZShift) |
                         ((generation & 0x7FU) << kGenerationShift) | 0x80000000U;
    return tag;
}

bool in_slice(int32_t x, int32_t z, uint64_t frame)
{
    constexpr uint32_t kStride = 7U;
    const uint32_t slice = (static_cast<uint32_t>(x) + (static_cast<uint32_t>(z) * kStride)) % kRefreshSlices;
    return slice == static_cast<uint32_t>(frame % kRefreshSlices);
}

LineTrace line_trace(int32_t x, int32_t z, float texel, const std::array<float, 2>& slant,
                     const std::array<float, 3>& direction, float camera_y)
{
    const float u = (static_cast<float>(x) + kHalf) * texel;
    const float v = (static_cast<float>(z) + kHalf) * texel;
    const float top = camera_y + kTraceAbove;
    const float rise = top - kReferenceHeight;
    LineTrace trace;
    trace.origin = {u - (core::at(slant, 0) * rise), top, v - (core::at(slant, 1) * rise)};
    trace.direction = direction;
    const float down = std::max(-core::at(direction, 1), 1.0e-3F);
    trace.reach = (kTraceAbove + kTraceBelow) / down;
    return trace;
}

float reach_slack(float texel, const std::array<float, 2>& slant)
{
    const float sideways =
        std::sqrt((core::at(slant, 0) * core::at(slant, 0)) + (core::at(slant, 1) * core::at(slant, 1)));
    return kReachSlack + (texel * (1.0F + (kHalf * sideways))); // (a ramp's rise across a texel too)
}

float line_height(std::span<const Triangle> triangles, const std::array<float, 3>& point,
                  const std::array<float, 3>& direction, float top, float bottom)
{
    const float down = std::max(-core::at(direction, 1), 1.0e-6F);
    const float back = (top - core::at(point, 1)) / down;
    const std::array<float, 3> origin{core::at(point, 0) - (core::at(direction, 0) * back), top,
                                      core::at(point, 2) - (core::at(direction, 2) * back)};
    const float reach = (top - bottom) / down;
    float nearest = reach;
    bool found = false;
    for (const Triangle& triangle : triangles) {
        const float t = hit(triangle, origin, direction);
        if (t >= 0.0F && t < nearest) {
            nearest = t;
            found = true;
        }
    }
    return found ? top + (core::at(direction, 1) * nearest) : kOpenHeight;
}

bool reached(std::span<const Triangle> triangles, const std::array<float, 3>& point,
             const std::array<float, 3>& direction, float top)
{
    constexpr float kSelf = 0.5F; // (a point on a surface is its line's first hit: just above it)
    const float height = line_height(triangles, point, direction, top, core::at(point, 1) + kSelf);
    return height == kOpenHeight;
}

const char* reached_glsl()
{
    return R"(
// (precip_occlusion.h's twin) the share of the four texels round a point's line of fall that reach it
float precip_texel(ivec2 t, int layer, float y, float slack)
{
    ivec2 slot = ivec2(uvec2(t) % uvec2(PRECIP_SIZE));
    uvec2 v = texelFetch(PRECIP_MAP, ivec2(slot.x + layer * PRECIP_SIZE, slot.y), 0).rg;
    if (v.g == 0u) return 1.0; // (not traced yet: open)
    return y + slack >= uintBitsToFloat(v.r) ? 1.0 : 0.0;
}
float precip_cascade(vec2 uv, int layer, vec4 window, float y)
{
    vec2 f = uv / window.z - 0.5;
    vec2 base = floor(f);
    vec2 t = f - base;
    ivec2 b = ivec2(base);
    ivec2 local = b - ivec2(window.xy);
    if (any(lessThan(local, ivec2(0))) || any(greaterThanEqual(local + 1, ivec2(PRECIP_SIZE)))) return -1.0;
    float a00 = precip_texel(b, layer, y, window.w);
    float a10 = precip_texel(b + ivec2(1, 0), layer, y, window.w);
    float a01 = precip_texel(b + ivec2(0, 1), layer, y, window.w);
    float a11 = precip_texel(b + ivec2(1, 1), layer, y, window.w);
    return mix(mix(a00, a10, t.x), mix(a01, a11, t.x), t.y);
}
// the height at which the line `uv` first meets something (the near window's texel), -1e9: none or not known
float precip_line_height(vec2 uv)
{
    vec4 window = PRECIP_BLOCK.precip_near;
    if (PRECIP_BLOCK.precip_fall.w < 0.5) return -1.0e9;
    ivec2 t = ivec2(floor(uv / window.z));
    ivec2 local = t - ivec2(window.xy);
    if (any(lessThan(local, ivec2(0))) || any(greaterThanEqual(local, ivec2(PRECIP_SIZE)))) return -1.0e9;
    ivec2 slot = ivec2(uvec2(t) % uvec2(PRECIP_SIZE));
    uvec2 v = texelFetch(PRECIP_MAP, slot, 0).rg;
    return v.g == 0u ? -1.0e9 : uintBitsToFloat(v.r);
}
float precip_reached(vec3 world)
{
    if (PRECIP_BLOCK.precip_fall.w < 0.5) return 1.0;
    vec2 uv = world.xz + PRECIP_BLOCK.precip_slant.xy * (world.y - PRECIP_BLOCK.precip_slant.z);
    float near = precip_cascade(uv, 0, PRECIP_BLOCK.precip_near, world.y);
    if (near >= 0.0) return near;
    float far = precip_cascade(uv, 1, PRECIP_BLOCK.precip_far, world.y);
    return far >= 0.0 ? far : 1.0;
}
)";
}

} // namespace render::precip
