// The engine's primitive (render::Primitive) and its lists: see engine_primitive.h. From the disassembly of
// 0x0049C390-0x0049C906.

#include <cstdint>

#include "engine/gfx/engine_primitive.h"

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/gfx/pc_primitive.h"
#include "engine/core/code_address.h"

namespace render {

namespace {

extern void** const kVtable;
constexpr uint32_t kDefaultBlendMode = 0x54628ED7;
constexpr uint32_t kNewFlag = 0x100;

uint32_t g_primitive_states;
Primitive* g_drawn_primitives;
Primitive* g_new_primitives;

Primitive*& new_primitives() { return g_new_primitives; }
Primitive*& drawn_primitives() { return g_drawn_primitives; }
uint32_t& primitive_states() { return g_primitive_states; }

void** get_vtable(Primitive* primitive) { return primitive->vtable_; }

} // namespace

// 0x0049C660
Primitive* Primitive::make_new(int own)
{
    unknown1c_ = 0;
    ready_ = false;
    following_ = nullptr;
    unknown14 = 0;
    flag_bits_ = 0;
    vtable_ = reinterpret_cast<void**>(kVtable);
    blend_mode_ = kDefaultBlendMode;
    owns_vertices_ = own;
    if (own == 1)
        link_primitive(this);
    return this;
}

// 0x0049C6A0
void Primitive::destroy_body()
{
    vtable_ = reinterpret_cast<void**>(kVtable);
    if (owns_vertices_ == 1)
        unlink_primitive(this);
}

// 0x0049C790
Primitive* Primitive::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::dealloc(this);
    return this;
}

// 0x0049C6C0
void Primitive::get_ready()
{
    reinterpret_cast<void(__thiscall*)(Primitive*)>(get_vtable(this)[4])(this);
    ready_ = true;
}

// 0x0049C6D0
void Primitive::render_if_ready()
{
    if (ready_)
        reinterpret_cast<void(__thiscall*)(Primitive*)>(get_vtable(this)[5])(this);
}

// 0x0049C6E0
void Primitive::draw_if_ready(const float* positions, const uint32_t* colours, const float* uvs, const float* normals)
{
    if (ready_)
        reinterpret_cast<void(__thiscall*)(Primitive*, const float*, const uint32_t*, const float*, const float*)>(
            get_vtable(this)[6])(this, positions, colours, uvs, normals);
}

// --- the virtuals' bodies (0x0049C390-0x0049C640) ---

void Primitive::set_texture(const uint8_t*, int, int) { debug::printf("STUB: plat_set_texture\n"); }
void Primitive::change_colour(uint32_t) { debug::printf("STUB: plat_set_base_color\n"); }
void Primitive::prepare_all(uint32_t, int, int) { debug::printf("STUB: plat_begin\n"); }
void Primitive::get_end() { debug::printf("STUB: plat_end\n"); }
void Primitive::render() { debug::printf("STUB: plat_draw\n"); }
void Primitive::blit(const float*, const uint32_t*, const float*, const float*)
{
    debug::printf("STUB: plat_render\n");
}
void Primitive::change_place(int, float, float, float) { debug::printf("STUB: plat_set_pos\n"); }
void Primitive::set_x(int, float) { debug::printf("STUB: plat_set_pos_x\n"); }
void Primitive::set_y(int, float) { debug::printf("STUB: plat_set_pos_y\n"); }
void Primitive::set_z(int, float) { debug::printf("STUB: plat_set_pos_z\n"); }
void Primitive::set_normal(int, float, float, float) { debug::printf("STUB: plat_set_normal\n"); }
void Primitive::set_normal_x(int, float) { debug::printf("STUB: plat_set_normal_x\n"); }
void Primitive::set_normal_y(int, float) { debug::printf("STUB: plat_set_normal_y\n"); }
void Primitive::set_normal_z(int, float) { debug::printf("STUB: plat_set_normal_z\n"); }
void Primitive::set_vertex_colour(int, uint32_t) { debug::printf("STUB: plat_set_color\n"); }
void Primitive::set_uv(int, float, float) { debug::printf("STUB: plat_set_uv\n"); }
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
void Primitive::get_slide(float, float, float) { debug::printf("STUB: plat_add_pos\n"); }
void Primitive::translate_x(float) { debug::printf("STUB: plat_add_pos_x\n"); }
void Primitive::translate_y(float) { debug::printf("STUB: plat_add_pos_y\n"); }
void Primitive::translate_z(float) { debug::printf("STUB: plat_add_pos_z\n"); }
void Primitive::scroll_uv(float, float) { debug::printf("STUB: plat_add_uv\n"); }
void Primitive::scroll_u(float) { debug::printf("STUB: plat_add_u\n"); }
void Primitive::scroll_v(float) { debug::printf("STUB: plat_add_v\n"); }
int Primitive::weigh(const Primitive*)
{
    debug::printf("STUB: plat_sort\n");
    return 0;
}

