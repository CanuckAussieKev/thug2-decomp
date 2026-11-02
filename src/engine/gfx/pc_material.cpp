// The PC materials (MaterialRef, surface_material.h): constructed and destroyed, their animations (UV wibbles, colours,
// textures) brought up to date, and set up on the device for drawing - all passes (for the pixel shaders) or one pass
// through the fixed-function texture stages. The animations' arithmetic keeps the original's: values its x87 code
// kept in registers are doubles, rounded to float where it stores.

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>
#include <span>

#include "engine/core/manager.h"
#include "engine/maths/trig.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/gfx/primitive_states.h"
#include "engine/gfx/render_state.h"
#include "platform/stopwatch.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/bitmap_texture.h"
#include <bit>
#include "platform/d3d_matrix.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/view_setup.h"

namespace render {

namespace {
// NOT ORIGINAL grouping - pc_material.cpp's state (the original's globals). Made on first use, never destroyed
// (pc_material_state()).
struct PcMaterialState {
    std::array<float, 8> billboard_uv_matrix_{{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
    std::array<float, 16> wibble_matrix_pass_{{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
    std::array<float, 16> wibble_matrix_{{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
    std::array<float, 16> environment_matrix_{{0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f}};
    // the environment pass's texture matrix (its scales set per pass)
    std::array<float, 16> environment_matrix_pass_{{0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f}};
};

PcMaterialState& pc_material_state()
{
    static core::NoDestroy<PcMaterialState> state;
    return state.get();
}

void device_stage_state(uint32_t stage, D3DTEXTURESTAGESTATETYPE type, uint32_t value)
{
    renderer().d3d_device()->SetTextureStageState(stage, type, value);
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
uint32_t blend_key_colors(int num, int den, const std::array<uint8_t, 4>& from, const std::array<uint8_t, 4>& to)
{
    static const std::array<int, 4> kOrder = {{3, 0, 1, 2}};
    const double f = static_cast<double>(num) / den;
    const double rest = 1.0 - f;
    uint32_t color = 0;
    for (int k = 0; k < 4; ++k) {
        const auto value = static_cast<uint32_t>(maths::truncate_to_i64(core::at(to, core::at(kOrder, k)) * f + core::at(from, core::at(kOrder, k)) * rest));
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
    change_phase(kPhaseAlphaTest, material->alpha_ref());
    change_phase(kCullMode, material->two_sided() ? D3DCULL_NONE : D3DCULL_CW);
    change_phase(kDepthBias, material->depth_bias());
    material->update_uv_wibbles();
    material->update_color_anims();
    material->update_texture_anims();
    const uint32_t specular_on = renderer().specular_state();
    D3DMATERIAL9 specular;
    if (core::at(material->pass_flags(), 0) & kPassSpecular) {
        if (specular_on)
            return;
        change_phase(kSpecular, 1);
        std::memset(&specular, 0, sizeof(specular));
        specular.Specular.r = core::at(material->specular_value(), 0);
        specular.Specular.g = core::at(material->specular_value(), 1);
        specular.Specular.b = core::at(material->specular_value(), 2);
        specular.Specular.a = 1.0f;
        specular.Power = material->specular_power();
    } else {
        if (!specular_on)
            return;
        change_phase(kSpecular, 0);
        std::memset(&specular, 0, sizeof(specular));
    }
    renderer().d3d_device()->SetMaterial(&specular);
}

// The pass's border colour (0x00786EA4, a word a stage): 0xE0000000 with flag 0x200, else 0.
void set_border_color(const MaterialRef* material, uint32_t pass, uint32_t stage)
{
    uint32_t& border = core::at(renderer().border_colours(), stage);
    if (core::at(material->pass_flags(), pass) & kPassBorder) {
        if (border != 0xE0000000)
            border = 0xE0000000;
    } else if (border) {
        border = 0;
    }
}

void change_bump_map_matrix(uint32_t stage)
{
    device_stage_state(stage, D3DTSS_BUMPENVMAT00, std::bit_cast<uint32_t>(core::at(renderer().heading_matrix().row, 0).x));
    device_stage_state(stage, D3DTSS_BUMPENVMAT01, std::bit_cast<uint32_t>(core::at(renderer().heading_matrix().row, 0).z));
    device_stage_state(stage, D3DTSS_BUMPENVMAT10, std::bit_cast<uint32_t>(core::at(renderer().heading_matrix().row, 2).x));
    device_stage_state(stage, D3DTSS_BUMPENVMAT11, std::bit_cast<uint32_t>(core::at(renderer().heading_matrix().row, 2).z));
}

// A UV wibble's texture matrix (a static, `matrix`): its 2x2 part (the second row's first value negated) and offsets.
void fill_wibble_matrix(std::span<float, 16> matrix, const UvOscillator* wibble)
{
    core::at(matrix, 0) = core::at(wibble->matrix, 0);
    core::at(matrix, 1) = core::at(wibble->matrix, 1);
    core::at(matrix, 5) = core::at(wibble->matrix, 0);
    core::at(matrix, 4) = -core::at(wibble->matrix, 1);
    core::at(matrix, 8) = core::at(wibble->offset, 0);
    core::at(matrix, 9) = core::at(wibble->offset, 1);
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
        core::at(unknownc0_, pass) = 0x10001;
        core::at(pass_flags_, pass) = 0;
        core::at(textures, pass) = nullptr;
        core::at(uv_wibble_, pass) = nullptr;
        core::at(core::at(reflection_tiling_, pass), 0) = 3.0f;
        core::at(core::at(reflection_tiling_, pass), 1) = 3.0f;
    }
}

// 0x004D2BD0
MaterialRef::~MaterialRef()
{
    memory::Hub* heap = memory::Hub::get_pull();
    if (color_anims_) {
        for (const ColorAnim& anim : color_anim_run())
            heap->dealloc(anim.keys);
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
        if (core::at(pass_flags_, pass) & kPassTextureAlpha)
            mask |= 1u << (pass & 31);
    return mask;
}

// 0x004D1B40
void MaterialRef::update_color_anims()
{
    if (!(core::at(pass_flags_, 0) & kPassColorAnim))
        return;
    for (uint32_t i = 0; i < color_anim_count_; ++i) {
        const ColorAnim& anim = core::at(color_anim_run(), i);
        const std::span<ColorKey> keys = anim.key_run();
        const int first = core::at(keys, 0).time;
        const int period = core::at(keys, anim.key_count_ - 1).time - first;
        const int t = period ? (anim.phase + renderer().step_seconds()) % period + first : 1;
        int key = static_cast<int>(anim.key_count_) - 1;
        while (key >= 0 && t < core::at(keys, key).time)
            --key;
        const ColorKey& from = core::at(keys, key);
        const ColorKey& to = core::at(keys, key + 1);
        core::at(anim_colour_run(), i) = blend_key_colors(t - from.time, to.time - from.time, from.rgba, to.rgba);
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
        if (!(core::at(pass_flags_, pass) & kPassTextureAnim))
            continue;
        const std::span<TextureKey> keys = anim->key_run(static_cast<size_t>(pass));
        const int count = static_cast<int>(core::at(anim->key_counts_, pass));
        const int first = core::at(keys, 0).time;
        const int period = core::at(keys, count - 1).time - first;
        const int phase = static_cast<int>(core::at(anim->phases, pass));
        int t = (now + phase) % period + first;
        const int loops = core::at(anim->loops, pass);
        if (loops > 0 && (now - first) / period >= loops)
            t = (loops * period + phase) % period + first; // (held on the last loop's end)
        int key = count - 1;
        while (key >= 0 && t < core::at(keys, key).time)
            --key;
        core::at(textures, pass) = core::at(keys, key).texture;
    }
}

// 0x004D1DC0
void MaterialRef::update_uv_wibbles()
{
    if (!has_uv_wibble_)
        return;
    const float t = wibble_time(frame_clock::get_time());
    for (uint32_t pass = 0; pass < pass_count_; ++pass) {
        if (!(core::at(pass_flags_, pass) & kPassUvWibble) || (core::at(pass_flags_, pass) & kPassExplicitUv))
            continue;
        UvOscillator* w = core::at(uv_wibble_, pass);
        const float u = wibble_offset(t, core::at(w->frequency, 0), core::at(w->phase, 0), core::at(w->amplitude, 0), core::at(w->velocity, 0), false);
        const float v = wibble_offset(t, core::at(w->frequency, 1), core::at(w->phase, 1), core::at(w->amplitude, 1), core::at(w->velocity, 1), true);
        core::at(w->offset, 0) = u;
        core::at(w->offset, 1) = v;
    }
}

// 0x004D1F00
void MaterialRef::hand_in()
{
    if (renderer().submit_locked())
        return;
    choose_blend_mode(core::at(blend_modes_, 0));
    set_common_states(this);
    for (uint32_t pass = 0; pass < pass_count_; ++pass) {
        if (renderer().skipped_passes() & (1u << (pass & 31)))
            continue;
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison): compares the bits, as the original
        if (!renderer().pixel_shader_changed() && std::memcmp(core::at(pass_color_, pass).data(), std::span<float>(renderer().ps_constant_bank()).subspan(4 * static_cast<size_t>(pass)).data(), 16) != 0)
            renderer().set_pixel_shader_changed(true);
        const TextureImage* texture = core::at(textures, pass);
        if (!texture) {
            set_texture(static_cast<int>(pass), nullptr);
            continue;
        }
        set_texture(static_cast<int>(pass), texture->texture);
        change_phase(kAddressMode0 + static_cast<int>(pass), core::at(address_modes_, pass));
        change_phase(kMipLodBias0 + static_cast<int>(pass), core::at(mip_lod_bias_, pass));
        set_border_color(this, pass, pass);
        uint32_t flags = core::at(pass_flags_, pass);
        if (flags & kPassBumpEnv)
            change_bump_map_matrix(pass);
        flags = core::at(pass_flags_, pass);
        auto texture_matrix = static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0 + pass);
        if ((flags & kPassEnvironment) && renderer().near_camera()) {
            auto& matrix = pc_material_state().environment_matrix_pass_;
            core::at(matrix, 0) = static_cast<float>(core::at(core::at(reflection_tiling_, pass), 0) * 0.5);
            core::at(matrix, 5) = static_cast<float>(core::at(core::at(reflection_tiling_, pass), 1) * 0.5);
            change_phase(kLocalViewer, 1);
            renderer().d3d_device()->SetTransform(texture_matrix, d3d::matrix(matrix.data()));
            device_stage_state(pass, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
            device_stage_state(pass, D3DTSS_TEXCOORDINDEX, pass | D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
        } else if (flags & kPassUvWibble) {
            device_stage_state(pass, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
            device_stage_state(pass, D3DTSS_TEXCOORDINDEX, pass);
            auto& matrix = pc_material_state().wibble_matrix_pass_;
            fill_wibble_matrix(matrix, core::at(uv_wibble_, pass));
            renderer().d3d_device()->SetTransform(texture_matrix, d3d::matrix(matrix.data()));
        } else {
            if (view_setup().billboard_flag()) { // (the billboard flag: a static matrix's 2x2 part reset)
                auto& m = pc_material_state().billboard_uv_matrix_;
                core::at(m, 0) = 1.0f;
                core::at(m, 1) = 0;
                core::at(m, 3) = 0;
                core::at(m, 4) = 0;
                core::at(m, 5) = 1.0f;
                core::at(m, 7) = 0;
            }
            DWORD transform_flags = 0, coordinates = 0;
            renderer().d3d_device()->GetTextureStageState(pass, D3DTSS_TEXTURETRANSFORMFLAGS, &transform_flags);
            renderer().d3d_device()->GetTextureStageState(pass, D3DTSS_TEXCOORDINDEX, &coordinates);
            if (transform_flags)
                device_stage_state(pass, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
            if (coordinates != pass)
                device_stage_state(pass, D3DTSS_TEXCOORDINDEX, pass);
        }
    }
    if (renderer().pixel_shader_changed())
        std::memcpy(renderer().ps_constant_bank().data(), pass_color_.data(), pass_count_ * 16);
}

// 0x004D23E0
void MaterialRef::submit_states()
{
    if (renderer().submit_locked())
        return;
    set_common_states(this);
}

// 0x004D24D0
void MaterialRef::submit_pass(uint32_t pass, const float* color)
{
    if (renderer().submit_locked())
        return;
    choose_blend_mode(core::at(blend_modes_, pass));
    if (!(renderer().skipped_passes() & (1u << (pass & 31)))) {
        const TextureImage* texture = core::at(textures, pass);
        if (!texture) {
            set_texture(0, nullptr);
        } else {
            set_texture(0, texture->texture);
            change_phase(kAddressMode0, core::at(address_modes_, pass));
            change_phase(kMipLodBias0, core::at(mip_lod_bias_, pass));
            set_border_color(this, pass, pass);
            if (core::at(pass_flags_, pass) & kPassBumpEnv)
                change_bump_map_matrix(0);
            const uint32_t flags = core::at(pass_flags_, pass);
            if ((flags & kPassEnvironment) && renderer().near_camera()) {
                auto& matrix = pc_material_state().environment_matrix_;
                core::at(matrix, 0) = static_cast<float>(core::at(core::at(reflection_tiling_, pass), 0) * 0.5);
                core::at(matrix, 5) = static_cast<float>(core::at(core::at(reflection_tiling_, pass), 1) * 0.5);
                change_phase(kLocalViewer, 1);
                render::set_transform(D3DTS_TEXTURE0, matrix.data());
                set_texture_stage_state(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
                set_texture_stage_state(0, D3DTSS_TEXCOORDINDEX, pass | D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
            } else if (flags & kPassUvWibble) {
                device_stage_state(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
                device_stage_state(0, D3DTSS_TEXCOORDINDEX, pass);
                auto& matrix = pc_material_state().wibble_matrix_;
                fill_wibble_matrix(matrix, core::at(uv_wibble_, pass));
                renderer().d3d_device()->SetTransform(D3DTS_TEXTURE0, d3d::matrix(matrix.data()));
            } else {
                DWORD transform_flags = 0, coordinates = 0;
                renderer().d3d_device()->GetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, &transform_flags);
                renderer().d3d_device()->GetTextureStageState(0, D3DTSS_TEXCOORDINDEX, &coordinates);
                if (transform_flags)
                    set_texture_stage_state(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
                if (coordinates != pass)
                    set_texture_stage_state(0, D3DTSS_TEXCOORDINDEX, pass);
                const uint32_t blend = core::at(blend_modes_, pass);
                if (blend == 0xC || blend == 0xD) { // (blending with the pass before: its texture on stage 1)
                    set_texture_stage_state(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
                    if (static_cast<int>(pass) > 0) {
                        set_texture_stage_state(1, D3DTSS_TEXCOORDINDEX, pass - 1);
                        set_texture(1, core::at(textures, pass - 1)->texture);
                        change_phase(kAddressMode0 + 1, core::at(address_modes_, pass - 1));
                        change_phase(kMipLodBias0 + 1, core::at(mip_lod_bias_, pass - 1));
                    } else {
                        set_texture_stage_state(1, D3DTSS_TEXCOORDINDEX, pass);
                        set_texture(1, core::at(textures, pass)->texture);
                    }
                }
            }
        }
    }
    if (renderer().pixel_shader_changed())
        std::memcpy(renderer().ps_constant_bank().data(), pass_color_.data(), pass_count_ * 16);

    // the stages' colour and alpha operations
    if (static_cast<int8_t>(core::at(pass_flags_, pass) & 0xFF) < 0)
        color = nullptr; // (the pass's colour isn't to be changed)
    const std::array<float, 4>& own = core::at(pass_color_, pass);
    const bool alpha_from_texture = (core::at(pass_flags_, pass) & kPassTextureAlpha) != 0;
    const bool blends_with_previous = core::at(blend_modes_, pass) == 0xC || core::at(blend_modes_, pass) == 0xD;
    const uint32_t half = 0x3F000000; // 0.5: the colour unchanged
    // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison): compares the bits, as the original
    if (std::memcmp(&core::at(own, 0), &half, 4) == 0 && std::memcmp(&core::at(own, 1), &half, 4) == 0 &&
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison): compares the bits, as the original
        std::memcmp(&core::at(own, 2), &half, 4) == 0 && !color) {
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
        const std::span<const float, 3> factor = color ? std::span<const float, 3>(color, 3) : std::span<const float>(own).first<3>();
        const uint32_t r = factor_channel(core::at(factor, 0));
        const uint32_t g = factor_channel(core::at(factor, 1));
        const uint32_t b = factor_channel(core::at(factor, 2));
        renderer().d3d_device()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFF000000u | r << 16 | g << 8 | b);
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
