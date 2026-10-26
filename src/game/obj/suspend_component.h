#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

// The suspend component (type 0xCE0CA665, "suspend"; vtable 0x006465C8, 0x4C bytes; made by 0x00434290): suspends its
// object while the camera is further than SuspendDistance from it (unless NeverSuspend - the default until a distance
// is given), and decides how often its model animates and whether it's drawn (its levels of detail, lod_dist1-4). The
// component suspends itself at its first update: a task (made by RegisterMemberFunctions) runs every suspend component
// each frame instead. Its script member function: Obj_ResetNoSuspendCount (the table at 0x00684C48, filled by
// 0x00644560).
//
// Vtable: 0 the scalar deleting destructor; 1 Update; 2 InitFromStructure; 3 Finalize; 4 / 9 the empty 0x005480A0; 5
// the shared 0x004E3F90; 6 0x0048DCD0; 7 / 8 / 10 the empty 0x005BAB90 (it can't be suspended by its object); 11
// 0x0042BB20 (shared); 12 CallMemberFunction 0x0051ADC0 (none by name).
class CSuspendComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0xCE0CA665;

    // 0x00434290 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x00434610 (not a Ghidra function; registered with Create): its script member functions registered, then the
    // suspend task made (CreateSuspendTask).
    static void __cdecl RegisterMemberFunctions();
    // 0x00433F50: the base's constructor, its vtable and type; NeverSuspend; the suspend distance
    // DefaultMovingObjectSuspendDistance feet (and its square); 10 frames before the first check, 3 before the LOD
    // checks. Returns this.
    CSuspendComponent* Construct();
    // 0x00433E10: the base's body.
    void DestroyBody();
    // 0x00433FD0 (vtable slot 0)
    CSuspendComponent* Destroy(unsigned int flags);
    // 0x00433E50 (vtable slot 1): suspends itself (the base's Suspend(true)) - the task does its work.
    void Update();
    // 0x00434000 (vtable slot 2): SuspendDistance (feet; 0: lod_dist1, else the default) - which turns NeverSuspend
    // off -, NeverSuspend; the LOD distances (feet): none with NoLOD (the script integer), else lod_dist1 (50),
    // lod_dist2 (lod_dist1 + 75), lod_dist3 / lod_dist4 (1000001 / 1000002), the first two scaled by the script
    // floats LOD0DistanceMultiplier / LOD1DistanceMultiplier (out-of-order distances only reported, compiled out).
    void InitFromStructure(Script::CStruct* params);
    // 0x004341C0 (vtable slot 3): the model component found and kept.
    void Finalize();

    // 0x00433E60: 10 frames before the next suspend check and the LOD checks.
    void ResetCounts();
    // 0x00433E70: the LOD distances out of reach (no LOD).
    void DisableLod();
    // 0x00433E90: whether it's too far away (suspended).
    bool IsFar();
    // 0x00433EA0: whether the model animates this frame: always within the LOD frames; within lod_dist1, every frame
    // within AnimLODInterleave2, every second one within AnimLODInterleave4, every fourth beyond (the frame counters
    // 0x006951B4 / 0x006951B8); never beyond lod_dist1. The distance squared through `distanceSquared` if given.
    bool ShouldAnimate(float* distanceSquared);
    // 0x004342F0: whether the model is drawn: not when the object is invisible; always without LOD, in more than one
    // viewport (0x00701378 > 1) or when the model asks (+0x234); otherwise within lod_dist2 and its bounding sphere in
    // view (0x0048C490). Told to the model (0x00493C60); a model just made visible is updated at once.
    void UpdateVisibility();

    // --- script member functions (the table at 0x00684C48) ---
    // 0x004341E0 (not a Ghidra function) Obj_ResetNoSuspendCount: ResetCounts and the object unsuspended. True.
    bool Obj_ResetNoSuspendCount(Script::CStruct* params, Script::CScript* script);

    bool m_neverSuspend;             // +0x18
    uint8_t pad19[3];
    float m_suspendDistance;         // +0x1C (inches)
    float m_suspendDistanceSquared;  // +0x20
    float m_distanceSquared;         // +0x24 to the camera, at the last check (0 when not checked)
    int m_framesBeforeCheck;         // +0x28
    CBaseComponent* mp_model;        // +0x2C
    bool m_far;                      // +0x30 suspended by distance
    uint8_t pad31[3];
    float m_lodDistances[4];         // +0x34 (inches)
    int m_framesBeforeLod;           // +0x44
    uint32_t m_unused48;             // +0x48
};
static_assert(offsetof(CSuspendComponent, m_suspendDistance) == 0x1C && offsetof(CSuspendComponent, m_far) == 0x30 &&
                  offsetof(CSuspendComponent, m_lodDistances) == 0x34 &&
                  offsetof(CSuspendComponent, m_framesBeforeLod) == 0x44 && sizeof(CSuspendComponent) == 0x4C,
              "layout must match the original");

// 0x00433E20: a new frame for the animation LOD counters (0x006951B0 counts frames; 0x006951B4 starts each frame at
// the count, 0x006951B8 at the count with the low bits changed by its second bit - spreading the interleaved objects
// over the frames).
void __cdecl NextSuspendFrame();
// 0x004345B0: the suspend task's holder made (0x006951BC; 4 bytes - the task - by 0x00434520).
void __cdecl CreateSuspendTask();
struct SuspendTaskHolder {
    // 0x00434520: the task (Tsk::Task, 0x20 bytes, priority -1100, callback SuspendTask, its data this holder) made
    // and added to the main loop's logic tasks (none if out of memory: null added). Returns this.
    SuspendTaskHolder* Construct();

    void* mp_task;
};
// 0x00434420 (not a Ghidra function; the task's callback): every suspend component: its check frames counted down
// (all of them at once: the original doesn't move on to the next component while counting), else - unless the camera
// data (the skate module's +0x3DC) says otherwise or it never suspends - after its LOD frames, its distance squared to
// the camera against the suspend distance; the object suspended or woken to match; then the model updated (during the
// LOD frames: shown) or its visibility decided (UpdateVisibility).
void __cdecl SuspendTask(void* task);

} // namespace Obj
