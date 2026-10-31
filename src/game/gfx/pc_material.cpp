// The PC materials (sMaterial, material.h): constructed and destroyed, their animations (UV wibbles, colours,
// textures) brought up to date, and set up on the device for drawing - all passes (for the pixel shaders) or one pass
// through the fixed-function texture stages. The animations' arithmetic is the original's x87 code (in registers,
// rounded where it stores), so it is written in x87 here too.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/gfx/material.h"
#include "game/gfx/pc_billboards.h"
#include "game/gfx/primitive_states.h"
#include "game/gfx/render.h"
#include "game/gfx/render_state.h"
#include "game/sys/timer.h"
#include "game/gfx/device.h"
#include "game/sys/startup_init.h"

namespace Nx {

float g_billboardUvMatrix[8]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
float g_wibbleMatrixPass[16]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
float g_environmentMatrixPass[16]{
    0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f};
float g_wibbleMatrix[16]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
float g_environmentMatrix[16]{
    0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f};

namespace {

IDirect3DDevice9* Device() { return g_d3dDevice; }
float* PassConstants() { return g_pixelShaderConstants; } // the pixel shader's colours, 16 bytes a pass
bool& ConstantsChanged() { return reinterpret_cast<bool&>(g_pixelShaderChanged); }

void DeviceStageState(uint32_t stage, D3DTEXTURESTAGESTATETYPE type, uint32_t value)
{
    Device()->SetTextureStageState(stage, type, value);
}

constexpr uint32_t kPassUVWibble = 0x1;      // the texture matrix from the pass's UV wibble
constexpr uint32_t kPassColorAnim = 0x2;     // (pass 0's) the material's colour animations
constexpr uint32_t kPassEnvironment = 0x8;   // environment mapped (near the camera only)
constexpr uint32_t kPassSpecular = 0x100;    // (pass 0's) specular lighting
constexpr uint32_t kPassBorder = 0x200;      // the sampler's border colour 0xE0000000
constexpr uint32_t kPassBumpEnv = 0x400;     // the bump environment matrix
constexpr uint32_t kPassTextureAnim = 0x800; // the texture from the material's texture animation
constexpr uint32_t kPassTextureAlpha = 0x1000; // alpha from the texture only
constexpr uint32_t kPassExplicitUV = 0x4000; // the UV wibble's matrix is set, not animated

// (t - from) / (to - from) between two keys' colours, channel by channel in the order a, r, g, b (the bytes 3, 0, 1,
// 2), each the x87's to * f + from * (1 - f) truncated - and not masked: the four values are shifted and or'd as
// they come.
uint32_t BlendKeyColors(int num, int den, const uint8_t* from, const uint8_t* to)
{
    static const int kOrder[4] = {3, 0, 1, 2};
    uint32_t color = 0;
    double scratch = 0;
    __asm {
        fild num
        fild den
        fdivp st(1), st(0)
        fld1
        fsub st(0), st(1)
    }
    for (int k = 0; k < 4; ++k) {
        int a = to[kOrder[k]];
        int b = from[kOrder[k]];
        int64_t value;
        __asm {
            fild a
            fmul st(0), st(2)
            fild b
            fmul st(0), st(2)
            faddp st(1), st(0)
            fisttp value
        }
        color = k ? (color << 8 | static_cast<uint32_t>(value)) : static_cast<uint32_t>(value);
    }
    __asm {
        fstp scratch
        fstp scratch
    }
    return color;
}

// The time in seconds the UV wibbles use: the milliseconds (unsigned) times 0.001, stored as a float.
float WibbleTime(uint32_t milliseconds)
{
    float result;
    float scale = 0.001f;
    float wrap = 4294967296.0f;
    __asm {
        fild milliseconds
        test milliseconds, 0x80000000
        jz positive
        fadd wrap
    positive:
        fmul scale
        fstp result
    }
    return result;
}

// One axis of a UV wibble's offset: the phase-shifted sine times the amplitude plus the velocity's travel, plus 8,
// less its whole part rounded down to a multiple of 16 - then 8 back the other way (added below 0, taken off
// otherwise). The v axis is stored as a float before that last step, the u axis isn't.
float WibbleOffset(float t, float frequency, float phase, float amplitude, float velocity, bool storeFirst)
{
    float result;
    float eight = 8.0f;
    float zero = 0.0f;
    int64_t whole;
    int truncated;
    float stored;
    __asm {
        fld t
        fmul frequency
        fadd phase
        fsin
        fmul amplitude
        fld t
        fmul velocity
        faddp st(1), st(0)
        fadd eight
        fld st(0)
        fisttp whole
        mov eax, dword ptr [whole]
        and eax, 0xFFFFFFF0
        mov truncated, eax
        fisub truncated
        cmp storeFirst, 0
        je compare
        fstp stored
        fld stored
    compare:
        fcom zero
        fnstsw ax
        test ah, 5
        jp notBelow
        fadd eight
        jmp done
    notBelow:
        fsub eight
    done:
        fstp result
    }
    return result;
}

// What both set-ups share: the alpha reference, culling, depth bias, the animations, and the specular material
// (pass 0's flag 0x100) - or none, once, when the last material had it (0x00786E4C).
void SetCommonStates(sMaterial* material)
{
    SetState(kAlphaRef, material->alphaRef);
    SetState(kCullMode, material->twoSided ? D3DCULL_NONE : D3DCULL_CW);
    SetState(kDepthBias, material->depthBias);
    material->UpdateUVWibbles();
    material->UpdateColorAnims();
    material->UpdateTextureAnims();
    uint32_t specularOn = g_specular;
    D3DMATERIAL9 specular;
    if (material->passFlags[0] & kPassSpecular) {
        if (specularOn)
            return;
        SetState(kSpecular, 1);
        std::memset(&specular, 0, sizeof(specular));
        specular.Specular.r = material->specular[0];
        specular.Specular.g = material->specular[1];
        specular.Specular.b = material->specular[2];
        specular.Specular.a = 1.0f;
        specular.Power = material->specularPower;
    } else {
        if (!specularOn)
            return;
        SetState(kSpecular, 0);
        std::memset(&specular, 0, sizeof(specular));
    }
    Device()->SetMaterial(&specular);
}

// The pass's border colour (0x00786EA4, a word a stage): 0xE0000000 with flag 0x200, else 0.
void SetBorderColor(const sMaterial* material, uint32_t pass, uint32_t stage)
{
    uint32_t& border = g_borderColours[stage];
    if (material->passFlags[pass] & kPassBorder) {
        if (border != 0xE0000000)
            border = 0xE0000000;
    } else if (border) {
        border = 0;
    }
}

void SetBumpEnvMatrix(uint32_t stage)
{
    DeviceStageState(stage, D3DTSS_BUMPENVMAT00, reinterpret_cast<uint32_t&>(g_headingMatrix));
    DeviceStageState(stage, D3DTSS_BUMPENVMAT01, reinterpret_cast<uint32_t&>(g_headingMatrix.row[0].z));
    DeviceStageState(stage, D3DTSS_BUMPENVMAT10, reinterpret_cast<uint32_t&>(g_headingMatrix.row[2].x));
    DeviceStageState(stage, D3DTSS_BUMPENVMAT11, reinterpret_cast<uint32_t&>(g_headingMatrix.row[2].z));
}

// A UV wibble's texture matrix (a static, `matrix`): its 2x2 part (the second row's first value negated) and offsets.
void FillWibbleMatrix(float* matrix, const sUVWibble* wibble)
{
    matrix[0] = wibble->matrix[0];
    matrix[1] = wibble->matrix[1];
    matrix[5] = wibble->matrix[0];
    matrix[4] = -wibble->matrix[1];
    matrix[8] = wibble->offset[0];
    matrix[9] = wibble->offset[1];
}

uint32_t FactorChannel(float value) { return static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(value) * 255.0f)) & 0xFF; }

} // namespace

