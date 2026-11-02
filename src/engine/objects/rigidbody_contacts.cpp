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

namespace objects {

namespace {

double get_wide(float value) { return value; }
float get_narrow(double value) { return static_cast<float>(value); }

// The rigid bodies' collision cache (0x006939E0), refilled for each body's box before it looks.
render::HitMeshCache* body_cache() { return &*g_rigid_body_cache; }

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
    const float r = radius_;
    maths::BBox box;
    box.min = {get_narrow(get_wide(at_point_.x) - r), get_narrow(get_wide(at_point_.y) - r),
               get_narrow(get_wide(at_point_.z) - r), get_narrow(get_wide(at_point_.w) - 1.0f)};
    box.max = {get_narrow(get_wide(r) + at_point_.x), get_narrow(get_wide(r) + at_point_.y),
               get_narrow(get_wide(r) + at_point_.z), get_narrow(get_wide(at_point_.w) + 1.0f)};
    body_cache()->tick_frame(box);
    contacts_hit_ = 0;
    unknown191_ = 0;

    if (contact_count_ != 0) {
        render::ProbeRay feeler;
        feeler.hit_cache_ = body_cache();
        for (int i = contact_count_ - 1; i >= 0; --i) {
            const maths::Vec4& point = core::at(world_contact_points(), i);
            Contact& contact = get_contacts()[contacts_hit_];
            feeler.line_.start = at_point_;
            feeler.line_.end = {
                get_narrow(get_wide(at_point_.x) + point.x), get_narrow(get_wide(at_point_.y) + point.y),
                get_narrow(get_wide(at_point_.z) + point.z), get_narrow(get_wide(at_point_.w) + point.w)};
            contact.active = feeler.get_hit_shape(false, false) != 0;
            if (!contact.active)
                continue;
            const maths::Seg& line = feeler.line_;
            const double dz = get_wide(line.end.z) - line.start.z;
            const double dy = get_wide(line.end.y) - line.start.y;
            const float dx = get_narrow(get_wide(line.end.x) - line.start.x);
            const float rest = get_narrow(1.0 - feeler.reach_);
            contact.offset = point;
            contact.normal = feeler.surface_dir_;
            contact.scaled = false;
            const float az = get_narrow(dz * rest);
            contact.impulse = 0.0f;
            const double depth = ((dy * rest) * contact.normal.y + get_wide(az) * contact.normal.z) +
                                 (get_wide(dx) * rest) * contact.normal.x;
            contact.depth = get_narrow(depth);
            contact.active = depth <= 0.0;
            contact.friction = static_cast<int8_t>(i);
            if (contact.active)
                ++unknown191_;
            ++contacts_hit_;
        }
    }

    for (int j = sphere_count_ - 1; j >= 0; --j) {
        const maths::Vec4& centre = core::at(world_sphere_centers(), j);
        const Sphere& sphere = core::at(sphere_run(), j);
        const maths::Vec4 at = {
            get_narrow(get_wide(centre.x) + at_point_.x), get_narrow(get_wide(centre.y) + at_point_.y),
            get_narrow(get_wide(at_point_.z) + centre.z), get_narrow(get_wide(centre.w) + at_point_.w)};
        render::SphereFeeler feeler(&at, sphere.radius);
        feeler.hit_cache_ = body_cache();
        if (!feeler.get_query(0))
            continue;
        for (int k = feeler.hits_.count - 1; k >= 0; --k) {
            const render::SphereHit& hit = core::at(feeler.hits_.hits, k);
            Contact& contact = get_contacts()[contacts_hit_];
            const maths::Vec4& c = feeler.bound_ball_.centre;
            const double ox = get_wide(hit.point.x) - at_point_.x;
            const double oy = get_wide(hit.point.y) - at_point_.y;
            contact.offset.z = get_narrow(get_wide(hit.point.z) - at_point_.z);
            contact.offset.w = get_narrow(get_wide(hit.point.w) - at_point_.w);
            contact.offset.x = get_narrow(ox);
            contact.offset.y = get_narrow(oy);
            const double nx = get_wide(hit.point.x) - c.x;
            const double ny = get_wide(hit.point.y) - c.y;
            contact.normal.z = get_narrow(get_wide(hit.point.z) - c.z);
            contact.normal.w = get_narrow(get_wide(hit.point.w) - c.w);
            contact.normal.x = get_narrow(nx);
            contact.normal.y = get_narrow(ny);
            maths::Vec4& n = contact.normal;
            const float length =
                get_narrow(std::sqrt((get_wide(n.x) * n.x + get_wide(n.y) * n.y) + get_wide(n.z) * n.z));
            if (length == 0.0f) {
                n = {0.0f, 1.0f, 0.0f, 1.0f};
            } else {
                // (towards the centre: the hit point less the centre, reversed)
                const double inverse = 1.0 / -get_wide(length);
                n = {get_narrow(inverse * n.x), get_narrow(inverse * n.y), get_narrow(inverse * n.z),
                     get_narrow(inverse * n.w)};
            }
            // (a face turned away from it, or a hit ahead of the centre of mass: none)
            if ((get_wide(hit.normal.z) * n.z + get_wide(hit.normal.x) * n.x) + get_wide(n.y) * hit.normal.y < 0.0)
                continue;
            const maths::Vec4& o = contact.offset;
            if ((get_wide(n.z) * o.z + get_wide(n.y) * o.y) + get_wide(n.x) * o.x > 0.0)
                continue;
            contact.scaled = false;
            contact.impulse = 0.0f;
            const double depth = get_wide(length) - sphere.radius;
            contact.depth = get_narrow(depth);
            contact.active = depth <= 0.0;
            contact.friction = static_cast<int8_t>(j);
            if (contact.active)
                ++unknown191_;
            ++contacts_hit_;
        }
    }
    return unknown191_ != 0;
}

} // namespace objects
