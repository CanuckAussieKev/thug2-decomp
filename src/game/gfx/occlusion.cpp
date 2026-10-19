// Occlusion polygons and view culling: see occlusion.h. The plane maths follows the original's x87 code: some
// intermediate values stay in double precision (in registers), the rest are rounded to floats where it stored them.

#include "game/gfx/occlusion.h"

#include <cmath>
#include <cstdint>

#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

OcclusionPoly* Polys() { return &GameVariable<OcclusionPoly>(0x00787360); }
uint32_t& PolyCount() { return GameVariable<uint32_t>(0x00787350); }
uint32_t& NextPoly() { return GameVariable<uint32_t>(0x00787354); } // where AddOccluders carries on
int& Frame() { return GameVariable<int>(0x00787358); }
Occluder* Occluders() { return &GameVariable<Occluder>(0x00792360); }
uint32_t& OccluderCount() { return GameVariable<uint32_t>(0x0078735C); }
constexpr uint32_t kMaxOccluders = 8;

// The object's transform for culling (SetWorldTransform), or null: only its position row is used here.
const float* WorldTransform() { return GameVariable<const float*>(0x007CA968); }
// The view matrix (0x00786BD0) and the view's clip values.
const void* ViewMatrix() { return &GameVariable<float>(0x00786BD0); }
float NearClip() { return GameVariable<float>(0x00786CA8); }
float FarClip() { return GameVariable<float>(0x00786CAC); }
// The frustum's side planes as (cos, sin) pairs: horizontal 0x00786E20 / 0x00786E28, vertical 0x00786E24 / 0x00786E2C.
float SideCos() { return GameVariable<float>(0x00786E20); }
float VerticalCos() { return GameVariable<float>(0x00786E24); }
float SideSin() { return GameVariable<float>(0x00786E28); }
float VerticalSin() { return GameVariable<float>(0x00786E2C); }

// D3DXVec3Transform (linked library code; stdcall): (x, y, z, 1) x matrix.
void Transform(float* out, const float* point, const void* matrix)
{
    GameFunction<void*(__stdcall*)(float*, const float*, const void*)>(0x005BF19F)(out, point, matrix);
}

// The C runtime's qsort (the game's, so equal occluders end up in the same order).
void Qsort(void* base, uint32_t count, uint32_t size, int(__cdecl* compare)(const void*, const void*))
{
    GameFunction<void(__cdecl*)(void*, uint32_t, uint32_t, int(__cdecl*)(const void*, const void*))>(0x006242A0)(
        base, count, size, compare);
}

// How each of the original's (inlined) plane calculations rounds.
enum class LengthOrder { XZY, ZYX }; // the order the squares are added in
enum class DotOrder { ZYX, XYZ };     // ... and the products of the plane's distance

// A plane through `point` with the normal (v x u) normalised, where u = uEnd - origin (kept in double precision)
// and v = vEnd - origin (x and y rounded to floats, z kept).
void SetPlane(float* plane, const float* origin, const float* uEnd, const float* vEnd, LengthOrder lengthOrder,
              bool exactInverseForX, const float* point, DotOrder dotOrder)
{
    double u0 = static_cast<double>(uEnd[0]) - origin[0];
    double u1 = static_cast<double>(uEnd[1]) - origin[1];
    double u2 = static_cast<double>(uEnd[2]) - origin[2];
    float v0 = static_cast<float>(static_cast<double>(vEnd[0]) - origin[0]);
    float v1 = static_cast<float>(static_cast<double>(vEnd[1]) - origin[1]);
    double v2 = static_cast<double>(vEnd[2]) - origin[2];
    float nx = static_cast<float>(v2 * u1 - v1 * u2);
    float ny = static_cast<float>(u2 * v0 - v2 * u0);
    float nz = static_cast<float>(v1 * u0 - u1 * v0);
    double xx = static_cast<double>(nx) * nx, yy = static_cast<double>(ny) * ny, zz = static_cast<double>(nz) * nz;
    double length = std::sqrt(lengthOrder == LengthOrder::XZY ? (xx + zz) + yy : (zz + yy) + xx);
    double x = nx;
    float y = ny, z = nz;
    if (length > 0.0) {
        double inverse = 1.0 / static_cast<float>(length);
        float inverseFloat = static_cast<float>(inverse);
        x = (exactInverseForX ? inverse : inverseFloat) * static_cast<double>(nx);
        y = static_cast<float>(static_cast<double>(inverseFloat) * ny);
        z = static_cast<float>(static_cast<double>(inverseFloat) * nz);
    }
    double distance = dotOrder == DotOrder::ZYX
                          ? (static_cast<double>(z) * point[2] + static_cast<double>(y) * point[1]) + x * point[0]
                          : (x * point[0] + static_cast<double>(y) * point[1]) + static_cast<double>(z) * point[2];
    plane[0] = static_cast<float>(x);
    plane[1] = y;
    plane[2] = z;
    plane[3] = static_cast<float>(distance);
}

