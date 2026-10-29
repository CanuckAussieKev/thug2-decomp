#pragma once

#include <cstddef>
#include <cstdint>

namespace Obj {
class CCompositeObject;
}
namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The animation controllers an object's animation blender stacks up (made by the factory 0x00481FB0 from a
// structure's Type, with its Priority): each fills in a pose (Gfx::CPose: 64 rotations at +0, 64 translations at
// +0x400, more at +0x800) for the bones of the object's skeleton and answers script member functions. They all
// derive from the base controller (CAnimController: vtable 0x0064877C, constructed by 0x00481710; e.g. its
// InitFromStructure 0x00481740 reads `id`).
//
// Vtable slots: 0 the scalar deleting destructor, 1 GetPose(pose), 2 (0x005BAB90: ignores its argument),
// 3 CallMemberFunction(name, params, script) -> 0 false / 1 handled / 2 not its own, 4 InitFromStructure(params),
// 5 Update (once a frame).
namespace Gfx {

// The animation timeline a controller plays on (vtable 0x00648768; 0x00481650 constructs one): an animation
// between two times, the current time stepping by speed * 60 per second in a direction, with a loop type. It's a
// node of the engine's intrusive lists (Mem::ListNode: data, priority, next, prev at +4..+0x10).
// Vtable slots: 0 Destroy, 1 Reset, 2 Update, 3 PlayAnim, 4 (0x005BAB90: ignores its argument).
struct CAnimTimeline {
    enum LoopType : int { kOnce = 0, kCycle = 1, kPingPong = 2, kHold = 3 };

    // 0x00481650: an empty timeline, unlinked (once, forwards, no animation: 0xF60C9090).
    CAnimTimeline* Construct();
    // 0x00481690: the list node's destructor body - its vtable 0x006486CC, unlinked (the channel's list nodes and
    // the blend timeline use it too). 0x004816E0 (slot 0, also the channel list head's): that, then freed.
    void DestroyBody();
    CAnimTimeline* Destroy(uint8_t flags);
    // 0x00481380 (slot 1): back to the constructor's times, speed, direction, loop type and animation.
    void Reset();
    // 0x00481640 (slot 2): the current time stepped (NextTime).
    void Update();
    // 0x00481430 (slot 3): `anim` from `from` to `to` (backwards when `to` isn't above it) at `speed` (per 1/60 s);
    // `blendPeriod` isn't used here.
    void PlayAnim(uint32_t anim, float from, float to, int loopType, float blendPeriod, float speed);
    // 0x004813B0: the start, current and end times in the direction it plays (start and end swapped backwards).
    void GetTimes(float* start, float* current, float* end);
    // 0x004813F0: whether a once / ping-pong / hold timeline (not a cycling one) is at its end.
    bool IsComplete();
    // 0x004814B0: the time a frame on (the frame's slow-motion seconds x 60 x speed), wrapped, held or turned
    // round at the ends by the loop type, within the start and end; sets the complete flag. Unrounded (ST0).
    double NextTime();
    // 0x004816B0: `seconds` (in frames) added to the current time, then wrapped / held as a step of no time does.
    void AddTime(float frames);

    void** m_vtable;
    uint32_t m_unknown04[4];
    float m_start;      // +0x14
    float m_current;    // +0x18
    float m_end;        // +0x1C
    float m_speed;      // +0x20 (per 1/60 s)
    int m_direction;    // +0x24 0 forwards, -1 backwards
    int m_loopType;     // +0x28
    bool m_complete;    // +0x2C
    uint8_t m_pad2D[3];
    uint32_t m_anim;    // +0x30 the animation's name
};
static_assert(offsetof(CAnimTimeline, m_start) == 0x14 && offsetof(CAnimTimeline, m_anim) == 0x30 &&
                  sizeof(CAnimTimeline) == 0x34,
              "layout must match the original");

// The base controller's fields.
struct CAnimController {
    void** m_vtable;
    CAnimTimeline* mp_timeline;        // +0x04 (the timeline of the blender channel it belongs to)
    Obj::CCompositeObject* mp_object;  // +0x08 (set by some controllers only; GetObject asks the channel)
    int m_priority;                    // +0x0C
    uint32_t m_id;                     // +0x10

