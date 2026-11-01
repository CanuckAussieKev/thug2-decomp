// The PC materials (MaterialRef, surface_material.h): constructed and destroyed, their animations (UV wibbles, colours,
// textures) brought up to date, and set up on the device for drawing - all passes (for the pixel shaders) or one pass
// through the fixed-function texture stages. The animations' arithmetic keeps the original's: values its x87 code
// kept in registers are doubles, rounded to float where it stores.

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/maths/trig.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/gfx/primitive_states.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "platform/stopwatch.h"
#include "engine/gfx/device.h"
#include "engine/sys/startup_init.h"

namespace render {

namespace {
float g_billboard_uv_matrix[8]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
float g_wibble_matrix_pass[16]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
float g_environment_matrix_pass[16]{0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f,
                                    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f,  0.5f};
float g_wibble_matrix[16]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
float g_environment_matrix[16]{0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f};

IDirect3DDevice9* input_port() { return g_d3d_device; }
float* pass_constants() { return g_ps_constant_bank; } // the pixel shader's colours, 16 bytes a pass
bool& constants_changed() { return reinterpret_cast<bool&>(g_pixel_shader_changed); }

void device_stage_state(uint32_t stage, D3DTEXTURESTAGESTATETYPE type, uint32_t value)
{
    input_port()->SetTextureStageState(stage, type, value);
}

constexpr uint32_t kPassUvWibble = 0x1;        // the texture matrix from the pass's UV wibble
constexpr uint32_t kPassColorAnim = 0x2;       // (pass 0's) the material's colour animations
constexpr uint32_t kPassEnvironment = 0x8;     // environment mapped (near the camera only)
constexpr uint32_t kPassSpecular = 0x100;      // (pass 0's) specular lighting
constexpr uint32_t kPassBorder = 0x200;        // the sampler's border colour 0xE0000000
constexpr uint32_t kPassBumpEnv = 0x400;       // the bump environment matrix
constexpr uint32_t kPassTextureAnim = 0x800;   // the texture from the material's texture animation
constexpr uint32_t kPassTextureAlpha = 0x1000; // alpha from the texture only
constexpr uint32_t kPassExplicitUv = 0x4000;   // the UV wibble's matrix is set, not animated

// (t - from) / (to - from) between two keys' colours, channel by channel in the order a, r, g, b (the bytes 3, 0, 1,
// 2), each to * f + from * (1 - f) truncated - and not masked: the four values are shifted and or'd as they come.
uint32_t blend_key_colors(int num, int den, const uint8_t* from, const uint8_t* to)
{
    static const int kOrder[4] = {3, 0, 1, 2};
    const double f = static_cast<double>(num) / den;
    const double rest = 1.0 - f;
    uint32_t color = 0;
    for (int k = 0; k < 4; ++k) {
        const auto value = static_cast<uint32_t>(maths::truncate_to_i64(to[kOrder[k]] * f + from[kOrder[k]] * rest));
        color = k ? (color << 8 | value) : value;
    }
    return color;
}

// The time in seconds the UV wibbles use: the milliseconds (unsigned) times 0.001, stored as a float.
float wibble_time(uint32_t milliseconds) { return static_cast<float>(static_cast<double>(milliseconds) * 0.001f); }

// One axis of a UV wibble's offset: the phase-shifted sine times the amplitude plus the velocity's travel, plus 8,
// less its whole part rounded down to a multiple of 16 - then 8 back the other way (added below 0, taken off
// otherwise). The v axis is stored as a float before that last step, the u axis isn't.
float wibble_offset(float t, float frequency, float phase, float amplitude, float velocity, bool store_first)
{
    double value =
        std::sin(static_cast<double>(t) * frequency + phase) * amplitude + static_cast<double>(t) * velocity + 8.0;
    const auto whole = static_cast<int32_t>(static_cast<uint32_t>(maths::truncate_to_i64(value)) & 0xFFFFFFF0u);
    value -= whole;
    if (store_first)
        value = static_cast<float>(value);
    return static_cast<float>(value < 0.0 ? value + 8.0 : value - 8.0);
}

// What both set-ups share: the alpha reference, culling, depth bias, the animations, and the specular material
// (pass 0's flag 0x100) - or none, once, when the last material had it (0x00786E4C).
void set_common_states(MaterialRef* material)
{
    change_phase(kPhaseAlphaTest, material->alpha_ref_);
    change_phase(kCullMode, material->two_sided_ ? D3DCULL_NONE : D3DCULL_CW);
    change_phase(kDepthBias, material->depth_bias_);
    material->update_uv_wibbles();
    material->update_color_anims();
    material->update_texture_anims();
    const uint32_t specular_on = g_specular;
    D3DMATERIAL9 specular;
    if (material->pass_flags_[0] & kPassSpecular) {
        if (specular_on)
            return;
        change_phase(kSpecular, 1);
        std::memset(&specular, 0, sizeof(specular));
        specular.Specular.r = material->specular[0];
        specular.Specular.g = material->specular[1];
        specular.Specular.b = material->specular[2];
        specular.Specular.a = 1.0f;
        specular.Power = material->specular_power_;
    } else {
        if (!specular_on)
            return;
        change_phase(kSpecular, 0);
        std::memset(&specular, 0, sizeof(specular));
    }
    input_port()->SetMaterial(&specular);
}

// The pass's border colour (0x00786EA4, a word a stage): 0xE0000000 with flag 0x200, else 0.
void set_border_color(const MaterialRef* material, uint32_t pass, uint32_t stage)
{
    uint32_t& border = g_border_colours[stage];
    if (material->pass_flags_[pass] & kPassBorder) {
        if (border != 0xE0000000)
            border = 0xE0000000;
    } else if (border) {
        border = 0;
    }
}

void change_bump_map_matrix(uint32_t stage)
{
    device_stage_state(stage, D3DTSS_BUMPENVMAT00, reinterpret_cast<uint32_t&>(g_heading_matrix));
    device_stage_state(stage, D3DTSS_BUMPENVMAT01, reinterpret_cast<uint32_t&>(g_heading_matrix.row[0].z));
    device_stage_state(stage, D3DTSS_BUMPENVMAT10, reinterpret_cast<uint32_t&>(g_heading_matrix.row[2].x));
    device_stage_state(stage, D3DTSS_BUMPENVMAT11, reinterpret_cast<uint32_t&>(g_heading_matrix.row[2].z));
}

// A UV wibble's texture matrix (a static, `matrix`): its 2x2 part (the second row's first value negated) and offsets.
void fill_wibble_matrix(float* matrix, const UvOscillator* wibble)
{
    matrix[0] = wibble->matrix[0];
    matrix[1] = wibble->matrix[1];
    matrix[5] = wibble->matrix[0];
    matrix[4] = -wibble->matrix[1];
    matrix[8] = wibble->offset[0];
    matrix[9] = wibble->offset[1];
}

uint32_t factor_channel(float value)
{
    return static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(value) * 255.0f)) & 0xFF;
}

} // namespace