// The view tests shared by SphereOutsideView and SphereInView, on the centre in view space: in front of the near
// plane and inside the four side planes.
bool InsideFrustum(const float* view, float radius)
{
    if (static_cast<double>(radius) - view[2] < NearClip())
        return false;
    double a = static_cast<double>(SideCos()) * view[2];
    double b = static_cast<double>(SideSin()) * view[0];
    if (a - b > radius || radius < a + b)
        return false;
    double c = static_cast<double>(VerticalCos()) * view[2];
    double d = static_cast<double>(VerticalSin()) * view[1];
    if (d + c > radius || radius < c - d)
        return false;
    return true;
}

} // namespace

// 0x004D8BB0
int __cdecl CompareOccluders(const void* a, const void* b)
{
    auto* first = static_cast<Occluder*>(const_cast<void*>(a));
    auto* second = static_cast<Occluder*>(const_cast<void*>(b));
    if (!first->poly->enabled)
        first->hidden[0] = 0;
    if (!second->poly->enabled)
        second->hidden[0] = 0;
    int firstHidden0 = first->hidden[0], secondHidden0 = second->hidden[0];
    if (!first->poly->enabled)
        first->hidden[1] = 0;
    if (!second->poly->enabled)
        second->hidden[1] = 0;
    int firstSum = firstHidden0 + first->hidden[1];
    int secondSum = secondHidden0 + second->hidden[1];
    if (firstSum < secondSum)
        return 1;
    return firstSum <= secondSum ? 0 : -1;
}

// 0x004D8C10
void __cdecl SortOccluders()
{
    uint32_t count = OccluderCount();
    if (count == 0)
        return;
    Qsort(Occluders(), count, sizeof(Occluder), &CompareOccluders);
    for (int i = static_cast<int>(OccluderCount()) - 1; i >= 0; --i) {
        Occluder& occluder = Occluders()[i];
        if (occluder.hidden[1] + occluder.hidden[0] == 0) {
            // Hid nothing: its polygon is free again (the list is sorted, so it's at the end).
            occluder.poly->inUse = 0;
            --OccluderCount();
        } else {
            occluder.hidden[Frame()] = 0;
        }
    }
}

// 0x004D8C90
void __cdecl EnableOcclusionPoly(uint32_t checksum, bool enabled)
{
    for (uint32_t i = 0; i < PolyCount(); ++i) {
        if (Polys()[i].checksum == checksum)
            Polys()[i].enabled = enabled;
    }
}

// 0x004D8CC0
void __cdecl RemoveAllOcclusionPolys()
{
    OccluderCount() = 0;
    PolyCount() = 0;
    NextPoly() = 0;
}

