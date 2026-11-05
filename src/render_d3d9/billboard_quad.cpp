// 2D sprites (Nx engine, PC): a texture drawn as a quad, from the 640x480 screen layout onto the back buffer.

#include "render_d3d9/billboard_quad.h"

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <array>
#include <utility>

#include "base/maths/vec4.h"
#include "render_d3d9/render_state.h"
#include "engine/gfx/screen.h"
#include "platform/run_log.h"
#include "engine/gfx/engine.h"
#include "base/core/checked_index.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/engine_parts.h"

namespace render {

constexpr std::size_t kQuadIndexCount = 6;
constexpr uint32_t kAlphaMask = 0xFF000000U;
constexpr std::size_t kSeenSlots = 256;
constexpr int kSeenLimit = 256;

namespace {
// Two triangles of a quad: 0 1 2, 1 3 2.

// The 640x480 layout -> back buffer conversion (set_letterbox) and the back buffer's size.

// The layout's size (the constants at 0x00649FD4 and 0x006484B4).

void set_texture_stage_state(uint32_t stage, GpuStageState type, uint32_t value) { gpu().set_stage_state(stage, type, value); }
const std::array<uint16_t, kQuadIndexCount>& quad_indices()
{
    static constexpr std::array<uint16_t, 6> kSpriteQuadIndices{{0, 1, 2, 1, 3, 2}};
    return kSpriteQuadIndices;
}

struct Vertex {
    float x, y, z, rhw;
    uint32_t color; // ARGB
    float u, v;
};
constexpr std::size_t kVertexBytes = 0x1C;

static_assert(sizeof(Vertex) == kVertexBytes, "the original's vertex");

using Vector4 = maths::Vec4;
// 0x004882D0 (a Vector member): rotate about z by `angle` radians.
void spin_around_z(Vector4& v, float angle) { v.rotate_about_z(angle); }

// The original's _ftol (0x006236FC): truncated towards zero, as a 64-bit integer.
int64_t get_truncated(double value) { return static_cast<int64_t>(value); }

// A sprite's left and right edges (in the layout, or on the screen).
struct SpriteEdges {
    float left;
    float right;
};

// An upright sprite's sides in the widescreen front end: each edge reaching past its layout edge goes to the
// screen's edge.
void stretch_upright_sides(SpriteEdges layout, SpriteEdges& screen, float screen_right)
{
    if (layout.left <= 0.0F && layout.right > 0.0F) {
        screen.left = 0.0F;
    }
    if (layout.right >= kLayoutWidth && layout.left < kLayoutWidth) {
        screen.right = screen_right;
    }
}

// A turned sprite's sides in the widescreen front end: its left side is its two leftmost corners in the layout, the
// right side the two rightmost; each side reaching past its layout edge goes to the screen's edge (only when the
// sprite overlaps the layout).
void stretch_turned_sides(std::array<Vertex, 4>& vertices, const std::array<double, 4>& layout_x, float screen_right)
{
    std::array<int, 4> order = {{0, 1, 2, 3}};
    for (int outer = 0; outer < 4; ++outer) {
        for (int inner = outer + 1; inner < 4; ++inner) {
            if (core::at(layout_x, core::at(order, inner)) < core::at(layout_x, core::at(order, outer))) {
                std::swap(core::at(order, outer), core::at(order, inner));
            }
        }
    }
    // (again only when the sprite overlaps the layout)
    if (core::at(layout_x, core::at(order, 0)) <= 0.0 && core::at(layout_x, core::at(order, 3)) > 0.0) {
        core::at(vertices, core::at(order, 0)).x = core::at(vertices, core::at(order, 1)).x = 0.0F;
    }
    if (core::at(layout_x, core::at(order, 3)) >= kLayoutWidth &&
        core::at(layout_x, core::at(order, 0)) < kLayoutWidth) {
        core::at(vertices, core::at(order, 2)).x = core::at(vertices, core::at(order, 3)).x = screen_right;
    }
}

// The sprite log's positions seen: whether `key` is a new one reaching past the layout (`reaches`) - noted (up to
// the limit).
bool note_new_spot(uint32_t key, bool reaches)
{
    static std::array<uint32_t, kSeenSlots> s_seen;
    static int tally = 0;
    bool seen = false;
    for (int i = 0; i < tally; ++i) {
        seen |= core::at(s_seen, i) == key;
    }
    if (!seen && tally < kSeenLimit && reaches) {
        core::at(s_seen, tally++) = key;
        return true;
    }
    return false;
}

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
    float u0 = 0.0F;
    float v_top = 1.0F;
    float u1 = 0.0F;
    float v_bottom = 0.0F;
    if (texture) {
        u1 = static_cast<float>(static_cast<double>(texture->image_width_) / texture->width);
        v_bottom = static_cast<float>(1.0 - (static_cast<double>(texture->image_height_) / texture->height));
    } else {
        u1 = 1.0F;
        v_bottom = 0.0F;
    }
    float scale_x = xscale;
    float scale_y = yscale;
    if (scale_x == 0.0F || scale_y == 0.0F) {
        return;
    }
    if (scale_x < 0.0F) {
        scale_x = -scale_x;
        u0 = u1;
        u1 = 0.0F;
    }
    if (scale_y < 0.0F) {
        scale_y = -scale_y;
        v_top = v_bottom;
        v_bottom = 1.0F;
    }
    // (the flips go through a 16-bit integer, as in the original: coordinates between 0 and 1 come out 0, 1.0 stays)
    if (flip_v_) {
        const float old = v_top;
        v_top = v_bottom;
        v_bottom = static_cast<float>(static_cast<uint16_t>(get_truncated(old)));
    }
    if (flip_u_) {
        const float old = u0;
        u0 = u1;
        u1 = static_cast<float>(static_cast<uint16_t>(get_truncated(old)));
    }

