// The renderer's cached Direct3D 9 state: render states, sampler states, textures, blend modes, pixel shaders.

#include "engine/gfx/render_state.h"
#include "engine/gfx/surface_material.h"

#include <cstddef>
#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstdint>
#include <array>
#include <span>

#include "engine/gfx/device.h"
#include "engine/gfx/graphics_options_d3d.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/gfx/pixel_shaders.h"
#include "engine/gfx/view_mode.h"
#include "engine/gfx/shader_slots.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/d3d9_state.h"
#include "gpu/d3d9_handles.h"
#include "gpu/gpu_handles.h"
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

void set_tss(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
    d3d9::state().d3d_device()->SetTextureStageState(stage, type, value);
}
void set_sampler(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value)
{
    d3d9::state().d3d_device()->SetSamplerState(sampler, type, value);
}
void set_device_render_state(D3DRENDERSTATETYPE state, DWORD value) { d3d9::state().d3d_device()->SetRenderState(state, value); }

// D3DTADDRESS for the renderer's addressing modes (border needs the device's support; else clamp).
bool address_value(uint32_t mode, DWORD* value)
{
    switch (mode) {
    case 0:
        *value = D3DTADDRESS_WRAP;
        return true;
    case 1:
        *value = D3DTADDRESS_CLAMP;
        return true;
    case 2:
        *value = renderer().border_addressing_supported() ? D3DTADDRESS_BORDER : D3DTADDRESS_CLAMP;
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
        DWORD address = 0;
        if (address_value(u, &address)) {
            set_sampler(sampler, D3DSAMP_ADDRESSU, address);
        }
        cached = (cached & kHighHalfMask) | u;
    }
    const uint32_t v = value & 0xFFFF0000;
    if (v == (cached & kHighHalfMask)) {
        return;
    }
    DWORD address = 0;
    if (address_value(v >> kHalfBits, &address)) {
        set_sampler(sampler, D3DSAMP_ADDRESSV, address);
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
        set_sampler(sampler, D3DSAMP_MAGFILTER, ((value & kMagLinearBit) != 0U) ? D3DTEXF_LINEAR : D3DTEXF_POINT);
        cached = (cached & kLowHalfMask) | mag;
    }
    const uint32_t min = value & 0xFFFF;
    if (min != (cached & kLowHalfMask)) {
        // NOT ORIGINAL - [Graphics] anisotropy (engine/gfx/graphics_options.h): anisotropic in place of linear.
        // Original behaviour: (value & 1) ? D3DTEXF_LINEAR : D3DTEXF_POINT
        set_sampler(sampler, D3DSAMP_MINFILTER,
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
                render::set_render_state(D3DRS_ZWRITEENABLE, TRUE);
                gfx.set_z_write(1);
            }
        } else if (gfx.z_write() == 1) {
            render::set_render_state(D3DRS_ZWRITEENABLE, FALSE);
            gfx.set_z_write(0);
        }
        return;
    case RenderStateId::kZTest:
        if (value != 0U) {
            if (get_z_test() == 0U) {
                render::set_render_state(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
                get_z_test() = 1;
            }
        } else if (get_z_test() == 1) {
            render::set_render_state(D3DRS_ZFUNC, D3DCMP_ALWAYS);
            get_z_test() = 0;
        }
        return;
    case RenderStateId::kPhaseAlphaTest:
        if (value == alpha_test_level()) {
            return;
        }
        alpha_test_level() = value;
        if (value != 0U) {
            render::set_render_state(D3DRS_ALPHAREF, value);
            if (alpha_test() == 0U) {
                render::set_render_state(D3DRS_ALPHATESTENABLE, TRUE);
                alpha_test() = 1;
            }
        } else if (alpha_test() != 0U) {
            render::set_render_state(D3DRS_ALPHATESTENABLE, FALSE);
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
            set_sampler(sampler, D3DSAMP_MIPMAPLODBIAS, value);
            core::at(mip_lod_biases(), sampler) = value;
        }
        return;
    }
    case RenderStateId::kCullMode:
        if (value != renderer().cull_mode()) {
            gfx.set_cull_mode(value);
            render::set_render_state(D3DRS_CULLMODE, value);
        }
        return;
    case RenderStateId::kAlphaBlend:
        if (value != alpha_blend()) {
            alpha_blend() = value;
            render::set_render_state(D3DRS_ALPHABLENDENABLE, static_cast<uint32_t>(value != 0));
        }
        return;
    case RenderStateId::kAlphaTest:
        if (value != alpha_test()) {
            alpha_test() = value;
            render::set_render_state(D3DRS_ALPHATESTENABLE, static_cast<uint32_t>(value != 0));
        }
        return;
    case RenderStateId::kSpecular:
        if (value != gfx.specular_state()) {
            gfx.set_specular(value);
            render::set_render_state(D3DRS_SPECULARENABLE, static_cast<uint32_t>(value != 0));
            render::set_render_state(D3DRS_LOCALVIEWER, static_cast<uint32_t>(value != 0));
        }
        return;
    case RenderStateId::kFog:
        if (value != gfx.fog_active()) {
            gfx.set_fog_active(value);
            render::set_render_state(D3DRS_FOGENABLE, static_cast<uint32_t>(value != 0));
        }
        return;
    case RenderStateId::kDepthBias:
        if (value != renderer().depth_bias()) {
            gfx.set_depth_bias(value);
            // (unsigned, scaled; x87 at double precision, stored as a float)
            auto bias = static_cast<float>(static_cast<double>(value) * kDepthBiasUnit);
            render::set_render_state(D3DRS_DEPTHBIAS, std::bit_cast<uint32_t>(bias));
        }
        return;
    case RenderStateId::kLighting:
        if (value != get_lighting()) {
            get_lighting() = value;
            render::set_render_state(D3DRS_LIGHTING, static_cast<uint32_t>(value != 0));
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
            render::set_render_state(D3DRS_LOCALVIEWER, value);
        }
        return;
    }
    // (0xC-0xF, 0x17-0x1F: nothing)
}