// 0x004D8CE0
void __cdecl AddOccluders()
{
    uint32_t count = PolyCount();
    if (count == 0)
        return;
    uint32_t next = NextPoly();
    uint32_t occluders = OccluderCount();
    uint32_t added = 0;
    for (uint32_t looked = 0;;) {
        OcclusionPoly& poly = Polys()[next++];
        if (!poly.inUse && poly.enabled) {
            if (occluders < kMaxOccluders) {
                Occluder& occluder = Occluders()[occluders++];
                occluder.poly = &poly;
                poly.inUse = 1;
                occluder.hidden[0] = 0;
                occluder.hidden[1] = 0;
            }
            ++added; // (counted even when there's no room)
        }
        ++looked;
        if (next >= count)
            next = 0;
        if (looked >= count || added >= 4)
            break;
    }
    OccluderCount() = occluders;
    NextPoly() = next;
}

// 0x004D8D70
void __cdecl AddOcclusionPoly(const Mth::Vector* p0, const Mth::Vector* p1, const Mth::Vector* p2,
                              const Mth::Vector* p3, uint32_t checksum)
{
    OcclusionPoly& poly = Polys()[PolyCount()];
    poly.inUse = 0;
    poly.enabled = 1;
    poly.checksum = checksum;
    const Mth::Vector* corners[4] = {p0, p1, p2, p3};
    for (int i = 0; i < 4; ++i) {
        poly.corners[i][0] = corners[i]->x;
        poly.corners[i][1] = corners[i]->y;
        poly.corners[i][2] = corners[i]->z;
        poly.corners[i][3] = corners[i]->w;
    }
    // The normal: (p1 - p0) x (p3 - p0), the first component of p3 - p0 kept in double precision.
    double a0 = static_cast<double>(p3->x) - p0->x;
    float a1 = static_cast<float>(static_cast<double>(p3->y) - p0->y);
    float a2 = static_cast<float>(static_cast<double>(p3->z) - p0->z);
    float b0 = static_cast<float>(static_cast<double>(p1->x) - p0->x);
    float b1 = static_cast<float>(static_cast<double>(p1->y) - p0->y);
    float b2 = static_cast<float>(static_cast<double>(p1->z) - p0->z);
    poly.normal[0] = static_cast<float>(static_cast<double>(b1) * a2 - static_cast<double>(b2) * a1);
    poly.normal[1] = static_cast<float>(b2 * a0 - static_cast<double>(a2) * b0);
    poly.normal[2] = static_cast<float>(static_cast<double>(b0) * a1 - b1 * a0);
    poly.normal[3] = 0.0f;
    reinterpret_cast<Mth::Vector*>(poly.normal)->Normalize();
    ++PolyCount();
}

// 0x004D8ED0
void __cdecl BuildOccluders(const Mth::Vector* eye, int frame)
{
    Frame() = frame;
    SortOccluders();
    AddOccluders();
    const float* e = &eye->x;
    using L = LengthOrder;
    using D = DotOrder;
    for (uint32_t i = 0; i < OccluderCount(); ++i) {
        Occluder& occluder = Occluders()[i];
        const OcclusionPoly& poly = *occluder.poly;
        const float* p0 = poly.corners[0];
        const float* p1 = poly.corners[1];
        const float* p2 = poly.corners[2];
        const float* p3 = poly.corners[3];
        // Which side of the polygon the eye is on decides the corners' order.
        double facing = ((static_cast<double>(e[2]) - p0[2]) * poly.normal[2] +
                         (static_cast<double>(e[1]) - p0[1]) * poly.normal[1]) +
                        (static_cast<double>(e[0]) - p0[0]) * poly.normal[0];
        if (facing >= 0.0) {
            SetPlane(occluder.planes[0], p0, p2, p3, L::XZY, false, p0, D::ZYX);
            SetPlane(occluder.planes[1], e, p0, p1, L::XZY, false, e, D::XYZ);
            SetPlane(occluder.planes[2], e, p2, p3, L::XZY, false, e, D::XYZ);
            SetPlane(occluder.planes[3], e, p1, p2, L::XZY, false, e, D::XYZ);
            SetPlane(occluder.planes[4], e, p3, p0, L::ZYX, false, e, D::XYZ);
        } else { // (also when it's not a number)
            SetPlane(occluder.planes[0], p0, p3, p2, L::XZY, false, p0, D::ZYX);
            SetPlane(occluder.planes[1], e, p1, p0, L::ZYX, true, e, D::XYZ);
            SetPlane(occluder.planes[2], e, p3, p2, L::ZYX, false, e, D::XYZ);
            SetPlane(occluder.planes[3], e, p2, p1, L::XZY, false, e, D::XYZ);
            SetPlane(occluder.planes[4], e, p0, p3, L::XZY, false, e, D::XYZ);
        }
    }
}

