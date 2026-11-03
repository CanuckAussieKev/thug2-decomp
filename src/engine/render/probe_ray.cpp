// The collision feeler. See probe_ray.h.

#include "engine/render/probe_ray.h"

#include <cmath>
#include <cstdint>
#include <array>

#include "engine/gfx/debug_lines.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_line.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_tri.h"
#include "engine/render/coll_world.h"
#include "engine/objects/entity.h"
#include "game/rider/debug_collision.h"
#include "platform/compiler.h"
#include "platform/code_address.h"
#include "engine/core/checked_index.h"
#include "engine/render/collision_state.h"

namespace render {

void set_current_cache(HitMeshCache* cache) { collision_state().set_current_cache(cache); }

namespace {

// The cache queries use when the feeler has none.

void reset_hit(CollisionHit& hit)
{
    hit.ignore_flags_ = kDefaultIgnoredFaceFlags;
    hit.require_flags_ = kNoFaceFlags;
    hit.normal = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    hit.point = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
}

// get_hit_vertex_value without a hit (0x006453BC), and a vertex byte's scale (1 / 255, 0x00645D70).
constexpr float kNoHitValue = 0.5f;
constexpr float kByteScale = 1.0f / 255.0f;

float get_difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }
double get_length_squared(double x, double y, double z) { return (z * z + y * y) + x * x; }

} // namespace

// 0x00417760
ProbeRay::ProbeRay() : callback_(nullptr), hit_cache_(nullptr)
{
    entity_.preceding_ = nullptr;
    entity_.following_ = nullptr;
    entity_.target_ = nullptr;
    reset_hit(hit_);
}

// 0x00417250 (the original first sets the hit's normal to (1, 1, 1, 1), then to (0, 0, 0, 1))
ProbeRay::ProbeRay(const maths::Vec4* start, const maths::Vec4* end) : callback_(nullptr), hit_cache_(nullptr)
{
    entity_.preceding_ = nullptr;
    entity_.following_ = nullptr;
    entity_.target_ = nullptr;
    line_.start = *start;
    line_.end = *end;
    reset_hit(hit_);
}

// 0x004170F0
void ProbeRay::set_excluded(FaceMasks face_masks)
{
    const auto ignore = face_masks.ignore;
    const auto require = face_masks.require;
    hit_.ignore_flags_ = ignore;
    hit_.require_flags_ = require;
}

// 0x00417190
void ProbeRay::change_origin(const maths::Vec4* start) { line_.start = *start; }

// 0x004171B0
void ProbeRay::change_finish(const maths::Vec4* end) { line_.end = *end; }

// 0x004171D0
void ProbeRay::change_strand(const maths::Vec4* start, const maths::Vec4* end)
{
    line_.start = *start;
    line_.end = *end;
}

// 0x00417110
void ProbeRay::callback_relay(CollisionHit* hit)
{
    auto* feeler = static_cast<ProbeRay*>(hit->callback_data_);
    feeler->callback_(feeler);
}

// 0x004172F0
int ProbeRay::get_hit_shape(bool movables, bool farthest)
{
    const maths::Seg line = line_;
    hit_.callback_data_ = this;
    hit_.hit = false;
    hit_.face_flag40_ = 0;
    hit_.unknown38 = 0;
    hit_.owner = nullptr;
    CollisionHitCallback callback = callback_ ? &callback_relay : nullptr;
    HitMeshCache* cache = hit_cache_ ? hit_cache_ : collision_state().current_cache();
    entity_.get_release();
    entity_key_ = 0;
    const bool hit_static = farthest ? collide_with_statics_farthest(&line, &hit_, callback, cache)
                                     : collide_with_statics(&line, &hit_, callback, cache);
    if (movables) {
        entity_.assign_value(farthest ? collide_with_movables_farthest(&line, &hit_, callback, cache)
                                      : collide_with_movables(&line, &hit_, callback, cache));
        if (entity_)
            entity_key_ = entity_->ident();
    }
    reach_ = hit_.distance;
    contact_pos_ = hit_.point;
    surface_dir_ = hit_.normal;
    const int result = hit_static || entity_ ? 1 : 0;
    // NOT ORIGINAL: [features] debug_collision logs what was hit (src/game/rider/debug_collision.h).
    collision_overlay::note_feeler_query(this, {.hit = result, .caller = static_cast<uint32_t>(code_address_value(THUG2_RETURN_ADDRESS()))});
    return result;
}

// 0x00417710
int ProbeRay::get_collision_along(const maths::Vec4* start, const maths::Vec4* end, bool movables)
{
    line_.start = *start;
    line_.end = *end;
    return get_hit_shape(movables, false);
}

// 0x00417120 (the original's EAX keeps its upper bytes from before when nothing was hit)
bool ProbeRay::is_hit_triangle_collision()
{
    if (!hit_.hit)
        return true;
    return hit_.object->is_triangle_hit();
}

// 0x00417140
objects::GameEntity* ProbeRay::get_tested_owner() { return hit_.owner; }