// 0x004D9DB0
void set_vertex_format(uint32_t fvf)
{
    if (renderer().vertex_format_locked() == 0 && vertex_format() != fvf) {
        d3d9::state().d3d_device()->SetFVF(fvf);
        vertex_format() = fvf;
    }
}

// 0x004DA730
void set_texture(int stage, GpuTexture* texture)
{
    auto& gfx = renderer();
    if (gfx.stage_texture(stage) != texture) {
        d3d9::state().d3d_device()->SetTexture(stage, d3d9::object(texture));
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
                render::set_render_state(D3DRS_FOGCOLOR, 0);
            } else {
                set_device_render_state(D3DRS_FOGCOLOR, engine_parts().fog_colour());
            }
        }
        uint32_t op = 0;
        uint32_t src = 0;
        uint32_t dest = 0;
        switch (kind) {
        case 0:
        case kBlendModeVertexTexAlpha:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_ONE;
            dest = D3DBLEND_ZERO;
            break;
        case 1:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_SRCALPHA;
            dest = D3DBLEND_ONE;
            break;
        case 2:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_BLENDFACTOR;
            dest = D3DBLEND_ONE;
            break;
        case 3:
            op = D3DBLENDOP_REVSUBTRACT;
            src = D3DBLEND_SRCALPHA;
            dest = D3DBLEND_ONE;
            break;
        case 4:
            op = D3DBLENDOP_REVSUBTRACT;
            src = D3DBLEND_BLENDFACTOR;
            dest = D3DBLEND_ONE;
            break;
        case kBlendModeAlpha:
        case kBlendModeLerpPrevious:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_SRCALPHA;
            dest = D3DBLEND_INVSRCALPHA;
            break;
        case kBlendModeFixBlend:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_BLENDFACTOR;
            dest = D3DBLEND_INVBLENDFACTOR;
            break;
        case kBlendModeModulate:
        case kBlendModeFixModulate:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_ZERO;
            dest = D3DBLEND_SRCALPHA;
            break;
        case kBlendModeBrighten:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_DESTCOLOR;
            dest = D3DBLEND_ONE;
            break;
        case kBlendModeFixBrighten:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_DESTCOLOR;
            dest = D3DBLEND_BLENDFACTOR;
            break;
        case kBlendModeLerpFromPrevious:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_INVSRCALPHA;
            dest = D3DBLEND_SRCALPHA;
            break;
        case kBlendModeMultiplyColour:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_ZERO;
            dest = D3DBLEND_SRCCOLOR;
            break;
        case kBlendModePrimitive:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_ONE;
            dest = D3DBLEND_INVSRCALPHA;
            break;
        default: // (0xE, 0x10 and anything bigger: the argument itself - not the override - for all three)
            op = src = dest = mode;
            break;
        }
        // Without blend factors: the nearest the colour and source alpha can do.
        if (!renderer().blend_factor_supported()) {
            if (src == D3DBLEND_BLENDFACTOR && dest == D3DBLEND_INVBLENDFACTOR) {
                src = D3DBLEND_ONE;
                dest = D3DBLEND_INVSRCALPHA;
            } else {
                if (src == D3DBLEND_BLENDFACTOR) {
                    src = D3DBLEND_ZERO;
                }
                if (dest == D3DBLEND_BLENDFACTOR) {
                    dest = D3DBLEND_SRCALPHA;
                } else if (dest == D3DBLEND_INVBLENDFACTOR) {
                    dest = D3DBLEND_INVSRCALPHA;
                }
            }
        }
        if (op != blend_op()) {
            set_device_render_state(D3DRS_BLENDOP, op);
            blend_op() = op;
        }
        if (src != src_blend()) {
            set_device_render_state(D3DRS_SRCBLEND, src);
            src_blend() = src;
        }
        if (dest != dest_blend()) {
            set_device_render_state(D3DRS_DESTBLEND, dest);
            dest_blend() = dest;
        }
    }
    // The blend factor: the mode's top byte doubled (0x80 and up: full), in all four channels.
    uint32_t alpha = wanted & kAlphaMask;
    if (alpha != (get_blend_mode() & kAlphaMask)) {
        alpha = alpha >= kAlphaHighBit ? kAlphaMask : alpha << 1;
        set_device_render_state(D3DRS_BLENDFACTOR, (((alpha >> kByte1Shift | alpha) >> kByte1Shift | alpha) >> kByte1Shift) | alpha);
    }
    get_blend_mode() = wanted;
}

