#pragma once

#include <cstdint>

// The renderer's cached Direct3D 9 state (Nx engine, PC): every setter compares with what it last sent and only
// calls the device when something changes. The cache is reset to the device's own defaults (ResetRenderState)
// when the device is created and pushed to the device again (ApplyRenderState) after a reset.
namespace Nx {

// The renderer's own render state numbering (SetState's `state`).
enum RenderStateId {
    kZWrite = 1,          // bool
    kZTest = 2,           // bool: less-equal, else always
    kAlphaRef = 3,        // 0 = alpha test off, else on with this reference
    kAddressMode0 = 4,    // 4-7: a sampler's addressing, U in the low 16 bits and V in the high: 0 wrap,
                          // 1 clamp, 2 border (clamp without border support)
    kMipLodBias0 = 8,     // 8-11: a sampler's mip level bias (float bits)
    kCullMode = 0x10,     // D3DCULL
    kAlphaBlend = 0x11,   // bool
    kAlphaTest = 0x12,    // bool
    kSpecular = 0x13,     // bool (and the local viewer with it)
    kFog = 0x14,          // bool
    kDepthBias = 0x15,    // in units of the factor at 0x0064AAA4
    kLighting = 0x16,     // bool
    kFilter0 = 0x20,      // 0x20-0x23: a sampler's filters, bit 0 minification linear, bit 16 magnification
    kLocalViewer = 0x24,  // bool
};

// 0x004DAB60: set one of the renderer's states (RenderStateId) on the device, if it isn't already.
void __cdecl SetState(int state, uint32_t value);
// 0x004D9DB0: the vertex format (D3DFVF), unless the format is locked (0x00786F28).
void __cdecl SetVertexFormat(uint32_t fvf);
// 0x004DA730: the texture of a stage.
void __cdecl SetTexture(int stage, void* texture);
// 0x004DA760: the blend mode - the low 24 bits pick the source / destination factors and operation (a table of
// the game's modes), the high 8 bits the blend factor's alpha (x2, saturating). An override (0x00786F34) wins if
// set.
void __cdecl SetBlendMode(uint32_t mode);
// 0x004D9DE0: the texture stages that stand in for a pixel shader (the shader handles at 0x007CA944-64) when
// the device has no pixel shaders; `factor` is the texture factor colour some of them use.
void __cdecl SetFixedFunctionStages(void* shader, uint32_t factor);
// 0x004DAA10: the same with the material's colour (+0x40, three floats 0-1) as the factor - opaque white
// without a material.
void __cdecl SetFixedFunctionStagesForMaterial(void* shader, const void* material);
// 0x004DAA80: the pixel shader and its five constants (0x00786ECC) - or the stand-in texture stages without
// pixel shaders (0x00786AB8).
void __cdecl SetPixelShader(void* shader, const void* material);
// 0x004DAAF0: the pixel shader with `count` constants, and constant 4 (0x00786F0C) again.
void __cdecl SetPixelShaderWithConstants(void* shader, uint32_t count);
// 0x004DA0E0: the value 0x007CA978 (a distance the visibility test 0x004DB790 leaves).
float __cdecl LastSphereDistance();
// 0x004DB620: the object's world transform for culling and lighting (its position row kept), or none.
void __cdecl SetWorldTransform(const float* matrix);

// 0x004D3CA0 / 0x004D3CE0: the scaling the skinned meshes' loaders apply set (four values, 0x007865D0 - the last a
// table of 16-byte bone scales, 0x007865DC) and on (0x007865CC) / off and cleared.
void __cdecl SetSkinScaling(const uint32_t* settings);
void __cdecl ClearSkinScaling();
// 0x004D3ED0: the pixel shader a scene file's index names (0x007CA920-64; 0 and 1: none; past 0x12: the index
// itself).
void* __stdcall PixelShaderFromIndex(uint32_t index);

// 0x004D7D70: the cache set to the state of a new device (identity transforms, the default light...).
void __cdecl ResetRenderState();
// 0x004D80D0: the cached state sent to the device (after it is created or reset), with the fixed ones.
void __cdecl ApplyRenderState();

} // namespace Nx