    // The corners from the hot spot, in layout units (the left one kept at double precision, as the original does).
    const double left = -(static_cast<double>(scale_x) * hot_x_);
    auto top = static_cast<float>(-(static_cast<double>(scale_y) * hot_y_));
    auto right = static_cast<float>((static_cast<double>(width) * scale_x) + left);
    auto bottom = static_cast<float>((static_cast<double>(height) * scale_y) + top);

    const uint32_t argb = (color_ & 0xFF00FF00) | ((color_ & 0xFF) << 16) | ((color_ >> 16) & 0xFF);
    if ((argb & kAlphaMask) == 0U) {
        return;
    }

    const bool front_end_widescreen = get_front_end_widescreen();
    auto screen_right = static_cast<float>(renderer().back_buffer_width());
    std::array<Vertex, 4> vertices{}; // top left, bottom left, top right, bottom right
    if (rotation == 0.0F) {
        auto x0 = static_cast<float>(left + x);
        auto y0 = static_cast<float>(static_cast<double>(top) + y);
        auto x1 = static_cast<float>(static_cast<double>(right) + x);
        double y1 = static_cast<double>(bottom) + y;
        const float layout_x0 = x0;
        const float layout_x1 = x1;
        // As wide as the layout or wider: the whole back buffer's width (the same for heights).
        if (x0 <= 0.0F && x1 >= kLayoutWidth) {
            x0 = 0.0F;
            x1 = screen_right;
        } else {
            x0 = static_cast<float>((static_cast<double>(renderer().display_factor_x()) * x0) +
                                    renderer().screen_offset_x());
            x1 = static_cast<float>((static_cast<double>(renderer().display_factor_x()) * x1) +
                                    renderer().screen_offset_x());
        }
        if (y0 <= 0.0F && y1 >= kLayoutHeight) {
            y0 = 0.0F;
            y1 = renderer().back_buffer_height();
        } else {
            // (an edge exactly at 0 isn't moved by the offset - the original's)
            if (y0 != 0.0F) {
                y0 = static_cast<float>((static_cast<double>(renderer().display_factor_y()) * y0) +
                                        renderer().screen_offset_y());
            }
            if (y1 != 0.0) {
                y1 = (y1 * renderer().display_factor_y()) + renderer().screen_offset_y();
            }
        }
        if (front_end_widescreen) {
            // (only sprites overlapping the layout: one lying wholly outside it - parked off-screen, or the end
            // strip of a band - keeps its place instead of becoming a strip at the screen's edge)
            SpriteEdges screen{.left = x0, .right = x1};
            stretch_upright_sides({.left = layout_x0, .right = layout_x1}, screen, screen_right);
            x0 = screen.left;
            x1 = screen.right;
        }
        core::at(vertices, 0).x = core::at(vertices, 1).x = x0;
        core::at(vertices, 2).x = core::at(vertices, 3).x = x1;
        core::at(vertices, 0).y = core::at(vertices, 2).y = y0;
        core::at(vertices, 1).y = core::at(vertices, 3).y = static_cast<float>(y1);
    } else {
        std::array<Vector4, 4> corners = {{{.x = static_cast<float>(left), .y = top, .z = 0.0F, .w = 0.0F},
                                           {.x = static_cast<float>(left), .y = bottom, .z = 0.0F, .w = 0.0F},
                                           {.x = right, .y = top, .z = 0.0F, .w = 0.0F},
                                           {.x = right, .y = bottom, .z = 0.0F, .w = 0.0F}}};
        // (in the original's order: top left, top right, bottom left, bottom right)
        for (const int corner : {0, 2, 1, 3}) {
            spin_around_z(core::at(corners, corner), rotation);
        }
        // Still (nearly) upright or on its side: within 0.01 radians of a multiple of 90 degrees. (The menus' bottom
        // band is turned by pi + 0.001, so its corners don't line up exactly.)
        const double quarter_turns = static_cast<double>(rotation) / 1.5707963267948966;
        const bool upright = std::fabs(quarter_turns - std::round(quarter_turns)) * 1.5707963267948966 <= 0.01;
        const double offset_x = renderer().screen_offset_x();
        auto offset_y = static_cast<float>(renderer().screen_offset_y());
        std::array<double, 4> layout_x{};
        for (int i = 0; i < 4; ++i) {
            // (truncated to whole pixels, unlike the upright ones)
            core::at(layout_x, i) = static_cast<double>(core::at(corners, i).x) + x;
            core::at(vertices, i).x = static_cast<float>(static_cast<int32_t>(
                get_truncated((core::at(layout_x, i) * renderer().display_factor_x()) + offset_x)));
            core::at(vertices, i).y = static_cast<float>(static_cast<int32_t>(get_truncated(
                ((static_cast<double>(core::at(corners, i).y) + y) * renderer().display_factor_y()) + offset_y)));
        }
        if (front_end_widescreen && upright) {
            // The sprite's left side is its two leftmost corners in the layout, the right side the two rightmost
            // (a turned sprite's own left corners can be on the right); each side reaching past its layout edge
            // goes to the screen's edge, as for upright sprites.
            stretch_turned_sides(vertices, layout_x, screen_right);
        }
    }
    // Diagnostic: thug2-decomp.ini [features] debug_sprites=1 logs, in the front end, each sprite (once per position)
    // that reaches past the 4:3 layout's left or right edge, with the screen x it gets.
    static const bool kDebugSprites = config_int("features", "debug_sprites", 0) != 0;
    if (kDebugSprites && front_end_widescreen) {
        double reach_left = left + x;
        double reach_right = static_cast<double>(right) + x;
        if (rotation != 0.0F) {
            reach_left = std::fabs(static_cast<double>(left)) + std::fabs(static_cast<double>(right));
            reach_right = x + reach_left; // (rotated: anything whose extent could cross an edge)
            reach_left = x - reach_left;
        }
        const uint32_t key = (static_cast<uint32_t>(x * 16.0F) * 2654435761U) ^ static_cast<uint32_t>(y * 16.0F) ^
                             static_cast<uint32_t>(width) << 20;
        if (note_new_spot(key, reach_left <= 0.0 || reach_right >= kLayoutWidth)) {
            get_log("sprite: pos %.2f,%.2f size %ux%u scale %.3f,%.3f hot %.2f,%.2f rot %.5f flip %u%u -> screen x "
                    "%.1f %.1f "
                    "%.1f %.1f, y %.1f %.1f",
                    x, y, width, height, xscale, yscale, hot_x_, hot_y_, rotation, flip_u_, flip_v_, core::at(vertices, 0).x,
                    core::at(vertices, 1).x, core::at(vertices, 2).x, core::at(vertices, 3).x, core::at(vertices, 0).y, core::at(vertices, 1).y);
        }
    }
    const std::array<float, 4> u = {{u0, u0, u1, u1}};
    const std::array<float, 4> v = {{v_top, v_bottom, v_top, v_bottom}};
    for (int i = 0; i < 4; ++i) {
        core::at(vertices, i).z = 0.0F;
        core::at(vertices, i).rhw = 1.0F;
        core::at(vertices, i).color = argb;
        core::at(vertices, i).u = core::at(u, i);
        core::at(vertices, i).v = core::at(v, i);
    }