// 0x004D1A90
MaterialRef::MaterialRef()
{
    for (int pass = 0; pass < 4; ++pass) {
        unknownc0_[pass] = 0x10001;
        pass_flags_[pass] = 0;
        textures[pass] = nullptr;
        uv_wibble_[pass] = nullptr;
        reflection_tiling_[pass][0] = 3.0f;
        reflection_tiling_[pass][1] = 3.0f;
    }
}

// 0x004D2BD0
MaterialRef::~MaterialRef()
{
    memory::Hub* heap = memory::Hub::get_pull();
    if (color_anims_) {
        for (uint32_t i = 0; i < color_anim_count_; ++i)
            heap->dealloc(color_anims_[i].keys);
        heap->dealloc(color_anims_);
    }
    if (anim_colors_)
        heap->dealloc(anim_colors_);
    for (UvOscillator* wibble : uv_wibble_)
        if (wibble)
            heap->dealloc(wibble);
    if (texture_anim_) {
        for (TextureKey* keys : texture_anim_->keys)
            heap->dealloc(keys);
        heap->dealloc(texture_anim_);
    }
}

// 0x004D1D90
uint32_t MaterialRef::texture_alpha_passes() const
{
    uint32_t mask = 0;
    for (uint32_t pass = 0; pass < pass_count_; ++pass)
        if (pass_flags_[pass] & kPassTextureAlpha)
            mask |= 1u << (pass & 31);
    return mask;
}

