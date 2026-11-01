// Full-screen effects: see screen_fx.h.

#include "engine/gfx/screen_fx.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>

#include "engine/gfx/graphics_options.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/pc_scene.h"
#include "engine/gfx/primitive_states.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/settings.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/device.h"
#include "engine/gfx/engine_glue.h"

namespace render {

bool g_screen_copy_in_use;
bool g_blur_second_pass{1};
float g_blur_tap_weights[4]{0.16666667f, 0.33333334f, 0.33333334f, 0.16666667f};
float g_blur_tap_offsets[4]{-0.022857143f, -0.005714286f, 0.005714286f, 0.022857143f};

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }
IDirect3DDevice9* device_copy() { return g_draw_device; } // (the same device)
IDirect3DTexture9* screen_copy(int index) { return g_screen_textures[index]; }
IDirect3DTexture9* current_screen_copy() { return screen_copy(g_screen_copy_index); }
IDirect3DSurface9* z_buffer_surface() { return g_z_buffer_surface; }
bool& screen_copy_in_use() { return g_screen_copy_in_use; }
void* get_shader(int slot) { return g_pixel_shaders[slot]; }

// Fog on or off (not rewritten yet; the request kept, the device's state following).
void change_fog(bool on) { engine::enable_fog(on); }

// The renderer's draw counters.
void count_draw(int triangles)
{
    g_drawn_triangles += triangles;
    ++g_draw_calls;
}

// An unsigned size as the x87 loads it (FILD, plus 2^32 when the top bit is set).
double Unsigned(uint32_t value) { return static_cast<double>(value); }

void set_target(IDirect3DTexture9* texture, IDirect3DSurface9* depth)
{
    // NOT ORIGINAL - [Graphics] msaa (engine/gfx/graphics_options.h): a full-size screen copy drawn through the
    // multisampled scene surface.
    if (graphics_options::set_scene_target(texture))
        return;
    IDirect3DSurface9* surface = nullptr;
    texture->GetSurfaceLevel(0, &surface);
    input_port()->SetRenderTarget(0, surface);
    input_port()->SetDepthStencilSurface(depth);
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
float tap_offset(int tap) { return g_blur_tap_offsets[tap]; }
float tap_weight(int tap) { return g_blur_tap_weights[tap]; }

// A grey of 0-255 at half alpha: 0x80 then the value three times (from _ftol: a bigger value spills over).
uint32_t tap_colour(float weight)
{
    uint32_t w = static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(weight) * 128.0)); // 0x00647B2C
    return (((w | 0xFFFF8000) << 8 | w) << 8) | w;
}

} // namespace

// 0x004DE5E0
void draw_screen_quad(uint32_t blend, void* target, void* source, float du, float dv, uint32_t colour, bool shader)
{
    bool fog_was_on = g_fog_active != 0;
    change_fog(false);
    auto* target_texture = static_cast<IDirect3DTexture9*>(target);
    set_target(target_texture, nullptr);
    D3DSURFACE_DESC target_desc;
    target_texture->GetLevelDesc(0, &target_desc);
    set_texture(0, nullptr);
    device_copy()->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    device_copy()->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    change_phase(kAddressMode0, 0x10001);
    change_phase(kPhaseAlphaTest, 0);
    float width = static_cast<float>(Unsigned(target_desc.Width));
    float height = static_cast<float>(Unsigned(target_desc.Height));
    choose_blend_mode(blend);
    if (source) {
        auto* source_texture = static_cast<IDirect3DTexture9*>(source);
        D3DSURFACE_DESC source_desc;
        source_texture->GetLevelDesc(0, &source_desc); // (not used: half a texel of the target)
        float half_x = static_cast<float>(0.5 / Unsigned(target_desc.Width));
        float half_y = static_cast<float>(0.5 / Unsigned(target_desc.Height));
        set_texture(0, source);
        if (shader)
            set_pixel_shader(get_shader(kPixelShader950), nullptr);
        set_vertex_format(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        float right = static_cast<float>(static_cast<double>(du) + 1.0);
        float u0 = static_cast<float>(static_cast<double>(half_x) + du);
        float v0 = static_cast<float>(static_cast<double>(half_y) + dv);
        float u1 = static_cast<float>(static_cast<double>(right) + half_x);
        float v1 = static_cast<float>((static_cast<double>(dv) + 1.0) + half_y);
        TexturedVertex quad[4] = {
            {0.0f, 0.0f, 0.0f, 1.0f, colour, u0, v0},
            {width, 0.0f, 0.0f, 1.0f, colour, u1, v0},
            {0.0f, height, 0.0f, 1.0f, colour, u0, v1},
            {width, height, 0.0f, 1.0f, colour, u1, v1},
        };
        device_copy()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(TexturedVertex));
    } else {
        set_pixel_shader(get_shader(kPixelShader94c), nullptr);
        // (the vertex format is left as it was)
        FlatVertex quad[4] = {
            {0.0f, 0.0f, 0.0f, 1.0f, colour},
            {width, 0.0f, 0.0f, 1.0f, colour},
            {0.0f, height, 0.0f, 1.0f, colour},
            {width, height, 0.0f, 1.0f, colour},
        };
        device_copy()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(FlatVertex));
    }
    count_draw(2);
    change_fog(fog_was_on);
}

