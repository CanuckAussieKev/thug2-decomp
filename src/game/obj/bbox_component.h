#pragma once

#include <cstddef>
#include <cstdint>

#include "game/maths/mat4.h"
#include "game/maths/vec4.h"
#include "game/obj/entity.h"

namespace Scripts {
class CScript;
class CStruct;
} // namespace Script

namespace GameObjects {

// The bounding box component (type 0xB87C8E43 "bbox", vtable 0x00645DC4, 0x4C bytes; made by 0x0041ADC0; every
// composite object gets one, AddBaseComponents): the member functions Obj_ObjectInRadius,
// Obj_ObjectInRadiusOfNode and Obj_ObjectInRect - whether a skater / ped / car / game object (other than the object
// itself) is within a distance of the object (or of a node), or inside a box turned with the object. Its own box (the
// other objects' boxes give the height of their middle) is set only by the constructor: +-10 about the origin.
//
// Vtable: 0 the scalar deleting destructor, 12 InvokeMethodByName; the rest the base's / shared (1 0x0051B970, 2 / 8
// / 10 0x005BAB90, 3 / 4 / 9 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 0x0045D760, 11 0x0045D720).
class CBBoxComponent : public CObjectModule {
public:
    static constexpr uint32_t kType = 0xB87C8E43;

    // The box of Obj_ObjectInRect (a local of InvokeMethodByName; no constructor or vtable).
    struct Rect2 {
        Maths::Mat4 m_basisFrame;  // +0x00 the object's orientation (turned by m_angle about Y), then inverted
        float m_levelY;        // +0x40 height (inches)
        float m_separation;          // +0x44 dist (inches): forwards
        float m_span;         // +0x48 width (inches)
        float m_angle;         // +0x4C angle (radians)
        Maths::Vec4 m_upperLimit;     // +0x50 the box in the object's frame: (width / 2, height / 2, dist, 1) + m_shift
        Maths::Vec4 m_lowerLimit;     // +0x60 (-width / 2, -height / 2, 0, -1) + m_shift
        Maths::Vec4 m_shift;  // +0x70 the component's offset + offset (inches)
        uint8_t unknown80[4];
        bool m_computed;       // +0x84 m_basisFrame, m_upperLimit and m_lowerLimit done (ComputeRect)
        bool m_radiusComputed; // +0x85 m_reachSquared done (RectRadiusSqr)
        uint8_t pad86[2];
        float m_reachSquared;     // +0x88
    };

    // 0x0041ADC0 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static CObjectModule* __cdecl Fabricate();
    // 0x0041AAD0: the base's constructor, its vtable and type; the box (10, 10, 10, 1) - (-10, -10, -10, 1), the
    // offset (0, 0, 0, 1). Returns this.
    CBBoxComponent* MakeNew();
    // 0x0041A810: the base's body.
    void DestroyBody();
    // 0x0041A880 (vtable slot 0)
    CBBoxComponent* Teardown(unsigned int flags);
    // 0x0041B1A0 (vtable slot 12): Obj_ObjectInRect (0x6520B902; a Rect from `params`), Obj_ObjectInRadius
    // (0x67660FE5), Obj_ObjectInRadiusOfNode (0xDCE760A1: about the position of `node` - none: 0): 1 / 0 for whether
    // an object is there (QueryObjects); anything else 2 (not its own).
    int InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script);

    // 0x0041A950: `rect` from `params`: offset (feet, added to the component's offset), angle (degrees), height,
    // dist, width (feet; 0 when not given); not computed.
    void InitRect(Rect2* rect, Scripts::CStruct* params);
    // 0x0041AB20: once: the matrix (the object's orientation, turned by the angle about Y first when it isn't 0),
    // inverted; the box's corners.
    void ComputeRect(Rect2* rect);
    // 0x0041AC40: whether `object` is inside `rect`: within the box's bounding distance of the box's middle, then
    // the middle of its box (its position + half its box's height) brought into the box's frame (PointInBox).
    bool IsInRect(CGameEntity* object, Rect2* rect);
    // 0x0041AE20: whether a skater other than the object is inside `rect`, or (without one) nearer than the square
    // root of `reachSquared` to `point` (none: the object's position).
    bool AnySkaterNear(Rect2* rect, float reachSquared, const Maths::Vec4* point);
    // 0x0041AF10: the same for the objects of `type`: skater (0x5B8AB877, AnySkaterNear), ped (0x061A741E), gameobj
    // (0x3B5737A6) or car (0x88C21962) - the objects of their object types in the object's manager. Anything else:
    // false. (`params` and `script` aren't used.)
    bool AnyObjectNear(Scripts::CStruct* params, Scripts::CScript* script, float reachSquared, Rect2* rect, uint32_t type,
                       const Maths::Vec4* point);
    // 0x0041B0A0: the query of the three members: without a rect, radius (feet; none: false); type - a checksum or an
    // array of them (any one there: true; neither: false).
    bool QueryObjects(Scripts::CStruct* params, Scripts::CScript* script, Rect2* rect, const Maths::Vec4* point);

    Maths::Vec4 m_boxMax; // +0x18
    Maths::Vec4 m_boxMin; // +0x28
    Maths::Vec4 m_shift; // +0x38
    uint8_t unknown48[4];
};
static_assert(offsetof(CBBoxComponent, m_boxMax) == 0x18 && offsetof(CBBoxComponent, m_boxMin) == 0x28 &&
                  offsetof(CBBoxComponent, m_shift) == 0x38 && sizeof(CBBoxComponent) == 0x4C,
              "layout must match the original");
static_assert(offsetof(CBBoxComponent::Rect2, m_levelY) == 0x40 &&
                  offsetof(CBBoxComponent::Rect2, m_angle) == 0x4C &&
                  offsetof(CBBoxComponent::Rect2, m_upperLimit) == 0x50 &&
                  offsetof(CBBoxComponent::Rect2, m_shift) == 0x70 &&
                  offsetof(CBBoxComponent::Rect2, m_computed) == 0x84 &&
                  offsetof(CBBoxComponent::Rect2, m_reachSquared) == 0x88 &&
                  sizeof(CBBoxComponent::Rect2) == 0x8C,
              "layout must match the original");

// 0x0041A8B0 (cdecl): the square of a distance that holds the whole box: (the largest of height, dist and width + the
// largest of the offset's x, y and z, all as magnitudes) squared - computed once (kept as a float; the first call
// returns it unrounded, as the original does on the FPU stack).
double RectRadiusSqr(CBBoxComponent::Rect2* rect);

} // namespace Obj
