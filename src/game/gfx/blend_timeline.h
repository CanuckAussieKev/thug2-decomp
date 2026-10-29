#pragma once

// A blend channel's timeline (0x74 bytes, vtable 0x00648794; Gfx::CBlendChannel keeps them on a list, the newest
// first): an animation timeline whose controllers (up to six) fill in its pose, blended out by weight once a newer
// one starts, with the animation's events. The controllers' mp_timeline points at one of these.
//
// Vtable slots: 0 the scalar deleting destructor (0x00482340), 1 Reset, 2 Update (0x00481AC0), 3 the timeline's
// PlayAnim, 4 (0x005BAB90), 5 CallMemberFunction (0x00482200), 6 PlayAnim with its controllers (0x004823C0).

#include <cstddef>
#include <cstdint>

#include "game/gfx/anim_controller.h"

namespace Obj {
class CCompositeObject;
}

namespace Gfx {

struct CAnimEvent;

struct CBlendTimeline : CAnimTimeline {
    enum State : int { kDone = 0, kBlendingOut = 1, kPlaying = 2 };

    // 0x00481830: the timeline's constructor, then no controllers and no events, `object`, events on, playing.
    // Returns this.
    CBlendTimeline* Construct(Obj::CCompositeObject* object);
    // 0x00481960 (slot 1): the timeline's Reset, then done with no weight.
    void Reset();
    // 0x00481870: each controller deleted (its slot 0 with 1), none left.
    void DeleteControllers();
    // 0x004818B0: `controller` (if it's one of them) deleted and the rest moved down.
    void RemoveController(CAnimController* controller);
    // 0x00481920: each controller's pose (slot 1) into `pose`. True.
    bool GetPoses(void* pose);
    // 0x00481980 / 0x00481990: whether it isn't done / is blending out.
    bool IsActive();
    bool IsBlendingOut();
    // 0x004819A0: each event armed again (0x00486150 with true).
    void ArmEvents();
    // 0x004819D0
    Obj::CCompositeObject* GetObject();

    uint32_t m_unknown34;
    int m_state;                          // +0x38
    CAnimController* mp_controllers[6];   // +0x3C
    int m_controllerCount;                // +0x54
    float m_blendRate;                    // +0x58 (the blend-out time used up per frame)
    float m_blendLeft;                    // +0x5C
    float m_weightScale;                  // +0x60 (the weight is this times m_blendLeft while blending out)
    Obj::CCompositeObject* mp_object;     // +0x64
    uint8_t* mp_events;                   // +0x68 (0x14 bytes each)
    uint32_t m_eventCount;                // +0x6C
    bool m_eventsOn;                      // +0x70
    uint8_t m_pad71[3];
};
static_assert(offsetof(CBlendTimeline, m_state) == 0x38 && offsetof(CBlendTimeline, m_controllerCount) == 0x54 &&
                  offsetof(CBlendTimeline, mp_object) == 0x64 && offsetof(CBlendTimeline, m_eventsOn) == 0x70 &&
                  sizeof(CBlendTimeline) == 0x74,
              "layout must match the original");

} // namespace Gfx