// 0x00417210
bool ProbeRay::hit_object_alive()
{
    if (entity_)
        return true;
    if (entity_key_ != 0) {
        entity_key_ = 0;
        hit_.object_checksum_ = 0;
    }
    return false;
}

// 0x00417240
objects::GameEntity* ProbeRay::get_hit_object() { return entity_.get_pull(); }

// 0x004174A0 (the corners about the hit point; the original's x87 code keeps the lengths and the blend in double
// precision)
double ProbeRay::get_hit_vertex_value() const
{
    if (!hit_.hit)
        return kNoHitValue;
    const HitMesh* object = hit_.object;
    const TriMeshSource* geometry = object->geometry();
    const maths::Vec4& at = hit_.point;
    std::array<maths::Vec4, 3> corner{};
    std::array<float, 3> value{};
    for (int k = 0; k < 3; ++k) {
        const uint32_t vertex = geometry->face_vertex(static_cast<int>(hit_.face), k);
        const maths::Vec4 position = object->read_vertex_point(static_cast<int>(vertex));
        core::at(corner, k) = {.x = get_difference(position.x, at.x), .y = get_difference(position.y, at.y),
                     .z = get_difference(position.z, at.z), .w = 1.0f};
        core::at(value, k) = static_cast<float>(core::at(geometry->vertex_byte_run(), vertex)) * kByteScale;
    }
    // The line from the first corner through the hit point, where it meets the opposite edge's line.
    const maths::Vec4 hit_point{.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    const maths::Seg from_corner(core::at(corner, 0), hit_point);
    const maths::Seg opposite(core::at(corner, 1), core::at(corner, 2));
    maths::Vec4 on_line{.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f},
        on_edge{}; // (the original's are left unset when the lines are parallel)
    float s = 0.0f, t = 0.0f;
    maths::closest_points_between_lines(from_corner, opposite, on_line, on_edge, s, t, false);
    // Where a difference's x, y and z go.
    struct DeltaOut {
        double* x{};
        double* y{};
        double* z{};
    };
    auto minus = [](const maths::Vec4& a, const maths::Vec4& b, DeltaOut delta_out) {
        auto* const x = delta_out.x;
        auto* const y = delta_out.y;
        auto* const z = delta_out.z;
        *x = static_cast<double>(a.x) - b.x;
        *y = static_cast<double>(a.y) - b.y;
        *z = static_cast<double>(a.z) - b.z;
    };
    double x = 0.0, y = 0.0, z = 0.0;
    minus(on_line, core::at(corner, 1), {.x = &x, .y = &y, .z = &z});
    const auto along_edge_squared = static_cast<float>(get_length_squared(x, y, z));
    minus(core::at(corner, 2), core::at(corner, 1), {.x = &x, .y = &y, .z = &z});
    const auto edge_squared = static_cast<float>(get_length_squared(x, y, z));
    minus(on_line, core::at(corner, 0), {.x = &x, .y = &y, .z = &z});
    const double to_edge = std::sqrt(get_length_squared(x, y, z));
    const double to_hit = std::sqrt(get_length_squared(core::at(corner, 0).x, core::at(corner, 0).y, core::at(corner, 0).z));
    const double fraction =
        std::sqrt(static_cast<double>(along_edge_squared)) / std::sqrt(static_cast<double>(edge_squared));
    const double on_edge_value = (static_cast<double>(core::at(value, 2)) - core::at(value, 1)) * fraction + core::at(value, 1);
    return (on_edge_value - core::at(value, 0)) * (to_hit / to_edge) + core::at(value, 0);
}

// 0x00417150 (the colour: alpha 0xFF over the low byte of b, then g and r ORed in whole, as the original)
void ProbeRay::debug_draw(uint32_t r, uint32_t g, uint32_t b, int frames)
{
    const uint32_t colour = ((((b | 0xFFFFFF00u) << 8) | g) << 8) | r;
    gfx::push_diag_line(&line_.start, &line_.end, colour, colour, frames);
}

// 0x0041C350
void ProbeRay::destruct() { entity_.get_release(); }

// 0x004F8200
ProbeRay* ProbeRay::copy_construct(const ProbeRay& other)
{
    line_ = other.line_;
    callback_ = other.callback_;
    copy_from(hit_, other.hit_);
    reach_ = other.reach_;
    contact_pos_ = other.contact_pos_;
    surface_dir_ = other.surface_dir_;
    entity_.construct_copy(&other.entity_);
    entity_key_ = other.entity_key_;
    user_data_ = other.user_data_;
    hit_cache_ = other.hit_cache_;
    return this;
}

// 0x0043A930
ProbeRay* ProbeRay::get_assign(const ProbeRay& other)
{
    line_ = other.line_;
    callback_ = other.callback_;
    copy_from(hit_, other.hit_);
    reach_ = other.reach_;
    contact_pos_ = other.contact_pos_;
    surface_dir_ = other.surface_dir_;
    entity_.get_assign(&other.entity_);
    entity_key_ = other.entity_key_;
    user_data_ = other.user_data_;
    hit_cache_ = other.hit_cache_;
    return this;
}

} // namespace render
