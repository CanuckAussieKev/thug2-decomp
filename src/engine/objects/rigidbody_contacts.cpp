// The rigid body component's contacts with the world (engine/objects/rigidbody_component.h): find_contacts.
//
// The original's arithmetic is x87 code at 53-bit precision: what it keeps on the FPU stack is a double here, what
// it stores to memory a float - each value rounded where the original rounds it, and sums keep its grouping.

#include <cmath>
#include <cstdint>
#include "engine/maths/shape_tests.h"
#include "engine/render/coll_cache.h"
#include "engine/render/coll_sphere.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/rigidbody_component.h"
#include "engine/core/checked_index.h"
#include "engine/objects/objects_shared.h"

namespace objects {

namespace {

double get_wide(float value) { return value; }
float get_narrow(double value) { return static_cast<float>(value); }

// The rigid bodies' collision cache (0x006939E0), refilled for each body's box before it looks.
render::HitMeshCache* body_cache() { return &*objects_shared().rigid_body_cache(); }

} // namespace

// 0x0042C950: the contacts with the world this frame, into Contacts() (contacts_hit_ of them; unknown191_ of them
// touching - at or inside the surface): the cache refilled for the bounding box; each contact point (last first)
// by a line from the centre of mass out to it - a hit is a contact: its point, the surface's normal, how deep (the
// rest of the line along the normal; touching at 0 or less) and its directed friction (the point's number); then
// each sphere (last first) by a sphere query - each face it touches (last first) facing it and not behind its
// centre is one: the hit point, the normal from the hit to the centre, the depth the distance less the radius, the
// sphere's number as the friction. Whether any is touching.
bool BodyDynamicsModule::find_contacts()
{
    const float radius = radius_;
    maths::BBox box;
    box.min = {.x = get_narrow(get_wide(at_point_.x) - radius),
               .y = get_narrow(get_wide(at_point_.y) - radius),
               .z = get_narrow(get_wide(at_point_.z) - radius),
               .w = get_narrow(get_wide(at_point_.w) - 1.0F)};
    box.max = {.x = get_narrow(get_wide(radius) + at_point_.x),
               .y = get_narrow(get_wide(radius) + at_point_.y),
               .z = get_narrow(get_wide(radius) + at_point_.z),
               .w = get_narrow(get_wide(at_point_.w) + 1.0F)};
    body_cache()->tick_frame(box);
    contacts_hit_ = 0;
    unknown191_ = 0;

    if (contact_count_ != 0) {
        render::ProbeRay feeler;
        feeler.set_hit_cache(body_cache());
        for (int i = contact_count_ - 1; i >= 0; --i) {
            const maths::Vec4& point = core::at(world_contact_points(), i);
            Contact& contact = core::at(get_contacts(), contacts_hit_);
            feeler.line().start = at_point_;
            feeler.line().end = {
                .x = get_narrow(get_wide(at_point_.x) + point.x), .y = get_narrow(get_wide(at_point_.y) + point.y),
                .z = get_narrow(get_wide(at_point_.z) + point.z), .w = get_narrow(get_wide(at_point_.w) + point.w)};
            contact.active = feeler.get_hit_shape(false, false) != 0;
            if (!contact.active) {
                continue;
            }
            const maths::Seg& line = feeler.line();
            const double dz = get_wide(line.end.z) - line.start.z;
            const double dy = get_wide(line.end.y) - line.start.y;
            const float dx = get_narrow(get_wide(line.end.x) - line.start.x);
            const float rest = get_narrow(1.0 - feeler.reach());
            contact.offset = point;
            contact.normal = feeler.surface_dir();
            contact.scaled = false;
            const float az = get_narrow(dz * rest);
            contact.impulse = 0.0F;
            const double depth = (((dy * rest) * contact.normal.y) + (get_wide(az) * contact.normal.z)) +
                                 ((get_wide(dx) * rest) * contact.normal.x);
            contact.depth = get_narrow(depth);
            contact.active = depth <= 0.0;
            contact.friction = static_cast<int8_t>(i);
            if (contact.active) {
                ++unknown191_;
            }
            ++contacts_hit_;
        }
    }

    for (int j = sphere_count_ - 1; j >= 0; --j) {
        const maths::Vec4& centre = core::at(world_sphere_centers(), j);
        const Sphere& sphere = core::at(sphere_run(), j);
        const maths::Vec4 at = {
            .x = get_narrow(get_wide(centre.x) + at_point_.x), .y = get_narrow(get_wide(centre.y) + at_point_.y),
            .z = get_narrow(get_wide(at_point_.z) + centre.z), .w = get_narrow(get_wide(centre.w) + at_point_.w)};
        render::SphereFeeler feeler(&at, sphere.radius);
        feeler.set_hit_cache(body_cache());
        if (!feeler.get_query(0)) {
            continue;
        }
        for (int k = feeler.hits().count - 1; k >= 0; --k) {
            const render::SphereHit& hit = core::at(feeler.hits().hits, k);
            Contact& contact = core::at(get_contacts(), contacts_hit_);
            const maths::Vec4& ball_centre = feeler.bound_ball().centre;
            const double ox = get_wide(hit.point.x) - at_point_.x;
            const double oy = get_wide(hit.point.y) - at_point_.y;
            contact.offset.z = get_narrow(get_wide(hit.point.z) - at_point_.z);
            contact.offset.w = get_narrow(get_wide(hit.point.w) - at_point_.w);
            contact.offset.x = get_narrow(ox);
            contact.offset.y = get_narrow(oy);
            const double nx = get_wide(hit.point.x) - ball_centre.x;
            const double ny = get_wide(hit.point.y) - ball_centre.y;
            contact.normal.z = get_narrow(get_wide(hit.point.z) - ball_centre.z);
            contact.normal.w = get_narrow(get_wide(hit.point.w) - ball_centre.w);
            contact.normal.x = get_narrow(nx);
            contact.normal.y = get_narrow(ny);
            maths::Vec4& normal = contact.normal;
            const float length = get_narrow(std::sqrt(
                ((get_wide(normal.x) * normal.x) + (get_wide(normal.y) * normal.y)) + (get_wide(normal.z) * normal.z)));
            if (length == 0.0F) {
                normal = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 1.0F};
            } else {
                // (towards the centre: the hit point less the centre, reversed)
                const double inverse = 1.0 / -get_wide(length);
                normal = {.x = get_narrow(inverse * normal.x),
                          .y = get_narrow(inverse * normal.y),
                          .z = get_narrow(inverse * normal.z),
                          .w = get_narrow(inverse * normal.w)};
            }
            // (a face turned away from it, or a hit ahead of the centre of mass: none)
            if (((get_wide(hit.normal.z) * normal.z) + (get_wide(hit.normal.x) * normal.x)) +
                    (get_wide(normal.y) * hit.normal.y) <
                0.0) {
                continue;
            }
            const maths::Vec4& offset = contact.offset;
            if (((get_wide(normal.z) * offset.z) + (get_wide(normal.y) * offset.y)) + (get_wide(normal.x) * offset.x) >
                0.0) {
                continue;
            }
            contact.scaled = false;
            contact.impulse = 0.0F;
            const double depth = get_wide(length) - sphere.radius;
            contact.depth = get_narrow(depth);
            contact.active = depth <= 0.0;
            contact.friction = static_cast<int8_t>(j);
            if (contact.active) {
                ++unknown191_;
            }
            ++contacts_hit_;
        }
    }
    return unknown191_ != 0;
}

} // namespace objects
