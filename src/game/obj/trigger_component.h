#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Nx {
class CFeeler;
} // namespace Nx

namespace Script {
class CArray;
class CScript;
class CStruct;
} // namespace Script

// The trigger component (type 0xE594F0A2, vtable 0x00646648, 0x2C bytes; made by 0x00434B90): what runs a node's
// TriggerScript when the object passes through a trigger face (a face flagged 0x40 - a gap's geometry, a kill zone)
// or its collision feelers touch one - the skater landing on a gap. Each update it feels along the line from where
// the object was to where it is now. TriggerType asks which of the events (a bit each) set it off last.
//
// Vtable: 0 the scalar deleting destructor; 1 Update; 2 InitFromStructure; 3 / 4 / 9 the empty 0x005480A0; 5 the
// shared 0x004E3F90; 6 0x0048DCD0; 7 the base's Suspend; 8 / 10 the empty 0x005BAB90; 11 the base's member function
// table entry 0x0045D720; 12 CallMemberFunction.
namespace Obj {

class CTriggerComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0xE594F0A2;

    // 0x00434B90 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory): the
    // base's constructor, its vtable and type; the rest zero (a cleared allocation).
    static CBaseComponent* __cdecl Create();
    // 0x00434900: the destructor body (the base's).
    void DestroyBody();
    // 0x00434910 (vtable slot 0)
    CTriggerComponent* Destroy(unsigned int flags);
    // 0x00434940 (vtable slot 2): where the object is now (where the next update feels from).
    void InitFromStructure(Script::CStruct* params);
    // 0x00434D90 (vtable slot 1): if the object has moved since the last update - and wasn't teleported - a feeler
    // from where it was to where it is (faces flagged 0x50 only, movable objects too) with FeelerHit as its callback
    // for each face it crosses; then where it is now kept.
    void Update();
    // 0x00434970 (vtable slot 12): TriggerType [0x3A717382]: whether the last trigger's events include all of
    // those in the parameters' unnamed integer array - or of any one of the arrays in an unnamed array of arrays
    // (a negative event is no event). 1 if so; 0 if not, or for an array of anything else; 2 for other names.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    // 0x00434A60: the trigger of the node named `name` (a face's collision object): the node is the one named so -
    // looked up in the node manager (GetNode) when `nodes` is null, else searched for in `nodes` from its end - its
    // TriggerScript run on the object (SpawnAndRunScript, with its NetEnabled / Permanent flags, the node's name and
    // the parameters structure 0x5180A4DB of the node named by `other` +0x14 when there is one), the events recorded
    // (+0x28: a bit for each of the three in `events` that isn't 0). False when there's no node (ours - see the .cpp),
    // no TriggerScript, or the skate module skips the node (0x00565BB0).
    bool TriggerNode(const int* events, uint32_t name, Script::CArray* nodes, const uint8_t* other);
    // 0x00434BF0: the face `feeler` hit, if it's a trigger face (flag 0x40) with a collision object name: its
    // trigger (TriggerNode) - while a movable object is being tested, or the hit object is still alive, through
    // that object's node array (its 0xC472ECC5 component's slot 13; none: the node manager) and with that object as
    // `other`. False if it's no trigger face, or the hit object is gone.
    bool CheckFeeler(const int* events, Nx::CFeeler* feeler);
    // 0x00434CE0: each of the trigger faces in `hits` (from the last; +0xC30 the count, 0x60 bytes each from +0x88:
    // +0 the name, +4 whether it's a trigger) whose name isn't met again later in the list: its trigger
    // (TriggerNode, no node array or other object).
    void TriggerHits(const int* events, const uint8_t* hits);
    // 0x00434D70 (not a Ghidra function; cdecl): the feeler's callback during Update: CheckFeeler with no events
    // (0x0064663C) on the component it carries (+0xA8).
    static bool __cdecl FeelerHit(Nx::CFeeler* feeler);

    Mth::Vector m_lastPos; // +0x18 where the object was at the last update
    uint32_t m_events;     // +0x28 the last trigger's events (a bit for each)
};
static_assert(offsetof(CTriggerComponent, m_lastPos) == 0x18 && offsetof(CTriggerComponent, m_events) == 0x28 &&
                  sizeof(CTriggerComponent) == 0x2C,
              "layout must match the original");

} // namespace Obj
