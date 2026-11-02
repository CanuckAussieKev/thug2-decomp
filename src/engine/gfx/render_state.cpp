// The renderer's cached Direct3D 9 state: render states, sampler states, textures, blend modes, pixel shaders.

#include "engine/gfx/render_state.h"
#include "engine/gfx/surface_material.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstdint>
#include <initializer_list>
#include <array>
#include <span>

#include "engine/gfx/device.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/gfx/pixel_shaders.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/view_mode.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/engine_glue.h"
#include "engine/core/checked_index.h"
#include "engine/maths/mat4.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/shared_state.h"

namespace render {

namespace {

// NOT ORIGINAL grouping - the state the original kept in globals. Made on first use, never destroyed
// (render_state_values()).
struct RenderStateValues {
    bool shaders_available_{};
    uint32_t specular_{};
    uint8_t z_write_{};
    uint32_t depth_bias_{};
    uint32_t cull_mode_{};
    bool near_camera_{};
    std::array<void*, 4> textures_{};
    std::array<uint32_t, 4> border_colours_{};
    const void* pixel_shader_locked_{}; // (a shader set for a whole pass: set_pixel_shader leaves the device's alone)
    void* current_pixel_shader_{};
    std::array<float, 20> ps_constant_bank_{};
    uint32_t instance_colour_on_{};
    std::array<uint8_t, 4> instance_colour_{};
    uint8_t pixel_shader_changed_{};
    uint32_t vertex_format_locked_{};
    uint32_t skipped_passes_{};
    uint32_t submit_locked_{};
    uint32_t fog_active_{};
    const float* world_transform_{};
    std::array<float, 16> world_transform_copy_{};
};

RenderStateValues& render_state_values()
{
    static core::NoDestroy<RenderStateValues> state;
    return state.get();
}

} // namespace

bool shaders_available() { return render_state_values().shaders_available_; }
void set_shaders_available(bool value) { render_state_values().shaders_available_ = value; }
uint32_t specular_state() { return render_state_values().specular_; }
uint8_t z_write() { return render_state_values().z_write_; }
void set_z_write(uint8_t value) { render_state_values().z_write_ = value; }
uint32_t depth_bias() { return render_state_values().depth_bias_; }
void set_depth_bias(uint32_t value) { render_state_values().depth_bias_ = value; }
uint32_t cull_mode() { return render_state_values().cull_mode_; }
void set_cull_mode(uint32_t value) { render_state_values().cull_mode_ = value; }
bool near_camera() { return render_state_values().near_camera_; }
void set_near_camera(bool value) { render_state_values().near_camera_ = value; }
const std::array<void*, 4>& stage_textures() { return render_state_values().textures_; }
std::array<uint32_t, 4>& border_colours() { return render_state_values().border_colours_; }
const void* pixel_shader_locked() { return render_state_values().pixel_shader_locked_; }
void set_pixel_shader_locked(const void* value) { render_state_values().pixel_shader_locked_ = value; }
void* current_pixel_shader() { return render_state_values().current_pixel_shader_; }
void set_current_pixel_shader(void* value) { render_state_values().current_pixel_shader_ = value; }
std::array<float, 20>& ps_constant_bank() { return render_state_values().ps_constant_bank_; }
uint32_t instance_colour_on() { return render_state_values().instance_colour_on_; }
void set_instance_colour_on(uint32_t value) { render_state_values().instance_colour_on_ = value; }
std::array<uint8_t, 4>& instance_colour() { return render_state_values().instance_colour_; }
uint8_t pixel_shader_changed() { return render_state_values().pixel_shader_changed_; }
void set_pixel_shader_changed(uint8_t value) { render_state_values().pixel_shader_changed_ = value; }
uint32_t vertex_format_locked() { return render_state_values().vertex_format_locked_; }
void set_vertex_format_locked(uint32_t value) { render_state_values().vertex_format_locked_ = value; }
uint32_t& skipped_passes() { return render_state_values().skipped_passes_; }
uint32_t submit_locked() { return render_state_values().submit_locked_; }
void set_submit_locked(uint32_t value) { render_state_values().submit_locked_ = value; }
uint32_t fog_active() { return render_state_values().fog_active_; }
void set_fog_active(uint32_t value) { render_state_values().fog_active_ = value; }
const float*& current_world_transform() { return render_state_values().world_transform_; }
std::array<float, 16>& world_transform_copy() { return render_state_values().world_transform_copy_; }

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
    d3d_device()->SetTextureStageState(stage, type, value);
}
void set_sampler(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value)
{
    d3d_device()->SetSamplerState(sampler, type, value);
}
void set_device_render_state(D3DRENDERSTATETYPE state, DWORD value) { d3d_device()->SetRenderState(state, value); }

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
        *value = border_addressing_supported() ? D3DTADDRESS_BORDER : D3DTADDRESS_CLAMP;
        return true;
    default:
        return false; // (nothing sent, but the cache takes the value)
    }
}