// --- the lists ---

// 0x0049C7D0
void __cdecl link_primitive(Primitive* primitive)
{
    primitive->following_ = new_primitives();
    new_primitives() = primitive;
    primitive->flag_bits_ |= kNewFlag;
}

// 0x0049C7F0
void __cdecl unlink_primitive(Primitive* primitive)
{
    Primitive** list = (primitive->flag_bits_ & kNewFlag) ? &new_primitives() : &drawn_primitives();
    Primitive* before = *list;
    if (before == primitive) {
        *list = before->following_;
        return;
    }
    while (before && before->following_ && before->following_ != primitive)
        before = before->following_;
    before->following_ = before->following_->following_; // (not found: through null, as the original)
}

// 0x0049C840
void __cdecl render_primitives()
{
    while (Primitive* primitive = new_primitives()) {
        unlink_primitive(primitive);
        primitive->flag_bits_ &= ~kNewFlag;
        Primitive** at = &drawn_primitives();
        if (*at) {
            for (;;) {
                // (the primitive's Compare - vtable slot 35 - with the one there)
                int order = reinterpret_cast<int(__thiscall*)(Primitive*, Primitive*)>(get_vtable(primitive)[35])(
                    primitive, *at);
                if (order < 0)
                    break;
                at = &(*at)->following_;
                if (!*at)
                    break;
            }
        }
        primitive->following_ = *at;
        *at = primitive;
    }
    for (Primitive* primitive = drawn_primitives(); primitive; primitive = primitive->following_) {
        if (primitive->ready_)
            primitive->render_if_ready();
    }
}

// 0x0049C700
void __cdecl set_primitive_state(int state, int value)
{
    uint32_t& states = primitive_states();
    switch (state) {
    case 1:
        states = (states & ~0x7u) | (static_cast<uint32_t>(value) & 0x7u);
        break;
    case 2:
        states = (states & ~0xF8u) | ((static_cast<uint32_t>(value) << 3) & 0xF8u);
        break;
    case 4:
        states = (states & ~0x300u) | ((static_cast<uint32_t>(value) << 8) & 0x300u);
        break;
    default:
        break;
    }
    set_primitive_depth_test(state, value);
}

namespace {
static VtableSlot g_vtable_slots[] = {&render::Primitive::teardown,      &render::Primitive::set_texture,
                                      &render::Primitive::change_colour, &render::Primitive::prepare_all,
                                      &render::Primitive::get_end,       &render::Primitive::render,
                                      &render::Primitive::blit,          &render::Primitive::change_place,
                                      &render::Primitive::set_x,         &render::Primitive::set_y,
                                      &render::Primitive::set_z,         &render::Primitive::set_normal,
                                      &render::Primitive::set_normal_x,  &render::Primitive::set_normal_y,
                                      &render::Primitive::set_normal_z,  &render::Primitive::set_vertex_colour,
                                      &render::Primitive::set_uv,        &render::Primitive::choose_u,
                                      &render::Primitive::choose_v,      &render::Primitive::fetch_pos_x,
                                      &render::Primitive::fetch_pos_y,   &render::Primitive::fetch_pos_z,
                                      &render::Primitive::get_normal_x,  &render::Primitive::get_normal_y,
                                      &render::Primitive::get_normal_z,  &render::Primitive::get_vertex_colour,
                                      &render::Primitive::get_u,         &render::Primitive::get_v,
                                      &render::Primitive::get_slide,     &render::Primitive::translate_x,
                                      &render::Primitive::translate_y,   &render::Primitive::translate_z,
                                      &render::Primitive::scroll_uv,     &render::Primitive::scroll_u,
                                      &render::Primitive::scroll_v,      &render::Primitive::weigh};
void** const kVtable = reinterpret_cast<void**>(g_vtable_slots);
} // namespace

} // namespace render