// 0x004D1A90
sMaterial* sMaterial::Construct()
{
    passCount = 0;
    colorAnimCount = 0;
    colorAnims = nullptr;
    animColors = nullptr;
    textureAnim = nullptr;
    hasUVWibble = false;
    hasTextureAnim = false;
    unknown18 = 0;
    depthBias = 0;
    for (int pass = 0; pass < 4; ++pass) {
        unknownC0[pass] = 0x10001;
        passFlags[pass] = 0;
        textures[pass] = nullptr;
        uvWibble[pass] = nullptr;
        envMapTiling[pass][0] = 3.0f;
        envMapTiling[pass][1] = 3.0f;
    }
    return this;
}

// 0x004D2BD0
void sMaterial::DestroyBody()
{
    Mem::Manager* heap = Mem::Manager::Get();
    if (colorAnims) {
        for (uint32_t i = 0; i < colorAnimCount; ++i)
            heap->Free(colorAnims[i].keys);
        heap->Free(colorAnims);
    }
    if (animColors)
        heap->Free(animColors);
    for (sUVWibble* wibble : uvWibble)
        if (wibble)
            heap->Free(wibble);
    if (textureAnim) {
        for (sTextureKey* keys : textureAnim->keys)
            heap->Free(keys);
        heap->Free(textureAnim);
    }
}

