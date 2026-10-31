// The engine's texture projection (see engine_texture_projection.h). The x87 arithmetic is followed at 53 bits: what
// the original keeps on the FPU stays a double here, what it stores is rounded to a float there.

#include "engine/gfx/engine_texture_projection.h"
#include "engine/gfx/d3dx.h"

#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/texture_dict.h"
#include "engine/gfx/viewport.h"
#include "engine/maths/shape_tests.h"
#include "engine/render/coll_grid.h"
#include "engine/render/coll_obj.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/core/game_vtables.h"
#include "engine/objects/texture_projection_component.h"
#include "engine/gfx/pc_texture_projection.h"
#include "game/skate/skate_module.h"

namespace render {

namespace {

extern void** const kProjectionVtable;
void** const& g_projection_node_vtable = vt::kProjectionNode;

float get_f(double value) { return static_cast<float>(value); }

// The camera of the first active viewport (0x004A09C0; null if none) and its position (0x00485A00).
void* first_camera() { return fetch_live_camera(0); }
const maths::Vec4* viewer_location(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_location(); }
// Whether a sphere is out of the camera's view (0x0048C490).
bool sphere_out_of_view(const maths::Vec4* centre, float radius)
{
    return engine::sphere_visible_in_one_viewport(centre, radius) != 0;
}
// The collision grids a line reaches (0x0048C980: null-terminated), and the sector of a name in any scene
// (0x0048C510).
CollGrid** grids_along_line(const maths::Seg* line) { return get_coll_grids_on_line(line); }
void* find_sector(uint32_t name) { return engine::look_up_sector(name); }

void* g_projected_sectors[128];
void* g_projected_collision[128];

// The sectors and collision objects a projection reaches (0x00700F40 / 0x00701140, null-terminated).
void** projected_sectors() { return g_projected_sectors; }
void** projected_collision() { return g_projected_collision; }

} // namespace

// 0x0049FE20
TextureProjection* TextureProjection::make_new()
{
    self_link_ = reinterpret_cast<memory::ListNode*>(this);
    node_priority_ = 0;
    succ_ = reinterpret_cast<memory::ListNode*>(this);
    last_value_ = reinterpret_cast<memory::ListNode*>(this);
    vtable = reinterpret_cast<void**>(kProjectionVtable);
    label_ = 0;
    bitmap_ = nullptr;
    blend_mode_ = 0;
    fineness_ = 0;
    projection_.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    projection_.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    projection_.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    projection_.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    extent_ = 0.0f;
    field_of_view_ = 0.0f;
    aspect_ = 0.0f;
    direction_ = {0.0f, 0.0f, 0.0f, 1.0f};
    anchor_point_ = {0.0f, 0.0f, 0.0f, 1.0f};
    range_ = 0.0f;
    unknowna4_ = 0;
    shift_ = {0.0f, 0.0f, 0.0f, 1.0f};
    angle_ = 0.0f;
    unknownbc_ = 0.0f;
    view_distance_ = 0.0f;
    flag_bits_ = 0;
    return this;
}

// 0x004A04A0
void TextureProjection::destroy_body()
{
    reinterpret_cast<memory::ListNode*>(this)->unlink_as(g_projection_node_vtable);
}

// 0x0049FE10
void TextureProjection::plat_project(void**, void**)
{
    debug::printf("STUB: plat_update\n"); // "STUB: plat_update..."
}

// 0x004A04C0
void TextureProjection::configure_from_data(script::Struct* params)
{
    auto vector = [](maths::Vec4* v) { return reinterpret_cast<script::VectorValue*>(v); };
    extent_ = 0.0f;
    field_of_view_ = 60.0f;
    aspect_ = 1.0f;
    direction_ = {0.0f, 0.0f, 0.0f, 1.0f};
    params->fetch_vec(0xB1DE1E91, vector(&direction_), 1);
    direction_.make_unit();
    world_direction_ = direction_;
    range_ = 0.0f;
    params->fetch_float(0xDF61A4BD, &range_, 1);
    if (params->fetch_float(0x083FDB95, &extent_, 0)) {
        flag_bits_ = (flag_bits_ & ~kProjectionPerspective) | kProjectionOrthographic;
    } else if (params->fetch_float(0x17E7321E, &field_of_view_, 0)) {
        params->fetch_float(0x0F7C3CAA, &aspect_, 0);
        flag_bits_ = (flag_bits_ & ~kProjectionOrthographic) | kProjectionPerspective;
    }
    unknowna4_ = 0;
    params->read_hash_value(0xDC46BA66, &unknowna4_, 0);
    shift_ = {0.0f, 0.0f, 0.0f, 1.0f};
    params->fetch_vec(0xA6F5352F, vector(&shift_), 0);
    angle_ = 0.0f;
    params->fetch_float(0xFF7EBAF6, &angle_, 0);
    angle_ = get_f(static_cast<double>(angle_) * 0.0174532924f); // degrees to radians (0x00645464)
    uint32_t texture = 0;
    params->read_hash_value(0x7D99F28D, &texture, 1);
    blend_mode_ = 0x54628ED7; // "blend"
    params->read_hash_value(0xDD6BB3D5, &blend_mode_, 0);
    int fineness = 0;
    params->fetch_int_value(0x12602598, &fineness, 0);
    fineness_ = static_cast<uint32_t>(fineness);
    bitmap_ = reinterpret_cast<uint8_t*>(g_particle_texture_dict->fetch_skin(texture));
    int flag = 0;
    params->fetch_int_value(0xAF06447B, &flag, 0);
    if (flag)
        flag_bits_ |= kProjectionFlag1;
    else
        flag_bits_ &= ~kProjectionFlag1;
    unknownbc_ = 100000.0f;
    params->fetch_float(0xE979B3D3, &unknownbc_, 0);
    view_distance_ = 15000.0f;
    params->fetch_float(0x7FCAEAB1, &view_distance_, 0);
    update_matrix();
}

// 0x0049FEF0
void TextureProjection::update_matrix()
{
    maths::Vec4 target = {get_f(static_cast<double>(world_direction_.x) + anchor_point_.x),
                          get_f(static_cast<double>(world_direction_.y) + anchor_point_.y),
                          get_f(static_cast<double>(world_direction_.z) + anchor_point_.z),
                          get_f(static_cast<double>(world_direction_.w) + anchor_point_.w)};
    maths::Vec4 up = {0.0f, 1.0f, 0.0f, 0.0f};
    maths::Mat4 view;
    maths::create_look_at_matrix(view, anchor_point_, target, up);
    maths::Mat4 lens;
    if (flag_bits_ & kProjectionOrthographic)
        maths::create_orthographic_matrix(lens, extent_, extent_, 0.1f, range_);
    else if (flag_bits_ & kProjectionPerspective)
        maths::create_perspective_matrix(lens, field_of_view_, aspect_, 0.1f, range_);
    else
        return;
    // (D3DXMatrixMultiply)
    d3dx::matrix_multiply(&projection_, &view, &lens);
}

// 0x0049FFE0
void TextureProjection::tick_frame(objects::GameEntity* object)
{
    maths::Mat4 m;
    m = object->display_matrix_;
    const maths::Vec4& pos = object->at_point_;
    double ox = shift_.x, oy = shift_.y, oz = shift_.z;
    // the position: the object's plus the offset through its matrix (x kept at 53 bits but for the third row's
    // term; y, z, w rounded at each step; w's third term unrounded)
    double x = static_cast<double>(m.row[0].x) * ox + pos.x;
    x += static_cast<double>(m.row[1].x) * oy;
    x += get_f(static_cast<double>(m.row[2].x) * oz);
    auto rounded = [&](int c) {
        float a = get_f(get_f(get_f((&m.row[0].x)[c] * ox) + static_cast<double>((&pos.x)[c])) +
                        static_cast<double>(get_f((&m.row[1].x)[c] * oy)));
        return a;
    };
    float y = get_f(static_cast<double>(rounded(1)) + get_f(static_cast<double>(m.row[2].y) * oz));
    float z = get_f(static_cast<double>(rounded(2)) + get_f(static_cast<double>(m.row[2].z) * oz));
    float w = get_f(static_cast<double>(rounded(3)) + static_cast<double>(m.row[2].w) * oz);
    anchor_point_ = {get_f(x), y, z, w};
    world_direction_ = direction_ * m;

    if (modules::g_viewport_count == 1 && first_camera()) {
        const maths::Vec4* camera = viewer_location(first_camera());
        double dx = static_cast<double>(anchor_point_.x) - camera->x;
        double dy = static_cast<double>(anchor_point_.y) - camera->y;
        double dz = static_cast<double>(anchor_point_.z) - camera->z;
        double reach = view_distance_;
        if (!(reach * reach > dx * dx + dz * dz + dy * dy))
            return; // (too far, or NaN)
        // a sphere half the range ahead - along the object-space direction, as the original -, as big as that plus
        // the size
        double half = static_cast<double>(range_) * 0.5f;
        maths::Vec4 centre = {get_f(static_cast<double>(get_f(half * direction_.x)) + anchor_point_.x),
                              get_f(static_cast<double>(get_f(half * direction_.y)) + anchor_point_.y),
                              get_f(static_cast<double>(get_f(half * direction_.z)) + anchor_point_.z),
                              get_f(half * direction_.w + anchor_point_.w)};
        if (sphere_out_of_view(&centre, get_f(half + extent_)))
            return;
    }
    update_matrix();
    // the line along the range, its box grown by 2.5 sizes
    double range = range_;
    maths::Vec4 end = {get_f(range * world_direction_.x + anchor_point_.x),
                       get_f(static_cast<double>(get_f(range * world_direction_.y)) + anchor_point_.y),
                       get_f(static_cast<double>(get_f(range * world_direction_.z)) + anchor_point_.z),
                       get_f(static_cast<double>(get_f(range * world_direction_.w)) + anchor_point_.w)};
    maths::Seg line(anchor_point_, end);
    maths::BBox box(line.start);
    box.append_spot(line.end);
    double grow = static_cast<double>(extent_) * 2.5f; // (0x00649C1C)
    maths::BBox reach;
    reach.min = {get_f(box.min.x - grow), get_f(box.min.y - grow), get_f(box.min.z - grow), 1.0f};
    grow = static_cast<double>(extent_) * 2.5f;
    reach.max = {get_f(box.max.x + grow), get_f(box.max.y + grow), get_f(box.max.z + grow), 1.0f};
    CollGrid** grids = grids_along_line(&line);
    int count = 0;
    for (CollGrid** grid = grids; *grid; ++grid) {
        HitMesh** objects = (*grid)->objects_in_box(&reach);
        if (!objects)
            return; // (nothing projected - even what earlier grids found)
        for (HitMesh** found = objects; *found; ++found) {
            const auto* bounds = (*found)->get_call<const maths::BBox*>(21);
            // (touching: no comparison says apart - NaNs count as touching)
            if (bounds->min.x > reach.max.x || reach.min.x > bounds->max.x || bounds->min.z > reach.max.z ||
                reach.min.z > bounds->max.z || bounds->min.y > reach.max.y || reach.min.y > bounds->max.y)
                continue;
            void* sector = find_sector((*found)->checksum_);
            projected_collision()[count] = *found;
            projected_sectors()[count] = sector;
            ++count;
        }
    }
    if (count > 0) {
        projected_sectors()[count] = nullptr;
        projected_collision()[count] = nullptr;
        reinterpret_cast<void(__thiscall*)(TextureProjection*, void**, void**)>(vtable[1])(this, projected_sectors(),
                                                                                           projected_collision());
    }
}

namespace {
static VtableSlot g_projection_vtable_slots[] = {&render::PcTextureProjection::teardown,
                                                 &render::TextureProjection::plat_project};
void** const kProjectionVtable = reinterpret_cast<void**>(g_projection_vtable_slots);
} // namespace

} // namespace render

namespace vt {
static VtableSlot g_projection_node_slots[] = {&objects::TextureProjectionComponent::ProjectionList::teardown};
void** const kProjectionNode = reinterpret_cast<void**>(g_projection_node_slots);
} // namespace vt
