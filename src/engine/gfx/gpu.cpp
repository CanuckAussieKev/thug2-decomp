// NOT ORIGINAL - the installed Gpu and its state block (gpu.h).

#include "engine/gfx/gpu.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <array>

#include "base/core/checked_index.h"
#include "base/core/enum_ops.h"
#include "engine/gfx/gpu_handles.h"

namespace render {

namespace {

// The Gpu app/ installed (a constant-initialised pointer: no guard, nothing to destroy).
Gpu*& installed()
{
    static Gpu* gpu = nullptr;
    return gpu;
}

constexpr uint32_t kTrue = 1;
constexpr uint32_t kAllBits = 0xFFFFFFFF;
constexpr uint32_t kColourWriteAll = 0xF;     // red, green, blue and alpha written
constexpr uint32_t kZBufferOn = 1;            // D3DZB_TRUE (a device made with a depth buffer)
constexpr uint32_t kShadeGouraud = 2;         // D3DSHADE_GOURAUD
constexpr uint32_t kStencilKeep = 1;          // D3DSTENCILOP_KEEP
constexpr uint32_t kDegreeCubic = 3;          // D3DDEGREE_CUBIC
constexpr uint32_t kDegreeLinear = 1;         // D3DDEGREE_LINEAR
constexpr uint32_t kMaxAnisotropyDefault = 1; // one tap
constexpr float kPointSizeMaxDefault = 64.0F;
constexpr std::size_t kWorldSlot = 0;
constexpr std::size_t kViewSlot = 1;
constexpr std::size_t kProjectionSlot = 2;
constexpr std::size_t kFirstStageSlot = 3;
constexpr std::size_t kConstantFloats = 4; // a pixel shader constant: four floats
constexpr uint32_t kFloatOne = std::bit_cast<uint32_t>(1.0F);

// The render states Direct3D 9 doesn't start at 0 (the rest do).
struct StateDefault {
    uint32_t state;
    uint32_t value;
};
constexpr auto kRenderStateDefaults = std::to_array<StateDefault>({
    {.state = 7, .value = kZBufferOn},                                     // ZENABLE
    {.state = 8, .value = gpu_value::kFillSolid},                          // FILLMODE
    {.state = 9, .value = kShadeGouraud},                                  // SHADEMODE
    {.state = 14, .value = kTrue},                                         // ZWRITEENABLE
    {.state = 16, .value = kTrue},                                         // LASTPIXEL
    {.state = 19, .value = gpu_value::kBlendOne},                          // SRCBLEND
    {.state = 20, .value = gpu_value::kBlendZero},                         // DESTBLEND
    {.state = 22, .value = gpu_value::kCullCounterClockwise},              // CULLMODE
    {.state = 23, .value = gpu_value::kCompareLessEqual},                  // ZFUNC
    {.state = 25, .value = gpu_value::kCompareAlways},                     // ALPHAFUNC
    {.state = 37, .value = kFloatOne},                                     // FOGEND
    {.state = 38, .value = kFloatOne},                                     // FOGDENSITY
    {.state = 53, .value = kStencilKeep},                                  // STENCILFAIL
    {.state = 54, .value = kStencilKeep},                                  // STENCILZFAIL
    {.state = 55, .value = kStencilKeep},                                  // STENCILPASS
    {.state = 56, .value = gpu_value::kCompareAlways},                     // STENCILFUNC
    {.state = 58, .value = kAllBits},                                      // STENCILMASK
    {.state = 59, .value = kAllBits},                                      // STENCILWRITEMASK
    {.state = 60, .value = kAllBits},                                      // TEXTUREFACTOR
    {.state = 136, .value = kTrue},                                        // CLIPPING
    {.state = 137, .value = kTrue},                                        // LIGHTING
    {.state = 141, .value = kTrue},                                        // COLORVERTEX
    {.state = 142, .value = kTrue},                                        // LOCALVIEWER
    {.state = 145, .value = gpu_value::kSourceColour1},                    // DIFFUSEMATERIALSOURCE
    {.state = 146, .value = gpu_value::kSourceColour2},                    // SPECULARMATERIALSOURCE
    {.state = 154, .value = kFloatOne},                                    // POINTSIZE
    {.state = 155, .value = kFloatOne},                                    // POINTSIZE_MIN
    {.state = 158, .value = kFloatOne},                                    // POINTSCALE_A
    {.state = 161, .value = kTrue},                                        // MULTISAMPLEANTIALIAS
    {.state = 162, .value = kAllBits},                                     // MULTISAMPLEMASK
    {.state = 166, .value = std::bit_cast<uint32_t>(kPointSizeMaxDefault)}, // POINTSIZE_MAX
    {.state = 168, .value = kColourWriteAll},                              // COLORWRITEENABLE
    {.state = 171, .value = gpu_value::kBlendOpAdd},                       // BLENDOP
    {.state = 172, .value = kDegreeCubic},                                 // POSITIONDEGREE
    {.state = 173, .value = kDegreeLinear},                                // NORMALDEGREE
    {.state = 178, .value = kFloatOne},                                    // MINTESSELLATIONLEVEL
    {.state = 179, .value = kFloatOne},                                    // MAXTESSELLATIONLEVEL
    {.state = 182, .value = kFloatOne},                                    // ADAPTIVETESS_Z
    {.state = 186, .value = kStencilKeep},                                 // CCW_STENCILFAIL
    {.state = 187, .value = kStencilKeep},                                 // CCW_STENCILZFAIL
    {.state = 188, .value = kStencilKeep},                                 // CCW_STENCILPASS
    {.state = 189, .value = gpu_value::kCompareAlways},                    // CCW_STENCILFUNC
    {.state = 190, .value = kColourWriteAll},                              // COLORWRITEENABLE1
    {.state = 191, .value = kColourWriteAll},                              // COLORWRITEENABLE2
    {.state = 192, .value = kColourWriteAll},                              // COLORWRITEENABLE3
    {.state = 193, .value = kAllBits},                                     // BLENDFACTOR
    {.state = 207, .value = gpu_value::kBlendOne},                         // SRCBLENDALPHA
    {.state = 208, .value = gpu_value::kBlendZero},                        // DESTBLENDALPHA
    {.state = 209, .value = gpu_value::kBlendOpAdd},                       // BLENDOPALPHA
});

constexpr GpuMatrix kIdentity = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                                 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};

// The block's slot for a transform, or kGpuTransformCount for one it doesn't keep.
std::size_t transform_slot(GpuTransform transform)
{
    const uint32_t number = core::to_underlying(transform);
    const uint32_t first_stage = core::to_underlying(GpuTransform::kTexture0);
    if (transform == GpuTransform::kWorld) {
        return kWorldSlot;
    }
    if (transform == GpuTransform::kView) {
        return kViewSlot;
    }
    if (transform == GpuTransform::kProjection) {
        return kProjectionSlot;
    }
    if (number >= first_stage && number < first_stage + kGpuStageCount) {
        return kFirstStageSlot + (number - first_stage);
    }
    return kGpuTransformCount;
}

} // namespace

