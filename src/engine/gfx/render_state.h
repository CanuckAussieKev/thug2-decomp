#pragma once

#include <cstdint>
#include <array>
#include <span>

#include "base/core/enum_ops.h"
#include "gpu/gpu_handles.h"

// The renderer's cached Direct3D 9 state (Nx engine, PC): every setter compares with what it last sent and only
// calls the device when something changes. The cache is reset to the device's own defaults (reset_render_state)
// when the device is created and pushed to the device again (apply_render_state) after a reset.
namespace render {

// choose_blend_mode's modes the pixel shaders and the fixed-function states know (with kBlendModeAlpha, 5, and
// kBlendModePrimitive below): the Fix modes take the pass colour's alpha, the others the texture's; 0xC and 0xD blend
// with the pass before.
constexpr int kBlendModeFixBlend = 6;
constexpr int kBlendModeModulate = 7;
constexpr int kBlendModeFixModulate = 8;
constexpr int kBlendModeBrighten = 9;
constexpr int kBlendModeFixBrighten = 0xA;
constexpr int kBlendModeVertexTexAlpha = 0xB;
constexpr int kBlendModeLerpPrevious = 0xC;
constexpr int kBlendModeLerpFromPrevious = 0xD;
constexpr int kBlendModeMultiplyColour = 0xF;

// Values the renderer's callers pass to the setters below.
constexpr int kClampBothWays = 0x10001;   // an address mode: clamp (1) in U (the low half) and in V (the high half)
constexpr int kBlendModeAlpha = 5;        // choose_blend_mode: the alpha blend the 2D drawing uses
constexpr int kBlendModePrimitive = 0x11; // choose_blend_mode: what the primitives' state is set up with
constexpr int kScreenVertexFormat = 0x144; // D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1
constexpr int kBlendModeMask = 0xFFFFFF;    // a pass's blend mode in its blend word (the top byte: its fixed alpha, flags)
constexpr int kTextureOpModulate2x = 5;    // D3DTOP_MODULATE2X (a texture stage's colour or alpha op)
constexpr int kLockDiscard = 0x10;         // D3DLOCK_DISCARD
constexpr unsigned kLockReadOnly = 0x10U;  // D3DLOCK_READONLY
// The flexible vertex format's bits the renderer and the editor read (D3DFVF_*).
constexpr uint32_t kFvfNormal = 0x010U;
constexpr uint32_t kFvfDiffuse = 0x040U;
constexpr uint32_t kFvfSpecular = 0x080U;
constexpr uint32_t kFvfTex1 = 0x100U; // one to four sets of texture coordinates
constexpr uint32_t kFvfTex2 = 0x200U;
constexpr uint32_t kFvfTex3 = 0x300U;
constexpr uint32_t kFvfTex4 = 0x400U;

struct MaterialRef;

// The renderer's own render state numbering (change_phase's `state`).
enum class RenderStateId : uint8_t {
    kZWrite = 1,         // bool
    kZTest = 2,          // bool: less-equal, else always
    kPhaseAlphaTest = 3, // 0 = alpha test off, else on with this reference
    // a sampler's addressing, U in the low 16 bits and V in the high: 0 wrap, 1 clamp, 2 border (clamp without border
    // support)
    kAddressMode0 = 4,
    kAddressMode1 = 5,
    kAddressMode2 = 6,
    kAddressMode3 = 7,
    // a sampler's mip level bias (float bits)
    kMipLodBias0 = 8,
    kMipLodBias1 = 9,
    kMipLodBias2 = 0xA,
    kMipLodBias3 = 0xB,
    kCullMode = 0x10,   // D3DCULL
    kAlphaBlend = 0x11, // bool
    kAlphaTest = 0x12,  // bool
    kSpecular = 0x13,   // bool (and the local viewer with it)
    kFog = 0x14,        // bool
    kDepthBias = 0x15,  // in units of the factor at 0x0064AAA4
    kLighting = 0x16,   // bool
    // a sampler's filters, bit 0 minification linear, bit 16 magnification
    kFilter0 = 0x20,
    kFilter1 = 0x21,
    kFilter2 = 0x22,
    kFilter3 = 0x23,
    kLocalViewer = 0x24, // bool
};
// The per-sampler states of sampler `sampler` (0-3).
[[nodiscard]] constexpr RenderStateId address_mode_state(int sampler)
{
    return static_cast<RenderStateId>(core::to_underlying(RenderStateId::kAddressMode0) + sampler);
}
[[nodiscard]] constexpr RenderStateId mip_lod_bias_state(int sampler)
{
    return static_cast<RenderStateId>(core::to_underlying(RenderStateId::kMipLodBias0) + sampler);
}
// The sampler a per-sampler state is for, its first sampler's state `first`.
[[nodiscard]] constexpr int sampler_of(RenderStateId state, RenderStateId first)
{
    return core::to_underlying(state) - core::to_underlying(first);
}

// 0x004DAB60: set one of the renderer's states (RenderStateId) on the device, if it isn't already.
void change_phase(RenderStateId state, uint32_t value);
// 0x004D9DB0: the vertex format (D3DFVF), unless the format is locked (0x00786F28).
void set_vertex_format(uint32_t fvf);
// 0x004DA730: the texture of a stage.
void set_texture(int stage, GpuTexture* texture);
// 0x004DA760: the blend mode - the low 24 bits pick the source / destination factors and operation (a table of
// the game's modes), the high 8 bits the blend factor's alpha (x2, saturating). An override (0x00786F34) wins if
// set.
void choose_blend_mode(uint32_t mode);
// 0x004D9DE0: the texture stages that stand in for a pixel shader (the shader handles at 0x007CA944-64) when
// the device has no pixel shaders; `factor` is the texture factor colour some of them use.
void set_fixed_function_stages(void* shader, uint32_t factor);
// 0x004DAA10: the same with the material's colour (+0x40, three floats 0-1) as the factor - opaque white
// without a material.
void set_fixed_function_stages_for_material(void* shader, const MaterialRef* material);
// 0x004DAA80: the pixel shader and its five constants (0x00786ECC) - or the stand-in texture stages without
// pixel shaders (0x00786AB8).
void set_pixel_shader(void* shader, const MaterialRef* material);
// 0x004DAAF0: the pixel shader with `count` constants, and constant 4 (0x00786F0C) again.
void set_pixel_shader_with_constants(void* shader, uint32_t count);
// 0x004DA0E0: the value 0x007CA978 (a distance the visibility test 0x004DB790 leaves).
// 0x004DB620: the object's world transform for culling and lighting (its position row kept), or none.
void set_world_transform(const float* matrix);

// 0x004D3CA0 / 0x004D3CE0: the scaling the skinned meshes' loaders apply set (four values, 0x007865D0 - the last a
// table of 16-byte bone scales, 0x007865DC) and on (0x007865CC) / off and cleared.
// The skin scaling's settings (a cutscene's skinned model's colours, while it loads): the colour table's entries, the
// table, and the colours (four floats a bone) the loaders scale the bones' positions by.
struct SkinScaling {
    std::span<const uint8_t> entries; // the colour table's entries (the original: its end and its start)
    void* table = nullptr;
    std::span<const float> colours; // four floats a bone
};
void set_skin_scaling(const SkinScaling* settings);
void clear_skin_scaling();
// 0x004D3ED0: the pixel shader a scene file's index names (0x007CA920-64; 0 and 1: none; past 0x12: the index
// itself).
void* pixel_shader_from_index(uint32_t index);

// 0x004D7D70: the cache set to the state of a new device (identity transforms, the default light...).
void reset_render_state();
// 0x004D80D0: the cached state sent to the device (after it is created or reset), with the fixed ones.
void apply_render_state();

// The render state the device was last sent (render_state.cpp's record; the original's globals).

} // namespace render