// 0x004DEA80
void begin_screen_effects()
{
    screen_copy_in_use() = false;
    IDirect3DSurface9* surface = nullptr;
    if (g_screen_blur > 2) { // the motion blur
        current_screen_copy()->GetSurfaceLevel(0, &surface);
        // (NOT ORIGINAL: or the multisampled scene surface - [Graphics] msaa, engine/gfx/graphics_options.h)
        if (!graphics_options::set_scene_target(current_screen_copy())) {
            input_port()->SetRenderTarget(0, surface);
            input_port()->SetDepthStencilSurface(z_buffer_surface());
        }
    } else {
        if (!g_bloom_on || !g_shaders_available) // no bloom (or no pixel shaders)
            return;
        current_screen_copy()->GetSurfaceLevel(0, &surface);
        // (NOT ORIGINAL: or the multisampled scene surface - [Graphics] msaa, engine/gfx/graphics_options.h)
        if (!graphics_options::set_scene_target(current_screen_copy()))
            set_render_target_and_depth(surface, z_buffer_surface());
    }
    // The clear colour: the fog's with distance clipping and fog on (0x007D643A, 0x007D6436), else 0x00786CC0.
    uint32_t colour = (g_fog_setting && g_distance_clipping) ? g_fog_colour : g_camera.background_colour_;
    input_port()->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, colour, 1.0f, 0);
    surface->Release();
    screen_copy_in_use() = true;
}

// 0x004DE980
void copy_screen_quarter()
{
    if (!screen_copy_in_use())
        return;
    IDirect3DTexture9* source = current_screen_copy();
    IDirect3DTexture9* quarter = screen_copy(2);
    // NOT ORIGINAL - [Graphics] msaa (engine/gfx/graphics_options.h): the frame so far resolved into the copy first.
    graphics_options::resolve_scene_target(source);
    change_phase(kDepthBias, 0);
    change_phase(kLighting, 0);
    change_phase(kZWrite, 0);
    change_phase(kZTest, 0);
    change_phase(kAlphaTest, 0);
    change_phase(kAlphaBlend, 0);
    change_phase(kCullMode, D3DCULL_NONE);
    draw_screen_quad(0, quarter, source, 0.0f, 0.0f, 0x80808080, true);
    set_target(current_screen_copy(), z_buffer_surface());
    change_phase(kZWrite, 1);
    change_phase(kZTest, 1);
    change_phase(kAlphaTest, 1);
    change_phase(kAlphaBlend, 1);
}

