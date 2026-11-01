// The collision feeler. See probe_ray.h.

#include "engine/render/probe_ray.h"

#include <intrin.h>

#include <cmath>
#include <cstdint>

#include "engine/gfx/debug_lines.h"
#include "engine/maths/shape_tests.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_tri.h"
#include "engine/render/coll_world.h"
#include "engine/objects/entity.h"
#include "game/rider/debug_collision.h"
#include "platform/compiler.h"

namespace render {

HitMeshCache* g_current_cache;
namespace {

// The cache queries use when the feeler has none.
HitMeshCache*& s_current_cache = g_current_cache;

void reset_hit(CollisionHit& hit)
{
    hit.ignore_flags_ = 0x10;
    hit.require_flags_ = 0;
    hit.normal = {0.0f, 0.0f, 0.0f, 1.0f};
    hit.point = {0.0f, 0.0f, 0.0f, 1.0f};
}

// get_hit_vertex_value without a hit (0x006453BC), and a vertex byte's scale (1 / 255, 0x00645D70).
constexpr float kNoHitValue = 0.5f;
constexpr float kByteScale = 1.0f / 255.0f;

float get_difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }
double get_length_squared(double x, double y, double z) { return (z * z + y * y) + x * x; }

} // namespace

// 0x00417760
ProbeRay::ProbeRay()
{
    entity_.preceding_ = nullptr;
    entity_.following_ = nullptr;
    entity_.owner_object_ = nullptr;
    callback_ = nullptr;
    hit_cache_ = nullptr;
    reset_hit(hit_);
}

// 0x00417250 (the original first sets the hit's normal to (1, 1, 1, 1), then to (0, 0, 0, 1))
ProbeRay::ProbeRay(const maths::Vec4* start, const maths::Vec4* end)
{
    entity_.preceding_ = nullptr;
    entity_.following_ = nullptr;
    entity_.owner_object_ = nullptr;
    line_.start = *start;
    line_.end = *end;
    callback_ = nullptr;
    hit_cache_ = nullptr;
    reset_hit(hit_);
}

// 0x004170F0
void ProbeRay::set_excluded(uint16_t ignore, uint16_t require)
{
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
    auto* feeler = reinterpret_cast<ProbeRay*>(hit->callback_data_);
    feeler->callback_(feeler);
}

