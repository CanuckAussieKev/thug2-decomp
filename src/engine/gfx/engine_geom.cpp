// The engine's geom: see engine_geom.h.

#include "engine/gfx/engine_geom.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/core/code_address.h"
#include "game/save_cards/memcard.h"
#include "engine/core/stubs.h"
#include "engine/gfx/sector.h"

namespace render {

// The function-local statics of the stubs below (the boxes they return, and their guards).
maths::Vec4 g_position_box;
uint32_t g_position_guard;
maths::Mat4 g_orientation_box;
uint32_t g_orientation_guard;
maths::Mat4 g_transform_box;
uint32_t g_transform_guard;

namespace {

extern void** const kGeomVtable;

template <typename R = void, typename... A>
R Virtual(Geom* geom, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(Geom*, A...)>(geom->vtable_[slot])(geom, args...);
}

// The stubs' messages ("STUB: ...\n").
void get_stub(const char* message) { debug::printf(message); }

// A function-local static of the original's: its guard's bit 1 set the first time (the box constructed then).
template <typename T>
T* local_static(T& box, uint32_t& guard)
{
    guard |= 1;
    return &box;
}

} // namespace

// 0x0048DD60
Geom* Geom::make_new()
{
    vtable_ = reinterpret_cast<void**>(kGeomVtable);
    clone_kind_ = 0;
    hit_triangles_ = nullptr;
    render_colours_ = nullptr;
    multicolour_ = 0;
    return this;
}

// 0x0048E130
void Geom::destroy_body()
{
    vtable_ = reinterpret_cast<void**>(kGeomVtable);
    if (render_colours_)
        memory::Hub::get_pull()->dealloc(render_colours_);
}

// 0x0048E220
Geom* Geom::teardown(unsigned int flags)
{
    vtable_ = reinterpret_cast<void**>(kGeomVtable);
    if (render_colours_)
        memory::Hub::get_pull()->dealloc(render_colours_);
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x0048DD80
Geom* Geom::replicate(int instance, void* scene)
{
    auto* clone = Virtual<Geom*>(this, 2, instance, scene); // (the whole word passed on)
    if (clone) {
        clone->clone_kind_ = static_cast<uint8_t>(instance) ? 1 : 2;
        if (hit_triangles_)
            clone->hit_triangles_ = hit_triangles_;
        clone->render_colours_ = nullptr;
    }
    return clone;
}

// 0x0048DDC0
Geom* Geom::clone_for_model(int instance, void* model)
{
    auto* clone = Virtual<Geom*>(this, 1, instance, model); // (the whole word passed on)
    if (clone) {
        clone->render_colours_ = nullptr;
        clone->clone_kind_ = static_cast<uint8_t>(instance) ? 1 : 2;
    }
    return clone;
}

// 0x0048DDF0
bool Geom::init_instance(void* mesh, void* model, int multicolour)
{
    clone_kind_ = 1;
    multicolour_ = static_cast<uint8_t>(multicolour);
    // (the original jumps with its own arguments: the whole word of the third)
    return Virtual<bool>(this, 3, mesh, model, multicolour);
}

// 0x0048DE10
uint32_t* Geom::fetch_tint(uint32_t* out)
{
    Virtual(this, 7, out);
    return out;
}

// 0x0048DE90
maths::Vec4* Geom::fetch_bound_sphere(maths::Vec4* out)
{
    Virtual(this, 17, out);
    return out;
}

// 0x0048DF10
void Geom::choose_tri_source(void* faces)
{
    hit_triangles_ = faces;
}

// 0x0048E150
void Geom::fetch_tints(uint32_t* out)
{
    uint32_t count = Virtual<uint32_t>(this, 36);
    if (!render_colours_) {
        memory::Hub* manager = memory::Hub::get_pull();
        manager->enter_heap_scope(manager->front_end_heap_);
        render_colours_ = static_cast<uint32_t*>(memory::alloc_block(count * 4));
        memory::Hub::get_pull()->leave_heap_scope();
        Virtual(this, 38, render_colours_);
    }
    std::memcpy(out, render_colours_, count * 4);
}

// --- the base's platform calls ----------------------------------------------------------------------------------

// 0x0048DA90
bool Geom::plat_init_instance(void*, void*, int)
{
    get_stub("STUB: PlatLoadGeomData\n"); // "STUB: PlatLoadGeomData\n"
    return true;
}
// 0x0048DAB0
uint32_t Geom::plat_get_checksum()
{
    get_stub("STUB: PlatGetChecksum\n"); // "STUB: PlatGetChecksum\n"
    return 0;
}
// 0x0048DAC0
void Geom::plat_set_color(uint32_t) { get_stub("STUB: PlatSetColor\n"); } // "STUB: PlatSetColor\n"
// 0x0048E040
uint32_t* Geom::plat_get_color(uint32_t* out)
{
    get_stub("STUB: PlatGetColor\n"); // "STUB: PlatGetColor\n"
    std::memset(out, 0, 4);
    return out;
}
// 0x0048DAD0
void Geom::plat_clear_color() { get_stub("STUB: PlatClearColor\n"); } // "STUB: PlatClearColor\n"
// 0x0048DAE0
bool Geom::plat_set_material_color(uint32_t, int, uint32_t)
{
    get_stub("STUB: PlatSetMaterialColor\n"); // "STUB: PlatSetMaterialColor\n"
    return false;
}
// 0x0048E070
uint32_t* Geom::plat_get_material_color(uint32_t* out, uint32_t, int)
{
    get_stub("STUB: PlatGetMaterialColor\n"); // "STUB: PlatGetMaterialColor\n"
    std::memset(out, 0, 4);
    return out;
}
// 0x0048DB00
void Geom::plat_set_visibility(uint32_t) { get_stub("STUB: PlatSetVisibility\n"); } // "STUB: PlatSetVisibility\n"
// 0x0048DB10
uint32_t Geom::plat_get_visibility()
{
    get_stub("STUB: PlatGetVisibility\n"); // "STUB: PlatGetVisibility\n"
    return 0;
}
// 0x0048DB20
void Geom::plat_set_active(bool) { get_stub("STUB: PlatSetActive\n"); } // "STUB: PlatSetActive\n"
// 0x0048DB30
bool Geom::plat_is_active()
{
    get_stub("STUB: PlatIsActive\n"); // "STUB: PlatIsActive\n"
    return false;
}
// (all zero at start, as the original's: the first PlatGetBoundingBox makes it empty)
maths::BBox g_plat_bounding_box{maths::BBox(maths::Vec4{0.0f, 0.0f, 0.0f, 0.0f})};
uint32_t g_plat_bounding_box_guard; // (bit 0: made)

// 0x0048DB40 (the box 0x006FAA08, its guard 0x006FAA28: made empty the first time)
maths::BBox* Geom::plat_get_bounding_box()
{
    uint32_t& guard = g_plat_bounding_box_guard;
    if (!(guard & 1)) {
        guard |= 1;
        new (&g_plat_bounding_box) maths::BBox();
    }
    get_stub("STUB: PlatGetBoundingBox\n"); // "STUB: PlatGetBoundingBox\n"
    return &g_plat_bounding_box;
}
// 0x0048DC30
void Geom::plat_set_bounding_sphere(const maths::Vec4*) { get_stub("STUB: PlatSetBoundingSphere\n"); } // "STUB: PlatSetBoundingSphere\n"
// 0x0048E0A0
maths::Vec4* Geom::plat_get_bounding_sphere(maths::Vec4* out)
{
    get_stub("STUB: PlatGetBoundingSphere\n"); // "STUB: PlatGetBoundingSphere\n"
    out->x = 0.0f;
    out->y = 0.0f;
    out->z = 0.0f;
    out->w = 10000.0f;
    return out;
}
// 0x0048DBB0
void Geom::plat_set_world_position(const maths::Vec4*) { get_stub("STUB: PlatSetWorldPosition\n"); } // "STUB: PlatSetWorldPosition\n"
// 0x0048E0D0 (the vector 0x006FAA2C, its guard 0x006FAA3C: nothing to construct)
maths::Vec4* Geom::plat_get_world_position()
{
    maths::Vec4* position = local_static(g_position_box, g_position_guard);
    get_stub("STUB: PlatGetWorldPosition\n"); // "STUB: PlatGetWorldPosition\n"
    return position;
}
// 0x0048DBC0
void Geom::plat_set_orientation(const maths::Mat4*) { get_stub("STUB: PlatSetOrientation\n"); } // "STUB: PlatSetOrientation\n"
// 0x0048E1C0 (the matrix 0x006FAA40, its guard 0x006FAA80)
maths::Mat4* Geom::plat_get_orientation()
{
    maths::Mat4* orientation = local_static(g_orientation_box, g_orientation_guard);
    get_stub("STUB: PlatGetOrientation\n"); // "STUB: PlatGetOrientation\n"
    return orientation;
}
// 0x0048DBD0
void Geom::plat_rotate_y(int) { get_stub("STUB: PlatRotateY\n"); } // "STUB: PlatRotateY\n"
// 0x0048DBE0
void Geom::plat_set_transform(const maths::Mat4*) { get_stub("STUB: PlatSetTransform\n"); } // "STUB: PlatSetTransform\n"
// 0x0048E1F0 (the matrix 0x006FAA88, its guard 0x006FAAC8)
maths::Mat4* Geom::plat_get_transform()
{
    maths::Mat4* transform = local_static(g_transform_box, g_transform_guard);
    get_stub("STUB: PlatGetTransform\n"); // "STUB: PlatGetTransform\n"
    return transform;
}
// 0x0048DBF0
void Geom::plat_set_scale(const maths::Vec4*) { get_stub("STUB: PlatSetScale\n"); } // "STUB: PlatSetScale\n"
// 0x0048E100
maths::Vec4* Geom::plat_get_scale(maths::Vec4* out)
{
    get_stub("STUB: PlatGetScale\n"); // "STUB: PlatGetScale\n"
    out->x = 0.0f;
    out->y = 0.0f;
    out->z = 0.0f;
    out->w = 0.0f;
    return out;
}
// 0x0048DC00
void Geom::plat_set_model_lights(void*) { get_stub("STUB: PlatSetModelLights\n"); } // "STUB: PlatSetModelLights\n"
// 0x0048DC10
bool Geom::plat_render_setup(const maths::Mat4*, const maths::Mat4*, int, int, int)
{
    get_stub("STUB: PlatRenderSetup\n"); // "STUB: PlatRenderSetup\n"
    return true;
}
// 0x0048DC40
bool Geom::plat_hide_polys(uint32_t)
{
    get_stub("STUB: PlatHidePolys\n"); // "STUB: PlatHidePolys\n"
    return true;
}
// 0x0048DC60
uint32_t Geom::plat_get_num_render_verts()
{
    get_stub("STUB: CGeom::plat_get_num_render_verts\n"); // "STUB: CGeom::plat_get_num_render_verts\n"
    return 0;
}
// 0x0048DC70
void Geom::plat_get_render_verts(maths::Vec4*) { get_stub("STUB: CGeom::plat_get_render_verts\n"); } // "STUB: CGeom::plat_get_render_verts\n"
// 0x0048DC80
uint32_t Geom::plat_get_num_render_polys()
{
    get_stub("STUB: CGeom::plat_get_num_render_polys\n"); // "STUB: CGeom::plat_get_num_render_polys\n"
    return 0;
}
// 0x0048DC90
uint32_t Geom::plat_get_num_render_base_polys()
{
    get_stub("STUB: CGeom::plat_get_num_render_base_polys\n"); // "STUB: CGeom::plat_get_num_render_base_polys\n"
    return 0;
}
// 0x0048DCA0
void Geom::plat_get_render_colors(uint32_t*) { get_stub("STUB: CGeom::plat_get_render_colors\n"); } // "STUB: CGeom::plat_get_render_colors\n"
// 0x0048DCB0
void Geom::plat_set_render_verts(const maths::Vec4*) { get_stub("STUB: CGeom::plat_set_render_verts\n"); } // "STUB: CGeom::plat_set_render_verts\n"
// 0x0048DCC0
void Geom::plat_set_render_colors(const uint32_t*) { get_stub("STUB: CGeom::plat_set_render_colors\n"); } // "STUB: CGeom::plat_set_render_colors\n"
// 0x0048DCE0
void Geom::plat_set_uv_wibble_params(float, float, float, float, float, float, float, float)
{
    get_stub("STUB: CGeom::plat_set_uv_wibble_params\n"); // "STUB: CGeom::plat_set_uv_wibble_params\n"
}
// 0x0048DCF0
void Geom::plat_use_explicit_uv_wibble(int) { get_stub("STUB: CGeom::plat_use_explicit_uv_wibble\n"); } // "STUB: CGeom::plat_use_explicit_uv_wibble\n"
// 0x0048DD10
bool Geom::plat_set_material_uv_wibble_offsets(uint32_t, int, float, float)
{
    get_stub("STUB: CGeom::plat_set_uv_wibble_offsets\n"); // "STUB: CGeom::plat_set_uv_wibble_offsets\n"
    return false;
}
// 0x0048DD00
void Geom::plat_set_uv_wibble_offsets(float, float) { get_stub("STUB: CGeom::plat_set_uv_wibble_offsets\n"); } // "STUB: CGeom::plat_set_uv_wibble_offsets\n"
// 0x0048DD30
bool Geom::plat_set_uv_matrix(uint32_t, int, const maths::Mat4*)
{
    get_stub("STUB: CGeom::plat_set_uv_matrix\n"); // "STUB: CGeom::plat_set_uv_matrix\n"
    return false;
}

// --- the dispatchers --------------------------------------------------------------------------------------------

#define NX_VIRTUAL_SLOT_JUMP(slot)                                                                                     \
    __declspec(naked) void virtual_slot##slot##_jump()                                                                   \
    {                                                                                                                  \
        __asm mov eax, dword ptr [ecx]                                                                                 \
        __asm jmp dword ptr [eax + 4 * slot]                                                                           \
    }

NX_VIRTUAL_SLOT_JUMP(7)
NX_VIRTUAL_SLOT_JUMP(8)
NX_VIRTUAL_SLOT_JUMP(9)
NX_VIRTUAL_SLOT_JUMP(10)
NX_VIRTUAL_SLOT_JUMP(11)
NX_VIRTUAL_SLOT_JUMP(12)
NX_VIRTUAL_SLOT_JUMP(13)
NX_VIRTUAL_SLOT_JUMP(14)
NX_VIRTUAL_SLOT_JUMP(15)
NX_VIRTUAL_SLOT_JUMP(16)
NX_VIRTUAL_SLOT_JUMP(18)
NX_VIRTUAL_SLOT_JUMP(19)
NX_VIRTUAL_SLOT_JUMP(20)
NX_VIRTUAL_SLOT_JUMP(22)
NX_VIRTUAL_SLOT_JUMP(25)
NX_VIRTUAL_SLOT_JUMP(27)
NX_VIRTUAL_SLOT_JUMP(28)
NX_VIRTUAL_SLOT_JUMP(29)
NX_VIRTUAL_SLOT_JUMP(30)
NX_VIRTUAL_SLOT_JUMP(31)
NX_VIRTUAL_SLOT_JUMP(36)
NX_VIRTUAL_SLOT_JUMP(37)
NX_VIRTUAL_SLOT_JUMP(39)
NX_VIRTUAL_SLOT_JUMP(40)
NX_VIRTUAL_SLOT_JUMP(42)
NX_VIRTUAL_SLOT_JUMP(43)
NX_VIRTUAL_SLOT_JUMP(44)
NX_VIRTUAL_SLOT_JUMP(45)
NX_VIRTUAL_SLOT_JUMP(46)
NX_VIRTUAL_SLOT_JUMP(47)
NX_VIRTUAL_SLOT_JUMP(48)
NX_VIRTUAL_SLOT_JUMP(49)
NX_VIRTUAL_SLOT_JUMP(50)

#undef NX_VIRTUAL_SLOT_JUMP

namespace {
static VtableSlot g_geom_vtable_slots[] = {&render::Geom::teardown, &render::WorldCell::plat_clone, &render::WorldCell::plat_clone,
                &render::Geom::plat_init_instance, stubs::do_nothing, &render::Geom::plat_get_checksum, &render::Geom::plat_set_color,
                &render::Geom::plat_get_color, &render::Geom::plat_clear_color, &render::Geom::plat_set_material_color,
                &render::Geom::plat_get_material_color, &render::Geom::plat_set_visibility, &render::Geom::plat_get_visibility,
                &render::Geom::plat_set_active, &render::Geom::plat_is_active, &render::Geom::plat_get_bounding_box,
                &render::Geom::plat_set_bounding_sphere, &render::Geom::plat_get_bounding_sphere, &render::Geom::plat_set_world_position,
                &render::Geom::plat_get_world_position, &render::Geom::plat_set_orientation, &render::Geom::plat_get_orientation,
                &render::Geom::plat_rotate_y, &render::Geom::plat_set_transform, &render::Geom::plat_get_transform,
                &render::Geom::plat_set_scale, &render::Geom::plat_get_scale, &render::Geom::plat_set_model_lights,
                &render::Geom::plat_render_setup, stubs::ignore_arguments2, stubs::ignore_arguments2,
                &render::Geom::plat_hide_polys, &save_cards::SaveCard::always_true, &render::Geom::plat_get_num_render_polys,
                &render::Geom::plat_get_num_render_base_polys, stubs::return_zero, &render::Geom::plat_get_num_render_verts,
                &render::Geom::plat_get_render_verts, &render::Geom::plat_get_render_colors, &render::Geom::plat_set_render_verts,
                &render::Geom::plat_set_render_colors, stubs::ignore_arguments3, &render::Geom::plat_set_uv_wibble_params,
                &render::Geom::plat_use_explicit_uv_wibble, &render::Geom::plat_set_material_uv_wibble_offsets,
                &render::Geom::plat_set_uv_wibble_offsets, &render::Geom::plat_set_uv_matrix, stubs::return_true2, stubs::do_nothing,
                stubs::ignore_argument, stubs::do_nothing};
void** const kGeomVtable = reinterpret_cast<void**>(g_geom_vtable_slots);
} // namespace

} // namespace Nx