// 0x004DEB70
void get_bloom()
{
    if (!g_shaders_available)
        return;
    change_phase(kZWrite, 0);
    change_phase(kZTest, 0);
    change_phase(kAlphaTest, 0);
    change_phase(kAlphaBlend, 0);
    IDirect3DTexture9* quarter = screen_copy(2);
    IDirect3DTexture9* bright = screen_copy(3);
    set_pixel_shader(get_shader(kPixelShader948), nullptr); // the cut-off
    // Constant 0: the cut-off colour (bytes / 128) and 0.5.
    const uint8_t* cutoff = reinterpret_cast<uint8_t*>(&g_bloom_cut_off);
    float constant[4] = {static_cast<float>(cutoff[0] * (1.0 / 128)), static_cast<float>(cutoff[1] * (1.0 / 128)),
                         static_cast<float>(cutoff[2] * (1.0 / 128)), 0.5f};
    input_port()->SetPixelShaderConstantF(0, constant, 1);
    draw_screen_quad(0, bright, quarter, 0.0f, 0.0f, 0x80808080, false);
    // Blur: vertically into the eighth, back horizontally; then the same at a sixteenth.
    IDirect3DTexture9* a = screen_copy(3);
    IDirect3DTexture9* b = screen_copy(4);
    change_phase(kAlphaBlend, 1);
    D3DSURFACE_DESC unused;
    b->GetLevelDesc(0, &unused);
    for (int tap = 0; tap < 4; ++tap)
        draw_screen_quad(tap > 0, b, a, 0.0f, tap_offset(tap), tap_colour(tap_weight(tap)), true);
    b = screen_copy(4);
    a = screen_copy(3);
    for (int tap = 0; tap < 4; ++tap)
        draw_screen_quad(tap > 0, a, b, tap_offset(tap), 0.0f, tap_colour(tap_weight(tap)), true);
    draw_screen_quad(0, screen_copy(7), screen_copy(3), 0.0f, 0.0f, 0x80808080, true);
    b = screen_copy(8);
    a = screen_copy(7);
    b->GetLevelDesc(0, &unused);
    for (int tap = 0; tap < 4; ++tap)
        draw_screen_quad(tap > 0, b, a, 0.0f, tap_offset(tap), tap_colour(tap_weight(tap)), true);
    b = screen_copy(8);
    a = screen_copy(7);
    for (int tap = 0; tap < 4; ++tap)
        draw_screen_quad(tap > 0, a, b, tap_offset(tap), 0.0f, tap_colour(tap_weight(tap)), true);
    set_target(current_screen_copy(), z_buffer_surface());
    if (!g_blur_second_pass)
        return;
    // Both blurred copies added over the frame.
    change_phase(kZWrite, 0);
    change_phase(kZTest, 0);
    change_phase(kAlphaTest, 0);
    change_phase(kAlphaBlend, 1);
    change_phase(kPhaseAlphaTest, 0);
    change_phase(kAddressMode0, 0x10001);
    set_pixel_shader(get_shader(kPixelShader950), nullptr);
    set_vertex_format(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    choose_blend_mode(1);
    for (int pass = 0; pass < 2; ++pass) {
        IDirect3DTexture9* blurred = pass ? screen_copy(7) : screen_copy(3);
        D3DSURFACE_DESC desc;
        current_screen_copy()->GetLevelDesc(0, &desc);
        double half_x = 0.5 / Unsigned(desc.Width);
        double half_y = 0.5 / Unsigned(desc.Height);
        uint32_t alpha = pass ? g_bloom_alpha_second : g_bloom_alpha_first;
        float half_x_float = static_cast<float>(half_x), half_y_float = static_cast<float>(half_y);
        float width = static_cast<float>(g_back_buffer_width);
        float height = static_cast<float>(g_back_buffer_height);
        uint32_t colour = alpha << 24 | 0x808080;
        // (as the original builds them: z 1, the second vertex's u 1, the last vertex half a texel short of the bottom)
        TexturedVertex quad[4] = {
            {0.0f, 0.0f, 1.0f, 1.0f, colour, half_x_float, half_y_float},
            {width, 0.0f, 1.0f, 1.0f, colour, 1.0f, half_y_float},
            {0.0f, height, 1.0f, 1.0f, colour, half_x_float,
             static_cast<float>(static_cast<double>(half_y_float) + 1.0)},
            {width, static_cast<float>(static_cast<double>(height) - half_y_float), 1.0f, 1.0f, colour,
             static_cast<float>(half_x + 1.0), static_cast<float>(static_cast<double>(half_y_float) + 1.0)},
        };
        set_texture(0, nullptr);
        set_texture(0, blurred);
        set_texture(1, nullptr);
        set_texture(2, nullptr);
        set_texture(3, nullptr);
        device_copy()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(TexturedVertex));
        count_draw(2);
    }
    change_phase(kAlphaTest, 1);
    change_phase(kAlphaBlend, 1);
    change_phase(kZWrite, 1);
    change_phase(kZTest, 1);
    for (int stage = 0; stage < 4; ++stage)
        set_texture(stage, nullptr);
    set_pixel_shader(nullptr, nullptr);
}

} // namespace render
