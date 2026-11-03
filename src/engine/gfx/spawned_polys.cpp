// The engine's spawned polygons: see spawned_polys.h. The maths follows the original's x87 code (53-bit precision:
// double where it keeps values in registers, float where it stores them).

#include "engine/gfx/spawned_polys.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <array>

#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/gfx/pc_primitive.h"
#include "engine/lists/object_set.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/script/cfuncs_engine.h"
#include "platform/stopwatch.h"
#include "engine/gfx/engine_effects.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/effects_state.h"
#include "engine/core/packed_colour.h"

namespace render {

constexpr double kHalf = 0.5;

memory::PoolPair& spawned_poly_pools()
{
    static core::NoDestroy<memory::PoolPair> spawned_poly_pools;
    return spawned_poly_pools.get();
}

namespace {

// A corner's position and colour.
void set_corner(PcPrimitive* primitive, int corner, const maths::Vec4& at)
{
    primitive->change_place(corner, {.x = at.x, .y = at.y, .z = at.z});
}
void set_corner_colour(PcPrimitive* primitive, int corner, const std::array<uint8_t, 4>& colour)
{
    uint32_t rgba = 0;
    std::memcpy(&rgba, colour.data(), 4);
    primitive->set_vertex_colour(corner, colour::Packed{rgba});
}

// Four corners turned by the polygon's matrix and moved to its position.
void place_corners(SpawnedPoly* poly, std::array<maths::Vec4, 4>& corners)
{
    for (maths::Vec4& corner : corners)
        corner.multiply_by(poly->matrix());
    for (maths::Vec4& corner : corners) {
        corner.x = static_cast<float>(static_cast<double>(corner.x) + poly->pos().x);
        corner.y = static_cast<float>(static_cast<double>(corner.y) + poly->pos().y);
        corner.z = static_cast<float>(static_cast<double>(corner.z) + poly->pos().z);
    }
}

} // namespace

// 0x00491990
SpawnedPoly::SpawnedPoly() = default;

namespace engine {

// 0x00492450
void spawn_poly(SpawnedPoly* poly, maths::Vec4* direction, maths::Vec4* up)
{
    auto* made = memory::make_owned<PcPrimitive>(1); // (the polygon's: deleted with it)
    poly->set_primitive(made);
    maths::Mat4& m = poly->matrix();
    std::memset(&m, 0, sizeof(m));
    core::at(m.row, 0).x = core::at(m.row, 1).y = core::at(m.row, 2).z = core::at(m.row, 3).w = 1.0f;
    direction->make_unit();
    up->make_unit();
    core::at(m.row, 0) = *direction;
    // Row 2 across the direction and up, row 1 across row 2 and the direction.
    const maths::Vec4& r0 = core::at(m.row, 0);
    const double s0 = (static_cast<double>(r0.z) * up->y) - (static_cast<double>(r0.y) * up->z);
    const double s1 = (static_cast<double>(up->z) * r0.x) - (static_cast<double>(r0.z) * up->x);
    const double s2 = (static_cast<double>(r0.y) * up->x) - (static_cast<double>(up->y) * r0.x);
    core::at(m.row, 2).w = 0.0f;
    core::at(m.row, 2).z = static_cast<float>(s2);
    core::at(m.row, 2).x = static_cast<float>(s0);
    core::at(m.row, 2).y = static_cast<float>(s1);
    const maths::Vec4& r2 = core::at(m.row, 2);
    const double t0 = (static_cast<double>(r2.y) * r0.z) - (static_cast<double>(r2.z) * r0.y);
    const double t1 = (static_cast<double>(r0.x) * r2.z) - (static_cast<double>(r2.x) * r0.z);
    const double t2 = (static_cast<double>(r2.x) * r0.y) - (static_cast<double>(r0.x) * r2.y);
    core::at(m.row, 1).w = 0.0f;
    core::at(m.row, 1).z = static_cast<float>(t2);
    core::at(m.row, 1).x = static_cast<float>(t0);
    core::at(m.row, 1).y = static_cast<float>(t1);
    uint32_t layout = 1; // (colours; 3: and texture coordinates)
    if (poly->texture()) {
        poly->primitive()->set_texture(poly->texture(), 0, 0); // (through the jump 0x004A1090)
        layout = 3;
    }
    std::memcpy(poly->colours().data(), poly->start_colours().data(), sizeof(poly->colours()));
    const float height = core::at(poly->start_dims(), 1);
    core::at(poly->dims(), 1) = height;
    const float width = core::at(poly->start_dims(), 0);
    core::at(poly->dims(), 0) = width;
    poly->set_age(0.0f);
    // The corners about the centre (w: see the header).
    const double left = width * -0.5;
    const double top = height * -0.5;
    const double right = width * 0.5;
    const double bottom = height * 0.5;
    std::array<maths::Vec4, 4> corners = {{{.x = static_cast<float>(left), .y = static_cast<float>(top), .z = 0.0f, .w = 0.0f},
                              {.x = static_cast<float>(right), .y = static_cast<float>(top), .z = 0.0f, .w = 0.0f},
                              {.x = static_cast<float>(right), .y = static_cast<float>(bottom), .z = 0.0f, .w = 0.0f},
                              {.x = static_cast<float>(left), .y = static_cast<float>(bottom), .z = 0.0f, .w = 0.0f}}};
    place_corners(poly, corners);
    PcPrimitive* primitive = poly->primitive();
    primitive->set_blend_mode(poly->blend_mode());
    primitive->prepare_all({.layout_flags_ = layout, .primitive_type_ = 4, .count = 4}); // (through the jump 0x004A10A0)
    const std::array<std::array<float, 2>, 4> uvs = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
    for (int i = 0; i < 4; ++i) {
        set_corner_colour(poly->primitive(), i, core::at(poly->colours(), i));
        if (layout & 2)
            poly->primitive()->set_uv(i, {.x = core::at(core::at(uvs, i), 0), .y = core::at(core::at(uvs, i), 1)});
        set_corner(poly->primitive(), i, core::at(corners, i));
    }
    poly->primitive()->set_unknown14((poly->flags() >> 1) & 1);
    poly->primitive()->get_ready();
    effects_state().spawned_polys().insert(poly);
}

// 0x004919C0
void update_spawned_polys()
{
    auto elapsed = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    effects_state().spawned_polys().for_each([elapsed](SpawnedPoly* poly) {
        if (poly->flags() & 1)
            return;
        const double age = static_cast<double>(elapsed) + poly->age();
        poly->set_age(static_cast<float>(age));
        if (age >= poly->lifetime()) {
            // Done: its primitive destroyed, the polygon taken out and freed.
            poly->flags() |= 1;
            poly->set_age(0.0f);
            gsl::owner<PcPrimitive*> primitive = memory::owned_element(poly->primitive());
            delete primitive;
            poly->set_primitive(nullptr);
            effects_state().spawned_polys().erase(poly);
            commands::free_spawned_poly_details(poly);
            return;
        }
        const double t = age / poly->lifetime();
        for (int c = 0; c < 4; ++c) {
            for (int k = 0; k < 4; ++k) {
                const int change = static_cast<int>(static_cast<int64_t>(
                    static_cast<double>(core::at(core::at(poly->end_colours(), c), k) - core::at(core::at(poly->start_colours(), c), k)) * t));
                core::at(core::at(poly->colours(), c), k) = static_cast<uint8_t>(core::at(core::at(poly->start_colours(), c), k) + change);
            }
        }
        for (int i = 0; i < 2; ++i)
            core::at(poly->dims(), i) = static_cast<float>(
                ((static_cast<double>(core::at(poly->end_dims(), i)) - core::at(poly->start_dims(), i)) * t) +
                core::at(poly->start_dims(), i));
        for (int c = 0; c < 4; ++c)
            set_corner_colour(poly->primitive(), c, core::at(poly->colours(), c));
        // Where the corners go, by the justification: right / bottom of the position, centred, or left / top.
        float x_offset = 0.0f;
        if (poly->align_h() > 0.0f)
            x_offset = core::at(poly->dims(), 0);
        else if (poly->align_h() == 0.0f)
            x_offset = static_cast<float>(core::at(poly->dims(), 0) * kHalf);
        double y_offset = 0.0;
        if (poly->align_v() > 0.0f)
            y_offset = core::at(poly->dims(), 1);
        else if (poly->align_v() == 0.0f)
            y_offset = core::at(poly->dims(), 1) * kHalf;
        const float left = -x_offset;
        auto top = static_cast<float>(-y_offset);
        auto right = static_cast<float>(static_cast<double>(left) + core::at(poly->dims(), 0));
        auto bottom = static_cast<float>(static_cast<double>(top) + core::at(poly->dims(), 1));
        std::array<maths::Vec4, 4> corners = {{
            {.x = left, .y = top, .z = 0.0f, .w = 0.0f}, {.x = right, .y = top, .z = 0.0f, .w = 0.0f}, {.x = right, .y = bottom, .z = 0.0f, .w = 0.0f}, {.x = left, .y = bottom, .z = 0.0f, .w = 0.0f}}};
        place_corners(poly, corners);
        for (int c = 0; c < 4; ++c)
            set_corner(poly->primitive(), c, core::at(corners, c));
    });
}

// 0x00491E10
void kill_all_spawned_polys()
{
    effects_state().spawned_polys().for_each([](SpawnedPoly* poly) {
        gsl::owner<PcPrimitive*> primitive = memory::owned_element(poly->primitive());
        delete primitive;
        poly->set_primitive(nullptr);
        effects_state().spawned_polys().erase(poly);
        spawned_poly_pools().dealloc(poly);
    });
}

} // namespace engine

} // namespace render