// 0x004D1B40
void MaterialRef::update_color_anims()
{
    if (!(pass_flags_[0] & kPassColorAnim))
        return;
    for (uint32_t i = 0; i < color_anim_count_; ++i) {
        const ColorAnim& anim = color_anims_[i];
        const ColorKey* keys = anim.keys;
        const int first = keys[0].time;
        const int period = keys[anim.key_count_ - 1].time - first;
        const int t = period ? (anim.phase + g_step_seconds) % period + first : 1;
        int key = static_cast<int>(anim.key_count_) - 1;
        while (key >= 0 && t < keys[key].time)
            --key;
        const ColorKey& from = keys[key];
        const ColorKey& to = keys[key + 1];
        anim_colors_[i] = blend_key_colors(t - from.time, to.time - from.time, from.rgba, to.rgba);
    }
}

// 0x004D1CC0
void MaterialRef::update_texture_anims()
{
    if (!has_texture_anim_)
        return;
    const int now = static_cast<int>(frame_clock::get_time());
    const TextureAnim* anim = texture_anim_;
    for (int pass = 0; pass < 4; ++pass) {
        if (!(pass_flags_[pass] & kPassTextureAnim))
            continue;
        const TextureKey* keys = anim->keys[pass];
        const int count = static_cast<int>(anim->key_counts_[pass]);
        const int first = keys[0].time;
        const int period = keys[count - 1].time - first;
        const int phase = static_cast<int>(anim->phases[pass]);
        int t = (now + phase) % period + first;
        const int loops = anim->loops[pass];
        if (loops > 0 && (now - first) / period >= loops)
            t = (loops * period + phase) % period + first; // (held on the last loop's end)
        int key = count - 1;
        while (key >= 0 && t < keys[key].time)
            --key;
        textures[pass] = keys[key].texture;
    }
}

// 0x004D1DC0
void MaterialRef::update_uv_wibbles()
{
    if (!has_uv_wibble_)
        return;
    const float t = wibble_time(frame_clock::get_time());
    for (uint32_t pass = 0; pass < pass_count_; ++pass) {
        if (!(pass_flags_[pass] & kPassUvWibble) || (pass_flags_[pass] & kPassExplicitUv))
            continue;
        UvOscillator* w = uv_wibble_[pass];
        const float u = wibble_offset(t, w->frequency[0], w->phase[0], w->amplitude[0], w->velocity[0], false);
        const float v = wibble_offset(t, w->frequency[1], w->phase[1], w->amplitude[1], w->velocity[1], true);
        w->offset[0] = u;
        w->offset[1] = v;
    }
}

