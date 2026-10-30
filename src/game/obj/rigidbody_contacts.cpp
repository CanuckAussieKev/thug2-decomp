// The rigid body component's contacts with the world (game/obj/rigidbody_component.h): FindContacts.
//
// The original's arithmetic is x87 code at 53-bit precision: what it keeps on the FPU stack is a double here, what
// it stores to memory a float - each value rounded where the original rounds it, and sums keep its grouping.

#include <cmath>
#include <cstdint>

#include "game/mth/geometry.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_sphere.h"
#include "game/nx/feeler.h"
#include "game/obj/rigidbody_component.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

double Wide(float value) { return value; }
float Narrow(double value) { return static_cast<float>(value); }

// The rigid bodies' collision cache (0x006939E0), refilled for each body's box before it looks.
Nx::CCollCache* BodyCache() { return &g_rigidBodyCache; }

} // namespace

// 0x0042C950: the contacts with the world this frame, into Contacts() (m_contactsHit of them; m_unknown191 of them
// touching - at or inside the surface): the cache refilled for the bounding box; each contact point (last first)
// by a line from the centre of mass out to it - a hit is a contact: its point, the surface's normal, how deep (the
// rest of the line along the normal; touching at 0 or less) and its directed friction (the point's number); then
// each sphere (last first) by a sphere query - each face it touches (last first) facing it and not behind its
// centre is one: the hit point, the normal from the hit to the centre, the depth the distance less the radius, the
// sphere's number as the friction. Whether any is touching.
bool CRigidBodyComponent::FindContacts()
{
    const float r = m_radius;
    Mth::BBox box;
    box.min = {Narrow(Wide(m_pos.x) - r), Narrow(Wide(m_pos.y) - r), Narrow(Wide(m_pos.z) - r),
               Narrow(Wide(m_pos.w) - 1.0f)};
    box.max = {Narrow(Wide(r) + m_pos.x), Narrow(Wide(r) + m_pos.y), Narrow(Wide(r) + m_pos.z),
               Narrow(Wide(m_pos.w) + 1.0f)};
    BodyCache()->Update(box);
    m_contactsHit = 0;
    m_unknown191 = 0;

    if (m_contactCount != 0) {
        Nx::CFeeler feeler;
        feeler.Construct();
        feeler.mp_cache = BodyCache();
        for (int i = m_contactCount - 1; i >= 0; --i) {
            const Mth::Vector& point = WorldContactPoints()[i];
            Contact& contact = Contacts()[m_contactsHit];
            feeler.m_line.start = m_pos;
            feeler.m_line.end = {Narrow(Wide(m_pos.x) + point.x), Narrow(Wide(m_pos.y) + point.y),
                                 Narrow(Wide(m_pos.z) + point.z), Narrow(Wide(m_pos.w) + point.w)};
            contact.active = feeler.GetCollision(false, false) != 0;
            if (!contact.active)
                continue;
            const Mth::Line& line = feeler.m_line;
            double dz = Wide(line.end.z) - line.start.z;
            double dy = Wide(line.end.y) - line.start.y;
            float dx = Narrow(Wide(line.end.x) - line.start.x);
            float rest = Narrow(1.0 - feeler.m_distance);
            contact.offset = point;
            contact.normal = feeler.m_normal;
            contact.scaled = false;
            float az = Narrow(dz * rest);
            contact.impulse = 0.0f;
            double depth = ((dy * rest) * contact.normal.y + Wide(az) * contact.normal.z) +
                           (Wide(dx) * rest) * contact.normal.x;
            contact.depth = Narrow(depth);
            contact.active = depth <= 0.0;
            contact.friction = static_cast<int8_t>(i);
            if (contact.active)
                ++m_unknown191;
            ++m_contactsHit;
        }
    }

    for (int j = m_sphereCount - 1; j >= 0; --j) {
        const Mth::Vector& centre = WorldSphereCenters()[j];
        const Sphere& sphere = mp_spheres[j];
        Mth::Vector at = {Narrow(Wide(centre.x) + m_pos.x), Narrow(Wide(centre.y) + m_pos.y),
                          Narrow(Wide(m_pos.z) + centre.z), Narrow(Wide(centre.w) + m_pos.w)};
        Nx::CSphereFeeler feeler;
        feeler.Construct(&at, sphere.radius);
        feeler.mp_cache = BodyCache();
        if (!feeler.Query(0))
            continue;
        for (int k = feeler.m_hits.count - 1; k >= 0; --k) {
            const Nx::SphereHit& hit = feeler.m_hits.hits[k];
            Contact& contact = Contacts()[m_contactsHit];
            const Mth::Vector& c = feeler.m_sphere.centre;
            double ox = Wide(hit.point.x) - m_pos.x;
            double oy = Wide(hit.point.y) - m_pos.y;
            contact.offset.z = Narrow(Wide(hit.point.z) - m_pos.z);
            contact.offset.w = Narrow(Wide(hit.point.w) - m_pos.w);
            contact.offset.x = Narrow(ox);
            contact.offset.y = Narrow(oy);
            double nx = Wide(hit.point.x) - c.x;
            double ny = Wide(hit.point.y) - c.y;
            contact.normal.z = Narrow(Wide(hit.point.z) - c.z);
            contact.normal.w = Narrow(Wide(hit.point.w) - c.w);
            contact.normal.x = Narrow(nx);
            contact.normal.y = Narrow(ny);
            Mth::Vector& n = contact.normal;
            float length = Narrow(std::sqrt((Wide(n.x) * n.x + Wide(n.y) * n.y) + Wide(n.z) * n.z));
            if (length == 0.0f) {
                n = {0.0f, 1.0f, 0.0f, 1.0f};
            } else {
                // (towards the centre: the hit point less the centre, reversed)
                double inverse = 1.0 / -Wide(length);
                n = {Narrow(inverse * n.x), Narrow(inverse * n.y), Narrow(inverse * n.z), Narrow(inverse * n.w)};
            }
            // (a face turned away from it, or a hit ahead of the centre of mass: none)
            if ((Wide(hit.normal.z) * n.z + Wide(hit.normal.x) * n.x) + Wide(n.y) * hit.normal.y < 0.0)
                continue;
            const Mth::Vector& o = contact.offset;
            if ((Wide(n.z) * o.z + Wide(n.y) * o.y) + Wide(n.x) * o.x > 0.0)
                continue;
            contact.scaled = false;
            contact.impulse = 0.0f;
            double depth = Wide(length) - sphere.radius;
            contact.depth = Narrow(depth);
            contact.active = depth <= 0.0;
            contact.friction = static_cast<int8_t>(j);
            if (contact.active)
                ++m_unknown191;
            ++m_contactsHit;
        }
    }
    return m_unknown191 != 0;
}

} // namespace Obj

REPLACE(0x0042C950, &Obj::CRigidBodyComponent::FindContacts);
