// Full-screen effects: see screen_fx.h.

#include "engine/gfx/screen_fx.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <array>

#include "engine/gfx/engine_effects.h"
#include "engine/gfx/primitive_states.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/shader_slots.h"
#include "engine/core/checked_index.h"
#include "engine/core/packed_colour.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/effects_state.h"
#include "engine/gfx/view_setup.h"
#include "engine/gfx/engine_parts.h"
#include "engine/maths/vec2.h"
#include "engine/gfx/graphics_options_d3d.h"

namespace render {

constexpr float kBlurWeightEdge = 0.16666667F; // the blur's four taps: weights and offsets
constexpr float kBlurWeightMiddle = 0.33333334F;
constexpr float kBlurOffsetOuter = 0.022857143F;
constexpr float kBlurOffsetInner = 0.005714286F;
constexpr double kBlurWeightScale = 128.0;
constexpr uint32_t kBytePadBits = 0xFFFF8000;
constexpr int kByte1Shift = 8;
constexpr double kHalf = 0.5;
constexpr uint32_t kNeutralColour = 0x80808080;
constexpr int kHalfByte = 128;
constexpr float kHalfF = 0.5F;
constexpr int kScreenCopyBlurA = 7;
constexpr int kScreenCopyBlurB = 8;

namespace {
constexpr bool kBlurSecondPass{true};
// NOT ORIGINAL grouping - screen_fx.cpp's state (the original's globals). Made on first use, never destroyed
// (screen_fx_state()).
struct ScreenFxState {
    std::array<float, 4> blur_tap_weights_{{kBlurWeightEdge, kBlurWeightMiddle, kBlurWeightMiddle, kBlurWeightEdge}};
    std::array<float, 4> blur_tap_offsets_{{-kBlurOffsetOuter, -kBlurOffsetInner, kBlurOffsetInner, kBlurOffsetOuter}};
};

ScreenFxState& screen_fx_state()
{
    static core::NoDestroy<ScreenFxState> state;
    return state.get();
}

IDirect3DTexture9* screen_copy(int index) { return core::at(renderer().screen_textures(), index); }
IDirect3DTexture9* current_screen_copy() { return screen_copy(renderer().screen_copy_index()); }

void* get_shader(int slot) { return core::at(pixel_shaders(), slot); }

// Fog on or off (not rewritten yet; the request kept, the device's state following).
void change_fog(bool on) { engine::enable_fog(on); }

// The renderer's draw counters.
void count_draw(int triangles)
{
    renderer().count_draw(triangles);
}

// An unsigned size as the x87 loads it (FILD, plus 2^32 when the top bit is set).
double as_unsigned(uint32_t value) { return static_cast<double>(value); }

void set_target(IDirect3DTexture9* texture, IDirect3DSurface9* depth)
{
    // NOT ORIGINAL - [Graphics] msaa (engine/gfx/graphics_options.h): a full-size screen copy drawn through the
    // multisampled scene surface.
    if (graphics_options::set_scene_target(texture)) {
        return;
    }
    IDirect3DSurface9* surface = nullptr;
    texture->GetSurfaceLevel(0, &surface);
    renderer().d3d_device()->SetRenderTarget(0, surface);
    renderer().d3d_device()->SetDepthStencilSurface(depth);
    surface->Release();
}

struct TexturedVertex {
    float x, y, z, rhw;
    uint32_t colour;
    float u, v;
};
struct FlatVertex {
    float x, y, z, rhw;
    uint32_t colour;
};

// The blur's four taps (texel offsets and weights) at 0x0067EF98 / 0x0067EF88.
float tap_offset(int tap) { return core::at(screen_fx_state().blur_tap_offsets_, tap); }
float tap_weight(int tap) { return core::at(screen_fx_state().blur_tap_weights_, tap); }

// A grey of 0-255 at half alpha: 0x80 then the value three times (from _ftol: a bigger value spills over).
uint32_t tap_colour(float weight)
{
    auto w = static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(weight) * kBlurWeightScale)); // 0x00647B2C
    return (((w | kBytePadBits) << kByte1Shift | w) << kByte1Shift) | w;
}

} // namespace