// 0x004D1D90
uint32_t sMaterial::TextureAlphaPasses() const
{
    uint32_t mask = 0;
    for (uint32_t pass = 0; pass < passCount; ++pass)
        if (passFlags[pass] & kPassTextureAlpha)
            mask |= 1u << (pass & 31);
    return mask;
}

// 0x004D1B40
void sMaterial::UpdateColorAnims()
{
    if (!(passFlags[0] & kPassColorAnim))
        return;
    for (uint32_t i = 0; i < colorAnimCount; ++i) {
        const sColorAnim& anim = colorAnims[i];
        const sColorKey* keys = anim.keys;
        int first = keys[0].time;
        int period = keys[anim.keyCount - 1].time - first;
        int t = period ? (anim.phase + g_frameTime) % period + first : 1;
        int key = static_cast<int>(anim.keyCount) - 1;
        while (key >= 0 && t < keys[key].time)
            --key;
        const sColorKey& from = keys[key];
        const sColorKey& to = keys[key + 1];
        animColors[i] = BlendKeyColors(t - from.time, to.time - from.time, from.rgba, to.rgba);
    }
}

// 0x004D1CC0
void sMaterial::UpdateTextureAnims()
{
    if (!hasTextureAnim)
        return;
    int now = static_cast<int>(Tmr::GetTime());
    sTextureAnim* anim = textureAnim;
    for (int pass = 0; pass < 4; ++pass) {
        if (!(passFlags[pass] & kPassTextureAnim))
            continue;
        const sTextureKey* keys = anim->keys[pass];
        int count = static_cast<int>(anim->keyCounts[pass]);
        int first = keys[0].time;
        int period = keys[count - 1].time - first;
        int phase = static_cast<int>(anim->phases[pass]);
        int t = (now + phase) % period + first;
        int loops = anim->loops[pass];
        if (loops > 0 && (now - first) / period >= loops)
            t = (loops * period + phase) % period + first; // (held on the last loop's end)
        int key = count - 1;
        while (key >= 0 && t < keys[key].time)
            --key;
        textures[pass] = keys[key].texture;
    }
}

// 0x004D1DC0
void sMaterial::UpdateUVWibbles()
{
    if (!hasUVWibble)
        return;
    float t = WibbleTime(Tmr::GetTime());
    for (uint32_t pass = 0; pass < passCount; ++pass) {
        if (!(passFlags[pass] & kPassUVWibble) || (passFlags[pass] & kPassExplicitUV))
            continue;
        sUVWibble* w = uvWibble[pass];
        float u = WibbleOffset(t, w->frequency[0], w->phase[0], w->amplitude[0], w->velocity[0], false);
        float v = WibbleOffset(t, w->frequency[1], w->phase[1], w->amplitude[1], w->velocity[1], true);
        w->offset[0] = u;
        w->offset[1] = v;
    }
}

