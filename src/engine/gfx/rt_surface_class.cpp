// NOT ORIGINAL - (round PT-B) the surfaces' classes for rays (rt_surface_class.h).
#include "engine/gfx/rt_surface_class.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_vertex_attributes.h"

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

float least_vertex_alpha_in(std::span<const uint8_t> bytes, uint32_t fvf, uint32_t stride, uint32_t count)
{
    Geometry layout;
    layout.vertex_stride = stride; // (the attributes that fit within it - set_vertex_attributes)
    if (!set_vertex_attributes(fvf, &layout) || layout.colour_format != kColourA8R8G8B8 || stride == 0) {
        return 1.0F;
    }
    constexpr uint32_t kAlphaByte = 3; // (the colour word's bytes B, G, R, A)
    constexpr uint32_t kByteMax = 255;
    const auto fits = static_cast<uint32_t>(std::min<std::size_t>(count, bytes.size() / stride));
    uint32_t lowest = kByteMax;
    for (uint32_t vi = 0; vi < fits; ++vi) {
        lowest = std::min<uint32_t>(lowest, core::at(bytes, (std::size_t{vi} * stride) + layout.colour_offset + kAlphaByte));
    }
    return static_cast<float>(lowest) / static_cast<float>(kByteMax);
}

VertexColours vertex_colours_in(std::span<const uint8_t> bytes, uint32_t fvf, uint32_t stride, uint32_t count)
{
    Geometry layout;
    layout.vertex_stride = stride;
    VertexColours out;
    if (!set_vertex_attributes(fvf, &layout) || layout.colour_format != kColourA8R8G8B8 || stride == 0) {
        return out;
    }
    constexpr uint32_t kAlphaByte = 3; // (the colour word's bytes B, G, R, A)
    const auto fits = static_cast<uint32_t>(std::min<std::size_t>(count, bytes.size() / stride));
    if (fits == 0) {
        return out;
    }
    out.known = true;
    out.brightest = UINT8_MAX;
    out.least_alpha = UINT8_MAX;
    for (uint32_t vi = 0; vi < fits; ++vi) {
        const std::size_t at = (std::size_t{vi} * stride) + layout.colour_offset;
        out.brightest =
            std::min(out.brightest, std::max({core::at(bytes, at), core::at(bytes, at + 1), core::at(bytes, at + 2)}));
        const uint8_t alpha = core::at(bytes, at + kAlphaByte);
        out.least_alpha = std::min(out.least_alpha, alpha);
        out.most_alpha = std::max(out.most_alpha, alpha);
    }
    return out;
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

uint32_t reflection_class_on(const Material& material, SurfaceClass surface)
{
    const bool solid = surface == SurfaceClass::kOpaque || surface == SurfaceClass::kBlendedOpaque ||
                       surface == SurfaceClass::kCutOut;
    if (material.reflection_class == kReflectGlass && material.reflection_mask == kReflectEverywhere &&
        material.reflectivity > 0.0F && solid) {
        return kReflectCoat;
    }
    return material.reflection_class;
}

SurfaceCoat surface_coat(const Material& material, uint32_t class_on)
{
    if (class_on != kReflectCoat) {
        return {};
    }
    SurfaceCoat coat{.strength = std::clamp(material.reflectivity, 0.0F, 1.0F), .masks = 0};
    constexpr uint32_t kStages = 4;
    for (std::size_t k = 0; k < material.mask_passes.size(); ++k) {
        const Material::MaskPass& mask = core::at(material.mask_passes, k);
        if (mask.kind == kMaskNone || mask.uv_set >= kStages) {
            continue;
        }
        const uint32_t bits = mask.kind | (mask.texture_alpha ? kCoatMaskTexelAlpha : 0U) | (mask.uv_set << kCoatMaskStageShift);
        coat.masks |= bits << (kCoatMaskBits * static_cast<uint32_t>(k));
    }
    return coat;
}

bool baked_shadow_mesh(bool blended, SurfaceClass surface, std::span<const uint32_t> colours)
{
    // (the vertex colour's luminance in bytes - Rec. 601's weights, as the relit world reads the original's colours)
    constexpr float kRed = 0.299F;
    constexpr float kGreen = 0.587F;
    constexpr float kBlue = 0.114F;
    constexpr uint32_t kByte = 0xFFU;
    const auto black = [](uint32_t argb) {
        return (kRed * static_cast<float>((argb >> 16U) & kByte)) + (kGreen * static_cast<float>((argb >> 8U) & kByte)) +
                   (kBlue * static_cast<float>(argb & kByte)) <
               1.0F;
    };
    return blended && surface != SurfaceClass::kBlendedOpaque && !colours.empty() && std::ranges::all_of(colours, black);
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