// 0x004DE5E0
void draw_screen_quad(uint32_t blend, void* target, void* source, maths::Vec2 uv_offset, uint32_t colour, bool shader)
{
    const auto du = uv_offset.x;
    const auto dv = uv_offset.y;
    const bool fog_was_on = renderer().fog_active() != 0;
    change_fog(false);
    auto* target_texture = static_cast<IDirect3DTexture9*>(target);
    set_target(target_texture, nullptr);
    D3DSURFACE_DESC target_desc;
    target_texture->GetLevelDesc(0, &target_desc);
    set_texture(0, nullptr);
    renderer().draw_device()->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    renderer().draw_device()->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    change_phase(RenderStateId::kAddressMode0, kClampBothWays);
    change_phase(RenderStateId::kPhaseAlphaTest, 0);
    auto width = static_cast<float>(as_unsigned(target_desc.Width));
    auto height = static_cast<float>(as_unsigned(target_desc.Height));
    choose_blend_mode(blend);
    if (source) {
        auto* source_texture = static_cast<IDirect3DTexture9*>(source);
        D3DSURFACE_DESC source_desc;
        source_texture->GetLevelDesc(0, &source_desc); // (not used: half a texel of the target)
        auto half_x = static_cast<float>(kHalf / as_unsigned(target_desc.Width));
        auto half_y = static_cast<float>(kHalf / as_unsigned(target_desc.Height));
        set_texture(0, source);
        if (shader) {
            set_pixel_shader(get_shader(kPixelShader950), nullptr);
        }
        set_vertex_format(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        auto right = static_cast<float>(static_cast<double>(du) + 1.0);
        auto u0 = static_cast<float>(static_cast<double>(half_x) + du);
        auto v0 = static_cast<float>(static_cast<double>(half_y) + dv);
        auto u1 = static_cast<float>(static_cast<double>(right) + half_x);
        auto v1 = static_cast<float>((static_cast<double>(dv) + 1.0) + half_y);
        std::array<TexturedVertex, 4> quad = {{
            {.x = 0.0F, .y = 0.0F, .z = 0.0F, .rhw = 1.0F, .colour = colour, .u = u0, .v = v0},
            {.x = width, .y = 0.0F, .z = 0.0F, .rhw = 1.0F, .colour = colour, .u = u1, .v = v0},
            {.x = 0.0F, .y = height, .z = 0.0F, .rhw = 1.0F, .colour = colour, .u = u0, .v = v1},
            {.x = width, .y = height, .z = 0.0F, .rhw = 1.0F, .colour = colour, .u = u1, .v = v1},
        }};
        renderer().draw_device()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad.data(), sizeof(TexturedVertex));
    } else {
        set_pixel_shader(get_shader(kPixelShader94c), nullptr);
        // (the vertex format is left as it was)
        std::array<FlatVertex, 4> quad = {{
            {.x = 0.0F, .y = 0.0F, .z = 0.0F, .rhw = 1.0F, .colour = colour},
            {.x = width, .y = 0.0F, .z = 0.0F, .rhw = 1.0F, .colour = colour},
            {.x = 0.0F, .y = height, .z = 0.0F, .rhw = 1.0F, .colour = colour},
            {.x = width, .y = height, .z = 0.0F, .rhw = 1.0F, .colour = colour},
        }};
        renderer().draw_device()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad.data(), sizeof(FlatVertex));
    }
    count_draw(2);
    change_fog(fog_was_on);
}

// 0x004DEA80
void begin_screen_effects()
{
    effects_state().set_screen_copy_in_use(false);
    IDirect3DSurface9* surface = nullptr;
    if (effects_state().screen_blur() > 2) { // the motion blur
        current_screen_copy()->GetSurfaceLevel(0, &surface);
        // (NOT ORIGINAL: or the multisampled scene surface - [Graphics] msaa, engine/gfx/graphics_options.h)
        if (!graphics_options::set_scene_target(current_screen_copy())) {
            renderer().d3d_device()->SetRenderTarget(0, surface);
            renderer().d3d_device()->SetDepthStencilSurface(renderer().z_buffer_surface());
        }
    } else {
        if (!effects_state().bloom_on() || !renderer().shaders_available()) { // no bloom (or no pixel shaders)
            return;
        }
        current_screen_copy()->GetSurfaceLevel(0, &surface);
        // (NOT ORIGINAL: or the multisampled scene surface - [Graphics] msaa, engine/gfx/graphics_options.h)
        if (!graphics_options::set_scene_target(current_screen_copy())) {
            set_render_target_and_depth({.surface = surface, .depth = renderer().z_buffer_surface()});
        }
    }
    // The clear colour: the fog's with distance clipping and fog on (0x007D643A, 0x007D6436), else 0x00786CC0.
    const uint32_t colour = (view_setup().fog_setting() && view_setup().distance_clipping()) ? engine_parts().fog_colour() : renderer().render_camera().background_colour_;
    renderer().d3d_device()->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, colour, 1.0F, 0);
    surface->Release();
    effects_state().set_screen_copy_in_use(true);
}