// 0x004D1F00
void sMaterial::Submit()
{
    if (g_submitLocked)
        return;
    SetBlendMode(blendModes[0]);
    SetCommonStates(this);
    for (uint32_t pass = 0; pass < passCount; ++pass) {
        if (g_skippedPasses & (1u << (pass & 31)))
            continue;
        if (!ConstantsChanged() && std::memcmp(passColor[pass], PassConstants() + 4 * pass, 16) != 0)
            ConstantsChanged() = true;
        uint8_t* texture = textures[pass];
        if (!texture) {
            SetTexture(static_cast<int>(pass), nullptr);
            continue;
        }
        SetTexture(static_cast<int>(pass), *reinterpret_cast<void**>(texture + 0x10));
        SetState(kAddressMode0 + static_cast<int>(pass), addressModes[pass]);
        SetState(kMipLodBias0 + static_cast<int>(pass), mipLodBias[pass]);
        SetBorderColor(this, pass, pass);
        uint32_t flags = passFlags[pass];
        if (flags & kPassBumpEnv)
            SetBumpEnvMatrix(pass);
        flags = passFlags[pass];
        D3DTRANSFORMSTATETYPE textureMatrix = static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0 + pass);
        if ((flags & kPassEnvironment) && g_nearCamera) {
            float* matrix = g_environmentMatrixPass;
            matrix[0] = static_cast<float>(envMapTiling[pass][0] * 0.5);
            matrix[5] = static_cast<float>(envMapTiling[pass][1] * 0.5);
            SetState(kLocalViewer, 1);
            Device()->SetTransform(textureMatrix, reinterpret_cast<const D3DMATRIX*>(matrix));
            DeviceStageState(pass, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
            DeviceStageState(pass, D3DTSS_TEXCOORDINDEX, pass | D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
        } else if (flags & kPassUVWibble) {
            DeviceStageState(pass, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
            DeviceStageState(pass, D3DTSS_TEXCOORDINDEX, pass);
            float* matrix = g_wibbleMatrixPass;
            FillWibbleMatrix(matrix, uvWibble[pass]);
            Device()->SetTransform(textureMatrix, reinterpret_cast<const D3DMATRIX*>(matrix));
        } else {
            if (g_billboardFlag) { // (the billboard flag: a static matrix's 2x2 part reset)
                float* m = g_billboardUvMatrix;
                m[0] = 1.0f;
                m[1] = 0;
                m[3] = 0;
                m[4] = 0;
                m[5] = 1.0f;
                m[7] = 0;
            }
            DWORD transformFlags = 0, coordinates = 0;
            Device()->GetTextureStageState(pass, D3DTSS_TEXTURETRANSFORMFLAGS, &transformFlags);
            Device()->GetTextureStageState(pass, D3DTSS_TEXCOORDINDEX, &coordinates);
            if (transformFlags)
                DeviceStageState(pass, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
            if (coordinates != pass)
                DeviceStageState(pass, D3DTSS_TEXCOORDINDEX, pass);
        }
    }
    if (ConstantsChanged())
        std::memcpy(PassConstants(), passColor, passCount * 16);
}

// 0x004D23E0
void sMaterial::SubmitStates()
{
    if (g_submitLocked)
        return;
    SetCommonStates(this);
}

// 0x004D24D0
void sMaterial::SubmitPass(uint32_t pass, const float* color)
{
    if (g_submitLocked)
        return;
    SetBlendMode(blendModes[pass]);
    if (!(g_skippedPasses & (1u << (pass & 31)))) {
        uint8_t* texture = textures[pass];
        if (!texture) {
            SetTexture(0, nullptr);
        } else {
            SetTexture(0, *reinterpret_cast<void**>(texture + 0x10));
            SetState(kAddressMode0, addressModes[pass]);
            SetState(kMipLodBias0, mipLodBias[pass]);
            SetBorderColor(this, pass, pass);
            if (passFlags[pass] & kPassBumpEnv)
                SetBumpEnvMatrix(0);
            uint32_t flags = passFlags[pass];
            if ((flags & kPassEnvironment) && g_nearCamera) {
                float* matrix = g_environmentMatrix;
                matrix[0] = static_cast<float>(envMapTiling[pass][0] * 0.5);
                matrix[5] = static_cast<float>(envMapTiling[pass][1] * 0.5);
                SetState(kLocalViewer, 1);
                Nx::SetTransform(D3DTS_TEXTURE0, matrix);
                SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
                SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, pass | D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
            } else if (flags & kPassUVWibble) {
                DeviceStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
                DeviceStageState(0, D3DTSS_TEXCOORDINDEX, pass);
                float* matrix = g_wibbleMatrix;
                FillWibbleMatrix(matrix, uvWibble[pass]);
                Device()->SetTransform(D3DTS_TEXTURE0, reinterpret_cast<const D3DMATRIX*>(matrix));
            } else {
                DWORD transformFlags = 0, coordinates = 0;
                Device()->GetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, &transformFlags);
                Device()->GetTextureStageState(0, D3DTSS_TEXCOORDINDEX, &coordinates);
                if (transformFlags)
                    SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
                if (coordinates != pass)
                    SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, pass);
                uint32_t blend = blendModes[pass];
                if (blend == 0xC || blend == 0xD) { // (blending with the pass before: its texture on stage 1)
                    SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
                    if (static_cast<int>(pass) > 0) {
                        SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, pass - 1);
                        SetTexture(1, *reinterpret_cast<void**>(textures[pass - 1] + 0x10));
                        SetState(kAddressMode0 + 1, addressModes[pass - 1]);
                        SetState(kMipLodBias0 + 1, mipLodBias[pass - 1]);
                    } else {
                        SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, pass);
                        SetTexture(1, *reinterpret_cast<void**>(textures[pass] + 0x10));
                    }
                }
            }
        }
    }
    if (ConstantsChanged())
        std::memcpy(PassConstants(), passColor, passCount * 16);

    // the stages' colour and alpha operations
    if (static_cast<int8_t>(passFlags[pass] & 0xFF) < 0)
        color = nullptr; // (the pass's colour isn't to be changed)
    const float* own = passColor[pass];
    bool alphaFromTexture = (passFlags[pass] & kPassTextureAlpha) != 0;
    bool blendsWithPrevious = blendModes[pass] == 0xC || blendModes[pass] == 0xD;
    const uint32_t half = 0x3F000000; // 0.5: the colour unchanged
    if (std::memcmp(&own[0], &half, 4) == 0 && std::memcmp(&own[1], &half, 4) == 0 &&
        std::memcmp(&own[2], &half, 4) == 0 && !color) {
        if (!blendsWithPrevious) {
            DeviceStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
            DeviceStageState(0, D3DTSS_ALPHAOP, alphaFromTexture ? D3DTOP_SELECTARG1 : D3DTOP_MODULATE2X);
            DeviceStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
            DeviceStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            DeviceStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
            DeviceStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            DeviceStageState(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
            return;
        }
        DeviceStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        DeviceStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        DeviceStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        DeviceStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
        DeviceStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        DeviceStageState(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
        DeviceStageState(1, D3DTSS_ALPHAOP, alphaFromTexture ? D3DTOP_SELECTARG1 : D3DTOP_MODULATE2X);
        DeviceStageState(1, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        DeviceStageState(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        DeviceStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
    } else {
        DeviceStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        DeviceStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        DeviceStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
        const float* factor = color ? color : own;
        uint32_t r = FactorChannel(factor[0]);
        uint32_t g = FactorChannel(factor[1]);
        uint32_t b = FactorChannel(factor[2]);
        Device()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFF000000u | r << 16 | g << 8 | b);
        if (alphaFromTexture) {
            DeviceStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        } else {
            DeviceStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE2X);
            DeviceStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
        }
        DeviceStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        DeviceStageState(1, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        DeviceStageState(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
        DeviceStageState(1, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        if (blendsWithPrevious) {
            DeviceStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        } else {
            DeviceStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
            DeviceStageState(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
        }
    }
    DeviceStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
}

} // namespace Nx

namespace Nx {

namespace {

// A material nothing uses but its static initialiser and exit function.
sMaterial g_spareMaterial;

} // namespace

// 0x00640D70 (a static initialiser)
void ConstructSpareMaterial() { g_spareMaterial.Construct(); }

} // namespace Nx