Gpu& gpu() { return *installed(); }

void install_gpu(Gpu& gpu) { installed() = &gpu; }

void GpuStateBlock::reset()
{
    render_states.fill(0);
    for (const StateDefault& entry : kRenderStateDefaults) {
        core::at(render_states, entry.state) = entry.value;
    }
    for (std::size_t stage = 0; stage < kGpuStageCount; ++stage) {
        auto& states = core::at(stage_states, stage);
        states.fill(0);
        const bool first = stage == 0;
        core::at(states, core::to_underlying(GpuStageState::kColourOp)) =
            first ? gpu_value::kOpModulate : gpu_value::kOpDisable;
        core::at(states, core::to_underlying(GpuStageState::kColourArg1)) = gpu_value::kArgTexture;
        core::at(states, core::to_underlying(GpuStageState::kColourArg2)) = gpu_value::kArgCurrent;
        core::at(states, core::to_underlying(GpuStageState::kAlphaOp)) =
            first ? gpu_value::kOpSelectArg1 : gpu_value::kOpDisable;
        core::at(states, core::to_underlying(GpuStageState::kAlphaArg1)) = gpu_value::kArgTexture;
        core::at(states, core::to_underlying(GpuStageState::kAlphaArg2)) = gpu_value::kArgCurrent;
        core::at(states, core::to_underlying(GpuStageState::kTexCoordIndex)) = static_cast<uint32_t>(stage);
        core::at(states, core::to_underlying(GpuStageState::kColourArg0)) = gpu_value::kArgCurrent;
        core::at(states, core::to_underlying(GpuStageState::kAlphaArg0)) = gpu_value::kArgCurrent;
        core::at(states, core::to_underlying(GpuStageState::kResultArg)) = gpu_value::kArgCurrent;
    }
    for (auto& states : sampler_states) {
        states.fill(0);
        core::at(states, core::to_underlying(GpuSamplerState::kAddressU)) = gpu_value::kAddressWrap;
        core::at(states, core::to_underlying(GpuSamplerState::kAddressV)) = gpu_value::kAddressWrap;
        core::at(states, core::to_underlying(GpuSamplerState::kAddressW)) = gpu_value::kAddressWrap;
        core::at(states, core::to_underlying(GpuSamplerState::kMagFilter)) = gpu_value::kFilterPoint;
        core::at(states, core::to_underlying(GpuSamplerState::kMinFilter)) = gpu_value::kFilterPoint;
        core::at(states, core::to_underlying(GpuSamplerState::kMipFilter)) = gpu_value::kFilterNone;
        core::at(states, core::to_underlying(GpuSamplerState::kMaxAnisotropy)) = kMaxAnisotropyDefault;
    }
    textures.fill(nullptr);
    vertex_format = 0;
    pixel_shader = nullptr;
    for (auto& constant : pixel_constants) {
        constant.fill(0.0F);
    }
    vertex_shader = nullptr;
    for (auto& constant : vertex_constants) {
        constant.fill(0.0F);
    }
    material = GpuMaterial{};
    lights.fill(GpuLight{});
    lights_enabled = 0;
    transforms.fill(kIdentity);
    viewport = GpuViewport{};
}

