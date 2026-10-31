// The animation timeline (Gfx::CAnimTimeline, anim_controller.h), the blend channel's timeline (blend_timeline.h),
// the base animation controller's members and the base animation event's destructor body
// (0x00481380-0x004819E6, but 0x004817D0). From the disassembly.

#include <cstdint>

#include "game/gfx/anim_track.h"
#include "game/gfx/custom_anim_key.h"

#include "game/core/alloc.h"
#include "game/core/mem.h"
#include "game/obj/object.h"
#include "game/obj/skeleton_component.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/core/code_address.h"
#include "game/core/game_vtables.h"
#include "game/skater/vehicle_sound.h"
#include "game/core/stubs.h"

namespace Gfx {

namespace {

extern void** const kTimelineVtable;
void** const& kListNodeVtable = Vt::kAnimListNode; // (the channel list's node type)
void** const& kBlendTimelineVtable = Vt::kAnimTrack;
extern void** const kControllerVtable;
void** const& kEventVtable = Vt::kAnimEventBase;
constexpr uint32_t kNoAnim = 0xF60C9090;     // Unassigned
constexpr uint32_t kId = 0x40C698AF;         // id
constexpr uint32_t kSkeleton = 0x222756D5;   // skeleton
constexpr float kFramesPerSecond = 60.0f;    // 0x00645EA0
constexpr float kSecondsPerFrame = 1.0f / 60.0f; // 0x006463B8

template <typename T>
void** Vtable(T address)
{
    return reinterpret_cast<void**>(address);
}

Mem::ListNode* Node(CAnimTimeline* timeline) { return reinterpret_cast<Mem::ListNode*>(timeline); }

// The timeline's empty state (Reset and the constructor).
void Clear(CAnimTimeline* t)
{
    t->m_start = 0.0f;
    t->m_current = 0.0f;
    t->m_end = 0.0f;
    t->m_speed = 0.0f;
    t->m_direction = 0;
    t->m_loopType = CAnimTimeline::kCycle;
    t->m_complete = false;
    t->m_anim = kNoAnim;
}

// A controller's scalar deleting destructor (slot 0) and pose (slot 1).
void DeleteController(CAnimController* controller)
{
    reinterpret_cast<void(__thiscall*)(CAnimController*, int)>(controller->m_vtable[0])(controller, 1);
}

} // namespace

// --- the timeline ---

// 0x00481650
CAnimTimeline* CAnimTimeline::Construct()
{
    Mem::ListNode* node = Node(this);
    node->data = this;
    node->priority = 0;
    node->next = node;
    node->prev = node;
    m_vtable = kTimelineVtable;
    Clear(this);
    return this;
}

// 0x00481690
void CAnimTimeline::DestroyBody()
{
    Node(this)->UnlinkAs(kListNodeVtable);
}

// 0x004816E0
CAnimTimeline* CAnimTimeline::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00481380
void CAnimTimeline::Reset()
{
    Clear(this);
}

// 0x00481640
void CAnimTimeline::Update()
{
    m_current = static_cast<float>(NextTime());
}

// 0x00481430 (start: the smaller of the two - `to` when they're equal or NaN; end: the other)
void CAnimTimeline::PlayAnim(uint32_t anim, float from, float to, int loopType, float /*blendPeriod*/, float speed)
{
    bool forwards = from < to;
    m_start = forwards ? from : to;
    m_current = from;
    m_end = forwards ? to : from;
    m_speed = speed * kSecondsPerFrame;
    m_direction = forwards ? 0 : -1;
    m_loopType = loopType;
    m_complete = false;
    m_anim = anim;
}

// 0x004813B0
void CAnimTimeline::GetTimes(float* start, float* current, float* end)
{
    *current = m_current;
    if (m_direction == 0) {
        *start = m_start;
        *end = m_end;
    } else {
        *start = m_end;
        *end = m_start;
    }
}

// 0x004813F0
bool CAnimTimeline::IsComplete()
{
    if (m_loopType != kOnce)
        return false;
    return m_direction == 0 ? m_current == m_end : m_current == m_start;
}

// 0x004814B0 (x87: the step and the wrapped time unrounded; the stepped time itself kept as a float)
double CAnimTimeline::NextTime()
{
    float stepped = 0.0f; // the time a frame on
    float bound = 0.0f;   // the end it reaches
    double time = 0.0;
    m_complete = false;
    if (m_loopType == kHold)
        return 0.0;
    if (m_direction == 0) {
        double next = Tmr::GetSlomoFrameSeconds() * kFramesPerSecond * m_speed + m_current;
        stepped = static_cast<float>(next);
        m_complete = next > m_end;
        if (m_start == m_end) {
            bound = m_end;
            time = m_end;
        } else {
            // (wrapped round the end, as many times as it takes - forever on a degenerate range, as the original)
            time = static_cast<double>(stepped) - m_end + m_start;
            while (time > m_end)
                time = time - m_end + m_start;
            bound = m_end;
        }
    } else if (m_direction == -1) {
        double next = m_current - Tmr::GetSlomoFrameSeconds() * kFramesPerSecond * m_speed;
        stepped = static_cast<float>(next);
        m_complete = next < m_start;
        if (m_start == m_end) {
            time = m_end;
        } else {
            time = static_cast<double>(stepped) - m_start + m_end;
            while (time < m_start)
                time = time - m_start + m_end;
        }
        bound = m_start;
    }
    if (m_complete && m_loopType == kOnce) {
        time = bound;
    } else if (m_complete && m_loopType == kPingPong) {
        time = bound;
        m_direction = m_direction != 0 ? 0 : -1;
    } else if (!m_complete || m_loopType != kCycle) {
        time = stepped;
    }
    if (time < m_start)
        time = m_start;
    if (time > m_end)
        time = m_end;
    return time;
}

// 0x004816B0
void CAnimTimeline::AddTime(float frames)
{
    float speed = m_speed;
    m_speed = 0.0f;
    m_current = static_cast<float>(static_cast<double>(frames) + m_current);
    m_current = static_cast<float>(NextTime());
    m_speed = speed;
}

// --- the blend timeline ---

// 0x00481830
CAnimTrack* CAnimTrack::Construct(Obj::CCompositeObject* object)
{
    CAnimTimeline::Construct();
    m_controllerCount = 0;
    mp_events = nullptr;
    m_eventCount = 0;
    mp_object = object;
    m_vtable = kBlendTimelineVtable;
    m_eventsOn = true;
    m_state = kPlaying;
    return this;
}

// 0x00481960
void CAnimTrack::Reset()
{
    CAnimTimeline::Reset();
    m_state = kOff;
    m_fadeLeft = 0.0f;
    m_fadeScale = 0.0f;
}

// 0x00481870
void CAnimTrack::DeleteControllers()
{
    for (int i = 0; i < m_controllerCount; ++i) {
        if (mp_controllers[i])
            DeleteController(mp_controllers[i]);
        mp_controllers[i] = nullptr;
    }
    m_controllerCount = 0;
}

// 0x004818B0
void CAnimTrack::RemoveController(CAnimController* controller)
{
    int i = 0;
    for (;; ++i) {
        if (i >= m_controllerCount)
            return;
        if (mp_controllers[i] == controller)
            break;
    }
    if (mp_controllers[i])
        DeleteController(mp_controllers[i]);
    mp_controllers[i] = nullptr;
    for (; i < m_controllerCount - 1; ++i)
        mp_controllers[i] = mp_controllers[i + 1];
    --m_controllerCount;
}

// 0x00481920
bool CAnimTrack::GetPoses(void* pose)
{
    for (int i = 0; i < m_controllerCount; ++i) {
        CAnimController* controller = mp_controllers[i];
        reinterpret_cast<bool(__thiscall*)(CAnimController*, void*)>(controller->m_vtable[1])(controller, pose);
    }
    return true;
}

// 0x00481980
bool CAnimTrack::IsActive()
{
    return m_state != kOff;
}

// 0x00481990
bool CAnimTrack::IsFading()
{
    return m_state == kFading;
}

// 0x004819A0
void CAnimTrack::ArmEvents()
{
    for (uint32_t i = 0; i < m_eventCount; ++i)
        reinterpret_cast<CCustomAnimKey*>(&mp_events[i])->SetActive(true);
}

// 0x004819D0
Obj::CCompositeObject* CAnimTrack::GetObject()
{
    return mp_object;
}

// --- the base controller ---

// 0x00481710
CAnimController* CAnimController::Construct(CAnimTimeline* timeline)
{
    mp_timeline = timeline;
    m_vtable = kControllerVtable;
    m_id = 0;
    mp_object = nullptr;
    m_priority = 0;
    return this;
}

// 0x00481730
void CAnimController::DestroyBody()
{
    m_vtable = kControllerVtable;
}

// 0x004817A0
CAnimController* CAnimController::Destroy(uint8_t flags)
{
    m_vtable = kControllerVtable;
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00481740
bool CAnimController::InitFromStructure(Script::CStruct* params)
{
    return params->GetChecksum(kId, &m_id, 0);
}

// 0x00481760
Obj::CCompositeObject* CAnimController::GetObject()
{
    return static_cast<CAnimTrack*>(mp_timeline)->GetObject();
}

// 0x00481770
uint8_t* CAnimController::GetSkeleton()
{
    Obj::CBaseComponent* component = static_cast<CAnimTrack*>(mp_timeline)->GetObject()->GetComponent(kSkeleton);
    return component ? static_cast<Obj::CSkeletonComponent*>(component)->mp_skeleton : nullptr;
}

// --- the base event ---

// 0x004819E0
void CAnimEventBase::DestroyBody()
{
    m_vtable = kEventVtable;
}

namespace {
static VtableSlot kTimelineVtable_slots[] = {&Gfx::CAnimTimeline::Destroy, &Gfx::CAnimTimeline::Reset,
                &Gfx::CAnimTimeline::Update, &Gfx::CAnimTimeline::PlayAnim, Stubs::IgnoreArgument};
void** const kTimelineVtable = reinterpret_cast<void**>(kTimelineVtable_slots);
static VtableSlot kControllerVtable_slots[] = {&Gfx::CAnimController::Destroy, Stubs::ReturnFalse1,
                Stubs::IgnoreArgument, &Obj::ComponentWithoutMemberFunctions::CallMemberFunction,
                &Gfx::CAnimController::InitFromStructure, Stubs::DoNothing};
void** const kControllerVtable = reinterpret_cast<void**>(kControllerVtable_slots);
} // namespace

} // namespace Gfx