// 0x004DE980
void copy_screen_quarter()
{
    if (!effects_state().screen_copy_in_use()) {
        return;
    }
    IDirect3DTexture9* source = current_screen_copy();
    IDirect3DTexture9* quarter = screen_copy(2);
    // NOT ORIGINAL - [Graphics] msaa (engine/gfx/graphics_options.h): the frame so far resolved into the copy first.
    graphics_options::resolve_scene_target(source);
    change_phase(RenderStateId::kDepthBias, 0);
    change_phase(RenderStateId::kLighting, 0);
    change_phase(RenderStateId::kZWrite, 0);
    change_phase(RenderStateId::kZTest, 0);
    change_phase(RenderStateId::kAlphaTest, 0);
    change_phase(RenderStateId::kAlphaBlend, 0);
    change_phase(RenderStateId::kCullMode, D3DCULL_NONE);
    draw_screen_quad(0, quarter, source, {.x = 0.0F, .y = 0.0F}, kNeutralColour, true);
    set_target(current_screen_copy(), renderer().z_buffer_surface());
    change_phase(RenderStateId::kZWrite, 1);
    change_phase(RenderStateId::kZTest, 1);
    change_phase(RenderStateId::kAlphaTest, 1);
    change_phase(RenderStateId::kAlphaBlend, 1);
}