    // 0x00481710: on `timeline`, no object, priority 0, id 0 (vtable 0x0064877C). Returns this.
    CAnimController* Construct(CAnimTimeline* timeline);
    // 0x00481730: the base's vtable back (its destructor body). 0x004817A0 (slot 0): that, then freed.
    void DestroyBody();
    CAnimController* Destroy(uint8_t flags);
    // 0x00481740 (slot 4): `id` (0x40C698AF) from the parameters (GetChecksum's result).
    bool InitFromStructure(Script::CStruct* params);
    // 0x00481760: the object its timeline (a blend timeline) belongs to.
    Obj::CCompositeObject* GetObject();
    // 0x00481770: the object's skeleton component (0x222756D5)'s skeleton (0x005480F0: its +0x18), or null.
    uint8_t* GetSkeleton();

    // 0x004A4610 (slot 4 of the controllers without one of their own): a jump to the base's InitFromStructure
    // (0x00481740).
    void BaseInitFromStructure(Script::CStruct* params);
};
static_assert(sizeof(CAnimController) == 0x14, "layout must match the original");

// The standard controller (type 0xCB4533FF, vtable 0x00649F50, 0x28 bytes from its own item pools 0x00701430):
// plays an animation of the object's anim set on the channel's timeline.
class CStandardAnimController : public CAnimController {
public:
    // 0x004A4500: on `timeline`; a frame source of its own (0x0048BFC0), the object's anim set (the blender's
    // 0x00419AB0); no frame, no frame index (-1). Returns this.
    CStandardAnimController* Construct(CAnimTimeline* timeline);
    // 0x004A4590: the frame's index let go, the frame source freed, then the base's.
    void DestroyBody();
    // 0x004A55E0 (slot 0): back to the item pools.
    CStandardAnimController* Destroy(uint8_t flags);
    // 0x004A4F70 (slot 1)
    bool GetPose(uint8_t* pose);
    // 0x004A4670 (slot 3): PlayAnim (0xAF2FAE19: AnimName, StartTime, EndTime, LoopType, BlendPeriod, Speed),
    // 0xC68365EB (a new frame source for Anim), 0x92267217 (0x00485270 with the global 0x006F92B0's +0xD4),
    // InvalidateCache (refetches, then 2).
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x004A4620 (slot 5): the timeline steps; events between the two times; looping back refetches the frame.
    void Update();

    // 0x004A4F30 / 0x004A5600: the class's operator new / delete (its item pools).
    static void* operator new(size_t size, int unused);
    static void operator delete(void* item);
    static void operator delete(void* item, int) { operator delete(item); }

