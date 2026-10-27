#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

// The object proximity component ("objectproximity", type 0x1D5B1AE6, vtable 0x0064B6C4, 0x30 bytes; made by
// 0x004EC420): radii (kept squared, in inches: the scripts give feet) round an object and the nearest skater's
// distance from it. Its script member functions are the table at 0x00682620 (filled at start-up).
class CObjectProximityComponent : public CBaseComponent {
public:
    // 0x004EC420: a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x004EC220 (not a Ghidra function): its script member functions (the table at 0x00682620) registered.
    static void __cdecl RegisterMemberFunctions();
    // 0x004EC370: the base's constructor, its vtable and type. Returns this.
    CObjectProximityComponent* Construct();
    // 0x004EC240: the destructor body (the base's).
    void DestroyBody();
    // 0x004EC390 (vtable slot 0)
    CObjectProximityComponent* Destroy(unsigned int flags);

    // 0x004EC3C0: the squared distance from the local skater (unrounded, in ST0), its id into `skaterId`; no local
    // skater, or this object is it: 0x55CE224C (about 2.8e13).
    double LocalSkaterDistanceSquared(uint32_t* skaterId);
    // 0x004EC560 (vtable slot 1): the events of the radii given (non-zero): inner - the local skater in it
    // 0x7048029A (and 0xF5B2A042), another skater 0xF5B2A042; outer - outside it 0x91BCAD1D; inner avoid - in it
    // 0xD69BD3DE; outer avoid - outside it 0x35FDAFAE; attention - in it 0xA06C9031, else 0x9C1CDE7A. One distance
    // is worked out and reused (the nearest skater's or the local one's - as the original mixes them). A dead
    // object gets only the inner radius's first event.
    void Update();
    // 0x004EC4B0: the squared distance from the nearest other skater, its id into `skaterId` (none nearer than
    // 0x55CE224C: that, the id left alone).
    float NearestSkaterDistanceSquared(uint32_t* skaterId);

    // --- script member functions (the table at 0x00682620): the radius (the unnamed float, in feet) kept as
    // (12 x it) squared. True. ---
    // 0x004EC250 [Obj_SetInnerRadius]
    bool Obj_SetInnerRadius(Script::CStruct* params, Script::CScript* script);
    // 0x004EC280 [Obj_SetOuterRadius]
    bool Obj_SetOuterRadius(Script::CStruct* params, Script::CScript* script);
    // 0x004EC2B0 [Obj_SetInnerAvoidRadius]
    bool Obj_SetInnerAvoidRadius(Script::CStruct* params, Script::CScript* script);
    // 0x004EC2E0 [Obj_SetOuterAvoidRadius]
    bool Obj_SetOuterAvoidRadius(Script::CStruct* params, Script::CScript* script);
    // 0x004EC310 [Obj_SetAttentionRadius]: with the flag 0xFD2EACAD only if none is set yet.
    bool Obj_SetAttentionRadius(Script::CStruct* params, Script::CScript* script);
    // 0x004EC480 [Obj_ObjectInAttentionRadius]: the local skater within the attention radius.
    bool Obj_ObjectInAttentionRadius(Script::CStruct* params, Script::CScript* script);

    float m_innerRadiusSquared;       // +0x18
    float m_outerRadiusSquared;       // +0x1C
    float m_innerAvoidRadiusSquared;  // +0x20
    float m_outerAvoidRadiusSquared;  // +0x24
    float m_attentionRadiusSquared;   // +0x28
    uint32_t m_unknown2C;             // +0x2C
};
static_assert(offsetof(CObjectProximityComponent, m_innerRadiusSquared) == 0x18 &&
                  offsetof(CObjectProximityComponent, m_attentionRadiusSquared) == 0x28 &&
                  sizeof(CObjectProximityComponent) == 0x30,
              "layout must match the original");

} // namespace Obj