constexpr float kDepthBiasUnit = std::bit_cast<float>(0xB60637BDu); // 0x0064AAA4
constexpr float k255 = 255.0f;                                      // 0x00646B88

// The CRT's _ftol (0x006236FC): truncation, as a 64-bit integer.
int64_t ftol(double value) { return static_cast<int64_t>(value); }

} // namespace

// 0x004DAB60
void change_phase(int state, uint32_t value)
{
    auto& values = render_state_values();
    switch (state) {
    case kZWrite:
        if (value) {
            if (!values.z_write_) {
                render::set_render_state(D3DRS_ZWRITEENABLE, TRUE);
                values.z_write_ = 1;
            }
        } else if (values.z_write_ == 1) {
            render::set_render_state(D3DRS_ZWRITEENABLE, FALSE);
            values.z_write_ = 0;
        }
        return;
    case kZTest:
        if (value) {
            if (!get_z_test()) {
                render::set_render_state(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
                get_z_test() = 1;
            }
        } else if (get_z_test() == 1) {
            render::set_render_state(D3DRS_ZFUNC, D3DCMP_ALWAYS);
            get_z_test() = 0;
        }
        return;
    case kPhaseAlphaTest:
        if (value == alpha_test_level())
            return;
        alpha_test_level() = value;
        if (value) {
            render::set_render_state(D3DRS_ALPHAREF, value);
            if (!alpha_test()) {
                render::set_render_state(D3DRS_ALPHATESTENABLE, TRUE);
                alpha_test() = 1;
            }
        } else if (alpha_test()) {
            render::set_render_state(D3DRS_ALPHATESTENABLE, FALSE);
            alpha_test() = 0;
        }
        return;
    case kAddressMode0:
    case kAddressMode0 + 1:
    case kAddressMode0 + 2:
    case kAddressMode0 + 3: {
        const int sampler = state - kAddressMode0;
        uint32_t& cached = core::at(address_modes(), sampler);
        const uint32_t u = value & 0xFFFF;
        if (u != (cached & 0xFFFF)) {
            DWORD address = 0;
            if (address_value(u, &address))
                set_sampler(sampler, D3DSAMP_ADDRESSU, address);
            cached = (cached & 0xFFFF0000) | u;
        }
        const uint32_t v = value & 0xFFFF0000;
        if (v == (cached & 0xFFFF0000))
            return;
        DWORD address = 0;
        if (address_value(v >> 16, &address))
            set_sampler(sampler, D3DSAMP_ADDRESSV, address);
        cached = (cached & 0xFFFF) | v;
        return;
    }
    case kMipLodBias0:
    case kMipLodBias0 + 1:
    case kMipLodBias0 + 2:
    case kMipLodBias0 + 3: {
        const int sampler = state - kMipLodBias0;
        if (value != core::at(mip_lod_biases(), sampler)) {
            set_sampler(sampler, D3DSAMP_MIPMAPLODBIAS, value);
            core::at(mip_lod_biases(), sampler) = value;
        }
        return;
    }
    case kCullMode:
        if (value != cull_mode()) {
            values.cull_mode_ = value;
            render::set_render_state(D3DRS_CULLMODE, value);
        }
        return;
    case kAlphaBlend:
        if (value != alpha_blend()) {
            alpha_blend() = value;
            render::set_render_state(D3DRS_ALPHABLENDENABLE, value != 0);
        }
        return;
    case kAlphaTest:
        if (value != alpha_test()) {
            alpha_test() = value;
            render::set_render_state(D3DRS_ALPHATESTENABLE, value != 0);
        }
        return;
    case kSpecular:
        if (value != values.specular_) {
            values.specular_ = value;
            render::set_render_state(D3DRS_SPECULARENABLE, value != 0);
            render::set_render_state(D3DRS_LOCALVIEWER, value != 0);
        }
        return;
    case kFog:
        if (value != values.fog_active_) {
            values.fog_active_ = value;
            render::set_render_state(D3DRS_FOGENABLE, value != 0);
        }
        return;
    case kDepthBias:
        if (value != depth_bias()) {
            values.depth_bias_ = value;
            // (unsigned, scaled; x87 at double precision, stored as a float)
            auto bias = static_cast<float>(static_cast<double>(value) * kDepthBiasUnit);
            render::set_render_state(D3DRS_DEPTHBIAS, std::bit_cast<uint32_t>(bias));
        }
        return;
    case kLighting:
        if (value != get_lighting()) {
            get_lighting() = value;
            render::set_render_state(D3DRS_LIGHTING, value != 0);
        }
        return;
    case kFilter0:
    case kFilter0 + 1:
    case kFilter0 + 2:
    case kFilter0 + 3: {
        const int sampler = state - kFilter0;
        uint32_t& cached = core::at(filters(), sampler);
        const uint32_t mag = value & 0xFFFF0000;
        if (mag != (cached & 0xFFFF0000)) {
            set_sampler(sampler, D3DSAMP_MAGFILTER, (value & 0x10000) ? D3DTEXF_LINEAR : D3DTEXF_POINT);
            cached = (cached & 0xFFFF) | mag;
        }
        const uint32_t min = value & 0xFFFF;
        if (min != (cached & 0xFFFF)) {
            // NOT ORIGINAL - [Graphics] anisotropy (engine/gfx/graphics_options.h): anisotropic in place of linear.
            // Original behaviour: (value & 1) ? D3DTEXF_LINEAR : D3DTEXF_POINT
            set_sampler(sampler, D3DSAMP_MINFILTER,
                        graphics_options::get_min_filter((value & 1) != 0, graphics_options::active_anisotropy()));
            cached = (cached & 0xFFFF0000) | min;
        }
        return;
    }
    case kLocalViewer:
        if (value != local_viewer()) {
            local_viewer() = value != 0;
            render::set_render_state(D3DRS_LOCALVIEWER, value);
        }
        return;
    default: // (0xC-0xF, 0x17-0x1F: nothing)
        return;
    }
}

// 0x004D9DB0
void set_vertex_format(uint32_t fvf)
{
    if (vertex_format_locked() == 0 && vertex_format() != fvf) {
        d3d_device()->SetFVF(fvf);
        vertex_format() = fvf;
    }
}

// 0x004DA730
void set_texture(int stage, void* texture)
{
    auto& values = render_state_values();
    if (core::at(values.textures_, stage) != texture) {
        d3d_device()->SetTexture(stage, static_cast<IDirect3DBaseTexture9*>(texture));
        core::at(values.textures_, stage) = texture;
    }
}

// 0x004DA760
void choose_blend_mode(uint32_t mode)
{
    const uint32_t wanted = forced_blend() ? forced_blend() : mode;
    if (wanted == get_blend_mode())
        return;
    const uint32_t kind = wanted & 0xFFFFFF;
    if (kind != (get_blend_mode() & 0xFFFFFF)) {
        // Fog goes black under the additive and subtractive modes (1-4).
        if (render_state_values().fog_active_) {
            if (kind >= 1 && kind <= 4)
                render::set_render_state(D3DRS_FOGCOLOR, 0);
            else
                set_device_render_state(D3DRS_FOGCOLOR, fog_colour());
        }
        uint32_t op = 0, src = 0, dest = 0;
        switch (kind) {
        case 0:
        case 0xB:
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
        case 5:
        case 0xC:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_SRCALPHA;
            dest = D3DBLEND_INVSRCALPHA;
            break;
        case 6:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_BLENDFACTOR;
            dest = D3DBLEND_INVBLENDFACTOR;
            break;
        case 7:
        case 8:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_ZERO;
            dest = D3DBLEND_SRCALPHA;
            break;
        case 9:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_DESTCOLOR;
            dest = D3DBLEND_ONE;
            break;
        case 0xA:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_DESTCOLOR;
            dest = D3DBLEND_BLENDFACTOR;
            break;
        case 0xD:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_INVSRCALPHA;
            dest = D3DBLEND_SRCALPHA;
            break;
        case 0xF:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_ZERO;
            dest = D3DBLEND_SRCCOLOR;
            break;
        case 0x11:
            op = D3DBLENDOP_ADD;
            src = D3DBLEND_ONE;
            dest = D3DBLEND_INVSRCALPHA;
            break;
        default: // (0xE, 0x10 and anything bigger: the argument itself - not the override - for all three)
            op = src = dest = mode;
            break;
        }
        // Without blend factors: the nearest the colour and source alpha can do.
        if (!blend_factor_supported()) {
            if (src == D3DBLEND_BLENDFACTOR && dest == D3DBLEND_INVBLENDFACTOR) {
                src = D3DBLEND_ONE;
                dest = D3DBLEND_INVSRCALPHA;
            } else {
                if (src == D3DBLEND_BLENDFACTOR)
                    src = D3DBLEND_ZERO;
                if (dest == D3DBLEND_BLENDFACTOR)
                    dest = D3DBLEND_SRCALPHA;
                else if (dest == D3DBLEND_INVBLENDFACTOR)
                    dest = D3DBLEND_INVSRCALPHA;
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
    uint32_t alpha = wanted & 0xFF000000;
    if (alpha != (get_blend_mode() & 0xFF000000)) {
        alpha = alpha >= 0x80000000 ? 0xFF000000 : alpha << 1;
        set_device_render_state(D3DRS_BLENDFACTOR, (((alpha >> 8 | alpha) >> 8 | alpha) >> 8) | alpha);
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
        set_fixed_function_stages(shader, 0xFFFFFFFF);
        return;
    }
    const std::array<float, 4>& color = core::at(material->pass_color_, 0); // (the first pass's colour)
    const uint32_t r = static_cast<uint8_t>(ftol(static_cast<double>(std::get<0>(color)) * k255));
    const uint32_t g = static_cast<uint8_t>(ftol(static_cast<double>(std::get<1>(color)) * k255));
    const uint32_t b = static_cast<uint8_t>(ftol(static_cast<double>(std::get<2>(color)) * k255));
    set_fixed_function_stages(shader, (((r | 0xFFFFFF00) << 8 | g) << 8) | b);
}

// 0x004DAA80
void set_pixel_shader(void* shader, const MaterialRef* material)
{
    auto& values = render_state_values();
    if (!values.shaders_available_) {
        set_fixed_function_stages_for_material(shader, material);
        return;
    }
    if (pixel_shader_locked() == nullptr) {
        if (current_pixel_shader() != shader) {
            // Not original - the level editor's Unlit view (engine/gfx/view_mode.h): the shader's unlit twin during the 3D
            // passes. Original behaviour: the device gets `shader`.
            d3d_device()->SetPixelShader(static_cast<IDirect3DPixelShader9*>(view_mode::device_shader(shader)));
            values.current_pixel_shader_ = shader;
            values.pixel_shader_changed_ = 1;
        }
        d3d_device()->SetPixelShaderConstantF(0, ps_constant_bank().data(), 5);
    }
    values.pixel_shader_changed_ = 0;
}

// 0x004DAAF0
void set_pixel_shader_with_constants(void* shader, uint32_t count)
{
    auto& values = render_state_values();
    if (pixel_shader_locked() == nullptr) {
        if (current_pixel_shader() != shader) {
            // Not original - the Unlit view's twin (view_mode.h), as in SetPixelShader.
            d3d_device()->SetPixelShader(static_cast<IDirect3DPixelShader9*>(view_mode::device_shader(shader)));
            values.current_pixel_shader_ = shader;
            values.pixel_shader_changed_ = 1;
        }
        d3d_device()->SetPixelShaderConstantF(0, ps_constant_bank().data(), count);
        d3d_device()->SetPixelShaderConstantF(4, std::span(ps_constant_bank()).subspan(16).data(), 1);
    }
    values.pixel_shader_changed_ = 0;
}

// 0x004DA0E0
float last_sphere_distance() { return occlusion_shared().last_sphere_distance_; }

// 0x004DB620
void set_world_transform(const float* matrix)
{
    auto& values = render_state_values();
    if (!matrix) {
        values.world_transform_ = nullptr;
        return;
    }
    auto& world = values.world_transform_copy_;
    values.world_transform_ = world.data();
    const std::span<const float, 16> given(matrix, 16);
    for (int row = 0; row < 4; ++row) {
        core::at(world, row * 4 + 0) = core::at(given, row * 4 + 0);
        core::at(world, row * 4 + 1) = core::at(given, row * 4 + 1);
        core::at(world, row * 4 + 2) = core::at(given, row * 4 + 2);
        core::at(world, row * 4 + 3) = row == 3 ? 1.0f : 0.0f;
    }
}

// 0x004D7D70
void reset_render_state()
{
    auto& values = render_state_values();
    local_viewer() = 0;
    // The world, view and projection transforms (0x00786B90, 0x00786BD0, 0x00786C10): identity.
    for (maths::Mat4* matrix : {&world_matrix(), &view_matrix(), &projection_matrix()}) {
        const auto m = matrix->floats();
        for (int i = 0; i < 16; ++i)
            core::at(m, i) = (i % 5 == 0) ? 1.0f : 0.0f;
    }
    get_lighting() = 0;
    values.specular_ = 0;
    values.cull_mode_ = D3DCULL_CW;
    values.near_camera_ = true;
    dither() = 1;
    get_z_test() = 1;
    values.z_write_ = 1;
    alpha_blend() = 1;
    alpha_test() = 1;
    alpha_test_level() = 0;
    for (int i = 0; i < 4; ++i) {
        core::at(address_modes(), i) = 0;
        core::at(mip_lod_biases(), i) = 0;
        core::at(values.textures_, i) = nullptr;
        core::at(filters(), i) = 0x10000;
        core::at(values.border_colours_, i) = 0;
    }
    // (none of these is a mode: the next one is always sent)
    get_blend_mode() = 0xDEADBABE;
    blend_op() = 0xDEADBABE;
    src_blend() = 0xDEADBABE;
    dest_blend() = 0xDEADBABE;
    core::at(engine_glue_shared().pending_screen_shot_, 0) = 0;
    engine_glue_shared().screen_shot_name_exact_ = false;
    // Pixel shader constant 4: (0, 0.5, 0, 0.5).
    core::at(values.ps_constant_bank_, 16) = 0.0f;
    core::at(values.ps_constant_bank_, 17) = 0.5f;
    core::at(values.ps_constant_bank_, 18) = 0.0f;
    core::at(values.ps_constant_bank_, 19) = 0.5f;
    values.pixel_shader_locked_ = nullptr;
    values.current_pixel_shader_ = nullptr;
    values.pixel_shader_changed_ = 0;
    render_camera().custom_transforms_ = 0;
    pc_billboards_shared().billboard_flag_ = 0;
    values.vertex_format_locked_ = 0;
    values.skipped_passes_ = 0;
    values.submit_locked_ = 0;
    forced_blend() = 0;
    render_camera().clear_colour_and_depth_ = true;
    render_camera().background_colour_ = 0;
    render_camera().letterbox_active_ = 0; // the letterbox
    // The default light: its direction (0x00786F48) and colours, as bit patterns.
    static const auto kLight = std::to_array<uint32_t>({
        0xBF000000, 0xBF5DB3D7, 0, 0, 0x3F160419, 0x3F160419, 0x3F160419, 0x3F800000, 0x3F800000, 0, 0, 0, 0, 0, 0,
        0x3F800000, 0x3F800000, 0, 0, 0,          0,          0,          0,          0x3F800000,
    });
    for (int i = 0; i < 24; ++i)
        core::at(pc_lights_shared().light_constants_, 4 + i) = std::bit_cast<float>(core::at(kLight, i));
    constexpr auto kAmbient = std::bit_cast<float>(0x3F1624DDu); // the ambient colour (0.586)
    core::at(pc_lights_shared().light_constants_, 0) = kAmbient;
    core::at(pc_lights_shared().light_constants_, 1) = kAmbient;
    core::at(pc_lights_shared().light_constants_, 2) = kAmbient;
    core::at(pc_lights_shared().light_constants_, 3) = 1.0f;
    values.fog_active_ = 0;
    set_fog_colour(0);
    set_fog_begin_distance(-240.0f); // fog start and end (the original stored their bit patterns)
    set_fog_end(-24600.0f);
}

// 0x004D80D0
void apply_render_state()
{
    auto& values = render_state_values();
    IDirect3DDevice9* device = d3d_device();
    device->SetRenderState(D3DRS_LOCALVIEWER, local_viewer());
    d3d_device()->SetRenderState(D3DRS_COLORVERTEX, FALSE);
    d3d_device()->SetRenderState(D3DRS_LIGHTING, get_lighting());
    d3d_device()->SetRenderState(D3DRS_SPECULARENABLE, values.specular_);
    d3d_device()->SetRenderState(D3DRS_CULLMODE, cull_mode());
    d3d_device()->SetRenderState(D3DRS_DITHERENABLE, dither());
    d3d_device()->SetRenderState(D3DRS_ZENABLE, get_z_test());
    d3d_device()->SetRenderState(D3DRS_ZWRITEENABLE, values.z_write_);
    d3d_device()->SetRenderState(D3DRS_ALPHABLENDENABLE, alpha_blend());
    d3d_device()->SetRenderState(D3DRS_ALPHATESTENABLE, alpha_test());
    d3d_device()->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    d3d_device()->SetRenderState(D3DRS_ALPHAREF, alpha_test_level());
    for (int sampler = 0; sampler < 4; ++sampler) {
        core::at(address_modes(), sampler) = 0;
        core::at(mip_lod_biases(), sampler) = 0;
        core::at(values.textures_, sampler) = nullptr;
        core::at(filters(), sampler) = 0x10000;
        core::at(values.border_colours_, sampler) = 0;
        d3d_device()->SetSamplerState(sampler, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        d3d_device()->SetSamplerState(sampler, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        // NOT ORIGINAL - [Graphics] anisotropy (engine/gfx/graphics_options.h): anisotropic minification on every
        // sampler at the chosen taps. (Off: the original's below.)
        if (const int taps = graphics_options::active_anisotropy()) {
            d3d_device()->SetSamplerState(sampler, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
            d3d_device()->SetSamplerState(sampler, D3DSAMP_MAXANISOTROPY, static_cast<DWORD>(taps));
        } else if (sampler == 0 && back_buffer_height() <= 480) {
            // Anisotropic minification on the first sampler, at back buffers up to 480 lines high.
            d3d_device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
            const uint32_t anisotropy = max_anisotropy();
            d3d_device()->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, anisotropy < 3 ? anisotropy : 3);
        } else {
            d3d_device()->SetSamplerState(sampler, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        }
        d3d_device()->SetSamplerState(sampler, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        d3d_device()->SetSamplerState(sampler, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    }
    const D3DMATERIAL9 material = {};
    d3d_device()->SetMaterial(&material);
    d3d_device()->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    d3d_device()->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2);
    d3d_device()->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
    d3d_device()->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
    mark_billboards_ready();
    compile_pixel_shaders();
    d3d_device()->SetRenderState(D3DRS_FOGENABLE, values.fog_active_);
    d3d_device()->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
    d3d_device()->SetRenderState(D3DRS_RANGEFOGENABLE, TRUE);
    d3d_device()->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
    d3d_device()->SetRenderState(D3DRS_FOGCOLOR, fog_colour());
    d3d_device()->SetRenderState(D3DRS_FOGSTART, std::bit_cast<uint32_t>(fog_begin_distance()));
    d3d_device()->SetRenderState(D3DRS_FOGEND, std::bit_cast<uint32_t>(fog_end()));
    // (NOT ORIGINAL: or [Graphics] msaa's samples - engine/gfx/graphics_options.h)
    if (antialiasing() || graphics_options::chosen_samples()) // the antialiasing setting
        d3d_device()->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
}

} // namespace render
