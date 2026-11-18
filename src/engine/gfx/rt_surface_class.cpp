// NOT ORIGINAL - (round PT-B) the surfaces' classes for rays (rt_surface_class.h).
#include "engine/gfx/rt_surface_class.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace render::rt {

namespace {

constexpr uint32_t kBlendModeBits = 0xFFFFFFU; // (the pass's blend mode: hit_blend reads the same bits)
// the game's blend modes (render_pc's material passes; vulkan_rt_scene.cpp hit_blend's)
constexpr uint32_t kBlendAlpha = 5;         // lerp by the drawn alpha
constexpr uint32_t kBlendFixedAlpha = 6;    // lerp by the pass's fixed alpha
constexpr uint32_t kBlendModulate = 7;      // multiply by the alpha
constexpr uint32_t kBlendFixedModulate = 8; // multiply by the fixed alpha
constexpr uint32_t kBlendColourModulate = 15;
constexpr uint32_t kBlendAddFirst = 1; // add / add by the fixed alpha / subtract / subtract by the fixed alpha
constexpr uint32_t kBlendAddLast = 4;
constexpr uint32_t kBlendBrighten = 9;
constexpr uint32_t kBlendFixedBrighten = 10;

uint32_t blend_of(const Material& material) { return material.blend & kBlendModeBits; }

} // namespace

bool vertex_alpha_counts(const Material& material)
{
    return (material.flags & kTransparent) != 0U && blend_of(material) == kBlendAlpha &&
           (material.flags & kTextureAlpha) == 0U;
}

float least_drawn_alpha(const Material& material, float least_vertex_alpha)
{
    if (blend_of(material) != kBlendAlpha) {
        return 1.0F;
    }
    if ((material.flags & kTextureAlpha) != 0U) {
        return material.least_texel_alpha;
    }
    // (the game's pixel shader: the vertex alpha x the texel's, doubled, held to 1 - 0x80 x 255 is 1)
    return std::min(1.0F, 2.0F * least_vertex_alpha * material.least_texel_alpha);
}

SurfaceClass surface_class(const Material& material, float least_vertex_alpha)
{
    const bool tested = (material.flags & kAlphaTested) != 0U;
    if ((material.flags & kTransparent) == 0U) {
        return tested ? SurfaceClass::kCutOut : SurfaceClass::kOpaque;
    }
    const uint32_t mode = blend_of(material);
    if ((mode >= kBlendAddFirst && mode <= kBlendAddLast) || mode == kBlendBrighten || mode == kBlendFixedBrighten) {
        return SurfaceClass::kAddsLight;
    }
    if (mode == kBlendModulate || mode == kBlendFixedModulate || mode == kBlendColourModulate) {
        return SurfaceClass::kSeenThrough;
    }
    bool covered = true; // (a blended pass that replaces what is behind: 0, 11 - 13 - coverage 1, as hit_blend's)
    if (mode == kBlendAlpha) {
        covered = least_drawn_alpha(material, least_vertex_alpha) >= 1.0F;
    } else if (mode == kBlendFixedAlpha) {
        covered = std::get<3>(material.colour) >= 1.0F;
    }
    if (covered) {
        return SurfaceClass::kBlendedOpaque;
    }
    return tested ? SurfaceClass::kCutOut : SurfaceClass::kSeenThrough;
}

uint32_t surface_instance_flags(SurfaceClass surface, const Material& material)
{
    switch (surface) {
    case SurfaceClass::kOpaque:
    case SurfaceClass::kBlendedOpaque:
        return kOpaque | kCastsShadow;
    case SurfaceClass::kCutOut:
        return kAlphaTested | kCastsShadow | (material.flags & kTransparent);
    case SurfaceClass::kSeenThrough:
        return kTransparent;
    case SurfaceClass::kAddsLight:
        return kTransparent | kAddsLight;
    }
    return kOpaque | kCastsShadow;
}

const char* surface_class_name(SurfaceClass surface)
{
    switch (surface) {
    case SurfaceClass::kOpaque:
        return "opaque";
    case SurfaceClass::kCutOut:
        return "cut-out";
    case SurfaceClass::kBlendedOpaque:
        return "blended opaque";
    case SurfaceClass::kSeenThrough:
        return "seen through";
    case SurfaceClass::kAddsLight:
        return "adds light";
    }
    return "?";
}

} // namespace render::rt
