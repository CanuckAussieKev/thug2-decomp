#pragma once

// An animation track: one animation playing on an object's blend channel (the channel keeps a list of them - the
// newest plays, the ones it replaced fade out). A timeline (Gfx::CAnimTimeline) with the controllers that fill in
// the pose from it (up to six, by priority), the animation's events (from the anim set's event table) and a
// fading weight.
//
// Vtable 0x00648794: 0 Destroy (0x00482340), 1 Reset (0x00481960), 2 Update (0x00481AC0), 3 the timeline's
// 0x00481430, 4 0x005BAB90 (nothing), 5 CallMemberFunction (0x00482200), 6 PlayAnim (0x004823C0).
// (Its members 0x00481830-0x004819D0 are in anim_timeline.cpp.)

#include <cstddef>
#include <cstdint>

#include "game/gfx/anim_controller.h"

namespace Mem {
struct PoolPair;
}
namespace Obj {
class CCompositeObject;
}
namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Gfx {

// An animation event (0x14 bytes; vtable 0x006487BC, its base's 0x006487B0): at a frame of the animation, an event
// type with parameters, sent to the object once the timeline passes it while armed. 0x00481A20 constructs one;
// 0x004865D0 sets it up (frame, type, a copy of the parameters), 0x00486150 arms / disarms it, 0x00486160 fires it
// (if armed: disarmed, then its slot 2 with the object), 0x00486560 is its destructor body; slot 1 (0x004860C0)
// says whether it lies between two times.
class CAnimEvent {
public:
    // 0x00481A20: the event's vtable, no parameters. Returns this.
    CAnimEvent* Construct();
    // 0x00481DB0 (slot 0): the vector deleting destructor - `flags` 2: the whole array this starts (its count in
    // the word before it) destroyed last to first, 1: freed.
    CAnimEvent* Destroy(uint8_t flags);

    void** m_vtable;
    int m_frame;              // +0x04
    bool m_armed;             // +0x08
    uint8_t m_pad09[3];
    uint32_t m_type;          // +0x0C
    Script::CStruct* mp_params; // +0x10
};
static_assert(sizeof(CAnimEvent) == 0x14 && offsetof(CAnimEvent, mp_params) == 0x10, "layout must match the original");

// The events' base (vtable 0x006487B0: slot 2 pure).
class CAnimEventBase {
public:
    // 0x004819F0 (slot 0): the base's vtable; freed with `flags` 1. Returns this.
    CAnimEventBase* Destroy(uint8_t flags);
    // 0x004819E0: the base's vtable back (its destructor body; CCustomAnimKey's and CAnimEvent's call it).
    void DestroyBody();

    void** m_vtable;
};

class CAnimTrack : public CAnimTimeline {
public:
    enum State { kOff = 0, kFading = 1, kPlaying = 2 };

    // 0x00481EE0 / 0x00482360: the class's operator new / delete (its item pools 0x006F92C8).
    static void* operator new(size_t size);
    static void operator delete(void* item);

    // 0x00481830: the timeline's constructor, then no controllers and no events, `object`, events on, playing.
    // Returns this.
    CAnimTrack* Construct(Obj::CCompositeObject* object);
    // 0x00481960 (slot 1): the timeline's Reset, then off with no fade.
    void Reset();
    // 0x00481870: each controller deleted (its slot 0 with 1; null ones skipped), none left.
    void DeleteControllers();
    // 0x004818B0: `controller` (if it's one of them) deleted and the rest moved down.
    void RemoveController(CAnimController* controller);
    // 0x00481920: each controller's pose (slot 1) into `pose` (no null check). True.
    bool GetPoses(void* pose);
    // 0x00481980 / 0x00481990: whether it isn't off / is fading.
    bool IsActive();
    bool IsFading();
    // 0x004819A0: each event armed again (0x00486150 with true).
    void ArmEvents();
    // 0x004819D0
    Obj::CCompositeObject* GetObject();

    // 0x00481F20: its controllers and events deleted, then the timeline's destructor body (0x00481690).
    void DestroyBody();
    // 0x00482340 (slot 0)
    CAnimTrack* Destroy(uint8_t flags);
    // 0x00481AC0 (slot 2): not complete; fading, the weight drops by the frame's share of the fade rate (off at
    // nothing left, or once the fade times its scale goes below 0); playing or fading, each controller's Update.
    void Update();
    // 0x00482200 (slot 5): AddAnimController (1), RemoveAnimController (the one with `id`), 0xC68365EB (a new
    // animation and duration - Sync keeps the timeline's place in proportion); anything else to the controllers in
    // turn until one doesn't answer 2 (not its own).
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x004823C0 (slot 6): the animation played - its events, the standard (or with LoopType 3 the wobble)
    // controller, a weighted one (`params` with 0xD697A799 or flag 0x1B92121C), one for the structure 0xAEEEC12E,
    // the FlipRotate one; then each controller told 0xD0209498 (fraction 0) and PlayAnim with all of it.
    void PlayAnim(uint32_t anim, float start, float end, int loopType, float blendPeriod, float speed, bool flipped,
                  Script::CStruct* params);

    // 0x00481B40: starts fading out over `period` if playing (true); a period of 0 stops it at once.
    bool StartFade(float period);
    // 0x00481B80: its weight - 1 playing, the fade times its scale fading, 0 off (ST0, unrounded).
    double GetWeight();
    // 0x00481FB0: a controller made from `params` (its "type"; null for an unknown one) and added by its
    // "priority" (500, FlipRotate 100).
    CAnimController* AddController(Script::CStruct* params);
    // 0x00481A30: `controller` inserted before the first one of lower priority.
    void InsertController(CAnimController* controller, int priority);
    // 0x00481A80: the controller with `id`, or null.
    CAnimController* FindController(uint32_t id);
    // 0x00481C00 (not a Ghidra function): the events of `anim` from the object's anim set's event table (frame,
    // or time in seconds; event; params), armed.
    void LoadEvents(uint32_t anim);
    // 0x00481BB0: the events deleted.
    void DeleteEvents();
    // 0x00481E10 (not a Ghidra function): playing with events on, each event armed and fired at the object if it
    // lies between two times (in seconds, as frames); when the times run the wrong way for the direction (it
    // looped), the events are re-armed first and the first time becomes the timeline's start.
    void FireEvents(float from, float to);

    static Mem::PoolPair& Pools();

    uint32_t m_unknown34;
    int m_state;                          // +0x38 State
    CAnimController* mp_controllers[6];   // +0x3C (by priority, highest first)
    int m_controllerCount;                // +0x54
    float m_fadeRate;                     // +0x58 (1/60 when played)
    float m_fadeLeft;                     // +0x5C
    float m_fadeScale;                    // +0x60
    Obj::CCompositeObject* mp_object;     // +0x64
    CAnimEvent* mp_events;                // +0x68 (an array of m_eventCount)
    uint32_t m_eventCount;                // +0x6C
    bool m_eventsOn;                      // +0x70
    uint8_t m_pad71[3];
};
static_assert(offsetof(CAnimTrack, m_state) == 0x38 && offsetof(CAnimTrack, mp_controllers) == 0x3C &&
                  offsetof(CAnimTrack, m_controllerCount) == 0x54 && offsetof(CAnimTrack, mp_object) == 0x64 &&
                  offsetof(CAnimTrack, m_eventsOn) == 0x70 && sizeof(CAnimTrack) == 0x74,
              "layout must match the original");

} // namespace Gfx