// 0x004D9AC0
bool __cdecl SphereOccluded(const Mth::Vector* centre, float radius, int weight)
{
    double x = centre->x, y = centre->y, z = centre->z;
    if (const float* world = WorldTransform()) {
        x = static_cast<double>(world[12]) + centre->x;
        y = static_cast<double>(world[13]) + centre->y;
        z = static_cast<double>(world[14]) + centre->z;
    }
    float limit = -radius;
    for (uint32_t i = 0; i < OccluderCount(); ++i) {
        Occluder& occluder = Occluders()[i];
        int plane = 0;
        for (; plane < 5; ++plane) {
            const float* p = occluder.planes[plane];
            double distance = ((z * p[2] + x * p[0]) + y * p[1]) - p[3];
            if (distance >= limit) // (not a number counts as behind)
                break;
        }
        if (plane == 5) {
            occluder.hidden[Frame()] += weight;
            return true;
        }
    }
    return false;
}

// 0x004DB570
int __cdecl SphereOutsideView(const Mth::Vector* centre, float radius)
{
    float view[4];
    Transform(view, &centre->x, ViewMatrix());
    return InsideFrustum(view, radius) ? 0 : 1;
}

// 0x004DB790
bool __cdecl SphereInView(const Mth::Vector* centre, float radius)
{
    float moved[4];
    const float* point = &centre->x;
    if (const float* world = WorldTransform()) {
        moved[0] = world[12] + centre->x;
        moved[1] = world[13] + centre->y;
        moved[2] = world[14] + centre->z;
        point = moved;
    }
    float view[4];
    Transform(view, point, ViewMatrix());
    GameVariable<float>(0x007CA978) = static_cast<float>(-static_cast<double>(view[2]) - radius);
    return InsideFrustum(view, radius);
}

// 0x004DB6D0
bool __cdecl BeyondClipDistance(const void* bounds)
{
    if (!GameVariable<bool>(0x007D643A))
        return false;
    const auto* sphere = static_cast<const float*>(bounds);
    float radius = sphere[4];
    float centre[3] = {sphere[1], sphere[2], sphere[3]};
    float moved[3];
    const float* point = centre;
    if (const float* world = WorldTransform()) {
        moved[0] = centre[0] + world[12];
        moved[1] = centre[1] + world[13];
        moved[2] = centre[2] + world[14];
        point = moved;
    }
    float view[4];
    Transform(view, point, ViewMatrix());
    double distance = std::sqrt((static_cast<double>(view[2]) * view[2] + static_cast<double>(view[1]) * view[1]) +
                                static_cast<double>(view[0]) * view[0]);
    return distance - radius > FarClip();
}

} // namespace Nx

REPLACE(0x004D8BB0, Nx::CompareOccluders);
REPLACE(0x004D8C10, Nx::SortOccluders);
REPLACE(0x004D8C90, Nx::EnableOcclusionPoly);
REPLACE(0x004D8CC0, Nx::RemoveAllOcclusionPolys);
REPLACE(0x004D8CE0, Nx::AddOccluders);
REPLACE(0x004D8D70, Nx::AddOcclusionPoly);
REPLACE(0x004D8ED0, Nx::BuildOccluders);
REPLACE(0x004D9AC0, Nx::SphereOccluded);
REPLACE(0x004DB570, Nx::SphereOutsideView);
REPLACE(0x004DB790, Nx::SphereInView);
REPLACE(0x004DB6D0, Nx::BeyondClipDistance);