uint32_t GpuStateBlock::render_state(GpuRenderState state) const
{
    return core::at(render_states, core::to_underlying(state));
}

uint32_t GpuStateBlock::stage_state(uint32_t stage, GpuStageState state) const
{
    return core::at(core::at(stage_states, stage), core::to_underlying(state));
}

uint32_t GpuStateBlock::sampler_state(uint32_t sampler, GpuSamplerState state) const
{
    return core::at(core::at(sampler_states, sampler), core::to_underlying(state));
}

int GpuStateBlock::enabled_light_count() const { return std::popcount(lights_enabled); }

void GpuStateBlock::record_render_state(GpuRenderState state, uint32_t value)
{
    const uint32_t number = core::to_underlying(state);
    if (number < kGpuRenderStateCount) {
        core::at(render_states, number) = value;
    }
}

void GpuStateBlock::record_stage_state(uint32_t stage, GpuStageState state, uint32_t value)
{
    const uint32_t number = core::to_underlying(state);
    if (stage < kGpuStageCount && number < kGpuStageStateCount) {
        core::at(core::at(stage_states, stage), number) = value;
    }
}

void GpuStateBlock::record_sampler_state(uint32_t sampler, GpuSamplerState state, uint32_t value)
{
    const uint32_t number = core::to_underlying(state);
    if (sampler < kGpuSamplerCount && number < kGpuSamplerStateCount) {
        core::at(core::at(sampler_states, sampler), number) = value;
    }
}

void GpuStateBlock::record_texture(uint32_t stage, GpuTexture* texture)
{
    if (stage < kGpuSamplerCount) {
        core::at(textures, stage) = texture;
    }
}

namespace {
// `count` four-float constants from constant `first` into `constants` (the ones past its end are not kept).
template <std::size_t N>
void record_constants(std::array<std::array<float, kConstantFloats>, N>& constants, uint32_t first, const float* values,
                      uint32_t count)
{
    if (values == nullptr || first >= N) {
        return;
    }
    const std::size_t kept = std::min<std::size_t>(count, N - first);
    const std::span<const float> floats(values, kept * kConstantFloats);
    for (std::size_t i = 0; i < kept; ++i) {
        auto& constant = core::at(constants, first + i);
        std::copy_n(floats.subspan(i * kConstantFloats).begin(), kConstantFloats, constant.begin());
    }
}
} // namespace

void GpuStateBlock::record_pixel_constants(uint32_t first, const float* values, uint32_t count)
{
    record_constants(pixel_constants, first, values, count);
}

void GpuStateBlock::record_vertex_constants(uint32_t first, const float* values, uint32_t count)
{
    record_constants(vertex_constants, first, values, count);
}

void GpuStateBlock::record_light(uint32_t index, const GpuLight& light)
{
    if (index < kGpuLightCount) {
        core::at(lights, index) = light;
    }
}

void GpuStateBlock::record_light_enabled(uint32_t index, bool on)
{
    if (index < kGpuLightCount) {
        const uint32_t bit = 1U << index;
        lights_enabled = on ? (lights_enabled | bit) : (lights_enabled & ~bit);
    }
}

void GpuStateBlock::record_transform(GpuTransform transform, const float* matrix)
{
    const std::size_t slot = transform_slot(transform);
    if (matrix != nullptr && slot < kGpuTransformCount) {
        std::copy_n(std::span(matrix, kGpuMatrixFloats).begin(), kGpuMatrixFloats, core::at(transforms, slot).begin());
    }
}

} // namespace render
