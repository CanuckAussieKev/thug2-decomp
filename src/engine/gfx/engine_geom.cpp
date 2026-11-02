// The engine's geom: see engine_geom.h.

#include "engine/gfx/engine_geom.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <span>

#include "engine/core/checked_index.h"
#include "engine/core/span_copy.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/core/raw_view.h"

namespace render {

namespace {
// The function-local statics of the stubs below (the boxes they return, and their guards).
maths::Vec4 g_position_box;
uint32_t g_position_guard;
maths::Mat4 g_orientation_box;
uint32_t g_orientation_guard;
maths::Mat4 g_transform_box;
uint32_t g_transform_guard;

// The stubs' messages ("STUB: ...\n").
void get_stub(const char* message) { debug::printf_text(message); }

// A function-local static of the original's: its guard's bit 1 set the first time (the box constructed then).
template <typename T>
T* local_static(T& box, uint32_t& guard)
{
    guard |= 1;
    return &box;
}

} // namespace

// 0x0048E130
Geom::~Geom()
{
    if (render_colours_)
        memory::Hub::get_pull()->dealloc(render_colours_);
}

// 0x0048DD80
Geom* Geom::replicate(int instance, WorldScene* scene)
{
    Geom* clone = plat_replicate(instance, scene); // (the whole word passed on)
    if (clone) {
        clone->clone_kind_ = static_cast<uint8_t>(instance) ? 1 : 2;
        if (hit_triangles_)
            clone->hit_triangles_ = hit_triangles_;
        clone->render_colours_ = nullptr;
    }
    return clone;
}

// 0x0048DDC0
Geom* Geom::clone_for_model(int instance, RenderModel* model)
{
    Geom* clone = clone_instance(instance, model); // (the whole word passed on)
    if (clone) {
        clone->render_colours_ = nullptr;
        clone->clone_kind_ = static_cast<uint8_t>(instance) ? 1 : 2;
    }
    return clone;
}

// 0x0048DDF0
bool Geom::init_instance(SubMesh* mesh, RenderModel* model, int multicolour)
{
    clone_kind_ = 1;
    multicolour_ = static_cast<uint8_t>(multicolour);
    // (the original jumps with its own arguments: the whole word of the third)
    return plat_init_instance(mesh, model, multicolour);
}

// 0x0048DE10
uint32_t* Geom::fetch_tint(uint32_t* out)
{
    plat_get_color(out);
    return out;
}

// 0x0048DE90
maths::Vec4* Geom::fetch_bound_sphere(maths::Vec4* out)
{
    plat_get_bounding_sphere(out);
    return out;
}

// 0x0048DF10
void Geom::choose_tri_source(void* faces) { hit_triangles_ = faces; }

// 0x0048E150
void Geom::fetch_tints(std::span<uint32_t> out)
{
    const uint32_t count = fetch_vertex_tally();
    if (!render_colours_) {
        memory::Hub* manager = memory::Hub::get_pull();
        manager->enter_heap_scope(manager->front_end_heap_);
        render_colours_ = static_cast<uint32_t*>(memory::alloc_block(count * 4));
        memory::Hub::get_pull()->leave_heap_scope();
        plat_get_render_colors({render_colours_, count});
    }
    core::copy_into(std::span<const uint32_t>(render_colours_, count), out);
}

// --- the base's platform calls ----------------------------------------------------------------------------------

// 0x0049E8E0 (folded with WorldCell::plat_clone)
Geom* Geom::clone_instance(int, RenderModel*)
{
    get_stub("STUB: PlatClone\n"); // "STUB: PlatClone\n"
    return nullptr;
}
Geom* Geom::plat_replicate(int, WorldScene*)
{
    get_stub("STUB: PlatClone\n");
    return nullptr;
}

// 0x0048DA90
bool Geom::plat_init_instance(SubMesh*, RenderModel*, int)
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
void Geom::change_tint(uint32_t) { get_stub("STUB: PlatSetColor\n"); } // "STUB: PlatSetColor\n"
// 0x0048E040
uint32_t* Geom::plat_get_color(uint32_t* out)
{
    get_stub("STUB: PlatGetColor\n"); // "STUB: PlatGetColor\n"
    std::memset(out, 0, 4);
    return out;
}
// 0x0048DAD0
void Geom::wipe_tint() { get_stub("STUB: PlatClearColor\n"); } // "STUB: PlatClearColor\n"
// 0x0048DAE0
bool Geom::change_material_tint(uint32_t, int, uint32_t)
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
void Geom::toggle_shown(uint32_t) { get_stub("STUB: PlatSetVisibility\n"); } // "STUB: PlatSetVisibility\n"
// 0x0048DB10
uint32_t Geom::fetch_shown()
{
    get_stub("STUB: PlatGetVisibility\n"); // "STUB: PlatGetVisibility\n"
    return 0;
}
// 0x0048DB20
void Geom::change_live(bool) { get_stub("STUB: PlatSetActive\n"); } // "STUB: PlatSetActive\n"
// 0x0048DB30
bool Geom::is_operative()
{
    get_stub("STUB: PlatIsActive\n"); // "STUB: PlatIsActive\n"
    return false;
}
namespace {
// (all zero at start, as the original's: the first PlatGetBoundingBox makes it empty)
maths::BBox g_plat_bounding_box{maths::BBox(maths::Vec4{.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f})};
uint32_t g_plat_bounding_box_guard; // (bit 0: made)
} // namespace

// 0x0048DB40 (the box 0x006FAA08, its guard 0x006FAA28: made empty the first time)
maths::BBox* Geom::fetch_bounds()
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
void Geom::change_bound_sphere(const maths::Vec4*)
{
    get_stub("STUB: PlatSetBoundingSphere\n");
} // "STUB: PlatSetBoundingSphere\n"
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
void Geom::place_at_absolute_point(const maths::Vec4*)
{
    get_stub("STUB: PlatSetWorldPosition\n");
} // "STUB: PlatSetWorldPosition\n"
// 0x0048E0D0 (the vector 0x006FAA2C, its guard 0x006FAA3C: nothing to construct)
maths::Vec4* Geom::get_absolute_point()
{
    maths::Vec4* position = local_static(g_position_box, g_position_guard);
    get_stub("STUB: PlatGetWorldPosition\n"); // "STUB: PlatGetWorldPosition\n"
    return position;
}
// 0x0048DBC0
void Geom::change_facing(const maths::Mat4*) { get_stub("STUB: PlatSetOrientation\n"); } // "STUB: PlatSetOrientation\n"
// 0x0048E1C0 (the matrix 0x006FAA40, its guard 0x006FAA80)
maths::Mat4* Geom::fetch_facing()
{
    maths::Mat4* orientation = local_static(g_orientation_box, g_orientation_guard);
    get_stub("STUB: PlatGetOrientation\n"); // "STUB: PlatGetOrientation\n"
    return orientation;
}
// 0x0048DBD0
void Geom::rotate_y(int) { get_stub("STUB: PlatRotateY\n"); } // "STUB: PlatRotateY\n"
// 0x0048DBE0
void Geom::plat_set_transform(const maths::Mat4*)
{
    get_stub("STUB: PlatSetTransform\n");
} // "STUB: PlatSetTransform\n"
// 0x0048E1F0 (the matrix 0x006FAA88, its guard 0x006FAAC8)
maths::Mat4* Geom::plat_get_transform()
{
    maths::Mat4* transform = local_static(g_transform_box, g_transform_guard);
    get_stub("STUB: PlatGetTransform\n"); // "STUB: PlatGetTransform\n"
    return transform;
}
// 0x0048DBF0
void Geom::change_scale(const maths::Vec4*) { get_stub("STUB: PlatSetScale\n"); } // "STUB: PlatSetScale\n"
// 0x0048E100
maths::Vec4* Geom::fetch_magnification(maths::Vec4* out)
{
    get_stub("STUB: PlatGetScale\n"); // "STUB: PlatGetScale\n"
    out->x = 0.0f;
    out->y = 0.0f;
    out->z = 0.0f;
    out->w = 0.0f;
    return out;
}
// 0x0048DC00
void Geom::set_model_lights(void*) { get_stub("STUB: PlatSetModelLights\n"); } // "STUB: PlatSetModelLights\n"
// 0x0048DC10
bool Geom::upload_skinning_palette(const maths::Mat4*, const maths::Mat4*, int, int, int)
{
    get_stub("STUB: PlatRenderSetup\n"); // "STUB: PlatRenderSetup\n"
    return true;
}
// 0x0048DC40
bool Geom::break_strips(uint32_t)
{
    get_stub("STUB: PlatHidePolys\n"); // "STUB: PlatHidePolys\n"
    return true;
}
// 0x0048DC60
uint32_t Geom::fetch_vertex_tally()
{
    get_stub("STUB: CGeom::plat_get_num_render_verts\n"); // "STUB: CGeom::plat_get_num_render_verts\n"
    return 0;
}
// 0x0048DC70
void Geom::get_vertices(std::span<maths::Vec4>)
{
    get_stub("STUB: CGeom::plat_get_render_verts\n");
} // "STUB: CGeom::plat_get_render_verts\n"
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
void Geom::plat_get_render_colors(std::span<uint32_t>)
{
    get_stub("STUB: CGeom::plat_get_render_colors\n");
} // "STUB: CGeom::plat_get_render_colors\n"
// 0x0048DCB0
void Geom::set_vertices(std::span<const maths::Vec4>)
{
    get_stub("STUB: CGeom::plat_set_render_verts\n");
} // "STUB: CGeom::plat_set_render_verts\n"
// 0x0048DCC0
void Geom::change_colors(std::span<const uint32_t>)
{
    get_stub("STUB: CGeom::plat_set_render_colors\n");
} // "STUB: CGeom::plat_set_render_colors\n"
// 0x0048DCE0
void Geom::set_uv_wibble_params(float, float, float, float, float, float, float, float)
{
    get_stub("STUB: CGeom::plat_set_uv_wibble_params\n"); // "STUB: CGeom::plat_set_uv_wibble_params\n"
}
// 0x0048DCF0
void Geom::enable_explicit_uv_wibble(int)
{
    get_stub("STUB: CGeom::plat_use_explicit_uv_wibble\n");
} // "STUB: CGeom::plat_use_explicit_uv_wibble\n"
// 0x0048DD10
bool Geom::set_material_uv_wibble_offsets(uint32_t, int, float, float)
{
    get_stub("STUB: CGeom::plat_set_uv_wibble_offsets\n"); // "STUB: CGeom::plat_set_uv_wibble_offsets\n"
    return false;
}
// 0x0048DD00
void Geom::set_uv_wibble_offsets(float, float)
{
    get_stub("STUB: CGeom::plat_set_uv_wibble_offsets\n");
} // "STUB: CGeom::plat_set_uv_wibble_offsets\n"
// 0x0048DD30
bool Geom::choose_uv_matrix(uint32_t, int, const maths::Mat4*)
{
    get_stub("STUB: CGeom::plat_set_uv_matrix\n"); // "STUB: CGeom::plat_set_uv_matrix\n"
    return false;
}

} // namespace render
