// The engine's texture projection (see engine_texture_projection.h). The x87 arithmetic is followed at 53 bits: what
// the original keeps on the FPU stays a double here, what it stores is rounded to a float there.

#include "engine/gfx/engine_texture_projection.h"
#include "platform/d3dx.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

#include "engine/core/dbg.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/texture_dict.h"
#include "engine/gfx/viewport.h"
#include "engine/maths/mat4.h"
#include "engine/maths/shape_tests.h"
#include "engine/render/coll_grid.h"
#include "engine/render/coll_obj.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "engine/objects/texture_projection_component.h"
#include "game/skate/skate_module.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/engine_parts.h"
#include "engine/maths/vec4.h"

namespace render {

constexpr std::size_t kProjectedSlotCount = 128;
constexpr float kDefaultFieldOfView = 60.0F;
// Text unknown for these parameter names.
constexpr uint32_t kDirectionKey = 0xB1DE1E91U;
constexpr uint32_t kRangeKey = 0xDF61A4BDU;
constexpr uint32_t kUnknownA4Key = 0xDC46BA66U;
constexpr uint32_t kFinenessKey = 0x12602598U;
constexpr uint32_t kUnknownBcKey = 0xE979B3D3U;
constexpr uint32_t kViewDistanceKey = 0x7FCAEAB1U;
constexpr float kDegreesToRadians = 0.0174532924F;
constexpr float kUnknownBcDefault = 100000.0F;
constexpr float kViewDistanceDefault = 15000.0F;
constexpr float kProjectionNearDepth = 0.1F;
constexpr float kGrowFactor = 2.5F;

// Checksums used in this file (the text each hashes).
constexpr uint32_t kANGLE = 0xFF7EBAF6U;     // ANGLE
constexpr uint32_t kASPECT = 0x0F7C3CAAU;    // ASPECT
constexpr uint32_t kBlend = 0x54628ED7U;     // Blend
constexpr uint32_t kBlendMode = 0xDD6BB3D5U; // BlendMode
constexpr uint32_t kEnabled = 0xAF06447BU;   // Enabled
constexpr uint32_t kFov = 0x17E7321EU;       // Fov
constexpr uint32_t kOffset = 0xA6F5352FU;    // Offset
constexpr uint32_t kSIZE = 0x083FDB95U;      // SIZE
constexpr uint32_t kTexture = 0x7D99F28DU;   // Texture

namespace {

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
std::span<CollGrid* const> grids_along_line(const maths::Seg* line) { return get_coll_grids_on_line(line); }
void* find_sector(uint32_t name) { return engine::look_up_sector(name); }

// NOT ORIGINAL grouping - engine_texture_projection.cpp's state (the original's globals). Made on first use, never
// destroyed (engine_texture_projection_state()).
struct EngineTextureProjectionState {
    std::array<void*, kProjectedSlotCount> projected_sectors_{};
    std::array<void*, kProjectedSlotCount> projected_collision_{};
};

EngineTextureProjectionState& engine_texture_projection_state()
{
    static core::NoDestroy<EngineTextureProjectionState> state;
    return state.get();
}

// The sectors and collision objects a projection reaches (0x00700F40 / 0x00701140, null-terminated).
std::array<void*, kProjectedSlotCount>& projected_sectors() { return engine_texture_projection_state().projected_sectors_; }
std::array<void*, kProjectedSlotCount>& projected_collision() { return engine_texture_projection_state().projected_collision_; }

} // namespace

// 0x0049FE20
TextureProjection::TextureProjection()
{
    list_link_.make_node(this);
    core::at(projection_.row, 0) = {.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    core::at(projection_.row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
    core::at(projection_.row, 2) = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F};
    core::at(projection_.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    direction_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    anchor_point_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    shift_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
}

// 0x004A04A0
TextureProjection::~TextureProjection() { list_link_.uncouple(); }

// 0x0049FE10
void TextureProjection::plat_project(void** /*unused*/, void** /*unused*/)
{
    debug::printf("STUB: plat_update\n"); // "STUB: plat_update..."
}

// 0x004A04C0
void TextureProjection::configure_from_data(script::Struct* params)
{
    auto vector = [](maths::Vec4* v) { return v; };
    extent_ = 0.0F;
    field_of_view_ = kDefaultFieldOfView;
    aspect_ = 1.0F;
    direction_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    params->fetch_vec(kDirectionKey, vector(&direction_), 1);
    direction_.make_unit();
    world_direction_ = direction_;
    range_ = 0.0F;
    params->fetch_float(kRangeKey, &range_, 1);
    if (params->fetch_float(kSIZE, &extent_, 0)) {
        flag_bits_ = (flag_bits_ & ~kProjectionPerspective) | kProjectionOrthographic;
    } else if (params->fetch_float(kFov, &field_of_view_, 0)) {
        params->fetch_float(kASPECT, &aspect_, 0);
        flag_bits_ = (flag_bits_ & ~kProjectionOrthographic) | kProjectionPerspective;
    }
    unknowna4_ = 0;
    params->read_hash_value(kUnknownA4Key, &unknowna4_, 0);
    shift_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    params->fetch_vec(kOffset, vector(&shift_), 0);
    angle_ = 0.0F;
    params->fetch_float(kANGLE, &angle_, 0);
    angle_ = get_f(static_cast<double>(angle_) * kDegreesToRadians); // degrees to radians (0x00645464)
    uint32_t texture = 0;
    params->read_hash_value(kTexture, &texture, 1);
    blend_mode_ = kBlend; // "blend"
    params->read_hash_value(kBlendMode, &blend_mode_, 0);
    int fineness = 0;
    params->fetch_int_value(kFinenessKey, &fineness, 0);
    fineness_ = static_cast<uint32_t>(fineness);
    bitmap_ = engine_parts().particle_texture_dict()->fetch_skin(texture);
    int flag = 0;
    params->fetch_int_value(kEnabled, &flag, 0);
    if (flag) {
        flag_bits_ |= kProjectionFlag1;
    } else {
        flag_bits_ &= ~kProjectionFlag1;
    }
    unknownbc_ = kUnknownBcDefault;
    params->fetch_float(kUnknownBcKey, &unknownbc_, 0);
    view_distance_ = kViewDistanceDefault;
    params->fetch_float(kViewDistanceKey, &view_distance_, 0);
    update_matrix();
}

// 0x0049FEF0
void TextureProjection::update_matrix()
{
    const maths::Vec4 target = {.x = get_f(static_cast<double>(world_direction_.x) + anchor_point_.x),
                                .y = get_f(static_cast<double>(world_direction_.y) + anchor_point_.y),
                                .z = get_f(static_cast<double>(world_direction_.z) + anchor_point_.z),
                                .w = get_f(static_cast<double>(world_direction_.w) + anchor_point_.w)};
    const maths::Vec4 up = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
    maths::Mat4 view{};
    maths::create_look_at_matrix(view, anchor_point_, target, up);
    maths::Mat4 lens{};
    if (flag_bits_ & kProjectionOrthographic) {
        maths::create_orthographic_matrix(lens, {.width = extent_, .height = extent_, .near_depth_ = kProjectionNearDepth, .far_depth_ = range_});
    } else if (flag_bits_ & kProjectionPerspective) {
        maths::create_perspective_matrix(lens, {.fov = field_of_view_, .aspect = aspect_, .near_depth_ = kProjectionNearDepth, .far_depth_ = range_});
    } else {
        return;
    }
    // (D3DXMatrixMultiply)
    d3dx::matrix_multiply(&projection_, &view, &lens);
}

// 0x0049FFE0
void TextureProjection::tick_frame(objects::GameEntity* object)
{
    maths::Mat4 m{};
    m = object->display_matrix();
    const maths::Vec4& pos = object->at_point();
    double ox = shift_.x;
    double oy = shift_.y;
    double const oz = shift_.z;
    // the position: the object's plus the offset through its matrix (x kept at 53 bits but for the third row's
    // term; y, z, w rounded at each step; w's third term unrounded)
    double x = (static_cast<double>(core::at(m.row, 0).x) * ox) + pos.x;
    x += static_cast<double>(core::at(m.row, 1).x) * oy;
    x += get_f(static_cast<double>(core::at(m.row, 2).x) * oz);
    auto rounded = [&](int c) {
        const float a = get_f(get_f(get_f(core::at(m.row, 0)[c] * ox) + static_cast<double>(pos[c])) +
                              static_cast<double>(get_f(core::at(m.row, 1)[c] * oy)));
        return a;
    };
    const float y = get_f(static_cast<double>(rounded(1)) + get_f(static_cast<double>(core::at(m.row, 2).y) * oz));
    const float z = get_f(static_cast<double>(rounded(2)) + get_f(static_cast<double>(core::at(m.row, 2).z) * oz));
    const float w = get_f(static_cast<double>(rounded(3)) + (static_cast<double>(core::at(m.row, 2).w) * oz));
    anchor_point_ = {.x = get_f(x), .y = y, .z = z, .w = w};
    world_direction_ = direction_ * m;

    if (modules::viewport_count() == 1 && first_camera()) {
        const maths::Vec4* camera = viewer_location(first_camera());
        const double dx = static_cast<double>(anchor_point_.x) - camera->x;
        const double dy = static_cast<double>(anchor_point_.y) - camera->y;
        const double dz = static_cast<double>(anchor_point_.z) - camera->z;
        const double reach = view_distance_;
        if (!(reach * reach > (dx * dx) + (dz * dz) + (dy * dy))) {
            return; // (too far, or NaN)
        }
        // a sphere half the range ahead - along the object-space direction, as the original -, as big as that plus
        // the size
        const double half = static_cast<double>(range_) * 0.5F;
        const maths::Vec4 centre = {.x = get_f(static_cast<double>(get_f(half * direction_.x)) + anchor_point_.x),
                                    .y = get_f(static_cast<double>(get_f(half * direction_.y)) + anchor_point_.y),
                                    .z = get_f(static_cast<double>(get_f(half * direction_.z)) + anchor_point_.z),
                                    .w = get_f((half * direction_.w) + anchor_point_.w)};
        if (sphere_out_of_view(&centre, get_f(half + extent_))) {
            return;
        }
    }
    update_matrix();
    // the line along the range, its box grown by 2.5 sizes
    const double range = range_;
    const maths::Vec4 end = {.x = get_f((range * world_direction_.x) + anchor_point_.x),
                             .y = get_f(static_cast<double>(get_f(range * world_direction_.y)) + anchor_point_.y),
                             .z = get_f(static_cast<double>(get_f(range * world_direction_.z)) + anchor_point_.z),
                             .w = get_f(static_cast<double>(get_f(range * world_direction_.w)) + anchor_point_.w)};
    const maths::Seg line(anchor_point_, end);
    maths::BBox box(line.start);
    box.append_spot(line.end);
    double grow = static_cast<double>(extent_) * kGrowFactor; // (0x00649C1C)
    maths::BBox reach;
    reach.min = {.x = get_f(box.min.x - grow), .y = get_f(box.min.y - grow), .z = get_f(box.min.z - grow), .w = 1.0F};
    grow = static_cast<double>(extent_) * kGrowFactor;
    reach.max = {.x = get_f(box.max.x + grow), .y = get_f(box.max.y + grow), .z = get_f(box.max.z + grow), .w = 1.0F};
    const std::span<CollGrid* const> grids = grids_along_line(&line);
    int count = 0;
    for (CollGrid* const grid : grids) {
        for (HitMesh* const found : grid->objects_in_box(&reach)) {
            const maths::BBox* bounds = found->get_world_bounds();
            // (touching: no comparison says apart - NaNs count as touching)
            if (bounds->min.x > reach.max.x || reach.min.x > bounds->max.x || bounds->min.z > reach.max.z ||
                reach.min.z > bounds->max.z || bounds->min.y > reach.max.y || reach.min.y > bounds->max.y) {
                continue;
            }
            void* sector = find_sector(found->checksum());
            core::at(projected_collision(), count) = found;
            core::at(projected_sectors(), count) = sector;
            ++count;
        }
    }
    if (count > 0) {
        core::at(projected_sectors(), count) = nullptr;
        core::at(projected_collision(), count) = nullptr;
        plat_project(projected_sectors().data(), projected_collision().data());
    }
}

} // namespace render