// 0x004D9DE0
void set_fixed_function_stages(void* shader, uint32_t factor)
{
    // The last stage-0 state each one sets, then stage 1 off.
    D3DTEXTURESTAGESTATETYPE last_type = D3DTSS_COLOROP; // (every branch sets it)
    DWORD last_value = 0;
    if (shader == get_shader(kPixelShader964)) {
        set_tss(0, D3DTSS_COLOROP, D3DTOP_MODULATE4X);
        set_tss(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        set_tss(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        set_tss(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE2X);
        set_tss(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        set_tss(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        set_tss(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        return;
    }
    if (shader == get_shader(kPixelShader95c)) { // vertex colour x2 x the factor, vertex alpha
        set_device_render_state(D3DRS_TEXTUREFACTOR, factor);
        set_tss(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        set_tss(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        set_tss(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
        set_tss(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        last_type = D3DTSS_ALPHAARG1;
        last_value = D3DTA_DIFFUSE;
    } else if (shader != get_shader(kPixelShader958) && shader == get_shader(kPixelShader954)) { // the factor alone
        set_device_render_state(D3DRS_TEXTUREFACTOR, factor);
        set_tss(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        set_tss(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
        set_tss(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        last_type = D3DTSS_ALPHAARG1;
        last_value = D3DTA_TFACTOR;
    } else if (shader != get_shader(kPixelShader958) &&
               shader == get_shader(kPixelShader950)) { // texture x vertex colour x2, texture alpha
        set_tss(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        set_tss(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        set_tss(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        set_tss(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        set_tss(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        last_type = D3DTSS_ALPHAARG2;
        last_value = D3DTA_DIFFUSE;
    } else if (shader != get_shader(kPixelShader958) && shader == get_shader(kPixelShader94c)) { // vertex colour alone
        set_tss(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        set_tss(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        set_tss(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        last_type = D3DTSS_ALPHAARG1;
        last_value = D3DTA_DIFFUSE;
    } else if (shader != get_shader(kPixelShader958) &&
               shader == get_shader(kPixelShader944)) { // texture alpha x vertex alpha x2
        set_tss(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        set_tss(0, D3DTSS_COLORARG1, D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE);
        set_tss(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE | D3DTA_ALPHAREPLICATE);
        set_tss(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE2X);
        set_tss(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        last_type = D3DTSS_ALPHAARG2;
        last_value = D3DTA_DIFFUSE;
    } else { // kPixelShader958 and any other: the texture alone
        set_tss(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        set_tss(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        set_tss(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        last_type = D3DTSS_ALPHAARG1;
        last_value = D3DTA_TEXTURE;
    }
    set_tss(0, last_type, last_value);
    set_tss(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
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
            // Not original - the level editor's Unlit view (engine/gfx/view_mode.h): the shader's unlit twin during the 3D
            // passes. Original behaviour: the device gets `shader`.
            d3d9::state().d3d_device()->SetPixelShader(static_cast<IDirect3DPixelShader9*>(view_mode::device_shader(shader)));
            gfx.set_current_pixel_shader(shader);
            gfx.set_pixel_shader_changed(1);
        }
        d3d9::state().d3d_device()->SetPixelShaderConstantF(0, renderer().ps_constants().data(), kPsConstantVectors);
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
            d3d9::state().d3d_device()->SetPixelShader(static_cast<IDirect3DPixelShader9*>(view_mode::device_shader(shader)));
            gfx.set_current_pixel_shader(shader);
            gfx.set_pixel_shader_changed(1);
        }
        d3d9::state().d3d_device()->SetPixelShaderConstantF(0, renderer().ps_constants().data(), count);
        d3d9::state().d3d_device()->SetPixelShaderConstantF(4, std::span(renderer().ps_constants()).subspan(kPsConstantTailOffset).data(), 1);
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
    local_viewer() = 0;
    // The world, view and projection transforms (0x00786B90, 0x00786BD0, 0x00786C10): identity.
    gfx.reset_transforms();
    get_lighting() = 0;
    gfx.set_specular(0);
    gfx.set_cull_mode(D3DCULL_CW);
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
    IDirect3DDevice9* device = d3d9::state().d3d_device();
    device->SetRenderState(D3DRS_LOCALVIEWER, local_viewer());
    d3d9::state().d3d_device()->SetRenderState(D3DRS_COLORVERTEX, FALSE);
    d3d9::state().d3d_device()->SetRenderState(D3DRS_LIGHTING, get_lighting());
    d3d9::state().d3d_device()->SetRenderState(D3DRS_SPECULARENABLE, gfx.specular_state());
    d3d9::state().d3d_device()->SetRenderState(D3DRS_CULLMODE, renderer().cull_mode());
    d3d9::state().d3d_device()->SetRenderState(D3DRS_DITHERENABLE, dither());
    d3d9::state().d3d_device()->SetRenderState(D3DRS_ZENABLE, get_z_test());
    d3d9::state().d3d_device()->SetRenderState(D3DRS_ZWRITEENABLE, gfx.z_write());
    d3d9::state().d3d_device()->SetRenderState(D3DRS_ALPHABLENDENABLE, alpha_blend());
    d3d9::state().d3d_device()->SetRenderState(D3DRS_ALPHATESTENABLE, alpha_test());
    d3d9::state().d3d_device()->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    d3d9::state().d3d_device()->SetRenderState(D3DRS_ALPHAREF, alpha_test_level());
    for (int sampler = 0; sampler < 4; ++sampler) {
        core::at(address_modes(), sampler) = 0;
        core::at(mip_lod_biases(), sampler) = 0;
        gfx.set_stage_texture(sampler, nullptr);
        core::at(filters(), sampler) = kDefaultFilters;
        gfx.set_border_colour(static_cast<uint32_t>(sampler), 0);
        d3d9::state().d3d_device()->SetSamplerState(sampler, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        d3d9::state().d3d_device()->SetSamplerState(sampler, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        // NOT ORIGINAL - [Graphics] anisotropy (engine/gfx/graphics_options.h): anisotropic minification on every
        // sampler at the chosen taps. (Off: the original's below.)
        if (const int taps = graphics_options::active_anisotropy()) {
            d3d9::state().d3d_device()->SetSamplerState(sampler, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
            d3d9::state().d3d_device()->SetSamplerState(sampler, D3DSAMP_MAXANISOTROPY, static_cast<DWORD>(taps));
        } else if (sampler == 0 && renderer().back_buffer_height() <= kLowResHeight) {
            // Anisotropic minification on the first sampler, at back buffers up to 480 lines high.
            d3d9::state().d3d_device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
            const uint32_t anisotropy = renderer().max_anisotropy();
            d3d9::state().d3d_device()->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, anisotropy < 3 ? anisotropy : 3);
        } else {
            d3d9::state().d3d_device()->SetSamplerState(sampler, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        }
        d3d9::state().d3d_device()->SetSamplerState(sampler, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        d3d9::state().d3d_device()->SetSamplerState(sampler, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    }
    const D3DMATERIAL9 material = {};
    d3d9::state().d3d_device()->SetMaterial(&material);
    d3d9::state().d3d_device()->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    d3d9::state().d3d_device()->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2);
    d3d9::state().d3d_device()->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
    d3d9::state().d3d_device()->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
    mark_billboards_ready();
    compile_pixel_shaders();
    d3d9::state().d3d_device()->SetRenderState(D3DRS_FOGENABLE, gfx.fog_active());
    d3d9::state().d3d_device()->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
    d3d9::state().d3d_device()->SetRenderState(D3DRS_RANGEFOGENABLE, TRUE);
    d3d9::state().d3d_device()->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
    d3d9::state().d3d_device()->SetRenderState(D3DRS_FOGCOLOR, engine_parts().fog_colour());
    d3d9::state().d3d_device()->SetRenderState(D3DRS_FOGSTART, std::bit_cast<uint32_t>(renderer().fog_begin_distance()));
    d3d9::state().d3d_device()->SetRenderState(D3DRS_FOGEND, std::bit_cast<uint32_t>(renderer().fog_end()));
    // (NOT ORIGINAL: or [Graphics] msaa's samples - engine/gfx/graphics_options.h)
    if (renderer().antialiasing() || (graphics_options::chosen_samples() != 0)) { // the antialiasing setting
        d3d9::state().d3d_device()->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
    }
}

} // namespace render
