// The engine's spawned polygons: see spawned_polys.h. The maths follows the original's x87 code (53-bit precision:
// double where it keeps values in registers, float where it stores them).

#include "engine/gfx/spawned_polys.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/gfx/engine_primitive.h"
#include "engine/gfx/pc_primitive.h"
#include "engine/lists/key_map.h"
#include "engine/script/cfuncs_engine.h"
#include "platform/stopwatch.h"
#include "engine/gfx/engine_effects.h"
#include "engine/core/pointer_cell.h"

namespace render {

memory::PoolPair g_spawned_poly_pools;

namespace {

lists::KeyMap* spawned_polys() { return g_spawned_polys; }

void* next_value(lists::KeyMap* table)
{
    if (!table->step_iterator() || !table->iter_item_)
        return nullptr;
    return table->iter_item_->value;
}
void* get_first_value(lists::KeyMap* table)
{
    table->iter_bucket_ = -1;
    table->iter_item_ = nullptr;
    return next_value(table);
}

// A corner's position and colour.
void set_corner(PcPrimitive* primitive, int corner, const maths::Vec4& at)
{
    primitive->change_place(corner, at.x, at.y, at.z);
}
void set_corner_colour(PcPrimitive* primitive, int corner, const uint8_t* colour)
{
    uint32_t rgba = 0;
    std::memcpy(&rgba, colour, 4);
    primitive->set_vertex_colour(corner, rgba);
}

// Four corners turned by the polygon's matrix and moved to its position.
void place_corners(SpawnedPoly* poly, maths::Vec4* corners)
{
    for (int i = 0; i < 4; ++i)
        corners[i].multiply_by(poly->matrix);
    for (int i = 0; i < 4; ++i) {
        corners[i].x = static_cast<float>(static_cast<double>(corners[i].x) + poly->pos.x);
        corners[i].y = static_cast<float>(static_cast<double>(corners[i].y) + poly->pos.y);
        corners[i].z = static_cast<float>(static_cast<double>(corners[i].z) + poly->pos.z);
    }
}

} // namespace

// 0x00491990
SpawnedPoly::SpawnedPoly()
{
    std::memset(this, 0, sizeof(*this));
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    align_h_ = 0.0f;
    align_v_ = 0.0f;
    blend_mode_ = 0x54628ED7;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

namespace engine {

// 0x00492450
void spawn_poly(SpawnedPoly* poly, maths::Vec4* direction, maths::Vec4* up)
{
    poly->primitive = new PcPrimitive(1);
    maths::Mat4& m = poly->matrix;
    std::memset(&m, 0, sizeof(m));
    m.row[0].x = m.row[1].y = m.row[2].z = m.row[3].w = 1.0f;
    direction->make_unit();
    up->make_unit();
    m.row[0] = *direction;
    // Row 2 across the direction and up, row 1 across row 2 and the direction.
    const maths::Vec4& r0 = m.row[0];
    const double s0 = static_cast<double>(r0.z) * up->y - static_cast<double>(r0.y) * up->z;
    const double s1 = static_cast<double>(up->z) * r0.x - static_cast<double>(r0.z) * up->x;
    const double s2 = static_cast<double>(r0.y) * up->x - static_cast<double>(up->y) * r0.x;
    m.row[2].w = 0.0f;
    m.row[2].z = static_cast<float>(s2);
    m.row[2].x = static_cast<float>(s0);
    m.row[2].y = static_cast<float>(s1);
    const maths::Vec4& r2 = m.row[2];
    const double t0 = static_cast<double>(r2.y) * r0.z - static_cast<double>(r2.z) * r0.y;
    const double t1 = static_cast<double>(r0.x) * r2.z - static_cast<double>(r2.x) * r0.z;
    const double t2 = static_cast<double>(r2.x) * r0.y - static_cast<double>(r0.x) * r2.y;
    m.row[1].w = 0.0f;
    m.row[1].z = static_cast<float>(t2);
    m.row[1].x = static_cast<float>(t0);
    m.row[1].y = static_cast<float>(t1);
    uint32_t layout = 1; // (colours; 3: and texture coordinates)
    if (poly->texture) {
        poly->primitive->set_texture(poly->texture, 0, 0); // (through the jump 0x004A1090)
        layout = 3;
    }
    std::memcpy(poly->colours.data(), poly->start_colours_.data(), sizeof(poly->colours));
    const float height = poly->start_dims_[1];
    poly->dims[1] = height;
    const float width = poly->start_dims_[0];
    poly->dims[0] = width;
    poly->age = 0.0f;
    // The corners about the centre (w: see the header).
    const double left = width * -0.5;
    const double top = height * -0.5;
    const double right = width * 0.5;
    const double bottom = height * 0.5;
    std::array<maths::Vec4, 4> corners = {{{static_cast<float>(left), static_cast<float>(top), 0.0f, 0.0f},
                              {static_cast<float>(right), static_cast<float>(top), 0.0f, 0.0f},
                              {static_cast<float>(right), static_cast<float>(bottom), 0.0f, 0.0f},
                              {static_cast<float>(left), static_cast<float>(bottom), 0.0f, 0.0f}}};
    place_corners(poly, corners.data());
    PcPrimitive* primitive = poly->primitive;
    primitive->blend_mode_ = poly->blend_mode_;
    primitive->prepare_all(layout, 4, 4); // (through the jump 0x004A10A0)
    const std::array<std::array<float, 2>, 4> uvs = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
    for (int i = 0; i < 4; ++i) {
        set_corner_colour(poly->primitive, i, poly->colours[i].data());
        if (layout & 2)
            poly->primitive->set_uv(i, uvs[i][0], uvs[i][1]);
        set_corner(poly->primitive, i, corners[i]);
    }
    poly->primitive->unknown14 = (poly->flags >> 1) & 1;
    poly->primitive->get_ready();
    auto key = cell::of(poly);
    spawned_polys()->store_entry(key, poly);
}

// 0x004919C0
void update_spawned_polys()
{
    auto elapsed = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    for (void* value = get_first_value(spawned_polys()); value;) {
        auto* poly = static_cast<SpawnedPoly*>(value);
        if (poly->flags & 1) {
            value = next_value(spawned_polys());
            continue;
        }
        const double age = static_cast<double>(elapsed) + poly->age;
        poly->age = static_cast<float>(age);
        if (age >= poly->lifetime) {
            // Done: its primitive destroyed, the polygon taken out and freed.
            poly->flags |= 1;
            poly->age = 0.0f;
            value = next_value(spawned_polys());
            delete poly->primitive;
            poly->primitive = nullptr;
            auto key = cell::of(poly);
            spawned_polys()->erase_entry(key);
            commands::free_spawned_poly_details(poly);
            continue;
        }
        const double t = age / poly->lifetime;
        for (int c = 0; c < 4; ++c) {
            for (int k = 0; k < 4; ++k) {
                const int change = static_cast<int>(static_cast<int64_t>(
                    static_cast<double>(poly->end_colours_[c][k] - poly->start_colours_[c][k]) * t));
                poly->colours[c][k] = static_cast<uint8_t>(poly->start_colours_[c][k] + change);
            }
        }
        for (int i = 0; i < 2; ++i)
            poly->dims[i] = static_cast<float>((static_cast<double>(poly->end_dims_[i]) - poly->start_dims_[i]) * t +
                                               poly->start_dims_[i]);
        for (int c = 0; c < 4; ++c)
            set_corner_colour(poly->primitive, c, poly->colours[c].data());
        // Where the corners go, by the justification: right / bottom of the position, centred, or left / top.
        float x_offset = 0.0f;
        if (poly->align_h_ > 0.0f)
            x_offset = poly->dims[0];
        else if (poly->align_h_ == 0.0f)
            x_offset = static_cast<float>(poly->dims[0] * 0.5);
        double y_offset = 0.0;
        if (poly->align_v_ > 0.0f)
            y_offset = poly->dims[1];
        else if (poly->align_v_ == 0.0f)
            y_offset = poly->dims[1] * 0.5;
        float left = -x_offset;
        auto top = static_cast<float>(-y_offset);
        auto right = static_cast<float>(static_cast<double>(left) + poly->dims[0]);
        auto bottom = static_cast<float>(static_cast<double>(top) + poly->dims[1]);
        std::array<maths::Vec4, 4> corners = {{
            {left, top, 0.0f, 0.0f}, {right, top, 0.0f, 0.0f}, {right, bottom, 0.0f, 0.0f}, {left, bottom, 0.0f, 0.0f}}};
        place_corners(poly, corners.data());
        for (int c = 0; c < 4; ++c)
            set_corner(poly->primitive, c, corners[c]);
        value = next_value(spawned_polys());
    }
}

// 0x00491E10
void kill_all_spawned_polys()
{
    for (void* value = get_first_value(spawned_polys()); value;) {
        auto* poly = static_cast<SpawnedPoly*>(value);
        value = next_value(spawned_polys());
        delete poly->primitive;
        poly->primitive = nullptr;
        auto key = cell::of(poly);
        spawned_polys()->erase_entry(key);
        g_spawned_poly_pools.dealloc(poly);
    }
}

} // namespace engine

} // namespace render