// 0x004DEB70
void get_bloom()
{
    if (!renderer().shaders_available()) {
        return;
    }
    change_phase(RenderStateId::kZWrite, 0);
    change_phase(RenderStateId::kZTest, 0);
    change_phase(RenderStateId::kAlphaTest, 0);
    change_phase(RenderStateId::kAlphaBlend, 0);
    IDirect3DTexture9* quarter = screen_copy(2);
    IDirect3DTexture9* bright = screen_copy(3);
    set_pixel_shader(get_shader(kPixelShader948), nullptr); // the cut-off
    // Constant 0: the cut-off colour (bytes / 128) and 0.5.
    const colour::Bytes cutoff = colour::bytes_of(effects_state().bloom_cut_off());
    std::array<float, 4> constant = {{static_cast<float>(std::get<0>(cutoff) * (1.0 / kHalfByte)),
                                      static_cast<float>(std::get<1>(cutoff) * (1.0 / kHalfByte)),
                                      static_cast<float>(std::get<2>(cutoff) * (1.0 / kHalfByte)), kHalfF}};
    renderer().d3d_device()->SetPixelShaderConstantF(0, constant.data(), 1);
    draw_screen_quad(0, bright, quarter, {.x = 0.0F, .y = 0.0F}, kNeutralColour, false);
    // Blur: vertically into the eighth, back horizontally; then the same at a sixteenth.
    IDirect3DTexture9* a = screen_copy(3);
    IDirect3DTexture9* b = screen_copy(4);
    change_phase(RenderStateId::kAlphaBlend, 1);
    D3DSURFACE_DESC unused;
    b->GetLevelDesc(0, &unused);
    for (int tap = 0; tap < 4; ++tap) {
        draw_screen_quad(tap > 0, b, a, {.x = 0.0F, .y = tap_offset(tap)}, tap_colour(tap_weight(tap)), true);
    }
    b = screen_copy(4);
    a = screen_copy(3);
    for (int tap = 0; tap < 4; ++tap) {
        draw_screen_quad(tap > 0, a, b, {.x = tap_offset(tap), .y = 0.0F}, tap_colour(tap_weight(tap)), true);
    }
    draw_screen_quad(0, screen_copy(kScreenCopyBlurA), screen_copy(3), {.x = 0.0F, .y = 0.0F}, kNeutralColour, true);
    b = screen_copy(kScreenCopyBlurB);
    a = screen_copy(kScreenCopyBlurA);
    b->GetLevelDesc(0, &unused);
    for (int tap = 0; tap < 4; ++tap) {
        draw_screen_quad(tap > 0, b, a, {.x = 0.0F, .y = tap_offset(tap)}, tap_colour(tap_weight(tap)), true);
    }
    b = screen_copy(kScreenCopyBlurB);
    a = screen_copy(kScreenCopyBlurA);
    for (int tap = 0; tap < 4; ++tap) {
        draw_screen_quad(tap > 0, a, b, {.x = tap_offset(tap), .y = 0.0F}, tap_colour(tap_weight(tap)), true);
    }
    set_target(current_screen_copy(), renderer().z_buffer_surface());
    if (!kBlurSecondPass) {
        return;
    }
    // Both blurred copies added over the frame.
    change_phase(RenderStateId::kZWrite, 0);
    change_phase(RenderStateId::kZTest, 0);
    change_phase(RenderStateId::kAlphaTest, 0);
    change_phase(RenderStateId::kAlphaBlend, 1);
    change_phase(RenderStateId::kPhaseAlphaTest, 0);
    change_phase(RenderStateId::kAddressMode0, kClampBothWays);
    set_pixel_shader(get_shader(kPixelShader950), nullptr);
    set_vertex_format(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    choose_blend_mode(1);
    for (int pass = 0; pass < 2; ++pass) {
        IDirect3DTexture9* blurred = pass ? screen_copy(kScreenCopyBlurA) : screen_copy(3);
        D3DSURFACE_DESC desc;
        current_screen_copy()->GetLevelDesc(0, &desc);
        const double half_x = 0.5 / as_unsigned(desc.Width);
        const double half_y = 0.5 / as_unsigned(desc.Height);
        const uint32_t alpha = pass ? effects_state().bloom_alpha_second() : effects_state().bloom_alpha_first();
        auto half_x_float = static_cast<float>(half_x);
        auto half_y_float = static_cast<float>(half_y);
        auto width = static_cast<float>(renderer().back_buffer_width());
        auto height = static_cast<float>(renderer().back_buffer_height());
        const uint32_t colour = alpha << 24 | 0x808080;
        // (as the original builds them: z 1, the second vertex's u 1, the last vertex half a texel short of the bottom)
        std::array<TexturedVertex, 4> quad = {{
            {.x = 0.0F, .y = 0.0F, .z = 1.0F, .rhw = 1.0F, .colour = colour, .u = half_x_float, .v = half_y_float},
            {.x = width, .y = 0.0F, .z = 1.0F, .rhw = 1.0F, .colour = colour, .u = 1.0F, .v = half_y_float},
            {.x = 0.0F,
             .y = height,
             .z = 1.0F,
             .rhw = 1.0F,
             .colour = colour,
             .u = half_x_float,
             .v = static_cast<float>(static_cast<double>(half_y_float) + 1.0)},
            {.x = width,
             .y = static_cast<float>(static_cast<double>(height) - half_y_float),
             .z = 1.0F,
             .rhw = 1.0F,
             .colour = colour,
             .u = static_cast<float>(half_x + 1.0),
             .v = static_cast<float>(static_cast<double>(half_y_float) + 1.0)},
        }};
        set_texture(0, nullptr);
        set_texture(0, blurred);
        set_texture(1, nullptr);
        set_texture(2, nullptr);
        set_texture(3, nullptr);
        renderer().draw_device()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad.data(), sizeof(TexturedVertex));
        count_draw(2);
    }
    change_phase(RenderStateId::kAlphaTest, 1);
    change_phase(RenderStateId::kAlphaBlend, 1);
    change_phase(RenderStateId::kZWrite, 1);
    change_phase(RenderStateId::kZTest, 1);
    for (int stage = 0; stage < 4; ++stage) {
        set_texture(stage, nullptr);
    }
    set_pixel_shader(nullptr, nullptr);
}

} // namespace render