    set_vertex_format(kScreenVertexFormat);         // position (pre-transformed), colour, one texture coordinate
    set_texture_stage_state(0, GpuStageState::kColourOp, kTextureOpModulate2x); // colour: texture x diffuse, doubled
    set_texture_stage_state(0, GpuStageState::kAlphaOp, kTextureOpModulate2x); // alpha: the same
    if (!engine_parts().suppress_draw()) {           // (rendering not skipped)
        renderer().count_draw(2); // the frame's draw calls and triangles
        gpu().draw_indexed_from_memory(GpuPrimitive::kTriangleList, 0, 4, 2, quad_indices().data(), gpu_value::kFormatIndex16,
                                       vertices.data(), sizeof(Vertex));
    }
}

// 0x004DF990
void QuadSprite::open_sketch()
{
    render::set_vertex_format(kScreenVertexFormat); // xyzrhw, diffuse, one texture
    set_pixel_shader(nullptr, nullptr);
    if (texture) {
        set_texture_stage_state(0, GpuStageState::kColourOp, gpu_value::kOpModulate2x);
        set_texture_stage_state(0, GpuStageState::kAlphaOp, gpu_value::kOpModulate2x);
        render::set_texture(0, texture->texture);
    } else {
        set_texture_stage_state(0, GpuStageState::kColourOp, gpu_value::kOpSelectArg2);
        set_texture_stage_state(0, GpuStageState::kAlphaOp, gpu_value::kOpSelectArg2);
        render::set_texture(0, nullptr);
    }
    set_texture_stage_state(0, GpuStageState::kColourArg2, gpu_value::kArgCurrent);
    set_texture_stage_state(0, GpuStageState::kAlphaArg2, gpu_value::kArgCurrent);
}

// 0x004DFA40
void QuadSprite::close_sketch()
{
    set_texture_stage_state(0, GpuStageState::kColourOp, gpu_value::kOpModulate);
    set_texture_stage_state(0, GpuStageState::kAlphaOp, gpu_value::kOpModulate);
}

} // namespace render
