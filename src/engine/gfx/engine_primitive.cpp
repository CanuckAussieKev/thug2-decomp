// The engine's primitive (render::Primitive) and its lists: see engine_primitive.h. From the disassembly of
// 0x0049C390-0x0049C906.

#include <cstdint>
#include <span>

#include "engine/gfx/engine_primitive.h"

#include "engine/core/dbg.h"
#include "engine/gfx/pc_primitive.h"
#include "engine/core/no_destroy.h"
#include "engine/core/packed_colour.h"
#include "engine/maths/vec2.h"
#include "engine/maths/vec4.h"

namespace render {

constexpr uint32_t kStatesLowMask = 0x7u;    // a primitive's packed states: three fields (bits 0-2, 3-7, 8-9)
constexpr uint32_t kStatesMidMask = 0xF8u;
constexpr uint32_t kStatesHighMask = 0x300u;
constexpr int kStatesHighShift = 8;

namespace {

constexpr uint32_t kNewFlag = 0x100;

// NOT ORIGINAL grouping - engine_primitive.cpp's state (were globals). Made on first use, never destroyed
// (engine_primitive_state()).
struct EnginePrimitiveState {
    uint32_t primitive_states_{};
    Primitive* drawn_primitives_{};
    Primitive* new_primitives_{};
};

EnginePrimitiveState& engine_primitive_state()
{
    static core::NoDestroy<EnginePrimitiveState> state;
    return state.get();
}

Primitive*& new_primitives() { return engine_primitive_state().new_primitives_; }
Primitive*& drawn_primitives() { return engine_primitive_state().drawn_primitives_; }
uint32_t& primitive_states() { return engine_primitive_state().primitive_states_; }

} // namespace

// 0x0049C660
Primitive::Primitive(int own) : owns_vertices_(own)
{
    if (own == 1)
        link_primitive(this);
}

// 0x0049C6A0
Primitive::~Primitive()
{
    if (owns_vertices_ == 1)
        unlink_primitive(this);
}

// 0x0049C6C0
void Primitive::get_ready()
{
    end_setup();
    ready_ = true;
}

// 0x0049C6D0
void Primitive::render_if_ready()
{
    if (ready_)
        render();
}

// 0x0049C6E0
void Primitive::draw_if_ready(std::span<const float> positions, std::span<const uint32_t> colours, std::span<const float> uvs,
                              std::span<const float> normals)
{
    if (ready_)
        blit(positions, colours, uvs, normals);
}

// --- the virtuals' bodies (0x0049C390-0x0049C640) ---

void Primitive::set_texture(const BitmapTexture*, int, int) { debug::printf("STUB: plat_set_texture\n"); }
void Primitive::change_colour(uint32_t) { debug::printf("STUB: plat_set_base_color\n"); }
void Primitive::prepare_all(PrimitiveLayout /*primitive_layout*/) { debug::printf("STUB: plat_begin\n"); }
void Primitive::end_setup() { debug::printf("STUB: plat_end\n"); }
void Primitive::render() { debug::printf("STUB: plat_draw\n"); }
void Primitive::blit(std::span<const float>, std::span<const uint32_t>, std::span<const float>, std::span<const float>)
{
    debug::printf("STUB: plat_render\n");
}
void Primitive::change_place(int, const maths::Vec4& /*place*/) { debug::printf("STUB: plat_set_pos\n"); }
void Primitive::set_x(int, float) { debug::printf("STUB: plat_set_pos_x\n"); }
void Primitive::set_y(int, float) { debug::printf("STUB: plat_set_pos_y\n"); }
void Primitive::set_z(int, float) { debug::printf("STUB: plat_set_pos_z\n"); }
void Primitive::set_normal(int, const maths::Vec4& /*normal*/) { debug::printf("STUB: plat_set_normal\n"); }
void Primitive::set_normal_x(int, float) { debug::printf("STUB: plat_set_normal_x\n"); }
void Primitive::set_normal_y(int, float) { debug::printf("STUB: plat_set_normal_y\n"); }
void Primitive::set_normal_z(int, float) { debug::printf("STUB: plat_set_normal_z\n"); }
void Primitive::set_vertex_colour(int, colour::Packed) { debug::printf("STUB: plat_set_color\n"); }
void Primitive::set_uv(int, maths::Vec2 /*uv*/) { debug::printf("STUB: plat_set_uv\n"); }
void Primitive::choose_u(int, float) { debug::printf("STUB: plat_set_u\n"); }
void Primitive::choose_v(int, float) { debug::printf("STUB: plat_set_v\n"); }
float Primitive::fetch_pos_x(int)
{
    debug::printf("STUB: plat_get_pos_x\n");
    return 0.0f;
}
float Primitive::fetch_pos_y(int)
{
    debug::printf("STUB: plat_get_pos_y\n");
    return 0.0f;
}
float Primitive::fetch_pos_z(int)
{
    debug::printf("STUB: plat_get_pos_z\n");
    return 0.0f;
}
float Primitive::get_normal_x(int)
{
    debug::printf("STUB: plat_get_normal_x\n");
    return 0.0f;
}
float Primitive::get_normal_y(int)
{
    debug::printf("STUB: plat_get_normal_y\n");
    return 0.0f;
}
float Primitive::get_normal_z(int)
{
    debug::printf("STUB: plat_get_normal_z\n");
    return 0.0f;
}
uint32_t Primitive::get_vertex_colour(int)
{
    debug::printf("STUB: plat_get_color\n");
    return 0;
}
float Primitive::get_u(int)
{
    debug::printf("STUB: plat_get_u\n");
    return 0.0f;
}
float Primitive::get_v(int)
{
    debug::printf("STUB: plat_get_v\n");
    return 0.0f;
}
void Primitive::get_slide(const maths::Vec4& /*slide*/) { debug::printf("STUB: plat_add_pos\n"); }
void Primitive::translate_x(float) { debug::printf("STUB: plat_add_pos_x\n"); }
void Primitive::translate_y(float) { debug::printf("STUB: plat_add_pos_y\n"); }
void Primitive::translate_z(float) { debug::printf("STUB: plat_add_pos_z\n"); }
void Primitive::scroll_uv(maths::Vec2 /*uv*/) { debug::printf("STUB: plat_add_uv\n"); }
void Primitive::scroll_u(float) { debug::printf("STUB: plat_add_u\n"); }
void Primitive::scroll_v(float) { debug::printf("STUB: plat_add_v\n"); }
int Primitive::weigh(const Primitive*)
{
    debug::printf("STUB: plat_sort\n");
    return 0;
}

// --- the lists ---

// 0x0049C7D0
void link_primitive(Primitive* primitive)
{
    primitive->following() = new_primitives();
    new_primitives() = primitive;
    primitive->flag_bits() |= kNewFlag;
}

// 0x0049C7F0
void unlink_primitive(Primitive* primitive)
{
    Primitive** list = (primitive->flag_bits() & kNewFlag) ? &new_primitives() : &drawn_primitives();
    Primitive* before = *list;
    if (before == primitive) {
        *list = before->following();
        return;
    }
    while (before && before->following() && before->following() != primitive)
        before = before->following();
    before->following() = before->following()->following(); // (not found: through null, as the original)
}

// 0x0049C840
void render_primitives()
{
    while (Primitive* primitive = new_primitives()) {
        unlink_primitive(primitive);
        primitive->flag_bits() &= ~kNewFlag;
        Primitive** at = &drawn_primitives();
        if (*at) {
            for (;;) {
                const int order = primitive->weigh(*at);
                if (order < 0)
                    break;
                at = &(*at)->following();
                if (!*at)
                    break;
            }
        }
        primitive->following() = *at;
        *at = primitive;
    }
    for (Primitive* primitive = drawn_primitives(); primitive; primitive = primitive->following()) {
        if (primitive->ready())
            primitive->render_if_ready();
    }
}

// 0x0049C700
void set_primitive_state(int state, int value)
{
    uint32_t& states = primitive_states();
    switch (state) {
    case 1:
        states = (states & ~kStatesLowMask) | (static_cast<uint32_t>(value) & kStatesLowMask);
        break;
    case 2:
        states = (states & ~kStatesMidMask) | ((static_cast<uint32_t>(value) << 3) & kStatesMidMask);
        break;
    case 4:
        states = (states & ~kStatesHighMask) | ((static_cast<uint32_t>(value) << kStatesHighShift) & kStatesHighMask);
        break;
    default:
        break;
    }
    set_primitive_depth_test({.state = state, .value = value});
}

} // namespace render
