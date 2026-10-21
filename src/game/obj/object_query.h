#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

// The object query component (type 0xB87C8E43 - its name isn't in THUG2's scripts -, vtable 0x00645DC4, 0x4C bytes;
// made by 0x0041ADC0; every composite object gets one, AddBaseComponents): the member functions Obj_ObjectInRadius,
// Obj_ObjectInRadiusOfNode and Obj_ObjectInRect - whether a skater / ped / car / game object (other than the object
// itself) is within a distance of the object (or of a node), or inside a box turned with the object. Its own box (the
// other objects' boxes give the height of their middle) is set only by the constructor: +-10 about the origin.
//
// Vtable: 0 the scalar deleting destructor, 12 CallMemberFunction; the rest the base's / shared (1 0x0051B970, 2 / 8
// / 10 0x005BAB90, 3 / 4 / 9 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 0x0045D760, 11 0x0045D720).
class CObjectQueryComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0xB87C8E43;

    // The box of Obj_ObjectInRect (a local of CallMemberFunction; no constructor or vtable).
    struct Rect {
        Mth::Matrix m_matrix;  // +0x00 the object's orientation (turned by m_angle about Y), then inverted
        float m_height;        // +0x40 height (inches)
        float m_dist;          // +0x44 dist (inches): forwards
        float m_width;         // +0x48 width (inches)
        float m_angle;         // +0x4C angle (radians)
        Mth::Vector m_max;     // +0x50 the box in the object's frame: (width / 2, height / 2, dist, 1) + m_offset
        Mth::Vector m_min;     // +0x60 (-width / 2, -height / 2, 0, -1) + m_offset
        Mth::Vector m_offset;  // +0x70 the component's offset + offset (inches)
        uint8_t unknown80[4];
        bool m_computed;       // +0x84 m_matrix, m_max and m_min done (ComputeRect)
        bool m_radiusComputed; // +0x85 m_radiusSqr done (RectRadiusSqr)
        uint8_t pad86[2];
        float m_radiusSqr;     // +0x88
    };

    // 0x0041ADC0 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0041AAD0: the base's constructor, its vtable and type; the box (10, 10, 10, 1) - (-10, -10, -10, 1), the
    // offset (0, 0, 0, 1). Returns this.
    CObjectQueryComponent* Construct();
    // 0x0041A810: the base's body.
    void DestroyBody();
    // 0x0041A880 (vtable slot 0)
    CObjectQueryComponent* Destroy(unsigned int flags);
    // 0x0041B1A0 (vtable slot 12): Obj_ObjectInRect (0x6520B902; a Rect from `params`), Obj_ObjectInRadius
    // (0x67660FE5), Obj_ObjectInRadiusOfNode (0xDCE760A1: about the position of `node` - none: 0): 1 / 0 for whether
    // an object is there (QueryObjects); anything else 2 (not its own).
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    // 0x0041A950: `rect` from `params`: offset (feet, added to the component's offset), angle (degrees), height,
    // dist, width (feet; 0 when not given); not computed.
    void InitRect(Rect* rect, Script::CStruct* params);
    // 0x0041AB20: once: the matrix (the object's orientation, turned by the angle about Y first when it isn't 0),
    // inverted; the box's corners.
    void ComputeRect(Rect* rect);
    // 0x0041AC40: whether `object` is inside `rect`: within the box's bounding distance of the box's middle, then
    // the middle of its box (its position + half its box's height) brought into the box's frame (PointInBox).
    bool IsInRect(CCompositeObject* object, Rect* rect);
    // 0x0041AE20: whether a skater other than the object is inside `rect`, or (without one) nearer than the square
    // root of `radiusSqr` to `point` (none: the object's position).
    bool AnySkaterNear(Rect* rect, float radiusSqr, const Mth::Vector* point);
    // 0x0041AF10: the same for the objects of `type`: skater (0x5B8AB877, AnySkaterNear), ped (0x061A741E), gameobj
    // (0x3B5737A6) or car (0x88C21962) - the objects of their object types in the object's manager. Anything else:
    // false. (`params` and `script` aren't used.)
    bool AnyObjectNear(Script::CStruct* params, Script::CScript* script, float radiusSqr, Rect* rect, uint32_t type,
                       const Mth::Vector* point);
    // 0x0041B0A0: the query of the three members: without a rect, radius (feet; none: false); type - a checksum or an
    // array of them (any one there: true; neither: false).
    bool QueryObjects(Script::CStruct* params, Script::CScript* script, Rect* rect, const Mth::Vector* point);

    Mth::Vector m_boxMax; // +0x18
    Mth::Vector m_boxMin; // +0x28
    Mth::Vector m_offset; // +0x38
    uint8_t unknown48[4];
};
static_assert(offsetof(CObjectQueryComponent, m_boxMax) == 0x18 && offsetof(CObjectQueryComponent, m_boxMin) == 0x28 &&
                  offsetof(CObjectQueryComponent, m_offset) == 0x38 && sizeof(CObjectQueryComponent) == 0x4C,
              "layout must match the original");
static_assert(offsetof(CObjectQueryComponent::Rect, m_height) == 0x40 &&
                  offsetof(CObjectQueryComponent::Rect, m_angle) == 0x4C &&
                  offsetof(CObjectQueryComponent::Rect, m_max) == 0x50 &&
                  offsetof(CObjectQueryComponent::Rect, m_offset) == 0x70 &&
                  offsetof(CObjectQueryComponent::Rect, m_computed) == 0x84 &&
                  offsetof(CObjectQueryComponent::Rect, m_radiusSqr) == 0x88 &&
                  sizeof(CObjectQueryComponent::Rect) == 0x8C,
              "layout must match the original");

// 0x0041A8B0 (cdecl): the square of a distance that holds the whole box: (the largest of height, dist and width + the
// largest of the offset's x, y and z, all as magnitudes) squared - computed once (kept as a float; the first call
// returns it unrounded, as the original does on the FPU stack).
double RectRadiusSqr(CObjectQueryComponent::Rect* rect);

} // namespace Obj
