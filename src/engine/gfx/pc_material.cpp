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

#include "engine/memory/manager.h"
#include "base/maths/trig.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/gfx/primitive_states.h"
#include "engine/gfx/render_state.h"
#include "engine/sys/stopwatch.h"
#include "base/core/checked_index.h"
#include "engine/gfx/bitmap_texture.h"
#include <bit>
#include "platform/d3d_matrix.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/view_setup.h"
#include "base/maths/mat4.h"
#include "base/core/same_bits.h"

namespace render {

constexpr std::size_t kBillboardUvMatrixFloats = 8;
constexpr float kEnvironmentScale = 0.5F; // the environment map: -1..1 mapped to 0..1
constexpr float kEnvironmentOffset = 0.5F;
constexpr int kBitsPerByte = 8;
constexpr float kMillisecondsToSeconds = 0.001F;
constexpr double kWibbleWrap = 8.0;
constexpr uint32_t kBorderUnset = 0xE0000000;
constexpr int kUvScaleV = 5; // the UV matrix: the v scale, then the offsets in the next row
constexpr int kUvOffsetU = 8;
constexpr int kUvOffsetV = 9;
constexpr float kByteMaxFloat = 255.0F;
constexpr int kLowByteMask = 0xFF;
constexpr float kReflectionTiling = 3.0F;
constexpr int kPassBitMask = 31;
constexpr std::size_t kPassColourFloats = 4; // a pass colour in the pixel shader constants
constexpr double kHalf = 0.5;
constexpr uint32_t kOpaqueAlphaBits = 0xFF000000U;
constexpr int kByte1Shift = 8;
constexpr int kByte2Shift = 16;

namespace {
// NOT ORIGINAL grouping - pc_material.cpp's state (the original's globals). Made on first use, never destroyed
// (pc_material_state()).
struct PcMaterialState {
    std::array<float, kBillboardUvMatrixFloats> billboard_uv_matrix_{{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F}};
    std::array<float, maths::kMatrixFloats> wibble_matrix_pass_{{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F}};
    std::array<float, maths::kMatrixFloats> wibble_matrix_{{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F}};
    std::array<float, maths::kMatrixFloats> environment_matrix_{{kEnvironmentScale, 0.0F, 0.0F, 0.0F, 0.0F,
                                                                 -kEnvironmentScale, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
                                                                 kEnvironmentOffset, kEnvironmentOffset}};
    // the environment pass's texture matrix (its scales set per pass)
    std::array<float, maths::kMatrixFloats> environment_matrix_pass_{{kEnvironmentScale, 0.0F, 0.0F, 0.0F, 0.0F,
                                                                      -kEnvironmentScale, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
                                                                      0.0F, kEnvironmentOffset, kEnvironmentOffset}};
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
    const double fraction = static_cast<double>(num) / den;
    const double rest = 1.0 - fraction;
    uint32_t color = 0;
    for (int k = 0; k < 4; ++k) {
        const auto value = static_cast<uint32_t>(maths::truncate_to_i64((core::at(to, core::at(kOrder, k)) * fraction) +
                                                                        (core::at(from, core::at(kOrder, k)) * rest)));
        color = (k != 0) ? (color << kBitsPerByte | value) : value;
    }
    return color;
}

// The time in seconds the UV wibbles use: the milliseconds (unsigned) times 0.001, stored as a float.
float wibble_time(uint32_t milliseconds) { return static_cast<float>(static_cast<double>(milliseconds) * kMillisecondsToSeconds); }

// One axis of a UV wibble's offset: the phase-shifted sine times the amplitude plus the velocity's travel, plus 8,
// less its whole part rounded down to a multiple of 16 - then 8 back the other way (added below 0, taken off
// otherwise). The v axis is stored as a float before that last step, the u axis isn't.
float wibble_offset(float time, float frequency, float phase, float amplitude, float velocity, bool store_first)
{
    double value = (std::sin((static_cast<double>(time) * frequency) + phase) * amplitude) +
                   (static_cast<double>(time) * velocity) + kWibbleWrap;
    const auto whole = static_cast<int32_t>(static_cast<uint32_t>(maths::truncate_to_i64(value)) & 0xFFFFFFF0U);
    value -= whole;
    if (store_first) {
        value = static_cast<float>(value);
    }
    return static_cast<float>(value < 0.0 ? value + kWibbleWrap : value - kWibbleWrap);
}

// What both set-ups share: the alpha reference, culling, depth bias, the animations, and the specular material
// (pass 0's flag 0x100) - or none, once, when the last material had it (0x00786E4C).
void set_common_states(MaterialRef* material)
{
    change_phase(RenderStateId::kPhaseAlphaTest, material->alpha_ref());
    change_phase(RenderStateId::kCullMode, (material->two_sided() != 0U) ? D3DCULL_NONE : D3DCULL_CW);
    change_phase(RenderStateId::kDepthBias, material->depth_bias());
    material->update_uv_wibbles();
    material->update_color_anims();
    material->update_texture_anims();
    const uint32_t specular_on = renderer().specular_state();
    D3DMATERIAL9 specular;
    if ((core::at(material->pass_flags(), 0) & kPassSpecular) != 0U) {
        if (specular_on != 0U) {
            return;
        }
        change_phase(RenderStateId::kSpecular, 1);
        std::memset(&specular, 0, sizeof(specular));
        specular.Specular.r = core::at(material->specular(), 0);
        specular.Specular.g = core::at(material->specular(), 1);
        specular.Specular.b = core::at(material->specular(), 2);
        specular.Specular.a = 1.0F;
        specular.Power = material->specular_power();
    } else {
        if (specular_on == 0U) {
            return;
        }
        change_phase(RenderStateId::kSpecular, 0);
        std::memset(&specular, 0, sizeof(specular));
    }
    renderer().d3d_device()->SetMaterial(&specular);
}

// A material pass and the texture stage it uses.
struct PassStage {
    uint32_t pass{};
    uint32_t stage{};
};

// The pass's border colour (0x00786EA4, a word a stage): 0xE0000000 with flag 0x200, else 0.
void set_border_color(const MaterialRef* material, PassStage pass_stage)
{
    const auto pass = pass_stage.pass;
    const auto stage = pass_stage.stage;
    const uint32_t border = renderer().border_colour(stage);
    if ((core::at(material->pass_flags(), pass) & kPassBorder) != 0U) {
        if (border != kBorderUnset) {
            renderer().set_border_colour(stage, kBorderUnset);
        }
    } else if (border != 0U) {
        renderer().set_border_colour(stage, 0);
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
void fill_wibble_matrix(std::span<float, maths::kMatrixFloats> matrix, const UvOscillator* wibble)
{
    core::at(matrix, 0) = core::at(wibble->matrix(), 0);
    core::at(matrix, 1) = core::at(wibble->matrix(), 1);
    core::at(matrix, kUvScaleV) = core::at(wibble->matrix(), 0);
    core::at(matrix, 4) = -core::at(wibble->matrix(), 1);
    core::at(matrix, kUvOffsetU) = core::at(wibble->offset(), 0);
    core::at(matrix, kUvOffsetV) = core::at(wibble->offset(), 1);
}

uint32_t factor_channel(float value)
{
    return static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(value) * kByteMaxFloat)) & kLowByteMask;
}

} // namespace

// 0x004D1A90
MaterialRef::MaterialRef()
{
    for (int pass = 0; pass < 4; ++pass) {
        core::at(unknownc0_, pass) = kClampBothWays;
        core::at(pass_flags_, pass) = 0;
        core::at(textures_, pass) = nullptr;
        core::at(uv_wibble_, pass) = nullptr;
        core::at(core::at(reflection_tiling_, pass), 0) = kReflectionTiling;
        core::at(core::at(reflection_tiling_, pass), 1) = kReflectionTiling;
    }
}

// 0x004D2BD0
MaterialRef::~MaterialRef()
{
    if (color_anims_) {
        for (const ColorAnim& anim : color_anim_run()) {
            memory::Hub::dealloc(anim.keys);
        }
        memory::Hub::dealloc(color_anims_);
    }
    if (anim_colors_) {
        memory::Hub::dealloc(anim_colors_);
    }
    for (UvOscillator* wibble : uv_wibble_) {
        if (wibble) {
            memory::Hub::dealloc(wibble);
        }
    }
    if (texture_anim_) {
        for (TextureKey* keys : texture_anim_->keys) {
            memory::Hub::dealloc(keys);
        }
        memory::Hub::dealloc(texture_anim_);
    }
}

// 0x004D1D90
uint32_t MaterialRef::texture_alpha_passes() const
{
    uint32_t mask = 0;
    for (uint32_t pass = 0; pass < pass_count_; ++pass) {
        if ((core::at(pass_flags_, pass) & kPassTextureAlpha) != 0U) {
            mask |= 1U << (pass & kPassBitMask);
        }
    }
    return mask;
}

// 0x004D1B40
void MaterialRef::update_color_anims()
{
    if ((core::at(pass_flags_, 0) & kPassColorAnim) == 0U) {
        return;
    }
    for (uint32_t i = 0; i < color_anim_count_; ++i) {
        const ColorAnim& anim = core::at(color_anim_run(), i);
        const std::span<ColorKey> keys = key_run(anim);
        const int first = core::at(keys, 0).time;
        const int period = core::at(keys, anim.key_count_ - 1).time - first;
        const int time_now = (period != 0) ? ((anim.phase_ + renderer().step_seconds()) % period) + first : 1;
        int key = static_cast<int>(anim.key_count_) - 1;
        while (key >= 0 && time_now < core::at(keys, key).time) {
            --key;
        }
        const ColorKey& from = core::at(keys, key);
        const ColorKey& to = core::at(keys, key + 1);
        core::at(anim_colour_run(), i) =
            blend_key_colors(time_now - from.time, to.time - from.time, from.rgba, to.rgba);
    }
}

// 0x004D1CC0
void MaterialRef::update_texture_anims()
{
    if (!has_texture_anim_) {
        return;
    }
    const int now = static_cast<int>(frame_clock::get_time());
    const TextureAnim* anim = texture_anim_;
    for (int pass = 0; pass < 4; ++pass) {
        if ((core::at(pass_flags_, pass) & kPassTextureAnim) == 0U) {
            continue;
        }
        const std::span<TextureKey> keys = key_run(*anim, static_cast<size_t>(pass));
        const int count = static_cast<int>(core::at(anim->key_counts_, pass));
        const int first = core::at(keys, 0).time;
        const int period = core::at(keys, count - 1).time - first;
        const int phase = static_cast<int>(core::at(anim->phases, pass));
        int key_time = ((now + phase) % period) + first;
        const int loops = core::at(anim->loops, pass);
        if (loops > 0 && (now - first) / period >= loops) {
            key_time = (((loops * period) + phase) % period) + first; // (held on the last loop's end)
        }
        int key = count - 1;
        while (key >= 0 && key_time < core::at(keys, key).time) {
            --key;
        }
        core::at(textures_, pass) = core::at(keys, key).texture;
    }
}

// 0x004D1DC0
void MaterialRef::update_uv_wibbles()
{
    if (!has_uv_wibble_) {
        return;
    }
    const float wave_time = wibble_time(frame_clock::get_time());
    for (uint32_t pass = 0; pass < pass_count_; ++pass) {
        if (((core::at(pass_flags_, pass) & kPassUvWibble) == 0U) ||
            ((core::at(pass_flags_, pass) & kPassExplicitUv) != 0U)) {
            continue;
        }
        UvOscillator* w = core::at(uv_wibble_, pass);
        const float u = wibble_offset(wave_time, core::at(w->frequency(), 0), core::at(w->phase(), 0),
                                      core::at(w->amplitude(), 0), core::at(w->velocity(), 0), false);
        const float v = wibble_offset(wave_time, core::at(w->frequency(), 1), core::at(w->phase(), 1),
                                      core::at(w->amplitude(), 1), core::at(w->velocity(), 1), true);
        core::at(w->offset(), 0) = u;
        core::at(w->offset(), 1) = v;
    }
}

// 0x004D1F00
void MaterialRef::hand_in()
{
    if (renderer().submit_locked() != 0U) {
        return;
    }
    choose_blend_mode(core::at(blend_modes_, 0));
    set_common_states(this);
    for (uint32_t pass = 0; pass < pass_count_; ++pass) {
        if ((renderer().skipped_passes() & (1U << (pass & kPassBitMask))) != 0U) {
            continue;
        }
        if ((renderer().pixel_shader_changed() == 0U) &&
            !core::same_bits(core::at(pass_color_, pass),
                             std::span<const float>(renderer().ps_constants())
                                 .subspan(kPassColourFloats * static_cast<size_t>(pass), kPassColourFloats))) {
            renderer().set_pixel_shader_changed(1U);
        }
        const TextureImage* texture = core::at(textures_, pass);
        if (!texture) {
            set_texture(static_cast<int>(pass), nullptr);
            continue;
        }
        set_texture(static_cast<int>(pass), texture->texture);
        change_phase(address_mode_state(static_cast<int>(pass)), core::at(address_modes_, pass));
        change_phase(mip_lod_bias_state(static_cast<int>(pass)), core::at(mip_lod_bias_, pass));
        set_border_color(this, {.pass = pass, .stage = pass});
        uint32_t flags = core::at(pass_flags_, pass);
        if ((flags & kPassBumpEnv) != 0U) {
            change_bump_map_matrix(pass);
        }
        flags = core::at(pass_flags_, pass);
        auto texture_matrix = static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0 + pass);
        if (((flags & kPassEnvironment) != 0U) && renderer().near_camera()) {
            auto& matrix = pc_material_state().environment_matrix_pass_;
            core::at(matrix, 0) = static_cast<float>(core::at(core::at(reflection_tiling_, pass), 0) * kHalf);
            core::at(matrix, kUvScaleV) = static_cast<float>(core::at(core::at(reflection_tiling_, pass), 1) * kHalf);
            change_phase(RenderStateId::kLocalViewer, 1);
            renderer().d3d_device()->SetTransform(texture_matrix, d3d::matrix(matrix.data()));
            device_stage_state(pass, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
            device_stage_state(pass, D3DTSS_TEXCOORDINDEX, pass | D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
        } else if ((flags & kPassUvWibble) != 0U) {
            device_stage_state(pass, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
            device_stage_state(pass, D3DTSS_TEXCOORDINDEX, pass);
            auto& matrix = pc_material_state().wibble_matrix_pass_;
            fill_wibble_matrix(matrix, core::at(uv_wibble_, pass));
            renderer().d3d_device()->SetTransform(texture_matrix, d3d::matrix(matrix.data()));
        } else {
            if (view_setup().billboard_flag() != 0U) { // (the billboard flag: a static matrix's 2x2 part reset)
                auto& uv_matrix = pc_material_state().billboard_uv_matrix_;
                core::at(uv_matrix, 0) = 1.0F;
                core::at(uv_matrix, 1) = 0;
                core::at(uv_matrix, 3) = 0;
                core::at(uv_matrix, 4) = 0;
                core::at(uv_matrix, kUvScaleV) = 1.0F;
                core::at(uv_matrix, maths::kMatYw) = 0;
            }
            DWORD transform_flags = 0;
            DWORD coordinates = 0;
            renderer().d3d_device()->GetTextureStageState(pass, D3DTSS_TEXTURETRANSFORMFLAGS, &transform_flags);
            renderer().d3d_device()->GetTextureStageState(pass, D3DTSS_TEXCOORDINDEX, &coordinates);
            if (transform_flags != 0U) {
                device_stage_state(pass, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
            }
            if (coordinates != pass) {
                device_stage_state(pass, D3DTSS_TEXCOORDINDEX, pass);
            }
        }
    }
    if (renderer().pixel_shader_changed() != 0U) {
        renderer().load_pass_colours(pass_color_, pass_count_);
    }
}

// 0x004D23E0
void MaterialRef::submit_states()
{
    if (renderer().submit_locked() != 0U) {
        return;
    }
    set_common_states(this);
}

// A pass's own texture coordinates, untransformed; blending with the pass before: its texture on stage 1.
void MaterialRef::use_pass_coordinates(uint32_t pass)
{
    DWORD transform_flags = 0;
    DWORD coordinates = 0;
    renderer().d3d_device()->GetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, &transform_flags);
    renderer().d3d_device()->GetTextureStageState(0, D3DTSS_TEXCOORDINDEX, &coordinates);
    if (transform_flags != 0U) {
        set_texture_stage_state(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    }
    if (coordinates != pass) {
        set_texture_stage_state(0, D3DTSS_TEXCOORDINDEX, pass);
    }
    const uint32_t blend = core::at(blend_modes_, pass);
    if (blend == kBlendModeLerpPrevious ||
        blend == kBlendModeLerpFromPrevious) { // (blending with the pass before: its texture on stage 1)
        set_texture_stage_state(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        if (static_cast<int>(pass) > 0) {
            set_texture_stage_state(1, D3DTSS_TEXCOORDINDEX, pass - 1);
            set_texture(1, core::at(textures_, pass - 1)->texture);
            change_phase(RenderStateId::kAddressMode1, core::at(address_modes_, pass - 1));
            change_phase(RenderStateId::kMipLodBias1, core::at(mip_lod_bias_, pass - 1));
        } else {
            set_texture_stage_state(1, D3DTSS_TEXCOORDINDEX, pass);
            set_texture(1, core::at(textures_, pass)->texture);
        }
    }
}

// 0x004D24D0
void MaterialRef::submit_pass(uint32_t pass, const float* color)
{
    if (renderer().submit_locked() != 0U) {
        return;
    }
    choose_blend_mode(core::at(blend_modes_, pass));
    if ((renderer().skipped_passes() & (1U << (pass & kPassBitMask))) == 0U) {
        const TextureImage* texture = core::at(textures_, pass);
        if (!texture) {
            set_texture(0, nullptr);
        } else {
            set_texture(0, texture->texture);
            change_phase(RenderStateId::kAddressMode0, core::at(address_modes_, pass));
            change_phase(RenderStateId::kMipLodBias0, core::at(mip_lod_bias_, pass));
            set_border_color(this, {.pass = pass, .stage = pass});
            if ((core::at(pass_flags_, pass) & kPassBumpEnv) != 0U) {
                change_bump_map_matrix(0);
            }
            const uint32_t flags = core::at(pass_flags_, pass);
            if (((flags & kPassEnvironment) != 0U) && renderer().near_camera()) {
                auto& matrix = pc_material_state().environment_matrix_;
                core::at(matrix, 0) = static_cast<float>(core::at(core::at(reflection_tiling_, pass), 0) * kHalf);
                core::at(matrix, kUvScaleV) = static_cast<float>(core::at(core::at(reflection_tiling_, pass), 1) * kHalf);
                change_phase(RenderStateId::kLocalViewer, 1);
                render::set_transform(D3DTS_TEXTURE0, matrix.data());
                set_texture_stage_state(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
                set_texture_stage_state(0, D3DTSS_TEXCOORDINDEX, pass | D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
            } else if ((flags & kPassUvWibble) != 0U) {
                device_stage_state(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
                device_stage_state(0, D3DTSS_TEXCOORDINDEX, pass);
                auto& matrix = pc_material_state().wibble_matrix_;
                fill_wibble_matrix(matrix, core::at(uv_wibble_, pass));
                renderer().d3d_device()->SetTransform(D3DTS_TEXTURE0, d3d::matrix(matrix.data()));
            } else {
                use_pass_coordinates(pass);
            }
        }
    }
    if (renderer().pixel_shader_changed() != 0U) {
        renderer().load_pass_colours(pass_color_, pass_count_);
    }

    // the stages' colour and alpha operations
    if (static_cast<int8_t>(core::at(pass_flags_, pass) & kLowByteMask) < 0) {
        color = nullptr; // (the pass's colour isn't to be changed)
    }
    const std::array<float, 4>& own = core::at(pass_color_, pass);
    const bool alpha_from_texture = (core::at(pass_flags_, pass) & kPassTextureAlpha) != 0;
    const bool blends_with_previous = core::at(blend_modes_, pass) == 0xC || core::at(blend_modes_, pass) == 0xD;
    const uint32_t half = 0x3F000000; // 0.5: the colour unchanged
    if (core::has_bits(core::at(own, 0), half) && core::has_bits(core::at(own, 1), half) && core::has_bits(core::at(own, 2), half) &&
        !color) {
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
        const uint32_t red = factor_channel(core::at(factor, 0));
        const uint32_t green = factor_channel(core::at(factor, 1));
        const uint32_t blue = factor_channel(core::at(factor, 2));
        renderer().d3d_device()->SetRenderState(D3DRS_TEXTUREFACTOR,
                                                kOpaqueAlphaBits | red << kByte2Shift | green << kByte1Shift | blue);
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