// 0x004172F0
int ProbeRay::get_hit_shape(bool movables, bool farthest)
{
    maths::Seg line = line_;
    hit_.callback_data_ = reinterpret_cast<uint32_t>(this);
    hit_.hit = false;
    hit_.face_flag40_ = 0;
    hit_.unknown38 = 0;
    hit_.owner = 0;
    CollisionHitCallback callback = callback_ ? &callback_relay : nullptr;
    HitMeshCache* cache = hit_cache_ ? hit_cache_ : s_current_cache;
    entity_.get_release();
    entity_key_ = 0;
    bool hit_static = farthest ? collide_with_statics_farthest(&line, &hit_, callback, cache)
                               : collide_with_statics(&line, &hit_, callback, cache);
    if (movables) {
        entity_.assign_value(farthest ? collide_with_movables_farthest(&line, &hit_, callback, cache)
                                      : collide_with_movables(&line, &hit_, callback, cache));
        if (entity_)
            entity_key_ = entity_->ident_;
    }
    reach_ = hit_.distance;
    contact_pos_ = hit_.point;
    surface_dir_ = hit_.normal;
    int result = hit_static || entity_ ? 1 : 0;
    // NOT ORIGINAL: [features] debug_collision logs what was hit (src/game/rider/debug_collision.h).
    collision_overlay::note_feeler_query(this, result,
                                         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(THUG2_RETURN_ADDRESS())));
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
uint32_t ProbeRay::get_tested_owner() { return hit_.owner; }

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
    HitMesh* object = hit_.object;
    const TriMeshSource* geometry = object->geometry_;
    const maths::Vec4& at = hit_.point;
    maths::Vec4 corner[3];
    float value[3];
    for (int k = 0; k < 3; ++k) {
        uint32_t vertex = geometry->face_vertex(static_cast<int>(hit_.face), k);
        maths::Vec4 position = object->read_vertex_point(static_cast<int>(vertex));
        corner[k] = {get_difference(position.x, at.x), get_difference(position.y, at.y),
                     get_difference(position.z, at.z), 1.0f};
        value[k] = static_cast<float>(geometry->vertex_bytes_[vertex]) * kByteScale;
    }
    // The line from the first corner through the hit point, where it meets the opposite edge's line.
    const maths::Vec4 hit_point{0.0f, 0.0f, 0.0f, 1.0f};
    maths::Seg from_corner(corner[0], hit_point), opposite(corner[1], corner[2]);
    maths::Vec4 on_line{0.0f, 0.0f, 0.0f, 0.0f}, on_edge; // (the original's are left unset when the lines are parallel)
    float s, t;
    maths::closest_points_between_lines(from_corner, opposite, on_line, on_edge, s, t, false);
    auto minus = [](const maths::Vec4& a, const maths::Vec4& b, double* x, double* y, double* z) {
        *x = static_cast<double>(a.x) - b.x;
        *y = static_cast<double>(a.y) - b.y;
        *z = static_cast<double>(a.z) - b.z;
    };
    double x, y, z;
    minus(on_line, corner[1], &x, &y, &z);
    const float along_edge_squared = static_cast<float>(get_length_squared(x, y, z));
    minus(corner[2], corner[1], &x, &y, &z);
    const float edge_squared = static_cast<float>(get_length_squared(x, y, z));
    minus(on_line, corner[0], &x, &y, &z);
    const double to_edge = std::sqrt(get_length_squared(x, y, z));
    const double to_hit = std::sqrt(get_length_squared(corner[0].x, corner[0].y, corner[0].z));
    const double fraction =
        std::sqrt(static_cast<double>(along_edge_squared)) / std::sqrt(static_cast<double>(edge_squared));
    const double on_edge_value = (static_cast<double>(value[2]) - value[1]) * fraction + value[1];
    return (on_edge_value - value[0]) * (to_hit / to_edge) + value[0];
}

// 0x00417150 (the colour: alpha 0xFF over the low byte of b, then g and r ORed in whole, as the original)
void ProbeRay::debug_draw(uint32_t r, uint32_t g, uint32_t b, int frames)
{
    uint32_t colour = ((((b | 0xFFFFFF00u) << 8) | g) << 8) | r;
    gfx::push_diag_line(&line_.start, &line_.end, colour, colour, frames);
}

// 0x0041C350
void ProbeRay::destruct() { entity_.get_release(); }

// 0x004F8200
ProbeRay* ProbeRay::copy_construct(const ProbeRay& other)
{
    line_ = other.line_;
    callback_ = other.callback_;
    hit_.copy_from(other.hit_);
    reach_ = other.reach_;
    contact_pos_ = other.contact_pos_;
    surface_dir_ = other.surface_dir_;
    entity_.construct_copy(&other.entity_);
    entity_key_ = other.entity_key_;
    unknowna8_ = other.unknowna8_;
    hit_cache_ = other.hit_cache_;
    return this;
}

// 0x0043A930
ProbeRay* ProbeRay::get_assign(const ProbeRay& other)
{
    line_ = other.line_;
    callback_ = other.callback_;
    hit_.copy_from(other.hit_);
    reach_ = other.reach_;
    contact_pos_ = other.contact_pos_;
    surface_dir_ = other.surface_dir_;
    entity_.get_assign(&other.entity_);
    entity_key_ = other.entity_key_;
    unknowna8_ = other.unknowna8_;
    hit_cache_ = other.hit_cache_;
    return this;
}

} // namespace render
