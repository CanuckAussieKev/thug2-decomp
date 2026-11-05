// The renderer's cached Direct3D 9 state: render states, sampler states, textures, blend modes, pixel shaders.

#include "render_pc/render_state.h"
#include "engine/gfx/mesh_formats.h"
#include "engine/gfx/surface_material.h"

#include <cstddef>
#include <bit>
#include <cstdint>
#include <array>
#include <span>

#include "render_pc/device.h"
#include "render_pc/graphics_options_d3d.h"
#include "render_pc/pc_billboards.h"
#include "render_pc/pixel_shaders.h"
#include "render_pc/view_mode.h"
#include "render_pc/shader_slots.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/light_sets.h"
#include "engine/gfx/effects_state.h"
#include "engine/gfx/view_setup.h"
#include "engine/gfx/engine_parts.h"

namespace render {

constexpr uint32_t kLowHalfMask = 0xFFFF;
constexpr uint32_t kHighHalfMask = 0xFFFF0000;
constexpr int kHalfBits = 16;
constexpr uint32_t kMagLinearBit = 0x10000; // a filter value: magnification linear (the low half is minification)
constexpr uint32_t kDefaultFilters = 0x10000;
constexpr uint32_t kAlphaMask = 0xFF000000;
constexpr uint32_t kAlphaHighBit = 0x80000000;
constexpr int kByte1Shift = 8;
constexpr uint32_t kWhiteColour = 0xFFFFFFFF;
constexpr uint32_t kBytePadBits = 0xFFFFFF00;
constexpr int kPsConstantVectors = 5;
constexpr std::size_t kPsConstantTailOffset = 16;
constexpr uint32_t kUnsetState = 0xDEADBABE; // what the cached blend state is set to so the next change goes through
constexpr int kPsConstant4X = 16; // pixel shader constant 4 is floats 16-19
constexpr int kPsConstant4Y = 17;
constexpr int kPsConstant4Z = 18;
constexpr int kPsConstant4W = 19;
constexpr float kPsGrey = 0.5F;
constexpr int kLightConstantCount = 24;
constexpr float kFogBeginDefault = 240.0F;
constexpr float kFogEndDefault = 24600.0F;
constexpr int kLowResHeight = 480;

namespace {
// NOT ORIGINAL grouping - the device's render states as last set (blend, alpha test, dither, z test, lighting, the
// stages, the vertex format) (the original's globals). Made on first use, never destroyed (render_state_cache()).
struct RenderStateCache {
    uint32_t blend_mode_{};
    uint32_t blend_op_{};
    uint32_t src_blend_{};
    uint32_t dest_blend_{};
    uint32_t alpha_blend_{};
    uint32_t alpha_test_{};
    uint32_t alpha_test_value_{};
    uint8_t dither_{};
    uint8_t z_test_{};
    uint32_t lighting_{};
    std::array<uint32_t, 4> address_modes_{};
    std::array<uint32_t, 4> mip_lod_biases_{};
    std::array<uint32_t, 4> filters_{};
    uint32_t vertex_format_{};
};

RenderStateCache& render_state_cache()
{
    static core::NoDestroy<RenderStateCache> state;
    return state.get();
}

} // namespace
namespace {
// NOT ORIGINAL grouping - the forced blend and the local viewer switch (the original's globals). Made on first use,
// never destroyed (forced_states()).
struct ForcedStates {
    uint32_t forced_blend_{};
    uint8_t local_viewer_{};
};

ForcedStates& forced_states()
{
    static core::NoDestroy<ForcedStates> state;
    return state.get();
}

} // namespace

namespace {

// What the device was last given (the cache).
uint32_t& get_blend_mode() { return render_state_cache().blend_mode_; }
uint32_t& blend_op() { return render_state_cache().blend_op_; }
uint32_t& src_blend() { return render_state_cache().src_blend_; }
uint32_t& dest_blend() { return render_state_cache().dest_blend_; }
uint32_t& alpha_blend() { return render_state_cache().alpha_blend_; }
uint32_t& alpha_test() { return render_state_cache().alpha_test_; }
uint32_t& alpha_test_level() { return render_state_cache().alpha_test_value_; }

uint8_t& dither() { return render_state_cache().dither_; }

uint8_t& get_z_test() { return render_state_cache().z_test_; }

uint32_t& get_lighting() { return render_state_cache().lighting_; }

std::array<uint32_t, 4>& address_modes() { return render_state_cache().address_modes_; }   // [4]: U low 16 bits, V high
std::array<uint32_t, 4>& mip_lod_biases() { return render_state_cache().mip_lod_biases_; } // [4]: float bits
std::array<uint32_t, 4>& filters() { return render_state_cache().filters_; }               // [4]: min low 16 bits, mag high

uint32_t& vertex_format() { return render_state_cache().vertex_format_; }

uint32_t& forced_blend() { return forced_states().forced_blend_; }

uint8_t& local_viewer() { return forced_states().local_viewer_; }

// Device capabilities (found at start-up).

// The pixel shaders (handles made at start-up) the stand-in texture stages know.
void* get_shader(int slot) { return core::at(pixel_shaders(), slot); }

void set_tss(uint32_t stage, GpuStageState type, uint32_t value) { gpu().set_stage_state(stage, type, value); }
void set_sampler(uint32_t sampler, GpuSamplerState type, uint32_t value) { gpu().set_sampler_state(sampler, type, value); }
void set_device_render_state(GpuRenderState state, uint32_t value) { gpu().set_render_state(state, value); }

// The address mode (D3DTADDRESS) for the renderer's addressing modes (border needs the device's support; else clamp).
bool address_value(uint32_t mode, uint32_t* value)
{
    switch (mode) {
    case 0:
        *value = gpu_value::kAddressWrap;
        return true;
    case 1:
        *value = gpu_value::kAddressClamp;
        return true;
    case 2:
        *value = renderer().border_addressing_supported() ? gpu_value::kAddressBorder : gpu_value::kAddressClamp;
        return true;
    default:
        return false; // (nothing sent, but the cache takes the value)
    }
}

constexpr float kDepthBiasUnit = std::bit_cast<float>(0xB60637BDU); // 0x0064AAA4
constexpr float k255 = 255.0F;                                      // 0x00646B88

// The CRT's _ftol (0x006236FC): truncation, as a 64-bit integer.
int64_t ftol(double value) { return static_cast<int64_t>(value); }

// A sampler's address modes (u in the low half, v in the high one), each set when it changed.
void change_address_mode(RenderStateId state, uint32_t value)
{
    const int sampler = sampler_of(state, RenderStateId::kAddressMode0);
    uint32_t& cached = core::at(address_modes(), sampler);
    const uint32_t u = value & 0xFFFF;
    if (u != (cached & kLowHalfMask)) {
        uint32_t address = 0;
        if (address_value(u, &address)) {
            set_sampler(sampler, GpuSamplerState::kAddressU, address);
        }
        cached = (cached & kHighHalfMask) | u;
    }
    const uint32_t v = value & 0xFFFF0000;
    if (v == (cached & kHighHalfMask)) {
        return;
    }
    uint32_t address = 0;
    if (address_value(v >> kHalfBits, &address)) {
        set_sampler(sampler, GpuSamplerState::kAddressV, address);
    }
    cached = (cached & kLowHalfMask) | v;
}

// A sampler's filters (the magnification in the high half, the minification in the low one), each set when it
// changed.
void change_filter(RenderStateId state, uint32_t value)
{
    const int sampler = sampler_of(state, RenderStateId::kFilter0);
    uint32_t& cached = core::at(filters(), sampler);
    const uint32_t mag = value & 0xFFFF0000;
    if (mag != (cached & kHighHalfMask)) {
        set_sampler(sampler, GpuSamplerState::kMagFilter, ((value & kMagLinearBit) != 0U) ? gpu_value::kFilterLinear : gpu_value::kFilterPoint);
        cached = (cached & kLowHalfMask) | mag;
    }
    const uint32_t min = value & 0xFFFF;
    if (min != (cached & kLowHalfMask)) {
        // NOT ORIGINAL - [Graphics] anisotropy (engine/gfx/graphics_options.h): anisotropic in place of linear.
        // Original behaviour: (value & 1) ? D3DTEXF_LINEAR : D3DTEXF_POINT
        set_sampler(sampler, GpuSamplerState::kMinFilter,
                    graphics_options::get_min_filter((value & 1) != 0, graphics_options::active_anisotropy()));
        cached = (cached & kHighHalfMask) | min;
    }
}

} // namespace

// 0x004DAB60
void change_phase(RenderStateId state, uint32_t value)
{
    auto& gfx = renderer();
    switch (state) {
    case RenderStateId::kZWrite:
        if (value != 0U) {
            if (gfx.z_write() == 0U) {
                render::set_render_state(GpuRenderState::kZWriteEnable, 1U);
                gfx.set_z_write(1);
            }
        } else if (gfx.z_write() == 1) {
            render::set_render_state(GpuRenderState::kZWriteEnable, 0U);
            gfx.set_z_write(0);
        }
        return;
    case RenderStateId::kZTest:
        if (value != 0U) {
            if (get_z_test() == 0U) {
                render::set_render_state(GpuRenderState::kZFunc, gpu_value::kCompareLessEqual);
                get_z_test() = 1;
            }
        } else if (get_z_test() == 1) {
            render::set_render_state(GpuRenderState::kZFunc, gpu_value::kCompareAlways);
            get_z_test() = 0;
        }
        return;
    case RenderStateId::kPhaseAlphaTest:
        if (value == alpha_test_level()) {
            return;
        }
        alpha_test_level() = value;
        if (value != 0U) {
            render::set_render_state(GpuRenderState::kAlphaRef, value);
            if (alpha_test() == 0U) {
                render::set_render_state(GpuRenderState::kAlphaTestEnable, 1U);
                alpha_test() = 1;
            }
        } else if (alpha_test() != 0U) {
            render::set_render_state(GpuRenderState::kAlphaTestEnable, 0U);
            alpha_test() = 0;
        }
        return;
    case RenderStateId::kAddressMode0:
    case RenderStateId::kAddressMode1:
    case RenderStateId::kAddressMode2:
    case RenderStateId::kAddressMode3: {
        change_address_mode(state, value);
        return;
    }
    case RenderStateId::kMipLodBias0:
    case RenderStateId::kMipLodBias1:
    case RenderStateId::kMipLodBias2:
    case RenderStateId::kMipLodBias3: {
        const int sampler = sampler_of(state, RenderStateId::kMipLodBias0);
        if (value != core::at(mip_lod_biases(), sampler)) {
            set_sampler(sampler, GpuSamplerState::kMipLodBias, value);
            core::at(mip_lod_biases(), sampler) = value;
        }
        return;
    }
    case RenderStateId::kCullMode:
        if (value != renderer().cull_mode()) {
            gfx.set_cull_mode(value);
            render::set_render_state(GpuRenderState::kCullMode, value);
        }
        return;
    case RenderStateId::kAlphaBlend:
        if (value != alpha_blend()) {
            alpha_blend() = value;
            render::set_render_state(GpuRenderState::kAlphaBlendEnable, static_cast<uint32_t>(value != 0));
        }
        return;
    case RenderStateId::kAlphaTest:
        if (value != alpha_test()) {
            alpha_test() = value;
            render::set_render_state(GpuRenderState::kAlphaTestEnable, static_cast<uint32_t>(value != 0));
        }
        return;
    case RenderStateId::kSpecular:
        if (value != gfx.specular_state()) {
            gfx.set_specular(value);
            render::set_render_state(GpuRenderState::kSpecularEnable, static_cast<uint32_t>(value != 0));
            render::set_render_state(GpuRenderState::kLocalViewer, static_cast<uint32_t>(value != 0));
        }
        return;
    case RenderStateId::kFog:
        if (value != gfx.fog_active()) {
            gfx.set_fog_active(value);
            render::set_render_state(GpuRenderState::kFogEnable, static_cast<uint32_t>(value != 0));
        }
        return;
    case RenderStateId::kDepthBias:
        if (value != renderer().depth_bias()) {
            gfx.set_depth_bias(value);
            // (unsigned, scaled; x87 at double precision, stored as a float)
            auto bias = static_cast<float>(static_cast<double>(value) * kDepthBiasUnit);
            render::set_render_state(GpuRenderState::kDepthBias, std::bit_cast<uint32_t>(bias));
        }
        return;
    case RenderStateId::kLighting:
        if (value != get_lighting()) {
            get_lighting() = value;
            render::set_render_state(GpuRenderState::kLighting, static_cast<uint32_t>(value != 0));
        }
        return;
    case RenderStateId::kFilter0:
    case RenderStateId::kFilter1:
    case RenderStateId::kFilter2:
    case RenderStateId::kFilter3: {
        change_filter(state, value);
        return;
    }
    case RenderStateId::kLocalViewer:
        if (value != local_viewer()) {
            local_viewer() = static_cast<uint8_t>(value != 0);
            render::set_render_state(GpuRenderState::kLocalViewer, value);
        }
        return;
    }
    // (0xC-0xF, 0x17-0x1F: nothing)
}

// 0x004D9DB0
void set_vertex_format(uint32_t fvf)
{
    if (renderer().vertex_format_locked() == 0 && vertex_format() != fvf) {
        gpu().set_vertex_format(fvf);
        vertex_format() = fvf;
    }
}

// 0x004DA730
void set_texture(int stage, GpuTexture* texture)
{
    auto& gfx = renderer();
    if (gfx.stage_texture(stage) != texture) {
        gpu().set_texture(static_cast<uint32_t>(stage), texture);
        gfx.set_stage_texture(stage, texture);
    }
}

// 0x004DA760
void choose_blend_mode(uint32_t mode)
{
    const uint32_t wanted = (forced_blend() != 0U) ? forced_blend() : mode;
    if (wanted == get_blend_mode()) {
        return;
    }
    const uint32_t kind = wanted & 0xFFFFFF;
    if (kind != (get_blend_mode() & kBlendModeMask)) {
        // Fog goes black under the additive and subtractive modes (1-4).
        if (renderer().fog_active() != 0U) {
            if (kind >= 1 && kind <= 4) {
                render::set_render_state(GpuRenderState::kFogColour, 0);
            } else {
                set_device_render_state(GpuRenderState::kFogColour, engine_parts().fog_colour());
            }
        }
        uint32_t op = 0;
        uint32_t src = 0;
        uint32_t dest = 0;
        switch (kind) {
        case 0:
        case kBlendModeVertexTexAlpha:
            op = gpu_value::kBlendOpAdd;
            src = gpu_value::kBlendOne;
            dest = gpu_value::kBlendZero;
            break;
        case 1:
            op = gpu_value::kBlendOpAdd;
            src = gpu_value::kBlendSrcAlpha;
            dest = gpu_value::kBlendOne;
            break;
        case 2:
            op = gpu_value::kBlendOpAdd;
            src = gpu_value::kBlendFactor;
            dest = gpu_value::kBlendOne;
            break;
        case 3:
            op = gpu_value::kBlendOpRevSubtract;
            src = gpu_value::kBlendSrcAlpha;
            dest = gpu_value::kBlendOne;
            break;
        case 4:
            op = gpu_value::kBlendOpRevSubtract;
            src = gpu_value::kBlendFactor;
            dest = gpu_value::kBlendOne;
            break;
        case kBlendModeAlpha:
        case kBlendModeLerpPrevious:
            op = gpu_value::kBlendOpAdd;
            src = gpu_value::kBlendSrcAlpha;
            dest = gpu_value::kBlendInvSrcAlpha;
            break;
        case kBlendModeFixBlend:
            op = gpu_value::kBlendOpAdd;
            src = gpu_value::kBlendFactor;
            dest = gpu_value::kBlendInvFactor;
            break;
        case kBlendModeModulate:
        case kBlendModeFixModulate:
            op = gpu_value::kBlendOpAdd;
            src = gpu_value::kBlendZero;
            dest = gpu_value::kBlendSrcAlpha;
            break;
        case kBlendModeBrighten:
            op = gpu_value::kBlendOpAdd;
            src = gpu_value::kBlendDestColour;
            dest = gpu_value::kBlendOne;
            break;
        case kBlendModeFixBrighten:
            op = gpu_value::kBlendOpAdd;
            src = gpu_value::kBlendDestColour;
            dest = gpu_value::kBlendFactor;
            break;
        case kBlendModeLerpFromPrevious:
            op = gpu_value::kBlendOpAdd;
            src = gpu_value::kBlendInvSrcAlpha;
            dest = gpu_value::kBlendSrcAlpha;
            break;
        case kBlendModeMultiplyColour:
            op = gpu_value::kBlendOpAdd;
            src = gpu_value::kBlendZero;
            dest = gpu_value::kBlendSrcColour;
            break;
        case kBlendModePrimitive:
            op = gpu_value::kBlendOpAdd;
            src = gpu_value::kBlendOne;
            dest = gpu_value::kBlendInvSrcAlpha;
            break;
        default: // (0xE, 0x10 and anything bigger: the argument itself - not the override - for all three)
            op = src = dest = mode;
            break;
        }
        // Without blend factors: the nearest the colour and source alpha can do.
        if (!renderer().blend_factor_supported()) {
            if (src == gpu_value::kBlendFactor && dest == gpu_value::kBlendInvFactor) {
                src = gpu_value::kBlendOne;
                dest = gpu_value::kBlendInvSrcAlpha;
            } else {
                if (src == gpu_value::kBlendFactor) {
                    src = gpu_value::kBlendZero;
                }
                if (dest == gpu_value::kBlendFactor) {
                    dest = gpu_value::kBlendSrcAlpha;
                } else if (dest == gpu_value::kBlendInvFactor) {
                    dest = gpu_value::kBlendInvSrcAlpha;
                }
            }
        }
        if (op != blend_op()) {
            set_device_render_state(GpuRenderState::kBlendOp, op);
            blend_op() = op;
        }
        if (src != src_blend()) {
            set_device_render_state(GpuRenderState::kSrcBlend, src);
            src_blend() = src;
        }
        if (dest != dest_blend()) {
            set_device_render_state(GpuRenderState::kDestBlend, dest);
            dest_blend() = dest;
        }
    }
    // The blend factor: the mode's top byte doubled (0x80 and up: full), in all four channels.
    uint32_t alpha = wanted & kAlphaMask;
    if (alpha != (get_blend_mode() & kAlphaMask)) {
        alpha = alpha >= kAlphaHighBit ? kAlphaMask : alpha << 1;
        set_device_render_state(GpuRenderState::kBlendFactor, (((alpha >> kByte1Shift | alpha) >> kByte1Shift | alpha) >> kByte1Shift) | alpha);
    }
    get_blend_mode() = wanted;
}

// 0x004D9DE0
void set_fixed_function_stages(void* shader, uint32_t factor)
{
    // The last stage-0 state each one sets, then stage 1 off.
    GpuStageState last_type = GpuStageState::kColourOp; // (every branch sets it)
    uint32_t last_value = 0;
    if (shader == get_shader(kPixelShader964)) {
        set_tss(0, GpuStageState::kColourOp, gpu_value::kOpModulate4x);
        set_tss(0, GpuStageState::kColourArg1, gpu_value::kArgTexture);
        set_tss(0, GpuStageState::kColourArg2, gpu_value::kArgDiffuse);
        set_tss(0, GpuStageState::kAlphaOp, gpu_value::kOpModulate2x);
        set_tss(0, GpuStageState::kAlphaArg1, gpu_value::kArgTexture);
        set_tss(0, GpuStageState::kAlphaArg2, gpu_value::kArgDiffuse);
        set_tss(1, GpuStageState::kColourOp, gpu_value::kOpDisable);
        return;
    }
    if (shader == get_shader(kPixelShader95c)) { // vertex colour x2 x the factor, vertex alpha
        set_device_render_state(GpuRenderState::kTextureFactor, factor);
        set_tss(0, GpuStageState::kColourOp, gpu_value::kOpModulate2x);
        set_tss(0, GpuStageState::kColourArg1, gpu_value::kArgDiffuse);
        set_tss(0, GpuStageState::kColourArg2, gpu_value::kArgTextureFactor);
        set_tss(0, GpuStageState::kAlphaOp, gpu_value::kOpSelectArg1);
        last_type = GpuStageState::kAlphaArg1;
        last_value = gpu_value::kArgDiffuse;
    } else if (shader != get_shader(kPixelShader958) && shader == get_shader(kPixelShader954)) { // the factor alone
        set_device_render_state(GpuRenderState::kTextureFactor, factor);
        set_tss(0, GpuStageState::kColourOp, gpu_value::kOpSelectArg1);
        set_tss(0, GpuStageState::kColourArg1, gpu_value::kArgTextureFactor);
        set_tss(0, GpuStageState::kAlphaOp, gpu_value::kOpSelectArg1);
        last_type = GpuStageState::kAlphaArg1;
        last_value = gpu_value::kArgTextureFactor;
    } else if (shader != get_shader(kPixelShader958) &&
               shader == get_shader(kPixelShader950)) { // texture x vertex colour x2, texture alpha
        set_tss(0, GpuStageState::kColourOp, gpu_value::kOpModulate2x);
        set_tss(0, GpuStageState::kColourArg1, gpu_value::kArgTexture);
        set_tss(0, GpuStageState::kColourArg2, gpu_value::kArgDiffuse);
        set_tss(0, GpuStageState::kAlphaOp, gpu_value::kOpSelectArg1);
        set_tss(0, GpuStageState::kAlphaArg1, gpu_value::kArgTexture);
        last_type = GpuStageState::kAlphaArg2;
        last_value = gpu_value::kArgDiffuse;
    } else if (shader != get_shader(kPixelShader958) && shader == get_shader(kPixelShader94c)) { // vertex colour alone
        set_tss(0, GpuStageState::kColourOp, gpu_value::kOpSelectArg1);
        set_tss(0, GpuStageState::kColourArg1, gpu_value::kArgDiffuse);
        set_tss(0, GpuStageState::kAlphaOp, gpu_value::kOpSelectArg1);
        last_type = GpuStageState::kAlphaArg1;
        last_value = gpu_value::kArgDiffuse;
    } else if (shader != get_shader(kPixelShader958) &&
               shader == get_shader(kPixelShader944)) { // texture alpha x vertex alpha x2
        set_tss(0, GpuStageState::kColourOp, gpu_value::kOpModulate2x);
        set_tss(0, GpuStageState::kColourArg1, gpu_value::kArgTexture | gpu_value::kArgAlphaReplicate);
        set_tss(0, GpuStageState::kColourArg2, gpu_value::kArgDiffuse | gpu_value::kArgAlphaReplicate);
        set_tss(0, GpuStageState::kAlphaOp, gpu_value::kOpModulate2x);
        set_tss(0, GpuStageState::kAlphaArg1, gpu_value::kArgTexture);
        last_type = GpuStageState::kAlphaArg2;
        last_value = gpu_value::kArgDiffuse;
    } else { // kPixelShader958 and any other: the texture alone
        set_tss(0, GpuStageState::kColourOp, gpu_value::kOpSelectArg1);
        set_tss(0, GpuStageState::kColourArg1, gpu_value::kArgTexture);
        set_tss(0, GpuStageState::kAlphaOp, gpu_value::kOpSelectArg1);
        last_type = GpuStageState::kAlphaArg1;
        last_value = gpu_value::kArgTexture;
    }
    set_tss(0, last_type, last_value);
    set_tss(1, GpuStageState::kColourOp, gpu_value::kOpDisable);
}

// 0x004DAA10
void set_fixed_function_stages_for_material(void* shader, const MaterialRef* material)
{
    if (!material) {
        set_fixed_function_stages(shader, kWhiteColour);
        return;
    }
    const std::array<float, 4>& color = core::at(material->pass_color(), 0); // (the first pass's colour)
    const uint32_t red = static_cast<uint8_t>(ftol(static_cast<double>(std::get<0>(color)) * k255));
    const uint32_t green = static_cast<uint8_t>(ftol(static_cast<double>(std::get<1>(color)) * k255));
    const uint32_t blue = static_cast<uint8_t>(ftol(static_cast<double>(std::get<2>(color)) * k255));
    set_fixed_function_stages(shader, (((red | kBytePadBits) << kByte1Shift | green) << kByte1Shift) | blue);
}

// 0x004DAA80
void set_pixel_shader(void* shader, const MaterialRef* material)
{
    auto& gfx = renderer();
    if (!gfx.shaders_available()) {
        set_fixed_function_stages_for_material(shader, material);
        return;
    }
    if (renderer().pixel_shader_locked() == nullptr) {
        if (renderer().current_pixel_shader() != shader) {
            // Not original - the level editor's Unlit view (render_pc/view_mode.h): the shader's unlit twin during the 3D
            // passes. Original behaviour: the device gets `shader`.
            gpu().set_pixel_shader(static_cast<GpuPixelShader*>(view_mode::device_shader(shader)));
            gfx.set_current_pixel_shader(shader);
            gfx.set_pixel_shader_changed(1);
        }
        gpu().set_pixel_constants(0, renderer().ps_constants().data(), kPsConstantVectors);
    }
    gfx.set_pixel_shader_changed(0);
}

// 0x004DAAF0
void set_pixel_shader_with_constants(void* shader, uint32_t count)
{
    auto& gfx = renderer();
    if (renderer().pixel_shader_locked() == nullptr) {
        if (renderer().current_pixel_shader() != shader) {
            // Not original - the Unlit view's twin (view_mode.h), as in SetPixelShader.
            gpu().set_pixel_shader(static_cast<GpuPixelShader*>(view_mode::device_shader(shader)));
            gfx.set_current_pixel_shader(shader);
            gfx.set_pixel_shader_changed(1);
        }
        gpu().set_pixel_constants(0, renderer().ps_constants().data(), count);
        gpu().set_pixel_constants(4, std::span(renderer().ps_constants()).subspan(kPsConstantTailOffset).data(), 1);
    }
    gfx.set_pixel_shader_changed(0);
}

// 0x004DA0E0

// 0x004DB620
void set_world_transform(const float* matrix) { renderer().set_world_transform(matrix); }

// 0x004D7D70
void reset_render_state()
{
    auto& gfx = renderer();
    // NOT ORIGINAL - the Gpu's state block (engine/gfx/gpu.h) to a new device's defaults, as the device is.
    gpu().reset_state_block();
    local_viewer() = 0;
    // The world, view and projection transforms (0x00786B90, 0x00786BD0, 0x00786C10): identity.
    gfx.reset_transforms();
    get_lighting() = 0;
    gfx.set_specular(0);
    gfx.set_cull_mode(gpu_value::kCullClockwise);
    gfx.set_near_camera(true);
    dither() = 1;
    get_z_test() = 1;
    gfx.set_z_write(1);
    alpha_blend() = 1;
    alpha_test() = 1;
    alpha_test_level() = 0;
    for (int i = 0; i < 4; ++i) {
        core::at(address_modes(), i) = 0;
        core::at(mip_lod_biases(), i) = 0;
        gfx.set_stage_texture(i, nullptr);
        core::at(filters(), i) = kDefaultFilters;
        gfx.set_border_colour(static_cast<uint32_t>(i), 0);
    }
    // (none of these is a mode: the next one is always sent)
    get_blend_mode() = kUnsetState;
    blend_op() = kUnsetState;
    src_blend() = kUnsetState;
    dest_blend() = kUnsetState;
    core::at(effects_state().pending_screen_shot(), 0) = 0;
    effects_state().set_screen_shot_name_exact(false);
    // Pixel shader constant 4: (0, 0.5, 0, 0.5).
    gfx.set_ps_constant(kPsConstant4X, 0.0F);
    gfx.set_ps_constant(kPsConstant4Y, kPsGrey);
    gfx.set_ps_constant(kPsConstant4Z, 0.0F);
    gfx.set_ps_constant(kPsConstant4W, kPsGrey);
    gfx.set_pixel_shader_locked(nullptr);
    gfx.set_current_pixel_shader(nullptr);
    gfx.set_pixel_shader_changed(0);
    gfx.set_custom_transforms(0);
    view_setup().set_billboard_flag(0);
    gfx.set_vertex_format_locked(0);
    gfx.set_skipped_passes(0);
    gfx.set_submit_locked(0);
    forced_blend() = 0;
    gfx.set_clear_colour_and_depth(true);
    gfx.set_background_colour(0);
    gfx.set_letterbox_active(0); // the letterbox
    // The default light: its direction (0x00786F48) and colours, as bit patterns.
    static const auto kLight = std::to_array<uint32_t>({
        0xBF000000, 0xBF5DB3D7, 0, 0, 0x3F160419, 0x3F160419, 0x3F160419, 0x3F800000, 0x3F800000, 0, 0, 0, 0, 0, 0,
        0x3F800000, 0x3F800000, 0, 0, 0,          0,          0,          0,          0x3F800000,
    });
    for (int i = 0; i < kLightConstantCount; ++i) {
        core::at(light_sets().light_constants(), 4 + i) = std::bit_cast<float>(core::at(kLight, i));
    }
    constexpr auto kAmbient = std::bit_cast<float>(0x3F1624DDU); // the ambient colour (0.586)
    core::at(light_sets().light_constants(), 0) = kAmbient;
    core::at(light_sets().light_constants(), 1) = kAmbient;
    core::at(light_sets().light_constants(), 2) = kAmbient;
    core::at(light_sets().light_constants(), 3) = 1.0F;
    gfx.set_fog_active(0);
    engine_parts().set_fog_colour(0);
    renderer().set_fog_begin_distance(-kFogBeginDefault); // fog start and end (the original stored their bit patterns)
    renderer().set_fog_end(-kFogEndDefault);
}

// 0x004D80D0
void apply_render_state()
{
    auto& gfx = renderer();
    Gpu& device = gpu();
    device.set_render_state(GpuRenderState::kLocalViewer, local_viewer());
    device.set_render_state(GpuRenderState::kColourVertex, 0U);
    device.set_render_state(GpuRenderState::kLighting, get_lighting());
    device.set_render_state(GpuRenderState::kSpecularEnable, gfx.specular_state());
    device.set_render_state(GpuRenderState::kCullMode, renderer().cull_mode());
    device.set_render_state(GpuRenderState::kDitherEnable, dither());
    device.set_render_state(GpuRenderState::kZEnable, get_z_test());
    device.set_render_state(GpuRenderState::kZWriteEnable, gfx.z_write());
    device.set_render_state(GpuRenderState::kAlphaBlendEnable, alpha_blend());
    device.set_render_state(GpuRenderState::kAlphaTestEnable, alpha_test());
    device.set_render_state(GpuRenderState::kAlphaFunc, gpu_value::kCompareGreaterEqual);
    device.set_render_state(GpuRenderState::kAlphaRef, alpha_test_level());
    for (int sampler = 0; sampler < 4; ++sampler) {
        core::at(address_modes(), sampler) = 0;
        core::at(mip_lod_biases(), sampler) = 0;
        gfx.set_stage_texture(sampler, nullptr);
        core::at(filters(), sampler) = kDefaultFilters;
        gfx.set_border_colour(static_cast<uint32_t>(sampler), 0);
        device.set_sampler_state(sampler, GpuSamplerState::kMagFilter, gpu_value::kFilterLinear);
        device.set_sampler_state(sampler, GpuSamplerState::kMipFilter, gpu_value::kFilterLinear);
        // NOT ORIGINAL - [Graphics] anisotropy (engine/gfx/graphics_options.h): anisotropic minification on every
        // sampler at the chosen taps. (Off: the original's below.)
        if (const int taps = graphics_options::active_anisotropy()) {
            device.set_sampler_state(sampler, GpuSamplerState::kMinFilter, gpu_value::kFilterAnisotropic);
            device.set_sampler_state(sampler, GpuSamplerState::kMaxAnisotropy, static_cast<uint32_t>(taps));
        } else if (sampler == 0 && renderer().back_buffer_height() <= kLowResHeight) {
            // Anisotropic minification on the first sampler, at back buffers up to 480 lines high.
            device.set_sampler_state(0, GpuSamplerState::kMinFilter, gpu_value::kFilterAnisotropic);
            const uint32_t anisotropy = renderer().max_anisotropy();
            device.set_sampler_state(0, GpuSamplerState::kMaxAnisotropy, anisotropy < 3 ? anisotropy : 3);
        } else {
            device.set_sampler_state(sampler, GpuSamplerState::kMinFilter, gpu_value::kFilterLinear);
        }
        device.set_sampler_state(sampler, GpuSamplerState::kAddressU, gpu_value::kAddressWrap);
        device.set_sampler_state(sampler, GpuSamplerState::kAddressV, gpu_value::kAddressWrap);
    }
    const GpuMaterial material = {};
    device.set_material(material);
    device.set_render_state(GpuRenderState::kDiffuseMaterialSource, gpu_value::kSourceColour1);
    device.set_render_state(GpuRenderState::kSpecularMaterialSource, gpu_value::kSourceColour2);
    device.set_render_state(GpuRenderState::kAmbientMaterialSource, gpu_value::kSourceColour1);
    device.set_render_state(GpuRenderState::kEmissiveMaterialSource, gpu_value::kSourceMaterial);
    mark_billboards_ready();
    compile_pixel_shaders();
    device.set_render_state(GpuRenderState::kFogEnable, gfx.fog_active());
    device.set_render_state(GpuRenderState::kFogTableMode, gpu_value::kFogNone);
    device.set_render_state(GpuRenderState::kRangeFogEnable, 1U);
    device.set_render_state(GpuRenderState::kFogVertexMode, gpu_value::kFogLinear);
    device.set_render_state(GpuRenderState::kFogColour, engine_parts().fog_colour());
    device.set_render_state(GpuRenderState::kFogStart, std::bit_cast<uint32_t>(renderer().fog_begin_distance()));
    device.set_render_state(GpuRenderState::kFogEnd, std::bit_cast<uint32_t>(renderer().fog_end()));
    // (NOT ORIGINAL: or [Graphics] msaa's samples - engine/gfx/graphics_options.h)
    if (renderer().antialiasing() || (graphics_options::chosen_samples() != 0)) { // the antialiasing setting
        device.set_render_state(GpuRenderState::kMultisampleAntialias, 1U);
    }
}

} // namespace render