// 0x004D1F00
void MaterialRef::hand_in()
{
    if (g_submit_locked)
        return;
    choose_blend_mode(blend_modes_[0]);
    set_common_states(this);
    for (uint32_t pass = 0; pass < pass_count_; ++pass) {
        if (g_skipped_passes & (1u << (pass & 31)))
            continue;
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison): compares the bits, as the original
        if (!constants_changed() && std::memcmp(pass_color_[pass], pass_constants() + 4 * pass, 16) != 0)
            constants_changed() = true;
        uint8_t* texture = textures[pass];
        if (!texture) {
            set_texture(static_cast<int>(pass), nullptr);
            continue;
        }
        set_texture(static_cast<int>(pass), *reinterpret_cast<void**>(texture + 0x10));
        change_phase(kAddressMode0 + static_cast<int>(pass), address_modes_[pass]);
        change_phase(kMipLodBias0 + static_cast<int>(pass), mip_lod_bias_[pass]);
        set_border_color(this, pass, pass);
        uint32_t flags = pass_flags_[pass];
        if (flags & kPassBumpEnv)
            change_bump_map_matrix(pass);
        flags = pass_flags_[pass];
        auto texture_matrix = static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0 + pass);
        if ((flags & kPassEnvironment) && g_near_camera) {
            float* matrix = g_environment_matrix_pass;
            matrix[0] = static_cast<float>(reflection_tiling_[pass][0] * 0.5);
            matrix[5] = static_cast<float>(reflection_tiling_[pass][1] * 0.5);
            change_phase(kLocalViewer, 1);
            input_port()->SetTransform(texture_matrix, reinterpret_cast<const D3DMATRIX*>(matrix));
            device_stage_state(pass, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
            device_stage_state(pass, D3DTSS_TEXCOORDINDEX, pass | D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
        } else if (flags & kPassUvWibble) {
            device_stage_state(pass, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
            device_stage_state(pass, D3DTSS_TEXCOORDINDEX, pass);
            float* matrix = g_wibble_matrix_pass;
            fill_wibble_matrix(matrix, uv_wibble_[pass]);
            input_port()->SetTransform(texture_matrix, reinterpret_cast<const D3DMATRIX*>(matrix));
        } else {
            if (g_billboard_flag) { // (the billboard flag: a static matrix's 2x2 part reset)
                float* m = g_billboard_uv_matrix;
                m[0] = 1.0f;
                m[1] = 0;
                m[3] = 0;
                m[4] = 0;
                m[5] = 1.0f;
                m[7] = 0;
            }
            DWORD transform_flags = 0, coordinates = 0;
            input_port()->GetTextureStageState(pass, D3DTSS_TEXTURETRANSFORMFLAGS, &transform_flags);
            input_port()->GetTextureStageState(pass, D3DTSS_TEXCOORDINDEX, &coordinates);
            if (transform_flags)
                device_stage_state(pass, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
            if (coordinates != pass)
                device_stage_state(pass, D3DTSS_TEXCOORDINDEX, pass);
        }
    }
    if (constants_changed())
        std::memcpy(pass_constants(), pass_color_, pass_count_ * 16);
}

// 0x004D23E0
void MaterialRef::submit_states()
{
    if (g_submit_locked)
        return;
    set_common_states(this);
}

// 0x004D24D0
void MaterialRef::submit_pass(uint32_t pass, const float* color)
{
    if (g_submit_locked)
        return;
    choose_blend_mode(blend_modes_[pass]);
    if (!(g_skipped_passes & (1u << (pass & 31)))) {
        uint8_t* texture = textures[pass];
        if (!texture) {
            set_texture(0, nullptr);
        } else {
            set_texture(0, *reinterpret_cast<void**>(texture + 0x10));
            change_phase(kAddressMode0, address_modes_[pass]);
            change_phase(kMipLodBias0, mip_lod_bias_[pass]);
            set_border_color(this, pass, pass);
            if (pass_flags_[pass] & kPassBumpEnv)
                change_bump_map_matrix(0);
            const uint32_t flags = pass_flags_[pass];
            if ((flags & kPassEnvironment) && g_near_camera) {
                float* matrix = g_environment_matrix;
                matrix[0] = static_cast<float>(reflection_tiling_[pass][0] * 0.5);
                matrix[5] = static_cast<float>(reflection_tiling_[pass][1] * 0.5);
                change_phase(kLocalViewer, 1);
                render::set_transform(D3DTS_TEXTURE0, matrix);
                set_texture_stage_state(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
                set_texture_stage_state(0, D3DTSS_TEXCOORDINDEX, pass | D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
            } else if (flags & kPassUvWibble) {
                device_stage_state(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
                device_stage_state(0, D3DTSS_TEXCOORDINDEX, pass);
                float* matrix = g_wibble_matrix;
                fill_wibble_matrix(matrix, uv_wibble_[pass]);
                input_port()->SetTransform(D3DTS_TEXTURE0, reinterpret_cast<const D3DMATRIX*>(matrix));
            } else {
                DWORD transform_flags = 0, coordinates = 0;
                input_port()->GetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, &transform_flags);
                input_port()->GetTextureStageState(0, D3DTSS_TEXCOORDINDEX, &coordinates);
                if (transform_flags)
                    set_texture_stage_state(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
                if (coordinates != pass)
                    set_texture_stage_state(0, D3DTSS_TEXCOORDINDEX, pass);
                const uint32_t blend = blend_modes_[pass];
                if (blend == 0xC || blend == 0xD) { // (blending with the pass before: its texture on stage 1)
                    set_texture_stage_state(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
                    if (static_cast<int>(pass) > 0) {
                        set_texture_stage_state(1, D3DTSS_TEXCOORDINDEX, pass - 1);
                        set_texture(1, *reinterpret_cast<void**>(textures[pass - 1] + 0x10));
                        change_phase(kAddressMode0 + 1, address_modes_[pass - 1]);
                        change_phase(kMipLodBias0 + 1, mip_lod_bias_[pass - 1]);
                    } else {
                        set_texture_stage_state(1, D3DTSS_TEXCOORDINDEX, pass);
                        set_texture(1, *reinterpret_cast<void**>(textures[pass] + 0x10));
                    }
                }
            }
        }
    }
    if (constants_changed())
        std::memcpy(pass_constants(), pass_color_, pass_count_ * 16);

    // the stages' colour and alpha operations
    if (static_cast<int8_t>(pass_flags_[pass] & 0xFF) < 0)
        color = nullptr; // (the pass's colour isn't to be changed)
    const float* own = pass_color_[pass];
    const bool alpha_from_texture = (pass_flags_[pass] & kPassTextureAlpha) != 0;
    const bool blends_with_previous = blend_modes_[pass] == 0xC || blend_modes_[pass] == 0xD;
    const uint32_t half = 0x3F000000; // 0.5: the colour unchanged
    // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison): compares the bits, as the original
    if (std::memcmp(&own[0], &half, 4) == 0 && std::memcmp(&own[1], &half, 4) == 0 &&
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison): compares the bits, as the original
        std::memcmp(&own[2], &half, 4) == 0 && !color) {
        if (!blends_with_previous) {
            device_stage_state(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
            device_stage_state(0, D3DTSS_ALPHAOP, alpha_from_texture ? D3DTOP_SELECTARG1 : D3DTOP_MODULATE2X);
            device_stage_state(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
            device_stage_state(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            device_stage_state(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
            device_stage_state(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            device_stage_state(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
            return;
        }
        device_stage_state(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        device_stage_state(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        device_stage_state(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        device_stage_state(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
        device_stage_state(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        device_stage_state(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
        device_stage_state(1, D3DTSS_ALPHAOP, alpha_from_texture ? D3DTOP_SELECTARG1 : D3DTOP_MODULATE2X);
        device_stage_state(1, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        device_stage_state(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        device_stage_state(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
    } else {
        device_stage_state(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        device_stage_state(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        device_stage_state(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
        const float* factor = color ? color : own;
        const uint32_t r = factor_channel(factor[0]);
        const uint32_t g = factor_channel(factor[1]);
        const uint32_t b = factor_channel(factor[2]);
        input_port()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFF000000u | r << 16 | g << 8 | b);
        if (alpha_from_texture) {
            device_stage_state(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        } else {
            device_stage_state(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE2X);
            device_stage_state(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
        }
        device_stage_state(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        device_stage_state(1, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        device_stage_state(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
        device_stage_state(1, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        if (blends_with_previous) {
            device_stage_state(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        } else {
            device_stage_state(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
            device_stage_state(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
        }
    }
    device_stage_state(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
}

} // namespace render

namespace render {

namespace {

// A material nothing uses but its static initialiser (0x00640D70: its constructor - run by C++ static
// initialisation here) and exit function.
MaterialRef& spare_material()
{
    static MaterialRef value;
    return value;
}

} // namespace

} // namespace render
