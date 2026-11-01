// 2D sprites (Nx engine, PC): a texture drawn as a quad, from the 640x480 screen layout onto the back buffer.

#include "engine/gfx/billboard_quad.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstdint>
#include <initializer_list>

#include "engine/core/manager.h"
#include "engine/gfx/draw2d.h"
#include "engine/gfx/draw_frame.h"
#include "engine/maths/vec4.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/screen.h"
#include "platform/run_log.h"
#include "engine/gfx/device.h"
#include "engine/gfx/engine.h"

namespace render {

// Two triangles of a quad: 0 1 2, 1 3 2.
uint16_t g_sprite_quad_indices[6]{0, 1, 2, 1, 3, 2};

namespace {

// The 640x480 layout -> back buffer conversion (set_letterbox) and the back buffer's size.
float horiz_screen_ratio() { return g_display_factor_x; }
float vert_screen_ratio() { return g_display_factor_y; }
int32_t screen_offset_x() { return static_cast<int32_t>(g_screen_offset_x); }
int32_t screen_offset_y() { return static_cast<int32_t>(g_screen_offset_y); }
int32_t get_back_buffer_width() { return g_back_buffer_width; }
int32_t get_back_buffer_height() { return g_back_buffer_height; }
// The layout's size (the constants at 0x00649FD4 and 0x006484B4).
constexpr float kLayoutWidth = 640.0f;
constexpr float kLayoutHeight = 480.0f;

// The D3D9 device, its methods by vtable offset.
void** input_port() { return reinterpret_cast<void**>(g_d3d_device); }
template <typename F>
F device_method(int offset)
{
    return reinterpret_cast<F>((*reinterpret_cast<void***>(input_port()))[offset / 4]);
}
void set_texture_stage_state(int stage, int type, int value)
{
    device_method<long(__stdcall*)(void*, int, int, int)>(0x10C)(input_port(), stage, type, value);
}
const uint16_t* quad_indices() { return g_sprite_quad_indices; }

struct Vertex {
    float x, y, z, rhw;
    uint32_t color; // ARGB
    float u, v;
};
static_assert(sizeof(Vertex) == 0x1C, "the original's vertex");

struct Vector4 {
    float x, y, z, w;
};
// 0x004882D0 (a Vector member, thiscall, not rewritten yet): rotate about z by `angle` radians (x87 fsin/fcos).
void spin_around_z(Vector4& v, float angle) { reinterpret_cast<maths::Vec4*>(&v)->rotate_about_z(angle); }

// The original's _ftol (0x006236FC): truncated towards zero, as a 64-bit integer.
int64_t get_truncated(double value) { return static_cast<int64_t>(value); }

} // namespace

// 0x004DFCF0. The texture's part holding the image (u0..u1, and from v_top at the top edge to vBottom), mirrored
// for negative scales; the quad's corners from the hot spot, scale and size, rotated about the hot spot; mapped
// to the back buffer; drawn unless transparent.
//
// Widescreen front end (thug2-decomp.ini [features] widescreen_menus, not in the original): the 2D layout stays
// 4:3 and centred while nothing covers the sides any more (PartyMod's pillarbox bars hid them), so a sprite reaching
// the layout's left edge (x <= 0) is drawn from the screen's left edge, one reaching its right edge (x >= 640) to
// the screen's right edge - the menus' bands and panels. Each edge on its own, and only for sprites overlapping the
// layout (one wholly outside it is drawn where it is); heights unchanged; rotated sprites only when they stay upright
// or on their side (the bottom band is turned by 180 degrees).
void QuadSprite::draw()
{
    float u0 = 0.0f, v_top = 1.0f, u1, v_bottom;
    if (texture) {
        u1 = static_cast<float>(static_cast<double>(texture->actual_width_) / texture->base_width_);
        v_bottom = static_cast<float>(1.0 - static_cast<double>(texture->actual_height_) / texture->base_height_);
    } else {
        u1 = 1.0f;
        v_bottom = 0.0f;
    }
    float scale_x = xscale, scale_y = yscale;
    if (scale_x == 0.0f || scale_y == 0.0f)
        return;
    if (scale_x < 0.0f) {
        scale_x = -scale_x;
        u0 = u1;
        u1 = 0.0f;
    }
    if (scale_y < 0.0f) {
        scale_y = -scale_y;
        v_top = v_bottom;
        v_bottom = 1.0f;
    }
    // (the flips go through a 16-bit integer, as in the original: coordinates between 0 and 1 come out 0, 1.0 stays)
    if (flip_v_) {
        float old = v_top;
        v_top = v_bottom;
        v_bottom = static_cast<float>(static_cast<uint16_t>(get_truncated(old)));
    }
    if (flip_u_) {
        float old = u0;
        u0 = u1;
        u1 = static_cast<float>(static_cast<uint16_t>(get_truncated(old)));
    }

    // The corners from the hot spot, in layout units (the left one kept at double precision, as the original does).
    double left = -(static_cast<double>(scale_x) * hot_x_);
    auto top = static_cast<float>(-(static_cast<double>(scale_y) * hot_y_));
    auto right = static_cast<float>(static_cast<double>(width) * scale_x + left);
    auto bottom = static_cast<float>(static_cast<double>(height) * scale_y + top);

    uint32_t argb = (color & 0xFF00FF00) | ((color & 0xFF) << 16) | ((color >> 16) & 0xFF);
    if (!(argb & 0xFF000000))
        return;

    bool front_end_widescreen = get_front_end_widescreen();
    auto screen_right = static_cast<float>(get_back_buffer_width());
    Vertex vertices[4]; // top left, bottom left, top right, bottom right
    if (rotation == 0.0f) {
        auto x0 = static_cast<float>(left + x);
        auto y0 = static_cast<float>(static_cast<double>(top) + y);
        auto x1 = static_cast<float>(static_cast<double>(right) + x);
        double y1 = static_cast<double>(bottom) + y;
        float layout_x0 = x0, layout_x1 = x1;
        // As wide as the layout or wider: the whole back buffer's width (the same for heights).
        if (x0 <= 0.0f && x1 >= kLayoutWidth) {
            x0 = 0.0f;
            x1 = screen_right;
        } else {
            x0 = static_cast<float>(static_cast<double>(horiz_screen_ratio()) * x0 + screen_offset_x());
            x1 = static_cast<float>(static_cast<double>(horiz_screen_ratio()) * x1 + screen_offset_x());
        }
        if (y0 <= 0.0f && y1 >= kLayoutHeight) {
            y0 = 0.0f;
            y1 = get_back_buffer_height();
        } else {
            // (an edge exactly at 0 isn't moved by the offset - the original's)
            if (y0 != 0.0f)
                y0 = static_cast<float>(static_cast<double>(vert_screen_ratio()) * y0 + screen_offset_y());
            if (y1 != 0.0)
                y1 = y1 * vert_screen_ratio() + screen_offset_y();
        }
        if (front_end_widescreen) {
            // (only sprites overlapping the layout: one lying wholly outside it - parked off-screen, or the end
            // strip of a band - keeps its place instead of becoming a strip at the screen's edge)
            if (layout_x0 <= 0.0f && layout_x1 > 0.0f)
                x0 = 0.0f;
            if (layout_x1 >= kLayoutWidth && layout_x0 < kLayoutWidth)
                x1 = screen_right;
        }
        vertices[0].x = vertices[1].x = x0;
        vertices[2].x = vertices[3].x = x1;
        vertices[0].y = vertices[2].y = y0;
        vertices[1].y = vertices[3].y = static_cast<float>(y1);
    } else {
        Vector4 corners[4] = {{static_cast<float>(left), top, 0.0f, 0.0f},
                              {static_cast<float>(left), bottom, 0.0f, 0.0f},
                              {right, top, 0.0f, 0.0f},
                              {right, bottom, 0.0f, 0.0f}};
        // (in the original's order: top left, top right, bottom left, bottom right)
        for (int i : {0, 2, 1, 3})
            spin_around_z(corners[i], rotation);
        // Still (nearly) upright or on its side: within 0.01 radians of a multiple of 90 degrees. (The menus' bottom
        // band is turned by pi + 0.001, so its corners don't line up exactly.)
        double quarter_turns = static_cast<double>(rotation) / 1.5707963267948966;
        bool upright = std::fabs(quarter_turns - std::round(quarter_turns)) * 1.5707963267948966 <= 0.01;
        double offset_x = screen_offset_x();
        auto offset_y = static_cast<float>(screen_offset_y());
        double layout_x[4];
        for (int i = 0; i < 4; ++i) {
            // (truncated to whole pixels, unlike the upright ones)
            layout_x[i] = static_cast<double>(corners[i].x) + x;
            vertices[i].x =
                static_cast<float>(static_cast<int32_t>(get_truncated(layout_x[i] * horiz_screen_ratio() + offset_x)));
            vertices[i].y = static_cast<float>(static_cast<int32_t>(
                get_truncated((static_cast<double>(corners[i].y) + y) * vert_screen_ratio() + offset_y)));
        }
        if (front_end_widescreen && upright) {
            // The sprite's left side is its two leftmost corners in the layout, the right side the two rightmost
            // (a turned sprite's own left corners can be on the right); each side reaching past its layout edge
            // goes to the screen's edge, as for upright sprites.
            int order[4] = {0, 1, 2, 3};
            for (int a = 0; a < 4; ++a)
                for (int b = a + 1; b < 4; ++b)
                    if (layout_x[order[b]] < layout_x[order[a]])
                        std::swap(order[a], order[b]);
            // (again only when the sprite overlaps the layout)
            if (layout_x[order[0]] <= 0.0 && layout_x[order[3]] > 0.0)
                vertices[order[0]].x = vertices[order[1]].x = 0.0f;
            if (layout_x[order[3]] >= kLayoutWidth && layout_x[order[0]] < kLayoutWidth)
                vertices[order[2]].x = vertices[order[3]].x = screen_right;
        }
    }
    // Diagnostic: thug2-decomp.ini [features] debug_sprites=1 logs, in the front end, each sprite (once per position)
    // that reaches past the 4:3 layout's left or right edge, with the screen x it gets.
    static const bool kDebugSprites = config_int("features", "debug_sprites", 0) != 0;
    if (kDebugSprites && front_end_widescreen) {
        double reach_left = left + x, reach_right = static_cast<double>(right) + x;
        if (rotation != 0.0f) {
            reach_left = std::fabs(static_cast<double>(left)) + std::fabs(static_cast<double>(right));
            reach_right = x + reach_left; // (rotated: anything whose extent could cross an edge)
            reach_left = x - reach_left;
        }
        static uint32_t s_seen[256];
        static int tally = 0;
        uint32_t key = static_cast<uint32_t>(x * 16.0f) * 2654435761u ^ static_cast<uint32_t>(y * 16.0f) ^
                       static_cast<uint32_t>(width) << 20;
        bool seen = false;
        for (int i = 0; i < tally; ++i)
            seen |= s_seen[i] == key;
        if (!seen && tally < 256 && (reach_left <= 0.0 || reach_right >= kLayoutWidth)) {
            s_seen[tally++] = key;
            get_log("sprite: pos %.2f,%.2f size %ux%u scale %.3f,%.3f hot %.2f,%.2f rot %.5f flip %u%u -> screen x "
                    "%.1f %.1f "
                    "%.1f %.1f, y %.1f %.1f",
                    x, y, width, height, xscale, yscale, hot_x_, hot_y_, rotation, flip_u_, flip_v_, vertices[0].x,
                    vertices[1].x, vertices[2].x, vertices[3].x, vertices[0].y, vertices[1].y);
        }
    }
    const float u[4] = {u0, u0, u1, u1};
    const float v[4] = {v_top, v_bottom, v_top, v_bottom};
    for (int i = 0; i < 4; ++i) {
        vertices[i].z = 0.0f;
        vertices[i].rhw = 1.0f;
        vertices[i].color = argb;
        vertices[i].u = u[i];
        vertices[i].v = v[i];
    }

    set_vertex_format(0x144);         // position (pre-transformed), colour, one texture coordinate
    set_texture_stage_state(0, 1, 5); // colour: texture x diffuse, doubled
    set_texture_stage_state(0, 4, 5); // alpha: the same
    if (!g_suppress_draw) {           // (rendering not skipped)
        ++g_draw_calls;               // draw calls
        g_drawn_triangles += 2;       // triangles
        using DrawIndexedPrimitiveUP = long(__stdcall*)(void*, int, unsigned int, unsigned int, unsigned int,
                                                        const void*, int, const void*, unsigned int);
        device_method<DrawIndexedPrimitiveUP>(0x150)(input_port(), 4 /* triangle list */, 0, 4, 2, quad_indices(),
                                                     0x65 /* 16-bit indices */, vertices, sizeof(Vertex));
    }
}

// 0x004DF990
void QuadSprite::open_sketch()
{
    render::set_vertex_format(0x144); // xyzrhw, diffuse, one texture
    set_pixel_shader(nullptr, nullptr);
    if (texture) {
        set_texture_stage_state(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        set_texture_stage_state(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE2X);
        render::set_texture(0, texture->device_texture_);
    } else {
        set_texture_stage_state(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        set_texture_stage_state(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        render::set_texture(0, nullptr);
    }
    set_texture_stage_state(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
    set_texture_stage_state(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
}

// 0x004DFA40
void QuadSprite::close_sketch()
{
    set_texture_stage_state(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    set_texture_stage_state(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

} // namespace render