    void* mp_frame;       // +0x14 (unused)
    void* mp_frameData;   // +0x18 the current frame (0x0048CFA0 / 0x0048CFD0)
    void* mp_source;      // +0x1C its frame source
    uint32_t m_animSet;   // +0x20 added to an animation's name
    int m_frameIndex;     // +0x24 (-1 none)
};
static_assert(sizeof(CStandardAnimController) == 0x28, "layout must match the original");

// The 0x7E1940A1 controller (vtable 0x00649F68, 0x24 bytes): an animation of the object's anim set blended in
// with a weight, without a timeline of its own.
class CWeightedAnimController : public CAnimController {
public:
    // 0x004A5010: on `timeline`; the object is the channel's (or `object` without a timeline). Returns this.
    CWeightedAnimController* Construct(CAnimTimeline* timeline, Obj::CCompositeObject* object);
    // 0x004A4820
    void DestroyBody();
    // 0x004A50B0 (slot 0)
    CWeightedAnimController* Destroy(uint8_t flags);
    // 0x004A5660 (slot 1): nothing (false) without a frame or with a weight of 0 or less.
    bool GetPose(uint8_t* pose);
    // 0x004A4890 (slot 3): 0xAF2FAE19 / 0x5E3F0F17 (the animation 0xD697A799 or AnimName, the weight
    // 0xC6AE16AC), 0xC68365EB, InvalidateCache.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    void* mp_frameData;   // +0x14
    void* mp_source;      // +0x18
    uint32_t m_animSet;   // +0x1C
    float m_weight;       // +0x20
};
static_assert(sizeof(CWeightedAnimController) == 0x24, "layout must match the original");

// The wobble controller (type "wobble", vtable 0x00649F80, 0x38 bytes): plays the timeline's animation around a
// target time, swinging by two sines of the game clock (SetWobbleDetails).
class CWobbleController : public CAnimController {
public:
    // 0x004A49F0
    CWobbleController* Construct(CAnimTimeline* timeline);
    // 0x004A4A80
    void DestroyBody();
    // 0x004A50E0 (slot 0)
    CWobbleController* Destroy(uint8_t flags);
    // 0x004A5110 (slot 1)
    bool GetPose(uint8_t* pose);
    // 0x004A4AF0 (slot 3): PlayAnim, 0xD0209498 (the target as a fraction 0xD747FA0 of the timeline, 0..1),
    // SetWobbleDetails (all five or nothing), InvalidateCache.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x004A52E0 (slot 5)
    void Update();
    // 0x004A5180: the time to show now (ST0).
    float WobbleTime();

    float m_ampA;         // +0x14 wobbleAmpA
    float m_ampB;         // +0x18 wobbleAmpB
    float m_k1;           // +0x1C wobbleK1 (cycles per ms of the first sine)
    float m_k2;           // +0x20 wobbleK2
    float m_spazFactor;   // +0x24 spazFactor
    float m_target;       // +0x28 the time it wobbles around
    void* mp_source;      // +0x2C
    void* mp_frameData;   // +0x30
    uint32_t m_animSet;   // +0x34
};
static_assert(sizeof(CWobbleController) == 0x38, "layout must match the original");

// The pose controller (type "pose", vtable 0x00649F98, 0x854 bytes): a fixed pose (identity rotations).
class CPoseController : public CAnimController {
public:
    // 0x004A5730
    CPoseController* Construct(CAnimTimeline* timeline);
    // 0x004A4D20
    void DestroyBody();
    // 0x004A5320 (slot 0)
    CPoseController* Destroy(uint8_t flags);
    // 0x004A4D30 (slot 1): its pose copied out.
    bool GetPose(uint8_t* pose);

    uint8_t m_pose[0x840]; // +0x14 (64 rotations, then the rest)
};
static_assert(sizeof(CPoseController) == 0x854, "layout must match the original");

// The partial animation controller (type "partialAnim", vtable 0x00649FB0, 0x24 bytes): a second animation on a
// timeline of its own.
class CPartialAnimController : public CAnimController {
public:
    // 0x004A5350
    CPartialAnimController* Construct(CAnimTimeline* timeline);
    // 0x004A4D50
    void DestroyBody();
    // 0x004A5400 (slot 0)
    CPartialAnimController* Destroy(uint8_t flags);
    // 0x004A5430 (slot 1)
    bool GetPose(uint8_t* pose);
    // 0x004A54A0 (slot 3): 0x6AAEB76F (a step of 0x06C9F278 frames), 0xBD4EDD44 (Speed), GetPartialAnimParams,
    // 0xF5E2B871 (reverses), InvalidateCache.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x004A4DD0 (slot 4): the base's, then AnimName's times, loop type, Speed and Current on its timeline.
    void InitFromStructure(Script::CStruct* params);
    // 0x004A4EF0 (slot 5)
    void Update();

    CAnimTimeline* mp_partial; // +0x14 its own timeline
    void* mp_source;           // +0x18
    void* mp_frameData;        // +0x1C
    uint32_t m_animSet;        // +0x20
};
static_assert(sizeof(CPartialAnimController) == 0x24, "layout must match the original");

} // namespace Gfx
